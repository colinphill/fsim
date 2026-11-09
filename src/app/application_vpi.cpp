// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_vpi_statements.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/semantic/systemverilog_structure_image.hpp"
#include "fsim/runtime/output_format.hpp"
#include "fsim/runtime/vpi_type_descriptor.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <atomic>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace fsim::app::application_detail {
namespace {

    std::atomic<std::uint64_t> next_vpi_simulation { 1 };

    // Heterogeneous lookup for the builder's path-keyed hash tables.
    struct PathHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(
            const std::string_view path) const noexcept
        {
            return std::hash<std::string_view> {}(path);
        }
    };

    struct SiblingHash {
        [[nodiscard]] std::size_t operator()(
            const std::pair<fsim_vpi_handle_v1, std::string>& key)
            const noexcept
        {
            return std::hash<std::string_view> {}(key.second)
                ^ (std::hash<fsim_vpi_handle_v1> {}(key.first)
                    * 0x9e3779b97f4a7c15ULL);
        }
    };

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
        const auto unit = std::ranges::find(
            project.systemverilog_hir.units(), specialization.unit,
            &semantic::sv::Unit::id);
        if (unit != project.systemverilog_hir.units().end()) {
            identity = unit->library + "::" + unit->name;
        }
        const auto found = project.verilog_unit_revisions.find(identity);
        return found == project.verilog_unit_revisions.end()
            ? vpi_profile_language(specialization.language)
            : vpi_profile_language(found->second);
    }

    void apply_vpi_source(
        runtime::SystemVerilogVpiTypeInfo& type,
        const BuiltProject& project,
        const std::optional<semantic::SourceSpanId> source)
    {
        if (!source || !source->valid()
            || source->value() >= project.semantics.source_spans().size()) {
            return;
        }
        type.source_id = source->value();
        const auto& span = project.semantics.source_spans().at(
            source->value());
        type.source_line = span.begin.line;
        type.source_column = span.begin.column;
        if (!span.logical_name.empty()) {
            type.source_path = span.logical_name;
        } else if (span.file.valid()
            && span.file.value()
                < project.semantics.source_files().size()) {
            type.source_path = project.semantics.source_files().at(
                span.file.value()).physical_name;
        }
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
        apply_vpi_source(type, project, unit->source);
    }

    // A VHDL instance's provenance as a VPI scope of a mixed design: its
    // entity in its library, and where its design unit is.
    void apply_vpi_vhdl_provenance(runtime::SystemVerilogVpiTypeInfo& type,
        const BuiltProject& project,
        const semantic::design::Specialization& specialization)
    {
        const auto& unit
            = project.semantics.units().at(specialization.unit.value());
        const auto provenance = std::ranges::find(
            project.vhdl_unit_provenance, unit.id, &VhdlUnitProvenance::unit);
        runtime::SystemVerilogVpiTypeInfo located;
        apply_vpi_source(located, project, unit.source);
        if (provenance == project.vhdl_unit_provenance.end()
            || located.source_path.empty()
            || provenance->standard.empty()
            || provenance->compatibility_profile.empty()) {
            return;
        }
        const auto& entity = unit.kind == semantic::UnitKind::vhdl_architecture
            ? unit.secondary_name
            : unit.name;
        type.semantic_unit_id = unit.id.value();
        type.source_id = located.source_id;
        type.semantic_unit = unit.library + "::" + entity;
        type.source_path = located.source_path;
        type.source_line = located.source_line;
        type.source_column = located.source_column;
        type.definition_path = located.source_path;
        type.definition_line = located.source_line;
        type.standard = provenance->standard;
        type.compatibility_profile = provenance->compatibility_profile;
    }

    void apply_vpi_specialization_provenance(
        runtime::SystemVerilogVpiTypeInfo& type,
        const BuiltProject& project,
        const semantic::design::Specialization& specialization)
    {
        auto identity = specialization.library + "::" + specialization.name;
        const auto unit = std::ranges::find(
            project.systemverilog_hir.units(), specialization.unit,
            &semantic::sv::Unit::id);
        if (unit != project.systemverilog_hir.units().end()) {
            identity = unit->library + "::" + unit->name;
        }
        apply_vpi_unit_provenance(type, project, identity);
    }

    // The library.cell name of the SystemVerilog configuration that controls
    // an instance's binding (IEEE 1800-2023 33.7 vpiConfig). The elaboration
    // records the controlling configuration as the "__configuration"
    // identity "sv-config-v1;<len>:<library>;<len>:<name>;..."; a nested
    // `use config` rule replaces it for the instances beneath.
    [[nodiscard]] std::string vpi_configuration_name(
        const elaboration::SpecializationInfo& recorded)
    {
        constexpr std::string_view prefix = "sv-config-v1;";
        for (const auto& [name, identity] : recorded.parameter_identity_values) {
            if (name != "__configuration"
                || !std::string_view { identity }.starts_with(prefix)) {
                continue;
            }
            std::string_view rest { identity };
            rest.remove_prefix(prefix.size());
            std::array<std::string, 2> fields;
            for (auto& field : fields) {
                const auto colon = rest.find(':');
                if (colon == std::string_view::npos) {
                    return { };
                }
                std::size_t length { };
                const auto parsed = std::from_chars(
                    rest.data(), rest.data() + colon, length);
                if (parsed.ec != std::errc { }
                    || rest.size() < colon + 1U + length + 1U) {
                    return { };
                }
                field = std::string { rest.substr(colon + 1U, length) };
                rest.remove_prefix(colon + 1U + length + 1U);
            }
            return fields[0].empty() ? fields[1] : fields[0] + '.' + fields[1];
        }
        return { };
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
        case semantic::sv::UnitKind::configuration:
        case semantic::sv::UnitKind::bind:
        case semantic::sv::UnitKind::compilation_unit:
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
        // The built-in classes of package std are class variables too
        // (IEEE 1800-2023 15.4, 15.3, 9.7, 37.33).
        if (const auto& spelling = source.target.spelling;
            spelling == "mailbox" || spelling == "semaphore"
            || spelling == "process" || spelling.starts_with("std::")) {
            const auto leaf = spelling.starts_with("std::")
                ? spelling.substr(5U)
                : spelling;
            if (leaf == "mailbox" || leaf == "semaphore" || leaf == "process") {
                descriptor.kind
                    = runtime::SystemVerilogVpiDescriptorKind::ClassHandle;
                descriptor.category
                    = runtime::SystemVerilogVpiValueCategory::None;
                descriptor.width = 64U;
                descriptor.nominal_name = leaf;
                return descriptor;
            }
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

    // The end of a path's parent. An escaped leaf identifier may itself
    // hold periods, so the leaf's own spelling decides where it starts
    // (IEEE 1800-2023 5.6.1, 23.6).
    [[nodiscard]] std::size_t path_parent_end(
        const std::string_view path, const std::string_view leaf)
    {
        if (!leaf.empty() && path.size() > leaf.size()
            && path.ends_with(leaf) && path[path.size() - leaf.size() - 1U] == '.') {
            return path.size() - leaf.size() - 1U;
        }
        return path.find_last_of('.');
    }

    // A VPI name for a design object. A simple identifier holds no period
    // or white space, so a name that does (an internal net derived from an
    // escaped port name) is published as an escaped identifier (IEEE
    // 1800-2023 5.6.1).
    [[nodiscard]] std::string vpi_identifier(const std::string_view name)
    {
        if (name.starts_with('\\')
            || std::ranges::none_of(name, [](const char character) {
                   return character == '.'
                       || std::isspace(static_cast<unsigned char>(character))
                       != 0;
               })) {
            return std::string { name };
        }
        return '\\' + std::string { name };
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
    decode_vpi_real_or_string_parameter(
        const std::string_view identity, const semantic::Language language)
    {
        runtime::SystemVerilogVpiTypeInfo type;
        type.language = vpi_profile_language(language);
        type.is_constant = true;
        constexpr std::string_view scalar_prefix { "svscalar-v1:k=" };
        if (identity.starts_with(scalar_prefix)) {
            const auto bits_marker = identity.find(":b=");
            unsigned kind { };
            std::uint64_t bits { };
            if (bits_marker == std::string_view::npos
                || !parse_decimal(identity.substr(scalar_prefix.size(),
                       bits_marker - scalar_prefix.size()), kind)) {
                return std::nullopt;
            }
            const auto bits_text = identity.substr(bits_marker + 3U);
            const auto parsed = std::from_chars(bits_text.data(),
                bits_text.data() + bits_text.size(), bits, 16);
            if (bits_text.empty() || parsed.ec != std::errc { }
                || parsed.ptr != bits_text.data() + bits_text.size()) {
                return std::nullopt;
            }
            runtime::SystemVerilogVpiStoredValue value;
            switch (static_cast<frontend::SystemVerilogScalarKind>(kind)) {
            case frontend::SystemVerilogScalarKind::Real:
            case frontend::SystemVerilogScalarKind::Realtime:
                type.category = runtime::SystemVerilogVpiValueCategory::Real;
                type.width = 64U;
                value.payload = std::bit_cast<double>(bits);
                break;
            case frontend::SystemVerilogScalarKind::ShortReal:
                type.category
                    = runtime::SystemVerilogVpiValueCategory::ShortReal;
                type.width = 32U;
                value.payload = std::bit_cast<float>(
                    static_cast<std::uint32_t>(bits));
                break;
            default:
                return std::nullopt;
            }
            return DecodedVpiParameter { std::move(type), std::move(value) };
        }
        constexpr std::string_view string_prefix { "svstring-v1;bytes=" };
        constexpr std::string_view hex_marker { ";hex=" };
        if (!identity.starts_with(string_prefix)) {
            return std::nullopt;
        }
        const auto marker = identity.find(hex_marker);
        std::size_t count { };
        if (marker == std::string_view::npos
            || !parse_decimal(identity.substr(string_prefix.size(),
                   marker - string_prefix.size()), count)) {
            return std::nullopt;
        }
        const auto hex = identity.substr(marker + hex_marker.size());
        if (hex.size() != count * 2U) {
            return std::nullopt;
        }
        std::string text;
        text.reserve(count);
        for (std::size_t index = 0U; index < hex.size(); index += 2U) {
            unsigned byte { };
            const auto parsed = std::from_chars(
                hex.data() + index, hex.data() + index + 2U, byte, 16);
            if (parsed.ec != std::errc { }
                || parsed.ptr != hex.data() + index + 2U) {
                return std::nullopt;
            }
            text.push_back(static_cast<char>(byte));
        }
        type.category = runtime::SystemVerilogVpiValueCategory::String;
        runtime::SystemVerilogVpiStoredValue value;
        value.payload = std::move(text);
        return DecodedVpiParameter { std::move(type), std::move(value) };
    }

    // The fields of a canonical constant identity (`svconst-v3:...:v=<bits>`):
    // the value's width, sign, domain and bits, or none when it is malformed
    // or unbounded.
    struct VpiCanonicalConstant {
        std::uint64_t width { };
        bool is_signed { };
        std::uint64_t domain { };
        std::string_view bits;
    };

    [[nodiscard]] std::optional<VpiCanonicalConstant>
    parse_vpi_canonical_constant(const std::string_view identity)
    {
        constexpr std::string_view prefix { "svconst-v3:b=" };
        auto canonical = identity;
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
        return VpiCanonicalConstant { width, *signed_field == "1",
            domain_value, canonical };
    }

    [[nodiscard]] std::optional<DecodedVpiParameter>
    decode_vpi_parameter(
        const semantic::design::ParameterValue& parameter,
        const semantic::sv::Declaration& declaration,
        const semantic::Language language)
    {
        // A real or string parameter has a scalar or string canonical
        // value (IEEE 1800-2023 6.20.2).
        if (auto special = decode_vpi_real_or_string_parameter(
                parameter.identity, language)) {
            return special;
        }
        const auto parsed = parse_vpi_canonical_constant(parameter.identity);
        if (!parsed) {
            return std::nullopt;
        }
        const auto width = parsed->width;
        const auto domain_value = parsed->domain;
        const auto canonical = parsed->bits;

        // A parameter declared with a typedef name takes its width, sign
        // and domain from the canonical value; the typedef's own structure
        // is the parameter's typespec (IEEE 1800-2023 37.28).
        const auto typedef_type = [&] {
            if (!declaration.type || !declaration.type->target.target.valid()) {
                return false;
            }
            using Form = semantic::sv::TypeForm;
            const auto& form = declaration.type->value_form;
            return !form || *form == Form::alias || *form == Form::enumeration
                || *form == Form::packed_structure
                || *form == Form::packed_union
                || *form == Form::packed_integral;
        }();
        if (!declaration.type
            || (!typedef_type && declaration.type->value_form
                && declaration.type->value_form
                    != semantic::sv::TypeForm::packed_integral)
            || declaration.type->container_form
            || !declaration.type->unpacked_dimensions.empty()) {
            return std::nullopt;
        }
        const bool implicit_type
            = declaration.type->target.spelling == "implicit" || typedef_type;
        if (!implicit_type
            && (!declaration.type->executable_width
                || *declaration.type->executable_width != width
                || declaration.type->signed_value != parsed->is_signed)) {
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
        // A two-state integer atom type is a decimal constant's (IEEE
        // 1800-2023 6.11, 37.3.2 vpiConstType).
        const auto& spelling = declaration.type->target.spelling;
        if ((spelling == "byte" || spelling == "shortint" || spelling == "int"
                || spelling == "longint")
            && (domain == frontend::ValueDomain::Integer
                || domain == frontend::ValueDomain::Bit2)) {
            category = runtime::SystemVerilogVpiValueCategory::Integer2;
            four_state = false;
        }
        if (!implicit_type && declaration.type->four_state != four_state) {
            return std::nullopt;
        }

        runtime::SystemVerilogVpiTypeInfo type;
        type.language = vpi_profile_language(language);
        type.category = category;
        type.width = static_cast<std::uint32_t>(width);
        type.is_signed = parsed->is_signed;
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
                distance = support::index_distance(left, right);
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
                    || category
                        == runtime::SystemVerilogVpiValueCategory::Integer2
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

    // The declared-type descriptor (the shape VPI keeps for a structure,
    // union, enumeration or packed array) of a typespec; none when the
    // typespec has no packed bit stream (a real, string, class or dynamic
    // member), the same members the elaborator's descriptors refuse.
    [[nodiscard]] std::optional<runtime::SystemVerilogVpiTypeDescriptor>
    vpi_descriptor_of_typespec(
        const runtime::SystemVerilogVpiTypespec& typespec,
        const unsigned depth = 0U)
    {
        using Kind = runtime::SystemVerilogVpiTypespecKind;
        using Descriptor = runtime::SystemVerilogVpiTypeDescriptor;
        using Category = runtime::SystemVerilogVpiValueCategory;
        const auto repeat = [&] {
            std::uint64_t count = 1U;
            for (const auto& range : typespec.ranges) {
                count *= support::index_distance(range.left, range.right) + 1U;
            }
            return count;
        };
        const auto scalar = [&](const Category category,
                                const std::uint64_t width)
            -> std::optional<Descriptor> {
            if (width == 0U || width > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            Descriptor result;
            result.category = category;
            result.width = static_cast<std::uint32_t>(width);
            result.is_signed = typespec.is_signed;
            return result;
        };
        switch (typespec.kind) {
        case Kind::Logic:
            return scalar(Category::Logic4, repeat());
        case Kind::Bit:
            return scalar(Category::Bit2, repeat());
        case Kind::Byte:
            return scalar(Category::Integer2, 8U);
        case Kind::ShortInt:
            return scalar(Category::Integer2, 16U);
        case Kind::Int:
            return scalar(Category::Integer2, 32U);
        case Kind::LongInt:
            return scalar(Category::Integer2, 64U);
        case Kind::Integer:
            return scalar(Category::Integer4, 32U);
        case Kind::Time:
            return scalar(Category::Time, 64U);
        case Kind::Enum: {
            // An enumeration without a base type is an int (6.19).
            auto base = typespec.element.empty()
                ? scalar(Category::Integer2, 32U)
                : vpi_descriptor_of_typespec(typespec.element.front(), depth + 1U);
            if (!base || base->kind != runtime::SystemVerilogVpiDescriptorKind::Scalar
                || base->width > 64U) {
                return std::nullopt;
            }
            if (typespec.element.empty()) {
                base->is_signed = true;
            }
            base->kind = runtime::SystemVerilogVpiDescriptorKind::Enum;
            base->enum_literals = typespec.enum_constants;
            return base;
        }
        case Kind::Struct:
        case Kind::Union: {
            Descriptor aggregate;
            aggregate.kind = typespec.kind == Kind::Struct
                ? runtime::SystemVerilogVpiDescriptorKind::Struct
                : runtime::SystemVerilogVpiDescriptorKind::Union;
            aggregate.category = Category::None;
            aggregate.width = 0U;
            aggregate.packed = typespec.packed;
            aggregate.is_signed = typespec.packed && typespec.is_signed;
            if (typespec.members.empty()
                || typespec.members.size() != typespec.member_names.size()) {
                return std::nullopt;
            }
            for (const auto& member : typespec.members) {
                auto child = vpi_descriptor_of_typespec(member, depth + 1U);
                if (!child) {
                    return std::nullopt;
                }
                aggregate.children.push_back(std::move(*child));
            }
            aggregate.member_names = typespec.member_names;
            return aggregate;
        }
        case Kind::PackedArray:
        case Kind::Array: {
            const bool packed = typespec.kind == Kind::PackedArray;
            if (typespec.element.size() != 1U || typespec.ranges.empty()
                || (!packed
                    && typespec.array_kind
                        != runtime::SystemVerilogVpiArrayKind::Static)) {
                return std::nullopt;
            }
            auto element = vpi_descriptor_of_typespec(
                typespec.element.front(), depth + 1U);
            if (!element) {
                return std::nullopt;
            }
            // Packed dimensions over a vector widen it (37.18).
            if (packed
                && element->kind == runtime::SystemVerilogVpiDescriptorKind::Scalar) {
                return scalar(element->category,
                    std::uint64_t { element->width } * repeat());
            }
            Descriptor array;
            array.kind = packed
                ? runtime::SystemVerilogVpiDescriptorKind::PackedArray
                : runtime::SystemVerilogVpiDescriptorKind::UnpackedArray;
            array.category = Category::None;
            array.width = 0U;
            array.ranges = typespec.ranges;
            array.children.push_back(std::move(*element));
            return array;
        }
        default:
            return std::nullopt;
        }
    }

    // An unpacked structure or union parameter is a constant structure var
    // (union var) whose members read from the parameter's canonical bit
    // stream (IEEE 1800-2023 6.20.2, 7.2, 37.17, 37.26; owner decision
    // 2026-11-05, as for an unpacked array parameter). `typespec` is the
    // typedef's typespec the elaborator recorded; none for a parameter whose
    // type is spelled in place.
    [[nodiscard]] std::optional<DecodedVpiParameter>
    decode_vpi_aggregate_parameter(
        const std::string_view identity,
        const semantic::sv::Declaration& declaration,
        const runtime::SystemVerilogVpiTypespec* const typespec,
        const semantic::Language language)
    {
        using Kind = runtime::SystemVerilogVpiTypespecKind;
        if (typespec == nullptr
            || (typespec->kind != Kind::Struct && typespec->kind != Kind::Union)
            || typespec->packed || !declaration.type
            || declaration.type->container_form
            || !declaration.type->unpacked_dimensions.empty()) {
            return std::nullopt;
        }
        const auto parsed = parse_vpi_canonical_constant(identity);
        auto descriptor = parsed ? vpi_descriptor_of_typespec(*typespec)
                                 : std::nullopt;
        if (!descriptor
            || runtime::systemverilog_vpi_declared_bits(*descriptor)
                != parsed->width) {
            return std::nullopt;
        }
        runtime::SystemVerilogVpiTypeInfo type;
        type.language = vpi_profile_language(language);
        type.category = runtime::SystemVerilogVpiValueCategory::Logic4;
        type.width = static_cast<std::uint32_t>(parsed->width);
        type.is_constant = true;
        type.declared
            = std::make_shared<const runtime::SystemVerilogVpiTypeDescriptor>(
                std::move(*descriptor));
        return DecodedVpiParameter { std::move(type),
            systemverilog_vpi_signal_value(
                runtime::PackedLogic4::from_msb_string(parsed->bits),
                runtime::SystemVerilogScalarKind::None,
                runtime::SystemVerilogVpiValueCategory::Logic4) };
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
        // An input or inout port is a net whatever drives the collapsed
        // signal it shares (IEEE 1800-2023 23.2.2.3, 37.15).
        if ((kind == runtime::SystemVerilogVpiObjectKind::Net
                || (kind == runtime::SystemVerilogVpiObjectKind::Port
                    && (direction == runtime::SystemVerilogVpiDirection::Input
                        || direction
                            == runtime::SystemVerilogVpiDirection::Inout)))
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
            // A vector of several packed dimensions keeps each of them, so
            // an index selects an element of the leftmost one (IEEE
            // 1800-2023 37.17 detail 4, 38.24); the typespec holds them
            // when they tile the value.
            if (const auto& typespec = signal.vpi_typespec; typespec
                && (typespec->kind == runtime::SystemVerilogVpiTypespecKind::Logic
                    || typespec->kind
                        == runtime::SystemVerilogVpiTypespecKind::Bit)
                && typespec->ranges.size() > 1U) {
                std::uint64_t product = 1U;
                for (const auto& range : typespec->ranges) {
                    product *= (range.left >= range.right
                                       ? static_cast<std::uint64_t>(
                                           range.left - range.right)
                                       : static_cast<std::uint64_t>(
                                           range.right - range.left))
                        + 1U;
                }
                if (product == width) {
                    for (const auto& range : typespec->ranges) {
                        array.ranges.push_back({ range.left, range.right });
                    }
                }
            }
            if (array.ranges.empty()) {
                array.ranges.push_back(
                    { signal.packed_range->left, signal.packed_range->right });
            }
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
        if (type.element_kind == runtime::simir::ContainerElementKind::String) {
            return runtime::SystemVerilogVpiValueCategory::String;
        }
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
        if (result.category == runtime::SystemVerilogVpiValueCategory::String) {
            result.kind = runtime::SystemVerilogVpiDescriptorKind::String;
            result.width = 0U;
            return result;
        }
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
        result.width
            = result.category == runtime::SystemVerilogVpiValueCategory::String
            ? 0U
            : type.element_width;
        result.is_signed
            = type.scalar_kind == runtime::SystemVerilogScalarKind::None
            && type.signed_elements
            && result.category != runtime::SystemVerilogVpiValueCategory::String;
        result.descriptor
            = std::make_shared<runtime::SystemVerilogVpiTypeDescriptor>(
                container_element_descriptor(type));
        return result;
    }

    // A word of an array of a multidimensional packed type keeps its
    // packed dimensions, leftmost first (IEEE 1800-2023 7.4.5, 37.17).
    void apply_packed_word_dimensions(runtime::SystemVerilogVpiTypeInfo& word,
        const semantic::sv::Hir& hir, const semantic::sv::Unit& unit, const std::string_view name)
    {
        const semantic::sv::Declaration* found { };
        for (const auto id : unit.declarations) {
            const auto* declaration = hir.find_declaration(id);
            if (declaration == nullptr || declaration->name != name) {
                continue;
            }
            if (found != nullptr) {
                return;
            }
            found = declaration;
        }
        if (found == nullptr || !found->type
            || found->type->packed_dimensions.size() < 2U || !word.descriptor
            || word.descriptor->kind
                != runtime::SystemVerilogVpiDescriptorKind::Scalar) {
            return;
        }
        runtime::SystemVerilogVpiTypeDescriptor array;
        array.kind = runtime::SystemVerilogVpiDescriptorKind::PackedArray;
        array.category = runtime::SystemVerilogVpiValueCategory::None;
        array.width = 0U;
        // A bound is its folded value or an integer literal; other bounds
        // keep the word's flat range.
        const auto bound = [&](const std::optional<std::int64_t> value,
                               const std::optional<semantic::ExpressionId> source)
            -> std::optional<std::int64_t> {
            if (value) {
                return value;
            }
            const auto* expression = source ? hir.find_expression(*source)
                                            : nullptr;
            if (expression == nullptr
                || expression->kind != semantic::sv::ExpressionKind::integer_literal) {
                return std::nullopt;
            }
            if (const auto based
                = semantic::sv::systemverilog_based_literal_bits(expression->text)) {
                return static_cast<std::int64_t>(*based);
            }
            std::int64_t parsed { };
            const auto& text = expression->text;
            const auto result
                = std::from_chars(text.data(), text.data() + text.size(), parsed);
            return result.ec == std::errc { }
                    && result.ptr == text.data() + text.size()
                ? std::optional { parsed }
                : std::nullopt;
        };
        std::uint64_t product = 1U;
        for (const auto& range : found->type->packed_dimensions) {
            const auto left = bound(range.left, range.left_expression);
            const auto right = bound(range.right, range.right_expression);
            if (!left || !right) {
                return;
            }
            product *= support::index_distance(*left, *right) + 1U;
            array.ranges.push_back({ *left, *right });
        }
        if (product != word.width) {
            return;
        }
        auto element = *word.descriptor;
        element.width = 1U;
        element.is_signed = false;
        array.children.push_back(std::move(element));
        word.descriptor
            = std::make_shared<runtime::SystemVerilogVpiTypeDescriptor>(
                std::move(array));
    }

    [[nodiscard]] runtime::SystemVerilogVpiTypeInfo memory_type(
        const semantic::Language language,
        const runtime::simir::ContainerType& type)
    {
        auto descriptor = container_element_descriptor(type);
        if (!type.fixed) {
            // A dynamic array or a queue holds its elements without a fixed
            // range (IEEE 1800-2023 7.5, 7.10).
            runtime::SystemVerilogVpiTypeDescriptor array;
            array.kind = type.associative
                ? runtime::SystemVerilogVpiDescriptorKind::AssociativeArray
                : type.queue
                ? runtime::SystemVerilogVpiDescriptorKind::Queue
                : runtime::SystemVerilogVpiDescriptorKind::DynamicArray;
            array.category = runtime::SystemVerilogVpiValueCategory::None;
            array.width = 0U;
            if (type.associative) {
                // An associative array's first child is its index type
                // (IEEE 1800-2023 37.25 detail 2).
                runtime::SystemVerilogVpiTypeDescriptor index;
                if (type.string_indices) {
                    index.kind
                        = runtime::SystemVerilogVpiDescriptorKind::String;
                    index.category
                        = runtime::SystemVerilogVpiValueCategory::String;
                    index.width = 0U;
                } else {
                    index.category = type.index_width == 32U
                            && type.signed_indices
                        ? runtime::SystemVerilogVpiValueCategory::Integer4
                        : runtime::SystemVerilogVpiValueCategory::Logic4;
                    index.width = type.index_width;
                    index.is_signed = type.signed_indices;
                }
                array.children.push_back(std::move(index));
            }
            array.children.push_back(std::move(descriptor));
            descriptor = std::move(array);
        }
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

    // A container VPI publishes as an array: a fixed unpacked array, a
    // dynamic array or a queue of packed, scalar or string elements
    // (IEEE 1800-2023 37.17).
    [[nodiscard]] bool vpi_memory_container(
        const runtime::simir::ContainerType& type)
    {
        using Kind = runtime::simir::ContainerElementKind;
        // An associative array is an array object of its entries in key
        // order (IEEE 1800-2023 37.17); its size is the entry count.
        if ((type.element_kind != Kind::Packed
                && type.element_kind != Kind::Scalar
                && type.element_kind != Kind::String)) {
            return false;
        }
        return type.fixed ? !type.dimensions.empty()
                          : type.dimensions.empty() && !type.vhdl_shaped;
    }

    [[nodiscard]] std::string memory_word_name(
        const runtime::simir::ContainerType& type,
        const std::size_t ordinal)
    {
        if (!type.fixed) {
            return "[" + std::to_string(ordinal) + "]";
        }
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
        // The type moves into the registry; a failure reports its driver
        // range.
        const auto driver_range = descriptor.type
            ? descriptor.type->driver_range
            : std::nullopt;
        const auto width = descriptor.type ? descriptor.type->width : 0U;
        const auto created = registry.create(
            runtime::SystemVerilogVpiObjectDescriptor {
                descriptor.kind,
                descriptor.parent,
                descriptor.name,
                std::move(descriptor.source),
                std::move(descriptor.type),
                descriptor.ordinal,
            });
        if (!created) {
            const auto existing = registry.find(identity);
            const auto existing_detail = existing
                ? " existing-kind "
                    + std::to_string(static_cast<unsigned>(
                        existing.value->kind))
                    + " existing-parent "
                    + std::to_string(existing.value->parent)
                : std::string { };
            const auto driver_detail = driver_range
                ? " driver-offset " + std::to_string(driver_range->offset)
                    + " driver-width " + std::to_string(driver_range->width)
                    + " driver-whole "
                    + (driver_range->whole ? "true" : "false")
                    + " object-width " + std::to_string(width)
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

namespace {

// Announce one deferred child of `parent` (see
// SystemVerilogVpiDeferredChildren).
void defer_child(runtime::SystemVerilogVpiObjectRegistry& registry,
    const fsim_vpi_handle_v1 parent, const std::string_view path)
{
    if (registry.defer_children(parent, 1U)
        != runtime::SystemVerilogVpiObjectError::None) {
        throw std::logic_error {
            "failed to defer a VPI object beneath '" + std::string { path }
            + "'"
        };
    }
}

// A process occurrence beneath `parent`; a name that a sibling already has
// becomes `$process_<index>` (IEEE 1800-2023 37.43).
[[nodiscard]] fsim_vpi_handle_v1 create_systemverilog_vpi_process(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const BuiltProject& project,
    const semantic::design::ProcessOccurrence& process,
    const fsim_vpi_handle_v1 parent, std::string name,
    const std::string& path)
{
    const auto& specialization = project.design_ir.specializations().at(
        process.specialization.value());
    runtime::SystemVerilogVpiObjectDescriptor descriptor;
    descriptor.kind = runtime::SystemVerilogVpiObjectKind::Process;
    descriptor.parent = parent;
    runtime::SystemVerilogVpiTypeInfo process_type;
    process_type.language = vpi_profile_language(project, specialization);
    apply_vpi_specialization_provenance(
        process_type, project, specialization);
    apply_vpi_source(process_type, project, process.source);
    descriptor.type = process_type;
    auto published_name = std::move(name);
    for (std::uint32_t collision = 0U;; ++collision) {
        const auto sibling = registry.find_child(parent, published_name);
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
    return create_checked(registry, std::move(descriptor), path);
}

// The driver of `driver.object` beneath its object `parent`, bound to its
// current contribution (IEEE 1800-2023 37.16); none for an event.
[[nodiscard]] std::optional<
    std::pair<runtime::simir::SignalId, SystemVerilogVpiDriverBinding>>
create_systemverilog_vpi_driver(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const BuiltProject& project,
    const runtime::simir::Interpreter& interpreter,
    const fsim_vpi_handle_v1 parent,
    const semantic::design::Driver& driver)
{
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
    descriptor.parent = parent;
    descriptor.name = "$driver_" + std::to_string(driver.id.value());
    auto stored = interpreter.driver_value(process.runtime_index, signal);
    const auto& signal_info = project.design.signals().at(signal);
    const auto category
        = vpi_category(specialization.language, signal_info);
    if (category == runtime::SystemVerilogVpiValueCategory::Event) {
        return std::nullopt;
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
        registry, std::move(descriptor),
        project.design_ir.path(object.path));
    const auto vpi_stored = systemverilog_vpi_signal_value(
        std::move(stored), signal_info.systemverilog_scalar, category);
    std::optional<runtime::SystemVerilogVpiDriveStrength> strength;
    const auto* logic
        = std::get_if<runtime::PackedLogic4>(&vpi_stored.payload);
    if (logic != nullptr && logic->width() == 1U) {
        const auto program = runtime::simir::InterpreterProgramAccess::view(
            interpreter, process.runtime_index);
        strength = vpi_drive_strength(program.drive_strength());
    }
    const auto bound = registry.bind_live_value(handle);
    if (bound != runtime::SystemVerilogVpiValueError::None) {
        throw std::logic_error { "failed to bind live VPI driver value" };
    }
    return std::pair { signal,
        SystemVerilogVpiDriverBinding {
            handle, process.runtime_index, strength } };
}

// A parameter whose value elaboration recorded beneath a generate block
// or a package, published under `parent` (IEEE 1800-2023 37.28).
void publish_recorded_vpi_parameter(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const BuiltProject& project, const fsim_vpi_handle_v1 parent,
    const std::string& path, const std::string& name,
    const std::string& identity, const semantic::DeclarationId declaration_id,
    const bool local, const runtime::SystemVerilogVpiTypeInfo& owner_type,
    const std::optional<std::uint64_t> ordinal = std::nullopt,
    std::shared_ptr<const runtime::SystemVerilogVpiTypespec> typespec = nullptr)
{
    const auto* const declaration
        = project.systemverilog_hir.find_declaration(declaration_id);
    if (declaration == nullptr
        || registry.find_child(parent, vpi_identifier(name))) {
        return;
    }
    const semantic::design::ParameterValue parameter {
        name, identity, identity, declaration_id
    };
    auto decoded = decode_vpi_parameter(
        parameter, *declaration, semantic::Language::system_verilog);
    if (!decoded) {
        decoded = decode_vpi_aggregate_parameter(identity, *declaration,
            typespec.get(), semantic::Language::system_verilog);
    }
    if (!decoded) {
        return;
    }
    if (declaration->initializer && declaration->type
        && declaration->type->target.spelling == "implicit") {
        const auto* const initializer
            = project.systemverilog_hir.find_expression(
                *declaration->initializer);
        decoded->type.string_literal = initializer != nullptr
            && initializer->kind
                == semantic::sv::ExpressionKind::string_literal;
    }
    auto type = std::move(decoded->type);
    type.local_parameter = local;
    if (typespec) {
        type.typespec = std::move(typespec);
    }
    type.language = owner_type.language;
    if (owner_type.semantic_unit_id) {
        type.semantic_unit_id = owner_type.semantic_unit_id;
        type.semantic_unit = owner_type.semantic_unit;
        type.standard = owner_type.standard;
        type.compatibility_profile = owner_type.compatibility_profile;
        apply_vpi_source(type, project, declaration->source);
        if (type.source_path.empty()) {
            type.source_id = owner_type.source_id;
            type.source_path = owner_type.source_path;
            type.source_line = owner_type.source_line;
            type.source_column = owner_type.source_column;
        }
    }
    runtime::SystemVerilogVpiObjectDescriptor descriptor;
    descriptor.kind = runtime::SystemVerilogVpiObjectKind::Parameter;
    descriptor.parent = parent;
    descriptor.name = vpi_identifier(name);
    descriptor.type = std::move(type);
    descriptor.ordinal = ordinal;
    const auto handle
        = create_checked(registry, std::move(descriptor), path);
    const auto bound = registry.bind_value(
        handle, std::move(decoded->value));
    if (bound != runtime::SystemVerilogVpiValueError::None) {
        throw std::logic_error {
            "failed to bind VPI parameter '" + path + "' with error "
            + std::to_string(static_cast<unsigned>(bound))
        };
    }
}

struct VpiGenvarMatch {
    const semantic::sv::GenerateRegion* region { };
    std::int64_t value { };
};

// The loop generate region whose genvar the generate block occurrence at
// `remainder` within its instance holds, with the genvar's value; none for
// any other occurrence.
std::optional<VpiGenvarMatch> vpi_generate_region_at(
    const std::vector<semantic::sv::GenerateRegion>& generates,
    const std::string_view remainder,
    const semantic::DeclarationId active = { })
{
    const auto* regions = &generates;
    const semantic::sv::GenerateRegion* region = nullptr;
    std::int64_t value { };
    std::size_t begin = 0U;
    while (begin < remainder.size()) {
        const auto rest = remainder.substr(begin);
        // The longest scope name the rest begins with: an `else` branch
        // named by the construct holds a dot (`blk.else`), and an unlabelled
        // block takes its implicit name `genblk<n>` (IEEE 1800-2023 27.6).
        region = nullptr;
        std::size_t length { };
        const auto consider = [&](const semantic::sv::GenerateRegion& candidate,
                                  const std::string_view name) {
            // Branches of one construct that share a scope name tie; the
            // branch the elaborator selected wins (27.5).
            if (!name.empty()
                && (name.size() > length
                    || (name.size() == length && active.valid()
                        && candidate.declaration == active))
                && rest.starts_with(name)
                && (rest.size() == name.size() || rest[name.size()] == '.'
                    || rest[name.size()] == '[')) {
                region = &candidate;
                length = name.size();
            }
        };
        // The branches of a construct are nested regions of its own region
        // with their own alternative discriminator; each is a sibling scope
        // of the construct's first branch, so it is considered at this level
        // and the scope it names resolves to the branch itself (27.5). An
        // unnamed branch that holds only nested constructs adds no scope
        // (as in the elaborator), so the scopes of those are siblings too.
        const auto visit = [&](const auto& self,
                               const semantic::sv::GenerateRegion& candidate)
            -> void {
            consider(candidate, candidate.label);
            // An `else` branch that shares its `if` branch's name and scope
            // has no label of its own (27.5).
            for (const auto& branch : candidate.nested) {
                if (branch.label.empty() && branch.scope == candidate.scope
                    && branch.alternative_discriminator
                        != candidate.alternative_discriminator) {
                    consider(branch, candidate.label);
                }
            }
            const bool transparent
                = (candidate.label.ends_with(".else")
                      || candidate.label.ends_with(".alternative"))
                && candidate.declarations.empty()
                && candidate.instances.empty()
                && candidate.processes.empty()
                && candidate.concurrent_statements.empty();
            for (const auto& branch : candidate.nested) {
                if (transparent
                    || branch.alternative_discriminator
                        != candidate.alternative_discriminator) {
                    self(self, branch);
                }
            }
        };
        for (const auto& candidate : *regions) {
            visit(visit, candidate);
        }
        if (region == nullptr) {
            return std::nullopt;
        }
        auto position = begin + length;
        if (position < remainder.size() && remainder[position] == '[') {
            const auto close = remainder.find(']', position);
            if (close == std::string_view::npos) {
                return std::nullopt;
            }
            const auto digits
                = remainder.substr(position + 1U, close - position - 1U);
            const auto parsed = std::from_chars(digits.data(),
                digits.data() + digits.size(), value, 10);
            if (parsed.ec != std::errc { }
                || parsed.ptr != digits.data() + digits.size()) {
                return std::nullopt;
            }
            position = close + 1U;
        } else {
            value = 0;
        }
        regions = &region->nested;
        if (position >= remainder.size()) {
            break;
        }
        if (remainder[position] != '.') {
            return std::nullopt;
        }
        begin = position + 1U;
    }
    if (region == nullptr) {
        return std::nullopt;
    }
    return VpiGenvarMatch { region, value };
}

std::optional<VpiGenvarMatch> vpi_genvar_region(
    const std::vector<semantic::sv::GenerateRegion>& generates,
    const std::string_view remainder)
{
    const auto match = vpi_generate_region_at(generates, remainder);
    if (!match || match->region->iterator.empty()
        || match->region->kind != semantic::sv::GenerateKind::iterative
        || remainder.empty() || remainder.back() != ']') {
        return std::nullopt;
    }
    return match;
}

// The named blocks and forks of the processes a labelled generate block
// holds are scopes beneath it (IEEE 1800-2023 27.6, 37.43, 38.36.1.1):
// published once, with their nested blocks, as the generate scope's own
// children. A block of a process named by it directly is the process.
void publish_vpi_generate_blocks(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const BuiltProject& project, const std::uint32_t unit,
    const std::string_view relative, const std::string& full_path,
    const fsim_vpi_handle_v1 scope_handle,
    const semantic::DeclarationId active)
{
    const auto& units = project.systemverilog_hir.units();
    if (unit >= units.size()
        || registry.kind_of(scope_handle)
            != runtime::SystemVerilogVpiObjectKind::GenerateScope) {
        return;
    }
    const auto match
        = vpi_generate_region_at(units[unit].generates, relative, active);
    if (!match) {
        return;
    }
    runtime::SystemVerilogVpiTypeInfo scope_type;
    if (const auto owner = registry.lookup(scope_handle);
        owner && owner.value->type) {
        scope_type = *owner.value->type;
    }
    scope_type.is_cell = false;
    const auto blocks = [&](const auto& self,
                            const std::vector<semantic::StatementId>& ids,
                            const std::string& path,
                            const fsim_vpi_handle_v1 owner) -> void {
        for (const auto id : ids) {
            const auto* const statement
                = semantic::find_by_id(project.systemverilog_hir.statements(), id);
            if (statement == nullptr) {
                continue;
            }
            auto nested_path = path;
            auto nested_owner = owner;
            const bool block
                = statement->kind == semantic::sv::StatementKind::block;
            if ((block
                    || statement->kind == semantic::sv::StatementKind::fork)
                && !statement->label.empty()
                && !registry.find_child(owner, vpi_identifier(statement->label))) {
                runtime::SystemVerilogVpiObjectDescriptor descriptor;
                descriptor.kind = block
                    ? runtime::SystemVerilogVpiObjectKind::Begin
                    : runtime::SystemVerilogVpiObjectKind::Fork;
                descriptor.parent = owner;
                descriptor.name = vpi_identifier(statement->label);
                descriptor.type = scope_type;
                nested_path = path + "." + statement->label;
                nested_owner = create_checked(
                    registry, std::move(descriptor), nested_path);
            }
            self(self, statement->statements, nested_path, nested_owner);
            self(self, statement->else_statements, nested_path, nested_owner);
        }
    };
    for (const auto process_id : match->region->processes) {
        if (const auto* const process
            = semantic::find_by_id(project.systemverilog_hir.processes(),
                process_id)) {
            blocks(blocks, process->statements, full_path, scope_handle);
        }
    }
}

// The number of block and fork scopes publish_vpi_generate_blocks creates
// directly beneath the generate scope at `relative` of a unit: the named
// blocks its processes hold outside any other named block, one per name.
std::uint32_t count_vpi_generate_blocks(const BuiltProject& project,
    const std::uint32_t unit, const std::string_view relative,
    const semantic::DeclarationId active)
{
    const auto& units = project.systemverilog_hir.units();
    if (unit >= units.size()) {
        return 0U;
    }
    const auto match
        = vpi_generate_region_at(units[unit].generates, relative, active);
    if (!match) {
        return 0U;
    }
    std::set<std::string_view> names;
    const auto visit = [&](const auto& self,
                           const std::vector<semantic::StatementId>& ids)
        -> void {
        for (const auto id : ids) {
            const auto* const statement
                = semantic::find_by_id(project.systemverilog_hir.statements(), id);
            if (statement == nullptr) {
                continue;
            }
            if ((statement->kind == semantic::sv::StatementKind::block
                    || statement->kind == semantic::sv::StatementKind::fork)
                && !statement->label.empty()) {
                names.insert(statement->label);
                continue;
            }
            self(self, statement->statements);
            self(self, statement->else_statements);
        }
    };
    for (const auto process_id : match->region->processes) {
        if (const auto* const process
            = semantic::find_by_id(project.systemverilog_hir.processes(),
                process_id)) {
            visit(visit, process->statements);
        }
    }
    return static_cast<std::uint32_t>(names.size());
}

// A genvar's references within the scope `remainder` of an instance that
// an iteration generates are local parameters of that scope (IEEE
// 1800-2023 37.85 detail 3): one int `vpiParameter` named like the genvar,
// holding the iteration's value, which is the index of the scope's name
// (27.4). Scopes of a block without a label take an implicit name and are
// not matched.
void publish_vpi_genvar(runtime::SystemVerilogVpiObjectRegistry& registry,
    const std::vector<semantic::sv::GenerateRegion>& generates,
    const std::string_view remainder, const std::string& path,
    const fsim_vpi_handle_v1 scope_handle)
{
    if (registry.kind_of(scope_handle)
        != runtime::SystemVerilogVpiObjectKind::GenerateScope) {
        return;
    }
    const auto match = vpi_genvar_region(generates, remainder);
    if (!match) {
        return;
    }
    const auto* const region = match->region;
    const auto value = match->value;
    const auto parameter_path = path + "." + region->iterator;
    if (registry.find_child(scope_handle, vpi_identifier(region->iterator))) {
        return;
    }
    runtime::SystemVerilogVpiTypeInfo type;
    if (auto scope = registry.lookup(scope_handle);
        scope && scope.value->type) {
        type = std::move(*scope.value->type);
    }
    type.category = runtime::SystemVerilogVpiValueCategory::Integer2;
    type.width = 32U;
    type.is_signed = true;
    type.is_constant = true;
    type.local_parameter = true;
    type.descriptor = nullptr;
    type.declared = nullptr;
    type.typespec = nullptr;
    std::string bits(32U, '0');
    const auto pattern = static_cast<std::uint32_t>(value);
    for (unsigned bit = 0U; bit < 32U; ++bit) {
        if (((pattern >> bit) & 1U) != 0U) {
            bits[31U - bit] = '1';
        }
    }
    runtime::SystemVerilogVpiObjectDescriptor descriptor;
    descriptor.kind = runtime::SystemVerilogVpiObjectKind::Parameter;
    descriptor.parent = scope_handle;
    descriptor.name = vpi_identifier(region->iterator);
    descriptor.type = std::move(type);
    const auto handle
        = create_checked(registry, std::move(descriptor), parameter_path);
    const auto bound = registry.bind_value(handle,
        systemverilog_vpi_signal_value(
            runtime::PackedLogic4::from_msb_string(bits),
            runtime::SystemVerilogScalarKind::None,
            runtime::SystemVerilogVpiValueCategory::Integer2));
    if (bound != runtime::SystemVerilogVpiValueError::None) {
        throw std::logic_error {
            "failed to bind VPI genvar '" + parameter_path + "'"
        };
    }
}

// The gen vars an instance's loop generates iterate with (IEEE 1800-2023
// 37.85): one `vpiGenVar` object per distinct genvar name, named like it
// beneath the instance (`vpi_iterate(vpiGenVar, instance)`, `vpiName`,
// `vpiFullName`). A genvar has no value of its own outside a gen scope,
// where its references are local parameters (detail 3). A name an object of
// the instance already holds is left to that object.
void publish_vpi_genvar_objects(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const std::vector<semantic::sv::GenerateRegion>& generates,
    const std::string& instance_path, const fsim_vpi_handle_v1 instance,
    const runtime::SystemVerilogVpiTypeInfo& instance_type)
{
    const auto publish = [&](const auto& self,
                             const std::vector<semantic::sv::GenerateRegion>&
                                 regions) -> void {
        for (const auto& region : regions) {
            if (region.kind == semantic::sv::GenerateKind::iterative
                && !region.iterator.empty()
                && !registry.find_child(
                    instance, vpi_identifier(region.iterator))) {
                runtime::SystemVerilogVpiObjectDescriptor descriptor;
                descriptor.kind = runtime::SystemVerilogVpiObjectKind::GenVar;
                descriptor.parent = instance;
                descriptor.name = vpi_identifier(region.iterator);
                auto type = instance_type;
                type.is_cell = false;
                descriptor.type = std::move(type);
                (void)create_checked(registry, std::move(descriptor),
                    instance_path + "." + region.iterator);
            }
            self(self, region.nested);
        }
    };
    publish(publish, generates);
}

// Create the deferred generate scopes beneath `parent`, each with the
// scopes above it, its parameters, then the arrays of the new scopes and
// their genvars, the order the design publication creates them in.
void materialize_vpi_generate_scopes(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const BuiltProject& project, const fsim_vpi_handle_v1 parent,
    const std::vector<SystemVerilogVpiDeferredGenerateScope>& scopes)
{
    runtime::SystemVerilogVpiTypeInfo scope_type;
    if (auto info = registry.lookup(parent); info && info.value->type) {
        scope_type = std::move(*info.value->type);
    }
    scope_type.is_cell = false;
    struct Created {
        fsim_vpi_handle_v1 handle { };
        fsim_vpi_handle_v1 parent { };
        std::string path;
        // The length of the instance's path, which `path` begins with.
        std::size_t instance { };
        std::optional<std::uint32_t> unit;
        // A published scope whose parameters and genvar were deferred.
        bool existing { };
    };
    std::vector<Created> created;
    const auto& recorded_specializations = project.design.specializations();
    // The reserved places of the scopes by path within their instance, for
    // the scopes between a deferred scope and the parent.
    std::unordered_map<std::string_view, std::uint64_t> places;
    for (const auto& deferred : scopes) {
        places.try_emplace(recorded_specializations.at(deferred.specialization)
                               .vpi_generate_scopes.at(deferred.scope)
                               .path,
            deferred.ordinal);
    }
    for (const auto& deferred : scopes) {
        const auto& recorded
            = recorded_specializations.at(deferred.specialization);
        const auto& scope = recorded.vpi_generate_scopes.at(deferred.scope);
        const std::string_view scope_path { scope.path };
        auto handle = parent;
        if (deferred.offset >= scope_path.size()) {
            // The parent is the scope: only its parameters and genvar wait.
            if (auto info = registry.lookup(parent); info) {
                created.push_back({ parent, info.value->parent,
                    recorded.instance + "." + scope.path,
                    recorded.instance.size(), deferred.unit, true });
            }
        }
        bool blocked { };
        for (std::size_t offset = deferred.offset;
             offset < scope_path.size();) {
            const auto separator = scope_path.find('.', offset);
            const auto end = separator == std::string_view::npos
                ? scope_path.size()
                : separator;
            const auto segment = scope_path.substr(offset, end - offset);
            if (const auto existing = registry.find_child(handle, segment)) {
                if (existing.value->kind
                    != runtime::SystemVerilogVpiObjectKind::GenerateScope) {
                    blocked = true;
                    break;
                }
                handle = existing.value->handle;
            } else {
                auto path = recorded.instance + ".";
                path.append(scope_path.substr(0U, end));
                runtime::SystemVerilogVpiObjectDescriptor descriptor;
                descriptor.kind
                    = runtime::SystemVerilogVpiObjectKind::GenerateScope;
                descriptor.parent = handle;
                descriptor.name = std::string { segment };
                descriptor.type = scope_type;
                if (const auto place = places.find(scope_path.substr(0U, end));
                    place != places.end()) {
                    descriptor.ordinal = place->second;
                }
                const auto made
                    = create_checked(registry, std::move(descriptor), path);
                created.push_back({ made, handle, std::move(path),
                    recorded.instance.size(), deferred.unit, false });
                handle = made;
            }
            offset = end + 1U;
        }
        if (blocked) {
            continue;
        }
        for (const auto& process : deferred.processes) {
            (void)create_systemverilog_vpi_process(registry, project,
                project.design_ir.processes().at(process.process), handle,
                process.name, process.path);
        }
        if (deferred.unit) {
            publish_vpi_generate_blocks(registry, project, *deferred.unit,
                scope_path, recorded.instance + "." + scope.path, handle,
                scope.region);
        }
        if (scope.parameters.empty()) {
            continue;
        }
        runtime::SystemVerilogVpiTypeInfo owner_type;
        if (auto info = registry.lookup(handle); info && info.value->type) {
            owner_type = std::move(*info.value->type);
        }
        const auto full_path = recorded.instance + "." + scope.path;
        auto parameter_place = deferred.ordinal;
        for (const auto& parameter : scope.parameters) {
            publish_recorded_vpi_parameter(registry, project, handle,
                full_path + "." + parameter.name, parameter.name,
                parameter.identity, parameter.declaration, parameter.local,
                owner_type, ++parameter_place, parameter.typespec);
        }
    }
    // The elements `name[i]` of a generate scope array share an array
    // object `name` beside them (IEEE 1800-2023 37.11, 37.85).
    for (const auto& scope : created) {
        if (scope.existing) {
            continue;
        }
        const auto leaf = scope.path.rfind('.');
        const auto name = std::string_view { scope.path }.substr(leaf + 1U);
        const auto open = name.find('[');
        if (open == std::string_view::npos || open == 0U
            || name.back() != ']'
            || registry.find_child(scope.parent, name.substr(0U, open))) {
            continue;
        }
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind
            = runtime::SystemVerilogVpiObjectKind::GenerateScopeArray;
        descriptor.parent = scope.parent;
        descriptor.name = std::string { name.substr(0U, open) };
        descriptor.type = scope_type;
        (void)create_checked(registry, std::move(descriptor),
            scope.path.substr(0U, leaf + 1U + open));
    }
    const auto& units = project.systemverilog_hir.units();
    for (const auto& scope : created) {
        if (scope.unit) {
            publish_vpi_genvar(registry, units.at(*scope.unit).generates,
                std::string_view { scope.path }.substr(scope.instance + 1U),
                scope.path, scope.handle);
        }
    }
}

} // namespace

SystemVerilogVpiPublishedDesign make_systemverilog_vpi_design(
    const BuiltProject& project,
    const runtime::simir::Interpreter& interpreter)
{
    SystemVerilogVpiPublishedDesign result;
    result.registry = make_empty_systemverilog_vpi_registry();
    // About one object per design object, and a few per instance (scopes,
    // parameters, ports), plus every generate block occurrence with its
    // parameters.
    std::size_t generate_objects { };
    for (const auto& recorded : project.design.specializations()) {
        for (const auto& scope : recorded.vpi_generate_scopes) {
            generate_objects += 1U + scope.parameters.size();
        }
    }
    result.registry->reserve(project.design_ir.objects().size()
        + 4U * project.design_ir.instances().size() + generate_objects);

    std::set<std::uint32_t> relevant_specializations;
    std::map<std::uint32_t, runtime::SystemVerilogVpiLanguage>
        instance_profiles;
    for (const auto& specialization : project.design_ir.specializations()) {
        if (!vpi_language(specialization.language)) {
            continue;
        }
        // A UDP instance is a vpiUdp the service builds from the primitive
        // records, not a module (IEEE 1800-2023 37.36).
        if (specialization.unit.valid()
            && specialization.unit.value() < project.semantics.units().size()
            && project.semantics.units()
                   .at(specialization.unit.value())
                   .name.starts_with("$unit$udp$")) {
            continue;
        }
        relevant_specializations.insert(specialization.id.value());
        instance_profiles.insert_or_assign(specialization.instance.value(),
            vpi_profile_language(project, specialization));
    }
    // In a mixed design each VHDL instance is a module scope without
    // objects, so the whole instance tree is visible and a path through a
    // VHDL instance resolves; VHDL objects belong to VHPI.
    if (!instance_profiles.empty()) {
        for (const auto& specialization :
            project.design_ir.specializations()) {
            if (specialization.language != semantic::Language::vhdl
                || !specialization.unit.valid()
                || specialization.unit.value()
                    >= project.semantics.units().size()) {
                continue;
            }
            const auto kind
                = project.semantics.units().at(specialization.unit.value()).kind;
            if ((kind == semantic::UnitKind::vhdl_entity
                    || kind == semantic::UnitKind::vhdl_architecture
                    || kind == semantic::UnitKind::vhdl_configuration)) {
                instance_profiles.try_emplace(specialization.instance.value(),
                    runtime::SystemVerilogVpiLanguage::SystemVerilog2017);
            }
        }
    }

    // Package startup processes have a parentless, package-named synthetic
    // specialization. Publish its runtime paths beneath the package object
    // created by the package inventory pass, not as a configured Root.
    std::map<std::uint32_t, std::string> package_initializer_instances;
    for (const auto& specialization : project.design_ir.specializations()) {
        if (!relevant_specializations.contains(specialization.id.value())) {
            continue;
        }
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto& unit = project.semantics.units().at(
            specialization.unit.value());
        // A package's path may equal a root module's name: packages and
        // definitions are separate namespaces (IEEE 1800-2023 3.13).
        if (!instance.parent
            && unit.kind == semantic::UnitKind::systemverilog_package
            && project.design_ir.path(instance.path) == unit.name) {
            package_initializer_instances.emplace(
                specialization.instance.value(),
                unit.library + "::" + unit.name);
        }
    }

    // A package's scope is named `pkg::`, a namespace apart from the
    // design's instances (IEEE 1800-2023 3.13, 37.10).
    const auto vpi_scope_path = [&](const auto& instance) {
        auto path = std::string { project.design_ir.path(instance.path) };
        if (package_initializer_instances.contains(instance.id.value())) {
            path.append("::");
        }
        return path;
    };
    const auto vpi_occurrence_path = [&](const auto& instance,
                                         const std::string_view local) {
        const auto design_path = project.design_ir.path(instance.path);
        auto path = occurrence_path(design_path, local);
        if (!package_initializer_instances.contains(instance.id.value())) {
            return path;
        }
        return std::string { design_path } + "::"
            + (path.size() > design_path.size()
                    ? path.substr(design_path.size() + 1U)
                    : std::string { });
    };

    std::map<std::string, const semantic::design::InstanceOccurrence*,
        std::less<>>
        relevant_instances_by_path;
    for (const auto& instance : project.design_ir.instances()) {
        if (instance_profiles.contains(instance.id.value())
            && !package_initializer_instances.contains(instance.id.value())) {
            relevant_instances_by_path.emplace(
                project.design_ir.path(instance.path), &instance);
        }
    }

    std::map<std::uint32_t, fsim_vpi_handle_v1> instances;
    std::map<std::string, fsim_vpi_handle_v1, std::less<>> instance_paths;
    std::map<std::string, fsim_vpi_handle_v1, std::less<>> published_scopes;
    std::set<std::uint32_t> visiting_instances;
    // Task, function, and named block scopes of an instance (IEEE 1800-2023
    // 37.42, 37.43): published after the instance and before its children,
    // in source order. A function's implicit return variable is not a
    // separate VPI object.
    std::set<std::string, std::less<>> function_scope_paths;
    std::set<std::string, std::less<>> clocking_block_paths;
    std::unordered_map<std::uint32_t, const semantic::sv::Statement*>
        hir_statements;
    std::unordered_map<std::uint32_t, const semantic::sv::Process*>
        hir_processes;
    for (const auto& statement : project.systemverilog_hir.statements()) {
        hir_statements.emplace(statement.id.value(), &statement);
    }
    for (const auto& process : project.systemverilog_hir.processes()) {
        hir_processes.emplace(process.id.value(), &process);
    }
    // The DPI properties of an import or export (IEEE 1800-2023 37.40
    // details 6-10) and where its declaration is for the formals (37.41).
    const auto apply_dpi = [&](runtime::SystemVerilogVpiTypeInfo& scope_type,
                               const semantic::sv::DpiDeclaration& dpi,
                               const std::uint32_t dpi_unit,
                               const std::uint32_t dpi_index) {
        const auto import_declaration
            = dpi.direction == semantic::sv::DpiDirection::import;
        scope_type.dpi_access = import_declaration
            ? runtime::SystemVerilogVpiTypeInfo::DpiAccess::Import
            : runtime::SystemVerilogVpiTypeInfo::DpiAccess::Export;
        scope_type.dpi_pure = dpi.qualifier == semantic::sv::DpiQualifier::pure;
        scope_type.dpi_context
            = dpi.qualifier == semantic::sv::DpiQualifier::context;
        scope_type.dpi_legacy_string = dpi.link_name == "DPI";
        scope_type.dpi_c_identifier
            = dpi.c_identifier.value_or(dpi.systemverilog_name);
        if (import_declaration) {
            const auto dpi_result = vpi_dpi_result(dpi);
            scope_type.dpi_func_type = dpi_result.func_type;
            scope_type.dpi_size = dpi_result.size;
            scope_type.dpi_signed = dpi_result.is_signed;
            // The declaration's place joins the scope's provenance; a scope
            // with none (the stub $unit package) keeps none, since a source
            // without its unit is no valid provenance.
            if (scope_type.semantic_unit_id) {
                apply_vpi_source(scope_type, project, dpi.source);
            }
            scope_type.dpi_has_declaration = true;
            scope_type.dpi_unit = dpi_unit;
            scope_type.dpi_index = dpi_index;
        }
    };
    // `separator` joins the scope path and a member's name: "." in an
    // instance, "::" in a package (IEEE 1800-2023 3.13, 37.10, 37.41).
    const auto publish_callable_scopes = [&](const semantic::sv::Unit& unit,
                                             const std::string& instance_path,
                                             const fsim_vpi_handle_v1 parent,
                                             const runtime::SystemVerilogVpiTypeInfo& type,
                                             const std::string_view separator = ".") {
        const bool in_package = separator != ".";
        const auto publish_scope = [&](const runtime::SystemVerilogVpiObjectKind kind,
                                       const std::string& name,
                                       const std::string& path,
                                       const fsim_vpi_handle_v1 owner,
                                       const semantic::sv::DpiDeclaration* const dpi
                                       = nullptr,
                                       const std::uint32_t dpi_unit = 0U) {
            if (published_scopes.contains(path) || instance_paths.contains(path)) {
                return std::optional<fsim_vpi_handle_v1> { };
            }
            runtime::SystemVerilogVpiObjectDescriptor scope;
            scope.kind = kind;
            scope.parent = owner;
            scope.name = name;
            auto scope_type = type;
            scope_type.is_cell = false;
            // A DPI import or export (IEEE 1800-2023 37.40 details 6-10).
            if (dpi != nullptr) {
                apply_dpi(scope_type, *dpi, dpi_unit,
                    static_cast<std::uint32_t>(
                        dpi - unit.dpi_declarations.data()));
            }
            scope.type = scope_type;
            const auto handle = create_checked(
                *result.registry, std::move(scope), path);
            published_scopes.emplace(path, handle);
            return std::optional { handle };
        };
        std::function<void(const std::vector<semantic::StatementId>&,
            const std::string&, fsim_vpi_handle_v1)> blocks;
        blocks = [&](const std::vector<semantic::StatementId>& statements,
                     const std::string& path, const fsim_vpi_handle_v1 owner) {
            for (const auto id : statements) {
                const auto found = hir_statements.find(id.value());
                if (found == hir_statements.end()) {
                    continue;
                }
                const auto& statement = *found->second;
                auto nested_path = path;
                auto nested_owner = owner;
                if ((statement.kind == semantic::sv::StatementKind::block
                        || statement.kind == semantic::sv::StatementKind::fork)
                    && !statement.label.empty()) {
                    nested_path = path + "." + statement.label;
                    if (const auto handle = publish_scope(
                            statement.kind == semantic::sv::StatementKind::block
                                ? runtime::SystemVerilogVpiObjectKind::Begin
                                : runtime::SystemVerilogVpiObjectKind::Fork,
                            statement.label, nested_path, owner)) {
                        nested_owner = *handle;
                    }
                }
                blocks(statement.statements, nested_path, nested_owner);
                blocks(statement.else_statements, nested_path, nested_owner);
            }
        };
        for (const auto declaration_id : unit.declarations) {
            const auto* const declaration
                = project.systemverilog_hir.find_declaration(declaration_id);
            if (declaration == nullptr || declaration->name.empty()
                || (declaration->form != semantic::sv::DeclarationForm::task
                    && declaration->form
                        != semantic::sv::DeclarationForm::function)) {
                continue;
            }
            const auto path = instance_path + std::string { separator }
                + declaration->name;
            const auto function
                = declaration->form == semantic::sv::DeclarationForm::function;
            // A task or function the unit exports to C is a DPI export.
            const semantic::sv::DpiDeclaration* exported = nullptr;
            for (const auto& dpi : unit.dpi_declarations) {
                if (dpi.direction == semantic::sv::DpiDirection::export_declaration
                    && dpi.systemverilog_name == declaration->name) {
                    exported = &dpi;
                    break;
                }
            }
            const auto handle = publish_scope(function
                    ? runtime::SystemVerilogVpiObjectKind::Function
                    : runtime::SystemVerilogVpiObjectKind::Task,
                declaration->name, path, parent, exported);
            if (!handle) {
                continue;
            }
            if (function) {
                function_scope_paths.insert(path);
            }
            blocks(declaration->statements, path, *handle);
        }
        // A DPI import is a task or function object of the scope that
        // declares it (IEEE 1800-2023 37.40, 35.5.4).
        for (const auto& dpi : unit.dpi_declarations) {
            // The imports of a compilation unit belong to its `$unit`
            // package and are published there under their own names.
            if (dpi.direction != semantic::sv::DpiDirection::import
                || !dpi.validated || dpi.systemverilog_name.empty()
                || (in_package && unit.name.starts_with("fsim_unit_"))) {
                continue;
            }
            publish_scope(dpi.callable_kind == semantic::sv::DpiCallableKind::function
                    ? runtime::SystemVerilogVpiObjectKind::Function
                    : runtime::SystemVerilogVpiObjectKind::Task,
                dpi.systemverilog_name,
                instance_path + std::string { separator }
                    + dpi.systemverilog_name,
                parent, &dpi, unit.id.value());
        }
        if (in_package) {
            return;
        }
        for (const auto process_id : unit.processes) {
            const auto found = hir_processes.find(process_id.value());
            if (found != hir_processes.end()) {
                blocks(found->second->statements, instance_path, parent);
            }
        }
        // Clocking blocks are scopes too (IEEE 1800-2023 37.48): published
        // before the clockvar storage beneath them would make them generate
        // scopes. Their clockvars follow with the other clocking objects.
        for (const auto& block : unit.clocking_blocks) {
            const auto path = instance_path + "." + block.name;
            if (published_scopes.contains(path)
                || instance_paths.contains(path)) {
                continue;
            }
            runtime::SystemVerilogVpiObjectDescriptor scope;
            scope.kind = runtime::SystemVerilogVpiObjectKind::ClockingBlock;
            scope.parent = parent;
            scope.name = vpi_identifier(block.name);
            auto scope_type = type;
            scope_type.is_cell = false;
            apply_vpi_source(scope_type, project, block.source);
            scope.type = scope_type;
            published_scopes.emplace(path,
                create_checked(*result.registry, std::move(scope), path));
            clocking_block_paths.insert(path);
        }
    };
    const auto publish_instance = [&](const auto& self,
                                      const auto& instance)
        -> fsim_vpi_handle_v1 {
        if (package_initializer_instances.contains(instance.id.value())) {
            return { };
        }
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
        const std::string instance_path {
            project.design_ir.path(instance.path) };
        const semantic::design::InstanceOccurrence* ancestor { };
        if (!configured_root && instance.parent
            && instance_profiles.contains(instance.parent->value())) {
            ancestor = &project.design_ir.instances().at(
                instance.parent->value());
        }
        const auto has_non_vpi_parent = instance.parent
            && !instance_profiles.contains(instance.parent->value());
        auto ancestor_path = instance_path;
        const auto parent_separator
            = path_parent_end(ancestor_path, instance.name);
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
            const auto direct_parent_end
                = path_parent_end(instance_path, instance.name);
            const auto direct_parent = instance_path.substr(0U,
                direct_parent_end == std::string::npos ? 0U
                                                       : direct_parent_end);
            auto prefix
                = std::string { project.design_ir.path(ancestor->path) };
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
                        // A generate statement of a VHDL architecture is
                        // part of that VHDL region.
                        if (const auto enclosing
                            = result.registry->lookup(descriptor.parent);
                            enclosing && enclosing.value->type) {
                            scope_type.foreign_instance
                                = enclosing.value->type->foreign_instance;
                        }
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
        descriptor.name = instance.name.empty() ? instance_path : instance.name;
        if (!configured_root && !descriptor.parent) {
            descriptor.name = instance_path;
            std::ranges::replace(descriptor.name, '.', '_');
        }
        runtime::SystemVerilogVpiTypeInfo scope_type;
        scope_type.language = profile->second;
        if (const auto unit = std::ranges::find(
                project.systemverilog_hir.units(), specialization.unit,
                &semantic::sv::Unit::id);
            unit != project.systemverilog_hir.units().end()) {
            scope_type.is_cell = unit->compilation.cell;
            runtime::SystemVerilogVpiTypeInfo definition;
            apply_vpi_source(definition, project, unit->source);
            scope_type.definition_path = std::move(definition.source_path);
            scope_type.definition_line = definition.source_line;
            if (specialization.id.value()
                < project.design.specializations().size()) {
                scope_type.configuration = vpi_configuration_name(
                    project.design.specializations()[
                        specialization.id.value()]);
            }
            const auto& nettype = unit->compilation.default_nettype;
            scope_type.default_net_kind = nettype.empty()
                ? runtime::SystemVerilogVpiNetKind::Wire
                : vpi_net_kind(nettype);
            scope_type.unconnected_drive = unit->unconnected_drive
                    == semantic::sv::UnconnectedDrive::pull_zero
                ? runtime::SystemVerilogVpiUnconnectedDrive::Pull0
                : unit->unconnected_drive
                    == semantic::sv::UnconnectedDrive::pull_one
                ? runtime::SystemVerilogVpiUnconnectedDrive::Pull1
                : runtime::SystemVerilogVpiUnconnectedDrive::None;
            scope_type.implicit_generate_scopes
                = unit->implicit_generate_scopes;
            scope_type.implicit_nets = unit->implicit_nets;
        }
        apply_vpi_unit_provenance(scope_type, project, unit_identity);
        if (specialization.language == semantic::Language::vhdl) {
            scope_type.foreign_instance = true;
            apply_vpi_vhdl_provenance(scope_type, project, specialization);
        }
        descriptor.type = scope_type;
        const auto handle = create_checked(
            *result.registry, std::move(descriptor), instance_path);
        visiting_instances.erase(instance.id.value());
        instances.emplace(instance.id.value(), handle);
        instance_paths.emplace(instance_path, handle);
        if (const auto unit = std::ranges::find(
                project.systemverilog_hir.units(), specialization.unit,
                &semantic::sv::Unit::id);
            unit != project.systemverilog_hir.units().end()) {
            publish_callable_scopes(*unit, instance_path, handle, scope_type);
            publish_vpi_genvar_objects(*result.registry, unit->generates,
                instance_path, handle, scope_type);
        }
        return handle;
    };
    for (const auto& instance : project.design_ir.instances()) {
        if (instance_profiles.contains(instance.id.value())) {
            (void)publish_instance(publish_instance, instance);
        }
    }

    // Every compilation unit has a `$unit` package, the first package
    // (IEEE 1800-2023 3.12.1); the others follow in design order.
    std::vector<const semantic::sv::Unit*> package_units;
    for (const auto& unit : project.systemverilog_hir.units()) {
        if (unit.kind == semantic::sv::UnitKind::package) {
            package_units.push_back(&unit);
        }
    }
    std::ranges::stable_partition(package_units, [](const auto* unit) {
        return unit->name.starts_with("fsim_unit_");
    });
    const bool vpi_language_present = !relevant_specializations.empty();
    fsim_vpi_handle_v1 unit_stub { };
    if (vpi_language_present && (package_units.empty()
            || !package_units.front()->name.starts_with("fsim_unit_"))) {
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = runtime::SystemVerilogVpiObjectKind::Package;
        descriptor.name = "fsim_unit_";
        descriptor.type = runtime::SystemVerilogVpiTypeInfo { };
        unit_stub = create_checked(
            *result.registry, std::move(descriptor), "fsim_unit_");
    }
    std::map<std::string, fsim_vpi_handle_v1, std::less<>> packages;
    for (const auto* const package_unit : package_units) {
        const auto& unit = *package_unit;
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
        // The tasks and functions of a package, named `pkg::name` (IEEE
        // 1800-2023 37.10, 37.41).
        if (type.semantic_unit_id) {
            publish_callable_scopes(
                unit, unit.name + "::", package, type, "");
        }
    }

    // DPI imports declared in a compilation unit are tasks and functions of
    // its `$unit` package (IEEE 1800-2023 37.10 vpiUnit, 35.5.4). The
    // package is the one built from the same file as the declaration; a
    // design with a single `$unit` package puts them there.
    {
        std::vector<fsim_vpi_handle_v1> unit_packages;
        std::vector<const semantic::sv::Unit*> unit_package_units;
        for (const auto* const package_unit : package_units) {
            if (package_unit->name.starts_with("fsim_unit_")) {
                unit_packages.push_back(
                    packages.at(package_unit->name));
                unit_package_units.push_back(package_unit);
            }
        }
        if (unit_packages.empty()) {
            if (unit_stub != 0U) {
                unit_packages.push_back(unit_stub);
                unit_package_units.push_back(nullptr);
            }
        }
        const auto source_file = [&](const semantic::SourceSpanId span) {
            const auto& spans = project.semantics.source_spans();
            return span.valid() && span.value() < spans.size()
                ? spans.at(span.value()).file.value()
                : std::numeric_limits<std::uint32_t>::max();
        };
        std::uint32_t index { };
        // Every managed object of a design unit that calls a `$unit` import
        // carries its own copy of the declaration, so the linked design holds
        // one copy per such object; it is one task or function of its `$unit`
        // package (IEEE 1800-2023 35.5.4, 37.10).
        std::set<std::pair<fsim_vpi_handle_v1, std::string>, std::less<>>
            published_imports;
        for (const auto& dpi : project.systemverilog_hir.dpi_declarations()) {
            const auto position = index++;
            if (dpi.owner_kind != semantic::sv::DpiOwnerKind::compilation_unit
                || dpi.direction != semantic::sv::DpiDirection::import
                || !dpi.validated || dpi.systemverilog_name.empty()
                || unit_packages.empty()) {
                continue;
            }
            std::size_t chosen = 0U;
            for (std::size_t candidate = 0U;
                 candidate < unit_package_units.size(); ++candidate) {
                if (unit_package_units[candidate] != nullptr
                    && source_file(unit_package_units[candidate]->source)
                        == source_file(dpi.source)) {
                    chosen = candidate;
                    break;
                }
            }
            if (!published_imports.emplace(
                    unit_packages[chosen], dpi.systemverilog_name).second) {
                continue;
            }
            runtime::SystemVerilogVpiObjectDescriptor scope;
            scope.kind = dpi.callable_kind
                    == semantic::sv::DpiCallableKind::function
                ? runtime::SystemVerilogVpiObjectKind::Function
                : runtime::SystemVerilogVpiObjectKind::Task;
            scope.parent = unit_packages[chosen];
            scope.name = dpi.systemverilog_name;
            // The import has its `$unit` package's language and provenance.
            runtime::SystemVerilogVpiTypeInfo scope_type;
            if (const auto owner = result.registry->lookup(scope.parent);
                owner && owner.value->type) {
                scope_type = *owner.value->type;
            }
            apply_dpi(scope_type, dpi, std::numeric_limits<std::uint32_t>::max(),
                position);
            scope.type = scope_type;
            const auto path = std::string { "fsim_unit_::" }
                + std::to_string(position) + "::" + dpi.systemverilog_name;
            (void)create_checked(*result.registry, std::move(scope), path);
        }
    }

    for (const auto& [instance_id, package_identity]
        : package_initializer_instances) {
        const auto package = packages.find(package_identity);
        if (package == packages.end()) {
            throw std::logic_error {
                "package initializer specialization has no published VPI package"
            };
        }
        const auto& instance = project.design_ir.instances().at(instance_id);
        const auto package_path = vpi_scope_path(instance);
        if (!instances.emplace(instance_id, package->second).second
            || !instance_paths.emplace(package_path, package->second).second) {
            throw std::logic_error {
                "package initializer specialization aliases an existing VPI instance"
            };
        }
    }

    // The generate block occurrences elaboration recorded, by instance:
    // each one's path within the instance, sorted, and the place among the
    // objects that it and its parameters take, reserved here where the
    // publication reaches them. A scope that holds a published object is
    // published with it; the others are deferred (see below).
    struct RecordedGenerateScope {
        std::string_view path;
        std::uint64_t ordinal { };
    };
    std::unordered_map<std::string_view, std::vector<RecordedGenerateScope>>
        recorded_generate_scopes;
    // The place of each specialization's first recorded scope.
    std::vector<std::uint64_t> generate_scope_ordinals;
    {
        const auto& recorded_specializations = project.design.specializations();
        std::uint64_t places { };
        for (const auto& recorded : recorded_specializations) {
            for (const auto& scope : recorded.vpi_generate_scopes) {
                places += 1U + scope.parameters.size();
            }
        }
        auto next = result.registry->reserve_ordinals(places);
        generate_scope_ordinals.reserve(recorded_specializations.size());
        for (const auto& recorded : recorded_specializations) {
            generate_scope_ordinals.push_back(next);
            if (recorded.vpi_generate_scopes.empty()) {
                continue;
            }
            auto& paths = recorded_generate_scopes[recorded.instance];
            for (const auto& scope : recorded.vpi_generate_scopes) {
                paths.push_back({ scope.path, next });
                next += 1U + scope.parameters.size();
            }
        }
        for (auto& entry : recorded_generate_scopes) {
            std::ranges::sort(
                entry.second, { }, &RecordedGenerateScope::path);
        }
    }
    // The recorded generate scope at `relative` beneath an instance.
    const auto find_recorded_generate_scope
        = [&](const std::string_view instance_path,
              const std::string_view relative)
        -> const RecordedGenerateScope* {
        const auto recorded = recorded_generate_scopes.find(instance_path);
        if (recorded == recorded_generate_scopes.end()) {
            return nullptr;
        }
        const auto found = std::ranges::lower_bound(recorded->second,
            relative, { }, &RecordedGenerateScope::path);
        return found != recorded->second.end() && found->path == relative
            ? &*found
            : nullptr;
    };
    // The type of a generate scope beneath `owner`: the owner's, without
    // the cell flag.
    const auto generate_scope_type = [&](const fsim_vpi_handle_v1 owner) {
        runtime::SystemVerilogVpiTypeInfo scope_type;
        if (const auto owner_info = result.registry->lookup(owner);
            owner_info && owner_info.value->type) {
            scope_type = std::move(*owner_info.value->type);
        }
        scope_type.is_cell = false;
        return scope_type;
    };
    // The scope of a generate block occurrence at `relative` beneath an
    // instance, with the scopes between them (IEEE 1800-2023 27.5, 37.85).
    // None when a name on the way is not a scope.
    const auto ensure_generate_scope
        = [&](const std::string& instance_path, const std::string& relative,
              const fsim_vpi_handle_v1 owner,
              const runtime::SystemVerilogVpiTypeInfo& scope_type)
        -> fsim_vpi_handle_v1 {
        auto parent = owner;
        auto prefix = instance_path;
        std::size_t offset = 0U;
        while (offset < relative.size()) {
            const auto separator = relative.find('.', offset);
            const auto end
                = separator == std::string::npos ? relative.size() : separator;
            prefix.push_back('.');
            prefix.append(relative, offset, end - offset);
            if (const auto existing = published_scopes.find(prefix);
                existing != published_scopes.end()) {
                parent = existing->second;
            } else if (const auto nested = instance_paths.find(prefix);
                       nested != instance_paths.end()) {
                parent = nested->second;
            } else if (result.registry->find(prefix)) {
                return { };
            } else {
                runtime::SystemVerilogVpiObjectDescriptor scope;
                scope.kind = runtime::SystemVerilogVpiObjectKind::GenerateScope;
                scope.parent = parent;
                scope.name = relative.substr(offset, end - offset);
                scope.type = scope_type;
                if (const auto* const recorded = find_recorded_generate_scope(
                        instance_path,
                        std::string_view { relative }.substr(0U, end))) {
                    scope.ordinal = recorded->ordinal;
                }
                parent = create_checked(
                    *result.registry, std::move(scope), prefix);
                published_scopes.emplace(prefix, parent);
            }
            if (separator == std::string::npos) {
                break;
            }
            offset = separator + 1U;
        }
        return parent;
    };

    // The recorded generate scope at `path` beneath the instance at
    // `instance_path`.
    const auto recorded_generate_scope
        = [&](const std::string& instance_path, const std::string_view path)
        -> const RecordedGenerateScope* {
        if (path.size() <= instance_path.size() + 1U
            || !path.starts_with(instance_path)
            || path[instance_path.size()] != '.') {
            return nullptr;
        }
        return find_recorded_generate_scope(
            instance_path, path.substr(instance_path.size() + 1U));
    };
    // Publish the recorded generate scope at `path` beneath an instance, if
    // there is one, with the scopes between them.
    const auto ensure_recorded_generate_scope
        = [&](const std::string& instance_path, const std::string_view path,
              const fsim_vpi_handle_v1 owner) {
        if (const auto* const recorded
            = recorded_generate_scope(instance_path, path)) {
            (void)ensure_generate_scope(instance_path,
                std::string { recorded->path }, owner,
                generate_scope_type(owner));
        }
    };
    // The processes of generate scopes that hold no published object, by
    // the scope's recorded path string, with the instance that holds them
    // when the scope is not published; they are created with their scope.
    struct GenerateScopeProcesses {
        fsim_vpi_handle_v1 instance { };
        std::vector<SystemVerilogVpiDeferredProcess> processes;
    };
    std::unordered_map<const char*, GenerateScopeProcesses>
        generate_scope_processes;

    // The parameters of a package or of `$unit` are children of the package
    // object, named `pkg::name` (IEEE 1800-2023 3.12.1, 37.10, 37.28).
    for (const auto& recorded : project.design.specializations()) {
        for (const auto& parameter : recorded.vpi_package_parameters) {
            const auto package = packages.find(parameter.package);
            if (package == packages.end()) {
                continue;
            }
            const auto package_info = result.registry->lookup(package->second);
            if (!package_info) {
                continue;
            }
            publish_recorded_vpi_parameter(*result.registry, project,
                package->second,
                package_info.value->full_name + parameter.name,
                parameter.name, parameter.identity, parameter.declaration,
                parameter.local,
                package_info.value->type ? *package_info.value->type
                                         : runtime::SystemVerilogVpiTypeInfo { },
                std::nullopt, parameter.typespec);
        }
    }

    // Gates, switches and UDP instances are not registry objects: the
    // service builds them from the elaborated primitive records (IEEE
    // 1800-2023 37.35).

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
            {std::string { project.design_ir.path(occurrence.path) },
                owner->second});
    }

    std::map<std::string, std::vector<fsim_vpi_handle_v1>, std::less<>>
        published_classes;
    for (const auto& source : project.systemverilog_hir.classes()) {
        const auto declaration_identity
            = semantic::sv::class_declaration_identity(source);
        if (source.generate_owner
            && std::ranges::none_of(
                project.compiled_systemverilog_class_specializations,
                [&](const auto& specialization) {
                    return specialization.declaration_identity
                        == declaration_identity;
                })) {
            continue;
        }
        if (!source.scope.valid()
            || source.scope.value() >= project.semantics.scopes().size()) {
            throw std::logic_error {
                "VPI class has no owning semantic scope"
            };
        }
        const auto unit_id
            = project.semantics.scopes()[source.scope.value()].unit;
        if (!unit_id.valid()
            || unit_id.value() >= project.semantics.units().size()) {
            throw std::logic_error {
                "VPI class has no owning semantic unit"
            };
        }
        const auto& semantic_unit
            = project.semantics.units()[unit_id.value()];
        const auto unit_identity = semantic_unit.library + "::"
            + semantic_unit.name;
        auto class_profile
            = runtime::SystemVerilogVpiLanguage::SystemVerilog2017;
        if (const auto revision = project.verilog_unit_revisions.find(
                unit_identity);
            revision != project.verilog_unit_revisions.end()) {
            class_profile = vpi_profile_language(revision->second);
        }
        runtime::SystemVerilogVpiTypeDescriptor class_descriptor;
        class_descriptor.kind
            = runtime::SystemVerilogVpiDescriptorKind::Class;
        class_descriptor.category
            = runtime::SystemVerilogVpiValueCategory::None;
        class_descriptor.width = 0U;
        class_descriptor.nominal_name = declaration_identity;
        for (const auto& property : source.properties) {
            class_descriptor.children.push_back(
                vpi_hir_type_descriptor(property.type));
            class_descriptor.member_names.push_back(property.name);
        }

        // The declared typespecs of this class's array and container
        // properties, recorded by elaboration (IEEE 1800-2023 37.17, 37.20).
        std::map<std::string,
            std::shared_ptr<const runtime::SystemVerilogVpiTypespec>>
            property_typespecs;
        for (const auto& elaborated : project.design.specializations()) {
            for (const auto& named : elaborated.vpi_class_property_typespecs) {
                if (named.package == declaration_identity && named.typespec) {
                    property_typespecs.emplace(named.name, named.typespec);
                }
            }
        }
        const auto publish_class = [&](fsim_vpi_handle_v1 parent) {
            runtime::SystemVerilogVpiObjectDescriptor descriptor;
            descriptor.kind = runtime::SystemVerilogVpiObjectKind::Class;
            descriptor.name = parent == 0U
                ? class_descriptor.nominal_name
                : source.name;
            descriptor.parent = parent;
            descriptor.type = vpi_descriptor_type(class_descriptor);
            descriptor.type->language = class_profile;
            apply_vpi_unit_provenance(
                *descriptor.type, project, unit_identity);
            apply_vpi_source(*descriptor.type, project, source.source);
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
                // A virtual interface property's type is an interface
                // typespec (IEEE 1800-2023 37.29).
                if (const auto& declared = source.properties[index].type;
                    declared.virtual_interface && !declared.container_form
                    && declared.unpacked_dimensions.empty()) {
                    property.type->typespec = std::make_shared<
                        const runtime::SystemVerilogVpiTypespec>(
                        runtime::make_systemverilog_vpi_interface_typespec(
                            declared.interface_type, declared.interface_modport,
                            { }));
                }
                // The descriptor of a built-in element array may already have
                // derived a scalar typespec; the declared array typespec
                // replaces it (a virtual interface property is not recorded).
                if (const auto recorded
                    = property_typespecs.find(source.properties[index].name);
                    recorded != property_typespecs.end()) {
                    property.type->typespec = recorded->second;
                }
                // A parameter declared in the class body is a local
                // parameter (IEEE 1800-2023 37.31 detail 7); a static
                // property is not automatic.
                property.type->local_parameter
                    = source.properties[index].parameter;
                if (!source.properties[index].static_storage) {
                    property.type->lifetime
                        = runtime::SystemVerilogVpiLifetime::Automatic;
                }
                apply_vpi_unit_provenance(
                    *property.type, project, unit_identity);
                apply_vpi_source(
                    *property.type, project, source.properties[index].source);
                (void)create_checked(*result.registry, std::move(property),
                    class_descriptor.nominal_name + "::"
                        + source.properties[index].name);
            }
            return class_handle;
        };
        if (const auto enclosing
            = published_classes.find(source.enclosing_identity);
            enclosing != published_classes.end()) {
            auto& handles = published_classes[declaration_identity];
            for (const auto parent : enclosing->second) {
                handles.push_back(publish_class(parent));
            }
            continue;
        }
        if (const auto package = packages.find(source.enclosing_identity);
            package != packages.end()) {
            published_classes[declaration_identity].push_back(
                publish_class(package->second));
            continue;
        }
        const auto owners = unit_instances.find(source.enclosing_identity);
        if (owners == unit_instances.end()) {
            published_classes[declaration_identity].push_back(
                publish_class({ }));
            continue;
        }
        auto relative_name = source.canonical_identity;
        const auto unit_prefix = source.enclosing_identity + "::";
        if (relative_name.starts_with(unit_prefix)) {
            relative_name.erase(0U, unit_prefix.size());
        }
        // A generate block's class is identified `scope::scope::name`.
        const auto class_separator = relative_name.rfind("::");
        const auto generate_scope = class_separator == std::string::npos
            ? std::string { }
            : relative_name.substr(0U, class_separator);
        for (const auto& [instance_path, instance_handle] : owners->second) {
            auto parent = instance_handle;
            auto scope_path = instance_path;
            std::size_t begin = 0U;
            while (begin < generate_scope.size()) {
                const auto separator = generate_scope.find("::", begin);
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
                        scope_type, project, unit_identity);
                    scope.type = scope_type;
                    parent = create_checked(
                        *result.registry, std::move(scope), scope_path);
                    published_scopes.emplace(scope_path, parent);
                }
                if (separator == std::string::npos) {
                    break;
                }
                begin = separator + 2U;
            }
            published_classes[declaration_identity].push_back(
                publish_class(parent));
        }
    }
    // The elaborated specializations by instance path, for the typespecs
    // elaboration kept of parameters (IEEE 1800-2023 37.28).
    std::unordered_map<std::string, std::size_t> elaborated_specializations;
    for (std::size_t index = 0U;
         index < project.design.specializations().size(); ++index) {
        elaborated_specializations.emplace(
            project.design.specializations()[index].instance, index);
    }
    for (const auto& specialization : project.design_ir.specializations()) {
        if (!relevant_specializations.contains(specialization.id.value())) {
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
            const auto* declaration
                = project.systemverilog_hir.find_declaration(
                    *parameter.declaration);
            if (declaration == nullptr) {
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
            if ((declaration->name != parameter.name
                    || declaration->scope != specialization.scope)
                && specialization.language
                    == semantic::Language::system_verilog) {
                continue;
            }
            if (declaration->name != parameter.name
                || declaration->scope != specialization.scope) {
                throw std::logic_error {
                    "VPI parameter declaration does not match its specialization"
                };
            }
            // The typespec of a parameter declared with a typedef.
            std::shared_ptr<const runtime::SystemVerilogVpiTypespec>
                named_typespec;
            if (const auto elaborated = elaborated_specializations.find(
                    std::string { project.design_ir.path(instance.path) });
                elaborated != elaborated_specializations.end()) {
                for (const auto& named :
                    project.design.specializations()[elaborated->second]
                        .vpi_parameter_typespecs) {
                    if (named.name == parameter.name) {
                        named_typespec = named.typespec;
                    }
                }
            }
            auto decoded = decode_vpi_parameter(
                parameter, *declaration, specialization.language);
            if (!decoded
                && specialization.language
                    == semantic::Language::system_verilog) {
                decoded = decode_vpi_aggregate_parameter(parameter.identity,
                    *declaration, named_typespec.get(),
                    specialization.language);
            }
            // A SystemVerilog parameter of a type the registry does not
            // model yet (a type parameter, another aggregate) stays
            // unpublished.
            if (!decoded
                && specialization.language
                    == semantic::Language::system_verilog) {
                continue;
            }
            if (!decoded) {
                throw std::logic_error {
                    "VPI parameter '"
                    + std::string { project.design_ir.path(instance.path) }
                    + '.' + parameter.name
                    + "' has an invalid canonical value or type: identity '"
                    + parameter.identity + "'"
                };
            }
            // An untyped parameter keeps its string literal's constant
            // type (IEEE 1800-2023 6.20.2, 37.32).
            if (declaration->initializer && declaration->type
                && declaration->type->target.spelling == "implicit") {
                const auto* const initializer
                    = project.systemverilog_hir.find_expression(
                        *declaration->initializer);
                decoded->type.string_literal = initializer != nullptr
                    && initializer->kind
                        == semantic::sv::ExpressionKind::string_literal;
            }
            runtime::SystemVerilogVpiObjectDescriptor descriptor;
            descriptor.kind = runtime::SystemVerilogVpiObjectKind::Parameter;
            descriptor.parent = owner->second;
            descriptor.name = parameter.name;
            descriptor.type = std::move(decoded->type);
            descriptor.type->local_parameter = declaration->form
                == semantic::sv::DeclarationForm::local_parameter;
            if (named_typespec) {
                descriptor.type->typespec = named_typespec;
            }
            descriptor.type->language
                = vpi_profile_language(project, specialization);
            apply_vpi_specialization_provenance(
                *descriptor.type, project, specialization);
            apply_vpi_source(
                *descriptor.type, project, declaration->source);
            const auto path = vpi_occurrence_path(instance, parameter.name);
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
    std::unordered_map<std::uint32_t, fsim_vpi_handle_v1> objects;
    std::unordered_map<std::string,
        std::pair<runtime::simir::SignalId, fsim_vpi_handle_v1>, PathHash,
        std::equal_to<>>
        published_paths;
    std::unordered_map<std::pair<fsim_vpi_handle_v1, std::string>,
        std::pair<runtime::simir::SignalId, fsim_vpi_handle_v1>, SiblingHash>
        published_siblings;
    objects.reserve(project.design_ir.objects().size());
    published_paths.reserve(project.design_ir.objects().size());
    published_siblings.reserve(project.design_ir.objects().size());
    std::map<std::pair<runtime::simir::ContainerObjectId, std::size_t>,
        runtime::simir::ContainerElementSignalAlias>
        container_element_aliases;
    std::map<runtime::simir::SignalId,
        std::vector<const semantic::design::Object*>>
        signal_objects_by_runtime;
    for (const auto& alias :
        project.design.container_element_signal_aliases()) {
        if (!container_element_aliases.emplace(
                std::pair { alias.object,
                    static_cast<std::size_t>(alias.ordinal) },
                alias).second) {
            throw std::logic_error {
                "VPI memory has duplicate container-element signal aliases"
            };
        }
    }
    for (const auto& candidate : project.design_ir.objects()) {
        if (candidate.kind == semantic::design::ObjectKind::signal
            && candidate.runtime_index
                <= std::numeric_limits<runtime::simir::SignalId>::max()) {
            signal_objects_by_runtime[
                static_cast<runtime::simir::SignalId>(
                    candidate.runtime_index)]
                .push_back(&candidate);
        }
    }
    std::unordered_set<std::string, PathHash, std::equal_to<>>
        vpi_memory_paths;
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
        // An unpacked-array port is an array object like any other
        // (IEEE 1800-2023 37.14, 37.17); the port's connections name it.
        if (info.slice_alias || object.parent_object
            || !vpi_memory_container(type)) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        vpi_memory_paths.insert(
            vpi_occurrence_path(instance,
                project.design_ir.path(object.path)));
    }
    std::vector<const semantic::design::Object*> vpi_objects;
    // An interface port's formal is a reference object, published with the
    // ports (IEEE 1800-2023 37.15), not the net that holds its binding.
    std::set<std::string, std::less<>> interface_formal_paths;
    for (const auto& instance : project.design_ir.instances()) {
        const auto unit = std::ranges::find(project.systemverilog_hir.units(),
            project.design_ir.specializations()
                .at(instance.specialization.value())
                .unit,
            &semantic::sv::Unit::id);
        if (unit == project.systemverilog_hir.units().end()) {
            continue;
        }
        for (const auto declaration_id : unit->declarations) {
            const auto* const declaration
                = project.systemverilog_hir.find_declaration(declaration_id);
            if (declaration != nullptr
                && declaration->form == semantic::sv::DeclarationForm::port
                && !declaration->interface_type.empty()) {
                interface_formal_paths.insert(
                    std::string { project.design_ir.path(instance.path) }
                    + "." + declaration->name);
            }
        }
    }
    // The formal itself or a name through it (23.3.3.4), which denotes the
    // actual interface's object.
    const auto through_interface_formal = [&](const std::string_view path) {
        auto formal = interface_formal_paths.upper_bound(path);
        if (formal == interface_formal_paths.begin()) {
            return false;
        }
        --formal;
        return path == *formal
            || (path.starts_with(*formal) && path[formal->size()] == '.');
    };
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
        const auto path = vpi_occurrence_path(instance,
            project.design_ir.path(object.path));
        if (function_scope_paths.contains(path)
            || through_interface_formal(project.design_ir.path(object.path))) {
            continue;
        }
        // An alias named outside every instance, such as a compilation-unit
        // variable's bare name, is no declaration of the instance it was
        // attributed to (IEEE 1800-2023 3.12.1); its canonical object is
        // published in its own scope.
        if (object.parent_object) {
            const auto local = project.design_ir.path(object.path);
            const auto design_path = project.design_ir.path(instance.path);
            if (local != design_path
                && !(local.starts_with(design_path)
                    && local.size() > design_path.size()
                    && local[design_path.size()] == '.')) {
                continue;
            }
        }
        // Runtime signal aliases used to connect a selected memory word are
        // implementation details. The Memory and its bound word children are
        // the canonical VPI hierarchy for the declaration.
        const auto within_memory = [&] {
            if (vpi_memory_paths.empty()) {
                return false;
            }
            if (vpi_memory_paths.contains(path)) {
                return true;
            }
            for (auto split = path.find_first_of(".[");
                 split != std::string::npos;
                 split = path.find_first_of(".[", split + 1U)) {
                if (vpi_memory_paths.contains(
                        std::string_view { path }.substr(0U, split))) {
                    return true;
                }
            }
            return false;
        };
        if (within_memory()) {
            continue;
        }
        vpi_objects.push_back(&object);
    }
    {
        // Aliases name their canonical DesignIR object through parent_object.
        // Publish every canonical signal before considering path depth so an
        // alias that shadows a generated instance scope can reuse its handle.
        struct OrderKey {
            bool alias { };
            std::ptrdiff_t depth { };
            std::string_view path;
            const semantic::design::Object* object { };
        };
        std::vector<OrderKey> keys;
        keys.reserve(vpi_objects.size());
        for (const auto* const object : vpi_objects) {
            const auto path = project.design_ir.path(object->path);
            keys.push_back({ object->parent_object.has_value(),
                std::ranges::count(path, '.'), path, object });
        }
        std::ranges::stable_sort(keys, [](const auto& lhs, const auto& rhs) {
            if (lhs.alias != rhs.alias) {
                return !lhs.alias;
            }
            if (lhs.depth != rhs.depth) {
                return lhs.depth < rhs.depth;
            }
            return lhs.path < rhs.path;
        });
        for (std::size_t index = 0U; index < keys.size(); ++index) {
            vpi_objects[index] = keys[index].object;
        }
    }
    // The nets declared `interconnect`, by unit and name (IEEE 1800-2023
    // 6.6.8, 37.24), and the names of the user-defined nettypes a net may
    // be declared with (6.6.7, 37.16 detail 11).
    std::set<std::pair<std::uint32_t, std::string>> interconnect_nets;
    std::set<std::string, std::less<>> nettype_names;
    for (const auto& declaration : project.systemverilog_hir.declarations()) {
        if (declaration.form
            == semantic::sv::DeclarationForm::nettype_declaration) {
            nettype_names.insert(declaration.name);
            continue;
        }
        // Only an `interconnect` declaration carries the marker; an array
        // of them is recorded with its words' storage, not as a net form.
        if (!declaration.type
            || !declaration.type->systemverilog_interconnect
            || !declaration.scope.valid()
            || declaration.scope.value()
                >= project.semantics.scopes().size()) {
            continue;
        }
        const auto unit
            = project.semantics.scopes().at(declaration.scope.value()).unit;
        if (unit.valid()) {
            interconnect_nets.emplace(unit.value(), declaration.name);
        }
    }
    // The load ports that SDF annotation delayed (split_sdf_net_delays): the
    // load instance reads a delayed copy of the net, so the objects that name
    // the net at or below the load instance read and watch the copy, while the
    // source side keeps the net (IEEE 1364-2005 16.2.4). A port of the design
    // itself is the exception: its own object is what the outside drives, so
    // it stays on the net and the instances below it read the copy.
    struct DelayedLoad {
        runtime::simir::SignalId net { };
        runtime::simir::SignalId copy { };
        std::string instance;
        std::string object;
        bool root { };
    };
    std::vector<DelayedLoad> delayed_loads;
    for (const auto& sdf_delay : project.design.sdf_net_delays()) {
        if (!sdf_delay.delayed) {
            continue;
        }
        const auto& sdf_load = sdf_delay.destination;
        const auto sdf_dot = sdf_load.object.rfind('.');
        DelayedLoad sdf_entry { sdf_load.signal, sdf_delay.load_signal,
            sdf_dot == std::string::npos ? std::string { }
                                         : sdf_load.object.substr(0U, sdf_dot),
            sdf_load.object, false };
        sdf_entry.root = sdf_entry.instance.empty()
            || std::ranges::find(project.design.roots(), sdf_entry.instance)
                != project.design.roots().end();
        if (std::ranges::none_of(delayed_loads, [&](const DelayedLoad& known) {
                return known.net == sdf_entry.net && known.copy == sdf_entry.copy
                    && known.instance == sdf_entry.instance;
            })) {
            delayed_loads.push_back(std::move(sdf_entry));
        }
    }
    // The signal an object that names `of_net` at `at_path` reads.
    const auto read_signal = [&](const runtime::simir::SignalId of_net,
                                 const std::string_view at_path) {
        for (const auto& held : delayed_loads) {
            if (held.net != of_net || (held.root && at_path == held.object)) {
                continue;
            }
            if (held.instance.empty()
                || (at_path.size() > held.instance.size()
                    && at_path.starts_with(held.instance)
                    && at_path[held.instance.size()] == '.')) {
                return held.copy;
            }
        }
        return of_net;
    };
    for (const auto* object_pointer : vpi_objects) {
        const auto& object = *object_pointer;
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto path = vpi_occurrence_path(instance,
            project.design_ir.path(object.path));
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
                    "VPI path '" + path
                    + "' aliases distinct runtime signals"
                };
            }
            objects.emplace(object.id.value(), published->second.second);
            continue;
        }
        auto object_parent = owner->second;
        const auto parent_end = path_parent_end(path, object.name);
        // A clocking block's sample and drive storage is the elaborator's;
        // the block's VPI contents are its clockvars (IEEE 1800-2023 37.48).
        if (std::ranges::any_of(clocking_block_paths, [&](const auto& block) {
                return path.size() > block.size() && path.starts_with(block)
                    && path[block.size()] == '.';
            })) {
            continue;
        }
        if (parent_end != std::string::npos) {
            const auto parent_path = path.substr(0U, parent_end);
            const auto published_parent = published_paths.find(parent_path);
            if (published_parent != published_paths.end()) {
                object_parent = published_parent->second.second;
            } else if (const auto instance_parent
                = instance_paths.find(parent_path);
                instance_parent != instance_paths.end()) {
                object_parent = instance_parent->second;
            } else {
                auto prefix
                    = std::string { project.design_ir.path(instance.path) };
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
                            if (const auto* const recorded
                                = recorded_generate_scope(
                                    std::string { project.design_ir.path(
                                        instance.path) },
                                    prefix)) {
                                scope.ordinal = recorded->ordinal;
                            }
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
        // A net declared with a user-defined nettype is a net whatever its
        // resolution (IEEE 1800-2023 6.6.7, 37.16 detail 11).
        const bool nettype_net
            = !signal_info.systemverilog_net_type.empty()
            && nettype_names.contains(signal_info.systemverilog_net_type);
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind
            = category == runtime::SystemVerilogVpiValueCategory::Event
            ? runtime::SystemVerilogVpiObjectKind::NamedEvent
            : port != ports.end()
            ? runtime::SystemVerilogVpiObjectKind::Port
            : declared_net != runtime::SystemVerilogVpiNetKind::None
                || nettype_net
                || project.design.signals().at(signal).resolution
                    != runtime::simir::ResolutionKind::none
            ? runtime::SystemVerilogVpiObjectKind::Net
            : runtime::SystemVerilogVpiObjectKind::Variable;
        descriptor.parent = object_parent;
        descriptor.name = vpi_identifier(object.name);
        const auto direction = port == ports.end()
            ? runtime::SystemVerilogVpiDirection::None
            : vpi_direction(port->second->direction);
        const auto value = interpreter.signal_value_snapshot(signal);
        descriptor.type = packed_type(specialization.language,
            value.width(), object.signed_value, descriptor.kind, signal_info,
            direction);
        descriptor.type->language
            = vpi_profile_language(project, specialization);
        if (descriptor.kind == runtime::SystemVerilogVpiObjectKind::Net) {
            descriptor.type->interconnect = interconnect_nets.contains(
                { specialization.unit.value(), object.name });
            if (nettype_net) {
                descriptor.type->nettype = signal_info.systemverilog_net_type;
            }
        }
        apply_vpi_specialization_provenance(
            *descriptor.type, project, specialization);
        apply_vpi_source(*descriptor.type, project, object.source);
        // The declared structure, union, enumeration or packed array type
        // (IEEE 1800-2023 37.17, 37.18, 37.26).
        if (const auto& declared = signal_info.vpi_declared_type; declared
            && runtime::systemverilog_vpi_declared_bits(*declared)
                == value.width()) {
            descriptor.type->declared = declared;
        }
        descriptor.type->typespec = signal_info.vpi_typespec;
        // A port that shares its actual's signal has its own declared type
        // (IEEE 1800-2023 37.14, 37.17).
        if (port != ports.end()) {
            if (const auto elaborated = elaborated_specializations.find(
                    std::string { project.design_ir.path(instance.path) });
                elaborated != elaborated_specializations.end()) {
                for (const auto& named :
                    project.design.specializations()[elaborated->second]
                        .vpi_port_typespecs) {
                    if (named.name != object.name || !named.typespec) {
                        continue;
                    }
                    descriptor.type->typespec = named.typespec;
                    auto own = vpi_descriptor_of_typespec(*named.typespec);
                    descriptor.type->declared = own
                            && runtime::systemverilog_vpi_declared_bits(*own)
                                == value.width()
                        ? std::make_shared<
                              const runtime::SystemVerilogVpiTypeDescriptor>(
                              std::move(*own))
                        : nullptr;
                }
            }
        }
        const auto handle = create_checked(
            *result.registry, std::move(descriptor), path);
        if (category != runtime::SystemVerilogVpiValueCategory::Event) {
            const auto bound = result.registry->bind_live_value(handle);
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
            const auto read = read_signal(signal, path);
            result.signals[read].push_back(handle);
            result.handles.emplace(handle, read);
            result.scalar_kinds.emplace(
                read, signal_info.systemverilog_scalar);
            result.categories.emplace(read, category);
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
        if (info.slice_alias || object.parent_object
            || !vpi_memory_container(type)) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        if (!relevant_specializations.contains(specialization.id.value())) {
            continue;
        }
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto owner = instances.find(specialization.instance.value());
        if (owner == instances.end()) {
            throw std::logic_error { "VPI memory has no published instance" };
        }
        const auto path = vpi_occurrence_path(instance,
            project.design_ir.path(object.path));
        auto parent = owner->second;
        const auto separator = path_parent_end(path, object.name);
        if (separator != std::string::npos) {
            const auto parent_path = path.substr(0U, separator);
            if (const auto nested_instance = instance_paths.find(parent_path);
                nested_instance != instance_paths.end()) {
                parent = nested_instance->second;
            } else if (const auto nested_scope
                = published_scopes.find(parent_path);
                nested_scope != published_scopes.end()) {
                parent = nested_scope->second;
            } else if (parent_path.starts_with(
                           project.design_ir.path(instance.path))
                && parent_path.size()
                    > project.design_ir.path(instance.path).size()
                && parent_path[project.design_ir.path(instance.path).size()]
                    == '.') {
                auto prefix
                    = std::string { project.design_ir.path(instance.path) };
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
                        if (const auto* const recorded
                            = recorded_generate_scope(
                                std::string { project.design_ir.path(
                                    instance.path) },
                                prefix)) {
                            scope.ordinal = recorded->ordinal;
                        }
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

        // An array of nets is a net array, its words nets; each word has
        // the array's element type (IEEE 1800-2023 23.2.2.3, 37.16, 37.17).
        const auto net_array = info.net;
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = runtime::SystemVerilogVpiObjectKind::Memory;
        descriptor.parent = parent;
        descriptor.name = vpi_identifier(object.name);
        descriptor.type = memory_type(specialization.language, type);
        const bool interconnect_array = net_array
            && interconnect_nets.contains(
                { specialization.unit.value(), object.name });
        if (net_array) {
            descriptor.type->net_kind = runtime::SystemVerilogVpiNetKind::Wire;
            descriptor.type->interconnect = interconnect_array;
        }
        descriptor.type->language
            = vpi_profile_language(project, specialization);
        apply_vpi_specialization_provenance(
            *descriptor.type, project, specialization);
        apply_vpi_source(*descriptor.type, project, object.source);
        descriptor.type->typespec = info.vpi_typespec;
        const auto memory = create_checked(
            *result.registry, std::move(descriptor), path);
        objects.emplace(object.id.value(), memory);

        const auto value
            = interpreter.container_object_value_snapshot(container);
        const auto category = container_category(type);
        auto word_type = container_word_type(specialization.language, type);
        if (const auto unit = std::ranges::find(
                project.systemverilog_hir.units(), specialization.unit,
                &semantic::sv::Unit::id);
            specialization.language == semantic::Language::system_verilog
            && unit != project.systemverilog_hir.units().end()) {
            apply_packed_word_dimensions(word_type,
                project.systemverilog_hir, *unit, object.name);
        }
        word_type.language = vpi_profile_language(project, specialization);
        apply_vpi_specialization_provenance(
            word_type, project, specialization);
        apply_vpi_source(word_type, project, object.source);
        if (net_array) {
            word_type.net_kind = runtime::SystemVerilogVpiNetKind::Wire;
            word_type.interconnect = interconnect_array;
        }
        if (const auto& declared = info.vpi_element_type; declared
            && runtime::systemverilog_vpi_declared_bits(*declared)
                == word_type.width) {
            word_type.declared = declared;
        }
        // The words of an array of virtual interfaces are virtual interface
        // variables, of the interface typespec the array's element has (IEEE
        // 1800-2023 37.29 example 1).
        if (const auto& declared = info.vpi_typespec; declared) {
            const auto* leaf = declared.get();
            while (leaf->kind == runtime::SystemVerilogVpiTypespecKind::Array
                && leaf->element.size() == 1U) {
                leaf = &leaf->element.front();
            }
            if (leaf != declared.get()
                && leaf->kind
                    == runtime::SystemVerilogVpiTypespecKind::Interface) {
                word_type.typespec
                    = std::make_shared<const runtime::SystemVerilogVpiTypespec>(
                        *leaf);
            }
        }
        const auto word_count
            = runtime::simir::container_value_size(value);
        // The words of a fixed memory without element signals are created
        // on first access (IEEE 1800-2023 37.17 makes them reachable only
        // through the memory).
        if (const auto first_alias = container_element_aliases.lower_bound(
                { container, std::size_t { } });
            type.fixed && word_count != 0U
            && word_count <= std::numeric_limits<std::uint32_t>::max()
            && (first_alias == container_element_aliases.end()
                || first_alias->first.first != container)) {
            result.container_scalar_kinds.emplace(container, type.scalar_kind);
            result.container_categories.emplace(container, category);
            result.deferred[memory].memory = SystemVerilogVpiDeferredMemory {
                container, path, type, std::move(word_type), net_array,
                type.scalar_kind, category };
            if (result.registry->defer_children(
                    memory, static_cast<std::uint32_t>(word_count))
                != runtime::SystemVerilogVpiObjectError::None) {
                throw std::logic_error {
                    "failed to defer the words of VPI memory '" + path + "'"
                };
            }
            continue;
        }
        std::vector<fsim_vpi_handle_v1> live_words;
        const auto associative_names
            = systemverilog_vpi_associative_word_names(value);
        for (std::size_t ordinal = 0; ordinal < word_count; ++ordinal) {
            const auto word_name = ordinal < associative_names.size()
                ? associative_names[ordinal]
                : memory_word_name(type, ordinal);
            const auto element_alias = container_element_aliases.find(
                { container, ordinal });
            const elaboration::SignalInfo* element_signal_info { };
            auto word_category = category;
            if (element_alias != container_element_aliases.end()) {
                if (!element_alias->second.readable) {
                    throw std::logic_error {
                        "VPI memory cannot publish an unreadable element signal"
                    };
                }
                element_signal_info = &project.design.signals().at(
                    element_alias->second.signal);
                word_category = vpi_category(
                    specialization.language, *element_signal_info);
            }
            runtime::SystemVerilogVpiObjectDescriptor word;
            word.kind = net_array ? runtime::SystemVerilogVpiObjectKind::Net
                                  : runtime::SystemVerilogVpiObjectKind::Variable;
            word.parent = memory;
            word.name = word_name;
            word.type = word_type;
            const auto word_path = path + word_name;
            const auto word_handle = create_checked(*result.registry,
                std::move(word), word_path);
            const auto bound = result.registry->bind_live_value(word_handle);
            if (bound != runtime::SystemVerilogVpiValueError::None) {
                throw std::logic_error {
                    "failed to bind live VPI memory word '" + path
                    + memory_word_name(type, ordinal) + "'"
                };
            }
            // Physical element storage does not change the public memory
            // word's mutation contract. Keep its container write identity
            // (deposit supported, force unsupported) while signal bindings
            // below provide current values and change notifications.
            result.word_handles.emplace(
                word_handle, std::pair { container, ordinal });
            if (element_alias == container_element_aliases.end()) {
                result.container_words[container].push_back(
                    { word_handle, ordinal });
                live_words.push_back(word_handle);
                continue;
            }

            const auto& alias = element_alias->second;
            const auto signal = alias.signal;
            result.signals[signal].push_back(word_handle);
            result.handles.emplace(word_handle, signal);
            result.scalar_kinds.emplace(
                signal, element_signal_info->systemverilog_scalar);
            result.categories.emplace(signal, word_category);
            if (!alias.writable) {
                result.read_only_container_words.insert(word_handle);
            }
            published_paths.emplace(
                word_path, std::pair { signal, word_handle });
            published_siblings.emplace(
                std::pair { memory, word_name },
                std::pair { signal, word_handle });
            if (const auto signal_objects
                = signal_objects_by_runtime.find(signal);
                signal_objects != signal_objects_by_runtime.end()) {
                for (const auto* signal_object : signal_objects->second) {
                    const auto& signal_specialization
                        = project.design_ir.specializations().at(
                            signal_object->specialization.value());
                    const auto& signal_instance
                        = project.design_ir.instances().at(
                            signal_specialization.instance.value());
                    const auto signal_path = vpi_occurrence_path(signal_instance,
                        project.design_ir.path(signal_object->path));
                    if (signal_path == word_path) {
                        objects.insert_or_assign(
                            signal_object->id.value(), word_handle);
                    }
                }
            }
        }
        result.container_scalar_kinds.emplace(container, type.scalar_kind);
        result.container_categories.emplace(container, category);
        if (!type.fixed) {
            result.live_memories.emplace(container,
                SystemVerilogVpiLiveMemory { memory, path,
                    std::move(word_type), std::move(live_words),
                    std::move(associative_names) });
        }
    }

    // SystemVerilog string variables and string ports are string variables
    // with the live text of their string object (IEEE 1800-2023 6.16,
    // 37.17, 38.15).
    for (const auto& object : project.design_ir.objects()) {
        if (!relevant_specializations.contains(object.specialization.value())
            || object.kind != semantic::design::ObjectKind::string
            || object.runtime_index
                > std::numeric_limits<runtime::simir::StringObjectId>::max()
            || object.name.empty() || object.name.starts_with('$')
            || object.name.starts_with('@')) {
            continue;
        }
        const auto string_id = static_cast<runtime::simir::StringObjectId>(
            object.runtime_index);
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        if (specialization.language != semantic::Language::system_verilog) {
            continue;
        }
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto owner = instances.find(specialization.instance.value());
        if (owner == instances.end()) {
            continue;
        }
        const auto path = vpi_occurrence_path(
            instance, project.design_ir.path(object.path));
        if (result.registry->find(path)) {
            continue;
        }
        auto parent = owner->second;
        const std::string instance_path {
            project.design_ir.path(instance.path) };
        if (const auto separator = path_parent_end(path, object.name);
            separator != std::string::npos) {
            const auto parent_path = path.substr(0U, separator);
            if (const auto nested_instance = instance_paths.find(parent_path);
                nested_instance != instance_paths.end()) {
                parent = nested_instance->second;
            } else if (const auto nested_scope
                = published_scopes.find(parent_path);
                nested_scope != published_scopes.end()) {
                parent = nested_scope->second;
            } else if (parent_path.starts_with(instance_path)
                && parent_path.size() > instance_path.size()
                && parent_path[instance_path.size()] == '.') {
                parent = ensure_generate_scope(instance_path,
                    parent_path.substr(instance_path.size() + 1U),
                    owner->second, generate_scope_type(owner->second));
                if (parent == 0U) {
                    continue;
                }
            }
        }
        const auto port = ports.find(object.id.value());
        runtime::SystemVerilogVpiTypeInfo type;
        type.language = vpi_profile_language(project, specialization);
        type.category = runtime::SystemVerilogVpiValueCategory::String;
        runtime::SystemVerilogVpiTypeDescriptor string_descriptor;
        string_descriptor.kind = runtime::SystemVerilogVpiDescriptorKind::String;
        string_descriptor.category
            = runtime::SystemVerilogVpiValueCategory::String;
        string_descriptor.width = 0U;
        type.descriptor
            = std::make_shared<runtime::SystemVerilogVpiTypeDescriptor>(
                std::move(string_descriptor));
        if (port != ports.end()) {
            type.direction = vpi_direction(port->second->direction);
            if (type.direction == runtime::SystemVerilogVpiDirection::None) {
                type.direction = runtime::SystemVerilogVpiDirection::Inout;
            }
        }
        apply_vpi_specialization_provenance(type, project, specialization);
        apply_vpi_source(type, project, object.source);
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = port != ports.end()
            ? runtime::SystemVerilogVpiObjectKind::Port
            : runtime::SystemVerilogVpiObjectKind::Variable;
        descriptor.parent = parent;
        descriptor.name = vpi_identifier(object.name);
        descriptor.type = std::move(type);
        const auto handle = create_checked(
            *result.registry, std::move(descriptor), path);
        const auto bound = result.registry->bind_live_value(handle);
        if (bound != runtime::SystemVerilogVpiValueError::None) {
            throw std::logic_error {
                "failed to bind live VPI string variable '" + path
                + "' with error "
                + std::to_string(static_cast<unsigned>(bound))
            };
        }
        objects.emplace(object.id.value(), handle);
        result.string_handles.emplace(handle, string_id);
    }

    // The continuous assignments a gate, switch or pull instance is lowered
    // to are not processes: the instance is a vpiGate or vpiSwitch the
    // service builds from the primitive records (IEEE 1800-2023 37.35).
    // Their labels are the instance name, `name[index]` for an array
    // member, with an optional `$` suffix.
    std::map<std::uint32_t, std::set<std::string, std::less<>>>
        primitive_labels;
    for (const auto& unit : project.systemverilog_hir.units()) {
        auto& labels = primitive_labels[unit.id.value()];
        const auto collect
            = [&](const std::vector<semantic::sv::PrimitiveInstance>& list) {
                  for (const auto& primitive : list) {
                      if (primitive.name.empty()) {
                          continue;
                      }
                      labels.insert(primitive.array_member
                                  && !primitive.name.ends_with(']')
                              ? primitive.name + "["
                                  + std::to_string(primitive.array_index) + "]"
                              : primitive.name);
                  }
              };
        const auto regions
            = [&](const auto& self,
                  const std::vector<semantic::sv::GenerateRegion>& list)
            -> void {
            for (const auto& region : list) {
                collect(region.primitives);
                self(self, region.nested);
            }
        };
        collect(unit.primitives);
        regions(regions, unit.generates);
    }
    for (const auto& process : project.design_ir.processes()) {
        if (!relevant_specializations.contains(process.specialization.value())) {
            continue;
        }
        if (const auto labels = primitive_labels.find(
                project.design_ir.specializations()
                    .at(process.specialization.value())
                    .unit.value());
            labels != primitive_labels.end() && !labels->second.empty()) {
            const auto leaf = process.name.find_last_of('.');
            auto local = leaf == std::string::npos
                ? std::string_view { process.name }
                : std::string_view { process.name }.substr(leaf + 1U);
            local = local.substr(0U, local.find('$'));
            if (labels->second.contains(local)) {
                continue;
            }
        }
        const auto process_index = static_cast<std::uint32_t>(
            &process - project.design_ir.processes().data());
        const auto& specialization = project.design_ir.specializations().at(
            process.specialization.value());
        const auto& instance = project.design_ir.instances().at(
            specialization.instance.value());
        const auto path = vpi_occurrence_path(instance, process.name);
        const semantic::sv::ConcurrentAssertion* assertion_source { };
        const auto unit = std::ranges::find(
            project.systemverilog_hir.units(), specialization.unit,
            &semantic::sv::Unit::id);
        if (specialization.language
                == semantic::Language::system_verilog
            && unit != project.systemverilog_hir.units().end()) {
            const auto process_leaf = process.name.find_last_of('.');
            const auto local_name = process_leaf == std::string::npos
                ? std::string_view { process.name }
                : std::string_view { process.name }.substr(process_leaf + 1U);
            assertion_source = semantic::sv::find_concurrent_assertion(
                *unit, local_name);
        }
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = runtime::SystemVerilogVpiObjectKind::Process;
        descriptor.parent = instances.at(specialization.instance.value());
        const auto leaf = path.find_last_of('.');
        descriptor.name = leaf == std::string::npos
            ? path
            : path.substr(leaf + 1U);
        const auto asserted = specialization.language
                == semantic::Language::system_verilog
            && (process.observed || assertion_source != nullptr);
        const auto instance_path = vpi_scope_path(instance);
        // An asserted process is published now, and with it the generate
        // scope holding it or named like it.
        if (asserted) {
            ensure_recorded_generate_scope(
                instance_path, path, descriptor.parent);
            if (leaf != std::string::npos) {
                ensure_recorded_generate_scope(instance_path,
                    std::string_view { path }.substr(0U, leaf),
                    descriptor.parent);
            }
        }
        // Another process of a generate scope that holds no published
        // object is created with the scope.
        const RecordedGenerateScope* holder { };
        if (const auto matching_scope = published_scopes.find(path);
            matching_scope != published_scopes.end()) {
            if (descriptor.name != "<process>") {
                descriptor.parent = matching_scope->second;
            }
            descriptor.name
                = "$process_" + std::to_string(process.runtime_index);
        } else if (const auto* const scope
                   = recorded_generate_scope(instance_path, path)) {
            if (descriptor.name != "<process>") {
                holder = scope;
            }
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
            } else {
                holder = recorded_generate_scope(instance_path, parent_path);
            }
        }
        if (holder != nullptr) {
            auto& held = generate_scope_processes[holder->path.data()];
            held.instance = descriptor.parent;
            held.processes.push_back(
                { process_index, std::move(descriptor.name), path });
            continue;
        }
        if (!asserted) {
            result.deferred[descriptor.parent].processes.push_back(
                { process_index, std::move(descriptor.name), path });
            defer_child(*result.registry, descriptor.parent, path);
            continue;
        }
        const auto process_handle = create_systemverilog_vpi_process(
            *result.registry, project, process, descriptor.parent,
            std::move(descriptor.name), path);
        {
            runtime::SystemVerilogVpiObjectDescriptor assertion;
            assertion.kind
                = runtime::SystemVerilogVpiObjectKind::Assertion;
            assertion.parent = process_handle;
            assertion.name = "$assertion";
            runtime::SystemVerilogVpiTypeInfo assertion_type;
            assertion_type.language
                = vpi_profile_language(project, specialization);
            apply_vpi_specialization_provenance(
                assertion_type, project, specialization);
            apply_vpi_source(assertion_type, project,
                assertion_source == nullptr
                    ? process.source
                    : std::optional { assertion_source->source });
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
        // An event has no driver objects (IEEE 1800-2023 37.16).
        const auto& object = project.design_ir.objects().at(
            driver.object.value());
        const auto& specialization = project.design_ir.specializations().at(
            object.specialization.value());
        if (vpi_category(specialization.language,
                project.design.signals().at(
                    static_cast<runtime::simir::SignalId>(
                        object.runtime_index)))
            == runtime::SystemVerilogVpiValueCategory::Event) {
            continue;
        }
        result.deferred[parent->second].drivers.push_back(
            static_cast<std::uint32_t>(
                &driver - project.design_ir.drivers().data()));
        defer_child(*result.registry, parent->second,
            project.design_ir.path(object.path));
    }

    // The modports of each interface instance, and their members as I/O
    // declarations (IEEE 1800-2023 37.7).
    std::vector<runtime::SystemVerilogVpiPort> modport_members;
    for (const auto& instance : project.design_ir.instances()) {
        const auto published = instances.find(instance.id.value());
        if (published == instances.end()) {
            continue;
        }
        const auto interface = result.registry->lookup(published->second);
        if (!interface
            || interface.value->kind
                != runtime::SystemVerilogVpiObjectKind::Interface) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            instance.specialization.value());
        const auto unit = std::ranges::find(project.systemverilog_hir.units(),
            specialization.unit, &semantic::sv::Unit::id);
        if (unit == project.systemverilog_hir.units().end()) {
            continue;
        }
        const std::string interface_path {
            project.design_ir.path(instance.path) };
        for (const auto& modport : unit->modports) {
            runtime::SystemVerilogVpiObjectDescriptor descriptor;
            descriptor.kind = runtime::SystemVerilogVpiObjectKind::Modport;
            descriptor.parent = published->second;
            descriptor.name = vpi_identifier(modport.name);
            descriptor.type = interface.value->type;
            const auto path = interface_path + "." + modport.name;
            if (result.registry->find(path)) {
                continue;
            }
            const auto handle = create_checked(
                *result.registry, std::move(descriptor), path);
            std::uint32_t index = 0U;
            for (const auto& member : modport.members) {
                if (member.kind != semantic::sv::ModportMemberKind::signal) {
                    continue;
                }
                runtime::SystemVerilogVpiPort declaration;
                declaration.instance = handle;
                declaration.io_declaration = true;
                declaration.name = vpi_identifier(
                    member.alias.empty() ? member.name.spelling : member.alias);
                declaration.index = index++;
                switch (member.direction) {
                case semantic::sv::Direction::input:
                    declaration.direction
                        = runtime::SystemVerilogVpiDirection::Input;
                    break;
                case semantic::sv::Direction::output:
                    declaration.direction
                        = runtime::SystemVerilogVpiDirection::Output;
                    break;
                case semantic::sv::Direction::ref:
                    declaration.ref = true;
                    declaration.direction
                        = runtime::SystemVerilogVpiDirection::Inout;
                    break;
                case semantic::sv::Direction::inout:
                case semantic::sv::Direction::unknown:
                    declaration.direction
                        = runtime::SystemVerilogVpiDirection::Inout;
                    break;
                }
                if (const auto object = result.registry->find(
                        interface_path + "." + member.name.spelling)) {
                    declaration.low_conn = object.value->handle;
                }
                modport_members.push_back(std::move(declaration));
            }
        }
    }

    // The clocking blocks of each module, interface and program instance, and
    // their clockvars (IEEE 1800-2023 37.48). A clocking block is a child of
    // its instance and a clockvar a child of its clocking block; the
    // skews, event and expressions are read from the HIR on demand.
    std::map<std::uint32_t, std::vector<const semantic::sv::Declaration*>>
        nettypes_by_unit;
    for (const auto& declaration : project.systemverilog_hir.declarations()) {
        if (declaration.form
                != semantic::sv::DeclarationForm::nettype_declaration
            || !declaration.scope.valid()
            || declaration.scope.value()
                >= project.semantics.scopes().size()) {
            continue;
        }
        const auto unit
            = project.semantics.scopes().at(declaration.scope.value()).unit;
        if (unit.valid()) {
            nettypes_by_unit[unit.value()].push_back(&declaration);
        }
    }
    for (const auto& instance : project.design_ir.instances()) {
        const auto published = instances.find(instance.id.value());
        if (published == instances.end()) {
            continue;
        }
        const auto owner = result.registry->lookup(published->second);
        if (!owner
            || (owner.value->kind != runtime::SystemVerilogVpiObjectKind::Module
                && owner.value->kind != runtime::SystemVerilogVpiObjectKind::Root
                && owner.value->kind
                    != runtime::SystemVerilogVpiObjectKind::Interface
                && owner.value->kind
                    != runtime::SystemVerilogVpiObjectKind::Program)) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            instance.specialization.value());
        const auto unit = std::ranges::find(project.systemverilog_hir.units(),
            specialization.unit, &semantic::sv::Unit::id);
        if (unit == project.systemverilog_hir.units().end()) {
            continue;
        }
        const std::string instance_path {
            project.design_ir.path(instance.path) };
        for (const auto& block : unit->clocking_blocks) {
            const auto block_path = instance_path + "." + block.name;
            // The block itself was published with the instance's scopes.
            const auto existing = result.registry->find(block_path);
            if (!existing
                || existing.value->kind
                    != runtime::SystemVerilogVpiObjectKind::ClockingBlock) {
                continue;
            }
            const auto handle = existing.value->handle;
            for (const auto& signal : block.signals) {
                runtime::SystemVerilogVpiObjectDescriptor io;
                io.kind = runtime::SystemVerilogVpiObjectKind::ClockingIo;
                io.parent = handle;
                io.name = vpi_identifier(signal.name.spelling);
                io.type = owner.value->type;
                if (io.type) {
                    apply_vpi_source(*io.type, project, signal.source);
                }
                const auto io_path = block_path + "." + signal.name.spelling;
                if (result.registry->find(io_path)) {
                    continue;
                }
                (void)create_checked(
                    *result.registry, std::move(io), io_path);
            }
        }
        // The user-defined nettypes declared in the instance (37.23, 37.85
        // detail 6), one type specification object each.
        const auto declared = nettypes_by_unit.find(specialization.unit.value());
        if (declared == nettypes_by_unit.end()) {
            continue;
        }
        for (const auto* declaration : declared->second) {
            const auto path = instance_path + "." + declaration->name;
            if (result.registry->find(path)) {
                continue;
            }
            runtime::SystemVerilogVpiObjectDescriptor descriptor;
            descriptor.kind
                = runtime::SystemVerilogVpiObjectKind::TypeSpecification;
            descriptor.parent = published->second;
            descriptor.name = vpi_identifier(declaration->name);
            descriptor.type = owner.value->type;
            if (descriptor.type) {
                apply_vpi_source(*descriptor.type, project, declaration->source);
                // The data type the nettype carries (37.23 vpiTypespec).
                descriptor.type->typespec = nullptr;
                if (const auto elaborated = elaborated_specializations.find(
                        instance_path);
                    elaborated != elaborated_specializations.end()) {
                    for (const auto& named :
                        project.design.specializations()[elaborated->second]
                            .vpi_nettype_typespecs) {
                        if (named.name == declaration->name) {
                            descriptor.type->typespec = named.typespec;
                        }
                    }
                }
            }
            (void)create_checked(
                *result.registry, std::move(descriptor), path);
        }
    }

    // Arrays of generate scopes and of instances (IEEE 1800-2023 37.11,
    // 37.85): the elements `name[i]` share an array object `name` beside
    // them in their scope.
    std::map<std::string,
        std::pair<fsim_vpi_handle_v1, runtime::SystemVerilogVpiObjectKind>,
        std::less<>>
        element_arrays;
    const auto collect_array = [&](const std::string& path,
                                   const fsim_vpi_handle_v1 handle) {
        const auto dot = path.rfind('.');
        const auto open = path.find('[', dot == std::string::npos ? 0U : dot);
        if (path.empty() || path.back() != ']' || open == std::string::npos
            || open == 0U || (dot != std::string::npos && open == dot + 1U)) {
            return;
        }
        const auto element = result.registry->kind_of(handle);
        if (!element) {
            return;
        }
        using Kind = runtime::SystemVerilogVpiObjectKind;
        Kind kind { };
        switch (*element) {
        case Kind::GenerateScope:
            kind = Kind::GenerateScopeArray;
            break;
        case Kind::Module:
            kind = Kind::ModuleArray;
            break;
        case Kind::Interface:
            kind = Kind::InterfaceArray;
            break;
        case Kind::Program:
            kind = Kind::ProgramArray;
            break;
        default:
            return;
        }
        element_arrays.try_emplace(path.substr(0U, open), handle, kind);
    };
    for (const auto& [path, handle] : published_scopes) {
        collect_array(path, handle);
    }
    for (const auto& [path, handle] : instance_paths) {
        collect_array(path, handle);
    }
    for (const auto& [path, element] : element_arrays) {
        const auto object = result.registry->lookup(element.first);
        if (!object || result.registry->find(path)) {
            continue;
        }
        runtime::SystemVerilogVpiObjectDescriptor descriptor;
        descriptor.kind = element.second;
        descriptor.parent = object.value->parent;
        const auto dot = path.rfind('.');
        descriptor.name
            = dot == std::string::npos ? path : path.substr(dot + 1U);
        descriptor.type = object.value->type;
        (void)create_checked(*result.registry, std::move(descriptor), path);
    }

    // The genvar of each published generate scope (see publish_vpi_genvar).
    for (const auto& instance : project.design_ir.instances()) {
        const auto published = instances.find(instance.id.value());
        if (published == instances.end()
            || package_initializer_instances.contains(instance.id.value())) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            instance.specialization.value());
        const auto unit = std::ranges::find(project.systemverilog_hir.units(),
            specialization.unit, &semantic::sv::Unit::id);
        if (unit == project.systemverilog_hir.units().end()
            || unit->generates.empty()) {
            continue;
        }
        const std::string instance_path {
            project.design_ir.path(instance.path) };
        const auto prefix = instance_path + ".";
        std::vector<std::pair<std::string, fsim_vpi_handle_v1>> candidates;
        for (auto at = published_scopes.lower_bound(prefix);
             at != published_scopes.end() && at->first.starts_with(prefix);
             ++at) {
            candidates.emplace_back(at->first, at->second);
        }
        for (const auto& [path, scope_handle] : candidates) {
            // A recorded scope's genvar waits with its parameters (below).
            if (instance_paths.contains(instance_path)
                && find_recorded_generate_scope(instance_path,
                    std::string_view { path }.substr(prefix.size()))) {
                continue;
            }
            publish_vpi_genvar(*result.registry, unit->generates,
                std::string_view { path }.substr(prefix.size()), path,
                scope_handle);
        }
    }

    // Every generate block occurrence is a scope, whatever it holds, and
    // the parameters it declares are its children (IEEE 1800-2023 27.6,
    // 37.28, 37.85). One that holds no published object is created on first
    // access beneath its nearest published ancestor, with its genvar and
    // array (see SystemVerilogVpiDeferredGenerateScope).
    {
        std::unordered_map<std::string, std::uint32_t> generate_units;
        const auto& units = project.systemverilog_hir.units();
        for (const auto& instance : project.design_ir.instances()) {
            const auto& specialization = project.design_ir.specializations().at(
                instance.specialization.value());
            const auto unit = std::ranges::find(
                units, specialization.unit, &semantic::sv::Unit::id);
            if (unit != units.end() && !unit->generates.empty()) {
                generate_units.emplace(
                    std::string { project.design_ir.path(instance.path) },
                    static_cast<std::uint32_t>(unit - units.begin()));
            }
        }
        // The published scopes and instances by path, a scope before an
        // instance of the same path.
        std::unordered_map<std::string_view, fsim_vpi_handle_v1> published;
        published.reserve(published_scopes.size() + instance_paths.size());
        for (const auto& [path, handle] : published_scopes) {
            published.try_emplace(path, handle);
        }
        for (const auto& [path, handle] : instance_paths) {
            published.try_emplace(path, handle);
        }
        // The names of the children each scope will create, and of arrays
        // that exist already.
        std::map<fsim_vpi_handle_v1, std::map<std::string_view, bool>>
            deferred_names;
        std::string prefix;
        const auto defer_processes = [&](const fsim_vpi_handle_v1 holder,
                                         std::vector<SystemVerilogVpiDeferredProcess>&
                                             processes) {
            for (auto& process : processes) {
                defer_child(*result.registry, holder, process.path);
                result.deferred[holder].processes.push_back(std::move(process));
            }
        };
        const auto& recorded_specializations = project.design.specializations();
        for (std::size_t index = 0U; index < recorded_specializations.size();
             ++index) {
            const auto& recorded = recorded_specializations[index];
            if (recorded.vpi_generate_scopes.empty()) {
                continue;
            }
            const auto owner = instance_paths.find(recorded.instance);
            if (owner == instance_paths.end()) {
                continue;
            }
            std::optional<std::uint32_t> unit;
            if (const auto found = generate_units.find(recorded.instance);
                found != generate_units.end()) {
                unit = found->second;
            }
            auto place = generate_scope_ordinals.at(index);
            for (std::size_t position = 0U;
                 position < recorded.vpi_generate_scopes.size(); ++position) {
                const auto& scope = recorded.vpi_generate_scopes[position];
                const auto scope_place = place;
                place += 1U + scope.parameters.size();
                const std::string_view scope_path { scope.path };
                auto parent = owner->second;
                prefix = recorded.instance;
                std::optional<std::size_t> missing;
                bool blocked { };
                std::size_t offset = 0U;
                while (offset < scope_path.size()) {
                    const auto separator = scope_path.find('.', offset);
                    const auto segment = scope_path.substr(offset,
                        separator == std::string_view::npos
                            ? std::string_view::npos
                            : separator - offset);
                    prefix.push_back('.');
                    prefix.append(segment);
                    if (const auto existing = published.find(prefix);
                        existing != published.end()) {
                        parent = existing->second;
                    } else {
                        blocked = result.registry->contains(prefix);
                        missing = offset;
                        break;
                    }
                    if (separator == std::string_view::npos) {
                        break;
                    }
                    offset = separator + 1U;
                }
                auto processes = generate_scope_processes.extract(
                    scope_path.data());
                if (processes && (blocked || !missing)) {
                    // The processes of a published scope, or of a name
                    // that is not a scope, are deferred as usual.
                    defer_processes(blocked ? processes.mapped().instance
                                            : parent,
                        processes.mapped().processes);
                }
                if (blocked) {
                    continue;
                }
                if (!missing) {
                    // A published scope's parameters and genvar are created
                    // on first access, in their places.
                    const auto genvar = unit
                        && result.registry->kind_of(parent)
                            == runtime::SystemVerilogVpiObjectKind::GenerateScope
                        && vpi_genvar_region(units.at(*unit).generates,
                            scope_path);
                    // The named blocks of the scope's processes are its
                    // children too (IEEE 1800-2023 27.6, 37.43).
                    const auto count = scope.parameters.size()
                        + (genvar ? 1U : 0U)
                        + (unit && result.registry->kind_of(parent)
                                == runtime::SystemVerilogVpiObjectKind::
                                    GenerateScope
                                ? count_vpi_generate_blocks(
                                      project, *unit, scope_path, scope.region)
                                : 0U);
                    if (count == 0U) {
                        continue;
                    }
                    result.deferred[parent].generate_scopes.push_back({
                        static_cast<std::uint32_t>(index),
                        static_cast<std::uint32_t>(position),
                        static_cast<std::uint32_t>(scope_path.size()),
                        scope_place,
                        unit,
                        { },
                    });
                    if (result.registry->defer_children(
                            parent, static_cast<std::uint32_t>(count))
                        != runtime::SystemVerilogVpiObjectError::None) {
                        throw std::logic_error {
                            "failed to defer the parameters of a VPI generate "
                            "scope"
                        };
                    }
                    continue;
                }
                const auto first_end = scope_path.find('.', *missing);
                const auto first = scope_path.substr(*missing,
                    first_end == std::string_view::npos
                        ? std::string_view::npos
                        : first_end - *missing);
                auto& names = deferred_names[parent];
                names.insert_or_assign(first, true);
                if (const auto open = first.find('[');
                    open != std::string_view::npos && open != 0U
                    && first.back() == ']') {
                    if (const auto [array, inserted]
                        = names.try_emplace(first.substr(0U, open), false);
                        inserted) {
                        array->second = !result.registry->contains(prefix.substr(
                            0U, prefix.size() - first.size() + open));
                    }
                }
                result.deferred[parent].generate_scopes.push_back({
                    static_cast<std::uint32_t>(index),
                    static_cast<std::uint32_t>(position),
                    static_cast<std::uint32_t>(*missing),
                    scope_place,
                    unit,
                    processes ? std::move(processes.mapped().processes)
                              : std::vector<SystemVerilogVpiDeferredProcess> { },
                });
            }
        }
        // Processes whose scope was not reached stay with their instance.
        for (auto& [scope, held] : generate_scope_processes) {
            defer_processes(held.instance, held.processes);
        }
        for (const auto& [parent, names] : deferred_names) {
            if (result.registry->defer_children(parent,
                    static_cast<std::uint32_t>(std::ranges::count(
                        names | std::views::values, true)))
                != runtime::SystemVerilogVpiObjectError::None) {
                throw std::logic_error {
                    "failed to defer the generate scopes of a VPI scope"
                };
            }
        }
    }

    // The ports of each instance, in declaration order (IEEE 1800-2023
    // 37.14): the formal's object is the low connection, and the object a
    // simple-name actual denotes, found from the instantiating scope
    // outward, is the high connection.
    std::unordered_map<std::uint32_t, const semantic::sv::Instance*>
        hir_instances;
    for (const auto& record : project.systemverilog_hir.instances()) {
        hir_instances.emplace(record.id.value(), &record);
    }
    const auto find_object = [&](const std::string& path) {
        const auto found = result.registry->find(path);
        return found ? found.value->handle : fsim_vpi_handle_v1 { };
    };
    for (const auto& instance : project.design_ir.instances()) {
        const auto published = instances.find(instance.id.value());
        if (published == instances.end()
            || package_initializer_instances.contains(instance.id.value())) {
            continue;
        }
        const auto& specialization = project.design_ir.specializations().at(
            instance.specialization.value());
        const auto unit = std::ranges::find(project.systemverilog_hir.units(),
            specialization.unit, &semantic::sv::Unit::id);
        if (unit == project.systemverilog_hir.units().end()) {
            continue;
        }
        const std::string instance_path {
            project.design_ir.path(instance.path) };
        const semantic::sv::Instance* record = nullptr;
        if (instance.source_instance) {
            if (const auto found
                = hir_instances.find(instance.source_instance->value());
                found != hir_instances.end()) {
                record = found->second;
            }
        }
        const auto scope_end = instance_path.rfind('.');
        const auto actual_object = [&](const std::string& name) {
            auto scope = scope_end == std::string::npos
                ? std::string { }
                : instance_path.substr(0U, scope_end);
            while (!scope.empty()) {
                if (const auto handle = find_object(scope + "." + name)) {
                    return handle;
                }
                const auto separator = scope.rfind('.');
                if (separator == std::string::npos) {
                    break;
                }
                scope.resize(separator);
            }
            return fsim_vpi_handle_v1 { };
        };
        std::uint32_t index = 0U;
        for (const auto declaration_id : unit->declarations) {
            const auto* const declaration
                = project.systemverilog_hir.find_declaration(declaration_id);
            if (declaration == nullptr
                || declaration->form != semantic::sv::DeclarationForm::port) {
                continue;
            }
            runtime::SystemVerilogVpiPort port;
            port.instance = published->second;
            port.name = vpi_identifier(declaration->name);
            port.index = index;
            switch (declaration->direction) {
            case semantic::sv::Direction::input:
                port.direction = runtime::SystemVerilogVpiDirection::Input;
                break;
            case semantic::sv::Direction::output:
                port.direction = runtime::SystemVerilogVpiDirection::Output;
                break;
            case semantic::sv::Direction::ref:
                port.ref = true;
                port.direction = runtime::SystemVerilogVpiDirection::Inout;
                break;
            case semantic::sv::Direction::inout:
            case semantic::sv::Direction::unknown:
                port.direction = runtime::SystemVerilogVpiDirection::Inout;
                break;
            }
            port.interface_port = !declaration->interface_type.empty();
            port.modport_port
                = port.interface_port && !declaration->modport.empty();
            if (!port.interface_port) {
                port.low_conn
                    = find_object(instance_path + "." + declaration->name);
            }
            // The actual: associated by name, by `.*`, or by position.
            const semantic::sv::ActualAssociation* actual = nullptr;
            bool wildcard = false;
            if (record != nullptr) {
                std::uint32_t position = 0U;
                for (const auto& association : record->ports) {
                    if (!association.formal) {
                        if (position++ == index) {
                            actual = &association;
                        }
                    } else if (*association.formal == "*") {
                        wildcard = true;
                    } else if (*association.formal == declaration->name) {
                        actual = &association;
                        port.connected_by_name = true;
                    }
                }
            }
            std::optional<std::string> actual_name;
            if (actual != nullptr
                && actual->kind == semantic::sv::ActualKind::expression) {
                const auto* const expression = actual->expression
                    ? project.systemverilog_hir.find_expression(
                          *actual->expression)
                    : nullptr;
                if (expression != nullptr
                    && expression->kind == semantic::sv::ExpressionKind::name) {
                    actual_name = expression->text;
                } else if (!actual->expression && actual->formal) {
                    actual_name = *actual->formal;
                }
            } else if (actual == nullptr && wildcard) {
                actual_name = declaration->name;
                port.connected_by_name = true;
            }
            if (actual_name) {
                port.high_conn = actual_object(*actual_name);
            }
            // An actual that is not a name is an expression, built on
            // demand from the HIR in the instantiating scope (37.14, 37.78).
            if (port.high_conn == 0U && actual != nullptr
                && actual->kind == semantic::sv::ActualKind::expression
                && actual->expression
                && scope_end != std::string::npos
                && !port.interface_port) {
                port.high_expression = actual->expression->value();
                port.has_high_expression = true;
                port.high_scope = find_object(instance_path.substr(0U, scope_end));
            }
            // The interface or modport an interface port refers to
            // (IEEE 1800-2023 37.15).
            if (port.interface_port && port.high_conn != 0U) {
                port.actual = port.high_conn;
                const auto target = result.registry->lookup(port.high_conn);
                if (port.modport_port && target
                    && target.value->kind
                        == runtime::SystemVerilogVpiObjectKind::Interface) {
                    if (const auto modport = result.registry->find(
                            target.value->full_name + "."
                            + declaration->modport)) {
                        port.actual = modport.value->handle;
                    }
                }
            }
            result.ports.push_back(std::move(port));
            ++index;
        }
    }
    std::ranges::move(modport_members, std::back_inserter(result.ports));
    return result;
}

std::vector<std::string> systemverilog_vpi_associative_word_names(
    const runtime::simir::ContainerValue& value)
{
    std::vector<std::string> names;
    if (!value.type.associative) {
        return names;
    }
    if (value.type.string_indices) {
        for (const auto& key : value.string_keys) {
            // The index is written as the string literal that selects the
            // word (vpiDecompile form, IEEE 1800-2023 37.17 detail 28), with
            // the escapes of 5.9.1 for '"', '\' and unprintable characters.
            std::string text;
            for (const char character : key) {
                const auto code = static_cast<unsigned char>(character);
                switch (character) {
                case '"': text += "\\\""; continue;
                case '\\': text += "\\\\"; continue;
                case '\n': text += "\\n"; continue;
                case '\t': text += "\\t"; continue;
                case '\v': text += "\\v"; continue;
                case '\f': text += "\\f"; continue;
                case '\a': text += "\\a"; continue;
                default: break;
                }
                if (code < 0x20U || code >= 0x7fU) {
                    constexpr std::string_view digits { "0123456789abcdef" };
                    text += "\\x";
                    text += digits[code >> 4U];
                    text += digits[code & 15U];
                } else {
                    text += character;
                }
            }
            names.push_back("[\"" + text + "\"]");
        }
        return names;
    }
    for (const auto& key : value.keys) {
        // A key wider than one word is named by its decimal value.
        if (key.width() == 0U || key.width() > 64U || key.is_logic9()) {
            const auto text = runtime::simir::format_output_value(key,
                runtime::simir::OutputFormat::decimal,
                value.type.signed_indices, true);
            names.push_back(text.find_first_of("xXzZ") != std::string::npos
                    ? std::string { "[x]" }
                    : "[" + text + "]");
            continue;
        }
        const auto word = key.low_word();
        if (word.bval != 0U) {
            names.push_back("[x]");
            continue;
        }
        const auto width = std::min<std::uint32_t>(
            value.type.index_width, 64U);
        if (value.type.signed_indices && width != 0U && width < 64U
            && ((word.aval >> (width - 1U)) & 1U) != 0U) {
            names.push_back("[" + std::to_string(
                static_cast<std::int64_t>(word.aval)
                - (std::int64_t { 1 } << width)) + "]");
        } else if (value.type.signed_indices && width == 64U) {
            names.push_back("[" + std::to_string(
                static_cast<std::int64_t>(word.aval)) + "]");
        } else {
            names.push_back("[" + std::to_string(word.aval) + "]");
        }
    }
    return names;
}

fsim_vpi_handle_v1 create_systemverilog_vpi_live_word(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const SystemVerilogVpiLiveMemory& memory, const std::size_t ordinal)
{
    runtime::SystemVerilogVpiObjectDescriptor word;
    word.kind = runtime::SystemVerilogVpiObjectKind::Variable;
    word.parent = memory.memory;
    word.name = ordinal < memory.word_names.size()
        ? memory.word_names[ordinal]
        : "[" + std::to_string(ordinal) + "]";
    word.type = memory.word_type;
    const auto identity = memory.path + word.name;
    return create_checked(registry, std::move(word), identity);
}

SystemVerilogVpiMaterializedChildren materialize_systemverilog_vpi_children(
    runtime::SystemVerilogVpiObjectRegistry& registry,
    const BuiltProject& project,
    const runtime::simir::Interpreter& interpreter,
    const fsim_vpi_handle_v1 parent,
    const SystemVerilogVpiDeferredChildren& deferred)
{
    SystemVerilogVpiMaterializedChildren result;
    if (const auto& memory = deferred.memory) {
        const auto value
            = interpreter.container_object_value_snapshot(memory->container);
        const auto count = runtime::simir::container_value_size(value);
        result.words.reserve(count);
        for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
            runtime::SystemVerilogVpiObjectDescriptor word;
            word.kind = memory->net_array
                ? runtime::SystemVerilogVpiObjectKind::Net
                : runtime::SystemVerilogVpiObjectKind::Variable;
            word.parent = parent;
            word.name = memory_word_name(memory->type, ordinal);
            word.type = memory->word_type;
            auto word_path = memory->path + word.name;
            const auto handle
                = create_checked(registry, std::move(word), word_path);
            if (registry.bind_live_value(handle)
                != runtime::SystemVerilogVpiValueError::None) {
                throw std::logic_error {
                    "failed to bind live VPI memory word '" + word_path + "'"
                };
            }
            result.words.emplace_back(handle, ordinal);
        }
    }
    for (const auto& process : deferred.processes) {
        (void)create_systemverilog_vpi_process(registry, project,
            project.design_ir.processes().at(process.process), parent,
            process.name, process.path);
    }
    for (const auto driver : deferred.drivers) {
        if (auto created = create_systemverilog_vpi_driver(registry, project,
                interpreter, parent, project.design_ir.drivers().at(driver))) {
            result.drivers.push_back(std::move(*created));
        }
    }
    if (!deferred.generate_scopes.empty()) {
        materialize_vpi_generate_scopes(
            registry, project, parent, deferred.generate_scopes);
    }
    return result;
}

runtime::SystemVerilogVpiStoredValue systemverilog_vpi_container_word_value(
    const runtime::simir::ContainerValue& value, const std::size_t ordinal,
    const runtime::SystemVerilogScalarKind scalar_kind,
    const runtime::SystemVerilogVpiValueCategory category)
{
    if (category == runtime::SystemVerilogVpiValueCategory::String) {
        runtime::SystemVerilogVpiStoredValue result;
        result.payload = value.string_elements.at(ordinal);
        return result;
    }
    return systemverilog_vpi_signal_value(
        value.elements.at(ordinal), scalar_kind, category);
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
    // VHDL resolutions may also be SEC, MIN or HR (IEEE 1076-2008 5.2.4.2).
    const auto suffix = resolution.ends_with("fs") ? std::string_view { "fs" }
        : resolution.ends_with("ps")               ? std::string_view { "ps" }
        : resolution.ends_with("ns")               ? std::string_view { "ns" }
        : resolution.ends_with("us")               ? std::string_view { "us" }
        : resolution.ends_with("ms")               ? std::string_view { "ms" }
        : resolution.ends_with("sec")              ? std::string_view { "sec" }
        : resolution.ends_with("min")              ? std::string_view { "min" }
        : resolution.ends_with("hr")               ? std::string_view { "hr" }
        : resolution.ends_with('s')                ? std::string_view { "s" }
                                                   : std::string_view { };
    auto exponent = suffix == "s" || suffix == "sec" || suffix == "min"
            || suffix == "hr"
        ? 0
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
    if (suffix == "min" || suffix == "hr") {
        multiplier *= suffix == "min" ? 60U : 3600U;
    }
    while (multiplier % 10U == 0U && exponent < 0) {
        multiplier /= 10U;
        ++exponent;
    }
    return { static_cast<std::int8_t>(exponent),
        static_cast<std::int8_t>(exponent), multiplier };
}

} // namespace fsim::app::application_detail
