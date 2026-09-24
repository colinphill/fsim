// SPDX-License-Identifier: Apache-2.0
#include "elaboration_vhdl_configuration_hir.hpp"
#include "hierarchy_builder_internal.hpp"
#include "hierarchy_compiled_occurrence.hpp"
#include "hierarchy_sv_constant_evaluator.hpp"
#include "hierarchy_sv_generate_internal.hpp"
#include "hierarchy_sv_interface_ports_internal.hpp"
#include "hierarchy_sv_type_layout_internal.hpp"
#include "hierarchy_sv_parameters_internal.hpp"
#include "hierarchy_sv_ports_internal.hpp"
#include "lowerer_internal.hpp"
#include "vhdl_hir_type_validation.hpp"
#include "vhdl_callable_legality.hpp"
#include "fsim/frontend/systemverilog_standard_package.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <bit>
#include <cctype>
#include <charconv>
#include <functional>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;
using namespace hierarchy_sv_parameters_detail;

namespace {

    bool compiled_vhdl_name_equal(
        const std::string_view left,
        const std::string_view right)
    {
        return vhdl_configuration_detail::configuration_name_equal(
            left, right);
    }

    bool compiled_vhdl_library_equal(
        const std::string_view left,
        const std::string_view right)
    {
        const auto normalized_left = left.empty()
            ? std::string_view { "work" }
            : left;
        const auto normalized_right = right.empty()
            ? std::string_view { "work" }
            : right;
        return compiled_vhdl_name_equal(
            normalized_left, normalized_right);
    }

    std::string compiled_vhdl_occurrence_declaration_identity(
        const std::string_view identity,
        const std::string_view relative_path,
        const semantic::vhdl::DeclarationForm form)
    {
        auto result = std::string { identity };
        if (relative_path.empty()
            || (form
                    != semantic::vhdl::DeclarationForm::
                        generic_function_instance
                && form
                    != semantic::vhdl::DeclarationForm::
                        generic_procedure_instance)) {
            return result;
        }

        constexpr auto marker = std::string_view { ";template=" };
        const auto marker_begin = result.rfind(marker);
        if (marker_begin == std::string::npos) {
            return result;
        }
        const auto template_begin = marker_begin + marker.size();
        const auto template_end = result.find(';', template_begin);
        const auto template_name = template_end == std::string::npos
            ? std::string_view { }
            : std::string_view { result }.substr(
                  template_begin, template_end - template_begin);
        if (template_name.empty()
            || template_name.find('.') != std::string_view::npos) {
            return result;
        }
        result.insert(template_begin,
            std::string { relative_path } + ".");
        return result;
    }

    std::string_view compiled_systemverilog_library(
        const semantic::sv::Unit& unit)
    {
        return unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
    }

    bool compiled_systemverilog_selectable(
        const semantic::sv::Unit& unit)
    {
        return unit.kind == semantic::sv::UnitKind::module
            && !unit.external;
    }

    std::optional<frontend::StandardRevision>
    compiled_systemverilog_standard_revision(
        const std::string_view revision) noexcept
    {
        using Revision = frontend::StandardRevision;
        if (revision == "2005") {
            return Revision::SystemVerilog2005;
        }
        if (revision == "2009") {
            return Revision::SystemVerilog2009;
        }
        if (revision == "2012") {
            return Revision::SystemVerilog2012;
        }
        if (revision == "2017") {
            return Revision::SystemVerilog2017;
        }
        if (revision == "2023") {
            return Revision::SystemVerilog2023;
        }
        return std::nullopt;
    }

    bool compiled_systemverilog_integral_constant_type(
        const std::optional<semantic::sv::TypeReference>& type) noexcept
    {
        if (!type) {
            return true;
        }
        const auto scalar = compiled_systemverilog_scalar_kind(
            type->target.spelling);
        return scalar == frontend::SystemVerilogScalarKind::None
            || scalar == frontend::SystemVerilogScalarKind::Time;
    }

    bool compiled_vhdl_generate_supported(
        const semantic::vhdl::GenerateRegion& region)
    {
        return std::ranges::all_of(
            region.nested, compiled_vhdl_generate_supported);
    }

    std::string_view compiled_vhdl_target_architecture(
        const std::string_view target)
    {
        const auto open = target.rfind('(');
        return open != std::string_view::npos && target.ends_with(')')
            ? target.substr(open + 1U, target.size() - open - 2U)
            : std::string_view { };
    }

    class CompiledHierarchyStackGuard final {
    public:
        CompiledHierarchyStackGuard(
            std::vector<std::string>& stack,
            std::string identity)
            : stack_ { stack }
        {
            stack_.push_back(std::move(identity));
        }

        ~CompiledHierarchyStackGuard()
        {
            stack_.pop_back();
        }

        CompiledHierarchyStackGuard(const CompiledHierarchyStackGuard&) = delete;
        CompiledHierarchyStackGuard& operator=(
            const CompiledHierarchyStackGuard&) = delete;

    private:
        std::vector<std::string>& stack_;
    };

    template <typename Container>
    class CompiledHierarchyContainerGuard final {
    public:
        explicit CompiledHierarchyContainerGuard(Container& container)
            : container_ { container }
            , size_ { container.size() }
        {
        }

        ~CompiledHierarchyContainerGuard()
        {
            container_.resize(size_);
        }

        CompiledHierarchyContainerGuard(
            const CompiledHierarchyContainerGuard&) = delete;
        CompiledHierarchyContainerGuard& operator=(
            const CompiledHierarchyContainerGuard&) = delete;

        [[nodiscard]] std::size_t size() const noexcept
        {
            return size_;
        }

    private:
        Container& container_;
        std::size_t size_ { };
    };

    const std::string* compiled_physical_source(
        const semantic::CompiledDesign& compiled,
        const semantic::SourceSpanId source)
    {
        const auto& spans = compiled.semantics.source_spans();
        if (!source.valid() || source.value() >= spans.size()) {
            return nullptr;
        }
        const auto file = spans[source.value()].file;
        const auto& files = compiled.semantics.source_files();
        if (!file.valid() || file.value() >= files.size()) {
            return nullptr;
        }
        return &files[file.value()].physical_name;
    }

    frontend::SourceSpan compiled_source_span(
        const semantic::CompiledDesign& compiled,
        const semantic::SourceSpanId source)
    {
        frontend::SourceSpan result;
        const auto& spans = compiled.semantics.source_spans();
        if (!source.valid() || source.value() >= spans.size()) {
            return result;
        }
        const auto& span = spans[source.value()];
        result.source_name = span.logical_name;
        result.begin = {
            static_cast<std::size_t>(span.begin.offset),
            span.begin.line,
            span.begin.column,
        };
        result.end = {
            static_cast<std::size_t>(span.end.offset),
            span.end.line,
            span.end.column,
        };
        if (const auto* physical = compiled_physical_source(
                compiled, source)) {
            result.physical_source_name = *physical;
        }
        return result;
    }

    frontend::PortDirection compiled_port_direction(
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

    frontend::PortDirection compiled_port_direction(
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

    frontend::ValueDomain compiled_value_domain(
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

    struct CompiledContainerBridgeShape {
        enum class Kind {
            leaf,
            array,
            aggregate,
        };

        Kind kind { Kind::leaf };
        std::uint64_t width { };
        bool two_state { };
        std::uint64_t count { 1U };
        std::vector<CompiledContainerBridgeShape> children;

        friend bool operator==(
            const CompiledContainerBridgeShape&,
            const CompiledContainerBridgeShape&) = default;
    };

    std::optional<std::uint64_t> compiled_container_bridge_range_count(
        const std::int64_t left,
        const std::int64_t right)
    {
        const auto low = std::min(left, right);
        const auto high = std::max(left, right);
        const auto distance = static_cast<std::uint64_t>(high - low);
        if (distance == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        return distance + 1U;
    }

    CompiledContainerBridgeShape compiled_container_bridge_array_shape(
        CompiledContainerBridgeShape element,
        const std::uint64_t count)
    {
        if (count == 1U) {
            return element;
        }
        CompiledContainerBridgeShape result;
        result.kind = CompiledContainerBridgeShape::Kind::array;
        result.count = count;
        result.children.push_back(std::move(element));
        return result;
    }

    std::optional<CompiledContainerBridgeShape>
    compiled_container_bridge_element_shape(const ContainerType& type);

    std::optional<CompiledContainerBridgeShape>
    compiled_container_bridge_shape(const ContainerType& type)
    {
        auto element = compiled_container_bridge_element_shape(type);
        if (!element) {
            return std::nullopt;
        }
        if (type.aggregate_value) {
            return element;
        }
        if (!type.fixed || type.queue || type.associative
            || type.dimensions.empty()) {
            return std::nullopt;
        }
        for (auto dimension = type.dimensions.rbegin();
            dimension != type.dimensions.rend(); ++dimension) {
            const auto count = compiled_container_bridge_range_count(
                dimension->first, dimension->second);
            if (!count) {
                return std::nullopt;
            }
            *element = compiled_container_bridge_array_shape(
                std::move(*element), *count);
        }
        return element;
    }

    std::optional<CompiledContainerBridgeShape>
    compiled_container_bridge_element_shape(const ContainerType& type)
    {
        switch (type.element_kind) {
        case ContainerElementKind::Packed:
            return CompiledContainerBridgeShape {
                CompiledContainerBridgeShape::Kind::leaf,
                type.element_width,
                type.two_state,
                1U,
                { },
            };
        case ContainerElementKind::Scalar:
        case ContainerElementKind::String:
            return std::nullopt;
        case ContainerElementKind::Container:
            if (type.element_types.size() != 1U) {
                return std::nullopt;
            }
            return compiled_container_bridge_shape(
                type.element_types.front());
        case ContainerElementKind::Aggregate: {
            if (type.union_aggregate || type.element_types.empty()) {
                return std::nullopt;
            }
            CompiledContainerBridgeShape result;
            result.kind = CompiledContainerBridgeShape::Kind::aggregate;
            for (const auto& member : type.element_types) {
                auto child = compiled_container_bridge_shape(member);
                if (!child) {
                    return std::nullopt;
                }
                result.children.push_back(std::move(*child));
            }
            return result;
        }
        }
        return std::nullopt;
    }

    std::optional<CompiledContainerBridgeShape>
    compiled_container_bridge_packed_shape(
        const PackedTypeMetadata& type);

    std::optional<CompiledContainerBridgeShape>
    compiled_container_bridge_member_shape(
        const PackedMemberMetadata& member)
    {
        if (!member.nested_types.empty()) {
            if (member.nested_types.size() != 1U) {
                return std::nullopt;
            }
            return compiled_container_bridge_packed_shape(
                member.nested_types.front());
        }
        const auto width = member.width();
        if (!width || *width == 0U) {
            return std::nullopt;
        }
        return CompiledContainerBridgeShape {
            CompiledContainerBridgeShape::Kind::leaf,
            *width,
            is_two_state_domain(member.domain),
            1U,
            { },
        };
    }

    std::optional<CompiledContainerBridgeShape>
    compiled_container_bridge_packed_shape(
        const PackedTypeMetadata& type)
    {
        if (!type.packed_members.empty()) {
            CompiledContainerBridgeShape result;
            result.kind = CompiledContainerBridgeShape::Kind::aggregate;
            for (const auto& member : type.packed_members) {
                auto child = compiled_container_bridge_member_shape(member);
                if (!child) {
                    return std::nullopt;
                }
                result.children.push_back(std::move(*child));
            }
            return result;
        }
        if (type.vhdl_array) {
            const auto& array = *type.vhdl_array;
            if (!array.flat_width || *array.flat_width == 0U
                || array.dimensions.empty()) {
                return std::nullopt;
            }
            // Built-in one-dimensional vectors do not have a declaration
            // record from which to retain element_types.  They are the packed
            // leaf when nested inside a user-defined array.
            if (array.element_types.empty()) {
                return CompiledContainerBridgeShape {
                    CompiledContainerBridgeShape::Kind::leaf,
                    *array.flat_width,
                    is_two_state_domain(array.element_domain),
                    1U,
                    { },
                };
            }
            if (array.element_types.size() != 1U) {
                return std::nullopt;
            }
            const auto& element_type = array.element_types.front();
            const auto scalar_element = !element_type.vhdl_array
                && element_type.packed_members.empty()
                && element_type.width().value_or(0U) == 1U;
            if (scalar_element) {
                return CompiledContainerBridgeShape {
                    CompiledContainerBridgeShape::Kind::leaf,
                    *array.flat_width,
                    is_two_state_domain(array.element_domain),
                    1U,
                    { },
                };
            }
            auto element = compiled_container_bridge_packed_shape(
                element_type);
            if (!element) {
                return std::nullopt;
            }
            for (auto dimension = array.dimensions.rbegin();
                dimension != array.dimensions.rend(); ++dimension) {
                if (!dimension->range || dimension->null) {
                    return std::nullopt;
                }
                const auto count = compiled_container_bridge_range_count(
                    dimension->range->left,
                    dimension->range->right);
                if (!count) {
                    return std::nullopt;
                }
                *element = compiled_container_bridge_array_shape(
                    std::move(*element), *count);
            }
            return element;
        }
        const auto width = type.width();
        if (!width || *width == 0U) {
            return std::nullopt;
        }
        return CompiledContainerBridgeShape {
            CompiledContainerBridgeShape::Kind::leaf,
            *width,
            is_two_state_domain(type.domain),
            1U,
            { },
        };
    }

    std::optional<CompiledContainerBridgeShape>
    compiled_container_bridge_signal_shape(const SignalInfo& signal)
    {
        PackedTypeMetadata type;
        type.domain = signal.source_domain;
        type.spelling = signal.type_name;
        type.systemverilog_scalar = signal.systemverilog_scalar;
        type.is_signed = signal.is_signed;
        type.packed_range = signal.packed_range;
        type.vhdl_array = signal.vhdl_array;
        type.packed_members = signal.packed_members;
        type.nominal_type = signal.nominal_type;
        return compiled_container_bridge_packed_shape(type);
    }

    frontend::Language compiled_frontend_language(
        const semantic::Language language) noexcept
    {
        switch (language) {
        case semantic::Language::verilog:
            return frontend::Language::Verilog2005;
        case semantic::Language::system_verilog:
            return frontend::Language::SystemVerilog2017;
        case semantic::Language::vhdl:
            return frontend::Language::Vhdl2008;
        case semantic::Language::systemc:
            break;
        }
        return frontend::Language::SystemVerilog2017;
    }

    std::optional<std::string> compiled_systemverilog_literal_bits(
        const std::string_view spelling)
    {
        if (spelling.starts_with("svconst-v3:")) {
            const auto marker = spelling.rfind(":v=");
            if (marker == std::string_view::npos) {
                return std::nullopt;
            }
            auto bits = std::string { spelling.substr(marker + 3U) };
            if (bits.empty()
                || std::ranges::any_of(bits, [](const char bit) {
                       return bit != '0' && bit != '1'
                           && bit != 'x' && bit != 'X'
                           && bit != 'z' && bit != 'Z';
                   })) {
                return std::nullopt;
            }
            std::ranges::transform(bits, bits.begin(), [](const char bit) {
                return static_cast<char>(std::tolower(
                    static_cast<unsigned char>(bit)));
            });
            return bits;
        }

        std::string normalized;
        normalized.reserve(spelling.size());
        for (const auto byte : spelling) {
            if (byte != '_' && !std::isspace(static_cast<unsigned char>(byte))) {
                normalized.push_back(byte);
            }
        }
        const auto quote = normalized.find('\'');
        if (quote == std::string::npos || quote == 0U) {
            return std::nullopt;
        }
        std::size_t width { };
        const auto parsed_width = std::from_chars(
            normalized.data(), normalized.data() + quote, width);
        if (parsed_width.ec != std::errc { }
            || parsed_width.ptr != normalized.data() + quote
            || width == 0U || width > 1'048'576U) {
            return std::nullopt;
        }
        auto cursor = quote + 1U;
        if (cursor < normalized.size()
            && (normalized[cursor] == 's'
                || normalized[cursor] == 'S')) {
            ++cursor;
        }
        if (cursor >= normalized.size()) {
            return std::nullopt;
        }
        const auto base = static_cast<char>(std::tolower(
            static_cast<unsigned char>(normalized[cursor++])));
        if (cursor >= normalized.size()) {
            return std::nullopt;
        }

        std::string bits;
        const auto append_digit = [&](const char digit,
                                      const unsigned digit_width) {
            const auto normalized_digit = static_cast<char>(std::tolower(
                static_cast<unsigned char>(digit)));
            if (normalized_digit == 'x' || normalized_digit == 'z') {
                bits.append(digit_width, normalized_digit);
                return true;
            }
            unsigned value { };
            if (digit >= '0' && digit <= '9') {
                value = static_cast<unsigned>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                value = static_cast<unsigned>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                value = static_cast<unsigned>(digit - 'A' + 10);
            } else {
                return false;
            }
            if (value >= (1U << digit_width)) {
                return false;
            }
            for (auto bit = digit_width; bit > 0U; --bit) {
                bits.push_back(
                    (value & (1U << (bit - 1U))) != 0U ? '1' : '0');
            }
            return true;
        };
        if (base == 'b' || base == 'o' || base == 'h') {
            const auto digit_width = base == 'b' ? 1U : base == 'o' ? 3U
                                                                    : 4U;
            for (; cursor < normalized.size(); ++cursor) {
                if (!append_digit(normalized[cursor], digit_width)) {
                    return std::nullopt;
                }
            }
        } else if (base == 'd') {
            std::string decimal { normalized.substr(cursor) };
            if (decimal.size() == 1U
                && (decimal.front() == 'x' || decimal.front() == 'X'
                    || decimal.front() == 'z' || decimal.front() == 'Z')) {
                bits.assign(width, static_cast<char>(std::tolower(static_cast<unsigned char>(decimal.front()))));
                return bits;
            }
            if (decimal.empty()
                || std::ranges::any_of(decimal, [](const char digit) {
                       return digit < '0' || digit > '9';
                   })) {
                return std::nullopt;
            }
            std::string reversed;
            while (decimal != "0") {
                std::string quotient;
                unsigned carry { };
                for (const auto digit : decimal) {
                    const auto value = carry * 10U
                        + static_cast<unsigned>(digit - '0');
                    const auto next = value / 2U;
                    carry = value % 2U;
                    if (!quotient.empty() || next != 0U) {
                        quotient.push_back(
                            static_cast<char>('0' + next));
                    }
                }
                reversed.push_back(carry != 0U ? '1' : '0');
                decimal = quotient.empty() ? "0" : std::move(quotient);
                if (reversed.size() > width) {
                    break;
                }
            }
            bits.assign(reversed.rbegin(), reversed.rend());
            if (bits.empty()) {
                bits = "0";
            }
        } else {
            return std::nullopt;
        }
        if (bits.size() < width) {
            bits.insert(bits.begin(), width - bits.size(), '0');
        } else if (bits.size() > width) {
            bits.erase(0U, bits.size() - width);
        }
        return bits;
    }

    std::optional<std::string> compiled_systemverilog_identity_display(
        const std::string_view identity)
    {
        if (identity.starts_with("svscalar-v1:")) {
            const auto decoded
                = decode_hir_systemverilog_scalar_constant(identity);
            return decoded
                ? std::optional { decoded->display() }
                : std::nullopt;
        }
        if (identity.starts_with("svconst-v3:")) {
            const auto decoded = decode_hir_systemverilog_constant(identity);
            return decoded
                ? std::optional { decoded->display() }
                : std::nullopt;
        }
        const auto bits = compiled_systemverilog_literal_bits(identity);
        if (!bits) {
            return std::nullopt;
        }
        const auto signed_value = identity.find(":s=1:")
            != std::string_view::npos;
        const auto unknown = std::ranges::any_of(
            *bits, [](const char bit) { return bit == 'x' || bit == 'z'; });
        const auto packed = [&] {
            return std::to_string(bits->size())
                + (signed_value ? "'sb" : "'b") + *bits;
        };
        if (unknown) {
            return packed();
        }
        if (signed_value && bits->front() == '1') {
            const auto prefix = bits->size() > 64U
                ? std::string_view { *bits }.substr(0U, bits->size() - 64U)
                : std::string_view { };
            if (!std::ranges::all_of(
                    prefix, [](const char bit) { return bit == '1'; })) {
                return packed();
            }
            const auto low = std::string_view { *bits }.substr(
                bits->size() > 64U ? bits->size() - 64U : 0U);
            std::uint64_t raw { };
            for (const auto bit : low) {
                raw = (raw << 1U) | (bit == '1' ? 1U : 0U);
            }
            if (low.size() < 64U) {
                raw |= ~std::uint64_t { } << low.size();
            }
            if (raw == (std::uint64_t { 1U } << 63U)) {
                return std::to_string(std::numeric_limits<std::int64_t>::min());
            }
            const auto magnitude = (~raw) + 1U;
            if (magnitude
                <= static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                return "-" + std::to_string(magnitude);
            }
            return packed();
        }
        const auto first = bits->find('1');
        if (first == std::string::npos) {
            return std::string { "0" };
        }
        const auto significant = std::string_view { *bits }.substr(first);
        if (significant.size() > 64U
            || (signed_value && significant.size() == 64U)) {
            return packed();
        }
        std::uint64_t value { };
        for (const auto bit : significant) {
            value = (value << 1U) | (bit == '1' ? 1U : 0U);
        }
        return std::to_string(value);
    }

    std::optional<frontend::PackedRange> compiled_packed_range(
        const semantic::vhdl::SubtypeIndication& subtype)
    {
        const auto separator = subtype.type_mark.spelling.find_last_of('.');
        const auto simple_type = std::string_view { subtype.type_mark.spelling }
                                     .substr(separator == std::string::npos ? 0U : separator + 1U);
        if (subtype.domain == semantic::vhdl::ValueDomain::integer
            || compiled_vhdl_name_equal(simple_type, "boolean")
            || subtype.constraints.size() != 1U
            || !subtype.constraints.front().left
            || !subtype.constraints.front().right) {
            return std::nullopt;
        }
        const auto& constraint = subtype.constraints.front();
        return frontend::PackedRange {
            *constraint.left,
            *constraint.right,
            constraint.descending,
        };
    }

    bool compiled_vhdl_integer_constraint(
        const semantic::vhdl::RangeConstraint& constraint)
    {
        return (constraint.kind == semantic::vhdl::RangeKind::integer
                   || constraint.kind
                       == semantic::vhdl::RangeKind::discrete)
            && constraint.left && constraint.right;
    }

    std::optional<frontend::IntegerRange> compiled_integer_range(
        const semantic::vhdl::SubtypeIndication& subtype)
    {
        if (subtype.domain != semantic::vhdl::ValueDomain::integer) {
            return std::nullopt;
        }
        const auto constraint = std::ranges::find_if(
            subtype.constraints, compiled_vhdl_integer_constraint);
        if (constraint == subtype.constraints.end()) {
            return std::nullopt;
        }
        return frontend::IntegerRange {
            *constraint->left,
            *constraint->right,
            constraint->descending,
        };
    }

    std::optional<frontend::IntegerRange> executable_integer_range(
        const frontend::ValueDomain domain,
        const std::size_t width,
        const std::optional<frontend::IntegerRange>& explicit_range)
    {
        if (domain != frontend::ValueDomain::Integer) {
            return std::nullopt;
        }
        if (explicit_range) {
            return explicit_range;
        }
        if (width == 0U || width > 64U) {
            return std::nullopt;
        }
        if (width == 64U) {
            return frontend::IntegerRange {
                std::numeric_limits<std::int64_t>::min(),
                std::numeric_limits<std::int64_t>::max(),
                false,
            };
        }
        const auto magnitude = std::uint64_t { 1 } << (width - 1U);
        return frontend::IntegerRange {
            -static_cast<std::int64_t>(magnitude),
            static_cast<std::int64_t>(magnitude - 1U),
            false,
        };
    }

    bool integer_range_contains(
        const frontend::IntegerRange& container,
        const frontend::IntegerRange& candidate) noexcept
    {
        const auto container_lower = std::min(
            container.left, container.right);
        const auto container_upper = std::max(
            container.left, container.right);
        const auto candidate_lower = std::min(
            candidate.left, candidate.right);
        const auto candidate_upper = std::max(
            candidate.left, candidate.right);
        return candidate_lower >= container_lower
            && candidate_upper <= container_upper;
    }

    bool integer_alias_range_safe(
        const frontend::IntegerRange& formal,
        const frontend::IntegerRange& actual,
        const frontend::PortDirection direction) noexcept
    {
        const bool actual_to_formal
            = integer_range_contains(formal, actual);
        const bool formal_to_actual
            = integer_range_contains(actual, formal);
        switch (direction) {
        case frontend::PortDirection::Input:
            return actual_to_formal;
        case frontend::PortDirection::Output:
        case frontend::PortDirection::Buffer:
            return formal_to_actual;
        case frontend::PortDirection::Inout:
        case frontend::PortDirection::Ref:
            return actual_to_formal && formal_to_actual;
        case frontend::PortDirection::Unknown:
            return false;
        }
        return false;
    }

    std::optional<std::size_t> compiled_vhdl_signal_width(
        const semantic::vhdl::SubtypeIndication& subtype)
    {
        auto width = subtype.executable_width;
        if ((!width || *width == 0U)
            && subtype.domain == semantic::vhdl::ValueDomain::integer
            && subtype.integer_storage_width != 0U) {
            width = subtype.integer_storage_width;
        }
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(*width);
    }

    bool compiled_vhdl_scalar_domain(
        frontend::ValueDomain domain) noexcept;

    struct CompiledVhdlSignalLayout {
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    };

    const semantic::Scope* compiled_semantic_scope(
        const semantic::CompiledDesign& design,
        const semantic::ScopeId id)
    {
        const auto scope = std::ranges::find(
            design.semantics.scopes(), id, &semantic::Scope::id);
        return scope == design.semantics.scopes().end()
            ? nullptr
            : &*scope;
    }

    std::string_view compiled_vhdl_simple_name(
        const std::string_view spelling)
    {
        const auto separator = spelling.find_last_of(".:");
        return spelling.substr(separator == std::string_view::npos
                ? 0U
                : separator + 1U);
    }

    std::string compiled_vhdl_shape_name(const std::string_view input)
    {
        std::string result;
        result.reserve(input.size());
        constexpr auto hexadecimal = std::string_view { "0123456789abcdef" };
        for (const char value : input) {
            const auto byte = static_cast<unsigned char>(value);
            const auto normalized = static_cast<unsigned char>(
                std::tolower(byte));
            if ((normalized >= 'a' && normalized <= 'z')
                || (normalized >= '0' && normalized <= '9')
                || normalized == '_' || normalized == '.') {
                result.push_back(static_cast<char>(normalized));
                continue;
            }
            result.push_back('%');
            result.push_back(hexadecimal[normalized >> 4U]);
            result.push_back(hexadecimal[normalized & 0x0fU]);
        }
        return result;
    }

    void append_compiled_vhdl_shape_range(
        std::string& identity,
        const std::string_view prefix,
        const frontend::IntegerRange& range)
    {
        identity += ";";
        identity += prefix;
        identity += "-left=" + std::to_string(range.left);
        identity += ";";
        identity += prefix;
        identity += "-right=" + std::to_string(range.right);
        identity += ";";
        identity += prefix;
        identity += range.descending ? "-direction=downto"
                                     : "-direction=to";
    }

    void append_compiled_vhdl_shape_type(
        std::string& identity,
        const PackedTypeMetadata& type,
        const std::size_t depth)
    {
        if (depth > 64U) {
            identity += ";recursive=1";
            return;
        }
        const auto type_name = !type.named_type.empty()
            ? std::string_view { type.named_type }
            : std::string_view { type.spelling };
        identity += ";type=" + compiled_vhdl_shape_name(type_name);
        identity += ";domain="
            + std::to_string(static_cast<unsigned>(type.domain));
        identity += type.is_signed ? ";signed=1" : ";signed=0";
        if (type.packed_range) {
            append_compiled_vhdl_shape_range(
                identity, "packed", frontend::IntegerRange {
                    type.packed_range->left,
                    type.packed_range->right,
                    type.packed_range->descending,
                });
        }
        if (type.integer_range) {
            append_compiled_vhdl_shape_range(
                identity, "integer", *type.integer_range);
        }
        if (type.enumeration_range) {
            append_compiled_vhdl_shape_range(
                identity, "enumeration", frontend::IntegerRange {
                    type.enumeration_range->left,
                    type.enumeration_range->right,
                    type.enumeration_range->descending,
                });
        }
        for (const auto& literal : type.enumeration_literals) {
            identity += ";literal="
                + compiled_vhdl_shape_name(literal);
        }
        if (type.vhdl_array) {
            const auto& array = *type.vhdl_array;
            identity += array.unconstrained
                ? ";array-unconstrained=1"
                : ";array-unconstrained=0";
            if (array.flat_width) {
                identity += ";array-width="
                    + std::to_string(*array.flat_width);
            }
            for (std::size_t index { };
                index < array.dimensions.size(); ++index) {
                const auto& dimension = array.dimensions[index];
                identity += ";dimension=" + std::to_string(index);
                identity += ";index-type="
                    + compiled_vhdl_shape_name(
                        dimension.index_subtype);
                identity += dimension.unconstrained
                    ? ";index-unconstrained=1"
                    : ";index-unconstrained=0";
                identity += dimension.null
                    ? ";index-null=1" : ";index-null=0";
                identity += ";stride="
                    + std::to_string(dimension.stride);
                if (dimension.range) {
                    append_compiled_vhdl_shape_range(
                        identity, "index", *dimension.range);
                }
            }
            for (const auto& element : array.element_types) {
                identity += ";element={";
                append_compiled_vhdl_shape_type(
                    identity, element, depth + 1U);
                identity += ";}";
            }
        }
        for (const auto& member : type.packed_members) {
            identity += ";member="
                + compiled_vhdl_shape_name(member.name);
            identity += ";member-offset="
                + std::to_string(member.lsb_offset);
            for (const auto& nested : member.nested_types) {
                identity += ";member-type={";
                append_compiled_vhdl_shape_type(
                    identity, nested, depth + 1U);
                identity += ";}";
            }
        }
    }

    std::string compiled_vhdl_port_shape_identity(
        const SignalInfo& signal)
    {
        PackedTypeMetadata type;
        type.domain = signal.source_domain;
        type.spelling = signal.type_name;
        type.packed_range = signal.packed_range;
        type.is_signed = signal.is_signed;
        type.named_type = signal.type_name;
        type.integer_range = signal.integer_range;
        type.nominal_type = signal.nominal_type;
        type.enumeration_literals = signal.enumeration_literals;
        type.enumeration_range = signal.enumeration_range;
        type.vhdl_array = signal.vhdl_array;
        type.vhdl_access = signal.vhdl_access;
        type.vhdl_physical = signal.vhdl_physical;
        type.packed_members = signal.packed_members;
        std::string identity { "vhdl-array-shape-v2" };
        append_compiled_vhdl_shape_type(identity, type, 0U);
        return identity;
    }

    std::vector<std::string_view> compiled_vhdl_name_parts(
        const std::string_view spelling)
    {
        std::vector<std::string_view> result;
        std::size_t begin { };
        while (begin < spelling.size()) {
            const auto end = spelling.find('.', begin);
            const auto part = spelling.substr(begin,
                end == std::string_view::npos
                    ? std::string_view::npos
                    : end - begin);
            if (!part.empty()) {
                result.push_back(part);
            }
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 1U;
        }
        return result;
    }

    std::string_view compiled_vhdl_effective_library(
        const std::string_view spelling,
        const std::string_view owner_library)
    {
        if (compiled_vhdl_name_equal(spelling, "work")) {
            return owner_library.empty()
                ? std::string_view { "work" }
                : owner_library;
        }
        return spelling.empty()
            ? std::string_view { "work" }
            : spelling;
    }

    const semantic::vhdl::Unit* compiled_vhdl_unit(
        const semantic::CompiledDesign& design,
        const semantic::UnitId id)
    {
        const auto unit = design.find_unit(id);
        return unit && unit->vhdl != nullptr
            ? unit->vhdl
            : nullptr;
    }

    semantic::vhdl::SubtypeIndication compiled_vhdl_link_subtype(
        const semantic::SpecializedHirUnit& specialization,
        semantic::vhdl::SubtypeIndication subtype,
        const std::optional<semantic::ScopeId> owner_scope = std::nullopt)
    {
        return semantic::CompiledDesignResolver { specialization }
            .effective_vhdl_subtype(
                subtype, owner_scope.value_or(semantic::ScopeId { }))
            .value_or(std::move(subtype));
    }

    std::optional<semantic::vhdl::Name> compiled_vhdl_resolution_function(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& root,
        const std::optional<semantic::ScopeId> owner_scope = std::nullopt)
    {
        std::unordered_set<std::uint32_t> visiting;
        const auto resolve = [&](const auto& self,
                                 const semantic::vhdl::SubtypeIndication& input)
            -> std::optional<semantic::vhdl::Name> {
            const auto subtype = compiled_vhdl_link_subtype(
                specialization, input, owner_scope);
            if (!subtype.resolution_function.canonical.empty()
                || !subtype.resolution_function.spelling.empty()) {
                return subtype.resolution_function;
            }
            if (!subtype.type_mark.target.valid()
                || !visiting.insert(
                                subtype.type_mark.target.value())
                    .second) {
                return std::nullopt;
            }
            const auto type = specialization.find_type(
                subtype.type_mark.target);
            if (!type || type->vhdl == nullptr) {
                return std::nullopt;
            }
            const auto& definition = *type->vhdl;
            if (definition.form == semantic::vhdl::TypeForm::array
                && definition.element_subtype) {
                return self(self, *definition.element_subtype);
            }
            if (definition.form == semantic::vhdl::TypeForm::subtype
                || definition.form == semantic::vhdl::TypeForm::alias
                || definition.form == semantic::vhdl::TypeForm::scalar) {
                return self(self, definition.base);
            }
            return std::nullopt;
        };
        return resolve(resolve, root);
    }

    std::optional<CompiledVhdlSignalLayout>
    compiled_vhdl_named_signal_layout(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& root,
        const std::optional<semantic::ScopeId> owner_scope = std::nullopt)
    {
        std::unordered_set<std::uint32_t> visiting;
        std::unordered_set<std::uint32_t> element_visiting;
        const auto simple_name = [](const std::string_view spelling) {
            const auto separator = spelling.find_last_of(".:");
            return spelling.substr(separator == std::string_view::npos
                    ? 0U
                    : separator + 1U);
        };
        const auto builtin_scalar_layout = [&](const std::string_view spelling)
            -> std::optional<CompiledVhdlSignalLayout> {
            const auto name = simple_name(spelling);
            if (compiled_vhdl_name_equal(name, "bit")) {
                return CompiledVhdlSignalLayout {
                    1U, frontend::ValueDomain::Bit2
                };
            }
            if (compiled_vhdl_name_equal(name, "std_logic")
                || compiled_vhdl_name_equal(name, "std_ulogic")) {
                return CompiledVhdlSignalLayout {
                    1U, frontend::ValueDomain::Logic9
                };
            }
            if (compiled_vhdl_name_equal(name, "boolean")) {
                return CompiledVhdlSignalLayout {
                    1U, frontend::ValueDomain::Boolean
                };
            }
            if (compiled_vhdl_name_equal(name, "integer")
                || compiled_vhdl_name_equal(name, "natural")
                || compiled_vhdl_name_equal(name, "positive")
                || compiled_vhdl_name_equal(name, "universal_integer")) {
                return CompiledVhdlSignalLayout {
                    32U, frontend::ValueDomain::Integer
                };
            }
            if (compiled_vhdl_name_equal(name, "character")) {
                return CompiledVhdlSignalLayout {
                    8U, frontend::ValueDomain::Bit2
                };
            }
            return std::nullopt;
        };
        const auto builtin_element_layout = [&](const std::string_view spelling)
            -> std::optional<CompiledVhdlSignalLayout> {
            const auto name = simple_name(spelling);
            if (compiled_vhdl_name_equal(name, "bit_vector")) {
                return CompiledVhdlSignalLayout {
                    1U, frontend::ValueDomain::Bit2
                };
            }
            if (compiled_vhdl_name_equal(name, "std_logic_vector")
                || compiled_vhdl_name_equal(name, "std_ulogic_vector")
                || compiled_vhdl_name_equal(name, "signed")
                || compiled_vhdl_name_equal(name, "unsigned")) {
                return CompiledVhdlSignalLayout {
                    1U, frontend::ValueDomain::Logic9
                };
            }
            if (compiled_vhdl_name_equal(name, "string")) {
                return CompiledVhdlSignalLayout {
                    8U, frontend::ValueDomain::Bit2
                };
            }
            if (compiled_vhdl_name_equal(name, "boolean_vector")) {
                return CompiledVhdlSignalLayout {
                    1U, frontend::ValueDomain::Boolean
                };
            }
            return std::nullopt;
        };
        const auto boundary = [&](const std::optional<std::int64_t> value,
                                  const std::optional<semantic::ExpressionId>
                                      expression) {
            return expression
                ? specialization.evaluate_integral_expression(*expression)
                : value;
        };
        const auto range_count = [&](
                                     const semantic::vhdl::RangeConstraint& range)
            -> std::optional<std::size_t> {
            const auto left = boundary(range.left, range.left_expression);
            const auto right = boundary(range.right, range.right_expression);
            if (!left || !right) {
                return std::nullopt;
            }
            if (range.null
                || (range.descending && *left < *right)
                || (!range.descending && *left > *right)) {
                return 0U;
            }
            const auto distance = index_distance(*left, *right);
            if (distance == std::numeric_limits<std::uint64_t>::max()
                || distance + 1U
                    > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(distance + 1U);
        };
        const auto apply_count = [](CompiledVhdlSignalLayout layout,
                                     const std::size_t count)
            -> std::optional<CompiledVhdlSignalLayout> {
            if (layout.width == 0U || count == 0U) {
                layout.width = 0U;
                return layout;
            }
            if (layout.width
                > std::numeric_limits<std::size_t>::max() / count) {
                return std::nullopt;
            }
            layout.width *= count;
            return layout;
        };
        const auto merge_domain = [](const frontend::ValueDomain left,
                                      const frontend::ValueDomain right)
            -> std::optional<frontend::ValueDomain> {
            if (left == frontend::ValueDomain::Unknown) {
                return right;
            }
            if (right == frontend::ValueDomain::Unknown || left == right) {
                return left;
            }
            const auto logic_rank = [](const frontend::ValueDomain domain) {
                switch (domain) {
                case frontend::ValueDomain::Bit2:
                case frontend::ValueDomain::Boolean:
                case frontend::ValueDomain::Integer:
                    return 1;
                case frontend::ValueDomain::Logic4:
                    return 2;
                case frontend::ValueDomain::Logic9:
                    return 3;
                default:
                    return 0;
                }
            };
            const auto left_rank = logic_rank(left);
            const auto right_rank = logic_rank(right);
            if (left_rank != 0 && right_rank != 0) {
                const auto rank = std::max(left_rank, right_rank);
                return rank == 3
                    ? frontend::ValueDomain::Logic9
                    : rank == 2
                    ? frontend::ValueDomain::Logic4
                    : frontend::ValueDomain::Bit2;
            }
            return std::nullopt;
        };

        using Layout = std::optional<CompiledVhdlSignalLayout>;
        std::function<Layout(const semantic::vhdl::SubtypeIndication&)>
            resolve_subtype;
        std::function<Layout(semantic::DeclarationId)> resolve_declaration;
        std::function<Layout(semantic::ExpressionId)> resolve_expression;
        std::function<Layout(const semantic::vhdl::SubtypeIndication&)>
            resolve_element_subtype;

        const auto actual_for_generic_name = [&](const std::string_view name)
            -> const semantic::SpecializedHirActualIdentity* {
            const auto& actuals
                = specialization.specialization().actual_identities;
            if (name.empty()) {
                return nullptr;
            }
            const auto generic_name = simple_name(name);
            for (const auto& actual : actuals) {
                const auto declaration = specialization.find_declaration(
                    actual.declaration);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->form
                        == semantic::vhdl::DeclarationForm::generic_type
                    && compiled_vhdl_name_equal(
                        declaration->vhdl->name, generic_name)) {
                    return &actual;
                }
            }
            return nullptr;
        };
        const auto actual_for_type = [&](const semantic::TypeId type)
            -> const semantic::SpecializedHirActualIdentity* {
            const auto& actuals
                = specialization.specialization().actual_identities;
            for (const auto& actual : actuals) {
                const auto declaration = specialization.find_declaration(
                    actual.declaration);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->declared_type
                    && *declaration->vhdl->declared_type == type) {
                    return &actual;
                }
            }
            const auto mirrored_generic = std::ranges::find_if(
                specialization.design().vhdl_hir.declarations(),
                [&](const semantic::vhdl::Declaration& declaration) {
                    return declaration.form
                        == semantic::vhdl::DeclarationForm::generic_type
                        && declaration.declared_type
                        && *declaration.declared_type == type;
                });
            if (mirrored_generic
                == specialization.design().vhdl_hir.declarations().end()) {
                return nullptr;
            }
            return actual_for_generic_name(mirrored_generic->name);
        };

        const auto expression_name = [](const semantic::vhdl::Expression& record)
            -> std::string_view {
            if (record.referenced_name) {
                if (!record.referenced_name->canonical.empty()) {
                    return record.referenced_name->canonical;
                }
                if (!record.referenced_name->spelling.empty()) {
                    return record.referenced_name->spelling;
                }
            }
            return record.text;
        };

        resolve_element_subtype = [&](const semantic::vhdl::SubtypeIndication& subtype)
            -> Layout {
            const auto* generic_actual = subtype.type_mark.target.valid()
                ? actual_for_type(subtype.type_mark.target)
                : nullptr;
            if (generic_actual == nullptr) {
                generic_actual = actual_for_generic_name(
                    subtype.type_mark.spelling);
            }
            if (generic_actual != nullptr) {
                Layout result;
                if (generic_actual->vhdl_type
                    && generic_actual->vhdl_type->type_mark.target
                        != subtype.type_mark.target) {
                    result = resolve_element_subtype(
                        *generic_actual->vhdl_type);
                }
                if (!result && generic_actual->actual_expression) {
                    result = resolve_expression(
                        *generic_actual->actual_expression);
                }
                if (!result && generic_actual->actual_declaration) {
                    const auto declaration = specialization.find_declaration(
                        *generic_actual->actual_declaration);
                    if (declaration && declaration->vhdl != nullptr) {
                        if (declaration->vhdl->subtype) {
                            result = resolve_element_subtype(
                                *declaration->vhdl->subtype);
                        } else if (declaration->vhdl->declared_type) {
                            semantic::vhdl::SubtypeIndication actual_subtype;
                            actual_subtype.type_mark.target
                                = *declaration->vhdl->declared_type;
                            result = resolve_element_subtype(actual_subtype);
                        }
                    }
                }
                if (result) {
                    return result;
                }
            }
            if (!subtype.type_mark.target.valid()) {
                auto linked = compiled_vhdl_link_subtype(
                    specialization, subtype, owner_scope);
                if (linked.type_mark.target.valid()) {
                    return resolve_element_subtype(linked);
                }
            }
            if (!subtype.type_mark.target.valid()) {
                return builtin_element_layout(subtype.type_mark.spelling);
            }
            if (!element_visiting.insert(
                                     subtype.type_mark.target.value())
                    .second) {
                return std::nullopt;
            }
            const auto type = specialization.find_type(subtype.type_mark.target);
            Layout result;
            if (type && type->vhdl != nullptr) {
                const auto& definition = *type->vhdl;
                const auto& actuals
                    = specialization.specialization().actual_identities;
                const auto actual = std::ranges::find(actuals,
                    definition.declaration,
                    &semantic::SpecializedHirActualIdentity::declaration);
                if (actual != actuals.end() && actual->actual_expression) {
                    const auto expression = specialization.find_expression(
                        *actual->actual_expression);
                    if (expression && expression->vhdl != nullptr) {
                        const auto& record = *expression->vhdl;
                        if (record.kind == semantic::vhdl::ExpressionKind::slice
                            && !record.operands.empty()) {
                            const auto base = specialization.find_expression(
                                record.operands.front());
                            if (base && base->vhdl != nullptr) {
                                result = builtin_element_layout(
                                    expression_name(*base->vhdl));
                            }
                        } else {
                            result = builtin_element_layout(
                                expression_name(record));
                        }
                    }
                }
                if (!result
                    && definition.form == semantic::vhdl::TypeForm::array
                    && definition.element_subtype) {
                    result = resolve_subtype(*definition.element_subtype);
                } else if (!result
                    && (definition.form == semantic::vhdl::TypeForm::subtype
                        || definition.form
                            == semantic::vhdl::TypeForm::alias)) {
                    result = resolve_element_subtype(definition.base);
                }
            }
            element_visiting.erase(subtype.type_mark.target.value());
            return result;
        };

        resolve_declaration = [&](const semantic::DeclarationId id) -> Layout {
            const auto declaration = specialization.find_declaration(id);
            if (!declaration || declaration->vhdl == nullptr) {
                return std::nullopt;
            }
            if (declaration->vhdl->subtype) {
                return resolve_subtype(*declaration->vhdl->subtype);
            }
            if (!declaration->vhdl->declared_type) {
                return std::nullopt;
            }
            semantic::vhdl::SubtypeIndication subtype;
            subtype.type_mark.target = *declaration->vhdl->declared_type;
            return resolve_subtype(subtype);
        };

        const auto expression_element = [&](const semantic::vhdl::Expression& record)
            -> Layout {
            if (record.referenced_name && record.referenced_name->selected) {
                const auto declaration = specialization.find_declaration(
                    *record.referenced_name->selected);
                if (declaration && declaration->vhdl != nullptr) {
                    if (declaration->vhdl->subtype) {
                        if (const auto layout = resolve_element_subtype(
                                *declaration->vhdl->subtype)) {
                            return layout;
                        }
                    }
                    if (declaration->vhdl->declared_type) {
                        semantic::vhdl::SubtypeIndication subtype;
                        subtype.type_mark.target
                            = *declaration->vhdl->declared_type;
                        if (const auto layout = resolve_element_subtype(subtype)) {
                            return layout;
                        }
                    }
                }
            }
            return builtin_element_layout(expression_name(record));
        };

        resolve_expression = [&](const semantic::ExpressionId id) -> Layout {
            const auto expression = specialization.find_expression(id);
            if (!expression || expression->vhdl == nullptr) {
                return std::nullopt;
            }
            const auto& record = *expression->vhdl;
            if (record.kind == semantic::vhdl::ExpressionKind::name) {
                if (record.referenced_name
                    && record.referenced_name->selected) {
                    if (const auto layout = resolve_declaration(
                            *record.referenced_name->selected)) {
                        return layout;
                    }
                }
                return builtin_scalar_layout(expression_name(record));
            }

            Layout element;
            std::size_t count { 1U };
            if (record.kind == semantic::vhdl::ExpressionKind::slice
                && record.operands.size() == 3U) {
                const auto base = specialization.find_expression(
                    record.operands.front());
                if (!base || base->vhdl == nullptr) {
                    return std::nullopt;
                }
                element = expression_element(*base->vhdl);
                const auto left = specialization.evaluate_integral_expression(
                    record.operands[1]);
                const auto right = specialization.evaluate_integral_expression(
                    record.operands[2]);
                if (!left || !right) {
                    return std::nullopt;
                }
                const auto distance = index_distance(*left, *right);
                if (distance == std::numeric_limits<std::uint64_t>::max()
                    || distance + 1U
                        > std::numeric_limits<std::size_t>::max()) {
                    return std::nullopt;
                }
                count = static_cast<std::size_t>(distance + 1U);
            } else if (record.kind == semantic::vhdl::ExpressionKind::call
                && !record.operands.empty()) {
                element = expression_element(record);
                for (const auto operand : record.operands) {
                    const auto constraint = specialization.find_expression(
                        operand);
                    if (!constraint || constraint->vhdl == nullptr
                        || constraint->vhdl->kind
                            != semantic::vhdl::ExpressionKind::binary
                        || constraint->vhdl->operands.size() != 2U) {
                        return std::nullopt;
                    }
                    const auto left
                        = specialization.evaluate_integral_expression(
                            constraint->vhdl->operands[0]);
                    const auto right
                        = specialization.evaluate_integral_expression(
                            constraint->vhdl->operands[1]);
                    if (!left || !right) {
                        return std::nullopt;
                    }
                    const auto distance = index_distance(*left, *right);
                    if (distance == std::numeric_limits<std::uint64_t>::max()
                        || distance + 1U
                            > std::numeric_limits<std::size_t>::max() / count) {
                        return std::nullopt;
                    }
                    count *= static_cast<std::size_t>(distance + 1U);
                }
            } else {
                return std::nullopt;
            }
            return element ? apply_count(*element, count) : std::nullopt;
        };

        resolve_subtype = [&](const semantic::vhdl::SubtypeIndication& subtype)
            -> Layout {
            const auto* generic_actual = subtype.type_mark.target.valid()
                ? actual_for_type(subtype.type_mark.target)
                : nullptr;
            if (generic_actual == nullptr) {
                generic_actual = actual_for_generic_name(
                    subtype.type_mark.spelling);
            }
            if (generic_actual != nullptr) {
                Layout result;
                if (generic_actual->vhdl_type
                    && generic_actual->vhdl_type->type_mark.target
                        != subtype.type_mark.target) {
                    auto specialized_type = *generic_actual->vhdl_type;
                    if (!subtype.constraints.empty()) {
                        specialized_type.constraints = subtype.constraints;
                        specialized_type.unconstrained = false;
                        specialized_type.executable_width.reset();
                    }
                    result = resolve_subtype(specialized_type);
                }
                if (!result && generic_actual->actual_expression) {
                    result = resolve_expression(
                        *generic_actual->actual_expression);
                }
                if (!result && generic_actual->actual_declaration) {
                    result = resolve_declaration(
                        *generic_actual->actual_declaration);
                }
                if (result) {
                    return result;
                }
            }
            if (!subtype.type_mark.target.valid()) {
                auto linked = compiled_vhdl_link_subtype(
                    specialization, subtype, owner_scope);
                if (linked.type_mark.target.valid()) {
                    return resolve_subtype(linked);
                }
            }
            const auto domain = compiled_value_domain(subtype.domain);
            const bool has_residual_constraint = std::ranges::any_of(
                subtype.constraints,
                [](const semantic::vhdl::RangeConstraint& constraint) {
                    return constraint.left_expression
                        || constraint.right_expression;
                });
            if (!has_residual_constraint) {
                if (const auto width = compiled_vhdl_signal_width(subtype);
                    width && compiled_vhdl_scalar_domain(domain)) {
                    return CompiledVhdlSignalLayout { *width, domain };
                }
            }
            if (!subtype.type_mark.target.valid()) {
                if (const auto scalar = builtin_scalar_layout(
                        subtype.type_mark.spelling)) {
                    return scalar;
                }
                auto element = builtin_element_layout(
                    subtype.type_mark.spelling);
                if (!element || subtype.constraints.empty()) {
                    return std::nullopt;
                }
                for (const auto& constraint : subtype.constraints) {
                    const auto count = range_count(constraint);
                    if (!count) {
                        return std::nullopt;
                    }
                    element = apply_count(*element, *count);
                    if (!element) {
                        return std::nullopt;
                    }
                }
                return element;
            }
            if (!visiting.insert(
                             subtype.type_mark.target.value())
                    .second) {
                return std::nullopt;
            }
            const auto type = specialization.find_type(
                subtype.type_mark.target);
            if (!type || type->vhdl == nullptr) {
                visiting.erase(subtype.type_mark.target.value());
                return std::nullopt;
            }
            const auto& definition = *type->vhdl;
            Layout result;
            const auto& actuals
                = specialization.specialization().actual_identities;
            const auto actual = std::ranges::find(actuals,
                definition.declaration,
                &semantic::SpecializedHirActualIdentity::declaration);
            if (actual != actuals.end()) {
                if (actual->vhdl_type
                    && actual->vhdl_type->type_mark.target
                        != subtype.type_mark.target) {
                    auto specialized_type = *actual->vhdl_type;
                    if (!subtype.constraints.empty()) {
                        specialized_type.constraints = subtype.constraints;
                        specialized_type.unconstrained = false;
                        specialized_type.executable_width.reset();
                    }
                    result = resolve_subtype(specialized_type);
                }
                if (!result && actual->actual_expression) {
                    result = resolve_expression(*actual->actual_expression);
                }
                if (!result && actual->actual_declaration) {
                    result = resolve_declaration(*actual->actual_declaration);
                }
            }
            if (!result
                && definition.form == semantic::vhdl::TypeForm::record) {
                std::size_t width { };
                auto record_domain = frontend::ValueDomain::Unknown;
                bool valid = !definition.record_elements.empty();
                for (const auto& element : definition.record_elements) {
                    const auto layout = resolve_subtype(element.subtype);
                    const auto merged = layout
                        ? merge_domain(record_domain, layout->domain)
                        : std::nullopt;
                    if (!layout || !merged || layout->width == 0U
                        || layout->width
                            > std::numeric_limits<std::size_t>::max() - width) {
                        valid = false;
                        break;
                    }
                    width += layout->width;
                    record_domain = *merged;
                }
                if (valid && compiled_vhdl_scalar_domain(record_domain)) {
                    result = CompiledVhdlSignalLayout {
                        width, record_domain
                    };
                }
            } else if (!result
                && definition.form == semantic::vhdl::TypeForm::array
                && definition.element_subtype
                && !definition.array_dimensions.empty()) {
                auto layout = resolve_subtype(*definition.element_subtype);
                for (std::size_t index { };
                    layout && index < definition.array_dimensions.size();
                    ++index) {
                    const semantic::vhdl::RangeConstraint* constraint = nullptr;
                    if (index < subtype.constraints.size()) {
                        constraint = &subtype.constraints[index];
                    } else if (definition.array_dimensions[index].constraint) {
                        constraint
                            = &*definition.array_dimensions[index].constraint;
                    }
                    const auto count = constraint != nullptr
                        ? range_count(*constraint)
                        : std::nullopt;
                    layout = count
                        ? apply_count(*layout, *count)
                        : std::nullopt;
                }
                result = std::move(layout);
            } else if (!result
                && (definition.form == semantic::vhdl::TypeForm::subtype
                    || definition.form == semantic::vhdl::TypeForm::alias
                    || definition.form == semantic::vhdl::TypeForm::scalar)) {
                auto base = definition.base;
                if (!subtype.constraints.empty()) {
                    base.constraints = subtype.constraints;
                    base.unconstrained = false;
                } else if (definition.scalar_range) {
                    base.constraints = { *definition.scalar_range };
                    base.unconstrained = false;
                }
                if (subtype.executable_width) {
                    base.executable_width = subtype.executable_width;
                }
                result = resolve_subtype(base);
            } else if (!result
                && definition.form == semantic::vhdl::TypeForm::access) {
                result = CompiledVhdlSignalLayout {
                    32U, frontend::ValueDomain::Bit2
                };
            } else if (!result
                && definition.form == semantic::vhdl::TypeForm::physical) {
                const auto declaration = specialization.find_declaration(
                    definition.declaration);
                const auto physical_scope
                    = declaration && declaration->vhdl != nullptr
                    ? std::ranges::find(
                          specialization.design().semantics.scopes(),
                          declaration->vhdl->scope,
                          &semantic::Scope::id)
                    : specialization.design().semantics.scopes().end();
                const auto owner_unit = physical_scope
                        != specialization.design().semantics.scopes().end()
                    ? compiled_vhdl_unit(
                          specialization.design(), physical_scope->unit)
                    : nullptr;
                const auto storage_width = subtype.integer_storage_width != 0U
                    ? subtype.integer_storage_width
                    : definition.base.integer_storage_width != 0U
                    ? definition.base.integer_storage_width
                    : owner_unit != nullptr
                            && owner_unit->standard == "2019"
                    ? 64U
                    : 32U;
                result = CompiledVhdlSignalLayout {
                    storage_width, frontend::ValueDomain::Integer
                };
            }
            if (!result
                && definition.form == semantic::vhdl::TypeForm::enumeration
                && !definition.enumeration_literals.empty()) {
                auto maximum_ordinal
                    = definition.enumeration_literals.size() - 1U;
                if (subtype.constraints.size() == 1U) {
                    const auto& constraint = subtype.constraints.front();
                    const auto left = boundary(
                        constraint.left, constraint.left_expression);
                    const auto right = boundary(
                        constraint.right, constraint.right_expression);
                    if (left && right && *left >= 0 && *right >= 0) {
                        maximum_ordinal = static_cast<std::size_t>(
                            std::max(*left, *right));
                    }
                }
                std::size_t width { 1U };
                while (maximum_ordinal > 1U) {
                    ++width;
                    maximum_ordinal >>= 1U;
                }
                result = CompiledVhdlSignalLayout {
                    width, frontend::ValueDomain::Bit2
                };
            }
            visiting.erase(subtype.type_mark.target.value());
            return result;
        };
        return resolve_subtype(compiled_vhdl_link_subtype(
            specialization, root, owner_scope));
    }

    void insert_compiled_default(
        PackedLogic4& destination,
        const PackedLogic4& source,
        const std::size_t offset)
    {
        for (std::size_t bit { }; bit < source.width(); ++bit) {
            if (destination.is_logic9()) {
                destination.set_logic9(offset + bit, source.get_logic9(bit));
            } else {
                destination.set(
                    offset + bit, runtime::to_logic4(source.get_logic9(bit)));
            }
        }
    }

    std::optional<std::size_t> compiled_default_type_width(
        const PackedTypeMetadata& type)
    {
        if (!type.enumeration_literals.empty()) {
            auto maximum_ordinal = type.enumeration_literals.size() - 1U;
            std::size_t width { 1U };
            while (maximum_ordinal > 1U) {
                ++width;
                maximum_ordinal >>= 1U;
            }
            return width;
        }
        if (const auto direct = type.width()) {
            if (*direct <= std::numeric_limits<std::size_t>::max()) {
                return static_cast<std::size_t>(*direct);
            }
            return std::nullopt;
        }
        if (type.packed_members.empty()) {
            return std::nullopt;
        }
        std::size_t width { };
        for (const auto& member : type.packed_members) {
            std::optional<std::size_t> member_width;
            if (!member.nested_types.empty()) {
                member_width = compiled_default_type_width(
                    member.nested_types.front());
            } else if (const auto direct_member_width = member.width()) {
                if (*direct_member_width
                    > std::numeric_limits<std::size_t>::max()) {
                    return std::nullopt;
                }
                member_width = static_cast<std::size_t>(
                    *direct_member_width);
            }
            if (!member_width
                || member.lsb_offset
                    > std::numeric_limits<std::size_t>::max()
                || *member_width
                    > std::numeric_limits<std::size_t>::max()
                        - static_cast<std::size_t>(member.lsb_offset)) {
                return std::nullopt;
            }
            width = std::max(
                width,
                static_cast<std::size_t>(member.lsb_offset)
                    + *member_width);
        }
        return width == 0U
            ? std::nullopt
            : std::optional<std::size_t> { width };
    }

    PackedLogic4 default_compiled_packed_value(
        const PackedTypeMetadata& type,
        const std::size_t width)
    {
        if (type.enumeration_range) {
            return unsigned_value(
                static_cast<std::uint64_t>(type.enumeration_range->left),
                width);
        }
        auto result = PackedLogic4 {
            width,
            is_two_state_domain(type.domain) ? Logic4::zero : Logic4::x,
        };
        if (type.domain == frontend::ValueDomain::Logic9) {
            result.fill(runtime::Logic9::u);
        }
        if (type.vhdl_array && !type.vhdl_array->element_types.empty()) {
            const auto& element = type.vhdl_array->element_types.front();
            const auto element_width = compiled_default_type_width(element);
            if (element_width && *element_width != 0U) {
                const auto element_default = default_compiled_packed_value(
                    element, *element_width);
                for (std::size_t offset { };
                    offset + *element_width <= width;
                    offset += *element_width) {
                    insert_compiled_default(result, element_default, offset);
                }
            }
            return result;
        }
        if (type.domain == frontend::ValueDomain::Integer && width != 0U) {
            const auto default_value = type.integer_range
                ? type.integer_range->left
                : type.vhdl_integer_storage_width == 64U
                ? std::numeric_limits<std::int64_t>::min()
                : static_cast<std::int64_t>(
                      std::numeric_limits<std::int32_t>::min());
            return unsigned_value(
                static_cast<std::uint64_t>(default_value), width);
        }
        if (type.packed_aggregate
            == frontend::PackedAggregateKind::Union) {
            return result;
        }
        if (type.packed_aggregate
            == frontend::PackedAggregateKind::TaggedUnion) {
            std::size_t payload_width { };
            for (const auto& member : type.packed_members) {
                std::optional<std::size_t> member_width;
                if (!member.nested_types.empty()) {
                    member_width = compiled_default_type_width(
                        member.nested_types.front());
                } else if (const auto direct = member.width(); direct
                    && *direct
                        <= std::numeric_limits<std::size_t>::max()) {
                    member_width = static_cast<std::size_t>(*direct);
                }
                if (member_width) {
                    payload_width = std::max(
                        payload_width, *member_width);
                }
            }
            for (auto bit = payload_width; bit < width; ++bit) {
                result.set(bit, Logic4::zero);
            }
            return result;
        }
        for (const auto& member : type.packed_members) {
            std::optional<std::size_t> member_width;
            if (!member.nested_types.empty()) {
                member_width = compiled_default_type_width(
                    member.nested_types.front());
            } else if (const auto direct = member.width(); direct
                && *direct <= std::numeric_limits<std::size_t>::max()) {
                member_width = static_cast<std::size_t>(*direct);
            }
            if (!member_width || member.lsb_offset > width
                || *member_width > width - member.lsb_offset) {
                continue;
            }
            if (!member.nested_types.empty()) {
                insert_compiled_default(
                    result,
                    default_compiled_packed_value(
                        member.nested_types.front(),
                        static_cast<std::size_t>(*member_width)),
                    static_cast<std::size_t>(member.lsb_offset));
                continue;
            }
            for (std::uint64_t bit { }; bit < *member_width; ++bit) {
                const auto index = static_cast<std::size_t>(
                    member.lsb_offset + bit);
                if (result.is_logic9()) {
                    result.set_logic9(index,
                        member.domain == frontend::ValueDomain::Logic9
                            ? runtime::Logic9::u
                            : member.domain == frontend::ValueDomain::Logic4
                            ? runtime::Logic9::x
                            : runtime::Logic9::zero);
                } else {
                    result.set(index,
                        is_two_state_domain(member.domain)
                            ? Logic4::zero
                            : Logic4::x);
                }
            }
        }
        return result;
    }

    std::optional<PackedTypeMetadata> compiled_systemverilog_packed_type(
        const semantic::CompiledDesign& compiled,
        const semantic::SpecializedHirUnit& working_specialization,
        const semantic::sv::TypeReference& reference)
    {
        std::unordered_set<std::uint32_t> visiting;
        std::unordered_set<std::string> substituting;
        const semantic::CompiledDesignResolver type_resolver {
            working_specialization
        };
        const auto materialize = [&](const auto& self,
                                     const semantic::sv::TypeReference& candidate)
            -> std::optional<PackedTypeMetadata> {
            const auto effective
                = type_resolver.effective_systemverilog_type(
                    candidate, working_specialization.scope());
            if (effective && *effective != candidate) {
                const auto substitution_identity
                    = candidate.target.target.valid()
                    ? "id:"
                        + std::to_string(
                            candidate.target.target.value())
                    : "name:" + candidate.target.spelling;
                if (!substituting.insert(substitution_identity).second) {
                    return std::nullopt;
                }
                auto result = self(self, *effective);
                substituting.erase(substitution_identity);
                return result;
            }
            if (!candidate.target.target.valid()
                || !visiting.insert(
                                candidate.target.target.value())
                    .second) {
                return std::nullopt;
            }
            const auto type_id = candidate.target.target;
            const auto definition = working_specialization.find_type(
                type_id);
            if (!definition
                || definition->systemverilog == nullptr) {
                visiting.erase(type_id.value());
                return std::nullopt;
            }
            const auto& aggregate = *definition->systemverilog;
            if (aggregate.form
                    != semantic::sv::TypeForm::packed_structure
                && aggregate.form
                    != semantic::sv::TypeForm::packed_union
                && aggregate.form
                    != semantic::sv::TypeForm::tagged_union) {
                auto result = self(self, aggregate.base);
                visiting.erase(type_id.value());
                return result;
            }

            std::vector<std::uint64_t> member_widths;
            std::uint64_t aggregate_width { };
            for (const auto& member : aggregate.members) {
                semantic::sv::Declaration width_declaration;
                width_declaration.type = member.type;
                const auto width = hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
                    working_specialization, width_declaration);
                if (!width || *width == 0U
                    || (aggregate.form
                            == semantic::sv::TypeForm::
                                packed_structure
                        && *width
                            > std::numeric_limits<std::uint64_t>::max()
                                - aggregate_width)) {
                    visiting.erase(type_id.value());
                    return std::nullopt;
                }
                member_widths.push_back(
                    static_cast<std::uint64_t>(*width));
                if (aggregate.form
                        == semantic::sv::TypeForm::packed_union
                    || aggregate.form
                        == semantic::sv::TypeForm::tagged_union) {
                    aggregate_width = std::max(
                        aggregate_width, member_widths.back());
                } else {
                    aggregate_width += member_widths.back();
                }
            }
            if (aggregate.form
                == semantic::sv::TypeForm::tagged_union) {
                if (aggregate.members.empty()) {
                    visiting.erase(type_id.value());
                    return std::nullopt;
                }
                const auto tag_width = std::max<std::uint64_t>(
                    1U,
                    static_cast<std::uint64_t>(std::bit_width(
                        aggregate.members.size() - 1U)));
                if (tag_width
                    > std::numeric_limits<std::uint64_t>::max()
                        - aggregate_width) {
                    visiting.erase(type_id.value());
                    return std::nullopt;
                }
                aggregate_width += tag_width;
            }
            if (aggregate_width == 0U
                || aggregate_width - 1U
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                visiting.erase(type_id.value());
                return std::nullopt;
            }

            PackedTypeMetadata result;
            result.domain = candidate.four_state
                    || aggregate.base.four_state
                ? frontend::ValueDomain::Logic4
                : frontend::ValueDomain::Bit2;
            result.spelling = candidate.target.spelling.empty()
                ? aggregate.name
                : candidate.target.spelling;
            result.is_signed = candidate.signed_value
                || aggregate.base.signed_value;
            result.named_type = aggregate.name.empty()
                ? result.spelling
                : aggregate.name;
            result.named_type_span = compiled_source_span(compiled, aggregate.source);
            result.nominal_type = "sv-hir-type:"
                + std::to_string(type_id.value());
            result.packed_range = frontend::PackedRange {
                static_cast<std::int64_t>(aggregate_width - 1U),
                0,
                true,
            };
            result.packed_aggregate = aggregate.form
                    == semantic::sv::TypeForm::packed_union
                ? frontend::PackedAggregateKind::Union
                : aggregate.form
                    == semantic::sv::TypeForm::tagged_union
                ? frontend::PackedAggregateKind::TaggedUnion
                : frontend::PackedAggregateKind::Struct;
            result.packed_members.reserve(aggregate.members.size());
            auto remaining_width = aggregate_width;
            for (std::size_t index { };
                index < aggregate.members.size(); ++index) {
                const auto& member = aggregate.members[index];
                const auto member_type
                    = type_resolver.underlying_systemverilog_type(
                                       member.type, working_specialization.scope())
                          .value_or(member.type);
                PackedMemberMetadata materialized;
                materialized.name = member.name;
                materialized.domain = member.type.four_state
                        || member_type.four_state
                    ? frontend::ValueDomain::Logic4
                    : frontend::ValueDomain::Bit2;
                materialized.spelling
                    = member.type.target.spelling.empty()
                    ? member_type.target.spelling
                    : member.type.target.spelling;
                materialized.is_signed = member.type.signed_value
                    || member_type.signed_value;
                if (aggregate.form
                        == semantic::sv::TypeForm::packed_union
                    || aggregate.form
                        == semantic::sv::TypeForm::tagged_union) {
                    materialized.lsb_offset = 0U;
                } else {
                    remaining_width -= member_widths[index];
                    materialized.lsb_offset = remaining_width;
                }
                materialized.span = compiled_source_span(compiled, member.source);
                if (member_type.packed_range) {
                    const auto& range = *member_type.packed_range;
                    const auto left = range.left_expression
                        ? working_specialization
                              .evaluate_integral_expression(
                                  *range.left_expression)
                        : range.left;
                    const auto right = range.right_expression
                        ? working_specialization
                              .evaluate_integral_expression(
                                  *range.right_expression)
                        : range.right;
                    if (left && right) {
                        materialized.packed_range
                            = frontend::PackedRange {
                                  *left, *right, range.descending
                              };
                    }
                }
                if (auto nested = self(self, member.type)) {
                    materialized.nested_types.push_back(
                        std::move(*nested));
                }
                result.packed_members.push_back(
                    std::move(materialized));
            }
            visiting.erase(type_id.value());
            return result;
        };
        return materialize(materialize, reference);
    }

    std::optional<PackedLogic4> compiled_systemverilog_packed_default(
        const semantic::CompiledDesign& compiled,
        const semantic::SpecializedHirUnit& working_specialization,
        const semantic::sv::TypeReference& reference,
        const std::size_t width)
    {
        if (width == 0U
            || width > hir_systemverilog_maximum_constant_width) {
            return std::nullopt;
        }

        const semantic::CompiledDesignResolver type_resolver {
            working_specialization
        };
        const auto aggregate_definition = [&](const semantic::sv::TypeReference& candidate)
            -> const semantic::sv::TypeDefinition* {
            const auto effective
                = type_resolver.effective_systemverilog_type(
                                   candidate, working_specialization.scope())
                      .value_or(candidate);
            if (!effective.target.target.valid()) {
                return nullptr;
            }
            const auto definition = working_specialization.find_type(
                effective.target.target);
            if (!definition || definition->systemverilog == nullptr) {
                return nullptr;
            }
            const auto& type = *definition->systemverilog;
            using TypeForm = semantic::sv::TypeForm;
            return type.form == TypeForm::packed_structure
                    || type.form == TypeForm::packed_union
                    || type.form == TypeForm::tagged_union
                ? &type
                : nullptr;
        };
        const auto member_width = [&](const semantic::sv::TypeReference& member)
            -> std::optional<std::size_t> {
            semantic::sv::Declaration declaration;
            declaration.type = member;
            return hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
                working_specialization, declaration);
        };
        const auto member_layout = [&](const semantic::sv::TypeDefinition& type,
                                       const std::size_t aggregate_width)
            -> std::optional<std::vector<
                std::pair<std::size_t, std::size_t>>> {
            std::vector<std::pair<std::size_t, std::size_t>> result;
            result.reserve(type.members.size());
            std::size_t remaining = aggregate_width;
            if (type.form == semantic::sv::TypeForm::tagged_union) {
                if (type.members.empty()) {
                    return std::nullopt;
                }
                const auto tag_width = std::max<std::size_t>(
                    1U,
                    static_cast<std::size_t>(std::bit_width(
                        type.members.size() - 1U)));
                if (tag_width > remaining) {
                    return std::nullopt;
                }
                remaining -= tag_width;
            }
            for (const auto& member : type.members) {
                const auto width_value = member_width(member.type);
                if (!width_value || *width_value == 0U) {
                    return std::nullopt;
                }
                if (type.form
                        == semantic::sv::TypeForm::packed_union
                    || type.form
                        == semantic::sv::TypeForm::tagged_union) {
                    if (*width_value > remaining) {
                        return std::nullopt;
                    }
                    result.emplace_back(0U, *width_value);
                    continue;
                }
                if (*width_value > remaining) {
                    return std::nullopt;
                }
                remaining -= *width_value;
                result.emplace_back(remaining, *width_value);
            }
            return result;
        };

        std::unordered_set<std::uint32_t> active_types;
        std::function<std::optional<PackedLogic4>(
            semantic::ExpressionId,
            const semantic::sv::TypeReference&,
            std::size_t)>
            expression_value;
        std::function<std::optional<PackedLogic4>(
            const semantic::sv::TypeReference&,
            std::size_t)>
            type_default;

        const auto converted_constant = [&](const semantic::ExpressionId expression,
                                            const semantic::sv::TypeReference& expected,
                                            const std::size_t expected_width)
            -> std::optional<PackedLogic4> {
            std::string error;
            auto constant = evaluate_hir_systemverilog_constant(
                working_specialization, expression, error);
            if (!constant) {
                return std::nullopt;
            }
            constant = convert_hir_systemverilog_constant(
                std::move(*constant), expected, error,
                &working_specialization);
            if (!constant
                || constant->packed.width() != expected_width) {
                return std::nullopt;
            }
            return std::move(constant->packed);
        };

        expression_value = [&](const semantic::ExpressionId expression_id,
                               const semantic::sv::TypeReference& expected,
                               const std::size_t expected_width)
            -> std::optional<PackedLogic4> {
            const auto expression = working_specialization.find_expression(
                expression_id);
            if (!expression || expression->systemverilog == nullptr) {
                return std::nullopt;
            }
            const auto& source = *expression->systemverilog;
            const auto* aggregate = aggregate_definition(expected);
            constexpr auto tagged_prefix
                = std::string_view { "@sv-tagged:" };
            if (source.kind == semantic::sv::ExpressionKind::call
                && source.text.starts_with(tagged_prefix)) {
                if (aggregate == nullptr
                    || aggregate->form
                        != semantic::sv::TypeForm::tagged_union
                    || aggregate->members.empty()
                    || source.operands.size() != 1U) {
                    return std::nullopt;
                }
                const auto member_name
                    = std::string_view { source.text }.substr(
                        tagged_prefix.size());
                const auto member = std::ranges::find(
                    aggregate->members, member_name,
                    &semantic::sv::PackedMember::name);
                if (member == aggregate->members.end()) {
                    return std::nullopt;
                }
                const auto layout = member_layout(
                    *aggregate, expected_width);
                if (!layout) {
                    return std::nullopt;
                }
                const auto index = static_cast<std::size_t>(std::distance(
                    aggregate->members.begin(), member));
                const auto [offset, member_width_value] = (*layout)[index];
                const auto value = expression_value(
                    source.operands.front(), member->type,
                    member_width_value);
                const auto tag_width = std::max<std::size_t>(
                    1U,
                    static_cast<std::size_t>(std::bit_width(
                        aggregate->members.size() - 1U)));
                if (!value || tag_width > expected_width) {
                    return std::nullopt;
                }
                auto result = PackedLogic4 {
                    expected_width, Logic4::zero
                };
                insert_compiled_default(result, *value, offset);
                insert_compiled_default(
                    result,
                    unsigned_value(index, tag_width),
                    expected_width - tag_width);
                return result;
            }
            if (source.kind
                    != semantic::sv::ExpressionKind::assignment_pattern
                || source.text != "sv-pattern"
                || aggregate == nullptr
                || (aggregate->form
                        != semantic::sv::TypeForm::packed_structure
                    && aggregate->form
                        != semantic::sv::TypeForm::packed_union)
                || source.associations.empty()) {
                return converted_constant(
                    expression_id, expected, expected_width);
            }

            const auto layout = member_layout(*aggregate, expected_width);
            if (!layout) {
                return std::nullopt;
            }
            auto result = PackedLogic4 {
                expected_width,
                expected.four_state ? Logic4::x : Logic4::zero,
            };
            std::vector<bool> selected(aggregate->members.size());
            std::size_t positional { };
            std::optional<semantic::ExpressionId> default_value;
            const auto write_member = [&](const std::size_t index,
                                          const semantic::ExpressionId value_id) {
                if (index >= aggregate->members.size()) {
                    return false;
                }
                const auto& member = aggregate->members[index];
                const auto [offset, width_value] = (*layout)[index];
                const auto value = expression_value(
                    value_id, member.type, width_value);
                if (!value) {
                    return false;
                }
                insert_compiled_default(result, *value, offset);
                return true;
            };
            for (const auto& association : source.associations) {
                std::optional<std::size_t> index;
                if (association.choice_spelling == "default") {
                    if (default_value) {
                        return std::nullopt;
                    }
                    default_value = association.value;
                    continue;
                }
                if (association.choice_spelling == "@key") {
                    if (association.choices.size() != 1U) {
                        return std::nullopt;
                    }
                    const auto key = working_specialization.find_expression(
                        association.choices.front());
                    if (!key || key->systemverilog == nullptr
                        || key->systemverilog->kind
                            != semantic::sv::ExpressionKind::name) {
                        return std::nullopt;
                    }
                    const auto member = std::ranges::find(
                        aggregate->members,
                        key->systemverilog->text,
                        &semantic::sv::PackedMember::name);
                    if (member == aggregate->members.end()) {
                        return std::nullopt;
                    }
                    index = static_cast<std::size_t>(std::distance(
                        aggregate->members.begin(), member));
                } else if (association.choice_spelling.empty()) {
                    index = positional++;
                } else {
                    return std::nullopt;
                }
                if (*index >= selected.size() || selected[*index]
                    || !write_member(*index, association.value)) {
                    return std::nullopt;
                }
                selected[*index] = true;
            }
            if (default_value) {
                for (std::size_t index { }; index < selected.size();
                    ++index) {
                    if (!selected[index]
                        && !write_member(index, *default_value)) {
                        return std::nullopt;
                    }
                }
            }
            return result;
        };

        type_default = [&](const semantic::sv::TypeReference& type,
                           const std::size_t type_width)
            -> std::optional<PackedLogic4> {
            const auto metadata = compiled_systemverilog_packed_type(compiled,
                working_specialization, type);
            auto result = metadata
                ? default_compiled_packed_value(*metadata, type_width)
                : PackedLogic4 {
                      type_width,
                      type.four_state ? Logic4::x : Logic4::zero,
                  };
            const auto* aggregate = aggregate_definition(type);
            if (aggregate == nullptr) {
                return result;
            }
            if (!active_types.insert(aggregate->id.value()).second) {
                return std::nullopt;
            }
            const auto layout = member_layout(*aggregate, type_width);
            if (!layout) {
                active_types.erase(aggregate->id.value());
                return std::nullopt;
            }
            std::optional<std::size_t> initialized_union_member;
            for (std::size_t index { };
                index < aggregate->members.size(); ++index) {
                const auto& member = aggregate->members[index];
                const auto [offset, member_width_value] = (*layout)[index];
                std::optional<PackedLogic4> value;
                if (member.initializer) {
                    value = expression_value(
                        *member.initializer, member.type,
                        member_width_value);
                } else if (aggregate->form
                    == semantic::sv::TypeForm::packed_structure) {
                    value = type_default(
                        member.type, member_width_value);
                }
                if (!value) {
                    if (member.initializer) {
                        active_types.erase(aggregate->id.value());
                        return std::nullopt;
                    }
                    continue;
                }
                insert_compiled_default(result, *value, offset);
                if (aggregate->form
                    != semantic::sv::TypeForm::packed_structure) {
                    initialized_union_member = index;
                    break;
                }
            }
            if (initialized_union_member
                && aggregate->form
                    == semantic::sv::TypeForm::tagged_union) {
                const auto tag_width = std::max<std::size_t>(
                    1U,
                    static_cast<std::size_t>(std::bit_width(
                        aggregate->members.size() - 1U)));
                insert_compiled_default(
                    result,
                    unsigned_value(*initialized_union_member, tag_width),
                    type_width - tag_width);
            }
            active_types.erase(aggregate->id.value());
            return result;
        };

        return type_default(reference, width);
    }

    std::optional<PackedTypeMetadata> compiled_vhdl_signal_type(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& root,
        const std::optional<semantic::ScopeId> owner_scope = std::nullopt)
    {
        std::unordered_set<std::uint32_t> visiting;
        const semantic::CompiledDesignResolver type_resolver {
            specialization
        };
        const auto simple_name = [](const std::string_view spelling) {
            const auto separator = spelling.find_last_of(".:");
            return spelling.substr(separator == std::string_view::npos
                    ? 0U
                    : separator + 1U);
        };
        const auto boundary = [&](const std::optional<std::int64_t> value,
                                  const std::optional<semantic::ExpressionId>
                                      expression) {
            return expression
                ? specialization.evaluate_integral_expression(*expression)
                : value;
        };
        const auto canonical_name = [](std::string value) {
            std::ranges::transform(value, value.begin(), [](const char byte) {
                return static_cast<char>(std::tolower(
                    static_cast<unsigned char>(byte)));
            });
            return value;
        };
        const auto nominal_identity = [&](const semantic::vhdl::TypeDefinition&
                                              definition) {
            return "vhdl-hir-type:"
                + std::to_string(definition.id.value()) + ":"
                + canonical_name(definition.name);
        };
        const auto concrete_range = [&](const semantic::vhdl::RangeConstraint&
                                            constraint)
            -> std::optional<frontend::IntegerRange> {
            const auto left = boundary(
                constraint.left, constraint.left_expression);
            const auto right = boundary(
                constraint.right, constraint.right_expression);
            if (!left || !right) {
                return std::nullopt;
            }
            return frontend::IntegerRange {
                *left, *right, constraint.descending
            };
        };
        const auto resolved_packed_range = [&](
            const semantic::vhdl::SubtypeIndication& subtype)
            -> std::optional<frontend::PackedRange> {
            if ((subtype.domain != semantic::vhdl::ValueDomain::bit2
                    && subtype.domain
                        != semantic::vhdl::ValueDomain::logic4
                    && subtype.domain
                        != semantic::vhdl::ValueDomain::logic9)
                || subtype.constraints.size() != 1U) {
                return std::nullopt;
            }
            const auto& constraint = subtype.constraints.front();
            if (constraint.kind
                    != semantic::vhdl::RangeKind::array_index
                || constraint.null) {
                return std::nullopt;
            }
            const auto range = concrete_range(constraint);
            if (!range
                || (range->descending
                    ? range->left < range->right
                    : range->left > range->right)) {
                return std::nullopt;
            }
            const auto distance = index_distance(
                range->left, range->right);
            if (distance
                    == std::numeric_limits<std::uint64_t>::max()
                || distance >= hir_systemverilog_maximum_constant_width) {
                return std::nullopt;
            }
            return frontend::PackedRange {
                range->left, range->right, range->descending
            };
        };
        const auto actual_subtype = [&](const semantic::vhdl::TypeDefinition&
                                            definition)
            -> std::optional<semantic::vhdl::SubtypeIndication> {
            const auto& actuals
                = specialization.specialization().actual_identities;
            const auto actual = std::ranges::find(actuals,
                definition.declaration,
                &semantic::SpecializedHirActualIdentity::declaration);
            if (actual == actuals.end()) {
                return std::nullopt;
            }
            if (actual->vhdl_type) {
                return actual->vhdl_type;
            }
            if (actual->actual_declaration) {
                const auto declaration = specialization.find_declaration(
                    *actual->actual_declaration);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->subtype) {
                    return declaration->vhdl->subtype;
                }
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->declared_type) {
                    semantic::vhdl::SubtypeIndication subtype;
                    subtype.type_mark.target
                        = *declaration->vhdl->declared_type;
                    return subtype;
                }
            }
            return std::nullopt;
        };
        const auto generic_actual_subtype = [&](const std::string_view name)
            -> std::optional<semantic::vhdl::SubtypeIndication> {
            const auto generic_name = simple_name(name);
            if (generic_name.empty()) {
                return std::nullopt;
            }
            for (const auto& actual :
                specialization.specialization().actual_identities) {
                const auto declaration = specialization.find_declaration(
                    actual.declaration);
                if (!declaration || declaration->vhdl == nullptr
                    || declaration->vhdl->form
                        != semantic::vhdl::DeclarationForm::generic_type
                    || !compiled_vhdl_name_equal(
                        declaration->vhdl->name, generic_name)) {
                    continue;
                }
                if (actual.vhdl_type) {
                    return actual.vhdl_type;
                }
                if (!actual.actual_declaration) {
                    return std::nullopt;
                }
                const auto selected = specialization.find_declaration(
                    *actual.actual_declaration);
                if (!selected || selected->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (selected->vhdl->subtype) {
                    return selected->vhdl->subtype;
                }
                if (selected->vhdl->declared_type) {
                    semantic::vhdl::SubtypeIndication subtype;
                    subtype.type_mark.target
                        = *selected->vhdl->declared_type;
                    return subtype;
                }
                return std::nullopt;
            }
            return std::nullopt;
        };
        const auto resolve = [&](const auto& self,
                                 const semantic::vhdl::SubtypeIndication& subtype)
            -> std::optional<PackedTypeMetadata> {
            if (!subtype.type_mark.target.valid()) {
                if (const auto actual = generic_actual_subtype(
                        subtype.type_mark.spelling);
                    actual
                    && (actual->type_mark.target.valid()
                        || !compiled_vhdl_name_equal(
                            simple_name(actual->type_mark.spelling),
                            simple_name(subtype.type_mark.spelling)))) {
                    auto specialized_type = *actual;
                    if (!subtype.constraints.empty()) {
                        specialized_type.constraints = subtype.constraints;
                        specialized_type.unconstrained = false;
                        specialized_type.executable_width.reset();
                    }
                    return self(self, specialized_type);
                }
            }
            if (!subtype.type_mark.target.valid()) {
                auto linked = compiled_vhdl_link_subtype(
                    specialization, subtype, owner_scope);
                if (linked.type_mark.target.valid()) {
                    return self(self, linked);
                }
            }
            PackedTypeMetadata result;
            result.spelling = subtype.type_mark.spelling;
            result.domain = compiled_value_domain(subtype.domain);
            result.is_signed = subtype.signed_value
                || result.domain == frontend::ValueDomain::Integer;
            result.vhdl_integer_storage_width
                = subtype.integer_storage_width;
            result.packed_range = compiled_packed_range(subtype);
            if (!result.packed_range) {
                result.packed_range = resolved_packed_range(subtype);
            }
            result.integer_range = compiled_integer_range(subtype);
            if (const auto layout = compiled_vhdl_named_signal_layout(
                    specialization, subtype, owner_scope)) {
                result.domain = layout->domain;
            }
            if (!result.integer_range
                && result.domain == frontend::ValueDomain::Integer) {
                const auto constraint = std::ranges::find_if(
                    subtype.constraints,
                    compiled_vhdl_integer_constraint);
                if (constraint != subtype.constraints.end()) {
                    result.integer_range = frontend::IntegerRange {
                        *constraint->left,
                        *constraint->right,
                        constraint->descending,
                    };
                }
            }
            if (!subtype.type_mark.target.valid()) {
                const auto name = simple_name(
                    subtype.type_mark.spelling);
                const auto builtin_array
                    = compiled_vhdl_name_equal(name, "bit_vector")
                    || compiled_vhdl_name_equal(name, "boolean_vector")
                    || compiled_vhdl_name_equal(name, "string")
                    || compiled_vhdl_name_equal(name, "integer_vector")
                    || compiled_vhdl_name_equal(name, "time_vector")
                    || compiled_vhdl_name_equal(name, "std_logic_vector")
                    || compiled_vhdl_name_equal(name, "std_ulogic_vector")
                    || compiled_vhdl_name_equal(name, "signed")
                    || compiled_vhdl_name_equal(name, "unsigned")
                    || compiled_vhdl_name_equal(name, "ufixed")
                    || compiled_vhdl_name_equal(name, "sfixed")
                    || compiled_vhdl_name_equal(
                        name, "unresolved_ufixed")
                    || compiled_vhdl_name_equal(
                        name, "unresolved_sfixed")
                    || compiled_vhdl_name_equal(name, "float")
                    || compiled_vhdl_name_equal(
                        name, "unresolved_float")
                    || compiled_vhdl_name_equal(name, "u_float");
                if (builtin_array) {
                    auto array = std::make_shared<VhdlArrayMetadata>();
                    array->unconstrained = subtype.unconstrained
                        || subtype.constraints.empty();
                    array->element_domain = result.domain;
                    array->element_spelling
                        = compiled_vhdl_name_equal(name, "string")
                        ? "character"
                        : compiled_vhdl_name_equal(
                              name, "boolean_vector")
                        ? "boolean"
                        : compiled_vhdl_name_equal(name, "bit_vector")
                        ? "bit"
                        : compiled_vhdl_name_equal(
                              name, "integer_vector")
                            || compiled_vhdl_name_equal(
                                name, "time_vector")
                        ? "integer"
                        : "std_logic";
                    if (!subtype.constraints.empty()) {
                        VhdlArrayDimensionMetadata dimension;
                        dimension.index_subtype = "integer";
                        dimension.unconstrained = false;
                        dimension.range = concrete_range(
                            subtype.constraints.front());
                        dimension.null
                            = subtype.constraints.front().null;
                        dimension.stride = 1U;
                        array->dimensions.push_back(
                            std::move(dimension));
                    } else {
                        VhdlArrayDimensionMetadata dimension;
                        dimension.index_subtype = "integer";
                        dimension.unconstrained = true;
                        dimension.stride = 1U;
                        array->dimensions.push_back(
                            std::move(dimension));
                    }
                    array->index_subtype = "integer";
                    array->flat_width = result.width();
                    result.vhdl_array = std::move(array);
                }
                return result;
            }
            if (!visiting.insert(
                             subtype.type_mark.target.value())
                    .second) {
                result.named_type = subtype.type_mark.spelling;
                result.nominal_type = "vhdl-hir-type:"
                    + std::to_string(subtype.type_mark.target.value());
                return result;
            }
            const auto type = specialization.find_type(
                subtype.type_mark.target);
            if (!type || type->vhdl == nullptr) {
                visiting.erase(subtype.type_mark.target.value());
                return result;
            }
            const auto& definition = *type->vhdl;
            if (const auto actual = actual_subtype(definition);
                actual && actual->type_mark.target != subtype.type_mark.target) {
                auto specialized_type = *actual;
                if (!subtype.constraints.empty()) {
                    specialized_type.constraints = subtype.constraints;
                    specialized_type.unconstrained = false;
                    specialized_type.executable_width.reset();
                }
                auto specialized = self(self, specialized_type);
                visiting.erase(subtype.type_mark.target.value());
                return specialized;
            }
            result.spelling = definition.name.empty()
                ? subtype.type_mark.spelling
                : definition.name;
            result.named_type = result.spelling;
            result.nominal_type = nominal_identity(definition);
            result.vhdl_type_declaration = result.nominal_type;
            using TypeForm = semantic::vhdl::TypeForm;
            if (definition.form == TypeForm::subtype
                || definition.form == TypeForm::alias
                || definition.form == TypeForm::scalar) {
                auto base = definition.base;
                if (!subtype.constraints.empty()) {
                    base.constraints = subtype.constraints;
                    base.unconstrained = false;
                } else if (definition.scalar_range) {
                    base.constraints = { *definition.scalar_range };
                    base.unconstrained = false;
                }
                if (subtype.executable_width) {
                    base.executable_width = subtype.executable_width;
                }
                if (auto resolved = self(self, base)) {
                    const auto declaration = result.vhdl_type_declaration;
                    const auto spelling = result.spelling;
                    result = std::move(*resolved);
                    result.spelling = spelling;
                    result.named_type = spelling;
                    result.vhdl_type_declaration = declaration;
                }
            } else if (definition.form == TypeForm::enumeration) {
                result.domain = frontend::ValueDomain::Bit2;
                result.enumeration_literals.reserve(
                    definition.enumeration_literals.size());
                for (const auto& literal : definition.enumeration_literals) {
                    result.enumeration_literals.push_back(literal.spelling);
                }
                if (!result.enumeration_literals.empty()) {
                    result.enumeration_range = frontend::EnumerationRange {
                        0,
                        static_cast<std::int64_t>(
                            result.enumeration_literals.size() - 1U),
                        false
                    };
                    const auto constrained = std::ranges::find_if(
                        subtype.constraints,
                        [](const auto& range) {
                            return (range.kind
                                        == semantic::vhdl::RangeKind::
                                            enumeration
                                    || range.kind
                                        == semantic::vhdl::RangeKind::
                                            discrete
                                    || range.kind
                                        == semantic::vhdl::RangeKind::
                                            integer)
                                && (range.left
                                    || range.left_expression)
                                && (range.right
                                    || range.right_expression);
                        });
                    if (constrained != subtype.constraints.end()) {
                        auto left = constrained->left
                            ? constrained->left
                            : specialization.evaluate_integral_expression(
                                  *constrained->left_expression);
                        auto right = constrained->right
                            ? constrained->right
                            : specialization.evaluate_integral_expression(
                                  *constrained->right_expression);
                        if (!left && constrained->left_expression) {
                            left = type_resolver
                                       .vhdl_enumeration_literal_ordinal(
                                           definition.id,
                                           *constrained->left_expression);
                        }
                        if (!right && constrained->right_expression) {
                            right = type_resolver
                                        .vhdl_enumeration_literal_ordinal(
                                            definition.id,
                                            *constrained->right_expression);
                        }
                        if (left && right) {
                            result.enumeration_range
                                = frontend::EnumerationRange {
                                    *left,
                                    *right,
                                    constrained->descending,
                                };
                        }
                    }
                }
                if (const auto layout = compiled_vhdl_named_signal_layout(
                        specialization, subtype, owner_scope);
                    layout && layout->width != 0U) {
                    result.packed_range = frontend::PackedRange {
                        static_cast<std::int64_t>(layout->width - 1U),
                        0,
                        true
                    };
                }
            } else if (definition.form == TypeForm::record) {
                const auto layout = compiled_vhdl_named_signal_layout(
                    specialization, subtype, owner_scope);
                result.packed_aggregate
                    = frontend::PackedAggregateKind::Struct;
                if (layout && layout->width != 0U) {
                    result.packed_range = frontend::PackedRange {
                        static_cast<std::int64_t>(layout->width - 1U),
                        0,
                        true
                    };
                }
                std::uint64_t remaining = layout ? layout->width : 0U;
                for (const auto& element : definition.record_elements) {
                    auto nested = self(self, element.subtype);
                    const auto nested_layout
                        = compiled_vhdl_named_signal_layout(
                            specialization, element.subtype, owner_scope);
                    if (!nested || !nested_layout
                        || nested_layout->width > remaining) {
                        continue;
                    }
                    remaining -= nested_layout->width;
                    PackedMemberMetadata member;
                    member.name = canonical_name(element.name);
                    member.domain = nested->domain;
                    member.spelling = nested->spelling;
                    member.packed_range = nested->packed_range;
                    member.is_signed = nested->is_signed;
                    member.lsb_offset = remaining;
                    if (nested->vhdl_array) {
                        nested->vhdl_array->flat_width
                            = nested_layout->width;
                    }
                    member.nested_types.push_back(*nested);
                    result.packed_members.push_back(std::move(member));
                }
            } else if (definition.form == TypeForm::array
                && definition.element_subtype) {
                auto element_subtype = *definition.element_subtype;
                auto element = self(self, element_subtype);
                if (element_subtype.type_mark.target.valid()) {
                    const auto element_definition
                        = specialization.find_type(
                            element_subtype.type_mark.target);
                    if (element_definition
                        && element_definition->vhdl != nullptr
                        && (element_definition->vhdl->form
                                == TypeForm::subtype
                            || element_definition->vhdl->form
                                == TypeForm::alias)) {
                        auto base = element_definition->vhdl->base;
                        if (!element_subtype.constraints.empty()) {
                            base.constraints = element_subtype.constraints;
                            base.unconstrained = false;
                        }
                        element = self(self, base);
                    }
                }
                auto array = std::make_shared<VhdlArrayMetadata>();
                array->unconstrained = subtype.unconstrained;
                if (element) {
                    // A packed array carries the value domain of its
                    // elements.  The subtype's discrete constraints describe
                    // array indices and must not make the whole vector look
                    // like an integer value.
                    result.domain = element->domain;
                    array->element_spelling = element->spelling;
                    array->element_named_type = element->named_type;
                    array->element_domain = element->domain;
                    array->element_types.push_back(*element);
                }
                const auto layout = compiled_vhdl_named_signal_layout(
                    specialization, subtype, owner_scope);
                if (layout) {
                    array->flat_width = layout->width;
                }
                if (!result.packed_range
                    && !definition.array_dimensions.empty()
                    && definition.array_dimensions.front().constraint) {
                    const auto declared_range = concrete_range(
                        *definition.array_dimensions.front().constraint);
                    if (declared_range) {
                        result.packed_range = frontend::PackedRange {
                            declared_range->left,
                            declared_range->right,
                            declared_range->descending,
                        };
                    }
                }
                std::uint64_t stride = element && element->width()
                    ? *element->width()
                    : 1U;
                std::vector<VhdlArrayDimensionMetadata> dimensions;
                dimensions.reserve(definition.array_dimensions.size());
                for (std::size_t index { };
                    index < definition.array_dimensions.size(); ++index) {
                    const auto& source = definition.array_dimensions[index];
                    VhdlArrayDimensionMetadata dimension;
                    dimension.index_subtype = source.index_subtype.canonical.empty()
                        ? source.index_subtype.spelling
                        : source.index_subtype.canonical;
                    dimension.unconstrained = source.unconstrained;
                    const semantic::vhdl::RangeConstraint* constraint = nullptr;
                    if (index < subtype.constraints.size()) {
                        constraint = &subtype.constraints[index];
                    } else if (source.constraint) {
                        constraint = &*source.constraint;
                    }
                    if (constraint != nullptr) {
                        dimension.range = concrete_range(*constraint);
                        dimension.null = constraint->null
                            || (dimension.range
                                && (dimension.range->descending
                                        ? dimension.range->left
                                            < dimension.range->right
                                        : dimension.range->left
                                            > dimension.range->right));
                    }
                    dimensions.push_back(std::move(dimension));
                }
                for (auto index = dimensions.size(); index > 0U; --index) {
                    auto& dimension = dimensions[index - 1U];
                    dimension.stride = stride;
                    if (dimension.range) {
                        if (dimension.null) {
                            stride = 0U;
                            continue;
                        }
                        const auto count = index_distance(
                                               dimension.range->left,
                                               dimension.range->right)
                            + 1U;
                        if (count != 0U
                            && stride
                                <= std::numeric_limits<std::uint64_t>::max()
                                    / count) {
                            stride *= count;
                        }
                    }
                }
                array->dimensions = std::move(dimensions);
                if (!array->dimensions.empty()) {
                    array->index_subtype
                        = array->dimensions.front().index_subtype;
                }
                result.vhdl_array = std::move(array);
            } else if (definition.form == TypeForm::access) {
                result.domain = frontend::ValueDomain::Bit2;
                result.packed_range = frontend::PackedRange { 31, 0, true };
                auto access = std::make_shared<VhdlAccessMetadata>();
                access->maximum_objects = definition.maximum_objects;
                access->deallocate_releases_storage
                    = definition.deallocate_releases_storage;
                access->reclaim_when_unreachable
                    = definition.reclaim_when_unreachable;
                access->simulation_lifetime
                    = !definition.reclaim_when_unreachable;
                if (definition.designated_subtype) {
                    if (const auto designated = self(
                            self, *definition.designated_subtype)) {
                        access->designated_types.push_back(*designated);
                    }
                }
                result.vhdl_access = std::move(access);
            } else if (definition.form == TypeForm::physical) {
                result.domain = frontend::ValueDomain::Integer;
                result.is_signed = true;
                result.vhdl_integer_storage_width
                    = definition.base.integer_storage_width != 0U
                    ? definition.base.integer_storage_width
                    : 64U;
                auto physical = std::make_shared<VhdlPhysicalMetadata>();
                if (definition.scalar_range) {
                    physical->resolved_range
                        = concrete_range(*definition.scalar_range);
                    result.integer_range = physical->resolved_range;
                }
                physical->units.reserve(definition.physical_units.size());
                std::unordered_map<std::string, std::int64_t> unit_scales;
                for (std::size_t index { };
                    index < definition.physical_units.size(); ++index) {
                    const auto& unit = definition.physical_units[index];
                    VhdlPhysicalUnitMetadata converted;
                    converted.name = canonical_name(unit.name);
                    auto scale = unit.scale_factor
                        ? unit.scale_factor
                        : index == 0U
                        ? std::optional<std::int64_t> { 1 }
                        : std::nullopt;
                    if (!scale && unit.scale) {
                        const auto expression
                            = specialization.find_expression(*unit.scale);
                        constexpr std::string_view physical_prefix {
                            "@vhdl-physical:"
                        };
                        if (expression && expression->vhdl != nullptr
                            && expression->vhdl->text.starts_with(
                                physical_prefix)
                            && expression->vhdl->operands.size() == 1U) {
                            const auto referenced = canonical_name(std::string {
                                std::string_view { expression->vhdl->text }
                                    .substr(physical_prefix.size())
                            });
                            const auto prior = unit_scales.find(referenced);
                            const auto multiplier
                                = specialization.evaluate_integral_expression(
                                    expression->vhdl->operands.front());
                            if (prior != unit_scales.end() && multiplier
                                && *multiplier >= 0
                                && (*multiplier == 0
                                    || prior->second
                                        <= std::numeric_limits<
                                               std::int64_t>::max()
                                            / *multiplier)) {
                                scale = prior->second * *multiplier;
                            }
                        }
                    }
                    converted.scale_factor = scale;
                    converted.span = compiled_source_span(
                        specialization.design(), unit.source);
                    if (scale) {
                        unit_scales.insert_or_assign(
                            converted.name, *scale);
                    }
                    physical->units.push_back(std::move(converted));
                }
                result.vhdl_physical = std::move(physical);
            }
            visiting.erase(subtype.type_mark.target.value());
            return result;
        };
        auto result = resolve(resolve,
            compiled_vhdl_link_subtype(
                specialization, root, owner_scope));
        if (result
            && result->domain != frontend::ValueDomain::Integer) {
            // Discrete array bounds and enumeration ordinals have their own
            // metadata. They must not leak into the scalar-integer range
            // channel, where downstream lowering would emit IntegerCheck for
            // ordinary packed values.
            result->integer_range.reset();
        }
        return result;
    }

    CompiledVhdlResolutionBinding compiled_vhdl_resolution_binding(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& subtype,
        const semantic::vhdl::Name& resolver,
        const std::optional<semantic::ScopeId> owner_scope)
    {
        CompiledVhdlResolutionBinding result;
        result.designator = resolver.canonical.empty()
            ? resolver.spelling
            : resolver.canonical;
        result.source = resolver.source;
        const semantic::CompiledDesignResolver compiled_resolver {
            specialization
        };
        const auto resolutions = owner_scope
            ? compiled_resolver.resolve_vhdl_callables(
                  resolver, *owner_scope)
            : semantic::CompiledVhdlCallableResolutionResult { };
        std::vector<semantic::DeclarationId> bodies;
        const bool executable_body = !resolutions.candidates.empty();
        for (const auto& resolution : resolutions.candidates) {
            const auto declaration = specialization.find_declaration(
                resolution.body);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->callable) {
                continue;
            }
            const auto& callable = *declaration->vhdl;
            const auto base = compiled_vhdl_signal_type(
                specialization, subtype, owner_scope);
            const auto formal = callable.callable->formals.size() == 1U
                ? specialization.find_declaration(
                      callable.callable->formals.front())
                : std::nullopt;
            const auto formal_type = formal && formal->vhdl != nullptr
                    && formal->vhdl->subtype
                ? compiled_vhdl_signal_type(specialization,
                      *formal->vhdl->subtype, formal->vhdl->scope)
                : std::nullopt;
            const auto return_subtype = callable.callable->return_type
                ? callable.callable->return_type
                : callable.subtype;
            const auto return_type = return_subtype
                ? compiled_vhdl_signal_type(specialization,
                      *return_subtype, callable.scope)
                : std::nullopt;
            const bool supported_base = base
                && (base->domain == frontend::ValueDomain::Bit2
                    || base->domain == frontend::ValueDomain::Logic9);
            const bool profile_matches = callable.callable->function
                && callable.callable->pure
                && callable.callable->defined
                && callable.callable->formals.size() == 1U
                && supported_base && formal_type && formal_type->vhdl_array
                && formal_type->vhdl_array->element_domain == base->domain
                && return_type && !return_type->vhdl_array
                && return_type->domain == base->domain;
            if (profile_matches
                && std::ranges::find(
                       bodies, resolution.body)
                    == bodies.end()) {
                bodies.push_back(resolution.body);
            }
        }
        if (!executable_body) {
            result.issue = CompiledVhdlResolutionIssue::missing_body;
            return result;
        }
        if (bodies.empty()) {
            result.issue = CompiledVhdlResolutionIssue::invalid_profile;
            return result;
        }
        if (bodies.size() != 1U) {
            result.issue = CompiledVhdlResolutionIssue::ambiguous_profile;
            return result;
        }
        const auto declaration = specialization.find_declaration(
            bodies.front());
        const auto statement = declaration && declaration->vhdl != nullptr
                && declaration->vhdl->statements.size() == 1U
            ? specialization.find_statement(
                  declaration->vhdl->statements.front())
            : std::nullopt;
        const auto value = statement && statement->vhdl != nullptr
                && statement->vhdl->kind
                    == semantic::vhdl::StatementKind::return_statement
                && statement->vhdl->value
            ? specialization.find_expression(*statement->vhdl->value)
            : std::nullopt;
        if (!value || value->vhdl == nullptr
            || value->vhdl->kind != semantic::vhdl::ExpressionKind::binary
            || (!compiled_vhdl_name_equal(value->vhdl->text, "or")
                && !compiled_vhdl_name_equal(value->vhdl->text, "and"))) {
            result.issue = CompiledVhdlResolutionIssue::unsupported_body;
            return result;
        }
        result.kind = compiled_vhdl_name_equal(value->vhdl->text, "or")
            ? ResolutionKind::vhdl_user_or
            : ResolutionKind::vhdl_user_and;
        return result;
    }

    std::optional<CompiledVhdlSignalLayout> compiled_vhdl_mode_view_layout(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& root,
        const semantic::vhdl::ModeViewInterfaceProfile& view)
    {
        std::unordered_set<std::uint32_t> visiting;
        const auto boundary = [&](const std::optional<std::int64_t> value,
                                  const std::optional<semantic::ExpressionId>
                                      expression) {
            return expression
                ? specialization.evaluate_integral_expression(*expression)
                : value;
        };
        const auto merge_domain = [](const frontend::ValueDomain left,
                                      const frontend::ValueDomain right)
            -> std::optional<frontend::ValueDomain> {
            if (left == frontend::ValueDomain::Unknown) {
                return right;
            }
            if (right == frontend::ValueDomain::Unknown || left == right) {
                return left;
            }
            const auto logic_rank = [](const frontend::ValueDomain domain) {
                switch (domain) {
                case frontend::ValueDomain::Bit2:
                    return 1;
                case frontend::ValueDomain::Logic4:
                    return 2;
                case frontend::ValueDomain::Logic9:
                    return 3;
                default:
                    return 0;
                }
            };
            const auto left_rank = logic_rank(left);
            const auto right_rank = logic_rank(right);
            if (left_rank != 0 && right_rank != 0) {
                return left_rank >= right_rank ? left : right;
            }
            return std::nullopt;
        };
        const auto resolve = [&](const auto& self,
                                 const semantic::vhdl::SubtypeIndication& subtype)
            -> std::optional<CompiledVhdlSignalLayout> {
            const auto domain = compiled_value_domain(subtype.domain);
            if (const auto width = compiled_vhdl_signal_width(subtype);
                width && compiled_vhdl_scalar_domain(domain)) {
                return CompiledVhdlSignalLayout { *width, domain };
            }
            if (!subtype.type_mark.target.valid()
                || !visiting.insert(
                                subtype.type_mark.target.value())
                    .second) {
                return std::nullopt;
            }
            const auto type = specialization.find_type(
                subtype.type_mark.target);
            if (!type || type->vhdl == nullptr) {
                visiting.erase(subtype.type_mark.target.value());
                return std::nullopt;
            }
            const auto& definition = *type->vhdl;
            std::optional<CompiledVhdlSignalLayout> result;
            if (definition.form == semantic::vhdl::TypeForm::record) {
                std::size_t width { };
                auto record_domain = frontend::ValueDomain::Unknown;
                bool valid = !definition.record_elements.empty();
                for (const auto& element : definition.record_elements) {
                    const auto layout = self(self, element.subtype);
                    const auto merged = layout
                        ? merge_domain(record_domain, layout->domain)
                        : std::nullopt;
                    if (!layout || !merged || layout->width == 0U
                        || layout->width
                            > std::numeric_limits<std::size_t>::max() - width) {
                        valid = false;
                        break;
                    }
                    width += layout->width;
                    record_domain = *merged;
                }
                if (valid && width != 0U
                    && compiled_vhdl_scalar_domain(record_domain)) {
                    result = CompiledVhdlSignalLayout {
                        width, record_domain
                    };
                }
            } else if (definition.form == semantic::vhdl::TypeForm::array
                && definition.element_subtype
                && !definition.array_dimensions.empty()) {
                const auto element = self(self, *definition.element_subtype);
                std::size_t element_count { 1U };
                bool valid = element.has_value();
                for (std::size_t index { };
                    valid && index < definition.array_dimensions.size();
                    ++index) {
                    const auto& dimension = definition.array_dimensions[index];
                    const semantic::vhdl::RangeConstraint* constraint = nullptr;
                    if (index < subtype.constraints.size()) {
                        constraint = &subtype.constraints[index];
                    } else if (dimension.constraint) {
                        constraint = &*dimension.constraint;
                    }
                    if (constraint == nullptr || constraint->null) {
                        valid = false;
                        break;
                    }
                    const auto left = boundary(
                        constraint->left, constraint->left_expression);
                    const auto right = boundary(
                        constraint->right, constraint->right_expression);
                    if (!left || !right) {
                        valid = false;
                        break;
                    }
                    const auto distance = index_distance(*left, *right);
                    if (distance == std::numeric_limits<std::uint64_t>::max()
                        || distance + 1U
                            > std::numeric_limits<std::size_t>::max()
                                / element_count) {
                        valid = false;
                        break;
                    }
                    element_count *= static_cast<std::size_t>(distance + 1U);
                }
                if (valid && element->width != 0U
                    && element_count
                        <= std::numeric_limits<std::size_t>::max()
                            / element->width) {
                    result = CompiledVhdlSignalLayout {
                        element_count * element->width, element->domain
                    };
                }
            } else if (definition.form == semantic::vhdl::TypeForm::subtype
                || definition.form == semantic::vhdl::TypeForm::alias) {
                result = self(self, definition.base);
            }
            visiting.erase(subtype.type_mark.target.value());
            return result;
        };
        if (const auto layout = resolve(resolve, root)) {
            return layout;
        }
        const auto element_count = [&](
                                       const semantic::vhdl::SubtypeIndication&
                                           subtype)
            -> std::optional<std::size_t> {
            if (subtype.constraints.empty()) {
                return std::nullopt;
            }
            std::size_t count { 1U };
            for (const auto& constraint : subtype.constraints) {
                if (constraint.null) {
                    return std::nullopt;
                }
                const auto left = boundary(
                    constraint.left, constraint.left_expression);
                const auto right = boundary(
                    constraint.right, constraint.right_expression);
                if (!left || !right) {
                    return std::nullopt;
                }
                const auto distance = index_distance(*left, *right);
                if (distance == std::numeric_limits<std::uint64_t>::max()
                    || distance + 1U
                        > std::numeric_limits<std::size_t>::max() / count) {
                    return std::nullopt;
                }
                count *= static_cast<std::size_t>(distance + 1U);
            }
            return count;
        };
        const auto composed = [&](const auto& self,
                                  const std::span<const semantic::vhdl::ModeViewElement> elements)
            -> std::optional<CompiledVhdlSignalLayout> {
            std::size_t width { };
            auto composed_domain = frontend::ValueDomain::Unknown;
            for (const auto& element : elements) {
                auto layout = element.elements.empty()
                    ? element.subtype
                        ? resolve(resolve, *element.subtype)
                        : std::nullopt
                    : self(self, element.elements);
                if (layout
                    && element.form
                        == semantic::vhdl::ModeViewElementForm::array_view) {
                    const auto count = element.subtype
                        ? element_count(*element.subtype)
                        : std::nullopt;
                    if (!count || layout->width > std::numeric_limits<std::size_t>::max() / *count) {
                        layout.reset();
                    } else {
                        layout->width *= *count;
                    }
                }
                const auto merged = layout
                    ? merge_domain(composed_domain, layout->domain)
                    : std::nullopt;
                if (!layout || !merged || layout->width == 0U
                    || layout->width
                        > std::numeric_limits<std::size_t>::max() - width) {
                    return std::nullopt;
                }
                width += layout->width;
                composed_domain = *merged;
            }
            if (width == 0U
                || !compiled_vhdl_scalar_domain(composed_domain)) {
                return std::nullopt;
            }
            return CompiledVhdlSignalLayout { width, composed_domain };
        };
        auto layout = composed(composed, view.elements);
        if (layout
            && view.form == semantic::vhdl::ModeViewElementForm::array_view) {
            const auto count = element_count(root);
            if (!count || layout->width > std::numeric_limits<std::size_t>::max() / *count) {
                return std::nullopt;
            }
            layout->width *= *count;
        }
        return layout;
    }

    constexpr std::size_t maximum_compiled_vhdl_mode_view_endpoints
        = 65'536U;

    PackedTypeMetadata compiled_vhdl_mode_view_member_type(
        const PackedMemberMetadata& member)
    {
        if (!member.nested_types.empty()) {
            return member.nested_types.front();
        }
        return PackedTypeMetadata {
            member.domain,
            member.spelling,
            member.packed_range,
            member.is_signed,
        };
    }

    bool materialize_compiled_vhdl_mode_view_endpoints(
        const semantic::CompiledDesign& compiled,
        const semantic::SpecializedHirUnit& specialization,
        const semantic::ScopeId scope,
        const PackedTypeMetadata& root_type,
        const semantic::vhdl::ModeViewInterfaceProfile& view,
        const SignalId signal,
        const std::size_t storage_width,
        const std::string& formal,
        const std::string& actual,
        std::vector<VhdlModeViewElementBinding>& output,
        std::string& error)
    {
        const auto append_member = [](const std::string& base,
                                       const std::string_view member) {
            return base + "." + std::string { member };
        };
        const auto append_indices = [](const std::string& base,
                                        const std::vector<std::int64_t>& indices) {
            std::string result = base + "(";
            for (std::size_t index { }; index < indices.size(); ++index) {
                if (index != 0U) {
                    result += ',';
                }
                result += std::to_string(indices[index]);
            }
            result += ')';
            return result;
        };
        const auto checked_add = [](const std::uint64_t left,
                                     const std::uint64_t right,
                                     std::uint64_t& result) {
            if (right > std::numeric_limits<std::uint64_t>::max() - left) {
                return false;
            }
            result = left + right;
            return true;
        };

        using Elements = std::span<const semantic::vhdl::ModeViewElement>;
        std::function<bool(const PackedTypeMetadata&, Elements,
            std::uint64_t, const std::string&, const std::string&)>
            materialize_record;
        std::function<bool(const PackedTypeMetadata&, Elements,
            std::uint64_t, const std::string&, const std::string&)>
            expand_array;

        expand_array = [&](const PackedTypeMetadata& type,
                           const Elements elements,
                           const std::uint64_t base_offset,
                           const std::string& formal_base,
                           const std::string& actual_base) {
            if (!type.vhdl_array
                || type.vhdl_array->element_types.size() != 1U
                || !type.vhdl_array->flat_width) {
                error = "array-view endpoint requires one concrete array "
                        "element subtype";
                return false;
            }
            const auto& array = *type.vhdl_array;
            if (array.dimensions.empty()) {
                error = "array-view endpoint requires at least one concrete "
                        "dimension";
                return false;
            }
            if (std::ranges::any_of(
                    array.dimensions,
                    [](const auto& dimension) {
                        return !dimension.range
                            || (!dimension.null
                                && dimension.stride == 0U);
                    })) {
                error = "array-view endpoint has an unconstrained or "
                        "incomplete layout";
                return false;
            }
            if (std::ranges::any_of(
                    array.dimensions,
                    [](const auto& dimension) {
                        return dimension.null;
                    })) {
                return true;
            }

            std::vector<std::int64_t> indices(array.dimensions.size());
            const auto materialize_element
                = [&](const semantic::vhdl::ModeViewElement& element) {
                std::function<bool(std::size_t, std::uint64_t)> visit;
                visit = [&](const std::size_t dimension_index,
                            const std::uint64_t offset) {
                    if (dimension_index == array.dimensions.size()) {
                        const std::array one { element };
                        return materialize_record(
                            array.element_types.front(), one, offset,
                            append_indices(formal_base, indices),
                            append_indices(actual_base, indices));
                    }
                    const auto& dimension
                        = array.dimensions[dimension_index];
                    const auto& range = *dimension.range;
                    const auto distance = index_distance(
                        range.left, range.right);
                    if (distance
                            == std::numeric_limits<std::uint64_t>::max()
                        || distance + 1U
                            > maximum_compiled_vhdl_mode_view_endpoints
                                - output.size()) {
                        error = "array-view endpoint count exceeds the "
                                "bounded limit";
                        return false;
                    }
                    const auto count = distance + 1U;
                    for (std::uint64_t ordinal { }; ordinal < count;
                         ++ordinal) {
                        indices[dimension_index] = range.descending
                            ? range.left
                                - static_cast<std::int64_t>(ordinal)
                            : range.left
                                + static_cast<std::int64_t>(ordinal);
                        const auto packed_distance
                            = count - ordinal - 1U;
                        if (dimension.stride != 0U
                            && packed_distance
                                > std::numeric_limits<std::uint64_t>::max()
                                    / dimension.stride) {
                            error = "array-view endpoint offset overflows "
                                    "packed storage";
                            return false;
                        }
                        std::uint64_t element_offset { };
                        if (!checked_add(
                                offset,
                                packed_distance * dimension.stride,
                                element_offset)
                            || !visit(
                                dimension_index + 1U,
                                element_offset)) {
                            return false;
                        }
                    }
                    return true;
                };
                return visit(0U, base_offset);
            };
            return std::ranges::all_of(elements, materialize_element);
        };

        materialize_record = [&](const PackedTypeMetadata& type,
                                 const Elements elements,
                                 const std::uint64_t base_offset,
                                 const std::string& formal_base,
                                 const std::string& actual_base) {
            for (const auto& element : elements) {
                const auto member = std::ranges::find_if(
                    type.packed_members,
                    [&](const PackedMemberMetadata& candidate) {
                        return compiled_vhdl_name_equal(
                            candidate.name,
                            element.element.spelling);
                    });
                if (member == type.packed_members.end()) {
                    error = "view endpoint member '"
                        + element.element.spelling
                        + "' is absent from its associated record";
                    return false;
                }
                std::uint64_t member_offset { };
                if (!checked_add(
                        base_offset,
                        member->lsb_offset,
                        member_offset)) {
                    error = "view endpoint member offset overflows packed "
                            "storage";
                    return false;
                }
                auto member_type
                    = compiled_vhdl_mode_view_member_type(*member);
                if (element.subtype) {
                    if (const auto contextual_type
                        = compiled_vhdl_signal_type(
                            specialization,
                            *element.subtype,
                            scope)) {
                        member_type = *contextual_type;
                    }
                }
                const auto formal_member = append_member(
                    formal_base, member->name);
                const auto actual_member = append_member(
                    actual_base, member->name);
                if (element.form
                    == semantic::vhdl::ModeViewElementForm::record_view) {
                    if (!materialize_record(
                            member_type,
                            element.elements,
                            member_offset,
                            formal_member,
                            actual_member)) {
                        return false;
                    }
                    continue;
                }
                if (element.form
                    == semantic::vhdl::ModeViewElementForm::array_view) {
                    if (!expand_array(
                            member_type,
                            element.elements,
                            member_offset,
                            formal_member,
                            actual_member)) {
                        return false;
                    }
                    continue;
                }
                const auto width = member_type.width();
                if (!width || *width == 0U
                    || member_offset > storage_width
                    || *width > storage_width - member_offset) {
                    error = "view endpoint lies outside its associated "
                            "packed signal";
                    return false;
                }
                if (output.size()
                    >= maximum_compiled_vhdl_mode_view_endpoints) {
                    error = "view endpoint count exceeds the bounded limit";
                    return false;
                }
                output.push_back(VhdlModeViewElementBinding {
                    formal_member,
                    actual_member,
                    compiled_port_direction(element.direction),
                    compiled_source_span(compiled, element.element.source),
                    signal,
                    member_offset,
                    *width,
                });
            }
            return true;
        };

        const bool materialized = view.form
                == semantic::vhdl::ModeViewElementForm::array_view
            ? expand_array(
                  root_type,
                  view.elements,
                  0U,
                  formal,
                  actual)
            : materialize_record(
                  root_type,
                  view.elements,
                  0U,
                  formal,
                  actual);
        if (!materialized) {
            output.clear();
        }
        return materialized;
    }

    bool compiled_vhdl_scalar_domain(
        const frontend::ValueDomain domain) noexcept
    {
        return domain == frontend::ValueDomain::Bit2
            || domain == frontend::ValueDomain::Logic4
            || domain == frontend::ValueDomain::Logic9
            || domain == frontend::ValueDomain::Boolean
            || domain == frontend::ValueDomain::Integer;
    }

    PackedLogic4 compiled_vhdl_integral_value(
        const std::int64_t value,
        const frontend::ValueDomain domain,
        const std::size_t width)
    {
        auto result = PackedLogic4 {
            width,
            domain == frontend::ValueDomain::Logic9
                ? Logic4::x
                : Logic4::zero,
        };
        if (domain == frontend::ValueDomain::Logic9) {
            result.fill(runtime::Logic9::zero);
        }
        const auto bits = static_cast<std::uint64_t>(value);
        for (std::size_t index { }; index < width; ++index) {
            const auto one = index < 64U
                && (bits & (std::uint64_t { 1 } << index)) != 0U;
            if (domain == frontend::ValueDomain::Logic9) {
                result.set_logic9(index,
                    one ? runtime::Logic9::one : runtime::Logic9::zero);
            } else {
                result.set(index, one ? Logic4::one : Logic4::zero);
            }
        }
        return result;
    }

    std::string compiled_systemverilog_string_display(
        const std::string_view bytes)
    {
        std::string result { "\"" };
        for (const char raw_byte : bytes) {
            const auto byte = static_cast<unsigned char>(raw_byte);
            switch (byte) {
            case '\n':
                result += "\\n";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            default:
                if (byte >= 0x20U && byte <= 0x7eU) {
                    result.push_back(static_cast<char>(byte));
                } else {
                    result.push_back('\\');
                    result.push_back(static_cast<char>(
                        '0' + ((byte >> 6U) & 7U)));
                    result.push_back(static_cast<char>(
                        '0' + ((byte >> 3U) & 7U)));
                    result.push_back(static_cast<char>(
                        '0' + (byte & 7U)));
                }
                break;
            }
        }
        result.push_back('"');
        return result;
    }

    PackedLogic4 compiled_vhdl_initial_value(
        const semantic::vhdl::SubtypeIndication& subtype,
        const frontend::ValueDomain domain,
        const std::size_t width)
    {
        if (domain == frontend::ValueDomain::Logic9) {
            auto result = PackedLogic4 { width, Logic4::x };
            result.fill(runtime::Logic9::u);
            return result;
        }
        if (domain == frontend::ValueDomain::Integer) {
            const auto range = compiled_integer_range(subtype);
            const auto default_value = range
                ? range->left
                : subtype.integer_storage_width == 64U
                ? std::numeric_limits<std::int64_t>::min()
                : static_cast<std::int64_t>(
                      std::numeric_limits<std::int32_t>::min());
            return compiled_vhdl_integral_value(
                default_value, domain, width);
        }
        return PackedLogic4 {
            width,
            is_two_state_domain(domain) ? Logic4::zero : Logic4::x,
        };
    }

    std::optional<PackedLogic4> compiled_vhdl_static_port_value(
        const semantic::SpecializedHirUnit& specialization,
        semantic::ExpressionId expression_id,
        const semantic::vhdl::SubtypeIndication& subtype,
        const semantic::ScopeId scope,
        const frontend::ValueDomain domain,
        const std::size_t width)
    {
        constexpr auto qualified_prefix
            = std::string_view { "@vhdl-qualified:" };
        auto expression = specialization.find_expression(expression_id);
        if (expression && expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
            && expression->vhdl->text.starts_with(qualified_prefix)
            && expression->vhdl->operands.size() == 1U) {
            expression_id = expression->vhdl->operands.front();
            expression = specialization.find_expression(expression_id);
        }
        if (!expression || expression->vhdl == nullptr || width == 0U) {
            return std::nullopt;
        }
        const auto& source = *expression->vhdl;
        const auto effective_subtype = compiled_vhdl_link_subtype(
            specialization, subtype, scope);
        const auto packed = [&](const std::string_view bits,
                                const std::size_t expected_width)
            -> std::optional<PackedLogic4> {
            if (expected_width == 0U || bits.size() != expected_width) {
                return std::nullopt;
            }
            try {
                if (domain == frontend::ValueDomain::Logic9) {
                    return PackedLogic4::from_logic9_msb_string(bits);
                }
                if (!std::ranges::all_of(
                        bits, [](const char bit) {
                            return bit == '0' || bit == '1';
                        })) {
                    return std::nullopt;
                }
                return PackedLogic4::from_msb_string(bits);
            } catch (const std::invalid_argument&) {
                return std::nullopt;
            }
        };
        const bool unconstrained_builtin_logic9_vector
            = effective_subtype.builtin_type
                    == semantic::vhdl::BuiltinTypeIdentity::
                        ieee_std_logic_1164_std_logic_vector
            && effective_subtype.domain
                == semantic::vhdl::ValueDomain::logic9
            && domain == frontend::ValueDomain::Logic9
            && effective_subtype.constraints.empty();
        const auto actual_vector_width = [&](
            const semantic::DeclarationId declaration_id)
            -> std::optional<std::size_t> {
            if (!unconstrained_builtin_logic9_vector) {
                return std::nullopt;
            }
            const auto declaration
                = specialization.find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::constant
                || !declaration->vhdl->subtype) {
                return std::nullopt;
            }
            const auto actual_subtype = compiled_vhdl_link_subtype(
                specialization,
                *declaration->vhdl->subtype,
                declaration->vhdl->scope);
            if (actual_subtype.builtin_type != effective_subtype.builtin_type
                || actual_subtype.domain
                    != semantic::vhdl::ValueDomain::logic9
                || actual_subtype.constraints.empty()) {
                return std::nullopt;
            }
            const auto actual_layout = compiled_vhdl_named_signal_layout(
                specialization,
                *declaration->vhdl->subtype,
                declaration->vhdl->scope);
            if (!actual_layout
                || actual_layout->domain != frontend::ValueDomain::Logic9
                || actual_layout->width == 0U) {
                return std::nullopt;
            }
            return actual_layout->width;
        };
        const auto packed_formal = [&](const std::string_view bits) {
            return packed(bits, width);
        };
        if (source.kind == semantic::vhdl::ExpressionKind::name
            && source.referenced_name
            && source.referenced_name->selected) {
            const auto selected = *source.referenced_name->selected;
            const auto& actuals
                = specialization.specialization().actual_identities;
            const auto actual = std::ranges::find(
                actuals, selected,
                &semantic::SpecializedHirActualIdentity::declaration);
            if (actual != actuals.end()) {
                constexpr auto composite_prefix
                    = std::string_view { "vhdlcomposite-v1;" };
                constexpr auto value_marker
                    = std::string_view { ";value=" };
                const auto marker = actual->identity.rfind(value_marker);
                if (actual->identity.starts_with(composite_prefix)
                    && marker != std::string::npos) {
                    const auto encoded_value = std::string_view {
                        actual->identity
                    }.substr(marker + value_marker.size());
                    const auto value = packed_formal(encoded_value);
                    if (value) {
                        return value;
                    }
                }
                if (const auto value
                    = specialization.evaluate_integral_declaration(
                        selected)) {
                    return compiled_vhdl_integral_value(
                        *value, domain, width);
                }
            }
            const auto declaration = specialization.find_declaration(
                selected);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::constant
                && declaration->vhdl->initializer) {
                const auto initializer = specialization.find_expression(
                    *declaration->vhdl->initializer);
                const auto parameterless_function
                    = initializer && initializer->vhdl != nullptr
                            && initializer->vhdl->kind
                                == semantic::vhdl::ExpressionKind::name
                            && initializer->vhdl->referenced_name
                            && initializer->vhdl->referenced_name->selected
                    ? specialization.find_declaration(
                          *initializer->vhdl->referenced_name->selected)
                    : std::nullopt;
                const bool initializer_is_parameterless_function
                    = parameterless_function
                    && parameterless_function->vhdl != nullptr
                    && parameterless_function->vhdl->callable
                    && parameterless_function->vhdl->callable->function
                    && parameterless_function->vhdl->callable->formals.empty();
                if (initializer && initializer->vhdl != nullptr
                    && (initializer->vhdl->kind
                                == semantic::vhdl::ExpressionKind::call
                        || initializer_is_parameterless_function)) {
                    const auto value
                        = specialization
                              .evaluate_vhdl_packed_value_declaration(
                                  selected);
                    std::optional<PackedLogic4> projected;
                    if (value) {
                        auto expected_width = width;
                        if (unconstrained_builtin_logic9_vector) {
                            const auto distance = index_distance(
                                value->left_bound, value->right_bound);
                            const bool invalid_value_range
                                = distance
                                    == std::numeric_limits<std::uint64_t>::max()
                                || distance
                                    >= std::numeric_limits<std::size_t>::max();
                            if (invalid_value_range) {
                                return std::nullopt;
                            }
                            const auto value_width = static_cast<std::size_t>(
                                distance + 1U);
                            const auto actual_width
                                = actual_vector_width(selected);
                            if (!actual_width
                                || *actual_width != value_width
                                || value->bits.size() != value_width) {
                                return std::nullopt;
                            }
                            expected_width = value_width;
                        }
                        projected = packed(value->bits, expected_width);
                    }
                    return projected;
                }
            }
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->initializer
                && *declaration->vhdl->initializer != expression_id) {
                return compiled_vhdl_static_port_value(
                    specialization,
                    *declaration->vhdl->initializer,
                    subtype,
                    scope,
                    domain,
                    width);
            }
        }
        if (source.kind == semantic::vhdl::ExpressionKind::name) {
            const auto governed = resolve_vhdl_vital_named_constant(
                specialization, expression_id, width);
            if (governed && governed->value) {
                return governed->value;
            }
        }
        if (source.kind == semantic::vhdl::ExpressionKind::string_literal
            && source.decoded_string) {
            return packed_formal(*source.decoded_string);
        }
        if (source.kind == semantic::vhdl::ExpressionKind::logic_literal
            && source.text.size() == 3U && width == 1U) {
            const auto value = static_cast<char>(std::toupper(
                static_cast<unsigned char>(source.text[1])));
            return packed_formal(std::string_view { &value, 1U });
        }
        if (const auto integral
            = specialization.evaluate_integral_expression(expression_id)) {
            return compiled_vhdl_integral_value(
                *integral, domain, width);
        }
        if (source.kind != semantic::vhdl::ExpressionKind::aggregate
            || source.associations.empty()) {
            return std::nullopt;
        }
        const auto type_definition = [&]()
            -> const semantic::vhdl::TypeDefinition* {
            auto type_id = effective_subtype.type_mark.target;
            if (!type_id.valid()) {
                const auto type_name = compiled_vhdl_simple_name(
                    effective_subtype.type_mark.spelling);
                const auto found = std::ranges::find_if(
                    specialization.vhdl_types(),
                    [&](const semantic::vhdl::TypeDefinition& candidate) {
                        return compiled_vhdl_name_equal(
                            candidate.name, type_name);
                    });
                if (found != specialization.vhdl_types().end()) {
                    type_id = found->id;
                }
            }
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid()
                && visited.insert(type_id.value()).second) {
                const auto type = specialization.find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    return nullptr;
                }
                const auto* definition = type->vhdl;
                if (definition->form
                        != semantic::vhdl::TypeForm::subtype
                    && definition->form
                        != semantic::vhdl::TypeForm::alias) {
                    return definition;
                }
                type_id = definition->base.type_mark.target;
            }
            return nullptr;
        }();
        const auto scalar = [&](const semantic::ExpressionId value)
            -> std::optional<char> {
            const auto candidate = specialization.find_expression(value);
            if (!candidate || candidate->vhdl == nullptr) {
                return std::nullopt;
            }
            const auto& literal = *candidate->vhdl;
            if (literal.kind
                    == semantic::vhdl::ExpressionKind::logic_literal
                && literal.text.size() == 3U) {
                return static_cast<char>(std::toupper(
                    static_cast<unsigned char>(literal.text[1])));
            }
            const auto integral
                = specialization.evaluate_integral_expression(value);
            return integral == std::optional<std::int64_t> { 0 }
                ? std::optional<char> { '0' }
                : integral == std::optional<std::int64_t> { 1 }
                ? std::optional<char> { '1' }
                : std::nullopt;
        };
        if (type_definition != nullptr
            && type_definition->form
                == semantic::vhdl::TypeForm::record) {
            const auto& elements = type_definition->record_elements;
            if (elements.empty()) {
                return std::nullopt;
            }
            std::vector<std::optional<std::string>> values(
                elements.size());
            std::optional<semantic::ExpressionId> others;
            std::size_t positional { };
            const auto assign = [&](const std::size_t index,
                                    const semantic::ExpressionId value) {
                if (index >= values.size() || values[index]) {
                    return false;
                }
                const auto layout = compiled_vhdl_named_signal_layout(
                    specialization, elements[index].subtype, scope);
                if (!layout || layout->width == 0U) {
                    return false;
                }
                const auto evaluated = compiled_vhdl_static_port_value(
                    specialization,
                    value,
                    elements[index].subtype,
                    scope,
                    layout->domain,
                    layout->width);
                if (!evaluated) {
                    return false;
                }
                values[index] = evaluated->to_msb_string();
                return true;
            };
            for (const auto& association : source.associations) {
                auto choice = association.choice_spelling;
                std::ranges::transform(
                    choice, choice.begin(), [](const char character) {
                        return static_cast<char>(std::tolower(
                            static_cast<unsigned char>(character)));
                    });
                if (choice.find("others") != std::string::npos) {
                    if (others) {
                        return std::nullopt;
                    }
                    others = association.value;
                    continue;
                }
                std::vector<std::string> names;
                for (const auto choice_id : association.choices) {
                    const auto selected
                        = specialization.find_expression(choice_id);
                    if (selected && selected->vhdl != nullptr
                        && selected->vhdl->kind
                            == semantic::vhdl::ExpressionKind::name) {
                        names.push_back(selected->vhdl->text);
                    }
                }
                if (names.empty() && !choice.empty()) {
                    auto remaining = std::string_view { choice };
                    while (!remaining.empty()) {
                        const auto separator = remaining.find('|');
                        auto name = remaining.substr(0U, separator);
                        while (!name.empty()
                            && std::isspace(static_cast<unsigned char>(
                                   name.front())) != 0) {
                            name.remove_prefix(1U);
                        }
                        while (!name.empty()
                            && std::isspace(static_cast<unsigned char>(
                                   name.back())) != 0) {
                            name.remove_suffix(1U);
                        }
                        if (!name.empty()) {
                            names.emplace_back(name);
                        }
                        if (separator == std::string_view::npos) {
                            break;
                        }
                        remaining.remove_prefix(separator + 1U);
                    }
                }
                if (names.empty()) {
                    if (!association.choices.empty()
                        || !assign(positional++, association.value)) {
                        return std::nullopt;
                    }
                    continue;
                }
                for (const auto& name : names) {
                    const auto element = std::ranges::find_if(
                        elements, [&](const auto& candidate) {
                            return compiled_vhdl_name_equal(
                                candidate.name, name);
                        });
                    if (element == elements.end()
                        || !assign(
                            static_cast<std::size_t>(
                                std::distance(elements.begin(), element)),
                            association.value)) {
                        return std::nullopt;
                    }
                }
            }
            if (others) {
                for (std::size_t index { };
                    index < values.size(); ++index) {
                    if (!values[index]
                        && !assign(index, *others)) {
                        return std::nullopt;
                    }
                }
            }
            std::string bits;
            for (const auto& value : values) {
                if (!value) {
                    return std::nullopt;
                }
                bits += *value;
            }
            return packed_formal(bits);
        }
        auto left = std::optional<std::int64_t> {
            static_cast<std::int64_t>(width - 1U)
        };
        auto right = std::optional<std::int64_t> { 0 };
        const semantic::vhdl::RangeConstraint* subtype_range
            = effective_subtype.constraints.empty()
            ? nullptr
            : &effective_subtype.constraints.front();
        if (subtype_range == nullptr && type_definition != nullptr
            && type_definition->form
                == semantic::vhdl::TypeForm::array
            && !type_definition->array_dimensions.empty()
            && type_definition->array_dimensions.front().constraint) {
            subtype_range = &*type_definition->array_dimensions.front()
                                  .constraint;
        }
        if (subtype_range != nullptr) {
            const auto& range = *subtype_range;
            left = range.left
                ? range.left
                : range.left_expression
                ? specialization.evaluate_integral_expression(
                      *range.left_expression)
                : std::nullopt;
            right = range.right
                ? range.right
                : range.right_expression
                ? specialization.evaluate_integral_expression(
                      *range.right_expression)
                : std::nullopt;
        }
        if (!left || !right
            || index_distance(*left, *right) + 1U != width) {
            return std::nullopt;
        }
        std::string bits(width, '\0');
        std::optional<char> others;
        std::size_t positional { };
        const auto assign_index = [&](const std::int64_t index,
                                      const char value) {
            const auto low = std::min(*left, *right);
            const auto high = std::max(*left, *right);
            if (index < low || index > high) {
                return false;
            }
            const auto offset = index_distance(index, *right);
            if (offset >= width) {
                return false;
            }
            const auto position = width - 1U
                - static_cast<std::size_t>(offset);
            if (bits[position] != '\0') {
                return false;
            }
            bits[position] = value;
            return true;
        };
        for (const auto& association : source.associations) {
            const auto value = scalar(association.value);
            if (!value) {
                return std::nullopt;
            }
            auto choice_spelling = association.choice_spelling;
            std::ranges::transform(
                choice_spelling, choice_spelling.begin(),
                [](const char character) {
                    return static_cast<char>(std::tolower(
                        static_cast<unsigned char>(character)));
                });
            if (choice_spelling.find("others") != std::string::npos) {
                if (others) {
                    return std::nullopt;
                }
                others = *value;
                continue;
            }
            if (association.choices.empty()) {
                if (positional >= width || bits[positional] != '\0') {
                    return std::nullopt;
                }
                bits[positional++] = *value;
                continue;
            }
            for (const auto choice : association.choices) {
                const auto index
                    = specialization.evaluate_integral_expression(choice);
                if (!index || !assign_index(*index, *value)) {
                    return std::nullopt;
                }
            }
        }
        for (auto& bit : bits) {
            if (bit == '\0' && others) {
                bit = *others;
            }
            if (bit == '\0') {
                return std::nullopt;
            }
        }
        return packed_formal(bits);
    }

    std::optional<PackedLogic4>
    compiled_vhdl_static_packed_array_initializer(
        const semantic::SpecializedHirUnit& specialization,
        semantic::ExpressionId expression_id,
        const semantic::vhdl::SubtypeIndication& target_subtype,
        const semantic::ScopeId target_scope,
        const PackedTypeMetadata& target_type,
        const std::size_t width)
    {
        constexpr std::uint64_t maximum_work_units
            = 64U * 1024U * 1024U;
        const auto expression = specialization.find_expression(
            expression_id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
        }
        auto selected_expression_id = expression_id;
        auto selected_expression = expression;
        constexpr auto qualified_prefix
            = std::string_view { "@vhdl-qualified:" };
        if (selected_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
            && selected_expression->vhdl->text.starts_with(
                qualified_prefix)
              && selected_expression->vhdl->operands.size() == 1U) {
            selected_expression_id
                = selected_expression->vhdl->operands.front();
            selected_expression = specialization.find_expression(
                selected_expression_id);
        }
        if (!selected_expression || selected_expression->vhdl == nullptr
            || (selected_expression->vhdl->kind
                    != semantic::vhdl::ExpressionKind::name
                && selected_expression->vhdl->kind
                      != semantic::vhdl::ExpressionKind::call)) {
            return std::nullopt;
        }
        auto reference = selected_expression->vhdl->referenced_name
            ? *selected_expression->vhdl->referenced_name
            : semantic::vhdl::Name { };
        if (reference.spelling.empty()) {
            reference.spelling = selected_expression->vhdl->text;
        }
        if (reference.canonical.empty()) {
            reference.canonical = reference.spelling;
        }
        reference.source = selected_expression->vhdl->source;
        const semantic::CompiledDesignResolver resolver { specialization };
        std::optional<semantic::DeclarationId> selected_id;
        if (selected_expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::name) {
            selected_id = reference.selected;
            if (!selected_id) {
                const auto constant = resolver.resolve_vhdl(
                    reference, selected_expression->vhdl->scope,
                    [](const semantic::CompiledDeclarationView& candidate) {
                        return candidate.vhdl != nullptr
                            && (candidate.vhdl->form
                                    == semantic::vhdl::DeclarationForm::constant
                                || candidate.vhdl->form
                                    == semantic::vhdl::DeclarationForm::
                                        generic_constant);
                    }).unique();
                selected_id = constant;
            }
        }
        const auto callable = resolver.resolve_vhdl_callables(
            reference, selected_expression->vhdl->scope).unique();
        if (callable
            && (!reference.selected
                || *reference.selected == callable->key
                || *reference.selected == callable->body)
            && (reference.overloads.empty()
                || std::ranges::find(reference.overloads, callable->key)
                    != reference.overloads.end()
                || std::ranges::find(reference.overloads, callable->body)
                    != reference.overloads.end())) {
            selected_id = callable->body;
        }
        if (!selected_id) {
            return std::nullopt;
        }
        if (!reference.overloads.empty()
            && std::ranges::find(reference.overloads, *selected_id)
                == reference.overloads.end()) {
            return std::nullopt;
        }
        const auto selected
            = specialization.find_declaration(*selected_id);
        if (!selected || selected->vhdl == nullptr) {
            return std::nullopt;
        }

        std::optional<semantic::vhdl::SubtypeIndication> source_subtype;
        std::optional<semantic::SpecializedHirVhdlPackedArrayValue> value;
        const auto& declaration = *selected->vhdl;
        if (selected_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::name
            && (declaration.form
                    == semantic::vhdl::DeclarationForm::constant
                || declaration.form
                    == semantic::vhdl::DeclarationForm::generic_constant)
            && declaration.subtype) {
            source_subtype = declaration.subtype;
            value = specialization.evaluate_vhdl_packed_array_declaration(
                *selected_id);
        } else if (declaration.form
                    == semantic::vhdl::DeclarationForm::function
            && declaration.callable && declaration.callable->function
            && declaration.callable->pure
            && declaration.callable->formals.empty()
            && declaration.callable->return_type
            && selected_expression->vhdl->operands.empty()) {
            source_subtype = declaration.callable->return_type;
            value = specialization.evaluate_vhdl_packed_array_expression(
                selected_expression_id);
        }
        if (!source_subtype || !value) {
            return std::nullopt;
        }

        const auto canonical_array_type = [&] (
            const semantic::vhdl::SubtypeIndication& root,
            const semantic::ScopeId scope) -> std::optional<semantic::TypeId> {
            auto current = compiled_vhdl_link_subtype(
                specialization, root, scope);
            auto type_id = current.type_mark.target;
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid()
                && visited.insert(type_id.value()).second) {
                const auto type = specialization.find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                const auto& definition = *type->vhdl;
                if (definition.form
                    == semantic::vhdl::TypeForm::array) {
                    return type_id;
                }
                if (definition.form
                        != semantic::vhdl::TypeForm::subtype
                    && definition.form
                        != semantic::vhdl::TypeForm::alias) {
                    return std::nullopt;
                }
                current = compiled_vhdl_link_subtype(
                    specialization, definition.base, scope);
                type_id = current.type_mark.target;
            }
            return std::nullopt;
        };
        const auto target_array_type = canonical_array_type(
            target_subtype, target_scope);
        const auto source_array_type = canonical_array_type(
            *source_subtype, declaration.scope);
        if (!target_array_type || !source_array_type
            || *target_array_type != *source_array_type) {
            return std::nullopt;
        }
        if (!target_type.vhdl_array
            || target_type.vhdl_array->dimensions.size() != 1U
            || target_type.vhdl_array->element_types.size() != 1U
            || !target_type.vhdl_array->flat_width
            || !target_type.vhdl_array->dimensions.front().range
            || target_type.vhdl_array->dimensions.front().null
            || target_type.vhdl_array->unconstrained) {
            return std::nullopt;
        }
        const auto source_type = compiled_vhdl_signal_type(
            specialization, *source_subtype, declaration.scope);
        if (!source_type || !source_type->vhdl_array
            || source_type->vhdl_array->dimensions.size() != 1U
            || source_type->vhdl_array->element_types.size() != 1U
            || !source_type->vhdl_array->flat_width
            || !source_type->vhdl_array->dimensions.front().range
            || source_type->vhdl_array->dimensions.front().null
            || source_type->vhdl_array->unconstrained) {
            return std::nullopt;
        }
        const auto& target_array = *target_type.vhdl_array;
        const auto& source_array = *source_type->vhdl_array;
        const auto& target_dimension = target_array.dimensions.front();
        const auto& source_dimension = source_array.dimensions.front();
        const auto same_range = [](const frontend::IntegerRange& left,
                                   const frontend::IntegerRange& right) {
            return left.left == right.left
                && left.right == right.right
                && left.descending == right.descending;
        };
        if (!same_range(*target_dimension.range,
                *source_dimension.range)
            || value->left_bound != target_dimension.range->left
            || value->right_bound != target_dimension.range->right
            || value->element_domain
                != semantic::vhdl::ValueDomain::logic9
            || target_array.element_domain
                != frontend::ValueDomain::Logic9
            || source_array.element_domain
                != frontend::ValueDomain::Logic9) {
            return std::nullopt;
        }
        const auto& target_element = target_array.element_types.front();
        const auto& source_element = source_array.element_types.front();
        const auto element_width = target_element.width();
        const auto source_element_width = source_element.width();
        if (target_element.domain != frontend::ValueDomain::Logic9
            || source_element.domain != frontend::ValueDomain::Logic9
            || !target_element.packed_range
            || !source_element.packed_range
            || target_element.width() != source_element.width()
            || target_element.packed_range->left
                != source_element.packed_range->left
            || target_element.packed_range->right
                != source_element.packed_range->right
            || target_element.packed_range->descending
                != source_element.packed_range->descending
            || !element_width || !source_element_width
            || *element_width == 0U
            || *element_width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        const auto outer_distance = index_distance(
            target_dimension.range->left,
            target_dimension.range->right);
        if (outer_distance == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        const auto element_count = outer_distance + 1U;
        if (element_count != value->elements.size()
            || element_count > std::numeric_limits<std::uint32_t>::max()
            || element_count
                > std::numeric_limits<std::uint64_t>::max()
                    / *element_width) {
            return std::nullopt;
        }
        const auto total_width
            = element_count * *element_width;
        if (total_width != width
            || total_width != *target_array.flat_width
            || total_width != *source_array.flat_width
            || total_width > std::numeric_limits<std::uint32_t>::max()
            || total_width > maximum_work_units
            || element_count > maximum_work_units - total_width) {
            return std::nullopt;
        }
        std::string bits;
        bits.reserve(static_cast<std::size_t>(total_width));
        for (const auto& element : value->elements) {
            if (element.left_bound != target_element.packed_range->left
                || element.right_bound != target_element.packed_range->right
                || element.bits.size() != *element_width) {
                return std::nullopt;
            }
            bits += element.bits;
        }
        if (bits.size() != total_width) {
            return std::nullopt;
        }
        try {
            return PackedLogic4::from_logic9_msb_string(bits);
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }

    std::string compiled_instance_path(
        const semantic::CompiledDesign& compiled,
        const semantic::ScopeId unit_scope,
        const semantic::ScopeId instance_scope,
        const std::string_view parent_path,
        const std::string_view instance_name)
    {
        std::vector<std::string_view> scopes;
        auto current = std::optional { instance_scope };
        const auto& semantic_scopes = compiled.semantics.scopes();
        while (current && *current != unit_scope
            && current->valid() && current->value() < semantic_scopes.size()) {
            const auto& scope = semantic_scopes[current->value()];
            if (scope.id != *current) {
                scopes.clear();
                break;
            }
            if (!scope.name.empty()) {
                scopes.push_back(scope.name);
            }
            current = scope.parent;
        }
        if (!current || *current != unit_scope) {
            scopes.clear();
        }
        std::string result { parent_path };
        for (auto iterator = scopes.rbegin(); iterator != scopes.rend();
            ++iterator) {
            result += ".";
            result += *iterator;
        }
        result += ".";
        result += instance_name;
        return result;
    }

    struct CompiledSpecializationFailure {
        std::string code;
        std::string message;
        semantic::SourceSpanId source;
    };

    std::optional<std::string> compiled_systemverilog_default_error(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::sv::Declaration& declaration)
    {
        if (!declaration.initializer) {
            return std::nullopt;
        }
        const auto expression = specialization.find_expression(
            *declaration.initializer);
        if (expression && expression->systemverilog != nullptr) {
            const auto& record = *expression->systemverilog;
            if (record.kind == semantic::sv::ExpressionKind::call
                && record.text == "$clog2"
                && record.operands.size() == 1U) {
                const auto operand = specialization.evaluate_integral_expression(
                    record.operands.front());
                if (operand && *operand < 0) {
                    return std::string {
                        "$clog2 requires a nonnegative integral argument"
                    };
                }
            }
            if (record.kind == semantic::sv::ExpressionKind::binary
                && (record.text == "/" || record.text == "%")
                && record.operands.size() == 2U) {
                const auto divisor = specialization.evaluate_integral_expression(
                    record.operands.back());
                if (divisor && *divisor == 0) {
                    return std::string {
                        "division by zero in parameter default"
                    };
                }
            }
        }
        return std::nullopt;
    }

    std::string compiled_vhdl_integral_identity(
        const semantic::vhdl::Declaration& declaration,
        const std::int64_t value)
    {
        const auto* subtype = declaration.subtype
            ? &*declaration.subtype
            : nullptr;
        const auto width = subtype
            ? compiled_vhdl_signal_width(*subtype).value_or(0U)
            : 0U;
        auto result = std::string { "vhdlconst-v1;domain=" }
            + std::to_string(static_cast<unsigned>(subtype
                    ? compiled_value_domain(subtype->domain)
                    : frontend::ValueDomain::Unknown))
            + ";width=" + std::to_string(width)
            + ";signed="
            + (subtype && subtype->signed_value ? "1" : "0")
            + ";type="
            + (subtype ? subtype->type_mark.spelling : std::string { })
            + ";nominal="
            + (subtype
                    ? vhdl_configuration_detail::configuration_canonical_name(
                          subtype->type_mark.spelling)
                    : std::string { });
        if (subtype) {
            if (const auto packed = compiled_packed_range(*subtype)) {
                result += ";packed=" + std::to_string(packed->left) + ":"
                    + std::to_string(packed->right) + ":"
                    + (packed->descending ? "down" : "up");
            }
            if (const auto integer = compiled_integer_range(*subtype)) {
                result += ";integer=" + std::to_string(integer->left) + ":"
                    + std::to_string(integer->right);
            }
        }
        result += ";value=" + std::to_string(value);
        return result;
    }

    std::optional<frontend::IntegerRange>
    compiled_vhdl_specialization_integer_constraint(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& subtype)
    {
        std::unordered_set<std::uint32_t> visiting;
        const auto resolve = [&](const auto& self,
                                 const semantic::vhdl::SubtypeIndication& input)
            -> std::optional<frontend::IntegerRange> {
            if (const auto range = compiled_integer_range(input)) {
                return range;
            }
            const auto constraint = std::ranges::find_if(
                input.constraints,
                [](const auto& range) {
                    return range.kind
                            == semantic::vhdl::RangeKind::integer
                        || range.kind
                            == semantic::vhdl::RangeKind::enumeration
                        || range.kind
                            == semantic::vhdl::RangeKind::discrete;
                });
            if (constraint != input.constraints.end()) {
                const auto left = constraint->left
                    ? constraint->left
                    : constraint->left_expression
                    ? specialization.evaluate_integral_expression(
                          *constraint->left_expression)
                    : std::nullopt;
                const auto right = constraint->right
                    ? constraint->right
                    : constraint->right_expression
                    ? specialization.evaluate_integral_expression(
                          *constraint->right_expression)
                    : std::nullopt;
                if (!left || !right) {
                    return std::nullopt;
                }
                return frontend::IntegerRange {
                    *left,
                    *right,
                    constraint->descending,
                };
            }
            const auto parts = compiled_vhdl_name_parts(
                input.type_mark.spelling);
            const auto name = parts.empty()
                ? std::string_view { input.type_mark.spelling }
                : std::string_view { parts.back() };
            if (compiled_vhdl_name_equal(name, "natural")) {
                return frontend::IntegerRange {
                    0, std::numeric_limits<std::int64_t>::max(), false
                };
            }
            if (compiled_vhdl_name_equal(name, "positive")) {
                return frontend::IntegerRange {
                    1, std::numeric_limits<std::int64_t>::max(), false
                };
            }
            if (!input.type_mark.target.valid()
                || !visiting.insert(input.type_mark.target.value()).second) {
                return std::nullopt;
            }
            const auto definition = specialization.find_type(
                input.type_mark.target);
            if (!definition || definition->vhdl == nullptr) {
                return std::nullopt;
            }
            auto base = definition->vhdl->base;
            if (!input.constraints.empty()) {
                base.constraints = input.constraints;
                base.unconstrained = false;
            } else if (definition->vhdl->scalar_range) {
                base.constraints = { *definition->vhdl->scalar_range };
                base.unconstrained = false;
            }
            return self(self, base);
        };
        return resolve(resolve, subtype);
    }

    bool compiled_vhdl_specialization_value_satisfies_subtype(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& subtype,
        const std::int64_t value)
    {
        if (subtype.domain == semantic::vhdl::ValueDomain::boolean) {
            return value == 0 || value == 1;
        }
        const auto range = compiled_vhdl_specialization_integer_constraint(
            specialization, subtype);
        return !range
            || (value >= std::min(range->left, range->right)
                && value <= std::max(range->left, range->right));
    }

    struct CompiledVhdlSpecializationActual {
        semantic::SpecializedHirActualIdentity actual;
        bool requires_static_value { };
    };

    std::optional<PackedLogic4> compiled_vhdl_packed_aggregate_value(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::ExpressionId expression_id,
        const semantic::vhdl::SubtypeIndication& contextual_subtype,
        const semantic::ScopeId use_scope)
    {
        const auto expression = specialization.find_expression(expression_id);
        if (!expression || expression->vhdl == nullptr
            || expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::aggregate
            || expression->vhdl->associations.empty()) {
            return std::nullopt;
        }
        const auto effective = semantic::CompiledDesignResolver {
            specialization
        }.effective_vhdl_subtype(contextual_subtype, use_scope);
        const auto layout = effective
            ? compiled_vhdl_named_signal_layout(
                  specialization, *effective, use_scope)
            : std::nullopt;
        if (!effective || !layout || layout->width == 0U) {
            return std::nullopt;
        }

        const semantic::vhdl::TypeDefinition* definition { };
        auto type_id = effective->type_mark.target;
        std::unordered_set<std::uint32_t> visited;
        while (type_id.valid() && visited.insert(type_id.value()).second) {
            const auto type = specialization.find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                break;
            }
            definition = type->vhdl;
            if ((definition->form
                        != semantic::vhdl::TypeForm::subtype
                    && definition->form
                        != semantic::vhdl::TypeForm::alias)
                || !definition->base.type_mark.target.valid()) {
                break;
            }
            type_id = definition->base.type_mark.target;
        }
        std::optional<semantic::vhdl::SubtypeIndication> element;
        if (definition != nullptr
            && definition->form == semantic::vhdl::TypeForm::array
            && definition->element_subtype) {
            element = semantic::CompiledDesignResolver { specialization }
                .effective_vhdl_subtype(
                    *definition->element_subtype, use_scope);
        }
        if (!element) {
            auto name = compiled_vhdl_simple_name(
                effective->type_mark.spelling);
            const bool bit_vector = compiled_vhdl_name_equal(
                name, "bit_vector");
            const bool logic_vector = compiled_vhdl_name_equal(
                    name, "std_logic_vector")
                || compiled_vhdl_name_equal(name, "std_ulogic_vector")
                || compiled_vhdl_name_equal(name, "signed")
                || compiled_vhdl_name_equal(name, "unsigned");
            if (bit_vector || logic_vector) {
                element.emplace();
                element->type_mark.spelling = bit_vector
                    ? "bit"
                    : "std_logic";
                element->domain = bit_vector
                    ? semantic::vhdl::ValueDomain::bit2
                    : semantic::vhdl::ValueDomain::logic9;
                element->executable_width = 1U;
            }
        }
        const auto element_layout = element
            ? compiled_vhdl_named_signal_layout(
                  specialization, *element, use_scope)
            : std::nullopt;
        if (!element_layout || element_layout->width == 0U
            || layout->width % element_layout->width != 0U) {
            return std::nullopt;
        }
        const auto element_count = layout->width / element_layout->width;
        if (element_count == 0U) {
            return std::nullopt;
        }
        std::optional<semantic::vhdl::RangeConstraint> range;
        if (!effective->constraints.empty()) {
            range = effective->constraints.front();
        } else if (definition != nullptr
            && !definition->array_dimensions.empty()
            && definition->array_dimensions.front().constraint) {
            range = definition->array_dimensions.front().constraint;
        }
        const auto boundary = [&](const std::optional<std::int64_t> folded,
                                  const std::optional<semantic::ExpressionId>
                                      residual) {
            return folded ? folded
                : residual
                ? specialization.evaluate_integral_expression(*residual)
                : std::nullopt;
        };
        auto left = range
            ? boundary(range->left, range->left_expression)
            : std::nullopt;
        auto right = range
            ? boundary(range->right, range->right_expression)
            : std::nullopt;
        if (!left || !right) {
            left = static_cast<std::int64_t>(element_count - 1U);
            right = 0;
        }
        if (index_distance(*left, *right) + 1U != element_count) {
            return std::nullopt;
        }

        std::vector<std::optional<PackedLogic4>> values(element_count);
        std::optional<semantic::ExpressionId> others;
        std::size_t positional { };
        const auto value = [&](const semantic::ExpressionId id) {
            return compiled_vhdl_static_port_value(
                specialization,
                id,
                *element,
                use_scope,
                element_layout->domain,
                element_layout->width);
        };
        const auto assign_offset = [&](const std::size_t offset,
                                       const semantic::ExpressionId id) {
            if (offset >= values.size() || values[offset]) {
                return false;
            }
            values[offset] = value(id);
            return values[offset].has_value();
        };
        const auto assign_index = [&](const std::int64_t index,
                                      const semantic::ExpressionId id) {
            if (index < std::min(*left, *right)
                || index > std::max(*left, *right)) {
                return false;
            }
            return assign_offset(
                static_cast<std::size_t>(index_distance(index, *right)), id);
        };
        for (const auto& association : expression->vhdl->associations) {
            auto spelling = association.choice_spelling;
            std::ranges::transform(
                spelling, spelling.begin(), [](const char character) {
                    return static_cast<char>(std::tolower(
                        static_cast<unsigned char>(character)));
                });
            spelling.erase(std::remove_if(
                spelling.begin(), spelling.end(), [](const char character) {
                    return std::isspace(
                               static_cast<unsigned char>(character))
                        != 0;
                }), spelling.end());
            if (spelling.empty() && association.choices.empty()) {
                if (positional >= element_count
                    || !assign_offset(
                        element_count - 1U - positional,
                        association.value)) {
                    return std::nullopt;
                }
                ++positional;
                continue;
            }
            if (spelling == "others") {
                if (others) {
                    return std::nullopt;
                }
                others = association.value;
                continue;
            }
            for (const auto choice_id : association.choices) {
                const auto choice = specialization.find_expression(choice_id);
                if (choice && choice->vhdl != nullptr
                    && choice->vhdl->kind
                        == semantic::vhdl::ExpressionKind::name
                    && compiled_vhdl_name_equal(
                        choice->vhdl->text, "others")) {
                    if (others || association.choices.size() != 1U) {
                        return std::nullopt;
                    }
                    others = association.value;
                    continue;
                }
                if (choice && choice->vhdl != nullptr
                    && choice->vhdl->kind
                        == semantic::vhdl::ExpressionKind::binary
                    && (choice->vhdl->text == "to"
                        || choice->vhdl->text == "downto")
                    && choice->vhdl->operands.size() == 2U) {
                    const auto first
                        = specialization.evaluate_integral_expression(
                            choice->vhdl->operands[0]);
                    const auto last
                        = specialization.evaluate_integral_expression(
                            choice->vhdl->operands[1]);
                    if (!first || !last) {
                        return std::nullopt;
                    }
                    const auto descending = choice->vhdl->text == "downto";
                    if ((descending && *first < *last)
                        || (!descending && *first > *last)) {
                        continue;
                    }
                    auto index = *first;
                    for (;;) {
                        if (!assign_index(index, association.value)) {
                            return std::nullopt;
                        }
                        if (index == *last) {
                            break;
                        }
                        index += descending ? -1 : 1;
                    }
                    continue;
                }
                const auto index
                    = specialization.evaluate_integral_expression(choice_id);
                if (!index || !assign_index(*index, association.value)) {
                    return std::nullopt;
                }
            }
        }
        if (others) {
            for (std::size_t offset { }; offset < values.size(); ++offset) {
                if (!values[offset] && !assign_offset(offset, *others)) {
                    return std::nullopt;
                }
            }
        }
        if (std::ranges::any_of(values,
                [](const auto& item) { return !item.has_value(); })) {
            return std::nullopt;
        }
        auto packed = PackedLogic4 { layout->width, Logic4::zero };
        if (layout->domain == frontend::ValueDomain::Logic9) {
            packed.fill(runtime::Logic9::zero);
        }
        for (std::size_t offset { }; offset < values.size(); ++offset) {
            packed.insert_bits(
                *values[offset], offset * element_layout->width);
        }
        return packed;
    }

    std::optional<PackedLogic4> compiled_vhdl_static_composite_value(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::ExpressionId expression,
        const semantic::vhdl::SubtypeIndication& subtype,
        const semantic::ScopeId scope)
    {
        if (const auto aggregate = compiled_vhdl_packed_aggregate_value(
                specialization, expression, subtype, scope)) {
            return aggregate;
        }
        const auto effective = semantic::CompiledDesignResolver {
            specialization
        }.effective_vhdl_subtype(subtype, scope);
        const auto layout = effective
            ? compiled_vhdl_named_signal_layout(
                  specialization, *effective, scope)
            : std::nullopt;
        if (!effective || !layout || layout->width == 0U) {
            return std::nullopt;
        }
        return compiled_vhdl_static_port_value(
            specialization,
            expression,
            *effective,
            scope,
            layout->domain,
            layout->width);
    }

    std::string compiled_vhdl_composite_identity(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Declaration& declaration,
        const PackedLogic4& value)
    {
        const auto* subtype = declaration.subtype
            ? &*declaration.subtype
            : nullptr;
        const auto effective = subtype
            ? semantic::CompiledDesignResolver { specialization }
                  .effective_vhdl_subtype(*subtype, declaration.scope)
            : std::nullopt;
        const auto* identity_subtype = effective
            ? &*effective
            : subtype;
        auto result = std::string { "vhdlcomposite-v1;domain=" }
            + std::to_string(static_cast<unsigned>(identity_subtype
                    ? compiled_value_domain(identity_subtype->domain)
                    : frontend::ValueDomain::Unknown))
            + ";width=" + std::to_string(value.width())
            + ";signed="
            + (identity_subtype && identity_subtype->signed_value ? "1" : "0")
            + ";type="
            + (identity_subtype
                    ? identity_subtype->type_mark.spelling
                    : std::string { })
            + ";nominal="
            + (identity_subtype
                    ? vhdl_configuration_detail::configuration_canonical_name(
                          identity_subtype->type_mark.spelling)
                    : std::string { });
        result += ";value=" + value.to_msb_string();
        return result;
    }

    std::optional<ContainerType> compiled_container_bridge_type(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::sv::TypeReference& reference)
    {
        const auto boundary = [&](const std::optional<std::int64_t> value,
                                  const std::optional<semantic::ExpressionId>
                                      expression) {
            return expression
                ? specialization.evaluate_integral_expression(*expression)
                : value;
        };
        const semantic::CompiledDesignResolver resolver { specialization };
        std::unordered_set<std::uint32_t> visiting;
        const auto profile = [&](const auto& self,
                                 const semantic::sv::TypeReference& candidate,
                                 const bool box_leaf)
            -> std::optional<ContainerType> {
            const auto effective = resolver.effective_systemverilog_type(
                candidate, specialization.scope());
            if (effective && *effective != candidate) {
                return self(self, *effective, box_leaf);
            }
            if (candidate.container_form) {
                if (*candidate.container_form
                        != semantic::sv::TypeForm::static_array
                    || candidate.container_element_types.size() > 1U) {
                    return std::nullopt;
                }
                auto leaf = candidate.container_element_types.empty()
                    ? candidate
                    : candidate.container_element_types.front();
                if (candidate.container_element_types.empty()) {
                    leaf.container_form.reset();
                    leaf.queue_maximum.reset();
                    leaf.associative_index.reset();
                    leaf.unpacked_dimensions.clear();
                    leaf.container_element_types.clear();
                }
                auto element = self(self, leaf, false);
                if (!element) {
                    return std::nullopt;
                }
                ContainerType result;
                if (leaf.container_form) {
                    result.element_kind = ContainerElementKind::Container;
                    result.element_types.push_back(std::move(*element));
                } else {
                    result.element_kind = element->element_kind;
                    result.scalar_kind = element->scalar_kind;
                    result.element_width = element->element_width;
                    result.two_state = element->two_state;
                    result.signed_elements = element->signed_elements;
                    result.union_aggregate = element->union_aggregate;
                    result.element_nominal_type
                        = element->element_nominal_type;
                    result.element_types
                        = std::move(element->element_types);
                    result.member_names
                        = std::move(element->member_names);
                }
                result.fixed = true;
                for (const auto& dimension :
                    candidate.unpacked_dimensions) {
                    const auto left = boundary(
                        dimension.left, dimension.left_expression);
                    const auto right = boundary(
                        dimension.right, dimension.right_expression);
                    if (!left || !right
                        || *left
                            < std::numeric_limits<std::int32_t>::min()
                        || *left
                            > std::numeric_limits<std::int32_t>::max()
                        || *right
                            < std::numeric_limits<std::int32_t>::min()
                        || *right
                            > std::numeric_limits<std::int32_t>::max()) {
                        return std::nullopt;
                    }
                    result.dimensions.emplace_back(
                        static_cast<std::int32_t>(*left),
                        static_cast<std::int32_t>(*right));
                }
                if (result.dimensions.empty()) {
                    return std::nullopt;
                }
                result.index_left = result.dimensions.front().first;
                result.index_right = result.dimensions.front().second;
                return result;
            }

            ContainerType result;
            result.element_nominal_type = candidate.target.spelling;
            if (candidate.target.target.valid()) {
                if (!visiting.insert(candidate.target.target.value()).second) {
                    return std::nullopt;
                }
                const auto definition = specialization.find_type(
                    candidate.target.target);
                if (!definition
                    || definition->systemverilog == nullptr) {
                    visiting.erase(candidate.target.target.value());
                    return std::nullopt;
                }
                const auto& type = *definition->systemverilog;
                if (type.form
                        == semantic::sv::TypeForm::unpacked_structure
                    || type.form
                        == semantic::sv::TypeForm::unpacked_union) {
                    result.element_kind = ContainerElementKind::Aggregate;
                    result.union_aggregate = type.form
                        == semantic::sv::TypeForm::unpacked_union;
                    result.aggregate_value = box_leaf;
                    result.element_nominal_type = type.name.empty()
                        ? candidate.target.spelling
                        : type.name;
                    for (const auto& member : type.members) {
                        auto member_type = self(self, member.type, true);
                        if (!member_type) {
                            visiting.erase(
                                candidate.target.target.value());
                            return std::nullopt;
                        }
                        result.member_names.push_back(member.name);
                        result.element_types.push_back(
                            std::move(*member_type));
                    }
                    visiting.erase(candidate.target.target.value());
                    return result;
                }
                if (type.form
                        == semantic::sv::TypeForm::packed_structure
                    || type.form
                        == semantic::sv::TypeForm::packed_union) {
                    std::uint64_t width { };
                    bool two_state = !candidate.four_state;
                    for (const auto& member : type.members) {
                        const auto member_type = self(
                            self, member.type, false);
                        if (!member_type
                            || member_type->element_width == 0U) {
                            visiting.erase(
                                candidate.target.target.value());
                            return std::nullopt;
                        }
                        if (type.form
                            == semantic::sv::TypeForm::packed_union) {
                            width = std::max<std::uint64_t>(
                                width, member_type->element_width);
                        } else if (width
                            > std::numeric_limits<std::uint64_t>::max()
                                - member_type->element_width) {
                            visiting.erase(
                                candidate.target.target.value());
                            return std::nullopt;
                        } else {
                            width += member_type->element_width;
                        }
                        two_state = two_state
                            && member_type->two_state;
                    }
                    visiting.erase(candidate.target.target.value());
                    if (width == 0U
                        || width
                            > std::numeric_limits<std::uint32_t>::max()) {
                        return std::nullopt;
                    }
                    result.element_kind = ContainerElementKind::Packed;
                    result.element_width
                        = static_cast<std::uint32_t>(width);
                    result.two_state = two_state;
                    result.signed_elements = candidate.signed_value;
                } else {
                    auto base = self(self, type.base, box_leaf);
                    visiting.erase(candidate.target.target.value());
                    return base;
                }
            } else {
                std::optional<std::uint64_t> width
                    = candidate.executable_width;
                if (!width && candidate.packed_range) {
                    const auto left = boundary(
                        candidate.packed_range->left,
                        candidate.packed_range->left_expression);
                    const auto right = boundary(
                        candidate.packed_range->right,
                        candidate.packed_range->right_expression);
                    if (left && right) {
                        width = index_distance(*left, *right) + 1U;
                    }
                }
                if (!width || *width == 0U
                    || *width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                result.element_kind = ContainerElementKind::Packed;
                result.element_width
                    = static_cast<std::uint32_t>(*width);
                result.two_state = !candidate.four_state;
                result.signed_elements = candidate.signed_value;
            }
            if (box_leaf) {
                result.fixed = true;
                result.index_left = 0;
                result.index_right = 0;
                result.dimensions.emplace_back(0, 0);
            }
            return result;
        };

        auto result = profile(profile, reference, false);
        if (!result) {
            return std::nullopt;
        }
        try {
            static_cast<void>(default_container_value(*result));
        } catch (const std::exception&) {
            return std::nullopt;
        }
        return result;
    }

    CompiledVhdlSpecializationActual compiled_vhdl_specialization_actual(
        const semantic::CompiledDesign& compiled,
        const semantic::SpecializedHirUnit& parent,
        const semantic::SpecializedHirAssociationBinding& binding)
    {
        auto identity = binding.identity;
        auto actual_declaration = binding.actual_declaration;
        if (actual_declaration) {
            const auto forwarded
                = semantic::CompiledDesignResolver { parent }
                      .actual_declaration(*actual_declaration);
            if (forwarded) {
                actual_declaration = *forwarded;
            }
        }
        bool requires_static_value { };
        std::optional<semantic::SpecializedHirVhdlPackedValue>
            vhdl_packed_value;
        const auto formal = compiled.find_declaration(binding.formal);
        if (formal && formal->vhdl != nullptr
            && formal->vhdl->form
                == semantic::vhdl::DeclarationForm::generic_constant
            && binding.expression) {
            auto effective_subtype = formal->vhdl->subtype
                ? semantic::CompiledDesignResolver { parent }
                      .effective_vhdl_subtype(
                          *formal->vhdl->subtype, formal->vhdl->scope)
                : std::nullopt;
            const bool logic9_vector_formal
                = effective_subtype
                && effective_subtype->domain
                    == semantic::vhdl::ValueDomain::logic9
                && (effective_subtype->builtin_type
                        == semantic::vhdl::BuiltinTypeIdentity::
                            ieee_std_logic_1164_std_logic_vector
                    || effective_subtype->builtin_type
                        == semantic::vhdl::BuiltinTypeIdentity::
                            ieee_std_logic_1164_std_ulogic_vector);
            std::optional<std::int64_t> value;
            std::optional<PackedLogic4> composite;
            if (logic9_vector_formal) {
                const auto evaluated
                    = parent.evaluate_vhdl_constant_expression(
                        *binding.expression);
                const auto* packed = evaluated
                        ? std::get_if<
                              semantic::SpecializedHirVhdlPackedValue>(
                              &*evaluated)
                        : nullptr;
                const auto layout = compiled_vhdl_named_signal_layout(
                    parent, *effective_subtype, formal->vhdl->scope);
                if (packed != nullptr && layout
                    && layout->domain == frontend::ValueDomain::Logic9
                    && layout->width != 0U) {
                    const auto distance = index_distance(
                        packed->left_bound, packed->right_bound);
                    const bool valid_actual_range
                        = distance
                            != std::numeric_limits<std::uint64_t>::max()
                        && distance + 1U == packed->bits.size();
                    const bool unconstrained_formal
                        = effective_subtype->constraints.empty()
                        && (effective_subtype->unconstrained
                            || effective_subtype->builtin_type
                                == semantic::vhdl::BuiltinTypeIdentity::
                                    ieee_std_logic_1164_std_logic_vector
                            || effective_subtype->builtin_type
                                == semantic::vhdl::BuiltinTypeIdentity::
                                    ieee_std_logic_1164_std_ulogic_vector);
                    auto bound_value = *packed;
                    if (valid_actual_range
                        && (unconstrained_formal
                            || packed->bits.size() == layout->width)) {
                        bool valid_formal_range = true;
                        if (!unconstrained_formal) {
                            const auto bounds = compiled_packed_range(
                                *effective_subtype);
                            const auto formal_distance = bounds
                                ? index_distance(
                                      bounds->left, bounds->right)
                                : std::numeric_limits<std::uint64_t>::max();
                            valid_formal_range = bounds
                                && formal_distance
                                    != std::numeric_limits<std::uint64_t>::max()
                                && formal_distance + 1U
                                    == bound_value.bits.size();
                            if (valid_formal_range) {
                                bound_value.left_bound = bounds->left;
                                bound_value.right_bound = bounds->right;
                            }
                        }
                        if (valid_formal_range) {
                            try {
                                composite =
                                    PackedLogic4::from_logic9_msb_string(
                                        bound_value.bits);
                                vhdl_packed_value = std::move(bound_value);
                            } catch (const std::invalid_argument&) {
                                composite.reset();
                                vhdl_packed_value.reset();
                            }
                        }
                    }
                } else if (!evaluated) {
                    composite = compiled_vhdl_static_composite_value(
                        parent, *binding.expression,
                        *formal->vhdl->subtype, formal->vhdl->scope);
                }
            } else {
                value = parent.evaluate_integral_expression(
                    *binding.expression);
                if (!value && formal->vhdl->subtype) {
                    composite = compiled_vhdl_static_composite_value(
                        parent, *binding.expression,
                        *formal->vhdl->subtype, formal->vhdl->scope);
                }
            }
            if (value) {
                identity = compiled_vhdl_integral_identity(
                    *formal->vhdl, *value);
            } else if (composite) {
                identity = compiled_vhdl_composite_identity(
                    parent, *formal->vhdl, *composite);
                const auto evaluated
                    = parent.evaluate_vhdl_constant_expression(
                        *binding.expression);
                const auto* packed = evaluated
                        ? std::get_if<
                              semantic::SpecializedHirVhdlPackedValue>(
                              &*evaluated)
                        : nullptr;
                const auto typed_formal_subtype
                    = formal->vhdl->subtype
                    ? semantic::CompiledDesignResolver { parent }
                          .effective_vhdl_subtype(
                              *formal->vhdl->subtype,
                              formal->vhdl->scope)
                    : std::nullopt;
                const auto layout = typed_formal_subtype
                    ? compiled_vhdl_named_signal_layout(
                          parent, *typed_formal_subtype,
                          formal->vhdl->scope)
                    : std::nullopt;
                if (packed != nullptr && typed_formal_subtype && layout
                    && layout->width != 0U
                    && compiled_value_domain(typed_formal_subtype->domain)
                        == layout->domain
                    && (layout->domain == frontend::ValueDomain::Bit2
                        || layout->domain
                            == frontend::ValueDomain::Logic4
                        || layout->domain
                            == frontend::ValueDomain::Logic9)
                    && packed->bits == composite->to_msb_string()
                    && packed->bits.size() == composite->width()) {
                    const auto distance = index_distance(
                        packed->left_bound, packed->right_bound);
                    const bool valid_range
                        = distance
                            != std::numeric_limits<std::uint64_t>::max()
                        && distance + 1U == packed->bits.size();
                    const bool unconstrained_builtin_vector
                        = typed_formal_subtype->builtin_type
                                == semantic::vhdl::BuiltinTypeIdentity::
                                    ieee_std_logic_1164_std_logic_vector
                        && typed_formal_subtype->constraints.empty();
                    const bool unconstrained_formal
                        = typed_formal_subtype->unconstrained
                        || unconstrained_builtin_vector;
                    if (valid_range
                        && (unconstrained_formal
                            || packed->bits.size() == layout->width)) {
                        auto bound_value = *packed;
                        if (!unconstrained_formal) {
                            const auto bounds
                                = compiled_packed_range(
                                    *typed_formal_subtype);
                            const auto bounds_distance = bounds
                                ? index_distance(bounds->left,
                                      bounds->right)
                                : std::numeric_limits<std::uint64_t>::max();
                            if (!bounds
                                || bounds_distance
                                    == std::numeric_limits<std::uint64_t>::max()
                                || bounds_distance + 1U
                                    != bound_value.bits.size()) {
                                bound_value.bits.clear();
                            } else {
                                bound_value.left_bound = bounds->left;
                                bound_value.right_bound = bounds->right;
                            }
                        }
                        if (!bound_value.bits.empty()) {
                            vhdl_packed_value = std::move(bound_value);
                        }
                    }
                }
            } else if (formal->vhdl->subtype) {
                const auto& subtype = *formal->vhdl->subtype;
                const auto layout = compiled_vhdl_named_signal_layout(
                    parent, subtype, formal->vhdl->scope);
                const auto domain = layout
                    ? layout->domain
                    : compiled_value_domain(subtype.domain);
                requires_static_value
                    = domain == frontend::ValueDomain::Boolean
                    || domain == frontend::ValueDomain::Integer;
            }
        }
        semantic::SpecializedHirActualIdentity actual {
            binding.formal,
            std::move(identity),
            actual_declaration,
            binding.expression,
            binding.systemverilog_type,
            binding.vhdl_type,
            binding.source,
        };
        actual.vhdl_packed_value = std::move(vhdl_packed_value);
        return { std::move(actual), requires_static_value };
    }

    std::optional<semantic::SpecializedHirUnit> compiled_specialization(
        const semantic::ValidatedCompiledDesign& validated,
        const semantic::UnitId unit,
        std::vector<semantic::SpecializedHirActualIdentity>& actuals,
        std::vector<CompiledSpecializationFailure>* const failures)
    {
        const auto& compiled = validated.design();
        auto result = semantic::make_specialized_hir_unit(
            validated, unit, actuals);
        if (!result) {
            return std::nullopt;
        }
        bool normalized_actual { };
        for (auto& actual : actuals) {
            const auto declaration = result->find_declaration(
                actual.declaration);
            if (!declaration || declaration->systemverilog == nullptr
                || declaration->systemverilog->form
                    != semantic::sv::DeclarationForm::parameter
                || !declaration->systemverilog->type) {
                continue;
            }
            const auto target_scalar = compiled_systemverilog_scalar_kind(
                declaration->systemverilog->type->target.spelling);
            if (target_scalar
                != frontend::SystemVerilogScalarKind::None) {
                auto scalar = decode_hir_systemverilog_scalar_constant(
                    actual.identity);
                if (!scalar) {
                    if (const auto integral
                        = decode_hir_systemverilog_constant(actual.identity)) {
                        if (const auto value = integral->integer_value()) {
                            scalar = frontend::SystemVerilogScalarConstant {
                                frontend::SystemVerilogScalarKind::None,
                                std::bit_cast<std::uint64_t>(*value)
                            };
                        }
                    }
                }
                if (!scalar) {
                    continue;
                }
                std::string conversion_error;
                scalar = frontend::convert_systemverilog_scalar_constant(
                    *scalar, target_scalar, conversion_error);
                if (!scalar) {
                    continue;
                }
                auto identity = scalar->canonical();
                if (identity != actual.identity) {
                    actual.identity = std::move(identity);
                    normalized_actual = true;
                }
                continue;
            }
            if (!hir_systemverilog_explicit_integral_type(
                    *declaration->systemverilog->type)) {
                continue;
            }
            auto constant = decode_hir_systemverilog_constant(
                actual.identity);
            if (!constant) {
                continue;
            }
            std::string conversion_error;
            constant = convert_hir_systemverilog_constant(
                std::move(*constant), *declaration->systemverilog->type,
                conversion_error, &*result);
            if (!constant) {
                continue;
            }
            auto identity = constant->canonical();
            if (identity != actual.identity) {
                actual.identity = std::move(identity);
                normalized_actual = true;
            }
        }
        if (normalized_actual) {
            result = semantic::make_specialized_hir_unit(
                validated, unit, actuals);
            if (!result) {
                return std::nullopt;
            }
        }
        const auto already_bound = [&](const semantic::DeclarationId declaration) {
            return std::ranges::any_of(
                actuals, [&](const auto& actual) {
                    return actual.declaration == declaration;
                });
        };
        bool appended { };
        const auto refresh_specialization = [&] {
            auto refreshed = semantic::make_specialized_hir_unit(
                validated, unit, actuals);
            if (!refreshed) {
                return false;
            }
            result = std::move(refreshed);
            return true;
        };
        const auto append_systemverilog_default
            = [&](const semantic::DeclarationId declaration_id) {
                  const auto declaration = result->find_declaration(declaration_id);
                  if (!declaration || declaration->systemverilog == nullptr
                      || already_bound(declaration_id)) {
                      return;
                  }
                  const auto form = declaration->systemverilog->form;
                  if (form == semantic::sv::DeclarationForm::type_parameter) {
                      if (!declaration->systemverilog->default_type) {
                          return;
                      }
                      actuals.push_back({ declaration_id,
                          compiled_systemverilog_type_identity(
                              *declaration->systemverilog->default_type),
                          std::nullopt,
                          std::nullopt,
                          declaration->systemverilog->default_type,
                          std::nullopt });
                      appended = true;
                      return;
                  }
                  if (form != semantic::sv::DeclarationForm::parameter
                      || !declaration->systemverilog->initializer) {
                      return;
                  }
                  if (compiled_systemverilog_string_declaration(
                          *declaration->systemverilog)) {
                      const auto string_value = result->evaluate_string_expression(
                          *declaration->systemverilog->initializer);
                      if (string_value) {
                          actuals.push_back({ declaration_id,
                              semantic::systemverilog_string_identity(*string_value),
                              std::nullopt,
                              declaration->systemverilog->initializer });
                          appended = true;
                      } else if (failures != nullptr) {
                          failures->push_back({ "FSIM-ELAB-SVSTRING-001",
                              "cannot evaluate default for string parameter '"
                                  + declaration->systemverilog->name + "'",
                              declaration->systemverilog->source });
                      }
                      return;
                  }
                  const auto target_scalar = declaration->systemverilog->type
                      ? compiled_systemverilog_scalar_kind(
                            declaration->systemverilog->type->target.spelling)
                      : frontend::SystemVerilogScalarKind::None;
                  const auto scalar_applicable
                      = target_scalar != frontend::SystemVerilogScalarKind::None
                      || hir_systemverilog_scalar_expression_applicable(
                          *result, *declaration->systemverilog->initializer);
                  if (scalar_applicable) {
                      std::string scalar_error;
                      auto scalar = evaluate_hir_systemverilog_scalar_declaration(
                          *result, declaration_id, scalar_error);
                      if (scalar) {
                          actuals.push_back({ declaration_id, scalar->canonical(),
                              std::nullopt,
                              declaration->systemverilog->initializer });
                          appended = true;
                      } else if (failures != nullptr) {
                          failures->push_back({ "FSIM-ELAB-PARAM-005",
                              "cannot evaluate parameter default for '"
                                  + declaration->systemverilog->name + "': "
                                  + scalar_error,
                              declaration->systemverilog->source });
                      }
                      return;
                  }
                  std::string constant_error;
                  auto constant = compiled_systemverilog_integral_constant_type(
                                      declaration->systemverilog->type)
                      ? evaluate_hir_systemverilog_constant(
                            *result, *declaration->systemverilog->initializer,
                            constant_error)
                      : std::optional<HirSystemVerilogConstant> { };
                  if (constant && declaration->systemverilog->type
                      && hir_systemverilog_explicit_integral_type(
                          *declaration->systemverilog->type)) {
                      constant = convert_hir_systemverilog_constant(
                          std::move(*constant),
                          *declaration->systemverilog->type,
                          constant_error, &*result);
                  }
                  if (constant) {
                      actuals.push_back({ declaration_id, constant->canonical(),
                          std::nullopt,
                          declaration->systemverilog->initializer });
                      appended = true;
                  } else if (const auto value
                      = result->evaluate_integral_expression(
                          *declaration->systemverilog->initializer)) {
                      actuals.push_back({ declaration_id,
                          compiled_systemverilog_integral_identity(
                              *declaration->systemverilog,
                              std::to_string(*value)),
                          std::nullopt,
                          declaration->systemverilog->initializer });
                      appended = true;
                  } else if (!constant_error.empty()) {
                      if (failures != nullptr) {
                          failures->push_back({ "FSIM-ELAB-PARAM-005",
                              "cannot evaluate parameter default for '"
                                  + declaration->systemverilog->name + "': "
                                  + constant_error,
                              declaration->systemverilog->source });
                      }
                      return;
                  } else if (const auto string_value
                      = result->evaluate_string_expression(
                          *declaration->systemverilog->initializer)) {
                      actuals.push_back({ declaration_id,
                          semantic::systemverilog_string_identity(*string_value),
                          std::nullopt,
                          declaration->systemverilog->initializer });
                      appended = true;
                  } else if (const auto initializer = result->find_expression(
                                 *declaration->systemverilog->initializer);
                      initializer && initializer->systemverilog != nullptr
                      && (initializer->systemverilog->kind
                              == semantic::sv::ExpressionKind::integer_literal
                          || initializer->systemverilog->kind
                              == semantic::sv::ExpressionKind::logic_literal
                          || initializer->systemverilog->kind
                              == semantic::sv::ExpressionKind::boolean_literal)
                      && !initializer->systemverilog->text.empty()) {
                      actuals.push_back({ declaration_id,
                          initializer->systemverilog->text,
                          std::nullopt,
                          declaration->systemverilog->initializer });
                      appended = true;
                  } else if (failures != nullptr) {
                      const auto error = compiled_systemverilog_default_error(
                          *result, *declaration->systemverilog);
                      if (!error && constant_error.empty()) {
                          return;
                      }
                      failures->push_back({ "FSIM-ELAB-PARAM-005",
                          "cannot evaluate parameter default for '"
                              + declaration->systemverilog->name + "': "
                              + (constant_error.empty() ? *error : constant_error),
                          declaration->systemverilog->source });
                  }
              };
        const auto append_vhdl_default
            = [&](const semantic::DeclarationId declaration_id) {
                  const auto declaration = result->find_declaration(declaration_id);
                  if (!declaration || declaration->vhdl == nullptr
                      || already_bound(declaration_id)) {
                      return;
                  }
                  const auto form = declaration->vhdl->form;
                  const auto generic_function = form
                      == semantic::vhdl::DeclarationForm::generic_function;
                  const auto generic_procedure = form
                      == semantic::vhdl::DeclarationForm::generic_procedure;
                  if (generic_function || generic_procedure) {
                      if (failures != nullptr
                          && (!declaration->vhdl->callable
                              || !declaration->vhdl->callable
                                      ->default_callable)) {
                          failures->push_back({
                              generic_function
                                  ? "FSIM-ELAB-VHFUNC-004"
                                  : "FSIM-ELAB-VHPROC-004",
                              "required VHDL interface subprogram '"
                                  + declaration->vhdl->name
                                  + "' has no actual",
                              declaration->vhdl->source });
                      }
                      return;
                  }
                  if (form
                      == semantic::vhdl::DeclarationForm::generic_package) {
                      if (failures != nullptr) {
                          failures->push_back({
                              "FSIM-ELAB-VHPKG-004",
                              "interface package generic '"
                                  + declaration->vhdl->name
                                  + "' requires a package instance actual",
                              declaration->vhdl->source });
                      }
                      return;
                  }
                  if (form
                          != semantic::vhdl::DeclarationForm::generic_constant
                      || !declaration->vhdl->initializer) {
                      return;
                  }
                  auto value = result->evaluate_integral_declaration(
                      declaration_id);
                  const auto composite
                      = !value && declaration->vhdl->subtype
                      ? compiled_vhdl_static_composite_value(
                          *result, *declaration->vhdl->initializer,
                          *declaration->vhdl->subtype,
                          declaration->vhdl->scope)
                      : std::nullopt;
                  if (value) {
                      if (declaration->vhdl->subtype
                          && !compiled_vhdl_specialization_value_satisfies_subtype(
                              *result, *declaration->vhdl->subtype, *value)) {
                          if (failures != nullptr) {
                              failures->push_back({ "FSIM-ELAB-GENERIC-008",
                                  "generic '" + declaration->vhdl->name
                                      + "' default violates its scalar subtype",
                                  declaration->vhdl->source });
                          }
                          return;
                      }
                      actuals.push_back({ declaration_id,
                          compiled_vhdl_integral_identity(
                              *declaration->vhdl, *value),
                          std::nullopt });
                      appended = true;
                      static_cast<void>(refresh_specialization());
                  } else if (composite) {
                      actuals.push_back({ declaration_id,
                          compiled_vhdl_composite_identity(
                              *result, *declaration->vhdl, *composite),
                          std::nullopt,
                          declaration->vhdl->initializer });
                      appended = true;
                      static_cast<void>(refresh_specialization());
                  } else if (failures != nullptr
                      && declaration->vhdl->subtype
                      && (declaration->vhdl->subtype->domain
                              == semantic::vhdl::ValueDomain::boolean
                          || declaration->vhdl->subtype->domain
                              == semantic::vhdl::ValueDomain::integer)) {
                      failures->push_back({ "FSIM-ELAB-GENERIC-005",
                          "cannot evaluate default for generic '"
                              + declaration->vhdl->name + "'",
                          declaration->vhdl->source });
                  }
              };
        const auto selected = compiled.find_unit(unit);
        if (!selected) {
            return std::nullopt;
        }
        if (selected->systemverilog != nullptr) {
            for (const auto declaration :
                selected->systemverilog->declarations) {
                append_systemverilog_default(declaration);
            }
        } else if (selected->vhdl != nullptr) {
            for (const auto declaration : selected->vhdl->declarations) {
                append_vhdl_default(declaration);
            }
            if (selected->vhdl->kind
                == semantic::vhdl::UnitKind::architecture) {
                const auto entity = std::ranges::find_if(
                    compiled.vhdl_units(), [&](const auto& candidate) {
                        return candidate.kind
                            == semantic::vhdl::UnitKind::entity
                            && compiled_vhdl_name_equal(
                                candidate.name,
                                selected->vhdl->primary_name)
                            && compiled_vhdl_library_equal(
                                candidate.library,
                                selected->vhdl->library);
                    });
                if (entity == compiled.vhdl_units().end()) {
                    return std::nullopt;
                }
                for (const auto declaration : entity->declarations) {
                    append_vhdl_default(declaration);
                }
            }
        }
        return appended
            ? semantic::make_specialized_hir_unit(validated, unit, actuals)
            : result;
    }

    std::string_view compiled_vhdl_library(
        const semantic::vhdl::Unit& unit)
    {
        return unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
    }

    std::optional<semantic::CompiledUnitView> compiled_vhdl_architecture(
        const semantic::CompiledDesign& compiled,
        const std::string_view library,
        const std::string_view entity,
        const std::string_view architecture,
        std::size_t& matches)
    {
        std::optional<semantic::CompiledUnitView> result;
        matches = 0U;
        for (const auto& candidate : compiled.vhdl_units()) {
            if (candidate.kind != semantic::vhdl::UnitKind::architecture
                || !compiled_vhdl_library_equal(candidate.library, library)
                || !compiled_vhdl_name_equal(candidate.primary_name, entity)
                || (!architecture.empty()
                    && !compiled_vhdl_name_equal(
                        candidate.name, architecture))) {
                continue;
            }
            result = compiled.find_unit(candidate.id);
            ++matches;
        }
        return result;
    }

    const semantic::vhdl::Unit* compiled_vhdl_configuration(
        const semantic::CompiledDesign& compiled,
        const std::string_view owner_library,
        const semantic::vhdl::Name& requested,
        std::size_t& matches)
    {
        const auto parts = vhdl_configuration_detail::configuration_name_parts(
            requested.spelling);
        if (parts.empty() || parts.size() > 2U) {
            matches = 0U;
            return nullptr;
        }
        const auto library = parts.size() == 2U
            ? (compiled_vhdl_name_equal(parts.front(), "work")
                      ? std::string { owner_library }
                      : parts.front())
            : std::string { owner_library };
        const semantic::vhdl::Unit* result = nullptr;
        matches = 0U;
        for (const auto& candidate : compiled.vhdl_units()) {
            if (candidate.kind != semantic::vhdl::UnitKind::configuration
                || !compiled_vhdl_library_equal(candidate.library, library)
                || !compiled_vhdl_name_equal(candidate.name, parts.back())) {
                continue;
            }
            result = &candidate;
            ++matches;
        }
        return result;
    }

    void append_compiled_vhdl_binding_identity(
        std::ostringstream& output,
        const semantic::CompiledDesign& compiled,
        const semantic::vhdl::BindingIndication& binding)
    {
        output << ";aspect=" << static_cast<unsigned>(binding.kind)
               << ";entity="
               << vhdl_configuration_detail::configuration_canonical_name(
                      binding.entity.spelling)
               << ";architecture="
               << vhdl_configuration_detail::configuration_canonical_name(
                      binding.architecture)
               << ";configuration="
               << vhdl_configuration_detail::configuration_canonical_name(
                      binding.configuration.spelling);
        const auto append_map = [&](const std::string_view prefix,
                                    const auto& associations) {
            for (const auto& association : associations) {
                output << ';' << prefix << ':'
                       << (association.formal
                                  ? vhdl_configuration_detail::
                                        configuration_canonical_name(
                                            association.formal->spelling)
                                  : std::string { "#" })
                       << '=' << static_cast<unsigned>(association.kind);
                if (association.expression) {
                    output << ':' << vhdl_configuration_detail::configuration_expression_identity(compiled, *association.expression);
                } else if (association.type) {
                    output << ':' << vhdl_configuration_detail::configuration_canonical_name(association.type->type_mark.spelling);
                }
            }
        };
        append_map("generic", binding.generic_map);
        append_map("port", binding.port_map);
    }

    void append_compiled_vhdl_block_identity(
        std::ostringstream& output,
        const semantic::CompiledDesign& compiled,
        const semantic::vhdl::BlockConfiguration& block)
    {
        output << ";block="
               << vhdl_configuration_detail::configuration_canonical_name(
                      block.block.spelling);
        if (block.generate_index) {
            output << '[' << vhdl_configuration_detail::configuration_expression_identity(compiled, *block.generate_index) << ']';
        }
        for (const auto& rule : block.components) {
            output << ";component="
                   << vhdl_configuration_detail::configuration_canonical_name(
                          rule.component.spelling)
                   << ";selection="
                   << static_cast<unsigned>(rule.selection);
            for (const auto& label : rule.labels) {
                output << ','
                       << vhdl_configuration_detail::
                              configuration_canonical_name(label);
            }
            append_compiled_vhdl_binding_identity(
                output, compiled, rule.binding);
        }
        for (const auto& child : block.blocks) {
            append_compiled_vhdl_block_identity(output, compiled, child);
        }
        output << ";end-block";
    }

    std::string compiled_vhdl_configuration_identity(
        const semantic::CompiledDesign& compiled,
        const semantic::vhdl::Unit& configuration)
    {
        std::ostringstream output;
        output << "vhdl-configuration-v2;library="
               << compiled_vhdl_library(configuration)
               << ";name=" << configuration.name
               << ";entity=" << configuration.primary_name;
        if (configuration.configuration) {
            output << ";architecture="
                   << configuration.configuration->block.spelling;
            append_compiled_vhdl_block_identity(
                output, compiled, *configuration.configuration);
        }
        return output.str();
    }

    std::string compiled_vhdl_binding_identity(
        const semantic::CompiledDesign& compiled,
        const semantic::vhdl::BindingIndication& binding)
    {
        std::ostringstream output;
        output << "vhdl-configuration-binding-v2";
        append_compiled_vhdl_binding_identity(output, compiled, binding);
        return output.str();
    }

    const semantic::vhdl::ComponentConfiguration*
    compiled_vhdl_configuration_rule(
        const std::span<const semantic::vhdl::ComponentConfiguration> rules,
        const semantic::vhdl::Instance& instance,
        const std::string_view local_label,
        bool* const missing_explicit_rule = nullptr)
    {
        const semantic::vhdl::ComponentConfiguration* all = nullptr;
        const semantic::vhdl::ComponentConfiguration* others = nullptr;
        for (const auto& rule : rules) {
            if (!compiled_vhdl_name_equal(
                    rule.component.spelling, instance.target.spelling)) {
                continue;
            }
            if (rule.selection == semantic::vhdl::InstanceSelection::labels
                && std::ranges::any_of(
                    rule.labels, [&](const auto& label) {
                        return compiled_vhdl_name_equal(label, local_label);
                    })) {
                return &rule;
            }
            if (rule.selection == semantic::vhdl::InstanceSelection::all
                && all == nullptr) {
                all = &rule;
            }
            if (rule.selection == semantic::vhdl::InstanceSelection::others
                && others == nullptr) {
                const auto excluded = std::ranges::any_of(
                    rule.labels, [&](const auto& label) {
                        return compiled_vhdl_name_equal(label, local_label);
                    });
                if (excluded) {
                    if (missing_explicit_rule != nullptr) {
                        *missing_explicit_rule = true;
                    }
                } else {
                    others = &rule;
                }
            }
        }
        return all != nullptr ? all : others;
    }

    std::vector<const semantic::vhdl::BlockConfiguration*>
    compiled_vhdl_configuration_blocks(
        const semantic::CompiledDesign& compiled,
        const semantic::vhdl::BlockConfiguration& root,
        const std::span<const std::string> occurrence)
    {
        std::vector<const semantic::vhdl::BlockConfiguration*> result { &root };
        const auto* selected = &root;
        if (occurrence.size() < 2U) {
            return result;
        }
        for (std::size_t index = 0U; index + 1U < occurrence.size(); ++index) {
            const semantic::vhdl::BlockConfiguration* next = nullptr;
            for (const auto& child : selected->blocks) {
                const auto scope = vhdl_configuration_detail::
                    configuration_block_scope(compiled, child);
                const auto indexed_open = occurrence[index].rfind('[');
                const auto unindexed = indexed_open != std::string::npos
                        && occurrence[index].ends_with(']')
                    ? std::string_view { occurrence[index] }.substr(
                          0U, indexed_open)
                    : std::string_view { occurrence[index] };
                if (scope
                    && (compiled_vhdl_name_equal(*scope, occurrence[index])
                        || (!child.generate_index
                            && compiled_vhdl_name_equal(
                                child.block.spelling, unindexed)))) {
                    if (next != nullptr) {
                        return result;
                    }
                    next = &child;
                }
            }
            if (next == nullptr) {
                break;
            }
            selected = next;
            result.push_back(selected);
        }
        return result;
    }

    struct CompiledSystemVerilogDiagnostic {
        std::string code;
        std::string message;
        semantic::SourceSpanId source;
    };

    enum class CompiledSystemVerilogMemberKind : std::uint8_t {
        none,
        constant,
        type,
        other,
    };

    std::optional<semantic::UnitId> compiled_scope_unit(
        const semantic::CompiledDesign& compiled,
        const semantic::ScopeId scope)
    {
        const auto& scopes = compiled.semantics.scopes();
        if (!scope.valid() || scope.value() >= scopes.size()) {
            return std::nullopt;
        }
        return scopes[scope.value()].unit;
    }

    bool compiled_scope_belongs_to_unit(
        const semantic::CompiledDesign& compiled,
        const semantic::ScopeId scope,
        const semantic::UnitId unit)
    {
        const auto owner = compiled_scope_unit(compiled, scope);
        return owner && *owner == unit;
    }

    const semantic::sv::Unit* compiled_systemverilog_package(
        const semantic::CompiledDesign& compiled,
        const semantic::sv::Unit& owner,
        const std::string_view name)
    {
        const auto result = semantic::CompiledDesignResolver {
            compiled, owner.id }
                                .find_systemverilog_package(
                                    compiled_systemverilog_library(owner),
                                    name);
        return result && result->systemverilog != nullptr
            ? result->systemverilog
            : nullptr;
    }

    CompiledSystemVerilogMemberKind compiled_systemverilog_exported_member(
        const semantic::CompiledDesign& compiled,
        const semantic::sv::Unit& package,
        const std::string_view name)
    {
        const semantic::CompiledDesignResolver resolver {
            compiled, package.id
        };
        const auto declarations
            = resolver.resolve_systemverilog_package_member(
                compiled_systemverilog_library(package),
                package.name, name);
        CompiledSystemVerilogMemberKind result {
            CompiledSystemVerilogMemberKind::none
        };
        const auto merge = [&](const CompiledSystemVerilogMemberKind kind) {
            if (result == CompiledSystemVerilogMemberKind::none) {
                result = kind;
            } else if (result != kind) {
                result = CompiledSystemVerilogMemberKind::other;
            }
        };
        for (const auto declaration_id : declarations.candidates) {
            const auto view = compiled.find_declaration(declaration_id);
            if (!view || view->systemverilog == nullptr) {
                continue;
            }
            using Form = semantic::sv::DeclarationForm;
            switch (view->systemverilog->form) {
            case Form::parameter:
            case Form::local_parameter:
            case Form::enumeration_literal:
                merge(CompiledSystemVerilogMemberKind::constant);
                break;
            case Form::type_parameter:
            case Form::typedef_declaration:
            case Form::nettype_declaration:
                merge(CompiledSystemVerilogMemberKind::type);
                break;
            default:
                merge(CompiledSystemVerilogMemberKind::other);
                break;
            }
        }
        const auto lets = resolver.resolve_systemverilog_package_let(
            compiled_systemverilog_library(package), package.name, name);
        if (!lets.candidates.empty()) {
            merge(CompiledSystemVerilogMemberKind::other);
        }
        if (!resolver.resolve_systemverilog_package_class(
                compiled_systemverilog_library(package),
                package.name, name).candidates.empty()) {
            merge(CompiledSystemVerilogMemberKind::type);
        }
        return result;
    }

    std::vector<CompiledSystemVerilogDiagnostic>
    validate_compiled_systemverilog_unit(
        const semantic::CompiledDesign& compiled,
        const semantic::sv::Unit& unit)
    {
        using DeclarationForm = semantic::sv::DeclarationForm;
        using ExpressionKind = semantic::sv::ExpressionKind;
        using MemberKind = CompiledSystemVerilogMemberKind;
        using TypeForm = semantic::sv::TypeForm;

        std::vector<CompiledSystemVerilogDiagnostic> diagnostics;
        const auto append = [&](std::string code,
                                std::string message,
                                const semantic::SourceSpanId source) {
            diagnostics.push_back({ std::move(code), std::move(message), source });
        };

        std::vector<const semantic::sv::Unit*> packages;
        std::unordered_set<std::uint32_t> package_ids;
        const auto collect_package = [&](const auto& self,
                                         const semantic::sv::Unit& package)
            -> void {
            if (!package_ids.insert(package.id.value()).second) {
                return;
            }
            packages.push_back(&package);
            for (const auto& imported : package.imports) {
                if (const auto* dependency = compiled_systemverilog_package(
                        compiled, package, imported.package.spelling)) {
                    self(self, *dependency);
                }
            }
        };
        for (const auto& imported : unit.imports) {
            if (const auto* package = compiled_systemverilog_package(
                    compiled, unit, imported.package.spelling)) {
                collect_package(collect_package, *package);
            }
        }

        const auto import_description = [](const semantic::sv::Import& imported) {
            return imported.package.spelling + "::"
                + (imported.member
                        ? imported.member->spelling
                        : std::string { "*" });
        };
        const auto validate_imports = [&](const semantic::sv::Unit& owner) {
            for (const auto& imported : owner.imports) {
                const auto* package = compiled_systemverilog_package(
                    compiled, owner, imported.package.spelling);
                if (package == nullptr) {
                    append("FSIM-ELAB-SVPKG-001",
                        "SystemVerilog package '"
                            + imported.package.spelling + "' was not found",
                        imported.source);
                    continue;
                }
                if (imported.member
                    && compiled_systemverilog_exported_member(
                           compiled, *package, imported.member->spelling)
                        == MemberKind::none) {
                    append("FSIM-ELAB-SVPKG-002",
                        "SystemVerilog package '" + package->name
                            + "' has no exported item '"
                            + imported.member->spelling + "'",
                        imported.source);
                }
            }
        };
        validate_imports(unit);
        for (const auto* package : packages) {
            validate_imports(*package);
        }

        for (const auto& imported : unit.imports) {
            if (!imported.member) {
                continue;
            }
            const bool conflicts = std::ranges::any_of(
                unit.declarations,
                [&](const semantic::DeclarationId declaration_id) {
                    const auto declaration = compiled.find_declaration(
                        declaration_id);
                    return declaration
                        && declaration->systemverilog != nullptr
                        && declaration->systemverilog->name
                        == imported.member->spelling;
                });
            if (conflicts) {
                append("FSIM-ELAB-SVPKG-009",
                    "explicit SystemVerilog package import '"
                        + import_description(imported)
                        + "' conflicts with a declaration in the same scope",
                    imported.source);
            }
        }

        std::unordered_map<std::uint32_t, std::uint8_t> package_states;
        std::vector<const semantic::sv::Unit*> package_stack;
        const auto visit_package = [&](const auto& self,
                                       const semantic::sv::Unit& package)
            -> void {
            auto& state = package_states[package.id.value()];
            if (state == 2U) {
                return;
            }
            if (state == 1U) {
                return;
            }
            state = 1U;
            package_stack.push_back(&package);
            for (const auto& imported : package.imports) {
                const auto* dependency = compiled_systemverilog_package(
                    compiled, package, imported.package.spelling);
                if (dependency == nullptr) {
                    continue;
                }
                if (package_states[dependency->id.value()] == 1U) {
                    std::string cycle;
                    const auto first = std::ranges::find(
                        package_stack, dependency);
                    for (auto current = first;
                        current != package_stack.end(); ++current) {
                        if (!cycle.empty()) {
                            cycle += " -> ";
                        }
                        cycle += (*current)->name;
                    }
                    cycle += " -> " + dependency->name;
                    append("FSIM-ELAB-SVPKG-004",
                        "cyclic SystemVerilog package visibility: " + cycle,
                        imported.source);
                    continue;
                }
                self(self, *dependency);
            }
            package_stack.pop_back();
            state = 2U;
        };
        for (const auto* package : packages) {
            visit_package(visit_package, *package);
        }

        for (const auto* package : packages) {
            for (const auto& exported : package->exports) {
                if (exported.package.spelling == "*" && exported.member) {
                    append("FSIM-ELAB-SVPKG-007",
                        "a wildcard export package must use the form *::*",
                        exported.source);
                    continue;
                }
                const auto matching_import = std::ranges::find_if(
                    package->imports,
                    [&](const semantic::sv::Import& imported) {
                        const bool package_matches
                            = exported.package.spelling == "*"
                            || exported.package.spelling
                                == imported.package.spelling;
                        const bool member_matches = !exported.member
                            || !imported.member
                            || exported.member->spelling
                                == imported.member->spelling;
                        return package_matches && member_matches;
                    });
                if (matching_import == package->imports.end()) {
                    append("FSIM-ELAB-SVPKG-007",
                        "package export '" + exported.package.spelling + "::"
                            + (exported.member
                                    ? exported.member->spelling
                                    : std::string { "*" })
                            + "' is not backed by a matching import",
                        exported.source);
                    continue;
                }
                if (exported.member) {
                    const auto* source = compiled_systemverilog_package(
                        compiled, *package,
                        matching_import->package.spelling);
                    if (source == nullptr
                        || compiled_systemverilog_exported_member(
                               compiled, *source,
                               exported.member->spelling)
                            == MemberKind::none) {
                        append("FSIM-ELAB-SVPKG-008",
                            "package export '" + exported.package.spelling
                                + "::" + exported.member->spelling
                                + "' does not select an imported declaration",
                            exported.source);
                    }
                }
            }
        }

        std::unordered_set<std::string> referenced_names;
        for (const auto& expression : compiled.systemverilog_hir.expressions()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, expression.scope, unit.id)
                || (expression.kind != ExpressionKind::name
                    && expression.kind != ExpressionKind::call)
                || expression.text.empty()
                || expression.text.starts_with("@sv-")
                || expression.text.find('.') != std::string::npos
                || expression.text.find("::") != std::string::npos) {
                continue;
            }
            referenced_names.insert(expression.text);
        }
        const auto remember_type_name = [&](const semantic::sv::TypeReference& type) {
            const auto& name = type.target.spelling;
            if (!name.empty() && name.find('.') == std::string::npos
                && name.find("::") == std::string::npos) {
                referenced_names.insert(name);
            }
        };
        for (const auto& declaration :
            compiled.systemverilog_hir.declarations()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, declaration.scope, unit.id)) {
                continue;
            }
            if (declaration.type) {
                remember_type_name(*declaration.type);
            }
            if (declaration.default_type) {
                remember_type_name(*declaration.default_type);
            }
        }

        const semantic::CompiledDesignResolver name_resolver {
            compiled, unit.id
        };
        std::unordered_set<std::string> ambiguous_types;
        for (const auto& name : referenced_names) {
            semantic::SourceSpanId source = unit.source;
            const auto imported = std::ranges::find_if(
                unit.imports,
                [&](const semantic::sv::Import& candidate) {
                    return candidate.wildcard
                        || (candidate.member
                            && candidate.member->spelling == name);
                });
            if (imported != unit.imports.end()) {
                source = imported->source;
            }
            const auto constants = name_resolver.resolve_systemverilog_constant(
                name, unit.scope, false);
            if (constants.status
                == semantic::CompiledResolutionStatus::ambiguous) {
                append("FSIM-ELAB-SVPKG-003",
                    "SystemVerilog package constant '" + name
                        + "' is imported from multiple packages",
                    source);
            }
            const auto types = name_resolver.resolve_systemverilog_named_type(
                name, unit.scope, false);
            const auto classes = name_resolver.resolve_systemverilog_class(
                name, unit.scope, false);
            const auto type_ambiguous
                = types.status == semantic::CompiledResolutionStatus::ambiguous
                || classes.status
                    == semantic::CompiledResolutionStatus::ambiguous
                || (!types.candidates.empty()
                    && !classes.candidates.empty());
            if (type_ambiguous) {
                ambiguous_types.insert(name);
                append("FSIM-ELAB-SVTYPE-002",
                    "SystemVerilog type '" + name
                        + "' is imported from multiple packages",
                    source);
            }
        }

        const auto builtin_type = [](const std::string_view name) {
            static constexpr std::array builtins {
                std::string_view { "bit" },
                std::string_view { "byte" },
                std::string_view { "chandle" },
                std::string_view { "const" },
                std::string_view { "enum" },
                std::string_view { "event" },
                std::string_view { "genvar" },
                std::string_view { "implicit" },
                std::string_view { "int" },
                std::string_view { "integer" },
                std::string_view { "logic" },
                std::string_view { "longint" },
                std::string_view { "mailbox" },
                std::string_view { "process" },
                std::string_view { "real" },
                std::string_view { "realtime" },
                std::string_view { "reg" },
                std::string_view { "semaphore" },
                std::string_view { "shortint" },
                std::string_view { "shortreal" },
                std::string_view { "signed" },
                std::string_view { "string" },
                std::string_view { "struct" },
                std::string_view { "struct packed" },
                std::string_view { "supply0" },
                std::string_view { "supply1" },
                std::string_view { "time" },
                std::string_view { "tri" },
                std::string_view { "tri0" },
                std::string_view { "tri1" },
                std::string_view { "triand" },
                std::string_view { "trior" },
                std::string_view { "trireg" },
                std::string_view { "union" },
                std::string_view { "union packed" },
                std::string_view { "union tagged packed" },
                std::string_view { "unsigned" },
                std::string_view { "uwire" },
                std::string_view { "var" },
                std::string_view { "void" },
                std::string_view { "wand" },
                std::string_view { "weak_reference" },
                std::string_view { "wire" },
                std::string_view { "wor" },
            };
            return std::ranges::find(builtins, name) != builtins.end();
        };
        const auto scope_encloses = [&](const semantic::ScopeId outer,
                                        semantic::ScopeId inner) {
            const auto& scopes = compiled.semantics.scopes();
            while (inner.valid() && inner.value() < scopes.size()) {
                if (inner == outer) {
                    return true;
                }
                const auto& scope = scopes[inner.value()];
                if (!scope.parent) {
                    break;
                }
                inner = *scope.parent;
            }
            return false;
        };
        const auto resolved_type_visible = [&](const std::string_view name,
                                               const semantic::ScopeId scope) {
            return !name_resolver.resolve_systemverilog_named_type(
                        name, scope, false).candidates.empty()
                || !name_resolver.resolve_systemverilog_class(
                        name, scope, false).candidates.empty();
        };
        const auto resolved_equivalent_type_reference = [&](
                                                            const semantic::sv::TypeReference&
                                                                unresolved) {
            const auto matches = [&](
                                     const std::optional<semantic::sv::TypeReference>&
                                         candidate) {
                return candidate && candidate->target.target.valid()
                    && unresolved.target.source.valid()
                    && candidate->target.source == unresolved.target.source
                    && candidate->target.spelling
                    == unresolved.target.spelling;
            };
            return std::ranges::any_of(
                compiled.systemverilog_hir.declarations(),
                [&](const semantic::sv::Declaration& declaration) {
                    return matches(declaration.type)
                        || matches(declaration.default_type);
                });
        };
        const auto validate_type_reference = [&](
                                                 const semantic::sv::TypeReference& type,
                                                 const semantic::ScopeId scope,
                                                 const semantic::SourceSpanId fallback) {
            const auto& name = type.target.spelling;
            const bool covergroup = std::ranges::any_of(
                unit.coverage,
                [&](const semantic::sv::CovergroupDeclaration& declaration) {
                    return declaration.name == name;
                });
            const bool interface_or_program = std::ranges::any_of(
                    compiled.systemverilog_units(),
                    [&](const semantic::sv::Unit& candidate) {
                        return candidate.name == name
                            && (candidate.kind
                                    == semantic::sv::UnitKind::interface || candidate.kind == semantic::sv::UnitKind::program);
                    });
            if (type.target.target.valid() || name.empty()
                || builtin_type(name) || covergroup || interface_or_program
                || resolved_type_visible(name, scope)
                || resolved_equivalent_type_reference(type)
                || !type.systemverilog_net_type.empty()
                || !type.interface_type.empty()
                || type.virtual_interface
                || !type.class_identity.empty()
                || ambiguous_types.contains(name)) {
                return;
            }
            append("FSIM-ELAB-SVTYPE-001",
                "SystemVerilog type alias '" + name
                    + "' is not visible in this unit",
                type.target.source.valid() ? type.target.source : fallback);
        };
        for (const auto& declaration :
            compiled.systemverilog_hir.declarations()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, declaration.scope, unit.id)) {
                continue;
            }
            if (declaration.type) {
                validate_type_reference(
                    *declaration.type, declaration.scope, declaration.source);
            }
            if (declaration.default_type) {
                validate_type_reference(*declaration.default_type,
                    declaration.scope, declaration.source);
            }
        }

        std::unordered_set<std::uint32_t> relevant_units { unit.id.value() };
        relevant_units.insert(package_ids.begin(), package_ids.end());
        std::vector<const semantic::sv::TypeDefinition*> types;
        std::unordered_set<std::uint32_t> included_types;
        const auto add_type = [&](const auto& self,
                                  const semantic::sv::TypeDefinition& type)
            -> void {
            if (!included_types.insert(type.id.value()).second) {
                return;
            }
            types.push_back(&type);
            const auto add_reference = [&](const semantic::sv::TypeReference& ref) {
                if (!ref.target.target.valid()) {
                    return;
                }
                const auto target = compiled.find_type(ref.target.target);
                if (target && target->systemverilog != nullptr) {
                    self(self, *target->systemverilog);
                }
            };
            add_reference(type.base);
            for (const auto& member : type.members) {
                add_reference(member.type);
            }
        };
        for (const auto& type : compiled.systemverilog_hir.types()) {
            const auto declaration = compiled.find_declaration(type.declaration);
            if (!declaration || declaration->systemverilog == nullptr) {
                continue;
            }
            const auto owner = compiled_scope_unit(
                compiled, declaration->systemverilog->scope);
            if (owner && relevant_units.contains(owner->value())) {
                add_type(add_type, type);
            }
        }
        for (const auto& declaration :
            compiled.systemverilog_hir.declarations()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, declaration.scope, unit.id)) {
                continue;
            }
            const auto add_reference
                = [&](const std::optional<semantic::sv::TypeReference>& ref) {
                      if (!ref || !ref->target.target.valid()) {
                          return;
                      }
                      const auto target = compiled.find_type(
                          ref->target.target);
                      if (target && target->systemverilog != nullptr) {
                          add_type(add_type, *target->systemverilog);
                      }
                  };
            add_reference(declaration.type);
            add_reference(declaration.default_type);
        }
        std::unordered_map<std::uint32_t, std::uint8_t> type_states;
        const auto visit_type = [&](const auto& self,
                                    const semantic::sv::TypeDefinition& type)
            -> void {
            auto& state = type_states[type.id.value()];
            if (state == 2U) {
                return;
            }
            if (state == 1U) {
                append("FSIM-ELAB-SVTYPE-003",
                    "cyclic SystemVerilog typedef involving '" + type.name
                        + "'",
                    type.source);
                state = 2U;
                return;
            }
            state = 1U;
            if (type.base.target.target.valid()) {
                const auto base = compiled.find_type(type.base.target.target);
                if (base && base->systemverilog != nullptr) {
                    self(self, *base->systemverilog);
                }
            }
            state = 2U;
        };
        for (const auto* type : types) {
            visit_type(visit_type, *type);
        }

        std::unordered_map<std::uint32_t,
            std::optional<semantic::SpecializedHirUnit>>
            specializations;
        const auto specialization_for = [&](const semantic::UnitId owner)
            -> const semantic::SpecializedHirUnit* {
            const auto [found, inserted] = specializations.try_emplace(
                owner.value(), std::nullopt);
            if (inserted) {
                found->second = semantic::make_specialized_hir_unit(
                    compiled, owner, { });
            }
            return found->second ? &*found->second : nullptr;
        };
        const auto* unit_specialized = specialization_for(unit.id);
        const auto owner_specialization = [&](
                                              const semantic::sv::TypeDefinition& type)
            -> const semantic::SpecializedHirUnit* {
            auto declaration = compiled.find_declaration(type.declaration);
            if (!declaration && type.id.valid()) {
                const auto owner = std::ranges::find_if(
                    compiled.systemverilog_hir.declarations(),
                    [&](const semantic::sv::Declaration& candidate) {
                        return (candidate.declared_type
                                   && *candidate.declared_type == type.id)
                            || (candidate.type
                                && candidate.type->target.target == type.id)
                            || (candidate.default_type
                                && candidate.default_type->target.target
                                    == type.id);
                    });
                if (owner
                    != compiled.systemverilog_hir.declarations().end()) {
                    declaration = compiled.find_declaration(owner->id);
                }
            }
            const auto owner = declaration
                    && declaration->systemverilog != nullptr
                ? compiled_scope_unit(
                      compiled, declaration->systemverilog->scope)
                : std::nullopt;
            return owner ? specialization_for(*owner) : nullptr;
        };
        const auto boundary_value = [&](
                                        const semantic::SpecializedHirUnit* specialized,
                                        const std::optional<std::int64_t> value,
                                        const std::optional<semantic::ExpressionId>
                                            expression) {
            return value ? value
                : specialized != nullptr && expression
                ? specialized->evaluate_integral_expression(*expression)
                : std::nullopt;
        };
        std::function<std::optional<std::uint64_t>(
            const semantic::sv::TypeReference&,
            const semantic::SpecializedHirUnit*,
            std::unordered_set<std::uint32_t>&)>
            type_width;
        type_width = [&](const semantic::sv::TypeReference& reference,
                         const semantic::SpecializedHirUnit* specialized,
                         std::unordered_set<std::uint32_t>& visiting)
            -> std::optional<std::uint64_t> {
            if (specialized != nullptr) {
                const auto effective
                    = semantic::CompiledDesignResolver { *specialized }
                          .effective_systemverilog_type(
                              reference, specialized->scope());
                if (effective && *effective != reference) {
                    return type_width(*effective, specialized, visiting);
                }
            }
            if (reference.executable_width && *reference.executable_width != 0U) {
                return reference.executable_width;
            }
            if (reference.packed_range) {
                const auto left = boundary_value(specialized,
                    reference.packed_range->left,
                    reference.packed_range->left_expression);
                const auto right = boundary_value(specialized,
                    reference.packed_range->right,
                    reference.packed_range->right_expression);
                if (left && right) {
                    const auto distance = *left >= *right
                        ? static_cast<std::uint64_t>(*left - *right)
                        : static_cast<std::uint64_t>(*right - *left);
                    if (distance != std::numeric_limits<std::uint64_t>::max()) {
                        return distance + 1U;
                    }
                }
                return std::nullopt;
            }
            if (!reference.target.target.valid()
                || !visiting.insert(reference.target.target.value()).second) {
                return std::nullopt;
            }
            const auto type = compiled.find_type(reference.target.target);
            if (!type || type->systemverilog == nullptr) {
                visiting.erase(reference.target.target.value());
                return std::nullopt;
            }
            const auto* definition = type->systemverilog;
            auto result = type_width(
                definition->base, specialized, visiting);
            if (!definition->members.empty()) {
                std::uint64_t aggregate_width { };
                for (const auto& member : definition->members) {
                    const auto width = type_width(
                        member.type, specialized, visiting);
                    if (!width || *width == 0U) {
                        result.reset();
                        break;
                    }
                    if (definition->form == TypeForm::packed_union
                        || definition->form == TypeForm::tagged_union) {
                        aggregate_width = std::max(aggregate_width, *width);
                    } else if (*width
                        > std::numeric_limits<std::uint64_t>::max()
                            - aggregate_width) {
                        result.reset();
                        break;
                    } else {
                        aggregate_width += *width;
                    }
                    result = aggregate_width;
                }
                if (result
                    && definition->form == TypeForm::tagged_union) {
                    const auto tag_width = std::max<std::uint64_t>(
                        1U,
                        static_cast<std::uint64_t>(std::bit_width(
                            definition->members.size() - 1U)));
                    if (tag_width
                        > std::numeric_limits<std::uint64_t>::max()
                            - *result) {
                        result.reset();
                    } else {
                        *result += tag_width;
                    }
                }
            }
            visiting.erase(reference.target.target.value());
            return result;
        };
        const auto width_of = [&](const semantic::sv::TypeReference& reference,
                                  const semantic::SpecializedHirUnit* specialized) {
            std::unordered_set<std::uint32_t> visiting;
            return type_width(reference, specialized, visiting);
        };
        std::function<std::optional<std::pair<std::int64_t, std::int64_t>>(
            const semantic::sv::TypeReference&,
            const semantic::SpecializedHirUnit*,
            std::unordered_set<std::uint32_t>&)>
            type_bounds;
        type_bounds = [&](const semantic::sv::TypeReference& reference,
                          const semantic::SpecializedHirUnit* specialized,
                          std::unordered_set<std::uint32_t>& visiting)
            -> std::optional<std::pair<std::int64_t, std::int64_t>> {
            if (specialized != nullptr) {
                const auto effective
                    = semantic::CompiledDesignResolver { *specialized }
                          .effective_systemverilog_type(
                              reference, specialized->scope());
                if (effective && *effective != reference) {
                    return type_bounds(*effective, specialized, visiting);
                }
            }
            if (reference.packed_range) {
                const auto left = boundary_value(specialized,
                    reference.packed_range->left,
                    reference.packed_range->left_expression);
                const auto right = boundary_value(specialized,
                    reference.packed_range->right,
                    reference.packed_range->right_expression);
                if (left && right) {
                    return std::pair { *left, *right };
                }
                return std::nullopt;
            }
            if (reference.target.target.valid()
                && visiting.insert(reference.target.target.value()).second) {
                const auto type = compiled.find_type(reference.target.target);
                if (type && type->systemverilog != nullptr) {
                    const auto result = type_bounds(
                        type->systemverilog->base, specialized, visiting);
                    visiting.erase(reference.target.target.value());
                    if (result) {
                        return result;
                    }
                } else {
                    visiting.erase(reference.target.target.value());
                }
            }
            const auto width = width_of(reference, specialized);
            if (!width || *width == 0U
                || *width
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
            return std::pair {
                static_cast<std::int64_t>(*width - 1U), std::int64_t { 0 }
            };
        };
        const auto bounds_of = [&](const semantic::sv::TypeReference& reference,
                                   const semantic::SpecializedHirUnit* specialized) {
            std::unordered_set<std::uint32_t> visiting;
            return type_bounds(reference, specialized, visiting);
        };

        for (const auto* type : types) {
            const auto* specialized = owner_specialization(*type);
            if (type->form == TypeForm::packed_structure
                || type->form == TypeForm::packed_union
                || type->form == TypeForm::tagged_union) {
                std::optional<std::uint64_t> union_width;
                for (const auto& member : type->members) {
                    const auto width = width_of(member.type, specialized);
                    if (!width || *width == 0U) {
                        append("FSIM-ELAB-SVSTRUCT-001",
                            "packed aggregate member '" + member.name
                                + "' has an invalid or overflowing width",
                            member.source);
                        continue;
                    }
                    if (type->form == TypeForm::packed_union && union_width
                        && *union_width != *width) {
                        append("FSIM-ELAB-SVTYPE-007",
                            "ordinary packed union member '" + member.name
                                + "' has a different width after type and "
                                  "parameter resolution",
                            member.source);
                    }
                    if (type->form == TypeForm::packed_union) {
                        union_width = std::max(
                            union_width.value_or(0U), *width);
                    }
                }
            }
            if (type->form != TypeForm::enumeration || specialized == nullptr) {
                continue;
            }
            const auto base_width = width_of(type->base, specialized);
            if (!base_width || *base_width == 0U) {
                append("FSIM-ELAB-SVENUM-001",
                    "enum '" + type->name
                        + "' has no host-addressable base width",
                    type->source);
                continue;
            }
            if (*base_width > 64U) {
                continue;
            }
            std::unordered_map<std::uint64_t, std::string> values;
            for (const auto& literal : type->enumeration_literals) {
                const auto value = literal.value
                    ? specialized->evaluate_integral_expression(*literal.value)
                    : std::nullopt;
                if (!value) {
                    continue;
                }
                bool in_range = true;
                if (type->base.signed_value) {
                    if (*base_width < 64U) {
                        const auto maximum = (std::int64_t { 1 }
                                                 << (*base_width - 1U))
                            - 1;
                        const auto minimum = -(std::int64_t { 1 }
                            << (*base_width - 1U));
                        in_range = *value >= minimum && *value <= maximum;
                    }
                } else {
                    in_range = *value >= 0
                        && (*base_width == 64U
                            || static_cast<std::uint64_t>(*value)
                                < (std::uint64_t { 1 } << *base_width));
                }
                const auto mask = *base_width == 64U
                    ? std::numeric_limits<std::uint64_t>::max()
                    : (std::uint64_t { 1 } << *base_width) - 1U;
                const auto normalized = static_cast<std::uint64_t>(*value) & mask;
                if (!in_range) {
                    append("FSIM-ELAB-SVENUM-001",
                        "enum literal '" + literal.name
                            + "' does not fit the base type of '" + type->name
                            + "'",
                        literal.source);
                }
                const auto [duplicate, inserted] = values.emplace(
                    normalized, literal.name);
                if (!inserted) {
                    append("FSIM-ELAB-SVENUM-002",
                        "enum literals '" + duplicate->second + "' and '"
                            + literal.name + "' have the same value",
                        literal.source);
                }
            }
        }

        const auto nominal_type_identity = [&](const auto& self,
                                               const semantic::TypeId id,
                                               std::unordered_set<std::uint32_t>&
                                                   visiting)
            -> std::optional<semantic::TypeId> {
            if (!id.valid() || !visiting.insert(id.value()).second) {
                return std::nullopt;
            }
            const auto type = compiled.find_type(id);
            if (!type || type->systemverilog == nullptr) {
                return std::nullopt;
            }
            const auto& definition = *type->systemverilog;
            switch (definition.form) {
            case TypeForm::enumeration:
            case TypeForm::packed_structure:
            case TypeForm::packed_union:
            case TypeForm::unpacked_structure:
            case TypeForm::tagged_union:
            case TypeForm::unpacked_union:
                return id;
            case TypeForm::alias:
                return self(
                    self, definition.base.target.target, visiting);
            default:
                return std::nullopt;
            }
        };
        const auto nominal_type_of = [&](const semantic::sv::TypeReference& type)
            -> std::optional<semantic::TypeId> {
            std::unordered_set<std::uint32_t> visiting;
            return nominal_type_identity(
                nominal_type_identity, type.target.target, visiting);
        };
        const auto expression_nominal_type = [&](const semantic::ExpressionId id)
            -> std::optional<semantic::TypeId> {
            const auto expression = compiled.find_expression(id);
            if (!expression || expression->systemverilog == nullptr
                || !expression->systemverilog->referenced_name) {
                return std::nullopt;
            }
            const auto& name = *expression->systemverilog->referenced_name;
            auto selected = name.selected;
            if (!selected) {
                selected = semantic::CompiledDesignResolver {
                    compiled, unit.id }
                               .resolve_systemverilog_named_type(
                                   name.spelling,
                                   expression->systemverilog->scope,
                                   false)
                               .unique();
            }
            if (selected) {
                const auto declaration = compiled.find_declaration(
                    *selected);
                if (declaration && declaration->systemverilog != nullptr) {
                    const auto& selected_declaration
                        = *declaration->systemverilog;
                    if (selected_declaration.declared_type) {
                        std::unordered_set<std::uint32_t> visiting;
                        if (const auto nominal = nominal_type_identity(
                                nominal_type_identity,
                                *selected_declaration.declared_type,
                                visiting)) {
                            return nominal;
                        }
                    }
                    if (selected_declaration.type) {
                        if (const auto nominal = nominal_type_of(
                                *selected_declaration.type)) {
                            return nominal;
                        }
                    }
                    if (selected_declaration.callable
                        && selected_declaration.callable->function) {
                        if (const auto nominal = nominal_type_of(
                                selected_declaration.callable->return_type)) {
                            return nominal;
                        }
                    }
                    for (const auto* type : types) {
                        if (type->declaration != *selected) {
                            continue;
                        }
                        std::unordered_set<std::uint32_t> visiting;
                        if (const auto nominal = nominal_type_identity(
                                nominal_type_identity, type->id, visiting)) {
                            return nominal;
                        }
                    }
                    for (const auto& type : compiled.systemverilog_hir.types()) {
                        if (type.form != TypeForm::enumeration) {
                            continue;
                        }
                        const auto literal = std::ranges::find(
                            type.enumeration_literals, *selected,
                            &semantic::sv::EnumerationLiteral::declaration);
                        if (literal != type.enumeration_literals.end()) {
                            return type.id;
                        }
                    }
                }
            }
            return std::nullopt;
        };
        const auto nominal_type_name = [&](const semantic::TypeId id) {
            const auto type = compiled.find_type(id);
            return type && type->systemverilog != nullptr
                ? type->systemverilog->name
                : std::string { "<unknown>" };
        };
        const auto expression_type = [&](const semantic::ExpressionId id)
            -> const semantic::sv::TypeReference* {
            const auto expression = compiled.find_expression(id);
            if (!expression || expression->systemverilog == nullptr
                || !expression->systemverilog->referenced_name) {
                return nullptr;
            }
            const auto& reference
                = *expression->systemverilog->referenced_name;
            auto selected = reference.selected;
            if (!selected) {
                selected = semantic::CompiledDesignResolver {
                    compiled, unit.id, unit_specialized }
                               .resolve_systemverilog(
                                   reference.spelling,
                                   expression->systemverilog->scope)
                               .unique();
            }
            if (!selected) {
                return nullptr;
            }
            const auto declaration = unit_specialized != nullptr
                ? unit_specialized->find_declaration(*selected)
                : compiled.find_declaration(*selected);
            if (!declaration || declaration->systemverilog == nullptr) {
                return nullptr;
            }
            const auto& source_type = *declaration->systemverilog;
            if (source_type.type) {
                return &*source_type.type;
            }
            return source_type.callable && source_type.callable->function
                ? &source_type.callable->return_type
                : nullptr;
        };
        std::function<bool(const semantic::sv::TypeReference&,
            semantic::ExpressionId, std::string_view, std::string_view)>
            validate_nominal_value;
        validate_nominal_value = [&](const semantic::sv::TypeReference& expected,
                                     const semantic::ExpressionId value_id,
                                     const std::string_view diagnostic,
                                     const std::string_view context) {
            const auto expected_id = nominal_type_of(expected);
            if (!expected_id) {
                return true;
            }
            const auto expression = compiled.find_expression(value_id);
            if (!expression || expression->systemverilog == nullptr) {
                return true;
            }
            const auto& value = *expression->systemverilog;
            const auto actual_id = expression_nominal_type(value_id);
            if (actual_id && *actual_id == *expected_id) {
                return true;
            }
            const auto expected_type = compiled.find_type(*expected_id);
            if (!expected_type || expected_type->systemverilog == nullptr) {
                return true;
            }
            const auto& definition = *expected_type->systemverilog;
            const auto aggregate
                = definition.form == TypeForm::packed_structure
                || definition.form == TypeForm::packed_union
                || definition.form == TypeForm::unpacked_structure
                || definition.form == TypeForm::tagged_union
                || definition.form == TypeForm::unpacked_union;
            constexpr auto tagged_prefix = std::string_view { "@sv-tagged:" };
            if (!actual_id && definition.form == TypeForm::tagged_union
                && value.kind == ExpressionKind::call
                && value.text.starts_with(tagged_prefix)) {
                const auto member_name = std::string_view { value.text }.substr(
                    tagged_prefix.size());
                const auto member = std::ranges::find(
                    definition.members, member_name,
                    &semantic::sv::PackedMember::name);
                if (member == definition.members.end()) {
                    append("FSIM-ELAB-SVAGG-002",
                        "tagged-union construction names unknown member '"
                            + std::string { member_name } + "'",
                        value.source);
                    return false;
                }
                if (value.operands.size() != 1U) {
                    append("FSIM-ELAB-SVUNION-001",
                        "tagged-union construction requires one member value",
                        value.source);
                    return false;
                }
                return validate_nominal_value(
                    member->type, value.operands.front(), diagnostic, context);
            }
            if (!actual_id && aggregate
                && value.kind == ExpressionKind::assignment_pattern) {
                std::vector<bool> bound(definition.members.size());
                std::size_t positional { };
                std::optional<semantic::ExpressionId> default_value;
                bool valid = true;
                for (const auto& association : value.associations) {
                    std::optional<std::size_t> member_index;
                    if (association.choice_spelling.empty()) {
                        while (positional < bound.size()
                            && bound[positional]) {
                            ++positional;
                        }
                        if (positional < bound.size()) {
                            member_index = positional++;
                        }
                    } else if (association.choice_spelling == "default") {
                        if (default_value) {
                            append("FSIM-ELAB-SVAGG-002",
                                "assignment pattern specifies more than one "
                                "default aggregate member",
                                association.source);
                            valid = false;
                        } else {
                            default_value = association.value;
                        }
                        continue;
                    } else {
                        auto choice = std::string_view {
                            association.choice_spelling
                        };
                        if (choice == "@key"
                            && association.choices.size() == 1U) {
                            const auto key = compiled.find_expression(
                                association.choices.front());
                            if (key && key->systemverilog != nullptr) {
                                choice = key->systemverilog->text;
                            }
                        }
                        const auto member = std::ranges::find(
                            definition.members, choice,
                            &semantic::sv::PackedMember::name);
                        if (member != definition.members.end()) {
                            member_index = static_cast<std::size_t>(
                                std::distance(definition.members.begin(), member));
                        }
                    }
                    if (!member_index) {
                        append("FSIM-ELAB-SVAGG-002",
                            "assignment pattern names an unknown or malformed "
                            "aggregate member",
                            association.source);
                        valid = false;
                        continue;
                    }
                    if (bound[*member_index]) {
                        continue;
                    }
                    bound[*member_index] = true;
                    valid = validate_nominal_value(
                                definition.members[*member_index].type,
                                association.value, diagnostic, context)
                        && valid;
                }
                if (default_value) {
                    const auto union_aggregate
                        = definition.form == TypeForm::packed_union
                        || definition.form == TypeForm::unpacked_union
                        || definition.form == TypeForm::tagged_union;
                    if (union_aggregate) {
                        append("FSIM-ELAB-SVAGG-002",
                            "a union assignment pattern must select exactly "
                            "one member",
                            value.source);
                        valid = false;
                    } else {
                        for (std::size_t member { };
                             member < definition.members.size(); ++member) {
                            if (bound[member]) {
                                continue;
                            }
                            valid = validate_nominal_value(
                                        definition.members[member].type,
                                        *default_value, diagnostic, context)
                                && valid;
                        }
                    }
                }
                return valid;
            }
            const auto raw_literal = value.kind == ExpressionKind::integer_literal
                || value.kind == ExpressionKind::boolean_literal
                || value.kind == ExpressionKind::logic_literal;
            if (!actual_id && !raw_literal) {
                return true;
            }
            append(std::string { diagnostic },
                std::string { context } + " for "
                    + (aggregate ? "aggregate" : "enumeration") + " '"
                    + nominal_type_name(*expected_id)
                    + "' requires the same nominal type, a matching explicit "
                      "cast, or a contextual assignment pattern (received '"
                    + (actual_id ? nominal_type_name(*actual_id)
                                 : std::string { "<none>" })
                    + "')",
                value.source);
            return false;
        };
        const auto writable_actual = [&](const auto& self,
                                         const semantic::ExpressionId id)
            -> bool {
            const auto expression = compiled.find_expression(id);
            if (!expression || expression->systemverilog == nullptr) {
                return false;
            }
            const auto& source = *expression->systemverilog;
            if ((source.kind == ExpressionKind::index
                    || source.kind == ExpressionKind::slice)
                && !source.operands.empty()) {
                return self(self, source.operands.front());
            }
            if (source.kind != ExpressionKind::name
                || !source.referenced_name
                || !source.referenced_name->selected) {
                return false;
            }
            const auto declaration = compiled.find_declaration(
                *source.referenced_name->selected);
            if (!declaration || declaration->systemverilog == nullptr) {
                return false;
            }
            const auto form = declaration->systemverilog->form;
            return form != DeclarationForm::parameter
                && form != DeclarationForm::local_parameter
                && form != DeclarationForm::type_parameter;
        };
        const auto enclosing_callable = [&](const semantic::ScopeId scope)
            -> const semantic::sv::Declaration* {
            const semantic::sv::Declaration* owner = nullptr;
            for (const auto& candidate :
                compiled.systemverilog_hir.declarations()) {
                if (!candidate.callable || !candidate.nested_scope
                    || !scope_encloses(*candidate.nested_scope, scope)) {
                    continue;
                }
                if (owner == nullptr
                    || (owner->nested_scope
                        && scope_encloses(
                            *owner->nested_scope, *candidate.nested_scope))) {
                    owner = &candidate;
                }
            }
            return owner;
        };
        const auto static_reference_actual = [&](const auto& self,
                                                 const semantic::ExpressionId id)
            -> bool {
            const auto expression = compiled.find_expression(id);
            if (!expression || expression->systemverilog == nullptr) {
                return false;
            }
            const auto& source = *expression->systemverilog;
            if ((source.kind == ExpressionKind::index
                    || source.kind == ExpressionKind::slice)
                && !source.operands.empty()) {
                const auto* base_type = expression_type(
                    source.operands.front());
                if (base_type != nullptr
                    && (base_type->container_form
                        || base_type->value_form == TypeForm::string)) {
                    return false;
                }
                return self(self, source.operands.front());
            }
            if (source.kind != ExpressionKind::name
                || !source.referenced_name
                || !source.referenced_name->selected) {
                return false;
            }
            const auto declaration = compiled.find_declaration(
                *source.referenced_name->selected);
            if (!declaration || declaration->systemverilog == nullptr) {
                return false;
            }
            const auto& selected = *declaration->systemverilog;
            const auto* callable = enclosing_callable(selected.scope);
            if (callable == nullptr || !callable->callable) {
                return true;
            }
            if (selected.form == DeclarationForm::port) {
                return selected.static_reference;
            }
            return callable->callable->lifetime
                != semantic::sv::Lifetime::automatic;
        };
        const auto validate_associations = [&](const auto& formals,
                                               const auto& associations,
                                               const bool automatic,
                                               const bool function,
                                               const std::string_view context) {
            const auto association_code = function
                ? "FSIM-ELAB-SVFUNC-010"
                : "FSIM-ELAB-SVTASK-012";
            const auto arity_code = function
                ? "FSIM-ELAB-SVFUNC-003"
                : "FSIM-ELAB-SVTASK-005";
            const auto reference_code = function
                ? "FSIM-ELAB-SVFUNC-012"
                : "FSIM-ELAB-SVTASK-013";
            const auto static_reference_code = function
                ? "FSIM-ELAB-SVFUNC-013"
                : "FSIM-ELAB-SVTASK-015";
            std::vector<bool> bound(formals.size());
            std::size_t positional { };
            bool saw_named { };
            for (const auto& association : associations) {
                if (!association.actual) {
                    append(association_code,
                        std::string { context }
                            + " has no actual expression",
                        association.source);
                    continue;
                }
                auto formal_index = positional;
                if (!association.formal || association.formal->empty()) {
                    if (saw_named) {
                        append(association_code,
                            std::string { context }
                                + " uses a positional actual after a named actual",
                            association.source);
                    }
                    while (formal_index < bound.size()
                        && bound[formal_index]) {
                        ++formal_index;
                    }
                    positional = formal_index + 1U;
                } else {
                    saw_named = true;
                    const auto found = std::ranges::find_if(
                        formals,
                        [&](const semantic::DeclarationId formal_id) {
                            const auto formal = compiled.find_declaration(
                                formal_id);
                            return formal && formal->systemverilog != nullptr
                                && formal->systemverilog->name
                                == *association.formal;
                        });
                    if (found == formals.end()) {
                        append(association_code,
                            std::string { context } + " names unknown formal '"
                                + *association.formal + "'",
                            association.source);
                        continue;
                    }
                    formal_index = static_cast<std::size_t>(
                        std::distance(formals.begin(), found));
                }
                if (formal_index >= bound.size() || bound[formal_index]) {
                    append(association_code,
                        std::string { context }
                            + (formal_index >= bound.size()
                                    ? " supplies too many actuals"
                                    : " duplicates a formal binding"),
                        association.source);
                    continue;
                }
                bound[formal_index] = true;
                const auto formal = compiled.find_declaration(
                    formals[formal_index]);
                if (formal && formal->systemverilog != nullptr
                    && formal->systemverilog->direction
                        == semantic::sv::Direction::ref) {
                    const auto writable = automatic
                        && writable_actual(
                            writable_actual, *association.actual);
                    if (!writable) {
                        append(reference_code,
                            std::string { "a ref " }
                                + (function ? "function" : "task")
                                + " actual must be a writable variable target "
                                  "of an automatic callable",
                            association.source);
                    } else if (formal->systemverilog->static_reference
                        && !static_reference_actual(static_reference_actual,
                            *association.actual)) {
                        append(static_reference_code,
                            std::string { "a ref static " }
                                + (function ? "function" : "task")
                                + " actual must have static storage lifetime",
                            association.source);
                    }
                }
                if (!formal || formal->systemverilog == nullptr
                    || !formal->systemverilog->type) {
                    continue;
                }
                static_cast<void>(validate_nominal_value(
                    *formal->systemverilog->type, *association.actual,
                    "FSIM-ELAB-SVTYPE-004", context));
            }
            for (std::size_t index { }; index < formals.size(); ++index) {
                if (bound[index]) {
                    continue;
                }
                const auto formal = compiled.find_declaration(formals[index]);
                if (formal && formal->systemverilog != nullptr
                    && formal->systemverilog->initializer) {
                    continue;
                }
                append(association_code,
                    std::string { context }
                        + " omits a required actual without a default",
                    formal && formal->systemverilog != nullptr
                        ? formal->systemverilog->source
                        : unit.source);
                append(arity_code,
                    "a bounded function or task call has the wrong number "
                    "of arguments",
                    formal && formal->systemverilog != nullptr
                        ? formal->systemverilog->source
                        : unit.source);
            }
        };
        const auto range_boundary = [&](const semantic::sv::PackedRange& range,
                                        const bool left) {
            const auto expression = left
                ? range.left_expression
                : range.right_expression;
            const auto value = left ? range.left : range.right;
            return expression && unit_specialized != nullptr
                ? unit_specialized->evaluate_integral_expression(*expression)
                : value;
        };
        const auto range_count = [&](const semantic::sv::PackedRange& range)
            -> std::optional<std::uint64_t> {
            const auto left = range_boundary(range, true);
            const auto right = range_boundary(range, false);
            if (!left || !right) {
                return std::nullopt;
            }
            const auto distance = *left >= *right
                ? static_cast<std::uint64_t>(*left - *right)
                : static_cast<std::uint64_t>(*right - *left);
            return distance == std::numeric_limits<std::uint64_t>::max()
                ? std::nullopt
                : std::optional<std::uint64_t> { distance + 1U };
        };
        std::function<bool(const semantic::sv::TypeReference&,
            semantic::ExpressionId, std::size_t)>
            validate_static_array_pattern;
        validate_static_array_pattern
            = [&](const semantic::sv::TypeReference& type,
                  const semantic::ExpressionId value_id,
                  const std::size_t dimension) {
                  if (dimension >= type.unpacked_dimensions.size()) {
                      return true;
                  }
                  const auto value = compiled.find_expression(value_id);
                  if (!value || value->systemverilog == nullptr
                      || value->systemverilog->kind
                          != ExpressionKind::assignment_pattern) {
                      append("FSIM-ELAB-SVPATTERN-002",
                          "a multidimensional assignment pattern must provide one "
                          "nested pattern per declared dimension",
                          value && value->systemverilog != nullptr
                              ? value->systemverilog->source
                              : type.target.source);
                      return false;
                  }
                  const auto& pattern = *value->systemverilog;
                  const auto positional = std::ranges::all_of(
                      pattern.associations, [](const auto& association) {
                          return association.choice_spelling.empty();
                      });
                  const auto count = range_count(type.unpacked_dimensions[dimension]);
                  if (positional && count
                      && pattern.associations.size() != *count) {
                      append("FSIM-ELAB-SVPATTERN-002",
                          "a positional multidimensional assignment-pattern "
                          "dimension must match its declared element count",
                          pattern.source);
                      return false;
                  }
                  if (!positional
                      || dimension + 1U >= type.unpacked_dimensions.size()) {
                      return true;
                  }
                  bool valid = true;
                  for (const auto& association : pattern.associations) {
                      valid = validate_static_array_pattern(
                                  type, association.value, dimension + 1U)
                          && valid;
                  }
                  return valid;
              };
        for (const auto declaration_id : unit.declarations) {
            const auto declaration = compiled.find_declaration(declaration_id);
            if (!declaration || declaration->systemverilog == nullptr) {
                continue;
            }
            const auto& parameter = *declaration->systemverilog;
            if ((parameter.form != DeclarationForm::parameter
                    && parameter.form != DeclarationForm::local_parameter)
                || !parameter.type || !parameter.initializer) {
                continue;
            }
            if (unit_specialized != nullptr
                && hir_systemverilog_explicit_integral_type(
                    *parameter.type)) {
                std::string error;
                const auto value = evaluate_hir_systemverilog_constant(
                    *unit_specialized, *parameter.initializer, error);
                if (value && value->unbounded) {
                    append("FSIM-ELAB-SVCONST-001",
                        "symbolic unbounded '$' requires an implicit "
                        "parameter type",
                        parameter.source);
                    continue;
                }
            }
            const auto expected = nominal_type_of(*parameter.type);
            const auto actual = expression_nominal_type(*parameter.initializer);
            if (!expected || !actual || *expected == *actual) {
                continue;
            }
            const auto expression = compiled.find_expression(
                *parameter.initializer);
            append("FSIM-ELAB-SVCONST-001",
                "default for SystemVerilog parameter '" + parameter.name
                    + "' has nominal type '" + nominal_type_name(*actual)
                    + "' but requires '" + nominal_type_name(*expected) + "'",
                expression && expression->systemverilog != nullptr
                    ? expression->systemverilog->source
                    : parameter.source);
        }
        for (const auto& statement : compiled.systemverilog_hir.statements()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, statement.scope, unit.id)
                || statement.kind != semantic::sv::StatementKind::assignment
                || !statement.target || !statement.value) {
                continue;
            }
            const auto* target = expression_type(*statement.target);
            const auto value = compiled.find_expression(*statement.value);
            constexpr auto tagged_prefix = std::string_view { "@sv-tagged:" };
            if (value && value->systemverilog != nullptr
                && value->systemverilog->kind == ExpressionKind::call
                && value->systemverilog->text.starts_with(tagged_prefix)) {
                const auto nominal = target != nullptr
                    ? nominal_type_of(*target)
                    : std::nullopt;
                const auto definition = nominal
                    ? compiled.find_type(*nominal)
                    : std::nullopt;
                if (!definition || definition->systemverilog == nullptr
                    || definition->systemverilog->form
                        != TypeForm::tagged_union) {
                    append("FSIM-ELAB-SVUNION-001",
                        "tagged-union construction requires a contextual "
                        "tagged-union type",
                        value->systemverilog->source);
                    continue;
                }
            }
            const auto assignment_target_expression = compiled.find_expression(
                *statement.target);
            const bool whole_container_pattern = target != nullptr
                && target->container_form
                && assignment_target_expression
                && assignment_target_expression->systemverilog != nullptr
                && assignment_target_expression->systemverilog->kind
                    == ExpressionKind::name
                && value && value->systemverilog != nullptr
                && value->systemverilog->kind
                    == ExpressionKind::assignment_pattern;
            if (target != nullptr && !whole_container_pattern) {
                static_cast<void>(validate_nominal_value(
                    *target, *statement.value, "FSIM-ELAB-SVTYPE-004",
                    "assignment"));
            }
            auto base_id = *statement.target;
            std::vector<semantic::ExpressionId> indices;
            for (;;) {
                const auto selection = compiled.find_expression(base_id);
                if (!selection || selection->systemverilog == nullptr
                    || selection->systemverilog->kind
                        != ExpressionKind::index
                    || selection->systemverilog->operands.size() != 2U) {
                    break;
                }
                indices.push_back(selection->systemverilog->operands.back());
                base_id = selection->systemverilog->operands.front();
            }
            std::ranges::reverse(indices);
            const auto* base_type = expression_type(base_id);
            const auto* assigned_type = expression_type(
                *statement.value);
            if (base_type != nullptr
                && !base_type->unpacked_dimensions.empty()) {
                if (indices.empty() && value
                    && value->systemverilog != nullptr
                    && value->systemverilog->kind
                        == ExpressionKind::assignment_pattern) {
                    static_cast<void>(validate_static_array_pattern(
                        *base_type, *statement.value, 0U));
                } else if (!indices.empty()
                    && indices.size()
                        < base_type->unpacked_dimensions.size()
                    && (!value || value->systemverilog == nullptr
                        || value->systemverilog->kind
                            != ExpressionKind::assignment_pattern)
                    && !(assigned_type != nullptr
                        && assigned_type->unpacked_dimensions.size()
                            == base_type->unpacked_dimensions.size()
                                - indices.size())
                    && std::ranges::none_of(
                        compiled.systemverilog_hir.instances(),
                        [&](const semantic::sv::Instance& instance) {
                            return instance.source == statement.source;
                        })) {
                    append("FSIM-ELAB-SVMDARRAY-001",
                        "a multidimensional static-array element access must "
                        "supply exactly one index per declared dimension",
                        statement.source);
                } else if (indices.size()
                    >= base_type->unpacked_dimensions.size()) {
                    for (std::size_t dimension { };
                        dimension < base_type->unpacked_dimensions.size();
                        ++dimension) {
                        const auto index = unit_specialized != nullptr
                            ? unit_specialized->evaluate_integral_expression(
                                  indices[dimension])
                            : std::nullopt;
                        const auto left = range_boundary(
                            base_type->unpacked_dimensions[dimension], true);
                        const auto right = range_boundary(
                            base_type->unpacked_dimensions[dimension], false);
                        if (!index || !left || !right
                            || (*index >= std::min(*left, *right)
                                && *index <= std::max(*left, *right))) {
                            continue;
                        }
                        const auto index_expression = compiled.find_expression(
                            indices[dimension]);
                        append("FSIM-ELAB-SVMDARRAY-003",
                            "multidimensional static-array index is outside "
                            "its declared range",
                            index_expression
                                    && index_expression->systemverilog != nullptr
                                ? index_expression->systemverilog->source
                                : statement.source);
                    }
                }
            }
            const auto target_expression = compiled.find_expression(
                *statement.target);
            if ((!statement.delay
                    && statement.assignment_control
                        == semantic::sv::AssignmentControl::none)
                || !target_expression
                || target_expression->systemverilog == nullptr) {
                continue;
            }
            const auto target_name = std::string_view {
                target_expression->systemverilog->text
            };
            const auto member_separator = target_name.find('.');
            if (member_separator == std::string_view::npos) {
                continue;
            }
            const auto base_name = target_name.substr(0U, member_separator);
            const auto typed_object
                = [](const semantic::CompiledDeclarationView& candidate) {
                      return candidate.systemverilog != nullptr
                          && candidate.systemverilog->type.has_value();
                  };
            const auto selected
                = semantic::CompiledDesignResolver {
                      compiled, unit.id, unit_specialized }
                      .resolve_systemverilog(base_name,
                          target_expression->systemverilog->scope,
                          typed_object, false)
                      .unique();
            if (!selected) {
                continue;
            }
            const auto declaration = unit_specialized != nullptr
                ? unit_specialized->find_declaration(*selected)
                : compiled.find_declaration(*selected);
            if (!declaration || declaration->systemverilog == nullptr
                || !declaration->systemverilog->type) {
                continue;
            }
            const auto nominal = nominal_type_of(
                *declaration->systemverilog->type);
            const auto definition = nominal
                ? compiled.find_type(*nominal)
                : std::nullopt;
            if (definition && definition->systemverilog != nullptr
                && definition->systemverilog->form
                    == TypeForm::tagged_union) {
                append("FSIM-ELAB-SVUNION-001",
                    "selected union writes require a time-free scalar "
                    "assignment",
                    statement.source);
            }
        }
        std::unordered_set<std::uint32_t> unreachable_expressions;
        std::unordered_set<std::uint32_t> unreachable_statements;
        std::unordered_map<std::uint32_t, std::size_t>
            unselected_expression_operands;
        const auto visit_delay_expressions = [&](const auto& self,
                                                     const semantic::sv::Delay& delay,
                                                     const auto& visit) -> void {
            const auto visit_value = [&](
                                             const semantic::sv::DelayValue& value) {
                if (value.expression) {
                    visit(*value.expression);
                }
            };
            visit_value(delay.primary);
            if (delay.minimum) {
                visit_value(*delay.minimum);
            }
            if (delay.typical) {
                visit_value(*delay.typical);
            }
            if (delay.maximum) {
                visit_value(*delay.maximum);
            }
            for (const auto& additional : delay.additional) {
                self(self, additional, visit);
            }
        };
        const auto visit_statement_expressions = [&](
                                                       const semantic::sv::Statement& source,
                                                       const auto& visit) {
            const auto visit_optional = [&](const auto expression) {
                if (expression) {
                    visit(*expression);
                }
            };
            visit_optional(source.target);
            visit_optional(source.value);
            visit_optional(source.condition);
            visit_optional(source.loop_initial);
            visit_optional(source.loop_limit);
            visit_optional(source.loop_update_target);
            visit_optional(source.clocking_cycle_count);
            visit_optional(source.verilog_switch_source);
            visit_optional(source.verilog_switch_control);
            visit_optional(source.file_handle);
            for (const auto& argument : source.task_arguments) {
                visit_optional(argument.actual);
            }
            for (const auto& sensitivity : source.sensitivities) {
                visit_optional(sensitivity.expression);
            }
            for (const auto& output : source.output_values) {
                visit_optional(output.value);
            }
            if (source.delay) {
                visit_delay_expressions(
                    visit_delay_expressions, *source.delay, visit);
            }
            for (const auto& alternative : source.case_alternatives) {
                for (const auto choice : alternative.choices) {
                    visit(choice);
                }
            }
        };
        std::function<void(semantic::ExpressionId)> mark_expression;
        mark_expression = [&](const semantic::ExpressionId expression_id) {
            if (!expression_id.valid()
                || !unreachable_expressions.insert(expression_id.value()).second) {
                return;
            }
            const auto expression = compiled.find_expression(expression_id);
            if (!expression || expression->systemverilog == nullptr) {
                return;
            }
            for (const auto operand : expression->systemverilog->operands) {
                mark_expression(operand);
            }
            for (const auto& argument :
                expression->systemverilog->call_arguments) {
                if (argument.actual) {
                    mark_expression(*argument.actual);
                }
            }
            for (const auto& association :
                expression->systemverilog->associations) {
                mark_expression(association.value);
                for (const auto choice : association.choices) {
                    mark_expression(choice);
                }
            }
        };
        std::function<void(semantic::StatementId)> mark_statement;
        mark_statement = [&](const semantic::StatementId statement_id) {
            if (!statement_id.valid()
                || !unreachable_statements.insert(
                    statement_id.value()).second) {
                return;
            }
            const auto statement = compiled.find_statement(statement_id);
            if (!statement || statement->systemverilog == nullptr) {
                return;
            }
            const auto& source = *statement->systemverilog;
            visit_statement_expressions(source, mark_expression);
            for (const auto nested : source.loop_updates) {
                mark_statement(nested);
            }
            for (const auto nested : source.statements) {
                mark_statement(nested);
            }
            for (const auto nested : source.else_statements) {
                mark_statement(nested);
            }
            for (const auto& alternative : source.case_alternatives) {
                for (const auto choice : alternative.choices) {
                    mark_expression(choice);
                }
                for (const auto nested : alternative.statements) {
                    mark_statement(nested);
                }
            }
        };
        const auto evaluate_specialization_integer =
            [&](const semantic::ExpressionId expression_id)
            -> std::optional<std::int64_t> {
            const auto value
                = unit_specialized->evaluate_integral_expression(
                    expression_id);
            if (value) {
                return value;
            }
            const auto expression = compiled.find_expression(expression_id);
            if (!expression || expression->systemverilog == nullptr
                || !expression->systemverilog->referenced_name
                || !expression->systemverilog->referenced_name->selected) {
                return std::nullopt;
            }
            const auto declaration = compiled.find_declaration(
                *expression->systemverilog->referenced_name->selected);
            if (!declaration || declaration->systemverilog == nullptr
                || (declaration->systemverilog->form
                        != DeclarationForm::parameter
                    && declaration->systemverilog->form
                        != DeclarationForm::local_parameter
                    && declaration->systemverilog->form
                        != DeclarationForm::enumeration_literal)) {
                return std::nullopt;
            }
            return unit_specialized->evaluate_integral_declaration(
                declaration->systemverilog->id);
        };
        if (unit_specialized != nullptr) {
            for (const auto& statement :
                compiled.systemverilog_hir.statements()) {
                if (statement.kind
                        != semantic::sv::StatementKind::conditional
                    || !statement.condition
                    || !compiled_scope_belongs_to_unit(
                        compiled, statement.scope, unit.id)) {
                    continue;
                }
                const auto constant = evaluate_specialization_integer(
                    *statement.condition);
                if (!constant) {
                    continue;
                }
                const auto& unreachable = *constant != 0
                    ? statement.else_statements
                    : statement.statements;
                for (const auto nested : unreachable) {
                    mark_statement(nested);
                }
            }
            // A specialization-known expression conditional has the same
            // reachability boundary as a conditional statement. Exclude only
            // the unselected arm from shape validation; the selected arm and
            // the conditional expression itself remain fully validated.
            for (const auto& expression :
                compiled.systemverilog_hir.expressions()) {
                if (!compiled_scope_belongs_to_unit(
                        compiled, expression.scope, unit.id)
                    || expression.kind != semantic::sv::ExpressionKind::call
                    || expression.text != "?:"
                    || expression.operands.size() != 3U) {
                    continue;
                }
                const auto constant = evaluate_specialization_integer(
                    expression.operands[0]);
                if (!constant) {
                    continue;
                }
                const auto unreachable_operand = *constant != 0 ? 2U : 1U;
                unselected_expression_operands.insert_or_assign(
                    expression.id.value(), unreachable_operand);
                mark_expression(
                    expression.operands[unreachable_operand]);
            }
        }

        // Expression IDs can have more than one incoming edge. Treat the
        // pruned subgraphs above as candidates, then restore every candidate
        // reached by a live expression or statement edge. In particular,
        // pruning one conditional arm must not hide the same ID in a selected
        // arm, a different expression root, or an active statement.
        std::unordered_set<std::uint32_t> live_expressions;
        std::unordered_set<std::uint32_t> live_statements;
        std::function<void(semantic::ExpressionId)> mark_live_expression;
        mark_live_expression = [&](const semantic::ExpressionId expression_id) {
            if (!expression_id.valid()
                || !live_expressions.insert(expression_id.value()).second) {
                return;
            }
            const auto expression = compiled.find_expression(expression_id);
            if (!expression || expression->systemverilog == nullptr) {
                return;
            }
            const auto pruned_operand = unselected_expression_operands.find(
                expression_id.value());
            std::unordered_set<std::uint32_t> nested_expression_ids;
            for (const auto operand : expression->systemverilog->operands) {
                nested_expression_ids.insert(operand.value());
            }
            for (std::size_t index { };
                index < expression->systemverilog->operands.size(); ++index) {
                if (pruned_operand != unselected_expression_operands.end()
                    && pruned_operand->second == index) {
                    continue;
                }
                mark_live_expression(
                    expression->systemverilog->operands[index]);
            }
            for (const auto& argument :
                expression->systemverilog->call_arguments) {
                if (argument.actual
                    && nested_expression_ids.insert(
                        argument.actual->value()).second) {
                    mark_live_expression(*argument.actual);
                }
            }
            for (const auto& association :
                expression->systemverilog->associations) {
                if (nested_expression_ids.insert(
                        association.value.value()).second) {
                    mark_live_expression(association.value);
                }
                for (const auto choice : association.choices) {
                    if (nested_expression_ids.insert(choice.value()).second) {
                        mark_live_expression(choice);
                    }
                }
            }
        };

        std::function<void(semantic::StatementId)> mark_live_statement;
        mark_live_statement = [&](const semantic::StatementId statement_id) {
            if (!statement_id.valid()
                || !live_statements.insert(statement_id.value()).second) {
                return;
            }
            const auto statement = compiled.find_statement(statement_id);
            if (!statement || statement->systemverilog == nullptr
                || !compiled_scope_belongs_to_unit(
                    compiled, statement->systemverilog->scope, unit.id)) {
                return;
            }
            const auto& source = *statement->systemverilog;
            visit_statement_expressions(source, mark_live_expression);

            const std::vector<semantic::StatementId>* selected { };
            if (unit_specialized != nullptr
                && source.kind == semantic::sv::StatementKind::conditional
                && source.condition) {
                const auto constant = evaluate_specialization_integer(
                    *source.condition);
                if (constant) {
                    selected = *constant != 0
                        ? &source.statements
                        : &source.else_statements;
                }
            }
            const auto mark_live_children = [&](
                                                  const auto& children) {
                for (const auto child : children) {
                    mark_live_statement(child);
                }
            };
            if (selected != nullptr) {
                mark_live_children(*selected);
            } else {
                mark_live_children(source.statements);
                mark_live_children(source.else_statements);
            }
            mark_live_children(source.loop_updates);
            for (const auto& alternative : source.case_alternatives) {
                mark_live_children(alternative.statements);
            }
        };

        const auto mark_live_process = [&](const semantic::ProcessId process_id) {
            const auto process = compiled.find_process(process_id);
            if (!process || process->systemverilog == nullptr
                || !compiled_scope_belongs_to_unit(
                    compiled, process->systemverilog->scope, unit.id)) {
                return;
            }
            for (const auto& sensitivity :
                process->systemverilog->sensitivities) {
                if (sensitivity.expression) {
                    mark_live_expression(*sensitivity.expression);
                }
            }
            for (const auto statement : process->systemverilog->statements) {
                mark_live_statement(statement);
            }
        };

        const auto mark_live_declaration =
            [&](const semantic::DeclarationId declaration_id) {
                const auto declaration = compiled.find_declaration(
                    declaration_id);
                if (!declaration || declaration->systemverilog == nullptr
                    || !compiled_scope_belongs_to_unit(
                        compiled, declaration->systemverilog->scope,
                        unit.id)) {
                    return;
                }
                const auto& source = *declaration->systemverilog;
                if (source.initializer) {
                    mark_live_expression(*source.initializer);
                }
                if (source.delay) {
                    visit_delay_expressions(
                        visit_delay_expressions, *source.delay,
                        mark_live_expression);
                }
                for (const auto statement : source.statements) {
                    mark_live_statement(statement);
                }
            };

        std::unordered_set<std::uint32_t> expression_referenced;
        for (const auto& expression :
            compiled.systemverilog_hir.expressions()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, expression.scope, unit.id)) {
                continue;
            }
            for (const auto operand : expression.operands) {
                expression_referenced.insert(operand.value());
            }
            for (const auto& argument : expression.call_arguments) {
                if (argument.actual) {
                    expression_referenced.insert(argument.actual->value());
                }
            }
            for (const auto& association : expression.associations) {
                expression_referenced.insert(association.value.value());
                for (const auto choice : association.choices) {
                    expression_referenced.insert(choice.value());
                }
            }
        }
        for (const auto& expression :
            compiled.systemverilog_hir.expressions()) {
            if (compiled_scope_belongs_to_unit(
                    compiled, expression.scope, unit.id)
                && !unreachable_expressions.contains(expression.id.value())
                && !expression_referenced.contains(expression.id.value())) {
                mark_live_expression(expression.id);
            }
        }
        for (const auto& statement :
            compiled.systemverilog_hir.statements()) {
            if (compiled_scope_belongs_to_unit(
                    compiled, statement.scope, unit.id)
                && !unreachable_statements.contains(statement.id.value())) {
                mark_live_statement(statement.id);
            }
        }
        for (const auto& alias : unit.aliases) {
            for (const auto terminal : alias.terminals) {
                mark_live_expression(terminal);
            }
        }
        for (const auto& let : unit.lets) {
            mark_live_expression(let.expression);
            for (const auto& port : let.ports) {
                if (port.default_value) {
                    mark_live_expression(*port.default_value);
                }
            }
        }
        for (const auto& defparam : unit.defparams) {
            mark_live_expression(defparam.value);
            for (const auto& segment : defparam.path) {
                for (const auto index : segment.indices) {
                    mark_live_expression(index);
                }
            }
        }
        for (const auto statement : unit.concurrent_statements) {
            mark_live_statement(statement);
        }
        for (const auto process_id : unit.processes) {
            mark_live_process(process_id);
        }
        for (const auto& declaration :
            compiled.systemverilog_hir.declarations()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, declaration.scope, unit.id)) {
                continue;
            }
            mark_live_declaration(declaration.id);
        }
        for (const auto expression_id : live_expressions) {
            unreachable_expressions.erase(expression_id);
        }
        for (const auto statement_id : live_statements) {
            unreachable_statements.erase(statement_id);
        }

        for (const auto& expression : compiled.systemverilog_hir.expressions()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, expression.scope, unit.id)
                || unreachable_expressions.contains(expression.id.value())) {
                continue;
            }
            constexpr auto cast_prefix = std::string_view { "@sv-cast:" };
            const auto streaming = expression.kind == ExpressionKind::call
                && (expression.text == "@stream-left"
                    || expression.text == "@stream-right"
                    || expression.text == "@stream-target-left"
                    || expression.text == "@stream-target-right");
            if (streaming) {
                const auto assignment_target
                    = expression.text == "@stream-target-left"
                    || expression.text == "@stream-target-right";
                if (expression.operands.size() < 2U
                    || (assignment_target
                        && expression.operands.size() != 2U)) {
                    append("FSIM-ELAB-SVEXPR-001",
                        "streaming concatenation requires a slice size and "
                        "at least one packed operand",
                        expression.source);
                } else {
                    const auto slice_size = unit_specialized != nullptr
                        ? unit_specialized->evaluate_integral_expression(
                              expression.operands.front())
                        : std::nullopt;
                    if (!slice_size || *slice_size <= 0
                        || static_cast<std::uint64_t>(*slice_size)
                            > std::numeric_limits<std::size_t>::max()) {
                        append("FSIM-ELAB-SVEXPR-002",
                            "streaming concatenation requires a positive "
                            "locally constant host-addressable slice size",
                            expression.source);
                    }
                    for (std::size_t index = 1U;
                        index < expression.operands.size(); ++index) {
                        const auto* type = expression_type(
                            expression.operands[index]);
                        if (type == nullptr || !type->container_form) {
                            continue;
                        }
                        append("FSIM-ELAB-SVEXPR-003",
                            "streaming concatenation supports only fixed-width "
                            "packed integral operands",
                            expression.source);
                    }
                }
            }
            if (expression.kind == ExpressionKind::call
                && expression.text.starts_with(cast_prefix)) {
                const auto cast_type = std::string_view { expression.text }.substr(
                    cast_prefix.size());
                const auto cast_size_declaration
                    = [](const semantic::CompiledDeclarationView& candidate) {
                          if (candidate.systemverilog == nullptr) {
                              return false;
                          }
                          const auto form = candidate.systemverilog->form;
                          return form == DeclarationForm::parameter
                              || form == DeclarationForm::local_parameter;
                      };
                const auto cast_sizes
                    = semantic::CompiledDesignResolver {
                          compiled, unit.id, unit_specialized }
                          .resolve_systemverilog(cast_type,
                              expression.scope, cast_size_declaration,
                              false);
                const auto positive_cast_size
                    = unit_specialized != nullptr
                    && std::ranges::any_of(cast_sizes.candidates,
                        [&](const semantic::DeclarationId declaration) {
                            return unit_specialized
                                       ->evaluate_integral_declaration(
                                           declaration)
                                       .value_or(0)
                                > 0;
                        });
                std::uint32_t numeric_cast_size { };
                const auto parsed_numeric_cast_size = std::from_chars(
                    cast_type.data(), cast_type.data() + cast_type.size(),
                    numeric_cast_size);
                const auto positive_numeric_cast_size
                    = !cast_type.empty()
                    && std::ranges::all_of(cast_type, [](const char value) {
                           return value >= '0' && value <= '9';
                       })
                    && parsed_numeric_cast_size.ec == std::errc { }
                    && parsed_numeric_cast_size.ptr
                        == cast_type.data() + cast_type.size()
                    && numeric_cast_size != 0U
                    && numeric_cast_size
                        <= hir_systemverilog_maximum_constant_width;
                const auto visible = builtin_type(cast_type)
                    || cast_type.find("::") != std::string_view::npos
                    || resolved_type_visible(cast_type, expression.scope)
                    || std::ranges::any_of(
                        types, [&](const semantic::sv::TypeDefinition* type) {
                            return type->name == cast_type;
                        })
                    || positive_cast_size
                    || positive_numeric_cast_size;
                if (!visible) {
                    append("FSIM-ELAB-SVCAST-002",
                        "SystemVerilog cast type '" + std::string { cast_type }
                            + "' is not visible",
                        expression.source);
                }
            }
            const auto* receiver_type = !expression.operands.empty()
                ? expression_type(expression.operands.front())
                : nullptr;
            const bool legacy_container_method
                = receiver_type != nullptr
                && receiver_type->container_form
                && (expression.text == ".delete"
                    || expression.text == ".exists"
                    || expression.text == ".first"
                    || expression.text == ".last"
                    || expression.text == ".next"
                    || expression.text == ".prev"
                    || expression.text == ".num"
                    || expression.text == ".size");
            const bool container_method_call
                = expression.text.starts_with("@sv-container-method:")
                || legacy_container_method;
            if (expression.kind == ExpressionKind::call
                && expression.referenced_name
                && !container_method_call) {
                auto selected = expression.referenced_name->selected;
                if (!selected
                    && expression.referenced_name->overloads.size() == 1U) {
                    selected = expression.referenced_name->overloads.front();
                }
                const auto callable = selected
                    ? compiled.find_declaration(*selected)
                    : std::nullopt;
                if (callable && callable->systemverilog != nullptr
                    && callable->systemverilog->callable) {
                    std::vector<semantic::sv::CallAssociation>
                        synthesized_arguments;
                    auto arguments = std::span {
                        expression.call_arguments
                    };
                    if (arguments.empty()
                        && !expression.operands.empty()) {
                        synthesized_arguments.reserve(
                            expression.operands.size());
                        for (std::size_t index { };
                            index < expression.operands.size(); ++index) {
                            semantic::sv::CallAssociation association;
                            if (index < expression.argument_names.size()
                                && !expression.argument_names[index].empty()) {
                                association.formal
                                    = expression.argument_names[index];
                            }
                            association.actual = expression.operands[index];
                            association.source = expression.source;
                            synthesized_arguments.push_back(
                                std::move(association));
                        }
                        arguments = synthesized_arguments;
                    }
                    if (!expression.argument_names.empty()
                        && expression.argument_names.size()
                            != expression.operands.size()) {
                        append("FSIM-ELAB-SVFUNC-010",
                            "function actual association metadata does not "
                            "match the operand list",
                            expression.source);
                    }
                    validate_associations(
                        callable->systemverilog->callable->formals,
                        arguments,
                        callable->systemverilog->callable->lifetime
                            == semantic::sv::Lifetime::automatic,
                        true,
                        "call argument");
                }
            }
            if (expression.kind == ExpressionKind::slice
                && (expression.text == "+:" || expression.text == "-:")
                && expression.operands.size() == 3U) {
                const auto width = unit_specialized != nullptr
                    ? unit_specialized->evaluate_integral_expression(
                          expression.operands[2])
                    : std::nullopt;
                if (!width || *width <= 0
                    || static_cast<std::uint64_t>(*width)
                        > std::numeric_limits<std::uint32_t>::max()) {
                    append("FSIM-ELAB-SVEXPR-004",
                        "a runtime-base packed part-select width must be a "
                        "positive locally constant execution width",
                        expression.source);
                }
            }
            if (expression.kind != ExpressionKind::binary
                || (expression.text != "==" && expression.text != "!="
                    && expression.text != "==="
                    && expression.text != "!==")
                || expression.operands.size() != 2U) {
                continue;
            }
            const auto left = expression_nominal_type(
                expression.operands.front());
            const auto right = expression_nominal_type(
                expression.operands.back());
            if ((!left && !right) || (left && right && *left == *right)) {
                continue;
            }
            // An enumeration value may be compared with an ordinary
            // integral expression. Distinct enumeration types remain
            // nominally incompatible, as do packed aggregates compared
            // with untyped packed operands.
            if (left.has_value() != right.has_value()) {
                const auto nominal = compiled.find_type(
                    left ? *left : *right);
                if (nominal && nominal->systemverilog != nullptr
                    && nominal->systemverilog->form
                        == TypeForm::enumeration) {
                    continue;
                }
            }
            append("FSIM-ELAB-SVTYPE-005",
                "nominal packed equality requires two values of the same "
                "nominal SystemVerilog type",
                expression.source);
        }
        for (const auto* type : types) {
            for (const auto& member : type->members) {
                if (!member.initializer) {
                    continue;
                }
                const auto expected_id = nominal_type_of(member.type);
                const auto expected = expected_id
                    ? compiled.find_type(*expected_id)
                    : std::nullopt;
                const auto initializer = compiled.find_expression(
                    *member.initializer);
                if (!expected || expected->systemverilog == nullptr
                    || expected->systemverilog->members.empty()
                    || !initializer || initializer->systemverilog == nullptr
                    || initializer->systemverilog->kind
                        != ExpressionKind::assignment_pattern) {
                    continue;
                }
                std::vector<bool> bound(
                    expected->systemverilog->members.size());
                std::size_t positional { };
                bool has_default { };
                for (const auto& association :
                    initializer->systemverilog->associations) {
                    if (association.choice_spelling.empty()) {
                        while (positional < bound.size()
                            && bound[positional]) {
                            ++positional;
                        }
                        if (positional < bound.size()) {
                            bound[positional++] = true;
                        }
                        continue;
                    }
                    if (association.choice_spelling == "default") {
                        has_default = true;
                        continue;
                    }
                    if (association.choice_spelling != "@key"
                        || association.choices.size() != 1U) {
                        continue;
                    }
                    const auto key = compiled.find_expression(
                        association.choices.front());
                    if (!key || key->systemverilog == nullptr) {
                        continue;
                    }
                    const auto selected = std::ranges::find(
                        expected->systemverilog->members,
                        key->systemverilog->text,
                        &semantic::sv::PackedMember::name);
                    if (selected != expected->systemverilog->members.end()) {
                        bound[static_cast<std::size_t>(std::distance(
                            expected->systemverilog->members.begin(),
                            selected))] = true;
                    }
                }
                if (!has_default
                    && std::ranges::find(bound, false) != bound.end()) {
                    append("FSIM-ELAB-SVAGG-007",
                        "cannot evaluate initializer for packed member '"
                            + member.name
                            + "': assignment pattern leaves a member unset",
                        initializer->systemverilog->source);
                }
            }
        }
        for (const auto& statement : compiled.systemverilog_hir.statements()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, statement.scope, unit.id)) {
                continue;
            }
            if (statement.kind == semantic::sv::StatementKind::task_call) {
                auto selected = statement.task.selected;
                if (!selected && statement.task.overloads.size() == 1U) {
                    selected = statement.task.overloads.front();
                }
                const auto callable = selected
                    ? compiled.find_declaration(*selected)
                    : std::nullopt;
                if (callable && callable->systemverilog != nullptr
                    && callable->systemverilog->callable) {
                    validate_associations(
                        callable->systemverilog->callable->formals,
                        statement.task_arguments,
                        callable->systemverilog->callable->lifetime
                            == semantic::sv::Lifetime::automatic,
                        false,
                        "task argument");
                }
                continue;
            }
            if (statement.kind
                    != semantic::sv::StatementKind::return_statement
                || !statement.value) {
                continue;
            }
            const semantic::sv::Declaration* owner = nullptr;
            for (const auto& candidate :
                compiled.systemverilog_hir.declarations()) {
                if (!compiled_scope_belongs_to_unit(
                        compiled, candidate.scope, unit.id)
                    || !candidate.callable || !candidate.callable->function
                    || !candidate.nested_scope
                    || !scope_encloses(
                        *candidate.nested_scope, statement.scope)) {
                    continue;
                }
                if (owner == nullptr
                    || (owner->nested_scope
                        && scope_encloses(
                            *owner->nested_scope, *candidate.nested_scope))) {
                    owner = &candidate;
                }
            }
            if (owner != nullptr) {
                static_cast<void>(validate_nominal_value(
                    owner->callable->return_type, *statement.value,
                    "FSIM-ELAB-SVTYPE-004", "function return"));
            }
        }

        const auto* owner_specialized = specialization_for(unit.id);
        if (owner_specialized != nullptr) {
            for (const auto instance_id : unit.instances) {
                const auto instance = compiled.find_instance(instance_id);
                if (!instance) {
                    continue;
                }
                const auto occurrence = resolve_compiled_occurrence(
                    *owner_specialized, *instance);
                if (!occurrence || !occurrence->linked_target
                    || occurrence->linked_target->systemverilog == nullptr) {
                    continue;
                }
                const auto bindings = semantic::resolve_specialized_hir_associations(
                    compiled, occurrence->linked_target->systemverilog->id,
                    *instance,
                    semantic::SpecializedHirAssociationSurface::ports,
                    owner_specialized);
                for (const auto& binding : bindings.bindings) {
                    if (binding.kind
                            != semantic::SpecializedHirAssociationKind::expression
                        || !binding.expression) {
                        continue;
                    }
                    const auto formal = compiled.find_declaration(
                        binding.formal);
                    if (!formal || formal->systemverilog == nullptr
                        || !formal->systemverilog->type) {
                        continue;
                    }
                    const auto expected = nominal_type_of(
                        *formal->systemverilog->type);
                    const auto type = expected
                        ? compiled.find_type(*expected)
                        : std::nullopt;
                    const auto diagnostic = type
                            && type->systemverilog != nullptr
                            && type->systemverilog->form
                                == TypeForm::enumeration
                        ? std::string_view { "FSIM-ELAB-BIND-053" }
                        : std::string_view { "FSIM-ELAB-BIND-057" };
                    static_cast<void>(validate_nominal_value(
                        *formal->systemverilog->type,
                        *binding.expression, diagnostic,
                        "port association"));
                }
            }
        }

        for (const auto* package : packages) {
            const auto* specialized = specialization_for(package->id);
            if (specialized == nullptr) {
                continue;
            }
            for (const auto declaration_id : package->declarations) {
                const auto declaration = compiled.find_declaration(declaration_id);
                if (!declaration || declaration->systemverilog == nullptr
                    || (declaration->systemverilog->form
                            != DeclarationForm::parameter
                        && declaration->systemverilog->form
                            != DeclarationForm::local_parameter)
                    || !declaration->systemverilog->initializer
                    || (declaration->systemverilog->type
                        && declaration->systemverilog->type->value_form
                            == TypeForm::string)
                    || !compiled_systemverilog_integral_constant_type(
                        declaration->systemverilog->type)
                    || specialized->evaluate_integral_declaration(
                        declaration_id)) {
                    continue;
                }
                const auto initializer = specialized->find_expression(
                    *declaration->systemverilog->initializer);
                if (initializer
                    && initializer->systemverilog != nullptr
                    && initializer->systemverilog->kind
                        == ExpressionKind::assignment_pattern
                    && declaration->systemverilog->type) {
                    static_cast<void>(validate_nominal_value(
                        *declaration->systemverilog->type,
                        *declaration->systemverilog->initializer,
                        "FSIM-ELAB-SVTYPE-004",
                        "package constant initializer"));
                    continue;
                }
                const auto width = declaration->systemverilog->type
                    ? width_of(*declaration->systemverilog->type, specialized)
                    : std::nullopt;
                if (width && *width > 64U && initializer
                    && initializer->systemverilog != nullptr
                    && (initializer->systemverilog->kind
                            == ExpressionKind::integer_literal
                        || initializer->systemverilog->kind
                            == ExpressionKind::logic_literal
                        || initializer->systemverilog->kind
                            == ExpressionKind::boolean_literal)) {
                    continue;
                }
                append("FSIM-ELAB-SVPKG-006",
                    "cannot evaluate default for SystemVerilog package "
                    "parameter '"
                        + declaration->systemverilog->name + "'",
                    declaration->systemverilog->source);
            }
        }

        const auto* root_specialization = specialization_for(unit.id);
        const auto resolver = semantic::CompiledDesignResolver {
            compiled, unit.id, root_specialization };
        const auto find_declaration = [&](const std::string_view name)
            -> const semantic::sv::Declaration* {
            const auto selected = resolver.resolve_systemverilog(
                                              name, unit.scope,
                                              [&](const auto& candidate) {
                                                  return candidate.systemverilog
                                                      != nullptr
                                                      && compiled_scope_belongs_to_unit(
                                                          compiled,
                                                          candidate.systemverilog
                                                              ->scope,
                                                          unit.id);
                                              },
                                              false)
                                      .unique();
            if (!selected) {
                return nullptr;
            }
            const auto declaration = root_specialization != nullptr
                ? root_specialization->find_declaration(*selected)
                : compiled.find_declaration(*selected);
            return declaration && declaration->systemverilog != nullptr
                ? declaration->systemverilog
                : nullptr;
        };
        const auto selected_member = [&](const semantic::sv::Expression& expression)
            -> std::pair<const semantic::sv::PackedMember*,
                const semantic::sv::TypeDefinition*> {
            const auto separator = expression.text.find('.');
            if (separator == std::string::npos) {
                return { nullptr, nullptr };
            }
            const auto* declaration = find_declaration(
                std::string_view { expression.text }.substr(0U, separator));
            if (declaration == nullptr || !declaration->type
                || !declaration->type->target.target.valid()) {
                return { nullptr, nullptr };
            }
            const auto type = compiled.find_type(
                declaration->type->target.target);
            if (!type || type->systemverilog == nullptr) {
                return { nullptr, nullptr };
            }
            const auto member_name = std::string_view { expression.text }.substr(
                separator + 1U);
            const auto nested = member_name.find('.');
            const auto simple_name = member_name.substr(0U, nested);
            const auto member = std::ranges::find(
                type->systemverilog->members, simple_name,
                &semantic::sv::PackedMember::name);
            return member == type->systemverilog->members.end()
                ? std::pair<const semantic::sv::PackedMember*,
                      const semantic::sv::TypeDefinition*> { nullptr,
                      type->systemverilog }
                : std::pair<const semantic::sv::PackedMember*,
                      const semantic::sv::TypeDefinition*> { &*member,
                      type->systemverilog };
        };
        const auto expression_bounds = [&](const semantic::ExpressionId id)
            -> std::optional<std::pair<std::int64_t, std::int64_t>> {
            const auto expression = compiled.find_expression(id);
            if (!expression || expression->systemverilog == nullptr) {
                return std::nullopt;
            }
            const auto& source = *expression->systemverilog;
            const auto member_selection = selected_member(source);
            if (member_selection.first != nullptr) {
                return bounds_of(
                    member_selection.first->type, root_specialization);
            }
            if (source.referenced_name && source.referenced_name->selected) {
                const auto declaration = compiled.find_declaration(
                    *source.referenced_name->selected);
                if (declaration && declaration->systemverilog != nullptr
                    && declaration->systemverilog->type) {
                    return bounds_of(
                        *declaration->systemverilog->type,
                        root_specialization);
                }
            }
            if ((source.kind == ExpressionKind::integer_literal
                    || source.kind == ExpressionKind::logic_literal
                    || source.kind == ExpressionKind::boolean_literal)
                && source.text.find('\'') != std::string::npos) {
                std::uint64_t width { };
                const auto quote = source.text.find('\'');
                const auto [end, error] = std::from_chars(
                    source.text.data(), source.text.data() + quote, width);
                if (error == std::errc { }
                    && end == source.text.data() + quote && width != 0U) {
                    if (width
                        > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max())) {
                        return std::nullopt;
                    }
                    return std::pair {
                        static_cast<std::int64_t>(width - 1U),
                        std::int64_t { 0 }
                    };
                }
            }
            return std::nullopt;
        };
        const auto report_invalid_selection = [&](
                                                  const semantic::sv::Expression&
                                                      expression) {
            append("FSIM-ELAB-068",
                "a part-select requires an inferable packed source, constant "
                "in-range bounds, a positive indexed width, and compatible "
                "direction",
                expression.source);
        };
        const auto statically_constant_expression = [&](
                                                        const auto& self,
                                                        const semantic::ExpressionId id,
                                                        const std::size_t depth)
            -> bool {
            if (depth > compiled.systemverilog_hir.expressions().size()) {
                return false;
            }
            const auto expression = compiled.find_expression(id);
            if (!expression || expression->systemverilog == nullptr) {
                return false;
            }
            const auto& source = *expression->systemverilog;
            switch (source.kind) {
            case ExpressionKind::integer_literal:
            case ExpressionKind::boolean_literal:
            case ExpressionKind::logic_literal:
                return true;
            case ExpressionKind::name: {
                if (!source.referenced_name
                    || !source.referenced_name->selected) {
                    return false;
                }
                const auto declaration = compiled.find_declaration(
                    *source.referenced_name->selected);
                if (!declaration || declaration->systemverilog == nullptr) {
                    return false;
                }
                const auto form = declaration->systemverilog->form;
                return form == DeclarationForm::parameter
                    || form == DeclarationForm::local_parameter
                    || form == DeclarationForm::enumeration_literal;
            }
            case ExpressionKind::unary:
            case ExpressionKind::binary:
            case ExpressionKind::concatenation:
            case ExpressionKind::replication:
                return !source.operands.empty()
                    && std::ranges::all_of(source.operands,
                        [&](const semantic::ExpressionId operand) {
                            return self(self, operand, depth + 1U);
                        });
            default:
                return false;
            }
        };
        std::unordered_set<std::string> case_match_bindings;
        for (const auto& expression : compiled.systemverilog_hir.expressions()) {
            static constexpr std::string_view binding_prefix { "@match-bind:" };
            if (compiled_scope_belongs_to_unit(
                    compiled, expression.scope, unit.id)
                && expression.kind == ExpressionKind::call
                && expression.text.starts_with(binding_prefix)) {
                case_match_bindings.insert(
                    expression.text.substr(binding_prefix.size()));
            }
        }
        for (const auto& expression : compiled.systemverilog_hir.expressions()) {
            if (!compiled_scope_belongs_to_unit(
                    compiled, expression.scope, unit.id)
                || unreachable_expressions.contains(expression.id.value())) {
                continue;
            }
            if (unit.standard_package) {
                const auto revision
                    = compiled_systemverilog_standard_revision(
                        unit.standard);
                const auto selected_spelling = [&]() -> std::string_view {
                    if (expression.referenced_name) {
                        const auto& name = *expression.referenced_name;
                        const auto spelling = std::string_view {
                            name.spelling };
                        if (spelling.starts_with("std::")) {
                            return spelling;
                        }
                    }
                    return expression.text;
                }();
                constexpr auto prefix = std::string_view { "std::" };
                if (revision && selected_spelling.starts_with(prefix)) {
                    auto member = selected_spelling.substr(prefix.size());
                    if (const auto separator = member.find("::");
                        separator != std::string_view::npos) {
                        member = member.substr(0U, separator);
                    }
                    if (member.empty()
                        || frontend::
                               find_systemverilog_standard_package_declaration(
                                   *revision, member)
                            == nullptr) {
                        append(
                            "FSIM-ELAB-SVPKG-011",
                            "standard package 'std' has no member '"
                                + std::string { member }
                                + "' in SystemVerilog " + unit.standard,
                            expression.source);
                        continue;
                    }
                }
            }
            if (expression.text.find("::") != std::string::npos) {
                const auto first = expression.text.find("::");
                const auto second = expression.text.find("::", first + 2U);
                if (first != 0U && second != std::string::npos
                    && compiled_systemverilog_package(compiled, unit,
                        std::string_view { expression.text }.substr(0U, first))) {
                    append("FSIM-ELAB-SVPKG-005",
                        "a package-scoped item must be package::name",
                        expression.source);
                }
            }
            if (expression.kind == ExpressionKind::name
                && expression.text.find('.') != std::string::npos) {
                const auto [member, owner] = selected_member(expression);
                const bool packed_owner = owner != nullptr
                    && (owner->form == TypeForm::packed_structure
                        || owner->form == TypeForm::packed_union
                        || owner->form == TypeForm::tagged_union);
                if (packed_owner
                    && (member == nullptr
                        || !width_of(
                            member->type, root_specialization))) {
                    append("FSIM-ELAB-SVSTRUCT-002",
                        "packed aggregate member '" + expression.text
                            + "' has no executable layout",
                        expression.source);
                }
            }
            if (expression.kind == ExpressionKind::replication) {
                const auto count = root_specialization
                        && !expression.operands.empty()
                    ? root_specialization->evaluate_integral_expression(
                          expression.operands.front())
                    : std::nullopt;
                bool valid = count && *count >= 0
                    && expression.operands.size() >= 2U
                    && static_cast<std::uint64_t>(*count)
                        <= std::numeric_limits<std::uint32_t>::max();
                if (!valid) {
                    append("FSIM-ELAB-SVREPL-001",
                        "a replication concatenation requires a nonnegative "
                        "constant count, packed operands, and a supported "
                        "result width",
                        expression.source);
                }
                continue;
            }
            const bool index = expression.kind == ExpressionKind::index;
            const bool slice = expression.kind == ExpressionKind::slice;
            if ((!index && !slice)
                || expression.operands.size() != (index ? 2U : 3U)
                || root_specialization == nullptr) {
                continue;
            }
            const auto selected = compiled.find_expression(
                expression.operands.front());
            if (selected && selected->systemverilog != nullptr
                && selected->systemverilog->kind == ExpressionKind::name
                && selected->systemverilog->text == "new") {
                continue;
            }
            const auto bounds = expression_bounds(expression.operands.front());
            if (!bounds) {
                continue;
            }
            const auto root_declaration = [&](const auto& self,
                                              const semantic::ExpressionId id)
                -> std::optional<semantic::CompiledDeclarationView> {
                const auto base = compiled.find_expression(id);
                if (!base || base->systemverilog == nullptr) {
                    return std::nullopt;
                }
                const auto& source = *base->systemverilog;
                if (source.referenced_name
                    && source.referenced_name->selected) {
                    return compiled.find_declaration(
                        *source.referenced_name->selected);
                }
                if ((source.kind == ExpressionKind::index
                        || source.kind == ExpressionKind::slice)
                    && !source.operands.empty()) {
                    return self(self, source.operands.front());
                }
                return std::nullopt;
            };
            const auto base_declaration = root_declaration(
                root_declaration, expression.operands.front());
            if (base_declaration
                && base_declaration->systemverilog != nullptr
                && (base_declaration->systemverilog->form
                        == DeclarationForm::parameter
                    || base_declaration->systemverilog->form
                        == DeclarationForm::local_parameter
                    || case_match_bindings.contains(
                        base_declaration->systemverilog->name)
                    || (base_declaration->systemverilog->type
                        && base_declaration->systemverilog->type
                            ->container_form))) {
                continue;
            }
            const auto lower_bound = std::min(bounds->first, bounds->second);
            const auto upper_bound = std::max(bounds->first, bounds->second);
            const auto first_is_static = statically_constant_expression(
                statically_constant_expression, expression.operands[1], 0U);
            const auto second_is_static = slice
                && statically_constant_expression(statically_constant_expression,
                    expression.operands[2], 0U);
            const auto first = first_is_static
                ? root_specialization->evaluate_integral_expression(
                      expression.operands[1])
                : std::optional<std::int64_t> { };
            const auto second = second_is_static
                ? root_specialization->evaluate_integral_expression(
                      expression.operands[2])
                : std::optional<std::int64_t> { };
            if (index && first
                && (*first < lower_bound || *first > upper_bound)) {
                report_invalid_selection(expression);
                continue;
            }
            if (!slice) {
                continue;
            }
            if (!second) {
                continue;
            }
            if ((expression.text == "+:" || expression.text == "-:")
                && *second <= 0) {
                report_invalid_selection(expression);
                continue;
            }
            if (!first) {
                continue;
            }
            bool valid = true;
            std::int64_t left = *first;
            std::int64_t right = second.value_or(0);
            if (valid && (expression.text == "+:" || expression.text == "-:")) {
                valid = *second > 0;
                if (valid && expression.text == "+:") {
                    valid = *first
                        <= std::numeric_limits<std::int64_t>::max()
                            - (*second - 1);
                    right = valid ? *first + *second - 1 : *first;
                } else if (valid) {
                    valid = *first
                        >= std::numeric_limits<std::int64_t>::min()
                            + (*second - 1);
                    right = *first;
                    left = valid ? *first - *second + 1 : *first;
                }
            }
            valid = valid && left >= lower_bound && left <= upper_bound
                && right >= lower_bound && right <= upper_bound;
            if (expression.text != "+:" && expression.text != "-:") {
                valid = valid
                    && (left >= right) == (bounds->first >= bounds->second);
            }
            if (!valid) {
                report_invalid_selection(expression);
            }
        }
        return diagnostics;
    }

    template <typename AppendSourceDependency>
    void append_compiled_vhdl_dependency_unit(
        const semantic::CompiledDesign& compiled,
        const semantic::UnitId id,
        std::vector<semantic::UnitId>& appended_dependency_units,
        AppendSourceDependency&& append_source_dependency)
    {
        std::vector pending { id };
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (std::ranges::find(
                    appended_dependency_units, current)
                != appended_dependency_units.end()) {
                continue;
            }
            appended_dependency_units.push_back(current);
            const auto dependency = compiled.find_unit(current);
            if (!dependency || dependency->vhdl == nullptr) {
                continue;
            }
            if (const auto* source = compiled_physical_source(
                    compiled, dependency->vhdl->source)) {
                append_source_dependency(*source);
            }
            for (const auto& source :
                dependency->vhdl->source_dependencies) {
                append_source_dependency(source);
            }
            if (dependency->vhdl->kind
                == semantic::vhdl::UnitKind::package) {
                for (const auto& candidate : compiled.vhdl_units()) {
                    if (candidate.kind
                            == semantic::vhdl::UnitKind::package
                        && compiled_vhdl_library_equal(
                            candidate.library,
                            dependency->vhdl->library)
                        && compiled_vhdl_name_equal(
                            candidate.name,
                            dependency->vhdl->name)) {
                        pending.push_back(candidate.id);
                    }
                }
            }
            for (const auto& reference : compiled.references()) {
                if (reference.owner == current && reference.target
                    && (reference.kind
                            == semantic::CompiledReferenceKind::package
                        || reference.kind
                            == semantic::CompiledReferenceKind::import
                        || reference.kind
                            == semantic::CompiledReferenceKind::context)) {
                    pending.push_back(*reference.target);
                }
            }
        }
    }

    template <typename ReportConfigurationIssue>
    void validate_compiled_vhdl_configuration_rules(
        const semantic::CompiledDesign& compiled,
        const std::string_view path,
        const std::span<const semantic::vhdl::ComponentConfiguration> rules,
        const std::span<const semantic::ScopeId> selected_scopes,
        ReportConfigurationIssue&& report_issue)
    {
        const auto& semantic_scopes = compiled.semantics.scopes();
        const auto in_selected_scope = [&](const semantic::ScopeId scope) {
            auto current = scope;
            std::unordered_set<std::uint32_t> visiting;
            while (current.valid()
                && current.value() < semantic_scopes.size()
                && visiting.insert(current.value()).second) {
                if (std::ranges::find(selected_scopes, current)
                    != selected_scopes.end()) {
                    return true;
                }
                const auto& record = semantic_scopes[current.value()];
                if (record.id != current || !record.parent) {
                    break;
                }
                current = *record.parent;
            }
            return false;
        };
        const auto matching_instance
            = [&](const semantic::vhdl::ComponentConfiguration& rule,
                  const std::optional<std::string_view> label) {
                  return std::ranges::any_of(
                      compiled.vhdl_hir.instances(),
                      [&](const semantic::vhdl::Instance& instance) {
                          return instance.component
                              && in_selected_scope(instance.scope)
                              && compiled_vhdl_name_equal(
                                  instance.target.spelling,
                                  rule.component.spelling)
                              && (!label
                                  || compiled_vhdl_name_equal(
                                      instance.name, *label));
                      });
              };

        std::unordered_map<std::string,
            std::unordered_set<std::string>>
            explicit_labels;
        std::unordered_map<std::string, std::size_t> all_counts;
        std::unordered_map<std::string, std::size_t> others_counts;
        std::unordered_map<std::string, semantic::SourceSpanId> sources;
        for (const auto& rule : rules) {
            const auto component = vhdl_configuration_detail::
                configuration_canonical_name(rule.component.spelling);
            sources.insert_or_assign(component, rule.source);
            if (!matching_instance(rule, std::nullopt)) {
                report_issue(
                    "FSIM-ELAB-VHCONFIG-005",
                    "configuration in '" + std::string { path }
                        + "' names component '"
                        + rule.component.spelling
                        + "' with no component instances",
                    rule.source);
            }
            switch (rule.selection) {
            case semantic::vhdl::InstanceSelection::labels: {
                auto& seen = explicit_labels[component];
                for (const auto& label : rule.labels) {
                    if (!matching_instance(
                            rule, std::string_view { label })) {
                        report_issue(
                            "FSIM-ELAB-VHCONFIG-006",
                            "configuration label '" + label
                                + "' does not select component '"
                                + rule.component.spelling + "' in '"
                                + std::string { path } + "'",
                            rule.source);
                    }
                    const auto canonical_label
                        = vhdl_configuration_detail::
                            configuration_canonical_name(label);
                    if (!seen.insert(canonical_label).second) {
                        report_issue(
                            "FSIM-ELAB-VHCONFIG-007",
                            "configuration label '" + label
                                + "' is bound more than once for "
                                  "component '"
                                + rule.component.spelling + "'",
                            rule.source);
                    }
                }
                break;
            }
            case semantic::vhdl::InstanceSelection::all:
                ++all_counts[component];
                break;
            case semantic::vhdl::InstanceSelection::others:
                ++others_counts[component];
                break;
            }
        }
        for (const auto& [component, count] : all_counts) {
            if (count == 1U && !others_counts.contains(component)
                && !explicit_labels.contains(component)) {
                continue;
            }
            report_issue(
                "FSIM-ELAB-VHCONFIG-007",
                "an all configuration for component '" + component
                    + "' overlaps another binding",
                sources.at(component));
        }
        for (const auto& [component, count] : others_counts) {
            if (count == 1U) {
                continue;
            }
            report_issue(
                "FSIM-ELAB-VHCONFIG-007",
                "component '" + component
                    + "' has multiple others bindings",
                sources.at(component));
        }
    }

    template <typename ReportConfigurationIssue>
    bool validate_compiled_vhdl_configuration_scopes(
        const semantic::CompiledDesign& compiled,
        const semantic::vhdl::Unit& architecture,
        const semantic::vhdl::BlockConfiguration* configuration,
        const std::string_view path,
        const std::span<const semantic::ScopeId> architecture_scope,
        ReportConfigurationIssue&& report_issue)
    {
        if (configuration == nullptr) {
            return true;
        }

        const auto& semantic_scopes = compiled.semantics.scopes();
        struct ConfigurationScope {
            semantic::ScopeId id;
            std::vector<std::string> parts;
        };
        std::vector<ConfigurationScope> scopes;
        for (const auto& candidate : semantic_scopes) {
            if (candidate.unit != architecture.id
                || candidate.id == architecture.scope) {
                continue;
            }
            std::vector<std::string> parts;
            auto current = candidate.id;
            std::unordered_set<std::uint32_t> visiting;
            while (current != architecture.scope
                && current.valid()
                && current.value() < semantic_scopes.size()
                && visiting.insert(current.value()).second) {
                const auto& scope = semantic_scopes[current.value()];
                if (scope.id != current || scope.unit != architecture.id
                    || !scope.parent) {
                    parts.clear();
                    break;
                }
                if (!scope.name.empty()) {
                    parts.push_back(scope.name);
                }
                current = *scope.parent;
            }
            if (current != architecture.scope || parts.empty()) {
                continue;
            }
            std::ranges::reverse(parts);
            scopes.push_back({ candidate.id, std::move(parts) });
        }

        const auto scope_part_matches = [](
                                            const std::string_view actual,
                                            const std::string_view configured) {
            if (vhdl_configuration_detail::configuration_name_equal(
                    actual, configured)) {
                return true;
            }
            const auto opening = configured.rfind('[');
            return opening != std::string_view::npos
                && configured.ends_with(']')
                && vhdl_configuration_detail::configuration_name_equal(
                    actual, configured.substr(0U, opening));
        };

        bool valid = true;
        std::vector<std::string> configured_path;
        validate_compiled_vhdl_configuration_rules(
            compiled, path, configuration->components, architecture_scope,
            report_issue);
        const auto validate_block = [&](
                                        const auto& self,
                                        const semantic::vhdl::BlockConfiguration& block) -> void {
            std::unordered_set<std::string> sibling_scopes;
            for (const auto& child : block.blocks) {
                const auto local = vhdl_configuration_detail::
                    configuration_block_scope(compiled, child);
                if (!local) {
                    report_issue(
                        "FSIM-ELAB-VHCONFIG-010",
                        "configuration block '" + child.block.spelling
                            + "' requires a locally static generate index",
                        child.source);
                    valid = false;
                    continue;
                }
                const auto canonical = vhdl_configuration_detail::
                    configuration_canonical_name(*local);
                if (!sibling_scopes.insert(canonical).second) {
                    report_issue(
                        "FSIM-ELAB-VHCONFIG-015",
                        "configuration block scope '" + *local
                            + "' is selected more than once",
                        child.source);
                    valid = false;
                    continue;
                }

                configured_path.push_back(*local);
                std::vector<semantic::ScopeId> matched_scopes;
                for (const auto& scope : scopes) {
                    std::size_t configured_index { };
                    for (const auto& part : scope.parts) {
                        if (scope_part_matches(
                                part,
                                configured_path[configured_index])) {
                            ++configured_index;
                            if (configured_index
                                == configured_path.size()) {
                                matched_scopes.push_back(scope.id);
                                break;
                            }
                        }
                    }
                }
                if (matched_scopes.empty()) {
                    std::string scope;
                    for (const auto& part : configured_path) {
                        if (!scope.empty()) {
                            scope += ".";
                        }
                        scope += part;
                    }
                    report_issue(
                        "FSIM-ELAB-VHCONFIG-010",
                        "configuration block scope '" + scope
                            + "' does not select an elaborated block or "
                              "generate occurrence",
                        child.source);
                    valid = false;
                    configured_path.pop_back();
                    continue;
                }
                validate_compiled_vhdl_configuration_rules(
                    compiled, path, child.components, matched_scopes,
                    report_issue);
                self(self, child);
                configured_path.pop_back();
            }
        };
        validate_block(validate_block, *configuration);
        return valid;
    }

    template <typename Materializations, typename Specialization,
        typename AppendDeclarationDependencies>
    void append_generated_vhdl_identity_projection(
        const std::string_view path,
        const Materializations& generated_materializations,
        Specialization& specialization,
        AppendDeclarationDependencies&& append_declaration_dependencies)
    {
        for (const auto& materialization : generated_materializations) {
            const auto relative_path
                = materialization.path.size() > path.size()
                    && materialization.path.starts_with(path)
                    && materialization.path[path.size()] == '.'
                ? std::string_view { materialization.path }.substr(
                      path.size() + 1U)
                : std::string_view { materialization.region->label };
            for (const auto declaration_id :
                materialization.region->declarations) {
                const auto declaration
                    = materialization.specialization.find_declaration(
                        declaration_id);
                if (!declaration || declaration->vhdl == nullptr) {
                    continue;
                }
                const auto form = declaration->vhdl->form;
                if (form
                        != semantic::vhdl::DeclarationForm::generic_constant
                    && form
                        != semantic::vhdl::DeclarationForm::generic_type
                    && form
                        != semantic::vhdl::DeclarationForm::generic_function
                    && form
                        != semantic::vhdl::DeclarationForm::generic_procedure
                    && form
                        != semantic::vhdl::DeclarationForm::generic_package) {
                    continue;
                }
                const auto& materialized_actuals
                    = materialization.specialization
                          .specialization()
                          .actual_identities;
                const auto actual = std::ranges::find(
                    materialized_actuals, declaration_id,
                    &semantic::SpecializedHirActualIdentity::declaration);
                if (actual == materialized_actuals.end()) {
                    continue;
                }
                const auto name = "__block:" + std::string { relative_path }
                    + ":" + declaration->vhdl->name;
                specialization.parameter_values.emplace_back(
                    name, actual->identity);
                specialization.parameter_identity_values.emplace_back(
                    name, actual->identity);
                if (actual->actual_declaration) {
                    append_declaration_dependencies(
                        *actual->actual_declaration);
                }
            }
            for (const auto declaration_id :
                materialization.region->declarations) {
                const auto declaration
                    = materialization.specialization.find_declaration(
                        declaration_id);
                if (!declaration || declaration->vhdl == nullptr) {
                    continue;
                }
                const auto form = declaration->vhdl->form;
                if (form
                        != semantic::vhdl::DeclarationForm::
                            generic_function_instance
                    && form
                        != semantic::vhdl::DeclarationForm::
                            generic_procedure_instance
                    && form
                        != semantic::vhdl::DeclarationForm::package_instance) {
                    continue;
                }
                const auto declaration_identity
                    = materialization.specialization
                          .vhdl_declaration_identity(declaration_id);
                if (!declaration_identity) {
                    continue;
                }
                specialization.parameter_identity_values.emplace_back(
                    std::string { relative_path } + "."
                        + declaration->vhdl->name,
                    compiled_vhdl_occurrence_declaration_identity(
                        *declaration_identity, relative_path, form));
                append_declaration_dependencies(declaration_id);
            }
        }
    }

    template <typename Materializations,
        typename AppendDeclarationDependencies, typename ReportIssue>
    bool append_compiled_vhdl_specialization_identity_projection(
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture,
        semantic::SpecializedHirUnit& specialized,
        const std::string& path,
        const Materializations& generated_materializations,
        SpecializationInfo& specialization,
        const std::string& applied_configuration_identity,
        const std::string& applied_component_identity,
        AppendDeclarationDependencies&& append_declaration_dependencies,
        ReportIssue&& report_issue)
    {
        for (const auto& actual :
            specialized.specialization().actual_identities) {
            const auto declaration
                = specialized.find_declaration(actual.declaration);
            if (!declaration || declaration->vhdl == nullptr) {
                report_issue(
                    "FSIM-ELAB-HIR-001",
                    "compiled specialization actual has no VHDL "
                    "declaration",
                    architecture.source);
                return false;
            }
            const auto value = specialized.evaluate_integral_declaration(
                actual.declaration);
            auto display = actual.identity;
            if (value && declaration->vhdl->subtype) {
                const auto& subtype = *declaration->vhdl->subtype;
                const auto domain = compiled_value_domain(subtype.domain);
                auto width = compiled_vhdl_signal_width(subtype);
                if (!width || !compiled_vhdl_scalar_domain(domain)) {
                    const auto layout = compiled_vhdl_named_signal_layout(
                        specialized, subtype,
                        declaration->vhdl->scope);
                    if (layout) {
                        width = layout->width;
                    }
                }
                if (domain == frontend::ValueDomain::Boolean) {
                    display = *value == 0 ? "false" : "true";
                } else if (domain == frontend::ValueDomain::Integer
                    || !width) {
                    display = std::to_string(*value);
                } else {
                    display = compiled_vhdl_integral_value(
                        *value, domain, *width)
                                  .to_msb_string();
                }
            }
            specialization.parameter_values.emplace_back(
                declaration->vhdl->name, std::move(display));
            specialization.parameter_identity_values.emplace_back(
                declaration->vhdl->name, actual.identity);
            if (actual.actual_declaration) {
                const auto selected_actual
                    = specialized.find_declaration(
                        *actual.actual_declaration);
                const bool structural_type_identity
                    = selected_actual
                    && selected_actual->vhdl != nullptr
                    && (selected_actual->vhdl->form
                            == semantic::vhdl::DeclarationForm::type
                        || selected_actual->vhdl->form
                            == semantic::vhdl::DeclarationForm::subtype
                        || selected_actual->vhdl->form
                            == semantic::vhdl::DeclarationForm::generic_type)
                    && (actual.identity.starts_with(
                            "vhdl-type-v1;structural;")
                        || actual.identity.starts_with(
                            "vhdl-type-constraint-v1;"));
                // Structural type identities already contain the selected
                // type's shape. Do not broaden their native-code dependency
                // to every declaration sharing the parent's source file.
                if (!structural_type_identity) {
                    append_declaration_dependencies(
                        *actual.actual_declaration);
                }
            }
        }
        append_generated_vhdl_identity_projection(
            path, generated_materializations, specialization,
            append_declaration_dependencies);
        const auto append_local_generic_subprograms
            = [&](const semantic::vhdl::Unit& unit) {
                  for (const auto declaration_id : unit.declarations) {
                      const auto declaration
                          = specialized.find_declaration(declaration_id);
                      if (!declaration || declaration->vhdl == nullptr) {
                          continue;
                      }
                      const auto form = declaration->vhdl->form;
                      if (form
                              != semantic::vhdl::DeclarationForm::
                                  generic_function_instance
                          && form
                              != semantic::vhdl::DeclarationForm::
                                  generic_procedure_instance) {
                          continue;
                      }
                      const auto declaration_identity
                          = specialized.vhdl_declaration_identity(
                              declaration_id);
                      if (!declaration_identity) {
                          continue;
                      }
                      specialization.parameter_identity_values.emplace_back(
                          declaration->vhdl->name,
                          *declaration_identity);
                      append_declaration_dependencies(declaration_id);
                  }
              };
        append_local_generic_subprograms(entity);
        append_local_generic_subprograms(architecture);
        if (!applied_configuration_identity.empty()) {
            specialization.parameter_identity_values.emplace_back(
                "__configuration", applied_configuration_identity);
        }
        if (!applied_component_identity.empty()) {
            specialization.parameter_identity_values.emplace_back(
                "__component", applied_component_identity);
        }
        return true;
    }

} // namespace

void HierarchyBuilder::note_boundary_driver(
    const SignalId signal,
    const Binding* binding,
    const std::string& path,
    const frontend::SourceSpan& source)
{
    auto& paths = boundary_driver_paths_[signal];
    const auto nested_with = [](const std::string_view left,
                                 const std::string_view right) {
        if (left == right) {
            return true;
        }
        const auto owner = [](const std::string_view endpoint) {
            const auto separator = endpoint.rfind('.');
            return separator == std::string_view::npos
                ? std::string_view { }
                : endpoint.substr(0U, separator);
        };
        const auto left_owner = owner(left);
        const auto right_owner = owner(right);
        if (left_owner.empty() || right_owner.empty()
            || left_owner == right_owner) {
            return false;
        }
        const auto left_prefix = std::string { left_owner } + ".";
        const auto right_prefix = std::string { right_owner } + ".";
        return left_owner.starts_with(right_prefix)
            || right_owner.starts_with(left_prefix);
    };
    const bool conflicting = std::ranges::any_of(
        paths,
        [&](const std::string& existing) {
            return !nested_with(path, existing);
        });
    if (std::ranges::find(paths, path) == paths.end()) {
        paths.push_back(path);
    }
    if (binding != nullptr && binding->resolver) {
        const auto [found, inserted] = resolver_by_signal_.emplace(
            signal, *binding->resolver);
        if (!inserted && found->second != *binding->resolver) {
            report(
                "FSIM-ELAB-BIND-023",
                "conflicting resolvers for boundary net '" + path + "'",
                source);
        }
    }
    auto resolution = native_resolution(design_.signal_info_.at(signal));
    if (conflicting && resolution == ResolutionKind::none) {
        resolution = explicit_resolution(signal).value_or(
            ResolutionKind::none);
    }
    if (conflicting && resolution == ResolutionKind::none) {
        report(
            "FSIM-ELAB-BIND-024",
            "multiple boundary drivers on '" + path
                + "' require resolver = \"std_logic\" or \"sv_wire\"",
            source);
    }
}

std::optional<SignalId>
HierarchyBuilder::connect_compiled_boundary_port(
    const CompiledBoundaryPort& port,
    const SignalId actual,
    const std::string& path,
    const frontend::SourceSpan connection_source,
    const Binding* const binding)
{
    if (actual >= design_.signal_info_.size()
        || port.width == 0U
        || port.domain == frontend::ValueDomain::Unknown
        || port.domain == frontend::ValueDomain::String) {
        report(
            "FSIM-ELAB-BIND-019",
            "unsupported value domain on boundary '" + path + "."
                + port.name + "'",
            connection_source);
        return std::nullopt;
    }
    const auto actual_info = design_.signal_info_[actual];
    if (actual_info.source_domain == frontend::ValueDomain::Unknown
        || actual_info.source_domain
            == frontend::ValueDomain::String) {
        report(
            "FSIM-ELAB-BIND-019",
            "unsupported value domain on boundary '" + path + "."
                + port.name + "'",
            connection_source);
        return std::nullopt;
    }
    if (port.systemverilog_scalar
            != actual_info.systemverilog_scalar
        && (port.systemverilog_scalar
                != frontend::SystemVerilogScalarKind::None
            || actual_info.systemverilog_scalar
                != frontend::SystemVerilogScalarKind::None)) {
        report(
            "FSIM-ELAB-BIND-019",
            "incompatible SystemVerilog scalar types on boundary '"
                + path + "." + port.name + "'",
            connection_source);
        return std::nullopt;
    }
    if (port.packed_aggregate
        || !actual_info.packed_members.empty()) {
        report(
            "FSIM-ELAB-BIND-049",
            "packed aggregate boundary '" + path + "." + port.name
                + "' requires a same-language scalar/vector wrapper",
            connection_source);
        return std::nullopt;
    }
    const auto unsupported_vhdl_array = [](
                                            const auto& array) {
        return array
            && (!array->element_types.empty()
                || (array->element_domain
                        != frontend::ValueDomain::Bit2
                    && array->element_domain
                        != frontend::ValueDomain::Logic4
                    && array->element_domain
                        != frontend::ValueDomain::Logic9));
    };
    if (unsupported_vhdl_array(port.vhdl_array)
        || unsupported_vhdl_array(actual_info.vhdl_array)) {
        report(
            "FSIM-ELAB-BIND-055",
            "VHDL array boundary '" + path + "." + port.name
                + "' requires a same-language scalar/vector wrapper",
            connection_source);
        return std::nullopt;
    }
    const bool produces_output
        = port.direction == frontend::PortDirection::Output
        || port.direction == frontend::PortDirection::Buffer
        || port.direction == frontend::PortDirection::Inout;
    if (produces_output) {
        note_boundary_driver(
            actual, binding, path + "." + port.name,
            connection_source);
    }
    const bool width_changed = port.width != actual_info.width;
    const bool signedness_changed = port.width > 1U
        && port.signed_value != actual_info.is_signed;
    const bool boolean_boundary = port.width == 1U
        && actual_info.width == 1U
        && ((port.domain == frontend::ValueDomain::Boolean
                && (actual_info.source_domain
                        == frontend::ValueDomain::Bit2
                    || actual_info.source_domain
                        == frontend::ValueDomain::Logic4))
            || (actual_info.source_domain
                    == frontend::ValueDomain::Boolean
                && (port.domain == frontend::ValueDomain::Bit2
                    || port.domain
                        == frontend::ValueDomain::Logic4)));
    const bool integer_boundary = port.width == 32U
        && actual_info.width == 32U
        && port.signed_value && actual_info.is_signed
        && ((port.domain == frontend::ValueDomain::Integer
                && (actual_info.source_domain
                        == frontend::ValueDomain::Bit2
                    || actual_info.source_domain
                        == frontend::ValueDomain::Logic4))
            || (actual_info.source_domain
                    == frontend::ValueDomain::Integer
                && (port.domain == frontend::ValueDomain::Bit2
                    || port.domain
                        == frontend::ValueDomain::Logic4)));
    const bool state_domain_boundary
        = (port.domain == frontend::ValueDomain::Logic4
              && actual_info.source_domain
                  == frontend::ValueDomain::Logic9)
        || (port.domain == frontend::ValueDomain::Logic9
            && actual_info.source_domain
                == frontend::ValueDomain::Logic4);
    const bool adaptable_direction
        = port.direction == frontend::PortDirection::Input
        || port.direction == frontend::PortDirection::Output
        || port.direction == frontend::PortDirection::Buffer;
    const bool integral_boundary
        = !boolean_boundary && !integer_boundary;
    bool incompatible_shape { };
    if (width_changed && (!integral_boundary || !adaptable_direction)) {
        report(
            "FSIM-ELAB-BIND-020",
            "width mismatch on '" + path + "." + port.name + "': "
                + std::to_string(port.width) + " versus "
                + std::to_string(actual_info.width),
            connection_source);
        incompatible_shape = true;
    }
    if (signedness_changed && (!integral_boundary || !adaptable_direction)) {
        report(
            "FSIM-ELAB-BIND-021",
            "signedness mismatch on '" + path + "." + port.name
                + "'",
            connection_source);
        incompatible_shape = true;
    }
    if (incompatible_shape) {
        return std::nullopt;
    }
    const auto lossy_into_two_state = [](
                                          const frontend::ValueDomain destination,
                                          const frontend::ValueDomain source) {
        return is_two_state_domain(destination)
            && !is_two_state_domain(source);
    };
    const bool lossy = port.direction
            == frontend::PortDirection::Output
        ? lossy_into_two_state(
              actual_info.source_domain, port.domain)
        : lossy_into_two_state(
              port.domain, actual_info.source_domain);
    if (lossy && !boolean_boundary && !integer_boundary) {
        report(
            "FSIM-ELAB-BIND-022",
            "implicit lossy conversion into a 2-state boundary at '"
                + path + "." + port.name + "' is forbidden",
            connection_source);
        return std::nullopt;
    }
    const bool integer_involved
        = port.domain == frontend::ValueDomain::Integer
        || actual_info.source_domain
            == frontend::ValueDomain::Integer;
    if (integer_involved && !integer_boundary) {
        if (port.signed_value != actual_info.is_signed) {
            report(
                "FSIM-ELAB-BIND-021",
                "signedness mismatch on '" + path + "." + port.name
                    + "'",
                connection_source);
        }
        report(
            "FSIM-ELAB-BIND-051",
            "integer subtype ranges on boundary '" + path + "."
                + port.name + "' cannot guarantee a range-safe "
                              "32-bit signed conversion",
            connection_source);
        return std::nullopt;
    }
    const bool needs_adapter = adaptable_direction
        && (width_changed || signedness_changed
            || boolean_boundary || integer_boundary);
    if (port.direction == frontend::PortDirection::Inout
        && (binding == nullptr || !binding->resolver)) {
        report(
            "FSIM-ELAB-BIND-030",
            "cross-language inout '" + path + "." + port.name
                + "' requires an explicit resolver",
            connection_source);
        return std::nullopt;
    }
    if (port.direction == frontend::PortDirection::Inout
        && needs_adapter) {
        report(
            "FSIM-ELAB-BIND-019",
            "cross-language inout '" + path + "." + port.name
                + "' requires matching widths, signedness, and value "
                  "domains",
            connection_source);
        return std::nullopt;
    }

    auto formal = actual;
    std::optional<ProcessId> conversion_process;
    if (needs_adapter) {
        if (design_.signals_.size()
            > std::numeric_limits<SignalId>::max()) {
            report(
                "FSIM-ELAB-011",
                "the design has too many signals for dense 32-bit IDs",
                connection_source);
            return std::nullopt;
        }
        formal = static_cast<SignalId>(design_.signals_.size());
        const auto formal_name = path + "." + port.name;
        SignalInfo info;
        info.id = formal;
        info.name = formal_name;
        info.width = port.width;
        info.type_name = port.type_name;
        info.source_domain = port.domain;
        info.systemverilog_scalar = port.systemverilog_scalar;
        info.is_signed = port.signed_value;
        info.packed_range = port.packed_range;
        info.integer_range = port.integer_range;
        info.is_port = true;
        info.direction = port.direction;
        info.declaration_span = port.declaration_source;
        design_.signal_info_.push_back(std::move(info));
        design_.signals_.push_back(runtime::simir::Signal {
            formal_name,
            port.initial,
            ResolutionKind::none,
            value_kind(port.domain),
            std::nullopt,
            { StrengthRank::pull, StrengthRank::pull },
            std::nullopt,
            std::nullopt,
            port.systemverilog_scalar,
        });
        design_.signal_by_name_.emplace(formal_name, formal);

        const bool input
            = port.direction == frontend::PortDirection::Input;
        const auto source = input ? actual : formal;
        const auto destination = input ? formal : actual;
        const auto source_width
            = input ? actual_info.width : port.width;
        const auto destination_width
            = input ? port.width : actual_info.width;
        const auto source_domain
            = input ? actual_info.source_domain : port.domain;
        const auto destination_domain
            = input ? port.domain : actual_info.source_domain;
        const bool sign_extend
            = input ? actual_info.is_signed : port.signed_value;
        Process adapter;
        adapter.id = static_cast<ProcessId>(
            design_.processes_.size());
        adapter.name = formal_name
            + (boolean_boundary
                    ? "$boundary_boolean"
                    : integer_boundary
                    ? "$boundary_integer"
                    : "$compiled_boundary");
        adapter.static_sensitivity.push_back(
            Sensitivity { source, EdgeKind::any });
        InstructionIndex conversion_start = 0U;
        if (destination_domain == frontend::ValueDomain::Boolean
            || (destination_domain
                    == frontend::ValueDomain::Integer
                && !is_two_state_domain(source_domain))) {
            adapter.operations.emplace_back(WaitSensitivity { });
            adapter.operations.emplace_back(Jump { 2 });
            conversion_start = 2U;
        }
        adapter.operations.emplace_back(ReadSignal { 0, source });
        RegisterId converted = 1;
        if (destination_domain == frontend::ValueDomain::Boolean) {
            adapter.register_count = 3;
            converted = 0;
            adapter.register_value_kinds = {
                value_kind(source_domain),
                value_kind(source_domain),
                value_kind(source_domain),
            };
            adapter.operations.emplace_back(LoadConstant {
                1, PackedLogic4(31, Logic4::zero) });
            adapter.operations.emplace_back(
                Concatenate { 2, { 1, 0 }, 32 });
            adapter.operations.emplace_back(
                IntegerCheck { 2, 0, 1 });
        } else if (destination_domain
            == frontend::ValueDomain::Integer) {
            const auto range = input
                ? port.integer_range
                : actual_info.integer_range;
            const auto lower = range
                ? static_cast<std::int32_t>(
                      std::min(range->left, range->right))
                : std::numeric_limits<std::int32_t>::min();
            const auto upper = range
                ? static_cast<std::int32_t>(
                      std::max(range->left, range->right))
                : std::numeric_limits<std::int32_t>::max();
            adapter.register_count = 1;
            converted = 0;
            adapter.register_value_kinds = {
                value_kind(source_domain),
            };
            adapter.operations.emplace_back(
                IntegerCheck { 0, lower, upper });
        } else if (destination_width == source_width) {
            adapter.register_count = 2;
            adapter.register_value_kinds = {
                value_kind(source_domain),
                value_kind(destination_domain),
            };
            adapter.operations.emplace_back(
                CopyRegister { converted, 0 });
        } else if (destination_width < source_width) {
            adapter.register_count = 2;
            adapter.register_value_kinds = {
                value_kind(source_domain),
                value_kind(destination_domain),
            };
            adapter.operations.emplace_back(Extract {
                converted,
                0,
                0,
                static_cast<std::uint32_t>(destination_width),
            });
        } else {
            adapter.register_count = 3;
            converted = 2;
            std::vector<RegisterId> operands;
            if (sign_extend) {
                adapter.operations.emplace_back(Extract {
                    1,
                    0,
                    static_cast<std::uint32_t>(source_width - 1U),
                    1,
                });
                operands.assign(
                    destination_width - source_width, 1);
            } else {
                adapter.operations.emplace_back(LoadConstant {
                    1,
                    PackedLogic4(
                        destination_width - source_width,
                        Logic4::zero),
                });
                operands.push_back(1);
            }
            operands.push_back(0);
            adapter.register_value_kinds = {
                value_kind(source_domain),
                value_kind(sign_extend
                        ? source_domain
                        : destination_domain),
                value_kind(destination_domain),
            };
            adapter.operations.emplace_back(Concatenate {
                converted,
                std::move(operands),
                static_cast<std::uint32_t>(destination_width),
            });
        }
        adapter.operations.emplace_back(
            WriteUpdate { destination, converted });
        adapter.operations.emplace_back(WaitSensitivity { });
        adapter.operations.emplace_back(Jump { conversion_start });
        adapter.driver_regions.push_back(Process::DriverRegion {
            destination,
            0,
            static_cast<std::uint32_t>(destination_width),
            true,
        });
        conversion_process = adapter.id;
        if (!design_.specializations_.empty()) {
            design_.specializations_.back().processes.push_back(
                adapter.id);
        }
        design_.processes_.push_back(std::move(adapter));
    }

    design_.boundary_conversions_.push_back(BoundaryConversionInfo {
        boolean_boundary
            ? BoundaryConversionKind::boolean_adapter
            : integer_boundary
            ? BoundaryConversionKind::integer_adapter
            : state_domain_boundary && !width_changed
                && !signedness_changed
            ? BoundaryConversionKind::state_domain_alias
            : width_changed && signedness_changed
            ? BoundaryConversionKind::width_signedness_adapter
            : width_changed
            ? BoundaryConversionKind::width_adapter
            : signedness_changed
            ? BoundaryConversionKind::signedness_adapter
            : BoundaryConversionKind::ordinal_alias,
        path + "." + port.name,
        formal,
        actual_info.id,
        conversion_process,
        port.direction,
        port.width,
        actual_info.width,
        port.domain,
        actual_info.source_domain,
        port.signed_value,
        actual_info.is_signed,
        state_domain_boundary,
        port.packed_range,
        actual_info.packed_range,
        port.integer_range,
        actual_info.integer_range,
        connection_source,
        port.declaration_source,
        actual_info.declaration_span,
    });
    return formal;
}

void HierarchyBuilder::add_root(
    const semantic::CompiledUnitView root,
    std::string path)
{
    if (root.vhdl != nullptr && root.systemverilog == nullptr) {
        add_compiled_vhdl_root(root, std::move(path));
        return;
    }
    active_root_ = std::move(path);
    if (compiled_ != nullptr && root.systemverilog != nullptr
        && root.systemverilog->kind
            == semantic::sv::UnitKind::configuration) {
        const auto& configuration = *root.systemverilog;
        if (!configuration.configuration
            || configuration.configuration->designs.size() != 1U) {
            report(
                "FSIM-ELAB-SVCONFIG-001",
                "configuration '" + configuration.name
                    + "' selected as one root must contain exactly one "
                      "design top",
                compiled_source_span(*compiled_, configuration.source));
            return;
        }
        const auto& design
            = configuration.configuration->designs.front();
        auto selected = design.target
            ? compiled_->find_unit(*design.target)
            : std::optional<semantic::CompiledUnitView> { };
        if (!selected) {
            const auto library = design.library.empty()
                ? compiled_systemverilog_library(configuration)
                : std::string_view { design.library };
            const auto match = find_exact_systemverilog_module(
                *compiled_, library, design.cell);
            if (match.ambiguous) {
                report(
                    "FSIM-ELAB-SVCONFIG-002",
                    "configuration design top '"
                        + std::string { library } + "."
                        + design.cell + "' is ambiguous",
                    compiled_source_span(*compiled_, design.source));
                return;
            }
            selected = match.unit;
        }
        if (!selected || selected->systemverilog == nullptr
            || !compiled_systemverilog_selectable(
                *selected->systemverilog)) {
            report(
                "FSIM-ELAB-SVCONFIG-002",
                "configuration design top '" + design.cell
                    + "' was not found",
                compiled_source_span(*compiled_, design.source));
            return;
        }
        const auto* saved_configuration
            = active_compiled_systemverilog_configuration_;
        auto saved_root
            = active_compiled_systemverilog_configuration_root_;
        active_compiled_systemverilog_configuration_ = &configuration;
        active_compiled_systemverilog_configuration_root_ = active_root_;
        static_cast<void>(instantiate_compiled_systemverilog_unit(
            CompiledSystemVerilogInstantiationContext {
                *selected, active_root_, { }, { }, { }, { },
                std::nullopt, std::nullopt }));
        active_compiled_systemverilog_configuration_
            = saved_configuration;
        active_compiled_systemverilog_configuration_root_
            = std::move(saved_root);
        return;
    }
    static_cast<void>(instantiate_compiled_systemverilog_unit(
        CompiledSystemVerilogInstantiationContext {
            root, active_root_, { }, { }, { }, { },
            std::nullopt, std::nullopt }));
}

std::optional<HierarchyBuilder::SystemVerilogAliasPlan>
HierarchyBuilder::build_systemverilog_alias_plan(
    const semantic::SpecializedHirUnit& working_specialization,
    std::span<const semantic::DeclarationId> declarations,
    std::span<const semantic::sv::Alias> aliases)
{
    struct HirAliasSegment {
        std::string name;
        semantic::DeclarationId declaration;
        std::uint64_t offset { };
        std::uint64_t width { };
    };
    const auto alias_error = [&](const std::string_view code,
                                 std::string message,
                                 const semantic::SourceSpanId source) {
        report(std::string { code }, std::move(message),
            compiled_source_span(*compiled_, source));
    };
    const auto alias_declaration = [&](
                                       const auto& retained_declarations,
                                       const semantic::sv::Expression&
                                           expression)
        -> std::optional<semantic::DeclarationId> {
        const auto retained = [&](const auto& candidate) {
            return candidate.systemverilog != nullptr
                && std::ranges::find(
                       retained_declarations,
                       candidate.systemverilog->id)
                != retained_declarations.end();
        };
        return semantic::CompiledDesignResolver {
            working_specialization
        }
            .resolve_systemverilog(
                expression.text, expression.scope, retained, false)
            .unique();
    };
    const auto alias_range = [&](
                                 const semantic::sv::Declaration& declaration,
                                 const std::uint64_t width)
        -> std::optional<std::pair<std::int64_t, std::int64_t>> {
        if (!declaration.type || !declaration.type->packed_range) {
            if (width == 0U
                || width - 1U
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
            return std::pair {
                static_cast<std::int64_t>(width - 1U),
                std::int64_t { 0 }
            };
        }
        const auto& range = *declaration.type->packed_range;
        const auto left = range.left ? range.left
            : range.left_expression
            ? working_specialization.evaluate_integral_expression(
                  *range.left_expression)
            : std::nullopt;
        const auto right = range.right ? range.right
            : range.right_expression
            ? working_specialization.evaluate_integral_expression(
                  *range.right_expression)
            : std::nullopt;
        if (!left || !right) {
            return std::nullopt;
        }
        return std::pair { *left, *right };
    };
    const auto append_alias_selection = [&](
                                            const auto& retained_declarations,
                                            const semantic::sv::Expression&
                                                expression,
                                            const semantic::sv::Expression&
                                                base,
                                            const std::int64_t right,
                                            const std::uint64_t selected_width,
                                            std::vector<HirAliasSegment>&
                                                segments)
        -> bool {
        const auto declaration_id = alias_declaration(
            retained_declarations, base);
        const auto declaration = declaration_id
            ? working_specialization.find_declaration(*declaration_id)
            : std::optional<semantic::CompiledDeclarationView> { };
        const auto width = declaration
                && declaration->systemverilog != nullptr
            ? hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
                  working_specialization,
                  *declaration->systemverilog)
            : std::optional<std::size_t> { };
        if (!declaration_id || !declaration
            || declaration->systemverilog == nullptr
            || declaration->systemverilog->form
                != semantic::sv::DeclarationForm::net
            || !width || *width == 0U) {
            alias_error("FSIM-ELAB-SVALIAS-001",
                "SystemVerilog alias terminals must be statically "
                "selected packed nets",
                expression.source);
            return false;
        }
        const auto range = alias_range(
            *declaration->systemverilog, *width);
        if (!range || right < std::min(range->first, range->second)
            || right > std::max(range->first, range->second)) {
            alias_error("FSIM-ELAB-SVALIAS-001",
                "SystemVerilog alias selection is outside its packed "
                "net",
                expression.source);
            return false;
        }
        const auto offset = index_distance(right, range->second);
        if (selected_width == 0U || offset > *width
            || selected_width > *width - offset) {
            alias_error("FSIM-ELAB-SVALIAS-001",
                "SystemVerilog alias selection is outside its packed "
                "net",
                expression.source);
            return false;
        }
        segments.push_back({
            declaration->systemverilog->name,
            *declaration_id,
            offset,
            selected_width,
        });
        return true;
    };
    const auto flatten_alias = [&](const auto& self,
                                   const auto& retained_declarations,
                                   const semantic::ExpressionId expression_id,
                                   std::vector<HirAliasSegment>& segments)
        -> bool {
        const auto view
            = working_specialization.find_expression(expression_id);
        if (!view || view->systemverilog == nullptr) {
            return false;
        }
        const auto& expression = *view->systemverilog;
        using Kind = semantic::sv::ExpressionKind;
        if (expression.kind == Kind::concatenation) {
            if (expression.operands.empty()) {
                alias_error("FSIM-ELAB-SVALIAS-001",
                    "SystemVerilog alias concatenations cannot be empty",
                    expression.source);
                return false;
            }
            bool valid = true;
            for (auto operand = expression.operands.rbegin();
                operand != expression.operands.rend(); ++operand) {
                valid = self(self, retained_declarations, *operand, segments)
                    && valid;
            }
            return valid;
        }
        if (expression.kind == Kind::name) {
            const auto declaration_id = alias_declaration(
                retained_declarations, expression);
            const auto declaration = declaration_id
                ? working_specialization.find_declaration(*declaration_id)
                : std::optional<semantic::CompiledDeclarationView> { };
            const auto width = declaration
                    && declaration->systemverilog != nullptr
                ? hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
                      working_specialization,
                      *declaration->systemverilog)
                : std::optional<std::size_t> { };
            if (!declaration_id || !declaration
                || declaration->systemverilog == nullptr
                || declaration->systemverilog->form
                    != semantic::sv::DeclarationForm::net
                || !width || *width == 0U) {
                alias_error("FSIM-ELAB-SVALIAS-001",
                    "SystemVerilog alias terminals must be packed nets, "
                    "not variables",
                    expression.source);
                return false;
            }
            segments.push_back({
                declaration->systemverilog->name,
                *declaration_id,
                0U,
                *width,
            });
            return true;
        }
        if (expression.kind == Kind::index
            && expression.operands.size() == 2U) {
            const auto base = working_specialization.find_expression(
                expression.operands[0]);
            const auto index
                = working_specialization.evaluate_integral_expression(
                    expression.operands[1]);
            if (!base || base->systemverilog == nullptr || !index) {
                alias_error("FSIM-ELAB-SVALIAS-001",
                    "SystemVerilog alias indices must be locally "
                    "constant",
                    expression.source);
                return false;
            }
            return append_alias_selection(
                retained_declarations, expression,
                *base->systemverilog, *index, 1U, segments);
        }
        if (expression.kind == Kind::slice
            && expression.operands.size() == 3U) {
            const auto base = working_specialization.find_expression(
                expression.operands[0]);
            const auto left
                = working_specialization.evaluate_integral_expression(
                    expression.operands[1]);
            const auto right
                = working_specialization.evaluate_integral_expression(
                    expression.operands[2]);
            if (!base || base->systemverilog == nullptr
                || !left || !right) {
                alias_error("FSIM-ELAB-SVALIAS-001",
                    "SystemVerilog alias part-selects must be locally "
                    "constant",
                    expression.source);
                return false;
            }
            if (expression.text == "+:" || expression.text == "-:") {
                if (*right <= 0) {
                    return false;
                }
                const auto selected_width
                    = static_cast<std::uint64_t>(*right);
                const auto selected_right = expression.text == "+:"
                    ? *left
                    : *left - *right + 1;
                return append_alias_selection(
                    retained_declarations, expression,
                    *base->systemverilog, selected_right,
                    selected_width, segments);
            }
            return append_alias_selection(
                retained_declarations, expression,
                *base->systemverilog, *right,
                index_distance(*left, *right) + 1U, segments);
        }
        alias_error("FSIM-ELAB-SVALIAS-001",
            "SystemVerilog alias terminals must be static packed net "
            "lvalues",
            expression.source);
        return false;
    };

    SystemVerilogAliasPlan alias_plan;
    for (const auto& alias : aliases) {
        std::vector<std::vector<HirAliasSegment>> terminals;
        bool valid = alias.terminals.size() >= 2U;
        for (const auto terminal : alias.terminals) {
            auto& flattened = terminals.emplace_back();
            valid = flatten_alias(flatten_alias, declarations,
                        terminal, flattened)
                && valid;
        }
        if (!valid) {
            return std::nullopt;
        }
        const auto total_width = [](const auto& terminal) {
            return std::accumulate(terminal.begin(), terminal.end(),
                std::uint64_t { }, [](const auto total, const HirAliasSegment& segment) {
                    return total + segment.width;
                });
        };
        const auto width = total_width(terminals.front());
        for (std::size_t index = 1U;
            index < terminals.size(); ++index) {
            if (total_width(terminals[index]) != width) {
                alias_error("FSIM-ELAB-SVALIAS-003",
                    "SystemVerilog alias terminals have different bit "
                    "lengths",
                    alias.source);
                valid = false;
            }
        }
        if (!valid) {
            return std::nullopt;
        }
        const bool whole = std::ranges::all_of(
            alias.terminals, [&](const auto terminal) {
                const auto expression
                    = working_specialization.find_expression(terminal);
                return expression
                    && expression->systemverilog != nullptr
                    && expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::name;
            });
        if (whole) {
            std::vector<std::string> names;
            for (const auto& terminal : terminals) {
                names.push_back(terminal.front().name);
            }
            std::vector<std::size_t> matching;
            for (std::size_t group = 0U;
                group < alias_plan.whole_groups.size(); ++group) {
                if (std::ranges::any_of(names, [&](const auto& name) {
                        return std::ranges::find(
                                   alias_plan.whole_groups[group], name)
                            != alias_plan.whole_groups[group].end();
                    })) {
                    matching.push_back(group);
                }
            }
            if (matching.empty()) {
                alias_plan.whole_groups.push_back(std::move(names));
            } else {
                auto& merged
                    = alias_plan.whole_groups[matching.front()];
                for (const auto& name : names) {
                    if (std::ranges::find(merged, name) == merged.end()) {
                        merged.push_back(name);
                    }
                }
                for (auto index = matching.size(); index-- > 1U;) {
                    const auto group = matching[index];
                    for (const auto& name :
                        alias_plan.whole_groups[group]) {
                        if (std::ranges::find(merged, name)
                            == merged.end()) {
                            merged.push_back(name);
                        }
                    }
                    alias_plan.whole_groups.erase(
                        alias_plan.whole_groups.begin()
                        + static_cast<std::ptrdiff_t>(group));
                }
            }
            continue;
        }
        for (std::size_t terminal = 1U;
            terminal < terminals.size(); ++terminal) {
            const auto& left = terminals.front();
            const auto& right = terminals[terminal];
            std::size_t left_index { };
            std::size_t right_index { };
            std::uint64_t left_consumed { };
            std::uint64_t right_consumed { };
            while (left_index < left.size()
                && right_index < right.size()) {
                const auto& left_segment = left[left_index];
                const auto& right_segment = right[right_index];
                const auto count = std::min(
                    left_segment.width - left_consumed,
                    right_segment.width - right_consumed);
                alias_plan.connections.push_back({
                    left_segment.name,
                    left_segment.offset + left_consumed,
                    right_segment.name,
                    right_segment.offset + right_consumed,
                    count,
                    compiled_source_span(*compiled_, alias.source),
                });
                left_consumed += count;
                right_consumed += count;
                if (left_consumed == left_segment.width) {
                    ++left_index;
                    left_consumed = 0U;
                }
                if (right_consumed == right_segment.width) {
                    ++right_index;
                    right_consumed = 0U;
                }
            }
        }
    }
    return alias_plan;
}

void HierarchyBuilder::finalize_systemverilog_whole_aliases(
    SystemVerilogHirMaterialization& materialization)
{
    for (const auto& group : materialization.alias_plan.whole_groups) {
        std::optional<SignalId> canonical;
        for (const auto& name : group) {
            if (const auto found = materialization.signals.find(name);
                found != materialization.signals.end()) {
                canonical = found->second;
                break;
            }
        }
        if (!canonical) {
            continue;
        }
        for (const auto& name : group) {
            materialization.signals.insert_or_assign(name, *canonical);
            const auto full_name = materialization.path + "." + name;
            materialization.signals.insert_or_assign(
                full_name, *canonical);
            design_.signal_by_name_.insert_or_assign(
                full_name, *canonical);
            if (design_.roots_.size() == 1U) {
                design_.signal_by_name_.insert_or_assign(
                    name, *canonical);
            }
        }
    }
}

void HierarchyBuilder::materialize_systemverilog_clocking_blocks(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialization,
    const std::string& path,
    SignalMap& signals,
    std::vector<Process>& clocking_processes)
{
    const auto clocking_signal_name = [&](const semantic::sv::ClockingSignal& member)
        -> std::optional<std::string> {
        if (!member.expression) {
            if (member.name.selected) {
                const auto declaration = specialization.find_declaration(
                    *member.name.selected);
                if (declaration
                    && declaration->systemverilog != nullptr) {
                    return declaration->systemverilog->name;
                }
            }
            return member.name.spelling.empty()
                ? std::nullopt
                : std::optional { member.name.spelling };
        }
        const auto expression = specialization.find_expression(
            *member.expression);
        if (!expression || expression->systemverilog == nullptr
            || expression->systemverilog->kind
                != semantic::sv::ExpressionKind::name) {
            return std::nullopt;
        }
        if (expression->systemverilog->referenced_name
            && expression->systemverilog->referenced_name->selected) {
            const auto declaration = specialization.find_declaration(
                *expression->systemverilog->referenced_name->selected);
            if (declaration
                && declaration->systemverilog != nullptr) {
                return declaration->systemverilog->name;
            }
        }
        return expression->systemverilog->text.empty()
            ? std::nullopt
            : std::optional { expression->systemverilog->text };
    };
    const auto add_clocking_signal = [&](const std::string& local_name,
                                         const SignalId model)
        -> std::optional<SignalId> {
        if (model >= design_.signal_info_.size()
            || model >= design_.signals_.size()
            || design_.signals_.size()
                > std::numeric_limits<SignalId>::max()) {
            return std::nullopt;
        }
        const auto id = static_cast<SignalId>(design_.signals_.size());
        const auto full_name = path + "." + local_name;
        auto info = design_.signal_info_[model];
        info.id = id;
        info.name = full_name;
        info.is_port = false;
        info.direction = frontend::PortDirection::Unknown;
        auto signal = design_.signals_[model];
        signal.name = full_name;
        design_.signal_info_.push_back(std::move(info));
        design_.signals_.push_back(std::move(signal));
        design_.signal_by_name_.insert_or_assign(full_name, id);
        signals.insert_or_assign(local_name, id);
        signals.insert_or_assign(full_name, id);
        return id;
    };
    const auto clocking_edge = [](const semantic::sv::EdgeKind edge) {
        switch (edge) {
        case semantic::sv::EdgeKind::positive:
            return runtime::simir::EdgeKind::posedge;
        case semantic::sv::EdgeKind::negative:
            return runtime::simir::EdgeKind::negedge;
        case semantic::sv::EdgeKind::any:
            return runtime::simir::EdgeKind::any;
        }
        return runtime::simir::EdgeKind::any;
    };
    const auto clocking_delay = [&](const semantic::sv::ClockingSkew* skew,
                                    const std::string_view member)
        -> std::optional<runtime::SimulationTick> {
        if (skew == nullptr) {
            return 0U;
        }
        if (skew->one_step) {
            return 1U;
        }
        if (!skew->delay) {
            return 0U;
        }
        auto magnitude = skew->delay->primary.magnitude;
        if (skew->delay->primary.expression) {
            const auto value = specialization.evaluate_integral_expression(
                *skew->delay->primary.expression);
            if (!value || *value < 0) {
                if (unit.standard != "2023") {
                    return magnitude;
                }
                report(
                    "FSIM-ELAB-CLOCK-008",
                    "clocking skew for '" + path + "."
                        + std::string { member }
                        + "' must be a known nonnegative "
                          "elaboration-time constant",
                    compiled_source_span(
                        *compiled_, skew->delay->primary.source));
                return std::nullopt;
            }
            const auto factor = static_cast<std::uint64_t>(*value);
            if (factor != 0U
                && magnitude
                    > std::numeric_limits<std::uint64_t>::max()
                        / factor) {
                report(
                    "FSIM-ELAB-CLOCK-009",
                    "clocking skew for '" + path + "."
                        + std::string { member }
                        + "' overflows the 64-bit simulation time range",
                    compiled_source_span(
                        *compiled_, skew->delay->primary.source));
                return std::nullopt;
            }
            magnitude *= factor;
        }
        return magnitude;
    };
    const auto add_clocking_process = [&](
                                          const std::string& name,
                                          const SignalId source,
                                          const SignalId destination,
                                          const SignalId event,
                                          const runtime::simir::EdgeKind edge,
                                          const runtime::SimulationTick delay,
                                          const bool initialize,
                                          const bool observed) {
        Process process;
        process.name = path + "." + name;
        process.register_count = 1U;
        process.register_value_kinds.push_back(value_kind(
            design_.signal_info_[source].source_domain));
        process.static_sensitivity.push_back({ event, edge });
        process.initialize = true;
        process.observed = observed;
        if (!initialize) {
            process.operations.emplace_back(WaitSensitivity { });
        }
        process.operations.emplace_back(ReadSignal { 0U, source });
        if (delay == 0U) {
            process.operations.emplace_back(WriteBlocking {
                destination, 0U });
        } else {
            process.operations.emplace_back(WriteAfter {
                destination, 0U, delay });
        }
        if (initialize) {
            process.operations.emplace_back(WaitSensitivity { });
        }
        process.operations.emplace_back(Jump { 0U });
        process.driver_regions.push_back({
            destination,
            0U,
            static_cast<std::uint32_t>(std::min<std::size_t>(
                design_.signal_info_[destination].width,
                std::numeric_limits<std::uint32_t>::max())),
            true,
        });
        clocking_processes.push_back(std::move(process));
    };
    if (!unit.clocking_blocks.empty()) {
        for (const auto& block : unit.clocking_blocks) {
            if (block.event.size() != 1U
                || block.event.front().signal.empty()) {
                report(
                    "FSIM-ELAB-CLOCK-001",
                    "clocking block '" + path + "." + block.name
                        + "' requires one signal event",
                    compiled_source_span(*compiled_, block.source));
                continue;
            }
            const auto event = signals.find(
                block.event.front().signal);
            if (event == signals.end()) {
                report(
                    "FSIM-ELAB-CLOCK-002",
                    "unknown event signal '"
                        + block.event.front().signal
                        + "' for clocking block '" + path + "."
                        + block.name + "'",
                    compiled_source_span(
                        *compiled_, block.event.front().source));
                continue;
            }
            signals.insert_or_assign(block.name, event->second);
            systemverilog_clocking_event_signals_.insert_or_assign(
                path + "." + block.name, event->second);
            for (const auto& member : block.signals) {
                const auto actual_name = clocking_signal_name(member);
                if (!actual_name) {
                    report(
                        "FSIM-ELAB-CLOCK-003",
                        "clocking member '" + block.name + "."
                            + member.name.spelling
                            + "' requires a signal identifier "
                              "expression",
                        compiled_source_span(
                            *compiled_, member.source));
                    continue;
                }
                const auto actual = signals.find(*actual_name);
                if (actual == signals.end()) {
                    report(
                        "FSIM-ELAB-CLOCK-004",
                        "unknown signal '" + *actual_name
                            + "' for clocking member '" + block.name
                            + "." + member.name.spelling + "'",
                        compiled_source_span(
                            *compiled_, member.source));
                    continue;
                }
                const auto member_name = block.name + "."
                    + member.name.spelling;
                const auto* selected_skew = member.skew
                    ? &*member.skew
                    : member.direction == semantic::sv::Direction::input
                    ? block.default_input_skew
                        ? &*block.default_input_skew
                        : nullptr
                    : member.direction
                            == semantic::sv::Direction::output
                        && block.default_output_skew
                    ? &*block.default_output_skew
                    : nullptr;
                auto delay = clocking_delay(
                    selected_skew, member_name);
                if (!delay) {
                    continue;
                }
                if (member.direction
                        == semantic::sv::Direction::input
                    && (selected_skew == nullptr
                        || selected_skew->one_step
                        || !selected_skew->delay)) {
                    // SystemVerilog's default input skew is one time step.
                    // Keep the history signal one tick behind the event so
                    // an observed-region sample cannot see the active-region
                    // update that triggered the clocking event.
                    delay = 1U;
                }
                auto selected_edge = clocking_edge(
                    block.event.front().edge);
                if (selected_skew != nullptr
                    && selected_skew->edge
                        != semantic::sv::EdgeKind::any) {
                    selected_edge = clocking_edge(selected_skew->edge);
                }
                if (member.direction
                        == semantic::sv::Direction::output
                    && *delay != 0U) {
                    const auto request = add_clocking_signal(
                        member_name, actual->second);
                    if (!request) {
                        continue;
                    }
                    const bool edge_qualified
                        = selected_skew != nullptr
                        && selected_skew->edge
                            != semantic::sv::EdgeKind::any;
                    add_clocking_process(
                        "$clocking$" + block.name + "$"
                            + member.name.spelling + "$drive",
                        *request,
                        actual->second,
                        edge_qualified ? event->second : *request,
                        edge_qualified
                            ? selected_edge
                            : runtime::simir::EdgeKind::any,
                        *delay,
                        !edge_qualified,
                        false);
                    continue;
                }
                if (member.direction
                    != semantic::sv::Direction::input) {
                    const auto full_name = path + "." + member_name;
                    signals.insert_or_assign(
                        member_name, actual->second);
                    signals.insert_or_assign(
                        full_name, actual->second);
                    design_.signal_by_name_.insert_or_assign(
                        full_name, actual->second);
                }
                if (member.direction
                    == semantic::sv::Direction::output) {
                    continue;
                }
                auto sample_source = actual->second;
                if (*delay != 0U) {
                    const auto skew_name = "$clocking$" + block.name
                        + "$" + member.name.spelling + "$skew";
                    const auto skew_signal = add_clocking_signal(
                        skew_name, actual->second);
                    if (!skew_signal) {
                        continue;
                    }
                    add_clocking_process(
                        skew_name + "$history",
                        actual->second,
                        *skew_signal,
                        actual->second,
                        runtime::simir::EdgeKind::any,
                        *delay,
                        true,
                        false);
                    sample_source = *skew_signal;
                }
                const auto sampled = add_clocking_signal(
                    member_name, actual->second);
                if (!sampled) {
                    continue;
                }
                add_clocking_process(
                    "$clocking$" + block.name + "$"
                        + member.name.spelling + "$sample",
                    sample_source,
                    *sampled,
                    event->second,
                    selected_edge,
                    0U,
                    false,
                    true);
            }
        }
    }
}

void HierarchyBuilder::append_selected_systemverilog_classes(
    const semantic::SpecializedHirUnit& specialized,
    const std::span<const semantic::DeclarationId> selected_generates)
{
    for (const auto& declaration :
        specialized.design().systemverilog_hir.classes()) {
        if (!declaration.generate_owner
            || std::ranges::find(
                   selected_generates, *declaration.generate_owner)
                == selected_generates.end()) {
            continue;
        }
        const auto& owner_scope
            = specialized.design().semantics.scopes().at(
                declaration.scope.value());
        const auto owner_unit = owner_scope.unit;
        SelectedSystemVerilogClass selection {
            owner_unit,
            *declaration.generate_owner,
            declaration.scope,
            declaration.origin,
            semantic::sv::class_declaration_identity(declaration),
        };
        if (std::ranges::find(
                selected_systemverilog_classes_, selection)
            == selected_systemverilog_classes_.end()) {
            selected_systemverilog_classes_.push_back(
                std::move(selection));
        }
    }
}

std::vector<semantic::UnitId>
HierarchyBuilder::collect_compiled_systemverilog_source_dependency_units(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialized,
    SpecializationInfo& specialization)
{
    const auto append_source_dependency = [&](const std::string& dependency) {
        if (!dependency.empty()
            && dependency != specialization.source
            && std::ranges::find(
                   specialization.source_dependencies, dependency)
                == specialization.source_dependencies.end()) {
            specialization.source_dependencies.push_back(
                dependency);
        }
    };
    std::vector<semantic::UnitId> dependency_units;
    const auto append_dependency_unit = [&](const auto& self, const semantic::UnitId dependency_id) -> void {
        if (std::ranges::find(
                dependency_units, dependency_id)
            != dependency_units.end()) {
            return;
        }
        dependency_units.push_back(dependency_id);
        const auto dependency = compiled_->find_unit(
            dependency_id);
        if (!dependency
            || dependency->systemverilog == nullptr) {
            return;
        }
        if (const auto* source = compiled_physical_source(
                *compiled_, dependency->systemverilog->source)) {
            append_source_dependency(*source);
        }
        for (const auto& source :
            dependency->systemverilog->source_dependencies) {
            append_source_dependency(source);
        }
        for (const auto& reference : compiled_->references()) {
            if (reference.owner == dependency_id
                && reference.target
                && (reference.kind
                        == semantic::CompiledReferenceKind::package
                    || reference.kind
                        == semantic::CompiledReferenceKind::import)) {
                self(self, *reference.target);
            }
        }
    };
    for (const auto& dependency : unit.source_dependencies) {
        append_source_dependency(dependency);
    }
    append_dependency_unit(append_dependency_unit, unit.id);
    for (const auto& actual :
        specialized.specialization().actual_identities) {
        if (!actual.actual_declaration) {
            continue;
        }
        const auto declaration = compiled_->find_declaration(
            *actual.actual_declaration);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->scope.valid()) {
            continue;
        }
        const auto* declaration_scope = compiled_semantic_scope(
            *compiled_, declaration->systemverilog->scope);
        if (declaration_scope != nullptr) {
            append_dependency_unit(
                append_dependency_unit, declaration_scope->unit);
        }
    }
    return dependency_units;
}

bool HierarchyBuilder::compiled_systemverilog_concurrent_assertions_materialized(
    const semantic::sv::Unit& unit) const
{
    const auto matches = [&](const semantic::sv::Process& process,
                             const semantic::sv::ConcurrentAssertion& assertion) {
        return process.concurrent_assertion
            && process.name == assertion.name
            && process.source == assertion.source;
    };
    for (std::size_t index = 0U;
        index < unit.concurrent_assertions.size(); ++index) {
        const auto& assertion = unit.concurrent_assertions[index];
        const bool failure_callback
            = assertion.kind
                == semantic::sv::ConcurrentAssertionKind::assertion
            || assertion.kind
                == semantic::sv::ConcurrentAssertionKind::assumption;
        if (assertion.coverage_slot != index
            || assertion.sampling_region
                != semantic::sv::AssertionRegion::preponed
            || assertion.evaluation_region
                != semantic::sv::AssertionRegion::observed
            || assertion.action_region
                != semantic::sv::AssertionRegion::reactive
            || assertion.observers.callback_on_failure
                != failure_callback
            || !assertion.observers.debugger_visible
            || !assertion.observers.trace_visible
            || !assertion.observers.coverage_enabled) {
            return false;
        }
        std::size_t process_matches { };
        for (const auto process_id : unit.processes) {
            const auto process = compiled_->find_process(process_id);
            if (process && process->systemverilog != nullptr
                && matches(*process->systemverilog, assertion)) {
                ++process_matches;
            }
        }
        if (process_matches != 1U) {
            return false;
        }
    }
    for (const auto process_id : unit.processes) {
        const auto process = compiled_->find_process(process_id);
        if (!process || process->systemverilog == nullptr
            || !process->systemverilog->concurrent_assertion) {
            continue;
        }
        if (std::ranges::count_if(unit.concurrent_assertions,
                [&](const auto& assertion) {
                    return matches(*process->systemverilog, assertion);
                })
            != 1) {
            return false;
        }
    }
    return true;
}

bool HierarchyBuilder::instantiate_compiled_systemverilog_unit(
    CompiledSystemVerilogInstantiationContext context)
{
    const auto root = context.unit;
    auto path = std::move(context.path);
    auto actuals = std::move(context.actuals);
    auto port_aliases = std::move(context.port_aliases);
    auto string_port_aliases = std::move(context.string_port_aliases);
    auto container_port_aliases = std::move(context.container_port_aliases);
    const auto source_instance = context.source_instance;
    auto prepared_specialization
        = std::move(context.prepared_specialization);
    if (compiled_ == nullptr || root.identity == nullptr
        || root.systemverilog == nullptr || root.vhdl != nullptr) {
        report(
            "FSIM-ELAB-HIR-001",
            "parser-free hierarchy construction requires one "
            "SystemVerilog compiled-HIR unit",
            { });
        return false;
    }
    const auto& unit = *root.systemverilog;
    if (path == active_root_) {
        for (auto& diagnostic : validate_compiled_systemverilog_unit(
                 *compiled_, unit)) {
            report(
                std::move(diagnostic.code),
                std::move(diagnostic.message),
                compiled_source_span(*compiled_, diagnostic.source));
        }
    }
    const auto executable_unit
        = unit.kind == semantic::sv::UnitKind::module
        || unit.kind == semantic::sv::UnitKind::interface || unit.kind == semantic::sv::UnitKind::program;
    const auto concurrent_assertions_materialized
        = compiled_systemverilog_concurrent_assertions_materialized(unit);
    // Clocking blocks, the default-clocking selection, and DPI declarations
    // are complete HIR metadata. Interface hierarchy and virtual-interface
    // handles can be materialized without retaining a structural syntax
    // owner. DPI linkage and resolved-profile metadata is consumed by the
    // foreign-call path rather than hierarchy construction. Checker instances
    // are likewise compile-time provenance: the frontend has already
    // substituted their actuals into the declaration and concurrent-assertion
    // HIR validated above, including the executable assertion processes.
    const auto unsupported_structure = !executable_unit
        || unit.external
        || !concurrent_assertions_materialized
        || unit.configuration.has_value();
    if (unsupported_structure) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled-HIR unit '" + unit.name
                + "' still requires a structural syntax-lowering "
                  "adapter",
            compiled_source_span(*compiled_, unit.source));
        return false;
    }
    const auto identity = unit_identity(root);
    if (std::ranges::find(stack_, identity) != stack_.end()) {
        report(
            "FSIM-ELAB-HIR-001",
            "recursive compiled-HIR hierarchy for '" + identity
                + "' requires an unavailable specialization adapter",
            compiled_source_span(*compiled_, unit.source));
        return false;
    }
    CompiledHierarchyStackGuard stack_guard { stack_, identity };
    register_systemverilog_resolution_functions(unit);
    std::vector<CompiledSpecializationFailure>
        specialization_failures;
    auto specialized = std::move(prepared_specialization);
    if (!specialized) {
        specialized = compiled_specialization(
            validated_compiled_, unit.id, actuals,
            &specialization_failures);
    }
    for (const auto& failure : specialization_failures) {
        report(
            failure.code,
            failure.message,
            compiled_source_span(*compiled_, failure.source));
    }
    if (!specialized) {
        report(
            "FSIM-ELAB-HIR-001",
            "cannot construct a parser-free specialization for '"
                + unit.name + "'",
            compiled_source_span(*compiled_, unit.source));
        return false;
    }
    for (const auto declaration_id : unit.declarations) {
        const auto declaration = specialized->find_declaration(
            declaration_id);
        if (!declaration || declaration->systemverilog == nullptr
            || declaration->systemverilog->form
                != semantic::sv::DeclarationForm::local_parameter
            || !declaration->systemverilog->initializer
            || !compiled_systemverilog_string_declaration(
                *declaration->systemverilog)
            || specialized->evaluate_string_declaration(
                declaration_id)) {
            continue;
        }
        report(
            "FSIM-ELAB-SVSTRING-001",
            "cannot evaluate default for string local parameter '"
                + declaration->systemverilog->name + "'",
            compiled_source_span(
                *compiled_, declaration->systemverilog->source));
    }

    CompiledHierarchyContainerGuard defparam_guard {
        active_compiled_systemverilog_defparams_
    };
    CompiledHierarchyContainerGuard bind_guard {
        active_compiled_systemverilog_binds_
    };
    activate_compiled_systemverilog_defparams(
        unit.defparams, path, *specialized);
    for (const auto& bind : unit.binds) {
        active_compiled_systemverilog_binds_.push_back({ &bind, &unit });
    }

    auto generate_collection
        = hierarchy_sv_generate_detail::collect_occurrences(
            unit, path, *specialized);
    if (generate_collection.diagnostic) {
        report(
            std::move(generate_collection.diagnostic->code),
            std::move(generate_collection.diagnostic->message),
            compiled_source_span(
                *compiled_, generate_collection.diagnostic->source));
        return false;
    }
    auto generate_occurrences
        = std::move(generate_collection.occurrences);
    const auto generated_path = [](const std::string_view parent,
                                   const std::string_view local) {
        if (parent.empty()) {
            return std::string { local };
        }
        if (local.empty()) {
            return std::string { parent };
        }
        return std::string { parent } + "." + std::string { local };
    };
    for (const auto& occurrence : generate_occurrences) {
        activate_compiled_systemverilog_defparams(
            occurrence.region->defparams,
            occurrence.path,
            occurrence.specialization);
    }

    std::vector<semantic::DeclarationId> active_declarations {
        unit.declarations.begin(), unit.declarations.end()
    };
    std::vector<semantic::ProcessId> active_processes {
        unit.processes.begin(), unit.processes.end()
    };
    std::vector<semantic::StatementId> active_concurrent_statements {
        unit.concurrent_statements.begin(),
        unit.concurrent_statements.end()
    };
    if (unit.kind == semantic::sv::UnitKind::program) {
        active_concurrent_statements.clear();
    }
    const auto selected_generates = specialized->selected_generates();

    bool generated_constants_valid = true;
    for (const auto& occurrence : generate_occurrences) {
        for (const auto declaration : occurrence.region->declarations) {
            const auto validation
                = hierarchy_sv_generate_detail::validate_generated_constant(
                    occurrence, declaration);
            if (validation.diagnostic) {
                report(
                    validation.diagnostic->code,
                    validation.diagnostic->message,
                    compiled_source_span(
                        *compiled_, validation.diagnostic->source));
            }
            generated_constants_valid
                = validation.valid() && generated_constants_valid;
        }
    }
    if (!generated_constants_valid) {
        return false;
    }

    const auto systemverilog_packed_type = [&](
        const semantic::SpecializedHirUnit& working_specialization,
        const semantic::sv::TypeReference& reference) {
        return compiled_systemverilog_packed_type(
            *compiled_, working_specialization, reference);
    };
    const auto systemverilog_packed_default = [&](
        const semantic::SpecializedHirUnit& working_specialization,
        const semantic::sv::TypeReference& reference,
        const std::size_t width) {
        return compiled_systemverilog_packed_default(
            *compiled_, working_specialization, reference, width);
    };
    const SystemVerilogPackedTypeResolver packed_type_resolver {
        systemverilog_packed_type
    };
    const SystemVerilogPackedDefaultResolver packed_default_resolver {
        systemverilog_packed_default
    };
    const SystemVerilogPackedFallbackResolver packed_fallback_resolver {
        [](const PackedTypeMetadata& type, const std::size_t width) {
            return default_compiled_packed_value(type, width);
        }
    };

    auto root_alias_plan = build_systemverilog_alias_plan(
        *specialized, active_declarations, unit.aliases);
    if (!root_alias_plan) {
        return false;
    }

    SystemVerilogHirMaterialization root_materialization {
        &*specialized,
        path,
        std::move(port_aliases),
        std::move(string_port_aliases),
        std::move(container_port_aliases),
        { },
        { },
        { },
        { },
        std::move(*root_alias_plan),
    };
    std::vector<PendingVirtualInterfaceInitializer>
        pending_virtual_interface_initializers;
    auto unconnected_drive = semantic::sv::UnconnectedDrive::none;
    if (source_instance) {
        const auto instance = compiled_->find_instance(
            *source_instance);
        if (instance && instance->systemverilog != nullptr) {
            unconnected_drive
                = instance->systemverilog->unconnected_drive;
        }
    }
    for (const auto declaration_id : active_declarations) {
        const auto declaration = specialized->find_declaration(
            declaration_id);
        if (!declaration
            || declaration->systemverilog == nullptr
            || !materialize_compiled_systemverilog_declaration(
                    unit, root_materialization,
                    *declaration->systemverilog, path, unconnected_drive,
                    pending_virtual_interface_initializers,
                    packed_type_resolver, packed_default_resolver,
                    packed_fallback_resolver)) {
            return false;
        }
    }
    auto& signals = root_materialization.signals;
    auto& string_objects = root_materialization.string_objects;
    auto& container_objects = root_materialization.container_objects;
    auto& read_only_strings
        = root_materialization.read_only_strings;
    auto& read_only_containers
        = root_materialization.read_only_containers;
    auto& read_only_signals = root_materialization.read_only_signals;
    for (const auto& [name, signal] : signals) {
        const auto qualified_name = name.starts_with(path + ".")
            ? name
            : path + "." + name;
        if (systemverilog_read_only_interface_member_paths_.contains(
                qualified_name)) {
            read_only_signals.insert(signal);
        }
    }
    finalize_systemverilog_whole_aliases(root_materialization);

    std::vector<Process> clocking_processes;
    materialize_systemverilog_clocking_blocks(
        unit, *specialized, path, signals, clocking_processes);
    std::vector<SystemVerilogHirMaterialization>
        generated_materializations;
    if (!materialize_compiled_systemverilog_generated_scopes(
            unit, root_materialization, generate_occurrences, path,
            unconnected_drive, pending_virtual_interface_initializers,
            packed_type_resolver, packed_default_resolver,
            packed_fallback_resolver, generated_materializations)) {
        return false;
    }

    // Procedural virtual-interface assignments are lowered before child
    // occurrences are recursively instantiated. Reserve deterministic
    // handles for interface children now so retained HIR can name those
    // occurrences without depending on the later hierarchy walk.
    reserve_compiled_systemverilog_interface_occurrences(
        unit, *specialized, path, generate_occurrences, generated_path);

    Lowerer lowerer {
        design_,
        signals,
        read_only_signals,
        string_objects,
        read_only_strings,
        container_objects,
        read_only_containers,
        diagnostics_,
    };
    lowerer.set_specialized_hir_unit(&*specialized);
    lowerer.set_systemverilog_interface_handles(
        &systemverilog_interface_handles_);

    const auto specialization_index = design_.specializations_.size();
    const auto specialization_id = static_cast<SpecializationId>(
        specialization_index);
    if (static_cast<std::size_t>(specialization_id)
        != specialization_index) {
        throw std::length_error(
            "too many elaborated design-unit specializations");
    }
    SpecializationInfo specialization;
    specialization.id = specialization_id;
    specialization.unit = identity;
    specialization.instance = path;
    specialization.source_unit = unit.id;
    specialization.source_instance = source_instance;
    const auto program_owner
        = unit.kind == semantic::sv::UnitKind::program
        ? std::optional<std::uint32_t> { specialization_id }
        : std::nullopt;
    lowerer.set_systemverilog_program_owner(program_owner);
    specialization.source_span = unit.source;
    specialization.origin = unit.origin;
    specialization.source = compiled_physical_source(
                                *compiled_, unit.source)
            != nullptr
        ? *compiled_physical_source(*compiled_, unit.source)
        : unit.compilation_unit_identity;
    const auto source_language = compiled_frontend_language(
        root.language);
    specialization.language = source_language;
    specialization.library = unit.library.empty()
        ? "work"
        : unit.library;
    specialization.is_cell = unit.compilation.cell;
    auto dependency_units
        = collect_compiled_systemverilog_source_dependency_units(
            unit, *specialized, specialization);
    std::unordered_set<std::string> published_parameter_names;
    const auto append_parameter_metadata = [&](const semantic::DeclarationId declaration_id,
                                               const bool require_record) {
        return append_compiled_systemverilog_parameter_metadata(
            declaration_id,
            require_record,
            unit,
            *specialized,
            published_parameter_names,
            specialization);
    };
    for (const auto dependency_id : dependency_units) {
        if (dependency_id == unit.id) {
            continue;
        }
        const auto dependency = compiled_->find_unit(dependency_id);
        if (!dependency || dependency->systemverilog == nullptr
            || dependency->systemverilog->kind
                != semantic::sv::UnitKind::package) {
            continue;
        }
        for (const auto declaration_id :
            dependency->systemverilog->declarations) {
            static_cast<void>(append_parameter_metadata(
                declaration_id, false));
        }
    }
    for (const auto declaration_id : unit.declarations) {
        if (!append_parameter_metadata(declaration_id, true)) {
            return false;
        }
    }
    const auto specify_path_begin
        = design_.verilog_specify_paths_.size();
    validate_verilog_specify(
        unit, *specialized, path, signals, specialization);
    if (active_compiled_systemverilog_configuration_ != nullptr) {
        specialization.parameter_identity_values.emplace_back(
            "__configuration",
            systemverilog_configuration_identity(
                *active_compiled_systemverilog_configuration_));
    }

    std::size_t concurrent_order { };
    if (!lower_compiled_systemverilog_processes(
            unit, *specialized, path, source_language,
            active_concurrent_statements, active_processes,
            generate_occurrences, generated_materializations, lowerer,
            clocking_processes, specialization, program_owner,
            concurrent_order)) {
        return false;
    }
    append_selected_systemverilog_classes(
        *specialized, selected_generates);
    attach_verilog_specify_drivers(
        specify_path_begin, specialization.processes);
    add_systemverilog_alias_connections(
        root_materialization.alias_plan, path, signals,
        specialization);
    for (const auto& materialization :
        generated_materializations) {
        add_systemverilog_alias_connections(
            materialization.alias_plan,
            materialization.path,
            materialization.signals,
            specialization);
    }
    design_.specializations_.push_back(std::move(specialization));

    auto instance_worklist
        = collect_compiled_systemverilog_instance_materializations(
            unit, root_materialization, generate_occurrences,
            generated_materializations, path);
    if (instance_worklist.diagnostic) {
        report(
            instance_worklist.diagnostic->code,
            instance_worklist.diagnostic->message,
            compiled_source_span(
                *compiled_, instance_worklist.diagnostic->source));
        return false;
    }
    auto& instance_materializations = instance_worklist.instances;

    if (!instantiate_compiled_systemverilog_instance_worklist(
            unit, instance_materializations, source_language,
            concurrent_order, generated_path)) {
        return false;
    }
    resolve_compiled_systemverilog_virtual_interface_initializers(
        unit, pending_virtual_interface_initializers);
    for (std::size_t index = defparam_guard.size();
        index < active_compiled_systemverilog_defparams_.size();
        ++index) {
        const auto& defparam
            = active_compiled_systemverilog_defparams_[index];
        if (!defparam.matched) {
            report(
                "FSIM-ELAB-DEFPARAM-002",
                "unknown defparam hierarchical target '"
                    + defparam.target_path + "."
                    + defparam.parameter + "'",
                compiled_source_span(*compiled_, defparam.source));
        }
    }
    return true;
}

bool HierarchyBuilder::materialize_compiled_systemverilog_generated_scopes(
    const semantic::sv::Unit& unit,
    const SystemVerilogHirMaterialization& root_materialization,
    const std::vector<hierarchy_sv_generate_detail::Occurrence>&
        generate_occurrences,
    const std::string& path,
    const semantic::sv::UnconnectedDrive unconnected_drive,
    std::vector<PendingVirtualInterfaceInitializer>&
        pending_virtual_interface_initializers,
    const SystemVerilogPackedTypeResolver& packed_type_resolver,
    const SystemVerilogPackedDefaultResolver& packed_default_resolver,
    const SystemVerilogPackedFallbackResolver& packed_fallback_resolver,
    std::vector<SystemVerilogHirMaterialization>&
        generated_materializations)
{
    generated_materializations.reserve(generate_occurrences.size());
    for (const auto& occurrence : generate_occurrences) {
        auto occurrence_alias_plan = build_systemverilog_alias_plan(
            occurrence.specialization,
            occurrence.region->declarations,
            occurrence.region->aliases);
        if (!occurrence_alias_plan) {
            return false;
        }
        const SystemVerilogHirMaterialization* parent
            = &root_materialization;
        for (auto candidate = generated_materializations.rbegin();
            candidate != generated_materializations.rend();
            ++candidate) {
            if (occurrence.path.size() > candidate->path.size()
                && occurrence.path.starts_with(candidate->path)
                && occurrence.path[candidate->path.size()] == '.') {
                parent = &*candidate;
                break;
            }
        }
        SystemVerilogHirMaterialization materialization {
            &occurrence.specialization,
            occurrence.path,
            SignalMap { &parent->signals },
            StringMap { &parent->string_objects },
            ContainerMap { &parent->container_objects },
            ReadOnlyStringSet { &parent->read_only_strings },
            ReadOnlyContainerSet { &parent->read_only_containers },
            ReadOnlySignalSet { &parent->read_only_signals },
            { },
            std::move(*occurrence_alias_plan),
        };
        for (const auto declaration_id :
            occurrence.region->declarations) {
            const auto declaration = occurrence.specialization
                                         .find_declaration(
                                             declaration_id);
            if (!declaration
                || declaration->systemverilog == nullptr
                || !materialize_compiled_systemverilog_declaration(
                    unit, materialization, *declaration->systemverilog,
                    path, unconnected_drive,
                    pending_virtual_interface_initializers,
                    packed_type_resolver, packed_default_resolver,
                    packed_fallback_resolver)) {
                return false;
            }
        }
        finalize_systemverilog_whole_aliases(materialization);
        generated_materializations.push_back(
            std::move(materialization));
    }

    return true;
}

void HierarchyBuilder::reserve_compiled_systemverilog_interface_occurrences(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialized,
    const std::string_view path,
    const std::vector<hierarchy_sv_generate_detail::Occurrence>&
        generate_occurrences,
    std::string (*generated_path)(std::string_view, std::string_view))
{
    const auto reserve_interface_occurrence = [&](
                                                  const semantic::InstanceId id,
                                                  const semantic::SpecializedHirUnit& owner,
                                                  const std::string_view owner_path,
                                                  const bool generated) {
        const auto instance = owner.find_instance(id);
        if (!instance || instance->systemverilog == nullptr) {
            return;
        }
        const auto occurrence = resolve_compiled_occurrence(
            owner, *instance);
        if (!occurrence || !occurrence->linked_target
            || occurrence->linked_target->systemverilog == nullptr
            || occurrence->linked_target->systemverilog->kind
                != semantic::sv::UnitKind::interface) {
            return;
        }
        const auto& record = *instance->systemverilog;
        auto occurrence_path = generated
            ? generated_path(owner_path, record.name)
            : compiled_instance_path(*compiled_, unit.scope,
                  record.scope, owner_path, record.name);
        const auto reserve = [&](const std::string& child_path) {
            auto [handle, inserted]
                = systemverilog_interface_handles_.try_emplace(
                    child_path, 0U);
            if (inserted) {
                handle->second = next_systemverilog_interface_handle_++;
            }
            systemverilog_interface_types_.insert_or_assign(
                child_path,
                occurrence->linked_target->systemverilog->name);
        };
        if (record.array_indices.empty()) {
            reserve(occurrence_path);
            return;
        }
        for (const auto index : record.array_indices) {
            reserve(occurrence_path + "[" + std::to_string(index) + "]");
        }
    };
    for (const auto instance : unit.instances) {
        reserve_interface_occurrence(
            instance, specialized, path, false);
    }
    for (const auto& occurrence : generate_occurrences) {
        for (const auto instance : occurrence.region->instances) {
            reserve_interface_occurrence(
                instance, occurrence.specialization,
                occurrence.path, true);
        }
    }
}

void HierarchyBuilder::resolve_compiled_systemverilog_virtual_interface_initializers(
    const semantic::sv::Unit& unit,
    const std::vector<PendingVirtualInterfaceInitializer>&
        pending_virtual_interface_initializers)
{
    const auto compiled_interface_type = [&](const std::string_view name)
        -> const semantic::sv::Unit* {
        const auto found = compiled_->find_unit(
            semantic::UnitKind::systemverilog_interface,
            compiled_systemverilog_library(unit), name);
        return found && found->systemverilog != nullptr
            ? found->systemverilog
            : nullptr;
    };
    const auto virtual_interface_specialization_identity
        = [&](const semantic::sv::TypeReference& type,
              const semantic::SpecializedHirUnit& parent)
        -> std::optional<std::vector<
            std::pair<std::string, std::string>>> {
        const auto* interface = compiled_interface_type(
            type.interface_type);
        if (interface == nullptr) {
            return std::nullopt;
        }
        semantic::sv::Instance type_instance;
        type_instance.scope = parent.scope();
        type_instance.parameters = type.interface_parameter_actuals;
        type_instance.source = type.target.source;
        auto bindings = semantic::resolve_specialized_hir_associations(
            *compiled_, interface->id,
            semantic::CompiledInstanceView { &type_instance, nullptr },
            semantic::SpecializedHirAssociationSurface::parameters,
            &parent);
        if (!bindings) {
            return std::nullopt;
        }
        std::vector<semantic::SpecializedHirActualIdentity>
            interface_actuals;
        interface_actuals.reserve(bindings.bindings.size());
        for (const auto& binding : bindings.bindings) {
            if (binding.identity.empty()) {
                return std::nullopt;
            }
            interface_actuals.push_back({
                binding.formal,
                binding.identity,
                binding.actual_declaration,
                binding.expression,
                binding.systemverilog_type,
                binding.vhdl_type,
                binding.source,
            });
        }
        auto specialized_interface = compiled_specialization(
            validated_compiled_, interface->id, interface_actuals,
            nullptr);
        if (!specialized_interface) {
            return std::nullopt;
        }
        std::vector<std::pair<std::string, std::string>>
            parameter_identity;
        for (const auto declaration_id : interface->declarations) {
            const auto declaration
                = specialized_interface->find_declaration(
                    declaration_id);
            if (!declaration
                || declaration->systemverilog == nullptr) {
                continue;
            }
            using Form = semantic::sv::DeclarationForm;
            const auto form = declaration->systemverilog->form;
            if (form != Form::parameter
                && form != Form::local_parameter
                && form != Form::type_parameter) {
                continue;
            }
            const auto actual = std::ranges::find(
                interface_actuals, declaration_id,
                &semantic::SpecializedHirActualIdentity::declaration);
            if (form == Form::type_parameter
                && actual != interface_actuals.end()
                && actual->systemverilog_type) {
                parameter_identity.emplace_back(
                    declaration->systemverilog->name,
                    compiled_systemverilog_type_identity(
                        *actual->systemverilog_type));
                continue;
            }
            const auto target_scalar
                = declaration->systemverilog->type
                ? compiled_systemverilog_scalar_kind(
                      declaration->systemverilog->type
                          ->target.spelling)
                : frontend::SystemVerilogScalarKind::None;
            const bool scalar_applicable
                = declaration->systemverilog->initializer
                && (target_scalar
                        != frontend::SystemVerilogScalarKind::None
                    || hir_systemverilog_scalar_expression_applicable(
                        *specialized_interface,
                        *declaration->systemverilog->initializer));
            std::string scalar_error;
            if (scalar_applicable) {
                const auto scalar
                    = evaluate_hir_systemverilog_scalar_declaration(
                        *specialized_interface, declaration_id,
                        scalar_error);
                if (!scalar) {
                    return std::nullopt;
                }
                parameter_identity.emplace_back(
                    declaration->systemverilog->name,
                    scalar->canonical());
                continue;
            }
            if (actual != interface_actuals.end()
                && (actual->identity.starts_with("svconst-v3:")
                    || actual->identity.starts_with("svscalar-v1:"))) {
                parameter_identity.emplace_back(
                    declaration->systemverilog->name,
                    actual->identity);
                continue;
            }
            const auto value
                = specialized_interface
                      ->evaluate_integral_declaration(declaration_id);
            if (!value) {
                return std::nullopt;
            }
            parameter_identity.emplace_back(
                declaration->systemverilog->name,
                compiled_systemverilog_integral_identity(
                    *declaration->systemverilog,
                    std::to_string(*value)));
        }
        return parameter_identity;
    };
    const auto matching_interface_identity_value = [](
                                                       const std::string& left,
                                                       const std::string& right) {
        if (left == right) {
            return true;
        }
        const auto left_value
            = compiled_systemverilog_identity_display(left);
        const auto right_value
            = compiled_systemverilog_identity_display(right);
        return left_value && right_value
            && *left_value == *right_value;
    };
    const auto matching_interface_identity = [&](
                                                 const auto& required,
                                                 const auto& actual) {
        return required && std::ranges::all_of(*required, [&](const auto& expected) {
            const auto found = std::ranges::find_if(
                actual,
                [&](const auto& candidate) {
                    return candidate.first == expected.first;
                });
            return found != actual.end()
                && matching_interface_identity_value(
                    found->second, expected.second);
        });
    };
    for (const auto& pending :
        pending_virtual_interface_initializers) {
        auto actual_path = pending.owner_path.empty()
            ? pending.actual_name
            : pending.owner_path + "." + pending.actual_name;
        auto source_signal = design_.signal_by_name_.find(actual_path);
        if (source_signal == design_.signal_by_name_.end()) {
            source_signal = design_.signal_by_name_.find(
                pending.actual_name);
        }
        if (source_signal != design_.signal_by_name_.end()
            && !systemverilog_interface_handles_.contains(actual_path)) {
            if (pending.source_type) {
                if (pending.source_type->interface_type
                    != pending.target_type.interface_type) {
                    report(
                        "FSIM-ELAB-SVIFACE-003",
                        "virtual interface '" + pending.owner_path
                            + "' requires type '"
                            + pending.target_type.interface_type
                            + "' but initializer '"
                            + pending.actual_name + "' has type '"
                            + pending.source_type->interface_type + "'",
                        compiled_source_span(
                            *compiled_, pending.source));
                    continue;
                }
                if (!pending.source_type->interface_modport.empty()
                    && (pending.target_type.interface_modport.empty()
                        || pending.target_type.interface_modport
                            != pending.source_type
                                ->interface_modport)) {
                    report(
                        "FSIM-ELAB-SVIFACE-012",
                        "virtual-interface initializer '"
                            + pending.actual_name + "' exposes modport '"
                            + pending.source_type->interface_modport
                            + "' and cannot be widened or rebound",
                        compiled_source_span(
                            *compiled_, pending.source));
                    continue;
                }
                const auto required_identity
                    = virtual_interface_specialization_identity(
                        pending.target_type,
                        *pending.specialization);
                const auto source_identity
                    = virtual_interface_specialization_identity(
                        *pending.source_type,
                        *pending.specialization);
                if (!source_identity
                    || !matching_interface_identity(
                        required_identity, *source_identity)) {
                    report(
                        "FSIM-ELAB-SVIFACE-010",
                        "virtual interface '" + pending.owner_path
                            + "' requires a different interface "
                              "specialization than initializer '"
                            + pending.actual_name + "'",
                        compiled_source_span(
                            *compiled_, pending.source));
                    continue;
                }
            }
            design_.signals_[pending.signal].initial_value
                = design_.signals_[source_signal->second]
                      .initial_value;
            continue;
        }
        auto handle
            = systemverilog_interface_handles_.find(actual_path);
        auto lexical_path = pending.owner_path;
        while (handle == systemverilog_interface_handles_.end()
            && lexical_path.find('.') != std::string::npos) {
            lexical_path.resize(lexical_path.rfind('.'));
            actual_path = lexical_path + "." + pending.actual_name;
            handle = systemverilog_interface_handles_.find(
                actual_path);
        }
        if (handle == systemverilog_interface_handles_.end()) {
            report(
                "FSIM-ELAB-SVIFACE-002",
                "virtual-interface initializer '" + actual_path
                    + "' must name an elaborated interface instance",
                compiled_source_span(*compiled_, pending.source));
            continue;
        }
        const auto actual_type
            = systemverilog_interface_types_.find(actual_path);
        if (actual_type != systemverilog_interface_types_.end()
            && actual_type->second
                != pending.target_type.interface_type) {
            report(
                "FSIM-ELAB-SVIFACE-003",
                "virtual interface '" + pending.owner_path
                    + "' requires type '"
                    + pending.target_type.interface_type
                    + "' but initializer '" + actual_path
                    + "' has type '" + actual_type->second + "'",
                compiled_source_span(*compiled_, pending.source));
            continue;
        }
        const auto required_identity
            = virtual_interface_specialization_identity(
                pending.target_type, *pending.specialization);
        const auto actual_identity
            = systemverilog_interface_parameter_identities_.find(
                actual_path);
        if (actual_identity
                == systemverilog_interface_parameter_identities_.end()
            || !matching_interface_identity(
                required_identity, actual_identity->second)) {
            report(
                "FSIM-ELAB-SVIFACE-010",
                "virtual interface '" + pending.owner_path
                    + "' requires a different interface specialization "
                      "than initializer '"
                    + actual_path + "'",
                compiled_source_span(*compiled_, pending.source));
            continue;
        }
        design_.signals_[pending.signal].initial_value
            = PackedLogic4::from_aval_bval(
                64U, handle->second, 0U);
    }
}

bool HierarchyBuilder::instantiate_compiled_systemverilog_instance_worklist(
    const semantic::sv::Unit& unit,
    const std::vector<CompiledSystemVerilogInstanceMaterialization>&
        instance_materializations,
    const frontend::Language source_language,
    std::size_t& concurrent_order,
    std::string (*generated_path)(
        std::string_view, std::string_view))
{
    for (const auto& materialization : instance_materializations) {
        const auto instance = materialization.instance;
        const auto& working_specialization
            = *materialization.specialization;
        const auto& working_signals = *materialization.signals;
        const auto& working_read_only_signals
            = *materialization.read_only_signals;
        const auto& working_strings
            = *materialization.string_objects;
        const auto& working_read_only_strings
            = *materialization.read_only_strings;
        const auto& working_containers
            = *materialization.container_objects;
        const auto& working_read_only_containers
            = *materialization.read_only_containers;
        const auto working_path = materialization.path;
        if (instance.systemverilog == nullptr) {
            report(
                "FSIM-ELAB-HIR-001",
                "SystemVerilog hierarchy contains a non-SystemVerilog "
                "instance record",
                compiled_source_span(*compiled_, unit.source));
            return false;
        }
        const auto& record = *instance.systemverilog;
        auto child_path = materialization.generated
                || materialization.bound
            ? generated_path(working_path, record.name)
            : compiled_instance_path(*compiled_, unit.scope,
                  record.scope, working_path, record.name);
        if (materialization.array_index) {
            child_path += "["
                + std::to_string(*materialization.array_index) + "]";
        }
        const auto* external_binding = binding_for(child_path);
        const auto dispatch
            = dispatch_compiled_systemverilog_instance(
                CompiledSystemVerilogInstanceDispatchContext {
                    .unit = unit,
                    .instance = instance,
                    .record = record,
                    .working_specialization = working_specialization,
                    .child_path = child_path,
                    .working_path = working_path,
                    .array_index = materialization.array_index,
                    .bound = materialization.bound,
                    .systemverilog_2023_bound
                    = materialization.systemverilog_2023_bound,
                    .working_signals = working_signals,
                    .working_read_only_signals
                    = working_read_only_signals,
                    .working_strings = working_strings,
                    .working_read_only_strings
                    = working_read_only_strings,
                    .working_containers = working_containers,
                    .working_read_only_containers
                    = working_read_only_containers,
                    .external_binding = external_binding,
                });
        if (dispatch.status
            == CompiledSystemVerilogInstanceDispatchResult::Status::fatal) {
            return false;
        }
        if (dispatch.status
            == CompiledSystemVerilogInstanceDispatchResult::Status::handled) {
            continue;
        }
        const auto* nested_configuration
            = dispatch.nested_configuration;
        auto child = std::move(dispatch.child);
        if (child && child->vhdl != nullptr
            && child->systemverilog == nullptr) {
            if (!instantiate_compiled_systemverilog_vhdl_child(
                    CompiledSystemVerilogVhdlChildContext {
                        .child = child,
                        .instance = instance,
                        .record = record,
                        .source_instance = dispatch.source_instance,
                        .child_path = child_path,
                        .working_path = working_path,
                        .working_specialization = working_specialization,
                        .working_signals = working_signals,
                        .external_binding = external_binding,
                    })) {
                return false;
            }
            continue;
        }
        if (!child || child->systemverilog == nullptr
            || child->vhdl != nullptr) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled instance '" + record.name
                    + "' has no linked SystemVerilog HIR target",
                compiled_source_span(*compiled_, record.source));
            return false;
        }
        auto parameter_result = resolve_compiled_systemverilog_parameters(
            *child, record, instance, child_path, working_specialization);
        for (const auto& diagnostic : parameter_result.diagnostics) {
            report(
                diagnostic.code,
                diagnostic.message,
                compiled_source_span(*compiled_, diagnostic.source));
        }
        if (parameter_result.status
            == CompiledSystemVerilogParameterResult::Status::fatal) {
            return false;
        }
        auto child_actuals = std::move(parameter_result.actuals);

        std::vector<CompiledSpecializationFailure>
            child_specialization_failures;
        auto child_interface_specialization = compiled_specialization(
            validated_compiled_, child->identity->id, child_actuals,
            &child_specialization_failures);
        for (const auto& failure : child_specialization_failures) {
            report(
                failure.code,
                failure.message,
                compiled_source_span(*compiled_, failure.source));
        }
        if (!child_interface_specialization) {
            report(
                "FSIM-ELAB-HIR-001",
                "cannot construct a SystemVerilog interface "
                "specialization for '"
                    + child_path + "'",
                compiled_source_span(*compiled_, record.source));
            return false;
        }

        auto port_bindings
            = semantic::resolve_specialized_hir_associations(
                *compiled_, child->identity->id, instance,
                semantic::SpecializedHirAssociationSurface::ports,
                &working_specialization);
        if (!port_bindings) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled port association for '" + record.name
                    + "' is unsupported: " + port_bindings.error,
                compiled_source_span(*compiled_,
                    port_bindings.error_source.valid()
                        ? port_bindings.error_source
                        : record.source));
            return false;
        }
        SignalMap child_aliases;
        StringMap child_string_aliases;
        ContainerMap child_container_aliases;
        std::vector<ProcessId> child_boundary_processes;
        bool child_ports_valid = true;
        const auto specialize_interface
            = +[](const semantic::ValidatedCompiledDesign& validated,
                   const semantic::UnitId interface_id,
                   std::vector<
                       semantic::SpecializedHirActualIdentity>& actuals) {
                  return compiled_specialization(
                      validated, interface_id, actuals, nullptr);
              };
        if (!bind_compiled_systemverilog_ports(
                CompiledSystemVerilogPortBindingContext {
                    .unit = unit,
                    .record = record,
                    .child = child,
                    .working_specialization = working_specialization,
                    .child_interface_specialization
                    = child_interface_specialization,
                    .port_bindings = port_bindings,
                    .child_path = child_path,
                    .working_path = working_path,
                    .working_signals = working_signals,
                    .working_read_only_signals
                    = working_read_only_signals,
                    .working_strings = working_strings,
                    .working_read_only_strings
                    = working_read_only_strings,
                    .working_containers = working_containers,
                    .working_read_only_containers
                    = working_read_only_containers,
                    .external_binding = external_binding,
                    .source_language = source_language,
                    .concurrent_order = concurrent_order,
                    .specialize_interface = specialize_interface,
                    .child_aliases = child_aliases,
                    .child_string_aliases = child_string_aliases,
                    .child_container_aliases
                    = child_container_aliases,
                    .child_boundary_processes
                    = child_boundary_processes,
                    .child_ports_valid = child_ports_valid,
                })) {
            return false;
        }

        if (!child_ports_valid) {
            continue;
        }
        const auto* saved_configuration
            = active_compiled_systemverilog_configuration_;
        auto saved_configuration_root
            = active_compiled_systemverilog_configuration_root_;
        if (nested_configuration != nullptr) {
            active_compiled_systemverilog_configuration_
                = nested_configuration;
            active_compiled_systemverilog_configuration_root_
                = child_path;
        }
        const auto instantiated = instantiate_compiled_systemverilog_unit(
            CompiledSystemVerilogInstantiationContext {
                .unit = *child,
                .path = child_path,
                .actuals = std::move(child_actuals),
                .port_aliases = std::move(child_aliases),
                .string_port_aliases = std::move(child_string_aliases),
                .container_port_aliases
                = std::move(child_container_aliases),
                .source_instance = dispatch.source_instance,
                .prepared_specialization
                = std::move(child_interface_specialization),
            });
        active_compiled_systemverilog_configuration_
            = saved_configuration;
        active_compiled_systemverilog_configuration_root_
            = std::move(saved_configuration_root);
        if (!instantiated) {
            return false;
        }
        if (!child_boundary_processes.empty()) {
            const auto child_specialization = std::ranges::find(
                design_.specializations_, child_path,
                &SpecializationInfo::instance);
            if (child_specialization
                == design_.specializations_.end()) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled port adapter for '" + child_path
                        + "' has no child specialization owner",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            child_specialization->processes.insert(
                child_specialization->processes.end(),
                child_boundary_processes.begin(),
                child_boundary_processes.end());
        }
        if (child->systemverilog->kind
            == semantic::sv::UnitKind::interface) {
            auto [handle, inserted]
                = systemverilog_interface_handles_.try_emplace(
                    child_path, 0U);
            if (inserted) {
                handle->second
                    = next_systemverilog_interface_handle_++;
            }
            systemverilog_interface_types_.insert_or_assign(
                child_path, child->systemverilog->name);
            const auto child_specialization = std::ranges::find(
                design_.specializations_, child_path,
                &SpecializationInfo::instance);
            if (child_specialization
                != design_.specializations_.end()) {
                systemverilog_interface_parameter_identities_
                    .insert_or_assign(child_path,
                        child_specialization
                            ->parameter_identity_values);
            }
        }
    }
    return true;
}

bool HierarchyBuilder::append_compiled_systemverilog_parameter_metadata(
    const semantic::DeclarationId declaration_id,
    const bool require_record,
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialized,
    std::unordered_set<std::string>& published_parameter_names,
    SpecializationInfo& specialization)
{
    const auto declaration
        = specialized.find_declaration(declaration_id);
    if (!declaration || declaration->systemverilog == nullptr) {
        if (require_record) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled unit declaration has no SystemVerilog "
                "specialization record",
                compiled_source_span(*compiled_, unit.source));
        }
        return !require_record;
    }
    using Form = semantic::sv::DeclarationForm;
    const auto form = declaration->systemverilog->form;
    if (form != Form::parameter && form != Form::local_parameter
        && form != Form::type_parameter) {
        return true;
    }
    if (published_parameter_names.contains(
            declaration->systemverilog->name)) {
        return true;
    }
    const auto& specialization_actuals
        = specialized.specialization().actual_identities;
    const auto actual = std::ranges::find(
        specialization_actuals, declaration_id,
        &semantic::SpecializedHirActualIdentity::declaration);
    std::optional<std::string> display;
    std::optional<std::string> identity_value;
    if (actual != specialization_actuals.end()
        && (actual->identity.starts_with("svconst-v3:")
            || actual->identity.starts_with("svscalar-v1:"))) {
        display = compiled_systemverilog_identity_display(
            actual->identity);
        identity_value = actual->identity;
    }
    if (!display) {
        const auto target_scalar = declaration->systemverilog->type
            ? compiled_systemverilog_scalar_kind(
                  declaration->systemverilog->type->target.spelling)
            : frontend::SystemVerilogScalarKind::None;
        const auto scalar_applicable
            = declaration->systemverilog->initializer
            && (target_scalar
                    != frontend::SystemVerilogScalarKind::None
                || hir_systemverilog_scalar_expression_applicable(
                    specialized,
                    *declaration->systemverilog->initializer));
        std::string scalar_error;
        if (scalar_applicable) {
            if (const auto scalar
                = evaluate_hir_systemverilog_scalar_declaration(
                    specialized, declaration_id, scalar_error)) {
                display = scalar->display();
                identity_value = scalar->canonical();
            }
        } else if (const auto value
            = specialized.evaluate_integral_declaration(
                declaration_id)) {
            display = std::to_string(*value);
        } else if (const auto string_value
            = specialized.evaluate_string_declaration(
                declaration_id)) {
            display = compiled_systemverilog_string_display(
                *string_value);
            identity_value
                = semantic::systemverilog_string_identity(
                    *string_value);
        } else if (declaration->systemverilog->initializer) {
            std::string constant_error;
            auto constant = evaluate_hir_systemverilog_constant(
                specialized,
                *declaration->systemverilog->initializer,
                constant_error);
            if (constant && declaration->systemverilog->type
                && hir_systemverilog_explicit_integral_type(
                    *declaration->systemverilog->type)) {
                constant = convert_hir_systemverilog_constant(
                    std::move(*constant),
                    *declaration->systemverilog->type,
                    constant_error, &specialized);
            }
            if (constant) {
                display = constant->display();
                identity_value = constant->canonical();
            }
        }
    }
    if (!display && actual != specialization_actuals.end()) {
        display = compiled_systemverilog_identity_display(
            actual->identity)
                      .value_or(actual->identity);
        identity_value = actual->identity;
        if (form == Form::type_parameter
            && actual->systemverilog_type) {
            identity_value = compiled_systemverilog_type_identity(
                *actual->systemverilog_type);
        }
    }
    if (!display) {
        return true;
    }
    published_parameter_names.insert(
        declaration->systemverilog->name);
    specialization.parameter_values.emplace_back(
        declaration->systemverilog->name, *display);
    specialization.parameter_identity_values.emplace_back(
        declaration->systemverilog->name,
        identity_value.value_or(
            compiled_systemverilog_integral_identity(
                *declaration->systemverilog, *display)));
    return true;
}

void HierarchyBuilder::add_compiled_vhdl_root(
    const semantic::CompiledUnitView root,
    std::string path)
{
    active_root_ = std::move(path);
    if (compiled_ != nullptr && root.vhdl != nullptr
        && root.vhdl->kind
            == semantic::vhdl::UnitKind::configuration) {
        const auto& configuration = *root.vhdl;
        if (!configuration.configuration) {
            report(
                "FSIM-ELAB-VHCONFIG-001",
                "VHDL configuration '" + configuration.name
                    + "' has no retained block configuration",
                compiled_source_span(
                    *compiled_, configuration.source));
            return;
        }
        const auto configuration_library
            = compiled_vhdl_library(configuration);
        const auto entity_matches = std::ranges::count_if(
            compiled_->vhdl_units(),
            [&](const semantic::vhdl::Unit& candidate) {
                return candidate.kind
                        == semantic::vhdl::UnitKind::entity
                    && compiled_vhdl_library_equal(
                        candidate.library, configuration_library)
                    && compiled_vhdl_name_equal(
                        candidate.name,
                        configuration.primary_name);
            });
        if (entity_matches != 1) {
            report(
                "FSIM-ELAB-VHCONFIG-002",
                "configuration '" + configuration.name
                    + "' requires one entity '"
                    + std::string { configuration_library } + "."
                    + configuration.primary_name + "'",
                compiled_source_span(
                    *compiled_, configuration.source));
            return;
        }
        std::size_t matches { };
        const auto selected = compiled_vhdl_architecture(
            *compiled_, configuration_library,
            configuration.primary_name,
            configuration.configuration->block.spelling,
            matches);
        if (matches != 1U || !selected) {
            report(
                matches == 0U
                    ? "FSIM-ELAB-VHCONFIG-003"
                    : "FSIM-ELAB-VHCONFIG-004",
                "configuration '" + configuration.name
                    + "' selects "
                    + (matches == 0U ? "missing" : "ambiguous")
                    + " architecture '"
                    + configuration.configuration->block.spelling
                    + "' of entity '" + configuration.primary_name
                    + "'",
                compiled_source_span(
                    *compiled_,
                    configuration.configuration->source));
            return;
        }
        const auto* saved_configuration
            = active_compiled_vhdl_configuration_;
        auto saved_identity
            = active_compiled_vhdl_configuration_identity_;
        auto saved_source
            = active_compiled_vhdl_configuration_source_;
        active_compiled_vhdl_configuration_ = &configuration;
        active_compiled_vhdl_configuration_identity_
            = compiled_vhdl_configuration_identity(
                *compiled_, configuration);
        active_compiled_vhdl_configuration_source_
            = configuration.source;
        static_cast<void>(instantiate_compiled_vhdl_unit(
            CompiledVhdlInstantiationContext {
                .unit = *selected,
                .path = active_root_,
                .actuals = { },
                .port_aliases = { },
                .source_instance = std::nullopt,
                .prepared_specialization = std::nullopt,
                .vhdl_types_validated = false,
            }));
        active_compiled_vhdl_configuration_ = saved_configuration;
        active_compiled_vhdl_configuration_identity_
            = std::move(saved_identity);
        active_compiled_vhdl_configuration_source_
            = saved_source;
        return;
    }
    static_cast<void>(instantiate_compiled_vhdl_unit(
        CompiledVhdlInstantiationContext {
            .unit = root,
            .path = active_root_,
            .actuals = { },
            .port_aliases = { },
            .source_instance = std::nullopt,
            .prepared_specialization = std::nullopt,
            .vhdl_types_validated = false,
        }));
}

std::optional<SignalId> HierarchyBuilder::find_compiled_vhdl_signal(
    const SignalMap& signals,
    const std::string_view name) const
{
    std::optional<SignalId> first_match;
    bool has_distinct_match = false;
    for (const auto& [signal_name, signal] : signals) {
        if (signal_name.find('.') != std::string::npos
            || !compiled_vhdl_name_equal(signal_name, name)) {
            continue;
        }
        if (!first_match) {
            first_match = signal;
        } else if (*first_match != signal) {
            has_distinct_match = true;
            break;
        }
    }
    if (!has_distinct_match) {
        return first_match;
    }

    // Preserve the legacy unordered-map winner only for observable
    // case-folded collisions between distinct signal IDs.
    const auto snapshot = signals.compatibility_snapshot();
    const auto found = std::ranges::find_if(
        snapshot,
        [&](const auto& signal) {
            return signal.first.find('.') == std::string::npos
                && compiled_vhdl_name_equal(signal.first, name);
        });
    return found == snapshot.end()
        ? std::nullopt
        : std::optional<SignalId> { found->second };
}

std::optional<HierarchyBuilder::CompiledVhdlPortActual>
HierarchyBuilder::materialize_compiled_vhdl_port_actual(
    const semantic::SpecializedHirAssociationBinding& binding,
    const semantic::CompiledExpressionView* expression,
    const semantic::vhdl::Declaration& formal_declaration,
    const semantic::SpecializedHirUnit& port_actual_specialization,
    const semantic::SpecializedHirUnit& child_interface_specialization,
    const frontend::PortDirection direction,
    const std::string& child_path,
    const std::string_view working_path,
    const semantic::vhdl::Unit& architecture,
    const std::unordered_set<std::uint32_t>& component_defaulted_port_formals,
    const SignalMap& working_signals,
    const ReadOnlySignalSet& working_read_only_signals,
    StringMap& string_objects,
    ReadOnlyStringSet& read_only_strings,
    ContainerMap& container_objects,
    ReadOnlyContainerSet& read_only_containers,
    const std::size_t specialization_id,
    std::size_t& concurrent_order)
{
    const bool accepts_input = direction == frontend::PortDirection::Input
        || direction == frontend::PortDirection::Inout;
    const bool produces_output = direction == frontend::PortDirection::Output
        || direction == frontend::PortDirection::Inout
        || direction == frontend::PortDirection::Buffer;
    if (expression && expression->vhdl != nullptr) {
        const auto& actual_expression = *expression->vhdl;
        constexpr auto qualified_prefix
            = std::string_view { "@vhdl-qualified:" };
        if (formal_declaration.subtype
            && actual_expression.kind
                == semantic::vhdl::ExpressionKind::call
            && actual_expression.text.starts_with(
                qualified_prefix)) {
            auto qualified_name = std::string_view {
                actual_expression.text
            };
            qualified_name.remove_prefix(
                qualified_prefix.size());
            semantic::vhdl::SubtypeIndication qualified_subtype;
            qualified_subtype.type_mark.spelling
                = std::string { qualified_name };
            if (actual_expression.referenced_name
                && actual_expression.referenced_name->selected) {
                const auto declaration
                    = port_actual_specialization.find_declaration(
                        *actual_expression.referenced_name
                            ->selected);
                if (declaration
                    && declaration->vhdl != nullptr) {
                    if (declaration->vhdl->subtype) {
                        qualified_subtype
                            = *declaration->vhdl->subtype;
                    } else if (declaration->vhdl
                                   ->declared_type) {
                        qualified_subtype.type_mark.target
                            = *declaration->vhdl
                                   ->declared_type;
                    }
                    qualified_subtype.type_mark.spelling
                        = std::string { qualified_name };
                }
            }
            const auto formal_name
                = compiled_vhdl_simple_name(
                    formal_declaration.subtype
                        ->type_mark.spelling);
            const auto actual_name
                = compiled_vhdl_simple_name(qualified_name);
            const auto compatible
                = (!formal_name.empty() && !actual_name.empty()
                      && compiled_vhdl_name_equal(
                          formal_name, actual_name))
                || semantic::CompiledDesignResolver {
                       port_actual_specialization
                   }
                       .vhdl_subtype_profiles_match(*formal_declaration.subtype, formal_declaration.scope, qualified_subtype, actual_expression.scope);
            if (!compatible) {
                report(
                    "FSIM-ELAB-VHPORT-001",
                    "VHDL input port '"
                        + formal_declaration.name
                        + "' has an incompatible qualified "
                          "expression actual",
                    compiled_source_span(
                        *compiled_, binding.source));
                return std::nullopt;
            }
        }
        const auto pre_2008
            = architecture.standard == "1987"
            || architecture.standard == "1993"
            || architecture.standard == "2000"
            || architecture.standard == "2002";
        const auto name_actual = [&](const auto& self,
                                     const semantic::ExpressionId id)
            -> bool {
            const auto candidate
                = port_actual_specialization.find_expression(id);
            if (!candidate || candidate->vhdl == nullptr) {
                return false;
            }
            const auto& source = *candidate->vhdl;
            if (source.kind
                == semantic::vhdl::ExpressionKind::name) {
                return true;
            }
            if ((source.kind
                        == semantic::vhdl::ExpressionKind::index
                    || source.kind
                        == semantic::vhdl::ExpressionKind::slice)
                && !source.operands.empty()) {
                return self(self, source.operands.front());
            }
            return source.kind
                == semantic::vhdl::ExpressionKind::call
                && source.text.starts_with("@vhdl-member:")
                && source.operands.size() == 1U
                && self(self, source.operands.front());
        };
        if (accepts_input && pre_2008
            && !name_actual(
                name_actual, *binding.expression)) {
            report(
                "FSIM-ELAB-VHPORT-001",
                "VHDL input port '" + formal_declaration.name
                    + "' requires a signal-name actual before "
                      "VHDL-2008",
                compiled_source_span(
                    *compiled_, binding.source));
            return std::nullopt;
        }
        if (produces_output
            && (actual_expression.kind
                    == semantic::vhdl::ExpressionKind::unary
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::binary
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::aggregate
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::concatenation
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::replication
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::conditional
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::integer_literal
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::real_literal
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::boolean_literal
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::logic_literal
                || actual_expression.kind
                    == semantic::vhdl::ExpressionKind::string_literal)) {
            report(
                "FSIM-ELAB-VHPORT-002",
                "VHDL output port '" + formal_declaration.name
                    + "' requires a writable actual",
                compiled_source_span(
                    *compiled_, binding.source));
            return std::nullopt;
        }
    }
    std::optional<SignalId> actual_signal;
    std::optional<std::string> actual_hir_type_identity;
    std::optional<PackedTypeMetadata> actual_hir_type;
    std::optional<semantic::vhdl::SubtypeIndication>
        actual_hir_subtype;
    std::optional<semantic::DeclarationId> actual_hir_declaration;
    std::optional<semantic::ScopeId> actual_hir_scope;
    std::string actual_name;
    if (expression && expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::name) {
        actual_name = expression->vhdl->text;
        auto selected_actual
            = expression->vhdl->referenced_name
            ? expression->vhdl->referenced_name->selected
            : std::nullopt;
        if (!selected_actual) {
            selected_actual
                = semantic::CompiledDesignResolver {
                      port_actual_specialization
                  }
                      .resolve_expression_name(*binding.expression)
                      .unique();
        }
        if (selected_actual) {
            actual_hir_declaration = selected_actual;
            const auto declaration
                = port_actual_specialization.find_declaration(
                    *selected_actual);
            if (declaration && declaration->vhdl != nullptr) {
                const auto member_separator
                    = expression->vhdl->text.find('.');
                actual_name = member_separator
                        == std::string::npos
                    ? declaration->vhdl->name
                    : expression->vhdl->text;
                if (declaration->vhdl->subtype) {
                    auto subtype = compiled_vhdl_link_subtype(
                        port_actual_specialization,
                        *declaration->vhdl->subtype,
                        declaration->vhdl->scope);
                    if (member_separator != std::string::npos) {
                        auto type_id = subtype.type_mark.target;
                        const semantic::vhdl::TypeDefinition*
                            definition = nullptr;
                        std::unordered_set<std::uint32_t> visiting;
                        while (type_id.valid()
                            && visiting.insert(
                                           type_id.value())
                                .second) {
                            const auto type
                                = port_actual_specialization
                                      .find_type(type_id);
                            if (!type || type->vhdl == nullptr) {
                                break;
                            }
                            if (type->vhdl->form
                                == semantic::vhdl::TypeForm::record) {
                                definition = type->vhdl;
                                break;
                            }
                            if ((type->vhdl->form
                                        != semantic::vhdl::TypeForm::subtype
                                    && type->vhdl->form
                                        != semantic::vhdl::TypeForm::alias)
                                || !type->vhdl->base.type_mark.target
                                    .valid()) {
                                break;
                            }
                            type_id
                                = type->vhdl->base.type_mark.target;
                        }
                        if (definition != nullptr) {
                            auto member_name = std::string_view {
                                expression->vhdl->text
                            }
                                                   .substr(member_separator + 1U);
                            if (const auto nested
                                = member_name.find('.');
                                nested != std::string_view::npos) {
                                member_name
                                    = member_name.substr(0U, nested);
                            }
                            const auto member = std::ranges::find_if(
                                definition->record_elements,
                                [&](const auto& element) {
                                    return compiled_vhdl_name_equal(
                                        element.name, member_name);
                                });
                            if (member
                                != definition->record_elements.end()) {
                                subtype = compiled_vhdl_link_subtype(
                                    port_actual_specialization,
                                    member->subtype,
                                    declaration->vhdl->scope);
                            }
                        }
                    }
                    actual_hir_type = compiled_vhdl_signal_type(
                        port_actual_specialization,
                        subtype,
                        declaration->vhdl->scope);
                    actual_hir_subtype = subtype;
                    actual_hir_scope = declaration->vhdl->scope;
                    actual_hir_type_identity
                        = "vhdl:"
                        + subtype.type_mark.spelling;
                    *actual_hir_type_identity += ":"
                        + std::to_string(static_cast<unsigned>(
                            subtype.domain));
                    if (subtype.type_mark.target.valid()) {
                        *actual_hir_type_identity += ":"
                            + std::to_string(
                                subtype.type_mark.target.value());
                    }
                }
            }
        }
        if (const auto actual = find_compiled_vhdl_signal(working_signals, actual_name)) {
            actual_signal = *actual;
        }
    }
    if (!actual_signal && formal_declaration.subtype) {
        const auto value = accepts_input
            ? port_actual_specialization
                  .evaluate_integral_expression(
                      *binding.expression)
            : std::nullopt;
        std::optional<std::size_t> width;
        auto domain = frontend::ValueDomain::Unknown;
        if (const auto layout
            = compiled_vhdl_named_signal_layout(
                child_interface_specialization,
                *formal_declaration.subtype,
                formal_declaration.scope)) {
            width = layout->width;
            domain = layout->domain;
        } else {
            width = compiled_vhdl_signal_width(
                *formal_declaration.subtype);
            domain = compiled_value_domain(
                formal_declaration.subtype->domain);
        }
        const auto integer_range = compiled_integer_range(
            *formal_declaration.subtype);
        const auto static_value = accepts_input && width
                && *width <= 64U
            ? compiled_vhdl_static_port_value(
                  port_actual_specialization,
                  *binding.expression,
                  *formal_declaration.subtype,
                  formal_declaration.scope,
                  domain,
                  *width)
            : std::nullopt;
        if (accepts_input && !value && !static_value
            && component_defaulted_port_formals.contains(
                binding.formal.value())) {
            report(
                "FSIM-ELAB-VHCOMP-013",
                "component input port '"
                    + formal_declaration.name
                    + "' has a non-static default expression",
                compiled_source_span(
                    *compiled_, binding.source));
            return std::nullopt;
        }
        if (width && *width != 0U
            && *width
                <= std::numeric_limits<std::uint32_t>::max()
            && compiled_vhdl_scalar_domain(domain)
            && (!value || !integer_range
                || integer_range->contains(*value))) {
            if (design_.signals_.size()
                > std::numeric_limits<SignalId>::max()) {
                report("FSIM-ELAB-011",
                    "the design has too many signals for "
                    "dense 32-bit IDs",
                    compiled_source_span(
                        *compiled_, binding.source));
                return std::nullopt;
            }
            const auto id = static_cast<SignalId>(
                design_.signals_.size());
            const auto constant_name = child_path
                + ".$actual_" + formal_declaration.name;
            SignalInfo info;
            info.id = id;
            info.name = constant_name;
            info.width = *width;
            info.type_name = formal_declaration.subtype
                                 ->type_mark.spelling;
            info.source_domain = domain;
            info.is_signed = formal_declaration.subtype
                                 ->signed_value
                || domain == frontend::ValueDomain::Integer;
            info.packed_range = compiled_packed_range(
                *formal_declaration.subtype);
            info.integer_range = integer_range;
            info.declaration_span = compiled_source_span(
                *compiled_, binding.source);
            design_.signal_info_.push_back(std::move(info));
            design_.signals_.push_back(
                runtime::simir::Signal {
                    constant_name,
                    value
                        ? compiled_vhdl_integral_value(
                              *value, domain, *width)
                        : static_value
                        ? *static_value
                        : compiled_vhdl_initial_value(
                              *formal_declaration.subtype,
                              domain, *width),
                    ResolutionKind::none,
                    value_kind(domain),
                    std::nullopt,
                    { StrengthRank::pull,
                        StrengthRank::pull },
                    std::nullopt,
                    std::nullopt,
                    frontend::SystemVerilogScalarKind::None,
                });
            design_.signal_by_name_.emplace(
                constant_name, id);
            actual_signal = id;
            Lowerer actual_lowerer {
                design_,
                working_signals,
                working_read_only_signals,
                string_objects,
                read_only_strings,
                container_objects,
                read_only_containers,
                diagnostics_,
            };
            actual_lowerer.set_specialized_hir_unit(
                &port_actual_specialization);
            const auto append_adapter = [&](
                                            std::optional<Process>
                                                lowered) {
                if (!lowered) {
                    return false;
                }
                lowered->language_standard
                    = architecture.standard;
                lowered->compatibility_profile
                    = architecture.compatibility_profile;
                canonicalize_process_operations(*lowered);
                design_.specializations_[specialization_id]
                    .processes.push_back(lowered->id);
                design_.processes_.push_back(
                    std::move(*lowered));
                return true;
            };
            if (accepts_input && !value && !static_value
                && !append_adapter(
                    actual_lowerer.lower_hir_input_actual(
                        *binding.expression,
                        id,
                        frontend::Language::Vhdl2008,
                        working_path,
                        concurrent_order++,
                        formal_declaration.subtype))) {
                actual_signal.reset();
            }
            if (actual_signal && produces_output
                && !append_adapter(
                    actual_lowerer.lower_hir_output_actual(
                        id,
                        *binding.expression,
                        frontend::Language::Vhdl2008,
                        working_path,
                        concurrent_order++))) {
                actual_signal.reset();
            }
        }
    }
    if (!actual_signal) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled VHDL port '" + formal_declaration.name
                + "' requires a compatible signal or scalar "
                  "expression actual in '"
                + std::string { working_path } + "'",
            compiled_source_span(*compiled_, binding.source));
        return std::nullopt;
    }

    return CompiledVhdlPortActual {
        *actual_signal,
        std::move(actual_hir_type_identity),
        std::move(actual_hir_type),
        std::move(actual_hir_subtype),
        std::move(actual_hir_declaration),
        std::move(actual_hir_scope),
    };
}

bool HierarchyBuilder::materialize_compiled_vhdl_declaration(
    const semantic::SpecializedHirUnit& working_specialization,
    const std::string& working_path,
    const std::string& root_path,
    SignalMap& working_signals,
    ReadOnlySignalSet& working_read_only_signals,
    std::unordered_set<std::string>& working_declared_signal_names,
    ContainerMap& container_objects,
    std::vector<std::pair<std::string, std::string>>&
        vhdl_port_shape_identities,
    const std::string_view standard,
    const semantic::vhdl::Declaration& declaration)
{
    const auto register_root_relative_signal = [&](
                                                   const std::string_view
                                                       signal_path,
                                                   const std::string_view name,
                                                   const SignalId signal) {
        if (design_.roots_.size() != 1U) {
            return;
        }
        if (signal_path == active_root_) {
            design_.signal_by_name_.emplace(name, signal);
            return;
        }
        const auto root_prefix = active_root_ + ".";
        if (signal_path.starts_with(root_prefix)) {
            design_.signal_by_name_.emplace(
                std::string { signal_path.substr(root_prefix.size()) }
                    + "." + std::string { name },
                signal);
        }
    };
    using Form = semantic::vhdl::DeclarationForm;
    if (declaration.form == Form::alias) {
        if (!declaration.alias_target) {
            return true;
        }
        auto target_name
            = declaration.alias_target->canonical.empty()
            ? declaration.alias_target->spelling
            : declaration.alias_target->canonical;
        if (declaration.alias_target->selected) {
            const auto selected
                = working_specialization.find_declaration(
                    *declaration.alias_target->selected);
            if (selected && selected->vhdl != nullptr) {
                target_name = selected->vhdl->name;
            }
        }
        const auto target = std::ranges::find_if(
            working_signals,
            [&](const auto& existing) {
                return existing.first.find('.')
                    == std::string::npos
                    && compiled_vhdl_name_equal(
                        existing.first, target_name);
            });
        if (target == working_signals.end()) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled VHDL alias '" + declaration.name
                    + "' has no materialized signal target",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        const auto full_alias
            = working_path + "." + declaration.name;
        working_signals.insert_or_assign(
            declaration.name, target->second);
        working_signals.insert_or_assign(
            full_alias, target->second);
        design_.signal_by_name_.insert_or_assign(
            full_alias, target->second);
        register_root_relative_signal(
            working_path, declaration.name, target->second);
        return true;
    }
    if (declaration.form == Form::variable
        && declaration.shared && declaration.subtype) {
        const auto object_subtype = compiled_vhdl_link_subtype(
            working_specialization, *declaration.subtype,
            declaration.scope);
        const auto object_type = object_subtype.type_mark.target.valid()
            ? working_specialization.find_type(
                  object_subtype.type_mark.target)
            : std::nullopt;
        if (object_type && object_type->vhdl != nullptr
            && object_type->vhdl->form
                == semantic::vhdl::TypeForm::protected_type) {
            if (declaration.initializer) {
                report(
                    "FSIM-ELAB-VHPROTECTED-010",
                    "a protected shared variable is constructed "
                    "from its private member defaults and cannot "
                    "have an object initializer",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return false;
            }
            const auto object_index
                = design_.vhdl_protected_object_info_.size();
            const auto object_id = static_cast<
                VhdlProtectedObjectId>(object_index);
            if (static_cast<std::size_t>(object_id)
                != object_index) {
                throw std::length_error(
                    "too many elaborated VHDL protected objects");
            }
            const auto materialized_object_type
                = compiled_vhdl_signal_type(
                    working_specialization, object_subtype,
                    declaration.scope);
            VhdlProtectedObjectInfo object;
            object.id = object_id;
            object.name = working_path + "." + declaration.name;
            object.type_name = materialized_object_type
                ? materialized_object_type->spelling
                : object_type->vhdl->name;
            object.nominal_type = materialized_object_type
                ? materialized_object_type->nominal_type
                : "vhdl-hir-type:"
                    + std::to_string(
                        object_type->vhdl->id.value())
                    + ":" + object_type->vhdl->name;
            object.declaration_span = compiled_source_span(
                *compiled_, declaration.source);

            const semantic::vhdl::TypeDefinition* protected_body { };
            for (const auto& stored :
                compiled_->vhdl_hir.types()) {
                const auto candidate
                    = working_specialization.find_type(stored.id);
                if (!candidate || candidate->vhdl == nullptr
                    || candidate->vhdl->form
                        != semantic::vhdl::TypeForm::protected_body
                    || !compiled_vhdl_name_equal(
                        candidate->vhdl->name,
                        object_type->vhdl->name)) {
                    continue;
                }
                protected_body = candidate->vhdl;
                break;
            }
            if (protected_body == nullptr) {
                report(
                    "FSIM-ELAB-VHPROTECTED-006",
                    "protected type '" + object_type->vhdl->name
                        + "' has no body",
                    compiled_source_span(
                        *compiled_, object_type->vhdl->source));
                return false;
            }
            bool profiles_valid = true;
            const semantic::CompiledDesignResolver profile_resolver {
                working_specialization
            };
            const auto validate_profiles = [&](
                                               const auto& required,
                                               const auto& provided,
                                               const semantic::vhdl::DeclarationForm form,
                                               const std::string_view code,
                                               const std::string_view description) {
                for (const auto member_id : required) {
                    const auto member
                        = working_specialization.find_declaration(
                            member_id);
                    if (!member || member->vhdl == nullptr
                        || member->vhdl->form != form) {
                        continue;
                    }
                    const auto matches = std::ranges::any_of(
                        provided,
                        [&](const semantic::DeclarationId candidate_id) {
                            const auto candidate
                                = working_specialization
                                      .find_declaration(candidate_id);
                            return candidate
                                && candidate->vhdl != nullptr
                                && candidate->vhdl->form == form
                                && compiled_vhdl_name_equal(
                                    candidate->vhdl->name,
                                    member->vhdl->name)
                                && profile_resolver
                                       .vhdl_callable_profile_matches(
                                           member_id,
                                           candidate_id, true);
                        });
                    if (matches) {
                        continue;
                    }
                    report(
                        std::string { code },
                        std::string { description } + " '"
                            + member->vhdl->name
                            + "' has no conforming profile",
                        compiled_source_span(
                            *compiled_, member->vhdl->source));
                    profiles_valid = false;
                }
            };
            const auto& public_members
                = object_type->vhdl->protected_members;
            const auto& body_members
                = protected_body->protected_members;
            validate_profiles(public_members, body_members,
                Form::function, "FSIM-ELAB-VHPROTECTED-002",
                "protected function");
            validate_profiles(body_members, public_members,
                Form::function, "FSIM-ELAB-VHPROTECTED-003",
                "protected function body");
            validate_profiles(public_members, body_members,
                Form::procedure, "FSIM-ELAB-VHPROTECTED-004",
                "protected procedure");
            validate_profiles(body_members, public_members,
                Form::procedure, "FSIM-ELAB-VHPROTECTED-005",
                "protected procedure body");
            if (!profiles_valid) {
                return false;
            }

            auto protected_members
                = object_type->vhdl->protected_members;
            const auto has_private_members = std::ranges::any_of(
                protected_members,
                [&](const semantic::DeclarationId member) {
                    const auto record
                        = working_specialization.find_declaration(
                            member);
                    return record && record->vhdl != nullptr
                        && record->vhdl->form == Form::variable;
                });
            if (!has_private_members) {
                protected_members.insert(
                    protected_members.end(),
                    protected_body->protected_members.begin(),
                    protected_body->protected_members.end());
            }

            std::size_t member_offset { };
            for (const auto member_id : protected_members) {
                const auto member
                    = working_specialization.find_declaration(
                        member_id);
                if (!member || member->vhdl == nullptr
                    || member->vhdl->form != Form::variable) {
                    continue;
                }
                const auto& member_declaration = *member->vhdl;
                if (!member_declaration.subtype) {
                    report(
                        "FSIM-ELAB-VHPROTECTED-017",
                        "protected private storage for '"
                            + object.name + "."
                            + member_declaration.name
                            + "' has no retained subtype",
                        compiled_source_span(
                            *compiled_, member_declaration.source));
                    return false;
                }
                const auto member_layout
                    = compiled_vhdl_named_signal_layout(
                        working_specialization,
                        *member_declaration.subtype,
                        member_declaration.scope);
                const auto member_type = compiled_vhdl_signal_type(
                    working_specialization,
                    *member_declaration.subtype,
                    member_declaration.scope);
                if (!member_layout || !member_type
                    || member_layout->width == 0U
                    || member_layout->width
                        > std::numeric_limits<
                            std::uint32_t>::max()
                    || member_layout->width
                        > std::numeric_limits<std::size_t>::max()
                            - member_offset) {
                    report(
                        "FSIM-ELAB-VHPROTECTED-017",
                        "protected private storage for '"
                            + object.name + "."
                            + member_declaration.name
                            + "' exceeds the executable container "
                              "representation",
                        compiled_source_span(
                            *compiled_, member_declaration.source));
                    return false;
                }
                ContainerType storage_type;
                storage_type.element_width
                    = static_cast<std::uint32_t>(
                        member_layout->width);
                storage_type.element_nominal_type
                    = member_type->nominal_type;
                storage_type.two_state = is_two_state_domain(
                    member_layout->domain);
                storage_type.signed_elements
                    = member_type->is_signed;
                storage_type.fixed = true;
                storage_type.index_left = 0;
                storage_type.index_right = 0;
                storage_type.dimensions.push_back({ 0, 0 });
                auto initial = default_container_value(storage_type);
                initial.elements.front()
                    = default_compiled_packed_value(
                        *member_type, member_layout->width);
                if (member_declaration.initializer) {
                    const auto value = working_specialization
                                           .evaluate_integral_expression(
                                               *member_declaration.initializer);
                    if (!value || member_layout->width > 64U) {
                        report(
                            "FSIM-ELAB-VHPROTECTED-011",
                            "protected private initializer for '"
                                + object.name + "."
                                + member_declaration.name
                                + "' is not a static value "
                                  "compatible with its declared "
                                  "subtype",
                            compiled_source_span(*compiled_,
                                member_declaration.source));
                        return false;
                    }
                    initial.elements.front()
                        = compiled_vhdl_integral_value(
                            *value, member_layout->domain,
                            member_layout->width);
                }
                const auto storage_index
                    = design_.container_objects_.size();
                const auto storage_id
                    = static_cast<ContainerObjectId>(
                        storage_index);
                if (static_cast<std::size_t>(storage_id)
                    != storage_index) {
                    throw std::length_error(
                        "too many elaborated container objects");
                }
                const auto member_name
                    = object.name + "."
                    + member_declaration.name;
                design_.container_object_info_.push_back(
                    ContainerObjectInfo {
                        storage_id,
                        member_name,
                        storage_type,
                        compiled_source_span(*compiled_,
                            member_declaration.source),
                        false,
                        frontend::PortDirection::Unknown,
                        std::nullopt,
                    });
                design_.container_objects_.push_back(
                    ContainerObject {
                        member_name,
                        std::move(initial),
                        std::nullopt,
                    });
                container_objects.emplace(
                    declaration.name + "."
                        + member_declaration.name,
                    storage_id);
                container_objects.emplace(member_name, storage_id);
                design_.container_by_name_.emplace(
                    member_name, storage_id);
                if (design_.roots_.size() == 1U
                    && working_path == active_root_) {
                    design_.container_by_name_.emplace(
                        declaration.name + "."
                            + member_declaration.name,
                        storage_id);
                }
                object.members.push_back(
                    VhdlProtectedMemberInfo {
                        member_declaration.name,
                        member_type->is_signed,
                        member_offset,
                        member_layout->width,
                        storage_id,
                        compiled_source_span(*compiled_,
                            member_declaration.source),
                    });
                member_offset += member_layout->width;
            }
            design_.vhdl_protected_object_info_.push_back(
                std::move(object));
            return true;
        }
    }
    const auto shared_variable
        = declaration.form == Form::variable
        && declaration.shared;
    if (shared_variable
        && standard != "1993") {
        report(
            "FSIM-ELAB-VHPROTECTED-008",
            "shared variable '" + working_path + "."
                + declaration.name
                + "' must have a protected type in VHDL-2000 and "
                  "later; only VHDL-1993 permits the legacy "
                  "unprotected form",
            compiled_source_span(*compiled_, declaration.source));
        return false;
    }
    if ((declaration.form == Form::variable
            && !shared_variable)
        || declaration.form == Form::file) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled VHDL declaration '" + declaration.name
                + "' requires an unavailable parser-free object "
                  "adapter",
            compiled_source_span(*compiled_, declaration.source));
        return false;
    }
    if (declaration.form != Form::port
        && declaration.form != Form::signal
        && !shared_variable) {
        return true;
    }
    const auto full_name
        = working_path + "." + declaration.name;
    const auto alias = std::ranges::find_if(
        working_signals,
        [&](const auto& existing) {
            return existing.first.find('.') == std::string::npos
                && compiled_vhdl_name_equal(
                    existing.first, declaration.name);
        });
    const auto* alias_info = alias != working_signals.end()
            && alias->second < design_.signal_info_.size()
        ? &design_.signal_info_[alias->second]
        : nullptr;
    std::optional<std::size_t> executable_width;
    auto domain = frontend::ValueDomain::Unknown;
    bool subtype_executable { };
    if (declaration.subtype) {
        const auto layout = compiled_vhdl_named_signal_layout(
            working_specialization, *declaration.subtype,
            declaration.scope);
        if (layout) {
            executable_width = layout->width;
            domain = layout->domain;
            subtype_executable = true;
        }
    }
    if (!subtype_executable && declaration.subtype) {
        executable_width = compiled_vhdl_signal_width(
            *declaration.subtype);
        domain = compiled_value_domain(
            declaration.subtype->domain);
        subtype_executable = executable_width
            && compiled_vhdl_scalar_domain(domain);
    }
    if (!subtype_executable && declaration.subtype
        && declaration.interface_view
        && declaration.interface_view->composition
            == semantic::vhdl::ModeViewCompositionState::complete) {
        const auto layout = compiled_vhdl_mode_view_layout(
            working_specialization, *declaration.subtype,
            *declaration.interface_view);
        if (layout) {
            executable_width = layout->width;
            domain = layout->domain;
            subtype_executable = true;
        }
    }
    if (!subtype_executable && alias_info != nullptr
        && declaration.form == Form::port) {
        executable_width = alias_info->width;
        domain = alias_info->source_domain;
    }
    const auto inferred_port_subtype
        = alias_info != nullptr
        && declaration.form == Form::port
        && declaration.subtype
        && declaration.subtype->unspecified_class
            != semantic::vhdl::UnspecifiedTypeClass::none;
    if (inferred_port_subtype) {
        executable_width = alias_info->width;
        domain = alias_info->source_domain;
        subtype_executable = true;
    }
    const auto unsupported_subtype
        = (!shared_variable
              && declaration.object_class
                  != semantic::vhdl::ObjectClass::signal)
        || !executable_width
        || !compiled_vhdl_scalar_domain(domain)
        || (!declaration.subtype && alias_info == nullptr);
    if (unsupported_subtype
        || (alias_info == nullptr && !subtype_executable)) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled VHDL declaration '" + declaration.name
                + "' requires an unavailable parser-free signal "
                  "adapter",
            compiled_source_span(*compiled_, declaration.source));
        return false;
    }
    if (std::ranges::any_of(
            working_declared_signal_names,
            [&](const std::string_view existing) {
                return compiled_vhdl_name_equal(
                    existing, declaration.name);
            })) {
        report(
            "FSIM-ELAB-HIR-001",
            "duplicate compiled VHDL signal declaration '"
                + declaration.name + "'",
            compiled_source_span(*compiled_, declaration.source));
        return false;
    }
    working_declared_signal_names.insert(declaration.name);
    const auto width = *executable_width;
    const auto direction = compiled_port_direction(
        declaration.direction);
    if (alias != working_signals.end()) {
        const auto compiled_boundary
            = std::ranges::find_if(
                design_.boundary_conversions_,
                [&](const auto& conversion) {
                    return conversion.path == full_name
                        && conversion.formal_signal
                        == alias->second;
                });
        const bool state_domain_alias_provenance
            = alias_info != nullptr && !inferred_port_subtype
            && alias_info->source_domain != domain
            && std::ranges::any_of(
                design_.boundary_conversions_,
                [&](const BoundaryConversionInfo& conversion) {
                    return conversion.kind
                        == BoundaryConversionKind::state_domain_alias
                        && conversion.state_domain_changed
                        && !conversion.process
                        && conversion.formal_signal == alias->second
                        && conversion.actual_signal == alias->second
                        && conversion.formal_domain == domain
                        && conversion.actual_domain
                        == alias_info->source_domain;
                });
        const auto layout_mismatch = alias_info != nullptr
            && !inferred_port_subtype
            && (alias_info->width != width
                || (alias_info->source_domain != domain
                    && !state_domain_alias_provenance));
        if (alias_info == nullptr
            || (layout_mismatch
                && compiled_boundary
                    == design_.boundary_conversions_.end())) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled VHDL port association for '" + full_name
                    + "' has an incompatible signal subtype",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        working_signals.emplace(full_name, alias->second);
        design_.signal_by_name_.emplace(full_name, alias->second);
        register_root_relative_signal(
            working_path, declaration.name, alias->second);
        if (direction == frontend::PortDirection::Input) {
            working_read_only_signals.insert(alias->second);
        }
        if (working_path == root_path && declaration.form == Form::port
            && alias_info != nullptr
            && (alias_info->vhdl_array
                || !alias_info->packed_members.empty())) {
            vhdl_port_shape_identities.emplace_back(
                "__vhdl_port_shape."
                    + compiled_vhdl_shape_name(declaration.name),
                compiled_vhdl_port_shape_identity(*alias_info));
        }
        return true;
    }
    if (design_.signals_.size()
        > std::numeric_limits<SignalId>::max()) {
        report(
            "FSIM-ELAB-011",
            "the design has too many signals for dense 32-bit IDs",
            compiled_source_span(*compiled_, declaration.source));
        return false;
    }
    const auto id = static_cast<SignalId>(design_.signals_.size());
    working_signals.insert_or_assign(declaration.name, id);
    working_signals.emplace(full_name, id);
    design_.signal_by_name_.emplace(full_name, id);
    register_root_relative_signal(
        working_path, declaration.name, id);
    SignalInfo info;
    info.id = id;
    info.name = full_name;
    info.width = width;
    info.type_name = declaration.subtype->type_mark.spelling;
    info.source_domain = domain;
    info.is_signed = declaration.subtype->signed_value
        || domain == frontend::ValueDomain::Integer;
    info.packed_range = compiled_packed_range(
        *declaration.subtype);
    info.integer_range = compiled_integer_range(
        *declaration.subtype);
    const auto materialized_type = compiled_vhdl_signal_type(
        working_specialization, *declaration.subtype,
        declaration.scope);
    if (materialized_type) {
        const auto& type = *materialized_type;
        info.type_name = type.spelling;
        info.source_domain = type.domain;
        info.is_signed = type.is_signed;
        info.packed_range = type.packed_range;
        info.vhdl_array = type.vhdl_array;
        info.vhdl_access = type.vhdl_access;
        info.vhdl_physical = type.vhdl_physical;
        info.packed_members = type.packed_members;
        info.integer_range = type.integer_range;
        info.nominal_type = type.nominal_type;
        info.enumeration_literals = type.enumeration_literals;
        info.enumeration_range = type.enumeration_range;
        if (declaration.subtype->type_mark.target.valid()) {
            const auto definition = working_specialization.find_type(
                declaration.subtype->type_mark.target);
            const auto type_declaration = definition
                    && definition->vhdl != nullptr
                ? working_specialization.find_declaration(
                      definition->vhdl->declaration)
                : std::nullopt;
            const auto* type_scope = type_declaration
                    && type_declaration->vhdl != nullptr
                ? compiled_semantic_scope(
                      *compiled_, type_declaration->vhdl->scope)
                : nullptr;
            const auto generated_scope = type_scope != nullptr
                && !type_scope->name.empty()
                && working_path.find(
                       type_scope->name + "[")
                    != std::string_view::npos;
            if (generated_scope) {
                const auto source = definition->vhdl->source;
                const auto& spans = compiled_->semantics.source_spans();
                const auto logical_name = source.valid()
                        && source.value() < spans.size()
                    ? std::string_view {
                          spans[source.value()].logical_name
                      }
                    : std::string_view { };
                info.nominal_type = std::string { logical_name }
                    + ":" + definition->vhdl->name + ":"
                    + std::string { working_path };
            }
        }
    }
    if (!info.integer_range) {
        info.integer_range
            = compiled_vhdl_specialization_integer_constraint(
                working_specialization, *declaration.subtype);
    }
    info.is_port = declaration.form == Form::port;
    info.direction = direction;
    info.declaration_span = compiled_source_span(
        *compiled_, declaration.source);
    auto initial = compiled_vhdl_initial_value(
        *declaration.subtype, domain, width);
    if (materialized_type
        && (!materialized_type->packed_members.empty()
            || materialized_type->enumeration_range
            || materialized_type->vhdl_access
            || materialized_type->vhdl_physical
            || materialized_type->domain
                == frontend::ValueDomain::Logic9
            || materialized_type->domain
                == frontend::ValueDomain::Integer)) {
        initial = default_compiled_packed_value(
            *materialized_type, width);
    }
    if (declaration.initializer) {
        const auto value = working_specialization
                               .evaluate_integral_expression(
                                   *declaration.initializer);
        auto static_value = compiled_vhdl_static_port_value(
            working_specialization,
            *declaration.initializer,
            *declaration.subtype,
            declaration.scope,
            domain,
            width);
        if (!static_value && materialized_type
            && materialized_type->vhdl_array) {
            static_value
                = compiled_vhdl_static_packed_array_initializer(
                    working_specialization,
                    *declaration.initializer,
                    *declaration.subtype,
                    declaration.scope,
                    *materialized_type,
                    width);
        }
        const auto integer_range = info.integer_range;
        if ((!value && !static_value)
            || (value && width > 64U)
            || (value && integer_range
                && !integer_range->contains(*value))) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled VHDL initializer for '"
                    + declaration.name
                    + "' is not a bounded integral value",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        initial = static_value
            ? *static_value
            : compiled_vhdl_integral_value(
                  *value, domain, width);
    }
    const auto runtime_domain = info.source_domain;
    design_.signal_info_.push_back(std::move(info));
    if (const auto resolution = compiled_vhdl_resolution_function(
            working_specialization, *declaration.subtype,
            declaration.scope)) {
        const auto binding = compiled_vhdl_resolution_binding(
            working_specialization, *declaration.subtype,
            *resolution, declaration.scope);
        if (const auto diagnostic
            = register_compiled_vhdl_resolution(
                id, *declaration.subtype, declaration.source,
                binding)) {
            report(
                diagnostic->code,
                diagnostic->message,
                compiled_source_span(
                    *compiled_, diagnostic->source));
        }
    }
    design_.signals_.push_back(runtime::simir::Signal {
        full_name,
        std::move(initial),
        ResolutionKind::none,
        value_kind(runtime_domain),
        std::nullopt,
        { StrengthRank::pull, StrengthRank::pull },
        std::nullopt,
        std::nullopt,
        frontend::SystemVerilogScalarKind::None,
    });
    if (direction == frontend::PortDirection::Input) {
        working_read_only_signals.insert(id);
    }
    return true;
}

bool HierarchyBuilder::validate_compiled_vhdl_context_visibility(
    const semantic::vhdl::Unit* entity,
    const semantic::vhdl::Unit& architecture,
    const std::optional<semantic::SpecializedHirUnit>& specialized)
{
    bool valid = true;
    std::unordered_map<std::uint32_t, std::uint8_t> package_state;
    std::unordered_set<std::uint32_t> validated_contexts;
    std::vector<semantic::UnitId> context_stack;
    const semantic::CompiledDesignResolver visibility_resolver {
        *specialized
    };

    const auto validate_static_conversions
        = [&](const auto& self,
              const semantic::ExpressionId expression_id) -> bool {
        const auto expression
            = specialized->find_expression(expression_id);
        if (!expression || expression->vhdl == nullptr) {
            return true;
        }
        const auto& source = *expression->vhdl;
        bool result { true };
        if (source.kind == semantic::vhdl::ExpressionKind::call
            && source.operands.size() == 1U
            && source.referenced_name) {
            const auto type_declaration
                = [](const semantic::CompiledDeclarationView& view) {
                      if (view.vhdl == nullptr) {
                          return false;
                      }
                      using Form
                          = semantic::vhdl::DeclarationForm;
                      return view.vhdl->form == Form::type
                          || view.vhdl->form == Form::subtype;
                  };
            const auto target = visibility_resolver
                                    .resolve_expression_name(
                                        expression_id,
                                        type_declaration)
                                    .unique();
            const auto declaration = target
                ? specialized->find_declaration(*target)
                : std::nullopt;
            const auto value
                = specialized->evaluate_integral_expression(
                    source.operands.front());
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->subtype && value
                && !compiled_vhdl_specialization_value_satisfies_subtype(
                    *specialized,
                    *declaration->vhdl->subtype,
                    *value)) {
                report(
                    "FSIM-ELAB-VHSTATIC-002",
                    "locally static VHDL conversion result is "
                    "outside its target scalar subtype",
                    compiled_source_span(
                        *compiled_, source.source));
                result = false;
            }
        }
        for (const auto operand : source.operands) {
            result = self(self, operand) && result;
        }
        for (const auto& association : source.associations) {
            result = self(self, association.value) && result;
            for (const auto choice : association.choices) {
                result = self(self, choice) && result;
            }
        }
        return result;
    };

    const auto find_unit = [&](const semantic::vhdl::UnitKind kind,
                               const std::string_view library,
                               const std::string_view name)
        -> const semantic::vhdl::Unit* {
        const semantic::vhdl::Unit* result = nullptr;
        for (const auto& candidate : compiled_->vhdl_units()) {
            if (candidate.kind != kind
                || !candidate.primary_name.empty()
                || !compiled_vhdl_library_equal(
                    candidate.library, library)
                || !compiled_vhdl_name_equal(
                    candidate.name, name)) {
                continue;
            }
            if (result != nullptr) {
                return nullptr;
            }
            result = &candidate;
        }
        return result;
    };
    const auto package_member = [&](
                                    const semantic::vhdl::Unit& unit,
                                    const std::string_view name) {
        return std::ranges::any_of(unit.declarations,
                   [&](const semantic::DeclarationId declaration_id) {
                       const auto declaration
                           = specialized->find_declaration(declaration_id);
                       return declaration && declaration->vhdl != nullptr
                           && compiled_vhdl_name_equal(
                               declaration->vhdl->name, name);
                   })
            || std::ranges::any_of(
                unit.standard_package_declarations,
                [&](const std::string& declaration) {
                    return compiled_vhdl_name_equal(
                        declaration, name);
                });
    };
    const auto integer_constraint = [&](
                                        const auto& self,
                                        const semantic::vhdl::
                                            SubtypeIndication& input,
                                        std::unordered_set<
                                            std::uint32_t>& visiting)
        -> std::optional<frontend::IntegerRange> {
        if (const auto range = compiled_integer_range(input)) {
            return range;
        }
        const auto spelling
            = std::string_view { input.type_mark.spelling };
        const auto parts = compiled_vhdl_name_parts(spelling);
        const auto name = parts.empty()
            ? spelling
            : parts.back();
        if (compiled_vhdl_name_equal(name, "natural")) {
            return frontend::IntegerRange {
                0, std::numeric_limits<std::int64_t>::max(), false
            };
        }
        if (compiled_vhdl_name_equal(name, "positive")) {
            return frontend::IntegerRange {
                1, std::numeric_limits<std::int64_t>::max(), false
            };
        }
        if (!input.type_mark.target.valid()
            || !visiting.insert(
                            input.type_mark.target.value())
                .second) {
            return std::nullopt;
        }
        const auto definition = specialized->find_type(
            input.type_mark.target);
        if (!definition || definition->vhdl == nullptr) {
            return std::nullopt;
        }
        return self(self, definition->vhdl->base, visiting);
    };
    // A valid locally-static value may remain residual when it
    // crosses a VHDL callable or type-conversion boundary. Follow
    // referenced constants so that this evaluator limitation is not
    // diagnosed as an unresolved declaration-order dependency.
    const auto requires_residual_callable_evaluation = [&](const semantic::ExpressionId initializer) -> bool {
        std::unordered_set<std::uint32_t> visited_expressions;
        std::unordered_set<std::uint32_t> visited_declarations;
        std::function<bool(semantic::DeclarationId)>
            visit_declaration;
        std::function<bool(semantic::ExpressionId)>
            visit_expression;
        const auto callable_or_conversion = [&](const semantic::DeclarationId id) {
            const auto declaration
                = specialized->find_declaration(id);
            if (!declaration || declaration->vhdl == nullptr) {
                return false;
            }
            using Form = semantic::vhdl::DeclarationForm;
            const auto form = declaration->vhdl->form;
            return (declaration->vhdl->callable
                       && declaration->vhdl->callable->function)
                || form == Form::type
                || form == Form::subtype;
        };
        visit_declaration = [&](const semantic::DeclarationId id) {
            if (!visited_declarations.insert(id.value()).second) {
                return false;
            }
            const auto declaration
                = specialized->find_declaration(id);
            return declaration && declaration->vhdl != nullptr
                && declaration->vhdl->initializer
                && visit_expression(
                    *declaration->vhdl->initializer);
        };
        visit_expression = [&](const semantic::ExpressionId id) {
            if (!visited_expressions.insert(id.value()).second) {
                return false;
            }
            const auto expression
                = specialized->find_expression(id);
            if (!expression || expression->vhdl == nullptr) {
                return false;
            }
            using Kind = semantic::vhdl::ExpressionKind;
            const auto& record = *expression->vhdl;
            const auto callable_expression
                = record.kind == Kind::call
                || record.kind == Kind::unary
                || record.kind == Kind::binary;
            if (callable_expression && record.referenced_name) {
                if (record.referenced_name->selected
                    && callable_or_conversion(
                        *record.referenced_name->selected)) {
                    return true;
                }
                if (std::ranges::any_of(
                        record.referenced_name->overloads,
                        callable_or_conversion)) {
                    return true;
                }
            }
            if (std::ranges::any_of(
                    record.operands, visit_expression)) {
                return true;
            }
            if (record.kind != Kind::name
                || !record.referenced_name) {
                return false;
            }
            if (record.referenced_name->selected
                && visit_declaration(
                    *record.referenced_name->selected)) {
                return true;
            }
            return std::ranges::any_of(
                record.referenced_name->overloads,
                visit_declaration);
        };
        return visit_expression(initializer);
    };

    std::function<void(const semantic::vhdl::Unit&)> validate_items;
    std::function<void(const semantic::vhdl::Unit&,
        semantic::SourceSpanId)>
        validate_package;
    validate_package = [&](const semantic::vhdl::Unit& package,
                           const semantic::SourceSpanId source) {
        auto& state = package_state[package.id.value()];
        if (state == 1U) {
            report(
                "FSIM-ELAB-PKG-007",
                "cyclic VHDL package visibility includes '"
                    + package.library + "." + package.name + "'",
                compiled_source_span(*compiled_, source));
            valid = false;
            return;
        }
        if (state == 2U) {
            return;
        }
        state = 1U;
        validate_items(package);
        for (const auto declaration_id : package.declarations) {
            const auto declaration
                = specialized->find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::constant
                || !declaration->vhdl->initializer
                || !declaration->vhdl->subtype) {
                continue;
            }
            if (!validate_static_conversions(
                    validate_static_conversions,
                    *declaration->vhdl->initializer)) {
                valid = false;
            }
            std::unordered_set<std::uint32_t> visiting;
            const auto range = integer_constraint(
                integer_constraint,
                *declaration->vhdl->subtype,
                visiting);
            const auto type = compiled_vhdl_signal_type(
                *specialized,
                *declaration->vhdl->subtype,
                declaration->vhdl->scope);
            const auto enumeration_range = type
                    && !type->enumeration_literals.empty()
                ? type->enumeration_range
                : std::nullopt;
            if (!range && !enumeration_range) {
                continue;
            }
            const auto value
                = specialized->evaluate_integral_declaration(
                    declaration_id);
            if (!value) {
                if (requires_residual_callable_evaluation(
                        *declaration->vhdl->initializer)) {
                    continue;
                }
                report(
                    "FSIM-ELAB-PKG-005",
                    "VHDL package constant '"
                        + declaration->vhdl->name
                        + "' cannot be evaluated in declaration "
                          "order",
                    compiled_source_span(
                        *compiled_, declaration->vhdl->source));
                valid = false;
                continue;
            }
            const auto lower = range
                ? std::min(range->left, range->right)
                : std::min(
                      enumeration_range->left,
                      enumeration_range->right);
            const auto upper = range
                ? std::max(range->left, range->right)
                : std::max(
                      enumeration_range->left,
                      enumeration_range->right);
            if (*value < lower || *value > upper) {
                report(
                    "FSIM-ELAB-PKG-006",
                    "VHDL package constant '"
                        + declaration->vhdl->name
                        + "' violates its scalar subtype",
                    compiled_source_span(
                        *compiled_, declaration->vhdl->source));
                valid = false;
            }
        }
        state = 2U;
    };
    validate_items = [&](const semantic::vhdl::Unit& owner) {
        std::unordered_map<std::string, std::string>
            visible_constants;
        std::unordered_map<std::string, std::string>
            visible_types;
        const auto owner_library = owner.library.empty()
            ? std::string_view { "work" }
            : std::string_view { owner.library };
        for (const auto& item : owner.context) {
            for (const auto& selected : item.selected_names) {
                const auto spelling = selected.canonical.empty()
                    ? std::string_view { selected.spelling }
                    : std::string_view { selected.canonical };
                const auto parts = compiled_vhdl_name_parts(spelling);
                if (item.kind
                    == semantic::vhdl::ContextKind::use_clause) {
                    if (parts.size() != 3U) {
                        report(
                            "FSIM-ELAB-PKG-001",
                            "bounded package imports require "
                            "library.package.all or "
                            "library.package.constant",
                            compiled_source_span(
                                *compiled_, item.source));
                        valid = false;
                        continue;
                    }
                    const auto requested_library
                        = compiled_vhdl_effective_library(
                            parts[0], owner_library);
                    const auto* package = find_unit(
                        semantic::vhdl::UnitKind::package,
                        requested_library,
                        parts[1]);
                    if (package == nullptr) {
                        if (!compiled_vhdl_name_equal(
                                parts[0], "ieee")
                            && !compiled_vhdl_name_equal(
                                parts[0], "std")) {
                            report(
                                "FSIM-ELAB-PKG-002",
                                "VHDL package '"
                                    + std::string { parts[0] } + "."
                                    + std::string { parts[1] }
                                    + "' was not found",
                                compiled_source_span(
                                    *compiled_, item.source));
                            valid = false;
                        }
                        continue;
                    }
                    const auto import_all
                        = compiled_vhdl_name_equal(parts[2], "all");
                    if (!import_all
                        && !package_member(*package, parts[2])) {
                        report(
                            "FSIM-ELAB-PKG-003",
                            "VHDL package '"
                                + std::string { parts[0] } + "."
                                + std::string { parts[1] }
                                + "' has no exported item '"
                                + std::string { parts[2] } + "'",
                            compiled_source_span(
                                *compiled_, item.source));
                        valid = false;
                    }
                    if (import_all) {
                        const auto package_owner
                            = std::string { requested_library }
                            + "." + package->name;
                        for (const auto declaration_id :
                            package->declarations) {
                            const auto declaration
                                = specialized->find_declaration(
                                    declaration_id);
                            if (!declaration
                                || declaration->vhdl == nullptr) {
                                continue;
                            }
                            using Form
                                = semantic::vhdl::DeclarationForm;
                            const auto form
                                = declaration->vhdl->form;
                            if (form == Form::constant) {
                                auto [known, inserted]
                                    = visible_constants.emplace(
                                        declaration->vhdl->name,
                                        package_owner);
                                if (!inserted
                                    && known->second
                                        != package_owner) {
                                    report(
                                        "FSIM-ELAB-PKG-004",
                                        "VHDL package constant '"
                                            + declaration->vhdl->name
                                            + "' is directly visible "
                                              "from multiple packages",
                                        compiled_source_span(
                                            *compiled_, item.source));
                                    valid = false;
                                }
                            }
                            if (form == Form::type
                                || form == Form::subtype) {
                                auto [known, inserted]
                                    = visible_types.emplace(
                                        declaration->vhdl->name,
                                        package_owner);
                                if (!inserted
                                    && known->second
                                        != package_owner) {
                                    report(
                                        "FSIM-ELAB-VHTYPE-003",
                                        "VHDL type '"
                                            + declaration->vhdl->name
                                            + "' is directly visible "
                                              "from multiple packages",
                                        compiled_source_span(
                                            *compiled_, item.source));
                                    valid = false;
                                }
                            }
                        }
                    }
                    validate_package(*package, item.source);
                    continue;
                }
                if (item.kind
                    != semantic::vhdl::ContextKind::
                        context_reference) {
                    continue;
                }
                if (parts.size() != 2U) {
                    report(
                        "FSIM-ELAB-CTX-001",
                        "bounded context references require "
                        "library.context",
                        compiled_source_span(
                            *compiled_, item.source));
                    valid = false;
                    continue;
                }
                const auto requested_library
                    = compiled_vhdl_effective_library(
                        parts[0], owner_library);
                const auto* context = find_unit(
                    semantic::vhdl::UnitKind::context,
                    requested_library,
                    parts[1]);
                if (context == nullptr) {
                    if (!compiled_vhdl_name_equal(parts[0], "ieee")
                        && !compiled_vhdl_name_equal(
                            parts[0], "std")) {
                        report(
                            "FSIM-ELAB-CTX-002",
                            "VHDL context '"
                                + std::string { parts[0] } + "."
                                + std::string { parts[1] }
                                + "' was not found",
                            compiled_source_span(
                                *compiled_, item.source));
                        valid = false;
                    }
                    continue;
                }
                if (std::ranges::find(
                        context_stack, context->id)
                    != context_stack.end()) {
                    report(
                        "FSIM-ELAB-CTX-003",
                        "cyclic VHDL context visibility includes '"
                            + context->library + "."
                            + context->name + "'",
                        compiled_source_span(
                            *compiled_, item.source));
                    valid = false;
                    continue;
                }
                if (validated_contexts.insert(
                                          context->id.value())
                        .second) {
                    context_stack.push_back(context->id);
                    validate_items(*context);
                    context_stack.pop_back();
                }
            }
        }
    };

    validate_items(*entity);
    validate_items(architecture);
    return valid;
}

bool HierarchyBuilder::validate_compiled_vhdl_access_type_declarations(
    const semantic::vhdl::Unit* entity,
    const semantic::vhdl::Unit& architecture,
    const std::optional<semantic::SpecializedHirUnit>& specialized)
{
    enum class AccessTypeIssue : std::uint8_t {
        none,
        missing_designated_subtype,
        recursive_designated_subtype,
        protected_designated_subtype,
    };
    const auto type_scope = [&](
                                const semantic::vhdl::TypeDefinition& type)
        -> std::optional<semantic::ScopeId> {
        const auto declaration = specialized->find_declaration(
            type.declaration);
        return declaration && declaration->vhdl != nullptr
            ? std::optional { declaration->vhdl->scope }
            : std::nullopt;
    };
    const auto scope_belongs_to_selected_unit = [&](
                                                    const semantic::ScopeId
                                                        scope) {
        const auto* record = compiled_semantic_scope(*compiled_, scope);
        return record != nullptr
            && (record->unit == architecture.id
                || record->unit == entity->id);
    };
    const auto builtin_designated_subtype = [](
                                                const semantic::vhdl::
                                                    SubtypeIndication& subtype) {
        if (subtype.domain
            != semantic::vhdl::ValueDomain::unknown) {
            return true;
        }
        const auto separator
            = subtype.type_mark.spelling.find_last_of(".:");
        const auto spelling = std::string_view {
            subtype.type_mark.spelling
        };
        const auto name = spelling.substr(
            separator == std::string::npos
                ? 0U
                : separator + 1U);
        return compiled_vhdl_name_equal(name, "bit")
            || compiled_vhdl_name_equal(name, "boolean")
            || compiled_vhdl_name_equal(name, "character")
            || compiled_vhdl_name_equal(name, "integer")
            || compiled_vhdl_name_equal(name, "natural")
            || compiled_vhdl_name_equal(name, "positive")
            || compiled_vhdl_name_equal(name, "real")
            || compiled_vhdl_name_equal(name, "time")
            || compiled_vhdl_name_equal(name, "std_logic")
            || compiled_vhdl_name_equal(name, "std_ulogic")
            || compiled_vhdl_name_equal(name, "bit_vector")
            || compiled_vhdl_name_equal(name, "boolean_vector")
            || compiled_vhdl_name_equal(name, "integer_vector")
            || compiled_vhdl_name_equal(name, "real_vector")
            || compiled_vhdl_name_equal(name, "time_vector")
            || compiled_vhdl_name_equal(name, "string")
            || compiled_vhdl_name_equal(name, "std_logic_vector")
            || compiled_vhdl_name_equal(name, "std_ulogic_vector")
            || compiled_vhdl_name_equal(name, "signed")
            || compiled_vhdl_name_equal(name, "unsigned");
    };
    const auto referenced_type = [&](
                                     const semantic::vhdl::
                                         SubtypeIndication& subtype,
                                     const std::optional<
                                         semantic::ScopeId>
                                         scope)
        -> const semantic::vhdl::TypeDefinition* {
        if (subtype.type_mark.target.valid()) {
            const auto type = specialized->find_type(
                subtype.type_mark.target);
            return type && type->vhdl != nullptr
                ? type->vhdl
                : nullptr;
        }
        const auto resolved = semantic::CompiledDesignResolver {
            *specialized
        }
                                  .resolve_vhdl_named_type(subtype.type_mark.spelling, scope);
        if (!resolved) {
            return nullptr;
        }
        const auto type = specialized->find_type(*resolved);
        return type && type->vhdl != nullptr
            ? type->vhdl
            : nullptr;
    };
    std::function<AccessTypeIssue(
        const semantic::vhdl::SubtypeIndication&,
        std::optional<semantic::ScopeId>,
        std::unordered_set<std::uint32_t>&)>
        inspect_designated_subtype;
    inspect_designated_subtype = [&](
                                     const semantic::vhdl::
                                         SubtypeIndication& subtype,
                                     const std::optional<
                                         semantic::ScopeId>
                                         scope,
                                     std::unordered_set<std::uint32_t>&
                                         visiting) {
        const auto* type = referenced_type(subtype, scope);
        if (type == nullptr) {
            return builtin_designated_subtype(subtype)
                ? AccessTypeIssue::none
                : AccessTypeIssue::missing_designated_subtype;
        }
        if (!visiting.insert(type->id.value()).second) {
            return AccessTypeIssue::recursive_designated_subtype;
        }
        AccessTypeIssue issue { AccessTypeIssue::none };
        using Form = semantic::vhdl::TypeForm;
        if (type->form == Form::access) {
            issue = type->designated_subtype
                ? inspect_designated_subtype(
                      *type->designated_subtype,
                      type_scope(*type), visiting)
                : AccessTypeIssue::missing_designated_subtype;
        } else if (type->form == Form::subtype
            || type->form == Form::alias) {
            issue = inspect_designated_subtype(
                type->base, type_scope(*type), visiting);
        } else if (type->form == Form::protected_type
            || type->form == Form::protected_body) {
            issue = AccessTypeIssue::protected_designated_subtype;
        } else if (type->form == Form::unresolved) {
            issue = AccessTypeIssue::missing_designated_subtype;
        }
        visiting.erase(type->id.value());
        return issue;
    };

    std::unordered_set<std::uint32_t> locally_referenced_types;
    for (const auto& declaration : specialized->vhdl_declarations()) {
        if (!declaration.subtype
            || !scope_belongs_to_selected_unit(declaration.scope)
            || !declaration.subtype->type_mark.target.valid()) {
            continue;
        }
        locally_referenced_types.insert(
            declaration.subtype->type_mark.target.value());
    }

    bool valid = true;
    for (const auto& stored : compiled_->vhdl_hir.types()) {
        const auto effective = specialized->find_type(stored.id);
        if (!effective || effective->vhdl == nullptr) {
            continue;
        }
        const auto& type = *effective->vhdl;
        if (type.form != semantic::vhdl::TypeForm::access) {
            continue;
        }
        const auto scope = type_scope(type);
        if ((!scope || !scope_belongs_to_selected_unit(*scope))
            && !locally_referenced_types.contains(type.id.value())) {
            continue;
        }
        const auto source = type.designated_subtype
                && type.designated_subtype->type_mark.source.valid()
            ? type.designated_subtype->type_mark.source
            : type.source;
        if (!type.designated_subtype) {
            report(
                "FSIM-ELAB-VHACCESS-002",
                "a VHDL access declaration requires exactly one "
                "designated subtype",
                compiled_source_span(*compiled_, source));
            valid = false;
            continue;
        }
        std::unordered_set<std::uint32_t> visiting {
            type.id.value(),
        };
        switch (inspect_designated_subtype(
            *type.designated_subtype, scope, visiting)) {
        case AccessTypeIssue::none:
            break;
        case AccessTypeIssue::missing_designated_subtype:
            report(
                "FSIM-ELAB-VHACCESS-002",
                "a VHDL access declaration requires exactly one "
                "resolved designated subtype",
                compiled_source_span(*compiled_, source));
            valid = false;
            break;
        case AccessTypeIssue::recursive_designated_subtype:
            report(
                "FSIM-ELAB-VHACCESS-001",
                "cyclic VHDL access designated subtype involving '"
                    + type.name + "'",
                compiled_source_span(*compiled_, source));
            valid = false;
            break;
        case AccessTypeIssue::protected_designated_subtype:
            report(
                "FSIM-ELAB-VHACCESS-003",
                "a bounded VHDL access type cannot designate a "
                "protected type",
                compiled_source_span(*compiled_, source));
            valid = false;
            break;
        }
    }
    return valid;
}

bool HierarchyBuilder::validate_compiled_vhdl_object_composite_types(
    const semantic::vhdl::Unit* entity,
    const semantic::vhdl::Unit& architecture,
    const std::optional<semantic::SpecializedHirUnit>& specialized)
{
    enum CompositeIssue : unsigned {
        no_composite_issue = 0U,
        unresolved_composite_type = 1U,
        unconstrained_composite_member = 2U,
        recursive_composite_type = 4U,
        unresolved_object_type = 8U,
        unresolved_selected_type = 16U,
    };
    const auto builtin_type = [](const std::string_view spelling) {
        const auto separator = spelling.find_last_of(".:");
        const auto name = spelling.substr(
            separator == std::string_view::npos
                ? 0U
                : separator + 1U);
        return compiled_vhdl_name_equal(name, "bit")
            || compiled_vhdl_name_equal(name, "boolean")
            || compiled_vhdl_name_equal(name, "character")
            || compiled_vhdl_name_equal(name, "integer")
            || compiled_vhdl_name_equal(name, "natural")
            || compiled_vhdl_name_equal(name, "positive")
            || compiled_vhdl_name_equal(name, "std_logic")
            || compiled_vhdl_name_equal(name, "std_ulogic")
            || compiled_vhdl_name_equal(name, "bit_vector")
            || compiled_vhdl_name_equal(name, "boolean_vector")
            || compiled_vhdl_name_equal(name, "string")
            || compiled_vhdl_name_equal(name, "std_logic_vector")
            || compiled_vhdl_name_equal(name, "std_ulogic_vector")
            || compiled_vhdl_name_equal(name, "signed")
            || compiled_vhdl_name_equal(name, "unsigned");
    };
    const auto resolve_type = [&](
                                  const semantic::vhdl::SubtypeIndication& subtype)
        -> std::optional<semantic::TypeId> {
        if (subtype.type_mark.target.valid()) {
            return subtype.type_mark.target;
        }
        if (subtype.type_mark.spelling.empty()) {
            return std::nullopt;
        }
        std::optional<semantic::TypeId> result;
        for (const auto& candidate : specialized->vhdl_types()) {
            if (!compiled_vhdl_name_equal(
                    candidate.name, subtype.type_mark.spelling)) {
                continue;
            }
            if (result && *result != candidate.id) {
                return std::nullopt;
            }
            result = candidate.id;
        }
        return result;
    };
    const auto bounded = [](const semantic::vhdl::RangeConstraint& range) {
        return (range.left || range.left_expression)
            && (range.right || range.right_expression);
    };
    std::function<unsigned(
        const semantic::vhdl::SubtypeIndication&,
        std::unordered_set<std::uint32_t>&,
        bool)>
        inspect;
    inspect = [&](const semantic::vhdl::SubtypeIndication& subtype,
                  std::unordered_set<std::uint32_t>& visiting,
                  const bool object_type)
        -> unsigned {
        const auto type_id = resolve_type(subtype);
        if (!type_id) {
            const auto selected_type = object_type
                && compiled_vhdl_name_parts(
                       subtype.type_mark.spelling)
                        .size()
                    > 1U;
            return subtype.domain
                        != semantic::vhdl::ValueDomain::unknown
                    || builtin_type(subtype.type_mark.spelling)
                ? no_composite_issue
                : selected_type
                ? unresolved_selected_type
                : object_type
                ? unresolved_object_type
                : unresolved_composite_type;
        }
        if (!visiting.insert(type_id->value()).second) {
            return static_cast<unsigned>(recursive_composite_type);
        }
        const auto type = specialized->find_type(*type_id);
        if (!type || type->vhdl == nullptr) {
            visiting.erase(type_id->value());
            return static_cast<unsigned>(unresolved_composite_type);
        }
        const auto& definition = *type->vhdl;
        auto issues = static_cast<unsigned>(no_composite_issue);
        if (definition.form == semantic::vhdl::TypeForm::record) {
            if (definition.record_elements.empty()) {
                issues |= unresolved_composite_type;
            }
            for (const auto& element : definition.record_elements) {
                issues |= inspect(element.subtype, visiting, false);
            }
        } else if (definition.form
            == semantic::vhdl::TypeForm::array) {
            for (std::size_t index { };
                index < definition.array_dimensions.size(); ++index) {
                const auto* constraint = index < subtype.constraints.size()
                    ? &subtype.constraints[index]
                    : definition.array_dimensions[index].constraint
                    ? &*definition.array_dimensions[index].constraint
                    : nullptr;
                if (constraint == nullptr || !bounded(*constraint)) {
                    issues |= unconstrained_composite_member;
                }
            }
            if (definition.element_subtype) {
                issues |= inspect(
                    *definition.element_subtype, visiting, false);
            } else {
                issues |= unresolved_composite_type;
            }
        } else if (definition.form
                == semantic::vhdl::TypeForm::subtype
            || definition.form == semantic::vhdl::TypeForm::alias) {
            auto base = definition.base;
            if (!subtype.constraints.empty()) {
                base.constraints = subtype.constraints;
            }
            issues |= inspect(base, visiting, object_type);
        } else if (definition.form
            == semantic::vhdl::TypeForm::unresolved) {
            const auto selected_type = object_type
                && compiled_vhdl_name_parts(
                       subtype.type_mark.spelling)
                        .size()
                    > 1U;
            issues |= selected_type
                ? unresolved_selected_type
                : object_type
                ? unresolved_object_type
                : unresolved_composite_type;
        }
        visiting.erase(type_id->value());
        return static_cast<unsigned>(issues);
    };

    bool valid = true;
    std::unordered_set<std::uint32_t> checked_declarations;
    const auto validate_unit = [&](const semantic::vhdl::Unit& unit) {
        for (const auto declaration_id : unit.declarations) {
            if (!checked_declarations.insert(
                                         declaration_id.value())
                    .second) {
                continue;
            }
            const auto declaration = specialized->find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->subtype) {
                continue;
            }
            using Form = semantic::vhdl::DeclarationForm;
            const auto form = declaration->vhdl->form;
            if (form != Form::port && form != Form::signal
                && form != Form::constant && form != Form::variable) {
                continue;
            }
            std::unordered_set<std::uint32_t> visiting;
            const auto linked_subtype = compiled_vhdl_link_subtype(
                *specialized, *declaration->vhdl->subtype,
                declaration->vhdl->scope);
            const auto issues = inspect(
                linked_subtype, visiting, true);
            const auto span = compiled_source_span(
                *compiled_, declaration->vhdl->source);
            if ((issues & unresolved_object_type) != 0U) {
                report(
                    "FSIM-ELAB-VHTYPE-001",
                    "VHDL object '" + declaration->vhdl->name
                        + "' has an unresolved type",
                    span);
                valid = false;
            }
            if ((issues & unresolved_selected_type) != 0U) {
                report(
                    "FSIM-ELAB-VHTYPE-004",
                    "selected VHDL type name '"
                        + declaration->vhdl->subtype
                            ->type_mark.spelling
                        + "' does not denote an exported type",
                    span);
                valid = false;
            }
            if ((issues & recursive_composite_type) != 0U) {
                report(
                    "FSIM-ELAB-VHTYPE-001",
                    "VHDL object '" + declaration->vhdl->name
                        + "' has a recursively contained composite type",
                    span);
                valid = false;
            }
            if ((issues & unresolved_composite_type) != 0U) {
                report(
                    "FSIM-ELAB-VHTYPE-002",
                    "VHDL object '" + declaration->vhdl->name
                        + "' has an unresolved composite member type",
                    span);
                valid = false;
            }
            if ((issues & unconstrained_composite_member) != 0U) {
                // Interface arrays and records may be unconstrained; the
                // occurrence's associated actual supplies their concrete
                // bounds.  Other objects still require a fully concrete
                // executable layout here.
                if (form != Form::port) {
                    report(
                        "FSIM-ELAB-VHRECORD-001",
                        "VHDL object '" + declaration->vhdl->name
                            + "' contains an unconstrained composite member",
                        span);
                    valid = false;
                }
            }
        }
    };
    validate_unit(*entity);
    validate_unit(architecture);
    return valid;
}

HierarchyBuilder::CompiledVhdlGenericActualResult
HierarchyBuilder::materialize_compiled_vhdl_generic_actuals(
    const semantic::vhdl::Instance& record,
    const semantic::SpecializedHirAssociationResult& generic_bindings,
    const semantic::SpecializedHirUnit& working_specialization)
{
    std::vector<semantic::SpecializedHirActualIdentity>
        child_actuals;
    bool child_actuals_valid { true };
    semantic::CompiledBindingFrame association_frame;
    association_frame.reserve(generic_bindings.bindings.size());
    for (const auto& binding : generic_bindings.bindings) {
        association_frame.push_back({ binding.formal,
            binding.expression, binding.actual_declaration,
            binding.systemverilog_type, binding.vhdl_type });
    }
    const std::array association_frames { association_frame };
    const semantic::CompiledDesignResolver association_resolver {
        working_specialization, association_frames
    };
    for (const auto& binding : generic_bindings.bindings) {
        const auto formal = compiled_->find_declaration(
            binding.formal);
        if (formal && formal->vhdl != nullptr
            && formal->vhdl->form
                == semantic::vhdl::DeclarationForm::generic_package) {
            const auto expression = binding.expression
                ? working_specialization.find_expression(
                      *binding.expression)
                : std::nullopt;
            if (binding.kind
                    != semantic::SpecializedHirAssociationKind::expression
                || !expression || expression->vhdl == nullptr
                || expression->vhdl->kind
                    != semantic::vhdl::ExpressionKind::name) {
                report(
                    "FSIM-ELAB-VHPKG-003",
                    "actual for interface package generic '"
                        + formal->vhdl->name
                        + "' must be a visible package instance name",
                    compiled_source_span(*compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
            const auto& source = *expression->vhdl;
            const auto actual_name = source.referenced_name
                ? source.referenced_name->canonical.empty()
                    ? std::string_view {
                          source.referenced_name->spelling
                      }
                    : std::string_view { source.referenced_name->canonical }
                : std::string_view { source.text };
            const auto separator = actual_name.find_last_of(".:");
            const auto simple_name = actual_name.substr(
                separator == std::string_view::npos
                    ? 0U
                    : separator + 1U);
            const auto actual_declaration = binding.actual_declaration
                ? working_specialization.find_declaration(
                      *binding.actual_declaration)
                : std::nullopt;
            if (actual_declaration
                && (actual_declaration->vhdl == nullptr
                    || (actual_declaration->vhdl->form
                            != semantic::vhdl::DeclarationForm::
                                package_instance
                        && actual_declaration->vhdl->form
                            != semantic::vhdl::DeclarationForm::
                                generic_package))) {
                report(
                    "FSIM-ELAB-VHPKG-006",
                    "generic actual '" + std::string { actual_name }
                        + "' is not a package instance",
                    compiled_source_span(*compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
            const bool generic_template = std::ranges::any_of(
                compiled_->vhdl_units(),
                [&](const semantic::vhdl::Unit& unit) {
                    if (unit.kind
                            != semantic::vhdl::UnitKind::package
                        || !unit.primary_name.empty()
                        || !compiled_vhdl_name_equal(
                            unit.name, simple_name)) {
                        return false;
                    }
                    return std::ranges::any_of(
                        unit.declarations,
                        [&](const semantic::DeclarationId id) {
                            const auto declaration
                                = compiled_->find_declaration(id);
                            if (!declaration
                                || declaration->vhdl == nullptr) {
                                return false;
                            }
                            using Form
                                = semantic::vhdl::DeclarationForm;
                            const auto form
                                = declaration->vhdl->form;
                            return form == Form::generic_constant
                                || form == Form::generic_type
                                || form == Form::generic_function
                                || form == Form::generic_procedure
                                || form == Form::generic_package;
                        });
                });
            if (!binding.actual_declaration
                && separator != std::string_view::npos) {
                report(
                    "FSIM-ELAB-VHPKG-011",
                    "generated or scoped package actual '"
                        + std::string { actual_name }
                        + "' is outside the bounded interface-package "
                          "subset",
                    compiled_source_span(*compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
            if (!binding.actual_declaration && generic_template) {
                report(
                    "FSIM-ELAB-VHPKG-014",
                    "generic package template '"
                        + std::string { actual_name }
                        + "' must be instantiated before it is passed "
                          "as an interface-package actual",
                    compiled_source_span(*compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
            if (!binding.actual_declaration
                && !generic_template
                && separator == std::string_view::npos) {
                report(
                    "FSIM-ELAB-VHPKG-005",
                    "package instance actual '"
                        + std::string { actual_name }
                        + "' is not directly visible",
                    compiled_source_span(*compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
            if (binding.actual_declaration && actual_declaration
                && actual_declaration->vhdl != nullptr
                && formal->vhdl->package
                && actual_declaration->vhdl->package
                && !association_resolver
                    .vhdl_package_templates_match(
                        binding.formal,
                        *binding.actual_declaration)) {
                report(
                    "FSIM-ELAB-VHPKG-007",
                    "VHDL generic package actual for '"
                        + formal->vhdl->name
                        + "' was instantiated from a different template",
                    compiled_source_span(*compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
            if (binding.actual_declaration && actual_declaration
                && actual_declaration->vhdl != nullptr
                && formal->vhdl->package
                && actual_declaration->vhdl->package
                && !association_resolver
                    .vhdl_package_generic_maps_match(
                        binding.formal,
                        *binding.actual_declaration)) {
                report(
                    "FSIM-ELAB-VHPKG-008",
                    "package instance actual '"
                        + std::string { actual_name }
                        + "' does not conform to the interface package "
                          "generic map for '"
                        + formal->vhdl->name + "'",
                    compiled_source_span(*compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
        }
        if (binding.kind
                == semantic::SpecializedHirAssociationKind::open
            || (binding.kind
                    == semantic::SpecializedHirAssociationKind::default_value
                && !binding.actual_declaration)) {
            continue;
        }
        if (binding.identity.empty()) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled VHDL generic association for '"
                    + record.name
                    + "' has no deterministic identity",
                compiled_source_span(*compiled_, binding.source));
            return {
                CompiledVhdlGenericActualResult::Status::fatal,
                { },
            };
        }
        auto actual = compiled_vhdl_specialization_actual(
            *compiled_, working_specialization, binding);
        if (actual.requires_static_value) {
            report(
                "FSIM-ELAB-GENERIC-004",
                "compiled VHDL generic association for '"
                    + record.name
                    + "' is not a locally static value",
                compiled_source_span(
                    *compiled_, binding.source));
            child_actuals_valid = false;
            continue;
        }
        if (formal && formal->vhdl != nullptr
            && formal->vhdl->form
                == semantic::vhdl::DeclarationForm::generic_constant
            && formal->vhdl->subtype && binding.expression) {
            const auto layout = compiled_vhdl_named_signal_layout(
                working_specialization,
                *formal->vhdl->subtype,
                formal->vhdl->scope);
            const auto value = working_specialization
                                   .evaluate_integral_expression(
                                       *binding.expression);
            const auto static_value = layout && layout->width != 0U
                ? compiled_vhdl_static_port_value(
                      working_specialization,
                      *binding.expression,
                      *formal->vhdl->subtype,
                      formal->vhdl->scope,
                      layout->domain,
                      layout->width)
                : std::nullopt;
            if (value
                && !compiled_vhdl_specialization_value_satisfies_subtype(
                    working_specialization,
                    *formal->vhdl->subtype,
                    *value)) {
                report(
                    "FSIM-ELAB-GENERIC-008",
                    "compiled VHDL generic association for '"
                        + record.name
                        + "' violates the formal scalar subtype",
                    compiled_source_span(
                        *compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
            if (layout && compiled_vhdl_scalar_domain(layout->domain)
                && !value && !static_value
                && !actual.actual.vhdl_packed_value) {
                report(
                    "FSIM-ELAB-GENERIC-004",
                    "compiled VHDL generic association for '"
                        + record.name
                        + "' is not a locally static packed value",
                    compiled_source_span(
                        *compiled_, binding.source));
                child_actuals_valid = false;
                continue;
            }
            if (layout && value
                && !actual.actual.vhdl_packed_value
                && layout->domain
                    != frontend::ValueDomain::Integer
                && layout->width < 64U) {
                const bool signed_value
                    = formal->vhdl->subtype->signed_value;
                const auto minimum = signed_value && layout->width != 0U
                    ? -(std::int64_t { 1 }
                          << (layout->width - 1U))
                    : std::int64_t { 0 };
                const auto maximum = signed_value
                        && layout->width != 0U
                    ? (std::int64_t { 1 }
                          << (layout->width - 1U))
                        - 1
                    : static_cast<std::int64_t>(
                          (std::uint64_t { 1 } << layout->width) - 1U);
                if (*value < minimum || *value > maximum) {
                    report(
                        "FSIM-ELAB-GENERIC-008",
                        "compiled VHDL generic association for '"
                            + record.name
                            + "' does not fit the formal packed subtype",
                        compiled_source_span(
                            *compiled_, binding.source));
                    child_actuals_valid = false;
                    continue;
                }
            }
        }
        child_actuals.push_back(std::move(actual.actual));
    }
    if (!child_actuals_valid) {
        return {
            CompiledVhdlGenericActualResult::Status::invalid,
            std::move(child_actuals),
        };
    }
    return {
        CompiledVhdlGenericActualResult::Status::ready,
        std::move(child_actuals),
    };
}

bool HierarchyBuilder::validate_compiled_vhdl_physical_type_units(
    const semantic::vhdl::Unit* entity,
    const semantic::vhdl::Unit& architecture,
    const std::optional<semantic::SpecializedHirUnit>& specialized)
{
    const auto physical_name = [](std::string value) {
        std::ranges::transform(
            value, value.begin(), [](const char character) {
                return static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            });
        return value;
    };
    bool valid = true;
    for (const auto& stored : compiled_->vhdl_hir.types()) {
        const auto effective = specialized->find_type(stored.id);
        if (!effective || effective->vhdl == nullptr
            || effective->vhdl->form
                != semantic::vhdl::TypeForm::physical) {
            continue;
        }
        const auto& type = *effective->vhdl;
        const auto declaration = specialized->find_declaration(
            type.declaration);
        if (!declaration || declaration->vhdl == nullptr) {
            continue;
        }
        const auto* scope = compiled_semantic_scope(
            *compiled_, declaration->vhdl->scope);
        if (scope == nullptr
            || (scope->unit != architecture.id
                && scope->unit != entity->id)) {
            continue;
        }
        if (!type.scalar_range || type.physical_units.empty()) {
            report(
                "FSIM-ELAB-VHPHYSICAL-001",
                "a VHDL physical type requires a range and a primary "
                "unit",
                compiled_source_span(*compiled_, type.source));
            valid = false;
            continue;
        }
        if (!type.scalar_range->left || !type.scalar_range->right
            || *type.scalar_range->left
                < std::numeric_limits<std::int32_t>::min()
            || *type.scalar_range->left
                > std::numeric_limits<std::int32_t>::max()
            || *type.scalar_range->right
                < std::numeric_limits<std::int32_t>::min()
            || *type.scalar_range->right
                > std::numeric_limits<std::int32_t>::max()) {
            report(
                "FSIM-ELAB-VHPHYSICAL-002",
                "a bounded physical range must be locally static and "
                "fit signed 32-bit primary-unit ticks",
                compiled_source_span(
                    *compiled_, type.scalar_range->source));
            valid = false;
            continue;
        }
        std::unordered_map<std::string, std::int64_t> scales;
        for (std::size_t index { };
            index < type.physical_units.size(); ++index) {
            const auto& unit = type.physical_units[index];
            const auto name = physical_name(unit.name);
            if (index == 0U) {
                if (unit.scale) {
                    report(
                        "FSIM-ELAB-VHPHYSICAL-003",
                        "the primary physical unit cannot have a "
                        "secondary-unit scale",
                        compiled_source_span(*compiled_, unit.source));
                    valid = false;
                    break;
                }
                scales.emplace(name, 1);
                continue;
            }
            auto scale = unit.scale_factor;
            if (!scale && unit.scale) {
                const auto expression = specialized->find_expression(
                    *unit.scale);
                constexpr std::string_view prefix {
                    "@vhdl-physical:"
                };
                if (expression && expression->vhdl != nullptr
                    && expression->vhdl->text.starts_with(prefix)
                    && expression->vhdl->operands.size() == 1U) {
                    const auto referenced = physical_name(std::string {
                        std::string_view { expression->vhdl->text }
                            .substr(prefix.size()) });
                    const auto prior = scales.find(referenced);
                    const auto multiplier
                        = specialized->evaluate_integral_expression(
                            expression->vhdl->operands.front());
                    if (prior != scales.end() && multiplier
                        && *multiplier > 0
                        && prior->second
                            <= std::numeric_limits<std::int32_t>::max()
                                / *multiplier) {
                        scale = prior->second * *multiplier;
                    }
                }
            }
            if (!scale || *scale <= 0
                || *scale
                    > std::numeric_limits<std::int32_t>::max()) {
                report(
                    "FSIM-ELAB-VHPHYSICAL-004",
                    "secondary unit '" + unit.name
                        + "' has an invalid, forward, or overflowing "
                          "scale",
                    compiled_source_span(*compiled_, unit.source));
                valid = false;
                break;
            }
            scales.insert_or_assign(name, *scale);
        }
    }
    return valid;
}

bool HierarchyBuilder::validate_compiled_vhdl_subtype_declarations(
    const semantic::vhdl::Unit* entity,
    const semantic::vhdl::Unit& architecture,
    const std::optional<semantic::SpecializedHirUnit>& specialized)
{
    enum class SubtypeClass : std::uint8_t {
        unknown,
        scalar,
        integer,
        enumeration,
        array,
    };
    struct SubtypeProfile {
        SubtypeClass type_class { SubtypeClass::unknown };
        std::optional<frontend::IntegerRange> scalar_range;
        bool constrained_array { };
        bool cyclic { };
        std::optional<semantic::TypeId> enumeration_type;
    };
    const auto simple_name = [](const std::string_view spelling) {
        const auto separator = spelling.find_last_of(".:");
        return spelling.substr(
            separator == std::string_view::npos
                ? 0U
                : separator + 1U);
    };
    const auto builtin_profile = [&](const std::string_view spelling) {
        const auto name = simple_name(spelling);
        if (compiled_vhdl_name_equal(name, "integer")
            || compiled_vhdl_name_equal(name, "natural")
            || compiled_vhdl_name_equal(name, "positive")
            || compiled_vhdl_name_equal(name, "time")) {
            auto result = SubtypeProfile {
                SubtypeClass::integer,
                std::nullopt,
                false,
                false,
                std::nullopt,
            };
            if (compiled_vhdl_name_equal(name, "natural")) {
                result.scalar_range = frontend::IntegerRange {
                    0, std::numeric_limits<std::int64_t>::max(), false
                };
            } else if (compiled_vhdl_name_equal(name, "positive")) {
                result.scalar_range = frontend::IntegerRange {
                    1, std::numeric_limits<std::int64_t>::max(), false
                };
            }
            return result;
        }
        const bool array
            = compiled_vhdl_name_equal(name, "bit_vector")
            || compiled_vhdl_name_equal(name, "boolean_vector")
            || compiled_vhdl_name_equal(name, "string")
            || compiled_vhdl_name_equal(name, "std_logic_vector")
            || compiled_vhdl_name_equal(name, "std_ulogic_vector")
            || compiled_vhdl_name_equal(name, "signed")
            || compiled_vhdl_name_equal(name, "unsigned")
            || compiled_vhdl_name_equal(name, "ufixed")
            || compiled_vhdl_name_equal(name, "sfixed")
            || compiled_vhdl_name_equal(name, "unresolved_ufixed")
            || compiled_vhdl_name_equal(name, "unresolved_sfixed")
            || compiled_vhdl_name_equal(name, "float")
            || compiled_vhdl_name_equal(name, "unresolved_float")
            || compiled_vhdl_name_equal(name, "u_float");
        if (array) {
            return SubtypeProfile {
                SubtypeClass::array,
                std::nullopt,
                false,
                false,
                std::nullopt,
            };
        }
        const bool scalar
            = compiled_vhdl_name_equal(name, "bit")
            || compiled_vhdl_name_equal(name, "boolean")
            || compiled_vhdl_name_equal(name, "character")
            || compiled_vhdl_name_equal(name, "std_logic")
            || compiled_vhdl_name_equal(name, "std_ulogic")
            || compiled_vhdl_name_equal(name, "real")
            || compiled_vhdl_name_equal(name, "line");
        if (compiled_vhdl_name_equal(name, "severity_level")
            || compiled_vhdl_name_equal(name, "file_open_kind")
            || compiled_vhdl_name_equal(name, "file_open_status")
            || compiled_vhdl_name_equal(name, "side")) {
            const auto right = compiled_vhdl_name_equal(
                                   name, "file_open_kind")
                ? 2
                : compiled_vhdl_name_equal(name, "side")
                ? 1
                : 3;
            return SubtypeProfile {
                SubtypeClass::enumeration,
                frontend::IntegerRange { 0, right, false },
                false,
                false,
                std::nullopt,
            };
        }
        return SubtypeProfile {
            scalar ? SubtypeClass::scalar : SubtypeClass::unknown,
            std::nullopt,
            false,
            false,
            std::nullopt,
        };
    };
    const auto effective_type = [&](const semantic::TypeId id)
        -> const semantic::vhdl::TypeDefinition* {
        const auto type = specialized->find_type(id);
        return type && type->vhdl != nullptr
            ? type->vhdl
            : nullptr;
    };
    const auto type_scope = [&](
                                const semantic::vhdl::TypeDefinition&
                                    type)
        -> std::optional<semantic::ScopeId> {
        const auto declaration = specialized->find_declaration(
            type.declaration);
        return declaration && declaration->vhdl != nullptr
            ? std::optional { declaration->vhdl->scope }
            : std::nullopt;
    };
    const auto locally_named_type = [&](const std::string_view spelling,
                                        const std::optional<semantic::ScopeId> scope)
        -> const semantic::vhdl::TypeDefinition* {
        if (!scope || spelling.find_last_of(".:") != std::string_view::npos) {
            return nullptr;
        }
        const auto* requested_scope = compiled_semantic_scope(
            *compiled_, *scope);
        const auto* requested_unit = requested_scope != nullptr
            ? compiled_vhdl_unit(*compiled_, requested_scope->unit)
            : nullptr;
        const semantic::vhdl::TypeDefinition* result = nullptr;
        for (const auto& compiled_type :
            compiled_->vhdl_hir.types()) {
            const auto* candidate = effective_type(
                compiled_type.id);
            if (candidate == nullptr
                || !compiled_vhdl_name_equal(
                    candidate->name, spelling)) {
                continue;
            }
            const auto candidate_scope_id = type_scope(*candidate);
            const auto* candidate_scope = candidate_scope_id
                ? compiled_semantic_scope(
                      *compiled_, *candidate_scope_id)
                : nullptr;
            const auto* candidate_unit = candidate_scope != nullptr
                ? compiled_vhdl_unit(
                      *compiled_, candidate_scope->unit)
                : nullptr;
            const auto directly_visible = candidate_scope_id == scope;
            const auto entity_visible_from_architecture
                = requested_unit != nullptr
                && candidate_unit != nullptr
                && requested_unit->kind
                    == semantic::vhdl::UnitKind::architecture
                && candidate_unit->kind
                    == semantic::vhdl::UnitKind::entity
                && compiled_vhdl_library_equal(
                    requested_unit->library,
                    candidate_unit->library)
                && compiled_vhdl_name_equal(
                    requested_unit->primary_name,
                    candidate_unit->name);
            if (!directly_visible
                && !entity_visible_from_architecture) {
                continue;
            }
            if (result != nullptr
                && result->id != candidate->id) {
                return nullptr;
            }
            result = candidate;
        }
        return result;
    };
    const auto apply_constraints = [&](
                                       SubtypeProfile& profile,
                                       const auto& constraints) {
        for (const auto& constraint : constraints) {
            const auto left = constraint.left
                ? constraint.left
                : constraint.left_expression
                ? specialized->evaluate_integral_expression(
                      *constraint.left_expression)
                : std::nullopt;
            const auto right = constraint.right
                ? constraint.right
                : constraint.right_expression
                ? specialized->evaluate_integral_expression(
                      *constraint.right_expression)
                : std::nullopt;
            if ((constraint.kind
                        == semantic::vhdl::RangeKind::integer
                    || constraint.kind
                        == semantic::vhdl::RangeKind::discrete)
                && profile.type_class == SubtypeClass::integer
                && left && right) {
                profile.scalar_range = frontend::IntegerRange {
                    *left,
                    *right,
                    constraint.descending,
                };
            } else if ((constraint.kind
                               == semantic::vhdl::RangeKind::enumeration
                           || constraint.kind
                               == semantic::vhdl::RangeKind::discrete)
                && profile.type_class == SubtypeClass::enumeration
                && left && right) {
                profile.scalar_range = frontend::IntegerRange {
                    *left,
                    *right,
                    constraint.descending,
                };
            } else if (constraint.kind
                    == semantic::vhdl::RangeKind::array_index
                && profile.type_class == SubtypeClass::array) {
                profile.constrained_array = true;
            }
        }
    };
    std::function<SubtypeProfile(
        const semantic::vhdl::SubtypeIndication&,
        std::optional<semantic::ScopeId>,
        std::unordered_set<std::uint32_t>&)>
        resolve_profile;
    resolve_profile = [&](const semantic::vhdl::SubtypeIndication& subtype,
                          const std::optional<semantic::ScopeId> scope,
                          std::unordered_set<std::uint32_t>& visiting) {
        const auto linked_subtype = compiled_vhdl_link_subtype(
            *specialized, subtype, scope);
        auto profile = builtin_profile(
            linked_subtype.type_mark.spelling);
        if (profile.type_class == SubtypeClass::unknown
            && scope
            && semantic::CompiledDesignResolver {
                *compiled_, architecture.id, &*specialized }
                .vhdl_builtin_type_visible(linked_subtype.type_mark.spelling, *scope)) {
            const auto name = simple_name(
                linked_subtype.type_mark.spelling);
            const auto array
                = compiled_vhdl_name_equal(name, "directory_items")
                || compiled_vhdl_name_equal(name, "directory")
                || compiled_vhdl_name_equal(
                    name, "call_path_vector")
                || compiled_vhdl_name_equal(
                    name, "call_path_vector_ptr");
            const auto enumeration
                = compiled_vhdl_name_equal(name, "dayofweek")
                || name.ends_with("_status")
                || compiled_vhdl_name_equal(name, "type_class")
                || compiled_vhdl_name_equal(name, "value_class");
            profile.type_class = array ? SubtypeClass::array
                : enumeration          ? SubtypeClass::enumeration
                                       : SubtypeClass::scalar;
        }
        const auto* type = linked_subtype.type_mark.target.valid()
            ? effective_type(linked_subtype.type_mark.target)
            : locally_named_type(
                  linked_subtype.type_mark.spelling, scope);
        if (type != nullptr) {
            if (!visiting.insert(type->id.value()).second) {
                profile.cyclic = true;
                return profile;
            }
            const auto& definition = *type;
            using Form = semantic::vhdl::TypeForm;
            if (definition.form == Form::subtype
                || definition.form == Form::alias) {
                profile = resolve_profile(
                    definition.base,
                    type_scope(definition),
                    visiting);
            } else if (definition.form == Form::array) {
                profile.type_class = SubtypeClass::array;
                profile.constrained_array
                    = !definition.array_dimensions.empty()
                    && std::ranges::all_of(
                        definition.array_dimensions,
                        [](const auto& dimension) {
                            return !dimension.unconstrained
                                || dimension.constraint.has_value();
                        });
            } else if (definition.form
                == Form::enumeration) {
                profile.type_class = SubtypeClass::enumeration;
                profile.enumeration_type = definition.id;
                if (definition.scalar_range
                    && definition.scalar_range->left
                    && definition.scalar_range->right) {
                    profile.scalar_range
                        = frontend::IntegerRange {
                              *definition.scalar_range->left,
                              *definition.scalar_range->right,
                              definition.scalar_range->descending,
                          };
                } else if (!definition.enumeration_literals.empty()) {
                    profile.scalar_range = frontend::IntegerRange {
                        0,
                        static_cast<std::int64_t>(
                            definition.enumeration_literals.size() - 1U),
                        false,
                    };
                }
            } else if (definition.form == Form::physical) {
                profile.type_class = SubtypeClass::integer;
            } else if (definition.form == Form::scalar) {
                profile.type_class = definition.base.domain
                        == semantic::vhdl::ValueDomain::integer
                    ? SubtypeClass::integer
                    : SubtypeClass::scalar;
            } else if (definition.form == Form::unresolved) {
                profile.type_class = SubtypeClass::unknown;
            } else {
                profile.type_class = SubtypeClass::scalar;
            }
            visiting.erase(type->id.value());
        } else if (linked_subtype.type_mark.target.valid()) {
            profile = { };
        }
        apply_constraints(profile, linked_subtype.constraints);
        return profile;
    };
    std::unordered_set<std::uint32_t> relevant_declarations;
    const auto collect_relevant_declarations
        = [&](const semantic::vhdl::Unit& unit) {
              for (const auto declaration : unit.declarations) {
                  relevant_declarations.insert(declaration.value());
              }
          };
    collect_relevant_declarations(*entity);
    collect_relevant_declarations(architecture);
    const auto relevant_type = [&](
                                   const semantic::vhdl::TypeDefinition&
                                       type) {
        if (relevant_declarations.contains(
                type.declaration.value())) {
            return true;
        }
        const auto declaration = compiled_->find_declaration(
            type.declaration);
        if (!declaration || declaration->vhdl == nullptr) {
            return false;
        }
        const auto* scope = compiled_semantic_scope(
            *compiled_, declaration->vhdl->scope);
        if (scope == nullptr) {
            return false;
        }
        if (scope->unit == architecture.id
            || scope->unit == entity->id) {
            return true;
        }
        const auto* owner = compiled_vhdl_unit(
            *compiled_, scope->unit);
        return owner != nullptr
            && owner->kind == semantic::vhdl::UnitKind::package
            && semantic::CompiledDesignResolver {
                   *compiled_, architecture.id, &*specialized
               }
                   .vhdl_package_member_visible(architecture, *owner, type.name);
    };
    bool valid = true;
    for (const auto& compiled_type :
        compiled_->vhdl_hir.types()) {
        const auto effective_view = specialized->find_type(
            compiled_type.id);
        if (!effective_view
            || effective_view->vhdl == nullptr) {
            continue;
        }
        const auto& type = *effective_view->vhdl;
        using Form = semantic::vhdl::TypeForm;
        if ((type.form != Form::subtype
                && type.form != Form::alias)
            || !relevant_type(type)) {
            continue;
        }
        auto unconstrained = type.base;
        const auto constraints = unconstrained.constraints;
        unconstrained.constraints.clear();
        std::unordered_set<std::uint32_t> visiting;
        auto profile = resolve_profile(
            unconstrained, type_scope(type), visiting);
        const auto source = constraints.empty()
            ? type.source
            : constraints.front().source;
        if (profile.cyclic) {
            report(
                "FSIM-ELAB-VHTYPE-002",
                "cyclic VHDL type declaration involving '"
                    + type.name + "'",
                compiled_source_span(*compiled_, type.source));
            valid = false;
            continue;
        }
        if (profile.type_class == SubtypeClass::unknown) {
            report(
                "FSIM-ELAB-VHTYPE-001",
                "VHDL type '" + type.base.type_mark.spelling
                    + "' is not visible in this unit",
                compiled_source_span(*compiled_, type.source));
            valid = false;
            continue;
        }
        const auto enumeration_bound_type = [&](const std::optional<semantic::ExpressionId> expression)
            -> std::optional<semantic::TypeId> {
            if (!expression) {
                return std::nullopt;
            }
            const auto bound = specialized->find_expression(*expression);
            if (!bound || bound->vhdl == nullptr
                || !bound->vhdl->referenced_name) {
                return std::nullopt;
            }
            auto selected = bound->vhdl->referenced_name->selected;
            if (!selected
                && bound->vhdl->referenced_name->overloads.size() == 1U) {
                selected
                    = bound->vhdl->referenced_name->overloads.front();
            }
            const auto declaration = selected
                ? specialized->find_declaration(*selected)
                : std::nullopt;
            return declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->subtype
                    && declaration->vhdl->subtype
                           ->type_mark.target.valid()
                ? std::optional {
                      declaration->vhdl->subtype->type_mark.target
                  }
                : std::nullopt;
        };
        const semantic::CompiledDesignResolver subtype_resolver {
            *compiled_, architecture.id, &*specialized
        };
        for (const auto& constraint : constraints) {
            if ((constraint.kind
                        == semantic::vhdl::RangeKind::integer
                    && profile.type_class
                        != SubtypeClass::integer)
                || (constraint.kind
                        == semantic::vhdl::RangeKind::enumeration
                    && profile.type_class
                        != SubtypeClass::enumeration)
                || (constraint.kind
                        == semantic::vhdl::RangeKind::discrete
                    && profile.type_class
                        != SubtypeClass::integer
                    && profile.type_class
                        != SubtypeClass::enumeration)) {
                report(
                    "FSIM-ELAB-VHSUBTYPE-001",
                    "a derived VHDL range constraint requires an "
                    "integer-family or enumeration base subtype",
                    compiled_source_span(*compiled_, source));
                valid = false;
                continue;
            }
            if (constraint.kind
                == semantic::vhdl::RangeKind::array_index) {
                if (profile.type_class != SubtypeClass::array) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-003",
                        "a derived VHDL packed index constraint requires "
                        "an unconstrained one-dimensional packed-array "
                        "base",
                        compiled_source_span(*compiled_, source));
                    valid = false;
                } else if (profile.constrained_array) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-004",
                        "a constrained VHDL packed-array subtype cannot "
                        "be constrained again",
                        compiled_source_span(*compiled_, source));
                    valid = false;
                }
                continue;
            }
            if (profile.type_class == SubtypeClass::enumeration) {
                auto left = constraint.left
                    ? constraint.left
                    : constraint.left_expression
                    ? specialized->evaluate_integral_expression(
                          *constraint.left_expression)
                    : std::nullopt;
                auto right = constraint.right
                    ? constraint.right
                    : constraint.right_expression
                    ? specialized->evaluate_integral_expression(
                          *constraint.right_expression)
                    : std::nullopt;
                if (!left && profile.enumeration_type
                    && constraint.left_expression) {
                    left = subtype_resolver
                               .vhdl_enumeration_literal_ordinal(
                                   *profile.enumeration_type,
                                   *constraint.left_expression);
                }
                if (!right && profile.enumeration_type
                    && constraint.right_expression) {
                    right = subtype_resolver
                                .vhdl_enumeration_literal_ordinal(
                                    *profile.enumeration_type,
                                    *constraint.right_expression);
                }
                if (!left || !right) {
                    report(
                        "FSIM-ELAB-VHENUMRANGE-001",
                        "VHDL enumeration subtype range bounds must "
                        "resolve to locally static literals",
                        compiled_source_span(*compiled_, source));
                    valid = false;
                    continue;
                }
                const auto left_type = enumeration_bound_type(
                    constraint.left_expression);
                const auto right_type = enumeration_bound_type(
                    constraint.right_expression);
                if (profile.enumeration_type
                    && ((left_type
                            && *left_type
                                != *profile.enumeration_type)
                        || (right_type
                            && *right_type
                                != *profile.enumeration_type))) {
                    report(
                        "FSIM-ELAB-VHENUMRANGE-001",
                        "VHDL enumeration subtype range bounds must "
                        "belong to the resolved base type",
                        compiled_source_span(*compiled_, source));
                    valid = false;
                    continue;
                }
                const auto null = constraint.descending
                    ? *left < *right
                    : *left > *right;
                if (constraint.null || null) {
                    report(
                        "FSIM-ELAB-VHENUMRANGE-002",
                        "null VHDL enumeration subtype constraints are "
                        "not executable in this bounded runtime",
                        compiled_source_span(*compiled_, source));
                    valid = false;
                } else if (profile.scalar_range
                    && (*left
                            < std::min(profile.scalar_range->left,
                                profile.scalar_range->right)
                        || *left
                            > std::max(profile.scalar_range->left,
                                profile.scalar_range->right)
                        || *right
                            < std::min(profile.scalar_range->left,
                                profile.scalar_range->right)
                        || *right
                            > std::max(profile.scalar_range->left,
                                profile.scalar_range->right))) {
                    report(
                        "FSIM-ELAB-VHENUMRANGE-003",
                        "derived VHDL enumeration subtype constraint "
                        "lies outside its resolved base subtype range",
                        compiled_source_span(*compiled_, source));
                    valid = false;
                }
                continue;
            }
            if (profile.type_class != SubtypeClass::integer
                || !constraint.left || !constraint.right) {
                continue;
            }
            const auto null = constraint.descending
                ? *constraint.left < *constraint.right
                : *constraint.left > *constraint.right;
            if (constraint.null || null) {
                report(
                    "FSIM-ELAB-INTEGER-002",
                    "null VHDL integer subtype constraints are not "
                    "executable in this bounded runtime",
                    compiled_source_span(*compiled_, source));
                valid = false;
            } else if (profile.scalar_range
                && (*constraint.left
                        < std::min(profile.scalar_range->left,
                            profile.scalar_range->right)
                    || *constraint.left
                        > std::max(profile.scalar_range->left,
                            profile.scalar_range->right)
                    || *constraint.right
                        < std::min(profile.scalar_range->left,
                            profile.scalar_range->right)
                    || *constraint.right
                        > std::max(profile.scalar_range->left,
                            profile.scalar_range->right))) {
                const auto legacy_predefined_integer
                    = architecture.standard != "2019"
                    && compiled_vhdl_name_equal(
                        type.base.type_mark.spelling, "integer");
                report(
                    legacy_predefined_integer
                        ? "FSIM-ELAB-INTEGER-002"
                        : "FSIM-ELAB-VHSUBTYPE-002",
                    legacy_predefined_integer
                        ? "VHDL integer subtype constraint lies outside "
                          "the portable signed 32-bit range before "
                          "VHDL-2019"
                        : "derived VHDL integer subtype constraint lies "
                          "outside its resolved base subtype range",
                    compiled_source_span(*compiled_, source));
                valid = false;
            }
        }
    }
    const auto validate_declaration_order = [&](const semantic::vhdl::Unit& unit) {
        std::vector<semantic::DeclarationId> declarations {
            unit.declarations
        };
        for (const auto& candidate :
            compiled_->vhdl_hir.declarations()) {
            const auto* scope = compiled_semantic_scope(
                *compiled_, candidate.scope);
            if (scope != nullptr && scope->unit == unit.id
                && std::ranges::find(declarations, candidate.id)
                    == declarations.end()) {
                declarations.push_back(candidate.id);
            }
        }
        for (const auto declaration_id : declarations) {
            const auto declaration = specialized->find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->subtype) {
                continue;
            }
            using DeclarationForm
                = semantic::vhdl::DeclarationForm;
            const auto form = declaration->vhdl->form;
            if (form != DeclarationForm::generic_constant
                && form != DeclarationForm::port
                && form != DeclarationForm::signal
                && form != DeclarationForm::constant
                && form != DeclarationForm::variable
                && form != DeclarationForm::file) {
                continue;
            }
            const auto linked_subtype = compiled_vhdl_link_subtype(
                *specialized,
                *declaration->vhdl->subtype,
                declaration->vhdl->scope);
            auto unconstrained = linked_subtype;
            const auto constraints = unconstrained.constraints;
            unconstrained.constraints.clear();
            std::unordered_set<std::uint32_t> constraint_visiting;
            const auto base_profile = resolve_profile(
                unconstrained,
                declaration->vhdl->scope,
                constraint_visiting);
            for (const auto& constraint : constraints) {
                if (base_profile.type_class
                        != SubtypeClass::integer
                    || (constraint.kind
                            != semantic::vhdl::RangeKind::integer
                        && constraint.kind
                            != semantic::vhdl::RangeKind::discrete)) {
                    continue;
                }
                const auto left = constraint.left
                    ? constraint.left
                    : constraint.left_expression
                    ? specialized->evaluate_integral_expression(
                          *constraint.left_expression)
                    : std::nullopt;
                const auto right = constraint.right
                    ? constraint.right
                    : constraint.right_expression
                    ? specialized->evaluate_integral_expression(
                          *constraint.right_expression)
                    : std::nullopt;
                if (!left || !right) {
                    continue;
                }
                const auto null = constraint.descending
                    ? *left < *right
                    : *left > *right;
                if (constraint.null || null) {
                    report(
                        "FSIM-ELAB-INTEGER-002",
                        "null VHDL integer subtype constraints are not "
                        "executable in this bounded runtime",
                        compiled_source_span(
                            *compiled_, constraint.source));
                    valid = false;
                } else if (base_profile.scalar_range
                    && (*left
                            < std::min(base_profile.scalar_range->left,
                                base_profile.scalar_range->right)
                        || *left
                            > std::max(base_profile.scalar_range->left,
                                base_profile.scalar_range->right)
                        || *right
                            < std::min(base_profile.scalar_range->left,
                                base_profile.scalar_range->right)
                        || *right
                            > std::max(base_profile.scalar_range->left,
                                base_profile.scalar_range->right))) {
                    report(
                        "FSIM-ELAB-INTEGER-003",
                        "VHDL object subtype constraint lies outside "
                        "its resolved base subtype range",
                        compiled_source_span(
                            *compiled_, constraint.source));
                    valid = false;
                }
            }
            const auto& reference = linked_subtype.type_mark;
            const auto* target = reference.target.valid()
                ? effective_type(reference.target)
                : locally_named_type(
                      reference.spelling,
                      declaration->vhdl->scope);
            if (target == nullptr) {
                std::unordered_set<std::uint32_t> visiting;
                const auto profile = resolve_profile(
                    *declaration->vhdl->subtype,
                    declaration->vhdl->scope,
                    visiting);
                const auto generic_type_visible = std::ranges::any_of(
                    compiled_->vhdl_hir.declarations(),
                    [&](const semantic::vhdl::Declaration& candidate) {
                        if (candidate.form
                                != semantic::vhdl::DeclarationForm::
                                    generic_type
                            || !compiled_vhdl_name_equal(
                                candidate.name,
                                reference.spelling)) {
                            return false;
                        }
                        const auto* candidate_scope
                            = compiled_semantic_scope(
                                *compiled_, candidate.scope);
                        const auto* declaration_scope
                            = compiled_semantic_scope(
                                *compiled_, declaration->vhdl->scope);
                        if (candidate_scope == nullptr
                            || declaration_scope == nullptr) {
                            return false;
                        }
                        if (candidate_scope->unit
                            == declaration_scope->unit) {
                            auto scope = std::optional {
                                declaration->vhdl->scope
                            };
                            while (scope) {
                                if (*scope == candidate.scope) {
                                    return true;
                                }
                                const auto* current
                                    = compiled_semantic_scope(
                                        *compiled_, *scope);
                                scope = current != nullptr
                                    ? current->parent
                                    : std::nullopt;
                            }
                            return false;
                        }
                        const auto* candidate_unit = compiled_vhdl_unit(
                            *compiled_, candidate_scope->unit);
                        const auto* declaration_unit
                            = compiled_vhdl_unit(
                                *compiled_, declaration_scope->unit);
                        return candidate_unit != nullptr
                            && declaration_unit != nullptr
                            && compiled_vhdl_library_equal(
                                candidate_unit->library,
                                declaration_unit->library)
                            && ((candidate_unit->kind
                                        == semantic::vhdl::UnitKind::entity
                                    && declaration_unit->kind
                                        == semantic::vhdl::UnitKind::
                                            architecture
                                    && compiled_vhdl_name_equal(
                                        candidate_unit->name,
                                        declaration_unit->primary_name))
                                || (declaration_unit->kind
                                        == semantic::vhdl::UnitKind::entity
                                    && candidate_unit->kind
                                        == semantic::vhdl::UnitKind::
                                            architecture
                                    && compiled_vhdl_name_equal(
                                        declaration_unit->name,
                                        candidate_unit->primary_name)));
                    });
                if (profile.type_class == SubtypeClass::unknown
                    && linked_subtype.unspecified_class
                        == semantic::vhdl::UnspecifiedTypeClass::none
                    && !generic_type_visible) {
                    report(
                        "FSIM-ELAB-VHTYPE-001",
                        "VHDL type '" + reference.spelling
                            + "' is not visible at this declaration",
                        compiled_source_span(
                            *compiled_, reference.source));
                    valid = false;
                }
                continue;
            }
            const auto target_declaration = compiled_->find_declaration(
                target->declaration);
            if (!target_declaration
                || target_declaration->vhdl == nullptr
                || target_declaration->vhdl->scope
                    != declaration->vhdl->scope) {
                continue;
            }
            const auto use_span = compiled_source_span(
                *compiled_, linked_subtype.type_mark.source);
            const auto target_span = compiled_source_span(
                *compiled_, target->source);
            const auto target_form = target_declaration->vhdl->form;
            const auto interface_uses_local_type
                = (form == DeclarationForm::generic_constant
                      || form == DeclarationForm::port)
                && (target_form == DeclarationForm::type
                    || target_form == DeclarationForm::subtype);
            if (!interface_uses_local_type
                && target_span.begin.offset <= use_span.begin.offset) {
                continue;
            }
            report(
                "FSIM-ELAB-VHTYPE-001",
                "VHDL type '"
                    + declaration->vhdl->subtype->type_mark.spelling
                    + "' is not visible at this declaration",
                use_span);
            valid = false;
        }
    };
    validate_declaration_order(*entity);
    validate_declaration_order(architecture);
    for (const auto& statement :
        compiled_->vhdl_hir.statements()) {
        const auto* scope = compiled_semantic_scope(
            *compiled_, statement.scope);
        if (scope == nullptr
            || (scope->unit != architecture.id
                && scope->unit != entity->id)
            || !statement.target) {
            continue;
        }
        const auto target = specialized->find_expression(
            *statement.target);
        const auto target_id = target
                && target->vhdl != nullptr
                && target->vhdl->referenced_name
            ? target->vhdl->referenced_name->selected
            : std::nullopt;
        const auto declaration = target_id
            ? specialized->find_declaration(*target_id)
            : std::nullopt;
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            continue;
        }
        const auto target_type = compiled_vhdl_signal_type(
            *specialized,
            *declaration->vhdl->subtype,
            declaration->vhdl->scope);
        if (target_type
            && !target_type->enumeration_literals.empty()) {
            continue;
        }
        const auto range
            = compiled_vhdl_specialization_integer_constraint(
                *specialized,
                *declaration->vhdl->subtype);
        if (!range) {
            continue;
        }
        std::vector<semantic::ExpressionId> values;
        if (statement.value) {
            values.push_back(*statement.value);
        }
        for (const auto& waveform : statement.waveform) {
            if (!waveform.disconnect) {
                values.push_back(waveform.value);
            }
        }
        for (const auto value_id : values) {
            const auto value
                = specialized->evaluate_integral_expression(value_id);
            if (!value
                || (*value >= std::min(range->left, range->right)
                    && *value
                        <= std::max(range->left, range->right))) {
                continue;
            }
            report(
                "FSIM-ELAB-INTEGER-004",
                "assigned VHDL integer value lies outside the target "
                "subtype range",
                compiled_source_span(*compiled_, statement.source));
            valid = false;
        }
    }
    return valid;
}

bool HierarchyBuilder::validate_compiled_vhdl_selected_package_names(
    const semantic::vhdl::Unit* entity,
    const semantic::vhdl::Unit& architecture,
    const std::optional<semantic::SpecializedHirUnit>& specialized)
{
    bool valid = true;
    std::unordered_set<std::uint32_t> association_actuals;
    for (const auto& instance : compiled_->vhdl_hir.instances()) {
        for (const auto& association : instance.generic_map) {
            if (association.expression) {
                association_actuals.insert(
                    association.expression->value());
            }
        }
    }
    const auto locally_declared = [&](const std::string_view name) {
        return std::ranges::any_of(
            compiled_->vhdl_hir.declarations(),
            [&](const semantic::vhdl::Declaration& declaration) {
                if (!compiled_vhdl_name_equal(
                        declaration.name, name)) {
                    return false;
                }
                const auto* scope = compiled_semantic_scope(
                    *compiled_, declaration.scope);
                return scope != nullptr
                    && (scope->unit == architecture.id
                        || scope->unit == entity->id);
            });
    };
    for (const auto& expression :
        compiled_->vhdl_hir.expressions()) {
        if (expression.kind
                != semantic::vhdl::ExpressionKind::name
            || association_actuals.contains(expression.id.value())
            || (expression.referenced_name
                && (expression.referenced_name->selected
                    || !expression.referenced_name
                        ->overloads.empty()))) {
            continue;
        }
        const auto* scope = compiled_semantic_scope(
            *compiled_, expression.scope);
        if (scope == nullptr
            || (scope->unit != architecture.id
                && scope->unit != entity->id)) {
            continue;
        }
        const auto spelling = expression.referenced_name
                && !expression.referenced_name->canonical.empty()
            ? std::string_view {
                  expression.referenced_name->canonical
              }
            : std::string_view { expression.text };
        const auto parts = compiled_vhdl_name_parts(spelling);
        const auto hierarchy_root_name = parts.size() >= 2U
            && (compiled_vhdl_name_equal(
                    parts.front(), entity->name)
                || compiled_vhdl_name_equal(
                    parts.front(), architecture.primary_name));
        if (parts.size() < 2U || locally_declared(parts.front())
            || hierarchy_root_name) {
            continue;
        }
        if (parts.size() != 2U && parts.size() != 3U) {
            report(
                "FSIM-ELAB-PKG-008",
                "a selected package constant must be "
                "package.constant or library.package.constant",
                compiled_source_span(
                    *compiled_, expression.source));
            valid = false;
            continue;
        }
        const auto package_name
            = parts[parts.size() - 2U];
        const auto member_name = parts.back();
        const auto owner_library = architecture.library.empty()
            ? std::string_view { "work" }
            : std::string_view { architecture.library };
        const auto requested_library = parts.size() == 2U
            ? owner_library
            : compiled_vhdl_effective_library(
                  parts.front(), owner_library);
        const auto package = std::ranges::find_if(
            compiled_->vhdl_units(),
            [&](const semantic::vhdl::Unit& candidate) {
                return candidate.kind
                    == semantic::vhdl::UnitKind::package
                    && candidate.primary_name.empty()
                    && compiled_vhdl_library_equal(
                        candidate.library, requested_library)
                    && compiled_vhdl_name_equal(
                        candidate.name, package_name);
            });
        if (package == compiled_->vhdl_units().end()) {
            semantic::vhdl::Name builtin_name;
            builtin_name.spelling
                = std::string { requested_library } + "."
                + std::string { package_name } + "."
                + std::string { member_name };
            builtin_name.canonical = builtin_name.spelling;
            if (semantic::CompiledDesignResolver {
                    *compiled_, architecture.id, &*specialized }
                    .vhdl_standard_package_member_visible(
                        builtin_name, expression.scope)) {
                continue;
            }
            report(
                "FSIM-ELAB-PKG-009",
                "VHDL package '"
                    + std::string { requested_library } + "."
                    + std::string { package_name }
                    + "' was not found",
                compiled_source_span(
                    *compiled_, expression.source));
            valid = false;
            continue;
        }
        const auto exported = std::ranges::any_of(
                                  package->declarations,
                                  [&](const semantic::DeclarationId declaration_id) {
                                      const auto declaration
                                          = specialized->find_declaration(declaration_id);
                                      return declaration && declaration->vhdl != nullptr
                                          && compiled_vhdl_name_equal(
                                              declaration->vhdl->name, member_name);
                                  })
            || std::ranges::any_of(
                package->declarations,
                [&](const semantic::DeclarationId declaration_id) {
                    const auto declaration
                        = specialized->find_declaration(declaration_id);
                    if (!declaration || declaration->vhdl == nullptr
                        || !declaration->vhdl->declared_type) {
                        return false;
                    }
                    const auto type = specialized->find_type(
                        *declaration->vhdl->declared_type);
                    return type && type->vhdl != nullptr
                        && (std::ranges::any_of(
                                type->vhdl->enumeration_literals,
                                [&](const semantic::vhdl::EnumerationLiteral& literal) {
                                    return compiled_vhdl_name_equal(
                                        literal.spelling, member_name);
                                })
                            || std::ranges::any_of(
                                type->vhdl->physical_units,
                                [&](const semantic::vhdl::PhysicalUnit& unit) {
                                    return compiled_vhdl_name_equal(
                                        unit.name, member_name);
                                }));
                })
            || std::ranges::any_of(
                package->standard_package_declarations,
                [&](const std::string& declaration) {
                    return compiled_vhdl_name_equal(
                        declaration, member_name);
                });
        if (!exported) {
            report(
                "FSIM-ELAB-PKG-010",
                "VHDL package '"
                    + std::string { requested_library } + "."
                    + std::string { package_name }
                    + "' has no exported item '"
                    + std::string { member_name } + "'",
                compiled_source_span(
                    *compiled_, expression.source));
            valid = false;
        }
    }
    return valid;
}

std::vector<const semantic::vhdl::Unit*>
HierarchyBuilder::compiled_vhdl_package_template_candidates(
    const semantic::vhdl::Name& name,
    const std::string_view owner_library)
{
    auto spelling = name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
    auto library = owner_library.empty()
        ? std::string_view { "work" }
        : owner_library;
    if (const auto separator = spelling.rfind('.');
        separator != std::string_view::npos) {
        library = compiled_vhdl_effective_library(
            spelling.substr(0U, separator), owner_library);
        spelling.remove_prefix(separator + 1U);
    }
    std::vector<const semantic::vhdl::Unit*> result;
    for (const auto& unit : compiled_->vhdl_units()) {
        if (unit.kind == semantic::vhdl::UnitKind::package
            && unit.primary_name.empty()
            && compiled_vhdl_library_equal(unit.library, library)
            && compiled_vhdl_name_equal(unit.name, spelling)) {
            result.push_back(&unit);
        }
    }
    return result;
}

void HierarchyBuilder::validate_compiled_vhdl_package_instances(
    const std::string& path,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture,
    const semantic::SpecializedHirUnit& specialized)
{
    if (path != active_root_) {
        return;
    }
    const auto validate_declarations = [&](const auto& declarations,
                                           const std::string_view library) {
        for (const auto declaration_id : declarations) {
            const auto declaration
                = specialized.find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::package_instance
                || !declaration->vhdl->package) {
                continue;
            }
            const auto candidates
                = compiled_vhdl_package_template_candidates(
                    declaration->vhdl->package->template_name, library);
            const auto span = compiled_source_span(
                *compiled_, declaration->vhdl->source);
            if (candidates.size() > 1U) {
                report(
                    "FSIM-ELAB-VHPKG-012",
                    "generic package template for local instance '"
                        + declaration->vhdl->name + "' is ambiguous",
                    span);
                continue;
            }
            if (candidates.empty()) {
                report(
                    "FSIM-ELAB-VHPKG-009",
                    "generic package template for local instance '"
                        + declaration->vhdl->name + "' was not found",
                    span);
                continue;
            }
            const auto* package = candidates.front();
            const auto generic = std::ranges::any_of(
                package->declarations,
                [&](const semantic::DeclarationId id) {
                    const auto record = specialized.find_declaration(id);
                    if (!record || record->vhdl == nullptr) {
                        return false;
                    }
                    using Form = semantic::vhdl::DeclarationForm;
                    const auto form = record->vhdl->form;
                    return form == Form::generic_constant
                        || form == Form::generic_type
                        || form == Form::generic_function
                        || form == Form::generic_procedure
                        || form == Form::generic_package;
                });
            if (!generic) {
                report(
                    "FSIM-ELAB-VHPKG-013",
                    "local package instance '"
                        + declaration->vhdl->name
                        + "' selects a nongeneric package",
                    span);
                continue;
            }
            const auto body = std::ranges::find_if(
                compiled_->vhdl_units(),
                [&](const semantic::vhdl::Unit& candidate) {
                    return candidate.kind
                        == semantic::vhdl::UnitKind::package
                        && !candidate.primary_name.empty()
                        && compiled_vhdl_library_equal(
                            candidate.library, package->library)
                        && compiled_vhdl_name_equal(
                            candidate.name, package->name)
                        && compiled_vhdl_name_equal(
                            candidate.primary_name, package->name);
                });
            const auto incomplete = std::ranges::any_of(
                package->declarations,
                [&](const semantic::DeclarationId id) {
                    const auto record = specialized.find_declaration(id);
                    if (!record || record->vhdl == nullptr
                        || !record->vhdl->callable
                        || record->vhdl->callable->defined
                        || (record->vhdl->form
                                != semantic::vhdl::DeclarationForm::function
                            && record->vhdl->form
                                != semantic::vhdl::DeclarationForm::procedure)) {
                        return false;
                    }
                    if (body == compiled_->vhdl_units().end()) {
                        return true;
                    }
                    return std::ranges::none_of(
                        body->declarations,
                        [&](const semantic::DeclarationId candidate_id) {
                            const auto candidate
                                = specialized.find_declaration(candidate_id);
                            return candidate
                                && candidate->vhdl != nullptr
                                && candidate->vhdl->callable
                                && candidate->vhdl->callable->defined
                                && compiled_vhdl_name_equal(
                                    candidate->vhdl->name,
                                    record->vhdl->name);
                        });
                });
            if (incomplete) {
                report(
                    "FSIM-ELAB-VHPKG-010",
                    "selected bounded package instance '"
                        + declaration->vhdl->name
                        + "' has an incomplete required callable body",
                    span);
            }
        }
    };
    validate_declarations(entity.declarations, entity.library);
    validate_declarations(architecture.declarations, architecture.library);
}

void HierarchyBuilder::validate_compiled_vhdl_instantiated_package_cycles(
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture,
    const semantic::SpecializedHirUnit& specialized)
{
    std::unordered_map<std::uint32_t, std::uint8_t> states;
    const auto visit = [&](const auto& self,
                           const semantic::vhdl::Unit& package,
                           const semantic::SourceSpanId source) -> void {
        auto& state = states[package.id.value()];
        if (state == 1U) {
            report(
                "FSIM-ELAB-PKG-007",
                "cyclic VHDL package visibility includes '"
                    + package.library + "." + package.name + "'",
                compiled_source_span(*compiled_, source));
            return;
        }
        if (state == 2U) {
            return;
        }
        state = 1U;
        for (const auto& item : package.context) {
            if (item.kind
                != semantic::vhdl::ContextKind::use_clause) {
                continue;
            }
            for (const auto& selected : item.selected_names) {
                const auto spelling = selected.canonical.empty()
                    ? std::string_view { selected.spelling }
                    : std::string_view { selected.canonical };
                const auto parts = compiled_vhdl_name_parts(spelling);
                if (parts.size() != 3U) {
                    continue;
                }
                const auto library = compiled_vhdl_effective_library(
                    parts[0], package.library);
                const auto dependency = std::ranges::find_if(
                    compiled_->vhdl_units(),
                    [&](const semantic::vhdl::Unit& candidate) {
                        return candidate.kind
                            == semantic::vhdl::UnitKind::package
                            && candidate.primary_name.empty()
                            && compiled_vhdl_library_equal(
                                candidate.library, library)
                            && compiled_vhdl_name_equal(
                                candidate.name, parts[1]);
                    });
                if (dependency != compiled_->vhdl_units().end()) {
                    self(self, *dependency, item.source);
                }
            }
        }
        state = 2U;
    };
    const auto validate_roots = [&](const auto& declarations,
                                    const std::string_view library) {
        for (const auto declaration_id : declarations) {
            const auto declaration
                = specialized.find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::package_instance
                || !declaration->vhdl->package) {
                continue;
            }
            const auto candidates
                = compiled_vhdl_package_template_candidates(
                    declaration->vhdl->package->template_name, library);
            if (candidates.size() == 1U) {
                visit(visit, *candidates.front(),
                    declaration->vhdl->source);
            }
        }
    };
    validate_roots(entity.declarations, entity.library);
    validate_roots(architecture.declarations, architecture.library);
}

bool HierarchyBuilder::validate_compiled_vhdl_predefined_subtype_attributes(
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture,
    const semantic::SpecializedHirUnit& specialized)
{
    struct PredefinedSubtypeProfile {
        std::size_t array_rank { };
        bool designated { };
    };
    const auto predefined_subtype_profile = [&](const semantic::vhdl::SubtypeIndication& subtype)
        -> PredefinedSubtypeProfile {
        std::unordered_set<std::uint32_t> visiting;
        const auto resolve = [&](const auto& self,
                                 const semantic::vhdl::SubtypeIndication& current)
            -> PredefinedSubtypeProfile {
            if (!current.type_mark.target.valid()) {
                const auto separator
                    = current.type_mark.spelling.find_last_of('.');
                const auto name = std::string_view {
                    current.type_mark.spelling
                }
                                      .substr(separator == std::string::npos ? 0U : separator + 1U);
                const auto one_dimensional
                    = compiled_vhdl_name_equal(name, "bit_vector")
                    || compiled_vhdl_name_equal(
                        name, "std_logic_vector")
                    || compiled_vhdl_name_equal(
                        name, "std_ulogic_vector")
                    || compiled_vhdl_name_equal(name, "signed")
                    || compiled_vhdl_name_equal(name, "unsigned")
                    || compiled_vhdl_name_equal(name, "string")
                    || compiled_vhdl_name_equal(
                        name, "boolean_vector")
                    || compiled_vhdl_name_equal(
                        name, "integer_vector")
                    || compiled_vhdl_name_equal(name, "real_vector")
                    || compiled_vhdl_name_equal(name, "time_vector")
                    || compiled_vhdl_name_equal(name, "ufixed")
                    || compiled_vhdl_name_equal(name, "sfixed")
                    || compiled_vhdl_name_equal(
                        name, "unresolved_ufixed")
                    || compiled_vhdl_name_equal(
                        name, "unresolved_sfixed")
                    || compiled_vhdl_name_equal(name, "float")
                    || compiled_vhdl_name_equal(
                        name, "unresolved_float")
                    || compiled_vhdl_name_equal(name, "u_float");
                return { one_dimensional ? 1U : 0U, false };
            }
            if (!visiting.insert(
                             current.type_mark.target.value())
                    .second) {
                return { };
            }
            const auto type = specialized.find_type(
                current.type_mark.target);
            if (!type || type->vhdl == nullptr) {
                return { };
            }
            const auto& definition = *type->vhdl;
            if (definition.form
                == semantic::vhdl::TypeForm::array) {
                return { definition.array_dimensions.size(), false };
            }
            if (definition.form
                == semantic::vhdl::TypeForm::access) {
                return { 0U,
                    definition.designated_subtype.has_value() };
            }
            if (definition.form
                == semantic::vhdl::TypeForm::file) {
                return { 0U,
                    definition.element_subtype.has_value() };
            }
            if (definition.form
                    == semantic::vhdl::TypeForm::subtype
                || definition.form
                    == semantic::vhdl::TypeForm::alias) {
                return self(self, definition.base);
            }
            return { };
        };
        return resolve(resolve, subtype);
    };
    const auto validate_predefined_subtype_attributes = [&](const semantic::vhdl::Unit& unit) {
        bool valid = true;
        for (const auto declaration_id : unit.declarations) {
            const auto declaration = specialized.find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->subtype
                || !declaration->vhdl->subtype
                    ->predefined_attribute) {
                continue;
            }
            const auto& record = *declaration->vhdl;
            const auto& subtype = *record.subtype;
            const auto profile = predefined_subtype_profile(subtype);
            auto source = subtype.type_mark.source.valid()
                ? subtype.type_mark.source
                : record.source;
            if (*subtype.predefined_attribute
                == semantic::vhdl::PredefinedAttribute::designated_subtype) {
                if (!profile.designated) {
                    report(
                        "FSIM-ELAB-VHATTR-009",
                        "the predefined 'designated_subtype attribute "
                        "requires an access or file type",
                        compiled_source_span(*compiled_, source));
                    valid = false;
                }
                continue;
            }
            if (*subtype.predefined_attribute
                != semantic::vhdl::PredefinedAttribute::index) {
                continue;
            }
            std::int64_t dimension { 1 };
            if (subtype.predefined_attribute_dimension) {
                const auto expression = specialized.find_expression(
                    *subtype.predefined_attribute_dimension);
                if (expression && expression->vhdl != nullptr) {
                    source = expression->vhdl->source;
                }
                const auto value
                    = specialized.evaluate_integral_expression(
                        *subtype.predefined_attribute_dimension);
                if (!value) {
                    report(
                        "FSIM-ELAB-VHATTR-010",
                        "the predefined 'index dimension must be "
                        "locally static",
                        compiled_source_span(*compiled_, source));
                    valid = false;
                    continue;
                }
                dimension = *value;
            }
            if (profile.array_rank == 0U || dimension < 1
                || static_cast<std::uint64_t>(dimension)
                    > profile.array_rank) {
                report(
                    "FSIM-ELAB-VHATTR-010",
                    "the predefined 'index attribute requires an array "
                    "type and a dimension inside its rank",
                    compiled_source_span(*compiled_, source));
                valid = false;
            }
        }
        return valid;
    };
    const auto entity_attributes_valid
        = validate_predefined_subtype_attributes(entity);
    const auto architecture_attributes_valid
        = validate_predefined_subtype_attributes(architecture);
    return entity_attributes_valid && architecture_attributes_valid;
}

bool HierarchyBuilder::validate_compiled_vhdl_package_callable_bodies(
    const std::string& path,
    const std::optional<semantic::SpecializedHirUnit>& specialized)
{
    if (path != active_root_) {
        return true;
    }
    bool valid = true;
    using Form = semantic::vhdl::DeclarationForm;
    for (const auto& package : compiled_->vhdl_units()) {
        if (package.kind != semantic::vhdl::UnitKind::package
            || !package.primary_name.empty()) {
            continue;
        }
        const auto body = std::ranges::find_if(
            compiled_->vhdl_units(),
            [&](const semantic::vhdl::Unit& candidate) {
                return candidate.kind
                    == semantic::vhdl::UnitKind::package
                    && compiled_vhdl_library_equal(
                        candidate.library, package.library)
                    && compiled_vhdl_name_equal(
                        candidate.name, package.name)
                    && compiled_vhdl_name_equal(
                        candidate.primary_name, package.name);
            });
        const auto body_declarations = body
                == compiled_->vhdl_units().end()
            ? std::span<const semantic::DeclarationId> { }
            : std::span<const semantic::DeclarationId> {
                  body->declarations
              };
        const semantic::CompiledDesignResolver profile_resolver {
            *specialized
        };
        const auto validate_direction = [&](
                                            const auto& required,
                                            const auto& provided,
                                            const Form form,
                                            const std::string_view code,
                                            const std::string_view description,
                                            const bool require_defined) {
            for (const auto declaration_id : required) {
                const auto declaration
                    = specialized->find_declaration(declaration_id);
                if (!declaration || declaration->vhdl == nullptr
                    || declaration->vhdl->form != form
                    || !declaration->vhdl->callable
                    || declaration->vhdl->callable->defined
                        != require_defined) {
                    continue;
                }
                const auto same_designator = std::ranges::any_of(
                    provided,
                    [&](const semantic::DeclarationId candidate_id) {
                        const auto candidate
                            = specialized->find_declaration(candidate_id);
                        return candidate
                            && candidate->vhdl != nullptr
                            && candidate->vhdl->form == form
                            && candidate->vhdl->callable
                            && candidate->vhdl->callable->defined
                            != require_defined
                            && compiled_vhdl_name_equal(
                                candidate->vhdl->name,
                                declaration->vhdl->name);
                    });
                if (!same_designator && require_defined) {
                    continue;
                }
                const auto conforming = std::ranges::any_of(
                    provided,
                    [&](const semantic::DeclarationId candidate_id) {
                        const auto candidate
                            = specialized->find_declaration(candidate_id);
                        return candidate
                            && candidate->vhdl != nullptr
                            && candidate->vhdl->form == form
                            && candidate->vhdl->callable
                            && candidate->vhdl->callable->defined
                            != require_defined
                            && compiled_vhdl_name_equal(
                                candidate->vhdl->name,
                                declaration->vhdl->name)
                            && profile_resolver
                                   .vhdl_callable_profile_matches(
                                       declaration_id,
                                       candidate_id, true);
                    });
                if (conforming) {
                    continue;
                }
                report(
                    std::string { code },
                    std::string { description } + " '"
                        + declaration->vhdl->name
                        + "' has no conforming profile",
                    compiled_source_span(
                        *compiled_, declaration->vhdl->source));
                valid = false;
            }
        };
        validate_direction(body_declarations, package.declarations,
            Form::function, "FSIM-ELAB-VHLEGAL-001",
            "VHDL package function body", true);
        validate_direction(package.declarations, body_declarations,
            Form::function, "FSIM-ELAB-VHLEGAL-002",
            "VHDL package function declaration", false);
        validate_direction(body_declarations, package.declarations,
            Form::procedure, "FSIM-ELAB-VHLEGAL-003",
            "VHDL package procedure body", true);
        validate_direction(package.declarations, body_declarations,
            Form::procedure, "FSIM-ELAB-VHLEGAL-004",
            "VHDL package procedure declaration", false);
        for (const auto declaration_id : package.declarations) {
            const auto declaration
                = specialized->find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || declaration->vhdl->form != Form::constant) {
                continue;
            }
            const auto completion = std::ranges::find_if(
                body_declarations,
                [&](const semantic::DeclarationId candidate_id) {
                    const auto candidate
                        = specialized->find_declaration(candidate_id);
                    return candidate && candidate->vhdl != nullptr
                        && candidate->vhdl->form == Form::constant
                        && compiled_vhdl_name_equal(
                            candidate->vhdl->name,
                            declaration->vhdl->name);
                });
            if (!declaration->vhdl->deferred) {
                if (completion != body_declarations.end()) {
                    const auto candidate
                        = specialized->find_declaration(*completion);
                    report(
                        "FSIM-ELAB-VHLEGAL-012",
                        "VHDL package body redeclares nondeferred constant '"
                            + declaration->vhdl->name + "'",
                        compiled_source_span(
                            *compiled_, candidate->vhdl->source));
                    valid = false;
                }
                continue;
            }
            if (completion == body_declarations.end()) {
                report(
                    "FSIM-ELAB-VHLEGAL-010",
                    "deferred VHDL package constant '"
                        + declaration->vhdl->name
                        + "' has no full declaration in the package body",
                    compiled_source_span(
                        *compiled_, declaration->vhdl->source));
                valid = false;
                continue;
            }
            const auto candidate
                = specialized->find_declaration(*completion);
            if (!candidate || candidate->vhdl == nullptr
                || !declaration->vhdl->subtype
                || !candidate->vhdl->subtype
                || !profile_resolver.vhdl_subtype_profiles_match(
                    *declaration->vhdl->subtype,
                    declaration->vhdl->scope,
                    *candidate->vhdl->subtype,
                    candidate->vhdl->scope)) {
                report(
                    "FSIM-ELAB-VHLEGAL-011",
                    "full declaration of deferred VHDL package constant '"
                        + declaration->vhdl->name
                        + "' does not conform to its subtype indication",
                    compiled_source_span(
                        *compiled_, candidate->vhdl->source));
                valid = false;
            }
        }
    }
    return valid;
}

bool HierarchyBuilder::validate_compiled_vhdl_port_actual_compatibility(
    CompiledVhdlPortCompatibilityContext context)
{
    const auto& formal_declaration = context.formal_declaration;
    const auto actual_signal = context.actual_signal;
    auto& actual_hir_type = context.actual_hir_type;
    const auto& actual_hir_subtype = context.actual_hir_subtype;
    const auto& actual_hir_declaration
        = context.actual_hir_declaration;
    const auto& actual_hir_scope = context.actual_hir_scope;
    const auto* const expression = context.expression;
    const auto direction = context.direction;
    const auto& child_path = context.child_path;
    const auto& child_interface_specialization
        = context.child_interface_specialization;
    const auto binding_source = context.binding_source;
    const auto formal_layout
        = compiled_vhdl_named_signal_layout(
            *child_interface_specialization,
            *formal_declaration.subtype,
            formal_declaration.scope);
    const auto& actual_info
        = design_.signal_info_[actual_signal];
    const auto connection_source = compiled_source_span(
        *compiled_, binding_source);
    if (!actual_hir_type) {
        // The associated signal has already crossed the HIR to
        // DesignIR boundary, so its SignalInfo is authoritative
        // for the concrete occurrence shape.  A decoded or
        // re-linked name need not retain the frontend-selected
        // declaration shortcut used above; preserve the same
        // type checks by rebuilding the non-owning metadata view
        // from the materialized signal instead of classifying a
        // concrete array as an untyped actual.
        actual_hir_type.emplace();
        actual_hir_type->domain = actual_info.source_domain;
        actual_hir_type->spelling = actual_info.type_name;
        actual_hir_type->named_type = actual_info.type_name;
        actual_hir_type->is_signed = actual_info.is_signed;
        actual_hir_type->packed_range
            = actual_info.packed_range;
        actual_hir_type->integer_range
            = actual_info.integer_range;
        actual_hir_type->nominal_type
            = actual_info.nominal_type;
        actual_hir_type->enumeration_literals
            = actual_info.enumeration_literals;
        actual_hir_type->enumeration_range
            = actual_info.enumeration_range;
        actual_hir_type->vhdl_array = actual_info.vhdl_array;
        actual_hir_type->vhdl_access = actual_info.vhdl_access;
        actual_hir_type->vhdl_physical
            = actual_info.vhdl_physical;
        actual_hir_type->packed_members
            = actual_info.packed_members;
    }
    bool incompatible_alias { };
    if (formal_layout
        && formal_layout->width != actual_info.width) {
        report(
            "FSIM-ELAB-BIND-020",
            "width mismatch on '" + child_path + "."
                + formal_declaration.name + "': "
                + std::to_string(formal_layout->width)
                + " versus "
                + std::to_string(actual_info.width),
            connection_source);
        incompatible_alias = true;
    }
    const auto formal_type = compiled_vhdl_signal_type(
        *child_interface_specialization,
        *formal_declaration.subtype,
        formal_declaration.scope);
    const auto array_type = [](const PackedTypeMetadata& type) {
        if (type.vhdl_array) {
            return true;
        }
        const auto name = compiled_vhdl_simple_name(
            type.spelling);
        return compiled_vhdl_name_equal(name, "bit_vector")
            || compiled_vhdl_name_equal(
                name, "std_logic_vector")
            || compiled_vhdl_name_equal(
                name, "std_ulogic_vector")
            || compiled_vhdl_name_equal(name, "signed")
            || compiled_vhdl_name_equal(name, "unsigned")
            || compiled_vhdl_name_equal(name, "string")
            || compiled_vhdl_name_equal(
                name, "boolean_vector")
            || compiled_vhdl_name_equal(
                name, "integer_vector")
            || compiled_vhdl_name_equal(name, "real_vector")
            || compiled_vhdl_name_equal(name, "time_vector");
    };
    const auto unconstrained_array = [&](
                                         const PackedTypeMetadata& type) {
        if (type.vhdl_array) {
            return type.vhdl_array->unconstrained
                || std::ranges::any_of(
                    type.vhdl_array->dimensions,
                    [](const auto& dimension) {
                        return dimension.unconstrained
                            && !dimension.range;
                    });
        }
        return array_type(type) && !type.packed_range;
    };
    if (formal_type && unconstrained_array(*formal_type)
        && (!actual_hir_type
            || !array_type(*actual_hir_type))) {
        report(
            "FSIM-ELAB-VHARRAY-005",
            "VHDL interface array port '"
                + formal_declaration.name
                + "' requires a concrete array actual to "
                  "supply its bounds",
            connection_source);
        return false;
    }
    if (formal_type && actual_hir_type
        && !formal_type->nominal_type.empty()
        && !actual_hir_type->nominal_type.empty()
        && formal_type->nominal_type
            != actual_hir_type->nominal_type) {
        report(
            !formal_type->enumeration_literals.empty()
                    || !actual_hir_type
                        ->enumeration_literals.empty()
                ? "FSIM-ELAB-BIND-053"
                : "FSIM-ELAB-BIND-057",
            "nominal VHDL type mismatch on '" + child_path
                + "." + formal_declaration.name + "'",
            connection_source);
        incompatible_alias = true;
    }
    if (formal_type && actual_hir_type
        && formal_type->nominal_type
            == actual_hir_type->nominal_type
        && formal_type->enumeration_range
        && actual_hir_type->enumeration_range) {
        const auto formal_enumeration_range
            = frontend::IntegerRange {
                  formal_type->enumeration_range->left,
                  formal_type->enumeration_range->right,
                  formal_type->enumeration_range->descending,
              };
        const auto actual_enumeration_range
            = frontend::IntegerRange {
                  actual_hir_type->enumeration_range->left,
                  actual_hir_type->enumeration_range->right,
                  actual_hir_type->enumeration_range->descending,
              };
        if (!integer_alias_range_safe(
                formal_enumeration_range,
                actual_enumeration_range,
                direction)) {
            report(
                "FSIM-ELAB-BIND-054",
                "enumeration subtype ranges on boundary '"
                    + child_path + "."
                    + formal_declaration.name
                    + "' are not range-safe for a direct alias",
                connection_source);
            incompatible_alias = true;
        }
    }
    const auto formal_domain = formal_type
        ? formal_type->domain
        : formal_layout
        ? formal_layout->domain
        : compiled_value_domain(
              formal_declaration.subtype->domain);
    const bool has_semantic_actual_domain
        = actual_hir_declaration && actual_hir_subtype
        && actual_hir_type;
    const bool has_semantic_state_domain_alias
        = has_semantic_actual_domain
        && actual_hir_type->domain
            != actual_info.source_domain
        && compiled_value_domain(actual_hir_subtype->domain)
            == actual_hir_type->domain
        && std::ranges::any_of(
            design_.boundary_conversions_,
            [&](const BoundaryConversionInfo& conversion) {
                return conversion.kind
                    == BoundaryConversionKind::state_domain_alias
                    && conversion.state_domain_changed
                    && !conversion.process
                    && conversion.formal_signal == actual_info.id
                    && conversion.actual_signal == actual_info.id
                    && conversion.formal_domain
                    == actual_hir_type->domain
                    && conversion.actual_domain
                    == actual_info.source_domain;
            });
    // A VHDL actual may share a signal that crossed a
    // state-domain boundary higher in the hierarchy. Keep its
    // selected HIR domain only when that alias is recorded.
    const auto actual_domain = has_semantic_actual_domain
        ? actual_hir_type->domain
        : actual_info.source_domain;
    const bool actual_domain_is_supported
        = !has_semantic_actual_domain
        || actual_hir_type->domain == actual_info.source_domain
        || has_semantic_state_domain_alias;
    if (!actual_domain_is_supported
        || formal_domain != actual_domain) {
        report(
            "FSIM-ELAB-BIND-019",
            "value-domain mismatch on '" + child_path
                + "." + formal_declaration.name + "'",
            connection_source);
        incompatible_alias = true;
    }
    const bool formal_signed = (formal_type
                                       ? formal_type->is_signed
                                       : formal_declaration.subtype->signed_value)
        || formal_domain == frontend::ValueDomain::Integer;
    const auto formal_width = formal_layout
        ? formal_layout->width
        : compiled_vhdl_signal_width(
              *formal_declaration.subtype)
              .value_or(0U);
    if (formal_width > 1U && actual_info.width > 1U
        && formal_signed != actual_info.is_signed) {
        report(
            "FSIM-ELAB-BIND-021",
            "signedness mismatch on '" + child_path
                + "." + formal_declaration.name + "': "
                + (formal_signed ? "signed" : "unsigned")
                + " formal versus "
                + (actual_info.is_signed
                        ? "signed"
                        : "unsigned")
                + " actual '" + actual_info.name + "'",
            connection_source);
        incompatible_alias = true;
    }
    if (formal_domain == frontend::ValueDomain::Integer
        && actual_info.source_domain
            == frontend::ValueDomain::Integer) {
        const auto formal_integer_range
            = executable_integer_range(
                formal_domain,
                formal_width,
                formal_type
                    ? formal_type->integer_range
                    : compiled_integer_range(
                          *formal_declaration.subtype));
        const auto actual_integer_range
            = executable_integer_range(
                actual_info.source_domain,
                actual_info.width,
                actual_info.integer_range);
        if (!formal_integer_range
            || !actual_integer_range
            || !integer_alias_range_safe(
                *formal_integer_range,
                *actual_integer_range,
                direction)) {
            report(
                "FSIM-ELAB-BIND-051",
                "integer subtype ranges on boundary '"
                    + child_path + "."
                    + formal_declaration.name
                    + "' are not range-safe for a direct "
                      "alias",
                connection_source);
            incompatible_alias = true;
        }
    }
    bool array_direction_mismatch { };
    const auto formal_subtype
        = compiled_vhdl_link_subtype(
            *child_interface_specialization,
            *formal_declaration.subtype,
            formal_declaration.scope);
    std::optional<bool> array_shapes_match;
    if (formal_type && actual_hir_type
        && array_type(*formal_type)
        && array_type(*actual_hir_type)
        && actual_hir_subtype
        && !formal_subtype.unconstrained
        && !formal_subtype.constraints.empty()) {
        array_shapes_match
            = semantic::CompiledDesignResolver {
                  *child_interface_specialization
              }
                  .vhdl_array_shapes_match(formal_subtype, formal_declaration.scope, *actual_hir_subtype, actual_hir_scope.value_or(expression->vhdl->scope));
        if (array_shapes_match && !*array_shapes_match) {
            report(
                "FSIM-ELAB-BIND-031",
                "array shape mismatch on '"
                    + child_path + "."
                    + formal_declaration.name + "'",
                connection_source);
            array_direction_mismatch = true;
            incompatible_alias = true;
        }
    }
    const auto formal_array = formal_type
            && formal_type->vhdl_array
        ? formal_type->vhdl_array
        : nullptr;
    const auto actual_array = actual_hir_type
            && actual_hir_type->vhdl_array
        ? actual_hir_type->vhdl_array
        : actual_info.vhdl_array;
    if (!array_shapes_match
        && !array_direction_mismatch
        && formal_array && actual_array
        && formal_array->dimensions.size()
            != actual_array->dimensions.size()) {
        report(
            "FSIM-ELAB-BIND-031",
            "array rank mismatch on '" + child_path + "."
                + formal_declaration.name + "'",
            connection_source);
        array_direction_mismatch = true;
        incompatible_alias = true;
    } else if (!array_shapes_match
        && !array_direction_mismatch
        && formal_array && actual_array) {
        for (std::size_t dimension { };
            dimension < formal_array->dimensions.size();
            ++dimension) {
            const auto& formal_dimension
                = formal_array->dimensions[dimension];
            const auto& actual_dimension
                = actual_array->dimensions[dimension];
            if (formal_dimension.range
                && actual_dimension.range
                && formal_dimension.range->descending
                    != actual_dimension.range->descending) {
                report(
                    "FSIM-ELAB-BIND-031",
                    "array direction mismatch on '"
                        + child_path + "."
                        + formal_declaration.name + "'",
                    connection_source);
                array_direction_mismatch = true;
                incompatible_alias = true;
                break;
            }
        }
    }
    auto formal_range = formal_type
        ? formal_type->packed_range
        : std::nullopt;
    if (!formal_range
        && !formal_declaration.subtype
            ->constraints.empty()) {
        const auto& constraint
            = formal_declaration.subtype
                  ->constraints.front();
        const auto left = constraint.left_expression
            ? child_interface_specialization
                  ->evaluate_integral_expression(
                      *constraint.left_expression)
            : constraint.left;
        const auto right = constraint.right_expression
            ? child_interface_specialization
                  ->evaluate_integral_expression(
                      *constraint.right_expression)
            : constraint.right;
        if (left && right) {
            formal_range = frontend::PackedRange {
                *left, *right, constraint.descending
            };
        }
    }
    if (!array_shapes_match
        && !array_direction_mismatch && formal_range
        && actual_info.packed_range
        && formal_range->descending
            != actual_info.packed_range->descending) {
        report(
            "FSIM-ELAB-BIND-031",
            "array direction mismatch on '" + child_path
                + "." + formal_declaration.name + "'",
            connection_source);
        incompatible_alias = true;
    }
    if (incompatible_alias) {
        return false;
    }
    return true;
}

namespace {

    void append_compiled_vhdl_component_binding_identity(
        const semantic::CompiledDesign& compiled,
        const semantic::vhdl::Declaration* component,
        const semantic::vhdl::Unit* target_entity,
        const std::vector<semantic::SpecializedHirActualIdentity>&
            child_actuals,
        const semantic::SpecializedHirAssociationResult& port_bindings,
        const semantic::vhdl::Instance& record,
        const semantic::CompiledUnitView& child,
        std::string& child_component_identity,
        const std::string& child_configuration_identity)
    {
        const auto declaration_source_identity = [&](
                                                     const semantic::SourceSpanId source) {
            const auto* physical = compiled_physical_source(
                compiled, source);
            const auto& spans = compiled.semantics.source_spans();
            const auto offset = source.valid()
                    && source.value() < spans.size()
                ? spans[source.value()].begin.offset
                : 0U;
            return (physical != nullptr ? *physical : std::string { })
                + ":" + std::to_string(offset);
        };
        const auto subtype_source_identity = [&](
                                                 const semantic::vhdl::SubtypeIndication& subtype) {
            auto source = subtype.type_mark.source;
            if (subtype.type_mark.target.valid()) {
                const auto type = compiled.find_type(
                    subtype.type_mark.target);
                if (type && type->vhdl != nullptr) {
                    source = type->vhdl->source;
                }
            }
            return declaration_source_identity(source);
        };
        const auto owner_scope = compiled_semantic_scope(
            compiled, component->scope);
        const auto* owner_unit = owner_scope != nullptr
            ? compiled_vhdl_unit(compiled, owner_scope->unit)
            : nullptr;
        unsigned region { };
        if (owner_unit != nullptr) {
            if (owner_unit->kind
                == semantic::vhdl::UnitKind::entity) {
                region = 1U;
            } else if (owner_unit->kind
                == semantic::vhdl::UnitKind::package) {
                region = 2U;
            } else if (component->scope != owner_unit->scope) {
                region = component->component->scope_path.find('[')
                        != std::string::npos
                    ? 4U
                    : 3U;
            }
        }
        std::size_t declaration_order { };
        if (owner_unit != nullptr) {
            const auto found = std::ranges::find(
                owner_unit->declarations, component->id);
            if (found != owner_unit->declarations.end()) {
                declaration_order = static_cast<std::size_t>(
                    std::distance(
                        owner_unit->declarations.begin(), found));
            }
        }
        auto owner_library = std::string_view {
            component->component->owner_library
        };
        if (owner_library.empty()) {
            owner_library = owner_unit != nullptr
                ? compiled_vhdl_library(*owner_unit)
                : std::string_view { "work" };
        }
        auto owner_name = std::string_view {
            component->component->owner_name
        };
        if (owner_name.empty() && owner_unit != nullptr) {
            owner_name = owner_unit->name;
        }
        const auto selected_component_binding_identity
            = child_component_identity;
        std::ostringstream component_identity_stream;
        component_identity_stream
            << "vhdl-component-binding-v7;name="
            << component->name
            << ";region=" << region
            << ";scope=" << component->component->scope_path
            << ";owner="
            << owner_library << '.' << owner_name
            << ";order=" << declaration_order
            << ";source="
            << declaration_source_identity(component->source)
            << ";target=" << unit_identity(child);
        const auto append_formal_identity = [&](
                                                const std::string_view prefix,
                                                const semantic::DeclarationId formal_id) {
            const auto formal = compiled.find_declaration(formal_id);
            if (!formal || formal->vhdl == nullptr) {
                return;
            }
            component_identity_stream
                << prefix
                << formal->vhdl->name
                << ":profile="
                << static_cast<unsigned>(formal->vhdl->form);
            if (formal->vhdl->subtype) {
                const auto source = subtype_source_identity(
                    *formal->vhdl->subtype);
                component_identity_stream
                    << ";nominal=" << source
                    << ":type-source=" << source;
            }
        };
        for (const auto formal_id :
            component->component->generics) {
            append_formal_identity(";generic=", formal_id);
        }
        for (const auto formal_id : component->component->ports) {
            append_formal_identity(";port=", formal_id);
        }
        std::vector<semantic::DeclarationId> target_generics;
        std::vector<semantic::DeclarationId> target_ports;
        for (const auto declaration_id : target_entity->declarations) {
            const auto declaration = compiled.find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                continue;
            }
            using Form = semantic::vhdl::DeclarationForm;
            const auto form = declaration->vhdl->form;
            if (form == Form::port) {
                target_ports.push_back(declaration_id);
            } else if (form == Form::generic_constant
                || form == Form::generic_type
                || form == Form::generic_function
                || form == Form::generic_procedure
                || form == Form::generic_package) {
                target_generics.push_back(declaration_id);
            }
        }
        for (const auto& actual : child_actuals) {
            const auto target = std::ranges::find(
                target_generics, actual.declaration);
            if (target == target_generics.end()) {
                continue;
            }
            const auto index = static_cast<std::size_t>(
                std::distance(target_generics.begin(), target));
            if (index >= component->component->generics.size()) {
                continue;
            }
            const auto formal = compiled.find_declaration(
                component->component->generics[index]);
            if (formal && formal->vhdl != nullptr) {
                component_identity_stream
                    << ";actual=" << formal->vhdl->name
                    << ':' << actual.identity;
            }
        }
        for (const auto& binding : port_bindings.bindings) {
            const auto target = std::ranges::find(
                target_ports, binding.formal);
            if (target == target_ports.end()) {
                continue;
            }
            const auto index = static_cast<std::size_t>(
                std::distance(target_ports.begin(), target));
            if (index >= component->component->ports.size()) {
                continue;
            }
            const auto formal = compiled.find_declaration(
                component->component->ports[index]);
            const auto target_formal = compiled.find_declaration(
                binding.formal);
            if (!formal || formal->vhdl == nullptr) {
                continue;
            }
            auto state = binding.kind
                    == semantic::SpecializedHirAssociationKind::open
                ? 2U
                : binding.kind
                    == semantic::SpecializedHirAssociationKind::
                        default_value
                ? 1U
                : 0U;
            if (state == 0U && binding.expression
                && formal->vhdl->initializer
                && *binding.expression
                    == *formal->vhdl->initializer) {
                state = 1U;
            }
            if (state == 0U && formal->vhdl->initializer) {
                const auto named_actual = std::ranges::find_if(
                    record.port_map,
                    [&](const semantic::vhdl::Association& association) {
                        return association.formal
                            && compiled_vhdl_name_equal(
                                association.formal->spelling,
                                formal->vhdl->name);
                    });
                std::size_t positional_index { };
                const semantic::vhdl::Association*
                    positional_actual = nullptr;
                for (const auto& association : record.port_map) {
                    if (association.formal) {
                        continue;
                    }
                    if (positional_index++ == index) {
                        positional_actual = &association;
                        break;
                    }
                }
                const auto* explicit_actual
                    = named_actual != record.port_map.end()
                    ? &*named_actual
                    : positional_actual;
                if (explicit_actual == nullptr
                    || explicit_actual->kind
                        != semantic::vhdl::AssociationKind::expression) {
                    state = 1U;
                }
            }
            if (target_formal
                && target_formal->vhdl != nullptr) {
                component_identity_stream
                    << ";mapped-port="
                    << target_formal->vhdl->name
                    << ":state=" << state;
            }
        }
        std::vector<const semantic::vhdl::Association*>
            positional_port_actuals;
        for (const auto& association : record.port_map) {
            if (!association.formal) {
                positional_port_actuals.push_back(&association);
            }
        }
        for (std::size_t index { };
            index < component->component->ports.size();
            ++index) {
            const auto formal = compiled.find_declaration(
                component->component->ports[index]);
            if (!formal || formal->vhdl == nullptr) {
                continue;
            }
            const semantic::vhdl::Association* actual = nullptr;
            const auto named = std::ranges::find_if(
                record.port_map,
                [&](const semantic::vhdl::Association& association) {
                    return association.formal
                        && compiled_vhdl_name_equal(
                            association.formal->spelling,
                            formal->vhdl->name);
                });
            if (named != record.port_map.end()) {
                actual = &*named;
            } else if (index < positional_port_actuals.size()) {
                actual = positional_port_actuals[index];
            }
            const auto defaulted
                = formal->vhdl->initializer.has_value();
            const auto state = actual != nullptr
                    && actual->kind
                        == semantic::vhdl::AssociationKind::expression
                ? 0U
                : defaulted ? 2U
                            : 1U;
            if (state != 0U) {
                component_identity_stream
                    << ";mapped-port=" << formal->vhdl->name
                    << ":state=" << state;
            }
        }
        if (!selected_component_binding_identity.empty()) {
            component_identity_stream
                << ";configuration="
                << selected_component_binding_identity;
        } else if (!child_configuration_identity.empty()) {
            component_identity_stream
                << ";configuration="
                << child_configuration_identity;
        }
        child_component_identity = component_identity_stream.str();
    }

} // namespace

bool HierarchyBuilder::instantiate_compiled_vhdl_child(
    CompiledVhdlChildActivationContext context)
{
    const auto* saved_configuration
        = active_compiled_vhdl_configuration_;
    auto saved_configuration_identity
        = active_compiled_vhdl_configuration_identity_;
    auto saved_configuration_source
        = active_compiled_vhdl_configuration_source_;
    auto saved_component_identity
        = active_compiled_vhdl_component_identity_;
    auto saved_component_source
        = active_compiled_vhdl_component_source_;
    auto saved_component_declaration_source
        = active_compiled_vhdl_component_declaration_source_;
    active_compiled_vhdl_configuration_ = context.configuration;
    active_compiled_vhdl_configuration_identity_
        = std::move(context.configuration_identity);
    active_compiled_vhdl_configuration_source_
        = context.configuration_source;
    active_compiled_vhdl_component_identity_
        = std::move(context.component_identity);
    active_compiled_vhdl_component_source_
        = context.component_source;
    active_compiled_vhdl_component_declaration_source_
        = context.component_declaration_source;
    const auto instantiated = instantiate_compiled_vhdl_unit(
        CompiledVhdlInstantiationContext {
            .unit = context.child,
            .path = context.path,
            .actuals = std::move(context.actuals),
            .port_aliases = std::move(context.port_aliases),
            .source_instance = context.source_instance,
            .prepared_specialization
            = std::move(context.prepared_specialization),
            .vhdl_types_validated = context.vhdl_types_validated,
        });
    active_compiled_vhdl_configuration_ = saved_configuration;
    active_compiled_vhdl_configuration_identity_
        = std::move(saved_configuration_identity);
    active_compiled_vhdl_configuration_source_
        = saved_configuration_source;
    active_compiled_vhdl_component_identity_
        = std::move(saved_component_identity);
    active_compiled_vhdl_component_source_
        = saved_component_source;
    active_compiled_vhdl_component_declaration_source_
        = saved_component_declaration_source;
    return instantiated;
}

HierarchyBuilder::CompiledVhdlSystemVerilogParametersResult
HierarchyBuilder::prepare_compiled_vhdl_systemverilog_parameters(
    CompiledVhdlSystemVerilogParametersContext context)
{
    const auto& child_unit = context.child_unit;
    const auto& record = context.record;
    const auto& effective_instance = context.effective_instance;
    const auto& working_specialization = context.working_specialization;
    const auto& child_path = context.child_path;
    CompiledVhdlSystemVerilogParametersResult result;
    for (const auto declaration_id : child_unit.declarations) {
        const auto declaration = compiled_->find_declaration(
            declaration_id);
        if (!declaration
            || declaration->systemverilog == nullptr
            || declaration->systemverilog->form
                != semantic::sv::DeclarationForm::type_parameter) {
            continue;
        }
        report(
            "FSIM-ELAB-SVTYPEPARAM-004",
            "type parameter '"
                + declaration->systemverilog->name
                + "' requires a same-language SystemVerilog "
                  "data-type actual",
            compiled_source_span(*compiled_, record.source));
    }
    auto parameter_bindings
        = semantic::resolve_specialized_hir_associations(
            *compiled_, child_unit.id,
            semantic::CompiledInstanceView {
                nullptr, &effective_instance },
            semantic::SpecializedHirAssociationSurface::
                parameters,
            &working_specialization);
    if (!parameter_bindings
        && parameter_bindings.issues.empty()) {
        report(
            "FSIM-ELAB-HIR-001",
            "mixed-language parameter association for '"
                + child_path + "' is unsupported: "
                + parameter_bindings.error,
            compiled_source_span(*compiled_,
                parameter_bindings.error_source.valid()
                    ? parameter_bindings.error_source
                    : record.source));
        return result;
    }
    for (const auto& issue : parameter_bindings.issues) {
        report(
            systemverilog_parameter_diagnostic_code(
                issue.diagnostic),
            issue.message,
            compiled_source_span(*compiled_,
                issue.source.valid()
                    ? issue.source
                    : record.source));
    }
    std::vector<semantic::SpecializedHirActualIdentity>
        child_actuals;
    for (const auto& association :
        parameter_bindings.bindings) {
        if (association.kind
                == semantic::
                    SpecializedHirAssociationKind::open
            || (association.kind
                    == semantic::
                        SpecializedHirAssociationKind::
                            default_value
                && !association.actual_declaration)) {
            continue;
        }
        if (association.kind
                == semantic::
                    SpecializedHirAssociationKind::expression
            && association.expression) {
            const auto formal = compiled_->find_declaration(
                association.formal);
            const auto formal_string = formal
                && formal->systemverilog != nullptr
                && compiled_systemverilog_string_declaration(
                    *formal->systemverilog);
            const auto actual_string
                = working_specialization
                      .evaluate_string_expression(
                          *association.expression)
                      .has_value();
            if (formal_string != actual_string
                && (formal_string || actual_string)) {
                report(
                    "FSIM-ELAB-SVSTRING-002",
                    "mixed-language string and integral "
                    "parameter actuals are not assignment "
                    "compatible",
                    compiled_source_span(
                        *compiled_, association.source));
                continue;
            }
        }
        if (association.identity.empty()) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language parameter association for '"
                    + child_path
                    + "' has no deterministic identity",
                compiled_source_span(
                    *compiled_, association.source));
            return result;
        }
        auto identity_value = association.identity;
        const auto formal = compiled_->find_declaration(
            association.formal);
        if (formal && formal->systemverilog != nullptr
            && association.expression) {
            const auto string_value = working_specialization
                                          .evaluate_string_expression(
                                              *association.expression);
            if (compiled_systemverilog_string_declaration(
                    *formal->systemverilog)
                && string_value) {
                identity_value
                    = semantic::systemverilog_string_identity(
                        *string_value);
            } else {
                std::string constant_error;
                auto constant = evaluate_hir_systemverilog_constant(
                    working_specialization,
                    *association.expression,
                    constant_error);
                if (constant) {
                    identity_value = constant->canonical();
                } else if (const auto value
                    = working_specialization
                        .evaluate_integral_expression(
                            *association.expression)) {
                    identity_value
                        = compiled_systemverilog_integral_identity(
                            *formal->systemverilog,
                            std::to_string(*value));
                }
            }
        }
        child_actuals.push_back({ association.formal,
            std::move(identity_value),
            association.actual_declaration,
            association.expression,
            association.systemverilog_type,
            association.vhdl_type });
    }

    std::vector<CompiledSpecializationFailure>
        child_specialization_failures;
    auto child_specialization = compiled_specialization(
        validated_compiled_, child_unit.id, child_actuals,
        &child_specialization_failures);
    for (const auto& failure : child_specialization_failures) {
        report(
            failure.code,
            failure.message,
            compiled_source_span(*compiled_, failure.source));
    }
    if (!child_specialization) {
        report(
            "FSIM-ELAB-HIR-001",
            "cannot construct a mixed-language SystemVerilog "
            "specialization for '"
                + child_path + "'",
            compiled_source_span(*compiled_, record.source));
        return result;
    }
    result.status
        = CompiledVhdlSystemVerilogParametersResult::Status::ready;
    result.child_actuals = std::move(child_actuals);
    result.child_specialization = std::move(child_specialization);
    return result;
}

bool HierarchyBuilder::instantiate_compiled_vhdl_systemc_child(
    const semantic::vhdl::Instance& effective_instance,
    const semantic::SpecializedHirUnit& working_specialization,
    const std::string& child_path,
    std::string_view working_path,
    const std::string& selected_systemc_target,
    const Binding* external_binding,
    const SignalMap& working_signals)
{
    const auto* description = construct_systemc_description(
        effective_instance, working_specialization,
        child_path, selected_systemc_target);
    if (description == nullptr) {
        return false;
    }
    const auto diagnostics_before = diagnostics_.size();
    SignalMap child_aliases;
    ObjectMap child_objects;
    std::vector<bool> connected(description->ports.size());
    std::size_t next_positional { };
    bool saw_named { };
    bool saw_positional { };
    for (const auto& association :
        effective_instance.port_map) {
        if (association.kind
                == semantic::vhdl::AssociationKind::open
            || association.kind
                == semantic::vhdl::AssociationKind::
                    default_box) {
            continue;
        }
        const auto source = compiled_source_span(
            *compiled_, association.source);
        if (association.kind
                != semantic::vhdl::AssociationKind::expression
            || !association.expression) {
            report(
                "FSIM-ELAB-HIR-001",
                "SystemC port association requires a "
                "compiled-HIR signal-name actual",
                source);
            continue;
        }
        std::optional<std::size_t> port_index;
        if (association.formal) {
            saw_named = true;
            const auto found = std::ranges::find_if(
                description->ports,
                [&](const auto& external_port) {
                    return compiled_vhdl_name_equal(
                        external_port.name,
                        association.formal->spelling);
                });
            if (found == description->ports.end()) {
                report(
                    "FSIM-ELAB-BIND-004",
                    "unknown SystemC port '"
                        + association.formal->spelling + "'",
                    source);
                continue;
            }
            port_index = static_cast<std::size_t>(
                std::distance(
                    description->ports.begin(), found));
        } else {
            saw_positional = true;
            while (next_positional < connected.size()
                && connected[next_positional]) {
                ++next_positional;
            }
            if (next_positional >= connected.size()) {
                report(
                    "FSIM-ELAB-BIND-004",
                    "too many positional SystemC port "
                    "associations",
                    source);
                continue;
            }
            port_index = next_positional++;
        }
        if (connected[*port_index]) {
            report(
                "FSIM-ELAB-BIND-004",
                "duplicate SystemC port association for '"
                    + description->ports[*port_index].name
                    + "'",
                source);
            continue;
        }
        connected[*port_index] = true;

        const auto expression
            = working_specialization.find_expression(
                *association.expression);
        std::string actual_name;
        if (expression && expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::name) {
            actual_name = expression->vhdl->text;
            if (expression->vhdl->referenced_name
                && expression->vhdl->referenced_name
                    ->selected) {
                const auto declaration
                    = working_specialization.find_declaration(
                        *expression->vhdl->referenced_name
                            ->selected);
                if (declaration
                    && declaration->vhdl != nullptr) {
                    actual_name
                        = declaration->vhdl->name;
                }
            }
        }
        const auto actual = find_compiled_vhdl_signal(working_signals, actual_name);
        if (!actual) {
            report(
                "FSIM-ELAB-HIR-001",
                "SystemC port actual '" + actual_name
                    + "' is not a signal in '"
                    + std::string { working_path } + "'",
                source);
            continue;
        }

        const auto& external_port
            = description->ports[*port_index];
        const auto external_width
            = external_port.type.width();
        if (!external_width || *external_width == 0U) {
            report(
                "FSIM-ELAB-BIND-019",
                "SystemC port '" + external_port.name
                    + "' has no executable scalar layout",
                source);
            continue;
        }
        CompiledBoundaryPort port;
        port.name = external_port.name;
        port.type_name = external_port.type.spelling;
        port.width = static_cast<std::size_t>(
            *external_width);
        port.domain = external_port.type.domain;
        port.systemverilog_scalar
            = external_port.type.systemverilog_scalar;
        port.signed_value = external_port.type.is_signed;
        port.packed_range
            = external_port.type.packed_range;
        port.integer_range
            = external_port.type.integer_range;
        port.direction = external_port.direction;
        port.declaration_source = source;
        port.initial = PackedLogic4 {
            port.width,
            is_two_state_domain(port.domain)
                ? Logic4::zero
                : Logic4::x,
        };
        const auto formal = connect_compiled_boundary_port(
            port, *actual, child_path, source,
            external_binding);
        if (!formal) {
            continue;
        }
        child_aliases.emplace(external_port.name, *formal);
        child_objects.emplace(
            external_port.handle, *formal);
    }
    if (saw_named && saw_positional) {
        report(
            "FSIM-ELAB-BIND-004",
            "named and positional SystemC port associations "
            "cannot be mixed",
            compiled_source_span(*compiled_, effective_instance.source));
    }
    if (diagnostics_.size() != diagnostics_before) {
        return false;
    }
    instantiate_systemc(
        *description, child_path, std::move(child_aliases),
        std::move(child_objects));
    return true;
}

bool HierarchyBuilder::compiled_vhdl_component_subtype_compatible(
    const semantic::vhdl::Declaration& candidate,
    const semantic::vhdl::Declaration& entity_formal,
    const semantic::vhdl::ComponentProfile& component_profile,
    const bool profile_interface,
    const semantic::vhdl::Unit* target_entity,
    const semantic::SpecializedHirUnit& working_specialization) const
{
    using DeclarationForm
        = semantic::vhdl::DeclarationForm;
    const auto candidate_form = candidate.form;
    const auto entity_form = entity_formal.form;
    const auto callable_form
        = candidate_form == DeclarationForm::generic_function
        || candidate_form
            == DeclarationForm::generic_procedure;
    if (callable_form
        || entity_form == DeclarationForm::generic_function
        || entity_form
            == DeclarationForm::generic_procedure) {
        if (candidate_form != entity_form
            || !candidate.callable
            || !entity_formal.callable
            || candidate.callable->function
                != entity_formal.callable->function
            || candidate.callable->formals.size()
                != entity_formal.callable->formals.size()) {
            return false;
        }
        const auto generic_type_name = [&](
                                           const auto& ids,
                                           const std::string_view name) {
            return std::ranges::any_of(
                ids, [&](const auto id) {
                    const auto declaration
                        = compiled_->find_declaration(id);
                    return declaration
                        && declaration->vhdl != nullptr
                        && declaration->vhdl->form
                        == DeclarationForm::generic_type
                        && compiled_vhdl_name_equal(
                            declaration->vhdl->name, name);
                });
        };
        const auto profile_subtype_matches = [&](
                                                 const std::optional<semantic::vhdl::
                                                         SubtypeIndication>& left,
                                                 const std::optional<semantic::vhdl::
                                                         SubtypeIndication>& right) {
            if (!left || !right) {
                return left.has_value()
                    == right.has_value();
            }
            if (left->domain
                    != semantic::vhdl::ValueDomain::unknown
                && right->domain
                    != semantic::vhdl::ValueDomain::unknown
                && left->domain != right->domain) {
                return false;
            }
            if (generic_type_name(
                    component_profile.generics,
                    left->type_mark.spelling)
                && generic_type_name(
                    target_entity->declarations,
                    right->type_mark.spelling)) {
                return true;
            }
            return left->type_mark.target.valid()
                && right->type_mark.target.valid()
                && left->type_mark.target
                == right->type_mark.target;
        };
        if (!profile_subtype_matches(
                candidate.callable->return_type,
                entity_formal.callable->return_type)) {
            return false;
        }
        for (std::size_t index { };
            index < candidate.callable->formals.size();
            ++index) {
            const auto left = compiled_->find_declaration(
                candidate.callable->formals[index]);
            const auto right = compiled_->find_declaration(
                entity_formal.callable->formals[index]);
            if (!left || left->vhdl == nullptr
                || !right || right->vhdl == nullptr
                || left->vhdl->direction
                    != right->vhdl->direction
                || !profile_subtype_matches(
                    left->vhdl->subtype,
                    right->vhdl->subtype)) {
                return false;
            }
        }
        return true;
    }
    if (candidate_form
            == DeclarationForm::generic_package
        || entity_form
            == DeclarationForm::generic_package) {
        if (candidate_form != entity_form
            || !candidate.package
            || !entity_formal.package) {
            return false;
        }
        const auto& candidate_template
            = candidate.package->template_name;
        const auto& entity_template
            = entity_formal.package->template_name;
        if (candidate_template.selected
            && entity_template.selected) {
            return *candidate_template.selected
                == *entity_template.selected;
        }
        return compiled_vhdl_name_equal(
            compiled_vhdl_simple_name(
                candidate_template.canonical.empty()
                    ? candidate_template.spelling
                    : candidate_template.canonical),
            compiled_vhdl_simple_name(
                entity_template.canonical.empty()
                    ? entity_template.spelling
                    : entity_template.canonical));
    }
    if (candidate_form == DeclarationForm::generic_type
        || entity_form == DeclarationForm::generic_type) {
        return candidate_form == entity_form;
    }
    if (!candidate.subtype || !entity_formal.subtype) {
        return candidate.subtype.has_value()
            == entity_formal.subtype.has_value();
    }
    const auto& candidate_subtype = *candidate.subtype;
    const auto& entity_subtype = *entity_formal.subtype;
    if (candidate_subtype.domain
            != semantic::vhdl::ValueDomain::unknown
        && entity_subtype.domain
            != semantic::vhdl::ValueDomain::unknown
        && candidate_subtype.domain
            != entity_subtype.domain) {
        return false;
    }
    const auto candidate_type
        = candidate_subtype.type_mark.target.valid()
        ? working_specialization.find_type(
              candidate_subtype.type_mark.target)
        : std::optional<semantic::CompiledTypeView> { };
    const auto candidate_type_declaration
        = candidate_type
            && candidate_type->vhdl != nullptr
        ? working_specialization.find_declaration(
              candidate_type->vhdl->declaration)
        : std::nullopt;
    auto candidate_is_generic_type
        = candidate.form
            == semantic::vhdl::DeclarationForm::generic_type
        || (candidate_type_declaration
            && candidate_type_declaration->vhdl != nullptr
            && candidate_type_declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::
                    generic_type);
    if (!candidate_is_generic_type) {
        candidate_is_generic_type
            = std::ranges::any_of(
                component_profile.generics,
                [&](const auto declaration_id) {
                    const auto declaration
                        = compiled_->find_declaration(
                            declaration_id);
                    return declaration
                        && declaration->vhdl != nullptr
                        && declaration->vhdl->form
                        == semantic::vhdl::
                            DeclarationForm::
                                generic_type
                        && compiled_vhdl_name_equal(
                            declaration->vhdl->name,
                            candidate_subtype.type_mark
                                .spelling);
                });
    }
    if (candidate_is_generic_type) {
        return true;
    }
    const auto predefined_type = [](const auto& name) {
        const auto simple = compiled_vhdl_simple_name(
            name.spelling);
        constexpr std::array builtins {
            std::string_view { "bit" },
            std::string_view { "boolean" },
            std::string_view { "character" },
            std::string_view { "integer" },
            std::string_view { "natural" },
            std::string_view { "positive" },
            std::string_view { "real" },
            std::string_view { "time" },
            std::string_view { "std_logic" },
            std::string_view { "std_ulogic" },
            std::string_view { "bit_vector" },
            std::string_view { "std_logic_vector" },
            std::string_view { "std_ulogic_vector" },
            std::string_view { "signed" },
            std::string_view { "unsigned" },
            std::string_view { "string" },
        };
        return std::ranges::any_of(
            builtins, [&](const auto builtin) {
                return compiled_vhdl_name_equal(
                    simple, builtin);
            });
    };
    const auto candidate_predefined
        = predefined_type(candidate_subtype.type_mark);
    const auto entity_predefined
        = predefined_type(entity_subtype.type_mark);
    const auto same_predefined_type
        = candidate_predefined && entity_predefined
        && compiled_vhdl_name_equal(
            compiled_vhdl_simple_name(
                candidate_subtype.type_mark.spelling),
            compiled_vhdl_simple_name(
                entity_subtype.type_mark.spelling));
    const auto visible_type_target = [&](
                                         const semantic::vhdl::SubtypeIndication& subtype,
                                         const semantic::ScopeId scope)
        -> std::optional<semantic::TypeId> {
        const auto* semantic_scope
            = compiled_semantic_scope(*compiled_, scope);
        const auto* owner = semantic_scope != nullptr
            ? compiled_vhdl_unit(
                  *compiled_, semantic_scope->unit)
            : nullptr;
        if (owner == nullptr) {
            return std::nullopt;
        }
        std::optional<semantic::TypeId> selected;
        bool ambiguous { };
        const auto consider = [&](
                                  const semantic::vhdl::
                                      Unit& unit) {
            for (const auto declaration_id :
                unit.declarations) {
                const auto declaration
                    = compiled_->find_declaration(
                        declaration_id);
                if (!declaration
                    || declaration->vhdl == nullptr
                    || !declaration->vhdl->declared_type
                    || !compiled_vhdl_name_equal(
                        declaration->vhdl->name,
                        subtype.type_mark.spelling)) {
                    continue;
                }
                if (selected
                    && *selected
                        != *declaration->vhdl
                            ->declared_type) {
                    ambiguous = true;
                    continue;
                }
                selected
                    = declaration->vhdl->declared_type;
            }
        };
        consider(*owner);
        for (const auto& unit :
            compiled_->vhdl_hir.units()) {
            if (unit.kind
                    != semantic::vhdl::UnitKind::package
                || !unit.primary_name.empty()
                || !semantic::CompiledDesignResolver {
                    *compiled_, owner->id,
                    &working_specialization }
                    .vhdl_package_member_visible(*owner, unit, subtype.type_mark.spelling)) {
                continue;
            }
            consider(unit);
        }
        return ambiguous ? std::nullopt : selected;
    };
    auto candidate_nominal = visible_type_target(
        candidate_subtype, candidate.scope);
    auto entity_nominal = visible_type_target(
        entity_subtype, entity_formal.scope);
    if (!candidate_nominal) {
        candidate_nominal
            = semantic::CompiledDesignResolver {
                  working_specialization
              }
                  .resolve_vhdl_named_type(candidate_subtype.type_mark.spelling, candidate.scope);
    }
    if (!entity_nominal) {
        entity_nominal
            = semantic::CompiledDesignResolver {
                  working_specialization
              }
                  .resolve_vhdl_named_type(entity_subtype.type_mark.spelling, entity_formal.scope);
    }
    if (!candidate_nominal
        && candidate_subtype.type_mark.target.valid()) {
        candidate_nominal
            = candidate_subtype.type_mark.target;
    }
    if (!entity_nominal
        && entity_subtype.type_mark.target.valid()) {
        entity_nominal
            = entity_subtype.type_mark.target;
    }
    const auto array_shapes_match
        = semantic::CompiledDesignResolver {
              working_specialization
          }
              .vhdl_array_shapes_match(candidate_subtype, candidate.scope, entity_subtype, entity_formal.scope);
    if (candidate_predefined || entity_predefined) {
        if (!same_predefined_type) {
            return false;
        }
        if (profile_interface) {
            // Component and entity interface constraints may
            // depend on different generic names and defaults.
            // Their base profiles conform here; occurrence
            // shape is checked after the component generic map
            // has specialized the bound entity.
            return true;
        }
    }
    if (!candidate_is_generic_type
        && !same_predefined_type
        && (!candidate_nominal || !entity_nominal
            || *candidate_nominal != *entity_nominal)) {
        return false;
    }
    if (array_shapes_match
        && !*array_shapes_match) {
        return false;
    }
    const auto candidate_layout
        = compiled_vhdl_named_signal_layout(
            working_specialization,
            candidate_subtype,
            candidate.scope);
    const auto entity_layout
        = compiled_vhdl_named_signal_layout(
            working_specialization,
            entity_subtype,
            entity_formal.scope);
    return !candidate_layout || !entity_layout
        || (candidate_layout->width
                == entity_layout->width
            && candidate_layout->domain
                == entity_layout->domain);
}

bool HierarchyBuilder::compiled_vhdl_component_profile_matches(
    const semantic::vhdl::Declaration& candidate,
    const bool match_actuals,
    const std::vector<const semantic::vhdl::Declaration*>& entity_generics,
    const std::vector<const semantic::vhdl::Declaration*>& entity_ports,
    const semantic::vhdl::Instance& record,
    const semantic::vhdl::Unit* target_entity,
    const semantic::SpecializedHirUnit& working_specialization) const
{
    if (!candidate.component) {
        return false;
    }
    const auto match_surface = [&](
                                   const auto& candidate_ids,
                                   const auto& entity_declarations,
                                   const bool ports) {
        if (candidate_ids.size() != entity_declarations.size()) {
            return false;
        }
        for (std::size_t index { };
            index < candidate_ids.size(); ++index) {
            const auto formal = compiled_->find_declaration(
                candidate_ids[index]);
            const auto direction_matches
                = !ports
                || (formal && formal->vhdl != nullptr
                    && formal->vhdl->direction
                        == entity_declarations[index]->direction);
            const auto subtype_matches
                = formal && formal->vhdl != nullptr
                && compiled_vhdl_component_subtype_compatible(
                    *formal->vhdl,
                    *entity_declarations[index],
                    *candidate.component,
                    true, target_entity,
                    working_specialization);
            if (!formal || formal->vhdl == nullptr
                || !direction_matches || !subtype_matches) {
                return false;
            }
        }
        return true;
    };
    if (!match_surface(
            candidate.component->generics,
            entity_generics,
            false)
        || !match_surface(
            candidate.component->ports,
            entity_ports,
            true)) {
        return false;
    }
    if (!match_actuals) {
        return true;
    }

    std::size_t positional { };
    for (const auto& association : record.port_map) {
        if (association.kind
                != semantic::vhdl::AssociationKind::expression
            || !association.expression) {
            continue;
        }
        auto formal_index = positional++;
        if (association.formal) {
            const auto selected = std::ranges::find_if(
                candidate.component->ports,
                [&](const semantic::DeclarationId id) {
                    const auto formal = compiled_->find_declaration(id);
                    return formal && formal->vhdl != nullptr
                        && compiled_vhdl_name_equal(
                            formal->vhdl->name,
                            association.formal->spelling);
                });
            if (selected == candidate.component->ports.end()) {
                return false;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(
                    candidate.component->ports.begin(), selected));
        }
        if (formal_index >= candidate.component->ports.size()) {
            return false;
        }
        const auto expression
            = working_specialization.find_expression(
                *association.expression);
        const auto actual_id = expression
                && expression->vhdl != nullptr
                && expression->vhdl->referenced_name
            ? expression->vhdl->referenced_name->selected
            : std::nullopt;
        const auto actual = actual_id
            ? working_specialization.find_declaration(*actual_id)
            : std::nullopt;
        const auto formal = compiled_->find_declaration(
            candidate.component->ports[formal_index]);
        auto effective_actual = actual && actual->vhdl != nullptr
            ? std::optional<semantic::vhdl::Declaration> {
                  *actual->vhdl
              }
            : std::nullopt;
        if (effective_actual && effective_actual->subtype && expression
            && expression->vhdl != nullptr) {
            const auto separator
                = expression->vhdl->text.find('.');
            if (separator != std::string::npos) {
                auto subtype = compiled_vhdl_link_subtype(
                    working_specialization,
                    *effective_actual->subtype,
                    effective_actual->scope);
                auto type_id = subtype.type_mark.target;
                const semantic::vhdl::TypeDefinition* definition = nullptr;
                std::unordered_set<std::uint32_t> visiting;
                while (type_id.valid()
                    && visiting.insert(type_id.value()).second) {
                    const auto type
                        = working_specialization.find_type(type_id);
                    if (!type || type->vhdl == nullptr) {
                        break;
                    }
                    if (type->vhdl->form
                        == semantic::vhdl::TypeForm::record) {
                        definition = type->vhdl;
                        break;
                    }
                    if ((type->vhdl->form
                                != semantic::vhdl::TypeForm::subtype
                            && type->vhdl->form
                                != semantic::vhdl::TypeForm::alias)
                        || !type->vhdl->base.type_mark.target.valid()) {
                        break;
                    }
                    type_id = type->vhdl->base.type_mark.target;
                }
                if (definition != nullptr) {
                    auto member_name = std::string_view {
                        expression->vhdl->text
                    }
                                           .substr(separator + 1U);
                    if (const auto nested = member_name.find('.');
                        nested != std::string_view::npos) {
                        member_name = member_name.substr(0U, nested);
                    }
                    const auto member = std::ranges::find_if(
                        definition->record_elements,
                        [&](const auto& element) {
                            return compiled_vhdl_name_equal(
                                element.name, member_name);
                        });
                    if (member != definition->record_elements.end()) {
                        effective_actual->subtype = member->subtype;
                    }
                }
            }
        }
        if (effective_actual && effective_actual->subtype && expression
            && expression->vhdl != nullptr
            && !expression->vhdl->nominal_type.empty()) {
            const auto expression_type
                = semantic::CompiledDesignResolver {
                      working_specialization
                  }
                      .resolve_vhdl_named_type(expression->vhdl->nominal_type, expression->vhdl->scope);
            if (expression_type) {
                effective_actual->subtype->type_mark.spelling
                    = expression->vhdl->nominal_type;
                effective_actual->subtype->type_mark.target
                    = *expression_type;
            }
        }
        if (actual && actual->vhdl != nullptr
            && formal && formal->vhdl != nullptr
            && !compiled_vhdl_component_subtype_compatible(
                *formal->vhdl, *effective_actual,
                *candidate.component, false,
                target_entity, working_specialization)) {
            return false;
        }
    }
    return true;
}

HierarchyBuilder::CompiledVhdlComponentCandidateSelection
HierarchyBuilder::select_compiled_vhdl_component_candidate(
    const semantic::vhdl::Unit& architecture,
    const semantic::vhdl::Instance& record,
    const semantic::vhdl::Unit* target_entity,
    const semantic::SpecializedHirUnit& working_specialization)
{
    const auto visible_components
        = collect_visible_compiled_vhdl_components(
            architecture, working_specialization, record.scope,
            record.target.spelling);
    const auto entity_formals = [&](const bool ports) {
        std::vector<const semantic::vhdl::Declaration*> result;
        if (target_entity == nullptr) {
            return result;
        }
        for (const auto declaration_id : target_entity->declarations) {
            const auto declaration
                = compiled_->find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                continue;
            }
            const auto form = declaration->vhdl->form;
            const auto selected = ports
                ? form == semantic::vhdl::DeclarationForm::port
                : form
                        == semantic::vhdl::DeclarationForm::generic_constant
                    || form
                        == semantic::vhdl::DeclarationForm::generic_type
                    || form
                        == semantic::vhdl::DeclarationForm::generic_function
                    || form
                        == semantic::vhdl::DeclarationForm::generic_procedure
                    || form
                        == semantic::vhdl::DeclarationForm::generic_package;
            if (selected) {
                result.push_back(declaration->vhdl);
            }
        }
        return result;
    };
    const auto entity_generics = entity_formals(false);
    const auto entity_ports = entity_formals(true);
    std::vector<const semantic::vhdl::Declaration*> matching;
    std::ranges::copy_if(
        visible_components,
        std::back_inserter(matching),
        [&](const semantic::vhdl::Declaration* candidate) {
            return target_entity == nullptr
                || compiled_vhdl_component_profile_matches(
                    *candidate,
                    visible_components.size() > 1U,
                    entity_generics,
                    entity_ports,
                    record,
                    target_entity,
                    working_specialization);
        });
    if (matching.empty() && visible_components.size() == 1U) {
        const auto* candidate = visible_components.front();
        bool generic_mismatch
            = candidate->component
            && candidate->component->generics.size()
                != entity_generics.size();
        if (!generic_mismatch && candidate->component) {
            for (std::size_t index { };
                index < entity_generics.size(); ++index) {
                const auto formal = compiled_->find_declaration(
                    candidate->component->generics[index]);
                if (!formal || formal->vhdl == nullptr
                    || !compiled_vhdl_component_subtype_compatible(
                        *formal->vhdl,
                        *entity_generics[index],
                        *candidate->component,
                        true,
                        target_entity,
                        working_specialization)) {
                    generic_mismatch = true;
                    break;
                }
            }
        }
        report(
            generic_mismatch
                ? "FSIM-ELAB-VHCOMP-006"
                : "FSIM-ELAB-VHCOMP-007",
            "component '" + candidate->name
                + (generic_mismatch
                        ? "' generic profile is incompatible with entity '"
                        : "' port profile is incompatible with entity '")
                + target_entity->name + "'",
            compiled_source_span(*compiled_, candidate->source));
        return { };
    }
    if (matching.size() == 1U) {
        return {
            CompiledVhdlComponentCandidateSelection::Status::selected,
            matching.front(),
        };
    }
    if (matching.empty() && !visible_components.empty()) {
        report(
            "FSIM-ELAB-VHCOMP-012",
            "component instance '" + record.name
                + "' does not match any visible overload '"
                + record.target.spelling + "'",
            compiled_source_span(*compiled_, record.source));
        return { };
    }
    if (matching.size() > 1U) {
        report(
            "FSIM-ELAB-VHCOMP-002",
            "component instance '" + record.name
                + "' ambiguously matches "
                + std::to_string(matching.size())
                + " visible declarations '" + record.target.spelling + "'",
            compiled_source_span(*compiled_, record.source));
        return { };
    }
    report(
        "FSIM-ELAB-VHCOMP-001",
        "component instance '" + record.name
            + "' has no visible component declaration for '"
            + record.target.spelling + "'",
        compiled_source_span(*compiled_, record.source));
    return { };
}

HierarchyBuilder::CompiledVhdlSystemVerilogPortMappingResult
HierarchyBuilder::materialize_compiled_vhdl_systemverilog_port_mappings(
    CompiledVhdlSystemVerilogPortMappingContext context)
{
    CompiledVhdlSystemVerilogPortMappingResult result;
    auto& child_aliases = result.signal_aliases;
    auto& child_container_aliases = result.container_aliases;
    bool boundary_ports_valid { true };
    for (const auto& association : context.bindings.bindings) {
        const auto formal
            = context.child_specialization.find_declaration(
                association.formal);
        if (!formal || formal->systemverilog == nullptr
            || formal->systemverilog->form
                != semantic::sv::DeclarationForm::port) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language port association for '"
                    + context.child_path
                    + "' has no SystemVerilog port declaration",
                compiled_source_span(*compiled_, association.source));
            return result;
        }
        const auto& formal_declaration = *formal->systemverilog;
        if (association.kind
                == semantic::SpecializedHirAssociationKind::open
            || association.kind
                == semantic::SpecializedHirAssociationKind::default_value) {
            // The SystemVerilog child owns the initialization of an
            // unconnected input; no cross-language adapter is needed.
            continue;
        }
        if (association.kind
                != semantic::SpecializedHirAssociationKind::expression
            || !association.expression) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language port '" + formal_declaration.name
                    + "' requires a signal-name actual",
                compiled_source_span(*compiled_, association.source));
            return result;
        }
        if (compiled_systemverilog_string_declaration(
                formal_declaration)) {
            report(
                "FSIM-ELAB-HIR-001",
                "mutable SystemVerilog string port '"
                    + formal_declaration.name
                    + "' has no compatible VHDL string object",
                compiled_source_span(*compiled_, association.source));
            return result;
        }
        const auto expression
            = context.parent_specialization.find_expression(
                *association.expression);
        std::string actual_name;
        if (expression && expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::name) {
            actual_name = expression->vhdl->text;
            if (expression->vhdl->referenced_name
                && expression->vhdl->referenced_name->selected) {
                const auto declaration
                    = context.parent_specialization.find_declaration(
                        *expression->vhdl->referenced_name->selected);
                if (declaration && declaration->vhdl != nullptr) {
                    actual_name = declaration->vhdl->name;
                }
            }
        }
        const auto actual = find_compiled_vhdl_signal(
            context.parent_signals, actual_name);
        if (!actual) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language port actual '" + actual_name
                    + "' is not a signal in '"
                    + context.parent_path + "'",
                compiled_source_span(*compiled_, association.source));
            return result;
        }
        const auto& actual_info = design_.signal_info_.at(*actual);
        if (!actual_info.enumeration_literals.empty()) {
            report(
                "FSIM-ELAB-BIND-052",
                "a nominal VHDL enumeration cannot cross a "
                "SystemVerilog scalar boundary",
                compiled_source_span(*compiled_, association.source));
            boundary_ports_valid = false;
            continue;
        }
        if (formal_declaration.type
            && formal_declaration.type->container_form) {
            const auto expected = compiled_container_bridge_type(
                context.child_specialization, *formal_declaration.type);
            const auto formal_shape = expected
                ? compiled_container_bridge_shape(*expected)
                : std::nullopt;
            const auto actual_shape
                = compiled_container_bridge_signal_shape(actual_info);
            if (!expected || !formal_shape || !actual_shape
                || *formal_shape != *actual_shape) {
                report(
                    "FSIM-ELAB-SVPORT-004",
                    "cross-language SystemVerilog container port '"
                        + context.child_path + "."
                        + formal_declaration.name
                        + "' requires identical fixed dimensions, "
                          "aggregate member grouping, leaf widths, "
                          "and state domains",
                    compiled_source_span(*compiled_, association.source));
                boundary_ports_valid = false;
                continue;
            }
            const auto direction = compiled_port_direction(
                formal_declaration.direction);
            const auto writable
                = direction == frontend::PortDirection::Output
                || direction == frontend::PortDirection::Buffer
                || direction == frontend::PortDirection::Inout;
            const auto readable
                = direction == frontend::PortDirection::Input
                || direction == frontend::PortDirection::Inout;
            if (!readable && !writable) {
                report(
                    "FSIM-ELAB-SVPORT-004",
                    "cross-language SystemVerilog container port '"
                        + context.child_path + "."
                        + formal_declaration.name
                        + "' has an unsupported ownership direction",
                    compiled_source_span(*compiled_, association.source));
                boundary_ports_valid = false;
                continue;
            }
            const auto index = design_.container_objects_.size();
            const auto object = static_cast<ContainerObjectId>(index);
            if (static_cast<std::size_t>(object) != index) {
                throw std::length_error(
                    "too many elaborated container objects");
            }
            const auto full_name = context.child_path + "."
                + formal_declaration.name;
            design_.container_object_info_.push_back(
                ContainerObjectInfo {
                    object,
                    full_name,
                    *expected,
                    compiled_source_span(*compiled_, association.source),
                    true,
                    direction,
                    std::nullopt,
                });
            design_.container_objects_.push_back(
                ContainerObject {
                    full_name,
                    default_container_value(*expected),
                    std::nullopt,
                });
            design_.container_signal_aliases_.push_back(
                runtime::simir::ContainerSignalAlias {
                    object,
                    *actual,
                    readable,
                    writable,
                });
            if (!child_container_aliases.emplace(
                                            formal_declaration.name, object)
                    .second) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "duplicate mixed-language container port binding for '"
                        + formal_declaration.name + "'",
                    compiled_source_span(*compiled_, association.source));
                return result;
            }
            continue;
        }
        if (!formal_declaration.type) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language SystemVerilog port '"
                    + formal_declaration.name
                    + "' has no executable scalar layout",
                compiled_source_span(
                    *compiled_, formal_declaration.source));
            return result;
        }
        const auto boundary = [&](const auto& value,
                                  const auto& boundary_expression) {
            return boundary_expression
                ? context.child_specialization.evaluate_integral_expression(
                      *boundary_expression)
                : value;
        };
        const semantic::CompiledDesignResolver type_resolver {
            context.child_specialization
        };
        const auto resolve_type = [&](const semantic::sv::TypeReference& reference)
            -> std::optional<std::pair<std::size_t,
                semantic::sv::TypeReference>> {
            auto effective
                = type_resolver.underlying_systemverilog_type(
                                   reference, formal_declaration.scope)
                      .value_or(reference);
            std::optional<std::size_t> width;
            if (effective.packed_range) {
                const auto left = boundary(
                    effective.packed_range->left,
                    effective.packed_range->left_expression);
                const auto right = boundary(
                    effective.packed_range->right,
                    effective.packed_range->right_expression);
                if (left && right) {
                    const auto distance = index_distance(*left, *right);
                    if (distance
                        < std::numeric_limits<std::size_t>::max()) {
                        width = static_cast<std::size_t>(distance + 1U);
                    }
                }
            } else if (effective.executable_width
                && *effective.executable_width > 0U
                && *effective.executable_width
                    <= std::numeric_limits<std::size_t>::max()) {
                width = static_cast<std::size_t>(
                    *effective.executable_width);
            } else if (!effective.target.target.valid()) {
                width = 1U;
            }
            return width
                ? std::optional { std::pair {
                      *width, std::move(effective) } }
                : std::nullopt;
        };
        const auto layout = resolve_type(*formal_declaration.type);
        if (!layout) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language SystemVerilog port '"
                    + formal_declaration.name
                    + "' has no executable scalar layout",
                compiled_source_span(
                    *compiled_, formal_declaration.source));
            return result;
        }
        CompiledBoundaryPort port;
        port.name = formal_declaration.name;
        port.type_name = formal_declaration.type->target.spelling;
        port.width = layout->first;
        port.domain
            = formal_declaration.type->four_state
                || layout->second.four_state
            ? frontend::ValueDomain::Logic4
            : frontend::ValueDomain::Bit2;
        port.systemverilog_scalar = compiled_systemverilog_scalar_kind(
            layout->second.target.spelling);
        if (port.systemverilog_scalar
            == frontend::SystemVerilogScalarKind::None) {
            port.systemverilog_scalar = compiled_systemverilog_scalar_kind(
                formal_declaration.type->target.spelling);
        }
        port.signed_value
            = formal_declaration.type->signed_value
            || layout->second.signed_value;
        const auto* packed_type = formal_declaration.type->packed_range
            ? &*formal_declaration.type
            : &layout->second;
        if (packed_type->packed_range) {
            const auto& range = *packed_type->packed_range;
            const auto left = boundary(
                range.left, range.left_expression);
            const auto right = boundary(
                range.right, range.right_expression);
            if (left && right) {
                port.packed_range = frontend::PackedRange {
                    *left, *right, range.descending
                };
            }
        }
        port.direction = compiled_port_direction(
            formal_declaration.direction);
        port.declaration_source = compiled_source_span(
            *compiled_, formal_declaration.source);
        port.initial = PackedLogic4 {
            port.width,
            is_two_state_domain(port.domain)
                ? Logic4::zero
                : Logic4::x,
        };
        const auto formal_signal = connect_compiled_boundary_port(
            port,
            *actual,
            context.child_path,
            compiled_source_span(*compiled_, association.source),
            context.external_binding);
        if (!formal_signal) {
            boundary_ports_valid = false;
            continue;
        }
        if (!child_aliases.emplace(
                              formal_declaration.name, *formal_signal)
                .second) {
            report(
                "FSIM-ELAB-HIR-001",
                "duplicate mixed-language port binding for '"
                    + formal_declaration.name + "'",
                compiled_source_span(*compiled_, association.source));
            return result;
        }
    }
    if (!boundary_ports_valid) {
        return result;
    }
    result.status = CompiledVhdlSystemVerilogPortMappingResult::Status::ready;
    return result;
}

bool HierarchyBuilder::instantiate_compiled_vhdl_unit(
    CompiledVhdlInstantiationContext context)
{
    const auto root = context.unit;
    auto path = std::move(context.path);
    auto actuals = std::move(context.actuals);
    auto port_aliases = std::move(context.port_aliases);
    const auto source_instance = context.source_instance;
    auto prepared_specialization
        = std::move(context.prepared_specialization);
    const auto vhdl_types_validated = context.vhdl_types_validated;
    if (compiled_ == nullptr || root.identity == nullptr
        || root.vhdl == nullptr || root.systemverilog != nullptr
        || root.vhdl->kind
            != semantic::vhdl::UnitKind::architecture) {
        report(
            "FSIM-ELAB-HIR-001",
            "parser-free VHDL hierarchy construction requires one "
            "compiled architecture",
            { });
        return false;
    }
    const auto& architecture = *root.vhdl;
    const auto* applied_configuration
        = active_compiled_vhdl_configuration_;
    const auto applied_configuration_identity
        = active_compiled_vhdl_configuration_identity_;
    const auto applied_configuration_source
        = active_compiled_vhdl_configuration_source_;
    const auto applied_component_identity
        = active_compiled_vhdl_component_identity_;
    const auto applied_component_source
        = active_compiled_vhdl_component_source_;
    const auto applied_component_declaration_source
        = active_compiled_vhdl_component_declaration_source_;
    const auto identity = unit_identity(root);
    if (std::ranges::find(stack_, identity) != stack_.end()) {
        report(
            "FSIM-ELAB-HIER-002",
            "recursive instantiation of '" + identity + "' at '"
                + path + "'",
            compiled_source_span(*compiled_, architecture.source));
        return false;
    }
    CompiledHierarchyStackGuard stack_guard { stack_, identity };
    const auto report_entity_interface_failure
        = [&](const bool ambiguous) {
        const bool component_target
            = !applied_component_identity.empty();
        report(
            component_target
                ? "FSIM-ELAB-VHCOMP-005"
                : "FSIM-ELAB-HIR-001",
            component_target
                ? "compiled VHDL component target '"
                    + architecture.primary_name + "' has "
                    + (ambiguous ? "an ambiguous" : "no linked")
                    + " entity interface"
                : "compiled architecture '" + architecture.name
                    + "' has "
                    + (ambiguous ? "an ambiguous" : "no linked")
                    + " entity interface",
            compiled_source_span(
                *compiled_,
                applied_component_source.value_or(
                    architecture.source)));
    };
    const semantic::vhdl::Unit* entity = nullptr;
    for (const auto& candidate : compiled_->vhdl_units()) {
        if (candidate.kind != semantic::vhdl::UnitKind::entity
            || !compiled_vhdl_name_equal(
                candidate.name, architecture.primary_name)
            || !compiled_vhdl_library_equal(
                candidate.library, architecture.library)) {
            continue;
        }
        if (entity != nullptr) {
            report_entity_interface_failure(true);
            return false;
        }
        entity = &candidate;
    }
    if (entity == nullptr) {
        report_entity_interface_failure(false);
        return false;
    }
    const auto report_configuration_issue = [&](
                                                std::string code,
                                                std::string message,
                                                const semantic::SourceSpanId source) {
        report(
            std::move(code), std::move(message),
            compiled_source_span(*compiled_, source));
    };
    const std::vector<semantic::ScopeId> architecture_scope {
        architecture.scope,
    };
    validate_compiled_vhdl_configuration_rules(
        *compiled_, path, architecture.component_configurations,
        architecture_scope, report_configuration_issue);
    const auto* applied_configuration_root
        = applied_configuration != nullptr
            && applied_configuration->configuration
        ? &*applied_configuration->configuration
        : nullptr;
    if (!validate_compiled_vhdl_configuration_scopes(
            *compiled_, architecture, applied_configuration_root, path,
            architecture_scope, report_configuration_issue)) {
        return false;
    }
    const auto structural_residual = [](
                                         const semantic::vhdl::Unit& unit) {
        return !unit.psl_verification_unit.has_value()
            && std::ranges::all_of(
                unit.generates, compiled_vhdl_generate_supported)
            && !unit.configuration.has_value();
    };
    if (!structural_residual(architecture)) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled VHDL architecture '" + architecture.name
                + "' still requires a structural syntax-lowering "
                  "adapter",
            compiled_source_span(*compiled_, architecture.source));
        return false;
    }
    if (entity->psl_verification_unit.has_value()
        || !entity->instances.empty()
        || !entity->generates.empty()
        || !entity->component_configurations.empty()
        || entity->configuration.has_value()) {
        report(
            "FSIM-ELAB-VHENTITY-001",
            "compiled VHDL entity '" + entity->name
                + "' contains structural records that are not legal in an "
                  "entity statement part",
            compiled_source_span(*compiled_, entity->source));
        return false;
    }

    std::vector<CompiledSpecializationFailure>
        specialization_failures;
    auto specialized = std::move(prepared_specialization);
    if (!specialized) {
        specialized = compiled_specialization(
            validated_compiled_, architecture.id, actuals,
            &specialization_failures);
    }
    for (const auto& failure : specialization_failures) {
        report(
            failure.code,
            failure.message,
            compiled_source_span(*compiled_, failure.source));
    }
    if (!specialized) {
        report(
            "FSIM-ELAB-HIR-001",
            "cannot construct a parser-free specialization for '"
                + architecture.primary_name + "(" + architecture.name
                + ")'",
            compiled_source_span(*compiled_, architecture.source));
        return false;
    }

    if (path == active_root_) {
        std::vector<semantic::DeclarationId> callable_declarations;
        callable_declarations.reserve(
            entity->declarations.size() + architecture.declarations.size());
        callable_declarations.insert(callable_declarations.end(),
            entity->declarations.begin(), entity->declarations.end());
        callable_declarations.insert(callable_declarations.end(),
            architecture.declarations.begin(),
            architecture.declarations.end());
        const auto legality_failures = validate_vhdl_callable_legality(
            *specialized, callable_declarations);
        for (const auto& failure : legality_failures) {
            report(failure.code, failure.message,
                compiled_source_span(*compiled_, failure.source));
        }
        if (!legality_failures.empty()) {
            return false;
        }
    }

    validate_compiled_vhdl_package_instances(
        path, *entity, architecture, *specialized);

    // Package-body legality errors reject the elaboration through the
    // diagnostic result, but must not suppress independent instance-generic
    // diagnostics in the same design.
    static_cast<void>(
        validate_compiled_vhdl_package_callable_bodies(
            path, specialized));


    const auto context_visibility_valid
        = validate_compiled_vhdl_context_visibility(
            entity, architecture, specialized);

    const auto selected_package_names_valid
        = validate_compiled_vhdl_selected_package_names(
            entity, architecture, specialized);
    if (!vhdl_types_validated) {
        for (const auto& issue : validate_vhdl_hir_types(
                 *specialized, *entity, architecture)) {
            report(
                issue.code,
                issue.message,
                compiled_source_span(*compiled_, issue.source));
        }
    }
    const auto subtype_declarations_valid
        = validate_compiled_vhdl_subtype_declarations(
            entity, architecture, specialized);
    const auto access_type_declarations_valid
        = validate_compiled_vhdl_access_type_declarations(
            entity, architecture, specialized);
    const auto object_composite_types_valid
        = validate_compiled_vhdl_object_composite_types(
            entity, architecture, specialized);
    const auto physical_type_units_valid
        = validate_compiled_vhdl_physical_type_units(
            entity, architecture, specialized);
    if (!context_visibility_valid || !selected_package_names_valid
        || !subtype_declarations_valid
        || !access_type_declarations_valid
        || !object_composite_types_valid
        || !physical_type_units_valid) {
        return false;
    }

    validate_compiled_vhdl_instantiated_package_cycles(
        *entity, architecture, *specialized);
    if (!validate_compiled_vhdl_predefined_subtype_attributes(
            *entity, architecture, *specialized)) {
        return false;
    }

    StringMap string_objects;
    ContainerMap container_objects;
    ReadOnlyStringSet read_only_strings;
    ReadOnlyContainerSet read_only_containers;
    std::vector<std::pair<std::string, std::string>>
        vhdl_port_shape_identities;

    SignalMap signals = std::move(port_aliases);
    ReadOnlySignalSet read_only_signals;
    std::unordered_set<std::string> declared_signal_names;
    const auto add_unit_declarations = [&](
                                           const semantic::vhdl::Unit&
                                               unit) -> bool {
        for (const auto declaration_id : unit.declarations) {
            const auto declaration = specialized->find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || !materialize_compiled_vhdl_declaration(
                    *specialized, path, path, signals,
                    read_only_signals, declared_signal_names,
                    container_objects, vhdl_port_shape_identities,
                    architecture.standard,
                    *declaration->vhdl)) {
                return false;
            }
        }
        return true;
    };
    if (!add_unit_declarations(*entity)
        || !add_unit_declarations(architecture)) {
        return false;
    }

    auto subprogram_validation
        = validate_compiled_vhdl_subprograms(
            *entity, architecture, *specialized);
    for (auto& diagnostic : subprogram_validation.diagnostics) {
        report(
            std::move(diagnostic.code), std::move(diagnostic.message),
            compiled_source_span(*compiled_, diagnostic.source));
    }
    if (!subprogram_validation.valid) {
        return false;
    }

    const auto generate_result
        = semantic::specialize_vhdl_generate_occurrences(*specialized);
    if (!generate_result) {
        report(generate_result.diagnostic_code,
            generate_result.error,
            compiled_source_span(*compiled_,
                generate_result.error_source.valid()
                    ? generate_result.error_source
                    : architecture.source));
        return false;
    }
    std::vector<VhdlHirMaterialization> generated_materializations;
    generated_materializations.reserve(
        generate_result.occurrences.size());
    for (const auto& occurrence : generate_result.occurrences) {
        const auto outcome = process_compiled_vhdl_generated_occurrence(
            CompiledVhdlGeneratedOccurrenceContext {
                .occurrence = occurrence,
                .root_path = path,
                .root_specialization = *specialized,
                .root_signals = signals,
                .root_read_only_signals = read_only_signals,
                .architecture = architecture,
                .container_objects = container_objects,
                .vhdl_port_shape_identities = vhdl_port_shape_identities,
                .generated_materializations = generated_materializations,
            });
        if (outcome
            == CompiledVhdlGeneratedOccurrenceStatus::failed) {
            return false;
        }
    }

    Lowerer lowerer {
        design_,
        signals,
        read_only_signals,
        string_objects,
        read_only_strings,
        container_objects,
        read_only_containers,
        diagnostics_,
    };
    lowerer.set_specialized_hir_unit(&*specialized);

    const auto specialization_index = design_.specializations_.size();
    const auto specialization_id = static_cast<SpecializationId>(
        specialization_index);
    if (static_cast<std::size_t>(specialization_id)
        != specialization_index) {
        throw std::length_error(
            "too many elaborated design-unit specializations");
    }
    SpecializationInfo specialization;
    specialization.id = specialization_id;
    specialization.unit = identity;
    specialization.instance = path;
    specialization.source_unit = architecture.id;
    specialization.source_instance = source_instance;
    specialization.source_span = architecture.source;
    specialization.origin = architecture.origin;
    const auto* physical_source = compiled_physical_source(
        *compiled_, architecture.source);
    specialization.source = physical_source != nullptr
        ? *physical_source
        : architecture.compilation_unit_identity;
    specialization.language = frontend::Language::Vhdl2008;
    specialization.library = architecture.library.empty()
        ? "work"
        : architecture.library;
    specialization.parameter_identity_values.insert(
        specialization.parameter_identity_values.end(),
        vhdl_port_shape_identities.begin(),
        vhdl_port_shape_identities.end());
    const auto append_source_dependency = [&](
                                              const std::string& source) {
        if (!source.empty() && source != specialization.source
            && std::ranges::find(
                   specialization.source_dependencies, source)
                == specialization.source_dependencies.end()) {
            specialization.source_dependencies.push_back(source);
        }
    };
    std::vector<semantic::UnitId> appended_dependency_units;
    const auto append_dependency_unit = [&](const semantic::UnitId id) {
        append_compiled_vhdl_dependency_unit(
            *compiled_, id, appended_dependency_units,
            append_source_dependency);
    };
    const auto append_declaration_dependencies
        = [&](const semantic::DeclarationId declaration) {
              const auto selected
                  = specialized->find_declaration(declaration);
              // Preserve the direct source for diagnostics and coverage, but
              // let the declaration dependency resolver contribute only the
              // packages actually used by the declaration.
              if (selected && selected->vhdl != nullptr) {
                  if (const auto* source = compiled_physical_source(
                          *compiled_, selected->vhdl->source)) {
                      append_source_dependency(*source);
                  }
              }
              for (const auto unit : specialized
                       ->vhdl_declaration_dependency_units(
                           declaration)) {
                  append_dependency_unit(unit);
              }
          };
    for (const auto& dependency : entity->source_dependencies) {
        append_source_dependency(dependency);
    }
    for (const auto& dependency : architecture.source_dependencies) {
        append_source_dependency(dependency);
    }
    if (const auto* entity_source = compiled_physical_source(
            *compiled_, entity->source)) {
        append_source_dependency(*entity_source);
    }
    append_dependency_unit(entity->id);
    append_dependency_unit(architecture.id);
    if (applied_configuration_source) {
        if (const auto* source = compiled_physical_source(
                *compiled_, *applied_configuration_source)) {
            append_source_dependency(*source);
        }
    }
    if (applied_component_source) {
        if (const auto* source = compiled_physical_source(
                *compiled_, *applied_component_source)) {
            append_source_dependency(*source);
        }
    }
    if (applied_component_declaration_source) {
        if (const auto* source = compiled_physical_source(
                *compiled_, *applied_component_declaration_source)) {
            append_source_dependency(*source);
        }
    }
    if (!append_compiled_vhdl_specialization_identity_projection(
            *entity, architecture, *specialized, path,
            generated_materializations, specialization,
            applied_configuration_identity, applied_component_identity,
            append_declaration_dependencies,
            report_configuration_issue)) {
        return false;
    }
    std::size_t concurrent_order { };
    if (!lower_compiled_vhdl_unit_processes(
            *entity, architecture, *specialized, lowerer, path,
            specialization, concurrent_order)) {
        return false;
    }
    if (!lower_compiled_vhdl_generated_processes(
            generated_materializations, architecture, string_objects,
            read_only_strings, container_objects, read_only_containers,
            specialization, concurrent_order)) {
        return false;
    }
    design_.specializations_.push_back(std::move(specialization));

    auto instance_materializations
        = collect_compiled_vhdl_instance_worklist(
            architecture, *specialized, signals, read_only_signals, path,
            generated_materializations);

    for (const auto& materialization : instance_materializations) {
        const auto instance = materialization.instance;
        const auto& working_specialization
            = *materialization.specialization;
        const auto& working_signals = *materialization.signals;
        const auto& working_read_only_signals
            = *materialization.read_only_signals;
        const auto working_path = materialization.path;
        if (instance.vhdl == nullptr) {
            report(
                "FSIM-ELAB-HIR-001",
                "VHDL hierarchy contains a non-VHDL instance record",
                compiled_source_span(*compiled_, architecture.source));
            return false;
        }
        const auto& record = *instance.vhdl;
        const auto child_path = materialization.generated
            ? std::string { working_path } + "." + record.name
            : compiled_instance_path(*compiled_, architecture.scope,
                  record.scope, working_path, record.name);
        semantic::vhdl::Instance effective_instance = record;
        std::optional<semantic::CompiledUnitView>
            explicitly_bound_child;
        std::optional<std::string> selected_systemc_target;
        const auto* external_binding = binding_for(child_path);
        if (external_binding != nullptr
            && external_binding->target) {
            const auto target = parse_target(
                *external_binding->target);
            if (!target) {
                report(
                    "FSIM-ELAB-BIND-013",
                    "malformed binding target '"
                        + *external_binding->target + "'",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            if (target->language == "systemc") {
                selected_systemc_target
                    = *external_binding->target;
            }
            if (target->language == "vhdl"
                && !target->architecture) {
                report(
                    "FSIM-ELAB-BIND-016",
                    "an explicit VHDL binding target must name an "
                    "architecture",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            if (!selected_systemc_target) {
                explicitly_bound_child = choose_bound_unit(
                    *compiled_, *target);
                if (!explicitly_bound_child) {
                    report(
                        "FSIM-ELAB-BIND-015",
                        "binding target '"
                            + *external_binding->target
                            + "' was not found",
                        compiled_source_span(
                            *compiled_, record.source));
                    return false;
                }
            }
        }
        auto child_selection = select_compiled_vhdl_child(
            CompiledVhdlChildSelectionContext {
                .architecture = architecture,
                .record = record,
                .instance = instance,
                .child_path = child_path,
                .working_specialization = working_specialization,
                .applied_configuration = applied_configuration,
                .explicitly_bound_child = explicitly_bound_child,
                .selected_systemc_target
                = std::move(selected_systemc_target),
            });
        if (child_selection.status
            != CompiledVhdlChildSelectionResult::Status::selected) {
            return false;
        }
        const auto* selected_rule = child_selection.selected_rule;
        const auto* child_configuration
            = child_selection.child_configuration;
        auto child_configuration_identity
            = std::move(child_selection.child_configuration_identity);
        const auto child_configuration_source
            = child_selection.child_configuration_source;
        auto child_component_identity
            = std::move(child_selection.child_component_identity);
        const auto child_component_source
            = child_selection.child_component_source;
        auto child = std::move(child_selection.child);
        selected_systemc_target
            = std::move(child_selection.selected_systemc_target);
        std::optional<semantic::SourceSpanId>
            child_component_declaration_source;
        if (selected_systemc_target) {
            if (!instantiate_compiled_vhdl_systemc_child(
                    effective_instance, working_specialization, child_path,
                    working_path, *selected_systemc_target,
                    external_binding, working_signals)) {
                return false;
            }
            continue;
        }
        if (!child) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled VHDL instance '" + record.name
                    + "' has no linked architecture HIR target",
                compiled_source_span(*compiled_, record.source));
            return false;
        }

        if (child->systemverilog != nullptr) {
            const auto& child_unit = *child->systemverilog;
            if (child->vhdl != nullptr
                || child_unit.kind
                    != semantic::sv::UnitKind::module) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "mixed-language binding for '" + child_path
                        + "' does not select a SystemVerilog module",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            auto parameter_result
                = prepare_compiled_vhdl_systemverilog_parameters(
                    CompiledVhdlSystemVerilogParametersContext {
                        .child_unit = child_unit,
                        .record = record,
                        .effective_instance = effective_instance,
                        .working_specialization
                        = working_specialization,
                        .child_path = child_path,
                    });
            if (parameter_result.status
                == CompiledVhdlSystemVerilogParametersResult::Status::fatal) {
                return false;
            }
            auto child_actuals
                = std::move(parameter_result.child_actuals);
            auto child_specialization
                = std::move(parameter_result.child_specialization);

            auto port_bindings
                = semantic::resolve_specialized_hir_associations(
                    *compiled_, child_unit.id,
                    semantic::CompiledInstanceView {
                        nullptr, &effective_instance },
                    semantic::SpecializedHirAssociationSurface::ports,
                    &working_specialization);
            if (!port_bindings) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "mixed-language port association for '"
                        + child_path + "' is unsupported: "
                        + port_bindings.error,
                    compiled_source_span(*compiled_,
                        port_bindings.error_source.valid()
                            ? port_bindings.error_source
                            : record.source));
                return false;
            }
            auto port_mapping
                = materialize_compiled_vhdl_systemverilog_port_mappings(
                    CompiledVhdlSystemVerilogPortMappingContext {
                        .child_specialization = *child_specialization,
                        .bindings = port_bindings,
                        .parent_specialization = working_specialization,
                        .parent_signals = working_signals,
                        .parent_path = std::string { working_path },
                        .child_path = child_path,
                        .external_binding = external_binding,
                    });
            if (port_mapping.status
                == CompiledVhdlSystemVerilogPortMappingResult::Status::fatal) {
                return false;
            }
            if (!instantiate_compiled_systemverilog_unit(
                    CompiledSystemVerilogInstantiationContext {
                        .unit = *child,
                        .path = child_path,
                        .actuals = std::move(child_actuals),
                        .port_aliases
                        = std::move(port_mapping.signal_aliases),
                        .string_port_aliases
                        = std::move(port_mapping.string_aliases),
                        .container_port_aliases
                        = std::move(port_mapping.container_aliases),
                        .source_instance = record.id,
                        .prepared_specialization
                        = std::move(child_specialization),
                    })) {
                return false;
            }
            continue;
        }

        const semantic::vhdl::Declaration* component = nullptr;
        const semantic::vhdl::Unit* target_entity = nullptr;
        for (const auto& candidate : compiled_->vhdl_units()) {
            if (candidate.kind == semantic::vhdl::UnitKind::entity
                && compiled_vhdl_library_equal(
                    candidate.library, child->vhdl->library)
                && compiled_vhdl_name_equal(
                    candidate.name, child->vhdl->primary_name)) {
                target_entity = &candidate;
                break;
            }
        }
        if (record.component) {
            const auto selection
                = select_compiled_vhdl_component_candidate(
                    architecture, record, target_entity,
                    working_specialization);
            if (selection.status
                != CompiledVhdlComponentCandidateSelection::Status::selected) {
                continue;
            }
            component = selection.component;
            child_component_declaration_source = component->source;
            const auto missing_generic_default_sources
                = prepare_compiled_vhdl_component_associations(
                    record, selected_rule, component, target_entity,
                    effective_instance);
            for (const auto source : missing_generic_default_sources) {
                report(
                    "FSIM-ELAB-VHCOMP-014",
                    "component generic default box has no "
                    "component declaration default",
                    compiled_source_span(*compiled_, source));
            }
            if (!missing_generic_default_sources.empty()) {
                continue;
            }
        }

        auto association_resolution
            = resolve_compiled_vhdl_associations(
                child->identity->id, record, effective_instance,
                working_specialization, selected_rule, target_entity);
        auto generic_bindings
            = std::move(association_resolution.generic_bindings);
        auto port_bindings
            = std::move(association_resolution.port_bindings);
        const bool generic_bindings_valid
            = static_cast<bool>(generic_bindings);
        for (auto& diagnostic : association_resolution.diagnostics) {
            report(
                std::move(diagnostic.code), std::move(diagnostic.message),
                compiled_source_span(*compiled_, diagnostic.source));
        }
        auto child_actual_result
            = materialize_compiled_vhdl_generic_actuals(
                record, generic_bindings, working_specialization);
        if (child_actual_result.status
            == CompiledVhdlGenericActualResult::Status::fatal) {
            return false;
        }
        if (child_actual_result.status
            == CompiledVhdlGenericActualResult::Status::invalid) {
            continue;
        }
        auto child_actuals = std::move(child_actual_result.actuals);
        if (component != nullptr && component->component
            && target_entity != nullptr) {
            append_compiled_vhdl_component_binding_identity(
                *compiled_, component, target_entity, child_actuals,
                port_bindings, record, *child,
                child_component_identity,
                child_configuration_identity);
        }
        std::vector<CompiledSpecializationFailure>
            child_specialization_failures;
        auto child_interface_specialization = compiled_specialization(
            validated_compiled_, child->identity->id, child_actuals,
            &child_specialization_failures);
        for (const auto& failure : child_specialization_failures) {
            report(
                failure.code,
                failure.message,
                compiled_source_span(*compiled_, failure.source));
        }
        if (!generic_bindings_valid) {
            continue;
        }
        if (!child_interface_specialization) {
            report(
                "FSIM-ELAB-HIR-001",
                "cannot construct a VHDL interface specialization for '"
                    + child_path + "'",
                compiled_source_span(*compiled_, record.source));
            return false;
        }
        if (target_entity != nullptr) {
            for (const auto& issue : validate_vhdl_hir_types(
                *child_interface_specialization,
                *target_entity,
                *child->vhdl)) {
                report(
                    issue.code,
                    issue.message,
                    compiled_source_span(*compiled_, issue.source));
            }
        }

        bool invalid_mode_view_interface { };
        if (target_entity != nullptr) {
            for (const auto declaration_id :
                target_entity->declarations) {
                const auto declaration
                    = child_interface_specialization->find_declaration(
                        declaration_id);
                if (!declaration || declaration->vhdl == nullptr
                    || declaration->vhdl->form
                        != semantic::vhdl::DeclarationForm::port
                    || !declaration->vhdl->interface_view) {
                    continue;
                }
                const auto& profile
                    = *declaration->vhdl->interface_view;
                if (profile.composition
                    == semantic::vhdl::ModeViewCompositionState::complete) {
                    continue;
                }
                const auto view_declaration
                    = profile.view.selected
                    ? child_interface_specialization->find_declaration(
                          *profile.view.selected)
                    : std::nullopt;
                const auto view_composition = view_declaration
                        && view_declaration->vhdl != nullptr
                        && view_declaration->vhdl->mode_view
                    ? view_declaration->vhdl->mode_view->composition
                    : semantic::vhdl::ModeViewCompositionState::uncomposed;
                std::string code;
                std::string message;
                if (!profile.view.selected) {
                    code = "FSIM-ELAB-VHVIEW-001";
                    message = "VHDL mode view '" + profile.view.spelling
                        + "' is not visible for interface '"
                        + declaration->vhdl->name + "'";
                } else if (profile.composition
                        == semantic::vhdl::ModeViewCompositionState::recursive
                    || view_composition
                        == semantic::vhdl::ModeViewCompositionState::recursive) {
                    code = "FSIM-ELAB-VHVIEW-002";
                    message = "VHDL mode view '" + profile.view.spelling
                        + "' has a recursive direction map";
                } else if (profile.explicit_subtype
                    && view_composition
                        == semantic::vhdl::ModeViewCompositionState::complete) {
                    code = "FSIM-ELAB-VHVIEW-004";
                    message = "VHDL view-based interface '"
                        + declaration->vhdl->name
                        + "' has a subtype incompatible with mode view '"
                        + profile.view.spelling + "'";
                } else {
                    code = "FSIM-ELAB-VHVIEW-003";
                    message = "VHDL mode view '" + profile.view.spelling
                        + "' does not cover one unresolved record with "
                          "compatible nested views";
                }
                report(
                    code,
                    message,
                    compiled_source_span(*compiled_, profile.view.source));
                invalid_mode_view_interface = true;
            }
        }
        if (invalid_mode_view_interface) {
            continue;
        }

        if (!port_bindings) {
            return false;
        }
        const auto component_defaulted_port_formals
            = materialize_compiled_vhdl_component_port_defaults(
                record, component, target_entity, port_bindings.bindings);
        if (target_entity != nullptr) {
            for (const auto declaration_id :
                target_entity->declarations) {
                const auto declaration = compiled_->find_declaration(
                    declaration_id);
                if (!declaration || declaration->vhdl == nullptr
                    || declaration->vhdl->form
                        != semantic::vhdl::DeclarationForm::port
                    || compiled_port_direction(
                        declaration->vhdl->direction)
                        != frontend::PortDirection::Input
                    || declaration->vhdl->initializer
                    || std::ranges::any_of(
                        port_bindings.bindings,
                        [&](const auto& binding) {
                            return binding.formal == declaration_id;
                        })) {
                    continue;
                }
                report(
                    record.component
                        ? "FSIM-ELAB-VHCOMP-009"
                        : "FSIM-ELAB-BIND-027",
                    "required VHDL input port '"
                        + declaration->vhdl->name
                        + "' has no associated actual",
                    compiled_source_span(
                        *compiled_, record.source));
                return false;
            }
        }
        SignalMap child_aliases;
        if (!materialize_compiled_vhdl_port_associations(
                CompiledVhdlPortAssociationContext {
                    .architecture = architecture,
                    .record = record,
                    .component = component,
                    .target_entity = target_entity,
                    .port_bindings = port_bindings,
                    .child_actuals = child_actuals,
                    .working_specialization = working_specialization,
                    .child_interface_specialization
                    = child_interface_specialization,
                    .component_defaulted_port_formals
                    = component_defaulted_port_formals,
                    .child_path = child_path,
                    .working_path = working_path,
                    .working_signals = working_signals,
                    .working_read_only_signals
                    = working_read_only_signals,
                    .string_objects = string_objects,
                    .read_only_strings = read_only_strings,
                    .container_objects = container_objects,
                    .read_only_containers = read_only_containers,
                    .specialization_id = specialization_id,
                    .concurrent_order = concurrent_order,
                    .child_aliases = child_aliases,
                })) {
            return false;
        }
        if (!instantiate_compiled_vhdl_child(
                CompiledVhdlChildActivationContext {
                    .child = *child,
                    .path = child_path,
                    .actuals = child_actuals,
                    .port_aliases = child_aliases,
                    .source_instance = record.id,
                    .prepared_specialization
                        = child_interface_specialization,
                    .vhdl_types_validated = target_entity != nullptr,
                    .configuration = child_configuration,
                    .configuration_identity
                        = child_configuration_identity,
                    .configuration_source = child_configuration_source,
                    .component_identity = child_component_identity,
                    .component_source = child_component_source,
                    .component_declaration_source
                        = child_component_declaration_source,
                })) {
            return false;
        }
    }
    return true;
}

bool HierarchyBuilder::validate_compiled_vhdl_generated_callables(
    const std::span<const semantic::DeclarationId> declarations,
    const semantic::SpecializedHirUnit& specialization)
{
    bool generated_callables_valid { true };
    std::vector<const semantic::vhdl::Declaration*> generated_callables;
    const semantic::CompiledDesignResolver generated_resolver {
        specialization
    };
    for (const auto declaration_id : declarations) {
        const auto declaration
            = specialization.find_declaration(declaration_id);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->callable
            || !declaration->vhdl->callable->defined
            || (declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::function
                && declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::procedure)) {
            continue;
        }
        const auto duplicate = std::ranges::find_if(
            generated_callables,
            [&](const auto* candidate) {
                return candidate->form == declaration->vhdl->form
                    && compiled_vhdl_name_equal(
                        candidate->name, declaration->vhdl->name)
                    && generated_resolver.vhdl_callable_profile_matches(
                        candidate->id, declaration_id, true);
            });
        if (duplicate != generated_callables.end()) {
            const auto function = declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::function;
            report(
                function
                    ? "FSIM-ELAB-VHOVER-003"
                    : "FSIM-ELAB-VHOVER-006",
                "duplicate VHDL "
                    + std::string {
                        function ? "function profile '"
                                 : "procedure profile '" }
                    + declaration->vhdl->name + "'",
                compiled_source_span(*compiled_, declaration->vhdl->source));
            generated_callables_valid = false;
            continue;
        }
        generated_callables.push_back(declaration->vhdl);
    }
    if (!generated_callables_valid) {
        return false;
    }

    bool generated_callable_visibility_valid { true };
    const auto& generated_semantic_scopes = compiled_->semantics.scopes();
    const auto scope_within = [&](semantic::ScopeId inner,
                                  const semantic::ScopeId outer) {
        for (std::size_t depth { };
            inner.valid()
            && depth <= generated_semantic_scopes.size();
            ++depth) {
            if (inner == outer) {
                return true;
            }
            if (inner.value() >= generated_semantic_scopes.size()
                || !generated_semantic_scopes[inner.value()].parent) {
                return false;
            }
            inner = *generated_semantic_scopes[inner.value()].parent;
        }
        return false;
    };
    std::unordered_set<std::uint32_t> inspected_expressions;
    const auto inspect_expression = [&](
                                        const semantic::vhdl::Expression& expression) {
        if (!inspected_expressions.insert(expression.id.value()).second
            || expression.kind
                != semantic::vhdl::ExpressionKind::call
            || !expression.referenced_name) {
            return;
        }
        const semantic::vhdl::Declaration* owner { };
        for (const auto* callable : generated_callables) {
            if (callable->nested_scope
                && scope_within(
                    expression.scope, *callable->nested_scope)) {
                owner = callable;
                break;
            }
        }
        if (owner == nullptr) {
            return;
        }
        const semantic::vhdl::Declaration* target { };
        if (expression.referenced_name->selected) {
            const auto selected = specialization.find_declaration(
                *expression.referenced_name->selected);
            if (selected && selected->vhdl != nullptr) {
                target = selected->vhdl;
            }
        }
        if (target == nullptr) {
            for (const auto declaration_id : declarations) {
                const auto candidate
                    = specialization.find_declaration(declaration_id);
                if (candidate && candidate->vhdl != nullptr
                    && compiled_vhdl_name_equal(
                        candidate->vhdl->name,
                        expression.referenced_name->spelling)) {
                    target = candidate->vhdl;
                    break;
                }
            }
        }
        if (target == nullptr || target == owner
            || target->scope != owner->scope) {
            return;
        }
        const auto expression_span = compiled_source_span(
            *compiled_, expression.source);
        const auto target_span = compiled_source_span(
            *compiled_, target->source);
        if (target_span.begin.offset <= expression_span.begin.offset) {
            return;
        }
        report(
            "FSIM-ELAB-VHNAME-001",
            "VHDL callable '" + target->name
                + "' is not visible at this call",
            expression_span);
        generated_callable_visibility_valid = false;
    };
    for (const auto& expression : specialization.vhdl_expressions()) {
        inspect_expression(expression);
    }
    for (const auto& expression : compiled_->vhdl_hir.expressions()) {
        inspect_expression(expression);
    }
    for (const auto declaration_id : declarations) {
        const auto declaration
            = specialization.find_declaration(declaration_id);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->package
            || (declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::
                        generic_function_instance
                && declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::
                        generic_procedure_instance)) {
            continue;
        }
        const auto& reference = declaration->vhdl->package->template_name;
        const semantic::vhdl::Declaration* target { };
        if (reference.selected) {
            const auto selected = specialization.find_declaration(
                *reference.selected);
            if (selected && selected->vhdl != nullptr) {
                target = selected->vhdl;
            }
        }
        if (target == nullptr) {
            for (const auto candidate_id : declarations) {
                const auto candidate
                    = specialization.find_declaration(candidate_id);
                if (candidate && candidate->vhdl != nullptr
                    && compiled_vhdl_name_equal(
                        candidate->vhdl->name, reference.spelling)) {
                    target = candidate->vhdl;
                    break;
                }
            }
        }
        const auto declaration_span = compiled_source_span(
            *compiled_, declaration->vhdl->source);
        const auto target_span = target != nullptr
            ? compiled_source_span(*compiled_, target->source)
            : frontend::SourceSpan { };
        if (target != nullptr
            && target_span.begin.offset <= declaration_span.begin.offset) {
            continue;
        }
        report(
            "FSIM-ELAB-VHGSUB-001",
            "generic VHDL callable template '"
                + reference.spelling
                + "' is not visible at this instantiation",
            declaration_span);
        generated_callable_visibility_valid = false;
    }
    return generated_callable_visibility_valid;
}

bool HierarchyBuilder::validate_compiled_vhdl_generated_declaration_visibility(
    const std::span<const semantic::DeclarationId> declarations,
    const semantic::SpecializedHirUnit& specialization)
{
    bool generated_declarations_valid { true };
    for (const auto declaration_id : declarations) {
        const auto declaration
            = specialization.find_declaration(declaration_id);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            continue;
        }
        using DeclarationForm = semantic::vhdl::DeclarationForm;
        const auto form = declaration->vhdl->form;
        if (form != DeclarationForm::generic_constant
            && form != DeclarationForm::port
            && form != DeclarationForm::signal
            && form != DeclarationForm::constant
            && form != DeclarationForm::variable
            && form != DeclarationForm::file) {
            continue;
        }
        const auto& type_mark = declaration->vhdl->subtype->type_mark;
        auto type = type_mark.target.valid()
            ? specialization.find_type(type_mark.target)
            : std::optional<semantic::CompiledTypeView> { };
        if (!type) {
            const auto consider_type = [&](const auto& candidate) {
                if (type) {
                    return;
                }
                if (!compiled_vhdl_name_equal(
                        candidate.name, type_mark.spelling)) {
                    return;
                }
                const auto candidate_declaration
                    = specialization.find_declaration(
                        candidate.declaration);
                if (!candidate_declaration
                    || candidate_declaration->vhdl == nullptr
                    || candidate_declaration->vhdl->scope
                        != declaration->vhdl->scope) {
                    return;
                }
                type = semantic::CompiledTypeView {
                    nullptr, &candidate
                };
            };
            for (const auto& candidate : specialization.vhdl_types()) {
                consider_type(candidate);
            }
            for (const auto& candidate : compiled_->vhdl_hir.types()) {
                consider_type(candidate);
            }
        }
        const auto type_declaration
            = type && type->vhdl != nullptr
            ? specialization.find_declaration(type->vhdl->declaration)
            : std::nullopt;
        if (!type || type->vhdl == nullptr
            || !type_declaration
            || type_declaration->vhdl == nullptr
            || type_declaration->vhdl->scope
                != declaration->vhdl->scope) {
            continue;
        }
        const auto use_span
            = compiled_source_span(*compiled_, type_mark.source);
        const auto type_span
            = compiled_source_span(*compiled_, type->vhdl->source);
        if (type_span.begin.offset <= use_span.begin.offset) {
            continue;
        }
        report(
            "FSIM-ELAB-VHTYPE-001",
            "VHDL type '" + type_mark.spelling
                + "' is not visible at this declaration",
            use_span);
        generated_declarations_valid = false;
    }
    return generated_declarations_valid;
}

bool HierarchyBuilder::validate_compiled_vhdl_generated_constants(
    const std::span<const semantic::DeclarationId> declarations,
    const semantic::SpecializedHirUnit& specialization)
{
    bool generated_constants_valid { true };
    for (const auto declaration_id : declarations) {
        const auto declaration
            = specialization.find_declaration(declaration_id);
        if (!declaration || declaration->vhdl == nullptr
            || declaration->vhdl->form
                != semantic::vhdl::DeclarationForm::constant
            || !declaration->vhdl->initializer
            || !declaration->vhdl->subtype) {
            continue;
        }
        const auto value
            = specialization.evaluate_integral_declaration(
                declaration_id);
        if (!value) {
            if (specialization.evaluate_vhdl_packed_value_declaration(
                    declaration_id)
                || specialization.evaluate_vhdl_packed_array_declaration(
                    declaration_id)) {
                continue;
            }
            report(
                "FSIM-ELAB-GEN-011",
                "cannot evaluate generated VHDL constant '"
                    + declaration->vhdl->name + "'",
                compiled_source_span(
                    *compiled_, declaration->vhdl->source));
            generated_constants_valid = false;
            continue;
        }
        if (!compiled_vhdl_specialization_value_satisfies_subtype(
                specialization,
                *declaration->vhdl->subtype, *value)) {
            report(
                "FSIM-ELAB-GEN-012",
                "generated VHDL constant '"
                    + declaration->vhdl->name
                    + "' value is outside subtype '"
                    + declaration->vhdl->subtype->type_mark.spelling
                    + "'",
                compiled_source_span(
                    *compiled_, declaration->vhdl->source));
            generated_constants_valid = false;
        }
    }
    return generated_constants_valid;
}

bool HierarchyBuilder::lower_compiled_vhdl_unit_processes(
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture,
    const semantic::SpecializedHirUnit& specialized,
    Lowerer& lowerer,
    const std::string_view path,
    SpecializationInfo& specialization,
    std::size_t& concurrent_order)
{
    const auto append_generated_processes = [&](Lowerer& source,
                                                const semantic::vhdl::Unit& owner) {
        for (auto& generated : source.take_generated_processes()) {
            generated.language_standard = owner.standard;
            generated.compatibility_profile
                = owner.compatibility_profile;
            canonicalize_process_operations(generated);
            specialization.processes.push_back(generated.id);
            design_.processes_.push_back(std::move(generated));
        }
    };
    const auto report_unspecified_inference = [&](Lowerer& source,
                                                  const auto id) {
        const auto failure
            = source.hir_vhdl_unspecified_inference_failure(id);
        if (!failure) {
            return false;
        }
        report(
            "FSIM-ELAB-VHUNSPEC-001",
            "VHDL-2019 unspecified interface types do not infer one "
            "consistent callable profile",
            compiled_source_span(*compiled_, *failure));
        return true;
    };
    const auto lower_concurrent_statements
        = [&](const semantic::vhdl::Unit& owner) {
              for (const auto statement_id : owner.concurrent_statements) {
                  const auto statement
                      = specialized.find_statement(statement_id);
                  const auto statement_source = statement
                          && statement->vhdl != nullptr
                      ? statement->vhdl->source
                      : owner.source;
                  if (report_unspecified_inference(
                          lowerer, statement_id)) {
                      return false;
                  }
                  auto lowered = lowerer.lower_hir_concurrent_statement(
                      statement_id,
                      frontend::Language::Vhdl2008,
                      path,
                      concurrent_order++);
                  if (!lowered) {
                      report(
                          "FSIM-ELAB-HIR-001",
                          "compiled VHDL concurrent statement could not be "
                          "lowered from HIR",
                          compiled_source_span(
                              *compiled_, statement_source));
                      return false;
                  }
                  lowered->language_standard = owner.standard;
                  lowered->compatibility_profile
                      = owner.compatibility_profile;
                  canonicalize_process_operations(*lowered);
                  specialization.processes.push_back(lowered->id);
                  design_.processes_.push_back(std::move(*lowered));
                  append_generated_processes(lowerer, owner);
              }
              return true;
          };
    const auto lower_processes = [&](const semantic::vhdl::Unit& owner) {
        for (const auto process_id : owner.processes) {
            const auto process = specialized.find_process(process_id);
            const auto process_source = process && process->vhdl != nullptr
                ? process->vhdl->source
                : owner.source;
            if (report_unspecified_inference(lowerer, process_id)) {
                return false;
            }
            auto lowered = lowerer.lower_hir_process(
                process_id,
                frontend::Language::Vhdl2008,
                path);
            if (!lowered) {
                const auto process_name = process
                        && process->vhdl != nullptr
                        && !process->vhdl->name.empty()
                    ? " '" + process->vhdl->name + "'"
                    : std::string { };
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled VHDL process"
                        + process_name + " (HIR "
                        + std::to_string(process_id.value())
                        + ") could not be lowered from HIR",
                    compiled_source_span(*compiled_, process_source));
                return false;
            }
            lowered->language_standard = owner.standard;
            lowered->compatibility_profile = owner.compatibility_profile;
            canonicalize_process_operations(*lowered);
            specialization.processes.push_back(lowered->id);
            specialization.semantic_processes.emplace_back(
                lowered->id, process_id);
            design_.processes_.push_back(std::move(*lowered));
            append_generated_processes(lowerer, owner);
        }
        return true;
    };
    if (!lower_concurrent_statements(entity)
        || !lower_processes(entity)
        || !lower_concurrent_statements(architecture)
        || !lower_processes(architecture)) {
        return false;
    }
    return true;
}

bool HierarchyBuilder::materialize_compiled_vhdl_port_associations(
    CompiledVhdlPortAssociationContext context)
{
    const auto& architecture = context.architecture;
    const auto& record = context.record;
    const auto* const component = context.component;
    const auto* const target_entity = context.target_entity;
    const auto& port_bindings = context.port_bindings;
    const auto& child_actuals = context.child_actuals;
    const auto& working_specialization = context.working_specialization;
    const auto& child_interface_specialization
        = context.child_interface_specialization;
    const auto& component_defaulted_port_formals
        = context.component_defaulted_port_formals;
    const auto& child_path = context.child_path;
    const auto working_path = context.working_path;
    const auto& working_signals = context.working_signals;
    const auto& working_read_only_signals
        = context.working_read_only_signals;
    auto& string_objects = context.string_objects;
    auto& read_only_strings = context.read_only_strings;
    auto& container_objects = context.container_objects;
    auto& read_only_containers = context.read_only_containers;
    const auto specialization_id = context.specialization_id;
    auto& concurrent_order = context.concurrent_order;
    auto& child_aliases = context.child_aliases;
    auto port_actual_specialization = working_specialization;
    if (component != nullptr && component->component
        && target_entity != nullptr) {
        std::vector<semantic::DeclarationId> target_generics;
        for (const auto declaration_id :
            target_entity->declarations) {
            const auto declaration = compiled_->find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                continue;
            }
            const auto form = declaration->vhdl->form;
            if (form
                    == semantic::vhdl::DeclarationForm::generic_constant
                || form
                    == semantic::vhdl::DeclarationForm::generic_type
                || form
                    == semantic::vhdl::DeclarationForm::generic_function
                || form
                    == semantic::vhdl::DeclarationForm::generic_procedure
                || form
                    == semantic::vhdl::DeclarationForm::generic_package) {
                target_generics.push_back(declaration_id);
            }
        }
        std::vector<semantic::SpecializedHirActualIdentity>
            component_actuals;
        for (std::size_t index { };
            index < target_generics.size()
            && index < component->component->generics.size();
            ++index) {
            const auto actual = std::ranges::find(
                child_actuals,
                target_generics[index],
                &semantic::SpecializedHirActualIdentity::declaration);
            if (actual == child_actuals.end()) {
                continue;
            }
            auto component_actual = *actual;
            component_actual.declaration
                = component->component->generics[index];
            component_actuals.push_back(
                std::move(component_actual));
        }
        port_actual_specialization
            = port_actual_specialization
                  .with_local_actual_identities(component_actuals);
    }
    std::unordered_map<std::string, std::string>
        inferred_vhdl_port_types;
    for (const auto& binding : port_bindings.bindings) {
        const auto formal = compiled_->find_declaration(
            binding.formal);
        if (!formal || formal->vhdl == nullptr
            || formal->vhdl->form
                != semantic::vhdl::DeclarationForm::port) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled VHDL port association for '"
                    + record.name + "' has no port declaration",
                compiled_source_span(*compiled_, binding.source));
            return false;
        }
        const auto& formal_declaration = *formal->vhdl;
        if (binding.kind
                == semantic::SpecializedHirAssociationKind::open
            || binding.kind
                == semantic::SpecializedHirAssociationKind::default_value) {
            const auto has_default = record.component
                ? component_defaulted_port_formals.contains(
                      binding.formal.value())
                : formal_declaration.initializer.has_value();
            if (compiled_port_direction(
                    formal_declaration.direction)
                    == frontend::PortDirection::Input
                && !has_default) {
                report(
                    record.component
                        ? "FSIM-ELAB-VHCOMP-009"
                        : "FSIM-ELAB-BIND-027",
                    "required VHDL input port '"
                        + formal_declaration.name
                        + "' has no associated actual or default",
                    compiled_source_span(
                        *compiled_, binding.source));
                return false;
            }
            // Direct entity defaults belong to the child specialization.
            // Component defaults were converted above because they
            // belong to the parent-side component declaration.
            continue;
        }
        if (binding.kind
                != semantic::SpecializedHirAssociationKind::expression
            || !binding.expression) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled VHDL port '" + formal_declaration.name
                    + "' requires a simple signal-name actual",
                compiled_source_span(*compiled_, binding.source));
            return false;
        }
        if (std::ranges::any_of(
                child_aliases,
                [&](const auto& alias) {
                    return compiled_vhdl_name_equal(
                        alias.first, formal_declaration.name);
                })) {
            report(
                "FSIM-ELAB-HIR-001",
                "duplicate compiled VHDL port binding for '"
                    + formal_declaration.name + "'",
                compiled_source_span(*compiled_, binding.source));
            return false;
        }
        const auto expression
            = port_actual_specialization.find_expression(
                *binding.expression);
        const auto direction = compiled_port_direction(
            formal_declaration.direction);
        const auto accepts_input
            = direction == frontend::PortDirection::Input
            || direction == frontend::PortDirection::Inout;
        const auto has_runtime_dependency = [&] {
            std::unordered_set<std::uint32_t> visiting;
            const auto scan = [&](const auto& self,
                                  const semantic::ExpressionId id)
                -> bool {
                if (!visiting.insert(id.value()).second) {
                    return false;
                }
                const auto candidate
                    = port_actual_specialization.find_expression(id);
                if (!candidate || candidate->vhdl == nullptr) {
                    return false;
                }
                const auto& source = *candidate->vhdl;
                if (source.referenced_name
                    && source.referenced_name->selected) {
                    const auto declaration
                        = port_actual_specialization.find_declaration(
                            *source.referenced_name->selected);
                    if (declaration
                        && declaration->vhdl != nullptr) {
                        const auto form = declaration->vhdl->form;
                        if (form
                                == semantic::vhdl::DeclarationForm::port
                            || form
                                == semantic::vhdl::DeclarationForm::signal
                            || form
                                == semantic::vhdl::DeclarationForm::variable
                            || form
                                == semantic::vhdl::DeclarationForm::file) {
                            return true;
                        }
                    }
                }
                return std::ranges::any_of(
                           source.operands,
                           [&](const semantic::ExpressionId operand) {
                               return self(self, operand);
                           })
                    || std::ranges::any_of(
                        source.associations,
                        [&](const auto& association) {
                            return self(self, association.value)
                                || std::ranges::any_of(
                                    association.choices,
                                    [&](const semantic::ExpressionId
                                            choice) {
                                        return self(self, choice);
                                    });
                        });
            };
            return expression && scan(scan, *binding.expression);
        }();
        if (accepts_input
            && component_defaulted_port_formals.contains(
                binding.formal.value())
            && has_runtime_dependency) {
            report(
                "FSIM-ELAB-VHCOMP-013",
                "component input port '"
                    + formal_declaration.name
                    + "' has a hierarchy-dependent default expression",
                compiled_source_span(
                    *compiled_, binding.source));
            return false;
        }
        auto port_actual = materialize_compiled_vhdl_port_actual(
            binding, expression ? &*expression : nullptr,
            formal_declaration, port_actual_specialization,
            *child_interface_specialization, direction, child_path,
            working_path, architecture,
            component_defaulted_port_formals, working_signals,
            working_read_only_signals, string_objects,
            read_only_strings, container_objects,
            read_only_containers, specialization_id,
            concurrent_order);
        if (!port_actual) {
            return false;
        }
        std::optional<SignalId> actual_signal { port_actual->signal };
        auto actual_hir_type_identity
            = std::move(port_actual->hir_type_identity);
        auto actual_hir_type = std::move(port_actual->hir_type);
        auto actual_hir_subtype = std::move(port_actual->hir_subtype);
        auto actual_hir_declaration
            = std::move(port_actual->hir_declaration);
        auto actual_hir_scope = std::move(port_actual->hir_scope);
        const bool formal_infers_subtype
            = formal_declaration.subtype
            && (formal_declaration.subtype->unspecified_class
                    != semantic::vhdl::UnspecifiedTypeClass::none
                || !formal_declaration.subtype
                    ->unspecified_inference_identity.empty()
                || !formal_declaration.subtype
                    ->unspecified_component_classes.empty()
                || formal_declaration.subtype
                        ->unspecified_array_index_count
                    != 0U);
        if (formal_declaration.subtype
            && !formal_infers_subtype
            && *actual_signal < design_.signal_info_.size()) {
            if (!validate_compiled_vhdl_port_actual_compatibility(
                    CompiledVhdlPortCompatibilityContext {
                        formal_declaration,
                        *actual_signal,
                        actual_hir_type,
                        actual_hir_subtype,
                        actual_hir_declaration,
                        actual_hir_scope,
                        expression ? &*expression : nullptr,
                        direction,
                        child_path,
                        child_interface_specialization,
                        binding.source,
                    })) {
                return false;
            }
        }
        if (formal_infers_subtype
            && *actual_signal < design_.signal_info_.size()) {
            const auto& actual_info
                = design_.signal_info_[*actual_signal];
            auto actual_type_identity = actual_hir_type_identity
                                            .value_or(std::to_string(static_cast<unsigned>(
                                                actual_info.source_domain)));
            if (!actual_hir_type_identity) {
                actual_type_identity += ":"
                    + std::to_string(actual_info.width);
                actual_type_identity += ":" + actual_info.type_name;
                actual_type_identity += actual_info.is_signed
                    ? ":signed"
                    : ":unsigned";
                if (actual_info.packed_range) {
                    actual_type_identity += ":" + std::to_string(actual_info.packed_range->left);
                    actual_type_identity += ":" + std::to_string(actual_info.packed_range->right);
                }
            }
            const auto& inference_identity
                = formal_declaration.subtype
                      ->unspecified_inference_identity;
            const auto key = inference_identity.empty()
                ? formal_declaration.subtype->type_mark.source.valid()
                    ? "source:"
                        + std::to_string(formal_declaration.subtype
                                ->type_mark.source
                                .value())
                    : formal_declaration.name
                : inference_identity;
            const auto [existing, inserted]
                = inferred_vhdl_port_types.emplace(
                    key, actual_type_identity);
            if (!inserted
                && existing->second != actual_type_identity) {
                report(
                    "FSIM-ELAB-VHUNSPEC-001",
                    "port associations sharing one unspecified "
                    "formal type infer conflicting concrete "
                    "subtypes",
                    compiled_source_span(
                        *compiled_, binding.source));
                return false;
            }
        }
        if (formal_declaration.subtype
            && formal_declaration.interface_view
            && formal_declaration.interface_view->composition
                == semantic::vhdl::ModeViewCompositionState::complete
            && *actual_signal < design_.signal_info_.size()) {
            const auto formal_type = compiled_vhdl_signal_type(
                *child_interface_specialization,
                *formal_declaration.subtype,
                formal_declaration.scope);
            VhdlModeViewBinding view_binding;
            view_binding.formal
                = child_path + "." + formal_declaration.name;
            view_binding.view
                = formal_declaration.interface_view->view.spelling;
            view_binding.kind = formal_declaration.interface_view->form
                    == semantic::vhdl::ModeViewElementForm::array_view
                ? frontend::VhdlModeViewIndicationKind::array
                : frontend::VhdlModeViewIndicationKind::record;
            view_binding.source = compiled_source_span(
                *compiled_, binding.source);
            std::string endpoint_error;
            const auto& actual_info
                = design_.signal_info_[*actual_signal];
            if (!formal_type
                || !materialize_compiled_vhdl_mode_view_endpoints(
                    *compiled_,
                    *child_interface_specialization,
                    formal_declaration.scope,
                    *formal_type,
                    *formal_declaration.interface_view,
                    *actual_signal,
                    actual_info.width,
                    view_binding.formal,
                    actual_info.name,
                    view_binding.elements,
                    endpoint_error)) {
                report(
                    "FSIM-ELAB-VHVIEW-006",
                    "cannot materialize VHDL mode-view endpoints for '"
                        + view_binding.formal + "': "
                        + (endpoint_error.empty()
                                ? "unavailable retained-HIR type layout"
                                : endpoint_error),
                    view_binding.source);
                return false;
            }
            const bool all_input = !view_binding.elements.empty()
                && std::ranges::all_of(
                    view_binding.elements,
                    [](const auto& endpoint) {
                        return endpoint.direction
                            == frontend::PortDirection::Input;
                    });
            const bool has_writable_element = std::ranges::any_of(
                view_binding.elements,
                [](const auto& endpoint) {
                    return endpoint.direction
                        == frontend::PortDirection::Output
                        || endpoint.direction
                        == frontend::PortDirection::Inout
                        || endpoint.direction
                        == frontend::PortDirection::Buffer;
                });
            design_.signal_info_[*actual_signal]
                .vhdl_mode_view_bindings.push_back(
                    std::move(view_binding));
            static_cast<void>(all_input);
            if (has_writable_element) {
                note_boundary_driver(
                    *actual_signal,
                    nullptr,
                    child_path + "." + formal_declaration.name,
                    compiled_source_span(*compiled_, binding.source));
            }
        }
        child_aliases.emplace(
            formal_declaration.name, *actual_signal);
    }
    return true;
}

HierarchyBuilder::CompiledVhdlChildSelectionResult
HierarchyBuilder::select_compiled_vhdl_child(
    CompiledVhdlChildSelectionContext context)
{
    CompiledVhdlChildSelectionResult result;
    const auto& architecture = context.architecture;
    const auto& record = context.record;
    const auto& instance = context.instance;
    const auto& child_path = context.child_path;
    const auto& working_specialization
        = context.working_specialization;
    const auto* applied_configuration = context.applied_configuration;
    const auto& explicitly_bound_child
        = context.explicitly_bound_child;
    result.selected_systemc_target
        = std::move(context.selected_systemc_target);
    auto& selected_rule = result.selected_rule;
    auto& child_configuration = result.child_configuration;
    auto& child_configuration_identity
        = result.child_configuration_identity;
    auto& child_configuration_source
        = result.child_configuration_source;
    auto& child_component_identity
        = result.child_component_identity;
    auto& child_component_source
        = result.child_component_source;
    auto& child = result.child;
    auto& selected_systemc_target = result.selected_systemc_target;
    bool missing_explicit_configuration_rule { };
    const auto select = [&]() {
        const auto select_entity_architecture = [&](
                                                    const semantic::vhdl::Name& entity_name,
                                                    const std::string_view architecture_name,
                                                    const semantic::SourceSpanId source) -> bool {
            const auto parts = vhdl_configuration_detail::
                configuration_name_parts(
                    entity_name.canonical.empty()
                        ? std::string_view { entity_name.spelling }
                        : std::string_view { entity_name.canonical });
            if (parts.empty() || parts.size() > 2U) {
                report(
                    "FSIM-ELAB-VHCONFIG-008",
                    "VHDL binding for '" + child_path
                        + "' has a malformed entity aspect",
                    compiled_source_span(*compiled_, source));
                return false;
            }
            const auto parent_library = compiled_vhdl_library(
                architecture);
            const auto requested_library = parts.size() == 2U
                ? (compiled_vhdl_name_equal(parts.front(), "work")
                          ? std::string { parent_library }
                          : parts.front())
                : std::string { parent_library };
            std::size_t matches { };
            child = compiled_vhdl_architecture(
                *compiled_, requested_library, parts.back(),
                architecture_name, matches);
            if (matches != 1U || !child) {
                report(
                    matches == 0U
                        ? "FSIM-ELAB-VHCONFIG-008"
                        : "FSIM-ELAB-VHCONFIG-009",
                    "VHDL binding for '" + child_path + "' selects "
                        + (matches == 0U ? "missing" : "ambiguous")
                        + " architecture target",
                    compiled_source_span(*compiled_, source));
                return false;
            }
            return true;
        };
        const auto select_configuration = [&](
                                              const semantic::vhdl::Name& configuration_name,
                                              const semantic::SourceSpanId source) -> bool {
            std::size_t matches { };
            child_configuration = compiled_vhdl_configuration(
                *compiled_, compiled_vhdl_library(architecture),
                configuration_name, matches);
            if (matches != 1U || child_configuration == nullptr
                || !child_configuration->configuration) {
                report(
                    matches == 0U
                        ? "FSIM-ELAB-VHCONFIG-013"
                        : "FSIM-ELAB-VHCONFIG-014",
                    "VHDL configuration binding for '" + child_path
                        + "' selects "
                        + (matches == 0U ? "missing" : "ambiguous")
                        + " configuration '"
                        + configuration_name.spelling + "'",
                    compiled_source_span(*compiled_, source));
                return false;
            }
            if (!select_entity_architecture(
                    semantic::vhdl::Name {
                        child_configuration->primary_name,
                        child_configuration->primary_name,
                        child_configuration->source,
                        std::nullopt,
                        { } },
                    child_configuration->configuration->block.spelling,
                    child_configuration->configuration->source)) {
                return false;
            }
            child_configuration_identity
                = compiled_vhdl_configuration_identity(
                    *compiled_, *child_configuration);
            child_configuration_source
                = child_configuration->source;
            return true;
        };
        const auto select_default_component = [&] {
            const auto occurrence = resolve_compiled_occurrence(
                working_specialization, instance);
            if (occurrence && occurrence->linked_target) {
                const auto linked = *occurrence->linked_target;
                const bool executable_module
                    = linked.systemverilog != nullptr
                    && linked.vhdl == nullptr
                    && linked.systemverilog->kind
                        == semantic::sv::UnitKind::module
                    && !linked.systemverilog->external;
                const bool executable_architecture
                    = linked.vhdl != nullptr
                    && linked.systemverilog == nullptr
                    && linked.vhdl->kind
                        == semantic::vhdl::UnitKind::architecture;
                if (executable_module || executable_architecture) {
                    child = linked;
                    return true;
                }
            }
            const auto search_scope = effective_search_scope(
                compiled_vhdl_library(architecture),
                search_libraries_);
            std::vector<UnitResolutionCandidate> candidates;
            std::vector<std::string> unavailable_libraries;
            for (std::size_t index = 0U;
                index < search_scope.size(); ++index) {
                const auto& library = search_scope[index];
                if (!has_logical_library(
                        *compiled_, systemc_candidates_,
                        systemc_libraries_, library)) {
                    if (index != 0U) {
                        unavailable_libraries.push_back(library);
                    }
                    continue;
                }
                auto resolved = resolve_unit_candidates(
                    *compiled_, library, record.target.spelling);
                candidates.insert(
                    candidates.end(),
                    std::make_move_iterator(resolved.begin()),
                    std::make_move_iterator(resolved.end()));
                for (const auto& factory : systemc_candidates_) {
                    if (compiled_vhdl_name_equal(
                            factory.library, library)
                        && compiled_vhdl_name_equal(
                            factory.name,
                            record.target.spelling)) {
                        candidates.push_back({
                            factory.target,
                            "systemc:" + factory.library + "."
                                + factory.name,
                            std::nullopt,
                            nullptr,
                        });
                    }
                }
            }
            std::stable_sort(
                candidates.begin(), candidates.end(),
                [](const auto& left, const auto& right) {
                    return left.identity < right.identity;
                });
            const auto format_libraries = [](const auto& libraries) {
                std::string formatted;
                for (const auto& library : libraries) {
                    if (!formatted.empty()) {
                        formatted += ", ";
                    }
                    formatted += library;
                }
                return formatted;
            };
            if (!unavailable_libraries.empty()) {
                report(
                    "FSIM-ELAB-BIND-059",
                    "compiled VHDL component '" + record.name
                        + "' queried unavailable logical "
                          "library/libraries ["
                        + format_libraries(unavailable_libraries)
                        + "] in search scope ["
                        + format_libraries(search_scope) + "]",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            if (candidates.empty()) {
                report(
                    "FSIM-ELAB-VHCOMP-003",
                    "compiled VHDL component '" + record.name
                        + "' was not found across VHDL, Verilog, or "
                          "SystemVerilog in search scope ["
                        + format_libraries(search_scope)
                        + "]; candidates: <none>",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            if (candidates.size() != 1U) {
                report(
                    "FSIM-ELAB-VHCOMP-005",
                    "compiled VHDL component '" + record.name
                        + "' is ambiguous in search scope ["
                        + format_libraries(search_scope)
                        + "]; candidates: "
                        + format_resolution_candidates(candidates),
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            if (candidates.front().systemc_target) {
                selected_systemc_target
                    = *candidates.front().systemc_target;
                return true;
            }
            if (!candidates.front().compiled_unit) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled VHDL component '" + record.name
                        + "' selected a target without an executable "
                          "HDL HIR unit",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            child = candidates.front().compiled_unit;
            return true;
        };

        if (explicitly_bound_child) {
            child = explicitly_bound_child;
        } else if (!selected_systemc_target
            && record.configuration) {
            if (!select_configuration(record.target, record.source)) {
                return false;
            }
        } else if (!selected_systemc_target
            && record.component) {
            const auto occurrence_parts = vhdl_configuration_detail::
                configuration_occurrence_parts(
                    *compiled_, architecture, record, child_path);
            if (applied_configuration != nullptr
                && applied_configuration->configuration
                && occurrence_parts && !occurrence_parts->empty()) {
                const auto blocks = compiled_vhdl_configuration_blocks(
                    *compiled_, *applied_configuration->configuration,
                    *occurrence_parts);
                for (auto block = blocks.rbegin();
                    block != blocks.rend() && selected_rule == nullptr
                    && !missing_explicit_configuration_rule;
                    ++block) {
                    selected_rule = compiled_vhdl_configuration_rule(
                        (*block)->components, record,
                        occurrence_parts->back(),
                        &missing_explicit_configuration_rule);
                }
            }
            if (missing_explicit_configuration_rule) {
                report(
                    "FSIM-ELAB-VHCONFIG-016",
                    "compiled VHDL configuration is missing the "
                    "explicit component rule for '"
                        + child_path + "'",
                    compiled_source_span(*compiled_,
                        applied_configuration != nullptr
                            ? applied_configuration->source
                            : record.source));
                return false;
            }
            if (selected_rule == nullptr) {
                selected_rule = compiled_vhdl_configuration_rule(
                    architecture.component_configurations,
                    record, record.name);
            }
            if (selected_rule == nullptr) {
                if (!select_default_component()) {
                    return false;
                }
            } else {
                child_component_identity
                    = compiled_vhdl_binding_identity(
                        *compiled_, selected_rule->binding);
                child_component_source = selected_rule->source;
                switch (selected_rule->binding.kind) {
                case semantic::vhdl::BindingKind::open:
                    if (!select_default_component()) {
                        return false;
                    }
                    break;
                case semantic::vhdl::BindingKind::configuration:
                    if (!select_configuration(
                            selected_rule->binding.configuration,
                            selected_rule->binding.source)) {
                        return false;
                    }
                    child_component_identity += ";reference="
                        + child_configuration_identity;
                    break;
                case semantic::vhdl::BindingKind::entity:
                    if (!select_entity_architecture(
                            selected_rule->binding.entity,
                            selected_rule->binding.architecture,
                            selected_rule->binding.source)) {
                        return false;
                    }
                    break;
                }
            }
        } else if (!selected_systemc_target) {
            const auto occurrence = resolve_compiled_occurrence(
                working_specialization, instance);
            if (!occurrence || !occurrence->linked_target
                || occurrence->linked_target->vhdl == nullptr
                || occurrence->linked_target->systemverilog != nullptr
                || occurrence->linked_target->vhdl->kind
                    != semantic::vhdl::UnitKind::entity) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled VHDL instance '" + record.name
                        + "' has no linked entity HIR target",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
            const auto requested_architecture
                = compiled_vhdl_target_architecture(
                    record.target.spelling);
            semantic::vhdl::Name entity_name;
            entity_name.spelling
                = occurrence->linked_target->vhdl->name;
            entity_name.canonical = std::string { compiled_vhdl_library(
                                        *occurrence->linked_target->vhdl) }
                + "." + entity_name.spelling;
            entity_name.source = record.source;
            if (!select_entity_architecture(
                    entity_name, requested_architecture,
                    record.source)) {
                return false;
            }
        }
        const bool default_component_binding = record.component
            && (selected_rule == nullptr
                || selected_rule->binding.kind
                    == semantic::vhdl::BindingKind::open);
        if (default_component_binding && child
            && child->vhdl != nullptr
            && child->systemverilog == nullptr
            && (child->vhdl->kind
                    == semantic::vhdl::UnitKind::architecture
                || child->vhdl->kind
                    == semantic::vhdl::UnitKind::entity)) {
            const auto entity_name = child->vhdl->kind
                    == semantic::vhdl::UnitKind::architecture
                ? std::string_view { child->vhdl->primary_name }
                : std::string_view { child->vhdl->name };
            const auto entity_matches = std::ranges::count_if(
                compiled_->vhdl_units(),
                [&](const semantic::vhdl::Unit& candidate) {
                    return candidate.kind
                        == semantic::vhdl::UnitKind::entity
                        && compiled_vhdl_library_equal(
                            candidate.library, child->vhdl->library)
                        && compiled_vhdl_name_equal(
                            candidate.name,
                            entity_name);
                });
            if (entity_matches != 1) {
                report(
                    entity_matches == 0
                        ? "FSIM-ELAB-VHCOMP-003"
                        : "FSIM-ELAB-VHCOMP-005",
                    "compiled VHDL component '" + record.name
                        + "' has "
                        + (entity_matches == 0
                                ? "no linked entity interface"
                                : "ambiguous linked entity interfaces"),
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
        }
        return true;
    };
    if (!select()) {
        return result;
    }
    result.status = CompiledVhdlChildSelectionResult::Status::selected;
    return result;
}

void HierarchyBuilder::build(const SystemCInstanceDescription& root)
{
    add_root(root, root.path);
    finalize();
}

void HierarchyBuilder::add_root(
    const SystemCInstanceDescription& root,
    std::string path)
{
    active_root_ = std::move(path);
    instantiate_systemc(root, active_root_, { }, { });
}

void HierarchyBuilder::finalize()
{
    finish();
}

std::vector<SelectedSystemVerilogClass>
HierarchyBuilder::take_selected_systemverilog_classes()
{
    return std::move(selected_systemverilog_classes_);
}

HierarchyBuilder::CompiledSystemVerilogInstanceDispatchResult
HierarchyBuilder::dispatch_compiled_systemverilog_instance(
    const CompiledSystemVerilogInstanceDispatchContext& context)
{
    using Result = CompiledSystemVerilogInstanceDispatchResult;
    using Status = Result::Status;

    const auto& materialization = context;
    const auto& unit = context.unit;
    const auto& instance = context.instance;
    const auto& record = context.record;
    const auto& working_specialization = context.working_specialization;
    const auto& child_path = context.child_path;
    const auto working_path = context.working_path;
    const auto* const external_binding = context.external_binding;
    const auto& working_signals = context.working_signals;
    const auto& working_read_only_signals
        = context.working_read_only_signals;
    const auto& working_strings = context.working_strings;
    const auto& working_read_only_strings
        = context.working_read_only_strings;
    const auto& working_containers = context.working_containers;
    const auto& working_read_only_containers
        = context.working_read_only_containers;

    if (record.udp) {
        const auto target = compiled_instance_target(
            compiled_systemverilog_library(unit),
            record.target.spelling,
            child_path,
            compiled_source_span(*compiled_, record.source),
            external_binding,
            std::nullopt);
        if (!target) {
            return Result {};
        }
        if (target->compiled_udp == nullptr) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled UDP instance '" + child_path
                    + "' did not resolve to a compiled UDP "
                      "declaration",
                compiled_source_span(*compiled_, record.source));
            return Result {};
        }
        if (!instantiate_compiled_udp(
                *target->compiled_udp,
                record,
                working_specialization,
                child_path,
                materialization.array_index,
                working_signals,
                working_read_only_signals,
                working_strings,
                working_read_only_strings,
                working_containers,
                working_read_only_containers,
                external_binding)) {
            return Result {};
        }
        Result result;
        result.status = Status::handled;
        return result;
    }
    if (record.anonymous) {
        report(
            "FSIM-ELAB-BIND-062",
            "module instance '" + child_path
                + "' requires an explicit instance name",
            compiled_source_span(*compiled_, record.source));
        return Result {};
    }
    if (record.udp_delay || record.drive_zero
        || record.drive_one) {
        const bool delay = record.udp_delay.has_value();
        report(
            delay ? "FSIM-ELAB-BIND-063"
                  : "FSIM-ELAB-BIND-065",
            "module instance '" + child_path
                + "' cannot use UDP "
                + (delay ? "propagation-delay"
                         : "drive-strength")
                + " syntax",
            compiled_source_span(*compiled_, record.source));
        return Result {};
    }
    const auto resolved = resolve_compiled_occurrence(
        working_specialization, instance);
    if (!resolved) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled instance '" + record.name
                + "' has no valid compiled occurrence",
            compiled_source_span(*compiled_, record.source));
        return Result {};
    }
    auto linked_target = resolved->linked_target;
    const auto explicitly_qualified_target
        = record.target.spelling.find_first_of(".:")
        != std::string::npos;
    bool linked_target_authoritative = linked_target.has_value()
        && (materialization.bound || explicitly_qualified_target);
    const semantic::sv::Unit* nested_configuration = nullptr;
    if (active_compiled_systemverilog_configuration_ != nullptr
        && (external_binding == nullptr
            || !external_binding->target)) {
        const auto& configuration
            = *active_compiled_systemverilog_configuration_;
        const auto& declaration = *configuration.configuration;
        auto configured_child_path
            = declaration.designs.front().cell;
        if (child_path.size()
                > active_compiled_systemverilog_configuration_root_.size()
            && child_path.starts_with(
                active_compiled_systemverilog_configuration_root_)) {
            configured_child_path += child_path.substr(
                active_compiled_systemverilog_configuration_root_.size());
        }
        const semantic::sv::ConfigurationRule* selected_rule
            = nullptr;
        if (!materialization.systemverilog_2023_bound) {
            const auto instance_rule = std::ranges::find_if(
                declaration.rules, [&](const auto& rule) {
                    return rule.kind
                        == semantic::sv::ConfigurationRuleKind::instance
                        && rule.selector == configured_child_path;
                });
            if (instance_rule != declaration.rules.end()) {
                selected_rule = &*instance_rule;
            }
        }
        if (selected_rule == nullptr) {
            const auto cell_rule = std::ranges::find_if(
                declaration.rules, [&](const auto& rule) {
                    if (rule.kind
                        != semantic::sv::ConfigurationRuleKind::cell) {
                        return false;
                    }
                    const auto separator = rule.selector.rfind('.');
                    const auto library = separator
                            == std::string::npos
                        ? std::string_view { }
                        : std::string_view { rule.selector }.substr(
                              0U, separator);
                    const auto cell = separator == std::string::npos
                        ? std::string_view { rule.selector }
                        : std::string_view { rule.selector }.substr(
                              separator + 1U);
                    return cell == record.target.spelling
                        && (library.empty()
                            || library
                                == compiled_systemverilog_library(unit));
                });
            if (cell_rule != declaration.rules.end()) {
                selected_rule = &*cell_rule;
            }
        }
        const auto find_target = [&](
                                     const std::string_view library,
                                     const std::string_view cell,
                                     const std::optional<semantic::UnitId>
                                         linked)
            -> std::optional<semantic::CompiledUnitView> {
            const auto match
                = find_exact_systemverilog_module(
                    *compiled_, library, cell);
            if (match.ambiguous) {
                report(
                    "FSIM-ELAB-SVCONFIG-003",
                    "configured cell '"
                        + std::string { library } + "."
                        + std::string { cell }
                        + "' is ambiguous",
                    compiled_source_span(
                        *compiled_, record.source));
                return std::nullopt;
            }
            auto selected = match.unit;
            if (!selected && linked) {
                const auto linked_unit
                    = compiled_->find_unit(*linked);
                if (linked_unit
                    && linked_unit->systemverilog != nullptr
                    && compiled_systemverilog_selectable(
                        *linked_unit->systemverilog)) {
                    selected = linked_unit;
                }
            }
            return selected;
        };
        const auto select_from_liblist = [&](const auto& libraries) {
            std::optional<semantic::CompiledUnitView> selected;
            for (const auto& library : libraries) {
                selected = find_target(
                    library, record.target.spelling, std::nullopt);
                if (selected) {
                    break;
                }
            }
            return selected;
        };
        std::optional<semantic::CompiledUnitView> configured;
        bool applied = selected_rule != nullptr
            || !declaration.default_liblist.empty();
        if (selected_rule != nullptr
            && selected_rule->selection
                == semantic::sv::ConfigurationSelectionKind::use) {
            const auto library = selected_rule->use_library.empty()
                ? compiled_systemverilog_library(configuration)
                : std::string_view { selected_rule->use_library };
            if (selected_rule->use_configuration) {
                auto selected_configuration = selected_rule->target
                    ? compiled_->find_unit(*selected_rule->target)
                    : std::optional<semantic::CompiledUnitView> { };
                if (!selected_configuration) {
                    selected_configuration = compiled_->find_unit(
                        semantic::UnitKind::systemverilog_configuration,
                        library,
                        selected_rule->use_cell);
                }
                if (selected_configuration
                    && selected_configuration->systemverilog != nullptr
                    && selected_configuration->systemverilog
                        ->configuration
                    && selected_configuration->systemverilog
                            ->configuration->designs.size()
                        == 1U) {
                    nested_configuration
                        = selected_configuration->systemverilog;
                    const auto& nested_design
                        = nested_configuration->configuration
                              ->designs.front();
                    const auto nested_library
                        = nested_design.library.empty()
                        ? compiled_systemverilog_library(
                              *nested_configuration)
                        : std::string_view { nested_design.library };
                    configured = find_target(
                        nested_library,
                        nested_design.cell,
                        nested_design.target);
                }
            } else {
                configured = find_target(
                    library,
                    selected_rule->use_cell.empty()
                        ? std::string_view { record.target.spelling }
                        : std::string_view { selected_rule->use_cell },
                    selected_rule->target);
            }
        } else if (selected_rule != nullptr) {
            configured = select_from_liblist(
                selected_rule->liblist);
        } else if (applied) {
            configured = select_from_liblist(
                declaration.default_liblist);
        }
        if (applied) {
            if (!configured
                || configured->systemverilog == nullptr) {
                report(
                    "FSIM-ELAB-SVCONFIG-003",
                    "configuration '" + configuration.name
                        + "' cannot select target for instance '"
                        + child_path + "'",
                    compiled_source_span(
                        *compiled_, selected_rule != nullptr ? selected_rule->source : declaration.source));
                return Result {};
            }
            linked_target = configured;
            linked_target_authoritative = true;
        }
    }
    const auto selected_target = compiled_instance_target(
        compiled_systemverilog_library(unit),
        record.target.spelling,
        child_path,
        compiled_source_span(*compiled_, record.source),
        external_binding,
        linked_target,
        linked_target_authoritative);
    if (!selected_target) {
        return Result {};
    }
    if (selected_target->systemc_target) {
        const auto* description = construct_systemc_description(
            record, working_specialization, child_path,
            *selected_target->systemc_target);
        if (description == nullptr) {
            return Result {};
        }
        const auto diagnostics_before = diagnostics_.size();
        SignalMap child_aliases;
        ObjectMap child_objects;
        std::vector<bool> connected(description->ports.size());
        std::size_t next_positional { };
        bool saw_named { };
        bool saw_positional { };
        for (const auto& association : record.ports) {
            if (association.kind
                    == semantic::sv::ActualKind::open
                || association.kind
                    == semantic::sv::ActualKind::default_value) {
                continue;
            }
            const auto source = compiled_source_span(
                *compiled_, association.source);
            if (association.kind
                    != semantic::sv::ActualKind::expression
                || !association.expression) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "SystemC port association requires a "
                    "compiled-HIR signal-name actual",
                    source);
                continue;
            }
            std::optional<std::size_t> port_index;
            if (association.formal) {
                saw_named = true;
                const auto found = std::ranges::find(
                    description->ports, *association.formal,
                    &ExternalPort::name);
                if (found == description->ports.end()) {
                    report(
                        "FSIM-ELAB-BIND-004",
                        "unknown SystemC port '"
                            + *association.formal + "'",
                        source);
                    continue;
                }
                port_index = static_cast<std::size_t>(
                    std::distance(
                        description->ports.begin(), found));
            } else {
                saw_positional = true;
                while (next_positional < connected.size()
                    && connected[next_positional]) {
                    ++next_positional;
                }
                if (next_positional >= connected.size()) {
                    report(
                        "FSIM-ELAB-BIND-004",
                        "too many positional SystemC port "
                        "associations",
                        source);
                    continue;
                }
                port_index = next_positional++;
            }
            if (connected[*port_index]) {
                report(
                    "FSIM-ELAB-BIND-004",
                    "duplicate SystemC port association for '"
                        + description->ports[*port_index].name
                        + "'",
                    source);
                continue;
            }
            connected[*port_index] = true;

            const auto expression
                = working_specialization.find_expression(
                    *association.expression);
            std::string actual_name;
            if (expression
                && expression->systemverilog != nullptr
                && expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::name) {
                actual_name = expression->systemverilog->text;
                if (expression->systemverilog->referenced_name
                    && expression->systemverilog
                        ->referenced_name->selected) {
                    const auto declaration
                        = working_specialization.find_declaration(
                            *expression->systemverilog
                                ->referenced_name->selected);
                    if (declaration
                        && declaration->systemverilog != nullptr) {
                        actual_name
                            = declaration->systemverilog->name;
                    }
                }
            }
            const auto actual = working_signals.find(actual_name);
            if (actual == working_signals.end()) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "SystemC port actual '" + actual_name
                        + "' is not a signal in '"
                        + std::string { working_path } + "'",
                    source);
                continue;
            }

            const auto& external_port
                = description->ports[*port_index];
            const auto external_width
                = external_port.type.width();
            if (!external_width || *external_width == 0U) {
                report(
                    "FSIM-ELAB-BIND-019",
                    "SystemC port '" + external_port.name
                        + "' has no executable scalar layout",
                    source);
                continue;
            }
            CompiledBoundaryPort port;
            port.name = external_port.name;
            port.type_name = external_port.type.spelling;
            port.width = static_cast<std::size_t>(
                *external_width);
            port.domain = external_port.type.domain;
            port.systemverilog_scalar
                = external_port.type.systemverilog_scalar;
            port.signed_value = external_port.type.is_signed;
            port.packed_range
                = external_port.type.packed_range;
            port.integer_range
                = external_port.type.integer_range;
            port.direction = external_port.direction;
            port.declaration_source = source;
            port.initial = PackedLogic4 {
                port.width,
                is_two_state_domain(port.domain)
                    ? Logic4::zero
                    : Logic4::x,
            };
            const auto formal = connect_compiled_boundary_port(
                port, actual->second, child_path, source,
                external_binding);
            if (!formal) {
                continue;
            }
            child_aliases.emplace(external_port.name, *formal);
            child_objects.emplace(
                external_port.handle, *formal);
        }
        if (saw_named && saw_positional) {
            report(
                "FSIM-ELAB-BIND-004",
                "named and positional SystemC port associations "
                "cannot be mixed",
                compiled_source_span(*compiled_, record.source));
        }
        if (diagnostics_.size() != diagnostics_before) {
            return Result {};
        }
        instantiate_systemc(
            *description, child_path, std::move(child_aliases),
            std::move(child_objects));
        Result result;
        result.status = Status::handled;
        return result;
    }

    return Result {
        .status = Status::selected_target,
        .child = selected_target->compiled_unit,
        .nested_configuration = nested_configuration,
        .source_instance = resolved->instance,
    };
}

bool HierarchyBuilder::instantiate_compiled_systemverilog_vhdl_child(
    const CompiledSystemVerilogVhdlChildContext& context)
{
    const auto& child = context.child;
    const auto& instance = context.instance;
    const auto& record = context.record;
    const auto& source_instance = context.source_instance;
    const auto& child_path = context.child_path;
    const auto working_path = context.working_path;
    const auto& working_specialization = context.working_specialization;
    const auto& working_signals = context.working_signals;
    const auto* const external_binding = context.external_binding;

    if (child && child->vhdl != nullptr
        && child->systemverilog == nullptr) {
        if (child->identity == nullptr
            || child->vhdl->kind
                != semantic::vhdl::UnitKind::architecture) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language binding for '" + child_path
                    + "' does not select a VHDL architecture",
                compiled_source_span(*compiled_, record.source));
            return false;
        }
        auto generic_bindings
            = semantic::resolve_specialized_hir_associations(
                *compiled_, child->identity->id, instance,
                semantic::SpecializedHirAssociationSurface::parameters,
                &working_specialization);
        if (!generic_bindings && generic_bindings.issues.empty()) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language generic association for '"
                    + child_path + "' is unsupported: "
                    + generic_bindings.error,
                compiled_source_span(
                    *compiled_, generic_bindings.error_source.valid() ? generic_bindings.error_source : record.source));
            return false;
        }
        for (const auto& issue : generic_bindings.issues) {
            auto diagnostic
                = hierarchy_vhdl_associations_detail::
                    generic_diagnostic_code(issue.diagnostic);
            if (issue.diagnostic
                    == semantic::
                        SpecializedHirAssociationDiagnostic::
                            callable_language_mismatch
                && issue.callable_function) {
                diagnostic = *issue.callable_function
                    ? "FSIM-ELAB-VHFUNC-002"
                    : "FSIM-ELAB-VHPROC-002";
            }
            report(
                diagnostic,
                issue.message,
                compiled_source_span(
                    *compiled_, issue.source.valid() ? issue.source : record.source));
        }
        std::vector<semantic::SpecializedHirActualIdentity>
            child_actuals;
        for (const auto& binding : generic_bindings.bindings) {
            const auto formal
                = compiled_->find_declaration(binding.formal);
            if (formal && formal->vhdl != nullptr
                && formal->vhdl->form
                    == semantic::vhdl::DeclarationForm::generic_type) {
                report(
                    "FSIM-ELAB-GENTYPE-004",
                    "VHDL interface type generic '"
                        + formal->vhdl->name
                        + "' cannot receive an actual through a "
                          "non-VHDL association boundary",
                    compiled_source_span(*compiled_, binding.source));
                return false;
            }
            if (binding.kind
                    == semantic::SpecializedHirAssociationKind::open
                || (binding.kind
                        == semantic::SpecializedHirAssociationKind::
                            default_value
                    && !binding.actual_declaration)) {
                continue;
            }
            if (binding.identity.empty()) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "mixed-language generic association for '"
                        + child_path
                        + "' has no deterministic identity",
                    compiled_source_span(*compiled_, binding.source));
                return false;
            }
            auto actual = compiled_vhdl_specialization_actual(
                *compiled_, working_specialization, binding);
            if (actual.requires_static_value) {
                report(
                    "FSIM-ELAB-GENERIC-004",
                    "mixed-language generic association for '"
                        + child_path
                        + "' is not a locally static value",
                    compiled_source_span(*compiled_, binding.source));
                return false;
            }
            child_actuals.push_back(std::move(actual.actual));
        }
        std::vector<CompiledSpecializationFailure>
            child_specialization_failures;
        auto child_specialization = compiled_specialization(
            validated_compiled_, child->identity->id, child_actuals,
            &child_specialization_failures);
        for (const auto& failure : child_specialization_failures) {
            report(
                failure.code,
                failure.message,
                compiled_source_span(*compiled_, failure.source));
        }
        if (!child_specialization) {
            report(
                "FSIM-ELAB-HIR-001",
                "cannot construct a mixed-language VHDL "
                "specialization for '"
                    + child_path + "'",
                compiled_source_span(*compiled_, record.source));
            return false;
        }
        auto port_bindings
            = semantic::resolve_specialized_hir_associations(
                *compiled_, child->identity->id, instance,
                semantic::SpecializedHirAssociationSurface::ports,
                &working_specialization);
        if (!port_bindings) {
            report(
                "FSIM-ELAB-HIR-001",
                "mixed-language VHDL port association for '"
                    + child_path + "' is unsupported: "
                    + port_bindings.error,
                compiled_source_span(
                    *compiled_, port_bindings.error_source.valid() ? port_bindings.error_source : record.source));
            return false;
        }
        const auto child_entity = std::ranges::find_if(
            compiled_->vhdl_units(),
            [&](const semantic::vhdl::Unit& candidate) {
                return candidate.kind
                    == semantic::vhdl::UnitKind::entity
                    && compiled_vhdl_library_equal(
                        candidate.library, child->vhdl->library)
                    && compiled_vhdl_name_equal(
                        candidate.name, child->vhdl->primary_name);
            });
        if (child_entity != compiled_->vhdl_units().end()) {
            for (const auto declaration_id : child_entity->declarations) {
                const auto formal
                    = child_specialization->find_declaration(
                        declaration_id);
                if (!formal || formal->vhdl == nullptr
                    || formal->vhdl->form
                        != semantic::vhdl::DeclarationForm::port
                    || compiled_port_direction(formal->vhdl->direction)
                        != frontend::PortDirection::Input
                    || formal->vhdl->initializer
                    || std::ranges::any_of(
                        port_bindings.bindings,
                        [&](const auto& binding) {
                            return binding.formal == declaration_id;
                        })) {
                    continue;
                }
                report(
                    "FSIM-ELAB-BIND-027",
                    "required VHDL input port '"
                        + formal->vhdl->name
                        + "' has no associated actual or default",
                    compiled_source_span(*compiled_, record.source));
                return false;
            }
        }
        SignalMap child_aliases;
        for (const auto& binding : port_bindings.bindings) {
            const auto formal = child_specialization->find_declaration(
                binding.formal);
            if (!formal || formal->vhdl == nullptr
                || formal->vhdl->form
                    != semantic::vhdl::DeclarationForm::port
                || !formal->vhdl->subtype) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "mixed-language VHDL port association for '"
                        + child_path
                        + "' has no executable port declaration",
                    compiled_source_span(*compiled_, binding.source));
                return false;
            }
            const auto& declaration = *formal->vhdl;
            const auto direction
                = compiled_port_direction(declaration.direction);
            if (binding.kind
                    == semantic::SpecializedHirAssociationKind::open
                || binding.kind
                    == semantic::SpecializedHirAssociationKind::
                        default_value) {
                if (direction == frontend::PortDirection::Input
                    && !declaration.initializer) {
                    report(
                        "FSIM-ELAB-BIND-027",
                        "required VHDL input port '"
                            + declaration.name
                            + "' has no associated actual or default",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                // An unaliased formal is initialized when the child
                // specialization is materialized. This keeps a VHDL
                // default in the generic environment where it was declared.
                continue;
            }
            if (binding.kind
                    != semantic::SpecializedHirAssociationKind::expression
                || !binding.expression) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "mixed-language VHDL port '"
                        + declaration.name
                        + "' requires a signal-name actual",
                    compiled_source_span(*compiled_, binding.source));
                return false;
            }
            const auto expression
                = working_specialization.find_expression(
                    *binding.expression);
            std::string actual_name;
            if (expression && expression->systemverilog != nullptr
                && expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::name) {
                actual_name = expression->systemverilog->text;
                if (expression->systemverilog->referenced_name
                    && expression->systemverilog
                        ->referenced_name->selected) {
                    const auto selected
                        = working_specialization.find_declaration(
                            *expression->systemverilog
                                ->referenced_name->selected);
                    if (selected && selected->systemverilog != nullptr) {
                        actual_name = selected->systemverilog->name;
                    }
                }
            }
            const auto actual = working_signals.find(actual_name);
            std::optional<SignalId> actual_signal;
            if (actual != working_signals.end()) {
                actual_signal = actual->second;
            } else if (direction == frontend::PortDirection::Input) {
                std::string constant_error;
                auto constant = evaluate_hir_systemverilog_constant(
                    working_specialization,
                    *binding.expression,
                    constant_error);
                if (constant && constant->packed.width() != 0U
                    && constant->packed.width()
                        <= hir_systemverilog_maximum_constant_width) {
                    if (design_.signals_.size()
                        > std::numeric_limits<SignalId>::max()) {
                        report(
                            "FSIM-ELAB-011",
                            "the design has too many signals for dense "
                            "32-bit IDs",
                            compiled_source_span(
                                *compiled_, binding.source));
                        return false;
                    }
                    const auto id = static_cast<SignalId>(
                        design_.signals_.size());
                    const auto adapter_name = child_path + ".$actual_"
                        + declaration.name;
                    const auto constant_domain = constant->domain;
                    SignalInfo info;
                    info.id = id;
                    info.name = adapter_name;
                    info.width = constant->packed.width();
                    info.type_name = constant->nominal_type;
                    info.source_domain = constant->domain;
                    info.is_signed = constant->signed_value;
                    info.declaration_span = compiled_source_span(
                        *compiled_, binding.source);
                    design_.signal_info_.push_back(std::move(info));
                    design_.signals_.push_back(Signal {
                        adapter_name,
                        std::move(constant->packed),
                        ResolutionKind::none,
                        value_kind(constant_domain),
                        std::nullopt,
                        { StrengthRank::pull, StrengthRank::pull },
                        std::nullopt,
                        std::nullopt,
                        frontend::SystemVerilogScalarKind::None,
                    });
                    design_.signal_by_name_.emplace(adapter_name, id);
                    actual_signal = id;
                }
            }
            if (!actual_signal) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "mixed-language port actual '" + actual_name
                        + "' is not a signal in '"
                        + std::string { working_path } + "'",
                    compiled_source_span(*compiled_, binding.source));
                return false;
            }
            std::optional<std::size_t> width;
            auto domain = frontend::ValueDomain::Unknown;
            if (const auto layout = compiled_vhdl_named_signal_layout(
                    *child_specialization,
                    *declaration.subtype,
                    declaration.scope)) {
                width = layout->width;
                domain = layout->domain;
            } else {
                width = compiled_vhdl_signal_width(
                    *declaration.subtype);
                domain = compiled_value_domain(
                    declaration.subtype->domain);
            }
            if (!width || !compiled_vhdl_scalar_domain(domain)) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "mixed-language VHDL port '"
                        + declaration.name
                        + "' has no executable scalar layout",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return false;
            }
            CompiledBoundaryPort port;
            port.name = declaration.name;
            port.type_name = declaration.subtype->type_mark.spelling;
            port.width = *width;
            port.domain = domain;
            if (const auto type = compiled_vhdl_signal_type(
                    *child_specialization,
                    *declaration.subtype,
                    declaration.scope)) {
                port.packed_aggregate = !type->packed_members.empty();
                port.vhdl_array = type->vhdl_array;
            }
            port.signed_value = declaration.subtype->signed_value
                || domain == frontend::ValueDomain::Integer;
            port.packed_range = compiled_packed_range(
                *declaration.subtype);
            port.integer_range = compiled_integer_range(
                *declaration.subtype);
            port.direction = direction;
            port.declaration_source = compiled_source_span(
                *compiled_, declaration.source);
            port.initial = compiled_vhdl_initial_value(
                *declaration.subtype, domain, *width);
            const auto formal_signal = connect_compiled_boundary_port(
                port,
                *actual_signal,
                child_path,
                compiled_source_span(*compiled_, binding.source),
                external_binding);
            if (!formal_signal) {
                return false;
            }
            child_aliases.emplace(declaration.name, *formal_signal);
        }
        if (!instantiate_compiled_vhdl_unit(
                CompiledVhdlInstantiationContext {
                    .unit = *child,
                    .path = child_path,
                    .actuals = std::move(child_actuals),
                    .port_aliases = std::move(child_aliases),
                    .source_instance = source_instance,
                    .prepared_specialization
                    = std::move(child_specialization),
                })) {
            return false;
        }
        return true;
    }
    return false;
}

HierarchyBuilder::CompiledVhdlGeneratedOccurrenceStatus
HierarchyBuilder::process_compiled_vhdl_generated_occurrence(
    CompiledVhdlGeneratedOccurrenceContext context)
{
    const auto& occurrence = context.occurrence;
    const auto& path = context.root_path;
    const auto& specialized = context.root_specialization;
    const auto& signals = context.root_signals;
    const auto& read_only_signals = context.root_read_only_signals;
    auto& generated_materializations = context.generated_materializations;
    const auto occurrence_path = path + "." + occurrence.relative_path;

    const SignalMap* parent_signals = &signals;
    const ReadOnlySignalSet* parent_read_only = &read_only_signals;
    const semantic::SpecializedHirUnit* parent_specialization
        = &specialized;
    for (auto candidate = generated_materializations.rbegin();
        candidate != generated_materializations.rend(); ++candidate) {
        if (occurrence_path.size() > candidate->path.size()
            && occurrence_path.starts_with(candidate->path)
            && occurrence_path[candidate->path.size()] == '.') {
            parent_signals = &candidate->signals;
            parent_read_only = &candidate->read_only_signals;
            parent_specialization = &candidate->specialization;
            break;
        }
    }
    auto working_specialization
        = parent_specialization->with_hierarchy_identities(
            occurrence.hierarchy_identities);
    const auto block_bindings
        = semantic::resolve_specialized_hir_vhdl_block_associations(
            *compiled_, *occurrence.region,
            working_specialization);
    for (const auto& issue : block_bindings.issues) {
        auto diagnostic = std::string { "FSIM-ELAB-VHBLOCK-001" };
        using AssociationDiagnostic
            = semantic::SpecializedHirAssociationDiagnostic;
        if (issue.diagnostic
            == AssociationDiagnostic::missing_type_actual) {
            diagnostic = "FSIM-ELAB-GENTYPE-001";
        } else if (issue.diagnostic
                == AssociationDiagnostic::callable_no_match
            && issue.callable_function) {
            diagnostic = *issue.callable_function
                ? "FSIM-ELAB-VHFUNC-005"
                : "FSIM-ELAB-VHPROC-005";
        }
        report(
            std::move(diagnostic),
            issue.message,
            compiled_source_span(*compiled_,
                issue.source.valid()
                    ? issue.source
                    : occurrence.region->source));
    }
    if (!block_bindings) {
        return CompiledVhdlGeneratedOccurrenceStatus::skipped;
    }

    std::vector<semantic::SpecializedHirActualIdentity> block_actuals;
    block_actuals.reserve(block_bindings.bindings.size());
    bool block_actuals_valid { true };
    for (const auto& binding : block_bindings.bindings) {
        if (binding.kind
                == semantic::SpecializedHirAssociationKind::open
            || (binding.kind
                    == semantic::SpecializedHirAssociationKind::default_value
                && !binding.actual_declaration)) {
            continue;
        }
        if (binding.identity.empty()) {
            report(
                "FSIM-ELAB-VHBLOCK-001",
                "compiled VHDL block generic association has no "
                "deterministic identity",
                compiled_source_span(*compiled_, binding.source));
            block_actuals_valid = false;
            continue;
        }
        auto actual = compiled_vhdl_specialization_actual(
            *compiled_, working_specialization, binding);
        if (actual.requires_static_value) {
            report(
                "FSIM-ELAB-VHBLOCK-001",
                "compiled VHDL block value generic is not locally "
                "static",
                compiled_source_span(*compiled_, binding.source));
            block_actuals_valid = false;
            continue;
        }
        block_actuals.push_back(std::move(actual.actual));
    }
    if (!block_actuals_valid) {
        return CompiledVhdlGeneratedOccurrenceStatus::skipped;
    }
    working_specialization
        = working_specialization.with_local_actual_identities(
            block_actuals);
    if (!validate_compiled_vhdl_generated_callables(
            occurrence.region->declarations,
            working_specialization)
        || !validate_compiled_vhdl_generated_declaration_visibility(
            occurrence.region->declarations,
            working_specialization)
        || !validate_compiled_vhdl_generated_constants(
            occurrence.region->declarations,
            working_specialization)) {
        return CompiledVhdlGeneratedOccurrenceStatus::skipped;
    }
    if (!materialize_compiled_vhdl_generated_block(
            VhdlGeneratedBlockMaterializationContext {
                .region = *occurrence.region,
                .occurrence_path = occurrence_path,
                .root_path = path,
                .working_specialization
                = std::move(working_specialization),
                .block_actuals = block_actuals,
                .block_bindings = block_bindings,
                .parent_signals = parent_signals,
                .parent_read_only_signals = parent_read_only,
                .architecture = context.architecture,
                .container_objects = context.container_objects,
                .vhdl_port_shape_identities
                = context.vhdl_port_shape_identities,
                .generated_materializations = generated_materializations,
            })) {
        return CompiledVhdlGeneratedOccurrenceStatus::failed;
    }
    return CompiledVhdlGeneratedOccurrenceStatus::materialized;
}

bool HierarchyBuilder::materialize_compiled_vhdl_generated_block(
    VhdlGeneratedBlockMaterializationContext context)
{
    const auto& region = context.region;
    const auto& occurrence_path = context.occurrence_path;
    const auto& path = context.root_path;
    auto& working_specialization = context.working_specialization;
    const auto& block_actuals = context.block_actuals;
    const auto& block_bindings = context.block_bindings;
    const auto* const parent_signals = context.parent_signals;
    const auto* const parent_read_only
        = context.parent_read_only_signals;
    const auto& architecture = context.architecture;
    auto& container_objects = context.container_objects;
    auto& vhdl_port_shape_identities
        = context.vhdl_port_shape_identities;
    auto& generated_materializations
        = context.generated_materializations;

    const auto block_actual = [&](
                                  const semantic::DeclarationId formal) {
        return std::ranges::find(
            block_actuals, formal,
            &semantic::SpecializedHirActualIdentity::declaration);
    };
    bool block_nonvalue_actuals_valid { true };
    for (const auto declaration_id : region.declarations) {
        const auto formal = working_specialization.find_declaration(
            declaration_id);
        if (!formal || formal->vhdl == nullptr) {
            continue;
        }
        const auto actual = block_actual(declaration_id);
        const auto actual_declaration
            = actual != block_actuals.end()
                && actual->actual_declaration
            ? working_specialization.find_declaration(
                  *actual->actual_declaration)
            : std::nullopt;
        const auto binding = std::ranges::find(
            block_bindings.bindings, declaration_id,
            &semantic::SpecializedHirAssociationBinding::formal);
        const auto source = binding != block_bindings.bindings.end()
            ? binding->source
            : formal->vhdl->source;
        switch (formal->vhdl->form) {
        case semantic::vhdl::DeclarationForm::generic_type:
            if (actual == block_actuals.end()) {
                report(
                    "FSIM-ELAB-GENTYPE-001",
                    "VHDL generic type '" + formal->vhdl->name
                        + "' requires an actual",
                    compiled_source_span(*compiled_, source));
                block_nonvalue_actuals_valid = false;
            }
            break;
        case semantic::vhdl::DeclarationForm::generic_function:
        case semantic::vhdl::DeclarationForm::generic_procedure: {
            const auto function = formal->vhdl->form
                == semantic::vhdl::DeclarationForm::generic_function;
            const semantic::CompiledDesignResolver profile_resolver {
                working_specialization
            };
            if (!actual_declaration
                || actual_declaration->vhdl == nullptr
                || !profile_resolver.vhdl_callable_profile_matches(
                    declaration_id,
                    actual->actual_declaration.value_or(
                        semantic::DeclarationId { }),
                    true)) {
                report(
                    function ? "FSIM-ELAB-VHFUNC-005"
                             : "FSIM-ELAB-VHPROC-005",
                    std::string { "VHDL generic " }
                        + (function ? "function" : "procedure")
                        + " actual for '" + formal->vhdl->name
                        + "' does not conform to its profile",
                    compiled_source_span(*compiled_, source));
                block_nonvalue_actuals_valid = false;
            }
            break;
        }
        case semantic::vhdl::DeclarationForm::generic_package:
            if (!actual->actual_declaration || !formal->vhdl->package
                || !actual_declaration
                || actual_declaration->vhdl == nullptr
                || !actual_declaration->vhdl->package
                || !semantic::CompiledDesignResolver {
                    working_specialization }
                    .vhdl_package_templates_match(declaration_id, *actual->actual_declaration)) {
                report(
                    "FSIM-ELAB-VHPKG-007",
                    "VHDL generic package actual for '"
                        + formal->vhdl->name
                        + "' was instantiated from a different template",
                    compiled_source_span(*compiled_, source));
                block_nonvalue_actuals_valid = false;
            } else if (!semantic::CompiledDesignResolver {
                           working_specialization }
                           .vhdl_package_generic_maps_match(
                               declaration_id,
                               *actual->actual_declaration)) {
                report(
                    "FSIM-ELAB-VHPKG-008",
                    "VHDL generic package actual for '"
                        + formal->vhdl->name
                        + "' does not conform to its generic map",
                    compiled_source_span(*compiled_, source));
                block_nonvalue_actuals_valid = false;
            }
            break;
        default:
            break;
        }
    }
    if (!block_nonvalue_actuals_valid) {
        return true;
    }
    VhdlHirMaterialization materialization {
        &region,
        std::move(working_specialization),
        occurrence_path,
        SignalMap { parent_signals },
        ReadOnlySignalSet { parent_read_only },
        { },
        { },
    };
    std::vector<const semantic::vhdl::Declaration*> block_ports;
    for (const auto declaration_id : region.declarations) {
        const auto declaration
            = materialization.specialization.find_declaration(
                declaration_id);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::port) {
            block_ports.push_back(declaration->vhdl);
        }
    }
    std::size_t positional_port { };
    bool saw_named_port { };
    std::vector<bool> associated_block_ports(block_ports.size());
    std::vector<semantic::DeclarationId>
        association_materialized_block_ports;
    std::vector<bool> local_block_ports(block_ports.size());
    std::vector<std::optional<std::pair<
        semantic::ExpressionId, semantic::SourceSpanId>>>
        block_input_drivers(block_ports.size());
    for (const auto& association : region.port_map) {
        const semantic::vhdl::Declaration* formal = nullptr;
        std::optional<std::size_t> formal_index;
        if (association.formal && association.formal->selected) {
            const auto declaration
                = materialization.specialization.find_declaration(
                    *association.formal->selected);
            if (declaration && declaration->vhdl != nullptr) {
                formal = declaration->vhdl;
            }
        } else if (association.formal) {
            const auto formal_name
                = association.formal->canonical.empty()
                ? std::string_view { association.formal->spelling }
                : std::string_view { association.formal->canonical };
            const auto found = std::ranges::find_if(
                block_ports,
                [&](const auto* candidate) {
                    return compiled_vhdl_name_equal(
                        candidate->name, formal_name);
                });
            if (found != block_ports.end()) {
                formal = *found;
            }
        }
        if (association.formal) {
            saw_named_port = true;
            if (formal != nullptr) {
                const auto found = std::ranges::find(
                    block_ports, formal->id,
                    &semantic::vhdl::Declaration::id);
                if (found != block_ports.end()) {
                    formal_index = static_cast<std::size_t>(
                        std::distance(block_ports.begin(), found));
                }
            }
        } else if (saw_named_port) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "a positional block port actual follows a named actual",
                compiled_source_span(*compiled_, association.source));
            continue;
        } else if (positional_port < block_ports.size()) {
            formal_index = positional_port++;
            formal = block_ports[*formal_index];
        }
        if (!formal_index) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                association.formal
                    ? "unknown block port formal '"
                        + association.formal->spelling + "'"
                    : "too many positional block port actuals",
                compiled_source_span(*compiled_, association.source));
            continue;
        }
        if (associated_block_ports[*formal_index]) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "block port formal '" + formal->name
                    + "' is associated more than once",
                compiled_source_span(*compiled_, association.source));
            continue;
        }
        associated_block_ports[*formal_index] = true;
        const bool writable_formal
            = formal->direction == semantic::vhdl::Direction::output
            || formal->direction
                == semantic::vhdl::Direction::inout
            || formal->direction
                == semantic::vhdl::Direction::buffer;
        if (association.kind
            == semantic::vhdl::AssociationKind::open) {
            if (formal->direction
                    == semantic::vhdl::Direction::input
                && !formal->initializer) {
                report(
                    "FSIM-ELAB-VHBLOCK-002",
                    "input block port '" + formal->name
                        + "' is open but has no default",
                    compiled_source_span(
                        *compiled_, association.source));
            }
            const auto defaulted_input
                = formal->direction
                    == semantic::vhdl::Direction::input
                && formal->initializer.has_value();
            if (writable_formal || defaulted_input) {
                local_block_ports[*formal_index] = true;
                if (defaulted_input) {
                    block_input_drivers[*formal_index] = std::pair {
                        *formal->initializer,
                        association.source,
                    };
                }
            }
            continue;
        }
        if (association.kind
                != semantic::vhdl::AssociationKind::expression
            || !association.expression) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "block port '" + formal->name
                    + "' requires an expression or open actual",
                compiled_source_span(*compiled_, association.source));
            continue;
        }
        const auto actual
            = materialization.specialization.find_expression(
                *association.expression);
        if (!actual || actual->vhdl == nullptr) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "block port '" + formal->name
                    + "' has no retained expression actual",
                compiled_source_span(*compiled_, association.source));
            continue;
        }
        if (writable_formal
            && actual->vhdl->kind
                != semantic::vhdl::ExpressionKind::name) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "output, buffer, or inout block port '"
                    + formal->name
                    + "' requires a writable signal actual",
                compiled_source_span(*compiled_, association.source));
            continue;
        }
        auto actual_name = std::string_view { actual->vhdl->text };
        if (actual->vhdl->referenced_name) {
            const auto& reference = *actual->vhdl->referenced_name;
            actual_name = reference.canonical.empty()
                ? std::string_view { reference.spelling }
                : std::string_view { reference.canonical };
        }
        const auto signal = parent_signals->find(
            std::string { actual_name });
        if (signal != parent_signals->end()) {
            const auto* actual_info
                = signal->second < design_.signal_info_.size()
                ? &design_.signal_info_[signal->second]
                : nullptr;
            const auto formal_type = formal->subtype
                ? compiled_vhdl_signal_type(
                      materialization.specialization,
                      *formal->subtype, formal->scope)
                : std::optional<PackedTypeMetadata> { };
            const auto formal_layout = formal->subtype
                ? compiled_vhdl_named_signal_layout(
                      materialization.specialization,
                      *formal->subtype, formal->scope)
                : std::optional<CompiledVhdlSignalLayout> { };
            std::optional<std::size_t> formal_width;
            auto formal_domain = frontend::ValueDomain::Unknown;
            if (formal_layout) {
                formal_width = formal_layout->width;
                formal_domain = formal_layout->domain;
            } else if (formal_type) {
                formal_width = formal_type->width();
                formal_domain = formal_type->domain;
            } else if (formal->subtype) {
                formal_width = compiled_vhdl_signal_width(
                    *formal->subtype);
                formal_domain = compiled_value_domain(
                    formal->subtype->domain);
            }
            const auto formal_range = formal_type
                ? formal_type->packed_range
                : formal->subtype
                ? compiled_packed_range(*formal->subtype)
                : std::optional<frontend::PackedRange> { };
            const auto same_packed_range = [&]() {
                if (actual_info == nullptr) {
                    return false;
                }
                if (!formal_range) {
                    return true;
                }
                if (!actual_info->packed_range) {
                    return false;
                }
                return formal_range->left
                    == actual_info->packed_range->left
                    && formal_range->right
                    == actual_info->packed_range->right
                    && formal_range->descending
                    == actual_info->packed_range->descending;
            };
            const bool formal_signed
                = (formal_type && formal_type->is_signed)
                || (formal->subtype
                    && formal->subtype->signed_value)
                || formal_domain == frontend::ValueDomain::Integer;
            const auto& formal_nominal = formal_type
                ? formal_type->nominal_type
                : std::string { };
            if (actual_info == nullptr || !formal_width
                || *formal_width != actual_info->width
                || formal_domain != actual_info->source_domain
                || formal_signed != actual_info->is_signed
                || !same_packed_range()
                || (!formal_nominal.empty()
                    && formal_nominal != actual_info->nominal_type)) {
                report(
                    "FSIM-ELAB-VHBLOCK-003",
                    "block port '" + formal->name
                        + "' does not match signal target '"
                        + std::string { actual_name } + "'",
                    compiled_source_span(
                        *compiled_, association.source));
                continue;
            }
            materialization.signals.insert_or_assign(
                formal->name, signal->second);
        } else if (formal->direction
            == semantic::vhdl::Direction::input) {
            local_block_ports[*formal_index] = true;
            block_input_drivers[*formal_index] = std::pair {
                *association.expression,
                association.source,
            };
        }
    }
    for (std::size_t index = 0;
        index < block_ports.size(); ++index) {
        const auto* port = block_ports[index];
        if (!associated_block_ports[index]
            && port->direction == semantic::vhdl::Direction::input
            && !port->initializer) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "required input block port '" + port->name
                    + "' has no actual or default",
                compiled_source_span(*compiled_, port->source));
        } else if (!associated_block_ports[index]
            && port->direction
                == semantic::vhdl::Direction::input
            && port->initializer) {
            local_block_ports[index] = true;
            block_input_drivers[index] = std::pair {
                *port->initializer,
                port->source,
            };
        } else if (!associated_block_ports[index]
            && port->direction
                != semantic::vhdl::Direction::input) {
            local_block_ports[index] = true;
        }
    }
    for (std::size_t index = block_ports.size(); index-- > 0U;) {
        if (!local_block_ports[index]) {
            continue;
        }
        const auto* port = block_ports[index];
        if (!materialize_compiled_vhdl_declaration(
                materialization.specialization,
                materialization.path, path, materialization.signals,
                materialization.read_only_signals,
                materialization.declared_signal_names,
                container_objects, vhdl_port_shape_identities,
                architecture.standard,
                *port)) {
            return false;
        }
        association_materialized_block_ports.push_back(port->id);
        if (!block_input_drivers[index]) {
            continue;
        }
        const auto destination
            = materialization.signals.find(port->name);
        if (destination == materialization.signals.end()) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled VHDL block input port '" + port->name
                    + "' has no materialized driver target",
                compiled_source_span(
                    *compiled_, block_input_drivers[index]->second));
            return false;
        }
        materialization.block_input_adapters.push_back({
            block_input_drivers[index]->first,
            destination->second,
            port->subtype,
            block_input_drivers[index]->second,
        });
    }
    for (const auto declaration_id :
        region.declarations) {
        if (std::ranges::find(
                association_materialized_block_ports,
                declaration_id)
            != association_materialized_block_ports.end()) {
            continue;
        }
        const auto declaration
            = materialization.specialization.find_declaration(
                declaration_id);
        if (!declaration || declaration->vhdl == nullptr
            || !materialize_compiled_vhdl_declaration(
                materialization.specialization,
                materialization.path, path, materialization.signals,
                materialization.read_only_signals,
                materialization.declared_signal_names,
                container_objects, vhdl_port_shape_identities,
                architecture.standard,
                *declaration->vhdl)) {
            return false;
        }
    }
    generated_materializations.push_back(
        std::move(materialization));
    return true;
}

} // namespace fsim::elaboration
