// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/runtime/vpi_type_descriptor.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <memory>
#include <set>
#include <variant>

namespace fsim::app::application_detail {
namespace {

    std::atomic<std::uint64_t> next_vpi_simulation { 1 };

    [[nodiscard]] bool vpi_language(const semantic::Language language)
    {
        return language == semantic::Language::verilog
            || language == semantic::Language::system_verilog;
    }

    [[nodiscard]] runtime::SystemVerilogVpiLanguage vpi_profile_language(
        const semantic::Language language)
    {
        return language == semantic::Language::verilog
            ? runtime::SystemVerilogVpiLanguage::Verilog2005
            : runtime::SystemVerilogVpiLanguage::SystemVerilog2017;
    }

    [[nodiscard]] runtime::SystemVerilogVpiLanguage vpi_profile_language(
        const frontend::StandardRevision revision)
    {
        switch (revision) {
        case frontend::StandardRevision::Verilog1995:
            return runtime::SystemVerilogVpiLanguage::Verilog1995;
        case frontend::StandardRevision::Verilog2001:
            return runtime::SystemVerilogVpiLanguage::Verilog2001;
        case frontend::StandardRevision::Verilog2001NoConfig:
            return runtime::SystemVerilogVpiLanguage::Verilog2001NoConfig;
        case frontend::StandardRevision::Verilog2005:
            return runtime::SystemVerilogVpiLanguage::Verilog2005;
        case frontend::StandardRevision::SystemVerilog2005:
            return runtime::SystemVerilogVpiLanguage::SystemVerilog2005;
        case frontend::StandardRevision::SystemVerilog2009:
            return runtime::SystemVerilogVpiLanguage::SystemVerilog2009;
        case frontend::StandardRevision::SystemVerilog2012:
            return runtime::SystemVerilogVpiLanguage::SystemVerilog2012;
        case frontend::StandardRevision::SystemVerilog2017:
        case frontend::StandardRevision::SystemVerilog2023:
            return runtime::SystemVerilogVpiLanguage::SystemVerilog2017;
        case frontend::StandardRevision::Vhdl1987:
        case frontend::StandardRevision::Vhdl1993:
        case frontend::StandardRevision::Vhdl2000:
        case frontend::StandardRevision::Vhdl2002:
        case frontend::StandardRevision::Vhdl2008:
        case frontend::StandardRevision::Vhdl2019:
            break;
        }
        return runtime::SystemVerilogVpiLanguage::SystemVerilog2017;
    }

    [[nodiscard]] runtime::SystemVerilogVpiLanguage vpi_profile_language(
        const BuiltProject& project,
        const semantic::design::Specialization& specialization)
    {
        auto identity = specialization.library + "::" + specialization.name;
        if (specialization.unit.value()
            < project.systemverilog_hir.units().size()) {
            const auto& unit = project.systemverilog_hir.units().at(
                specialization.unit.value());
            identity = unit.library + "::" + unit.name;
        }
        const auto found = project.verilog_unit_revisions.find(identity);
        return found == project.verilog_unit_revisions.end()
            ? vpi_profile_language(specialization.language)
            : vpi_profile_language(found->second);
    }

    void apply_vpi_unit_provenance(
        runtime::SystemVerilogVpiTypeInfo& type,
        const BuiltProject& project,
        const std::string_view identity)
    {
        const auto revision = project.verilog_unit_revisions.find(identity);
        const auto profile
            = project.verilog_unit_compatibility_profiles.find(identity);
        const auto unit = std::ranges::find_if(
            project.semantics.units(), [&](const auto& candidate) {
                return (candidate.language == semantic::Language::verilog
                           || candidate.language
                               == semantic::Language::system_verilog)
                    && candidate.library + "::" + candidate.name == identity;
            });
        if (revision == project.verilog_unit_revisions.end()
            || profile == project.verilog_unit_compatibility_profiles.end()
            || unit == project.semantics.units().end()) {
            return;
        }
        type.language = vpi_profile_language(revision->second);
        type.semantic_unit_id = unit->id.value();
        type.source_id = unit->source.value();
        type.semantic_unit = std::string { identity };
        type.standard = frontend::to_string(revision->second);
        type.compatibility_profile = profile->second;
        if (unit->source.value() < project.semantics.source_spans().size()) {
            const auto& span = project.semantics.source_spans().at(
                unit->source.value());
            type.source_line = span.begin.line;
            type.source_column = span.begin.column;
            if (!span.logical_name.empty()) {
                type.source_path = span.logical_name;
            } else if (span.file.value()
                       < project.semantics.source_files().size()) {
                type.source_path = project.semantics.source_files().at(
                    span.file.value()).physical_name;
            }
        }
    }

    [[nodiscard]] runtime::SystemVerilogVpiObjectKind vpi_instance_kind(
        const BuiltProject& project,
        const semantic::design::Specialization& specialization,
        const bool configured_root)
    {
        if (configured_root) {
            return runtime::SystemVerilogVpiObjectKind::Root;
        }
        if (specialization.language != semantic::Language::system_verilog) {
            return runtime::SystemVerilogVpiObjectKind::Module;
        }
        if (specialization.name.find(".interface(")
            != std::string::npos) {
            return runtime::SystemVerilogVpiObjectKind::Interface;
        }
        if (specialization.name.find(".program(") != std::string::npos) {
            return runtime::SystemVerilogVpiObjectKind::Program;
        }
        const auto unit = std::ranges::find(
            project.systemverilog_hir.units(), specialization.unit,
            &semantic::sv::Unit::id);
        if (unit == project.systemverilog_hir.units().end()) {
            return runtime::SystemVerilogVpiObjectKind::Module;
        }
        switch (unit->kind) {
        case semantic::sv::UnitKind::interface:
            return runtime::SystemVerilogVpiObjectKind::Interface;
        case semantic::sv::UnitKind::program:
            return runtime::SystemVerilogVpiObjectKind::Program;
        case semantic::sv::UnitKind::package:
            return runtime::SystemVerilogVpiObjectKind::Package;
        case semantic::sv::UnitKind::module:
            return runtime::SystemVerilogVpiObjectKind::Module;
        }
        return runtime::SystemVerilogVpiObjectKind::Module;
    }

    [[nodiscard]] runtime::SystemVerilogVpiTypeDescriptor
    vpi_hir_type_descriptor(const semantic::sv::TypeReference& source)
    {
        runtime::SystemVerilogVpiTypeDescriptor descriptor;
        if (!source.class_identity.empty()) {
            descriptor.kind
                = runtime::SystemVerilogVpiDescriptorKind::ClassHandle;
            descriptor.category
                = runtime::SystemVerilogVpiValueCategory::None;
            descriptor.width = 64U;
            descriptor.nominal_name = source.class_identity;
            return descriptor;
        }
        if (source.value_form == semantic::sv::TypeForm::string) {
            descriptor.kind = runtime::SystemVerilogVpiDescriptorKind::String;
            descriptor.category
                = runtime::SystemVerilogVpiValueCategory::String;
            descriptor.width = 0U;
            return descriptor;
        }

        const auto integral_category = [&] {
            const auto& spelling = source.target.spelling;
            const bool integer_atom = spelling == "byte"
                || spelling == "shortint" || spelling == "int"
                || spelling == "longint" || spelling == "integer";
            if (integer_atom) {
                return source.four_state
                    ? runtime::SystemVerilogVpiValueCategory::Integer4
                    : runtime::SystemVerilogVpiValueCategory::Integer2;
            }
            return source.four_state
                ? runtime::SystemVerilogVpiValueCategory::Logic4
                : runtime::SystemVerilogVpiValueCategory::Bit2;
        }();
        const auto width = source.executable_width.value_or(1U);
        if (width == 0U
            || width > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error {
                "VPI class property width exceeds the governed host descriptor range"
            };
        }
        descriptor.category = integral_category;
        descriptor.width = static_cast<std::uint32_t>(width);
        descriptor.is_signed = source.signed_value;
        if (source.packed_range && source.packed_range->left
            && source.packed_range->right) {
            runtime::SystemVerilogVpiTypeDescriptor element;
            element.category = integral_category;
            element.width = 1U;
            element.is_signed = source.signed_value;
            descriptor.kind
                = runtime::SystemVerilogVpiDescriptorKind::PackedArray;
            descriptor.category
                = runtime::SystemVerilogVpiValueCategory::None;
            descriptor.width = 0U;
            descriptor.is_signed = false;
            descriptor.ranges.push_back(
                { *source.packed_range->left,
                    *source.packed_range->right });
            descriptor.children.push_back(std::move(element));
        }

        if (source.container_form) {
            runtime::SystemVerilogVpiTypeDescriptor container;
            container.category
                = runtime::SystemVerilogVpiValueCategory::None;
            container.width = 0U;
            container.kind
                = *source.container_form
                    == semantic::sv::TypeForm::dynamic_array
                ? runtime::SystemVerilogVpiDescriptorKind::DynamicArray
                : *source.container_form == semantic::sv::TypeForm::queue
                ? runtime::SystemVerilogVpiDescriptorKind::Queue
                : *source.container_form
                    == semantic::sv::TypeForm::associative_array
                ? runtime::SystemVerilogVpiDescriptorKind::AssociativeArray
                : runtime::SystemVerilogVpiDescriptorKind::UnpackedArray;
            if (container.kind
                == runtime::SystemVerilogVpiDescriptorKind::UnpackedArray) {
                for (const auto& range : source.unpacked_dimensions) {
                    if (range.left && range.right) {
                        container.ranges.push_back(
                            { *range.left, *range.right });
                    }
                }
            }
            if (container.kind
                == runtime::SystemVerilogVpiDescriptorKind::AssociativeArray) {
                runtime::SystemVerilogVpiTypeDescriptor index;
                index.category
                    = runtime::SystemVerilogVpiValueCategory::Integer4;
                index.width = 32U;
                index.is_signed = true;
                container.children.push_back(std::move(index));
            }
            container.children.push_back(std::move(descriptor));
            descriptor = std::move(container);
        }
        return descriptor;
    }

    [[nodiscard]] runtime::SystemVerilogVpiTypeInfo
    vpi_descriptor_type(runtime::SystemVerilogVpiTypeDescriptor descriptor)
    {
        runtime::SystemVerilogVpiTypeInfo result;
        result.language
            = runtime::SystemVerilogVpiLanguage::SystemVerilog2017;
        if (descriptor.kind
                == runtime::SystemVerilogVpiDescriptorKind::Scalar
            || descriptor.kind
                == runtime::SystemVerilogVpiDescriptorKind::Enum) {
            result.category = descriptor.category;
            result.width = descriptor.width;
            result.is_signed = descriptor.is_signed;
        } else if (descriptor.kind
            == runtime::SystemVerilogVpiDescriptorKind::String) {
            result.category
                = runtime::SystemVerilogVpiValueCategory::String;
        } else if (descriptor.kind
            == runtime::SystemVerilogVpiDescriptorKind::PackedArray) {
            const auto layout
                = runtime::validate_systemverilog_vpi_descriptor(descriptor);
            if (!layout
                || layout.value.fixed_bits
                    > std::numeric_limits<std::uint32_t>::max()) {
                throw std::length_error {
                    "VPI class packed property exceeds the governed host descriptor range"
                };
            }
            result.category = descriptor.children.front().category;
            result.width
                = static_cast<std::uint32_t>(layout.value.fixed_bits);
            result.is_signed = descriptor.children.front().is_signed;
        }
        result.descriptor
            = std::make_shared<runtime::SystemVerilogVpiTypeDescriptor>(
                std::move(descriptor));
        return result;
    }

    [[nodiscard]] std::string occurrence_path(
        const std::string_view instance_path,
        const std::string_view local_path)
    {
        if (local_path.empty() || local_path == instance_path) {
            return std::string { instance_path };
        }
        if (local_path.starts_with(instance_path)
            && local_path.size() > instance_path.size()
            && local_path[instance_path.size()] == '.') {
            return std::string { local_path };
        }
        return std::string { instance_path } + '.' + std::string { local_path };
    }

    [[nodiscard]] runtime::SystemVerilogVpiDirection vpi_direction(
        const semantic::design::Direction direction)
    {
        switch (direction) {
        case semantic::design::Direction::input:
            return runtime::SystemVerilogVpiDirection::Input;
        case semantic::design::Direction::output:
        case semantic::design::Direction::buffer:
            return runtime::SystemVerilogVpiDirection::Output;
        case semantic::design::Direction::inout:
        case semantic::design::Direction::ref:
            return runtime::SystemVerilogVpiDirection::Inout;
        case semantic::design::Direction::unknown:
            return runtime::SystemVerilogVpiDirection::None;
        }
        return runtime::SystemVerilogVpiDirection::None;
    }

    [[nodiscard]] runtime::SystemVerilogVpiStrengthRank vpi_strength_rank(
        const runtime::simir::StrengthRank rank)
    {
        switch (rank) {
        case runtime::simir::StrengthRank::highz:
            return runtime::SystemVerilogVpiStrengthRank::HighZ;
        case runtime::simir::StrengthRank::small:
            return runtime::SystemVerilogVpiStrengthRank::Small;
        case runtime::simir::StrengthRank::medium:
            return runtime::SystemVerilogVpiStrengthRank::Medium;
        case runtime::simir::StrengthRank::weak:
            return runtime::SystemVerilogVpiStrengthRank::Weak;
        case runtime::simir::StrengthRank::large:
            return runtime::SystemVerilogVpiStrengthRank::Large;
        case runtime::simir::StrengthRank::pull:
            return runtime::SystemVerilogVpiStrengthRank::Pull;
        case runtime::simir::StrengthRank::strong:
            return runtime::SystemVerilogVpiStrengthRank::Strong;
        case runtime::simir::StrengthRank::supply:
            return runtime::SystemVerilogVpiStrengthRank::Supply;
        }
        return runtime::SystemVerilogVpiStrengthRank::Strong;
    }

    [[nodiscard]] runtime::SystemVerilogVpiDriveStrength vpi_drive_strength(
        const runtime::simir::DriveStrength strength)
    {
        return { vpi_strength_rank(strength.zero),
            vpi_strength_rank(strength.one) };
    }

    [[nodiscard]] runtime::SystemVerilogVpiStoredValue packed_value(
        runtime::PackedLogic4 value)
    {
        runtime::SystemVerilogVpiStoredValue result;
        result.payload = std::move(value);
        return result;
    }

    struct DecodedVpiParameter {
        runtime::SystemVerilogVpiTypeInfo type;
        runtime::SystemVerilogVpiStoredValue value;
    };

    template <typename Integer>
    [[nodiscard]] bool parse_decimal(
        const std::string_view text, Integer& value)
    {
        if (text.empty()) {
            return false;
        }
        const auto converted = std::from_chars(
            text.data(), text.data() + text.size(), value, 10);
        return converted.ec == std::errc { }
        && converted.ptr == text.data() + text.size();
    }

    [[nodiscard]] std::optional<DecodedVpiParameter>
    decode_vpi_parameter(
        const semantic::design::ParameterValue& parameter,
        const semantic::sv::Declaration& declaration,
        const semantic::Language language)
    {
        constexpr std::string_view prefix { "svconst-v3:b=" };
        auto canonical = std::string_view { parameter.identity };
        if (!canonical.starts_with(prefix)) {
            return std::nullopt;
        }
        canonical.remove_prefix(prefix.size());
        const auto take_field = [&](const std::string_view marker)
            -> std::optional<std::string_view> {
            const auto separator = canonical.find(marker);
            if (separator == std::string_view::npos) {
                return std::nullopt;
            }
            const auto field = canonical.substr(0U, separator);
            canonical.remove_prefix(separator + marker.size());
            return field;
        };

        const auto unbounded_field = take_field(":w=");
        const auto width_field = take_field(":s=");
        const auto signed_field = take_field(":u=");
        const auto unsized_field = take_field(":d=");
        const auto domain_field = take_field(":n=");
        const auto nominal_size_field = take_field(":");
        std::uint64_t width { };
        std::uint64_t domain_value { };
        std::uint64_t nominal_size { };
        if (!unbounded_field || !width_field || !signed_field
            || !unsized_field || !domain_field
            || !nominal_size_field || !parse_decimal(*width_field, width)
            || *unbounded_field != "0"
            || width == 0U
            || width > std::numeric_limits<std::uint32_t>::max()
            || (*signed_field != "0" && *signed_field != "1")
            || (*unsized_field != "0" && *unsized_field != "1")
            || !parse_decimal(*domain_field, domain_value)
            || domain_value
                > static_cast<std::uint64_t>(frontend::ValueDomain::String)
            || !parse_decimal(*nominal_size_field, nominal_size)
            || nominal_size > canonical.size()) {
            return std::nullopt;
        }
        canonical.remove_prefix(static_cast<std::size_t>(nominal_size));
        constexpr std::string_view value_marker { ":v=" };
        if (!canonical.starts_with(value_marker)) {
            return std::nullopt;
        }
        canonical.remove_prefix(value_marker.size());
        if (canonical.size() != width
            || std::ranges::any_of(canonical, [](const char digit) {
                   return digit != '0' && digit != '1' && digit != 'x'
                       && digit != 'X' && digit != 'z' && digit != 'Z';
               })) {
            return std::nullopt;
        }

        if (!declaration.type
            || (declaration.type->value_form
                && declaration.type->value_form
                    != semantic::sv::TypeForm::packed_integral)
            || declaration.type->container_form
            || !declaration.type->unpacked_dimensions.empty()) {
            return std::nullopt;
        }
        const bool implicit_type
            = declaration.type->target.spelling == "implicit";
        if (!implicit_type
            && (!declaration.type->executable_width
                || *declaration.type->executable_width != width
                || declaration.type->signed_value
                    != (*signed_field == "1"))) {
            return std::nullopt;
        }

        const auto domain = static_cast<frontend::ValueDomain>(domain_value);
        auto category = runtime::SystemVerilogVpiValueCategory::None;
        bool four_state { };
        switch (domain) {
        case frontend::ValueDomain::Bit2:
        case frontend::ValueDomain::Boolean:
            category = runtime::SystemVerilogVpiValueCategory::Bit2;
            break;
        case frontend::ValueDomain::Logic4:
            category = runtime::SystemVerilogVpiValueCategory::Logic4;
            four_state = true;
            break;
        case frontend::ValueDomain::Integer:
            category = runtime::SystemVerilogVpiValueCategory::Integer4;
            four_state = true;
            break;
        case frontend::ValueDomain::Unknown:
        case frontend::ValueDomain::Logic9:
        case frontend::ValueDomain::String:
            return std::nullopt;
        }
        if (!implicit_type && declaration.type->four_state != four_state) {
            return std::nullopt;
        }

        runtime::SystemVerilogVpiTypeInfo type;
        type.language = vpi_profile_language(language);
        type.category = category;
        type.width = static_cast<std::uint32_t>(width);
        type.is_signed = *signed_field == "1";
        type.is_constant = true;
        if (declaration.type->packed_range
            && declaration.type->packed_range->left
            && declaration.type->packed_range->right) {
            const auto left = *declaration.type->packed_range->left;
            const auto right = *declaration.type->packed_range->right;
            const auto magnitude = [](const std::int64_t value) {
                return value >= 0
                    ? static_cast<std::uint64_t>(value)
                    : static_cast<std::uint64_t>(-(value + 1)) + 1U;
            };
            std::uint64_t distance { };
            if ((left < 0) != (right < 0)) {
                const auto left_magnitude = magnitude(left);
                const auto right_magnitude = magnitude(right);
                if (right_magnitude
                    > std::numeric_limits<std::uint64_t>::max()
                        - left_magnitude) {
                    return std::nullopt;
                }
                distance = left_magnitude + right_magnitude;
            } else {
                distance = left >= right
                    ? static_cast<std::uint64_t>(left - right)
                    : static_cast<std::uint64_t>(right - left);
            }
            if (distance == std::numeric_limits<std::uint64_t>::max()) {
                return std::nullopt;
            }
            const auto range_width = distance + 1U;
            if (range_width != width) {
                return std::nullopt;
            }
            runtime::SystemVerilogVpiTypeDescriptor element;
            element.category
                = category == runtime::SystemVerilogVpiValueCategory::Bit2
                ? runtime::SystemVerilogVpiValueCategory::Bit2
                : runtime::SystemVerilogVpiValueCategory::Logic4;
            element.width = 1U;
            runtime::SystemVerilogVpiTypeDescriptor array;
            array.kind = runtime::SystemVerilogVpiDescriptorKind::PackedArray;
            array.category = runtime::SystemVerilogVpiValueCategory::None;
            array.width = 0U;
            array.ranges.push_back({ left, right });
            array.children.push_back(std::move(element));
            type.descriptor
                = std::make_shared<runtime::SystemVerilogVpiTypeDescriptor>(
                    std::move(array));
        }

        auto packed = runtime::PackedLogic4::from_msb_string(canonical);
        return DecodedVpiParameter {
            std::move(type),
            systemverilog_vpi_signal_value(
                std::move(packed), runtime::SystemVerilogScalarKind::None,
                category)
        };
    }

    [[nodiscard]] runtime::SystemVerilogVpiNetKind vpi_net_kind(
        const std::string_view name)
    {
        if (name == "wire") {
            return runtime::SystemVerilogVpiNetKind::Wire;
        }
        if (name == "tri") {
            return runtime::SystemVerilogVpiNetKind::Tri;
        }
        if (name == "wand") {
            return runtime::SystemVerilogVpiNetKind::Wand;
        }
        if (name == "wor") {
            return runtime::SystemVerilogVpiNetKind::Wor;
        }
        if (name == "tri0") {
            return runtime::SystemVerilogVpiNetKind::Tri0;
        }
        if (name == "tri1") {
            return runtime::SystemVerilogVpiNetKind::Tri1;
        }
        if (name == "supply0") {
            return runtime::SystemVerilogVpiNetKind::Supply0;
        }
        if (name == "supply1") {
            return runtime::SystemVerilogVpiNetKind::Supply1;
        }
        if (name == "uwire") {
            return runtime::SystemVerilogVpiNetKind::Uwire;
        }
        if (name == "trireg") {
            return runtime::SystemVerilogVpiNetKind::TriReg;
        }
        if (name == "triand") {
            return runtime::SystemVerilogVpiNetKind::TriAnd;
        }
        if (name == "trior") {
            return runtime::SystemVerilogVpiNetKind::TriOr;
        }
        return runtime::SystemVerilogVpiNetKind::None;
    }

    [[nodiscard]] runtime::SystemVerilogVpiValueCategory vpi_category(
        const semantic::Language language,
        const elaboration::SignalInfo& signal)
    {
        switch (signal.systemverilog_scalar) {
        case runtime::SystemVerilogScalarKind::ShortReal:
            return runtime::SystemVerilogVpiValueCategory::ShortReal;
        case runtime::SystemVerilogScalarKind::Real:
        case runtime::SystemVerilogScalarKind::Realtime:
            return runtime::SystemVerilogVpiValueCategory::Real;
        case runtime::SystemVerilogScalarKind::Time:
            return runtime::SystemVerilogVpiValueCategory::Time;
        case runtime::SystemVerilogScalarKind::Chandle:
            return runtime::SystemVerilogVpiValueCategory::Integer4;
        case runtime::SystemVerilogScalarKind::None:
            break;
        }
        if (signal.type_name == "event") {
            return runtime::SystemVerilogVpiValueCategory::Event;
        }
        const bool integer = signal.type_name == "integer"
            || signal.type_name == "byte" || signal.type_name == "shortint"
            || signal.type_name == "int" || signal.type_name == "longint";
        switch (signal.source_domain) {
        case frontend::ValueDomain::Bit2:
            return integer ? runtime::SystemVerilogVpiValueCategory::Integer2
                           : runtime::SystemVerilogVpiValueCategory::Bit2;
        case frontend::ValueDomain::Integer:
            return signal.type_name == "integer"
                ? runtime::SystemVerilogVpiValueCategory::Integer4
                : runtime::SystemVerilogVpiValueCategory::Integer2;
        case frontend::ValueDomain::Logic9:
            return vpi_language(language)
                ? runtime::SystemVerilogVpiValueCategory::Logic4
                : runtime::SystemVerilogVpiValueCategory::Logic9;
        case frontend::ValueDomain::Logic4:
            return integer ? runtime::SystemVerilogVpiValueCategory::Integer4
                           : runtime::SystemVerilogVpiValueCategory::Logic4;
        case frontend::ValueDomain::Unknown:
        case frontend::ValueDomain::Boolean:
        case frontend::ValueDomain::String:
            return runtime::SystemVerilogVpiValueCategory::Logic4;
        }
        return runtime::SystemVerilogVpiValueCategory::Logic4;
    }

    [[nodiscard]] runtime::SystemVerilogVpiTypeInfo packed_type(
        const semantic::Language language,
        const std::size_t width,
        const bool signed_value,
        const runtime::SystemVerilogVpiObjectKind kind,
        const elaboration::SignalInfo& signal,
        const runtime::SystemVerilogVpiDirection direction
        = runtime::SystemVerilogVpiDirection::None)
    {
        const auto category = vpi_category(language, signal);
        if ((width == 0U
                && category != runtime::SystemVerilogVpiValueCategory::Event)
            || width > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error {
                "VPI packed object width exceeds the governed host descriptor range"
            };
        }
        runtime::SystemVerilogVpiTypeInfo result;
        result.language = vpi_profile_language(language);
        result.category = category;
        result.net_kind = kind == runtime::SystemVerilogVpiObjectKind::Net
                || kind == runtime::SystemVerilogVpiObjectKind::Port
            ? vpi_net_kind(signal.systemverilog_net_type.empty()
                      ? signal.type_name
                      : signal.systemverilog_net_type)
            : runtime::SystemVerilogVpiNetKind::None;
        if (kind == runtime::SystemVerilogVpiObjectKind::Net
            && result.net_kind == runtime::SystemVerilogVpiNetKind::None) {
            result.net_kind = runtime::SystemVerilogVpiNetKind::Wire;
        }
        result.direction = direction;
        result.width = category == runtime::SystemVerilogVpiValueCategory::Event
            ? 0U
            : static_cast<std::uint32_t>(width);
        result.is_signed
            = signal.systemverilog_scalar
                == runtime::SystemVerilogScalarKind::None
            && signed_value;
        result.is_constant = kind == runtime::SystemVerilogVpiObjectKind::Driver;
        if (signal.packed_range
            && category != runtime::SystemVerilogVpiValueCategory::Event
            && category != runtime::SystemVerilogVpiValueCategory::Real
            && category != runtime::SystemVerilogVpiValueCategory::ShortReal
            && category != runtime::SystemVerilogVpiValueCategory::Time) {
            runtime::SystemVerilogVpiTypeDescriptor element;
            element.category
                = category == runtime::SystemVerilogVpiValueCategory::Bit2
                    || category
                        == runtime::SystemVerilogVpiValueCategory::Integer2
                ? runtime::SystemVerilogVpiValueCategory::Bit2
                : category == runtime::SystemVerilogVpiValueCategory::Logic9
                ? runtime::SystemVerilogVpiValueCategory::Logic9
                : runtime::SystemVerilogVpiValueCategory::Logic4;
            element.width = 1U;

            runtime::SystemVerilogVpiTypeDescriptor array;
            array.kind = runtime::SystemVerilogVpiDescriptorKind::PackedArray;
            array.category = runtime::SystemVerilogVpiValueCategory::None;
            array.width = 0U;
            array.ranges.push_back(
                { signal.packed_range->left, signal.packed_range->right });
            array.children.push_back(std::move(element));
            result.descriptor
                = std::make_shared<runtime::SystemVerilogVpiTypeDescriptor>(
                    std::move(array));
        }
        return result;
    }

    [[nodiscard]] runtime::SystemVerilogVpiValueCategory container_category(
        const runtime::simir::ContainerType& type)
    {
        switch (type.scalar_kind) {
        case runtime::SystemVerilogScalarKind::ShortReal:
            return runtime::SystemVerilogVpiValueCategory::ShortReal;
        case runtime::SystemVerilogScalarKind::Real:
        case runtime::SystemVerilogScalarKind::Realtime:
            return runtime::SystemVerilogVpiValueCategory::Real;
        case runtime::SystemVerilogScalarKind::Time:
            return runtime::SystemVerilogVpiValueCategory::Time;
        case runtime::SystemVerilogScalarKind::Chandle:
            return runtime::SystemVerilogVpiValueCategory::Integer4;
        case runtime::SystemVerilogScalarKind::None:
            return type.two_state
                ? runtime::SystemVerilogVpiValueCategory::Bit2
                : runtime::SystemVerilogVpiValueCategory::Logic4;
        }
        return runtime::SystemVerilogVpiValueCategory::Logic4;
    }

    [[nodiscard]] runtime::SystemVerilogVpiTypeDescriptor
    container_element_descriptor(const runtime::simir::ContainerType& type)
    {
        runtime::SystemVerilogVpiTypeDescriptor result;
        result.category = container_category(type);
        result.width = type.element_width;
        result.is_signed
            = type.scalar_kind == runtime::SystemVerilogScalarKind::None
            && type.signed_elements;
        return result;
    }

    [[nodiscard]] runtime::SystemVerilogVpiTypeInfo container_word_type(
        const semantic::Language language,
        const runtime::simir::ContainerType& type)
    {
        runtime::SystemVerilogVpiTypeInfo result;
        result.language = vpi_profile_language(language);
        result.category = container_category(type);
        result.width = type.element_width;
        result.is_signed
            = type.scalar_kind == runtime::SystemVerilogScalarKind::None
            && type.signed_elements;
        result.descriptor
            = std::make_shared<runtime::SystemVerilogVpiTypeDescriptor>(
                container_element_descriptor(type));
        return result;
    }

    [[nodiscard]] runtime::SystemVerilogVpiTypeInfo memory_type(
        const semantic::Language language,
        const runtime::simir::ContainerType& type)
    {
        auto descriptor = container_element_descriptor(type);
        for (auto dimension = type.dimensions.rbegin();
            dimension != type.dimensions.rend(); ++dimension) {
            runtime::SystemVerilogVpiTypeDescriptor array;
            array.kind
                = runtime::SystemVerilogVpiDescriptorKind::UnpackedArray;
            array.category = runtime::SystemVerilogVpiValueCategory::None;
            array.width = 0U;
            array.ranges.push_back(
                { dimension->first, dimension->second });
            array.children.push_back(std::move(descriptor));
            descriptor = std::move(array);
        }
        runtime::SystemVerilogVpiTypeInfo result;
        result.language = vpi_profile_language(language);
        result.descriptor
            = std::make_shared<runtime::SystemVerilogVpiTypeDescriptor>(
                std::move(descriptor));
        return result;
    }

    [[nodiscard]] std::string memory_word_name(
        const runtime::simir::ContainerType& type,
        const std::size_t ordinal)
    {
        auto remaining = ordinal;
        std::string result;
        for (std::size_t dimension = 0;
            dimension < type.dimensions.size(); ++dimension) {
            std::size_t stride = 1U;
            for (std::size_t nested = dimension + 1U;
                nested < type.dimensions.size(); ++nested) {
                const auto [left, right] = type.dimensions[nested];
                const auto count = static_cast<std::uint64_t>(
                                       std::abs(static_cast<std::int64_t>(left) - right))
                    + 1U;
                if (count > std::numeric_limits<std::size_t>::max() / stride) {
                    throw std::length_error {
                        "VPI memory dimension stride exceeds the host domain"
                    };
                }
                stride *= static_cast<std::size_t>(count);
            }
            const auto [left, right] = type.dimensions[dimension];
            const auto position = remaining / stride;
            remaining %= stride;
            const auto index = static_cast<std::int64_t>(left)
                + (left >= right ? -static_cast<std::int64_t>(position)
                                 : static_cast<std::int64_t>(position));
            result.push_back('[');
            result.append(std::to_string(index));
            result.push_back(']');
        }
        return result;
    }

    [[nodiscard]] fsim_vpi_handle_v1 create_checked(
        runtime::SystemVerilogVpiObjectRegistry& registry,
        runtime::SystemVerilogVpiObjectDescriptor descriptor,
        const std::string_view identity)
    {
        const auto created = registry.create(descriptor);
        if (!created) {
            const auto existing = registry.find(identity);
            const auto existing_detail = existing
                ? " existing-kind "
                    + std::to_string(static_cast<unsigned>(
                        existing.value->kind))
                    + " existing-parent "
                    + std::to_string(existing.value->parent)
                : std::string { };
            const auto driver_detail
                = descriptor.type && descriptor.type->driver_range
                ? " driver-offset "
                    + std::to_string(descriptor.type->driver_range->offset)
                    + " driver-width "
                    + std::to_string(descriptor.type->driver_range->width)
                    + " driver-whole "
                    + (descriptor.type->driver_range->whole ? "true" : "false")
                    + " object-width "
                    + std::to_string(descriptor.type->width)
                : std::string { };
            throw std::logic_error {
                "failed to publish VPI object '" + std::string { identity }
                + "' with error "
                + std::to_string(static_cast<unsigned>(created.error))
                + " kind "
                + std::to_string(static_cast<unsigned>(descriptor.kind))
                + " parent " + std::to_string(descriptor.parent)
                + " name '" + descriptor.name + "'" + existing_detail
                + driver_detail
            };
        }
        return created.value;
    }

} // namespace

std::unique_ptr<runtime::SystemVerilogVpiObjectRegistry>
make_empty_systemverilog_vpi_registry()
{
    auto identity = next_vpi_simulation.fetch_add(
        1U, std::memory_order_relaxed);
    if (identity == 0U) {
        identity = next_vpi_simulation.fetch_add(
            1U, std::memory_order_relaxed);
    }
    auto registry
        = std::make_unique<runtime::SystemVerilogVpiObjectRegistry>(identity);
    if (!registry->valid()) {
        throw std::overflow_error { "VPI simulation identity space exhausted" };
    }
    return registry;
}

SystemVerilogVpiPublishedDesign make_systemverilog_vpi_design(
    const BuiltProject& project,
    const runtime::simir::Interpreter& interpreter)
{
    SystemVerilogVpiPublishedDesign result;
    result.registry = make_empty_systemverilog_vpi_registry();

    std::set<std::uint32_t> relevant_specializations;
    std::map<std::uint32_t, runtime::SystemVerilogVpiLanguage>
        instance_profiles;
    for (const auto& specialization : project.design_ir.specializations()) {
        if (!vpi_language(specialization.language)) {
            continue;
        }
        relevant_specializations.insert(specialization.id.value());
        instance_profiles.insert_or_assign(specialization.instance.value(),
            vpi_profile_language(project, specialization));
    }

    std::map<std::string, const semantic::design::InstanceOccurrence*,
        std::less<>>
        relevant_instances_by_path;
    for (const auto& instance : project.design_ir.instances()) {
        if (instance_profiles.contains(instance.id.value())) {
            relevant_instances_by_path.emplace(instance.path, &instance);
        }
    }

    std::map<std::uint32_t, fsim_vpi_handle_v1> instances;
    std::map<std::string, fsim_vpi_handle_v1, std::less<>> instance_paths;
    std::map<std::string, fsim_vpi_handle_v1, std::less<>> published_scopes;
    std::set<std::uint32_t> visiting_instances;
    const auto publish_instance = [&](const auto& self,
                                      const auto& instance)
        -> fsim_vpi_handle_v1 {
        const auto existing = instances.find(instance.id.value());
        if (existing != instances.end()) {
            return existing->second;
        }
        const auto profile = instance_profiles.find(instance.id.value());
        if (profile == instance_profiles.end()) {
            return { };
        }
        const auto& specialization = project.design_ir.specializations().at(
            instance.specialization.value());
        const auto& semantic_unit = project.semantics.units().at(
            specialization.unit.value());
        const auto unit_identity
            = semantic_unit.library + "::" + semantic_unit.name;
        if (!visiting_instances.insert(instance.id.value()).second) {
            throw std::logic_error { "VPI hierarchy contains an instance cycle" };
        }

        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        const auto configured_root = std::ranges::find(
                                         project.design_ir.roots(), instance.path)
            != project.design_ir.roots().end();
        const semantic::design::InstanceOccurrence* ancestor { };
        if (!configured_root && instance.parent
            && instance_profiles.contains(instance.parent->value())) {
            ancestor = &project.design_ir.instances().at(
                instance.parent->value());
        }
        const auto has_non_vpi_parent = instance.parent
            && !instance_profiles.contains(instance.parent->value());
        auto ancestor_path = instance.path;
        const auto parent_separator = ancestor_path.find_last_of('.');
        ancestor_path = parent_separator == std::string::npos
            ? std::string { }
            : ancestor_path.substr(0U, parent_separator);
        while (!configured_root && !has_non_vpi_parent && ancestor == nullptr
            && !ancestor_path.empty()) {
            if (const auto found
                = relevant_instances_by_path.find(ancestor_path);
                found != relevant_instances_by_path.end()) {
                ancestor = found->second;
                break;
            }
            const auto separator = ancestor_path.find_last_of('.');
            ancestor_path = separator == std::string::npos
                ? std::string { }
                : ancestor_path.substr(0U, separator);
        }

        if (ancestor != nullptr) {
            descriptor.parent = self(self, *ancestor);
            const auto direct_parent_end = instance.path.find_last_of('.');
            const auto direct_parent = instance.path.substr(0U,
                direct_parent_end == std::string::npos ? 0U
                                                       : direct_parent_end);
            auto prefix = ancestor->path;
            if (direct_parent != prefix) {
                if (!direct_parent.starts_with(prefix)
                    || direct_parent.size() <= prefix.size()
                    || direct_parent[prefix.size()] != '.') {
                    throw std::logic_error {
                        "VPI instance parent does not own its hierarchy path"
                    };
                }
                auto offset = prefix.size() + 1U;
                while (offset < direct_parent.size()) {
                    const auto separator = direct_parent.find('.', offset);
                    const auto end = separator == std::string::npos
                        ? direct_parent.size()
                        : separator;
                    const auto component
                        = direct_parent.substr(offset, end - offset);
                    prefix.push_back('.');
                    prefix.append(component);
                    if (const auto existing_scope
                        = published_scopes.find(prefix);
                        existing_scope != published_scopes.end()) {
                        descriptor.parent = existing_scope->second;
                    } else {
                        runtime::SystemVerilogVpiObjectDescriptor scope;
                        scope.kind
                            = runtime::SystemVerilogVpiObjectKind::GenerateScope;
                        scope.parent = descriptor.parent;
                        scope.name = component;
                        runtime::SystemVerilogVpiTypeInfo scope_type;
                        scope_type.language
                            = profile->second;
                        apply_vpi_unit_provenance(
                            scope_type, project, unit_identity);
                        scope.type = scope_type;
                        descriptor.parent = create_checked(*result.registry,
                            std::move(scope), prefix);
                        published_scopes.emplace(prefix, descriptor.parent);
                    }
                    if (separator == std::string::npos) {
                        break;
                    }
                    offset = separator + 1U;
                }
            }
            descriptor.kind
                = vpi_instance_kind(project, specialization, false);
        } else {
            descriptor.kind
                = vpi_instance_kind(project, specialization, true);
        }
        descriptor.name = instance.name.empty() ? instance.path : instance.name;
        if (!configured_root && !descriptor.parent) {
            descriptor.name = instance.path;
            std::ranges::replace(descriptor.name, '.', '_');
        }
        runtime::SystemVerilogVpiTypeInfo scope_type;
        scope_type.language = profile->second;
        apply_vpi_unit_provenance(scope_type, project, unit_identity);
        descriptor.type = scope_type;
        const auto handle = create_checked(
            *result.registry, std::move(descriptor), instance.path);
        visiting_instances.erase(instance.id.value());
        instances.emplace(instance.id.value(), handle);
        instance_paths.emplace(instance.path, handle);
        return handle;
    };
    for (const auto& instance : project.design_ir.instances()) {
        if (instance_profiles.contains(instance.id.value())) {
            (void)publish_instance(publish_instance, instance);
        }
    }

    std::map<std::string, fsim_vpi_handle_v1, std::less<>> packages;
    for (const auto& unit : project.systemverilog_hir.units()) {
        if (unit.kind != semantic::sv::UnitKind::package) {
            continue;
        }
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = runtime::SystemVerilogVpiObjectKind::Package;
        descriptor.name = unit.name;
        runtime::SystemVerilogVpiTypeInfo type;
        if (const auto revision = project.verilog_unit_revisions.find(
                unit.library + "::" + unit.name);
            revision != project.verilog_unit_revisions.end()) {
            type.language = vpi_profile_language(revision->second);
        }
        apply_vpi_unit_provenance(
            type, project, unit.library + "::" + unit.name);
        descriptor.type = type;
        const auto package = create_checked(
            *result.registry, std::move(descriptor), unit.name);
        packages.emplace(unit.name, package);
        packages.emplace(unit.library + "::" + unit.name, package);
    }

    std::map<
        std::string,
        std::vector<std::pair<std::string, fsim_vpi_handle_v1>>,
        std::less<>> unit_instances;
    for (const auto& specialization : project.design_ir.specializations()) {
        if (!relevant_specializations.contains(specialization.id.value())) {
            continue;
        }
        const auto owner = instances.find(specialization.instance.value());
        if (owner == instances.end()) {
            continue;
        }
        const auto& unit = project.semantics.units().at(
            specialization.unit.value());
        const auto& occurrence = project.design_ir.instances().at(
            specialization.instance.value());
        unit_instances[unit.library + "::" + unit.name].push_back(
            {occurrence.path, owner->second});
    }

    for (const auto& source : project.systemverilog_hir.classes()) {
        auto class_profile
            = runtime::SystemVerilogVpiLanguage::SystemVerilog2017;
        if (const auto revision = project.verilog_unit_revisions.find(
                source.enclosing_identity);
            revision != project.verilog_unit_revisions.end()) {
            class_profile = vpi_profile_language(revision->second);
        }
        runtime::SystemVerilogVpiTypeDescriptor class_descriptor;
        class_descriptor.kind
            = runtime::SystemVerilogVpiDescriptorKind::Class;
        class_descriptor.category
            = runtime::SystemVerilogVpiValueCategory::None;
        class_descriptor.width = 0U;
        class_descriptor.nominal_name = source.canonical_identity.empty()
            ? source.name
            : source.canonical_identity;
        for (const auto& property : source.properties) {
            class_descriptor.children.push_back(
                vpi_hir_type_descriptor(property.type));
            class_descriptor.member_names.push_back(property.name);
        }

        const auto publish_class = [&](fsim_vpi_handle_v1 parent) {
            runtime::SystemVerilogVpiObjectDescriptor descriptor;
            descriptor.kind = runtime::SystemVerilogVpiObjectKind::Class;
            descriptor.name = source.name;
            descriptor.parent = parent;
            descriptor.type = vpi_descriptor_type(class_descriptor);
            descriptor.type->language = class_profile;
            apply_vpi_unit_provenance(
                *descriptor.type, project, source.enclosing_identity);
            const auto class_handle = create_checked(*result.registry,
                std::move(descriptor), class_descriptor.nominal_name);
            for (std::size_t index = 0;
                index < source.properties.size(); ++index) {
                runtime::SystemVerilogVpiObjectDescriptor property;
                property.kind
                    = runtime::SystemVerilogVpiObjectKind::ClassProperty;
                property.parent = class_handle;
                property.name = source.properties[index].name;
                property.type = vpi_descriptor_type(
                    class_descriptor.children[index]);
                property.type->language = class_profile;
                (void)create_checked(*result.registry, std::move(property),
                    class_descriptor.nominal_name + "::"
                        + source.properties[index].name);
            }
        };
        if (const auto package = packages.find(source.enclosing_identity);
            package != packages.end()) {
            publish_class(package->second);
            continue;
        }
        const auto owners = unit_instances.find(source.enclosing_identity);
        if (owners == unit_instances.end()) {
            publish_class({ });
            continue;
        }
        auto relative_name = source.canonical_identity;
        const auto unit_prefix = source.enclosing_identity + "::";
        if (relative_name.starts_with(unit_prefix)) {
            relative_name.erase(0U, unit_prefix.size());
        }
        const auto class_separator = relative_name.rfind('.');
        const auto generate_scope = class_separator == std::string::npos
            ? std::string { }
            : relative_name.substr(0U, class_separator);
        for (const auto& [instance_path, instance_handle] : owners->second) {
            auto parent = instance_handle;
            auto scope_path = instance_path;
            std::size_t begin = 0U;
            while (begin < generate_scope.size()) {
                const auto separator = generate_scope.find('.', begin);
                const auto end = separator == std::string::npos
                    ? generate_scope.size()
                    : separator;
                const auto component = generate_scope.substr(
                    begin, end - begin);
                scope_path += "." + component;
                if (const auto existing = published_scopes.find(scope_path);
                    existing != published_scopes.end()) {
                    parent = existing->second;
                } else {
                    runtime::SystemVerilogVpiObjectDescriptor scope;
                    scope.kind =
                        runtime::SystemVerilogVpiObjectKind::GenerateScope;
                    scope.parent = parent;
                    scope.name = component;
                    runtime::SystemVerilogVpiTypeInfo scope_type;
                    scope_type.language = class_profile;
                    apply_vpi_unit_provenance(
                        scope_type, project, source.enclosing_identity);
                    scope.type = scope_type;
                    parent = create_checked(
                        *result.registry, std::move(scope), scope_path);
                    published_scopes.emplace(scope_path, parent);
                }
                if (separator == std::string::npos) {
                    break;
                }
                begin = separator + 1U;
            }
            publish_class(parent);
        }
    }
    for (const auto& specialization : project.design_ir.specializations()) {
        if (specialization.language != semantic::Language::verilog) {
            continue;
        }
        const auto owner = instances.find(specialization.instance.value());
        if (owner == instances.end()) {
            throw std::logic_error {
                "VPI parameter specialization has no published instance"
            };
        }
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        for (const auto& parameter : specialization.parameters) {
            if (!parameter.declaration) {
                continue;
            }
            const auto declaration = std::ranges::find(
                project.systemverilog_hir.declarations(),
                *parameter.declaration,
                &semantic::sv::Declaration::id);
            if (declaration
                == project.systemverilog_hir.declarations().end()) {
                throw std::logic_error {
                    "VPI parameter declaration is absent from SystemVerilog HIR"
                };
            }
            if (declaration->form
                    != semantic::sv::DeclarationForm::parameter
                && declaration->form
                    != semantic::sv::DeclarationForm::local_parameter) {
                continue;
            }
            if (declaration->name != parameter.name
                || declaration->scope != specialization.scope) {
                throw std::logic_error {
                    "VPI parameter declaration does not match its specialization"
                };
            }
            auto decoded = decode_vpi_parameter(
                parameter, *declaration, specialization.language);
            if (!decoded) {
                throw std::logic_error {
                    "VPI parameter '" + instance.path + '.' + parameter.name
                    + "' has an invalid canonical value or type: identity '"
                    + parameter.identity + "'"
                };
            }
            runtime::SystemVerilogVpiObjectDescriptor descriptor;
            descriptor.kind = runtime::SystemVerilogVpiObjectKind::Parameter;
            descriptor.parent = owner->second;
            descriptor.name = parameter.name;
            descriptor.type = std::move(decoded->type);
            descriptor.type->language
                = vpi_profile_language(project, specialization);
            const auto path = instance.path + '.' + parameter.name;
            const auto handle = create_checked(
                *result.registry, std::move(descriptor), path);
            const auto bound = result.registry->bind_value(
                handle, std::move(decoded->value));
            if (bound != runtime::SystemVerilogVpiValueError::None) {
                throw std::logic_error {
                    "failed to bind VPI parameter '" + path
                    + "' with error "
                    + std::to_string(static_cast<unsigned>(bound))
                };
            }
        }
    }

    std::map<std::uint32_t, const semantic::design::Port*> ports;
    for (const auto& port : project.design_ir.ports()) {
        ports.emplace(port.object.value(), &port);
    }
    std::map<std::uint32_t, fsim_vpi_handle_v1> objects;
    std::map<std::string, std::pair<runtime::simir::SignalId, fsim_vpi_handle_v1>, std::less<>> published_paths;
    std::map<std::pair<fsim_vpi_handle_v1, std::string>,
        std::pair<runtime::simir::SignalId, fsim_vpi_handle_v1>>
        published_siblings;
    std::set<std::string, std::less<>> vpi_memory_paths;
    for (const auto& object : project.design_ir.objects()) {
        if (!relevant_specializations.contains(object.specialization.value())
            || object.kind != semantic::design::ObjectKind::container
            || object.runtime_index
                > std::numeric_limits<runtime::simir::ContainerObjectId>::max()) {
            continue;
        }
        const auto container
            = static_cast<runtime::simir::ContainerObjectId>(
                object.runtime_index);
        const auto& info = project.design.container_objects().at(container);
        const auto& type = info.type;
        if (info.is_port || info.slice_alias || object.parent_object
            || !type.fixed || type.dimensions.empty() || type.queue
            || type.associative
            || (type.element_kind
                    != runtime::simir::ContainerElementKind::Packed
                && type.element_kind
                    != runtime::simir::ContainerElementKind::Scalar)) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        vpi_memory_paths.insert(
            occurrence_path(instance.path, object.path));
    }
    std::vector<const semantic::design::Object*> vpi_objects;
    for (const auto& object : project.design_ir.objects()) {
        if (!relevant_specializations.contains(object.specialization.value())
            || object.kind != semantic::design::ObjectKind::signal
            || object.runtime_index
                > std::numeric_limits<runtime::simir::SignalId>::max()) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto path = occurrence_path(instance.path, object.path);
        // Runtime signal aliases used to connect a selected memory word are
        // implementation details. The Memory and its bound word children are
        // the canonical VPI hierarchy for the declaration.
        if (std::ranges::any_of(vpi_memory_paths,
                [&](const std::string_view memory_path) {
                    return path == memory_path
                        || (path.starts_with(memory_path)
                            && path.size() > memory_path.size()
                            && path[memory_path.size()] == '.');
                })) {
            continue;
        }
        vpi_objects.push_back(&object);
    }
    std::ranges::stable_sort(vpi_objects, [](const auto* lhs, const auto* rhs) {
        const auto lhs_depth = std::ranges::count(lhs->path, '.');
        const auto rhs_depth = std::ranges::count(rhs->path, '.');
        if (lhs_depth != rhs_depth) {
            return lhs_depth < rhs_depth;
        }
        if (lhs->parent_object.has_value()
            != rhs->parent_object.has_value()) {
            return !lhs->parent_object.has_value();
        }
        return lhs->path < rhs->path;
    });
    for (const auto* object_pointer : vpi_objects) {
        const auto& object = *object_pointer;
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto path = occurrence_path(instance.path, object.path);
        if (instance_paths.contains(path)) {
            if (!object.parent_object) {
                throw std::logic_error {
                    "VPI signal path collides with an instance scope"
                };
            }
            const auto canonical = objects.find(object.parent_object->value());
            if (canonical == objects.end()) {
                throw std::logic_error {
                    "VPI scope-colliding alias has no canonical object"
                };
            }
            objects.emplace(object.id.value(), canonical->second);
            continue;
        }
        const auto owner = instances.find(specialization.instance.value());
        if (owner == instances.end()) {
            throw std::logic_error { "VPI object has no published instance" };
        }
        const auto signal = static_cast<runtime::simir::SignalId>(
            object.runtime_index);
        const auto published = published_paths.find(path);
        if (published != published_paths.end()) {
            if (published->second.first != signal) {
                throw std::logic_error {
                    "VPI path aliases distinct runtime signals"
                };
            }
            objects.emplace(object.id.value(), published->second.second);
            continue;
        }
        auto object_parent = owner->second;
        const auto parent_separator = path.find_last_of('.');
        if (parent_separator != std::string::npos) {
            const auto parent_path = path.substr(0U, parent_separator);
            const auto published_parent = published_paths.find(parent_path);
            if (published_parent != published_paths.end()) {
                object_parent = published_parent->second.second;
            } else if (const auto instance_parent
                = instance_paths.find(parent_path);
                instance_parent != instance_paths.end()) {
                object_parent = instance_parent->second;
            } else {
                auto prefix = instance.path;
                if (parent_path.starts_with(prefix)
                    && parent_path.size() > prefix.size()
                    && parent_path[prefix.size()] == '.') {
                    auto offset = prefix.size() + 1U;
                    while (offset < parent_path.size()) {
                        const auto separator = parent_path.find('.', offset);
                        const auto end = separator == std::string::npos
                            ? parent_path.size()
                            : separator;
                        const auto component
                            = parent_path.substr(offset, end - offset);
                        prefix.push_back('.');
                        prefix.append(component);
                        if (const auto nested_object
                            = published_paths.find(prefix);
                            nested_object != published_paths.end()) {
                            object_parent = nested_object->second.second;
                        } else if (const auto nested_instance
                            = instance_paths.find(prefix);
                            nested_instance != instance_paths.end()) {
                            object_parent = nested_instance->second;
                        } else if (const auto existing_scope
                            = published_scopes.find(prefix);
                            existing_scope != published_scopes.end()) {
                            object_parent = existing_scope->second;
                        } else {
                            runtime::SystemVerilogVpiObjectDescriptor scope;
                            scope.kind
                                = runtime::SystemVerilogVpiObjectKind::GenerateScope;
                            scope.parent = object_parent;
                            scope.name = component;
                            runtime::SystemVerilogVpiTypeInfo scope_type;
                            scope_type.language
                                = vpi_profile_language(project, specialization);
                            scope.type = scope_type;
                            object_parent = create_checked(*result.registry,
                                std::move(scope), prefix);
                            published_scopes.emplace(prefix, object_parent);
                        }
                        if (separator == std::string::npos) {
                            break;
                        }
                        offset = separator + 1U;
                    }
                }
            }
        }
        const auto sibling_key = std::pair { object_parent, object.name };
        const auto sibling = published_siblings.find(sibling_key);
        if (sibling != published_siblings.end()) {
            if (sibling->second.first != signal) {
                throw std::logic_error {
                    "VPI sibling '" + path
                    + "' aliases distinct runtime signals "
                    + std::to_string(sibling->second.first) + " and "
                    + std::to_string(signal)
                };
            }
            objects.emplace(object.id.value(), sibling->second.second);
            published_paths.emplace(path, sibling->second);
            continue;
        }
        const auto port = ports.find(object.id.value());
        const auto& signal_info = project.design.signals().at(signal);
        const auto category
            = vpi_category(specialization.language, signal_info);
        const auto declared_net = vpi_net_kind(
            signal_info.systemverilog_net_type.empty()
                ? signal_info.type_name
                : signal_info.systemverilog_net_type);
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind
            = category == runtime::SystemVerilogVpiValueCategory::Event
            ? runtime::SystemVerilogVpiObjectKind::NamedEvent
            : port != ports.end()
            ? runtime::SystemVerilogVpiObjectKind::Port
            : declared_net != runtime::SystemVerilogVpiNetKind::None
                || project.design.signals().at(signal).resolution
                    != runtime::simir::ResolutionKind::none
            ? runtime::SystemVerilogVpiObjectKind::Net
            : runtime::SystemVerilogVpiObjectKind::Variable;
        descriptor.parent = object_parent;
        descriptor.name = object.name;
        const auto direction = port == ports.end()
            ? runtime::SystemVerilogVpiDirection::None
            : vpi_direction(port->second->direction);
        const auto& value = interpreter.signal_value(signal);
        descriptor.type = packed_type(specialization.language,
            value.width(), object.signed_value, descriptor.kind, signal_info,
            direction);
        descriptor.type->language
            = vpi_profile_language(project, specialization);
        const auto handle = create_checked(
            *result.registry, std::move(descriptor), path);
        if (category != runtime::SystemVerilogVpiValueCategory::Event) {
            const auto bound = result.registry->bind_value(handle,
                systemverilog_vpi_signal_value(value,
                    signal_info.systemverilog_scalar, category));
            if (bound != runtime::SystemVerilogVpiValueError::None) {
                throw std::logic_error {
                    "failed to bind live VPI value '" + path
                    + "' with error "
                    + std::to_string(static_cast<unsigned>(bound))
                };
            }
        }
        objects.emplace(object.id.value(), handle);
        published_paths.emplace(path, std::pair { signal, handle });
        published_siblings.emplace(
            std::move(sibling_key), std::pair { signal, handle });
        if (category != runtime::SystemVerilogVpiValueCategory::Event) {
            result.signals[signal].push_back(handle);
            result.handles.emplace(handle, signal);
            result.scalar_kinds.emplace(
                signal, signal_info.systemverilog_scalar);
            result.categories.emplace(signal, category);
        } else {
            result.events[signal].push_back(handle);
        }
    }

    for (const auto& object : project.design_ir.objects()) {
        if (!relevant_specializations.contains(object.specialization.value())
            || object.kind != semantic::design::ObjectKind::container
            || object.runtime_index
                > std::numeric_limits<runtime::simir::ContainerObjectId>::max()) {
            continue;
        }
        const auto container
            = static_cast<runtime::simir::ContainerObjectId>(
                object.runtime_index);
        const auto& info = project.design.container_objects().at(container);
        const auto& type = info.type;
        if (info.is_port || info.slice_alias || object.parent_object
            || !type.fixed || type.dimensions.empty() || type.queue
            || type.associative
            || (type.element_kind
                    != runtime::simir::ContainerElementKind::Packed
                && type.element_kind
                    != runtime::simir::ContainerElementKind::Scalar)) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        if (!vpi_language(specialization.language)) {
            continue;
        }
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto owner = instances.find(specialization.instance.value());
        if (owner == instances.end()) {
            throw std::logic_error { "VPI memory has no published instance" };
        }
        const auto path = occurrence_path(instance.path, object.path);
        auto parent = owner->second;
        const auto separator = path.find_last_of('.');
        if (separator != std::string::npos) {
            const auto parent_path = path.substr(0U, separator);
            if (const auto nested_instance = instance_paths.find(parent_path);
                nested_instance != instance_paths.end()) {
                parent = nested_instance->second;
            } else if (const auto nested_scope
                = published_scopes.find(parent_path);
                nested_scope != published_scopes.end()) {
                parent = nested_scope->second;
            } else if (parent_path.starts_with(instance.path)
                && parent_path.size() > instance.path.size()
                && parent_path[instance.path.size()] == '.') {
                auto prefix = instance.path;
                auto offset = prefix.size() + 1U;
                while (offset < parent_path.size()) {
                    const auto next = parent_path.find('.', offset);
                    const auto end = next == std::string::npos
                        ? parent_path.size()
                        : next;
                    const auto component
                        = parent_path.substr(offset, end - offset);
                    prefix.push_back('.');
                    prefix.append(component);
                    if (const auto existing
                        = published_scopes.find(prefix);
                        existing != published_scopes.end()) {
                        parent = existing->second;
                    } else {
                        runtime::SystemVerilogVpiObjectDescriptor scope;
                        scope.kind = runtime::SystemVerilogVpiObjectKind::GenerateScope;
                        scope.parent = parent;
                        scope.name = component;
                        runtime::SystemVerilogVpiTypeInfo scope_type;
                        scope_type.language
                            = vpi_profile_language(project, specialization);
                        scope.type = scope_type;
                        parent = create_checked(*result.registry,
                            std::move(scope), prefix);
                        published_scopes.emplace(prefix, parent);
                    }
                    if (next == std::string::npos) {
                        break;
                    }
                    offset = next + 1U;
                }
            }
        }

        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = runtime::SystemVerilogVpiObjectKind::Memory;
        descriptor.parent = parent;
        descriptor.name = object.name;
        descriptor.type = memory_type(specialization.language, type);
        descriptor.type->language
            = vpi_profile_language(project, specialization);
        const auto memory = create_checked(
            *result.registry, std::move(descriptor), path);
        objects.emplace(object.id.value(), memory);

        const auto& value = interpreter.container_object_value(container);
        const auto category = container_category(type);
        for (std::size_t ordinal = 0; ordinal < value.elements.size();
            ++ordinal) {
            runtime::SystemVerilogVpiObjectDescriptor word;
            word.kind = runtime::SystemVerilogVpiObjectKind::Variable;
            word.parent = memory;
            word.name = memory_word_name(type, ordinal);
            word.type = container_word_type(specialization.language, type);
            word.type->language
                = vpi_profile_language(project, specialization);
            const auto word_handle = create_checked(*result.registry,
                std::move(word), path + memory_word_name(type, ordinal));
            const auto bound = result.registry->bind_value(word_handle,
                systemverilog_vpi_signal_value(value.elements[ordinal],
                    type.scalar_kind, category));
            if (bound != runtime::SystemVerilogVpiValueError::None) {
                throw std::logic_error {
                    "failed to bind live VPI memory word '" + path
                    + memory_word_name(type, ordinal) + "'"
                };
            }
            result.container_words[container].push_back(
                { word_handle, ordinal });
            result.word_handles.emplace(
                word_handle, std::pair { container, ordinal });
        }
        result.container_scalar_kinds.emplace(container, type.scalar_kind);
        result.container_categories.emplace(container, category);
    }

    for (const auto& process : project.design_ir.processes()) {
        if (!relevant_specializations.contains(process.specialization.value())) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            process.specialization.value());
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto path = occurrence_path(instance.path, process.name);
        const semantic::sv::ConcurrentAssertion* assertion_source { };
        if (specialization.language
                == semantic::Language::system_verilog
            && specialization.unit.value()
                < project.systemverilog_hir.units().size()) {
            const auto& unit = project.systemverilog_hir.units().at(
                specialization.unit.value());
            const auto process_leaf = process.name.find_last_of('.');
            const auto local_name = process_leaf == std::string::npos
                ? std::string_view { process.name }
                : std::string_view { process.name }.substr(process_leaf + 1U);
            const auto found = std::ranges::find(
                unit.concurrent_assertions, local_name,
                &semantic::sv::ConcurrentAssertion::name);
            if (found != unit.concurrent_assertions.end()) {
                assertion_source = &*found;
            }
        }
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = runtime::SystemVerilogVpiObjectKind::Process;
        descriptor.parent = instances.at(specialization.instance.value());
        const auto leaf = path.find_last_of('.');
        descriptor.name = leaf == std::string::npos
            ? path
            : path.substr(leaf + 1U);
        if (const auto matching_scope = published_scopes.find(path);
            matching_scope != published_scopes.end()) {
            descriptor.parent = matching_scope->second;
            descriptor.name
                = "$process_" + std::to_string(process.runtime_index);
        } else if (leaf != std::string::npos) {
            const auto parent_path = path.substr(0U, leaf);
            if (const auto parent_scope = published_scopes.find(parent_path);
                parent_scope != published_scopes.end()) {
                descriptor.parent = parent_scope->second;
            } else if (const auto parent_instance
                = instance_paths.find(parent_path);
                parent_instance != instance_paths.end()) {
                descriptor.parent = parent_instance->second;
            } else if (const auto parent_object
                = published_paths.find(parent_path);
                parent_object != published_paths.end()) {
                descriptor.parent = parent_object->second.second;
            }
        }
        runtime::SystemVerilogVpiTypeInfo process_type;
        process_type.language = vpi_profile_language(project, specialization);
        descriptor.type = process_type;
        auto published_name = descriptor.name;
        for (std::uint32_t collision = 0U;; ++collision) {
            const auto sibling
                = result.registry->find_child(descriptor.parent, published_name);
            if (sibling.error
                == runtime::SystemVerilogVpiObjectError::NotFound) {
                break;
            }
            if (sibling.error
                != runtime::SystemVerilogVpiObjectError::None) {
                throw std::logic_error {
                    "failed to inspect VPI process siblings for '" + path
                    + "' with error "
                    + std::to_string(
                        static_cast<unsigned>(sibling.error))
                };
            }
            published_name
                = "$process_" + std::to_string(process.runtime_index);
            if (collision != 0U) {
                published_name.push_back('_');
                published_name.append(std::to_string(collision));
            }
        }
        descriptor.name = std::move(published_name);
        const auto process_handle = create_checked(
            *result.registry, std::move(descriptor), path);
        if (specialization.language
                == semantic::Language::system_verilog
            && (process.observed || assertion_source != nullptr)) {
            runtime::SystemVerilogVpiObjectDescriptor assertion;
            assertion.kind
                = runtime::SystemVerilogVpiObjectKind::Assertion;
            assertion.parent = process_handle;
            assertion.name = assertion_source == nullptr
                ? "$assertion"
                : assertion_source->name;
            runtime::SystemVerilogVpiTypeInfo assertion_type;
            assertion_type.language
                = vpi_profile_language(project, specialization);
            assertion.type = assertion_type;
            const auto assertion_handle = create_checked(
                *result.registry, std::move(assertion),
                path + ".$assertion");
            result.assertions.insert_or_assign(
                process.name, assertion_handle);
            result.assertions.insert_or_assign(path, assertion_handle);
            if (assertion_source != nullptr) {
                result.assertions.insert_or_assign(
                    assertion_source->name, assertion_handle);
            }
        }
    }

    for (const auto& driver : project.design_ir.drivers()) {
        const auto parent = objects.find(driver.object.value());
        if (parent == objects.end()) {
            continue;
        }
        const auto& object = project.design_ir.objects().at(
            driver.object.value());
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        const auto& process = project.design_ir.processes().at(
            driver.process.value());
        const auto signal = static_cast<runtime::simir::SignalId>(
            object.runtime_index);
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = runtime::SystemVerilogVpiObjectKind::Driver;
        descriptor.parent = parent->second;
        descriptor.name = "$driver_" + std::to_string(driver.id.value());
        auto stored = interpreter.driver_value(process.runtime_index, signal);
        const auto& signal_info = project.design.signals().at(signal);
        const auto category
            = vpi_category(specialization.language, signal_info);
        if (category == runtime::SystemVerilogVpiValueCategory::Event) {
            continue;
        }
        auto driver_type = packed_type(specialization.language,
            stored.width(), object.signed_value, descriptor.kind, signal_info);
        driver_type.language = vpi_profile_language(project, specialization);
        driver_type.driver_range = runtime::SystemVerilogVpiDriverRange {
            driver.whole ? 0U : driver.offset,
            driver.whole ? static_cast<std::uint32_t>(stored.width())
                         : driver.width,
            driver.whole
        };
        descriptor.type = std::move(driver_type);
        const auto handle = create_checked(
            *result.registry, std::move(descriptor), object.path);
        auto vpi_stored = systemverilog_vpi_signal_value(
            std::move(stored), signal_info.systemverilog_scalar, category);
        std::optional<runtime::SystemVerilogVpiDriveStrength> strength;
        const auto* logic
            = std::get_if<runtime::PackedLogic4>(&vpi_stored.payload);
        if (logic != nullptr && logic->width() == 1U) {
            strength = vpi_drive_strength(
                interpreter.process_program(process.runtime_index)
                    .drive_strength);
            vpi_stored.strength = strength;
        }
        const auto bound
            = result.registry->bind_value(handle, std::move(vpi_stored));
        if (bound != runtime::SystemVerilogVpiValueError::None) {
            throw std::logic_error { "failed to bind live VPI driver value" };
        }
        result.drivers[signal].push_back(
            { handle, process.runtime_index, strength });
    }
    return result;
}

runtime::SystemVerilogVpiStoredValue systemverilog_vpi_signal_value(
    runtime::PackedLogic4 value,
    const runtime::SystemVerilogScalarKind scalar_kind,
    const runtime::SystemVerilogVpiValueCategory category)
{
    if (value.is_logic9()) {
        value = runtime::collapse_to_logic4(value);
    }
    if (scalar_kind == runtime::SystemVerilogScalarKind::None
        || scalar_kind == runtime::SystemVerilogScalarKind::Chandle) {
        if (category == runtime::SystemVerilogVpiValueCategory::Bit2
            || category == runtime::SystemVerilogVpiValueCategory::Integer2) {
            runtime::PackedBit2 bits { value.width() };
            for (std::size_t index = 0; index < value.width(); ++index) {
                const auto bit = value.get(index);
                if (bit != runtime::Logic4::zero
                    && bit != runtime::Logic4::one) {
                    throw std::logic_error {
                        "two-state VPI value contains an unknown bit"
                    };
                }
                bits.set(index, bit == runtime::Logic4::one);
            }
            runtime::SystemVerilogVpiStoredValue result;
            result.payload = std::move(bits);
            return result;
        }
        return packed_value(std::move(value));
    }
    const auto scalar
        = runtime::decode_systemverilog_scalar_payload(value, scalar_kind);
    if (!scalar) {
        if (scalar_kind == runtime::SystemVerilogScalarKind::Time
            && value.width() == 64U && !value.is_logic9()) {
            return packed_value(std::move(value));
        }
        throw std::logic_error { "invalid packed VPI scalar payload" };
    }
    runtime::SystemVerilogVpiStoredValue result;
    switch (scalar_kind) {
    case runtime::SystemVerilogScalarKind::ShortReal:
        result.payload = *scalar.value.as_shortreal();
        break;
    case runtime::SystemVerilogScalarKind::Real:
    case runtime::SystemVerilogScalarKind::Realtime:
        result.payload = *scalar.value.as_real();
        break;
    case runtime::SystemVerilogScalarKind::Time:
        result.payload = *scalar.value.as_time();
        break;
    case runtime::SystemVerilogScalarKind::None:
    case runtime::SystemVerilogScalarKind::Chandle:
        break;
    }
    return result;
}

runtime::PackedLogic4 systemverilog_vpi_packed_value(
    const runtime::SystemVerilogVpiStoredValue& value,
    const runtime::SystemVerilogScalarKind scalar_kind,
    const runtime::SystemVerilogVpiValueCategory category)
{
    if (scalar_kind == runtime::SystemVerilogScalarKind::None
        || scalar_kind == runtime::SystemVerilogScalarKind::Chandle) {
        if (category == runtime::SystemVerilogVpiValueCategory::Bit2
            || category == runtime::SystemVerilogVpiValueCategory::Integer2) {
            const auto* bits = std::get_if<runtime::PackedBit2>(&value.payload);
            if (bits == nullptr) {
                throw std::logic_error { "VPI value is not packed bit" };
            }
            runtime::PackedLogic4 result {
                bits->width(), runtime::Logic4::zero
            };
            for (std::size_t index = 0; index < bits->width(); ++index) {
                result.set(index,
                    bits->get(index) ? runtime::Logic4::one
                                     : runtime::Logic4::zero);
            }
            return result;
        }
        const auto* packed = std::get_if<runtime::PackedLogic4>(&value.payload);
        if (packed == nullptr) {
            throw std::logic_error { "VPI value is not packed logic" };
        }
        return *packed;
    }
    runtime::SystemVerilogScalarValue scalar;
    switch (scalar_kind) {
    case runtime::SystemVerilogScalarKind::ShortReal: {
        const auto* payload = std::get_if<float>(&value.payload);
        if (payload == nullptr) {
            throw std::logic_error { "VPI value is not shortreal" };
        }
        scalar = runtime::SystemVerilogScalarValue::shortreal(*payload);
        break;
    }
    case runtime::SystemVerilogScalarKind::Real:
    case runtime::SystemVerilogScalarKind::Realtime: {
        const auto* payload = std::get_if<double>(&value.payload);
        if (payload == nullptr) {
            throw std::logic_error { "VPI value is not real" };
        }
        scalar = scalar_kind == runtime::SystemVerilogScalarKind::Realtime
            ? runtime::SystemVerilogScalarValue::realtime(*payload)
            : runtime::SystemVerilogScalarValue::real(*payload);
        break;
    }
    case runtime::SystemVerilogScalarKind::Time: {
        if (const auto* packed
            = std::get_if<runtime::PackedLogic4>(&value.payload)) {
            return *packed;
        }
        const auto* payload = std::get_if<std::uint64_t>(&value.payload);
        if (payload == nullptr) {
            throw std::logic_error { "VPI value is not time" };
        }
        scalar = runtime::SystemVerilogScalarValue::time(*payload);
        break;
    }
    case runtime::SystemVerilogScalarKind::None:
    case runtime::SystemVerilogScalarKind::Chandle:
        break;
    }
    auto packed = runtime::encode_systemverilog_scalar_payload(scalar);
    if (!packed) {
        throw std::logic_error { "VPI scalar cannot be encoded" };
    }
    return std::move(packed.value);
}

runtime::SystemVerilogVpiTimeProfile systemverilog_vpi_time_profile(
    const std::string_view resolution)
{
    const auto suffix = resolution.ends_with("fs") ? std::string_view { "fs" }
        : resolution.ends_with("ps")               ? std::string_view { "ps" }
        : resolution.ends_with("ns")               ? std::string_view { "ns" }
        : resolution.ends_with("us")               ? std::string_view { "us" }
        : resolution.ends_with("ms")               ? std::string_view { "ms" }
        : resolution.ends_with('s')                ? std::string_view { "s" }
                                                   : std::string_view { };
    auto exponent = suffix == "s" ? 0
        : suffix == "ms"          ? -3
        : suffix == "us"          ? -6
        : suffix == "ns"          ? -9
        : suffix == "ps"          ? -12
        : suffix == "fs"          ? -15
                                  : -9;
    std::uint64_t multiplier { };
    const auto magnitude = resolution.substr(
        0, resolution.size() - suffix.size());
    const auto [end, error]
        = std::from_chars(magnitude.data(), magnitude.data() + magnitude.size(),
            multiplier);
    if (suffix.empty() || error != std::errc { }
        || end != magnitude.data() + magnitude.size() || multiplier == 0U) {
        throw std::invalid_argument { "invalid VPI time resolution" };
    }
    while (multiplier % 10U == 0U && exponent < 0) {
        multiplier /= 10U;
        ++exponent;
    }
    return { static_cast<std::int8_t>(exponent),
        static_cast<std::int8_t>(exponent), multiplier };
}

} // namespace fsim::app::application_detail
