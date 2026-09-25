// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_constant_evaluator.hpp"
#include "lowerer_internal.hpp"

#include <charconv>
#include <unordered_map>

namespace fsim::elaboration {
namespace {

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
        return std::ranges::equal(
            left,
            right,
            [](const char lhs, const char rhs) {
                return std::tolower(static_cast<unsigned char>(lhs))
                    == std::tolower(static_cast<unsigned char>(rhs));
            });
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
    const HirStaticContainerSelection& selection)
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
        process_.operations.emplace_back(IntegerCheck {
            *index, low, high });
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
    if (!selection_syntax) {
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
            return HirStaticContainerValue { value, *binding->type };
        }
    }
    if (expression && expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
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
    }
    const auto value = allocate_container_register(
        selection->selected_type);
    const auto source_offset
        = lower_hir_static_container_selection_offset(*selection);
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
    const ContainerRegisterId source)
{
    const auto source_type = process_.container_register_types.at(source);
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
    const semantic::ExpressionId expression_id) const
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
    if ((!instance && !static_access)
        || access.class_member_identity.empty()
        || (instance && access.operands.size() != 1U)
        || (static_access && !access.operands.empty())) {
        return std::nullopt;
    }

    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto* selected = resolver.resolve_systemverilog_class_property(
        access.class_member_identity, static_access).unique();
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
    if (selected == nullptr
        || selected->kind != semantic::sv::ClassMethodKind::function
        || ((selected->pure || !selected->defined) && !native_uvm)) {
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
    const auto result_width = callable.type
        ? hir_systemverilog_type_width(*callable.type)
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
    const semantic::sv::ClassMethod* selected = nullptr;
    if (class_call) {
        const semantic::CompiledDesignResolver resolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
        selected = resolver.resolve_systemverilog_class_method(
            identity, static_call).unique();
        if (selected == nullptr
            || (!native_uvm
                && selected->kind
                    != semantic::sv::ClassMethodKind::task)
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
                      call.scope, task)
                      .unique();
            if (selected_declaration) {
                interface_receiver = std::string { receiver };
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
    if (!callable.callable
        || (!native_uvm
            && (callable.form != semantic::sv::DeclarationForm::task
                || callable.callable->function
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
    result.automatic = native_uvm
        || callable.callable->lifetime
            == semantic::sv::Lifetime::automatic;
    if (instance_call) {
        const auto& receiver = call.task_arguments.front();
        if (!receiver.actual
            || hir_expression_width(*receiver.actual, process_scope)
                != std::optional<std::size_t> { 64U }) {
            return std::nullopt;
        }
        result.receiver = *receiver.actual;
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
    for (std::size_t actual_index = actual_offset;
        actual_index < call.task_arguments.size(); ++actual_index) {
        const auto& association = call.task_arguments[actual_index];
        if (!association.actual) {
            return std::nullopt;
        }
        auto formal_index = positional;
        if (!association.formal || association.formal->empty()) {
            while (formal_index < bound.size()
                && bound[formal_index].has_value()) {
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
    result.actuals.reserve(bound.size());
    for (auto& actual : bound) {
        result.actuals.push_back(std::move(*actual));
    }
    return result;
}

bool Lowerer::can_lower_hir_class_task_call(
    const semantic::StatementId statement,
    const semantic::ScopeId process_scope,
    std::unordered_set<std::uint32_t>&) const
{
    const auto profile = hir_class_task_profile(statement, process_scope);
    if (!profile) {
        return false;
    }
    const auto expression_supported = [&](
                                          const semantic::ExpressionId expression) {
        std::unordered_set<std::uint32_t> visiting;
        return can_lower_hir_expression(
            expression, process_scope, visiting);
    };
    if (profile->receiver
        && !expression_supported(*profile->receiver)) {
        return false;
    }
    return std::ranges::all_of(
        profile->actuals,
        [&](const HirClassMethodActual& actual) {
            if (actual.container) {
                const auto expression = specialized_hir_unit_->find_expression(
                    actual.expression);
                return expression && expression->systemverilog != nullptr
                    && (expression->systemverilog->kind
                            == semantic::sv::ExpressionKind::name
                        || expression->systemverilog->kind
                            == semantic::sv::ExpressionKind::slice);
            }
            if (actual.string) {
                if (!can_lower_hir_string_expression(
                        actual.expression, process_scope)) {
                    return false;
                }
                if (!class_method_copy_out(actual.direction)) {
                    return true;
                }
                const auto target = hir_target_declaration(
                    actual.expression);
                const auto binding = target
                    ? hir_string_binding(*target, process_scope, false)
                    : std::nullopt;
                return binding
                    && (binding->kind == HirStringBindingKind::local
                        || (binding->object
                            && !read_only_string_objects_.contains(
                                *binding->object)));
            }
            if (callable_copy_in(actual.direction)
                && !expression_supported(actual.expression)) {
                return false;
            }
            if (!class_method_copy_out(actual.direction)) {
                return true;
            }
            const auto target = hir_target_declaration(actual.expression);
            const auto binding = target
                ? hir_runtime_binding(*target, process_scope, false)
                : std::nullopt;
            return binding && binding->width == actual.width
                && (binding->kind == HirRuntimeBindingKind::local
                    || (binding->signal
                        && !read_only_signals_.contains(*binding->signal)));
        });
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
                    || (binding->signal
                        && !read_only_signals_.contains(
                            *binding->signal)));
        });
}

std::optional<RegisterId> Lowerer::lower_hir_class_method_call(
    const semantic::ExpressionId expression,
    const std::size_t expected_width)
{
    const auto profile = hir_class_method_profile(
        expression, hir_process_scope_);
    if (!profile) {
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
                    *lowered, actual.width, actual.signed_value);
            }
            actuals.push_back(*lowered);
            actual_kinds.push_back(0U);
        }
        actual_names.push_back(actual.name);
        actual_directions.push_back(
            static_cast<std::uint8_t>(actual.direction));
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

bool Lowerer::allocate_hir_static_callable_declarations(
    const std::size_t frame_index)
{
    if (frame_index >= hir_callable_frames_.size()) {
        return false;
    }
    if (hir_callable_frames_[frame_index].type.automatic
        || specialized_hir_unit_ == nullptr) {
        return true;
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

bool Lowerer::lower_hir_class_task_call(
    const semantic::StatementId statement_id)
{
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
                        *lowered, actual.width, actual.signed_value);
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

    const auto declaration = specialized_hir_unit_->find_declaration(
        profile->declaration);
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->callable
        || !declaration->systemverilog->nested_scope) {
        return false;
    }
    const auto& callable = *declaration->systemverilog;
    const auto scope = *callable.nested_scope;
    const auto formals = std::vector<semantic::DeclarationId> {
        callable.callable->formals.begin(),
        callable.callable->formals.end(),
    };
    if (formals.size() != profile->actuals.size()) {
        return false;
    }

    auto frame_index = hir_callable_frames_.size();
    bool new_frame { true };
    if (profile->interface_receiver) {
        const auto key = std::to_string(profile->declaration.value())
            + ":" + *profile->interface_receiver;
        const auto found = hir_interface_callable_indices_.find(key);
        if (found != hir_interface_callable_indices_.end()) {
            frame_index = found->second;
            new_frame = false;
        } else {
            hir_interface_callable_indices_.emplace(key, frame_index);
        }
    } else {
        const auto callable_key = systemverilog_task_callable_key(
            profile->declaration);
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
        frame.declaration = profile->declaration;
        frame.scope = scope;
        frame.type = {
            1U,
            frontend::ValueDomain::Bit2,
            false,
            profile->automatic,
            false,
            std::nullopt,
        };
        frame.interface_receiver = profile->interface_receiver;
        frame.formals = formals;
        frame.debug_name = callable.name;
        frame.function = false;
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
                return false;
            }
            const auto string = profile->actuals[index].string;
            const auto container = profile->actuals[index].container;
            frame.argument_is_string.push_back(string);
            frame.argument_is_container.push_back(container);
            if (container) {
                const auto container_type = hir_systemverilog_container_type(
                    *record->systemverilog->type);
                if (!container_type) {
                    return false;
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
                    || binding->kind != HirRuntimeBindingKind::local
                    || !callable_scalar_domain(binding->domain)) {
                    return false;
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
            return false;
        }
    }

    std::vector<std::optional<RegisterId>> lowered_actuals(
        profile->actuals.size());
    std::vector<std::optional<StringRegisterId>> lowered_string_actuals(
        profile->actuals.size());
    std::vector<std::optional<ContainerRegisterId>>
        lowered_container_actuals(profile->actuals.size());
    std::vector<std::optional<HirPackedUpdateTarget>>
        packed_copy_out_targets(profile->actuals.size());
    for (std::size_t index = 0U;
        index < profile->actuals.size(); ++index) {
        const auto& actual = profile->actuals[index];
        if (actual.container) {
            const auto formal_type = process_.container_register_types.at(
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
                    && *actual_type != formal_type) {
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
                    return false;
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
                return false;
            }
            continue;
        }
        lowered_actuals[index] = packed_copy_out_targets[index]
            ? std::optional<RegisterId> {
                  packed_copy_out_targets[index]->captured }
            : lower_hir_expression(
                  actual.expression, actual.width);
        if (!lowered_actuals[index]) {
            return false;
        }
        if (register_width(*lowered_actuals[index]) != actual.width) {
            lowered_actuals[index] = resize_register(
                *lowered_actuals[index],
                actual.width,
                actual.signed_value);
        }
    }

    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    emit_debug_point(
        DebugPointKind::call,
        hir_source_span(statement->systemverilog->source));
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
            return false;
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
                    return false;
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
                return false;
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
                    profile->actuals[index].width,
                    profile->actuals[index].domain),
            });
        }
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
    std::vector<std::optional<StringRegisterId>> string_copy_outs(
        profile->actuals.size());
    std::vector<std::optional<RegisterId>> packed_copy_outs(
        profile->actuals.size());
    std::vector<RegisterId> preserve_packed;
    std::vector<ContainerRegisterId> preserve_containers;
    for (std::size_t index = 0U;
        index < profile->actuals.size(); ++index) {
        if (!class_method_copy_out(frame.directions[index])) {
            continue;
        }
        if (frame.argument_is_container[index]) {
            if (!lowered_container_actuals[index]) {
                return false;
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
                    profile->actuals[index].expression,
                    *lowered_container_actuals[index])) {
                return false;
            }
            continue;
        }
        if (!string_copy_outs[index]) {
            if (!packed_copy_outs[index]) {
                continue;
            }
            const auto copied = packed_copy_out_targets[index]
                ? write_hir_packed_update_target(
                      *packed_copy_out_targets[index],
                      *packed_copy_outs[index])
                : lower_hir_packed_copy_out(
                      profile->actuals[index].expression,
                      *packed_copy_outs[index]);
            if (!copied) {
                return false;
            }
            continue;
        }
        if (!lower_hir_string_copy_out(
                profile->actuals[index].expression,
                *string_copy_outs[index])) {
            return false;
        }
    }
    return true;
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
        if (decode_hir_systemverilog_constant(actual->identity)
            || decode_hir_systemverilog_scalar_constant(
                actual->identity)
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
    std::ranges::transform(name, name.begin(), [](const char value) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    });
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
    return std::nullopt;
}

std::optional<semantic::vhdl::SubtypeIndication>
Lowerer::hir_effective_vhdl_subtype(
    const semantic::vhdl::SubtypeIndication& subtype) const
{
    return specialized_hir_unit_ != nullptr
        ? semantic::CompiledDesignResolver {
              *specialized_hir_unit_, hir_generic_binding_frames_
          }.effective_vhdl_subtype(subtype, hir_process_scope_)
        : std::nullopt;
}

std::optional<std::vector<Lowerer::HirGenericBinding>>
Lowerer::bind_hir_vhdl_generics(
    const std::span<const semantic::DeclarationId> formals,
    const std::span<const semantic::vhdl::Association> associations) const
{
    return specialized_hir_unit_ != nullptr
        ? semantic::CompiledDesignResolver {
              *specialized_hir_unit_, hir_generic_binding_frames_ }
              .bind_vhdl_generics(formals, associations)
        : std::nullopt;
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
            || !source.subtype) {
            return std::nullopt;
        }
        // VHDL subprogram locals have per-call activation lifetime.
        result.automatic = true;
        auto subtype = hir_effective_vhdl_subtype(*source.subtype);
        if (!subtype) {
            return std::nullopt;
        }
        if (subtype->domain == semantic::vhdl::ValueDomain::string) {
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
    const auto width = contextual
        ? hir_expression_width(actual, actual_scope)
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
    const auto domain = unspecified
        ? hir_expression_domain(actual, actual_scope)
        : std::optional {
              callable_vhdl_domain(subtype->domain)
          };
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
    result.signed_value = (unspecified
                                  ? hir_expression_signed(actual)
                                  : subtype->signed_value)
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
        const auto callable_expression
            = call.kind == semantic::vhdl::ExpressionKind::call
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
    if (!receiver || receiver->systemverilog == nullptr
        || receiver->systemverilog->kind
            != semantic::sv::ExpressionKind::name
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
              process_scope, function)
              .unique();
    return declaration
        ? std::optional<HirInterfaceFunctionProfile> {
              HirInterfaceFunctionProfile {
                  *declaration, receiver->systemverilog->text } }
        : std::nullopt;
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
        const auto unconstrained_result = declaration
                && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
            ? hir_effective_vhdl_subtype(
                  *declaration->vhdl->subtype)
            : std::nullopt;
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
        bool compatible = formals.size() == actuals->size();
        bool uses_unspecified_type { };
        std::unordered_map<std::string, std::string>
            unspecified_actual_types;
        for (std::size_t index { };
            compatible && index < formals.size(); ++index) {
            const auto formal = hir_callable_formal_binding(
                formals[index], scope, (*actuals)[index], process_scope);
            if (!formal) {
                compatible = false;
                break;
            }
            auto actual_width = hir_expression_width(
                (*actuals)[index], process_scope);
            auto actual_domain = hir_expression_domain(
                (*actuals)[index], process_scope);
            const auto actual
                = specialized_hir_unit_->find_expression(
                    (*actuals)[index]);
            const auto aggregate_actual = actual
                && actual->vhdl != nullptr
                && actual->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate;
            const auto context_dependent_literal = actual
                && actual->vhdl != nullptr
                && (actual->vhdl->kind
                        == semantic::vhdl::ExpressionKind::string_literal
                    || actual->vhdl->kind
                        == semantic::vhdl::ExpressionKind::logic_literal);
            if (aggregate_actual || context_dependent_literal) {
                actual_width = formal->width;
                actual_domain = formal->domain;
            }
            if (!actual_width || !actual_domain) {
                const auto nested = resolve_hir_vhdl_function_call(
                    (*actuals)[index], process_scope, formal->width);
                if (nested) {
                    hir_generic_binding_frames_.push_back(
                        nested->generic_bindings);
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
            const auto formal_declaration
                = specialized_hir_unit_->find_declaration(formals[index]);
            const auto formal_subtype = formal_declaration
                    && formal_declaration->vhdl != nullptr
                    && formal_declaration->vhdl->subtype
                ? hir_effective_vhdl_subtype(
                      *formal_declaration->vhdl->subtype)
                : std::nullopt;
            const auto actual_subtype = hir_vhdl_expression_subtype(
                (*actuals)[index]);
            const auto effective_actual_subtype = actual_subtype
                ? hir_effective_vhdl_subtype(*actual_subtype)
                : std::nullopt;
            if (formal_subtype
                && formal_subtype->unspecified_class
                    != semantic::vhdl::UnspecifiedTypeClass::none) {
                uses_unspecified_type = true;
                const auto& inference_identity
                    = formal_subtype->unspecified_inference_identity;
                if (!inference_identity.empty()
                    && effective_actual_subtype) {
                    const auto actual_identity
                        = effective_actual_subtype->type_mark.target.valid()
                        ? "type:"
                            + std::to_string(base_type(
                                  effective_actual_subtype
                                      ->type_mark.target)
                                                  .value())
                        : "profile:"
                            + std::to_string(static_cast<unsigned>(
                                  effective_actual_subtype->domain))
                            + ":"
                            + std::to_string(
                                effective_actual_subtype
                                    ->executable_width.value_or(0U));
                    const auto [found, inserted]
                        = unspecified_actual_types.emplace(
                            inference_identity, actual_identity);
                    if (!inserted && found->second != actual_identity) {
                        compatible = false;
                        break;
                    }
                }
            }
            if (effective_actual_subtype && !aggregate_actual
                && !context_dependent_literal) {
                const auto effective_width
                    = effective_actual_subtype->executable_width
                    ? effective_actual_subtype->executable_width
                    : effective_actual_subtype->domain
                            == semantic::vhdl::ValueDomain::integer
                        && effective_actual_subtype->integer_storage_width
                            != 0U
                    ? std::optional<std::uint64_t> {
                          effective_actual_subtype->integer_storage_width
                      }
                    : std::nullopt;
                if (effective_width
                    && *effective_width
                        <= std::numeric_limits<std::size_t>::max()) {
                    actual_width = static_cast<std::size_t>(
                        *effective_width);
                }
                if (effective_actual_subtype->domain
                    != semantic::vhdl::ValueDomain::unknown) {
                    actual_domain = callable_vhdl_domain(
                        effective_actual_subtype->domain);
                }
            }
            const auto same_nominal_type = aggregate_actual
                || context_dependent_literal
                || !formal_subtype || !effective_actual_subtype
                || !formal_subtype->type_mark.target.valid()
                || !effective_actual_subtype->type_mark.target.valid()
                || base_type(formal_subtype->type_mark.target)
                    == base_type(
                        effective_actual_subtype->type_mark.target)
                || (!resolution.generic_bindings.empty()
                    && (generic_type(
                            formal_subtype->type_mark.target)
                        || generic_type(
                            effective_actual_subtype
                                ->type_mark.target)));
            const auto same_nominal_array = [&] {
                if (!formal_subtype || !effective_actual_subtype
                    || !formal_subtype->type_mark.target.valid()
                    || !effective_actual_subtype->type_mark.target.valid()) {
                    return false;
                }
                const auto formal_type = base_type(
                    formal_subtype->type_mark.target);
                if (formal_type
                    != base_type(
                        effective_actual_subtype->type_mark.target)) {
                    return false;
                }
                const auto definition
                    = specialized_hir_unit_->find_type(formal_type);
                return definition && definition->vhdl != nullptr
                    && definition->vhdl->form
                    == semantic::vhdl::TypeForm::array;
            }();
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
            compatible = actual_width && actual_domain
                && *actual_domain == formal->domain
                && same_nominal_type
                && (!array_shapes_match || *array_shapes_match)
                && (formal->domain == frontend::ValueDomain::Integer
                    || *actual_width == formal->width
                    || same_nominal_array);
        }
        if (uses_unspecified_type
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
        bool compatible = formals.size() == actuals->size();
        for (std::size_t index { };
            compatible && index < formals.size(); ++index) {
            const auto formal = hir_callable_formal_binding(
                formals[index], scope, (*actuals)[index], process_scope);
            if (!formal) {
                compatible = false;
                break;
            }
            auto actual_width = hir_expression_width(
                (*actuals)[index], process_scope);
            auto actual_domain = hir_expression_domain(
                (*actuals)[index], process_scope);
            const auto actual
                = specialized_hir_unit_->find_expression(
                    (*actuals)[index]);
            const auto context_dependent_actual = actual
                && actual->vhdl != nullptr
                && (actual->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate
                    || actual->vhdl->kind
                        == semantic::vhdl::ExpressionKind::string_literal
                    || actual->vhdl->kind
                        == semantic::vhdl::ExpressionKind::logic_literal);
            if (context_dependent_actual) {
                actual_width = formal->width;
                actual_domain = formal->domain;
            }
            if (!actual_width || !actual_domain) {
                const auto nested = resolve_hir_vhdl_function_call(
                    (*actuals)[index], process_scope, formal->width);
                if (nested) {
                    hir_generic_binding_frames_.push_back(
                        nested->generic_bindings);
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
            const auto actual_subtype = hir_vhdl_expression_subtype(
                (*actuals)[index]);
            const auto effective_actual_subtype = actual_subtype
                ? hir_effective_vhdl_subtype(*actual_subtype)
                : std::nullopt;
            if (effective_actual_subtype
                && !context_dependent_actual) {
                const auto effective_width
                    = effective_actual_subtype->executable_width
                    ? effective_actual_subtype->executable_width
                    : effective_actual_subtype->domain
                            == semantic::vhdl::ValueDomain::integer
                        && effective_actual_subtype->integer_storage_width
                            != 0U
                    ? std::optional<std::uint64_t> {
                          effective_actual_subtype->integer_storage_width
                      }
                    : std::nullopt;
                if (effective_width
                    && *effective_width
                        <= std::numeric_limits<std::size_t>::max()) {
                    actual_width = static_cast<std::size_t>(
                        *effective_width);
                }
                if (effective_actual_subtype->domain
                    != semantic::vhdl::ValueDomain::unknown) {
                    actual_domain = callable_vhdl_domain(
                        effective_actual_subtype->domain);
                }
            }
            compatible = actual_width && actual_domain
                && *actual_domain == formal->domain
                && (formal->domain == frontend::ValueDomain::Integer
                    || *actual_width == formal->width);
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

bool Lowerer::can_lower_hir_vhdl_procedure_call(
    const semantic::StatementId statement_id,
    const semantic::ScopeId process_scope,
    std::unordered_set<std::uint32_t>& visiting) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    if (is_hir_vhdl_vital_delay_call(statement_id)) {
        return true;
    }
    if (is_hir_vhdl_vital_state_table_call(statement_id)
        || is_hir_vhdl_vital_timing_call(statement_id)) {
        return true;
    }
    if (is_hir_vhdl_file_statement(statement_id)) {
        return true;
    }
    if (is_hir_vhdl_access_deallocation(statement_id)) {
        return true;
    }
    if (is_hir_vhdl_protected_statement(statement_id)) {
        return true;
    }
    if (is_hir_vhdl_environment_directory_statement(statement_id)) {
        return true;
    }
    if (is_hir_vhdl_assert_statement(statement_id)) {
        return true;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (statement && statement->vhdl != nullptr
        && (same_callable_name(statement->vhdl->procedure.spelling,
                "std.env.setpslcoverassert", true)
            || same_callable_name(statement->vhdl->procedure.spelling,
                "std.env.clearpslstate", true)
            || same_callable_name(statement->vhdl->procedure.spelling,
                "std.env.stop", true)
            || same_callable_name(statement->vhdl->procedure.spelling,
                "std.env.finish", true))) {
        return true;
    }
    const auto resolution = resolve_hir_vhdl_procedure_call(
        statement_id, process_scope);
    if (!resolution) {
        if (statement && statement->vhdl != nullptr) {
            const auto candidates = hir_vhdl_callable_candidates(
                statement->vhdl->procedure,
                statement->vhdl->scope);
            if (candidates.size() > 1U) {
                return true;
            }
        }
        return false;
    }
    hir_generic_binding_frames_.push_back(
        resolution->generic_bindings);
    const PopBackGuard generic_scope { hir_generic_binding_frames_ };
    const auto declaration = specialized_hir_unit_->find_declaration(
        resolution->body);
    const auto actuals = bind_hir_vhdl_procedure_actuals(
        statement_id, resolution->body);
    if (!declaration || declaration->vhdl == nullptr || !actuals
        || !declaration->vhdl->callable
        || !declaration->vhdl->nested_scope) {
        return false;
    }
    const auto scope = *declaration->vhdl->nested_scope;
    const auto& formals = declaration->vhdl->callable->formals;
    for (std::size_t index { }; index < formals.size(); ++index) {
        const auto formal = specialized_hir_unit_->find_declaration(
            formals[index]);
        const auto binding = hir_callable_formal_binding(
            formals[index], scope, (*actuals)[index], process_scope);
        const auto direction = formal
            ? callable_direction(*formal)
            : frontend::PortDirection::Unknown;
        if (!binding
            || binding->kind != HirRuntimeBindingKind::local
            || direction == frontend::PortDirection::Unknown
            || !callable_scalar_domain(binding->domain)) {
            return false;
        }
        if (callable_copy_in(direction)
            && !can_lower_hir_expression(
                (*actuals)[index], process_scope, visiting)) {
            return false;
        }
        if (!class_method_copy_out(direction)) {
            continue;
        }
        const auto actual = specialized_hir_unit_->find_expression(
            (*actuals)[index]);
        const auto target = hir_target_declaration((*actuals)[index]);
        const auto target_binding = target
            ? hir_runtime_binding(*target, process_scope, false)
            : std::nullopt;
        const auto writable = target_binding
            && (target_binding->kind == HirRuntimeBindingKind::local
                || (target_binding->signal
                    && !read_only_signals_.contains(
                        *target_binding->signal)));
        if (!actual || actual->vhdl == nullptr
            || actual->vhdl->kind
                != semantic::vhdl::ExpressionKind::name
            || !writable || target_binding->width != binding->width
            || target_binding->domain != binding->domain) {
            return false;
        }
    }
    return true;
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
                && (!binding->signal
                    || read_only_signals_.contains(*binding->signal)))) {
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
            || binding->kind != HirRuntimeBindingKind::local
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
                    || (target_binding->signal
                        && !read_only_signals_.contains(
                            *target_binding->signal)));
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
    const auto interface_callable = !callable && expression
            && expression->systemverilog != nullptr
        ? hir_interface_function_profile(
              expression_id, hir_process_scope_)
        : std::nullopt;
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
    std::vector<std::pair<std::size_t, std::optional<HirPackedRange>>>
        vhdl_actual_ranges;
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
            const semantic::vhdl::SubtypeIndication& subtype)
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
            return std::nullopt;
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
            const auto formal_predefined_vector = formal_subtype
                    && formal_subtype->unconstrained
                ? predefined_packed_vector(*formal_subtype)
                : std::nullopt;
            const auto formal_array = formal_subtype
                    && formal_subtype->unconstrained
                    && formal_subtype->type_mark.target.valid()
                ? array_root(formal_subtype->type_mark.target)
                : std::nullopt;
            if ((!formal_array || formal_array->second != 1U)
                && !formal_predefined_vector) {
                continue;
            }

            std::optional<HirPackedRange> actual_range;
            const auto actual_root = hir_vhdl_composite_root_type(
                (*actuals)[index]);
            const auto actual_width = hir_expression_width(
                (*actuals)[index], hir_process_scope_);
            const auto actual_subtype = hir_vhdl_expression_subtype(
                (*actuals)[index]);
            const auto actual_predefined_vector = actual_subtype
                ? predefined_packed_vector(*actual_subtype)
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
                && formal_array->second == 1U
                && actual_array->first == formal_array->first
                && actual_array->second == 1U;
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
            if ((nominal_array_match || proven_vector_match)
                && actual_width && range
                && distance
                    != std::numeric_limits<std::uint64_t>::max()
                && distance + 1U == *actual_width
                && (range->left == range->right
                    || range->descending
                        == (range->left > range->right))) {
                actual_range = *range;
            }
            vhdl_actual_ranges.emplace_back(index, actual_range);
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
        if (range) {
            callable_key += std::to_string(range->left) + ":"
                + std::to_string(range->right) + ":"
                + (range->descending ? "down" : "up");
        } else {
            callable_key += "unknown";
        }
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
                : false;
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
                if (!binding
                    || binding->kind != HirRuntimeBindingKind::local) {
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
            const auto formal_type = process_.container_register_types.at(
                frame.container_arguments[index]);
            const auto actual = allocate_container_register(formal_type);
            lowered_container_actuals[index] = actual;
            if (callable_copy_in(frame.directions[index])) {
                const auto actual_type
                    = hir_static_container_expression_type(
                        (*actuals)[index]);
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
                formal->signed_value);
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
        process_.operations.emplace_back(CopyContainerRegister {
            *frame.container_result,
            *frame.container_result_default,
        });
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
    const auto callable = hir_referenced_declaration(expression_id);
    const auto resolution = expression && expression->vhdl != nullptr
        ? resolve_hir_vhdl_function_call(
              expression_id, hir_process_scope_, 0U)
        : callable ? hir_callable_resolution(*callable)
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
    const auto callable_key_text = std::to_string(callable_key);
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
                if (!binding
                    || binding->kind != HirRuntimeBindingKind::local) {
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
            const auto formal_type = process_.container_register_types.at(
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
                formal->signed_value);
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
    const auto& procedure = statement->vhdl->procedure;
    const auto set_psl_cover_assert = same_callable_name(
        procedure.spelling, "std.env.setpslcoverassert", true);
    const auto clear_psl_state = same_callable_name(
        procedure.spelling, "std.env.clearpslstate", true);
    const auto stop = same_callable_name(
        procedure.spelling, "std.env.stop", true);
    const auto finish = same_callable_name(
        procedure.spelling, "std.env.finish", true);
    if (set_psl_cover_assert || clear_psl_state || stop || finish) {
        const auto& associations = statement->vhdl->procedure_arguments;
        if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019) {
            report(
                "FSIM-ELAB-VHENV-001",
                "the STD.ENV simulator API requires VHDL-2019",
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
        return false;
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
    for (std::size_t index { }; index < formals.size(); ++index) {
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
        const auto writable = target_binding
            && (target_binding->kind == HirRuntimeBindingKind::local
                || (target_binding->signal
                    && !read_only_signals_.contains(
                        *target_binding->signal)));
        if (actual && actual->vhdl != nullptr && writable) {
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

    const auto callable_key = vhdl_procedure_callable_key(
        resolution->key);
    const auto callable_key_text = std::to_string(callable_key);
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
        frame.debug_name = statement->vhdl->procedure.spelling;
        frame.function = false;
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (frame.invocation_identity == 0U) {
            frame.invocation_identity
                = next_callable_invocation_identity_++;
        }
        for (std::size_t index { }; index < formals.size(); ++index) {
            const auto formal = formals[index];
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
    std::vector<std::optional<HirPackedUpdateTarget>>
        copy_out_targets(actuals->size());
    for (std::size_t index { }; index < actuals->size(); ++index) {
        const auto formal = hir_callable_formal_binding(
            formals[index], scope, (*actuals)[index],
            hir_process_scope_);
        if (!formal) {
            return false;
        }
        const auto direction
            = hir_callable_frames_[frame_index].directions[index];
        if (class_method_copy_out(direction)) {
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
                    formal->signed_value);
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
        { },
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
    process_.operations.emplace_back(CallableFramePop {
        frame.invocation_identity,
        preserved_values,
        { },
        { },
    });
    for (std::size_t index { }; index < copy_out_values.size(); ++index) {
        if (!copy_out_values[index]) {
            continue;
        }
        const auto copied = copy_out_targets[index]
            ? write_hir_packed_update_target(
                  *copy_out_targets[index], *copy_out_values[index])
            : lower_hir_packed_copy_out(
                  (*actuals)[index], *copy_out_values[index]);
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
        process_.operations.emplace_back(CopyContainerRegister {
            *result, *lowered });
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
        process_.operations.emplace_back(CopyStringRegister {
            hir_callable_frames_[index].string_result, *lowered });
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
    auto lowered = expression && expression->vhdl != nullptr
            && expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate
            && result_subtype
        ? lower_hir_vhdl_aggregate(
              *value, type.width, &*result_subtype)
        : lower_hir_expression(*value, type.width);
    if (!lowered) {
        return false;
    }
    if (register_width(*lowered) != type.width) {
        lowered = resize_register(
            *lowered, type.width, type.signed_value);
    }
    process_.operations.emplace_back(CopyRegister {
        hir_callable_frames_[index].result, *lowered });
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
    const auto restore_context = [&] {
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
    hir_local_registers_.clear();
    hir_local_string_registers_.clear();
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
        process_.debug_container_locals.push_back(DebugContainerLocal {
            initial_frame.debug_name.empty()
                ? source.name
                : initial_frame.debug_name + "." + source.name,
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
            const auto& type = process_.container_register_types.at(storage);
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
        if (variable) {
            variables.push_back(child);
        }
    }
    const auto packed_begin = next_register_;
    const auto string_begin = next_string_register_;
    const auto container_begin = next_container_register_;
    const auto declarations_ready = !automatic
        || initialize_hir_declarations(variables);
    const auto lowered = declarations_ready
        && lower_hir_statements(statements);
    if (lowered) {
        const auto epilogue = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump :
            hir_callable_frames_[callable_index].return_jumps) {
            process_.operations[jump] = Jump { epilogue };
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
                completed.invocation_containers.push_back(id);
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

    restore_context();
    return lowered;
}

bool Lowerer::lower_pending_hir_callables()
{
    while (!pending_hir_callables_.empty()) {
        const auto callable = pending_hir_callables_.front();
        pending_hir_callables_.pop_front();
        hir_callable_frames_[callable].queued = false;
        if (!lower_hir_callable_body(callable)) {
            return false;
        }
    }
    return true;
}

} // namespace fsim::elaboration
