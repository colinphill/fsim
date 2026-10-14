// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_constant_evaluator.hpp"
#include "lowerer_internal.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <new>
#include <unordered_map>

namespace fsim::elaboration {
namespace {

    constexpr std::size_t maximum_effective_subtype_cache_entries = 64U;
    constexpr std::size_t maximum_expression_resolution_cache_entries
        = 1024U;
    constexpr std::size_t maximum_vhdl_subtype_name_cache_entries = 512U;
    constexpr std::size_t maximum_pure_integral_attempt_cache_entries
        = 1024U;
    constexpr std::size_t maximum_vhdl_subtype_name_cache_entry_bytes
        = 4U * 1024U;
    constexpr std::size_t maximum_vhdl_subtype_name_cache_bytes
        = 64U * 1024U;
    constexpr std::size_t maximum_effective_subtype_cache_entry_bytes
        = 64U * 1024U;
    constexpr std::size_t maximum_effective_subtype_cache_bytes
        = 256U * 1024U;
    constexpr std::size_t maximum_effective_subtype_cache_type_depth = 16U;

    bool profile_expression_resolution_cache() noexcept
    {
        static const bool enabled = [] {
            const char* lowering = std::getenv("FSIM_PROFILE_LOWERING");
            return lowering != nullptr
                ? std::string_view { lowering } != "0"
                : std::getenv("FSIM_PROFILE_PHASES") != nullptr;
        }();
        return enabled;
    }

    bool add_effective_subtype_cache_bytes(
        std::size_t& bytes,
        const std::size_t additional,
        const std::size_t limit)
    {
        if (bytes > limit || additional > limit - bytes) {
            return false;
        }
        bytes += additional;
        return true;
    }

    bool cache_bytes_fit(
        const std::size_t used,
        const std::size_t retained,
        const std::size_t limit) noexcept
    {
        return used <= limit && retained <= limit - used;
    }

    bool add_effective_subtype_cache_sequence(
        std::size_t& bytes,
        const std::size_t count,
        const std::size_t element_size,
        const std::size_t limit)
    {
        constexpr auto allocation_overhead = 2U * sizeof(void*);
        if (bytes > limit || count > (limit - bytes) / element_size) {
            return false;
        }
        return add_effective_subtype_cache_bytes(
                   bytes, count * element_size, limit)
            && add_effective_subtype_cache_bytes(
                bytes, allocation_overhead, limit);
    }

    bool add_effective_subtype_cache_string(
        std::size_t& bytes,
        const std::string& value,
        const std::size_t limit)
    {
        constexpr auto allocation_overhead = 2U * sizeof(void*);
        return add_effective_subtype_cache_bytes(
                   bytes, value.size(), limit)
            && add_effective_subtype_cache_bytes(
                bytes, allocation_overhead, limit);
    }

    bool add_effective_subtype_cache_name(
        std::size_t& bytes,
        const semantic::vhdl::Name& name,
        const std::size_t limit)
    {
        return add_effective_subtype_cache_bytes(
                   bytes, sizeof(name), limit)
            && add_effective_subtype_cache_string(
                bytes, name.spelling, limit)
            && add_effective_subtype_cache_string(
                bytes, name.canonical, limit)
            && add_effective_subtype_cache_sequence(
                bytes,
                name.overloads.size(),
                sizeof(semantic::DeclarationId),
                limit);
    }

    bool add_effective_subtype_cache_vhdl_type(
        std::size_t& bytes,
        const semantic::vhdl::SubtypeIndication& subtype,
        const std::size_t limit)
    {
        if (!add_effective_subtype_cache_bytes(
                bytes, sizeof(subtype), limit)
            || !add_effective_subtype_cache_bytes(
                bytes, sizeof(subtype.type_mark), limit)
            || !add_effective_subtype_cache_string(
                bytes, subtype.type_mark.spelling, limit)
            || !add_effective_subtype_cache_name(
                bytes, subtype.resolution_function, limit)
            || !add_effective_subtype_cache_sequence(
                bytes,
                subtype.constraints.size(),
                sizeof(semantic::vhdl::RangeConstraint),
                limit)
            || !add_effective_subtype_cache_sequence(
                bytes,
                subtype.unspecified_component_classes.size(),
                sizeof(semantic::vhdl::UnspecifiedTypeClass),
                limit)
            || !add_effective_subtype_cache_sequence(
                bytes,
                subtype.unspecified_component_type_marks.size(),
                sizeof(std::string),
                limit)
            || !add_effective_subtype_cache_string(
                bytes, subtype.unspecified_inference_identity, limit)) {
            return false;
        }
        for (const auto& type_mark : subtype.unspecified_component_type_marks) {
            if (!add_effective_subtype_cache_string(
                    bytes, type_mark, limit)) {
                return false;
            }
        }
        return true;
    }

    bool add_effective_subtype_cache_sv_type(
        std::size_t& bytes,
        const semantic::sv::TypeReference& type,
        const std::size_t limit,
        const std::size_t depth)
    {
        if (depth > maximum_effective_subtype_cache_type_depth
            || !add_effective_subtype_cache_bytes(
                bytes, sizeof(type), limit)
            || !add_effective_subtype_cache_string(
                bytes, type.target.spelling, limit)
            || !add_effective_subtype_cache_string(
                bytes, type.class_identity, limit)
            || !add_effective_subtype_cache_string(
                bytes, type.interface_type, limit)
            || !add_effective_subtype_cache_string(
                bytes, type.interface_modport, limit)
            || !add_effective_subtype_cache_string(
                bytes, type.systemverilog_net_type, limit)
            || !add_effective_subtype_cache_string(
                bytes, type.systemverilog_resolution_function, limit)
            || !add_effective_subtype_cache_sequence(
                bytes,
                type.interface_parameter_actuals.size(),
                sizeof(semantic::sv::ActualAssociation),
                limit)
            || !add_effective_subtype_cache_sequence(
                bytes,
                type.unpacked_dimensions.size(),
                sizeof(semantic::sv::PackedRange),
                limit)
            || !add_effective_subtype_cache_sequence(
                bytes,
                type.container_element_types.size(),
                sizeof(semantic::sv::TypeReference),
                limit)) {
            return false;
        }
        for (const auto& actual : type.interface_parameter_actuals) {
            if (actual.formal
                && !add_effective_subtype_cache_string(
                    bytes, *actual.formal, limit)) {
                return false;
            }
            if (actual.type
                && !add_effective_subtype_cache_sv_type(
                    bytes, *actual.type, limit, depth + 1U)) {
                return false;
            }
        }
        if (type.associative_index
            && !add_effective_subtype_cache_string(
                bytes, type.associative_index->spelling, limit)) {
            return false;
        }
        for (const auto& element_type : type.container_element_types) {
            if (!add_effective_subtype_cache_sv_type(
                    bytes, element_type, limit, depth + 1U)) {
                return false;
            }
        }
        return true;
    }

    bool add_effective_subtype_cache_bindings(
        std::size_t& bytes,
        const std::span<const semantic::CompiledBindingFrame> frames,
        const std::size_t limit)
    {
        if (!add_effective_subtype_cache_bytes(
                bytes,
                sizeof(std::vector<semantic::CompiledBindingFrame>),
                limit)
            || !add_effective_subtype_cache_sequence(
                bytes,
                frames.size(),
                sizeof(semantic::CompiledBindingFrame),
                limit)) {
            return false;
        }
        for (const auto& frame : frames) {
            if (!add_effective_subtype_cache_sequence(
                    bytes,
                    frame.size(),
                    sizeof(semantic::CompiledActualBinding),
                    limit)) {
                return false;
            }
            for (const auto& binding : frame) {
                if (binding.systemverilog_type
                    && !add_effective_subtype_cache_sv_type(
                        bytes,
                        *binding.systemverilog_type,
                        limit,
                        0U)) {
                    return false;
                }
                if (binding.vhdl_type
                    && !add_effective_subtype_cache_vhdl_type(
                        bytes, *binding.vhdl_type, limit)) {
                    return false;
                }
            }
        }
        return true;
    }

    template <typename Container>
    class PopBackGuard final {
    public:
        explicit PopBackGuard(Container& container)
            : container_ { container }
        {
        }

        PopBackGuard(const PopBackGuard&) = delete;
        PopBackGuard& operator=(const PopBackGuard&) = delete;

        ~PopBackGuard() { container_.pop_back(); }

    private:
        Container& container_;
    };

    template <typename Container>
    PopBackGuard(Container&) -> PopBackGuard<Container>;

    bool same_callable_name(
        const std::string_view left,
        const std::string_view right,
        const bool vhdl)
    {
        if (!vhdl) {
            return left == right;
        }
        return equal_ignoring_ascii_case(left, right);
    }

    PackedLogic4 callable_default_value(
        const std::size_t width,
        const frontend::ValueDomain domain)
    {
        if (domain == frontend::ValueDomain::Logic9) {
            return PackedLogic4::from_logic9_msb_string(
                std::string(width, 'U'));
        }
        return PackedLogic4(
            width,
            domain == frontend::ValueDomain::Bit2
                    || domain == frontend::ValueDomain::Boolean
                    || domain == frontend::ValueDomain::Integer
                ? Logic4::zero
                : Logic4::x);
    }

    bool callable_scalar_domain(const frontend::ValueDomain domain)
    {
        return domain == frontend::ValueDomain::Bit2
            || domain == frontend::ValueDomain::Logic4
            || domain == frontend::ValueDomain::Logic9
            || domain == frontend::ValueDomain::Boolean
            || domain == frontend::ValueDomain::Integer;
    }

    constexpr std::uint64_t systemverilog_task_callable_key(
        const semantic::DeclarationId declaration) noexcept
    {
        return (std::uint64_t { 1U } << 62U)
            | declaration.value();
    }

    constexpr std::uint64_t vhdl_procedure_callable_key(
        const semantic::DeclarationId declaration) noexcept
    {
        return (std::uint64_t { 1U } << 61U)
            | declaration.value();
    }

    struct DpiPackedType {
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        bool string { };
    };

    std::optional<std::uint64_t> dpi_decimal_token(
        const std::string_view text)
    {
        std::uint64_t value { };
        const auto result = std::from_chars(
            text.data(), text.data() + text.size(), value);
        return result.ec == std::errc { }
                && result.ptr == text.data() + text.size()
            ? std::optional<std::uint64_t> { value }
            : std::nullopt;
    }

    std::optional<DpiPackedType> dpi_packed_type(
        const std::span<const semantic::sv::SourceToken> tokens)
    {
        DpiPackedType result;
        std::string_view base;
        bool explicit_signed { };
        bool explicit_unsigned { };
        std::size_t packed_width = 1U;
        for (std::size_t index = 0U; index < tokens.size(); ++index) {
            const auto text = std::string_view { tokens[index].text };
            if (text == "signed") {
                explicit_signed = true;
                continue;
            }
            if (text == "unsigned") {
                explicit_unsigned = true;
                continue;
            }
            if (text == "[") {
                if (index + 4U >= tokens.size()
                    || tokens[index + 2U].text != ":"
                    || tokens[index + 4U].text != "]") {
                    return std::nullopt;
                }
                const auto left = dpi_decimal_token(tokens[index + 1U].text);
                const auto right = dpi_decimal_token(tokens[index + 3U].text);
                if (!left || !right) {
                    return std::nullopt;
                }
                const auto extent = *left >= *right
                    ? *left - *right + 1U
                    : *right - *left + 1U;
                if (extent == 0U
                    || extent > std::numeric_limits<std::size_t>::max()
                    || packed_width
                        > std::numeric_limits<std::size_t>::max()
                            / static_cast<std::size_t>(extent)) {
                    return std::nullopt;
                }
                packed_width *= static_cast<std::size_t>(extent);
                index += 4U;
                continue;
            }
            if (text == "bit" || text == "logic" || text == "reg"
                || text == "byte" || text == "shortint" || text == "int"
                || text == "integer" || text == "longint" || text == "time"
                || text == "chandle" || text == "shortreal"
                || text == "real" || text == "realtime" || text == "string") {
                if (!base.empty()) {
                    return std::nullopt;
                }
                base = text;
                continue;
            }
            return std::nullopt;
        }
        if (base.empty() || (explicit_signed && explicit_unsigned)) {
            return std::nullopt;
        }
        result.string = base == "string";
        if (result.string) {
            return packed_width == 1U && !explicit_signed
                    && !explicit_unsigned
                ? std::optional<DpiPackedType> { result }
                : std::nullopt;
        }
        const auto base_width = base == "byte" ? 8U
            : base == "shortint" ? 16U
            : base == "int" || base == "integer" || base == "shortreal"
                ? 32U
            : base == "longint" || base == "time" || base == "chandle"
                    || base == "real" || base == "realtime"
                ? 64U
                : 1U;
        if (packed_width > std::numeric_limits<std::size_t>::max()
                / base_width) {
            return std::nullopt;
        }
        result.width = packed_width * base_width;
        result.domain = base == "logic" || base == "reg"
                || base == "integer" || base == "time"
            ? frontend::ValueDomain::Logic4
            : base == "byte" || base == "shortint" || base == "int"
                    || base == "longint"
                ? frontend::ValueDomain::Integer
                : frontend::ValueDomain::Bit2;
        const auto implicitly_signed = base == "byte" || base == "shortint"
            || base == "int" || base == "integer" || base == "longint";
        result.signed_value = explicit_signed
            || (!explicit_unsigned && implicitly_signed);
        return result;
    }

    frontend::ValueDomain callable_vhdl_domain(
        const semantic::vhdl::ValueDomain domain)
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
        case semantic::vhdl::ValueDomain::unknown:
            break;
        }
        return frontend::ValueDomain::Unknown;
    }

    frontend::PortDirection class_method_direction(
        const semantic::sv::Direction direction)
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

    bool class_method_copy_out(const frontend::PortDirection direction)
    {
        return direction == frontend::PortDirection::Output
            || direction == frontend::PortDirection::Inout
            || direction == frontend::PortDirection::Ref;
    }

    frontend::PortDirection callable_direction(
        const semantic::CompiledDeclarationView& formal)
    {
        if (formal.systemverilog != nullptr) {
            return class_method_direction(formal.systemverilog->direction);
        }
        if (formal.vhdl->object_class
            == semantic::vhdl::ObjectClass::file) {
            // VHDL file interface objects denote the caller's file object.
            // Preserve mutations such as file_close even though the parser's
            // implicit interface mode is represented as input.
            return frontend::PortDirection::Inout;
        }
        switch (formal.vhdl->direction) {
        case semantic::vhdl::Direction::input:
            return frontend::PortDirection::Input;
        case semantic::vhdl::Direction::output:
            return frontend::PortDirection::Output;
        case semantic::vhdl::Direction::inout:
        case semantic::vhdl::Direction::buffer:
            return frontend::PortDirection::Inout;
        case semantic::vhdl::Direction::unknown:
            break;
        }
        return frontend::PortDirection::Unknown;
    }

    bool callable_copy_in(const frontend::PortDirection direction)
    {
        return direction == frontend::PortDirection::Input
            || direction == frontend::PortDirection::Inout
            || direction == frontend::PortDirection::Ref;
    }

    std::uint64_t hir_static_element_count(const ContainerType& type)
    {
        const auto dimension_count = [](const ContainerDimension& dimension) {
            const auto [left, right] = dimension;
            return static_cast<std::uint64_t>(
                       left >= right
                           ? static_cast<std::int64_t>(left) - right
                           : static_cast<std::int64_t>(right) - left)
                + 1U;
        };
        if (type.dimensions.empty()) {
            return dimension_count(
                { type.index_left, type.index_right });
        }
        std::uint64_t result { 1U };
        for (const auto& dimension : type.dimensions) {
            const auto count = dimension_count(dimension);
            if (count > std::numeric_limits<std::uint64_t>::max()
                    / result) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            result *= count;
        }
        return result;
    }

    std::int32_t hir_static_ordinal_index(
        const ContainerType& type,
        const std::uint64_t ordinal)
    {
        const auto step = type.index_left >= type.index_right
            ? -static_cast<std::int64_t>(ordinal)
            : static_cast<std::int64_t>(ordinal);
        return static_cast<std::int32_t>(
            static_cast<std::int64_t>(type.index_left) + step);
    }

    bool same_hir_container_element_profile(
        const ContainerType& left,
        const ContainerType& right)
    {
        return left.element_kind == right.element_kind
            && left.scalar_kind == right.scalar_kind
            && left.element_width == right.element_width
            && left.two_state == right.two_state
            && left.signed_elements == right.signed_elements
            && left.union_aggregate == right.union_aggregate
            && left.element_nominal_type == right.element_nominal_type
            && left.element_types == right.element_types
            && left.member_names == right.member_names;
    }

    bool compatible_hir_sequence_copy(
        ContainerType source, ContainerType destination)
    {
        if (source.fixed || destination.fixed
            || source.associative || destination.associative
            || source.aggregate_value || destination.aggregate_value) {
            return false;
        }
        source.maximum_elements.reset();
        destination.maximum_elements.reset();
        source.queue = false;
        destination.queue = false;
        return source == destination;
    }

    std::vector<semantic::DeclarationId> vhdl_callable_candidates(
        const semantic::vhdl::Name& name)
    {
        std::vector<semantic::DeclarationId> result;
        if (name.selected) {
            result.push_back(*name.selected);
        } else {
            result = name.overloads;
        }
        std::ranges::sort(
            result,
            [](const semantic::DeclarationId left,
                const semantic::DeclarationId right) {
                return left.value() < right.value();
            });
        const auto duplicate = std::ranges::unique(result);
        result.erase(duplicate.begin(), duplicate.end());
        return result;
    }

} // namespace

std::optional<ContainerType> Lowerer::hir_systemverilog_container_type(
    const semantic::sv::TypeReference& input) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto effective_reference = [&](
                                         const semantic::sv::TypeReference& type) {
        return resolver.effective_systemverilog_type(
                   type, specialized_hir_unit_->scope())
            .value_or(type);
    };
    const auto materialized_reference = [&](
                                            const semantic::sv::TypeReference& type) {
        auto result = effective_reference(type);
        if (result.container_form || !result.target.target.valid()) {
            return result;
        }
        const auto definition = specialized_hir_unit_->find_type(
            result.target.target);
        if (!definition || definition->systemverilog == nullptr
            || !definition->systemverilog->container) {
            return result;
        }
        const auto& container = *definition->systemverilog->container;
        result.container_form = container.form;
        result.queue_maximum = container.queue_maximum;
        result.associative_index = container.associative_index
            ? std::optional { container.associative_index->target }
            : std::nullopt;
        result.unpacked_dimensions = container.static_dimensions;
        if (result.container_element_types.empty()) {
            result.container_element_types.push_back(
                definition->systemverilog->base);
        }
        return result;
    };
    const auto simple_spelling = [](std::string_view spelling) {
        const auto separator = spelling.find_last_of(".:$");
        if (separator != std::string_view::npos) {
            spelling.remove_prefix(separator + 1U);
        }
        return spelling;
    };
    const auto scalar_kind = [&](const semantic::sv::TypeReference& type) {
        using Kind = frontend::SystemVerilogScalarKind;
        const auto spelling = simple_spelling(type.target.spelling);
        if (spelling == "shortreal") {
            return Kind::ShortReal;
        }
        if (spelling == "real") {
            return Kind::Real;
        }
        if (spelling == "realtime") {
            return Kind::Realtime;
        }
        if (spelling == "time") {
            return Kind::Time;
        }
        if (spelling == "chandle") {
            return Kind::Chandle;
        }
        return Kind::None;
    };
    const auto nominal_identity = [&](
                                      const semantic::sv::TypeReference& type,
                                      const std::optional<
                                          semantic::CompiledTypeView>&
                                          definition) {
        if (!type.target.target.valid() || !definition
            || definition->systemverilog == nullptr) {
            return std::string { };
        }
        using Form = semantic::sv::TypeForm;
        const auto form = definition->systemverilog->form;
        if (form != Form::enumeration
            && form != Form::packed_structure
            && form != Form::packed_union
            && form != Form::unpacked_structure
            && form != Form::unpacked_union
            && form != Form::tagged_union) {
            return std::string { };
        }
        return std::string { "@sv-type:" }
            + std::to_string(type.target.target.value());
    };

    std::unordered_set<std::uint32_t> visiting;
    std::function<std::optional<ContainerType>(
        const semantic::sv::TypeReference&, bool)> profile;
    profile = [&](const semantic::sv::TypeReference& raw,
                  const bool box_leaf) -> std::optional<ContainerType> {
        auto candidate = materialized_reference(raw);
        if (candidate.container_form) {
            if (candidate.target.target.valid()
                && !visiting.insert(
                        candidate.target.target.value()).second) {
                return std::nullopt;
            }
            const auto nested = hir_systemverilog_container_type(candidate);
            if (candidate.target.target.valid()) {
                visiting.erase(candidate.target.target.value());
            }
            if (!nested) {
                return std::nullopt;
            }
            return nested;
        }

        const auto definition = candidate.target.target.valid()
            ? specialized_hir_unit_->find_type(candidate.target.target)
            : std::nullopt;
        if (definition && definition->systemverilog != nullptr) {
            const auto& source = *definition->systemverilog;
            if (source.form == semantic::sv::TypeForm::unpacked_structure
                || source.form == semantic::sv::TypeForm::unpacked_union) {
                if (!visiting.insert(candidate.target.target.value()).second) {
                    return std::nullopt;
                }
                ContainerType result;
                result.element_kind = ContainerElementKind::Aggregate;
                result.element_width = 0U;
                result.union_aggregate
                    = source.form == semantic::sv::TypeForm::unpacked_union;
                result.aggregate_value = box_leaf;
                result.element_nominal_type = nominal_identity(
                    candidate, definition);
                for (const auto& member : source.members) {
                    auto member_type = profile(member.type, true);
                    if (!member_type) {
                        visiting.erase(candidate.target.target.value());
                        return std::nullopt;
                    }
                    result.element_types.push_back(std::move(*member_type));
                    result.member_names.push_back(member.name);
                }
                visiting.erase(candidate.target.target.value());
                return result;
            }
        }

        ContainerType result;
        result.element_nominal_type = nominal_identity(
            candidate, definition);
        if (candidate.value_form == semantic::sv::TypeForm::string
            || simple_spelling(candidate.target.spelling) == "string") {
            result.element_kind = ContainerElementKind::String;
            result.element_width = 0U;
        } else {
            const auto width = hir_systemverilog_type_width(candidate);
            if (!width || *width == 0U
                || *width > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            result.element_width = static_cast<std::uint32_t>(*width);
            result.two_state
                = !hir_systemverilog_type_four_state(candidate);
            result.signed_elements = candidate.signed_value;
            result.scalar_kind = scalar_kind(candidate);
            if (result.scalar_kind
                != frontend::SystemVerilogScalarKind::None) {
                result.element_kind = ContainerElementKind::Scalar;
                result.two_state = true;
            }
        }
        if (box_leaf) {
            result.fixed = true;
            result.index_left = 0;
            result.index_right = 0;
            result.dimensions.emplace_back(0, 0);
        }
        return result;
    };

    const auto source = materialized_reference(input);
    if (!source.container_form) {
        auto aggregate = profile(source, true);
        return aggregate
                && aggregate->element_kind
                    == ContainerElementKind::Aggregate
                && aggregate->aggregate_value
            ? aggregate
            : std::nullopt;
    }
    if (source.container_element_types.size() > 1U) {
        return std::nullopt;
    }
    auto element = source.container_element_types.empty()
        ? source
        : source.container_element_types.front();
    if (source.container_element_types.empty()) {
        element.container_form.reset();
        element.queue_maximum.reset();
        element.associative_index.reset();
        element.unpacked_dimensions.clear();
        element.container_element_types.clear();
    }
    const auto nested_element
        = materialized_reference(element).container_form.has_value();
    auto element_type = profile(element, false);
    if (!element_type) {
        return std::nullopt;
    }
    ContainerType result;
    if (nested_element) {
        result.element_kind = ContainerElementKind::Container;
        result.element_width = 0U;
        result.element_nominal_type
            = element_type->element_nominal_type;
        result.element_types.push_back(std::move(*element_type));
    } else {
        result.element_kind = element_type->element_kind;
        result.scalar_kind = element_type->scalar_kind;
        result.element_width = element_type->element_width;
        result.two_state = element_type->two_state;
        result.signed_elements = element_type->signed_elements;
        result.union_aggregate = element_type->union_aggregate;
        result.element_nominal_type = element_type->element_nominal_type;
        result.element_types = std::move(element_type->element_types);
        result.member_names = std::move(element_type->member_names);
    }
    const auto form = *source.container_form;
    result.fixed = form == semantic::sv::TypeForm::static_array;
    result.queue = form == semantic::sv::TypeForm::queue;
    result.associative
        = form == semantic::sv::TypeForm::associative_array;
    if (result.associative && source.associative_index) {
        semantic::sv::TypeReference index_reference;
        index_reference.target = *source.associative_index;
        index_reference = effective_reference(index_reference);
        const auto resolved_spelling = simple_spelling(
            index_reference.target.spelling);
        if (index_reference.value_form == semantic::sv::TypeForm::string
            || resolved_spelling == "string") {
            result.index_width = 0U;
            result.two_state_indices = true;
            result.signed_indices = false;
            result.string_indices = true;
        } else {
            const auto index_width
                = hir_systemverilog_type_width(index_reference);
            if (index_width
                && *index_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            result.index_width = index_width
                ? static_cast<std::uint32_t>(*index_width)
                : 32U;
            result.two_state_indices
                = source.associative_index->target.valid()
                ? !hir_systemverilog_type_four_state(index_reference)
                : resolved_spelling == "bit";
            result.signed_indices
                = source.associative_index->target.valid()
                ? index_reference.signed_value
                : resolved_spelling != "bit"
                    && resolved_spelling != "time";
        }
    }
    if (result.queue && source.queue_maximum) {
        const auto maximum = specialized_hir_unit_
                                 ->evaluate_integral_expression(
                                     *source.queue_maximum);
        if (!maximum || *maximum < 0
            || static_cast<std::uint64_t>(*maximum)
                == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        result.maximum_elements
            = static_cast<std::uint64_t>(*maximum) + 1U;
    }
    if (!result.fixed) {
        return result;
    }
    for (const auto& dimension : source.unpacked_dimensions) {
        const auto left = dimension.left ? dimension.left
            : dimension.left_expression
            ? specialized_hir_unit_->evaluate_integral_expression(
                  *dimension.left_expression)
            : std::nullopt;
        const auto right = dimension.right ? dimension.right
            : dimension.right_expression
            ? specialized_hir_unit_->evaluate_integral_expression(
                  *dimension.right_expression)
            : std::nullopt;
        if (!left || !right
            || *left < std::numeric_limits<std::int32_t>::min()
            || *left > std::numeric_limits<std::int32_t>::max()
            || *right < std::numeric_limits<std::int32_t>::min()
            || *right > std::numeric_limits<std::int32_t>::max()) {
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

std::optional<Lowerer::HirStaticContainerSelection>
Lowerer::hir_static_container_selection_profile(
    const semantic::ExpressionId expression_id,
    HirStaticContainerSelectionFailure* const failure) const
{
    const auto reject = [&](const HirStaticContainerSelectionFailure reason) {
        if (failure != nullptr) {
            *failure = reason;
        }
        return std::optional<HirStaticContainerSelection> { };
    };
    if (failure != nullptr) {
        *failure = HirStaticContainerSelectionFailure::none;
    }
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return reject(HirStaticContainerSelectionFailure::invalid_base);
    }
    const auto& source = *expression->systemverilog;
    const bool slice = source.kind == semantic::sv::ExpressionKind::slice
        && (source.text == ":" || source.text == "+:"
            || source.text == "-:")
        && source.operands.size() == 3U;
    auto base_expression = slice
        ? source.operands.front()
        : expression_id;
    std::vector<semantic::ExpressionId> prefix_indices;
    while (true) {
        const auto indexed = specialized_hir_unit_->find_expression(
            base_expression);
        if (!indexed || indexed->systemverilog == nullptr
            || indexed->systemverilog->kind
                != semantic::sv::ExpressionKind::index
            || indexed->systemverilog->operands.size() != 2U) {
            break;
        }
        prefix_indices.push_back(
            indexed->systemverilog->operands.back());
        base_expression = indexed->systemverilog->operands.front();
    }
    std::ranges::reverse(prefix_indices);
    if (!slice && prefix_indices.empty()) {
        return reject(HirStaticContainerSelectionFailure::invalid_base);
    }
    const auto base = hir_container_object_binding(base_expression);
    if (!base || base->type == nullptr || !base->type->fixed
        || prefix_indices.size() >= base->type->dimensions.size()) {
        return reject(HirStaticContainerSelectionFailure::invalid_base);
    }
    if (base->type->element_kind != ContainerElementKind::Packed
        && base->type->element_kind != ContainerElementKind::Scalar) {
        return reject(
            HirStaticContainerSelectionFailure::unsupported_element_profile);
    }
    auto selected_type = *base->type;
    selected_type.dimensions.erase(
        selected_type.dimensions.begin(),
        selected_type.dimensions.begin()
            + static_cast<std::ptrdiff_t>(prefix_indices.size()));
    selected_type.index_left = selected_type.dimensions.front().first;
    selected_type.index_right = selected_type.dimensions.front().second;
    if (!slice) {
        return HirStaticContainerSelection {
            *base,
            std::move(selected_type),
            std::move(prefix_indices),
            std::nullopt,
        };
    }
    const auto first = hir_constant_integer(source.operands[1]);
    const auto second = hir_constant_integer(source.operands[2]);
    if (!first || !second
        || *first < std::numeric_limits<std::int32_t>::min()
        || *first > std::numeric_limits<std::int32_t>::max()
        || *second < std::numeric_limits<std::int32_t>::min()
        || *second > std::numeric_limits<std::int32_t>::max()) {
        return reject(
            HirStaticContainerSelectionFailure::nonconstant_bound);
    }
    auto left = *first;
    auto right = *second;
    if (source.text != ":") {
        if (*second <= 0) {
            return reject(
                HirStaticContainerSelectionFailure::nonpositive_width);
        }
        const auto distance = *second - 1;
        const auto lower = source.text == "+:"
            ? *first
            : *first - distance;
        const auto upper = source.text == "+:"
            ? *first + distance
            : *first;
        if (selected_type.index_left >= selected_type.index_right) {
            left = upper;
            right = lower;
        } else {
            left = lower;
            right = upper;
        }
    }
    const bool base_descending
        = selected_type.index_left >= selected_type.index_right;
    const bool selected_descending = left >= right;
    const auto low = std::min(
        selected_type.index_left, selected_type.index_right);
    const auto high = std::max(
        selected_type.index_left, selected_type.index_right);
    if ((left != right && base_descending != selected_descending)
        || left < low || left > high || right < low || right > high) {
        return reject(
            HirStaticContainerSelectionFailure::direction_or_range);
    }
    selected_type.index_left = static_cast<std::int32_t>(left);
    selected_type.index_right = static_cast<std::int32_t>(right);
    selected_type.dimensions.front() = {
        selected_type.index_left,
        selected_type.index_right,
    };
    return HirStaticContainerSelection {
        *base,
        std::move(selected_type),
        std::move(prefix_indices),
        static_cast<std::int32_t>(left),
    };
}

std::optional<Lowerer::HirStaticContainerSelection>
Lowerer::hir_static_container_selection(
    const semantic::ExpressionId expression_id)
{
    HirStaticContainerSelectionFailure failure { };
    if (const auto selection = hir_static_container_selection_profile(
            expression_id, &failure)) {
        return selection;
    }
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    // A slice of a queue is not a fixed-array selection (7.10.1).
    if (expression && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::slice
        && !expression->systemverilog->operands.empty()) {
        if (const auto base = hir_container_object_binding(
                expression->systemverilog->operands.front());
            base && base->type != nullptr && base->type->queue) {
            return std::nullopt;
        }
    }
    if (expression && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::slice) {
        auto code = std::string_view { "FSIM-ELAB-SVSLICE-001" };
        auto message = std::string_view {
            "an unpacked-array slice requires a valid fixed-array selection"
        };
        if (failure
            == HirStaticContainerSelectionFailure::
                unsupported_element_profile) {
            code = "FSIM-ELAB-SVSLICE-006";
            message = "static-array slicing requires a packed or scalar "
                      "element profile";
        } else if (failure
                == HirStaticContainerSelectionFailure::nonconstant_bound) {
            code = "FSIM-ELAB-SVSLICE-002";
            message = "static-array slice bounds must be locally constant "
                      "known signed 32-bit values";
        } else if (failure
                == HirStaticContainerSelectionFailure::nonpositive_width) {
            code = "FSIM-ELAB-SVSLICE-002";
            message = "a static-array indexed slice width must be positive";
        } else if (failure
                == HirStaticContainerSelectionFailure::direction_or_range) {
            code = "FSIM-ELAB-SVSLICE-003";
            message = "a static-array slice must preserve direction and "
                      "remain within its declared range";
        }
        report(std::string { code }, std::string { message },
            hir_source_span(expression->systemverilog->source));
    }
    return std::nullopt;
}

std::optional<RegisterId>
Lowerer::lower_hir_static_container_selection_offset(
    const HirStaticContainerSelection& selection,
    std::vector<InstructionIndex>* const skip_branches)
{
    if (selection.base.type == nullptr) {
        return std::nullopt;
    }
    const auto& dimensions = selection.base.type->dimensions;
    std::optional<RegisterId> linear;
    const auto append_ordinal = [&](
                                    const RegisterId ordinal,
                                    const std::size_t dimension) {
        if (!linear) {
            linear = ordinal;
            return;
        }
        const auto& bounds = dimensions[dimension];
        const auto count = static_cast<std::int64_t>(
            std::max(bounds.first, bounds.second))
            - std::min(bounds.first, bounds.second) + 1;
        const auto count_register = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            count_register, integer_value(count) });
        const auto scaled = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            scaled,
            *linear,
            count_register,
        });
        const auto combined = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            combined,
            scaled,
            ordinal,
        });
        linear = combined;
    };
    for (std::size_t dimension { };
        dimension < selection.prefix_indices.size(); ++dimension) {
        const auto expression = selection.prefix_indices[dimension];
        const auto& bounds = dimensions[dimension];
        const auto low = std::min(bounds.first, bounds.second);
        const auto high = std::max(bounds.first, bounds.second);
        auto index = lower_hir_expression(expression, 32U);
        if (!index) {
            return std::nullopt;
        }
        if (register_width(*index) != 32U) {
            index = resize_register(
                *index, 32U, hir_expression_signed(expression));
        }
        if (skip_branches != nullptr) {
            // An unknown or out-of-range index branches past the selection
            // (IEEE 1800-2017 7.4.6).
            const auto domain = register_domain(*index);
            const auto low_register = allocate_register(32U, domain);
            process_.operations.emplace_back(LoadConstant {
                low_register, integer_value(low, 32U) });
            const auto high_register = allocate_register(32U, domain);
            process_.operations.emplace_back(LoadConstant {
                high_register, integer_value(high, 32U) });
            const auto above = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(Binary {
                BinaryOperator::greater_equal_signed, above, *index,
                low_register });
            const auto below = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(Binary {
                BinaryOperator::less_equal_signed, below, *index,
                high_register });
            const auto valid = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(LogicalBinary {
                LogicalBinaryOperator::logical_and, valid, above, below });
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                valid, branch + 1U, 0U, UnknownBranchPolicy::when_false });
            skip_branches->push_back(branch);
        } else {
            process_.operations.emplace_back(IntegerCheck {
                *index, low, high });
        }
        const auto declared_left = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            declared_left, integer_value(bounds.first) });
        const auto ordinal = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::subtract,
            ordinal,
            bounds.first >= bounds.second ? declared_left : *index,
            bounds.first >= bounds.second ? *index : declared_left,
        });
        append_ordinal(ordinal, dimension);
    }
    auto consumed = selection.prefix_indices.size();
    if (selection.selected_left) {
        const auto& bounds = dimensions[consumed];
        const auto ordinal_value = bounds.first >= bounds.second
            ? static_cast<std::int64_t>(bounds.first)
                - *selection.selected_left
            : static_cast<std::int64_t>(*selection.selected_left)
                - bounds.first;
        const auto ordinal = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            ordinal, integer_value(ordinal_value) });
        append_ordinal(ordinal, consumed);
        ++consumed;
    }
    if (!linear) {
        linear = allocate_register(32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            *linear, integer_value(0) });
    }
    for (auto dimension = consumed;
        dimension < dimensions.size(); ++dimension) {
        const auto& bounds = dimensions[dimension];
        const auto count = static_cast<std::int64_t>(
            std::max(bounds.first, bounds.second))
            - std::min(bounds.first, bounds.second) + 1;
        const auto count_register = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            count_register, integer_value(count) });
        const auto scaled = allocate_register(
            32U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            scaled,
            *linear,
            count_register,
        });
        linear = scaled;
    }
    return linear;
}

std::optional<RegisterId> Lowerer::lower_hir_class_handle_array_index(
    const std::string_view identity,
    const std::vector<semantic::ExpressionId>& indices,
    RegisterId& validity)
{
    if (specialized_hir_unit_ == nullptr || indices.size() < 2U) {
        return std::nullopt;
    }
    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto* property = resolver.resolve_systemverilog_class_property(
        std::string { identity }, false).unique();
    if (property == nullptr || property->static_storage
        || property->type.class_identity.empty()
        || property->type.container_form
            != semantic::sv::TypeForm::static_array
        || property->type.unpacked_dimensions.size() != indices.size()) {
        return std::nullopt;
    }

    std::uint64_t total_elements { 1U };
    const auto addressable_elements = static_cast<std::uint64_t>(
        std::numeric_limits<std::size_t>::max()
        / sizeof(std::uint64_t));
    std::vector<std::pair<std::int64_t, std::int64_t>> dimensions;
    dimensions.reserve(property->type.unpacked_dimensions.size());
    for (const auto& range : property->type.unpacked_dimensions) {
        const auto left = range.left ? range.left
            : range.left_expression
            ? specialized_hir_unit_->evaluate_integral_expression(
                  *range.left_expression)
            : std::nullopt;
        const auto right = range.right ? range.right
            : range.right_expression
            ? specialized_hir_unit_->evaluate_integral_expression(
                  *range.right_expression)
            : std::nullopt;
        if (!left || !right) {
            return std::nullopt;
        }
        const auto low = std::min(*left, *right);
        const auto high = std::max(*left, *right);
        const auto distance = static_cast<std::uint64_t>(high)
            - static_cast<std::uint64_t>(low);
        if (distance == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        const auto extent = distance + 1U;
        if (extent > addressable_elements
            || total_elements > addressable_elements / extent) {
            return std::nullopt;
        }
        total_elements *= extent;
        dimensions.emplace_back(*left, *right);
    }
    validity = allocate_register(1U, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(LoadConstant {
        validity, PackedLogic4(1U, Logic4::one) });
    std::optional<RegisterId> linear;
    for (std::size_t dimension { }; dimension < indices.size(); ++dimension) {
        const auto expression = indices[dimension];
        auto index = lower_hir_expression(expression, 64U);
        if (!index) {
            return std::nullopt;
        }
        if (register_width(*index) != 64U) {
            index = resize_register(
                *index, 64U, hir_expression_signed(expression));
        }
        const auto& bounds = dimensions[dimension];
        const auto low = allocate_register(
            64U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            low, integer_value(
                std::min(bounds.first, bounds.second), 64U) });
        const auto high = allocate_register(
            64U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            high, integer_value(
                std::max(bounds.first, bounds.second), 64U) });
        const auto above = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::greater_equal_signed, above, *index, low });
        const auto below = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::less_equal_signed, below, *index, high });
        const auto dimension_valid = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalBinary {
            LogicalBinaryOperator::logical_and,
            dimension_valid,
            above,
            below,
        });
        const auto all_valid = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalBinary {
            LogicalBinaryOperator::logical_and,
            all_valid,
            validity,
            dimension_valid,
        });
        validity = all_valid;
        const auto left = allocate_register(
            64U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            left, integer_value(bounds.first, 64U) });
        const auto ordinal = allocate_register(
            64U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::subtract_unsigned,
            ordinal,
            bounds.first >= bounds.second ? left : *index,
            bounds.first >= bounds.second ? *index : left,
        });
        if (!linear) {
            linear = ordinal;
            continue;
        }
        const auto& current_bounds = dimensions[dimension];
        const auto extent = static_cast<std::uint64_t>(
            std::max(current_bounds.first, current_bounds.second))
            - static_cast<std::uint64_t>(
                std::min(current_bounds.first, current_bounds.second)) + 1U;
        const auto count = allocate_register(
            64U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            count, integer_value(static_cast<std::int64_t>(extent), 64U) });
        const auto scaled = allocate_register(
            64U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::multiply_unsigned, scaled, *linear, count });
        const auto combined = allocate_register(
            64U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_unsigned, combined, scaled, ordinal });
        linear = combined;
    }
    return linear;
}

void Lowerer::copy_hir_static_container_ordinals(
    const ContainerRegisterId destination,
    const ContainerType& destination_range,
    const ContainerRegisterId source,
    const ContainerType& source_range,
    const std::optional<RegisterId> destination_offset,
    const std::optional<RegisterId> source_offset)
{
    const auto count = hir_static_element_count(source_range);
    const auto source_linear = source_range.dimensions.size() > 1U;
    const auto destination_linear
        = destination_range.dimensions.size() > 1U;
    for (std::uint64_t ordinal { }; ordinal < count; ++ordinal) {
        const auto source_ordinal = source_linear || source_offset
            ? static_cast<std::uint32_t>(ordinal)
            : static_cast<std::uint32_t>(
                  hir_static_ordinal_index(source_range, ordinal));
        const auto source_index = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            source_index,
            unsigned_value(source_ordinal, 32U),
        });
        if (source_offset) {
            process_.operations.emplace_back(IntegerBinary {
                IntegerBinaryOperator::add,
                source_index,
                *source_offset,
                source_index,
            });
        }
        const auto value = allocate_register(
            source_range.element_width,
            source_range.two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ContainerRead {
            value, source, source_index,
            true,
            source_linear || source_offset.has_value() });
        const auto destination_ordinal
            = destination_linear || destination_offset
            ? static_cast<std::uint32_t>(ordinal)
            : static_cast<std::uint32_t>(
                  hir_static_ordinal_index(destination_range, ordinal));
        const auto destination_index = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination_index,
            unsigned_value(destination_ordinal, 32U),
        });
        if (destination_offset) {
            process_.operations.emplace_back(IntegerBinary {
                IntegerBinaryOperator::add,
                destination_index,
                *destination_offset,
                destination_index,
            });
        }
        process_.operations.emplace_back(ContainerWrite {
            destination, destination_index, value,
            true,
            destination_linear || destination_offset.has_value() });
    }
}

std::optional<Lowerer::HirStaticContainerValue>
Lowerer::lower_hir_static_container_value(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    const bool selection_syntax = expression
        && expression->systemverilog != nullptr
        && (expression->systemverilog->kind
                == semantic::sv::ExpressionKind::index
            || expression->systemverilog->kind
                == semantic::sv::ExpressionKind::slice);
    const auto selection = selection_syntax
        ? hir_static_container_selection(expression_id)
        : std::nullopt;
    constexpr std::string_view container_property { "@sv-container-property:" };
    if (expression && expression->systemverilog != nullptr
        && expression->systemverilog->text.starts_with(container_property)
        && expression->systemverilog->operands.size() == 1U) {
        if (const auto access = lower_hir_class_value_container(
                expression->systemverilog->operands.front(),
                std::string_view { expression->systemverilog->text }.substr(
                    container_property.size()))) {
            return access->container;
        }
    }
    if (expression && expression->vhdl != nullptr) {
        const auto value
            = lower_hir_vhdl_environment_call_path_container_expression(
                expression_id);
        if (value) {
            return HirStaticContainerValue {
                *value,
                hir_vhdl_environment_call_path_container_type(),
            };
        }
    }
    const bool callable_syntax = expression
        && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::call;
    if (!selection_syntax && !callable_syntax) {
        if (const auto binding = hir_container_object_binding(
                expression_id)) {
            if (binding->type == nullptr) {
                return std::nullopt;
            }
            if (binding->local) {
                return HirStaticContainerValue {
                    *binding->local, *binding->type
                };
            }
            const auto value = allocate_container_register(*binding->type);
            process_.operations.emplace_back(ReadContainerObject {
                value, binding->object });
            // A wildcard-sensitive process (always_comb, a continuous
            // assignment) wakes when any element changes.
            record_container_object_dependency(binding->object);
            return HirStaticContainerValue { value, *binding->type };
        }
    }
    if (expression && expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
        // `q[a:b]` copies the elements from index max(a, 0) through
        // min(b, $) into a new queue; it is empty when a > b (7.10.1).
        if (source.kind == semantic::sv::ExpressionKind::slice
            && source.text == ":" && source.operands.size() == 3U) {
            const auto base = hir_container_object_binding(
                source.operands.front());
            if (base && base->type != nullptr && base->type->queue
                && (base->type->element_kind == ContainerElementKind::Packed
                    || base->type->element_kind
                        == ContainerElementKind::Scalar)) {
                const auto& type = *base->type;
                const auto queue = base->local
                    ? *base->local
                    : allocate_container_register(type);
                if (!base->local) {
                    process_.operations.emplace_back(
                        ReadContainerObject { queue, base->object });
                    record_container_object_dependency(base->object);
                }
                const auto size = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(
                    ContainerSize { size, queue });
                const auto one = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(
                    LoadConstant { one, unsigned_value(1U, 32U) });
                const auto last = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::subtract_unsigned, last, size, one });
                const HirQueueLastIndexRestore restore_last {
                    hir_queue_last_index_, hir_queue_last_index_
                };
                hir_queue_last_index_ = last;
                auto low = lower_hir_expression(source.operands[1], 32U);
                auto high = lower_hir_expression(source.operands[2], 32U);
                if (!low || !high) {
                    return std::nullopt;
                }
                const auto zero = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(
                    LoadConstant { zero, unsigned_value(0U, 32U) });
                // Clamp the bounds to the queue: low to 0, high to $.
                const auto low_negative = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::less_signed, low_negative, *low, zero });
                const auto index = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(ConditionalSelect {
                    index, low_negative, zero, *low });
                const auto high_past = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::greater_signed, high_past, *high, last });
                const auto bound = allocate_register(
                    32U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(ConditionalSelect {
                    bound, high_past, last, *high });
                const auto result = allocate_container_register(type);
                process_.operations.emplace_back(AppendContainer {
                    result, true, { }, { }, { } });
                const auto loop = static_cast<InstructionIndex>(
                    process_.operations.size());
                const auto within = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::less_equal_signed, within, index, bound });
                const auto branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(Branch {
                    within, branch + 1U, 0U,
                    UnknownBranchPolicy::when_false });
                const auto element = allocate_register(type.element_width,
                    type.two_state ? frontend::ValueDomain::Bit2
                                   : frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(ContainerRead {
                    element, queue, index, true, false, false });
                process_.operations.emplace_back(AppendContainer {
                    result, false, { }, element, { } });
                process_.operations.emplace_back(Binary {
                    BinaryOperator::add_unsigned, index, index, one });
                process_.operations.emplace_back(Jump { loop });
                process_.operations[branch] = Branch {
                    within, branch + 1U,
                    static_cast<InstructionIndex>(process_.operations.size()),
                    UnknownBranchPolicy::when_false };
                return HirStaticContainerValue { result, type };
            }
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text == "?:" && source.operands.size() == 3U) {
            const auto when_true_type = hir_static_container_expression_type(
                source.operands[1]);
            const auto when_false_type = hir_static_container_expression_type(
                source.operands[2]);
            if (!when_true_type || !when_false_type
                || *when_true_type != *when_false_type) {
                report(
                    "FSIM-ELAB-SVCOND-002",
                    "container conditional alternatives require an exactly "
                    "compatible kind and profile",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            if (when_true_type->associative) {
                report(
                    "FSIM-ELAB-SVCOND-003",
                    "associative-array conditional values are outside the "
                    "bounded read-only consumer subset",
                    hir_source_span(source.source));
                return std::nullopt;
            }
            auto condition = lower_hir_expression(
                source.operands.front(), 1U);
            const auto when_true = lower_hir_static_container_value(
                source.operands[1]);
            const auto when_false = lower_hir_static_container_value(
                source.operands[2]);
            if (!condition || !when_true || !when_false
                || when_true->type != when_false->type) {
                return std::nullopt;
            }
            if (register_width(*condition) != 1U) {
                condition = resize_register(*condition, 1U, false);
            }
            const auto destination = allocate_container_register(
                when_true->type);
            process_.operations.emplace_back(ConditionalContainerSelect {
                destination,
                *condition,
                when_true->value,
                when_false->value,
            });
            return HirStaticContainerValue {
                destination, when_true->type
            };
        }
        if (source.kind == semantic::sv::ExpressionKind::call) {
            if (const auto result = lower_hir_container_function_call(
                    expression_id)) {
                return result;
            }
        }
    }
    if (!selection || selection->base.type == nullptr) {
        return std::nullopt;
    }
    const auto base = selection->base.local
        ? *selection->base.local
        : allocate_container_register(*selection->base.type);
    if (!selection->base.local) {
        process_.operations.emplace_back(ReadContainerObject {
            base, selection->base.object });
        record_container_object_dependency(selection->base.object);
    }
    const auto value = allocate_container_register(
        selection->selected_type);
    // A SystemVerilog read through an unknown or out-of-range index yields
    // the element type's default value (7.4.6).
    std::vector<InstructionIndex> skips;
    const auto source_offset = lower_hir_static_container_selection_offset(
        *selection,
        specialized_hir_unit_->language()
                == semantic::Language::system_verilog
            ? &skips
            : nullptr);
    if (!source_offset) {
        return std::nullopt;
    }
    copy_hir_static_container_ordinals(
        value,
        selection->selected_type,
        base,
        selection->selected_type,
        { },
        source_offset);
    const auto after = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto branch : skips) {
        if (auto* const skip = runtime::simir::operation_get_if<Branch>(
                &process_.operations[branch])) {
            skip->when_false = after;
        }
    }
    return HirStaticContainerValue {
        value, selection->selected_type
    };
}

std::optional<ContainerRegisterId>
Lowerer::lower_hir_static_container_actual(
    const semantic::ExpressionId expression_id,
    const ContainerType& formal_type,
    const semantic::DeclarationId contextual_declaration)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (contextual_declaration.valid() && expression
        && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::assignment_pattern) {
        if (formal_type.queue && !formal_type.fixed
            && !formal_type.associative
            && formal_type.maximum_elements) {
            // Build the positional pattern as an unbounded queue first.
            // Appending it into the declared bounded result applies the
            // queue's keep-first-elements rule (IEEE 1800-2017 7.10.5).
            auto pattern_type = formal_type;
            pattern_type.maximum_elements.reset();
            const auto source = lower_hir_container_assignment_pattern(
                expression_id,
                HirContainerObjectBinding {
                    contextual_declaration,
                    { },
                    { },
                    std::nullopt,
                    &pattern_type,
                    false,
                });
            if (!source) {
                return std::nullopt;
            }
            const auto value = allocate_container_register(formal_type);
            process_.operations.emplace_back(AppendContainer {
                value, true, *source, std::nullopt, std::nullopt });
            return value;
        }
        return lower_hir_container_assignment_pattern(
            expression_id,
            HirContainerObjectBinding {
                contextual_declaration,
                { },
                { },
                std::nullopt,
                &formal_type,
                false,
            });
    }
    const auto diagnostics_before = diagnostics_.size();
    const auto source = lower_hir_static_container_value(expression_id);
    if (!source) {
        return diagnostics_.size() != diagnostics_before
            ? std::optional {
                  allocate_container_register(formal_type)
              }
            : std::nullopt;
    }
    if (source->type.fixed != formal_type.fixed) {
        report(
            "FSIM-ELAB-SVSLICE-006",
            "static-array callable values require compatible fixed and "
            "nonstatic container kinds",
            hir_source_span(
                specialized_hir_unit_->find_expression(expression_id)
                    ->systemverilog->source));
        return allocate_container_register(formal_type);
    }
    if (source->type == formal_type) {
        const auto value = allocate_container_register(formal_type);
        process_.operations.emplace_back(CopyContainerRegister {
            value, source->value });
        return value;
    }
    if (compatible_hir_sequence_copy(source->type, formal_type)) {
        // Dynamic arrays and queues copy compatible element values
        // (7.6). Apply the destination queue bound while retaining the
        // destination register's declared container type.
        const auto value = allocate_container_register(formal_type);
        process_.operations.emplace_back(AppendContainer {
            value, true, source->value, std::nullopt, std::nullopt });
        return value;
    }
    if (!source->type.fixed && !formal_type.fixed) {
        return std::nullopt;
    }
    if (hir_static_element_count(source->type)
            != hir_static_element_count(formal_type)) {
        report(
            "FSIM-ELAB-SVSLICE-005",
            "static-array callable arguments require equal element counts (actual "
                + std::to_string(hir_static_element_count(source->type))
                + " across "
                + std::to_string(source->type.dimensions.size())
                + " dimensions, formal "
                + std::to_string(hir_static_element_count(formal_type))
                + " across "
                + std::to_string(formal_type.dimensions.size())
                + " dimensions)",
            hir_source_span(
                specialized_hir_unit_->find_expression(expression_id)
                    ->systemverilog->source));
        return allocate_container_register(formal_type);
    }
    if (!same_hir_container_element_profile(
            source->type, formal_type)) {
        report(
            "FSIM-ELAB-SVSLICE-006",
            "static-array callable arguments require identical element profiles",
            hir_source_span(
                specialized_hir_unit_->find_expression(expression_id)
                    ->systemverilog->source));
        return allocate_container_register(formal_type);
    }
    if (source->type.element_kind != ContainerElementKind::Packed
        && source->type.element_kind != ContainerElementKind::Scalar) {
        report(
            "FSIM-ELAB-SVSLICE-006",
            "static-array callable reshaping requires packed or scalar "
            "elements unless the complete array types are identical",
            hir_source_span(
                specialized_hir_unit_->find_expression(expression_id)
                    ->systemverilog->source));
        return allocate_container_register(formal_type);
    }
    const auto value = allocate_container_register(formal_type);
    copy_hir_static_container_ordinals(
        value, formal_type, source->value, source->type);
    return value;
}

bool Lowerer::lower_hir_container_copy_out(
    const semantic::ExpressionId target,
    const ContainerRegisterId source,
    const bool nonblocking)
{
    const auto source_type
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::copy_at(
            process_.container_register_types, source);
    auto write = [&](const HirContainerObjectBinding& destination,
                     const ContainerRegisterId value) {
        if (destination.read_only) {
            report(
                "FSIM-ELAB-SVPORT-009",
                "an input container port is read-only",
                hir_source_span(
                    specialized_hir_unit_->find_expression(target)
                        ->systemverilog->source));
            return true;
        }
        if (destination.local) {
            process_.operations.emplace_back(CopyContainerRegister {
                *destination.local, value });
        } else {
            process_.operations.emplace_back(WriteContainerObject {
                destination.object, value, std::nullopt });
        }
        return true;
    };
    const auto target_expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(target)
        : std::nullopt;
    const bool selection_syntax = target_expression
        && target_expression->systemverilog != nullptr
        && (target_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::index
            || target_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::slice);
    if (!selection_syntax) {
        if (nonblocking) {
            return false;
        }
        const auto destination = hir_container_object_binding(target);
        if (!destination || destination->type == nullptr
            || *destination->type != source_type) {
            return false;
        }
        return write(*destination, source);
    }
    const auto diagnostics_before = diagnostics_.size();
    auto selection = hir_static_container_selection(target);
    if (!selection && diagnostics_.size() != diagnostics_before) {
        return true;
    }
    if (!selection || selection->base.type == nullptr) {
        return false;
    }
    const auto destination_type = *selection->base.type;
    selection->base.type = &destination_type;
    if (selection->base.read_only) {
        report(
            "FSIM-ELAB-SVPORT-009",
            "an input container port is read-only",
            hir_source_span(
                specialized_hir_unit_->find_expression(target)
                    ->systemverilog->source));
        return true;
    }
    if (!source_type.fixed) {
        report(
            "FSIM-ELAB-SVSLICE-004",
            "a static-array selection assignment source must be fixed",
            hir_source_span(
                specialized_hir_unit_->find_expression(target)
                    ->systemverilog->source));
        return true;
    }
    if (hir_static_element_count(source_type)
        != hir_static_element_count(selection->selected_type)) {
        report(
            "FSIM-ELAB-SVSLICE-005",
            "static-array callable arguments require equal element counts",
            hir_source_span(
                specialized_hir_unit_->find_expression(target)
                    ->systemverilog->source));
        return true;
    }
    if (!same_hir_container_element_profile(
            source_type, selection->selected_type)) {
        report(
            "FSIM-ELAB-SVSLICE-006",
            "static-array callable arguments require identical element profiles",
            hir_source_span(
                specialized_hir_unit_->find_expression(target)
                    ->systemverilog->source));
        return true;
    }
    const auto destination_offset
        = lower_hir_static_container_selection_offset(*selection);
    if (!destination_offset) {
        return true;
    }
    const auto selected_value = allocate_container_register(
        selection->selected_type);
    copy_hir_static_container_ordinals(
        selected_value,
        selection->selected_type,
        source,
        source_type);
    if (nonblocking) {
        // Each selected element is captured now and written in the NBA
        // region, so other nonblocking writes to the array are kept.
        if (selection->base.local
            || (selection->selected_type.element_kind
                    != ContainerElementKind::Packed
                && selection->selected_type.element_kind
                    != ContainerElementKind::Scalar)) {
            return false;
        }
        const auto count = hir_static_element_count(selection->selected_type);
        for (std::uint64_t ordinal { }; ordinal < count; ++ordinal) {
            const auto source_index = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                source_index,
                unsigned_value(static_cast<std::uint32_t>(ordinal), 32U),
            });
            const auto value = allocate_register(
                selection->selected_type.element_width,
                selection->selected_type.two_state
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(ContainerRead {
                value, selected_value, source_index, true, true });
            const auto destination_index = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                destination_index,
                unsigned_value(static_cast<std::uint32_t>(ordinal), 32U),
            });
            process_.operations.emplace_back(IntegerBinary {
                IntegerBinaryOperator::add,
                destination_index,
                *destination_offset,
                destination_index,
            });
            WriteContainerObjectElement element_write;
            element_write.object = selection->base.object;
            element_write.index = destination_index;
            element_write.source = value;
            element_write.signed_index = true;
            element_write.linear_index = true;
            element_write.nonblocking = true;
            process_.operations.emplace_back(std::move(element_write));
        }
        return true;
    }
    const auto base = selection->base.local
        ? *selection->base.local
        : allocate_container_register(*selection->base.type);
    if (!selection->base.local) {
        process_.operations.emplace_back(ReadContainerObject {
            base, selection->base.object });
    }
    const auto replacement = allocate_container_register(
        *selection->base.type);
    process_.operations.emplace_back(CopyContainerRegister {
        replacement, base });
    copy_hir_static_container_ordinals(
        replacement,
        selection->selected_type,
        selected_value,
        selection->selected_type,
        destination_offset);
    process_.operations.emplace_back(CopyContainerRegister {
        base, replacement });
    if (!selection->base.local) {
        process_.operations.emplace_back(WriteContainerObject {
            selection->base.object, base, std::nullopt });
    }
    return true;
}

std::optional<Lowerer::HirClassPropertyProfile>
Lowerer::hir_class_property_profile(
    const semantic::ExpressionId expression_id,
    const bool string_property) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& access = *expression->systemverilog;
    const auto instance = access.kind
        == semantic::sv::ExpressionKind::class_property;
    const auto static_access = access.kind
        == semantic::sv::ExpressionKind::class_static_property;
    // A property read in a property initializer carries its identity only
    // in the access text.
    constexpr std::string_view property_prefix { "@sv-property:" };
    const auto member_identity = !access.class_member_identity.empty()
        ? std::string_view { access.class_member_identity }
        : std::string_view { access.text }.starts_with(property_prefix)
        ? std::string_view { access.text }.substr(property_prefix.size())
        : std::string_view { };
    if ((!instance && !static_access)
        || member_identity.empty()
        || (instance && access.operands.size() != 1U)
        || (static_access && !access.operands.empty())) {
        return std::nullopt;
    }

    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto* selected = resolver.resolve_systemverilog_class_property(
        std::string { member_identity }, static_access).unique();
    if (string_property) {
        if (selected == nullptr
            || selected->static_storage != static_access
            || selected->type.value_form != semantic::sv::TypeForm::string
            || selected->type.container_form) {
            return std::nullopt;
        }
        HirClassPropertyProfile result;
        result.identity = selected->canonical_identity;
        if (instance) {
            result.receiver = access.operands.front();
        }
        result.static_storage = selected->static_storage;
        result.writable = !selected->constant && !selected->parameter;
        return result;
    }
    const auto property_width = selected != nullptr
        ? hir_systemverilog_type_width(selected->type)
        : std::nullopt;
    if (selected == nullptr
        || selected->static_storage != static_access
        || selected->type.value_form
            == semantic::sv::TypeForm::string
        || !property_width || *property_width == 0U
        || *property_width
            > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    HirClassPropertyProfile result;
    result.identity = selected->canonical_identity;
    if (instance) {
        result.receiver = access.operands.front();
    }
    result.width = *property_width;
    result.domain = selected->type.four_state
        ? frontend::ValueDomain::Logic4
        : frontend::ValueDomain::Bit2;
    result.signed_value = selected->type.signed_value;
    result.static_storage = selected->static_storage;
    result.writable = !selected->constant && !selected->parameter;
    return result;
}

std::optional<Lowerer::HirClassPropertyMember>
Lowerer::hir_class_property_member(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    constexpr std::string_view selected_prefix { "@sv-select:" };
    std::string path;
    auto current = expression_id;
    for (std::size_t depth { }; depth < 16U; ++depth) {
        const auto expression = specialized_hir_unit_->find_expression(
            current);
        if (!expression || expression->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& source = *expression->systemverilog;
        if (source.kind != semantic::sv::ExpressionKind::call
            || !source.text.starts_with(selected_prefix)
            || source.operands.size() != 1U) {
            break;
        }
        const auto member = std::string_view { source.text }.substr(
            selected_prefix.size());
        path = path.empty() ? std::string { member }
                            : std::string { member } + "." + path;
        current = source.operands.front();
    }
    if (path.empty()) {
        return std::nullopt;
    }
    const auto profile = hir_class_property_profile(current);
    const auto expression = specialized_hir_unit_->find_expression(current);
    if (!profile || !expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto* selected = resolver.resolve_systemverilog_class_property(
        profile->identity, profile->static_storage).unique();
    if (selected == nullptr) {
        return std::nullopt;
    }
    // The property's type is named in the scope that declares it.
    const auto property_declaration = specialized_hir_unit_->find_declaration(
        selected->declaration);
    const auto type_scope = property_declaration
            && property_declaration->systemverilog != nullptr
        ? property_declaration->systemverilog->scope
        : expression->systemverilog->scope;
    auto member = hir_systemverilog_type_member_path(selected->type,
        type_scope, semantic::DeclarationId { }, path, true);
    if (!member) {
        member = hir_systemverilog_type_member_path(selected->type,
            expression->systemverilog->scope, semantic::DeclarationId { },
            path, true);
    }
    // A class body lowered in a module's process names compilation-unit
    // types as the module does.
    if (!member) {
        member = hir_systemverilog_type_member_path(selected->type,
            specialized_hir_unit_->scope(), semantic::DeclarationId { },
            path, true);
    }
    if (!member || member->tagged || member->width == 0U
        || member->offset + member->width > profile->width) {
        return std::nullopt;
    }
    return HirClassPropertyMember { current, *profile, std::move(*member) };
}

std::optional<RegisterId> Lowerer::lower_hir_class_property_member(
    const HirClassPropertyMember& member)
{
    const auto whole = lower_hir_expression(
        member.property, member.profile.width);
    if (!whole || register_width(*whole) != member.profile.width) {
        return std::nullopt;
    }
    const auto result = allocate_register(
        member.member.width, member.member.domain);
    process_.operations.emplace_back(Extract {
        result, *whole,
        static_cast<std::uint32_t>(member.member.offset),
        static_cast<std::uint32_t>(member.member.width) });
    return result;
}

bool Lowerer::lower_hir_class_property_member_write(
    const HirClassPropertyMember& member, RegisterId value)
{
    if (!member.profile.writable) {
        return false;
    }
    if (register_width(value) != member.member.width) {
        value = resize_register(
            value, member.member.width, member.member.signed_value);
    }
    const auto whole = lower_hir_expression(
        member.property, member.profile.width);
    if (!whole || register_width(*whole) != member.profile.width) {
        return false;
    }
    const auto updated = allocate_register(
        member.profile.width, register_domain(*whole));
    process_.operations.emplace_back(Insert {
        updated, *whole, value,
        static_cast<std::uint32_t>(member.member.offset) });
    if (member.profile.static_storage) {
        process_.operations.emplace_back(ClassStaticPropertyWrite {
            updated, member.profile.identity });
        return true;
    }
    const auto receiver = lower_hir_expression(*member.profile.receiver, 64U);
    if (!receiver || register_width(*receiver) != 64U) {
        return false;
    }
    process_.operations.emplace_back(ClassPropertyWrite {
        *receiver, updated, member.profile.identity });
    return true;
}

namespace {

// The single string actual of a string property access (see the class
// method hook in the application).
template <typename Call>
void prepare_string_property_call(Call& call, const StringRegisterId value,
    const bool write)
{
    call.method_identity = (write ? "@string-property-write:"
                                  : "@string-property-read:")
        + call.method_identity;
    call.actuals = { value };
    call.actual_names = { "value" };
    call.actual_directions = { static_cast<std::uint8_t>(write
            ? semantic::sv::Direction::input
            : semantic::sv::Direction::output) };
    call.actual_kinds = { 1U };
    call.result_width = 1U;
}

} // namespace

bool Lowerer::hir_genvar_hides_signal(
    const std::string_view name, const semantic::ScopeId scope) const
{
    // A genvar of an enclosing loop-generate block hides a signal of its
    // name (IEEE 1800-2017 27.4); its value is a hierarchy identity.
    return specialized_hir_unit_ != nullptr && scope.valid()
        && name.find('.') == std::string_view::npos
        && std::ranges::any_of(
            specialized_hir_unit_->specialization().hierarchy_identities,
            [&](const semantic::SpecializedHirNamedIdentity& identity) {
                return identity.name == name;
            })
        && semantic::CompiledDesignResolver {
               *specialized_hir_unit_, hir_generic_binding_frames_ }
               .resolve_systemverilog(name, scope, { }, true)
               .status
            == semantic::CompiledResolutionStatus::not_found;
}

std::optional<SignalId> Lowerer::hir_named_signal(
    const std::string& name, const semantic::ScopeId scope)
{
    if (hir_genvar_hides_signal(name, scope)) {
        return std::nullopt;
    }
    if (const auto found = signals_.find(name); found != signals_.end()) {
        return found->second;
    }
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto declaration
        = semantic::CompiledDesignResolver {
              *specialized_hir_unit_, hir_generic_binding_frames_ }
              .resolve_systemverilog(name, scope, { }, true)
              .unique();
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, scope, false)
        : std::nullopt;
    if (binding && binding->signal) {
        return binding->signal;
    }
    // A member of an instance (`@(posedge dif.clk)`), which may be
    // elaborated after this process: resolve it in the second pass.
    if (name.find('.') != std::string::npos) {
        if (const auto signal = hir_hierarchical_signal(name)) {
            hierarchical_reference_used_ = true;
            return signal;
        }
        if (hierarchical_reference_retry_) {
            hierarchical_reference_missed_ = true;
        }
    }
    return std::nullopt;
}

std::optional<StringRegisterId> Lowerer::lower_hir_class_string_property_read(
    const semantic::ExpressionId expression_id)
{
    const auto property = hir_class_property_profile(expression_id, true);
    if (!property) {
        return std::nullopt;
    }
    const auto value = allocate_string_register();
    process_.operations.emplace_back(LoadStringConstant { value, { } });
    const auto result = allocate_register(1U, frontend::ValueDomain::Bit2);
    if (property->static_storage) {
        ClassStaticMethodCall call;
        call.destination = result;
        call.method_identity = property->identity;
        prepare_string_property_call(call, value, false);
        process_.operations.emplace_back(std::move(call));
        return value;
    }
    const auto receiver = lower_hir_expression(*property->receiver, 64U);
    if (!receiver || register_width(*receiver) != 64U) {
        return std::nullopt;
    }
    ClassMethodCall call;
    call.destination = result;
    call.receiver = *receiver;
    call.method_identity = property->identity;
    call.virtual_dispatch = false;
    prepare_string_property_call(call, value, false);
    process_.operations.emplace_back(std::move(call));
    return value;
}

bool Lowerer::lower_hir_class_string_property_write(
    const semantic::ExpressionId target, const StringRegisterId value)
{
    const auto property = hir_class_property_profile(target, true);
    if (!property || !property->writable) {
        return false;
    }
    const auto result = allocate_register(1U, frontend::ValueDomain::Bit2);
    if (property->static_storage) {
        ClassStaticMethodCall call;
        call.destination = result;
        call.method_identity = property->identity;
        prepare_string_property_call(call, value, true);
        process_.operations.emplace_back(std::move(call));
        return true;
    }
    const auto receiver = lower_hir_expression(*property->receiver, 64U);
    if (!receiver || register_width(*receiver) != 64U) {
        return false;
    }
    ClassMethodCall call;
    call.destination = result;
    call.receiver = *receiver;
    call.method_identity = property->identity;
    call.virtual_dispatch = false;
    prepare_string_property_call(call, value, true);
    process_.operations.emplace_back(std::move(call));
    return true;
}

std::optional<std::vector<runtime::SystemVerilogConstraintTemplate>>
Lowerer::hir_inline_constraints(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto marker = std::ranges::find(
        expression->systemverilog->associations,
        "@sv-inline-constraint",
        &semantic::sv::AssignmentPatternAssociation::choice_spelling);
    if (marker == expression->systemverilog->associations.end()) {
        return std::vector<runtime::SystemVerilogConstraintTemplate> { };
    }

    using Template = runtime::SystemVerilogConstraintTemplate;
    using TemplateKind = runtime::SystemVerilogConstraintTemplateKind;
    using DomainKind = runtime::SystemVerilogConstraintDomainKind;
    const auto constant = [&](const semantic::ExpressionId id,
                              const std::string_view nominal)
        -> std::optional<Template> {
        const auto value = hir_constant_integer(id);
        const auto width = hir_expression_width(id, process_scope);
        const auto domain = hir_expression_domain(id, process_scope);
        if (!value || !width || *width == 0U || *width > 64U || !domain) {
            return std::nullopt;
        }
        Template output;
        output.kind = TemplateKind::Constant;
        output.constant = PackedLogic4::from_aval_bval(
            *width, static_cast<std::uint64_t>(*value), 0U);
        output.profile = {
            *domain == frontend::ValueDomain::Integer
                ? DomainKind::Integer
                : DomainKind::BitVector,
            *width,
            hir_expression_signed(id),
            nominal.empty()
                ? std::string { "$inline-constant" }
                : std::string { nominal },
            *domain == frontend::ValueDomain::Logic4,
        };
        return output;
    };

    std::function<std::optional<Template>(semantic::ExpressionId)> lower;
    lower = [&](const semantic::ExpressionId id) -> std::optional<Template> {
        const auto record = specialized_hir_unit_->find_expression(id);
        if (!record || record->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& source = *record->systemverilog;
        Template output;
        output.text = source.text;
        if (source.kind == semantic::sv::ExpressionKind::class_null) {
            output.kind = TemplateKind::Constant;
            output.constant = PackedLogic4::from_aval_bval(64U, 0U, 0U);
            output.profile = {
                DomainKind::BitVector,
                64U,
                false,
                "null",
                false,
            };
            return output;
        }
        if (source.kind == semantic::sv::ExpressionKind::name) {
            if (const auto folded = constant(id, source.nominal_type)) {
                return folded;
            }
            output.kind = TemplateKind::Name;
            return output;
        }
        if (source.kind
                == semantic::sv::ExpressionKind::integer_literal
            || source.kind
                == semantic::sv::ExpressionKind::boolean_literal
            || source.kind
                == semantic::sv::ExpressionKind::logic_literal) {
            return constant(id, source.nominal_type);
        }
        const auto special_kind = [&]() -> std::optional<TemplateKind> {
            if (source.kind == semantic::sv::ExpressionKind::unary) {
                return TemplateKind::Unary;
            }
            if (source.kind == semantic::sv::ExpressionKind::binary) {
                return TemplateKind::Binary;
            }
            if (source.kind != semantic::sv::ExpressionKind::call) {
                return std::nullopt;
            }
            if (source.text == "?:") {
                return TemplateKind::Conditional;
            }
            if (source.text == "inside") {
                return TemplateKind::InsideSet;
            }
            if (source.text == "@inside-range") {
                return TemplateKind::InsideRange;
            }
            if (source.text == "dist") {
                return TemplateKind::Distribution;
            }
            if (source.text == "@dist-:="
                || source.text == "@dist-:/") {
                return TemplateKind::DistributionItem;
            }
            if (source.text == "soft") {
                return TemplateKind::Soft;
            }
            if (source.text == "@constraint-block") {
                return TemplateKind::Block;
            }
            if (source.text == "@constraint-implies") {
                return TemplateKind::Implication;
            }
            if (source.text == "@constraint-if") {
                return TemplateKind::ConditionalConstraint;
            }
            if (source.text == "@solve-before") {
                return TemplateKind::SolveBefore;
            }
            if (source.text == "@solve-list") {
                return TemplateKind::SolveList;
            }
            if (source.text == "@constraint-unique") {
                return TemplateKind::Unique;
            }
            return std::nullopt;
        }();
        if (!special_kind) {
            return constant(id, source.nominal_type);
        }
        output.kind = *special_kind;
        output.operands.reserve(source.operands.size());
        for (const auto operand : source.operands) {
            auto lowered = lower(operand);
            if (!lowered) {
                return std::nullopt;
            }
            output.operands.push_back(std::move(*lowered));
        }
        return output;
    };

    std::vector<Template> result;
    result.reserve(marker->choices.size());
    for (const auto constraint : marker->choices) {
        auto lowered = lower(constraint);
        if (!lowered) {
            return std::nullopt;
        }
        result.push_back(std::move(*lowered));
    }
    return result;
}

std::optional<Lowerer::HirClassMethodProfile>
Lowerer::hir_class_method_profile(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    const auto instance_call = call.kind
        == semantic::sv::ExpressionKind::class_method_call;
    const auto static_call = call.kind
        == semantic::sv::ExpressionKind::class_static_method_call;
    if ((!instance_call && !static_call)
        || call.class_member_identity.empty()) {
        return std::nullopt;
    }

    constexpr auto randomize_identity
        = std::string_view { "@builtin-randomize" };
    constexpr auto random_mode_prefix
        = std::string_view { "@builtin-rand-mode:" };
    constexpr auto constraint_mode_prefix
        = std::string_view { "@builtin-constraint-mode:" };
    const auto builtin_randomize
        = call.class_member_identity == randomize_identity;
    const auto builtin_mode
        = call.class_member_identity.starts_with(random_mode_prefix)
        || call.class_member_identity.starts_with(constraint_mode_prefix);
    if (builtin_randomize || builtin_mode) {
        if (!instance_call || call.operands.empty()
            || (builtin_mode && call.operands.size() > 2U)
            || (!call.argument_names.empty()
                && call.argument_names.size() != call.operands.size())) {
            return std::nullopt;
        }
        const auto receiver_width = hir_expression_width(
            call.operands.front(), process_scope);
        if (!receiver_width || *receiver_width != 64U) {
            return std::nullopt;
        }

        HirClassMethodProfile result;
        result.identity = call.class_member_identity;
        result.receiver = call.operands.front();
        result.result_width = 32U;
        result.result_domain = frontend::ValueDomain::Integer;
        result.result_signed = true;
        result.virtual_dispatch = false;
        result.actuals.reserve(call.operands.size() - 1U);
        for (std::size_t index = 1U;
            index < call.operands.size(); ++index) {
            const auto width = hir_expression_width(
                call.operands[index], process_scope);
            const auto domain = hir_expression_domain(
                call.operands[index], process_scope);
            if (!width || *width == 0U
                || *width > std::numeric_limits<std::uint32_t>::max()
                || !domain || *domain == frontend::ValueDomain::String) {
                return std::nullopt;
            }
            HirClassMethodActual actual;
            actual.expression = call.operands[index];
            actual.direction = frontend::PortDirection::Input;
            actual.width = *width;
            actual.domain = *domain;
            if (!call.argument_names.empty()) {
                actual.name = call.argument_names[index];
            }
            result.actuals.push_back(std::move(actual));
        }
        return result;
    }

    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto* selected = resolver.resolve_systemverilog_class_method(
        call.class_member_identity, static_call,
        semantic::sv::ClassMethodKind::function).unique();
    const auto native_uvm = call.class_member_identity.starts_with("@uvm-")
        || call.class_member_identity.find("::uvm_")
            != std::string::npos;
    // A virtual call dispatches to the object's override, so a pure
    // virtual prototype supplies only the signature (IEEE 1800-2017 8.21).
    const auto dispatched = instance_call
        && !call.text.starts_with("@sv-base-method:");
    if (selected == nullptr
        || selected->kind != semantic::sv::ClassMethodKind::function
        || ((selected->pure || !selected->defined) && !native_uvm
            && !(dispatched && selected->pure))) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        selected->declaration);
    if (!declaration || declaration->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& callable = *declaration->systemverilog;
    const auto class_result = callable.type
        && callable.type->value_form
            == semantic::sv::TypeForm::class_handle;
    const auto string_result = callable.type
        && callable.type->value_form == semantic::sv::TypeForm::string;
    // A void method has no result; its calls discard a one-bit placeholder
    // (IEEE 1800-2017 13.4.1). A string result also leaves one.
    const auto result_width = callable.type
            && (callable.type->target.spelling == "void" || string_result)
        ? std::optional<std::size_t> { 1U }
        : callable.type ? hir_systemverilog_type_width(*callable.type)
                        : std::nullopt;
    if (!callable.callable || !callable.callable->function
        || !callable.type
        || (!class_result
            && (!result_width || *result_width == 0U
                || *result_width
                    > std::numeric_limits<std::uint32_t>::max()))) {
        return std::nullopt;
    }

    const auto operand_offset = instance_call ? 1U : 0U;
    if (call.operands.size() < operand_offset
        || call.operands.size() - operand_offset
            > callable.callable->formals.size()) {
        return std::nullopt;
    }
    std::span<const std::string> argument_names = call.argument_names;
    if (!argument_names.empty()
        && argument_names.size() != call.operands.size()
        && argument_names.size()
            != call.operands.size() - operand_offset) {
        return std::nullopt;
    }
    const auto argument_name = [&](const std::size_t operand_index) {
        if (argument_names.empty()) {
            return std::string_view { };
        }
        if (argument_names.size() == call.operands.size()) {
            return std::string_view { argument_names[operand_index] };
        }
        return std::string_view {
            argument_names[operand_index - operand_offset]
        };
    };

    HirClassMethodProfile result;
    result.declaration = selected->declaration;
    result.identity = selected->canonical_identity;
    result.result_width = class_result
        ? 64U
        : *result_width;
    result.result_domain = class_result
        ? frontend::ValueDomain::Bit2
        : callable.type->four_state
        ? frontend::ValueDomain::Logic4
        : frontend::ValueDomain::Bit2;
    result.result_signed = !class_result
        && callable.type->signed_value;
    result.static_method = static_call;
    result.virtual_dispatch = instance_call
        && !call.text.starts_with("@sv-base-method:");
    result.string_result = string_result;
    if (instance_call) {
        result.receiver = call.operands.front();
        const auto receiver_width = hir_expression_width(
            *result.receiver, process_scope);
        if (!receiver_width || *receiver_width != 64U) {
            return std::nullopt;
        }
    }

    result.actuals.reserve(call.operands.size() - operand_offset);
    std::vector<bool> bound(callable.callable->formals.size());
    std::size_t positional { };
    for (std::size_t operand_index = operand_offset;
        operand_index < call.operands.size(); ++operand_index) {
        auto formal_index = positional;
        const auto name = argument_name(operand_index);
        if (name.empty()) {
            while (formal_index < bound.size() && bound[formal_index]) {
                ++formal_index;
            }
            positional = formal_index + 1U;
        } else {
            const auto found = std::ranges::find_if(
                callable.callable->formals,
                [&](const semantic::DeclarationId formal) {
                    const auto record
                        = specialized_hir_unit_->find_declaration(formal);
                    return record && record->systemverilog != nullptr
                        && record->systemverilog->name == name;
                });
            if (found == callable.callable->formals.end()) {
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(callable.callable->formals.begin(), found));
        }
        if (formal_index >= bound.size()
            || bound[formal_index]) {
            return std::nullopt;
        }
        const auto formal_id = callable.callable->formals[formal_index];
        const auto formal = specialized_hir_unit_->find_declaration(formal_id);
        if (!formal || formal->systemverilog == nullptr
            || !formal->systemverilog->type) {
            return std::nullopt;
        }
        const auto& formal_record = *formal->systemverilog;
        const auto direction = class_method_direction(
            formal_record.direction);
        if (direction == frontend::PortDirection::Unknown) {
            return std::nullopt;
        }
        HirClassMethodActual actual;
        actual.formal = formal_id;
        actual.expression = call.operands[operand_index];
        actual.direction = direction;
        actual.name = std::string { name };
        actual.string = formal_record.type->value_form
            == semantic::sv::TypeForm::string;
        actual.container = hir_systemverilog_container_type(
            *formal_record.type).has_value();
        if (actual.container) {
            const auto type = hir_systemverilog_container_type(
                *formal_record.type);
            if (!type) {
                return std::nullopt;
            }
            actual.width = type->element_width;
            actual.domain = type->two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
            actual.signed_value = type->signed_elements;
        } else if (actual.string) {
            if (!hir_expression_is_string(
                    actual.expression, process_scope)) {
                return std::nullopt;
            }
            actual.domain = frontend::ValueDomain::String;
        } else {
            const auto width
                = hir_systemverilog_type_width(*formal_record.type);
            if (!width || *width == 0U
                || *width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            actual.width = *width;
            actual.domain = formal_record.type->four_state
                ? frontend::ValueDomain::Logic4
                : frontend::ValueDomain::Bit2;
            actual.signed_value = formal_record.type->signed_value;
        }
        result.actuals.push_back(std::move(actual));
        bound[formal_index] = true;
    }
    for (std::size_t index { }; index < bound.size(); ++index) {
        if (bound[index]) {
            continue;
        }
        const auto formal = specialized_hir_unit_->find_declaration(
            callable.callable->formals[index]);
        if (!formal || formal->systemverilog == nullptr
            || !formal->systemverilog->initializer) {
            return std::nullopt;
        }
    }
    return result;
}

std::optional<Lowerer::HirClassTaskProfile>
Lowerer::hir_class_task_profile(
    const semantic::StatementId statement_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& call = *statement->systemverilog;
    if (call.kind != semantic::sv::StatementKind::task_call) {
        return std::nullopt;
    }
    constexpr auto instance_prefix = std::string_view { "@sv-task:" };
    constexpr auto static_prefix
        = std::string_view { "@sv-static-task:" };
    const auto spelling = std::string_view { call.task.spelling };
    const auto instance_call = spelling.starts_with(instance_prefix);
    const auto static_call = spelling.starts_with(static_prefix);
    const auto class_call = instance_call || static_call;
    const auto identity = class_call
        ? spelling.substr(
              instance_call ? instance_prefix.size() : static_prefix.size())
        : spelling;
    if (identity.empty()) {
        return std::nullopt;
    }
    const auto native_uvm = class_call
        && (identity.starts_with("@uvm-")
            || identity.find("::uvm_") != std::string_view::npos);

    auto selected_declaration = std::optional<semantic::DeclarationId> { };
    auto interface_receiver = std::optional<std::string> { };
    bool instance_function { };
    const semantic::sv::ClassMethod* selected = nullptr;
    if (class_call) {
        const semantic::CompiledDesignResolver resolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
        selected = resolver.resolve_systemverilog_class_method(
            identity, static_call).unique();
        // A class function called as a statement (a void function, or a
        // discarded result) runs like a task when class functions are
        // lowered (13.4.1).
        if (selected == nullptr
            || (!native_uvm
                && selected->kind
                    != semantic::sv::ClassMethodKind::task
                && !(lower_class_functions_
                    && selected->kind
                        == semantic::sv::ClassMethodKind::function))
            || ((selected->pure || !selected->defined) && !native_uvm)) {
            return std::nullopt;
        }
        selected_declaration = selected->declaration;
    } else {
        const auto task = [](const semantic::CompiledDeclarationView& view) {
            return view.systemverilog != nullptr
                && view.systemverilog->form
                    == semantic::sv::DeclarationForm::task
                && view.systemverilog->callable
                && !view.systemverilog->callable->function;
        };
        const semantic::CompiledDesignResolver resolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
        selected_declaration = resolver.resolve_systemverilog_name(
            call.task, call.scope, task, false).unique();
        const auto separator = identity.rfind('.');
        if (!selected_declaration
            && separator != std::string_view::npos
            && separator != 0U && separator + 1U < identity.size()) {
            const auto receiver = identity.substr(0U, separator);
            selected_declaration
                = resolver.resolve_systemverilog_interface_member(
                      receiver, identity.substr(separator + 1U),
                      call.scope, task,
                      hir_generic_interface_type(receiver))
                      .unique();
            if (!selected_declaration) {
                // `intf.f(...);`: a void function of an interface called as
                // a statement (13.4.1).
                selected_declaration
                    = resolver.resolve_systemverilog_interface_member(
                          receiver, identity.substr(separator + 1U),
                          call.scope,
                          [](const semantic::CompiledDeclarationView& view) {
                              return view.systemverilog != nullptr
                                  && view.systemverilog->form
                                      == semantic::sv::DeclarationForm::
                                          function
                                  && view.systemverilog->callable
                                  && view.systemverilog->callable->function;
                          },
                          hir_generic_interface_type(receiver))
                          .unique();
                instance_function = selected_declaration.has_value();
            }
            if (selected_declaration) {
                interface_receiver = std::string { receiver };
            } else if (const auto instance = hir_instance_callable(
                           receiver, identity.substr(separator + 1U), false)) {
                // `u.t(...)`: a task of a module instance (23.8).
                selected_declaration = instance->first;
                interface_receiver = instance->second;
            } else if (const auto function_instance = hir_instance_callable(
                           receiver, identity.substr(separator + 1U), true)) {
                // `u.f(...);`: a function of a module instance called as a
                // statement, its result discarded (23.8, 13.4.1).
                selected_declaration = function_instance->first;
                interface_receiver = function_instance->second;
                instance_function = true;
            }
        }
    }
    if (!selected_declaration) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        *selected_declaration);
    if (!declaration || declaration->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& callable = *declaration->systemverilog;
    const bool statement_function
        = ((class_call && lower_class_functions_) || instance_function)
        && callable.callable && callable.callable->function;
    if (!callable.callable
        || (!native_uvm
            && ((callable.form != semantic::sv::DeclarationForm::task
                    && !statement_function)
                || (callable.callable->function && !statement_function)
                || !callable.nested_scope))) {
        return std::nullopt;
    }

    const auto actual_offset = instance_call ? 1U : 0U;
    if (call.task_arguments.size() < actual_offset
        || call.task_arguments.size() - actual_offset
            > callable.callable->formals.size()) {
        return std::nullopt;
    }
    HirClassTaskProfile result;
    result.declaration = *selected_declaration;
    result.identity = class_call
        ? selected->canonical_identity
        : std::string { identity };
    result.interface_receiver = std::move(interface_receiver);
    result.static_method = static_call;
    // Class methods have automatic lifetime unless declared static (IEEE
    // 1800-2017 8.6).
    result.automatic = native_uvm
        || callable.callable->lifetime
            == semantic::sv::Lifetime::automatic
        || (class_call
            && callable.callable->lifetime
                != semantic::sv::Lifetime::static_lifetime);
    if (instance_call) {
        const auto& receiver = call.task_arguments.front();
        if (!receiver.actual
            || hir_expression_width(*receiver.actual, process_scope)
                != std::optional<std::size_t> { 64U }) {
            return std::nullopt;
        }
        result.receiver = *receiver.actual;
    }

    auto actuals = bind_hir_class_actuals(*selected_declaration,
        std::span { call.task_arguments }.subspan(actual_offset),
        process_scope);
    if (!actuals) {
        return std::nullopt;
    }
    result.actuals = std::move(*actuals);
    return result;
}

std::optional<std::vector<Lowerer::HirClassMethodActual>>
Lowerer::bind_hir_class_actuals(
    const semantic::DeclarationId callable_declaration,
    const std::span<const semantic::sv::TaskAssociation> associations,
    const semantic::ScopeId process_scope) const
{
    const auto declaration = specialized_hir_unit_->find_declaration(
        callable_declaration);
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->callable) {
        return std::nullopt;
    }
    const auto& callable = *declaration->systemverilog;
    if (associations.size() > callable.callable->formals.size()) {
        return std::nullopt;
    }
    std::vector<std::optional<HirClassMethodActual>> bound(
        callable.callable->formals.size());
    const auto make_actual = [&](const semantic::DeclarationId formal_id,
                                 const semantic::ExpressionId expression,
                                 std::string name)
        -> std::optional<HirClassMethodActual> {
        const auto formal = specialized_hir_unit_->find_declaration(
            formal_id);
        if (!formal || formal->systemverilog == nullptr
            || !formal->systemverilog->type) {
            return std::nullopt;
        }
        const auto& formal_record = *formal->systemverilog;
        const auto direction = class_method_direction(
            formal_record.direction);
        if (direction == frontend::PortDirection::Unknown) {
            return std::nullopt;
        }
        HirClassMethodActual actual;
        actual.formal = formal_id;
        actual.expression = expression;
        actual.direction = direction;
        actual.name = std::move(name);
        actual.string = formal_record.type->value_form
            == semantic::sv::TypeForm::string;
        actual.container = hir_systemverilog_container_type(
            *formal_record.type).has_value();
        if (actual.container) {
            const auto type = hir_systemverilog_container_type(
                *formal_record.type);
            if (!type) {
                return std::nullopt;
            }
            actual.width = type->element_width;
            actual.domain = type->two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
            actual.signed_value = type->signed_elements;
            return actual;
        }
        if (actual.string) {
            if (!hir_expression_is_string(
                    actual.expression, process_scope)) {
                return std::nullopt;
            }
            actual.domain = frontend::ValueDomain::String;
            return actual;
        }
        const auto width = hir_systemverilog_type_width(
            *formal_record.type);
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        actual.width = *width;
        actual.domain = formal_record.type->four_state
            ? frontend::ValueDomain::Logic4
            : frontend::ValueDomain::Bit2;
        actual.signed_value = formal_record.type->signed_value;
        return actual;
    };
    std::size_t positional { };
    std::vector<bool> defaulted(bound.size());
    for (const auto& association : associations) {
        auto formal_index = positional;
        if (!association.formal || association.formal->empty()) {
            while (formal_index < bound.size()
                && (bound[formal_index].has_value()
                    || defaulted[formal_index])) {
                ++formal_index;
            }
            positional = formal_index + 1U;
            // An omitted positional actual takes the default (13.5.3).
            if (!association.actual) {
                if (formal_index >= bound.size()) {
                    return std::nullopt;
                }
                defaulted[formal_index] = true;
                continue;
            }
        } else {
            if (!association.actual) {
                return std::nullopt;
            }
            const auto found = std::ranges::find_if(
                callable.callable->formals,
                [&](const semantic::DeclarationId formal) {
                    const auto record
                        = specialized_hir_unit_->find_declaration(formal);
                    return record && record->systemverilog != nullptr
                        && record->systemverilog->name
                        == *association.formal;
                });
            if (found == callable.callable->formals.end()) {
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(callable.callable->formals.begin(), found));
        }
        if (formal_index >= bound.size()
            || bound[formal_index]) {
            return std::nullopt;
        }
        const auto formal_id = callable.callable->formals[formal_index];
        bound[formal_index] = make_actual(
            formal_id,
            *association.actual,
            association.formal.value_or(std::string { }));
        if (!bound[formal_index]) {
            return std::nullopt;
        }
    }
    for (std::size_t index { }; index < bound.size(); ++index) {
        if (bound[index]) {
            continue;
        }
        const auto formal = specialized_hir_unit_->find_declaration(
            callable.callable->formals[index]);
        if (!formal || formal->systemverilog == nullptr
            || !formal->systemverilog->initializer) {
            return std::nullopt;
        }
        bound[index] = make_actual(
            callable.callable->formals[index],
            *formal->systemverilog->initializer,
            { });
        if (!bound[index]) {
            return std::nullopt;
        }
    }
    std::vector<HirClassMethodActual> result;
    result.reserve(bound.size());
    for (auto& actual : bound) {
        result.push_back(std::move(*actual));
    }
    return result;
}

bool Lowerer::can_lower_hir_class_method_call(
    const semantic::ExpressionId expression,
    const semantic::ScopeId process_scope,
    std::unordered_set<std::uint32_t>& visiting) const
{
    const auto profile = hir_class_method_profile(
        expression, process_scope);
    if (!profile) {
        return false;
    }
    if (!hir_inline_constraints(expression, process_scope)) {
        return false;
    }
    if (profile->receiver
        && !can_lower_hir_expression(
            *profile->receiver, process_scope, visiting)) {
        return false;
    }
    return std::ranges::all_of(
        profile->actuals,
        [&](const HirClassMethodActual& actual) {
            const auto expression_supported = actual.string
                ? can_lower_hir_string_expression(
                      actual.expression, process_scope)
                : can_lower_hir_expression(
                      actual.expression, process_scope, visiting);
            if (!expression_supported
                || !class_method_copy_out(actual.direction)) {
                return expression_supported;
            }
            const auto target = hir_target_declaration(actual.expression);
            if (!target) {
                return false;
            }
            if (actual.string) {
                const auto binding = hir_string_binding(
                    *target, process_scope, false);
                return binding
                    && (binding->kind == HirStringBindingKind::local
                        || (binding->object
                            && !read_only_string_objects_.contains(
                                *binding->object)));
            }
            const auto binding = hir_runtime_binding(
                *target, process_scope, false);
            return binding
                && (binding->kind == HirRuntimeBindingKind::local
                    || signal_binding_is_writable(*binding));
        });
}

void Lowerer::rollback_hir_lowering(
    const std::size_t mark, const std::size_t frames)
{
    process_.operations.resize(mark);
    for (auto& frame : hir_callable_frames_) {
        std::erase_if(frame.call_sites,
            [&](const auto site) { return site >= mark; });
        std::erase_if(frame.invocation_push_sites,
            [&](const auto site) { return site >= mark; });
    }
    // A frame created for the abandoned call has no call site; it is
    // lowered only if another call uses it.
    std::erase_if(pending_hir_callables_, [&](const auto index) {
        return index >= frames;
    });
    for (auto index = frames; index < hir_callable_frames_.size(); ++index) {
        if (!hir_callable_frames_[index].lowered) {
            hir_callable_frames_[index].queued = false;
        }
    }
}

bool Lowerer::hir_class_derives_from_uvm(
    const std::string_view identity) const
{
    // A class extending the UVM library relies on the runtime's UVM
    // services (factory, phases, reporting) for its methods.
    const auto& classes
        = specialized_hir_unit_->design().systemverilog_hir.classes();
    std::set<std::string_view> visited;
    for (auto current = identity; !current.empty();) {
        if (current.find("uvm_") != std::string_view::npos) {
            return true;
        }
        if (!visited.insert(current).second) {
            return false;
        }
        const auto found = std::ranges::find(classes, current,
            &semantic::sv::ClassDeclaration::canonical_identity);
        if (found == classes.end()) {
            return false;
        }
        if (found->base && found->base->name.find("uvm_")
                != std::string::npos) {
            return true;
        }
        current = found->base_declaration_identity;
    }
    return false;
}

const semantic::sv::ClassDeclaration*
Lowerer::hir_lowerable_constructor_class(const std::string_view identity) const
{
    const auto& classes
        = specialized_hir_unit_->design().systemverilog_hir.classes();
    const auto find_class = [&](const std::string_view name)
        -> const semantic::sv::ClassDeclaration* {
        const auto found = std::ranges::find(classes, name,
            &semantic::sv::ClassDeclaration::canonical_identity);
        return found == classes.end() ? nullptr : &*found;
    };
    const auto* const selected = find_class(identity);
    if (hir_class_derives_from_uvm(identity)) {
        return nullptr;
    }
    std::set<std::string_view> visited;
    for (const auto* current = selected; current != nullptr;) {
        if (!visited.insert(current->canonical_identity).second
            || !current->parameters.empty() || current->interface_class
            || current->canonical_identity.find("uvm_pkg::")
                != std::string::npos
            || (current->base && !current->base->actuals.empty())) {
            return nullptr;
        }
        if (current->base_declaration_identity.empty()) {
            return selected;
        }
        current = find_class(current->base_declaration_identity);
    }
    return nullptr;
}

bool Lowerer::lower_hir_class_property_initializers(
    const semantic::sv::ClassDeclaration& declaration,
    const RegisterId receiver)
{
    const auto saved_receiver = hir_class_receiver_register_;
    hir_class_receiver_register_ = receiver;
    const auto restore = [&] { hir_class_receiver_register_ = saved_receiver; };
    for (const auto& property : declaration.properties) {
        if (property.static_storage || property.parameter
            || !property.initializer || property.type.container_form) {
            continue;
        }
        const auto initializer = specialized_hir_unit_->find_expression(
            *property.initializer);
        using semantic::sv::ExpressionKind;
        if (!initializer || initializer->systemverilog == nullptr) {
            restore();
            return false;
        }
        const auto kind = initializer->systemverilog->kind;
        if (kind == ExpressionKind::integer_literal
            || kind == ExpressionKind::boolean_literal
            || kind == ExpressionKind::string_literal) {
            continue;
        }
        if (property.type.value_form == semantic::sv::TypeForm::string) {
            const auto text = lower_hir_string_expression(*property.initializer);
            if (!text) {
                restore();
                return false;
            }
            ClassMethodCall call;
            call.destination = allocate_register(1U, frontend::ValueDomain::Bit2);
            call.receiver = receiver;
            call.method_identity = property.canonical_identity;
            call.virtual_dispatch = false;
            prepare_string_property_call(call, *text, true);
            process_.operations.emplace_back(std::move(call));
            continue;
        }
        const auto width = hir_systemverilog_type_width(property.type);
        if (!width || *width == 0U) {
            restore();
            return false;
        }
        const auto spelling = std::string_view { property.type.target.spelling };
        const auto scalar = spelling == "real"
            ? frontend::SystemVerilogScalarKind::Real
            : spelling == "shortreal"
            ? frontend::SystemVerilogScalarKind::ShortReal
            : spelling == "realtime"
            ? frontend::SystemVerilogScalarKind::Realtime
            : frontend::SystemVerilogScalarKind::None;
        auto value = lower_hir_scalar_converted_value(
            *property.initializer, *width, scalar);
        if (!value) {
            restore();
            return false;
        }
        if (register_width(*value) != *width) {
            value = resize_register(*value, *width,
                hir_expression_signed(*property.initializer));
        }
        process_.operations.emplace_back(ClassPropertyWrite {
            receiver, *value, property.canonical_identity });
    }
    restore();
    return true;
}

bool Lowerer::lower_hir_class_construction(
    const semantic::sv::ClassDeclaration& declaration,
    const RegisterId receiver,
    const std::span<const semantic::sv::TaskAssociation> actuals,
    const semantic::SourceSpanId source)
{
    const auto constructor = std::ranges::find_if(declaration.methods,
        [&](const semantic::sv::ClassMethod& method) {
            return method.kind == semantic::sv::ClassMethodKind::constructor
                && method.owner_identity == declaration.canonical_identity;
        });
    if (constructor != declaration.methods.end()) {
        if (!constructor->defined || constructor->pure) {
            return false;
        }
        auto bound = bind_hir_class_actuals(
            constructor->declaration, actuals, hir_process_scope_);
        if (!bound) {
            return false;
        }
        const HirClassTaskProfile profile {
            constructor->declaration,
            constructor->canonical_identity,
            std::nullopt,
            std::nullopt,
            std::move(*bound),
            false,
            true,
        };
        return lower_hir_class_callable_invocation(
                   source, profile, receiver, false)
            .has_value();
    }
    // The implicit constructor calls `super.new()` and runs the property
    // initializers (8.7).
    if (!actuals.empty()) {
        return false;
    }
    if (!declaration.base_declaration_identity.empty()) {
        const auto* const base = hir_lowerable_constructor_class(
            declaration.base_declaration_identity);
        if (base == nullptr
            || !lower_hir_class_construction(*base, receiver, { }, source)) {
            return false;
        }
    }
    return lower_hir_class_property_initializers(declaration, receiver);
}

namespace {

constexpr std::string_view base_constructor_prefix { "@sv-base-constructor:" };

} // namespace

bool Lowerer::lower_hir_constructor_prologue(
    const semantic::DeclarationId callable,
    const std::span<const semantic::StatementId> statements)
{
    const semantic::sv::ClassDeclaration* owner = nullptr;
    for (const auto& candidate :
        specialized_hir_unit_->design().systemverilog_hir.classes()) {
        for (const auto& method : candidate.methods) {
            if (method.declaration == callable
                && method.kind == semantic::sv::ClassMethodKind::constructor) {
                owner = &candidate;
            }
        }
    }
    if (owner == nullptr) {
        return true;
    }
    if (std::ranges::any_of(statements, [&](const semantic::StatementId id) {
            const auto statement = specialized_hir_unit_->find_statement(id);
            return statement && statement->systemverilog != nullptr
                && statement->systemverilog->kind
                    == semantic::sv::StatementKind::task_call
                && statement->systemverilog->task.spelling.starts_with(
                    base_constructor_prefix);
        })) {
        return true;
    }
    if (!hir_class_receiver_register_) {
        return false;
    }
    const auto receiver = *hir_class_receiver_register_;
    if (!owner->base_declaration_identity.empty()) {
        const auto* const base = hir_lowerable_constructor_class(
            owner->base_declaration_identity);
        if (base == nullptr
            || !lower_hir_class_construction(
                *base, receiver, { }, owner->source)) {
            return false;
        }
    }
    return lower_hir_class_property_initializers(*owner, receiver);
}

bool Lowerer::lower_hir_base_constructor_call(
    const semantic::StatementId statement_id)
{
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->systemverilog == nullptr
        || !active_hir_callable_ || !hir_class_receiver_register_
        || *active_hir_callable_ >= hir_callable_frames_.size()) {
        return false;
    }
    const auto callable
        = hir_callable_frames_[*active_hir_callable_].declaration;
    const semantic::sv::ClassDeclaration* owner = nullptr;
    for (const auto& candidate :
        specialized_hir_unit_->design().systemverilog_hir.classes()) {
        for (const auto& method : candidate.methods) {
            if (method.declaration == callable
                && method.kind == semantic::sv::ClassMethodKind::constructor) {
                owner = &candidate;
            }
        }
    }
    if (owner == nullptr || owner->base_declaration_identity.empty()) {
        return false;
    }
    const auto* const base = hir_lowerable_constructor_class(
        owner->base_declaration_identity);
    if (base == nullptr) {
        return false;
    }
    // The first association names `super` when the call was written
    // `super.new(...)`.
    std::span<const semantic::sv::TaskAssociation> actuals {
        statement->systemverilog->task_arguments
    };
    if (!actuals.empty() && actuals.front().actual) {
        const auto first = specialized_hir_unit_->find_expression(
            *actuals.front().actual);
        if (first && first->systemverilog != nullptr
            && first->systemverilog->text == "super") {
            actuals = actuals.subspan(1U);
        }
    }
    const auto receiver = *hir_class_receiver_register_;
    return lower_hir_class_construction(
               *base, receiver, actuals, statement->systemverilog->source)
        && lower_hir_class_property_initializers(*owner, receiver);
}

void Lowerer::cache_hir_class_container_properties(
    const semantic::DeclarationId callable, const RegisterId receiver)
{
    const auto& classes
        = specialized_hir_unit_->design().systemverilog_hir.classes();
    const semantic::sv::ClassDeclaration* owner = nullptr;
    for (const auto& candidate : classes) {
        for (const auto& method : candidate.methods) {
            if (method.declaration == callable) {
                owner = &candidate;
            }
        }
    }
    std::set<std::string> visited;
    for (const auto* current = owner; current != nullptr
         && visited.insert(current->canonical_identity).second;) {
        for (const auto& property : current->properties) {
            // Containers of class handles keep the object's handle storage.
            if (property.static_storage || !property.type.container_form
                || !property.type.class_identity.empty()) {
                continue;
            }
            const auto type = hir_systemverilog_container_type(property.type);
            if (!type) {
                continue;
            }
            const auto value = allocate_container_register(*type);
            process_.operations.emplace_back(ClassPropertyContainerRead {
                value, receiver, property.canonical_identity });
            hir_local_container_registers_.insert_or_assign(
                property.declaration.value(), value);
            hir_local_container_types_.insert_or_assign(
                property.declaration.value(), *type);
            hir_class_container_caches_.push_back(
                { value, property.canonical_identity, property.declaration });
        }
        const auto base = std::ranges::find(classes,
            current->base_declaration_identity,
            &semantic::sv::ClassDeclaration::canonical_identity);
        current = base == classes.end() ? nullptr : &*base;
    }
}

std::optional<ContainerType> Lowerer::hir_class_value_container_type(
    const std::string_view identity) const
{
    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto* property = resolver.resolve_systemverilog_class_property(
        std::string { identity }, false).unique();
    if (property == nullptr || property->static_storage
        || !property->type.container_form
        || !property->type.class_identity.empty()) {
        return std::nullopt;
    }
    return hir_systemverilog_container_type(property->type);
}

std::optional<Lowerer::HirClassContainerAccess>
Lowerer::lower_hir_class_value_container(
    const semantic::ExpressionId receiver_expression,
    const std::string_view identity)
{
    const auto type = hir_class_value_container_type(identity);
    if (!type) {
        return std::nullopt;
    }
    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto declaration = resolver.resolve_systemverilog_class_property(
        std::string { identity }, false).unique()->declaration;
    const auto receiver_record = specialized_hir_unit_->find_expression(
        receiver_expression);
    const auto cache = std::ranges::find(hir_class_container_caches_,
        identity, &HirClassContainerCache::identity);
    if (receiver_record && receiver_record->systemverilog != nullptr
        && (receiver_record->systemverilog->text == "this"
            || receiver_record->systemverilog->text == "super")
        && cache != hir_class_container_caches_.end()) {
        return HirClassContainerAccess { { cache->value, *type },
            std::nullopt, std::string { identity }, declaration };
    }
    const auto receiver = lower_hir_expression(receiver_expression, 64U);
    if (!receiver || register_width(*receiver) != 64U) {
        return std::nullopt;
    }
    const auto value = allocate_container_register(*type);
    process_.operations.emplace_back(ClassPropertyContainerRead {
        value, *receiver, std::string { identity } });
    return HirClassContainerAccess {
        { value, *type }, receiver, std::string { identity }, declaration };
}

void Lowerer::store_hir_class_value_container(
    const HirClassContainerAccess& access)
{
    if (access.receiver) {
        process_.operations.emplace_back(ClassPropertyContainerWrite {
            *access.receiver, access.container.value, access.identity });
    }
}

void Lowerer::flush_hir_class_container_caches()
{
    if (!hir_class_receiver_register_) {
        return;
    }
    for (const auto& cache : hir_class_container_caches_) {
        process_.operations.emplace_back(ClassPropertyContainerWrite {
            *hir_class_receiver_register_, cache.value, cache.identity });
    }
}

void Lowerer::reload_hir_class_container_caches()
{
    if (!hir_class_receiver_register_) {
        return;
    }
    for (const auto& cache : hir_class_container_caches_) {
        process_.operations.emplace_back(ClassPropertyContainerRead {
            cache.value, *hir_class_receiver_register_, cache.identity });
    }
}

std::optional<Lowerer::HirClassCallResult>
Lowerer::lower_hir_class_function_call(
    const semantic::ExpressionId expression,
    const HirClassMethodProfile& profile,
    const std::optional<RegisterId> receiver)
{
    // Methods the runtime provides (randomization, process and UVM
    // services) keep their runtime calls.
    if (!lower_class_functions_ || profile.static_method != !receiver
        || profile.identity.empty() || profile.identity.starts_with('@')
        || profile.identity.find("uvm_") != std::string::npos) {
        return std::nullopt;
    }
    if (const auto separator = profile.identity.rfind("::");
        separator != std::string::npos
        && hir_class_derives_from_uvm(
            std::string_view { profile.identity }.substr(0U, separator))) {
        return std::nullopt;
    }
    const semantic::sv::ClassMethod* method = nullptr;
    for (const auto& candidate :
        specialized_hir_unit_->design().systemverilog_hir.classes()) {
        for (const auto& item : candidate.methods) {
            if (item.declaration == profile.declaration) {
                method = &item;
            }
        }
    }
    static constexpr std::array runtime_methods {
        std::string_view { "randomize" },
        std::string_view { "pre_randomize" },
        std::string_view { "post_randomize" },
        std::string_view { "srandom" },
        std::string_view { "get_randstate" },
        std::string_view { "set_randstate" },
    };
    if (method == nullptr || !method->defined || method->pure
        || method->kind != semantic::sv::ClassMethodKind::function
        || std::ranges::find(runtime_methods, method->name)
            != runtime_methods.end()) {
        return std::nullopt;
    }
    const auto record = specialized_hir_unit_->find_expression(expression);
    const auto type = hir_callable_type(profile.declaration);
    if (!record || record->systemverilog == nullptr || !type
        || type->container || type->string != profile.string_result) {
        return std::nullopt;
    }
    const HirClassTaskProfile call {
        profile.declaration,
        profile.identity,
        profile.receiver,
        std::nullopt,
        profile.actuals,
        profile.static_method,
        true,
    };
    HirClassCallResult result;
    if (type->string) {
        result.string = allocate_string_register();
    } else {
        result.packed = allocate_register(type->width, type->domain);
    }
    const auto keep = [&](const HirClassCallResult& value) {
        if (result.string && value.string) {
            process_.operations.emplace_back(CopyStringRegister {
                *result.string, *value.string });
            return true;
        }
        if (result.packed && value.packed) {
            process_.operations.emplace_back(CopyRegister {
                *result.packed, *value.packed });
            return true;
        }
        return false;
    };
    const auto source = record->systemverilog->source;
    std::vector<InstructionIndex> exits;
    const auto overrides = profile.virtual_dispatch && receiver
        ? hir_class_method_overrides(profile.declaration)
        : std::vector<HirClassOverride> { };
    for (const auto& item : overrides) {
        const auto selected = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ClassMethodCall {
            selected, *receiver, "@checked-cast:" + item.owner_identity,
            { }, { }, { }, 1U, false,
        });
        const auto branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            selected, branch + 1U, 0U, UnknownBranchPolicy::when_false });
        auto override_call = call;
        override_call.declaration = item.declaration;
        override_call.identity = item.identity;
        const auto value = lower_hir_class_callable_invocation(
            source, override_call, receiver, true);
        if (!value || !keep(*value)) {
            return std::nullopt;
        }
        exits.push_back(static_cast<InstructionIndex>(
            process_.operations.size()));
        process_.operations.emplace_back(Jump { });
        process_.operations[branch] = Branch {
            selected,
            branch + 1U,
            static_cast<InstructionIndex>(process_.operations.size()),
            UnknownBranchPolicy::when_false,
        };
    }
    const auto value = lower_hir_class_callable_invocation(
        source, call, receiver, true);
    if (!value || !keep(*value)) {
        return std::nullopt;
    }
    for (const auto exit : exits) {
        process_.operations[exit] = Jump {
            static_cast<InstructionIndex>(process_.operations.size()) };
    }
    class_functions_lowered_ = true;
    return result;
}

std::optional<RegisterId> Lowerer::lower_hir_class_method_call(
    const semantic::ExpressionId expression,
    const std::size_t expected_width,
    std::optional<StringRegisterId>* const string_result)
{
    const auto profile = hir_class_method_profile(
        expression, hir_process_scope_);
    if (!profile || (profile->string_result != (string_result != nullptr))) {
        return std::nullopt;
    }
    auto inline_constraints = hir_inline_constraints(
        expression, hir_process_scope_);
    if (!inline_constraints) {
        return std::nullopt;
    }
    std::optional<RegisterId> receiver;
    if (profile->receiver) {
        receiver = lower_hir_expression(*profile->receiver, 64U);
        if (!receiver || register_width(*receiver) != 64U) {
            return std::nullopt;
        }
    }
    // A class function runs as a lowered subroutine when it can; otherwise
    // the runtime interprets it.
    if (receiver.has_value() != profile->static_method
        && inline_constraints->empty()) {
        const auto mark = process_.operations.size();
        const auto frames = hir_callable_frames_.size();
        if (const auto lowered = lower_hir_class_function_call(
                expression, *profile, receiver)) {
            if (lowered->string) {
                *string_result = *lowered->string;
                const auto unused = allocate_register(
                    std::max<std::size_t>(profile->result_width, 1U),
                    frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    unused, unsigned_value(0U, register_width(unused)) });
                return unused;
            }
            if (expected_width != 0U
                && expected_width != register_width(*lowered->packed)) {
                return resize_register(
                    *lowered->packed, expected_width, profile->result_signed);
            }
            return lowered->packed;
        }
        rollback_hir_lowering(mark, frames);
    }

    std::vector<RegisterId> actuals;
    std::vector<std::uint8_t> actual_kinds;
    std::vector<std::string> actual_names;
    std::vector<std::uint8_t> actual_directions;
    actuals.reserve(profile->actuals.size());
    actual_kinds.reserve(profile->actuals.size());
    actual_names.reserve(profile->actuals.size());
    actual_directions.reserve(profile->actuals.size());
    for (const auto& actual : profile->actuals) {
        if (actual.string) {
            const auto lowered = lower_hir_string_expression(
                actual.expression);
            if (!lowered) {
                return std::nullopt;
            }
            actuals.push_back(*lowered);
            actual_kinds.push_back(1U);
        } else {
            auto lowered = lower_hir_expression(
                actual.expression, actual.width);
            if (!lowered) {
                return std::nullopt;
            }
            if (register_width(*lowered) != actual.width) {
                lowered = resize_register(
                    *lowered, actual.width,
                    hir_actual_extension_signed(
                        actual.expression, actual.signed_value));
            }
            actuals.push_back(*lowered);
            actual_kinds.push_back(0U);
        }
        actual_names.push_back(actual.name);
        actual_directions.push_back(
            static_cast<std::uint8_t>(actual.direction));
    }

    if (profile->string_result) {
        // The runtime writes the result into this trailing string actual.
        const auto text = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant { text, { } });
        actuals.push_back(text);
        actual_kinds.push_back(1U);
        actual_names.emplace_back("@result-string");
        actual_directions.push_back(
            static_cast<std::uint8_t>(frontend::PortDirection::Output));
        *string_result = text;
    }
    const auto destination = allocate_register(
        profile->result_width, profile->result_domain);
    if (profile->static_method) {
        process_.operations.emplace_back(ClassStaticMethodCall {
            destination,
            profile->identity,
            actuals,
            actual_names,
            actual_directions,
            static_cast<std::uint32_t>(profile->result_width),
            actual_kinds,
        });
    } else {
        ClassMethodCall operation {
            destination,
            *receiver,
            profile->identity,
            actuals,
            actual_names,
            actual_directions,
            static_cast<std::uint32_t>(profile->result_width),
            profile->virtual_dispatch,
            actual_kinds,
        };
        operation.inline_constraints = std::move(*inline_constraints);
        process_.operations.emplace_back(std::move(operation));
    }
    for (std::size_t index { }; index < profile->actuals.size(); ++index) {
        const auto& actual = profile->actuals[index];
        if (!class_method_copy_out(actual.direction)) {
            continue;
        }
        const auto copied = actual.string
            ? lower_hir_string_copy_out(
                  actual.expression, actuals[index])
            : lower_hir_packed_copy_out(
                  actual.expression, actuals[index]);
        if (!copied) {
            return std::nullopt;
        }
    }
    if (expected_width != 0U
        && expected_width != profile->result_width) {
        return resize_register(
            destination, expected_width, profile->result_signed);
    }
    return destination;
}

void Lowerer::transfer_hir_static_formals(
    const HirCallableFrame& frame, const bool copy_in)
{
    if (!copy_in && frame.function) {
        if (const auto result = hir_static_storage(frame.declaration, &frame)) {
            if (frame.type.string && result->string) {
                process_.operations.emplace_back(ReadStringObject {
                    frame.string_result, *result->string });
            } else if (frame.container_result && result->container) {
                process_.operations.emplace_back(ReadContainerObject {
                    *frame.container_result, *result->container });
            } else if (result->signal) {
                process_.operations.emplace_back(ReadSignal {
                    frame.result, *result->signal });
            }
        }
    }
    for (std::size_t index { }; index < frame.formals.size(); ++index) {
        if (!(copy_in ? callable_copy_in(frame.directions[index])
                     : class_method_copy_out(frame.directions[index]))) {
            continue;
        }
        const auto storage = hir_static_storage(frame.formals[index], &frame);
        if (!storage) {
            continue;
        }
        if (frame.argument_is_string[index] && storage->string) {
            if (copy_in) {
                process_.operations.emplace_back(WriteStringObject {
                    *storage->string, frame.string_arguments[index] });
            } else {
                process_.operations.emplace_back(ReadStringObject {
                    frame.string_arguments[index], *storage->string });
            }
        } else if (frame.argument_is_container[index] && storage->container) {
            if (copy_in) {
                process_.operations.emplace_back(WriteContainerObject {
                    *storage->container, frame.container_arguments[index], std::nullopt });
            } else {
                process_.operations.emplace_back(ReadContainerObject {
                    frame.container_arguments[index], *storage->container });
            }
        } else if (storage->signal) {
            if (copy_in) {
                process_.operations.emplace_back(WriteBlocking {
                    *storage->signal, frame.arguments[index] });
            } else {
                process_.operations.emplace_back(ReadSignal {
                    frame.arguments[index], *storage->signal });
            }
        }
    }
}

bool Lowerer::allocate_hir_static_callable_declarations(
    const std::size_t frame_index)
{
    if (frame_index >= hir_callable_frames_.size()) {
        return false;
    }
    if (specialized_hir_unit_ == nullptr) {
        return true;
    }
    // An automatic callable keeps only its explicitly static variables
    // across calls (IEEE 1800-2017 6.21).
    const bool automatic_frame = hir_callable_frames_[frame_index].type.automatic;
    if (automatic_frame) {
        const auto callable = specialized_hir_unit_->find_declaration(
            hir_callable_frames_[frame_index].declaration);
        const bool any_static = callable && callable->systemverilog != nullptr
            && std::ranges::any_of(callable->systemverilog->children,
                [&](const semantic::DeclarationId child) {
                    const auto record
                        = specialized_hir_unit_->find_declaration(child);
                    return record && record->systemverilog != nullptr
                        && record->systemverilog->form
                            == semantic::sv::DeclarationForm::variable
                        && record->systemverilog->lifetime
                            == semantic::sv::Lifetime::static_lifetime;
                });
        if (!any_static) {
            return true;
        }
    }
    const auto frame_declaration
        = hir_callable_frames_[frame_index].declaration;
    const auto frame_scope = hir_callable_frames_[frame_index].scope;
    const auto frame_debug_name
        = hir_callable_frames_[frame_index].debug_name;
    const auto callable = specialized_hir_unit_->find_declaration(
        frame_declaration);
    if (!callable || callable->systemverilog == nullptr
        || !callable->systemverilog->callable
        || !callable->systemverilog->nested_scope) {
        return false;
    }

    const auto saved_scope = hir_process_scope_;
    auto saved_hir_locals = std::move(hir_local_registers_);
    auto saved_hir_string_locals
        = std::move(hir_local_string_registers_);
    auto saved_hir_container_locals
        = std::move(hir_local_container_registers_);
    auto saved_hir_container_types
        = std::move(hir_local_container_types_);
    auto saved_locals = std::move(locals_);
    auto saved_string_locals = std::move(string_locals_);
    auto saved_local_scope = std::move(local_scope_);
    const auto saved_active = active_hir_callable_;
    hir_local_registers_.clear();
    hir_local_string_registers_.clear();
    hir_local_container_registers_.clear();
    hir_local_container_types_.clear();
    locals_.clear();
    string_locals_.clear();
    local_scope_ = { frame_debug_name.empty()
            ? callable->systemverilog->name
            : frame_debug_name };
    hir_process_scope_ = *callable->systemverilog->nested_scope;
    active_hir_callable_.reset();

    const auto restore = [&] {
        active_hir_callable_ = saved_active;
        local_scope_ = std::move(saved_local_scope);
        string_locals_ = std::move(saved_string_locals);
        locals_ = std::move(saved_locals);
        hir_local_container_types_
            = std::move(saved_hir_container_types);
        hir_local_container_registers_
            = std::move(saved_hir_container_locals);
        hir_local_string_registers_
            = std::move(saved_hir_string_locals);
        hir_local_registers_ = std::move(saved_hir_locals);
        hir_process_scope_ = saved_scope;
    };

    std::unordered_set<std::uint32_t> allocated;
    const auto allocate = [&](const std::span<
                                  const semantic::DeclarationId> declarations) {
        std::vector<semantic::DeclarationId> variables;
        variables.reserve(declarations.size());
        for (const auto declaration_id : declarations) {
            const auto declaration
                = specialized_hir_unit_->find_declaration(declaration_id);
            if (!declaration || declaration->systemverilog == nullptr
                || declaration->systemverilog->form
                    != semantic::sv::DeclarationForm::variable
                || declaration->systemverilog->lifetime
                    == semantic::sv::Lifetime::automatic
                || (automatic_frame
                    && declaration->systemverilog->lifetime
                        != semantic::sv::Lifetime::static_lifetime)
                || hir_static_storage(
                    declaration_id, &hir_callable_frames_[frame_index])
                || !allocated.insert(declaration_id.value()).second) {
                continue;
            }
            variables.push_back(declaration_id);
        }
        if (!initialize_hir_declarations(variables)) {
            return false;
        }
        for (const auto declaration_id : variables) {
            const auto raw = declaration_id.value();
            if (const auto container
                = hir_local_container_registers_.find(raw);
                container != hir_local_container_registers_.end()) {
                const auto type = hir_local_container_types_.find(raw);
                if (type == hir_local_container_types_.end()) {
                    return false;
                }
                hir_callable_frames_[frame_index]
                    .static_container_variables.emplace(
                    raw, container->second);
                hir_callable_frames_[frame_index]
                    .static_container_types.emplace(raw, type->second);
                continue;
            }
            if (const auto string
                = hir_local_string_registers_.find(raw);
                string != hir_local_string_registers_.end()) {
                hir_callable_frames_[frame_index]
                    .static_string_variables.emplace(
                    raw, string->second);
                continue;
            }
            const auto packed = hir_local_registers_.find(raw);
            if (packed == hir_local_registers_.end()) {
                return false;
            }
            hir_callable_frames_[frame_index].static_variables.emplace(
                raw, packed->second);
        }
        return true;
    };

    std::vector<semantic::DeclarationId> root_declarations;
    for (const auto declaration_id : callable->systemverilog->children) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            declaration_id);
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->scope == frame_scope) {
            root_declarations.push_back(declaration_id);
        }
    }
    bool valid = allocate(root_declarations);
    const auto allocate_statements = [&](const auto& self,
                                         const std::span<
                                             const semantic::StatementId>
                                             statements) -> bool {
        for (const auto statement_id : statements) {
            const auto statement = specialized_hir_unit_->find_statement(
                statement_id);
            if (!statement || statement->systemverilog == nullptr) {
                return false;
            }
            const auto& input = *statement->systemverilog;
            // A class task call retains its callee's body for the class
            // executor; those locals belong to the callee's own frame.
            if (input.kind == semantic::sv::StatementKind::task_call) {
                continue;
            }
            const bool scoped
                = input.kind == semantic::sv::StatementKind::block
                || input.kind == semantic::sv::StatementKind::fork;
            const auto outer_scope = hir_process_scope_;
            auto outer_hir_locals = hir_local_registers_;
            auto outer_hir_string_locals = hir_local_string_registers_;
            auto outer_hir_container_locals
                = hir_local_container_registers_;
            auto outer_hir_container_types = hir_local_container_types_;
            auto outer_locals = locals_;
            auto outer_string_locals = string_locals_;
            const auto outer_local_scope_size = local_scope_.size();
            if (scoped) {
                hir_process_scope_ = input.nested_scope.value_or(
                    hir_process_scope_);
                if (input.kind == semantic::sv::StatementKind::block) {
                    local_scope_.push_back(!input.label.empty()
                            ? input.label
                            : input.nested_scope
                            ? "$block_"
                                + std::to_string(
                                    input.nested_scope->value())
                            : std::string { "$block" });
                }
            }
            const bool nested_valid = allocate(input.declarations)
                && self(self, input.statements)
                && self(self, input.else_statements)
                && std::ranges::all_of(
                    input.case_alternatives,
                    [&](const auto& alternative) {
                        return self(self, alternative.statements);
                    });
            if (scoped) {
                local_scope_.resize(outer_local_scope_size);
                string_locals_ = std::move(outer_string_locals);
                locals_ = std::move(outer_locals);
                hir_local_container_types_
                    = std::move(outer_hir_container_types);
                hir_local_container_registers_
                    = std::move(outer_hir_container_locals);
                hir_local_string_registers_
                    = std::move(outer_hir_string_locals);
                hir_local_registers_ = std::move(outer_hir_locals);
                hir_process_scope_ = outer_scope;
            }
            if (!nested_valid) {
                return false;
            }
        }
        return true;
    };
    valid = valid && allocate_statements(
        allocate_statements, callable->systemverilog->statements);
    restore();
    return valid;
}

std::optional<std::pair<semantic::DeclarationId,
    std::vector<std::pair<std::uint64_t, std::string>>>>
Lowerer::hir_virtual_interface_instances(const std::string_view receiver) const
{
    // The instances a virtual interface variable named `receiver` can
    // designate: every instance of its interface type, by handle.
    if (specialized_hir_unit_ == nullptr || receiver.empty()
        || receiver.starts_with("@instance:")
        || systemverilog_interface_handles_ == nullptr
        || systemverilog_interface_types_ == nullptr) {
        return std::nullopt;
    }
    const auto receiver_declaration = semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    }.resolve_systemverilog(receiver, hir_process_scope_,
         [](const semantic::CompiledDeclarationView& candidate) {
             return candidate.systemverilog != nullptr
                 && candidate.systemverilog->type.has_value()
                 && candidate.systemverilog->type->virtual_interface;
         },
         false)
                                          .unique();
    const auto declaration = receiver_declaration
        ? specialized_hir_unit_->find_declaration(*receiver_declaration)
        : std::nullopt;
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->type
        || declaration->systemverilog->type->interface_type.empty()) {
        return std::nullopt;
    }
    // Every interface instance must be elaborated: resolve in the second
    // lowering pass.
    if (hierarchical_reference_retry_) {
        hierarchical_reference_missed_ = true;
        return std::nullopt;
    }
    const auto& interface_type
        = declaration->systemverilog->type->interface_type;
    std::vector<std::pair<std::uint64_t, std::string>> instances;
    std::set<std::uint64_t> seen;
    for (const auto& [path, type] : *systemverilog_interface_types_) {
        const auto handle = systemverilog_interface_handles_->find(path);
        if (type == interface_type
            && handle != systemverilog_interface_handles_->end()
            && seen.insert(handle->second).second) {
            instances.emplace_back(handle->second, path);
        }
    }
    if (instances.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(instances);
    return std::pair { *receiver_declaration, std::move(instances) };
}

bool Lowerer::lower_hir_class_task_call(
    const semantic::StatementId statement_id)
{
    if (const auto statement = specialized_hir_unit_->find_statement(
            statement_id);
        statement && statement->systemverilog != nullptr
        && statement->systemverilog->task.spelling.starts_with(
            base_constructor_prefix)) {
        return lower_hir_base_constructor_call(statement_id);
    }
    const auto profile = hir_class_task_profile(
        statement_id, hir_process_scope_);
    if (!profile) {
        return false;
    }
    const auto native_uvm = profile->identity.starts_with("@uvm-")
        || profile->identity.find("::uvm_") != std::string::npos;
    std::optional<RegisterId> receiver;
    if (profile->receiver) {
        receiver = lower_hir_expression(*profile->receiver, 64U);
        if (!receiver || register_width(*receiver) != 64U) {
            return false;
        }
    } else if (!profile->static_method && hir_class_receiver_register_
        && !profile->interface_receiver) {
        // A method called by its simple name inside another method of the
        // object runs on `this` (8.11).
        receiver = *hir_class_receiver_register_;
    }

    if (native_uvm) {
        std::vector<RegisterId> actuals;
        std::vector<std::uint8_t> actual_kinds;
        std::vector<std::string> actual_names;
        std::vector<std::uint8_t> actual_directions;
        actuals.reserve(profile->actuals.size());
        actual_kinds.reserve(profile->actuals.size());
        actual_names.reserve(profile->actuals.size());
        actual_directions.reserve(profile->actuals.size());
        for (const auto& actual : profile->actuals) {
            if (actual.string) {
                const auto lowered = lower_hir_string_expression(
                    actual.expression);
                if (!lowered) {
                    return false;
                }
                actuals.push_back(*lowered);
                actual_kinds.push_back(1U);
            } else {
                auto lowered = lower_hir_expression(
                    actual.expression, actual.width);
                if (!lowered) {
                    return false;
                }
                if (register_width(*lowered) != actual.width) {
                    lowered = resize_register(
                        *lowered, actual.width,
                        hir_actual_extension_signed(
                            actual.expression, actual.signed_value));
                }
                actuals.push_back(*lowered);
                actual_kinds.push_back(0U);
            }
            actual_names.push_back(actual.name);
            actual_directions.push_back(
                static_cast<std::uint8_t>(actual.direction));
        }
        emit_deferred_assertion_action_handoff();
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        if (profile->static_method) {
            process_.operations.emplace_back(ClassStaticMethodCall {
                destination,
                profile->identity,
                actuals,
                actual_names,
                actual_directions,
                1U,
                actual_kinds,
            });
        } else {
            process_.operations.emplace_back(ClassMethodCall {
                destination,
                *receiver,
                profile->identity,
                actuals,
                actual_names,
                actual_directions,
                1U,
                true,
                actual_kinds,
            });
        }
        for (std::size_t index { };
            index < profile->actuals.size(); ++index) {
            const auto& actual = profile->actuals[index];
            if (!class_method_copy_out(actual.direction)) {
                continue;
            }
            const auto copied = actual.string
                ? lower_hir_string_copy_out(
                      actual.expression, actuals[index])
                : lower_hir_packed_copy_out(
                      actual.expression, actuals[index]);
            if (!copied) {
                return false;
            }
        }
        return true;
    }

    // A virtual task called through a class handle runs the override of
    // the object's dynamic class (IEEE 1800-2017 8.20): test the handle
    // against each overriding class, most derived first.
    if (receiver && !profile->static_method) {
        std::vector<InstructionIndex> exits;
        for (const auto& item : hir_class_method_overrides(profile->declaration)) {
            const auto selected = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(ClassMethodCall {
                selected,
                *receiver,
                "@checked-cast:" + item.owner_identity,
                { },
                { },
                { },
                1U,
                false,
            });
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                selected, branch + 1U, 0U, UnknownBranchPolicy::when_false });
            auto override_profile = *profile;
            override_profile.declaration = item.declaration;
            override_profile.identity = item.identity;
            if (!lower_hir_class_task_invocation(statement_id, override_profile, receiver)) {
                return false;
            }
            exits.push_back(static_cast<InstructionIndex>(
                process_.operations.size()));
            process_.operations.emplace_back(Jump { });
            process_.operations[branch] = Branch {
                selected,
                branch + 1U,
                static_cast<InstructionIndex>(process_.operations.size()),
                UnknownBranchPolicy::when_false,
            };
        }
        if (!lower_hir_class_task_invocation(statement_id, *profile, receiver)) {
            return false;
        }
        for (const auto exit : exits) {
            process_.operations[exit] = Jump {
                static_cast<InstructionIndex>(process_.operations.size()) };
        }
        return true;
    }
    // A task of the interface a virtual interface handle names (`v.t()`,
    // IEEE 1800-2017 25.9) runs on that instance: test the handle against
    // each instance of the interface type.
    if (profile->interface_receiver
        && !profile->interface_receiver->starts_with("@instance:")
        && systemverilog_interface_handles_ != nullptr
        && systemverilog_interface_types_ != nullptr) {
        const auto dispatch = hir_virtual_interface_instances(
            *profile->interface_receiver);
        if (hierarchical_reference_missed_ && hierarchical_reference_retry_) {
            return false;
        }
        if (dispatch) {
            const auto& [receiver_declaration, instances] = *dispatch;
            const auto handle
                = lower_hir_virtual_interface_handle(receiver_declaration);
            if (handle) {
                std::vector<InstructionIndex> exits;
                for (const auto& [identity, path] : instances) {
                    const auto constant = allocate_register(
                        64U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        constant, unsigned_value(identity, 64U) });
                    const auto selected = allocate_register(
                        1U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::case_equal, selected, *handle,
                        constant });
                    const auto branch = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(Branch {
                        selected, branch + 1U, 0U,
                        UnknownBranchPolicy::when_false });
                    auto instance_profile = *profile;
                    instance_profile.interface_receiver = "@instance:" + path;
                    if (!lower_hir_class_task_invocation(
                            statement_id, instance_profile, receiver)) {
                        return false;
                    }
                    exits.push_back(static_cast<InstructionIndex>(
                        process_.operations.size()));
                    process_.operations.emplace_back(Jump { });
                    process_.operations[branch] = Branch {
                        selected,
                        branch + 1U,
                        static_cast<InstructionIndex>(
                            process_.operations.size()),
                        UnknownBranchPolicy::when_false,
                    };
                }
                for (const auto exit : exits) {
                    process_.operations[exit] = Jump {
                        static_cast<InstructionIndex>(
                            process_.operations.size()) };
                }
                return true;
            }
        }
    }
    return lower_hir_class_task_invocation(statement_id, *profile, receiver);
}

std::vector<Lowerer::HirClassOverride> Lowerer::hir_class_method_overrides(
    const semantic::DeclarationId declaration) const
{
    const auto& classes
        = specialized_hir_unit_->design().systemverilog_hir.classes();
    const semantic::sv::ClassDeclaration* owner = nullptr;
    const semantic::sv::ClassMethod* called = nullptr;
    for (const auto& candidate : classes) {
        for (const auto& method : candidate.methods) {
            if (method.declaration == declaration) {
                owner = &candidate;
                called = &method;
            }
        }
    }
    if (owner == nullptr || called == nullptr || called->static_method) {
        return { };
    }
    const auto find_class = [&](const std::string& identity)
        -> const semantic::sv::ClassDeclaration* {
        const auto found = std::ranges::find(classes, identity,
            &semantic::sv::ClassDeclaration::canonical_identity);
        return found == classes.end() ? nullptr : &*found;
    };
    const auto same_method = [&](const semantic::sv::ClassMethod& item) {
        return item.name == called->name && item.kind == called->kind
            && !item.static_method;
    };
    // A method is virtual when it, or the method it overrides in a base
    // class, is declared virtual (8.20).
    bool virtual_method = called->virtual_method;
    {
        std::set<std::string> visited;
        for (auto* current = find_class(owner->base_declaration_identity);
             !virtual_method && current != nullptr
             && visited.insert(current->canonical_identity).second;
             current = find_class(current->base_declaration_identity)) {
            virtual_method = std::ranges::any_of(current->methods,
                [&](const semantic::sv::ClassMethod& item) {
                    return same_method(item) && item.virtual_method;
                });
        }
    }
    if (!virtual_method) {
        return { };
    }
    struct Ranked {
        HirClassOverride item;
        std::size_t depth;
    };
    std::vector<Ranked> overrides;
    for (const auto& candidate : classes) {
        if (&candidate == owner) {
            continue;
        }
        std::size_t depth { };
        bool derived { };
        std::set<std::string> visited;
        for (auto* current = &candidate; current != nullptr
             && !current->base_declaration_identity.empty()
             && visited.insert(current->canonical_identity).second;) {
            ++depth;
            if (current->base_declaration_identity
                == owner->canonical_identity) {
                derived = true;
                break;
            }
            current = find_class(current->base_declaration_identity);
        }
        if (!derived) {
            continue;
        }
        const auto method = std::ranges::find_if(candidate.methods,
            [&](const semantic::sv::ClassMethod& item) {
                return same_method(item) && item.defined;
            });
        if (method != candidate.methods.end()) {
            overrides.push_back({ { candidate.canonical_identity,
                method->declaration, method->canonical_identity }, depth });
        }
    }
    std::ranges::stable_sort(overrides, std::greater { }, &Ranked::depth);
    std::vector<HirClassOverride> result;
    result.reserve(overrides.size());
    for (auto& item : overrides) {
        result.push_back(std::move(item.item));
    }
    return result;
}

bool Lowerer::lower_hir_class_task_invocation(
    const semantic::StatementId statement_id,
    const HirClassTaskProfile& profile,
    const std::optional<RegisterId> receiver)
{
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    const auto callable = specialized_hir_unit_->find_declaration(
        profile.declaration);
    if (!statement || statement->systemverilog == nullptr || !callable
        || callable->systemverilog == nullptr
        || !callable->systemverilog->callable) {
        return false;
    }
    const bool function = callable->systemverilog->callable->function;
    if (function) {
        class_functions_lowered_ = true;
    }
    return lower_hir_class_callable_invocation(
               statement->systemverilog->source, profile, receiver, function)
        .has_value();
}

std::optional<Lowerer::HirClassCallResult>
Lowerer::lower_hir_class_callable_invocation(
    const semantic::SourceSpanId source,
    const HirClassTaskProfile& profile,
    const std::optional<RegisterId> receiver,
    const bool function)
{
    const auto declaration = specialized_hir_unit_->find_declaration(
        profile.declaration);
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->callable
        || !declaration->systemverilog->nested_scope) {
        return std::nullopt;
    }
    const auto& callable = *declaration->systemverilog;
    const auto scope = *callable.nested_scope;
    const auto formals = std::vector<semantic::DeclarationId> {
        callable.callable->formals.begin(),
        callable.callable->formals.end(),
    };
    if (formals.size() != profile.actuals.size()) {
        return std::nullopt;
    }

    auto frame_index = hir_callable_frames_.size();
    bool new_frame { true };
    if (profile.interface_receiver) {
        const auto key = std::to_string(profile.declaration.value())
            + ":" + *profile.interface_receiver;
        const auto found = hir_interface_callable_indices_.find(key);
        if (found != hir_interface_callable_indices_.end()) {
            frame_index = found->second;
            new_frame = false;
        } else {
            hir_interface_callable_indices_.emplace(key, frame_index);
        }
    } else {
        const auto callable_key = systemverilog_task_callable_key(
            profile.declaration);
        const auto key = std::to_string(callable_key);
        const auto found = hir_callable_indices_.find(key);
        if (found != hir_callable_indices_.end()) {
            frame_index = found->second;
            new_frame = false;
        } else {
            hir_callable_indices_.emplace(key, frame_index);
        }
    }
    if (new_frame) {
        HirCallableFrame frame;
        frame.declaration = profile.declaration;
        frame.scope = scope;
        frame.type = {
            1U,
            frontend::ValueDomain::Bit2,
            false,
            profile.automatic,
            false,
            std::nullopt,
        };
        if (function) {
            const auto type = hir_callable_type(profile.declaration);
            if (!type || type->container) {
                return std::nullopt;
            }
            frame.type = *type;
            frame.type.automatic = profile.automatic;
        }
        frame.interface_receiver = profile.interface_receiver;
        frame.formals = formals;
        bind_hir_instance_parameters(frame);
        frame.debug_name = callable.name;
        frame.function = function;
        if (function && frame.type.string) {
            frame.string_result = allocate_string_register();
            frame.invocation_strings.push_back(frame.string_result);
        } else if (function) {
            frame.result = allocate_register(
                frame.type.width, frame.type.domain);
            frame.invocation_registers.push_back(frame.result);
        }
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (frame.invocation_identity == 0U) {
            frame.invocation_identity
                = next_callable_invocation_identity_++;
        }
        if (receiver) {
            frame.class_receiver = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            frame.invocation_registers.push_back(
                *frame.class_receiver);
        }
        for (std::size_t index { }; index < formals.size(); ++index) {
            const auto formal = formals[index];
            const auto record = specialized_hir_unit_->find_declaration(
                formal);
            const auto direction = record
                ? callable_direction(*record)
                : frontend::PortDirection::Unknown;
            if (direction == frontend::PortDirection::Unknown) {
                return std::nullopt;
            }
            const auto string = profile.actuals[index].string;
            const auto container = profile.actuals[index].container;
            frame.argument_is_string.push_back(string);
            frame.argument_is_container.push_back(container);
            if (container) {
                const auto container_type = hir_systemverilog_container_type(
                    *record->systemverilog->type);
                if (!container_type) {
                    return std::nullopt;
                }
                frame.arguments.push_back({ });
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back(
                    allocate_container_register(*container_type));
                frame.container_output_defaults.push_back(
                    direction == frontend::PortDirection::Output
                        ? std::optional<ContainerRegisterId> {
                              allocate_container_register(*container_type) }
                        : std::nullopt);
                frame.invocation_containers.push_back(
                    frame.container_arguments.back());
            } else if (string) {
                frame.arguments.push_back({ });
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.container_arguments.push_back({ });
                frame.container_output_defaults.push_back(std::nullopt);
                frame.invocation_strings.push_back(
                    frame.string_arguments.back());
            } else {
                const auto binding = hir_runtime_binding(
                    formal, scope, false);
                if (!binding
                    || !callable_scalar_domain(binding->domain)) {
                    return std::nullopt;
                }
                frame.arguments.push_back(allocate_register(
                    binding->width, binding->domain));
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back({ });
                frame.container_output_defaults.push_back(std::nullopt);
                frame.invocation_registers.push_back(
                    frame.arguments.back());
            }
            frame.directions.push_back(direction);
        }
        frame.allocated = true;
        hir_callable_frames_.push_back(std::move(frame));
        if (!allocate_hir_static_callable_declarations(frame_index)) {
            return std::nullopt;
        }
    }

    std::vector<std::optional<RegisterId>> lowered_actuals(
        profile.actuals.size());
    std::vector<std::optional<StringRegisterId>> lowered_string_actuals(
        profile.actuals.size());
    std::vector<std::optional<ContainerRegisterId>>
        lowered_container_actuals(profile.actuals.size());
    std::vector<std::optional<HirPackedUpdateTarget>>
        packed_copy_out_targets(profile.actuals.size());
    for (std::size_t index = 0U;
        index < profile.actuals.size(); ++index) {
        const auto& actual = profile.actuals[index];
        if (actual.container) {
            const auto formal_type
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::copy_at(
                    process_.container_register_types,
                    hir_callable_frames_[frame_index]
                        .container_arguments[index]);
            const auto storage = allocate_container_register(formal_type);
            lowered_container_actuals[index] = storage;
            if (callable_copy_in(actual.direction)) {
                const auto actual_type
                    = hir_static_container_expression_type(
                        actual.expression);
                if (actual_type && !actual_type->fixed
                    && !formal_type.fixed
                    && *actual_type != formal_type
                    && !(actual.direction == frontend::PortDirection::Input
                        && compatible_hir_sequence_copy(
                            *actual_type, formal_type))) {
                    report(
                        "FSIM-ELAB-SVTASK-011",
                        "task container arguments require an exactly "
                        "compatible kind and profile",
                        hir_source_span(
                            specialized_hir_unit_
                                ->find_expression(actual.expression)
                                ->systemverilog->source));
                    continue;
                }
                const auto value = lower_hir_static_container_actual(
                    actual.expression, formal_type, formals[index]);
                if (!value) {
                    return std::nullopt;
                }
                process_.operations.emplace_back(CopyContainerRegister {
                    storage, *value });
            }
            continue;
        }
        if (!actual.string
            && class_method_copy_out(actual.direction)) {
            packed_copy_out_targets[index]
                = capture_hir_packed_update_target(
                    actual.expression);
        }
        if (!callable_copy_in(actual.direction)) {
            continue;
        }
        if (actual.string) {
            lowered_string_actuals[index]
                = lower_hir_string_expression(actual.expression);
            if (!lowered_string_actuals[index]) {
                return std::nullopt;
            }
            continue;
        }
        // A real actual of an integral inout formal converts on the way in
        // as on the way out (6.12.2).
        const bool real_inout_actual = packed_copy_out_targets[index]
            && frontend::is_real_scalar_kind(
                hir_systemverilog_scalar_kind(actual.expression));
        lowered_actuals[index] = packed_copy_out_targets[index]
                && !real_inout_actual
            ? std::optional<RegisterId> {
                  packed_copy_out_targets[index]->captured }
            : lower_hir_callable_actual(
                  actual.expression, formals[index], actual.width);
        if (!lowered_actuals[index]) {
            return std::nullopt;
        }
        if (register_width(*lowered_actuals[index]) != actual.width) {
            lowered_actuals[index] = resize_register(
                *lowered_actuals[index],
                actual.width,
                hir_actual_extension_signed(
                    actual.expression, actual.signed_value));
        }
        if (is_two_state_domain(actual.domain)) {
            lowered_actuals[index]
                = convert_to_two_state(*lowered_actuals[index]);
        }
    }

    emit_debug_point(DebugPointKind::call, hir_source_span(source));
    emit_deferred_assertion_action_handoff();
    auto& frame = hir_callable_frames_[frame_index];
    if (frame.type.automatic) {
        const auto push_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CallableFramePush {
            frame.invocation_identity,
            frame.invocation_registers,
            frame.invocation_strings,
            frame.invocation_containers,
            true,
        });
        if (!frame.snapshot_complete) {
            frame.invocation_push_sites.push_back(push_site);
        }
    }
    if (receiver) {
        if (!frame.class_receiver) {
            return std::nullopt;
        }
        process_.operations.emplace_back(CopyRegister {
            *frame.class_receiver, *receiver });
    }
    for (std::size_t index = 0U;
        index < lowered_actuals.size(); ++index) {
        if (frame.argument_is_container[index]) {
            if (frame.directions[index]
                == frontend::PortDirection::Output) {
                if (!frame.container_output_defaults[index]) {
                    return std::nullopt;
                }
                process_.operations.emplace_back(CopyContainerRegister {
                    frame.container_arguments[index],
                    *frame.container_output_defaults[index],
                });
            } else if (lowered_container_actuals[index]) {
                process_.operations.emplace_back(CopyContainerRegister {
                    frame.container_arguments[index],
                    *lowered_container_actuals[index],
                });
            } else {
                return std::nullopt;
            }
        } else if (frame.argument_is_string[index]) {
            if (lowered_string_actuals[index]) {
                process_.operations.emplace_back(CopyStringRegister {
                    frame.string_arguments[index],
                    *lowered_string_actuals[index],
                });
            } else {
                process_.operations.emplace_back(LoadStringConstant {
                    frame.string_arguments[index],
                    { },
                });
            }
        } else if (lowered_actuals[index]) {
            process_.operations.emplace_back(CopyRegister {
                frame.arguments[index], *lowered_actuals[index] });
        } else {
            process_.operations.emplace_back(LoadConstant {
                frame.arguments[index],
                callable_default_value(
                    profile.actuals[index].width,
                    profile.actuals[index].domain),
            });
        }
    }
    if (function && frame.type.string) {
        process_.operations.emplace_back(LoadStringConstant {
            frame.string_result, { } });
    } else if (function) {
        process_.operations.emplace_back(LoadConstant {
            frame.result,
            callable_default_value(frame.type.width, frame.type.domain),
        });
    }
    // The callee sees, and may change, this object's container properties.
    flush_hir_class_container_caches();
    transfer_hir_static_formals(frame, true);
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0U),
        static_cast<InstructionIndex>(call_site + 1U),
        { },
    });
    transfer_hir_static_formals(frame, false);
    if (!frame.target) {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_hir_callables_.push_back(frame_index);
    }
    std::vector<std::optional<StringRegisterId>> string_copy_outs(
        profile.actuals.size());
    std::vector<std::optional<RegisterId>> packed_copy_outs(
        profile.actuals.size());
    std::vector<RegisterId> preserve_packed;
    std::vector<ContainerRegisterId> preserve_containers;
    for (std::size_t index = 0U;
        index < profile.actuals.size(); ++index) {
        if (!class_method_copy_out(frame.directions[index])) {
            continue;
        }
        if (frame.argument_is_container[index]) {
            if (!lowered_container_actuals[index]) {
                return std::nullopt;
            }
            process_.operations.emplace_back(CopyContainerRegister {
                *lowered_container_actuals[index],
                frame.container_arguments[index],
            });
            preserve_containers.push_back(
                *lowered_container_actuals[index]);
            continue;
        }
        if (frame.argument_is_string[index]) {
            string_copy_outs[index] = allocate_string_register();
            process_.operations.emplace_back(CopyStringRegister {
                *string_copy_outs[index],
                frame.string_arguments[index],
            });
            continue;
        }
        packed_copy_outs[index] = allocate_register(
            register_width(frame.arguments[index]),
            register_domain(frame.arguments[index]));
        process_.operations.emplace_back(CopyRegister {
            *packed_copy_outs[index], frame.arguments[index] });
        preserve_packed.push_back(*packed_copy_outs[index]);
    }
    std::vector<StringRegisterId> preserve_strings;
    for (const auto copy_out : string_copy_outs) {
        if (copy_out) {
            preserve_strings.push_back(*copy_out);
        }
    }
    HirClassCallResult result;
    if (function && frame.type.string) {
        result.string = allocate_string_register();
        process_.operations.emplace_back(CopyStringRegister {
            *result.string, frame.string_result });
        preserve_strings.push_back(*result.string);
    } else if (function) {
        result.packed = allocate_register(
            frame.type.width, frame.type.domain);
        process_.operations.emplace_back(CopyRegister {
            *result.packed, frame.result });
        preserve_packed.push_back(*result.packed);
    }
    if (frame.type.automatic) {
        process_.operations.emplace_back(CallableFramePop {
            frame.invocation_identity,
            std::move(preserve_packed),
            std::move(preserve_strings),
            std::move(preserve_containers),
        });
    }
    reload_hir_class_container_caches();
    for (std::size_t index = 0U;
        index < string_copy_outs.size(); ++index) {
        if (frame.argument_is_container[index]
            && class_method_copy_out(frame.directions[index])) {
            if (!lowered_container_actuals[index]
                || !lower_hir_container_copy_out(
                    profile.actuals[index].expression,
                    *lowered_container_actuals[index])) {
                return std::nullopt;
            }
            continue;
        }
        if (!string_copy_outs[index]) {
            if (!packed_copy_outs[index]) {
                continue;
            }
            // A real formal copied out to an integral actual is converted
            // as an assignment converts it (IEEE 1800-2017 6.12.2).
            auto copied_value = *packed_copy_outs[index];
            const auto formal_kind = [&] {
                const auto formal = index < frame.formals.size()
                    ? specialized_hir_unit_->find_declaration(
                          frame.formals[index])
                    : std::nullopt;
                const auto spelling = formal && formal->systemverilog
                        && formal->systemverilog->type
                    ? std::string_view {
                          formal->systemverilog->type->target.spelling }
                    : std::string_view { };
                return spelling == "real"
                    ? frontend::SystemVerilogScalarKind::Real
                    : spelling == "shortreal"
                    ? frontend::SystemVerilogScalarKind::ShortReal
                    : spelling == "realtime"
                    ? frontend::SystemVerilogScalarKind::Realtime
                    : frontend::SystemVerilogScalarKind::None;
            }();
            const auto actual_kind = hir_systemverilog_scalar_kind(
                profile.actuals[index].expression);
            if (!frontend::is_real_scalar_kind(formal_kind)
                && frontend::is_real_scalar_kind(actual_kind)) {
                // An integral formal copied out to a real actual converts
                // as an assignment does (6.12.2).
                const auto formal = index < frame.formals.size()
                    ? specialized_hir_unit_->find_declaration(
                          frame.formals[index])
                    : std::nullopt;
                const bool formal_signed = formal && formal->systemverilog
                    && formal->systemverilog->type
                    && formal->systemverilog->type->signed_value;
                copied_value = convert_hir_integral_to_real(
                    copied_value, formal_signed, actual_kind);
            } else if (frontend::is_real_scalar_kind(formal_kind)
                && !frontend::is_real_scalar_kind(actual_kind)) {
                copied_value = convert_hir_real_to_integral(
                    copied_value, formal_kind);
                if (const auto width = hir_expression_width(
                        profile.actuals[index].expression,
                        hir_process_scope_);
                    width && *width != 0U
                    && *width != register_width(copied_value)) {
                    copied_value = resize_register(
                        copied_value, *width, true);
                }
            }
            // Output/inout copy-out is an assignment from the formal.
            // Widen using its signedness before writing the actual; the
            // actual's signedness must not change the extension (13.3.3).
            if (!frontend::is_real_scalar_kind(formal_kind)
                && !frontend::is_real_scalar_kind(actual_kind)) {
                const auto width = hir_expression_width(
                    profile.actuals[index].expression, hir_process_scope_);
                if (width && *width != 0U
                    && *width != register_width(copied_value)) {
                    const auto formal = index < frame.formals.size()
                        ? specialized_hir_unit_->find_declaration(
                              frame.formals[index])
                        : std::nullopt;
                    const bool formal_signed = formal && formal->systemverilog
                        && formal->systemverilog->type
                        && formal->systemverilog->type->signed_value;
                    copied_value = resize_register(
                        copied_value, *width, formal_signed);
                }
            }
            // A two-state actual receives x and z bits as zero (IEEE
            // 1800-2017 6.11.2).
            if (hir_expression_domain(profile.actuals[index].expression,
                    hir_process_scope_)
                == frontend::ValueDomain::Bit2) {
                copied_value = convert_to_two_state(copied_value);
            }
            const auto copied = packed_copy_out_targets[index]
                ? write_hir_packed_update_target(
                      *packed_copy_out_targets[index], copied_value)
                : lower_hir_packed_copy_out(
                      profile.actuals[index].expression, copied_value);
            if (!copied) {
                return std::nullopt;
            }
            continue;
        }
        if (!lower_hir_string_copy_out(
                profile.actuals[index].expression,
                *string_copy_outs[index])) {
            return std::nullopt;
        }
    }
    return result;
}


std::optional<semantic::DeclarationId> Lowerer::hir_actual_declaration(
    const semantic::DeclarationId formal) const
{
    return specialized_hir_unit_ != nullptr
        ? semantic::CompiledDesignResolver {
              *specialized_hir_unit_, hir_generic_binding_frames_ }
              .actual_declaration(formal)
        : std::nullopt;
}

std::optional<semantic::ExpressionId> Lowerer::hir_generic_actual(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    auto selected = expression && expression->systemverilog != nullptr
            && expression->systemverilog->kind
                == semantic::sv::ExpressionKind::name
            && expression->systemverilog->referenced_name
        ? expression->systemverilog->referenced_name->selected
        : expression && expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::name
            && expression->vhdl->referenced_name
        ? expression->vhdl->referenced_name->selected
        : std::nullopt;
    if (!selected && expression && expression->vhdl != nullptr
        && expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::name) {
        selected = semantic::CompiledDesignResolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        }.resolve_expression_name(expression_id).unique();
    }
    if (!selected) {
        return std::nullopt;
    }
    if (hir_local_registers_.contains(selected->value())
        || hir_local_string_registers_.contains(selected->value())
        || hir_local_container_registers_.contains(selected->value())) {
        return std::nullopt;
    }
    for (auto frame = hir_generic_binding_frames_.rbegin();
        frame != hir_generic_binding_frames_.rend(); ++frame) {
        const auto binding = std::ranges::find(
            *frame, *selected, &HirGenericBinding::formal);
        if (binding != frame->end() && binding->expression
            && *binding->expression != expression_id) {
            return binding->expression;
        }
    }
    const auto& actuals
        = specialized_hir_unit_->specialization().actual_identities;
    const auto actual = std::ranges::find(
        actuals, *selected,
        &semantic::SpecializedHirActualIdentity::declaration);
    if (actual != actuals.end() && actual->actual_expression
        && *actual->actual_expression != expression_id) {
        // A forwarded parameter actual can name a formal in the parent
        // specialization. That expression is not meaningful in this child
        // unit's scope, while the child's canonical actual identity already
        // contains the value selected by specialization. Preserve the child
        // formal whenever that value is available so all width, generate,
        // and process consumers observe the specialized value.
        // An unpacked array parameter is its container object.
        if (decode_hir_systemverilog_constant(actual->identity)
            || decode_hir_systemverilog_scalar_constant(
                actual->identity)
            || specialized_hir_unit_->systemverilog_array_actual(*selected)
            || specialized_hir_unit_->evaluate_integral_expression(
                expression_id)
            || specialized_hir_unit_->evaluate_string_expression(
                expression_id)) {
            return std::nullopt;
        }
        return actual->actual_expression;
    }
    return std::nullopt;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_vhdl_type_actual(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::name) {
        return std::nullopt;
    }
    if (const auto selected = hir_referenced_declaration(expression_id)) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            hir_actual_declaration(*selected).value_or(*selected));
        if (declaration && declaration->vhdl != nullptr) {
            if (declaration->vhdl->subtype) {
                return declaration->vhdl->subtype;
            }
            if (declaration->vhdl->declared_type) {
                const auto type = specialized_hir_unit_->find_type(
                    *declaration->vhdl->declared_type);
                if (type && type->vhdl != nullptr) {
                    return type->vhdl->base;
                }
            }
        }
    }

    auto name = expression->vhdl->text;
    lowercase_ascii(name);
    semantic::vhdl::SubtypeIndication result;
    result.type_mark.source = expression->vhdl->source;
    result.type_mark.spelling = name;
    if (name == "integer" || name == "natural" || name == "positive") {
        const auto width = frontend::vhdl_predefined_integer_storage_width(
            vhdl_standard_);
        result.domain = semantic::vhdl::ValueDomain::integer;
        result.executable_width = width;
        result.integer_storage_width = width;
        result.signed_value = true;
        return result;
    }
    if (name == "time" || name == "delay_length") {
        // TIME is a 64-bit physical type (IEEE 1076-2008 5.2.4.2).
        result.domain = semantic::vhdl::ValueDomain::integer;
        result.executable_width = 64U;
        result.integer_storage_width = 64U;
        result.signed_value = true;
        return result;
    }
    if (name == "real") {
        result.domain = semantic::vhdl::ValueDomain::bit2;
        result.executable_width = 64U;
        return result;
    }
    if (name == "boolean") {
        result.domain = semantic::vhdl::ValueDomain::boolean;
        result.executable_width = 1U;
        return result;
    }
    if (name == "bit") {
        result.domain = semantic::vhdl::ValueDomain::bit2;
        result.executable_width = 1U;
        return result;
    }
    if (name == "std_logic" || name == "std_ulogic") {
        result.domain = semantic::vhdl::ValueDomain::logic9;
        result.executable_width = 1U;
        return result;
    }
    if (name == "character") {
        result.domain = semantic::vhdl::ValueDomain::bit2;
        result.executable_width = 8U;
        return result;
    }
    if (name == "severity_level" || name == "file_open_kind"
        || name == "file_open_status") {
        result.domain = semantic::vhdl::ValueDomain::bit2;
        result.executable_width = 2U;
        return result;
    }
    return std::nullopt;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_effective_vhdl_subtype(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    const auto resolve = [&] {
        if (specialized_hir_unit_ == nullptr) {
            return std::optional<semantic::vhdl::SubtypeIndication> { };
        }
        auto scoped_subtype = subtype;
        const semantic::CompiledDesignResolver resolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
        constexpr auto builtin_prefix = std::string_view { "@builtin:" };
        if (!scoped_subtype.type_mark.target.valid()
            && !scoped_subtype.type_mark.spelling.empty()
            && !scoped_subtype.type_mark.spelling.starts_with(
                builtin_prefix)) {
            if (const auto named_type = resolver.resolve_vhdl_named_type(
                    scoped_subtype.type_mark.spelling, hir_process_scope_)) {
                // Restoring the visible nominal type lets the resolver derive
                // the array element domain and any constrained packed width.
                scoped_subtype.type_mark.target = *named_type;
            }
        }
        return resolver.effective_vhdl_subtype(
            scoped_subtype, hir_process_scope_);
    };
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U) {
        return resolve();
    }

    const auto scope = hir_process_scope_;
    for (const auto& entry : hir_effective_vhdl_subtype_cache_) {
        if (entry.subtype == subtype && entry.scope == scope
            && entry.binding_frames == hir_generic_binding_frames_) {
            try {
                return std::optional<semantic::vhdl::SubtypeIndication> {
                    entry.result
                };
            } catch (const std::bad_alloc&) {
                break;
            }
        }
    }

    auto result = resolve();
    if (!result) {
        return result;
    }
    if (hir_effective_vhdl_subtype_cache_.size()
            >= maximum_effective_subtype_cache_entries
        || hir_effective_vhdl_subtype_cache_bytes_
                >= maximum_effective_subtype_cache_bytes) {
        return result;
    }
    auto retained_bytes = sizeof(HirEffectiveVhdlSubtypeCacheEntry);
    const auto entry_limit = maximum_effective_subtype_cache_entry_bytes;
    if (!add_effective_subtype_cache_vhdl_type(
            retained_bytes, subtype, entry_limit)
        || !add_effective_subtype_cache_bindings(
            retained_bytes, hir_generic_binding_frames_, entry_limit)
        || !add_effective_subtype_cache_vhdl_type(
            retained_bytes, *result, entry_limit)
        || !cache_bytes_fit(hir_effective_vhdl_subtype_cache_bytes_,
            retained_bytes, maximum_effective_subtype_cache_bytes)) {
        return result;
    }
    try {
        hir_effective_vhdl_subtype_cache_.push_back({
            subtype,
            scope,
            hir_generic_binding_frames_,
            *result,
        });
    } catch (const std::bad_alloc&) {
        return result;
    }
    hir_effective_vhdl_subtype_cache_bytes_ += retained_bytes;
    return result;
}

const semantic::CompiledDeclarationResolution*
Lowerer::find_hir_expression_resolution_cache(
    const semantic::ExpressionId expression) const noexcept
{
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U
        || !expression.valid()) {
        return nullptr;
    }
    const auto found = hir_expression_resolution_cache_.find(expression);
    if (found == hir_expression_resolution_cache_.end()) {
        if (profile_expression_resolution_cache()) {
            ++hir_expression_resolution_cache_misses_;
        }
        return nullptr;
    }
    const auto entry = std::ranges::find_if(
        found->second, [&](const auto& candidate) {
            return candidate.binding_frames == hir_generic_binding_frames_;
        });
    if (profile_expression_resolution_cache()) {
        if (entry == found->second.end()) {
            ++hir_expression_resolution_cache_misses_;
        } else {
            ++hir_expression_resolution_cache_hits_;
        }
    }
    return entry == found->second.end() ? nullptr : &entry->resolution;
}

void Lowerer::cache_hir_expression_resolution(
    const semantic::ExpressionId expression,
    const semantic::CompiledDeclarationResolution& resolution) const noexcept
{
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U
        || !expression.valid()) {
        return;
    }
    if (hir_expression_resolution_cache_entries_
            >= maximum_expression_resolution_cache_entries
        || hir_expression_resolution_cache_bytes_
            >= maximum_effective_subtype_cache_bytes) {
        if (profile_expression_resolution_cache()) {
            ++hir_expression_resolution_cache_cap_drops_;
        }
        return;
    }

    auto retained_bytes
        = sizeof(semantic::ExpressionId)
        + sizeof(HirExpressionResolutionCacheEntry)
        + sizeof(std::vector<HirExpressionResolutionCacheEntry>)
        + 4U * sizeof(void*);
    const auto entry_limit = maximum_effective_subtype_cache_entry_bytes;
    if (!add_effective_subtype_cache_bindings(
            retained_bytes, hir_generic_binding_frames_, entry_limit)
        || !add_effective_subtype_cache_bytes(
            retained_bytes, sizeof(resolution), entry_limit)
        || !add_effective_subtype_cache_sequence(
            retained_bytes,
            resolution.candidates.size(),
            sizeof(semantic::DeclarationId),
            entry_limit)
        || !cache_bytes_fit(hir_expression_resolution_cache_bytes_,
            retained_bytes, maximum_effective_subtype_cache_bytes)) {
        if (profile_expression_resolution_cache()) {
            ++hir_expression_resolution_cache_cap_drops_;
        }
        return;
    }

    auto found = hir_expression_resolution_cache_.end();
    bool inserted { };
    try {
        const auto result
            = hir_expression_resolution_cache_.try_emplace(expression);
        found = result.first;
        inserted = result.second;
        found->second.push_back({ hir_generic_binding_frames_, resolution });
    } catch (const std::bad_alloc&) {
        if (inserted && found != hir_expression_resolution_cache_.end()
            && found->second.empty()) {
            hir_expression_resolution_cache_.erase(found);
        }
        if (profile_expression_resolution_cache()) {
            ++hir_expression_resolution_cache_cap_drops_;
        }
        return;
    }
    ++hir_expression_resolution_cache_entries_;
    hir_expression_resolution_cache_bytes_ += retained_bytes;
}

void Lowerer::begin_hir_effective_vhdl_subtype_cache_scope() const noexcept
{
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U) {
        clear_hir_effective_vhdl_subtype_cache();
        clear_hir_expression_resolution_cache();
        clear_hir_vhdl_subtype_name_cache();
        clear_hir_pure_integral_attempt_cache();
    }
    ++hir_effective_vhdl_subtype_cache_depth_;
}

void Lowerer::end_hir_effective_vhdl_subtype_cache_scope() const noexcept
{
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U) {
        return;
    }
    --hir_effective_vhdl_subtype_cache_depth_;
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U) {
        if (profile_expression_resolution_cache()
            && (hir_expression_resolution_cache_hits_ != 0U
                || hir_expression_resolution_cache_misses_ != 0U)) {
            std::cerr << "fsim-profile: hir-expression-resolution-cache"
                      << " hits=" << hir_expression_resolution_cache_hits_
                      << " misses=" << hir_expression_resolution_cache_misses_
                      << " cap_drops="
                      << hir_expression_resolution_cache_cap_drops_
                      << " entries="
                      << hir_expression_resolution_cache_entries_
                      << " bytes="
                      << hir_expression_resolution_cache_bytes_ << '\n';
        }
        if (profile_expression_resolution_cache()
            && (hir_vhdl_subtype_name_cache_hits_ != 0U
                || hir_vhdl_subtype_name_cache_misses_ != 0U)) {
            std::cerr << "fsim-profile: hir-vhdl-subtype-name-cache"
                      << " hits=" << hir_vhdl_subtype_name_cache_hits_
                      << " misses=" << hir_vhdl_subtype_name_cache_misses_
                      << " cap_drops="
                      << hir_vhdl_subtype_name_cache_cap_drops_
                      << " entries="
                      << hir_vhdl_subtype_name_cache_entries_
                      << " bytes=" << hir_vhdl_subtype_name_cache_bytes_
                      << '\n';
        }
        if (profile_expression_resolution_cache()
            && (hir_pure_integral_attempt_hits_ != 0U
                || hir_pure_integral_attempt_misses_ != 0U)) {
            std::cerr << "fsim-profile: hir-pure-integral-attempt-cache"
                      << " hits=" << hir_pure_integral_attempt_hits_
                      << " misses=" << hir_pure_integral_attempt_misses_
                      << " cap_drops="
                      << hir_pure_integral_attempt_cap_drops_
                      << " entries="
                      << hir_pure_integral_attempt_cache_.size() << '\n';
        }
        clear_hir_effective_vhdl_subtype_cache();
        clear_hir_expression_resolution_cache();
        clear_hir_vhdl_subtype_name_cache();
        clear_hir_pure_integral_attempt_cache();
    }
}

std::optional<std::int64_t> Lowerer::hir_pure_integral_attempt(
    const semantic::ExpressionId expression) const
{
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U) {
        return specialized_hir_unit_->evaluate_integral_expression(expression);
    }
    const auto found = hir_pure_integral_attempt_cache_.find(expression);
    if (found != hir_pure_integral_attempt_cache_.end()) {
        if (profile_expression_resolution_cache()) {
            ++hir_pure_integral_attempt_hits_;
        }
        return found->second;
    }
    if (profile_expression_resolution_cache()) {
        ++hir_pure_integral_attempt_misses_;
    }
    const auto result
        = specialized_hir_unit_->evaluate_integral_expression(expression);
    if (hir_pure_integral_attempt_cache_.size()
        >= maximum_pure_integral_attempt_cache_entries) {
        if (profile_expression_resolution_cache()) {
            ++hir_pure_integral_attempt_cap_drops_;
        }
        return result;
    }
    try {
        hir_pure_integral_attempt_cache_.emplace(expression, result);
    } catch (const std::bad_alloc&) {
        if (profile_expression_resolution_cache()) {
            ++hir_pure_integral_attempt_cap_drops_;
        }
    }
    return result;
}

void Lowerer::clear_hir_pure_integral_attempt_cache() const noexcept
{
    hir_pure_integral_attempt_cache_.clear();
    hir_pure_integral_attempt_hits_ = 0U;
    hir_pure_integral_attempt_misses_ = 0U;
    hir_pure_integral_attempt_cap_drops_ = 0U;
}

void Lowerer::clear_hir_effective_vhdl_subtype_cache() const noexcept
{
    hir_effective_vhdl_subtype_cache_.clear();
    hir_effective_vhdl_subtype_cache_bytes_ = 0U;
}

void Lowerer::clear_hir_expression_resolution_cache() const noexcept
{
    hir_expression_resolution_cache_.clear();
    hir_expression_resolution_cache_entries_ = 0U;
    hir_expression_resolution_cache_bytes_ = 0U;
    hir_expression_resolution_cache_hits_ = 0U;
    hir_expression_resolution_cache_misses_ = 0U;
    hir_expression_resolution_cache_cap_drops_ = 0U;
}

const std::optional<semantic::TypeId>*
Lowerer::find_hir_vhdl_subtype_name_cache(
    const semantic::ScopeId scope,
    const std::string_view spelling) const noexcept
{
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U) {
        return nullptr;
    }
    const auto scope_entry
        = hir_vhdl_subtype_name_cache_.find(scope.value());
    if (scope_entry == hir_vhdl_subtype_name_cache_.end()) {
        if (profile_expression_resolution_cache()) {
            ++hir_vhdl_subtype_name_cache_misses_;
        }
        return nullptr;
    }
    const auto name_entry = scope_entry->second.find(spelling);
    if (name_entry == scope_entry->second.end()) {
        if (profile_expression_resolution_cache()) {
            ++hir_vhdl_subtype_name_cache_misses_;
        }
        return nullptr;
    }
    if (profile_expression_resolution_cache()) {
        ++hir_vhdl_subtype_name_cache_hits_;
    }
    return &name_entry->second;
}

void Lowerer::cache_hir_vhdl_subtype_name(
    const semantic::ScopeId scope,
    const std::string_view spelling,
    const std::optional<semantic::TypeId> result) const noexcept
{
    if (hir_effective_vhdl_subtype_cache_depth_ == 0U) {
        return;
    }
    const auto retained_bytes
        = sizeof(std::uint32_t) + sizeof(std::string) + spelling.size()
        + sizeof(std::optional<semantic::TypeId>) + 4U * sizeof(void*);
    if (hir_vhdl_subtype_name_cache_entries_
            >= maximum_vhdl_subtype_name_cache_entries
        || spelling.size() > maximum_vhdl_subtype_name_cache_entry_bytes
        || retained_bytes > maximum_vhdl_subtype_name_cache_entry_bytes
        || !cache_bytes_fit(hir_vhdl_subtype_name_cache_bytes_,
            retained_bytes, maximum_vhdl_subtype_name_cache_bytes)) {
        if (profile_expression_resolution_cache()) {
            ++hir_vhdl_subtype_name_cache_cap_drops_;
        }
        return;
    }

    auto scope_entry = hir_vhdl_subtype_name_cache_.end();
    bool inserted_scope { };
    try {
        const auto scope_result
            = hir_vhdl_subtype_name_cache_.try_emplace(scope.value());
        scope_entry = scope_result.first;
        inserted_scope = scope_result.second;
        const auto name_result = scope_entry->second.try_emplace(
            std::string { spelling }, result);
        if (!name_result.second) {
            return;
        }
    } catch (const std::bad_alloc&) {
        if (inserted_scope
            && scope_entry != hir_vhdl_subtype_name_cache_.end()
            && scope_entry->second.empty()) {
            hir_vhdl_subtype_name_cache_.erase(scope_entry);
        }
        if (profile_expression_resolution_cache()) {
            ++hir_vhdl_subtype_name_cache_cap_drops_;
        }
        return;
    }
    ++hir_vhdl_subtype_name_cache_entries_;
    hir_vhdl_subtype_name_cache_bytes_ += retained_bytes;
}

void Lowerer::clear_hir_vhdl_subtype_name_cache() const noexcept
{
    hir_vhdl_subtype_name_cache_.clear();
    hir_vhdl_subtype_name_cache_bytes_ = 0U;
    hir_vhdl_subtype_name_cache_entries_ = 0U;
    hir_vhdl_subtype_name_cache_hits_ = 0U;
    hir_vhdl_subtype_name_cache_misses_ = 0U;
    hir_vhdl_subtype_name_cache_cap_drops_ = 0U;
}

std::vector<semantic::DeclarationId>
Lowerer::hir_vhdl_package_member_candidates(
    const semantic::vhdl::Name& name,
    const semantic::ScopeId use_scope) const
{
    std::vector<semantic::DeclarationId> result;
    if (specialized_hir_unit_ == nullptr) {
        return result;
    }
    const auto resolution = semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_ }
                                .resolve_vhdl_package_members(
                                    name, use_scope);
    result.reserve(resolution.candidates.size());
    for (const auto& candidate : resolution.candidates) {
        result.push_back(candidate.member);
    }
    std::ranges::sort(result);
    const auto duplicate = std::ranges::unique(result);
    result.erase(duplicate.begin(), duplicate.end());
    return result;
}

std::vector<semantic::DeclarationId>
Lowerer::hir_vhdl_callable_candidates(
    const semantic::vhdl::Name& name,
    const semantic::ScopeId use_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return vhdl_callable_candidates(name);
    }
    return semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_ }
        .resolve_vhdl_callable_candidates(name, use_scope)
        .candidates;
}

std::vector<Lowerer::HirCallableResolution>
Lowerer::hir_vhdl_callable_resolutions(
    const semantic::vhdl::Name& name,
    const semantic::ScopeId use_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return { };
    }
    return semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_ }
        .resolve_vhdl_callables(name, use_scope)
        .candidates;
}

std::optional<Lowerer::HirCallableResolution>
Lowerer::hir_callable_resolution(
    const semantic::DeclarationId declaration_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (declaration && declaration->systemverilog != nullptr) {
        return HirCallableResolution { declaration_id, declaration_id,
            std::nullopt, { } };
    }
    return semantic::CompiledDesignResolver {
        *specialized_hir_unit_, hir_generic_binding_frames_ }
        .resolve_vhdl_callable(declaration_id)
        .unique();
}

std::optional<std::pair<semantic::DeclarationId, std::string>>
Lowerer::hir_instance_callable(const std::string_view receiver,
    const std::string_view member, const bool function) const
{
    if (specialized_hir_unit_ == nullptr || receiver.empty()) {
        return std::nullopt;
    }
    const auto& scopes_view = specialized_hir_unit_->design().semantics.scopes();
    const auto member_of_scope = [&](const auto& scope_matches)
        -> std::optional<semantic::DeclarationId> {
        std::optional<semantic::DeclarationId> selected;
        for (const auto& declaration :
            specialized_hir_unit_->design().systemverilog_hir.declarations()) {
            if (declaration.name != member || !declaration.callable
                || declaration.callable->function != function
                || !declaration.nested_scope
                || (declaration.form != semantic::sv::DeclarationForm::task
                    && declaration.form != semantic::sv::DeclarationForm::function)
                || !declaration.scope.valid()
                || declaration.scope.value() >= scopes_view.size()
                || !scope_matches(scopes_view[declaration.scope.value()])) {
                continue;
            }
            if (selected) {
                return std::nullopt;
            }
            selected = declaration.id;
        }
        return selected;
    };
    // A task of a generate block of this module (`gen.t`) is local; its
    // variables are this instance's `gen.x` signals.
    if (receiver.find('.') == std::string_view::npos) {
        const auto unit = specialized_hir_unit_->unit();
        if (const auto local = member_of_scope([&](const semantic::Scope& scope) {
                return scope.unit == unit && scope.parent && scope.name == receiver;
            })) {
            return std::pair { *local, std::string { receiver } };
        }
    }
    // The receiver names a module instance, searched upward from the
    // calling scope (IEEE 1800-2017 23.8).
    const SpecializationInfo* target = nullptr;
    std::string lexical = hierarchy_;
    for (;;) {
        const auto candidate = lexical.empty()
            ? std::string { receiver }
            : lexical + "." + std::string { receiver };
        const auto found = std::ranges::find_if(design_.specializations_,
            [&](const SpecializationInfo& specialization) {
                return specialization.instance == candidate;
            });
        if (found != design_.specializations_.end()) {
            target = &*found;
            break;
        }
        if (lexical.empty()) {
            break;
        }
        const auto separator = lexical.rfind('.');
        lexical = separator == std::string::npos ? std::string { }
                                                 : lexical.substr(0U, separator);
    }
    const auto& design = specialized_hir_unit_->design();
    if (target == nullptr && receiver.find('.') == std::string_view::npos) {
        // An upward name may begin with the module name of an enclosing
        // instance (IEEE 1800-2017 23.8): `ma.f()` inside an instance below
        // an instance of module `ma`.
        for (std::string ancestor = hierarchy_; !ancestor.empty();) {
            const auto found = std::ranges::find_if(design_.specializations_,
                [&](const SpecializationInfo& specialization) {
                    if (specialization.instance != ancestor
                        || !specialization.source_unit) {
                        return false;
                    }
                    const auto& units = design.systemverilog_hir.units();
                    const auto unit = std::ranges::find(units,
                        *specialization.source_unit, &semantic::sv::Unit::id);
                    return unit != units.end() && unit->name == receiver;
                });
            if (found != design_.specializations_.end()) {
                target = &*found;
                break;
            }
            const auto separator = ancestor.rfind('.');
            ancestor = separator == std::string::npos
                ? std::string { }
                : ancestor.substr(0U, separator);
        }
    }
    std::optional<semantic::UnitId> target_unit;
    std::string target_path;
    if (target != nullptr && target->source_unit) {
        target_unit = target->source_unit;
        target_path = target->instance;
    } else if (std::ranges::find(design_.roots_, receiver) != design_.roots_.end()) {
        // Another top-level module, elaborated as its own root.
        for (const auto& unit : design.systemverilog_hir.units()) {
            if (unit.name == receiver) {
                target_unit = unit.id;
                target_path = std::string { receiver };
                break;
            }
        }
    }
    if (!target_unit) {
        // The instance may be elaborated after the calling process; the
        // process is retried then.
        hierarchical_reference_missed_ = true;
        return std::nullopt;
    }
    const auto& scopes = design.semantics.scopes();
    std::optional<semantic::DeclarationId> selected;
    for (const auto& declaration : design.systemverilog_hir.declarations()) {
        if (declaration.name != member || !declaration.callable
            || declaration.callable->function != function
            || !declaration.nested_scope
            || (declaration.form != semantic::sv::DeclarationForm::task
                && declaration.form != semantic::sv::DeclarationForm::function)
            || !declaration.scope.valid()
            || declaration.scope.value() >= scopes.size()) {
            continue;
        }
        const auto& scope = scopes[declaration.scope.value()];
        if (scope.unit != *target_unit || scope.parent) {
            continue;
        }
        if (selected) {
            return std::nullopt;
        }
        selected = declaration.id;
    }
    if (!selected) {
        return std::nullopt;
    }
    return std::pair { *selected, "@instance:" + target_path };
}

std::optional<Lowerer::HirCallableType> Lowerer::hir_callable_type(
    const semantic::DeclarationId declaration_id,
    const std::size_t contextual_width) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!declaration) {
        return std::nullopt;
    }
    HirCallableType result;
    if (declaration->systemverilog != nullptr) {
        const auto& source = *declaration->systemverilog;
        if (source.form != semantic::sv::DeclarationForm::function
            || !source.callable || !source.callable->function
            || !source.nested_scope || !source.type) {
            return std::nullopt;
        }
        result.automatic = source.callable->lifetime
            == semantic::sv::Lifetime::automatic;
        result.string = source.type->value_form
            == semantic::sv::TypeForm::string;
        if (result.string) {
            result.domain = frontend::ValueDomain::String;
            return result;
        }
        result.container = hir_systemverilog_container_type(*source.type);
        if (result.container) {
            return result;
        }
        if (source.type->target.spelling == "void") {
            // A void function has no result; its calls discard a one-bit
            // placeholder (IEEE 1800-2017 13.4.1).
            result.width = 1U;
            result.domain = frontend::ValueDomain::Bit2;
            return result;
        }
        const auto width = hir_systemverilog_type_width(*source.type);
        if (!width) {
            return std::nullopt;
        }
        result.width = *width;
        result.domain = source.type->four_state
            ? frontend::ValueDomain::Logic4
            : frontend::ValueDomain::Bit2;
        result.signed_value = source.type->signed_value;
    } else {
        const auto& source = *declaration->vhdl;
        if (source.form != semantic::vhdl::DeclarationForm::function
            || !source.callable || !source.callable->function
            || !source.callable->defined || !source.nested_scope
            || (!source.subtype && !source.callable->return_type)) {
            return std::nullopt;
        }
        // VHDL subprogram locals have per-call activation lifetime.
        result.automatic = true;
        const auto return_subtype = source.callable->return_type
            ? source.callable->return_type
            : source.subtype;
        if (!return_subtype) {
            return std::nullopt;
        }
        const semantic::CompiledDesignResolver resolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
        auto subtype = resolver.effective_vhdl_subtype(
            *return_subtype, source.scope);
        if (!subtype) {
            return std::nullopt;
        }
        if (subtype->domain == semantic::vhdl::ValueDomain::string
            && hir_vhdl_predefined_string_type(*subtype)) {
            result.string = true;
            result.domain = frontend::ValueDomain::String;
            return result;
        }
        const auto definition = subtype->type_mark.target.valid()
            ? specialized_hir_unit_->find_type(
                  subtype->type_mark.target)
            : std::nullopt;
        if (definition && definition->vhdl != nullptr) {
            if (!subtype->executable_width
                || *subtype->executable_width == 0U) {
                if (definition->vhdl->form
                    == semantic::vhdl::TypeForm::access) {
                    subtype->executable_width = 32U;
                } else if (definition->vhdl->form
                    == semantic::vhdl::TypeForm::physical) {
                    subtype->executable_width
                        = vhdl_standard_
                                == frontend::VhdlStandard::Vhdl2019
                        ? 64U
                        : 32U;
                }
            }
            if (subtype->domain
                == semantic::vhdl::ValueDomain::unknown) {
                if (definition->vhdl->form
                    == semantic::vhdl::TypeForm::access) {
                    subtype->domain
                        = semantic::vhdl::ValueDomain::bit2;
                } else if (definition->vhdl->form
                    == semantic::vhdl::TypeForm::physical) {
                    subtype->domain
                        = semantic::vhdl::ValueDomain::integer;
                }
            }
        }
        const auto width = subtype->unconstrained && contextual_width != 0U
            ? std::optional<std::uint64_t> { contextual_width }
            : subtype->executable_width
            ? subtype->executable_width
            : subtype->domain
                    == semantic::vhdl::ValueDomain::integer
            ? std::optional<std::uint64_t> {
                  subtype->integer_storage_width != 0U
                      ? subtype->integer_storage_width
                      : frontend::vhdl_predefined_integer_storage_width(
                            vhdl_standard_)
              }
            : std::nullopt;
        if (!width || (subtype->unconstrained && contextual_width == 0U)) {
            return std::nullopt;
        }
        result.width = static_cast<std::size_t>(*width);
        result.domain = callable_vhdl_domain(subtype->domain);
        result.signed_value = subtype->signed_value
            || result.domain == frontend::ValueDomain::Integer;
        // An array or record result of wide elements is a packed value.
        const bool composite = [&] {
            auto type_id = subtype->type_mark.target;
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid() && visited.insert(type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    return false;
                }
                if (type->vhdl->form == semantic::vhdl::TypeForm::array
                    || type->vhdl->form == semantic::vhdl::TypeForm::record) {
                    return true;
                }
                if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
                    return false;
                }
                type_id = type->vhdl->base.type_mark.target;
            }
            return false;
        }();
        if (composite
            && (!callable_scalar_domain(result.domain)
                || (result.domain == frontend::ValueDomain::Integer
                    && result.width != 32U && result.width != 64U))) {
            result.domain = frontend::ValueDomain::Bit2;
            result.signed_value = false;
        }
    }
    if (result.width == 0U
        || result.width > std::numeric_limits<std::uint32_t>::max()
        || (result.domain == frontend::ValueDomain::Integer
            && result.width != 32U && result.width != 64U)
        || !callable_scalar_domain(result.domain)) {
        return std::nullopt;
    }
    return result;
}

std::optional<Lowerer::HirRuntimeBinding>
Lowerer::hir_callable_formal_binding(
    const semantic::DeclarationId formal,
    const semantic::ScopeId callable_scope,
    const semantic::ExpressionId actual,
    const semantic::ScopeId actual_scope) const
{
    const auto existing = hir_runtime_binding(
        formal, callable_scope, false);
    if (specialized_hir_unit_ == nullptr) {
        return existing;
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        formal);
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->subtype) {
        return existing;
    }
    const auto subtype = hir_effective_vhdl_subtype(
        *declaration->vhdl->subtype);
    if (!subtype) {
        return std::nullopt;
    }
    const auto unspecified = subtype->unspecified_class
        != semantic::vhdl::UnspecifiedTypeClass::none;
    const auto contextual = unspecified || subtype->unconstrained;
    if (existing && !contextual) {
        return existing;
    }
    const auto actual_expression = specialized_hir_unit_->find_expression(
        actual);
    const bool contextual_string_literal = contextual
        && actual_expression && actual_expression->vhdl != nullptr
        && actual_expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::string_literal;
    auto width = contextual
        ? contextual_string_literal
            ? hir_expression_width(actual, actual_scope, &*subtype)
            : hir_expression_width(actual, actual_scope)
        : subtype->executable_width
        ? std::optional<std::size_t> {
              static_cast<std::size_t>(*subtype->executable_width)
          }
        : subtype->domain == semantic::vhdl::ValueDomain::integer
        ? std::optional<std::size_t> {
              subtype->integer_storage_width != 0U
                  ? subtype->integer_storage_width
                  : frontend::vhdl_predefined_integer_storage_width(
                        vhdl_standard_)
          }
        : std::nullopt;
    // An unconstrained array formal of wide elements (INTEGER, REAL,
    // records) holds the actual's packed value.
    const bool wide_element_array = subtype->unconstrained && !unspecified
        && [&] {
               auto type_id = subtype->type_mark.target;
               std::unordered_set<std::uint32_t> visited;
               while (type_id.valid() && visited.insert(type_id.value()).second) {
                   const auto type = specialized_hir_unit_->find_type(type_id);
                   if (!type || type->vhdl == nullptr) {
                       return false;
                   }
                   if (type->vhdl->form == semantic::vhdl::TypeForm::array) {
                       return type->vhdl->element_subtype.has_value();
                   }
                   if (type->vhdl->form != semantic::vhdl::TypeForm::subtype
                       && type->vhdl->form != semantic::vhdl::TypeForm::alias) {
                       return false;
                   }
                   type_id = type->vhdl->base.type_mark.target;
               }
               return false;
           }();
    auto domain = unspecified
        ? hir_expression_domain(actual, actual_scope)
        : std::optional {
              callable_vhdl_domain(subtype->domain)
          };
    if (wide_element_array && domain
        && (*domain == frontend::ValueDomain::Integer || !callable_scalar_domain(*domain))) {
        const auto actual_domain = hir_expression_domain(actual, actual_scope);
        domain = actual_domain && callable_scalar_domain(*actual_domain)
                && *actual_domain != frontend::ValueDomain::Integer
            ? actual_domain
            : std::optional { frontend::ValueDomain::Bit2 };
    }
    auto function_result_signed = std::optional<bool> { };
    const bool parameterless_function_name = contextual
        && actual_expression && actual_expression->vhdl != nullptr
        && actual_expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::name
        && actual_expression->vhdl->operands.empty()
        && hir_vhdl_function_name(actual);
    if (parameterless_function_name
        && (!width || *width == 0U || !domain
            || !callable_scalar_domain(*domain))) {
        // Resolve the actual with its complete VHDL profile. Width-only
        // syntax inference would choose the wrong overload for result-only
        // overload sets, so unresolved or ambiguous names remain unmatched.
        const auto selected_call = resolve_hir_vhdl_function_call(
            actual, actual_scope, 0U);
        if (selected_call) {
            hir_generic_binding_frames_.push_back(
                selected_call->generic_bindings);
            const PopBackGuard selected_call_scope {
                hir_generic_binding_frames_
            };
            auto selected_type = hir_callable_type(
                selected_call->body, width.value_or(0U));
            if ((!selected_type || selected_type->width == 0U)
                && selected_call->generic_bindings.empty()) {
                // An unconstrained array result has no contextual width at
                // this call site. For a parameterless function with one
                // direct return and no locals, derive only its static result
                // shape from that expression under the declared return
                // subtype; the function body remains executable normally.
                const auto function
                    = specialized_hir_unit_->find_declaration(
                        selected_call->body);
                if (function && function->vhdl != nullptr
                    && function->vhdl->callable
                    && function->vhdl->callable->formals.empty()
                    && function->vhdl->children.empty()
                    && function->vhdl->statements.size() == 1U
                    && function->vhdl->nested_scope) {
                    const auto& profile = function->vhdl->callable;
                    const auto declared_return = profile->return_type
                        ? profile->return_type : function->vhdl->subtype;
                    const semantic::CompiledDesignResolver return_resolver {
                        *specialized_hir_unit_, hir_generic_binding_frames_
                    };
                    const auto return_subtype = declared_return
                        ? return_resolver.effective_vhdl_subtype(
                              *declared_return, function->vhdl->scope)
                        : std::nullopt;
                    const auto statement
                        = specialized_hir_unit_->find_statement(
                            function->vhdl->statements.front());
                    if (return_subtype && return_subtype->unconstrained
                        && !hir_vhdl_predefined_string_type(*return_subtype)
                        && hir_vhdl_array_type_family(*return_subtype)
                        && statement && statement->vhdl != nullptr
                        && statement->vhdl->kind
                            == semantic::vhdl::StatementKind::return_statement
                        && statement->vhdl->value) {
                        const auto result_width = hir_expression_width(
                            *statement->vhdl->value,
                            *function->vhdl->nested_scope,
                            &*return_subtype);
                        if (result_width && *result_width != 0U) {
                            selected_type = hir_callable_type(
                                selected_call->body, *result_width);
                        }
                    }
                }
            }
            if ((!selected_type || selected_type->width == 0U)
                && selected_call->generic_bindings.empty()) {
                const auto value
                    = specialized_hir_unit_->evaluate_vhdl_constant_expression(
                        actual);
                const auto* packed = value
                        ? std::get_if<
                              semantic::SpecializedHirVhdlPackedValue>(
                                  &*value)
                        : nullptr;
                if (packed != nullptr && !packed->bits.empty()) {
                    selected_type = hir_callable_type(
                        selected_call->body, packed->bits.size());
                }
            }
            if (selected_type && selected_type->width != 0U) {
                if (!width || *width == 0U) {
                    width = selected_type->width;
                }
                if (!domain || !callable_scalar_domain(*domain)) {
                    domain = selected_type->domain;
                }
                function_result_signed = selected_type->signed_value;
            }
        }
    }
    if (!width || *width == 0U || !domain
        || !callable_scalar_domain(*domain)
        || (*domain == frontend::ValueDomain::Integer
            && *width != 32U && *width != 64U)) {
        return std::nullopt;
    }
    HirRuntimeBinding result;
    result.declaration = formal;
    result.kind = HirRuntimeBindingKind::local;
    result.name = declaration->vhdl->name;
    result.width = *width;
    result.domain = *domain;
    result.signed_value = (function_result_signed.value_or(
                               unspecified
                                   ? hir_expression_signed(actual)
                                   : subtype->signed_value))
        || *domain == frontend::ValueDomain::Integer;
    const auto local = hir_local_registers_.find(formal.value());
    if (local != hir_local_registers_.end()) {
        result.local = local->second;
    }
    return result;
}

std::optional<std::vector<semantic::ExpressionId>>
Lowerer::bind_hir_function_actuals(
    const semantic::ExpressionId expression_id,
    const semantic::DeclarationId declaration_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!expression || !declaration) {
        return std::nullopt;
    }

    std::span<const semantic::ExpressionId> operands;
    std::span<const std::string> names;
    std::span<const semantic::DeclarationId> formals;
    bool vhdl = false;
    if (expression->systemverilog != nullptr
        && declaration->systemverilog != nullptr) {
        const auto& call = *expression->systemverilog;
        const auto& callable = *declaration->systemverilog;
        if (call.kind != semantic::sv::ExpressionKind::call
            || !callable.callable || !callable.callable->function) {
            return std::nullopt;
        }
        operands = call.operands;
        names = call.argument_names;
        formals = callable.callable->formals;
        if (call.text.starts_with('.')
            && operands.size() == formals.size() + 1U) {
            operands = operands.subspan(1U);
            if (names.size() == operands.size() + 1U) {
                names = names.subspan(1U);
            }
        }
    } else if (expression->vhdl != nullptr
        && declaration->vhdl != nullptr) {
        const auto& call = *expression->vhdl;
        const auto& callable = *declaration->vhdl;
        // A bare name calls a function without an actual parameter part.
        const auto callable_expression
            = call.kind == semantic::vhdl::ExpressionKind::call
            || call.kind == semantic::vhdl::ExpressionKind::name
            || call.kind == semantic::vhdl::ExpressionKind::index
            || call.kind == semantic::vhdl::ExpressionKind::unary
            || call.kind == semantic::vhdl::ExpressionKind::binary;
        if (!callable_expression
            || !callable.callable || !callable.callable->function) {
            return std::nullopt;
        }
        operands = call.operands;
        names = call.argument_names;
        formals = callable.callable->formals;
        vhdl = true;
    } else {
        return std::nullopt;
    }
    if (operands.size() > formals.size()
        || (!names.empty() && names.size() != operands.size())) {
        return std::nullopt;
    }

    std::vector<semantic::ExpressionId> result(formals.size());
    std::size_t positional { };
    for (std::size_t index = 0U; index < operands.size(); ++index) {
        std::size_t formal_index = positional;
        const auto name = names.empty()
            ? std::string_view { }
            : std::string_view { names[index] };
        if (name.empty()) {
            while (formal_index < result.size()
                && result[formal_index].valid()) {
                ++formal_index;
            }
            positional = formal_index;
            ++positional;
        } else {
            const auto found = std::ranges::find_if(
                formals,
                [&](const semantic::DeclarationId formal) {
                    const auto candidate
                        = specialized_hir_unit_->find_declaration(formal);
                    std::string_view candidate_name;
                    if (candidate
                        && candidate->systemverilog != nullptr) {
                        candidate_name = candidate->systemverilog->name;
                    } else if (candidate
                        && candidate->vhdl != nullptr) {
                        candidate_name = candidate->vhdl->name;
                    }
                    return same_callable_name(
                        candidate_name, name, vhdl);
                });
            if (found == formals.end()) {
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(formals.begin(), found));
        }
        if (formal_index >= result.size()
            || result[formal_index].valid()) {
            return std::nullopt;
        }
        result[formal_index] = operands[index];
    }
    for (std::size_t index { }; index < result.size(); ++index) {
        if (result[index].valid()) {
            continue;
        }
        const auto formal = specialized_hir_unit_->find_declaration(
            formals[index]);
        auto initializer = std::optional<semantic::ExpressionId> { };
        if (formal && formal->systemverilog != nullptr) {
            initializer = formal->systemverilog->initializer;
        } else if (formal && formal->vhdl != nullptr) {
            initializer = formal->vhdl->initializer;
        }
        if (!initializer) {
            return std::nullopt;
        }
        result[index] = *initializer;
    }
    return result;
}

std::optional<Lowerer::HirInterfaceFunctionProfile>
Lowerer::hir_interface_function_profile(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    if (call.kind != semantic::sv::ExpressionKind::call
        || !call.text.starts_with('.') || call.text.size() == 1U
        || call.operands.empty()) {
        return std::nullopt;
    }
    const auto receiver = specialized_hir_unit_->find_expression(
        call.operands.front());
    if (receiver && receiver->systemverilog != nullptr
        && receiver->systemverilog->kind
            != semantic::sv::ExpressionKind::name) {
        // `a.u[0].f(...)`: a function of an element of a module instance
        // array, named by its constant path (23.8).
        const auto path = hir_systemverilog_constant_path(
            call.operands.front());
        if (!path || path->find('[') == std::string::npos) {
            return std::nullopt;
        }
        if (const auto instance = hir_instance_callable(
                *path, std::string_view { call.text }.substr(1U), true)) {
            return HirInterfaceFunctionProfile {
                instance->first, instance->second };
        }
        return std::nullopt;
    }
    if (!receiver || receiver->systemverilog == nullptr
        || receiver->systemverilog->text.empty()) {
        return std::nullopt;
    }
    const auto function = [](const semantic::CompiledDeclarationView& view) {
        return view.systemverilog != nullptr
            && view.systemverilog->form
                == semantic::sv::DeclarationForm::function
            && view.systemverilog->callable
            && view.systemverilog->callable->function;
    };
    semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto declaration
        = resolver.resolve_systemverilog_interface_member(
              receiver->systemverilog->text,
              std::string_view { call.text }.substr(1U),
              process_scope, function,
              hir_generic_interface_type(receiver->systemverilog->text))
              .unique();
    if (declaration) {
        return HirInterfaceFunctionProfile {
            *declaration, receiver->systemverilog->text };
    }
    // `u.f(...)`: a function of a module instance (23.8).
    if (const auto instance = hir_instance_callable(
            receiver->systemverilog->text,
            std::string_view { call.text }.substr(1U), true)) {
        return HirInterfaceFunctionProfile { instance->first, instance->second };
    }
    return std::nullopt;
}

std::optional<std::vector<semantic::ExpressionId>>
Lowerer::bind_hir_vhdl_procedure_actuals(
    const semantic::StatementId statement_id,
    const semantic::DeclarationId declaration_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    const auto declaration = specialized_hir_unit_->find_declaration(
        declaration_id);
    if (!statement || statement->vhdl == nullptr
        || !declaration || declaration->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *declaration->vhdl;
    if (source.form != semantic::vhdl::DeclarationForm::procedure
        || !source.callable || source.callable->function
        || !source.callable->defined || !source.nested_scope) {
        return std::nullopt;
    }
    const auto& associations = statement->vhdl->procedure_arguments;
    const auto& formals = source.callable->formals;
    if (associations.size() > formals.size()) {
        return std::nullopt;
    }
    std::vector<semantic::ExpressionId> result(formals.size());
    std::size_t positional { };
    for (const auto& association : associations) {
        auto formal_index = positional;
        if (!association.formal) {
            while (formal_index < result.size()
                && result[formal_index].valid()) {
                ++formal_index;
            }
            positional = formal_index;
            ++positional;
        } else {
            const auto name = association.formal->canonical.empty()
                ? std::string_view { association.formal->spelling }
                : std::string_view { association.formal->canonical };
            const auto found = std::ranges::find_if(
                formals,
                [&](const semantic::DeclarationId formal) {
                    const auto record
                        = specialized_hir_unit_->find_declaration(formal);
                    return record && record->vhdl != nullptr
                        && same_callable_name(
                            record->vhdl->name, name, true);
                });
            if (found == formals.end()) {
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(formals.begin(), found));
        }
        if (formal_index >= result.size()
            || result[formal_index].valid()) {
            return std::nullopt;
        }
        result[formal_index] = association.actual;
    }
    for (std::size_t index { }; index < result.size(); ++index) {
        if (result[index].valid()) {
            continue;
        }
        const auto formal = specialized_hir_unit_->find_declaration(
            formals[index]);
        if (!formal || formal->vhdl == nullptr
            || !formal->vhdl->initializer) {
            return std::nullopt;
        }
        result[index] = *formal->vhdl->initializer;
    }
    return result;
}

bool Lowerer::hir_vhdl_actual_matches_formal(
    const HirCallableResolution& resolution,
    const semantic::DeclarationId formal_id,
    const semantic::ScopeId callable_scope,
    const semantic::ExpressionId actual_id,
    const semantic::ScopeId process_scope,
    HirVhdlOverloadInference& inference) const
{
    const auto base_type = [&](semantic::TypeId candidate_type) {
        std::unordered_set<std::uint32_t> visiting;
        while (candidate_type.valid()
            && visiting.insert(candidate_type.value()).second) {
            const auto definition
                = specialized_hir_unit_->find_type(candidate_type);
            if (!definition || definition->vhdl == nullptr
                || (definition->vhdl->form
                        != semantic::vhdl::TypeForm::subtype
                    && definition->vhdl->form
                        != semantic::vhdl::TypeForm::alias)
                || !definition->vhdl->base.type_mark.target.valid()) {
                break;
            }
            candidate_type = definition->vhdl->base.type_mark.target;
        }
        return candidate_type;
    };
    const auto generic_type = [&](const semantic::TypeId candidate_type) {
        const auto root = base_type(candidate_type);
        const auto definition = root.valid()
            ? specialized_hir_unit_->find_type(root)
            : std::nullopt;
        const auto generic_declaration = definition
                && definition->vhdl != nullptr
            ? specialized_hir_unit_->find_declaration(
                  definition->vhdl->declaration)
            : std::nullopt;
        return generic_declaration
            && generic_declaration->vhdl != nullptr
            && generic_declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::generic_type;
    };
    const auto formal_declaration
        = specialized_hir_unit_->find_declaration(formal_id);
    const auto formal_subtype = formal_declaration
            && formal_declaration->vhdl != nullptr
            && formal_declaration->vhdl->subtype
        ? hir_effective_vhdl_subtype(*formal_declaration->vhdl->subtype)
        : std::nullopt;
    // STD.TEXTIO.LINE is an access-to-STRING type, but it is not a STRING
    // array. Preserve its nominal access identity before applying STRING's
    // array-profile check to values such as ordinary quoted literals.
    const auto is_standard_textio_line_subtype =
        [&](const semantic::vhdl::SubtypeIndication& subtype,
            const semantic::ScopeId use_scope) {
            if (specialized_hir_unit_ == nullptr
                || subtype.domain != semantic::vhdl::ValueDomain::string) {
                return false;
            }
            const auto* access_type
                = hir_vhdl_access_type_definition(subtype);
            if (access_type != nullptr) {
                if (!same_callable_name(access_type->name, "line", true)) {
                    return false;
                }
                if (access_type->declaration.valid()) {
                    const auto type_declaration
                        = specialized_hir_unit_->find_declaration(
                            access_type->declaration);
                    if (!type_declaration
                        || type_declaration->vhdl == nullptr
                        || !type_declaration->vhdl->scope.valid()) {
                        return false;
                    }
                    const auto& scopes
                        = specialized_hir_unit_->design().semantics.scopes();
                    const auto owner_scope = std::ranges::find(
                        scopes, type_declaration->vhdl->scope,
                        &semantic::Scope::id);
                    const auto owner = owner_scope != scopes.end()
                        ? specialized_hir_unit_->design().find_unit(
                              owner_scope->unit)
                        : std::nullopt;
                    return owner && owner->identity != nullptr
                        && owner->vhdl != nullptr
                        && owner->identity->kind
                            == semantic::UnitKind::vhdl_package
                        && owner->vhdl->kind
                            == semantic::vhdl::UnitKind::package
                        && same_callable_name(
                            owner->identity->library, "std", true)
                        && same_callable_name(
                            owner->identity->name, "textio", true);
                }
            } else if (subtype.type_mark.target.valid()) {
                return false;
            }
            auto spelling = std::string_view {
                subtype.type_mark.spelling
            };
            constexpr auto builtin_prefix
                = std::string_view { "@builtin:" };
            if (spelling.starts_with(builtin_prefix)) {
                spelling.remove_prefix(builtin_prefix.size());
            }
            if (const auto separator = spelling.find_last_of(".:");
                separator != std::string_view::npos) {
                spelling.remove_prefix(separator + 1U);
            }
            return same_callable_name(spelling, "line", true)
                && semantic::CompiledDesignResolver {
                       *specialized_hir_unit_
                   }.vhdl_builtin_type_visible(
                       subtype.type_mark.spelling, use_scope);
        };
    if (formal_subtype
        && is_standard_textio_line_subtype(*formal_subtype, callable_scope)) {
        const auto actual_subtype = hir_vhdl_expression_subtype(actual_id);
        return actual_subtype
            && is_standard_textio_line_subtype(
                *actual_subtype, process_scope);
    }
    // STRING and REAL formals accept exactly the actuals of their type,
    // including literals of that type (IEEE 1076-2008 9.3.2).
    if (vhdl_string_formal(formal_id)
        || (formal_subtype
            && formal_subtype->domain == semantic::vhdl::ValueDomain::string)) {
        const auto actual_expression
            = specialized_hir_unit_->find_expression(actual_id);
        const bool contextual_string_literal = actual_expression
            && actual_expression->vhdl != nullptr
            && actual_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::string_literal;
        if (contextual_string_literal || !formal_subtype) {
            return hir_expression_is_string(actual_id, process_scope);
        }
        auto string_actual_subtype
            = hir_vhdl_expression_subtype(actual_id);
        if (!string_actual_subtype && actual_expression
            && actual_expression->vhdl != nullptr
            && actual_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::name) {
            // Plain object names can have a precise declaration subtype even
            // when the expression-subtype helper cannot recover one from HIR.
            // Use that retained type identity before applying the contextual
            // STRING-expression fallback below.
            const auto actual_declaration
                = hir_referenced_declaration(actual_id);
            const auto declaration = actual_declaration
                ? specialized_hir_unit_->find_declaration(
                      *actual_declaration)
                : std::nullopt;
            if (declaration && declaration->vhdl != nullptr) {
                if (declaration->vhdl->callable
                    && declaration->vhdl->callable->return_type) {
                    string_actual_subtype
                        = *declaration->vhdl->callable->return_type;
                } else if (declaration->vhdl->subtype) {
                    string_actual_subtype = *declaration->vhdl->subtype;
                }
            }
            if (!string_actual_subtype
                && (!actual_declaration
                    || (declaration && declaration->vhdl != nullptr
                        && declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::function))) {
                // A parameterless function name is a function call in VHDL
                // (IEEE 1076-2008 9.3.4), even though its HIR node is a name.
                // Resolve its callable profile instead of treating the
                // absent object declaration as an untyped string expression.
                if (const auto actual_call = resolve_hir_vhdl_function_call(
                        actual_id, process_scope, 0U)) {
                    hir_generic_binding_frames_.push_back(
                        actual_call->generic_bindings);
                    const PopBackGuard actual_call_scope {
                        hir_generic_binding_frames_
                    };
                    const auto function
                        = specialized_hir_unit_->find_declaration(
                            actual_call->body);
                    if (function && function->vhdl != nullptr) {
                        const auto& profile = function->vhdl->callable;
                        if (profile && profile->return_type) {
                            string_actual_subtype
                                = *profile->return_type;
                        } else if (function->vhdl->subtype) {
                            string_actual_subtype
                                = *function->vhdl->subtype;
                        }
                    }
                }
            }
        }
        if (!string_actual_subtype) {
            // Computed STRING expressions such as concatenations may have no
            // retained subtype. Their expression-level STRING proof is only
            // applicable to the predefined STRING formal; a user-defined
            // character array has a distinct nominal type.
            return hir_vhdl_predefined_string_type(*formal_subtype)
                && hir_expression_is_string(actual_id, process_scope);
        }
        const auto effective_string_actual_subtype
            = hir_effective_vhdl_subtype(*string_actual_subtype);
        if (!effective_string_actual_subtype
            || effective_string_actual_subtype->domain
                != semantic::vhdl::ValueDomain::string) {
            return false;
        }
        const bool same_nominal_string
            = formal_subtype->type_mark.target.valid()
            && effective_string_actual_subtype->type_mark.target.valid()
            && base_type(formal_subtype->type_mark.target)
                == base_type(
                    effective_string_actual_subtype->type_mark.target);
        return same_nominal_string
            || (hir_vhdl_predefined_string_type(*formal_subtype)
                && hir_vhdl_predefined_string_type(
                    *effective_string_actual_subtype));
    }
    const bool real_formal = formal_subtype
        && hir_vhdl_subtype_is_real(*formal_subtype);
    if (real_formal || hir_vhdl_expression_is_real(actual_id)) {
        return real_formal && hir_vhdl_expression_is_real(actual_id);
    }
    const auto formal = hir_callable_formal_binding(
        formal_id, callable_scope, actual_id, process_scope);
    if (!formal) {
        return false;
    }
    auto actual_width = hir_expression_width(actual_id, process_scope);
    auto actual_domain = hir_expression_domain(actual_id, process_scope);
    const auto actual = specialized_hir_unit_->find_expression(actual_id);
    const auto actual_kind = actual && actual->vhdl != nullptr
        ? std::optional { actual->vhdl->kind }
        : std::nullopt;
    const bool aggregate_actual
        = actual_kind == semantic::vhdl::ExpressionKind::aggregate;
    // Aggregates and string and character literals take their type from
    // the formal.
    const bool context_dependent_actual = aggregate_actual
        || actual_kind == semantic::vhdl::ExpressionKind::string_literal
        || actual_kind == semantic::vhdl::ExpressionKind::logic_literal;
    if (context_dependent_actual) {
        actual_width = formal->width;
        if (actual_kind
                == semantic::vhdl::ExpressionKind::string_literal
            && formal_subtype) {
            // Keep the literal's positional CHARACTER-array extent when the
            // formal is a user array of predefined CHARACTER. In particular,
            // a constrained formal must still reject a literal with the
            // wrong number of elements.
            if (const auto contextual_width = hir_expression_width(
                    actual_id, process_scope, &*formal_subtype)) {
                actual_width = *contextual_width;
            }
        }
        actual_domain = formal->domain;
    }
    // An abstract literal without a unit is not of a physical type
    // (IEEE 1076-2008 9.3.2). Folded time literals keep their nominal TIME
    // type.
    if ((actual_kind == semantic::vhdl::ExpressionKind::integer_literal
            || actual_kind == semantic::vhdl::ExpressionKind::real_literal)
        && actual->vhdl->nominal_type.empty() && formal_subtype) {
        const auto mark = lowercase_ascii_copy(
            simple_name_of(formal_subtype->type_mark.spelling));
        if (mark == "time" || mark == "delay_length") {
            return false;
        }
    }
    // A character literal belongs to a character type: BIT for '0' and
    // '1', STD_ULOGIC for its nine values; never to BOOLEAN, an integer
    // type or an array type (IEEE 1076-2008 9.3.2).
    if (actual_kind == semantic::vhdl::ExpressionKind::logic_literal
        && actual->vhdl->text.size() == 3U
        && actual->vhdl->text.front() == '\'') {
        const auto value = static_cast<char>(std::toupper(
            static_cast<unsigned char>(actual->vhdl->text[1])));
        const bool array_formal = formal_subtype
            && hir_vhdl_array_type_family(*formal_subtype);
        const bool fits = !array_formal
            && formal->domain != frontend::ValueDomain::Boolean
            && formal->domain != frontend::ValueDomain::Integer
            && (formal->width != 1U
                || (formal->domain == frontend::ValueDomain::Bit2
                        ? value == '0' || value == '1'
                        : formal->domain != frontend::ValueDomain::Logic9
                            || std::string_view { "UX01ZWLH-" }.find(value)
                                != std::string_view::npos));
        if (!fits) {
            return false;
        }
    }
    if (!actual_width || !actual_domain) {
        const auto nested = resolve_hir_vhdl_function_call(
            actual_id, process_scope, formal->width);
        if (nested) {
            hir_generic_binding_frames_.push_back(nested->generic_bindings);
            const PopBackGuard nested_scope_guard {
                hir_generic_binding_frames_
            };
            if (const auto nested_type = hir_callable_type(
                    nested->body, formal->width)) {
                actual_width = nested_type->width;
                actual_domain = nested_type->domain;
            }
        }
    }
    auto actual_subtype = hir_vhdl_expression_subtype(actual_id);
    bool actual_is_function_name { };
    if (!actual_subtype
        && actual_kind == semantic::vhdl::ExpressionKind::name) {
        const auto actual_declaration
            = hir_referenced_declaration(actual_id);
        const auto declaration = actual_declaration
            ? specialized_hir_unit_->find_declaration(
                  *actual_declaration)
            : std::nullopt;
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->callable
            && declaration->vhdl->callable->function) {
            actual_is_function_name = true;
            const auto& profile = declaration->vhdl->callable;
            if (profile->return_type) {
                actual_subtype = *profile->return_type;
            } else if (declaration->vhdl->subtype) {
                actual_subtype = *declaration->vhdl->subtype;
            }
        }
    }
    const auto effective_actual_subtype = actual_subtype
        ? hir_effective_vhdl_subtype(*actual_subtype)
        : std::nullopt;
    if (formal_subtype
        && formal_subtype->unspecified_class
            != semantic::vhdl::UnspecifiedTypeClass::none) {
        inference.uses_unspecified_type = true;
        const auto& inference_identity
            = formal_subtype->unspecified_inference_identity;
        if (!inference_identity.empty() && effective_actual_subtype) {
            const auto actual_identity
                = effective_actual_subtype->type_mark.target.valid()
                ? "type:"
                    + std::to_string(
                        base_type(effective_actual_subtype->type_mark.target)
                            .value())
                : "profile:"
                    + std::to_string(static_cast<unsigned>(
                        effective_actual_subtype->domain))
                    + ":"
                    + std::to_string(
                        effective_actual_subtype->executable_width.value_or(
                            0U));
            const auto [found, inserted] = inference.actual_types.emplace(
                inference_identity, actual_identity);
            if (!inserted && found->second != actual_identity) {
                return false;
            }
        }
    }
    if (effective_actual_subtype && !context_dependent_actual) {
        const auto effective_width = effective_actual_subtype->executable_width
            ? effective_actual_subtype->executable_width
            : effective_actual_subtype->domain
                    == semantic::vhdl::ValueDomain::integer
                && effective_actual_subtype->integer_storage_width != 0U
            ? std::optional<std::uint64_t> {
                  effective_actual_subtype->integer_storage_width
              }
            : std::nullopt;
        if (effective_width
            && *effective_width <= std::numeric_limits<std::size_t>::max()) {
            actual_width = static_cast<std::size_t>(*effective_width);
        }
        if (effective_actual_subtype->domain
            != semantic::vhdl::ValueDomain::unknown) {
            actual_domain = callable_vhdl_domain(
                effective_actual_subtype->domain);
        }
    }
    const auto same_nominal_array = formal_subtype
        && effective_actual_subtype
        && hir_vhdl_same_nominal_array_type(
            *formal_subtype, *effective_actual_subtype, process_scope);
    const auto same_nominal_type = context_dependent_actual
        || !formal_subtype || !effective_actual_subtype
        || !formal_subtype->type_mark.target.valid()
        || !effective_actual_subtype->type_mark.target.valid()
        || base_type(formal_subtype->type_mark.target)
            == base_type(effective_actual_subtype->type_mark.target)
        || same_nominal_array
        || (!resolution.generic_bindings.empty()
            && (generic_type(formal_subtype->type_mark.target)
                || generic_type(effective_actual_subtype->type_mark.target)));
    const bool unresolved_function_array_profile = actual_is_function_name
        && formal_subtype
        && hir_vhdl_array_type_family(*formal_subtype).has_value()
        && !effective_actual_subtype;
    const auto array_shapes_match = same_nominal_array
            && !formal_subtype->unconstrained
            && !formal_subtype->constraints.empty()
        ? semantic::CompiledDesignResolver {
              *specialized_hir_unit_,
              hir_generic_binding_frames_
          }.vhdl_array_shapes_match(
              *formal_subtype,
              formal_declaration->vhdl->scope,
              *effective_actual_subtype,
              actual && actual->vhdl != nullptr
                  ? actual->vhdl->scope
                  : process_scope)
        : std::optional<bool> { };
    // A record formal packs its elements; an integer element actual
    // (`l.a + r.a` inside an overload for the record) must not match it
    // through the shared integer domain.
    const auto formal_record = formal_subtype
        && formal_subtype->type_mark.target.valid()
        && [&] {
               const auto definition = specialized_hir_unit_->find_type(
                   base_type(formal_subtype->type_mark.target));
               return definition && definition->vhdl != nullptr
                   && definition->vhdl->form
                       == semantic::vhdl::TypeForm::record;
           }();
    if (!actual_width || !actual_domain || *actual_domain != formal->domain
        || !same_nominal_type
        || unresolved_function_array_profile
        || (array_shapes_match && !*array_shapes_match)
        || (formal->domain != frontend::ValueDomain::Integer
            && *actual_width != formal->width && !same_nominal_array)
        || (formal_record && !aggregate_actual
            && *actual_width != formal->width)) {
        return false;
    }
    // Array actuals must belong to the formal's array type; a scalar BIT or
    // STD_ULOGIC value is not an array.
    if (formal_subtype && !context_dependent_actual) {
        const auto formal_family = hir_vhdl_array_type_family(*formal_subtype);
        const auto actual_family = actual_subtype
            ? hir_vhdl_array_type_family(*actual_subtype)
            : std::nullopt;
        const auto scalar_name = actual_subtype
            ? std::string_view { actual_subtype->type_mark.spelling }
            : std::string_view { };
        const bool scalar_logic_actual = actual_subtype
            && actual_subtype->constraints.empty()
            && (scalar_name == "std_ulogic" || scalar_name == "std_logic"
                || scalar_name == "bit");
        if ((formal_family && actual_family && *formal_family != *actual_family)
            || (formal_family && !actual_family && scalar_logic_actual)) {
            return false;
        }
    }
    return true;
}

std::optional<Lowerer::HirCallableResolution>
Lowerer::resolve_hir_vhdl_function_call(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope,
    const std::size_t contextual_width) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto callable_expression = expression
            && expression->vhdl != nullptr
        ? expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
            || expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::name
            || expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::index
            || expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::unary
            || expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::binary
        : false;
    if (!expression || expression->vhdl == nullptr
        || !callable_expression
        || !expression->vhdl->referenced_name) {
        return std::nullopt;
    }

    auto candidates = hir_vhdl_callable_resolutions(
        *expression->vhdl->referenced_name,
        expression->vhdl->scope);
    std::optional<HirCallableResolution> checker_resolution;
    const auto& referenced_name = *expression->vhdl->referenced_name;
    if (referenced_name.selected) {
        checker_resolution = hir_callable_resolution(
            *referenced_name.selected);
    }
    if (!checker_resolution) {
        if (const auto referenced
            = hir_referenced_declaration(expression_id)) {
            checker_resolution = hir_callable_resolution(*referenced);
        }
    }
    const bool linked_package_call
        = expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
        && candidates.size() == 1U
        && candidates.front().package_instance.has_value();
    const bool checker_selected_generic_call
        = expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
        && checker_resolution
        && !checker_resolution->generic_bindings.empty();
    std::optional<HirCallableResolution> selected;
    std::optional<HirCallableResolution> contextual_fallback;
    for (const auto& resolution : candidates) {
        hir_generic_binding_frames_.push_back(
            resolution.generic_bindings);
        const PopBackGuard generic_scope { hir_generic_binding_frames_ };
        const auto declaration = specialized_hir_unit_->find_declaration(
            resolution.body);
        const auto type = hir_callable_type(
            resolution.body, contextual_width);
        const auto actuals = bind_hir_function_actuals(
            expression_id, resolution.body);
        const auto unconstrained_result = [&]() {
            if (!declaration || declaration->vhdl == nullptr) {
                return std::optional<semantic::vhdl::SubtypeIndication> { };
            }
            const auto& callable = declaration->vhdl->callable;
            if (callable && callable->return_type) {
                return hir_effective_vhdl_subtype(
                    *callable->return_type);
            }
            return declaration->vhdl->subtype
                ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
                : std::optional<semantic::vhdl::SubtypeIndication> { };
        }();
        const bool context_dependent_result = unconstrained_result
            && unconstrained_result->unconstrained
            && contextual_width == 0U;
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->callable
            || !declaration->vhdl->callable->function
            || !declaration->vhdl->nested_scope || !actuals
            || (!type && !context_dependent_result)) {
            continue;
        }

        const auto scope = *declaration->vhdl->nested_scope;
        const auto& formals = declaration->vhdl->callable->formals;
        HirVhdlOverloadInference inference;
        bool compatible = formals.size() == actuals->size();
        for (std::size_t index { };
            compatible && index < formals.size(); ++index) {
            compatible = hir_vhdl_actual_matches_formal(resolution,
                formals[index], scope, (*actuals)[index], process_scope,
                inference);
        }
        if (inference.uses_unspecified_type
            && !expression->vhdl->unspecified_type_inference_unique) {
            compatible = false;
        }
        if (!compatible) {
            continue;
        }
        auto& destination = !type || contextual_width == 0U
                || type->width == contextual_width
            ? selected
            : contextual_fallback;
        if (destination
            && (destination->key != resolution.key
                || destination->body != resolution.body)) {
            return std::nullopt;
        }
        destination = resolution;
    }
    if (selected) {
        return selected;
    }
    if (contextual_fallback) {
        return contextual_fallback;
    }
    // A unique instantiated-package resolution or checker-selected generic
    // call is authoritative after HIR relocation. Its binding frame already
    // captures specialization identity, so incomplete residual nominal
    // metadata must not turn the resolved call into an overload diagnostic.
    // Operators still require a matching profile: a sole visible user
    // overload can be inapplicable, in which case lowering must continue with
    // the predefined operator.
    if (checker_selected_generic_call) {
        return checker_resolution;
    }
    return linked_package_call
        ? std::optional { candidates.front() }
        : std::nullopt;
}

std::optional<Lowerer::HirCallableResolution>
Lowerer::resolve_hir_vhdl_procedure_call(
    const semantic::StatementId statement_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::procedure_call) {
        return std::nullopt;
    }

    auto candidates = hir_vhdl_callable_resolutions(
        statement->vhdl->procedure, statement->vhdl->scope);
    std::optional<HirCallableResolution> checker_resolution;
    if (statement->vhdl->procedure.selected) {
        checker_resolution = hir_callable_resolution(
            *statement->vhdl->procedure.selected);
    }
    const bool linked_package_call = candidates.size() == 1U
        && candidates.front().package_instance.has_value();
    const bool checker_selected_generic_call = checker_resolution
        && !checker_resolution->generic_bindings.empty();
    std::optional<HirCallableResolution> selected;
    for (const auto& resolution : candidates) {
        hir_generic_binding_frames_.push_back(
            resolution.generic_bindings);
        const PopBackGuard generic_scope { hir_generic_binding_frames_ };
        const auto declaration = specialized_hir_unit_->find_declaration(
            resolution.body);
        const auto actuals = bind_hir_vhdl_procedure_actuals(
            statement_id, resolution.body);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->callable
            || declaration->vhdl->callable->function
            || !declaration->vhdl->nested_scope || !actuals) {
            continue;
        }

        const auto scope = *declaration->vhdl->nested_scope;
        const auto& formals = declaration->vhdl->callable->formals;
        HirVhdlOverloadInference inference;
        bool compatible = formals.size() == actuals->size();
        for (std::size_t index { };
            compatible && index < formals.size(); ++index) {
            compatible = hir_vhdl_actual_matches_formal(resolution,
                formals[index], scope, (*actuals)[index], process_scope,
                inference);
        }
        if (!compatible) {
            continue;
        }
        if (selected
            && (selected->key != resolution.key
                || selected->body != resolution.body)) {
            return std::nullopt;
        }
        selected = resolution;
    }
    if (selected) {
        return selected;
    }
    if (checker_selected_generic_call) {
        return checker_resolution;
    }
    return linked_package_call
        ? std::optional { candidates.front() }
        : std::nullopt;
}

std::optional<Lowerer::HirDpiFunctionProfile>
Lowerer::hir_dpi_function_profile(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::call
        || expression->systemverilog->text.empty()) {
        return std::nullopt;
    }
    const auto unit = specialized_hir_unit_->design().find_unit(
        specialized_hir_unit_->unit());
    if (!unit || unit->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    const semantic::sv::DpiDeclaration* declaration = nullptr;
    for (const auto& candidate : unit->systemverilog->dpi_declarations) {
        if (candidate.direction != semantic::sv::DpiDirection::import
            || candidate.callable_kind
                != semantic::sv::DpiCallableKind::function
            || !candidate.validated
            || candidate.systemverilog_name != source.text) {
            continue;
        }
        if (declaration != nullptr) {
            return std::nullopt;
        }
        declaration = &candidate;
    }
    if (declaration == nullptr || declaration->linkage_name.empty()) {
        return std::nullopt;
    }
    const auto result_type = dpi_packed_type(
        declaration->return_type_tokens);
    if (!result_type || result_type->string
        || result_type->width == 0U
        || result_type->width
            > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    std::vector<std::optional<semantic::ExpressionId>> bound(
        declaration->formals.size());
    std::size_t positional { };
    const auto& associations = source.call_arguments;
    if (associations.empty()) {
        if (source.operands.size() != bound.size()) {
            return std::nullopt;
        }
        for (std::size_t index = 0U; index < bound.size(); ++index) {
            bound[index] = source.operands[index];
        }
    } else {
        for (const auto& association : associations) {
            if (!association.actual) {
                return std::nullopt;
            }
            auto index = positional;
            if (association.formal && !association.formal->empty()) {
                const auto formal = std::ranges::find(
                    declaration->formals,
                    *association.formal,
                    &semantic::sv::DpiFormal::name);
                if (formal == declaration->formals.end()) {
                    return std::nullopt;
                }
                index = static_cast<std::size_t>(std::distance(
                    declaration->formals.begin(), formal));
            } else {
                while (index < bound.size() && bound[index]) {
                    ++index;
                }
                positional = index + 1U;
            }
            if (index >= bound.size() || bound[index]) {
                return std::nullopt;
            }
            bound[index] = *association.actual;
        }
    }
    if (std::ranges::any_of(bound, [](const auto& actual) {
            return !actual.has_value();
        })) {
        return std::nullopt;
    }

    HirDpiFunctionProfile result;
    result.linkage_name = declaration->linkage_name;
    result.result_width = result_type->width;
    result.result_domain = result_type->domain;
    result.result_signed = result_type->signed_value;
    result.actuals.reserve(bound.size());
    for (std::size_t index = 0U; index < bound.size(); ++index) {
        const auto& formal = declaration->formals[index];
        if (!formal.dimension_tokens.empty()) {
            return std::nullopt;
        }
        const auto type = dpi_packed_type(formal.type_tokens);
        const auto direction = class_method_direction(formal.direction);
        if (!type || direction == frontend::PortDirection::Unknown
            || (!type->string
                && (type->width == 0U
                    || type->width
                        > std::numeric_limits<std::uint32_t>::max()))
            || (formal.const_reference
                && direction != frontend::PortDirection::Input)) {
            return std::nullopt;
        }
        result.actuals.push_back({
            *bound[index],
            formal.name,
            direction,
            type->width,
            type->domain,
            type->signed_value,
        });
        if (type->string) {
            result.actuals.back().width = 0U;
            result.actuals.back().domain = frontend::ValueDomain::String;
        }
    }
    (void)process_scope;
    return result;
}

bool Lowerer::can_lower_hir_dpi_function_call(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope,
    std::unordered_set<std::uint32_t>& visiting) const
{
    const auto profile = hir_dpi_function_profile(
        expression_id, process_scope);
    if (!profile) {
        return false;
    }
    for (const auto& actual : profile->actuals) {
        if (callable_copy_in(actual.direction)) {
            const auto lowerable = actual.domain
                    == frontend::ValueDomain::String
                ? can_lower_hir_string_expression(
                      actual.expression, process_scope)
                : can_lower_hir_expression(
                      actual.expression, process_scope, visiting);
            if (!lowerable) {
                return false;
            }
        }
        if (!class_method_copy_out(actual.direction)) {
            continue;
        }
        const auto target = hir_target_declaration(actual.expression);
        if (actual.domain == frontend::ValueDomain::String) {
            const auto binding = target
                ? hir_string_binding(*target, process_scope, false)
                : std::nullopt;
            if (!binding
                || (binding->kind != HirStringBindingKind::local
                    && (!binding->object
                        || read_only_string_objects_.contains(
                            *binding->object)))) {
                return false;
            }
            continue;
        }
        const auto binding = target
            ? hir_runtime_binding(*target, process_scope, false)
            : std::nullopt;
        if (!binding || binding->width != actual.width
            || (binding->kind != HirRuntimeBindingKind::local
                && !signal_binding_is_writable(*binding))) {
            return false;
        }
    }
    return true;
}

bool Lowerer::can_lower_hir_function_call(
    const semantic::ExpressionId expression_id,
    const semantic::ScopeId process_scope,
    std::unordered_set<std::uint32_t>& visiting,
    const bool string_result) const
{
    if (can_lower_hir_dpi_function_call(
            expression_id, process_scope, visiting)) {
        return !string_result;
    }
    const auto expression = specialized_hir_unit_
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    const auto callable = hir_referenced_declaration(expression_id);
    const auto interface_callable = !callable && expression
            && expression->systemverilog != nullptr
        ? hir_interface_function_profile(expression_id, process_scope)
        : std::nullopt;
    const auto resolution = expression && expression->vhdl != nullptr
        ? resolve_hir_vhdl_function_call(
              expression_id, process_scope, 0U)
        : callable ? hir_callable_resolution(*callable)
        : interface_callable
            ? hir_callable_resolution(interface_callable->declaration)
                   : std::nullopt;
    if (!resolution) {
        return false;
    }
    hir_generic_binding_frames_.push_back(
        resolution->generic_bindings);
    const PopBackGuard generic_scope { hir_generic_binding_frames_ };
    const auto type = hir_callable_type(resolution->body);
    const auto actuals = bind_hir_function_actuals(
        expression_id, resolution->body);
    const auto declaration = specialized_hir_unit_->find_declaration(
        resolution->body);
    if (!actuals || !declaration) {
        return false;
    }
    if (!type) {
        const auto context_dependent_result = expression->vhdl != nullptr
                && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(
                  *declaration->vhdl->subtype)
            : std::nullopt;
        if (!context_dependent_result
            || !context_dependent_result->unconstrained
            || string_result) {
            return hir_static_array_function_call(expression_id);
        }
    }
    if (type && type->string != string_result) {
        return false;
    }
    const auto scope = declaration->systemverilog != nullptr
        ? declaration->systemverilog->nested_scope
        : declaration->vhdl->nested_scope;
    const auto formals = declaration->systemverilog != nullptr
        ? std::span<const semantic::DeclarationId> {
              declaration->systemverilog->callable->formals
          }
        : std::span<const semantic::DeclarationId> {
              declaration->vhdl->callable->formals
          };
    if (!scope || formals.size() != actuals->size()) {
        return false;
    }
    for (std::size_t index = 0U; index < formals.size(); ++index) {
        const auto formal = specialized_hir_unit_->find_declaration(
            formals[index]);
        const auto direction = formal
            ? callable_direction(*formal)
            : frontend::PortDirection::Unknown;
        const auto string = [&] {
            if (!formal) {
                return false;
            }
            if (formal->systemverilog != nullptr) {
                return formal->systemverilog->type
                    && formal->systemverilog->type->value_form
                        == semantic::sv::TypeForm::string;
            }
            if (formal->vhdl == nullptr || !formal->vhdl->subtype) {
                return false;
            }
            const auto subtype = hir_effective_vhdl_subtype(
                *formal->vhdl->subtype);
            return subtype
                && subtype->domain
                    == semantic::vhdl::ValueDomain::string;
        }();
        const auto container_type = formal
                && formal->systemverilog != nullptr
                && formal->systemverilog->type
            ? hir_systemverilog_container_type(
                  *formal->systemverilog->type)
            : std::nullopt;
        if (direction == frontend::PortDirection::Unknown) {
            return false;
        }
        if (container_type) {
            const auto actual_expression
                = specialized_hir_unit_->find_expression(
                    (*actuals)[index]);
            const auto contextual_pattern = actual_expression
                && actual_expression->systemverilog != nullptr
                && actual_expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::assignment_pattern
                && callable_copy_in(direction)
                && !class_method_copy_out(direction);
            if (!actual_expression
                || actual_expression->systemverilog == nullptr
                || (!contextual_pattern
                    && !hir_static_container_expression_type(
                        (*actuals)[index]))) {
                return false;
            }
            if (actual_expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::call
                && actual_expression->systemverilog->text != "?:"
                && !can_lower_hir_function_call(
                    (*actuals)[index], process_scope, visiting)) {
                return false;
            }
            continue;
        }
        if (string) {
            if (callable_copy_in(direction)
                && !can_lower_hir_string_expression(
                    (*actuals)[index], process_scope)) {
                return false;
            }
            if (!class_method_copy_out(direction)) {
                continue;
            }
            const auto target = hir_target_declaration(
                (*actuals)[index]);
            const auto target_binding = target
                ? hir_string_binding(*target, process_scope, false)
                : std::nullopt;
            if (!target_binding
                || (target_binding->kind
                        != HirStringBindingKind::local
                    && (!target_binding->object
                        || read_only_string_objects_.contains(
                            *target_binding->object)))) {
                return false;
            }
            continue;
        }
        const auto binding = hir_callable_formal_binding(
            formals[index], *scope, (*actuals)[index], process_scope);
        if (!binding
            || !callable_scalar_domain(binding->domain)) {
            return false;
        }
        if (callable_copy_in(direction)
            && !can_lower_hir_expression(
                (*actuals)[index], process_scope, visiting)) {
            return false;
        }
        if (class_method_copy_out(direction)) {
            const auto actual_expression
                = specialized_hir_unit_->find_expression(
                    (*actuals)[index]);
            const auto target = hir_target_declaration(
                (*actuals)[index]);
            const auto target_binding = target
                ? hir_runtime_binding(*target, process_scope, false)
                : std::nullopt;
            const auto name = actual_expression
                && ((actual_expression->systemverilog != nullptr
                        && actual_expression->systemverilog->kind
                            == semantic::sv::ExpressionKind::name)
                    || (actual_expression->vhdl != nullptr
                        && actual_expression->vhdl->kind
                            == semantic::vhdl::ExpressionKind::name));
            const auto writable = target_binding
                && (target_binding->kind == HirRuntimeBindingKind::local
                    || signal_binding_is_writable(*target_binding));
            if (!name || !writable
                || target_binding->width != binding->width) {
                return false;
            }
        }
    }
    return true;
}

std::optional<RegisterId> Lowerer::lower_hir_callable_actual(
    const semantic::ExpressionId actual,
    const semantic::DeclarationId formal,
    const std::size_t expected_width)
{
    const auto expression = specialized_hir_unit_
        ? specialized_hir_unit_->find_expression(actual)
        : std::nullopt;
    // A character literal actual of a CHARACTER formal is its position, not
    // a STD_ULOGIC value of the same spelling ('L', 'H').
    if (expression && expression->vhdl != nullptr
        && expression->vhdl->kind == semantic::vhdl::ExpressionKind::logic_literal
        && expression->vhdl->text.size() == 3U && expression->vhdl->text.front() == '\''
        && (expected_width == 8U || expected_width == 0U)) {
        const auto declaration = specialized_hir_unit_->find_declaration(formal);
        const auto subtype = declaration && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
            : std::nullopt;
        const auto name = subtype ? subtype->type_mark.spelling : std::string { };
        const auto separator = name.find_last_of(".:");
        if (same_callable_name(separator == std::string::npos ? std::string_view { name }
                                                               : std::string_view { name }.substr(separator + 1U),
                "character", true)) {
            const auto result = allocate_register(8U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant { result,
                PackedLogic4::from_aval_bval(8U,
                    static_cast<unsigned char>(expression->vhdl->text[1]), 0U) });
            return result;
        }
    }
    // A named event is passed only to an event formal (IEEE 1800-2017
    // 6.17, 15.5.5); it is not a value of any other type.
    if (expression && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::name) {
        const auto event_type = [&](const semantic::DeclarationId id) {
            const auto view = specialized_hir_unit_->find_declaration(id);
            return view && view->systemverilog != nullptr
                && view->systemverilog->type
                && view->systemverilog->type->target.spelling == "event";
        };
        if (const auto declaration = hir_referenced_declaration(actual);
            declaration && event_type(*declaration) && !event_type(formal)) {
            report("FSIM-ELAB-SVEVENT-011",
                "named event '" + expression->systemverilog->text
                    + "' cannot be passed to a task or function argument "
                      "that is not an event",
                hir_source_span(expression->systemverilog->source));
            return std::nullopt;
        }
    }
    if (expression && expression->systemverilog != nullptr) {
        // IEEE 1800-2017 13.3: an argument is assigned to its formal, which
        // converts between integral and real values (6.12.2).
        using Kind = frontend::SystemVerilogScalarKind;
        const auto real_kind = [](const Kind kind) {
            return kind == Kind::Real || kind == Kind::Realtime
                || kind == Kind::ShortReal;
        };
        const auto declaration = specialized_hir_unit_->find_declaration(
            formal);
        const auto& spelling = declaration
                && declaration->systemverilog != nullptr
                && declaration->systemverilog->type
                && !declaration->systemverilog->type->container_form
            ? declaration->systemverilog->type->target.spelling
            : std::string { };
        const auto formal_kind = spelling == "real" ? Kind::Real
            : spelling == "realtime"                ? Kind::Realtime
            : spelling == "shortreal"               ? Kind::ShortReal
                                                    : Kind::None;
        const auto actual_kind = hir_systemverilog_scalar_kind(actual);
        const bool integral_formal = declaration
            && declaration->systemverilog != nullptr
            && declaration->systemverilog->type
            && !declaration->systemverilog->type->container_form
            && formal_kind == Kind::None && spelling != "time"
            && spelling != "chandle" && spelling != "string"
            && spelling != "event";
        if (real_kind(formal_kind) && actual_kind == Kind::None) {
            const auto lowered = lower_hir_expression(actual,
                hir_expression_width(actual, hir_process_scope_)
                    .value_or(32U));
            if (!lowered) {
                return std::nullopt;
            }
            return convert_hir_integral_to_real(
                *lowered, hir_expression_signed(actual), formal_kind);
        }
        if (real_kind(formal_kind) && real_kind(actual_kind)
            && (formal_kind == Kind::ShortReal)
                != (actual_kind == Kind::ShortReal)) {
            const auto lowered = lower_hir_expression(actual,
                actual_kind == Kind::ShortReal ? 32U : 64U, actual_kind);
            if (!lowered) {
                return std::nullopt;
            }
            const auto converted = allocate_register(
                formal_kind == Kind::ShortReal ? 32U : 64U,
                frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(SystemVerilogScalarBinary {
                runtime::SystemVerilogScalarBinaryOperator::Convert,
                converted,
                *lowered,
                *lowered,
                actual_kind,
                actual_kind,
                formal_kind,
            });
            return converted;
        }
        if (integral_formal && real_kind(actual_kind)) {
            const auto lowered = lower_hir_expression(actual,
                actual_kind == Kind::ShortReal ? 32U : 64U, actual_kind);
            if (!lowered) {
                return std::nullopt;
            }
            auto converted = convert_hir_real_to_integral(
                *lowered, actual_kind);
            if (register_width(converted) != expected_width) {
                converted = resize_register(
                    converted, expected_width, true);
            }
            return converted;
        }
    }
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::aggregate) {
        return lower_hir_expression(actual, expected_width);
    }
    const auto declaration = specialized_hir_unit_->find_declaration(
        formal);
    auto subtype = declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype
        ? hir_effective_vhdl_subtype(*declaration->vhdl->subtype)
        : std::nullopt;
    if (!subtype) {
        return lower_hir_expression(actual, expected_width);
    }
    if (!subtype->executable_width
        || *subtype->executable_width == 0U) {
        subtype->executable_width = expected_width;
    }
    return lower_hir_vhdl_aggregate(
        actual, expected_width, &*subtype);
}

std::optional<Lowerer::HirFunctionCallValue>
Lowerer::lower_hir_function_call_value(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    if (hir_dpi_function_profile(expression_id, hir_process_scope_)) {
        const auto value = lower_hir_dpi_function_call(
            expression_id, expected_width);
        return value
            ? std::optional<HirFunctionCallValue> {
                  HirFunctionCallValue { *value, std::nullopt } }
            : std::nullopt;
    }
    const auto expression = specialized_hir_unit_
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    const auto callable = hir_referenced_declaration(expression_id);
    // A void function has no value (IEEE 1800-2017 13.4.1); only a call
    // statement may invoke it.
    const bool discarded = discarding_call_result_;
    discarding_call_result_ = false;
    if (!discarded && callable && expression
        && expression->systemverilog != nullptr) {
        const auto record = specialized_hir_unit_->find_declaration(*callable);
        if (record && record->systemverilog != nullptr
            && record->systemverilog->callable
            && record->systemverilog->callable->function
            && record->systemverilog->type
            && record->systemverilog->type->target.spelling == "void") {
            report(
                "FSIM-ELAB-SVFUNC-014",
                "void function '" + record->systemverilog->name
                    + "' cannot be used as a value",
                hir_source_span(expression->systemverilog->source));
            return std::nullopt;
        }
    }
    auto interface_callable = expression
            && expression->systemverilog != nullptr
        ? hir_interface_function_profile(
              expression_id, hir_process_scope_)
        : std::nullopt;
    if (interface_callable && hir_interface_receiver_override_) {
        interface_callable->receiver = *std::exchange(
            hir_interface_receiver_override_, std::nullopt);
    } else if (interface_callable) {
        // A function of the interface a virtual interface handle names
        // (`v.f()`, IEEE 1800-2017 25.9) runs on that instance: test the
        // handle against each instance of the interface type.
        const auto dispatch
            = hir_virtual_interface_instances(interface_callable->receiver);
        if (hierarchical_reference_retry_ && hierarchical_reference_missed_) {
            // Child storage is not available in the first hierarchy pass.
            // Defer this call instead of using a receiver-independent frame.
            return std::nullopt;
        }
        const auto type = dispatch
            ? hir_callable_type(interface_callable->declaration)
            : std::nullopt;
        if (dispatch && type && !type->string
            && (type->container || type->width != 0U)) {
            const auto handle
                = lower_hir_virtual_interface_handle(dispatch->first);
            if (!handle) {
                return std::nullopt;
            }
            const auto result = type->container
                ? std::optional<RegisterId> { }
                : std::optional { allocate_register(type->width, type->domain) };
            const auto container_result = type->container
                ? std::optional { allocate_container_register(*type->container) }
                : std::optional<ContainerRegisterId> { };
            if (result) {
                process_.operations.emplace_back(LoadConstant {
                    *result, callable_default_value(type->width, type->domain) });
            }
            std::vector<InstructionIndex> exits;
            for (const auto& [identity, path] : dispatch->second) {
                const auto constant = allocate_register(
                    64U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    constant, unsigned_value(identity, 64U) });
                const auto selected = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::case_equal, selected, *handle,
                    constant });
                const auto branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(Branch {
                    selected, branch + 1U, 0U,
                    UnknownBranchPolicy::when_false });
                hir_interface_receiver_override_ = "@instance:" + path;
                discarding_call_result_ = discarded;
                const auto value = lower_hir_function_call_value(
                    expression_id, expected_width);
                hir_interface_receiver_override_.reset();
                if (!value || (result && !value->packed)
                    || (container_result && !value->container)) {
                    return std::nullopt;
                }
                if (container_result) {
                    process_.operations.emplace_back(CopyContainerRegister {
                        *container_result, value->container->value });
                } else {
                    auto packed = *value->packed;
                    if (register_width(packed) != type->width) {
                        packed = resize_register(
                            packed, type->width, type->signed_value);
                    }
                    process_.operations.emplace_back(CopyRegister {
                        *result, packed });
                }
                exits.push_back(static_cast<InstructionIndex>(
                    process_.operations.size()));
                process_.operations.emplace_back(Jump { });
                process_.operations[branch] = Branch {
                    selected,
                    branch + 1U,
                    static_cast<InstructionIndex>(process_.operations.size()),
                    UnknownBranchPolicy::when_false,
                };
            }
            for (const auto exit : exits) {
                process_.operations[exit] = Jump {
                    static_cast<InstructionIndex>(process_.operations.size()) };
            }
            return HirFunctionCallValue { result,
                container_result
                    ? std::optional { HirStaticContainerValue {
                          *container_result, *type->container } }
                    : std::nullopt };
        }
    }
    const auto resolution = expression && expression->vhdl != nullptr
        ? resolve_hir_vhdl_function_call(
              expression_id, hir_process_scope_, expected_width)
        : callable ? hir_callable_resolution(*callable)
        : interface_callable
            ? hir_callable_resolution(interface_callable->declaration)
                   : std::nullopt;
    if (!resolution) {
        if (expression && expression->vhdl != nullptr
            && expression->vhdl->referenced_name) {
            const auto candidates = hir_vhdl_callable_candidates(
                *expression->vhdl->referenced_name,
                expression->vhdl->scope);
            if (candidates.empty()) {
                if (const auto extreme = lower_hir_vhdl_min_max(
                        expression_id, expected_width)) {
                    return HirFunctionCallValue { *extreme, std::nullopt };
                }
            }
            report(
                candidates.size() > 1U
                    ? "FSIM-ELAB-VHOVER-001"
                    : candidates.empty()
                    ? "FSIM-ELAB-VHNAME-001"
                    : "FSIM-ELAB-VHOVER-002",
                candidates.size() > 1U
                    ? "VHDL function call ambiguously matches visible overloads"
                    : candidates.empty()
                    ? "VHDL function call names no visible function"
                    : "VHDL function call has no matching overload",
                hir_source_span(expression->vhdl->source));
            const auto width = expected_width == 0U
                ? 32U
                : expected_width;
            const auto destination = allocate_register(
                width, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant {
                destination, unsigned_value(0U, width)
            });
            return HirFunctionCallValue {
                destination, std::nullopt
            };
        }
        return std::nullopt;
    }
    hir_generic_binding_frames_.push_back(
        resolution->generic_bindings);
    const PopBackGuard generic_scope { hir_generic_binding_frames_ };
    const auto type = hir_callable_type(
        resolution->body, expected_width);
    const auto actuals = bind_hir_function_actuals(
        expression_id, resolution->body);
    const auto declaration = specialized_hir_unit_->find_declaration(
        resolution->body);
    if (!type && hir_static_array_function_call(expression_id)) {
        report(
            "FSIM-ELAB-SVCONTAINER-020",
            "static unpacked-array bounds and storage must be locally "
            "constant, 32-bit, and within the per-container owning-storage "
            "budget",
            hir_source_span(
                expression->systemverilog->source));
        return std::nullopt;
    }
    if (!type || type->string || !actuals) {
        return std::nullopt;
    }
    if (!declaration) {
        return std::nullopt;
    }
    const auto scope = declaration->systemverilog != nullptr
        ? declaration->systemverilog->nested_scope
        : declaration->vhdl->nested_scope;
    std::vector<semantic::DeclarationId> formals;
    if (declaration->systemverilog != nullptr) {
        formals.assign(
            declaration->systemverilog->callable->formals.begin(),
            declaration->systemverilog->callable->formals.end());
    } else {
        formals.assign(
            declaration->vhdl->callable->formals.begin(),
            declaration->vhdl->callable->formals.end());
    }
    if (declaration->vhdl != nullptr && actuals
        && formals.size() == actuals->size()) {
        for (std::size_t index { }; index < formals.size(); ++index) {
            const auto formal = specialized_hir_unit_->find_declaration(
                formals[index]);
            const auto formal_subtype = formal
                    && formal->vhdl != nullptr
                    && formal->vhdl->subtype
                ? hir_effective_vhdl_subtype(*formal->vhdl->subtype)
                : std::nullopt;
            const auto actual_declaration = hir_referenced_declaration(
                (*actuals)[index]);
            const auto actual = actual_declaration
                ? specialized_hir_unit_->find_declaration(
                      *actual_declaration)
                : std::nullopt;
            if (!formal_subtype || !formal_subtype->unconstrained
                || !actual || actual->vhdl == nullptr
                || !actual->vhdl->callable
                || !actual->vhdl->callable->return_identifier) {
                continue;
            }
            report(
                "FSIM-ELAB-VHRESULT-001",
                "VHDL-2019 function result requires a fully constrained "
                "immediate array context",
                hir_source_span(expression->vhdl->source));
            return std::nullopt;
        }
    }
    if (!scope || formals.size() != actuals->size()) {
        return std::nullopt;
    }

    std::vector<std::pair<std::size_t, std::int64_t>>
        static_integer_actuals;
    std::vector<std::pair<std::size_t, semantic::ExpressionId>>
        static_integer_actual_expressions;
    std::vector<std::pair<std::size_t,
        std::vector<std::optional<HirPackedRange>>>>
        vhdl_actual_ranges;
    std::vector<std::pair<std::size_t,
        std::vector<semantic::vhdl::ElementConstraint>>>
        vhdl_actual_element_constraints;
    if (declaration->vhdl != nullptr) {
        const auto array_root = [&](semantic::TypeId type_id)
            -> std::optional<std::pair<semantic::TypeId, std::size_t>> {
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid()
                && visited.insert(type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (type->vhdl->form
                    == semantic::vhdl::TypeForm::array) {
                    return std::pair {
                        type_id, type->vhdl->array_dimensions.size()
                    };
                }
                if (type->vhdl->form
                        != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form
                        != semantic::vhdl::TypeForm::alias) {
                    return std::nullopt;
                }
                type_id = type->vhdl->base.type_mark.target;
            }
            return std::nullopt;
        };
        const auto predefined_packed_vector = [&](
            const semantic::vhdl::SubtypeIndication& subtype,
            const semantic::ScopeId use_scope)
            -> std::optional<std::pair<std::string_view,
                frontend::ValueDomain>> {
            using BuiltinType = semantic::vhdl::BuiltinTypeIdentity;
            if (subtype.domain
                == semantic::vhdl::ValueDomain::logic9) {
                if (subtype.builtin_type
                    == BuiltinType::
                        ieee_std_logic_1164_std_logic_vector) {
                    return std::pair {
                        std::string_view { "std_logic_vector" },
                        frontend::ValueDomain::Logic9,
                    };
                }
                if (subtype.builtin_type
                    == BuiltinType::
                        ieee_std_logic_1164_std_ulogic_vector) {
                    return std::pair {
                        std::string_view { "std_ulogic_vector" },
                        frontend::ValueDomain::Logic9,
                    };
                }
            }
            if (subtype.type_mark.target.valid()) {
                return std::nullopt;
            }
            constexpr auto builtin_prefix
                = std::string_view { "@builtin:" };
            auto spelling
                = std::string_view { subtype.type_mark.spelling };
            if (spelling.starts_with(builtin_prefix)) {
                spelling.remove_prefix(builtin_prefix.size());
            }
            if (same_callable_name(spelling, "bit_vector", true)
                || same_callable_name(
                    spelling, "standard.bit_vector", true)) {
                return std::pair {
                    std::string_view { "bit_vector" },
                    frontend::ValueDomain::Bit2,
                };
            }
            const bool integer_vector
                = same_callable_name(spelling, "integer_vector", true)
                || same_callable_name(
                    spelling, "standard.integer_vector", true)
                || same_callable_name(
                    spelling, "std.standard.integer_vector", true);
            if (integer_vector
                && subtype.domain
                    == semantic::vhdl::ValueDomain::integer
                && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2008
                && hir_vhdl_unshadowed_integer_vector(
                    *specialized_hir_unit_, subtype, use_scope,
                    hir_generic_binding_frames_)) {
                return std::pair {
                    std::string_view { "integer_vector" },
                    frontend::ValueDomain::Integer,
                };
            }
            return std::nullopt;
        };
        const auto index_origin = [&](const semantic::vhdl::Name& name)
            -> std::optional<HirPackedRange> {
            if (name.selected) {
                const auto declaration
                    = specialized_hir_unit_->find_declaration(*name.selected);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->subtype) {
                    for (const auto& constraint
                        : declaration->vhdl->subtype->constraints) {
                        if (constraint.kind
                                != semantic::vhdl::RangeKind::integer
                            && constraint.kind
                                != semantic::vhdl::RangeKind::enumeration
                            && constraint.kind
                                != semantic::vhdl::RangeKind::discrete) {
                            continue;
                        }
                        const auto left = hir_range_left(constraint);
                        const auto right = hir_range_right(constraint);
                        if (left && right) {
                            return HirPackedRange {
                                *left, *right, constraint.descending
                            };
                        }
                    }
                }
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->declared_type) {
                    const auto type = specialized_hir_unit_->find_type(
                        *declaration->vhdl->declared_type);
                    if (type && type->vhdl != nullptr
                        && type->vhdl->scalar_range) {
                        const auto& range = *type->vhdl->scalar_range;
                        const auto left = hir_range_left(range);
                        const auto right = hir_range_right(range);
                        if (left && right) {
                            return HirPackedRange {
                                *left, *right, range.descending
                            };
                        }
                    }
                    if (type && type->vhdl != nullptr
                        && !type->vhdl->enumeration_literals.empty()
                        && type->vhdl->enumeration_literals.size() - 1U
                            <= static_cast<std::size_t>(
                                std::numeric_limits<std::int64_t>::max())) {
                        return HirPackedRange { 0,
                            static_cast<std::int64_t>(
                                type->vhdl->enumeration_literals.size() - 1U),
                            false };
                    }
                }
            }
            auto spelling = std::string_view { name.spelling };
            if (const auto separator = spelling.find_last_of(".:");
                separator != std::string_view::npos) {
                spelling.remove_prefix(separator + 1U);
            }
            if (same_callable_name(spelling, "natural", true)) {
                return HirPackedRange { 0,
                    vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
                        ? std::numeric_limits<std::int64_t>::max()
                        : static_cast<std::int64_t>(
                              std::numeric_limits<std::int32_t>::max()),
                    false };
            }
            if (same_callable_name(spelling, "positive", true)) {
                return HirPackedRange { 1,
                    vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
                        ? std::numeric_limits<std::int64_t>::max()
                        : static_cast<std::int64_t>(
                              std::numeric_limits<std::int32_t>::max()),
                    false };
            }
            if (same_callable_name(spelling, "integer", true)) {
                return HirPackedRange {
                    vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
                        ? std::numeric_limits<std::int64_t>::min()
                        : static_cast<std::int64_t>(
                              std::numeric_limits<std::int32_t>::min()),
                    vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
                        ? std::numeric_limits<std::int64_t>::max()
                        : static_cast<std::int64_t>(
                              std::numeric_limits<std::int32_t>::max()),
                    false };
            }
            return std::nullopt;
        };
        const auto positional_shape = [&](const semantic::ExpressionId id,
                                          const std::size_t dimensions)
            -> std::optional<std::vector<std::size_t>> {
            using Kind = semantic::vhdl::ExpressionKind;
            std::vector<semantic::ExpressionId> level { id };
            std::vector<std::size_t> counts;
            counts.reserve(dimensions);
            for (std::size_t dimension { }; dimension < dimensions;
                 ++dimension) {
                std::optional<std::size_t> count;
                std::vector<semantic::ExpressionId> next;
                for (const auto expression_id : level) {
                    const auto expression
                        = specialized_hir_unit_->find_expression(expression_id);
                    if (!expression || expression->vhdl == nullptr
                        || expression->vhdl->kind != Kind::aggregate
                        || !std::ranges::all_of(
                            expression->vhdl->associations,
                            [](const semantic::vhdl::AggregateAssociation& a) {
                                return a.choices.empty()
                                    && a.choice_spelling.empty();
                            })) {
                        return std::nullopt;
                    }
                    const auto current_count
                        = expression->vhdl->associations.size();
                    if (count && *count != current_count) {
                        return std::nullopt;
                    }
                    count = current_count;
                    if (dimension + 1U < dimensions) {
                        for (const auto& association
                            : expression->vhdl->associations) {
                            next.push_back(association.value);
                        }
                    }
                }
                if (!count) {
                    return std::nullopt;
                }
                counts.push_back(*count);
                level = std::move(next);
            }
            return counts;
        };
        for (std::size_t index { }; index < formals.size(); ++index) {
            const auto formal = specialized_hir_unit_->find_declaration(
                formals[index]);
            if (!formal || formal->vhdl == nullptr
                || callable_direction(*formal)
                    != frontend::PortDirection::Input) {
                continue;
            }
            auto actual_expression = (*actuals)[index];
            auto actual_value = hir_constant_integer(actual_expression);
            if (!actual_value && active_hir_callable_
                && *active_hir_callable_ < hir_callable_frames_.size()) {
                const auto actual_declaration
                    = hir_referenced_declaration(actual_expression);
                const auto& caller = hir_callable_frames_[
                    *active_hir_callable_];
                const auto static_binding = actual_declaration
                    ? std::ranges::find_if(
                          caller.static_integer_bindings,
                          [&](const auto& binding) {
                              return binding.first == *actual_declaration;
                          })
                    : caller.static_integer_bindings.end();
                if (static_binding
                    != caller.static_integer_bindings.end()) {
                    auto body_formal = *actual_declaration;
                    const auto profile = std::ranges::find(
                        caller.profile_formals, *actual_declaration);
                    const bool profile_formal
                        = profile != caller.profile_formals.end();
                    const auto profile_index = profile_formal
                        ? static_cast<std::size_t>(
                              profile - caller.profile_formals.begin())
                        : 0U;
                    if (!profile_formal
                        || profile_index < caller.formals.size()) {
                        if (profile_formal) {
                            body_formal = caller.formals[profile_index];
                        }
                        const auto binding = std::ranges::find_if(
                            caller.generic_bindings.rbegin(),
                            caller.generic_bindings.rend(),
                            [&](const HirGenericBinding& candidate) {
                                return candidate.formal == body_formal
                                    && candidate.expression.has_value();
                            });
                        if (binding != caller.generic_bindings.rend()) {
                            actual_value = static_binding->second;
                            actual_expression = *binding->expression;
                        }
                    }
                }
            }
            if (actual_value) {
                static_integer_actuals.emplace_back(index, *actual_value);
                static_integer_actual_expressions.emplace_back(
                    index, actual_expression);
            }

            const auto formal_subtype = formal->vhdl->subtype
                ? hir_effective_vhdl_subtype(*formal->vhdl->subtype)
                : std::nullopt;
            // A STRING formal takes its actual's static index range: the
            // actual's constraint, or 1 to the length of a static value.
            if (formal_subtype && formal_subtype->unconstrained
                && formal_subtype->domain == semantic::vhdl::ValueDomain::string) {
                std::optional<HirPackedRange> string_range;
                if (const auto actual_subtype = hir_vhdl_expression_subtype((*actuals)[index])) {
                    for (const auto& constraint : actual_subtype->constraints) {
                        if (constraint.kind != semantic::vhdl::RangeKind::array_index) {
                            continue;
                        }
                        const auto left = hir_range_left(constraint);
                        const auto right = hir_range_right(constraint);
                        if (left && right) {
                            string_range = HirPackedRange { *left, *right, constraint.descending };
                        }
                        break;
                    }
                }
                if (!string_range) {
                    if (const auto text = specialized_hir_unit_->evaluate_string_expression(
                            (*actuals)[index]);
                        text && !text->empty()) {
                        string_range = HirPackedRange { 1,
                            static_cast<std::int64_t>(text->size()), false };
                    }
                }
                vhdl_actual_ranges.emplace_back(index,
                    std::vector<std::optional<HirPackedRange>> {
                        string_range,
                    });
                continue;
            }
            const auto formal_predefined_vector = formal_subtype
                    && formal_subtype->unconstrained
                ? predefined_packed_vector(
                      *formal_subtype, formal->vhdl->scope)
                : std::nullopt;
            const auto formal_array = formal_subtype
                    && formal_subtype->unconstrained
                    && formal_subtype->type_mark.target.valid()
                ? array_root(formal_subtype->type_mark.target)
                : std::nullopt;
            if ((!formal_array || formal_array->second == 0U)
                && !formal_predefined_vector) {
                continue;
            }

            std::optional<HirPackedRange> actual_range;
            const auto actual_root = hir_vhdl_composite_root_type(
                (*actuals)[index]);
            const auto actual_shape = specialized_hir_unit_->find_expression(
                (*actuals)[index]);
            const bool contextual_character_array_literal = formal_array
                && formal_subtype && actual_shape
                && actual_shape->vhdl != nullptr
                && actual_shape->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal;
            const auto actual_width = contextual_character_array_literal
                ? hir_expression_width((*actuals)[index], hir_process_scope_,
                      &*formal_subtype)
                : hir_expression_width(
                      (*actuals)[index], hir_process_scope_);
            const auto actual_subtype = hir_vhdl_expression_subtype(
                (*actuals)[index]);
            const auto actual_scope = actual_shape
                    && actual_shape->vhdl != nullptr
                ? actual_shape->vhdl->scope : semantic::ScopeId { };
            const auto actual_predefined_vector = actual_subtype
                ? predefined_packed_vector(*actual_subtype, actual_scope)
                : std::nullopt;
            const auto actual_effective = actual_subtype
                ? hir_effective_vhdl_subtype(*actual_subtype)
                : std::nullopt;
            const auto actual_array = actual_root
                    && actual_root->first
                        == semantic::vhdl::TypeForm::array
                ? array_root(actual_root->second)
                : std::nullopt;
            const auto actual_domain = hir_expression_domain(
                (*actuals)[index], hir_process_scope_);
            const auto range = hir_expression_range(
                (*actuals)[index], hir_process_scope_);
            const auto distance = range
                ? index_distance(range->left, range->right)
                : std::numeric_limits<std::uint64_t>::max();
            const auto nominal_array_match = formal_array && actual_array
                && actual_array->first == formal_array->first
                && actual_array->second == formal_array->second;
            const auto predefined_vector_match
                = formal_predefined_vector && actual_predefined_vector
                && same_callable_name(
                    formal_predefined_vector->first,
                    actual_predefined_vector->first, true)
                && actual_domain
                && *actual_domain == formal_predefined_vector->second
                && (!actual_root
                    || (actual_array && actual_array->second == 1U));
            const auto proven_vector_match = predefined_vector_match;
            // Elements may be wider than one bit (INTEGER, records): the
            // actual holds one element width per index.
            const auto element_width = [&]() -> std::uint64_t {
                if (!formal_array) {
                    if (formal_predefined_vector
                        && same_callable_name(
                            formal_predefined_vector->first,
                            "integer_vector", true)) {
                        return frontend::vhdl_predefined_integer_storage_width(
                            vhdl_standard_);
                    }
                    return 1U;
                }
                const auto type = specialized_hir_unit_->find_type(formal_array->first);
                if (!type || type->vhdl == nullptr || !type->vhdl->element_subtype) {
                    return 1U;
                }
                // An unbounded element takes the actual's element
                // constraints (IEEE 1076-2008 5.3.2.2).
                const auto declared_element = actual_effective
                    ? semantic::vhdl::constrained_element_subtype(
                          *actual_effective, "", *type->vhdl->element_subtype)
                    : *type->vhdl->element_subtype;
                const auto element = hir_effective_vhdl_subtype(declared_element)
                    .value_or(declared_element);
                return element.executable_width ? *element.executable_width
                    : element.domain == semantic::vhdl::ValueDomain::integer
                        && element.integer_storage_width != 0U
                    ? element.integer_storage_width
                    : 1U;
            }();
            if (((nominal_array_match && formal_array->second == 1U)
                    || proven_vector_match)
                && actual_width && range
                && distance
                    != std::numeric_limits<std::uint64_t>::max()
                && (distance + 1U) * element_width == *actual_width
                && (range->left == range->right
                    || range->descending
                        == (range->left > range->right))) {
                actual_range = *range;
                if (actual_effective
                    && !actual_effective->element_constraints.empty()) {
                    vhdl_actual_element_constraints.emplace_back(
                        index, actual_effective->element_constraints);
                }
            }
            const auto dimension_count
                = formal_array ? formal_array->second : 1U;
            std::vector<std::optional<HirPackedRange>> actual_ranges(
                dimension_count);
            if (dimension_count == 1U) {
                actual_ranges.front() = actual_range;
            }

            const auto actual_index_ranges = [&]() {
                std::vector<std::optional<HirPackedRange>> ranges(
                    dimension_count);
                std::size_t dimension { };
                if (actual_effective) {
                    for (const auto& constraint
                        : actual_effective->constraints) {
                        if (constraint.kind
                                != semantic::vhdl::RangeKind::array_index
                            || dimension >= dimension_count) {
                            continue;
                        }
                        const auto left = hir_range_left(constraint);
                        const auto right = hir_range_right(constraint);
                        if (left && right) {
                            ranges[dimension] = HirPackedRange {
                                *left, *right, constraint.descending
                            };
                        }
                        ++dimension;
                    }
                }
                if (actual_array) {
                    const auto array_type = specialized_hir_unit_->find_type(
                        actual_array->first);
                    if (array_type && array_type->vhdl != nullptr) {
                        for (std::size_t index { };
                             index < dimension_count
                                 && index
                                     < array_type->vhdl->array_dimensions.size();
                             ++index) {
                            if (ranges[index]
                                || !array_type->vhdl->array_dimensions[index]
                                        .constraint) {
                                continue;
                            }
                            const auto& constraint
                                = *array_type->vhdl->array_dimensions[index]
                                       .constraint;
                            const auto left = hir_range_left(constraint);
                            const auto right = hir_range_right(constraint);
                            if (left && right) {
                                ranges[index] = HirPackedRange {
                                    *left, *right, constraint.descending
                                };
                            }
                        }
                    }
                }
                return ranges;
            };
            const auto ranges_match_width = [&](
                const std::vector<std::optional<HirPackedRange>>& ranges) {
                if (!actual_width || ranges.empty() || element_width == 0U) {
                    return false;
                }
                std::uint64_t element_count { 1U };
                for (const auto& candidate : ranges) {
                    if (!candidate) {
                        return false;
                    }
                    const auto distance = index_distance(
                        candidate->left, candidate->right);
                    if (distance
                            == std::numeric_limits<std::uint64_t>::max()
                        || distance + 1U
                            > std::numeric_limits<std::uint64_t>::max()
                                / element_count) {
                        return false;
                    }
                    element_count *= distance + 1U;
                }
                return element_count
                        <= std::numeric_limits<std::uint64_t>::max()
                            / element_width
                    && element_count * element_width == *actual_width;
            };
            if (dimension_count > 1U && nominal_array_match) {
                actual_ranges = actual_index_ranges();
                if (!ranges_match_width(actual_ranges)) {
                    std::ranges::fill(actual_ranges,
                        std::optional<HirPackedRange> { });
                }
            }
            // A rank-one array actual can carry its known range in the
            // effective subtype even when its expression has no
            // expression-level range. Keep that range only when it proves
            // the actual packed extent exactly.
            if (dimension_count == 1U && nominal_array_match
                && !actual_ranges.front()) {
                auto resolved_ranges = actual_index_ranges();
                if (ranges_match_width(resolved_ranges)) {
                    actual_ranges = std::move(resolved_ranges);
                }
            }

            // A positional aggregate takes each dimension's bounds from the
            // corresponding formal index subtype (IEEE 1076-2008 9.3.3.3).
            auto actual_shape_expression = (*actuals)[index];
            auto actual_node = specialized_hir_unit_->find_expression(
                actual_shape_expression);
            if (actual_node && actual_node->vhdl != nullptr
                && actual_node->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && actual_node->vhdl->text.starts_with(
                    "@vhdl-qualified:")
                && actual_node->vhdl->operands.size() == 1U) {
                // The qualification supplies the nominal array identity;
                // its operand still carries the positional association shape
                // needed to bind an unconstrained formal's index range.
                actual_shape_expression
                    = actual_node->vhdl->operands.front();
                actual_node = specialized_hir_unit_->find_expression(
                    actual_shape_expression);
            }
            const bool positional_actual = actual_node
                && actual_node->vhdl != nullptr
                && (actual_node->vhdl->kind
                        == semantic::vhdl::ExpressionKind::string_literal
                    || (actual_node->vhdl->kind
                            == semantic::vhdl::ExpressionKind::aggregate
                        && std::ranges::all_of(
                            actual_node->vhdl->associations,
                            [](const semantic::vhdl::AggregateAssociation& a) {
                                return a.choices.empty()
                                    && a.choice_spelling.empty();
                            })));
            if (positional_actual && actual_width && element_width != 0U
                && dimension_count != 0U) {
                std::optional<std::vector<std::size_t>> shape;
                if (actual_node->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal) {
                    if (dimension_count == 1U
                        && *actual_width % element_width == 0U) {
                        shape = std::vector<std::size_t> {
                            static_cast<std::size_t>(
                                *actual_width / element_width),
                        };
                    }
                } else {
                    shape = positional_shape(actual_shape_expression,
                        dimension_count);
                }
                if (shape && shape->size() == dimension_count) {
                    std::vector<std::optional<HirPackedRange>> sized_ranges(
                        dimension_count);
                    const auto formal_type = formal_array
                        ? specialized_hir_unit_->find_type(formal_array->first)
                        : std::nullopt;
                    for (std::size_t dimension { };
                         dimension < dimension_count; ++dimension) {
                        std::optional<HirPackedRange> index_subtype;
                        if (formal_predefined_vector) {
                            index_subtype = HirPackedRange {
                                0, std::numeric_limits<std::int64_t>::max(),
                                false,
                            };
                        } else if (formal_type && formal_type->vhdl != nullptr
                            && dimension
                                < formal_type->vhdl->array_dimensions.size()) {
                            const auto& declaration_index
                                = formal_type->vhdl->array_dimensions[dimension]
                                      .index_subtype;
                            index_subtype = index_origin(declaration_index);
                        }
                        if (!index_subtype || (*shape)[dimension] == 0U
                            || (*shape)[dimension]
                                > static_cast<std::size_t>(
                                    std::numeric_limits<std::int64_t>::max())) {
                            break;
                        }
                        const auto count = static_cast<std::int64_t>(
                            (*shape)[dimension]);
                        const auto delta = count - 1;
                        if ((!index_subtype->descending
                                && index_subtype->left
                                    > std::numeric_limits<std::int64_t>::max()
                                        - delta)
                            || (index_subtype->descending
                                && index_subtype->left
                                    < std::numeric_limits<std::int64_t>::min()
                                        + delta)) {
                            break;
                        }
                        const auto right = index_subtype->descending
                            ? index_subtype->left - delta
                            : index_subtype->left + delta;
                        if ((!index_subtype->descending
                                && right > index_subtype->right)
                            || (index_subtype->descending
                                && right < index_subtype->right)) {
                            break;
                        }
                        sized_ranges[dimension] = HirPackedRange {
                            index_subtype->left, right,
                            index_subtype->descending,
                        };
                    }
                    if (std::ranges::all_of(sized_ranges,
                            [](const auto& range) { return range.has_value(); })
                        && ranges_match_width(sized_ranges)) {
                        actual_ranges = std::move(sized_ranges);
                    }
                }
            }
            vhdl_actual_ranges.emplace_back(index, std::move(actual_ranges));
        }
    }

    const auto callable_identity
        = (static_cast<std::uint64_t>(resolution->key.value()) << 32U)
        | static_cast<std::uint64_t>(type->width);
    auto callable_key = std::to_string(callable_identity);
    for (const auto& [index, value] : static_integer_actuals) {
        callable_key += "|" + std::to_string(index) + "="
            + std::to_string(value);
    }
    for (const auto& [index, range] : vhdl_actual_ranges) {
        callable_key += "|vhdl-range:" + std::to_string(index) + "=";
        for (std::size_t dimension { }; dimension < range.size();
             ++dimension) {
            callable_key += std::to_string(dimension) + ":";
            if (range[dimension]) {
                callable_key += std::to_string(range[dimension]->left) + ":"
                    + std::to_string(range[dimension]->right) + ":"
                    + (range[dimension]->descending ? "down" : "up");
            } else {
                callable_key += "unknown";
            }
            callable_key += ";";
        }
    }
    for (const auto& [index, constraints] : vhdl_actual_element_constraints) {
        callable_key += "|vhdl-element:" + std::to_string(index) + "=";
        append_vhdl_element_constraint_key(callable_key, constraints);
    }
    auto frame_index = hir_callable_frames_.size();
    bool new_frame { true };
    if (interface_callable) {
        const auto key = callable_key + "|receiver:"
            + std::to_string(interface_callable->receiver.size()) + ":"
            + interface_callable->receiver;
        const auto found = hir_interface_callable_indices_.find(key);
        if (found != hir_interface_callable_indices_.end()) {
            frame_index = found->second;
            new_frame = false;
        } else {
            hir_interface_callable_indices_.emplace(key, frame_index);
        }
    } else {
        const auto found = hir_callable_indices_.find(callable_key);
        if (found != hir_callable_indices_.end()) {
            frame_index = found->second;
            new_frame = false;
        } else {
            hir_callable_indices_.emplace(callable_key, frame_index);
        }
    }
    if (new_frame) {
        HirCallableFrame frame;
        frame.declaration = resolution->body;
        frame.scope = *scope;
        frame.type = *type;
        if (interface_callable) {
            frame.interface_receiver = interface_callable->receiver;
        }
        frame.formals = formals;
        frame.generic_bindings = resolution->generic_bindings;
        bind_hir_instance_parameters(frame);
        const auto debug_declaration
            = specialized_hir_unit_->find_declaration(resolution->key);
        if (resolution->key != resolution->body
            && debug_declaration
            && debug_declaration->vhdl != nullptr
            && debug_declaration->vhdl->callable
            && debug_declaration->vhdl->callable->formals.size()
                == formals.size()) {
            frame.profile_formals.assign(
                debug_declaration->vhdl->callable->formals.begin(),
                debug_declaration->vhdl->callable->formals.end());
        }
        for (const auto& [index, range] : vhdl_actual_ranges) {
            if (index >= frame.formals.size()) {
                return std::nullopt;
            }
            frame.vhdl_formal_ranges.emplace(
                frame.formals[index].value(), range);
            if (frame.profile_formals.size() == frame.formals.size()
                && frame.profile_formals[index]
                    != frame.formals[index]) {
                frame.vhdl_formal_ranges.emplace(
                    frame.profile_formals[index].value(), range);
            }
        }
        for (const auto& [index, constraints] :
            vhdl_actual_element_constraints) {
            frame.vhdl_formal_element_constraints.emplace(
                frame.formals[index].value(), constraints);
            if (frame.profile_formals.size() == frame.formals.size()) {
                frame.vhdl_formal_element_constraints.emplace(
                    frame.profile_formals[index].value(), constraints);
            }
        }
        // A VHDL function cannot suspend, so its invocation registers are no
        // longer live at any debugger stop. Keep those transient formals out
        // of the enclosing process's debug-local table. Procedures retain a
        // debug name in lower_hir_vhdl_procedure_call because they may wait
        // with their frame live.
        frame.debug_name = debug_declaration
                && debug_declaration->systemverilog != nullptr
            ? debug_declaration->systemverilog->name
            : std::string { };
        if (type->container) {
            frame.container_result = allocate_container_register(
                *type->container);
            frame.container_result_default = allocate_container_register(
                *type->container);
            frame.invocation_containers.push_back(
                *frame.container_result);
        } else {
            frame.result = allocate_register(type->width, type->domain);
            frame.invocation_registers.push_back(frame.result);
            // The implicit result variable of a 4-state function starts
            // unknown; a body that never assigns it returns x (IEEE
            // 1800-2017 13.4.1, 6.8 Table 6-7).
            if (language_ != frontend::Language::Vhdl2008
                && (type->domain == frontend::ValueDomain::Logic4
                    || type->domain == frontend::ValueDomain::Integer)) {
                process_.operations.emplace_back(LoadConstant {
                    frame.result,
                    PackedLogic4(type->width, Logic4::x),
                });
            }
        }
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (frame.invocation_identity == 0U) {
            frame.invocation_identity = next_callable_invocation_identity_++;
        }
        for (std::size_t index { }; index < formals.size(); ++index) {
            const auto formal = formals[index];
            const auto record = specialized_hir_unit_->find_declaration(
                formal);
            const auto direction = record
                ? callable_direction(*record)
                : frontend::PortDirection::Unknown;
            if (direction == frontend::PortDirection::Unknown) {
                return std::nullopt;
            }
            const auto static_actual = std::ranges::find_if(
                static_integer_actuals,
                [index](const auto& binding) {
                    return binding.first == index;
                });
            if (static_actual != static_integer_actuals.end()) {
                HirGenericBinding constant_actual;
                constant_actual.formal = formal;
                const auto static_expression = std::ranges::find_if(
                    static_integer_actual_expressions,
                    [index](const auto& binding) {
                        return binding.first == index;
                    });
                if (static_expression
                    == static_integer_actual_expressions.end()) {
                    return std::nullopt;
                }
                constant_actual.expression = static_expression->second;
                frame.generic_bindings.push_back(
                    std::move(constant_actual));
                frame.static_integer_bindings.emplace_back(
                    formal, static_actual->second);
                if (frame.profile_formals.size() == formals.size()
                    && frame.profile_formals[index] != formal) {
                    frame.static_integer_bindings.emplace_back(
                        frame.profile_formals[index],
                        static_actual->second);
                }
            }
            const auto string = record
                    && record->systemverilog != nullptr
                    && record->systemverilog->type
                ? record->systemverilog->type->value_form
                    == semantic::sv::TypeForm::string
                : vhdl_string_formal(formal);
            const auto container = record
                    && record->systemverilog != nullptr
                    && record->systemverilog->type
                ? hir_systemverilog_container_type(
                      *record->systemverilog->type).has_value()
                : false;
            frame.argument_is_string.push_back(string);
            frame.argument_is_container.push_back(container);
            if (container) {
                const auto container_type = hir_systemverilog_container_type(
                    *record->systemverilog->type);
                if (!container_type) {
                    return std::nullopt;
                }
                frame.arguments.push_back({ });
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back(
                    allocate_container_register(*container_type));
                frame.container_output_defaults.push_back(
                    direction == frontend::PortDirection::Output
                        ? std::optional<ContainerRegisterId> {
                              allocate_container_register(*container_type) }
                        : std::nullopt);
                frame.invocation_containers.push_back(
                    frame.container_arguments.back());
            } else if (string) {
                frame.arguments.push_back({ });
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.container_arguments.push_back({ });
                frame.container_output_defaults.push_back(std::nullopt);
                frame.invocation_strings.push_back(
                    frame.string_arguments.back());
            } else {
                const auto binding = hir_callable_formal_binding(
                    formal, *scope, (*actuals)[index],
                    hir_process_scope_);
                if (!binding) {
                    return std::nullopt;
                }
                frame.arguments.push_back(allocate_register(
                    binding->width, binding->domain));
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back({ });
                frame.container_output_defaults.push_back(std::nullopt);
                frame.invocation_registers.push_back(
                    frame.arguments.back());
            }
            frame.directions.push_back(direction);
        }
        frame.allocated = true;
        hir_callable_frames_.push_back(std::move(frame));
        if (!allocate_hir_static_callable_declarations(frame_index)) {
            return std::nullopt;
        }
    }

    std::vector<std::optional<RegisterId>> lowered_actuals(
        actuals->size());
    std::vector<std::optional<StringRegisterId>> lowered_string_actuals(
        actuals->size());
    std::vector<std::optional<ContainerRegisterId>>
        lowered_container_actuals(actuals->size());
    std::vector<std::optional<HirPackedUpdateTarget>>
        packed_copy_out_targets(actuals->size());
    for (std::size_t index = 0U; index < actuals->size(); ++index) {
        const auto& frame = hir_callable_frames_[frame_index];
        if (frame.argument_is_container[index]) {
            const auto formal_type
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::copy_at(
                    process_.container_register_types,
                    frame.container_arguments[index]);
            const auto actual = allocate_container_register(formal_type);
            lowered_container_actuals[index] = actual;
            if (callable_copy_in(frame.directions[index])) {
                const auto actual_type
                    = hir_static_container_expression_type(
                        (*actuals)[index]);
                if (actual_type && !actual_type->fixed
                    && !formal_type.fixed
                    && *actual_type != formal_type
                    && frame.directions[index]
                        != frontend::PortDirection::Input) {
                    report(
                        "FSIM-ELAB-SVFUNC-009",
                        "function container arguments require an "
                        "exactly compatible kind and profile",
                        hir_source_span(
                            specialized_hir_unit_
                                ->find_expression((*actuals)[index])
                                ->systemverilog->source));
                    continue;
                }
                const auto value = lower_hir_static_container_actual(
                    (*actuals)[index], formal_type, formals[index]);
                if (!value) {
                    if (actual_type && !actual_type->fixed
                        && !formal_type.fixed
                        && *actual_type != formal_type) {
                        report(
                            "FSIM-ELAB-SVFUNC-009",
                            "function container arguments require an "
                            "exactly compatible kind and profile",
                            hir_source_span(
                                specialized_hir_unit_
                                    ->find_expression((*actuals)[index])
                                    ->systemverilog->source));
                        continue;
                    }
                    return std::nullopt;
                }
                process_.operations.emplace_back(CopyContainerRegister {
                    actual, *value });
            }
            continue;
        }
        if (!frame.argument_is_string[index]
            && class_method_copy_out(frame.directions[index])) {
            packed_copy_out_targets[index]
                = capture_hir_packed_update_target(
                    (*actuals)[index]);
        }
        if (!callable_copy_in(frame.directions[index])) {
            continue;
        }
        if (frame.argument_is_string[index]) {
            lowered_string_actuals[index] = lower_hir_string_expression(
                (*actuals)[index]);
            if (!lowered_string_actuals[index]) {
                return std::nullopt;
            }
            continue;
        }
        const auto formal = hir_callable_formal_binding(
            formals[index], *scope, (*actuals)[index],
            hir_process_scope_);
        if (!formal) {
            return std::nullopt;
        }
        lowered_actuals[index] = packed_copy_out_targets[index]
            ? std::optional<RegisterId> {
                  packed_copy_out_targets[index]->captured }
            : lower_hir_callable_actual(
                  (*actuals)[index], formals[index], formal->width);
        if (!lowered_actuals[index]) {
            return std::nullopt;
        }
        if (register_width(*lowered_actuals[index]) != formal->width) {
            lowered_actuals[index] = resize_register(
                *lowered_actuals[index], formal->width,
                hir_actual_extension_signed(
                (*actuals)[index], formal->signed_value));
        }
        if (is_two_state_domain(formal->domain)) {
            // A 2-state formal receives X and Z bits as 0 (IEEE 1800-2017
            // 6.24.1 by way of 13.3).
            lowered_actuals[index]
                = convert_to_two_state(*lowered_actuals[index]);
        }
    }

    const auto call_expression = specialized_hir_unit_->find_expression(
        expression_id);
    const auto call_source = call_expression->systemverilog != nullptr
        ? call_expression->systemverilog->source
        : call_expression->vhdl->source;
    emit_debug_point(DebugPointKind::call, hir_source_span(call_source));
    auto& frame = hir_callable_frames_[frame_index];
    if (frame.type.automatic) {
        const auto push_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CallableFramePush {
            frame.invocation_identity,
            frame.invocation_registers,
            frame.invocation_strings,
            frame.invocation_containers,
            true,
        });
        if (!frame.snapshot_complete) {
            frame.invocation_push_sites.push_back(push_site);
        }
    }
    if (frame.type.container) {
        if (!frame.container_result || !frame.container_result_default) {
            return std::nullopt;
        }
        if (frame.type.automatic) {
            process_.operations.emplace_back(CopyContainerRegister {
                *frame.container_result,
                *frame.container_result_default,
            });
        }
    } else {
        process_.operations.emplace_back(LoadConstant {
            frame.result,
            callable_default_value(frame.type.width, frame.type.domain),
        });
    }
    for (std::size_t index = 0U;
        index < lowered_actuals.size(); ++index) {
        if (frame.argument_is_container[index]) {
            if (frame.directions[index]
                == frontend::PortDirection::Output) {
                if (!frame.container_output_defaults[index]) {
                    return std::nullopt;
                }
                process_.operations.emplace_back(CopyContainerRegister {
                    frame.container_arguments[index],
                    *frame.container_output_defaults[index],
                });
            } else if (lowered_container_actuals[index]) {
                process_.operations.emplace_back(CopyContainerRegister {
                    frame.container_arguments[index],
                    *lowered_container_actuals[index],
                });
            } else {
                return std::nullopt;
            }
        } else if (frame.argument_is_string[index]) {
            if (lowered_string_actuals[index]) {
                process_.operations.emplace_back(CopyStringRegister {
                    frame.string_arguments[index],
                    *lowered_string_actuals[index],
                });
            } else {
                process_.operations.emplace_back(LoadStringConstant {
                    frame.string_arguments[index],
                    { },
                });
            }
        } else if (lowered_actuals[index]) {
            process_.operations.emplace_back(CopyRegister {
                frame.arguments[index], *lowered_actuals[index] });
        } else {
            const auto formal = hir_callable_formal_binding(
                formals[index], *scope, (*actuals)[index],
                hir_process_scope_);
            if (!formal) {
                return std::nullopt;
            }
            process_.operations.emplace_back(LoadConstant {
                frame.arguments[index],
                callable_default_value(formal->width, formal->domain),
            });
        }
    }
    transfer_hir_static_formals(frame, true);
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0U),
        static_cast<InstructionIndex>(call_site + 1U),
        { },
    });
    transfer_hir_static_formals(frame, false);
    if (!frame.target) {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_hir_callables_.push_back(frame_index);
    }
    std::vector<std::optional<StringRegisterId>> string_copy_outs(
        actuals->size());
    std::vector<std::optional<RegisterId>> packed_copy_outs(
        actuals->size());
    std::vector<RegisterId> preserve_packed;
    std::vector<ContainerRegisterId> preserve_containers;
    for (std::size_t index = 0U; index < actuals->size(); ++index) {
        if (!class_method_copy_out(frame.directions[index])) {
            continue;
        }
        if (frame.argument_is_container[index]) {
            if (!lowered_container_actuals[index]) {
                return std::nullopt;
            }
            process_.operations.emplace_back(CopyContainerRegister {
                *lowered_container_actuals[index],
                frame.container_arguments[index],
            });
            preserve_containers.push_back(
                *lowered_container_actuals[index]);
            continue;
        }
        if (frame.argument_is_string[index]) {
            string_copy_outs[index] = allocate_string_register();
            process_.operations.emplace_back(CopyStringRegister {
                *string_copy_outs[index],
                frame.string_arguments[index],
            });
            continue;
        }
        packed_copy_outs[index] = allocate_register(
            register_width(frame.arguments[index]),
            register_domain(frame.arguments[index]));
        process_.operations.emplace_back(CopyRegister {
            *packed_copy_outs[index], frame.arguments[index] });
        preserve_packed.push_back(*packed_copy_outs[index]);
    }
    std::optional<RegisterId> packed_destination;
    std::optional<HirStaticContainerValue> container_destination;
    if (frame.type.container) {
        if (!frame.container_result) {
            return std::nullopt;
        }
        const auto destination = allocate_container_register(
            *frame.type.container);
        process_.operations.emplace_back(CopyContainerRegister {
            destination, *frame.container_result });
        preserve_containers.push_back(destination);
        container_destination = HirStaticContainerValue {
            destination, *frame.type.container
        };
    } else {
        packed_destination = allocate_register(
            frame.type.width, frame.type.domain);
        process_.operations.emplace_back(CopyRegister {
            *packed_destination, frame.result });
        preserve_packed.push_back(*packed_destination);
    }
    std::vector<StringRegisterId> preserve_strings;
    for (const auto copy_out : string_copy_outs) {
        if (copy_out) {
            preserve_strings.push_back(*copy_out);
        }
    }
    if (frame.type.automatic) {
        process_.operations.emplace_back(CallableFramePop {
            frame.invocation_identity,
            std::move(preserve_packed),
            std::move(preserve_strings),
            std::move(preserve_containers),
        });
    }
    for (std::size_t index = 0U;
        index < string_copy_outs.size(); ++index) {
        if (frame.argument_is_container[index]
            && class_method_copy_out(frame.directions[index])) {
            if (!lowered_container_actuals[index]
                || !lower_hir_container_copy_out(
                    (*actuals)[index],
                    *lowered_container_actuals[index])) {
                return std::nullopt;
            }
            continue;
        }
        if (string_copy_outs[index]
            && !lower_hir_string_copy_out(
                (*actuals)[index], *string_copy_outs[index])) {
            return std::nullopt;
        }
        if (!string_copy_outs[index] && packed_copy_outs[index]) {
            // A signed formal extends to a wider actual by its own sign
            // (IEEE 1800-2017 13.3, 10.7).
            const auto formal = index < frame.formals.size()
                ? specialized_hir_unit_->find_declaration(
                      frame.formals[index])
                : std::nullopt;
            const auto actual_width = hir_expression_width(
                (*actuals)[index], hir_process_scope_);
            if (formal && formal->systemverilog != nullptr
                && formal->systemverilog->type
                && formal->systemverilog->type->signed_value
                && actual_width
                && *actual_width > register_width(*packed_copy_outs[index])
                && !frontend::is_real_scalar_kind(
                    hir_systemverilog_scalar_kind((*actuals)[index]))
                && formal->systemverilog->type->target.spelling != "real"
                && formal->systemverilog->type->target.spelling != "shortreal"
                && formal->systemverilog->type->target.spelling
                    != "realtime") {
                packed_copy_outs[index] = resize_register(
                    *packed_copy_outs[index], *actual_width, true);
            }
            // A two-state actual receives x and z bits as zero (IEEE
            // 1800-2017 6.11.2).
            if (hir_expression_domain((*actuals)[index], hir_process_scope_)
                == frontend::ValueDomain::Bit2) {
                packed_copy_outs[index]
                    = convert_to_two_state(*packed_copy_outs[index]);
            }
            const auto copied = packed_copy_out_targets[index]
                ? write_hir_packed_update_target(
                      *packed_copy_out_targets[index],
                      *packed_copy_outs[index])
                : lower_hir_packed_copy_out(
                      (*actuals)[index], *packed_copy_outs[index]);
            if (!copied) {
                return std::nullopt;
            }
        }
    }
    if (container_destination) {
        return HirFunctionCallValue {
            std::nullopt, std::move(container_destination)
        };
    }
    if (!packed_destination) {
        return std::nullopt;
    }
    if (expected_width != 0U && expected_width != frame.type.width) {
        packed_destination = resize_register(
            *packed_destination, expected_width,
            frame.type.signed_value);
    }
    return HirFunctionCallValue {
        packed_destination, std::nullopt
    };
}

std::optional<RegisterId> Lowerer::lower_hir_vhdl_resolution_call(
    const semantic::DeclarationId function,
    const RegisterId driving_values,
    const std::size_t driver_count,
    const std::size_t element_width)
{
    // A resolution function receives the driving values as an array indexed
    // 0 to N-1 of the formal's index subtype (IEEE 1076-2008 4.6).
    const auto declaration = specialized_hir_unit_->find_declaration(function);
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->callable
        || declaration->vhdl->callable->formals.size() != 1U
        || !declaration->vhdl->nested_scope) {
        return std::nullopt;
    }
    const auto type = hir_callable_type(function, element_width);
    if (!type || type->string || type->container
        || type->width != element_width) {
        return std::nullopt;
    }
    const auto formal = declaration->vhdl->callable->formals.front();
    // The driving values are indexed from the low bound of the formal's
    // index subtype (NATURAL from 0, POSITIVE from 1).
    std::int64_t low { };
    if (const auto formal_record = specialized_hir_unit_->find_declaration(formal);
        formal_record && formal_record->vhdl != nullptr
        && formal_record->vhdl->subtype) {
        auto type_id = formal_record->vhdl->subtype->type_mark.target;
        std::unordered_set<std::uint32_t> visiting;
        const semantic::vhdl::TypeDefinition* array = nullptr;
        while (type_id.valid() && visiting.insert(type_id.value()).second) {
            const auto candidate = specialized_hir_unit_->find_type(type_id);
            if (!candidate || candidate->vhdl == nullptr) {
                break;
            }
            if (candidate->vhdl->form == semantic::vhdl::TypeForm::array) {
                array = candidate->vhdl;
                break;
            }
            type_id = candidate->vhdl->base.type_mark.target;
        }
        if (array != nullptr && array->array_dimensions.size() == 1U) {
            const auto& index = array->array_dimensions.front().index_subtype;
            auto name = std::string_view { index.spelling };
            name.remove_prefix(name.find_last_of(".:") + 1U);
            if (same_callable_name(name, "positive", true)) {
                low = 1;
            } else if (index.selected) {
                const auto index_declaration
                    = specialized_hir_unit_->find_declaration(*index.selected);
                const auto index_type = index_declaration
                        && index_declaration->vhdl != nullptr
                        && index_declaration->vhdl->declared_type
                    ? specialized_hir_unit_->find_type(
                          *index_declaration->vhdl->declared_type)
                    : std::nullopt;
                if (index_type && index_type->vhdl != nullptr
                    && index_type->vhdl->scalar_range
                    && index_type->vhdl->scalar_range->left
                    && index_type->vhdl->scalar_range->right) {
                    low = std::min(*index_type->vhdl->scalar_range->left,
                        *index_type->vhdl->scalar_range->right);
                }
            }
        }
    }
    const auto callable_key = "vhdl-resolver:"
        + std::to_string(function.value()) + ":"
        + std::to_string(driver_count) + ":"
        + std::to_string(element_width);
    auto frame_index = hir_callable_frames_.size();
    if (const auto found = hir_callable_indices_.find(callable_key);
        found != hir_callable_indices_.end()) {
        frame_index = found->second;
    } else {
        hir_callable_indices_.emplace(callable_key, frame_index);
        HirCallableFrame frame;
        frame.declaration = function;
        frame.scope = *declaration->vhdl->nested_scope;
        frame.type = *type;
        frame.formals = { formal };
        frame.vhdl_formal_ranges.emplace(formal.value(),
            std::vector<std::optional<HirPackedRange>> {
                HirPackedRange { low,
                    low + static_cast<std::int64_t>(driver_count) - 1, false },
            });
        frame.result = allocate_register(type->width, type->domain);
        frame.invocation_registers.push_back(frame.result);
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (frame.invocation_identity == 0U) {
            frame.invocation_identity = next_callable_invocation_identity_++;
        }
        frame.argument_is_string.push_back(false);
        frame.argument_is_container.push_back(false);
        frame.arguments.push_back(allocate_register(
            register_width(driving_values), register_domain(driving_values)));
        frame.string_arguments.push_back({ });
        frame.container_arguments.push_back({ });
        frame.container_output_defaults.push_back(std::nullopt);
        frame.invocation_registers.push_back(frame.arguments.back());
        frame.directions.push_back(frontend::PortDirection::Input);
        frame.allocated = true;
        hir_callable_frames_.push_back(std::move(frame));
        if (!allocate_hir_static_callable_declarations(frame_index)) {
            return std::nullopt;
        }
    }
    auto& frame = hir_callable_frames_[frame_index];
    if (frame.type.automatic) {
        const auto push_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CallableFramePush {
            frame.invocation_identity,
            frame.invocation_registers,
            frame.invocation_strings,
            frame.invocation_containers,
            true,
        });
        if (!frame.snapshot_complete) {
            frame.invocation_push_sites.push_back(push_site);
        }
    }
    process_.operations.emplace_back(LoadConstant {
        frame.result,
        callable_default_value(frame.type.width, frame.type.domain),
    });
    process_.operations.emplace_back(CopyRegister {
        frame.arguments.front(), driving_values });
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0U),
        static_cast<InstructionIndex>(call_site + 1U),
        { },
    });
    if (!frame.target) {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_hir_callables_.push_back(frame_index);
    }
    const auto destination = allocate_register(
        frame.type.width, frame.type.domain);
    process_.operations.emplace_back(CopyRegister {
        destination, frame.result });
    if (frame.type.automatic) {
        process_.operations.emplace_back(CallableFramePop {
            frame.invocation_identity,
            std::vector<RegisterId> { destination },
            { },
            { },
        });
    }
    return destination;
}

std::optional<RegisterId> Lowerer::lower_hir_dpi_function_call(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto profile = hir_dpi_function_profile(
        expression_id, hir_process_scope_);
    const auto expression = specialized_hir_unit_
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!profile || !expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    std::vector<RegisterId> actuals;
    std::vector<std::string> actual_names;
    std::vector<std::uint8_t> actual_directions;
    std::vector<std::uint8_t> actual_kinds;
    actuals.reserve(profile->actuals.size());
    actual_names.reserve(profile->actuals.size());
    actual_directions.reserve(profile->actuals.size());
    actual_kinds.reserve(profile->actuals.size());
    for (const auto& actual : profile->actuals) {
        const auto string = actual.domain == frontend::ValueDomain::String;
        RegisterId storage { };
        if (string) {
            const auto value = callable_copy_in(actual.direction)
                ? lower_hir_string_expression(actual.expression)
                : std::optional<StringRegisterId> { };
            if (callable_copy_in(actual.direction) && !value) {
                return std::nullopt;
            }
            const auto argument = allocate_string_register();
            if (value) {
                process_.operations.emplace_back(CopyStringRegister {
                    argument, *value });
            } else {
                process_.operations.emplace_back(LoadStringConstant {
                    argument, { } });
            }
            storage = argument;
        } else {
            auto value = callable_copy_in(actual.direction)
                ? lower_hir_expression(
                      actual.expression, actual.width)
                : std::optional<RegisterId> { };
            if (callable_copy_in(actual.direction) && !value) {
                return std::nullopt;
            }
            storage = allocate_register(actual.width, actual.domain);
            if (value) {
                if (register_width(*value) != actual.width) {
                    value = resize_register(
                        *value, actual.width, actual.signed_value);
                }
                process_.operations.emplace_back(CopyRegister {
                    storage, *value });
            } else {
                process_.operations.emplace_back(LoadConstant {
                    storage,
                    callable_default_value(actual.width, actual.domain),
                });
            }
        }
        actuals.push_back(storage);
        actual_names.push_back(actual.name);
        actual_directions.push_back(
            static_cast<std::uint8_t>(actual.direction));
        actual_kinds.push_back(string ? 1U : 0U);
    }
    emit_debug_point(
        DebugPointKind::call,
        hir_source_span(expression->systemverilog->source));
    auto destination = allocate_register(
        profile->result_width, profile->result_domain);
    process_.operations.emplace_back(ClassStaticMethodCall {
        destination,
        std::string { systemverilog_dpi_function_prefix }
            + profile->linkage_name,
        actuals,
        actual_names,
        actual_directions,
        static_cast<std::uint32_t>(profile->result_width),
        actual_kinds,
    });
    for (std::size_t index = 0U;
        index < profile->actuals.size(); ++index) {
        const auto& actual = profile->actuals[index];
        if (!class_method_copy_out(actual.direction)) {
            continue;
        }
        const auto copied = actual.domain == frontend::ValueDomain::String
            ? lower_hir_string_copy_out(
                  actual.expression, actuals[index])
            : lower_hir_packed_copy_out(
                  actual.expression, actuals[index]);
        if (!copied) {
            return std::nullopt;
        }
    }
    if (expected_width != 0U
        && expected_width != profile->result_width) {
        destination = resize_register(
            destination, expected_width, profile->result_signed);
    }
    return destination;
}

std::optional<RegisterId> Lowerer::lower_hir_function_call(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    const auto result = lower_hir_function_call_value(
        expression_id, expected_width);
    return result ? result->packed : std::nullopt;
}

std::optional<Lowerer::HirStaticContainerValue>
Lowerer::lower_hir_container_function_call(
    const semantic::ExpressionId expression_id)
{
    const auto result = lower_hir_function_call_value(expression_id, 0U);
    return result ? result->container : std::nullopt;
}

std::optional<StringRegisterId> Lowerer::lower_hir_string_function_call(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (expression && expression->vhdl != nullptr
        && expression->vhdl->kind == semantic::vhdl::ExpressionKind::call
        && expression->vhdl->referenced_name
        && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2008
        && [&] {
               const auto& source = *expression->vhdl;
               const auto& name = *source.referenced_name;
               const auto spelling = lowercase_ascii_copy(
                   name.canonical.empty() ? name.spelling : name.canonical);
               const auto separator = spelling.find_last_of(".:");
               const auto member = separator == std::string::npos
                   ? std::string_view { spelling }
                   : std::string_view { spelling }.substr(separator + 1U);
               if (member != "justify"
                   || !semantic::CompiledDesignResolver {
                           *specialized_hir_unit_, hir_generic_binding_frames_
                       }
                           .vhdl_standard_package_member_visible(
                               name, source.scope)) {
                   return false;
               }
               const auto resolution = resolve_hir_vhdl_function_call(
                   expression_id, hir_process_scope_, 0U);
               if (!resolution) {
                   const auto referenced = hir_referenced_declaration(
                       expression_id);
                   const auto declaration = referenced
                       ? specialized_hir_unit_->find_declaration(
                             *referenced)
                       : std::nullopt;
                   if (declaration && declaration->vhdl != nullptr
                       && declaration->vhdl->form
                           != semantic::vhdl::DeclarationForm::function) {
                       return false;
                   }
                   return true;
               }
               const auto declaration = specialized_hir_unit_->find_declaration(
                   resolution->body);
               if (!declaration || declaration->vhdl == nullptr
                   || !declaration->vhdl->scope.valid()) {
                   return false;
               }
               const auto& scopes = specialized_hir_unit_->design()
                                        .semantics.scopes();
               const auto scope = std::ranges::find(
                   scopes, declaration->vhdl->scope, &semantic::Scope::id);
               if (scope == scopes.end()) {
                   return false;
               }
               const auto owner = specialized_hir_unit_->design().find_unit(
                   scope->unit);
               return owner && owner->identity != nullptr
                   && owner->vhdl != nullptr
                   && owner->identity->kind
                       == semantic::UnitKind::vhdl_package
                   && owner->vhdl->kind
                       == semantic::vhdl::UnitKind::package
                   && lowercase_ascii_copy(owner->identity->library) == "std"
                   && lowercase_ascii_copy(owner->identity->name) == "textio";
           }()) {
        const auto& source = *expression->vhdl;
        std::vector<std::optional<semantic::ExpressionId>> actuals(3U);
        std::size_t positional_index { };
        if (source.operands.empty() || source.operands.size() > actuals.size()
            || (!source.argument_names.empty()
                && source.argument_names.size() != source.operands.size())) {
            return std::nullopt;
        }
        for (std::size_t index { }; index < source.operands.size(); ++index) {
            std::size_t parameter { };
            const auto argument_name = source.argument_names.empty()
                ? std::string { }
                : lowercase_ascii_copy(source.argument_names[index]);
            if (argument_name.empty()) {
                while (positional_index < actuals.size()
                    && actuals[positional_index]) {
                    ++positional_index;
                }
                parameter = positional_index++;
            } else if (argument_name == "value") {
                parameter = 0U;
            } else if (argument_name == "justified") {
                parameter = 1U;
            } else if (argument_name == "field") {
                parameter = 2U;
            } else {
                return std::nullopt;
            }
            if (parameter >= actuals.size() || actuals[parameter]) {
                return std::nullopt;
            }
            actuals[parameter] = source.operands[index];
        }
        if (!actuals[0]
            || !hir_expression_is_string(*actuals[0], hir_process_scope_)) {
            return std::nullopt;
        }
        bool left_justify { };
        if (actuals[1]) {
            const auto side = specialized_hir_unit_->find_expression(
                *actuals[1]);
            if (!side || side->vhdl == nullptr) {
                return std::nullopt;
            }
            auto ordinal = hir_vhdl_standard_enumeration_literal(
                *actuals[1]);
            if (!ordinal) {
                const auto evaluated = specialized_hir_unit_
                    ->evaluate_integral_expression(*actuals[1]);
                if (evaluated && *evaluated >= 0) {
                    ordinal = static_cast<std::uint64_t>(*evaluated);
                }
            }
            if (!ordinal) {
                const auto is_compiler_owned_side_type = [&](
                    const semantic::vhdl::SubtypeIndication& subtype) {
                    const auto type_id = subtype.type_mark.target;
                    if (!type_id.valid()) {
                        auto spelling = std::string_view {
                            subtype.type_mark.spelling
                        };
                        constexpr auto builtin_prefix
                            = std::string_view { "@builtin:" };
                        if (spelling.starts_with(builtin_prefix)) {
                            spelling.remove_prefix(builtin_prefix.size());
                        }
                        const auto separator = spelling.find_last_of(".:");
                        const auto member = separator == std::string_view::npos
                            ? spelling
                            : spelling.substr(separator + 1U);
                        if (!same_callable_name(member, "side", true)) {
                            return false;
                        }
                        const semantic::CompiledDesignResolver resolver {
                            *specialized_hir_unit_,
                            hir_generic_binding_frames_
                        };
                        semantic::vhdl::Name type_name;
                        type_name.spelling = std::string { spelling };
                        type_name.canonical = lowercase_ascii_copy(spelling);
                        if (!resolver.resolve_vhdl(
                                type_name, hir_process_scope_).candidates.empty()) {
                            return false;
                        }
                        if (separator == std::string_view::npos) {
                            return resolver.vhdl_builtin_package_member_imported(
                                "std", "textio", "side", hir_process_scope_);
                        }
                        const auto package_separator = spelling.find_last_of(
                            ".:", separator - 1U);
                        if (package_separator == std::string_view::npos
                            || !same_callable_name(
                                spelling.substr(0U, package_separator),
                                "std", true)
                            || !same_callable_name(
                                spelling.substr(
                                    package_separator + 1U,
                                    separator - package_separator - 1U),
                                "textio", true)) {
                            return false;
                        }
                        return resolver.vhdl_builtin_type_visible(
                            subtype.type_mark.spelling, hir_process_scope_);
                    }
                    const auto type
                        = specialized_hir_unit_->find_type(type_id);
                    if (!type || type->vhdl == nullptr
                        || type->vhdl->form
                            != semantic::vhdl::TypeForm::enumeration
                        || lowercase_ascii_copy(type->vhdl->name) != "side"
                        || !type->vhdl->declaration.valid()) {
                        return false;
                    }
                    const auto declaration
                        = specialized_hir_unit_->find_declaration(
                            type->vhdl->declaration);
                    if (!declaration || declaration->vhdl == nullptr
                        || !declaration->vhdl->scope.valid()) {
                        return false;
                    }
                    const auto& scopes = specialized_hir_unit_->design()
                                             .semantics.scopes();
                    const auto scope = std::ranges::find(
                        scopes, declaration->vhdl->scope,
                        &semantic::Scope::id);
                    if (scope == scopes.end()) {
                        return false;
                    }
                    const auto owner = specialized_hir_unit_->design()
                                           .find_unit(scope->unit);
                    return owner && owner->identity != nullptr
                        && owner->vhdl != nullptr
                        && owner->identity->kind
                            == semantic::UnitKind::vhdl_package
                        && owner->vhdl->kind
                            == semantic::vhdl::UnitKind::package
                        && lowercase_ascii_copy(owner->identity->library)
                            == "std"
                        && lowercase_ascii_copy(owner->identity->name)
                            == "textio";
                };
                const auto declaration_id = hir_referenced_declaration(
                    *actuals[1]);
                const auto declaration = declaration_id
                    ? specialized_hir_unit_->find_declaration(
                          *declaration_id)
                    : std::nullopt;
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->form
                        == semantic::vhdl::DeclarationForm::constant
                    && declaration->vhdl->initializer
                    && declaration->vhdl->subtype) {
                    const auto subtype = hir_effective_vhdl_subtype(
                        *declaration->vhdl->subtype);
                    if (subtype && is_compiler_owned_side_type(*subtype)) {
                        ordinal = hir_vhdl_standard_enumeration_literal(
                            *declaration->vhdl->initializer);
                    }
                }
            }
            if (!ordinal || *ordinal > 1U) {
                return std::nullopt;
            }
            left_justify = *ordinal == 1U;
        }
        std::uint32_t field { };
        if (actuals[2]) {
            const auto field_value = specialized_hir_unit_
                ->evaluate_integral_expression(*actuals[2]);
            if (!field_value || *field_value < 0
                || static_cast<std::uint64_t>(*field_value)
                    > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            field = static_cast<std::uint32_t>(*field_value);
        }
        const auto value = lower_hir_string_expression(*actuals[0]);
        if (!value) {
            return std::nullopt;
        }
        const auto destination = allocate_string_register();
        StringMethod operation;
        operation.operation = StringMethodOperator::format_string;
        operation.source = destination;
        operation.argument = *value;
        operation.minimum_width = field;
        operation.left_justify = left_justify;
        process_.operations.emplace_back(operation);
        return destination;
    }
    const auto callable = hir_referenced_declaration(expression_id);
    auto interface_callable = expression
            && expression->systemverilog != nullptr
        ? hir_interface_function_profile(expression_id, hir_process_scope_)
        : std::nullopt;
    if (interface_callable && hir_interface_receiver_override_) {
        interface_callable->receiver = *std::exchange(
            hir_interface_receiver_override_, std::nullopt);
    } else if (interface_callable) {
        const auto dispatch
            = hir_virtual_interface_instances(interface_callable->receiver);
        if (hierarchical_reference_retry_ && hierarchical_reference_missed_) {
            // Child storage is not available in the first hierarchy pass.
            // Defer this call instead of using a receiver-independent frame.
            return std::nullopt;
        }
        if (dispatch) {
            const auto handle
                = lower_hir_virtual_interface_handle(dispatch->first);
            if (!handle) {
                return std::nullopt;
            }
            const auto result = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant { result, { } });
            std::vector<InstructionIndex> exits;
            for (const auto& [identity, path] : dispatch->second) {
                const auto constant = allocate_register(
                    64U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    constant, unsigned_value(identity, 64U) });
                const auto selected = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::case_equal, selected, *handle,
                    constant });
                const auto branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(Branch {
                    selected, branch + 1U, 0U,
                    UnknownBranchPolicy::when_false });
                hir_interface_receiver_override_ = "@instance:" + path;
                const auto value = lower_hir_string_function_call(expression_id);
                hir_interface_receiver_override_.reset();
                if (!value) {
                    return std::nullopt;
                }
                process_.operations.emplace_back(CopyStringRegister {
                    result, *value });
                exits.push_back(static_cast<InstructionIndex>(
                    process_.operations.size()));
                process_.operations.emplace_back(Jump { });
                process_.operations[branch] = Branch {
                    selected, branch + 1U,
                    static_cast<InstructionIndex>(process_.operations.size()),
                    UnknownBranchPolicy::when_false };
            }
            for (const auto exit : exits) {
                process_.operations[exit] = Jump {
                    static_cast<InstructionIndex>(process_.operations.size()) };
            }
            return result;
        }
    }
    const auto resolution = expression && expression->vhdl != nullptr
        ? resolve_hir_vhdl_function_call(
              expression_id, hir_process_scope_, 0U)
        : callable ? hir_callable_resolution(*callable)
        : interface_callable
        ? hir_callable_resolution(interface_callable->declaration)
        : std::nullopt;
    if (!resolution) {
        return std::nullopt;
    }
    hir_generic_binding_frames_.push_back(
        resolution->generic_bindings);
    const PopBackGuard generic_scope { hir_generic_binding_frames_ };
    const auto type = hir_callable_type(resolution->body);
    const auto actuals = bind_hir_function_actuals(
        expression_id, resolution->body);
    const auto declaration = specialized_hir_unit_->find_declaration(
        resolution->body);
    const auto scope = declaration
        ? declaration->systemverilog != nullptr
            ? declaration->systemverilog->nested_scope
            : declaration->vhdl->nested_scope
        : std::nullopt;
    if (!type || !type->string || !actuals || !declaration
        || !scope
        || (declaration->systemverilog != nullptr
            && !declaration->systemverilog->callable)
        || (declaration->vhdl != nullptr
            && !declaration->vhdl->callable)) {
        return std::nullopt;
    }
    const auto formals = declaration->systemverilog != nullptr
        ? std::vector<semantic::DeclarationId> {
              declaration->systemverilog->callable->formals.begin(),
              declaration->systemverilog->callable->formals.end(),
          }
        : std::vector<semantic::DeclarationId> {
              declaration->vhdl->callable->formals.begin(),
              declaration->vhdl->callable->formals.end(),
          };
    if (formals.size() != actuals->size()) {
        return std::nullopt;
    }

    const auto callable_key = std::uint64_t { 1 } << 63U
        | (static_cast<std::uint64_t>(resolution->key.value()) << 32U);
    const auto callable_key_text = std::to_string(callable_key)
        + (interface_callable ? "|receiver:" + interface_callable->receiver : "");
    const auto found = hir_callable_indices_.find(callable_key_text);
    const auto frame_index = found != hir_callable_indices_.end()
        ? found->second
        : hir_callable_frames_.size();
    if (found == hir_callable_indices_.end()) {
        hir_callable_indices_.emplace(callable_key_text, frame_index);
        HirCallableFrame frame;
        frame.declaration = resolution->body;
        frame.scope = *scope;
        frame.type = *type;
        frame.formals = formals;
        frame.generic_bindings = resolution->generic_bindings;
        if (interface_callable) {
            frame.interface_receiver = interface_callable->receiver;
            bind_hir_instance_parameters(frame);
        }
        const auto debug_declaration
            = specialized_hir_unit_->find_declaration(resolution->key);
        frame.debug_name = debug_declaration
                && debug_declaration->systemverilog != nullptr
            ? debug_declaration->systemverilog->name
            : debug_declaration && debug_declaration->vhdl != nullptr
            ? debug_declaration->vhdl->name
            : std::string { };
        frame.string_result = allocate_string_register();
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (frame.invocation_identity == 0U) {
            frame.invocation_identity = next_callable_invocation_identity_++;
        }
        frame.invocation_strings.push_back(frame.string_result);
        for (std::size_t index { }; index < formals.size(); ++index) {
            const auto formal = formals[index];
            const auto record = specialized_hir_unit_->find_declaration(
                formal);
            const auto direction = record
                ? callable_direction(*record)
                : frontend::PortDirection::Unknown;
            if (direction == frontend::PortDirection::Unknown) {
                return std::nullopt;
            }
            const auto string = [&] {
                if (record->systemverilog != nullptr) {
                    return record->systemverilog->type
                        && record->systemverilog->type->value_form
                            == semantic::sv::TypeForm::string;
                }
                if (record->vhdl == nullptr
                    || !record->vhdl->subtype) {
                    return false;
                }
                const auto subtype = hir_effective_vhdl_subtype(
                    *record->vhdl->subtype);
                return subtype
                    && subtype->domain
                        == semantic::vhdl::ValueDomain::string;
            }();
            const auto container = record->systemverilog != nullptr
                    && record->systemverilog->type
                ? hir_systemverilog_container_type(
                      *record->systemverilog->type)
                : std::nullopt;
            frame.argument_is_string.push_back(string);
            frame.argument_is_container.push_back(
                container.has_value());
            if (container) {
                frame.arguments.push_back({ });
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back(
                    allocate_container_register(*container));
                frame.container_output_defaults.push_back(
                    direction == frontend::PortDirection::Output
                    ? std::optional<ContainerRegisterId> {
                          allocate_container_register(*container) }
                    : std::nullopt);
                frame.invocation_containers.push_back(
                    frame.container_arguments.back());
            } else if (string) {
                frame.arguments.push_back({ });
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.container_arguments.push_back({ });
                frame.container_output_defaults.push_back(std::nullopt);
                frame.invocation_strings.push_back(
                    frame.string_arguments.back());
            } else {
                const auto binding = hir_callable_formal_binding(
                    formal, *scope, (*actuals)[index],
                    hir_process_scope_);
                if (!binding) {
                    return std::nullopt;
                }
                frame.arguments.push_back(allocate_register(
                    binding->width, binding->domain));
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back({ });
                frame.container_output_defaults.push_back(std::nullopt);
                frame.invocation_registers.push_back(
                    frame.arguments.back());
            }
            frame.directions.push_back(direction);
        }
        frame.allocated = true;
        hir_callable_frames_.push_back(std::move(frame));
        if (!allocate_hir_static_callable_declarations(frame_index)) {
            return std::nullopt;
        }
    }

    std::vector<std::optional<RegisterId>> lowered_actuals(
        actuals->size());
    std::vector<std::optional<StringRegisterId>> lowered_string_actuals(
        actuals->size());
    std::vector<std::optional<ContainerRegisterId>>
        lowered_container_actuals(actuals->size());
    for (std::size_t index { }; index < actuals->size(); ++index) {
        const auto& frame = hir_callable_frames_[frame_index];
        if (frame.argument_is_container[index]) {
            const auto formal_type
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::copy_at(
                    process_.container_register_types,
                    frame.container_arguments[index]);
            lowered_container_actuals[index]
                = allocate_container_register(formal_type);
            if (callable_copy_in(frame.directions[index])) {
                const auto value = lower_hir_static_container_actual(
                    (*actuals)[index], formal_type, formals[index]);
                if (!value) {
                    return std::nullopt;
                }
                process_.operations.emplace_back(CopyContainerRegister {
                    *lowered_container_actuals[index], *value });
            }
            continue;
        }
        if (!callable_copy_in(frame.directions[index])) {
            continue;
        }
        if (frame.argument_is_string[index]) {
            lowered_string_actuals[index] = lower_hir_string_expression(
                (*actuals)[index]);
            if (!lowered_string_actuals[index]) {
                return std::nullopt;
            }
            continue;
        }
        const auto formal = hir_callable_formal_binding(
            formals[index], *scope, (*actuals)[index],
            hir_process_scope_);
        if (!formal) {
            return std::nullopt;
        }
        lowered_actuals[index] = lower_hir_expression(
            (*actuals)[index], formal->width);
        if (!lowered_actuals[index]) {
            return std::nullopt;
        }
        if (register_width(*lowered_actuals[index]) != formal->width) {
            lowered_actuals[index] = resize_register(
                *lowered_actuals[index], formal->width,
                hir_actual_extension_signed(
                (*actuals)[index], formal->signed_value));
        }
        if (is_two_state_domain(formal->domain)) {
            // A 2-state formal receives X and Z bits as 0 (IEEE 1800-2017
            // 6.24.1 by way of 13.3).
            lowered_actuals[index]
                = convert_to_two_state(*lowered_actuals[index]);
        }
    }

    const auto call_expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!call_expression) {
        return std::nullopt;
    }
    const auto call_source = call_expression->systemverilog != nullptr
        ? call_expression->systemverilog->source
        : call_expression->vhdl->source;
    emit_debug_point(
        DebugPointKind::call, hir_source_span(call_source));
    auto& frame = hir_callable_frames_[frame_index];
    if (frame.type.automatic) {
        const auto push_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CallableFramePush {
            frame.invocation_identity,
            frame.invocation_registers,
            frame.invocation_strings,
            frame.invocation_containers,
            true,
        });
        if (!frame.snapshot_complete) {
            frame.invocation_push_sites.push_back(push_site);
        }
    }
    process_.operations.emplace_back(LoadStringConstant {
        frame.string_result, { } });
    for (std::size_t index { }; index < actuals->size(); ++index) {
        if (frame.argument_is_container[index]) {
            if (frame.directions[index]
                == frontend::PortDirection::Output) {
                if (!frame.container_output_defaults[index]) {
                    return std::nullopt;
                }
                process_.operations.emplace_back(CopyContainerRegister {
                    frame.container_arguments[index],
                    *frame.container_output_defaults[index],
                });
            } else if (lowered_container_actuals[index]) {
                process_.operations.emplace_back(CopyContainerRegister {
                    frame.container_arguments[index],
                    *lowered_container_actuals[index],
                });
            } else {
                return std::nullopt;
            }
        } else if (frame.argument_is_string[index]) {
            if (lowered_string_actuals[index]) {
                process_.operations.emplace_back(CopyStringRegister {
                    frame.string_arguments[index],
                    *lowered_string_actuals[index],
                });
            } else {
                process_.operations.emplace_back(LoadStringConstant {
                    frame.string_arguments[index], { } });
            }
        } else if (lowered_actuals[index]) {
            process_.operations.emplace_back(CopyRegister {
                frame.arguments[index], *lowered_actuals[index] });
        } else {
            const auto formal = hir_callable_formal_binding(
                formals[index], *scope, (*actuals)[index],
                hir_process_scope_);
            if (!formal) {
                return std::nullopt;
            }
            process_.operations.emplace_back(LoadConstant {
                frame.arguments[index],
                callable_default_value(formal->width, formal->domain),
            });
        }
    }
    transfer_hir_static_formals(frame, true);
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0U),
        static_cast<InstructionIndex>(call_site + 1U),
        { },
    });
    transfer_hir_static_formals(frame, false);
    if (!frame.target) {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_hir_callables_.push_back(frame_index);
    }

    std::vector<std::optional<StringRegisterId>> string_copy_outs(
        actuals->size());
    std::vector<ContainerRegisterId> preserve_containers;
    for (std::size_t index { }; index < actuals->size(); ++index) {
        if (!class_method_copy_out(frame.directions[index])) {
            continue;
        }
        if (frame.argument_is_container[index]) {
            if (!lowered_container_actuals[index]) {
                return std::nullopt;
            }
            process_.operations.emplace_back(CopyContainerRegister {
                *lowered_container_actuals[index],
                frame.container_arguments[index],
            });
            preserve_containers.push_back(
                *lowered_container_actuals[index]);
        } else if (frame.argument_is_string[index]) {
            string_copy_outs[index] = allocate_string_register();
            process_.operations.emplace_back(CopyStringRegister {
                *string_copy_outs[index],
                frame.string_arguments[index],
            });
        } else if (!lower_hir_packed_copy_out(
                       (*actuals)[index], frame.arguments[index])) {
            return std::nullopt;
        }
    }
    const auto destination = allocate_string_register();
    process_.operations.emplace_back(CopyStringRegister {
        destination, frame.string_result });
    std::vector<StringRegisterId> preserve_strings { destination };
    for (const auto copy_out : string_copy_outs) {
        if (copy_out) {
            preserve_strings.push_back(*copy_out);
        }
    }
    if (frame.type.automatic) {
        process_.operations.emplace_back(CallableFramePop {
            frame.invocation_identity,
            { },
            std::move(preserve_strings),
            std::move(preserve_containers),
        });
    }
    for (std::size_t index { }; index < string_copy_outs.size(); ++index) {
        if (frame.argument_is_container[index]
            && class_method_copy_out(frame.directions[index])) {
            if (!lowered_container_actuals[index]
                || !lower_hir_container_copy_out(
                    (*actuals)[index],
                    *lowered_container_actuals[index])) {
                return std::nullopt;
            }
            continue;
        }
        if (string_copy_outs[index]
            && !lower_hir_string_copy_out(
                (*actuals)[index], *string_copy_outs[index])) {
            return std::nullopt;
        }
    }
    return destination;
}

std::string Lowerer::hir_vhdl_std_env_procedure_spelling(
    const semantic::vhdl::Name& name,
    const semantic::ScopeId use_scope) const
{
    // Without VHDL-2019 the STD.ENV package has no declarations, so a
    // procedure imported by `use std.env.all` resolves to nothing; name it
    // by its selected spelling for the simulator API checks.
    if (specialized_hir_unit_ == nullptr || name.spelling.empty()
        || name.spelling.find_first_of(".:") != std::string::npos
        || !hir_vhdl_callable_candidates(name, use_scope).empty()) {
        return name.spelling;
    }
    std::string member;
    member.reserve(name.spelling.size());
    for (const auto character : name.spelling) {
        member.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    if (!semantic::CompiledDesignResolver {
            *specialized_hir_unit_, hir_generic_binding_frames_ }
            .vhdl_builtin_package_member_imported(
                "std", "env", member, use_scope)) {
        return name.spelling;
    }
    return "std.env." + member;
}

bool Lowerer::lower_hir_vhdl_procedure_call(
    const semantic::StatementId statement_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    if (is_hir_vhdl_vital_delay_call(statement_id)) {
        return lower_hir_vhdl_vital_delay_call(statement_id);
    }
    if (is_hir_vhdl_vital_state_table_call(statement_id)) {
        return lower_hir_vhdl_vital_state_table_call(statement_id);
    }
    if (is_hir_vhdl_vital_timing_call(statement_id)) {
        return lower_hir_vhdl_vital_timing_call(statement_id);
    }
    if (is_hir_vhdl_file_statement(statement_id)) {
        return lower_hir_vhdl_file_statement(statement_id);
    }
    if (is_hir_vhdl_access_deallocation(statement_id)) {
        return lower_hir_vhdl_access_deallocation(statement_id);
    }
    if (is_hir_vhdl_protected_statement(statement_id)) {
        return lower_hir_vhdl_protected_statement(statement_id);
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr) {
        return false;
    }
    if (is_hir_vhdl_environment_directory_statement(statement_id)) {
        return lower_hir_vhdl_environment_directory_statement(
            statement_id);
    }
    if (is_hir_vhdl_assert_statement(statement_id)) {
        return lower_hir_vhdl_assert_statement(statement_id);
    }
    const auto procedure_spelling = hir_vhdl_std_env_procedure_spelling(
        statement->vhdl->procedure, statement->vhdl->scope);
    const auto set_psl_cover_assert = same_callable_name(
        procedure_spelling, "std.env.setpslcoverassert", true);
    const auto clear_psl_state = same_callable_name(
        procedure_spelling, "std.env.clearpslstate", true);
    const auto stop = same_callable_name(
        procedure_spelling, "std.env.stop", true);
    const auto finish = same_callable_name(
        procedure_spelling, "std.env.finish", true);
    if (set_psl_cover_assert || clear_psl_state || stop || finish) {
        const auto& associations = statement->vhdl->procedure_arguments;
        // STOP and FINISH are VHDL-2008 (IEEE 1076-2008 16.5); the PSL
        // state controls arrived in VHDL-2019.
        if (vhdl_standard_ < ((stop || finish)
                    ? frontend::VhdlStandard::Vhdl2008
                    : frontend::VhdlStandard::Vhdl2019)) {
            report(
                "FSIM-ELAB-VHENV-001",
                "this STD.ENV simulator API requires a later VHDL revision",
                hir_source_span(statement->vhdl->source));
            return true;
        }
        if (clear_psl_state) {
            if (!associations.empty()) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV CLEARPSLSTATE does not accept arguments",
                    hir_source_span(statement->vhdl->source));
                return true;
            }
            process_.operations.emplace_back(VhdlPslApi {
                VhdlPslApiKind::clear_state,
                std::nullopt,
                std::nullopt,
            });
            return true;
        }
        if (stop || finish) {
            const auto named_status = [](const auto& association) {
                if (!association.formal) {
                    return true;
                }
                const auto& formal = *association.formal;
                const auto spelling = formal.canonical.empty()
                    ? std::string_view { formal.spelling }
                    : std::string_view { formal.canonical };
                return same_callable_name(spelling, "status", true);
            };
            if (associations.size() > 1U
                || !std::ranges::all_of(associations, named_status)) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV STOP and FINISH accept at most one INTEGER "
                    "STATUS actual",
                    hir_source_span(statement->vhdl->source));
                return true;
            }
            std::optional<RegisterId> status;
            if (!associations.empty()) {
                const auto& actual = associations.front();
                const auto width = hir_expression_width(
                    actual.actual, hir_process_scope_).value_or(32U);
                status = lower_hir_expression(actual.actual, width);
                if (!status) {
                    return false;
                }
                if (register_width(*status) != width) {
                    status = resize_register(*status, width, true);
                }
            }
            if (stop) {
                process_.operations.emplace_back(Pause { status });
            } else {
                process_.operations.emplace_back(Stop { status });
            }
            return true;
        }
        const auto named_enable = [](const auto& association) {
            if (!association.formal) {
                return true;
            }
            const auto& formal = *association.formal;
            const auto spelling = formal.canonical.empty()
                ? std::string_view { formal.spelling }
                : std::string_view { formal.canonical };
            return same_callable_name(spelling, "enable", true);
        };
        if (associations.size() > 1U
            || !std::ranges::all_of(associations, named_enable)) {
            report(
                "FSIM-ELAB-VHENV-003",
                "STD.ENV SETPSLCOVERASSERT accepts at most one BOOLEAN "
                "ENABLE actual",
                hir_source_span(statement->vhdl->source));
            return true;
        }
        std::optional<RegisterId> enable;
        if (associations.empty()) {
            enable = allocate_register(1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(LoadConstant {
                *enable, PackedLogic4::from_aval_bval(1U, 1U, 0U) });
        } else {
            enable = lower_hir_expression(associations.front().actual, 1U);
            if (!enable) {
                return false;
            }
            if (register_width(*enable) != 1U
                || register_domain(*enable)
                    != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-VHENV-004",
                    "STD.ENV SETPSLCOVERASSERT ENABLE must be BOOLEAN",
                    hir_source_span(associations.front().source));
                return true;
            }
        }
        process_.operations.emplace_back(VhdlPslApi {
            VhdlPslApiKind::set_cover_assert,
            std::nullopt,
            enable,
        });
        return true;
    }
    const auto resolution = resolve_hir_vhdl_procedure_call(
        statement_id, hir_process_scope_);
    if (!resolution) {
        const auto candidates = hir_vhdl_callable_candidates(
            statement->vhdl->procedure,
            statement->vhdl->scope);
        if (candidates.size() > 1U) {
            report(
                "FSIM-ELAB-VHOVER-004",
                "VHDL procedure call ambiguously matches visible overloads",
                hir_source_span(statement->vhdl->source));
            return true;
        }
        if (candidates.size() == 1U) {
            const auto declaration
                = specialized_hir_unit_->find_declaration(
                    candidates.front());
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->callable
                && declaration->vhdl->callable->function) {
                report(
                    "FSIM-ELAB-VHPROC-014",
                    "VHDL procedure call names no visible procedure",
                    hir_source_span(statement->vhdl->source));
                return true;
            }
            report(
                "FSIM-ELAB-VHOVER-005",
                "VHDL procedure call has no matching overload",
                hir_source_span(statement->vhdl->source));
            return true;
        }
        // No visible subprogram has this name (IEEE 1076-2008 10.7).
        report(
            "FSIM-ELAB-VHPROC-014",
            "VHDL procedure call names no visible procedure '"
                + statement->vhdl->procedure.spelling + "'",
            hir_source_span(statement->vhdl->source));
        return true;
    }
    hir_generic_binding_frames_.push_back(
        resolution->generic_bindings);
    const PopBackGuard generic_scope { hir_generic_binding_frames_ };
    const auto declaration = specialized_hir_unit_->find_declaration(
        resolution->body);
    const auto actuals = bind_hir_vhdl_procedure_actuals(
        statement_id, resolution->body);
    if (!declaration || declaration->vhdl == nullptr
        || !actuals || !declaration->vhdl->callable
        || !declaration->vhdl->nested_scope) {
        return false;
    }
    const auto scope = *declaration->vhdl->nested_scope;
    const auto formals = std::vector<semantic::DeclarationId> {
        declaration->vhdl->callable->formals.begin(),
        declaration->vhdl->callable->formals.end(),
    };
    // A signal-class formal denotes its actual signal (IEEE 1076-2008
    // 4.2.2.3): reads, attributes, waits and drivers act on the actual.
    std::vector<std::optional<HirRuntimeBinding>> signal_actuals(
        formals.size());
    std::string signal_actual_key;
    for (std::size_t index { }; index < formals.size(); ++index) {
        const auto formal = specialized_hir_unit_->find_declaration(
            formals[index]);
        if (!formal || formal->vhdl == nullptr
            || formal->vhdl->object_class
                != semantic::vhdl::ObjectClass::signal) {
            continue;
        }
        const auto actual = specialized_hir_unit_->find_expression(
            (*actuals)[index]);
        const auto target = actual && actual->vhdl != nullptr
                && actual->vhdl->kind == semantic::vhdl::ExpressionKind::name
            ? hir_target_declaration((*actuals)[index])
            : std::nullopt;
        auto binding = target
            ? hir_runtime_binding(*target, hir_process_scope_, false)
            : std::nullopt;
        if (!binding || binding->kind != HirRuntimeBindingKind::signal
            || !binding->signal) {
            report(
                "FSIM-ELAB-VHPROC-022",
                "signal-class procedure formal '" + formal->vhdl->name
                    + "' requires a whole signal name actual",
                actual && actual->vhdl != nullptr
                    ? hir_source_span(actual->vhdl->source)
                    : hir_source_span(statement->vhdl->source));
            return true;
        }
        signal_actual_key += "|s" + std::to_string(index) + ":"
            + std::to_string(*binding->signal);
        signal_actuals[index] = std::move(binding);
    }
    for (std::size_t index { }; index < formals.size(); ++index) {
        if (signal_actuals[index]) {
            continue;
        }
        const auto formal = specialized_hir_unit_->find_declaration(
            formals[index]);
        if (!formal || formal->vhdl == nullptr) {
            return false;
        }
        const auto direction = callable_direction(*formal);
        const auto requires_writable
            = formal->vhdl->object_class
                    == semantic::vhdl::ObjectClass::variable
            || formal->vhdl->object_class
                    == semantic::vhdl::ObjectClass::file
            || class_method_copy_out(direction);
        if (!requires_writable) {
            continue;
        }
        const auto actual = specialized_hir_unit_->find_expression(
            (*actuals)[index]);
        const auto target = hir_target_declaration((*actuals)[index]);
        const auto target_binding = target
            ? hir_runtime_binding(*target, hir_process_scope_, false)
            : std::nullopt;
        const auto string_target = target
            ? hir_string_binding(*target, hir_process_scope_, true)
            : std::nullopt;
        const auto writable = (target_binding
            && (target_binding->kind == HirRuntimeBindingKind::local
                || signal_binding_is_writable(*target_binding)))
            || (string_target && string_target->local);
        if (actual && actual->vhdl != nullptr && writable) {
            continue;
        }
        // Individually associated subelements (IEEE 1076-2008 6.5.7.1).
        if (actual && actual->vhdl != nullptr
            && actual->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate
            && hir_vhdl_aggregate_is_writable((*actuals)[index])) {
            continue;
        }
        report(
            "FSIM-ELAB-VHPROC-018",
            "variable-class, output, and inout procedure formal '"
                + formal->vhdl->name
                + "' requires a writable signal or variable actual",
            actual && actual->vhdl != nullptr
                ? hir_source_span(actual->vhdl->source)
                : hir_source_span(statement->vhdl->source));
        return true;
    }

    // STRING formals are passed in a dynamic string register. An out or
    // inout string formal (a std.textio LINE) copies back to its actual's
    // string variable.
    std::vector<bool> string_formals(formals.size());
    std::vector<std::optional<StringRegisterId>> string_copy_out(
        formals.size());
    for (std::size_t index { }; index < formals.size(); ++index) {
        if (signal_actuals[index]) {
            continue;
        }
        const auto formal = specialized_hir_unit_->find_declaration(
            formals[index]);
        if (!formal || formal->vhdl == nullptr || !formal->vhdl->subtype) {
            continue;
        }
        const auto subtype = hir_effective_vhdl_subtype(
            *formal->vhdl->subtype);
        if (subtype
            && subtype->domain == semantic::vhdl::ValueDomain::string) {
            if (callable_direction(*formal)
                != frontend::PortDirection::Input) {
                const auto target = hir_target_declaration(
                    (*actuals)[index]);
                const auto string_target = target
                    ? hir_string_binding(*target, hir_process_scope_, true)
                    : std::nullopt;
                if (string_target && string_target->local) {
                    string_copy_out[index] = *string_target->local;
                    string_formals[index] = true;
                    continue;
                }
                report(
                    "FSIM-ELAB-VHPROC-023",
                    "STRING procedure formal '" + formal->vhdl->name
                        + "' must have mode in",
                    hir_source_span(formal->vhdl->source));
                return true;
            }
            string_formals[index] = true;
        }
    }
    // A locally static input actual becomes a static binding of the frame
    // (see below), so each distinct value needs its own frame; otherwise a
    // later call would observe the first call's value.
    std::string constant_actual_key;
    for (std::size_t index { }; index < formals.size(); ++index) {
        if (signal_actuals[index]) {
            continue;
        }
        const auto formal = specialized_hir_unit_->find_declaration(
            formals[index]);
        if (!formal || formal->vhdl == nullptr
            || callable_direction(*formal)
                != frontend::PortDirection::Input) {
            continue;
        }
        if (const auto constant = hir_constant_integer((*actuals)[index])) {
            constant_actual_key += "|c" + std::to_string(index) + ":"
                + std::to_string(*constant);
        }
    }
    // An unconstrained array formal takes the index range of its actual
    // (IEEE 1076-2008 4.2.2.2): each distinct range needs its own frame.
    std::vector<std::pair<std::size_t, HirPackedRange>> procedure_ranges;
    std::vector<std::pair<std::size_t,
        std::vector<semantic::vhdl::ElementConstraint>>>
        procedure_element_constraints;
    std::string range_actual_key;
    for (std::size_t index { }; index < formals.size(); ++index) {
        if (signal_actuals[index]) {
            continue;
        }
        const auto formal = specialized_hir_unit_->find_declaration(formals[index]);
        const auto subtype = formal && formal->vhdl != nullptr && formal->vhdl->subtype
            ? hir_effective_vhdl_subtype(*formal->vhdl->subtype)
            : std::nullopt;
        if (!subtype || !subtype->unconstrained) {
            continue;
        }
        const auto actual_id = (*actuals)[index];
        auto range = hir_expression_range(actual_id, hir_process_scope_);
        const auto width = hir_expression_width(actual_id, hir_process_scope_);
        bool null_actual_range { };
        if (!range || (width && *width == 0U)) {
            // Runtime-bound array names retain the occurrence-specific
            // range. Expression-range queries can omit that range, and a
            // null array cannot recover it from its zero width alone.
            const auto actual_expression
                = specialized_hir_unit_->find_expression(actual_id);
            const auto actual_declaration
                = actual_expression && actual_expression->vhdl != nullptr
                    && actual_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::name
                ? hir_target_declaration(actual_id)
                : std::nullopt;
            const auto actual_binding = actual_declaration
                ? hir_runtime_binding(*actual_declaration,
                      hir_process_scope_, false)
                : std::nullopt;
            if (actual_binding && actual_binding->signal
                && *actual_binding->signal < design_.signal_info_.size()) {
                const auto* occurrence_array
                    = design_.signal_info_[*actual_binding->signal]
                          .vhdl_array.get();
                if (occurrence_array != nullptr
                    && !occurrence_array->dimensions.empty()) {
                    const auto& dimension
                        = occurrence_array->dimensions.front();
                    if (dimension.range) {
                        range = HirPackedRange {
                            dimension.range->left,
                            dimension.range->right,
                            dimension.range->descending,
                        };
                        null_actual_range = dimension.null;
                    }
                }
            }
            if (width && *width == 0U && !null_actual_range) {
                const auto actual_subtype
                    = hir_vhdl_expression_subtype(actual_id);
                const auto actual_effective = actual_subtype
                    ? hir_effective_vhdl_subtype(*actual_subtype)
                    : std::nullopt;
                if (actual_effective) {
                    const auto constraint = std::ranges::find_if(
                        actual_effective->constraints,
                        [](const semantic::vhdl::RangeConstraint& item) {
                            return item.kind
                                    == semantic::vhdl::RangeKind::array_index
                                && item.null;
                        });
                    if (constraint
                        != actual_effective->constraints.end()) {
                        const auto left = hir_range_left(*constraint);
                        const auto right = hir_range_right(*constraint);
                        if (left && right) {
                            const auto descending
                                = constraint->direction_from_bounds
                                    ? *left > *right
                                    : constraint->descending;
                            range = HirPackedRange {
                                *left, *right, descending };
                            null_actual_range = true;
                        }
                    }
                }
            }
        }
        if (!range || !width || (*width == 0U && !null_actual_range)) {
            continue;
        }
        const auto distance = index_distance(range->left, range->right);
        if (distance == std::numeric_limits<std::uint64_t>::max()
            || (null_actual_range
                ? *width != 0U
                : *width % (distance + 1U) != 0U
                    || (range->left != range->right
                        && range->descending != (range->left > range->right)))) {
            continue;
        }
        procedure_ranges.emplace_back(index, *range);
        range_actual_key += "|r" + std::to_string(index) + ":"
            + std::to_string(range->left) + ":" + std::to_string(range->right)
            + (range->descending ? "d" : "a");
        // An unbounded element takes the actual's element constraints.
        const auto actual_subtype
            = hir_vhdl_expression_subtype((*actuals)[index]);
        const auto actual_effective = actual_subtype
            ? hir_effective_vhdl_subtype(*actual_subtype)
            : std::nullopt;
        if (actual_effective
            && !actual_effective->element_constraints.empty()) {
            range_actual_key += "|e" + std::to_string(index) + ":"
                + std::to_string(*width / (distance + 1U));
            append_vhdl_element_constraint_key(
                range_actual_key, actual_effective->element_constraints);
            procedure_element_constraints.emplace_back(
                index, actual_effective->element_constraints);
        }
    }
    const auto callable_key = vhdl_procedure_callable_key(
        resolution->key);
    const auto callable_key_text = std::to_string(callable_key)
        + signal_actual_key + constant_actual_key + range_actual_key;
    const auto found = hir_callable_indices_.find(callable_key_text);
    const auto frame_index = found != hir_callable_indices_.end()
        ? found->second
        : hir_callable_frames_.size();
    if (found == hir_callable_indices_.end()) {
        hir_callable_indices_.emplace(callable_key_text, frame_index);
        HirCallableFrame frame;
        frame.declaration = resolution->body;
        frame.scope = scope;
        frame.type = {
            1U,
            frontend::ValueDomain::Bit2,
            false,
            true,
            false,
            std::nullopt,
        };
        frame.formals = formals;
        frame.generic_bindings = resolution->generic_bindings;
        const auto profile_declaration
            = specialized_hir_unit_->find_declaration(resolution->key);
        if (resolution->key != resolution->body
            && profile_declaration
            && profile_declaration->vhdl != nullptr
            && profile_declaration->vhdl->callable
            && profile_declaration->vhdl->callable->formals.size()
                == formals.size()) {
            frame.profile_formals.assign(
                profile_declaration->vhdl->callable->formals.begin(),
                profile_declaration->vhdl->callable->formals.end());
        }
        for (const auto& [index, range] : procedure_ranges) {
            const std::vector<std::optional<HirPackedRange>> dimensions {
                range,
            };
            frame.vhdl_formal_ranges.emplace(
                frame.formals[index].value(), dimensions);
            if (frame.profile_formals.size() == frame.formals.size()) {
                frame.vhdl_formal_ranges.emplace(
                    frame.profile_formals[index].value(), dimensions);
            }
        }
        for (const auto& [index, constraints] :
            procedure_element_constraints) {
            frame.vhdl_formal_element_constraints.emplace(
                frame.formals[index].value(), constraints);
            if (frame.profile_formals.size() == frame.formals.size()) {
                frame.vhdl_formal_element_constraints.emplace(
                    frame.profile_formals[index].value(), constraints);
            }
        }
        frame.debug_name = statement->vhdl->procedure.spelling;
        frame.function = false;
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (frame.invocation_identity == 0U) {
            frame.invocation_identity
                = next_callable_invocation_identity_++;
        }
        for (std::size_t index { }; index < formals.size(); ++index) {
            const auto formal = formals[index];
            frame.argument_is_string.push_back(string_formals[index]);
            if (string_formals[index]) {
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.invocation_strings.push_back(
                    frame.string_arguments.back());
                frame.arguments.push_back(allocate_register(
                    1U, frontend::ValueDomain::Bit2));
                frame.invocation_registers.push_back(
                    frame.arguments.back());
                frame.directions.push_back(
                    frontend::PortDirection::Input);
                continue;
            }
            frame.string_arguments.push_back({ });
            if (signal_actuals[index]) {
                // No frame storage: the formal names the actual signal. A
                // placeholder register keeps argument positions aligned.
                frame.signal_formals.emplace(
                    formal.value(), *signal_actuals[index]);
                frame.arguments.push_back(allocate_register(
                    1U, frontend::ValueDomain::Bit2));
                frame.invocation_registers.push_back(
                    frame.arguments.back());
                frame.directions.push_back(
                    frontend::PortDirection::Input);
                continue;
            }
            const auto record = specialized_hir_unit_->find_declaration(
                formal);
            const auto binding = hir_callable_formal_binding(
                formal, scope, (*actuals)[index], hir_process_scope_);
            const auto direction = record
                ? callable_direction(*record)
                : frontend::PortDirection::Unknown;
            if (!binding
                || binding->kind != HirRuntimeBindingKind::local
                || direction == frontend::PortDirection::Unknown
                || !callable_scalar_domain(binding->domain)) {
                return false;
            }
            if (record->vhdl != nullptr
                && direction == frontend::PortDirection::Input
                && hir_constant_integer((*actuals)[index])) {
                HirGenericBinding constant_actual;
                constant_actual.formal = formal;
                constant_actual.expression = (*actuals)[index];
                frame.generic_bindings.push_back(
                    std::move(constant_actual));
            }
            frame.arguments.push_back(allocate_register(
                binding->width, binding->domain));
            frame.invocation_registers.push_back(
                frame.arguments.back());
            frame.directions.push_back(direction);
        }
        frame.allocated = true;
        hir_callable_frames_.push_back(std::move(frame));
    }

    std::vector<std::optional<RegisterId>> lowered_actuals(
        actuals->size());
    std::vector<std::optional<StringRegisterId>> lowered_string_actuals(
        actuals->size());
    std::vector<std::optional<HirPackedUpdateTarget>>
        copy_out_targets(actuals->size());
    for (std::size_t index { }; index < actuals->size(); ++index) {
        if (signal_actuals[index]) {
            continue;
        }
        if (string_formals[index] && string_copy_out[index]) {
            lowered_string_actuals[index] = *string_copy_out[index];
            continue;
        }
        if (string_formals[index]) {
            lowered_string_actuals[index] = lower_hir_string_expression(
                (*actuals)[index]);
            if (!lowered_string_actuals[index]) {
                return false;
            }
            continue;
        }
        const auto formal = hir_callable_formal_binding(
            formals[index], scope, (*actuals)[index],
            hir_process_scope_);
        if (!formal) {
            return false;
        }
        const auto direction
            = hir_callable_frames_[frame_index].directions[index];
        const auto actual_expression = specialized_hir_unit_->find_expression(
            (*actuals)[index]);
        const bool aggregate_actual = actual_expression
            && actual_expression->vhdl != nullptr
            && actual_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate;
        if (class_method_copy_out(direction) && !aggregate_actual) {
            copy_out_targets[index]
                = capture_hir_packed_update_target((*actuals)[index]);
        }
        if (callable_copy_in(direction)) {
            lowered_actuals[index] = copy_out_targets[index]
                ? std::optional<RegisterId> {
                      copy_out_targets[index]->captured }
                : lower_hir_callable_actual(
                      (*actuals)[index], formals[index], formal->width);
            if (!lowered_actuals[index]) {
                return false;
            }
            if (register_width(*lowered_actuals[index])
                != formal->width) {
                lowered_actuals[index] = resize_register(
                    *lowered_actuals[index], formal->width,
                    hir_actual_extension_signed(
                (*actuals)[index], formal->signed_value));
            }
            if (is_two_state_domain(formal->domain)) {
                lowered_actuals[index]
                    = convert_to_two_state(*lowered_actuals[index]);
            }
        }
    }

    emit_debug_point(
        DebugPointKind::call,
        hir_source_span(statement->vhdl->source));
    auto& frame = hir_callable_frames_[frame_index];
    const auto push_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(CallableFramePush {
        frame.invocation_identity,
        frame.invocation_registers,
        frame.invocation_strings,
        { },
        true,
    });
    if (!frame.snapshot_complete) {
        frame.invocation_push_sites.push_back(push_site);
    }
    for (std::size_t index { }; index < lowered_actuals.size(); ++index) {
        if (lowered_actuals[index]) {
            process_.operations.emplace_back(CopyRegister {
                frame.arguments[index], *lowered_actuals[index] });
            continue;
        }
        if (signal_actuals[index]) {
            continue;
        }
        if (string_formals[index]) {
            process_.operations.emplace_back(CopyStringRegister {
                frame.string_arguments[index],
                *lowered_string_actuals[index],
            });
            continue;
        }
        const auto formal = hir_callable_formal_binding(
            formals[index], scope, (*actuals)[index],
            hir_process_scope_);
        if (!formal) {
            return false;
        }
        process_.operations.emplace_back(LoadConstant {
            frame.arguments[index],
            callable_default_value(formal->width, formal->domain),
        });
    }
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0U),
        static_cast<InstructionIndex>(call_site + 1U),
        { },
    });
    if (!frame.target) {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_hir_callables_.push_back(frame_index);
    }
    std::vector<std::optional<RegisterId>> copy_out_values(
        frame.arguments.size());
    std::vector<RegisterId> preserved_values;
    for (std::size_t index { }; index < frame.arguments.size(); ++index) {
        if (!class_method_copy_out(frame.directions[index])) {
            continue;
        }
        copy_out_values[index] = allocate_register(
            register_width(frame.arguments[index]),
            register_domain(frame.arguments[index]));
        process_.operations.emplace_back(CopyRegister {
            *copy_out_values[index], frame.arguments[index] });
        preserved_values.push_back(*copy_out_values[index]);
    }
    std::vector<std::optional<StringRegisterId>> string_copy_values(
        frame.arguments.size());
    std::vector<StringRegisterId> preserved_strings;
    for (std::size_t index { }; index < string_copy_out.size()
         && index < frame.string_arguments.size();
         ++index) {
        if (!string_copy_out[index]) {
            continue;
        }
        string_copy_values[index] = allocate_string_register();
        process_.operations.emplace_back(CopyStringRegister {
            *string_copy_values[index], frame.string_arguments[index] });
        preserved_strings.push_back(*string_copy_values[index]);
    }
    process_.operations.emplace_back(CallableFramePop {
        frame.invocation_identity,
        preserved_values,
        preserved_strings,
        { },
    });
    for (std::size_t index { }; index < string_copy_values.size();
         ++index) {
        if (string_copy_values[index]) {
            process_.operations.emplace_back(CopyStringRegister {
                *string_copy_out[index], *string_copy_values[index] });
        }
    }
    for (std::size_t index { }; index < copy_out_values.size(); ++index) {
        if (!copy_out_values[index]) {
            continue;
        }
        const auto actual_expression = specialized_hir_unit_->find_expression(
            (*actuals)[index]);
        const auto formal_declaration = specialized_hir_unit_->find_declaration(
            formals[index]);
        const bool aggregate_actual = actual_expression
            && actual_expression->vhdl != nullptr
            && actual_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate
            && formal_declaration && formal_declaration->vhdl != nullptr
            && formal_declaration->vhdl->subtype;
        auto copied_value = *copy_out_values[index];
        if (formal_declaration && formal_declaration->systemverilog != nullptr
            && formal_declaration->systemverilog->type) {
            // A signed formal extends to a wider actual by its own sign,
            // as an assignment does (IEEE 1800-2017 13.3, 10.7), and a
            // two-state actual receives x and z bits as zero (6.11.2).
            const auto actual_width = hir_expression_width(
                (*actuals)[index], hir_process_scope_);
            if (formal_declaration->systemverilog->type->signed_value
                && actual_width
                && *actual_width > register_width(copied_value)
                && !frontend::is_real_scalar_kind(
                    hir_systemverilog_scalar_kind((*actuals)[index]))) {
                copied_value = resize_register(
                    copied_value, *actual_width, true);
            }
            if (hir_expression_domain((*actuals)[index], hir_process_scope_)
                == frontend::ValueDomain::Bit2) {
                copied_value = convert_to_two_state(copied_value);
            }
        }
        const auto copied = aggregate_actual
            ? lower_hir_vhdl_aggregate_copy_out((*actuals)[index],
                  *copy_out_values[index],
                  *formal_declaration->vhdl->subtype)
            : copy_out_targets[index]
            ? write_hir_packed_update_target(
                  *copy_out_targets[index], copied_value)
            : lower_hir_packed_copy_out(
                  (*actuals)[index], copied_value);
        if (!copied) {
            return false;
        }
    }
    return true;
}

bool Lowerer::lower_hir_callable_return(
    const std::optional<semantic::ExpressionId> value)
{
    if (!active_hir_callable_) {
        return false;
    }
    const auto index = *active_hir_callable_;
    if (!hir_callable_frames_[index].function) {
        if (value) {
            return false;
        }
        hir_callable_frames_[index].return_jumps.push_back(
            static_cast<InstructionIndex>(process_.operations.size()));
        process_.operations.emplace_back(Jump { });
        return true;
    }
    if (!value) {
        // `return;` ends a void function (IEEE 1800-2017 13.4.1).
        const auto declaration = specialized_hir_unit_ != nullptr
            ? specialized_hir_unit_->find_declaration(
                  hir_callable_frames_[index].declaration)
            : std::nullopt;
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->callable
            && declaration->systemverilog->callable->return_type.target.spelling
                == "void") {
            hir_callable_frames_[index].return_jumps.push_back(
                static_cast<InstructionIndex>(process_.operations.size()));
            process_.operations.emplace_back(Jump { });
            return true;
        }
        return false;
    }
    const auto type = hir_callable_frames_[index].type;
    if (type.container) {
        const auto result = hir_callable_frames_[index].container_result;
        const auto actual_type = hir_static_container_expression_type(
            *value);
        const auto lowered = lower_hir_static_container_actual(
            *value, *type.container,
            hir_callable_frames_[index].declaration);
        if (!lowered && actual_type && !actual_type->fixed
            && !type.container->fixed
            && *actual_type != *type.container) {
            report(
                "FSIM-ELAB-SVFUNC-008",
                "function return container kind and profile must exactly "
                "match the declared result",
                hir_source_span(
                    specialized_hir_unit_->find_expression(*value)
                        ->systemverilog->source));
            hir_callable_frames_[index].return_jumps.push_back(
                static_cast<InstructionIndex>(
                    process_.operations.size()));
            process_.operations.emplace_back(Jump { });
            return true;
        }
        if (!result || !lowered) {
            return false;
        }
        const auto& frame = hir_callable_frames_[index];
        const auto storage = hir_static_storage(frame.declaration, &frame);
        if (storage && storage->container) {
            process_.operations.emplace_back(WriteContainerObject {
                *storage->container, *lowered, std::nullopt });
        } else {
            process_.operations.emplace_back(CopyContainerRegister {
                *result, *lowered });
        }
        hir_callable_frames_[index].return_jumps.push_back(
            static_cast<InstructionIndex>(process_.operations.size()));
        process_.operations.emplace_back(Jump { });
        return true;
    }
    if (type.string) {
        const auto lowered = lower_hir_string_expression(*value);
        if (!lowered) {
            return false;
        }
        const auto& frame = hir_callable_frames_[index];
        const auto storage = hir_static_storage(frame.declaration, &frame);
        if (storage && storage->string) {
            process_.operations.emplace_back(WriteStringObject {
                *storage->string, *lowered });
        } else {
            process_.operations.emplace_back(CopyStringRegister {
                frame.string_result, *lowered });
        }
        hir_callable_frames_[index].return_jumps.push_back(
            static_cast<InstructionIndex>(process_.operations.size()));
        process_.operations.emplace_back(Jump { });
        return true;
    }
    const auto expression = specialized_hir_unit_->find_expression(*value);
    const auto declaration = specialized_hir_unit_->find_declaration(
        hir_callable_frames_[index].declaration);
    auto result_subtype = declaration && declaration->vhdl != nullptr
            && declaration->vhdl->callable
            && declaration->vhdl->callable->return_type
        ? hir_effective_vhdl_subtype(
              *declaration->vhdl->callable->return_type)
        : std::nullopt;
    if (result_subtype
        && (result_subtype->unconstrained
            || !result_subtype->executable_width
            || *result_subtype->executable_width == 0U)) {
        result_subtype->executable_width = type.width;
    }
    // A SystemVerilog return value converts to the declared return type
    // like an assignment (IEEE 1800-2017 13.4.1, 6.12.2).
    using ScalarKind = frontend::SystemVerilogScalarKind;
    auto return_kind = ScalarKind::None;
    if (declaration && declaration->systemverilog != nullptr
        && declaration->systemverilog->callable) {
        const auto& spelling
            = declaration->systemverilog->callable->return_type.target
                  .spelling;
        return_kind = spelling == "real" ? ScalarKind::Real
            : spelling == "realtime"     ? ScalarKind::Realtime
            : spelling == "shortreal"    ? ScalarKind::ShortReal
                                         : ScalarKind::None;
    }
    const auto real_kind = [](const ScalarKind kind) {
        return kind == ScalarKind::Real || kind == ScalarKind::Realtime
            || kind == ScalarKind::ShortReal;
    };
    const auto value_kind = expression && expression->systemverilog != nullptr
        ? hir_systemverilog_scalar_kind(*value)
        : ScalarKind::None;
    const bool real_to_integral = expression
        && expression->systemverilog != nullptr
        && declaration && declaration->systemverilog != nullptr
        && declaration->systemverilog->callable
        && return_kind == ScalarKind::None && real_kind(value_kind);
    auto lowered = expression && expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate
            && result_subtype
        ? lower_hir_vhdl_aggregate(
              *value, type.width, &*result_subtype)
        : real_to_integral
        ? lower_hir_expression(
              *value, value_kind == ScalarKind::ShortReal ? 32U : 64U,
              value_kind)
        : lower_hir_expression(*value, type.width);
    if (!lowered) {
        return false;
    }
    if (real_to_integral) {
        lowered = convert_hir_real_to_integral(*lowered, value_kind);
    } else if (real_kind(return_kind) && !real_kind(value_kind)
        && expression && expression->systemverilog != nullptr) {
        lowered = convert_hir_integral_to_real(
            *lowered, hir_expression_signed(*value), return_kind);
    }
    if (register_width(*lowered) != type.width) {
        lowered = resize_register(
            *lowered, type.width, type.signed_value);
    }
    const auto& frame = hir_callable_frames_[index];
    if (const auto storage = hir_static_variable_signal(frame.declaration, &frame)) {
        process_.operations.emplace_back(WriteBlocking { *storage, *lowered });
    } else {
        process_.operations.emplace_back(CopyRegister { frame.result, *lowered });
    }
    hir_callable_frames_[index].return_jumps.push_back(
        static_cast<InstructionIndex>(process_.operations.size()));
    process_.operations.emplace_back(Jump { });
    return true;
}

bool Lowerer::lower_hir_callable_body(const std::size_t callable_index)
{
    if (callable_index >= hir_callable_frames_.size()
        || hir_callable_frames_[callable_index].lowered) {
        return callable_index < hir_callable_frames_.size();
    }
    auto declaration = specialized_hir_unit_->find_declaration(
        hir_callable_frames_[callable_index].declaration);
    if (!declaration) {
        return false;
    }
    const auto statements = declaration->systemverilog != nullptr
        ? std::span<const semantic::StatementId> {
              declaration->systemverilog->statements
          }
        : std::span<const semantic::StatementId> { declaration->vhdl->statements };
    const auto children = declaration->systemverilog != nullptr
        ? std::span<const semantic::DeclarationId> {
              declaration->systemverilog->children
          }
        : std::span<const semantic::DeclarationId> { declaration->vhdl->children };

    auto& initial_frame = hir_callable_frames_[callable_index];
    const auto automatic = initial_frame.type.automatic;
    hir_generic_binding_frames_.push_back(
        initial_frame.generic_bindings);
    const PopBackGuard generic_scope { hir_generic_binding_frames_ };
    initial_frame.target = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto call_site : initial_frame.call_sites) {
        operation_get<Call>(process_.operations[call_site]).target
            = *initial_frame.target;
    }
    initial_frame.call_sites.clear();
    initial_frame.lowered = true;

    const auto saved_scope = hir_process_scope_;
    auto saved_hir_locals = std::move(hir_local_registers_);
    auto saved_hir_string_locals
        = std::move(hir_local_string_registers_);
    auto saved_hir_container_locals
        = std::move(hir_local_container_registers_);
    auto saved_hir_container_types
        = std::move(hir_local_container_types_);
    auto saved_locals = std::move(locals_);
    auto saved_string_locals = std::move(string_locals_);
    auto saved_loops = std::move(loop_controls_);
    const auto saved_active = active_hir_callable_;
    const auto saved_class_receiver = hir_class_receiver_register_;
    auto saved_container_caches = std::move(hir_class_container_caches_);
    hir_class_container_caches_.clear();
    const auto restore_context = [&] {
        hir_class_container_caches_ = std::move(saved_container_caches);
        active_hir_callable_ = saved_active;
        loop_controls_ = std::move(saved_loops);
        string_locals_ = std::move(saved_string_locals);
        locals_ = std::move(saved_locals);
        hir_local_string_registers_
            = std::move(saved_hir_string_locals);
        hir_local_container_types_
            = std::move(saved_hir_container_types);
        hir_local_container_registers_
            = std::move(saved_hir_container_locals);
        hir_local_registers_ = std::move(saved_hir_locals);
        hir_process_scope_ = saved_scope;
        hir_class_receiver_register_ = saved_class_receiver;
    };
    hir_process_scope_ = initial_frame.scope;
    hir_local_registers_ = hir_enclosing_local_registers_;
    // A subprogram declared in another subprogram reads that subprogram's
    // formals and locals (IEEE 1076-2008 4.3, 12.3).
    {
        const auto& scopes = specialized_hir_unit_->design().semantics.scopes();
        std::set<std::uint32_t> enclosing;
        auto scope = initial_frame.scope;
        for (std::size_t depth { }; scope.valid() && depth < 64U; ++depth) {
            if (scope.value() >= scopes.size()
                || !scopes[scope.value()].parent) {
                break;
            }
            scope = *scopes[scope.value()].parent;
            enclosing.insert(scope.value());
        }
        for (const auto& frame : hir_callable_frames_) {
            if (frame.lowered && frame.scope.valid()
                && enclosing.contains(frame.scope.value())) {
                for (const auto& [enclosing_declaration, register_id] :
                    frame.body_registers) {
                    hir_local_registers_.emplace(enclosing_declaration, register_id);
                }
            }
        }
    }
    hir_local_string_registers_ = hir_enclosing_local_string_registers_;
    hir_local_container_registers_.clear();
    hir_local_container_types_.clear();
    locals_.clear();
    string_locals_.clear();
    loop_controls_.clear();
    active_hir_callable_ = callable_index;
    hir_class_receiver_register_ = initial_frame.class_receiver;
    if (initial_frame.type.container
        && initial_frame.container_result
        && declaration->systemverilog != nullptr) {
        const auto declaration_id = initial_frame.declaration.value();
        hir_local_container_registers_.insert_or_assign(
            declaration_id, *initial_frame.container_result);
        hir_local_container_types_.insert_or_assign(
            declaration_id, *initial_frame.type.container);
        const auto& source = *declaration->systemverilog;
        const auto span = hir_source_span(source.source);
        auto debug_name = initial_frame.debug_name.empty()
            ? source.name
            : initial_frame.debug_name + "." + source.name;
        if (!debug_local_names_.emplace(debug_name).second) {
            debug_name += "@hir-callable-"
                + std::to_string(initial_frame.invocation_identity);
            debug_local_names_.emplace(debug_name);
        }
        process_.debug_container_locals.push_back(DebugContainerLocal {
            std::move(debug_name),
            *initial_frame.container_result,
            *initial_frame.type.container,
            SourceLocation {
                span.source_name.str(),
                static_cast<std::uint32_t>(span.begin.line),
                static_cast<std::uint32_t>(span.begin.column),
            },
        });
    }
    for (std::size_t index = 0U;
        index < initial_frame.formals.size(); ++index) {
        const auto formal = initial_frame.formals[index];
        const auto record = specialized_hir_unit_->find_declaration(formal);
        const auto name = record->systemverilog != nullptr
            ? record->systemverilog->name
            : record->vhdl->name;
        if (index < initial_frame.argument_is_container.size()
            && initial_frame.argument_is_container[index]) {
            const auto storage = initial_frame.container_arguments[index];
            const auto type
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::copy_at(
                    process_.container_register_types, storage);
            hir_local_container_registers_.insert_or_assign(
                formal.value(), storage);
            hir_local_container_types_.insert_or_assign(
                formal.value(), type);
            if (initial_frame.profile_formals.size()
                == initial_frame.formals.size()) {
                const auto profile = initial_frame.profile_formals[index];
                hir_local_container_registers_.insert_or_assign(
                    profile.value(), storage);
                hir_local_container_types_.insert_or_assign(
                    profile.value(), type);
            }
            if (!initial_frame.debug_name.empty()) {
                const auto source = record->systemverilog != nullptr
                    ? record->systemverilog->source
                    : record->vhdl->source;
                const auto span = hir_source_span(source);
                auto debug_name = initial_frame.debug_name + "." + name;
                if (!debug_local_names_.emplace(debug_name).second) {
                    debug_name += "@hir-callable-"
                        + std::to_string(
                            initial_frame.invocation_identity);
                    debug_local_names_.emplace(debug_name);
                }
                process_.debug_container_locals.push_back(
                    DebugContainerLocal {
                        std::move(debug_name),
                        storage,
                        type,
                        SourceLocation {
                            span.source_name.str(),
                            static_cast<std::uint32_t>(span.begin.line),
                            static_cast<std::uint32_t>(span.begin.column),
                        },
                    });
            }
            continue;
        }
        if (index < initial_frame.argument_is_string.size()
            && initial_frame.argument_is_string[index]) {
            hir_local_string_registers_.insert_or_assign(
                formal.value(), initial_frame.string_arguments[index]);
            if (initial_frame.profile_formals.size()
                == initial_frame.formals.size()) {
                hir_local_string_registers_.insert_or_assign(
                    initial_frame.profile_formals[index].value(),
                    initial_frame.string_arguments[index]);
            }
            string_locals_.insert_or_assign(
                name, initial_frame.string_arguments[index]);
            if (!initial_frame.debug_name.empty()) {
                const auto source = record->systemverilog != nullptr
                    ? record->systemverilog->source
                    : record->vhdl->source;
                const auto span = hir_source_span(source);
                auto debug_name = initial_frame.debug_name + "." + name;
                if (!debug_local_names_.emplace(debug_name).second) {
                    debug_name += "@hir-callable-"
                        + std::to_string(
                            initial_frame.invocation_identity);
                    debug_local_names_.emplace(debug_name);
                }
                process_.debug_string_locals.push_back(DebugStringLocal {
                    std::move(debug_name),
                    initial_frame.string_arguments[index],
                    SourceLocation {
                        span.source_name.str(),
                        static_cast<std::uint32_t>(span.begin.line),
                        static_cast<std::uint32_t>(span.begin.column),
                    },
                });
            }
            continue;
        }
        hir_local_registers_.insert_or_assign(
            formal.value(), initial_frame.arguments[index]);
        if (initial_frame.profile_formals.size()
            == initial_frame.formals.size()) {
            hir_local_registers_.insert_or_assign(
                initial_frame.profile_formals[index].value(),
                initial_frame.arguments[index]);
        }
        locals_.insert_or_assign(name, initial_frame.arguments[index]);
        if (!initial_frame.debug_name.empty()) {
            const auto source = record->systemverilog != nullptr
                ? record->systemverilog->source
                : record->vhdl->source;
            const auto type_name = record->systemverilog != nullptr
                    && record->systemverilog->type
                ? record->systemverilog->type->target.spelling
                : record->vhdl != nullptr && record->vhdl->subtype
                ? record->vhdl->subtype->type_mark.spelling
                : std::string { };
            const auto span = hir_source_span(source);
            const auto storage = initial_frame.arguments[index];
            auto debug_name = initial_frame.debug_name + "." + name;
            if (!debug_local_names_.emplace(debug_name).second) {
                debug_name += "@hir-callable-"
                    + std::to_string(
                        initial_frame.invocation_identity);
                debug_local_names_.emplace(debug_name);
            }
            process_.debug_locals.push_back(DebugLocal {
                std::move(debug_name),
                type_name,
                storage,
                register_width(storage),
                SourceLocation {
                    span.source_name.str(),
                    static_cast<std::uint32_t>(span.begin.line),
                    static_cast<std::uint32_t>(span.begin.column),
                },
                std::nullopt,
                std::nullopt,
                value_kind(register_domains_[storage]),
                { },
                frontend::SystemVerilogScalarKind::None,
            });
        }
    }

    if (!automatic) {
        for (const auto& [static_declaration, storage] :
            initial_frame.static_container_variables) {
            const auto record = specialized_hir_unit_->find_declaration(
                semantic::DeclarationId::from_index(static_declaration));
            const auto type = initial_frame.static_container_types.find(
                static_declaration);
            if (!record || record->systemverilog == nullptr
                || type == initial_frame.static_container_types.end()) {
                restore_context();
                return false;
            }
            hir_local_container_registers_.insert_or_assign(
                static_declaration, storage);
            hir_local_container_types_.insert_or_assign(
                static_declaration, type->second);
        }
        for (const auto& [static_declaration, storage] :
            initial_frame.static_string_variables) {
            const auto record = specialized_hir_unit_->find_declaration(
                semantic::DeclarationId::from_index(static_declaration));
            if (!record || record->systemverilog == nullptr) {
                active_hir_callable_ = saved_active;
                loop_controls_ = std::move(saved_loops);
                string_locals_ = std::move(saved_string_locals);
                locals_ = std::move(saved_locals);
                hir_local_string_registers_
                    = std::move(saved_hir_string_locals);
                hir_local_container_types_
                    = std::move(saved_hir_container_types);
                hir_local_container_registers_
                    = std::move(saved_hir_container_locals);
                hir_local_registers_ = std::move(saved_hir_locals);
                hir_process_scope_ = saved_scope;
                hir_class_receiver_register_ = saved_class_receiver;
                return false;
            }
            hir_local_string_registers_.insert_or_assign(
                static_declaration, storage);
            if (record->systemverilog->scope == initial_frame.scope) {
                string_locals_.insert_or_assign(
                    record->systemverilog->name, storage);
            }
            if (!initial_frame.debug_name.empty()) {
                const auto& source = *record->systemverilog;
                const auto span = hir_source_span(source.source);
                auto debug_name = initial_frame.debug_name + "."
                    + source.name;
                if (debug_local_names_.emplace(debug_name).second) {
                    process_.debug_string_locals.push_back(
                        DebugStringLocal {
                            std::move(debug_name),
                            storage,
                            SourceLocation {
                                span.source_name.str(),
                                static_cast<std::uint32_t>(
                                    span.begin.line),
                                static_cast<std::uint32_t>(
                                    span.begin.column),
                            },
                        });
                }
            }
        }
        for (const auto& [static_declaration, storage] :
            initial_frame.static_variables) {
            const auto record = specialized_hir_unit_->find_declaration(
                semantic::DeclarationId::from_index(static_declaration));
            if (!record || record->systemverilog == nullptr) {
                active_hir_callable_ = saved_active;
                loop_controls_ = std::move(saved_loops);
                string_locals_ = std::move(saved_string_locals);
                locals_ = std::move(saved_locals);
                hir_local_string_registers_
                    = std::move(saved_hir_string_locals);
                hir_local_container_types_
                    = std::move(saved_hir_container_types);
                hir_local_container_registers_
                    = std::move(saved_hir_container_locals);
                hir_local_registers_ = std::move(saved_hir_locals);
                hir_process_scope_ = saved_scope;
                hir_class_receiver_register_ = saved_class_receiver;
                return false;
            }
            hir_local_registers_.insert_or_assign(
                static_declaration, storage);
            if (record->systemverilog->scope == initial_frame.scope) {
                locals_.insert_or_assign(
                    record->systemverilog->name, storage);
            }
            if (!initial_frame.debug_name.empty()) {
                const auto& source = *record->systemverilog;
                const auto span = hir_source_span(source.source);
                auto debug_name = initial_frame.debug_name + "."
                    + source.name;
                if (debug_local_names_.emplace(debug_name).second) {
                    process_.debug_locals.push_back(DebugLocal {
                        std::move(debug_name),
                        source.type
                            ? source.type->target.spelling
                            : std::string { },
                        storage,
                        register_width(storage),
                        SourceLocation {
                            span.source_name.str(),
                            static_cast<std::uint32_t>(span.begin.line),
                            static_cast<std::uint32_t>(span.begin.column),
                        },
                        std::nullopt,
                        std::nullopt,
                        value_kind(register_domains_[storage]),
                        { },
                        frontend::SystemVerilogScalarKind::None,
                    });
                }
            }
        }
    }

    std::vector<semantic::DeclarationId> variables;
    std::vector<semantic::DeclarationId> vhdl_file_declarations;
    for (const auto child : children) {
        if (std::ranges::find(initial_frame.formals, child)
            != initial_frame.formals.end()) {
            continue;
        }
        const auto record = specialized_hir_unit_->find_declaration(child);
        if (!record) {
            active_hir_callable_ = saved_active;
            loop_controls_ = std::move(saved_loops);
            string_locals_ = std::move(saved_string_locals);
            locals_ = std::move(saved_locals);
            hir_local_string_registers_
                = std::move(saved_hir_string_locals);
            hir_local_container_types_
                = std::move(saved_hir_container_types);
            hir_local_container_registers_
                = std::move(saved_hir_container_locals);
            hir_local_registers_ = std::move(saved_hir_locals);
            hir_process_scope_ = saved_scope;
            hir_class_receiver_register_ = saved_class_receiver;
            return false;
        }
        const auto variable = record->systemverilog != nullptr
            ? record->systemverilog->form
                == semantic::sv::DeclarationForm::variable
            : record->vhdl->form
                == semantic::vhdl::DeclarationForm::variable;
        const bool vhdl_file = record->vhdl != nullptr
            && (record->vhdl->form
                    == semantic::vhdl::DeclarationForm::file
                || record->vhdl->object_class
                    == semantic::vhdl::ObjectClass::file);
        if (vhdl_file) {
            // A file declaration in a subprogram has that activation's
            // lifetime and is opened when its declarative part is elaborated.
            variables.push_back(child);
            vhdl_file_declarations.push_back(child);
            continue;
        }
        // A static variable of an automatic callable keeps its storage
        // and is initialized once (IEEE 1800-2017 6.21).
        if (variable && automatic && record->systemverilog != nullptr
            && record->systemverilog->lifetime
                == semantic::sv::Lifetime::static_lifetime) {
            if (const auto storage
                = initial_frame.static_variables.find(child.value());
                storage != initial_frame.static_variables.end()) {
                hir_local_registers_.insert_or_assign(
                    child.value(), storage->second);
                locals_.insert_or_assign(
                    record->systemverilog->name, storage->second);
                continue;
            }
        }
        if (variable && (automatic
                || (record->systemverilog != nullptr
                    && record->systemverilog->lifetime
                        == semantic::sv::Lifetime::automatic))) {
            variables.push_back(child);
        }
    }
    const auto packed_begin = next_register_;
    const auto string_begin = next_string_register_;
    const auto container_begin = next_container_register_;
    const auto declarations_ready = initialize_hir_declarations(variables);
    std::vector<RegisterId> vhdl_file_handles;
    if (declarations_ready) {
        vhdl_file_handles.reserve(vhdl_file_declarations.size());
        for (const auto file : vhdl_file_declarations) {
            if (const auto local = hir_local_registers_.find(file.value());
                local != hir_local_registers_.end()) {
                vhdl_file_handles.push_back(local->second);
            }
        }
    }
    // A task body is an interval that a disable statement naming the task
    // ends (IEEE 1800-2017 9.6.2).
    const auto* const task_declaration = declaration->systemverilog != nullptr
            && declaration->systemverilog->form
                == semantic::sv::DeclarationForm::task
        ? declaration->systemverilog
        : nullptr;
    if (task_declaration != nullptr && declarations_ready) {
        emit_disable_scope_marker(DebugPointKind::block_entry,
            task_declaration->name,
            hir_source_span(task_declaration->source), true);
    }
    if (declarations_ready && initial_frame.class_receiver) {
        cache_hir_class_container_properties(
            hir_callable_frames_[callable_index].declaration,
            *initial_frame.class_receiver);
    }
    const auto lowered = declarations_ready
        && lower_hir_constructor_prologue(
            hir_callable_frames_[callable_index].declaration, statements)
        && lower_hir_statements(statements);
    if (lowered) {
        const auto epilogue = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump :
            hir_callable_frames_[callable_index].return_jumps) {
            process_.operations[jump] = Jump { epilogue };
        }
        flush_hir_class_container_caches();
        if (task_declaration != nullptr) {
            emit_disable_scope_marker(DebugPointKind::block_exit,
                task_declaration->name,
                hir_source_span(task_declaration->source), true);
        }
        for (const auto handle : vhdl_file_handles) {
            // VHDL file objects declared in a subprogram are closed when the
            // subprogram activation ends. Explicit close leaves zero here;
            // ignore_zero makes the common epilogue safe in both cases.
            process_.operations.emplace_back(
                FileClose { handle, true, true });
        }
        process_.operations.emplace_back(Return { });
        auto& completed = hir_callable_frames_[callable_index];
        if (completed.type.automatic) {
            for (auto id = packed_begin; id < next_register_; ++id) {
                completed.invocation_registers.push_back(id);
            }
            for (auto id = string_begin;
                id < next_string_register_; ++id) {
                completed.invocation_strings.push_back(id);
            }
            for (auto id = container_begin;
                id < next_container_register_; ++id) {
                // A cached container property is reloaded after each call,
                // so a nested invocation need not preserve it.
                if (std::ranges::none_of(hir_class_container_caches_,
                        [&](const HirClassContainerCache& cache) {
                            return cache.value == id;
                        })) {
                    completed.invocation_containers.push_back(id);
                }
            }
            for (const auto push_site : completed.invocation_push_sites) {
                process_.operations[push_site] = CallableFramePush {
                    completed.invocation_identity,
                    completed.invocation_registers,
                    completed.invocation_strings,
                    completed.invocation_containers,
                    true,
                };
            }
        }
        completed.invocation_push_sites.clear();
        completed.snapshot_complete = true;
    }
    hir_callable_frames_[callable_index].body_registers
        = hir_local_registers_;

    restore_context();
    return lowered;
}

bool Lowerer::lower_pending_hir_callables()
{
    hir_enclosing_local_registers_ = hir_local_registers_;
    hir_enclosing_local_string_registers_ = hir_local_string_registers_;
    bool lowered = true;
    while (lowered && !pending_hir_callables_.empty()) {
        const auto callable = pending_hir_callables_.front();
        pending_hir_callables_.pop_front();
        hir_callable_frames_[callable].queued = false;
        lowered = lower_hir_callable_body(callable);
    }
    hir_enclosing_local_registers_.clear();
    hir_enclosing_local_string_registers_.clear();
    return lowered;
}

void Lowerer::append_vhdl_element_constraint_key(std::string& key,
    const std::vector<semantic::vhdl::ElementConstraint>& constraints) const
{
    const auto bound = [&](const std::optional<std::int64_t> value,
                           const std::optional<semantic::ExpressionId>
                               expression) {
        const auto folded = value ? value
            : expression ? hir_constant_integer(*expression)
                         : std::nullopt;
        return folded ? std::to_string(*folded) : std::string { "?" };
    };
    for (const auto& constraint : constraints) {
        for (const auto& step : constraint.path) {
            key += step + "/";
        }
        for (const auto& range : constraint.constraints) {
            key += bound(range.left, range.left_expression) + ":"
                + bound(range.right, range.right_expression)
                + (range.descending ? "d" : "a") + ",";
        }
        key += ";";
    }
}

void Lowerer::bind_hir_instance_parameters(HirCallableFrame& frame) const
{
    if (!frame.interface_receiver || frame.interface_receiver->empty()
        || specialized_hir_unit_ == nullptr) {
        return;
    }
    auto receiver = std::string_view { *frame.interface_receiver };
    if (receiver.starts_with("@instance:")) {
        receiver.remove_prefix(std::string_view { "@instance:" }.size());
    }
    const auto target = std::ranges::find(design_.specializations_,
        receiver, &SpecializationInfo::instance);
    const auto callable = specialized_hir_unit_->find_declaration(
        frame.declaration);
    if (target == design_.specializations_.end()
        || target->parameter_values.empty() || !callable
        || callable->systemverilog == nullptr) {
        return;
    }
    const auto value_of = [](const std::string_view text)
        -> std::optional<std::int64_t> {
        std::int64_t value { };
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc { }
            && parsed.ptr == text.data() + text.size()) {
            return value;
        }
        // `svconst-v3:...:v=<bits>`: a known value of at most 64 bits.
        const auto bits_at = text.rfind(":v=");
        if (!text.starts_with("svconst-v3:") || bits_at == std::string_view::npos) {
            return std::nullopt;
        }
        const auto bits = text.substr(bits_at + 3U);
        if (bits.empty() || bits.size() > 64U
            || bits.find_first_not_of("01") != std::string_view::npos) {
            return std::nullopt;
        }
        std::uint64_t raw { };
        for (const auto bit : bits) {
            raw = (raw << 1U) | (bit == '1' ? 1U : 0U);
        }
        const bool is_signed = text.find(":s=1:") != std::string_view::npos;
        if (is_signed && bits.size() < 64U && bits.front() == '1') {
            raw |= ~std::uint64_t { 0 } << bits.size();
        }
        return static_cast<std::int64_t>(raw);
    };
    const auto scope = callable->systemverilog->scope;
    for (const auto& declaration :
        specialized_hir_unit_->design().systemverilog_hir.declarations()) {
        if (declaration.scope != scope
            || (declaration.form != semantic::sv::DeclarationForm::parameter
                && declaration.form
                    != semantic::sv::DeclarationForm::local_parameter)) {
            continue;
        }
        const auto found = std::ranges::find(target->parameter_values,
            declaration.name,
            &std::pair<std::string, std::string>::first);
        if (found == target->parameter_values.end()) {
            continue;
        }
        if (const auto value = value_of(found->second)) {
            frame.static_integer_bindings.emplace_back(
                declaration.id, *value);
        }
    }
}

} // namespace fsim::elaboration
