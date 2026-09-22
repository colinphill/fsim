// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <charconv>

namespace fsim::elaboration {
namespace {

    class HirDebugScopeGuard final {
    public:
        HirDebugScopeGuard(
            std::vector<std::string>& scopes,
            std::string name)
            : scopes_(scopes)
            , active_(!name.empty())
        {
            if (active_) {
                scopes_.push_back(std::move(name));
            }
        }

        HirDebugScopeGuard(const HirDebugScopeGuard&) = delete;
        HirDebugScopeGuard& operator=(const HirDebugScopeGuard&) = delete;

        ~HirDebugScopeGuard()
        {
            if (active_) {
                scopes_.pop_back();
            }
        }

    private:
        std::vector<std::string>& scopes_;
        bool active_ { };
    };

    std::optional<semantic::sv::TypeReference>
    hir_container_leaf_type(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::DeclarationId declaration_id,
        const std::span<const semantic::CompiledBindingFrame> binding_frames)
    {
        const auto declaration = specialization.find_declaration(
            declaration_id);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->type) {
            return std::nullopt;
        }
        const auto& source = *declaration->systemverilog;
        auto type = semantic::CompiledDesignResolver {
            specialization, binding_frames
        }.effective_systemverilog_type(*source.type, source.scope)
                        .value_or(*source.type);
        std::size_t depth { };
        while (type.container_form) {
            if (++depth > 32U) {
                return std::nullopt;
            }
            if (type.container_element_types.size() > 1U) {
                return std::nullopt;
            }
            if (!type.container_element_types.empty()) {
                type = type.container_element_types.front();
                continue;
            }
            type.container_form.reset();
            type.queue_maximum.reset();
            type.associative_index.reset();
            type.unpacked_dimensions.clear();
            type.container_element_types.clear();
        }
        return type;
    }

    std::optional<semantic::TypeId> hir_container_nominal_type_id(
        const ContainerType& type)
    {
        constexpr auto prefix = std::string_view { "@sv-type:" };
        if (!type.element_nominal_type.starts_with(prefix)) {
            return std::nullopt;
        }
        std::uint32_t value { };
        const auto spelling = std::string_view {
            type.element_nominal_type }.substr(prefix.size());
        const auto [end, error] = std::from_chars(
            spelling.data(), spelling.data() + spelling.size(), value);
        return error == std::errc { }
                && end == spelling.data() + spelling.size()
            ? std::optional { semantic::TypeId::from_index(value) }
            : std::nullopt;
    }

} // namespace

Lowerer::SizedIntegralComparison Lowerer::size_integral_comparison(
    InsideIntegralOperand lhs,
    InsideIntegralOperand rhs)
{
    const auto width = std::max(lhs.width, rhs.width);
    const bool signed_value = lhs.signed_value && rhs.signed_value;
    if (lhs.width != width) {
        lhs.value = resize_register(lhs.value, width, signed_value);
    }
    if (rhs.width != width) {
        rhs.value = resize_register(rhs.value, width, signed_value);
    }
    return {
        lhs.value,
        rhs.value,
        signed_value,
    };
}

std::optional<ContainerRegisterId>
Lowerer::lower_hir_container_assignment_pattern(
    const semantic::ExpressionId expression_id,
    const HirContainerObjectBinding& target)
{
    const auto root = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!root || root->systemverilog == nullptr || target.type == nullptr) {
        return std::nullopt;
    }
    const auto root_span = hir_source_span(root->systemverilog->source);
    if (root->systemverilog->kind
        != semantic::sv::ExpressionKind::assignment_pattern) {
        return std::nullopt;
    }
    const auto& root_pattern = *root->systemverilog;
    const auto keyed_association = [](const auto& association) {
        return !association.choice_spelling.empty()
            || !association.choices.empty();
    };
    const auto scalar_elements
        = target.type->element_width != 0U
        && (target.type->element_kind == ContainerElementKind::Packed
            || target.type->element_kind
                == ContainerElementKind::Scalar);
    const bool aggregate_elements = target.type->element_kind
        == ContainerElementKind::Aggregate;
    const bool string_elements = target.type->element_kind
        == ContainerElementKind::String;
    if (!scalar_elements && !aggregate_elements && !string_elements
        && !target.type->fixed) {
        const auto has_key = std::ranges::any_of(
            root_pattern.associations, keyed_association);
        const auto has_position = std::ranges::any_of(
            root_pattern.associations,
            [&](const auto& association) {
                return !keyed_association(association);
            });
        if (target.type->associative && has_position) {
            report(
                "FSIM-ELAB-SVPATTERN-003",
                "an associative assignment-pattern member requires one "
                "locally constant key",
                root_span);
        } else if (!target.type->associative && has_key) {
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "dynamic-array and queue assignment patterns require "
                "positional members",
                root_span);
        }
        return std::nullopt;
    }
    if (!scalar_elements && !aggregate_elements && !string_elements) {
        return std::nullopt;
    }
    const auto element_type = hir_container_leaf_type(
        *specialized_hir_unit_, target.declaration,
        hir_generic_binding_frames_);
    const auto dimensions = target.type->dimensions.empty()
        ? std::vector<ContainerDimension> {
              { target.type->index_left, target.type->index_right }
          }
        : target.type->dimensions;
    if (dimensions.empty()) {
        return std::nullopt;
    }
    const auto destination = allocate_container_register(*target.type);
    const auto domain = target.type->two_state
        ? frontend::ValueDomain::Bit2
        : frontend::ValueDomain::Logic4;

    std::function<bool(semantic::ExpressionId, const ContainerType&,
        ContainerRegisterId, RegisterId, std::vector<std::uint32_t>,
        bool, bool)>
        emit_aggregate_value;
    emit_aggregate_value = [&](const semantic::ExpressionId value_id,
                               const ContainerType& aggregate,
                               const ContainerRegisterId output,
                               const RegisterId index,
                               std::vector<std::uint32_t> path,
                               const bool signed_index,
                               const bool linear_index) {
        if (aggregate.element_kind != ContainerElementKind::Aggregate
            || aggregate.element_types.empty()
            || (!aggregate.member_names.empty()
                && aggregate.member_names.size()
                    != aggregate.element_types.size())) {
            return false;
        }
        const auto expression = specialized_hir_unit_->find_expression(
            value_id);
        if (!expression || expression->systemverilog == nullptr) {
            return false;
        }
        const auto& source = *expression->systemverilog;
        const bool pattern = source.kind
            == semantic::sv::ExpressionKind::assignment_pattern;
        std::vector<std::optional<semantic::ExpressionId>> values(
            aggregate.element_types.size());
        if (!pattern) {
            const auto count = aggregate.union_aggregate
                ? std::size_t { 1U }
                : values.size();
            std::fill_n(values.begin(), count, value_id);
        } else {
            bool positional { };
            bool keyed { };
            std::size_t positional_index { };
            bool positional_overflow { };
            std::optional<semantic::ExpressionId> default_value;
            for (const auto& association : source.associations) {
                if (!keyed_association(association)) {
                    positional = true;
                    if (positional_index < values.size()) {
                        values[positional_index++] = association.value;
                    } else {
                        positional_overflow = true;
                    }
                    continue;
                }
                keyed = true;
                if (association.choice_spelling == "default") {
                    if (default_value) {
                        report(
                            "FSIM-ELAB-SVPATTERN-005",
                            "an unpacked aggregate pattern permits one "
                            "default member",
                            hir_source_span(association.source));
                        return false;
                    }
                    default_value = association.value;
                    continue;
                }
                std::string_view member_name = association.choice_spelling;
                if (member_name == "@key"
                    && association.choices.size() == 1U) {
                    const auto choice = specialized_hir_unit_->find_expression(
                        association.choices.front());
                    if (choice && choice->systemverilog != nullptr
                        && choice->systemverilog->kind
                            == semantic::sv::ExpressionKind::name) {
                        member_name = choice->systemverilog->text;
                    }
                }
                const auto member = std::ranges::find(
                    aggregate.member_names, member_name);
                if (member == aggregate.member_names.end()) {
                    report(
                        "FSIM-ELAB-SVPATTERN-007",
                        "an unpacked aggregate pattern names unknown member '"
                            + std::string { member_name } + "'",
                        hir_source_span(association.source));
                    return false;
                }
                const auto ordinal = static_cast<std::size_t>(
                    std::distance(aggregate.member_names.begin(), member));
                if (values[ordinal]) {
                    report(
                        "FSIM-ELAB-SVPATTERN-007",
                        "unpacked aggregate pattern member keys must be "
                        "unique",
                        hir_source_span(association.source));
                    return false;
                }
                values[ordinal] = association.value;
            }
            if (positional && keyed) {
                report(
                    "FSIM-ELAB-SVPATTERN-004",
                    "an aggregate assignment pattern cannot mix positional "
                    "and named members",
                    hir_source_span(source.source));
                return false;
            }
            if (positional_overflow) {
                report(
                    "FSIM-ELAB-SVPATTERN-002",
                    "a positional unpacked aggregate pattern must match its "
                    "member count",
                    hir_source_span(source.source));
                return false;
            }
            if (aggregate.union_aggregate) {
                const auto selected = std::ranges::count_if(
                    values, [](const auto& value) {
                        return value.has_value();
                    });
                if (selected != 1 || default_value) {
                    report(
                        "FSIM-ELAB-SVPATTERN-005",
                        "an unpacked-union assignment pattern must select "
                        "exactly one member",
                        hir_source_span(source.source));
                    return false;
                }
            } else {
                for (auto& value : values) {
                    if (!value) {
                        value = default_value;
                    }
                }
            }
        }
        for (std::size_t member { }; member < values.size(); ++member) {
            if (!values[member]) {
                if (aggregate.union_aggregate) {
                    continue;
                }
                report(
                    "FSIM-ELAB-SVPATTERN-002",
                    "an unpacked-structure assignment pattern must cover "
                    "every member",
                    hir_source_span(source.source));
                return false;
            }
            const auto& member_type = aggregate.element_types[member];
            auto member_path = path;
            member_path.push_back(static_cast<std::uint32_t>(member));
            if (member_type.element_kind
                == ContainerElementKind::Aggregate) {
                if (!emit_aggregate_value(
                        *values[member], member_type, output, index,
                        std::move(member_path), signed_index,
                        linear_index)) {
                    return false;
                }
                continue;
            }
            if ((member_type.element_kind != ContainerElementKind::Packed
                    && member_type.element_kind
                        != ContainerElementKind::Scalar)
                || member_type.element_width == 0U) {
                report(
                    "FSIM-ELAB-SVCONTAINER-024",
                    "unpacked aggregate patterns require packed or scalar "
                    "leaves",
                    hir_source_span(source.source));
                return false;
            }
            const auto member_expression
                = specialized_hir_unit_->find_expression(*values[member]);
            const auto packed_pattern = member_expression
                && member_expression->systemverilog != nullptr
                && (member_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::assignment_pattern
                    || (member_expression->systemverilog->kind
                            == semantic::sv::ExpressionKind::call
                        && member_expression->systemverilog->text.starts_with(
                            "@sv-tagged:")));
            const auto nominal_type = packed_pattern
                ? hir_container_nominal_type_id(member_type)
                : std::nullopt;
            semantic::sv::TypeReference member_reference;
            if (nominal_type) {
                member_reference.target.target = *nominal_type;
            }
            auto lowered = nominal_type
                ? lower_hir_systemverilog_packed_pattern(
                      *values[member], member_reference,
                      member_type.element_width)
                : lower_hir_expression(
                      *values[member], member_type.element_width,
                      member_type.scalar_kind);
            if (!lowered) {
                return false;
            }
            if (register_width(*lowered) != member_type.element_width) {
                lowered = resize_register(
                    *lowered, member_type.element_width,
                    member_type.signed_elements);
            }
            process_.operations.emplace_back(ContainerAggregateWrite {
                output,
                index,
                *lowered,
                std::move(member_path),
                signed_index,
                linear_index,
            });
        }
        return true;
    };

    const auto emit_value = [&](const semantic::ExpressionId value_id,
                                const std::int64_t declared_index,
                                const std::uint64_t linear_index,
                                const bool signed_index) {
        const auto index_width = target.type->associative
            ? static_cast<std::size_t>(target.type->index_width)
            : 32U;
        const auto index = allocate_register(
            index_width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            index,
            target.type->associative
                    && (signed_index || declared_index < 0)
                ? integer_value(declared_index, index_width)
                : unsigned_value(
                      dimensions.size() == 1U
                          ? static_cast<std::uint64_t>(declared_index)
                          : linear_index,
                      index_width),
        });
        if (aggregate_elements) {
            return emit_aggregate_value(
                value_id, *target.type, destination, index, { },
                target.type->associative
                    ? target.type->signed_indices
                    : true,
                target.type->fixed && dimensions.size() > 1U);
        }
        if (string_elements) {
            const auto value = lower_hir_string_expression(value_id);
            if (!value) {
                return false;
            }
            process_.operations.emplace_back(ContainerStringWrite {
                destination,
                index,
                *value,
                target.type->associative
                    ? target.type->signed_indices
                    : target.type->fixed,
                target.type->fixed && dimensions.size() > 1U,
                target.type->string_indices,
            });
            return true;
        }
        const auto source = specialized_hir_unit_->find_expression(value_id);
        const auto packed_pattern = element_type
            && source && source->systemverilog != nullptr
            && (source->systemverilog->kind
                    == semantic::sv::ExpressionKind::assignment_pattern
                || (source->systemverilog->kind
                        == semantic::sv::ExpressionKind::call
                    && source->systemverilog->text.starts_with(
                        "@sv-tagged:")));
        auto value = packed_pattern
            ? lower_hir_systemverilog_packed_pattern(
                  value_id, *element_type, target.type->element_width)
            : lower_hir_expression(
                  value_id, target.type->element_width);
        if (!value) {
            return false;
        }
        if (register_width(*value) != target.type->element_width) {
            value = resize_register(
                *value, target.type->element_width,
                hir_expression_signed(value_id));
        }
        if (register_domain(*value) != domain) {
            const auto converted = allocate_register(
                target.type->element_width, domain);
            process_.operations.emplace_back(CopyRegister {
                converted, *value });
            value = converted;
        }
        process_.operations.emplace_back(ContainerWrite {
            destination,
            index,
            *value,
            target.type->associative
                ? target.type->signed_indices
                : true,
            target.type->fixed && dimensions.size() > 1U,
            false,
        });
        return true;
    };

    if (target.type->aggregate_value) {
        // A boxed unpacked structure or union is one aggregate value, not a
        // one-element array dimension.  Its root assignment pattern names
        // members directly and must therefore bypass dimensional coverage.
        return emit_value(expression_id, 0, 0U, true)
            ? std::optional { destination }
            : std::nullopt;
    }

    if (!target.type->fixed) {
        const auto& source = *root->systemverilog;
        if (target.type->associative && target.type->string_indices) {
            report(
                "FSIM-ELAB-SVPATTERN-003",
                "string-keyed associative assignment patterns are not yet executable",
                root_span);
            return std::nullopt;
        }
        if (!target.type->associative
            && std::ranges::any_of(
                source.associations,
                [](const semantic::sv::AssignmentPatternAssociation&
                        association) {
                    return !association.choice_spelling.empty()
                        || !association.choices.empty();
                })) {
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "dynamic-array and queue assignment patterns require positional members",
                root_span);
            return std::nullopt;
        }
        if (target.type->maximum_elements
            && source.associations.size()
                > *target.type->maximum_elements) {
            report(
                "FSIM-ELAB-SVPATTERN-002",
                "an assignment pattern exceeds the bounded container capacity",
                root_span);
            return std::nullopt;
        }
        if (!target.type->associative) {
            const auto size = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                size,
                unsigned_value(source.associations.size(), 32U),
            });
            process_.operations.emplace_back(ResizeContainer {
                destination,
                size,
                std::nullopt,
                target.type->queue,
            });
        }
        std::vector<std::int64_t> keys;
        keys.reserve(source.associations.size());
        for (std::size_t ordinal { };
            ordinal < source.associations.size(); ++ordinal) {
            const auto& association = source.associations[ordinal];
            auto index_value = static_cast<std::int64_t>(ordinal);
            if (target.type->associative) {
                if (association.choice_spelling != "@key"
                    || association.choices.size() != 1U) {
                    report(
                        "FSIM-ELAB-SVPATTERN-003",
                        "an associative assignment-pattern member requires "
                        "one locally constant key",
                        hir_source_span(association.source));
                    return std::nullopt;
                }
                const auto key = hir_constant_integer(
                    association.choices.front());
                if (!key
                    || std::ranges::find(keys, *key) != keys.end()) {
                    report(
                        "FSIM-ELAB-SVPATTERN-003",
                        "associative assignment-pattern keys must be locally constant and unique",
                        hir_source_span(association.source));
                    return std::nullopt;
                }
                index_value = *key;
                keys.push_back(*key);
            }
            if (!emit_value(
                    association.value,
                    index_value,
                    static_cast<std::uint64_t>(index_value),
                    target.type->associative
                        && hir_expression_signed(
                            association.choices.front()))) {
                return std::nullopt;
            }
        }
        return destination;
    }

    std::function<bool(
        semantic::ExpressionId, std::size_t, std::uint64_t)>
        lower_dimension;
    lower_dimension = [&](const semantic::ExpressionId pattern_id,
                          const std::size_t dimension,
                          const std::uint64_t base) {
        const auto pattern = specialized_hir_unit_->find_expression(
            pattern_id);
        if (!pattern || pattern->systemverilog == nullptr
            || pattern->systemverilog->kind
                != semantic::sv::ExpressionKind::assignment_pattern) {
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "each static-array dimension requires an assignment pattern",
                pattern && pattern->systemverilog != nullptr
                    ? hir_source_span(pattern->systemverilog->source)
                    : root_span);
            return false;
        }
        const auto& source = *pattern->systemverilog;
        const auto& bounds = dimensions[dimension];
        const auto low = std::min(bounds.first, bounds.second);
        const auto high = std::max(bounds.first, bounds.second);
        const auto count = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(high) - low + 1);
        bool positional { };
        bool keyed { };
        std::optional<semantic::ExpressionId> default_value;
        std::vector<std::pair<std::int32_t, semantic::ExpressionId>>
            explicit_values;
        for (const auto& association : source.associations) {
            if (association.choice_spelling.empty()
                && association.choices.empty()) {
                positional = true;
                continue;
            }
            keyed = true;
            if (association.choice_spelling == "default"
                && association.choices.size() == 1U) {
                if (default_value) {
                    report(
                        "FSIM-ELAB-SVPATTERN-005",
                        "a static-array pattern dimension permits one default",
                        hir_source_span(association.source));
                    return false;
                }
                default_value = association.value;
                continue;
            }
            if (association.choice_spelling != "@key"
                || association.choices.size() != 1U) {
                report(
                    "FSIM-ELAB-SVPATTERN-001",
                    "a static-array assignment pattern has invalid key metadata",
                    hir_source_span(association.source));
                return false;
            }
            const auto key = hir_constant_integer(
                association.choices.front());
            if (!key) {
                report(
                    "FSIM-ELAB-SVPATTERN-006",
                    "static-array assignment-pattern keys must be locally constant",
                    hir_source_span(association.source));
                return false;
            }
            const auto normalized_key
                = *key >= std::numeric_limits<std::int32_t>::min()
                    && *key
                        <= static_cast<std::int64_t>(
                            std::numeric_limits<std::uint32_t>::max())
                ? std::optional {
                      std::bit_cast<std::int32_t>(
                          static_cast<std::uint32_t>(*key))
                  }
                : std::nullopt;
            if (!normalized_key || *normalized_key < low
                || *normalized_key > high
                || std::ranges::any_of(
                    explicit_values,
                    [&](const auto& item) {
                        return item.first == *normalized_key;
                    })) {
                report(
                    "FSIM-ELAB-SVPATTERN-007",
                    "a static-array assignment-pattern key is duplicate or out of range",
                    hir_source_span(association.source));
                return false;
            }
            explicit_values.emplace_back(
                *normalized_key, association.value);
        }
        if (positional && keyed) {
            report(
                "FSIM-ELAB-SVPATTERN-004",
                "one static-array pattern dimension cannot mix positional and keyed members",
                hir_source_span(source.source));
            return false;
        }
        if ((positional && source.associations.size() != count)
            || (keyed && !default_value)
            || (!positional && !keyed && count != 0U)) {
            report(
                keyed ? "FSIM-ELAB-SVPATTERN-005"
                      : "FSIM-ELAB-SVPATTERN-002",
                "a static-array assignment pattern does not cover its declared dimension",
                hir_source_span(source.source));
            return false;
        }
        for (std::uint64_t ordinal { }; ordinal < count; ++ordinal) {
            const auto declared = static_cast<std::int32_t>(
                static_cast<std::int64_t>(bounds.first)
                + (bounds.first >= bounds.second
                        ? -static_cast<std::int64_t>(ordinal)
                        : static_cast<std::int64_t>(ordinal)));
            std::optional<semantic::ExpressionId> value;
            if (positional) {
                value = source.associations[ordinal].value;
            } else if (const auto found = std::ranges::find_if(
                           explicit_values,
                           [&](const auto& item) {
                               return item.first == declared;
                           });
                found != explicit_values.end()) {
                value = found->second;
            } else {
                value = default_value;
            }
            const auto linear = base * count + ordinal;
            if (!value
                || (dimension + 1U < dimensions.size()
                        ? !lower_dimension(
                              *value, dimension + 1U, linear)
                        : !emit_value(*value, declared, linear, true))) {
                return false;
            }
        }
        return true;
    };
    return lower_dimension(expression_id, 0U, 0U)
        ? std::optional { destination }
        : std::nullopt;
}

namespace {

    runtime::simir::AssertionSeverity assertion_severity(
        const semantic::sv::AssertionSeverity severity)
    {
        switch (severity) {
        case semantic::sv::AssertionSeverity::note:
            return runtime::simir::AssertionSeverity::note;
        case semantic::sv::AssertionSeverity::warning:
            return runtime::simir::AssertionSeverity::warning;
        case semantic::sv::AssertionSeverity::error:
            return runtime::simir::AssertionSeverity::error;
        case semantic::sv::AssertionSeverity::failure:
            return runtime::simir::AssertionSeverity::failure;
        }
        return runtime::simir::AssertionSeverity::error;
    }

    runtime::simir::AssertionSeverity assertion_severity(
        const std::int64_t severity,
        const runtime::simir::AssertionSeverity fallback)
    {
        switch (severity) {
        case 0:
            return runtime::simir::AssertionSeverity::note;
        case 1:
            return runtime::simir::AssertionSeverity::warning;
        case 2:
            return runtime::simir::AssertionSeverity::error;
        case 3:
            return runtime::simir::AssertionSeverity::failure;
        default:
            return fallback;
        }
    }

    runtime::simir::EdgeKind event_edge(const semantic::sv::EdgeKind edge)
    {
        switch (edge) {
        case semantic::sv::EdgeKind::positive:
            return runtime::simir::EdgeKind::posedge;
        case semantic::sv::EdgeKind::negative:
            return runtime::simir::EdgeKind::negedge;
        case semantic::sv::EdgeKind::any:
            return runtime::simir::EdgeKind::any;
        }
        return runtime::simir::EdgeKind::any;
    }

    runtime::simir::OutputFormat output_format(
        const semantic::sv::OutputFormat format)
    {
        using Source = semantic::sv::OutputFormat;
        using Target = runtime::simir::OutputFormat;
        switch (format) {
        case Source::binary:
            return Target::binary;
        case Source::hexadecimal:
            return Target::hexadecimal;
        case Source::octal:
            return Target::octal;
        case Source::decimal:
            return Target::decimal;
        case Source::character:
            return Target::character;
        case Source::string:
            return Target::string;
        case Source::real_scientific:
            return Target::real_scientific;
        case Source::real_fixed:
            return Target::real_fixed;
        case Source::real_general:
            return Target::real_general;
        case Source::hierarchy:
            return Target::string;
        case Source::time:
            return Target::time;
        case Source::unformatted2:
            return Target::unformatted2;
        case Source::unformatted4:
            return Target::unformatted4;
        }
        return Target::decimal;
    }

    frontend::SystemVerilogScalarKind systemverilog_scalar_kind(
        const std::string_view spelling)
    {
        if (spelling == "shortreal") {
            return frontend::SystemVerilogScalarKind::ShortReal;
        }
        if (spelling == "real") {
            return frontend::SystemVerilogScalarKind::Real;
        }
        if (spelling == "realtime") {
            return frontend::SystemVerilogScalarKind::Realtime;
        }
        if (spelling == "time") {
            return frontend::SystemVerilogScalarKind::Time;
        }
        if (spelling == "chandle") {
            return frontend::SystemVerilogScalarKind::Chandle;
        }
        return frontend::SystemVerilogScalarKind::None;
    }

} // namespace

bool Lowerer::lower_hir_statements(
    const std::span<const semantic::StatementId> statements)
{
    bool valid = true;
    for (const auto statement : statements) {
        if (!lower_hir_statement(statement)) {
            valid = false;
            const auto record = specialized_hir_unit_ != nullptr
                ? specialized_hir_unit_->find_statement(statement)
                : std::nullopt;
            if (record && record->systemverilog != nullptr) {
                const auto origin = hir_source_span(
                    record->systemverilog->source);
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled SystemVerilog HIR statement "
                        + std::to_string(statement.value()) + " kind "
                        + std::to_string(static_cast<std::uint32_t>(
                            record->systemverilog->kind))
                        + " at source line "
                        + std::to_string(origin.begin.line)
                        + " could not be lowered",
                    origin);
            } else if (record && record->vhdl != nullptr) {
                const auto origin = hir_source_span(record->vhdl->source);
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled VHDL HIR statement "
                        + std::to_string(statement.value()) + " kind "
                        + std::to_string(static_cast<std::uint32_t>(
                            record->vhdl->kind))
                        + " at source line "
                        + std::to_string(origin.begin.line)
                        + " could not be lowered",
                    origin);
            }
        }
    }
    return valid;
}

bool Lowerer::lower_hir_force_release(
    const semantic::ExpressionId target,
    const std::optional<semantic::ExpressionId> value,
    const std::optional<RegisterId> lowered_value,
    const bool force,
    const semantic::SourceSpanId source,
    const bool vhdl_driving_value)
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(target);
    if (!expression) {
        return false;
    }
    if (expression->vhdl != nullptr) {
        const auto& selected = *expression->vhdl;
        const auto span = hir_source_span(
            selected.source.valid() ? selected.source : source);
        using Kind = semantic::vhdl::ExpressionKind;
        const bool name = selected.kind == Kind::name;
        const bool index = selected.kind == Kind::index;
        const bool slice = selected.kind == Kind::slice;
        if (!name && !index && !slice) {
            report(
                "FSIM-ELAB-VHFORCE-001",
                "VHDL force/release supports a signal, bit-select, or "
                "static part-select",
                span);
            return true;
        }
        if ((index && selected.operands.size() != 2U)
            || (slice && selected.operands.size() != 3U)) {
            return false;
        }
        const auto declaration = hir_target_declaration(target);
        const auto binding = declaration
            ? hir_runtime_binding(*declaration, hir_process_scope_, true)
            : std::nullopt;
        if (!binding || !binding->signal) {
            report(
                "FSIM-ELAB-VHFORCE-002",
                "VHDL force/release requires a visible packed signal",
                span);
            return true;
        }
        const auto member = hir_vhdl_member_selection(target);
        auto constant_selection = index || slice || member
            ? hir_constant_selection(target, hir_process_scope_)
            : std::nullopt;
        if (member && !constant_selection) {
            constant_selection = HirConstantSelection {
                member->offset, member->width
            };
        }
        if ((member
                && (member->offset
                        > std::numeric_limits<std::uint32_t>::max()
                    || member->width
                        > std::numeric_limits<std::uint32_t>::max()))
            || (constant_selection
                && constant_selection->offset
                    > std::numeric_limits<std::uint32_t>::max())) {
            report(
                "FSIM-ELAB-VHFORCE-002",
                "VHDL force/release target has no executable packed "
                "layout",
                span);
            return true;
        }
        if (slice && !constant_selection) {
            report(
                "FSIM-ELAB-VHFORCE-001",
                "VHDL force/release part-select requires static in-range "
                "bounds and width",
                span);
            return true;
        }
        const auto width = constant_selection
            ? std::optional { constant_selection->width }
            : index ? std::optional<std::size_t> { 1U }
                    : std::optional { binding->width };
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-VHFORCE-002",
                "VHDL force/release target has no executable packed "
                "layout",
                span);
            return true;
        }
        std::optional<DynamicIndex> dynamic_selection;
        if (index && !constant_selection) {
            dynamic_selection = lower_hir_dynamic_index(
                selected.operands.front(), selected.operands[1],
                member ? member->width : binding->width,
                member
                    ? static_cast<std::uint32_t>(member->offset)
                    : 0U);
            if (!dynamic_selection) {
                report(
                    "FSIM-ELAB-VHFORCE-001",
                    "VHDL force/release bit-select has no executable "
                    "index",
                    span);
                return true;
            }
        }
        const auto offset = constant_selection
            ? static_cast<std::uint32_t>(constant_selection->offset)
            : member ? static_cast<std::uint32_t>(member->offset)
                     : 0U;
        if (!force) {
            process_.operations.emplace_back(ReleaseSignalSlice {
                *binding->signal,
                dynamic_selection ? 0U : offset,
                static_cast<std::uint32_t>(*width),
                dynamic_selection,
                vhdl_driving_value,
            });
            return true;
        }
        auto force_value = lowered_value;
        if (!force_value && value) {
            force_value = lower_hir_expression(*value, *width);
        }
        if (!force_value) {
            return false;
        }
        if (register_width(*force_value) != *width) {
            force_value = resize_register(
                *force_value, *width,
                value && hir_expression_signed(*value));
        }
        const auto domain = member ? member->domain : binding->domain;
        if (is_two_state_domain(domain)
            && !is_two_state_domain(register_domain(*force_value))) {
            report(
                "FSIM-ELAB-VHFORCE-003",
                "force of a two-state target requires an explicit "
                "conversion",
                span);
            return true;
        }
        process_.operations.emplace_back(ForceSignalSlice {
            *binding->signal,
            *force_value,
            dynamic_selection ? 0U : offset,
            dynamic_selection,
            vhdl_driving_value,
        });
        return true;
    }
    if (expression->systemverilog == nullptr) {
        return false;
    }
    const auto& target_expression = *expression->systemverilog;
    const auto span = hir_source_span(
        target_expression.source.valid()
            ? target_expression.source
            : source);
    if (target_expression.kind
        == semantic::sv::ExpressionKind::concatenation) {
        if (target_expression.operands.empty()) {
            report(
                "FSIM-ELAB-SVCONCAT-001",
                "a concatenated force/release target requires at least one "
                "packed operand",
                span);
            return true;
        }
        if (!force) {
            return std::ranges::all_of(
                target_expression.operands,
                [&](const semantic::ExpressionId operand) {
                    return lower_hir_force_release(
                        operand, std::nullopt, std::nullopt, false, source);
                });
        }
        std::vector<std::size_t> widths;
        widths.reserve(target_expression.operands.size());
        std::size_t total_width { };
        for (const auto operand : target_expression.operands) {
            const auto width = hir_expression_width(
                operand, hir_process_scope_);
            if (!width || *width == 0U
                || *width > std::numeric_limits<std::uint32_t>::max()
                || total_width
                    > std::numeric_limits<std::uint32_t>::max() - *width) {
                report(
                    "FSIM-ELAB-SVCONCAT-001",
                    "every concatenated force target must have a static "
                    "nonzero packed width and the total width must fit SimIR",
                    span);
                return true;
            }
            widths.push_back(*width);
            total_width += *width;
        }
        auto combined = lowered_value;
        if (!combined && value) {
            combined = lower_hir_expression(*value, total_width);
        }
        if (!combined) {
            return false;
        }
        if (register_width(*combined) != total_width) {
            combined = resize_register(
                *combined, total_width,
                value && hir_expression_signed(*value));
        }
        std::vector<RegisterId> values(
            target_expression.operands.size());
        std::uint32_t offset { };
        for (std::size_t reverse = target_expression.operands.size();
            reverse != 0U; --reverse) {
            const auto index = reverse - 1U;
            values[index] = allocate_register(
                widths[index], register_domain(*combined));
            process_.operations.emplace_back(Extract {
                values[index],
                *combined,
                offset,
                static_cast<std::uint32_t>(widths[index]),
            });
            offset += static_cast<std::uint32_t>(widths[index]);
        }
        for (std::size_t index { };
            index < target_expression.operands.size(); ++index) {
            if (!lower_hir_force_release(
                    target_expression.operands[index],
                    std::nullopt,
                    values[index],
                    true,
                    source)) {
                return false;
            }
        }
        return true;
    }

    const auto declaration = hir_target_declaration(target);
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, hir_process_scope_, true)
        : std::nullopt;
    if (!binding || !binding->signal) {
        report(
            "FSIM-ELAB-SVFORCE-002",
            "procedural force/release requires a visible packed signal",
            span);
        return true;
    }
    const auto index = target_expression.kind
        == semantic::sv::ExpressionKind::index;
    const auto slice = target_expression.kind
        == semantic::sv::ExpressionKind::slice;
    if (target_expression.kind != semantic::sv::ExpressionKind::name
        && !index && !slice) {
        report(
            "FSIM-ELAB-SVFORCE-001",
            "procedural force/release supports a signal, bit-select, or "
            "static part-select",
            span);
        return true;
    }
    if ((index && target_expression.operands.size() != 2U)
        || (slice && target_expression.operands.size() != 3U)) {
        return false;
    }
    const auto selected = index || slice;
    const auto member = hir_systemverilog_member_selection(
        selected ? target_expression.operands.front() : target);
    auto constant_selection = selected
        ? hir_constant_selection(target, hir_process_scope_)
        : std::nullopt;
    if (member) {
        if (member->offset
                > std::numeric_limits<std::uint32_t>::max()
            || member->width
                > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-SVFORCE-002",
                "packed force/release member has no executable layout",
                span);
            return true;
        }
        if (constant_selection) {
            if (member->offset
                > std::numeric_limits<std::size_t>::max()
                    - constant_selection->offset) {
                return false;
            }
            constant_selection->offset += member->offset;
        } else if (!selected) {
            constant_selection = HirConstantSelection {
                member->offset,
                member->width,
            };
        }
    }
    if (slice && !constant_selection) {
        report(
            "FSIM-ELAB-SVFORCE-001",
            "procedural force/release part-select requires static in-range "
            "bounds and width",
            span);
        return true;
    }
    const auto width = constant_selection
        ? std::optional { constant_selection->width }
        : index  ? std::optional<std::size_t> { 1U }
        : member ? std::optional { member->width }
                 : std::optional { binding->width };
    if (!width || *width == 0U
        || *width > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    std::optional<DynamicIndex> dynamic_selection;
    if (index && !constant_selection) {
        const auto source_width = member ? member->width : binding->width;
        dynamic_selection = lower_hir_dynamic_index(
            target_expression.operands.front(),
            target_expression.operands[1],
            source_width,
            member
                ? static_cast<std::uint32_t>(member->offset)
                : 0U);
        if (!dynamic_selection) {
            return false;
        }
    }
    const auto offset = constant_selection
        ? static_cast<std::uint32_t>(constant_selection->offset)
        : member ? static_cast<std::uint32_t>(member->offset)
                 : 0U;
    if (!force) {
        process_.operations.emplace_back(ReleaseSignalSlice {
            *binding->signal,
            dynamic_selection ? 0U : offset,
            static_cast<std::uint32_t>(*width),
            dynamic_selection,
            false,
        });
        return true;
    }
    auto force_value = lowered_value;
    if (!force_value && value) {
        force_value = lower_hir_expression(*value, *width);
    }
    if (!force_value) {
        return false;
    }
    if (register_width(*force_value) != *width) {
        force_value = resize_register(
            *force_value, *width,
            value && hir_expression_signed(*value));
    }
    const auto domain = member ? member->domain : binding->domain;
    if (is_two_state_domain(domain)
        && !is_two_state_domain(register_domain(*force_value))) {
        auto value_span = span;
        if (value) {
            const auto value_expression
                = specialized_hir_unit_->find_expression(*value);
            if (value_expression
                && value_expression->systemverilog != nullptr) {
                value_span = hir_source_span(
                    value_expression->systemverilog->source);
            }
        }
        report(
            "FSIM-ELAB-SVFORCE-003",
            "force of a two-state target requires an explicit conversion",
            value_span);
        return true;
    }
    process_.operations.emplace_back(ForceSignalSlice {
        *binding->signal,
        *force_value,
        dynamic_selection ? 0U : offset,
        dynamic_selection,
        false,
    });
    return true;
}

void Lowerer::emit_deferred_assertion_action_handoff()
{
    if (!deferred_assertion_action_phase_) {
        return;
    }
    const auto fork = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Fork { });
    const auto continuation_jump = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Jump { });
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(
        WaitRegion { *deferred_assertion_action_phase_ });
    deferred_assertion_action_handoff_
        = DeferredAssertionActionHandoff {
              fork, continuation_jump, branch
          };
    deferred_assertion_action_phase_.reset();
}

std::optional<InstructionIndex>
Lowerer::finish_deferred_assertion_action_handoff()
{
    deferred_assertion_action_phase_.reset();
    if (!deferred_assertion_action_handoff_) {
        return std::nullopt;
    }
    process_.operations.emplace_back(ForkEnd { });
    const auto continuation = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto handoff = *deferred_assertion_action_handoff_;
    process_.operations[handoff.fork] = Fork {
        { handoff.branch }, runtime::simir::ForkJoinKind::none
    };
    process_.operations[handoff.continuation_jump]
        = Jump { continuation };
    deferred_assertion_action_handoff_.reset();
    return handoff.fork;
}

bool Lowerer::lower_hir_statement(
    const semantic::StatementId statement_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(statement_id);
    if (!statement) {
        return false;
    }
    semantic::SourceSpanId source;
    std::optional<semantic::ExpressionId> target;
    std::optional<semantic::ExpressionId> value;
    std::optional<semantic::ExpressionId> condition;
    std::span<const semantic::StatementId> statements;
    std::span<const semantic::StatementId> else_statements;
    std::span<const semantic::DeclarationId> declarations;
    std::optional<semantic::ScopeId> nested_scope;
    if (statement->systemverilog != nullptr) {
        const auto& input = *statement->systemverilog;
        constexpr std::string_view concurrent_assertion_action_marker {
            "\x1f"
            "fsim.concurrent-assertion-action-region|"
        };
        if (input.kind == semantic::sv::StatementKind::display
            && input.output_text.starts_with(
                concurrent_assertion_action_marker)) {
            process_.operations.emplace_back(
                WaitRegion { runtime::SchedulerPhase::reactive });
            sample_concurrent_assertion_reads_ = false;
            return true;
        }
        source = input.source;
        target = input.target;
        value = input.value;
        condition = input.condition;
        statements = input.statements;
        else_statements = input.else_statements;
        declarations = input.declarations;
        nested_scope = input.nested_scope;
    } else {
        const auto& input = *statement->vhdl;
        source = input.source;
        target = input.target;
        value = input.kind
                    == semantic::vhdl::StatementKind::signal_assignment
                && !input.waveform.empty()
            ? std::optional { input.waveform.front().value }
            : input.value;
        condition = input.condition;
        statements = input.statements;
        else_statements = input.else_statements;
        declarations = input.declarations;
        nested_scope = input.nested_scope;
    }
    const auto span = hir_source_span(source);
    std::string statement_scope;
    if (statement->vhdl != nullptr) {
        const auto& input = *statement->vhdl;
        if (input.kind != semantic::vhdl::StatementKind::block) {
            statement_scope = input.kind
                    == semantic::vhdl::StatementKind::loop
                ? input.loop_label
                : input.label;
            if (statement_scope.empty()
                && input.kind == semantic::vhdl::StatementKind::loop) {
                statement_scope = "$loop_"
                    + std::to_string(span.begin.line) + "_"
                    + std::to_string(span.begin.column) + "_"
                    + std::to_string(span.begin.offset);
            }
        }
    }
    HirDebugScopeGuard statement_scope_guard {
        local_scope_, std::move(statement_scope)
    };
    const bool lexical_block
        = (statement->systemverilog != nullptr
              && statement->systemverilog->kind
                  == semantic::sv::StatementKind::block)
        || (statement->vhdl != nullptr
            && statement->vhdl->kind
                == semantic::vhdl::StatementKind::block);
    if (!lexical_block) {
        const bool assertion
            = (statement->systemverilog != nullptr
                  && statement->systemverilog->kind
                      == semantic::sv::StatementKind::assertion)
            || (statement->vhdl != nullptr
                && statement->vhdl->kind
                    == semantic::vhdl::StatementKind::assertion);
        emit_debug_point(assertion ? DebugPointKind::assertion
                                   : DebugPointKind::statement,
            span);
    }
    if (is_hir_vhdl_access_assignment(statement_id)) {
        return lower_hir_vhdl_access_assignment(statement_id);
    }
    if (is_hir_vhdl_environment_call_path_assignment(statement_id)) {
        return lower_hir_vhdl_environment_call_path_assignment(
            statement_id);
    }
    if (reject_hir_vhdl_access_signal_escape(statement_id)) {
        return true;
    }

    const auto lower_loop_control = [&, this](
                                        const std::string_view label,
                                        const bool is_break) {
        if (loop_controls_.empty()) {
            report(
                "FSIM-ELAB-078",
                std::string { "a " }
                    + (is_break ? "break/exit" : "continue/next")
                    + " statement has no enclosing loop",
                span);
            return true;
        }
        auto context = std::prev(loop_controls_.end());
        if (!label.empty()) {
            const auto found = std::ranges::find(
                loop_controls_.rbegin(), loop_controls_.rend(),
                label, &LoopControlContext::label);
            if (found == loop_controls_.rend()) {
                report(
                    "FSIM-ELAB-080",
                    "loop-control target '" + std::string { label }
                        + "' has no enclosing loop",
                    span);
                return true;
            }
            context = std::prev(found.base());
        }
        const auto jump = static_cast<InstructionIndex>(
            process_.operations.size());
        if (!is_break && context->continue_target) {
            process_.operations.emplace_back(Jump {
                *context->continue_target });
            return true;
        }
        process_.operations.emplace_back(Jump { 0U });
        auto& jumps = is_break
            ? context->break_jumps
            : context->continue_jumps;
        jumps.push_back(jump);
        return true;
    };

    const auto lower_named_packed = [&](const std::string_view name)
        -> std::optional<RegisterId> {
        const auto read_binding = [&](const HirRuntimeBinding& binding)
            -> std::optional<RegisterId> {
            if (binding.kind == HirRuntimeBindingKind::local) {
                return binding.local;
            }
            if (!binding.signal) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                binding.width, binding.domain);
            process_.operations.emplace_back(ReadSignal {
                destination,
                *binding.signal,
                sample_concurrent_assertion_reads_
                    ? SignalReadKind::sampled
                    : SignalReadKind::current,
            });
            implicit_signal_dependencies_.push_back(*binding.signal);
            return destination;
        };
        const auto declaration
            = semantic::CompiledDesignResolver {
                  *specialized_hir_unit_, hir_generic_binding_frames_ }
                  .resolve_systemverilog(
                      name, hir_process_scope_, { }, true)
                  .unique();
        if (declaration) {
            if (const auto binding = hir_runtime_binding(
                    *declaration, hir_process_scope_, true)) {
                if (const auto lowered = read_binding(*binding)) {
                    return lowered;
                }
            }
        }
        auto signal = signals_.find(std::string { name });
        if (signal == signals_.end()) {
            const auto suffix = "." + std::string { name };
            signal = std::ranges::find_if(
                signals_,
                [&](const auto& candidate) {
                    return candidate.first.ends_with(suffix);
                });
        }
        if (signal == signals_.end()
            || signal->second >= design_.signal_info_.size()) {
            return std::nullopt;
        }
        const auto& info = design_.signal_info_[signal->second];
        const auto destination = allocate_register(
            info.width, info.source_domain);
        process_.operations.emplace_back(ReadSignal {
            destination,
            signal->second,
            sample_concurrent_assertion_reads_
                ? SignalReadKind::sampled
                : SignalReadKind::current,
        });
        implicit_signal_dependencies_.push_back(signal->second);
        return destination;
    };
    const auto lower_statement_packed_expression
        = [&](const semantic::ExpressionId expression_id,
              const std::size_t expected_width,
              const frontend::SystemVerilogScalarKind scalar_context
                  = frontend::SystemVerilogScalarKind::None)
        -> std::optional<RegisterId> {
        if (const auto lowered = lower_hir_expression(
                expression_id, expected_width, scalar_context)) {
            return lowered;
        }
        const auto expression = specialized_hir_unit_->find_expression(
            expression_id);
        if (!expression || expression->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& input = *expression->systemverilog;
        if (input.kind == semantic::sv::ExpressionKind::name) {
            return lower_named_packed(input.text);
        }
        if (input.kind != semantic::sv::ExpressionKind::call) {
            return std::nullopt;
        }
        if ((input.text == "process::self"
                || input.text == "std::process::self")
            && input.operands.empty()) {
            const auto destination = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(ProcessSelf { destination });
            return destination;
        }
        if ((input.text != ".status"
                && input.text != ".completed")
            || input.operands.size() != 1U) {
            return std::nullopt;
        }
        const auto receiver_expression
            = specialized_hir_unit_->find_expression(
                input.operands.front());
        auto receiver = receiver_expression
                && receiver_expression->systemverilog != nullptr
                && receiver_expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::name
            ? lower_named_packed(
                  receiver_expression->systemverilog->text)
            : lower_hir_expression(input.operands.front(), 64U);
        if (!receiver || register_width(*receiver) != 64U) {
            report(
                "FSIM-ELAB-SVPROCESS-001",
                "process status query receiver has no 64-bit handle value",
                hir_source_span(input.source));
            return std::nullopt;
        }
        if (input.text == ".status") {
            const auto destination = allocate_register(
                32U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(ProcessStatusQuery {
                destination, *receiver });
            return destination;
        }
        const auto destination = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ProcessCompleted {
            destination, *receiver });
        return destination;
    };
    const auto lower_process_randstate
        = [&](const semantic::ExpressionId expression_id)
        -> std::optional<StringRegisterId> {
        const auto expression = specialized_hir_unit_->find_expression(
            expression_id);
        if (!expression || expression->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& input = *expression->systemverilog;
        if (input.kind != semantic::sv::ExpressionKind::call
            || input.text != ".get_randstate"
            || input.operands.size() != 1U) {
            return std::nullopt;
        }
        const auto receiver_expression
            = specialized_hir_unit_->find_expression(
                input.operands.front());
        auto receiver = receiver_expression
                && receiver_expression->systemverilog != nullptr
                && receiver_expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::name
            ? lower_named_packed(
                  receiver_expression->systemverilog->text)
            : lower_hir_expression(input.operands.front(), 64U);
        if (!receiver || register_width(*receiver) != 64U) {
            report(
                "FSIM-ELAB-SVPROCESS-001",
                "process random-state receiver has no 64-bit handle value",
                hir_source_span(input.source));
            return std::nullopt;
        }
        const auto destination = allocate_string_register();
        process_.operations.emplace_back(ProcessGetRandState {
            destination, *receiver });
        return destination;
    };

    const auto delay_magnitude = [&](const auto& delay)
        -> std::optional<runtime::SimulationTick> {
        auto magnitude = delay.magnitude;
        if (!delay.expression) {
            return magnitude;
        }
        const auto evaluated
            = specialized_hir_unit_->evaluate_integral_expression(
                *delay.expression);
        if (!evaluated || *evaluated < 0) {
            return std::nullopt;
        }
        const auto multiplier = static_cast<std::uint64_t>(*evaluated);
        if (multiplier != 0U
            && magnitude > std::numeric_limits<std::uint64_t>::max()
                    / multiplier) {
            return std::nullopt;
        }
        return magnitude * multiplier;
    };

    const auto lower_condition = [&](const semantic::ExpressionId expression)
        -> std::optional<RegisterId> {
        const auto width = hir_expression_width(
            expression, hir_process_scope_)
                               .value_or(1U);
        auto result = lower_statement_packed_expression(
            expression, width);
        if (!result || register_width(*result) == 1U) {
            return result;
        }
        const auto zero = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalNot { zero, *result });
        const auto truth = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalNot { truth, zero });
        return truth;
    };
    const auto lower_event_wait = [&](const std::span<const semantic::sv::Sensitivity> sensitivities,
                                      const std::optional<semantic::ExpressionId> wildcard_expression,
                                      const std::span<const semantic::StatementId> wildcard_statements
                                      = { })
        -> bool {
        if (sensitivities.empty()) {
            return false;
        }
        emit_debug_point(DebugPointKind::wait, span);
        if (sensitivities.size() == 1U
            && sensitivities.front().signal == "*") {
            const auto dependencies = wildcard_expression
                ? hir_signal_dependencies(
                      *wildcard_expression, hir_process_scope_)
                : hir_statement_signal_dependencies(
                      wildcard_statements, hir_process_scope_);
            if (dependencies.empty()) {
                report(
                    "FSIM-ELAB-062",
                    "dynamic wildcard event control has no readable signal "
                    "dependencies",
                    hir_source_span(sensitivities.front().source));
                return true;
            }
            process_.operations.emplace_back(WaitOn {
                dependencies,
                std::vector<runtime::simir::EdgeKind>(
                    dependencies.size(),
                    runtime::simir::EdgeKind::any),
            });
            return true;
        }

        std::vector<SignalId> named_events;
        named_events.reserve(sensitivities.size());
        const bool all_named_events = std::ranges::all_of(
            sensitivities,
            [&](const semantic::sv::Sensitivity& sensitivity) {
                if (sensitivity.edge != semantic::sv::EdgeKind::any) {
                    return false;
                }
                const auto event = sensitivity.expression
                    ? hir_systemverilog_event_signal(
                          *sensitivity.expression, hir_process_scope_)
                    : std::optional<SignalId> { };
                if (!event) {
                    return false;
                }
                named_events.push_back(*event);
                return true;
            });
        if (all_named_events) {
            process_.operations.emplace_back(WaitOn {
                std::move(named_events),
                std::vector<runtime::simir::EdgeKind>(
                    sensitivities.size(), runtime::simir::EdgeKind::any),
            });
            return true;
        }

        const auto general = std::ranges::find_if(
            sensitivities,
            [](const semantic::sv::Sensitivity& sensitivity) {
                return sensitivity.expression.has_value();
            });
        if (general == sensitivities.end()) {
            std::vector<SignalId> signals;
            std::vector<runtime::simir::EdgeKind> edges;
            signals.reserve(sensitivities.size());
            edges.reserve(sensitivities.size());
            for (const auto& sensitivity : sensitivities) {
                const auto found = signals_.find(sensitivity.signal);
                if (found == signals_.end()) {
                    report(
                        "FSIM-ELAB-059",
                        "unknown wait signal '" + sensitivity.signal + "'",
                        hir_source_span(sensitivity.source));
                    return true;
                }
                if (sensitivity.edge != semantic::sv::EdgeKind::any
                    && design_.signal_info_[found->second].width != 1U) {
                    report(
                        "FSIM-ELAB-060",
                        "dynamic edge-qualified wait signal '"
                            + sensitivity.signal + "' must be scalar",
                        hir_source_span(sensitivity.source));
                    return true;
                }
                signals.push_back(found->second);
                edges.push_back(event_edge(sensitivity.edge));
            }
            process_.operations.emplace_back(WaitOn {
                std::move(signals), std::move(edges) });
            return true;
        }
        struct EventExpressionState {
            std::optional<semantic::ExpressionId> expression;
            std::optional<SignalId> signal;
            semantic::sv::EdgeKind edge { semantic::sv::EdgeKind::any };
            std::size_t width { };
            RegisterId baseline { };
        };
        std::vector<EventExpressionState> expressions;
        std::vector<SignalId> dependencies;
        expressions.reserve(sensitivities.size());
        for (const auto& sensitivity : sensitivities) {
            EventExpressionState state;
            state.expression = sensitivity.expression;
            state.edge = sensitivity.edge;
            std::optional<std::size_t> width;
            const auto dependency_start = dependencies.size();
            if (state.expression) {
                width = hir_expression_width(
                    *state.expression, hir_process_scope_);
                auto expression_dependencies = hir_signal_dependencies(
                    *state.expression, hir_process_scope_);
                dependencies.insert(
                    dependencies.end(),
                    expression_dependencies.begin(),
                    expression_dependencies.end());
            } else {
                const auto found = signals_.find(sensitivity.signal);
                if (found == signals_.end()) {
                    report(
                        "FSIM-ELAB-059",
                        "unknown wait signal '" + sensitivity.signal + "'",
                        hir_source_span(sensitivity.source));
                    return true;
                }
                state.signal = found->second;
                width = design_.signal_info_[found->second].width;
                dependencies.push_back(found->second);
            }
            if (dependencies.size() == dependency_start) {
                report(
                    "FSIM-ELAB-SVEVENT-001",
                    "packed event expression has no readable signal "
                    "dependencies",
                    hir_source_span(sensitivity.source));
                return true;
            }
            if (!width || *width == 0U || *width > 64U
                || (state.edge != semantic::sv::EdgeKind::any
                    && *width != 1U)) {
                report(
                    "FSIM-ELAB-SVEVENT-003",
                    state.edge == semantic::sv::EdgeKind::any
                        ? "packed event expression must have an executable "
                          "width from 1 through 64 bits"
                        : "edge-qualified event expression must be an "
                          "executable scalar",
                    hir_source_span(sensitivity.source));
                return true;
            }
            state.width = *width;
            std::optional<RegisterId> baseline;
            if (state.expression) {
                baseline = lower_hir_expression(
                    *state.expression, state.width);
            } else {
                baseline = allocate_register(
                    state.width,
                    design_.signal_info_[*state.signal].source_domain);
                process_.operations.emplace_back(ReadSignal {
                    *baseline, *state.signal });
            }
            if (!baseline) {
                return false;
            }
            state.baseline = *baseline;
            expressions.push_back(std::move(state));
        }
        std::ranges::sort(dependencies);
        dependencies.erase(
            std::ranges::unique(dependencies).begin(),
            dependencies.end());
        const auto evaluation = static_cast<InstructionIndex>(
            process_.operations.size() + 1U);
        process_.operations.emplace_back(WaitOn {
            dependencies,
            std::vector<runtime::simir::EdgeKind>(
                dependencies.size(), runtime::simir::EdgeKind::any),
        });
        const auto logical_not = [&](const RegisterId input) {
            const auto output = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LogicalNot { output, input });
            return output;
        };
        const auto logical = [&](const LogicalBinaryOperator operation,
                                 const RegisterId lhs,
                                 const RegisterId rhs) {
            const auto output = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LogicalBinary {
                operation, output, lhs, rhs });
            return output;
        };
        std::vector<RegisterId> matches;
        matches.reserve(expressions.size());
        for (auto& state : expressions) {
            std::optional<RegisterId> current;
            if (state.expression) {
                current = lower_hir_expression(
                    *state.expression, state.width);
            } else {
                current = allocate_register(
                    state.width,
                    design_.signal_info_[*state.signal].source_domain);
                process_.operations.emplace_back(ReadSignal {
                    *current, *state.signal });
            }
            if (!current) {
                return false;
            }
            const auto equal = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                equal,
                state.baseline,
                *current,
            });
            auto matched = logical_not(equal);
            if (state.edge != semantic::sv::EdgeKind::any) {
                const auto zero = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                const auto one = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(
                    LoadConstant { zero, unsigned_value(0U, 1U) });
                process_.operations.emplace_back(
                    LoadConstant { one, unsigned_value(1U, 1U) });
                const auto previous_forbidden = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                const auto current_forbidden = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::case_equal,
                    previous_forbidden,
                    state.baseline,
                    state.edge == semantic::sv::EdgeKind::positive
                        ? one
                        : zero,
                });
                process_.operations.emplace_back(Binary {
                    BinaryOperator::case_equal,
                    current_forbidden,
                    *current,
                    state.edge == semantic::sv::EdgeKind::positive
                        ? zero
                        : one,
                });
                matched = logical(
                    LogicalBinaryOperator::logical_and,
                    matched,
                    logical_not(previous_forbidden));
                matched = logical(
                    LogicalBinaryOperator::logical_and,
                    matched,
                    logical_not(current_forbidden));
            }
            matches.push_back(matched);
            process_.operations.emplace_back(CopyRegister {
                state.baseline, *current });
        }
        auto matched = matches.front();
        for (std::size_t index = 1U; index < matches.size(); ++index) {
            matched = logical(
                LogicalBinaryOperator::logical_or,
                matched,
                matches[index]);
        }
        const auto branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            matched, 0U, 0U, UnknownBranchPolicy::when_false });
        const auto rewait = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto dependency_count = dependencies.size();
        process_.operations.emplace_back(WaitOn {
            std::move(dependencies),
            std::vector<runtime::simir::EdgeKind>(
                dependency_count, runtime::simir::EdgeKind::any),
        });
        process_.operations.emplace_back(Jump { evaluation });
        const auto satisfied = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch] = Branch {
            matched,
            satisfied,
            rewait,
            UnknownBranchPolicy::when_false,
        };
        return true;
    };
    const auto lower_repeated_event_wait = [&](const auto& sensitivities,
                                               const std::optional<semantic::ExpressionId> event,
                                               const semantic::ExpressionId limit_expression,
                                               const std::span<const semantic::StatementId> wildcard_statements
                                               = { }) -> bool {
        auto limit = lower_hir_expression(limit_expression, 32U);
        if (!limit) {
            return false;
        }
        if (register_width(*limit) != 32U) {
            limit = resize_register(
                *limit, 32U, hir_expression_signed(limit_expression));
        }
        const auto counter = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        const auto one = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant { counter, unsigned_value(0U, 32U) });
        process_.operations.emplace_back(
            LoadConstant { one, unsigned_value(1U, 32U) });
        const auto loop = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto active = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary {
            hir_expression_signed(limit_expression)
                ? BinaryOperator::less_signed
                : BinaryOperator::less_unsigned,
            active,
            counter,
            *limit,
        });
        const auto branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            active, 0U, 0U, UnknownBranchPolicy::when_false });
        const auto wait = static_cast<InstructionIndex>(
            process_.operations.size());
        if (!lower_event_wait(
                sensitivities, event, wildcard_statements)) {
            return false;
        }
        const auto next = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_signed,
            next,
            counter,
            one,
        });
        process_.operations.emplace_back(CopyRegister { counter, next });
        process_.operations.emplace_back(Jump { loop });
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch] = Branch {
            active, wait, end, UnknownBranchPolicy::when_false
        };
        return true;
    };
    const auto lower_assignment_event_control = [&](const semantic::sv::Statement& input) -> bool {
        if (input.assignment_control
            != semantic::sv::AssignmentControl::event) {
            return true;
        }
        if (!input.assignment_control_repeated) {
            return lower_event_wait(input.sensitivities, input.value);
        }
        return input.loop_limit
            && lower_repeated_event_wait(
                input.sensitivities, input.value, *input.loop_limit);
    };
    const auto lower_assignment = [&](const bool signal_assignment,
                                      const bool nonblocking) {
        if (signal_assignment && statement->vhdl != nullptr && target
            && !value && statement->vhdl->waveform.empty()) {
            // A retained null waveform is an explicit no-update branch of a
            // conditional or selected concurrent assignment.
            return true;
        }
        if (!target || !value) {
            return false;
        }
        const auto target_expression = specialized_hir_unit_->find_expression(
            *target);
        if (!target_expression) {
            return false;
        }
        const auto vhdl_array_call = statement->vhdl != nullptr
            ? hir_vhdl_array_selection(*target)
            : std::nullopt;
        const auto composite_target_width = [&]()
            -> std::optional<std::size_t> {
            if (target_expression->systemverilog == nullptr) {
                return std::nullopt;
            }
            const auto& expression = *target_expression->systemverilog;
            std::vector<semantic::ExpressionId> elements;
            if (expression.kind
                    == semantic::sv::ExpressionKind::assignment_pattern) {
                elements.reserve(expression.associations.size());
                for (const auto& association : expression.associations) {
                    elements.push_back(association.value);
                }
            } else if (expression.kind
                    == semantic::sv::ExpressionKind::call
                && (expression.text == "@stream-left"
                    || expression.text == "@stream-right")
                && expression.operands.size() >= 2U) {
                elements.assign(
                    expression.operands.begin() + 1U,
                    expression.operands.end());
            } else {
                return std::nullopt;
            }
            if (elements.empty()) {
                return std::size_t { 0U };
            }
            std::size_t width { };
            for (const auto element : elements) {
                const auto element_width = hir_expression_width(
                    element, hir_process_scope_);
                if (!element_width || *element_width == 0U
                    || *element_width
                        > std::numeric_limits<std::uint32_t>::max() - width) {
                    return std::nullopt;
                }
                width += *element_width;
            }
            return width;
        }();
        if (composite_target_width) {
            const auto& expression = *target_expression->systemverilog;
            const auto streaming = expression.kind
                    == semantic::sv::ExpressionKind::call
                && (expression.text == "@stream-left"
                    || expression.text == "@stream-right");
            if (*composite_target_width == 0U
                || signal_assignment || nonblocking
                || statement->systemverilog == nullptr
                || statement->systemverilog->assignment_control
                    != semantic::sv::AssignmentControl::none
                || statement->systemverilog->delay
                || statement->systemverilog->update_kind
                    != semantic::sv::UpdateKind::none) {
                return false;
            }
            const auto source_width = hir_expression_width(
                *value, hir_process_scope_)
                                          .value_or(*composite_target_width);
            if (streaming && source_width < *composite_target_width) {
                report(
                    "FSIM-ELAB-SVSTREAM-002",
                    "streaming assignment source is smaller than its "
                    "destination",
                    span);
                return true;
            }
            auto lowered = lower_hir_expression(
                *value,
                streaming ? source_width : *composite_target_width);
            if (!lowered) {
                return false;
            }
            if (streaming
                && register_width(*lowered) > *composite_target_width) {
                const auto selected = allocate_register(
                    *composite_target_width, register_domain(*lowered));
                process_.operations.emplace_back(Extract {
                    selected,
                    *lowered,
                    static_cast<std::uint32_t>(
                        register_width(*lowered)
                        - *composite_target_width),
                    static_cast<std::uint32_t>(*composite_target_width),
                });
                lowered = selected;
            } else if (register_width(*lowered)
                != *composite_target_width) {
                lowered = resize_register(
                    *lowered,
                    *composite_target_width,
                    hir_expression_signed(*value));
            }
            return lower_hir_packed_copy_out(*target, *lowered);
        }
        const auto event_value_expression
            = specialized_hir_unit_->find_expression(
            *value);
        const auto event_binding = [&](const semantic::ExpressionId id)
            -> std::optional<HirRuntimeBinding> {
            const auto signal = hir_systemverilog_event_signal(
                id, hir_process_scope_);
            const auto declaration = hir_referenced_declaration(id);
            if (!signal || !declaration) {
                return std::nullopt;
            }
            const auto binding = hir_runtime_binding(
                *declaration, hir_process_scope_, true);
            if (!binding || binding->signal != signal) {
                return std::nullopt;
            }
            return binding;
        };
        const auto event_target = event_binding(*target);
        const auto event_source = event_binding(*value);
        const bool null_event = event_value_expression
            && event_value_expression->systemverilog != nullptr
            && event_value_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::class_null;
        if (event_target || event_source) {
            if (!event_target || (!event_source && !null_event)) {
                report(
                    "FSIM-ELAB-SVEVENT-009",
                    event_target
                        ? "named-event assignment requires an event variable "
                          "or null source"
                        : "named-event assignment requires an event-variable "
                          "target",
                    span);
                return true;
            }
            const auto& input = *statement->systemverilog;
            if (signal_assignment || nonblocking
                || input.assignment_kind
                    != semantic::sv::AssignmentKind::blocking
                || input.assignment_control
                    != semantic::sv::AssignmentControl::none
                || input.delay) {
                report(
                    "FSIM-ELAB-SVEVENT-010",
                    "named-event alias assignment must be blocking and "
                    "time-free",
                    span);
                return true;
            }
            process_.operations.emplace_back(EventAlias {
                *event_target->signal,
                null_event ? SignalId { } : *event_source->signal,
                !null_event,
            });
            return true;
        }
        const bool event_controlled
            = statement->systemverilog != nullptr
            && statement->systemverilog->assignment_control
                == semantic::sv::AssignmentControl::event;
        const bool event_control_lowered_early
            = event_controlled
            && statement->systemverilog->update_kind
                == semantic::sv::UpdateKind::none;
        if (event_control_lowered_early
            && !lower_assignment_event_control(
                *statement->systemverilog)) {
            return false;
        }
        const auto lower_intra_assignment_control = [&] {
            return event_control_lowered_early
                || statement->systemverilog == nullptr
                || lower_assignment_event_control(
                    *statement->systemverilog);
        };
        if (statement->systemverilog != nullptr
            && statement->systemverilog->update_kind
                != semantic::sv::UpdateKind::none) {
            const auto& input = *statement->systemverilog;
            const auto normalized
                = specialized_hir_unit_->find_expression(*value);
            if (signal_assignment || nonblocking
                || input.assignment_kind
                    != semantic::sv::AssignmentKind::blocking
                || input.update_operator.empty()
                || !normalized || normalized->systemverilog == nullptr
                || normalized->systemverilog->kind
                    != semantic::sv::ExpressionKind::binary
                || normalized->systemverilog->text
                    != input.update_operator
                || normalized->systemverilog->operands.size() != 2U) {
                report(
                    "FSIM-ELAB-106",
                    "procedural update metadata does not match its "
                    "normalized binary expression",
                    span);
                return true;
            }
            const auto captured = capture_hir_packed_update_target(*target);
            if (!captured) {
                return false;
            }
            if (event_controlled
                && !lower_assignment_event_control(input)) {
                return false;
            }
            const auto updated = lower_hir_packed_update_value(
                *captured,
                input.update_operator,
                normalized->systemverilog->operands.back());
            if (!updated) {
                return false;
            }
            if (!event_controlled
                && !lower_intra_assignment_control()) {
                return false;
            }
            if (input.assignment_control
                == semantic::sv::AssignmentControl::delay) {
                if (!input.delay || !input.delay->additional.empty()) {
                    return false;
                }
                const auto delay = delay_magnitude(
                    input.delay->primary);
                if (!delay) {
                    return false;
                }
                emit_debug_point(DebugPointKind::wait, span);
                process_.operations.emplace_back(WaitFor { *delay });
            } else if (input.delay) {
                return false;
            }
            return write_hir_packed_update_target(
                *captured, *updated);
        }
        if (target_expression->systemverilog != nullptr) {
            const auto& selected = *target_expression->systemverilog;
            const bool index
                = selected.kind == semantic::sv::ExpressionKind::index;
            const bool slice
                = selected.kind == semantic::sv::ExpressionKind::slice;
            const auto operand_count = index ? 2U : slice ? 3U
                                                          : 0U;
            const auto element = operand_count != 0U
                    && selected.operands.size() == operand_count
                ? hir_container_element_binding(
                      selected.operands.front())
                : std::nullopt;
            const auto packed_element = element
                && element->selected_type != nullptr
                && (element->selected_type->element_kind
                        == ContainerElementKind::Packed
                    || element->selected_type->element_kind
                        == ContainerElementKind::Scalar);
            if (packed_element) {
                const auto& input = *statement->systemverilog;
                const auto selection = hir_constant_selection(
                    *target, hir_process_scope_);
                if (signal_assignment || nonblocking
                    || input.assignment_control
                        != semantic::sv::AssignmentControl::none
                    || input.delay
                    || input.update_kind
                        != semantic::sv::UpdateKind::none
                    || element->read_only || !selection
                    || selection->width == 0U
                    || selection->offset > element->width
                    || selection->width
                        > element->width - selection->offset
                    || selection->offset
                        > std::numeric_limits<std::uint32_t>::max()
                    || selection->width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                auto current = lower_hir_expression(
                    selected.operands.front(), element->width);
                auto lowered = lower_hir_expression(
                    *value, selection->width);
                if (!current || !lowered) {
                    return false;
                }
                if (register_width(*current) != element->width) {
                    current = resize_register(
                        *current, element->width,
                        element->signed_value);
                }
                if (register_width(*lowered) != selection->width) {
                    lowered = resize_register(
                        *lowered, selection->width,
                        hir_expression_signed(*value));
                }
                if (register_domain(*lowered) != element->domain) {
                    const auto converted = allocate_register(
                        selection->width, element->domain);
                    process_.operations.emplace_back(CopyRegister {
                        converted, *lowered });
                    lowered = converted;
                }
                const auto updated = allocate_register(
                    element->width, element->domain);
                process_.operations.emplace_back(CopyRegister {
                    updated, *current });
                process_.operations.emplace_back(Insert {
                    updated,
                    updated,
                    *lowered,
                    static_cast<std::uint32_t>(selection->offset),
                });
                return lower_hir_packed_copy_out(
                    selected.operands.front(), updated);
            }
        }
        const auto container_value_expression
            = specialized_hir_unit_->find_expression(
                *value);
        const auto container = hir_container_object_binding(*target);
        if (statement->systemverilog != nullptr
            && container && container_value_expression
            && container_value_expression->systemverilog != nullptr
            && container_value_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::assignment_pattern) {
            const auto& input = *statement->systemverilog;
            if (signal_assignment || nonblocking
                || input.assignment_kind
                    != semantic::sv::AssignmentKind::blocking
                || input.assignment_control
                    != semantic::sv::AssignmentControl::none
                || input.delay
                || input.update_kind != semantic::sv::UpdateKind::none) {
                return false;
            }
            if (container->read_only) {
                report(
                    "FSIM-ELAB-SVPORT-009",
                    "an input container port is read-only",
                    span);
                return true;
            }
            const auto lowered = lower_hir_container_assignment_pattern(
                *value, *container);
            if (!lowered) {
                return !diagnostics_.empty();
            }
            if (container->local) {
                process_.operations.emplace_back(CopyContainerRegister {
                    *container->local, *lowered });
            } else {
                const auto destination = allocate_container_register(
                    *container->type);
                process_.operations.emplace_back(CopyContainerRegister {
                    destination, *lowered });
                process_.operations.emplace_back(WriteContainerObject {
                    container->object, destination, std::nullopt });
            }
            return true;
        }
        const auto locator_assignment
            = statement->systemverilog != nullptr
            && container_value_expression
            && container_value_expression->systemverilog != nullptr
            && container_value_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::call
            && (container_value_expression->systemverilog->text == ".min"
                || container_value_expression->systemverilog->text
                    == ".max"
                || container_value_expression->systemverilog->text
                    == ".unique"
                || container_value_expression->systemverilog->text
                    == ".unique_index"
                || container_value_expression->systemverilog->text
                    == ".find"
                || container_value_expression->systemverilog->text
                    == ".find_index"
                || container_value_expression->systemverilog->text
                    == ".find_first"
                || container_value_expression->systemverilog->text
                    == ".find_first_index"
                || container_value_expression->systemverilog->text
                    == ".find_last"
                || container_value_expression->systemverilog->text
                    == ".find_last_index");
        if (locator_assignment) {
            const auto& input = *statement->systemverilog;
            if (signal_assignment || nonblocking
                || input.assignment_kind
                    != semantic::sv::AssignmentKind::blocking
                || input.assignment_control
                    != semantic::sv::AssignmentControl::none
                || input.delay
                || input.update_kind != semantic::sv::UpdateKind::none) {
                return false;
            }
            return lower_hir_container_locator_assignment(
                *target, *value);
        }
        const auto target_static_type
            = hir_static_container_expression_type(*target);
        const auto value_static_type
            = hir_static_container_expression_type(*value);
        const auto unresolved_static_array_call
            = !value_static_type
            && hir_static_array_function_call(*value);
        const auto unpacked_slice_candidate
            = [&](const semantic::ExpressionId id) {
            const auto expression = specialized_hir_unit_->find_expression(id);
            return expression && expression->systemverilog != nullptr
                && expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::slice
                && (expression->systemverilog->text == ":"
                    || expression->systemverilog->text == "+:"
                    || expression->systemverilog->text == "-:")
                && !expression->systemverilog->operands.empty()
                && hir_container_object_binding(
                    expression->systemverilog->operands.front());
        };
        const auto unsupported_container_slice
            = [&](const semantic::ExpressionId id) {
            const auto expression = specialized_hir_unit_->find_expression(
                id);
            if (!expression || expression->systemverilog == nullptr
                || expression->systemverilog->kind
                    != semantic::sv::ExpressionKind::slice
                || expression->systemverilog->operands.empty()) {
                return false;
            }
            const auto base = hir_container_object_binding(
                expression->systemverilog->operands.front());
            return base && base->type != nullptr && base->type->fixed
                && base->type->element_kind
                    != ContainerElementKind::Packed
                && base->type->element_kind
                    != ContainerElementKind::Scalar;
        };
        if (unsupported_container_slice(*target)
            || unsupported_container_slice(*value)) {
            report(
                "FSIM-ELAB-SVSLICE-006",
                "static-array slicing requires a packed or scalar element "
                "profile",
                span);
            return true;
        }
        const auto target_indexes_slice
            = target_expression->systemverilog != nullptr
            && target_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::index
            && !target_expression->systemverilog->operands.empty()
            && unpacked_slice_candidate(
                target_expression->systemverilog->operands.front());
        if ((unpacked_slice_candidate(*target)
                && container_value_expression->systemverilog != nullptr
                && container_value_expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::assignment_pattern)
            || target_indexes_slice) {
            report(
                "FSIM-ELAB-031",
                "an assignment target must be a packed object, bit-select, "
                "or constant part-select",
                span);
            return true;
        }
        if (statement->systemverilog != nullptr
            && (target_static_type
                || unpacked_slice_candidate(*target))
            && (value_static_type
                || unresolved_static_array_call
                || unpacked_slice_candidate(*value)
                || hir_container_object_binding(*value))
            && !hir_container_object_binding(*target)) {
            const auto& input = *statement->systemverilog;
            if (signal_assignment || nonblocking
                || input.assignment_kind
                    != semantic::sv::AssignmentKind::blocking
                || input.assignment_control
                    != semantic::sv::AssignmentControl::none
                || input.delay
                || input.update_kind != semantic::sv::UpdateKind::none) {
                return false;
            }
            const auto diagnostics_before = diagnostics_.size();
            const auto container_source
                = lower_hir_static_container_value(*value);
            if (!container_source) {
                return diagnostics_.size() != diagnostics_before;
            }
            return lower_hir_container_copy_out(
                *target, container_source->value);
        }
        if (statement->systemverilog != nullptr && container
            && container_value_expression
            && container_value_expression->systemverilog != nullptr) {
            const auto& input = *statement->systemverilog;
            const auto& container_source
                = *container_value_expression->systemverilog;
            const auto constructor_call
                = container_source.kind == semantic::sv::ExpressionKind::call
                && container_source.text == "@new-array"
                && (container_source.operands.size() == 1U
                    || container_source.operands.size() == 2U);
            const auto constructor_index
                = container_source.kind == semantic::sv::ExpressionKind::index
                && container_source.operands.size() == 2U;
            const auto constructor_name = constructor_index
                ? specialized_hir_unit_->find_expression(
                      container_source.operands.front())
                : std::nullopt;
            const auto plain_constructor = constructor_name
                && constructor_name->systemverilog != nullptr
                && constructor_name->systemverilog->kind
                    == semantic::sv::ExpressionKind::name
                && constructor_name->systemverilog->text == "new";
            const auto source_container
                = hir_container_object_binding(*value);
            const bool static_slice
                = container_source.kind == semantic::sv::ExpressionKind::slice
                && value_static_type.has_value();
            const bool typed_container_value
                = value_static_type.has_value()
                || unresolved_static_array_call;
            if (constructor_call || plain_constructor || source_container
                || static_slice || typed_container_value) {
                if (signal_assignment || nonblocking
                    || input.assignment_kind
                        != semantic::sv::AssignmentKind::blocking
                    || input.assignment_control
                        != semantic::sv::AssignmentControl::none
                    || input.delay
                    || input.update_kind
                        != semantic::sv::UpdateKind::none) {
                    return false;
                }
                if (container->read_only) {
                    report(
                        "FSIM-ELAB-SVPORT-009",
                        "an input container port is read-only",
                        span);
                    return true;
                }
                const auto destination = container->local
                    ? *container->local
                    : allocate_container_register(*container->type);
                const auto publish = [&] {
                    if (!container->local) {
                        process_.operations.emplace_back(
                            WriteContainerObject {
                                container->object,
                                destination,
                                std::nullopt,
                            });
                    }
                };
                if (static_slice || typed_container_value) {
                    const auto source_register
                        = lower_hir_static_container_actual(
                            *value, *container->type);
                    if (!source_register) {
                        if (value_static_type
                            && !value_static_type->fixed
                            && !container->type->fixed
                            && *value_static_type != *container->type) {
                            report(
                                container_source.kind
                                        == semantic::sv::ExpressionKind::call
                                    ? "FSIM-ELAB-SVFUNC-008"
                                    : "FSIM-ELAB-SVCONTAINER-010",
                                "whole-container assignment requires an "
                                "exactly compatible kind and profile",
                                hir_source_span(container_source.source));
                            return true;
                        }
                        return !diagnostics_.empty();
                    }
                    process_.operations.emplace_back(
                        CopyContainerRegister {
                            destination, *source_register });
                    publish();
                    return true;
                }
                if (source_container) {
                    if (source_container->type != nullptr
                        && source_container->type->fixed
                        && container->type->fixed) {
                        const auto source_register
                            = lower_hir_static_container_actual(
                                *value, *container->type);
                        if (!source_register) {
                            return !diagnostics_.empty();
                        }
                        process_.operations.emplace_back(
                            CopyContainerRegister {
                                destination, *source_register });
                        publish();
                        return true;
                    }
                    if (source_container->type == nullptr
                        || *source_container->type != *container->type) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-010",
                            "whole-container assignment requires an exactly "
                            "compatible kind and profile",
                            hir_source_span(container_source.source));
                        return true;
                    }
                    const auto source_register = source_container->local
                        ? *source_container->local
                        : allocate_container_register(
                              *source_container->type);
                    if (!source_container->local) {
                        process_.operations.emplace_back(
                            ReadContainerObject {
                                source_register,
                                source_container->object,
                            });
                    }
                    process_.operations.emplace_back(
                        CopyContainerRegister {
                            destination, source_register });
                    publish();
                    return true;
                }
                if (container->type->fixed || container->type->queue
                    || container->type->associative) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-014",
                        container->type->fixed
                            ? "new[size] cannot resize a static array"
                            : container->type->associative
                            ? "new[size] cannot resize an associative array"
                            : "new[size] cannot resize a queue",
                        hir_source_span(container_source.source));
                    return true;
                }
                const auto size_id = constructor_call
                    ? container_source.operands.front()
                    : container_source.operands.back();
                auto size = lower_hir_expression(size_id, 32U);
                if (!size) {
                    return false;
                }
                if (register_width(*size) != 32U) {
                    size = resize_register(
                        *size, 32U, hir_expression_signed(size_id));
                }
                std::optional<ContainerRegisterId> initializer;
                if (constructor_call
                    && container_source.operands.size() == 2U) {
                    const auto initial = hir_container_object_binding(
                        container_source.operands.back());
                    if (!initial || initial->type == nullptr
                        || *initial->type != *container->type) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-023",
                            "new[size](initializer) requires an exactly "
                            "compatible dynamic-array value",
                            hir_source_span(container_source.source));
                        return true;
                    }
                    initializer = initial->local
                        ? *initial->local
                        : allocate_container_register(*initial->type);
                    if (!initial->local) {
                        process_.operations.emplace_back(
                            ReadContainerObject {
                                *initializer, initial->object });
                    }
                }
                process_.operations.emplace_back(ResizeContainer {
                    destination, *size, initializer, false });
                publish();
                return true;
            }
        }
        if (const auto aggregate_member
            = hir_container_aggregate_selection(*target)) {
            const auto& input = *statement->systemverilog;
            if (signal_assignment || nonblocking
                || input.assignment_control
                    != semantic::sv::AssignmentControl::none
                || input.delay
                || input.update_kind != semantic::sv::UpdateKind::none) {
                return false;
            }
            if (aggregate_member->element.read_only) {
                report(
                    "FSIM-ELAB-SVPORT-009",
                    "an input container port is read-only",
                    span);
                return true;
            }
            const auto element_index = lower_hir_container_element_index(
                aggregate_member->element);
            auto lowered = lower_hir_expression(
                *value, aggregate_member->leaf.element_width);
            if (!element_index || !lowered
                || aggregate_member->leaf.element_width == 0U) {
                return false;
            }
            if (register_width(*lowered)
                != aggregate_member->leaf.element_width) {
                lowered = resize_register(
                    *lowered, aggregate_member->leaf.element_width,
                    hir_expression_signed(*value));
            }
            const auto domain = aggregate_member->leaf.two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
            if (register_domain(*lowered) != domain) {
                const auto converted = allocate_register(
                    aggregate_member->leaf.element_width, domain);
                process_.operations.emplace_back(CopyRegister {
                    converted, *lowered });
                lowered = converted;
            }
            const auto aggregate_container = aggregate_member->element.local
                ? *aggregate_member->element.local
                : allocate_container_register(
                      *aggregate_member->element.type);
            if (!aggregate_member->element.local) {
                process_.operations.emplace_back(ReadContainerObject {
                    aggregate_container,
                    aggregate_member->element.object,
                });
            }
            process_.operations.emplace_back(ContainerAggregateWrite {
                aggregate_container,
                *element_index,
                *lowered,
                aggregate_member->members,
                aggregate_member->element.indices.size() > 1U
                    || aggregate_member->element.type->signed_indices,
                aggregate_member->element.indices.size() > 1U,
            });
            if (!aggregate_member->element.local) {
                process_.operations.emplace_back(WriteContainerObject {
                    aggregate_member->element.object,
                    aggregate_container,
                    std::nullopt,
                });
            }
            return true;
        }
        if (const auto element = hir_container_element_binding(*target)) {
            const auto& input = *statement->systemverilog;
            if (signal_assignment
                || input.assignment_control
                    != semantic::sv::AssignmentControl::none
                || input.delay
                || input.update_kind != semantic::sv::UpdateKind::none) {
                return false;
            }
            if (element->read_only) {
                report(
                    "FSIM-ELAB-SVPORT-009",
                    "an input container port is read-only",
                    span);
                return true;
            }
            if (element->selected_type != nullptr
                && element->selected_type->element_kind
                    == ContainerElementKind::Aggregate) {
                if (nonblocking) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-009",
                        "container assignments must be blocking and time-free",
                        span);
                    return true;
                }
                const auto source_element
                    = hir_container_element_binding(*value);
                if (!source_element || source_element->type == nullptr
                    || source_element->selected_type == nullptr
                    || source_element->selected_type->element_kind
                        != ContainerElementKind::Aggregate
                    || *source_element->type != *element->type
                    || *source_element->selected_type
                        != *element->selected_type) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-024",
                        "an unpacked aggregate element assignment requires "
                        "an exactly compatible selected element",
                        span);
                    return true;
                }
                const auto target_index
                    = lower_hir_container_element_index(*element);
                const auto source_index
                    = lower_hir_container_element_index(*source_element);
                if (!target_index || !source_index) {
                    return false;
                }
                const auto target_container = element->local
                    ? *element->local
                    : allocate_container_register(*element->type);
                if (!element->local) {
                    process_.operations.emplace_back(ReadContainerObject {
                        target_container, element->object });
                }
                const auto source_container = source_element->local
                    ? *source_element->local
                    : allocate_container_register(*source_element->type);
                if (!source_element->local) {
                    process_.operations.emplace_back(ReadContainerObject {
                        source_container, source_element->object });
                }
                process_.operations.emplace_back(
                    CopyContainerAggregateElement {
                        target_container,
                        *target_index,
                        source_container,
                        *source_index,
                        element->indices.size() > 1U
                            || element->type->signed_indices,
                        source_element->indices.size() > 1U
                            || source_element->type->signed_indices,
                    });
                if (!element->local) {
                    process_.operations.emplace_back(WriteContainerObject {
                        element->object,
                        target_container,
                        std::nullopt,
                    });
                }
                return true;
            }
            if (element->selected_type != nullptr
                && element->selected_type->element_kind
                    == ContainerElementKind::Container
                && element->selected_type->element_types.size() == 1U) {
                if (nonblocking) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-009",
                        "container assignments must be blocking and time-free",
                        span);
                    return true;
                }
                const auto path = lower_hir_container_element_path(*element);
                if (!path) {
                    return false;
                }
                HirContainerElementBinding selected_element = *element;
                selected_element.type = path->type;
                selected_element.selected_type = path->type;
                selected_element.indices = path->indices;
                const auto element_index = lower_hir_container_element_index(
                    selected_element);
                if (!element_index) {
                    return false;
                }
                const auto& child_type
                    = element->selected_type->element_types.front();
                const auto value_expression
                    = specialized_hir_unit_->find_expression(*value);
                const auto constructor_call = value_expression
                    && value_expression->systemverilog != nullptr
                    && value_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::call
                    && value_expression->systemverilog->text == "@new-array"
                    && (value_expression->systemverilog->operands.size() == 1U
                        || value_expression->systemverilog->operands.size()
                            == 2U);
                const auto constructor_index = value_expression
                    && value_expression->systemverilog != nullptr
                    && value_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::index
                    && value_expression->systemverilog->operands.size()
                        == 2U;
                const auto constructor_name = constructor_index
                    ? specialized_hir_unit_->find_expression(
                          value_expression->systemverilog->operands.front())
                    : std::nullopt;
                const auto plain_constructor = constructor_name
                    && constructor_name->systemverilog != nullptr
                    && constructor_name->systemverilog->kind
                        == semantic::sv::ExpressionKind::name
                    && constructor_name->systemverilog->text == "new";
                std::optional<ContainerRegisterId> lowered;
                if (constructor_call || plain_constructor) {
                    if (child_type.fixed || child_type.queue
                        || child_type.associative) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-014",
                            "new[size] requires a dynamic-array target",
                            hir_source_span(
                                value_expression->systemverilog->source));
                        return true;
                    }
                    const auto size_expression = constructor_call
                        ? value_expression->systemverilog->operands.front()
                        : value_expression->systemverilog->operands.back();
                    auto size = lower_hir_expression(
                        size_expression, 32U);
                    if (!size) {
                        return false;
                    }
                    if (register_width(*size) != 32U) {
                        size = resize_register(
                            *size, 32U,
                            hir_expression_signed(size_expression));
                    }
                    lowered = allocate_container_register(child_type);
                    std::optional<ContainerRegisterId> initializer;
                    if (constructor_call
                        && value_expression->systemverilog->operands.size()
                            == 2U) {
                        initializer = lower_hir_static_container_actual(
                            value_expression->systemverilog->operands.back(),
                            child_type);
                        if (!initializer) {
                            return false;
                        }
                    }
                    process_.operations.emplace_back(ResizeContainer {
                        *lowered, *size, initializer, false });
                } else {
                    lowered = lower_hir_static_container_actual(
                        *value, child_type, element->declaration);
                    if (!lowered) {
                        return false;
                    }
                }
                process_.operations.emplace_back(ContainerElementWrite {
                    path->container,
                    *element_index,
                    *lowered,
                    path->type->signed_indices,
                });
                publish_hir_container_element_path(*element, *path);
                return true;
            }
            if (element->selected_type != nullptr
                && element->selected_type->element_kind
                    == ContainerElementKind::String) {
                if (nonblocking) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-009",
                        "container assignments must be blocking and time-free",
                        span);
                    return true;
                }
                const auto path = lower_hir_container_element_path(*element);
                const auto lowered = lower_hir_string_expression(*value);
                if (!path || !lowered) {
                    return false;
                }
                HirContainerElementBinding selected_element = *element;
                selected_element.type = path->type;
                selected_element.selected_type = path->type;
                selected_element.indices = path->indices;
                const auto element_index = lower_hir_container_element_index(
                    selected_element);
                if (!element_index) {
                    return false;
                }
                process_.operations.emplace_back(ContainerStringWrite {
                    path->container,
                    *element_index,
                    *lowered,
                    path->indices.size() > 1U
                        || path->type->signed_indices,
                    path->indices.size() > 1U,
                    path->type->string_indices,
                });
                publish_hir_container_element_path(*element, *path);
                return true;
            }
            if (nonblocking && !element->local
                && element->type != nullptr
                && !element->type->fixed
                && !element->type->associative
                && element->indices.size() == 1U
                && systemverilog_standard_
                    == frontend::StandardRevision::SystemVerilog2023) {
                report(
                    "FSIM-ELAB-SVASSIGN-001",
                    "SystemVerilog-2023 prohibits a nonblocking assignment "
                    "to an element of a dynamic array",
                    span);
                return true;
            }
            if (nonblocking && element->type != nullptr
                && element->type->associative) {
                report(
                    "FSIM-ELAB-SVCONTAINER-009",
                    "container assignments must be blocking and time-free",
                    span);
                return true;
            }
            const auto element_index
                = lower_hir_container_element_index(*element);
            const auto value_expression
                = specialized_hir_unit_->find_expression(*value);
            const auto element_type = hir_container_leaf_type(
                *specialized_hir_unit_, element->declaration,
                hir_generic_binding_frames_);
            const auto packed_pattern = element_type
                && value_expression
                && value_expression->systemverilog != nullptr
                && (value_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::assignment_pattern
                    || (value_expression->systemverilog->kind
                            == semantic::sv::ExpressionKind::call
                        && value_expression->systemverilog->text.starts_with(
                            "@sv-tagged:")));
            const auto diagnostics_before = diagnostics_.size();
            auto lowered = packed_pattern
                ? lower_hir_systemverilog_packed_pattern(
                      *value, *element_type, element->width)
                : lower_hir_expression(*value, element->width);
            if (!element_index || !lowered) {
                if (!lowered && diagnostics_.size() == diagnostics_before
                    && value_expression
                    && value_expression->systemverilog != nullptr
                    && value_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::assignment_pattern) {
                    report(
                        "FSIM-ELAB-SVPATTERN-001",
                        "an assignment pattern requires a compatible "
                        "container or packed aggregate target",
                        hir_source_span(
                            value_expression->systemverilog->source));
                }
                return diagnostics_.size() != diagnostics_before;
            }
            if (register_width(*lowered) != element->width) {
                lowered = resize_register(
                    *lowered, element->width,
                    hir_expression_signed(*value));
            }
            if (register_domain(*lowered) != element->domain) {
                const auto converted = allocate_register(
                    element->width, element->domain);
                process_.operations.emplace_back(CopyRegister {
                    converted, *lowered });
                lowered = converted;
            }
            if (element->local) {
                if (nonblocking) {
                    return false;
                }
                process_.operations.emplace_back(ContainerWrite {
                    *element->local,
                    *element_index,
                    *lowered,
                    element->indices.size() > 1U
                        || element->type->signed_indices,
                    element->indices.size() > 1U,
                    element->type->string_indices,
                });
            } else if (element->type->string_indices) {
                if (nonblocking) {
                    return false;
                }
                const auto materialized = allocate_container_register(
                    *element->type);
                process_.operations.emplace_back(ReadContainerObject {
                    materialized, element->object });
                process_.operations.emplace_back(ContainerWrite {
                    materialized,
                    *element_index,
                    *lowered,
                    element->indices.size() > 1U
                        || element->type->signed_indices,
                    element->indices.size() > 1U,
                    true,
                });
                process_.operations.emplace_back(WriteContainerObject {
                    element->object, materialized, std::nullopt });
            } else {
                process_.operations.emplace_back(
                    WriteContainerObjectElement {
                        element->object,
                        *element_index,
                        *lowered,
                        element->indices.size() > 1U
                            || element->type->signed_indices,
                        element->indices.size() > 1U,
                        nonblocking,
                        std::nullopt,
                    });
            }
            return true;
        }
        if (target_expression->systemverilog != nullptr
            && target_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::concatenation) {
            const auto& input = *statement->systemverilog;
            const auto width = hir_expression_width(
                *target, hir_process_scope_);
            if (signal_assignment || nonblocking
                || input.assignment_control
                    != semantic::sv::AssignmentControl::none
                || input.delay
                || input.update_kind != semantic::sv::UpdateKind::none
                || !width || *width == 0U) {
                return false;
            }
            auto lowered = lower_hir_expression(*value, *width);
            if (!lowered) {
                return false;
            }
            if (register_width(*lowered) != *width) {
                lowered = resize_register(
                    *lowered, *width, hir_expression_signed(*value));
            }
            return lower_hir_packed_copy_out(*target, *lowered);
        }
        if (target_expression->systemverilog != nullptr
            && target_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::call
            && target_expression->systemverilog->text.starts_with(
                "@sv-container-index:")) {
            const auto& access = *target_expression->systemverilog;
            if (signal_assignment || nonblocking
                || access.operands.size() != 2U) {
                return false;
            }
            const auto receiver = lower_hir_expression(
                access.operands[0], 64U);
            const auto index_width = hir_expression_width(
                access.operands[1], hir_process_scope_)
                                         .value_or(32U);
            const auto element_index = lower_hir_expression(
                access.operands[1], index_width);
            const auto lowered = lower_hir_expression(*value, 64U);
            if (!receiver || register_width(*receiver) != 64U
                || !element_index || !lowered
                || register_width(*lowered) != 64U) {
                return false;
            }
            if (!lower_intra_assignment_control()) {
                return false;
            }
            const auto destination = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(ClassMethodCall {
                destination,
                *receiver,
                "@container-write:"
                    + access.text.substr(std::string_view {
                        "@sv-container-index:" }
                            .size()),
                { *element_index, *lowered },
                { "", "" },
                {
                    static_cast<std::uint8_t>(
                        frontend::PortDirection::Input),
                    static_cast<std::uint8_t>(
                        frontend::PortDirection::Input),
                },
                64U,
                false,
            });
            return true;
        }
        if (target_expression->systemverilog != nullptr
            && target_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::call
            && target_expression->systemverilog->text.starts_with(
                "@sv-container-property:")) {
            const auto& access = *target_expression->systemverilog;
            const auto resize_expression
                = specialized_hir_unit_->find_expression(*value);
            if (signal_assignment || nonblocking
                || access.operands.size() != 1U
                || !resize_expression
                || resize_expression->systemverilog == nullptr
                || resize_expression->systemverilog->kind
                    != semantic::sv::ExpressionKind::index
                || resize_expression->systemverilog->operands.size()
                    != 2U) {
                return false;
            }
            const auto& resize = *resize_expression->systemverilog;
            const auto constructor
                = specialized_hir_unit_->find_expression(
                    resize.operands.front());
            if (!constructor || constructor->systemverilog == nullptr
                || constructor->systemverilog->kind
                    != semantic::sv::ExpressionKind::name
                || constructor->systemverilog->text != "new") {
                return false;
            }
            const auto receiver = lower_hir_expression(
                access.operands.front(), 64U);
            const auto size_width = hir_expression_width(
                resize.operands.back(), hir_process_scope_)
                                        .value_or(32U);
            const auto size = lower_hir_expression(
                resize.operands.back(), size_width);
            if (!receiver || register_width(*receiver) != 64U || !size) {
                return false;
            }
            if (!lower_intra_assignment_control()) {
                return false;
            }
            const auto destination = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(ClassMethodCall {
                destination,
                *receiver,
                "@container-resize:"
                    + access.text.substr(std::string_view {
                        "@sv-container-property:" }
                            .size()),
                { *size },
                { "" },
                { static_cast<std::uint8_t>(
                    frontend::PortDirection::Input) },
                64U,
                false,
            });
            return true;
        }
        if (const auto property = hir_class_property_profile(*target)) {
            if (signal_assignment || nonblocking || !property->writable) {
                return false;
            }
            auto lowered = lower_hir_expression(*value, property->width);
            if (!lowered) {
                return false;
            }
            if (register_width(*lowered) != property->width) {
                lowered = resize_register(
                    *lowered, property->width, property->signed_value);
            }
            if (property->static_storage) {
                if (!lower_intra_assignment_control()) {
                    return false;
                }
                process_.operations.emplace_back(ClassStaticPropertyWrite {
                    *lowered, property->identity });
                return true;
            }
            const auto receiver = lower_hir_expression(
                *property->receiver, 64U);
            if (!receiver || register_width(*receiver) != 64U) {
                return false;
            }
            if (!lower_intra_assignment_control()) {
                return false;
            }
            process_.operations.emplace_back(ClassPropertyWrite {
                *receiver, *lowered, property->identity });
            return true;
        }
        const auto declaration = vhdl_array_call
            ? std::optional { vhdl_array_call->declaration }
            : hir_target_declaration(*target);
        const auto direct_binding = hir_direct_signal_binding(*target);
        if (!declaration && !direct_binding) {
            return false;
        }
        const auto mode_view_target_binding = direct_binding
            ? direct_binding
            : declaration
            ? hir_runtime_binding(
                  *declaration, hir_process_scope_, true)
            : std::optional<HirRuntimeBinding> { };
        if (statement->vhdl != nullptr && signal_assignment
            && mode_view_target_binding
            && mode_view_target_binding->signal
            && *mode_view_target_binding->signal
                < design_.signal_info_.size()) {
            const auto target_path = [&](const auto& self,
                                         const semantic::ExpressionId id)
                -> std::optional<std::string> {
                const auto expression
                    = specialized_hir_unit_->find_expression(id);
                if (!expression || expression->vhdl == nullptr) {
                    return std::nullopt;
                }
                const auto& vhdl_expression = *expression->vhdl;
                if (vhdl_expression.kind
                    == semantic::vhdl::ExpressionKind::name) {
                    return vhdl_expression.text;
                }
                if ((vhdl_expression.kind
                        == semantic::vhdl::ExpressionKind::index
                        || vhdl_expression.kind
                            == semantic::vhdl::ExpressionKind::slice)
                    && !vhdl_expression.operands.empty()) {
                    return self(self, vhdl_expression.operands.front());
                }
                constexpr auto member_prefix
                    = std::string_view { "@vhdl-member:" };
                if (vhdl_expression.kind
                        == semantic::vhdl::ExpressionKind::call
                    && vhdl_expression.text.starts_with(member_prefix)
                    && vhdl_expression.operands.size() == 1U) {
                    auto base = self(
                        self, vhdl_expression.operands.front());
                    if (!base) {
                        return std::nullopt;
                    }
                    *base += '.';
                    *base += vhdl_expression.text.substr(
                        member_prefix.size());
                    return base;
                }
                return std::nullopt;
            };
            const auto normalize_path = [](const std::string_view path) {
                std::string result;
                result.reserve(path.size());
                std::size_t index { };
                while (index < path.size()) {
                    if (path[index] != '(') {
                        result += path[index++];
                        continue;
                    }
                    const auto close = path.find(')', index + 1U);
                    if (close == std::string_view::npos) {
                        result.append(path.substr(index));
                        break;
                    }
                    index = close + 1U;
                }
                return result;
            };
            const auto path_prefix = [](const std::string_view prefix,
                                        const std::string_view candidate) {
                return candidate == prefix
                    || (candidate.size() > prefix.size()
                        && candidate.starts_with(prefix)
                        && candidate[prefix.size()] == '.');
            };
            auto relative = target_path(target_path, *target);
            if (relative) {
                const auto full_target
                    = relative->starts_with(hierarchy_ + ".")
                    ? *relative
                    : hierarchy_ + "." + *relative;
                const auto normalized_target
                    = normalize_path(full_target);
                const auto& info
                    = design_.signal_info_[
                        *mode_view_target_binding->signal];
                for (const auto& view_binding :
                     info.vhdl_mode_view_bindings) {
                    for (const auto& endpoint : view_binding.elements) {
                        const auto normalized_endpoint
                            = normalize_path(endpoint.formal_path);
                        if ((!path_prefix(
                                 normalized_target,
                                 normalized_endpoint)
                                && !path_prefix(
                                    normalized_endpoint,
                                    normalized_target))
                            || endpoint.direction
                                != frontend::PortDirection::Input) {
                            continue;
                        }
                        report(
                            "FSIM-ELAB-VHVIEW-007",
                            "VHDL mode-view input endpoint '"
                                + endpoint.formal_path
                                + "' is read-only within instance '"
                                + hierarchy_ + "'",
                            span);
                        return true;
                    }
                }
            }
        }
        if (statement->vhdl != nullptr) {
            const auto target_width = hir_vhdl_expression_runtime_width(
                *target, hir_process_scope_);
            if (target_width && *target_width == 0U) {
                const auto assigned
                    = specialized_hir_unit_->find_expression(*value);
                const auto null_aggregate = assigned
                    && assigned->vhdl != nullptr
                    && assigned->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate;
                const auto value_width = null_aggregate
                    ? std::optional<std::size_t> { 0U }
                    : hir_vhdl_expression_runtime_width(
                          *value, hir_process_scope_);
                if (value_width && *value_width == 0U) {
                    return true;
                }
            }
        }
        if (declaration && statement->systemverilog != nullptr
            && hir_string_binding(
                *declaration, hir_process_scope_, true)) {
            const auto& input = *statement->systemverilog;
            if (signal_assignment || nonblocking
                || input.assignment_kind
                    != semantic::sv::AssignmentKind::blocking
                || input.assignment_control
                    != semantic::sv::AssignmentControl::none
                || input.delay) {
                report(
                    "FSIM-ELAB-SVSTRING-013",
                    "string assignments must be blocking and time-free",
                    span);
                return true;
            }
        }
        const semantic::sv::Delay* driver_delay = nullptr;
        const semantic::sv::Delay* net_delay = nullptr;
        bool continuous_assignment = false;
        bool delayed_nonblocking_assignment = false;
        if (statement->systemverilog != nullptr) {
            const auto& input = *statement->systemverilog;
            continuous_assignment = input.assignment_kind
                == semantic::sv::AssignmentKind::continuous;
            delayed_nonblocking_assignment = input.assignment_kind
                == semantic::sv::AssignmentKind::nonblocking;
            if (input.delay) {
                driver_delay = &*input.delay;
            }
            if (continuous_assignment && declaration) {
                const auto target_declaration
                    = specialized_hir_unit_->find_declaration(
                        *declaration);
                if (target_declaration
                    && target_declaration->systemverilog != nullptr
                    && target_declaration->systemverilog->delay) {
                    net_delay
                        = &*target_declaration->systemverilog->delay;
                }
            }
        }
        std::optional<TransitionDelays> transition_delays;
        std::optional<runtime::SimulationTick> procedural_delay;
        struct VhdlProjectedAssignment {
            runtime::SimulationTick delay { };
            runtime::SimulationTick rejection { };
            ProjectedDelayMode mode { ProjectedDelayMode::inertial };
        };
        std::optional<VhdlProjectedAssignment> vhdl_projected_assignment;
        const auto transition_delay = [&](const semantic::sv::Delay& delay)
            -> std::optional<TransitionDelays> {
            if (delay.additional.size() > 2U) {
                return std::nullopt;
            }
            const auto rise = delay_magnitude(delay.primary);
            if (!rise) {
                return std::nullopt;
            }
            auto fall = rise;
            if (!delay.additional.empty()) {
                fall = delay_magnitude(delay.additional[0].primary);
                if (!fall) {
                    return std::nullopt;
                }
            }
            auto turnoff = std::min(*rise, *fall);
            if (delay.additional.size() > 1U) {
                const auto explicit_turnoff = delay_magnitude(
                    delay.additional[1].primary);
                if (!explicit_turnoff) {
                    return std::nullopt;
                }
                turnoff = *explicit_turnoff;
            }
            return TransitionDelays { *rise, *fall, turnoff };
        };
        if (continuous_assignment && (driver_delay || net_delay)) {
            const auto driver = driver_delay
                ? transition_delay(*driver_delay)
                : std::optional<TransitionDelays> { };
            const auto net = net_delay
                ? transition_delay(*net_delay)
                : std::optional<TransitionDelays> { };
            if ((driver_delay && !driver) || (net_delay && !net)) {
                return false;
            }
            transition_delays = driver ? driver : net;
            if (driver && net) {
                const auto combine = [&](const runtime::SimulationTick lhs,
                                         const runtime::SimulationTick rhs)
                    -> std::optional<runtime::SimulationTick> {
                    if (lhs > std::numeric_limits<
                                  runtime::SimulationTick>::max()
                            - rhs) {
                        return std::nullopt;
                    }
                    return lhs + rhs;
                };
                const auto rise = combine(driver->rise, net->rise);
                const auto fall = combine(driver->fall, net->fall);
                const auto turnoff = combine(
                    driver->turnoff, net->turnoff);
                if (!rise || !fall || !turnoff) {
                    report(
                        "FSIM-ELAB-SVDELAY-003",
                        "combined continuous-assignment and net-declaration "
                        "delay overflows the 64-bit simulation time range",
                        span);
                    return true;
                }
                transition_delays = TransitionDelays {
                    *rise, *fall, *turnoff
                };
            }
        } else if (driver_delay != nullptr) {
            const auto primary = delay_magnitude(driver_delay->primary);
            if (!primary) {
                return false;
            }
            procedural_delay = *primary;
        }
        if (statement->vhdl != nullptr && signal_assignment) {
            const auto& input = *statement->vhdl;
            const auto element_delay = !input.waveform.empty()
                    && input.waveform.front().delay
                ? delay_magnitude(input.waveform.front().delay->primary)
                : input.delay
                ? delay_magnitude(input.delay->primary)
                : std::optional<runtime::SimulationTick> { 0U };
            if (!element_delay) {
                return false;
            }
            const auto transport = input.delay_mechanism
                == semantic::vhdl::DelayMechanism::transport;
            const auto rejection = input.rejection_limit
                ? delay_magnitude(input.rejection_limit->primary)
                : std::optional<runtime::SimulationTick> {
                      transport ? 0U : *element_delay
                  };
            if (!rejection || *rejection > *element_delay) {
                return false;
            }
            vhdl_projected_assignment = VhdlProjectedAssignment {
                *element_delay,
                *rejection,
                transport
                    ? ProjectedDelayMode::transport
                    : ProjectedDelayMode::inertial,
            };
        }
        if (const auto string_binding = declaration
                ? hir_string_binding(
                      *declaration, hir_process_scope_, true)
                : std::nullopt) {
            const auto string_element
                = target_expression->systemverilog != nullptr
                    && target_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::index
                    && target_expression->systemverilog->operands.size()
                        == 2U
                    && hir_target_declaration(
                        target_expression->systemverilog->operands.front())
                        == declaration
                ? &*target_expression->systemverilog
                : nullptr;
            if (value
                && hir_string_format_expression_status(
                       *value, hir_process_scope_)
                    == HirStringFormatStatus::invalid) {
                return true;
            }
            if (string_element) {
                const auto& input = *statement->systemverilog;
                if (signal_assignment || nonblocking
                    || input.assignment_kind
                        != semantic::sv::AssignmentKind::blocking
                    || input.assignment_control
                        != semantic::sv::AssignmentControl::none
                    || input.delay
                    || input.update_kind
                        != semantic::sv::UpdateKind::none) {
                    return false;
                }
                if (string_binding->kind == HirStringBindingKind::object
                    && (!string_binding->object
                        || read_only_string_objects_.contains(
                            *string_binding->object))) {
                    if (string_binding->object) {
                        report(
                            "FSIM-ELAB-SVPORT-011",
                            "an input mutable string port is read-only",
                            span);
                        return true;
                    }
                    return false;
                }
                StringRegisterId string_target { };
                if (string_binding->kind == HirStringBindingKind::local) {
                    if (!string_binding->local) {
                        return false;
                    }
                    string_target = *string_binding->local;
                } else {
                    string_target = allocate_string_register();
                    process_.operations.emplace_back(ReadStringObject {
                        string_target, *string_binding->object });
                }
                const auto index_width = hir_expression_width(
                    string_element->operands.back(), hir_process_scope_)
                                             .value_or(32U);
                const auto string_index = lower_hir_expression(
                    string_element->operands.back(), index_width);
                const auto assigned
                    = specialized_hir_unit_->find_expression(*value);
                std::optional<RegisterId> byte;
                if (assigned && assigned->systemverilog != nullptr
                    && assigned->systemverilog->kind
                        == semantic::sv::ExpressionKind::string_literal
                    && assigned->systemverilog->decoded_string
                    && assigned->systemverilog->decoded_string->size()
                        == 1U) {
                    byte = allocate_register(
                        32U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        *byte,
                        unsigned_value(
                            static_cast<unsigned char>(
                                assigned->systemverilog->decoded_string
                                    ->front()),
                            32U),
                    });
                } else {
                    byte = lower_hir_expression(*value, 32U);
                }
                if (!string_index || !byte) {
                    return false;
                }
                process_.operations.emplace_back(StringReplaceByte {
                    string_target,
                    *string_index,
                    *byte,
                    hir_expression_signed(
                        string_element->operands.back()),
                });
                if (string_binding->kind == HirStringBindingKind::object) {
                    process_.operations.emplace_back(WriteStringObject {
                        *string_binding->object, string_target });
                }
                return true;
            }
            const auto process_randstate = lower_process_randstate(*value);
            if (signal_assignment || nonblocking
                || !can_lower_hir_string_expression(
                    *target, hir_process_scope_)
                || (!process_randstate
                    && !can_lower_hir_string_expression(
                        *value, hir_process_scope_))) {
                return false;
            }
            const auto lowered = process_randstate
                ? process_randstate
                : lower_hir_string_expression(*value);
            if (!lowered) {
                return false;
            }
            if (!lower_intra_assignment_control()) {
                return false;
            }
            if (string_binding->kind == HirStringBindingKind::local) {
                if (!string_binding->local) {
                    return false;
                }
                process_.operations.emplace_back(CopyStringRegister {
                    *string_binding->local, *lowered });
                return true;
            }
            if (!string_binding->object) {
                return false;
            }
            if (read_only_string_objects_.contains(
                    *string_binding->object)) {
                report(
                    "FSIM-ELAB-SVPORT-011",
                    "an input mutable string port is read-only",
                    span);
                return true;
            }
            process_.operations.emplace_back(WriteStringObject {
                *string_binding->object, *lowered });
            return true;
        }
        const auto binding = direct_binding
            ? direct_binding
            : hir_runtime_binding(
                  *declaration, hir_process_scope_, true);
        if (!binding) {
            return false;
        }
        std::span<const semantic::ExpressionId> target_operands;
        std::string_view selection_operation;
        bool index = false;
        bool slice = false;
        if (target_expression->systemverilog != nullptr) {
            const auto& selection_expression
                = *target_expression->systemverilog;
            target_operands = selection_expression.operands;
            selection_operation = selection_expression.text;
            index = selection_expression.kind
                == semantic::sv::ExpressionKind::index;
            slice = selection_expression.kind
                == semantic::sv::ExpressionKind::slice;
        } else {
            const auto& selection_expression = *target_expression->vhdl;
            target_operands = selection_expression.operands;
            selection_operation = selection_expression.text;
            index = selection_expression.kind
                == semantic::vhdl::ExpressionKind::index;
            slice = selection_expression.kind
                == semantic::vhdl::ExpressionKind::slice;
        }
        const auto target_is_selected
            = index || slice || vhdl_array_call.has_value();
        if (target_is_selected && !vhdl_array_call
            && (target_operands.size() != (index ? 2U : 3U)
                || hir_target_declaration(target_operands.front())
                    != declaration)) {
            return false;
        }
        const auto systemverilog_member
            = hir_systemverilog_member_selection(
                target_is_selected ? target_operands.front() : *target);
        const auto vhdl_member = vhdl_array_call
            ? std::optional<HirVhdlMemberSelection> { }
            : hir_vhdl_member_selection(
                  target_is_selected ? target_operands.front() : *target);
        const bool parent_vhdl_array_selection
            = target_is_selected && !target_operands.empty()
                && target_expression->vhdl != nullptr
                && hir_vhdl_array_selection(
                       target_operands.front()).has_value();
        const auto vhdl_root_selection = vhdl_member
                && vhdl_member->root.valid()
            ? hir_constant_selection(
                  vhdl_member->root, hir_process_scope_)
            : std::nullopt;
        const auto vhdl_root_expression = vhdl_member
                && vhdl_member->root.valid()
            ? specialized_hir_unit_->find_expression(vhdl_member->root)
            : std::nullopt;
        const auto vhdl_root_is_selection = vhdl_root_expression
            && vhdl_root_expression->vhdl != nullptr
            && (vhdl_root_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::index
                || vhdl_root_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::slice);
        const auto vhdl_root_dynamic_index = vhdl_root_expression
            && vhdl_root_expression->vhdl != nullptr
            && vhdl_root_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::index
            && vhdl_root_expression->vhdl->operands.size() == 2U
            && !vhdl_root_selection;
        if (vhdl_root_is_selection && !vhdl_root_selection
            && !vhdl_root_dynamic_index) {
            return false;
        }
        const auto vhdl_member_offset = [&]()
            -> std::optional<std::size_t> {
            if (!vhdl_member) {
                return std::nullopt;
            }
            if (!vhdl_root_selection) {
                return vhdl_member->offset;
            }
            if (vhdl_member->offset
                > std::numeric_limits<std::size_t>::max()
                    - vhdl_root_selection->offset) {
                return std::nullopt;
            }
            return vhdl_root_selection->offset + vhdl_member->offset;
        }();
        const auto member_offset = systemverilog_member
            ? std::optional { systemverilog_member->offset }
            : vhdl_member_offset;
        const auto member_width = systemverilog_member
            ? std::optional { systemverilog_member->width }
            : vhdl_member
            ? std::optional { vhdl_member->width }
            : std::nullopt;
        const auto member_domain = systemverilog_member
            ? std::optional { systemverilog_member->domain }
            : vhdl_member
            ? std::optional { vhdl_member->domain }
            : std::nullopt;
        const auto member_signed = systemverilog_member
            ? systemverilog_member->signed_value
            : vhdl_member && vhdl_member->signed_value;
        const auto occurrence_vhdl_array_selection = [&]()
            -> std::optional<HirConstantSelection> {
            if (target_expression->vhdl == nullptr
                || target_expression->vhdl->kind
                    != semantic::vhdl::ExpressionKind::call
                || !binding->signal
                || *binding->signal >= design_.signal_info_.size()) {
                return std::nullopt;
            }
            const auto& vhdl_expression = *target_expression->vhdl;
            const auto& info = design_.signal_info_[*binding->signal];
            const auto* array = info.vhdl_array.get();
            if (array == nullptr
                || array->dimensions.size()
                    != vhdl_expression.operands.size()
                || binding->width == 0U) {
                return std::nullopt;
            }
            std::size_t offset { };
            auto width = binding->width;
            for (std::size_t dimension_index { };
                dimension_index < vhdl_expression.operands.size();
                ++dimension_index) {
                const auto& dimension
                    = array->dimensions[dimension_index];
                if (!dimension.range || dimension.null
                    || dimension.stride == 0U
                    || dimension.stride
                        > std::numeric_limits<std::size_t>::max()) {
                    return std::nullopt;
                }
                const auto stride = static_cast<std::size_t>(
                    dimension.stride);
                const auto operand = specialized_hir_unit_
                    ->find_expression(
                        vhdl_expression.operands[dimension_index]);
                const auto* selected_range
                    = operand && operand->vhdl != nullptr
                        && operand->vhdl->kind
                            == semantic::vhdl::ExpressionKind::binary
                        && (operand->vhdl->text == "to"
                            || operand->vhdl->text == "downto")
                        && operand->vhdl->operands.size() == 2U
                    ? operand->vhdl
                    : nullptr;
                if (selected_range != nullptr) {
                    if (dimension_index + 1U
                        != vhdl_expression.operands.size()) {
                        return std::nullopt;
                    }
                    const auto left = hir_constant_integer(
                        selected_range->operands.front());
                    const auto right = hir_constant_integer(
                        selected_range->operands.back());
                    const auto descending
                        = selected_range->text == "downto";
                    if (!left || !right
                        || descending != dimension.range->descending
                        || *left < std::min(dimension.range->left,
                            dimension.range->right)
                        || *left > std::max(dimension.range->left,
                            dimension.range->right)
                        || *right < std::min(dimension.range->left,
                            dimension.range->right)
                        || *right > std::max(dimension.range->left,
                            dimension.range->right)) {
                        return std::nullopt;
                    }
                    const auto count = index_distance(*left, *right) + 1U;
                    const auto ordinal = index_distance(
                        *right, dimension.range->right);
                    if (count
                            > std::numeric_limits<std::size_t>::max()
                                / stride
                        || ordinal
                            > std::numeric_limits<std::size_t>::max()
                                / stride) {
                        return std::nullopt;
                    }
                    width = static_cast<std::size_t>(count) * stride;
                    const auto displacement
                        = static_cast<std::size_t>(ordinal) * stride;
                    if (displacement
                        > std::numeric_limits<std::size_t>::max()
                            - offset) {
                        return std::nullopt;
                    }
                    offset += displacement;
                    continue;
                }
                const auto selected_index = hir_constant_integer(
                    vhdl_expression.operands[dimension_index]);
                if (!selected_index
                    || *selected_index < std::min(dimension.range->left,
                        dimension.range->right)
                    || *selected_index > std::max(dimension.range->left,
                        dimension.range->right)) {
                    return std::nullopt;
                }
                const auto ordinal = index_distance(
                    *selected_index, dimension.range->right);
                if (ordinal
                    > std::numeric_limits<std::size_t>::max() / stride) {
                    return std::nullopt;
                }
                const auto displacement
                    = static_cast<std::size_t>(ordinal) * stride;
                if (displacement
                    > std::numeric_limits<std::size_t>::max() - offset) {
                    return std::nullopt;
                }
                offset += displacement;
                width = stride;
            }
            return offset <= binding->width
                    && width <= binding->width - offset
                ? std::optional { HirConstantSelection { offset, width } }
                : std::nullopt;
        }();
        auto constant_selection = occurrence_vhdl_array_selection
            ? occurrence_vhdl_array_selection
            : vhdl_array_call
                && vhdl_array_call->dynamic_indices.empty()
            ? std::optional { HirConstantSelection {
                  vhdl_array_call->offset, vhdl_array_call->width } }
            : target_is_selected && !vhdl_array_call
            ? hir_root_constant_selection(*target, hir_process_scope_)
            : std::nullopt;
        if (member_offset && member_width) {
            if (*member_offset
                    > std::numeric_limits<std::uint32_t>::max()
                || *member_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            if (constant_selection) {
                // Root selection composes an ordinary outer slice or index
                // with its parent array offset.  The member profile describes
                // that same parent offset and must not add it a second time.
                if (!parent_vhdl_array_selection) {
                    if (*member_offset
                        > std::numeric_limits<std::size_t>::max()
                            - constant_selection->offset) {
                        return false;
                    }
                    constant_selection->offset += *member_offset;
                }
            } else if (!target_is_selected && !vhdl_root_dynamic_index) {
                constant_selection = HirConstantSelection {
                    *member_offset,
                    *member_width,
                };
            }
        }
        if (occurrence_vhdl_array_selection) {
            // The occurrence calculation already includes every array
            // dimension.  A generic member selection may have rediscovered
            // the selected row as a root offset; discard that duplicate.
            constant_selection = occurrence_vhdl_array_selection;
        } else if (vhdl_array_call
            && vhdl_array_call->dynamic_indices.empty()) {
            // A multidimensional VHDL call is one flattened selection.  Do
            // not compose a member/root offset discovered by the generic
            // target path with the offset already produced from all of its
            // selectors.
            constant_selection = HirConstantSelection {
                vhdl_array_call->offset,
                vhdl_array_call->width,
            };
        }
        if (constant_selection
            && (constant_selection->offset > binding->width
                || constant_selection->width
                    > binding->width - constant_selection->offset)) {
            return false;
        }
        std::optional<DynamicPartIndex> dynamic_vhdl_selection;
        if (vhdl_array_call
            && !vhdl_array_call->dynamic_indices.empty()) {
            const auto offset = lower_hir_vhdl_array_offset(
                *vhdl_array_call);
            if (!offset || vhdl_array_call->root_width == 0U
                || vhdl_array_call->root_width - 1U
                    > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())
                || vhdl_array_call->width == 0U
                || vhdl_array_call->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            dynamic_vhdl_selection = DynamicPartIndex {
                *offset,
                static_cast<std::int64_t>(
                    vhdl_array_call->root_width - 1U),
                0,
                0U,
                static_cast<std::uint32_t>(vhdl_array_call->width),
                true,
                true,
            };
        }
        if (vhdl_root_dynamic_index && vhdl_member && member_offset
            && member_width) {
            const auto& root = *vhdl_root_expression->vhdl;
            const auto element_subtype = hir_vhdl_expression_subtype(
                vhdl_member->root);
            const auto element_width = element_subtype
                    && element_subtype->executable_width
                    && *element_subtype->executable_width
                        <= std::numeric_limits<std::size_t>::max()
                ? std::optional { static_cast<std::size_t>(
                      *element_subtype->executable_width) }
                : std::nullopt;
            if (!element_width || binding->width == 0U
                || binding->width - 1U
                    > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                return false;
            }
            const auto offset = lower_hir_vhdl_dynamic_element_offset(
                root.operands[0], root.operands[1], *element_width,
                static_cast<std::uint32_t>(*member_offset));
            if (!offset) {
                return false;
            }
            dynamic_vhdl_selection = DynamicPartIndex {
                *offset,
                static_cast<std::int64_t>(binding->width - 1U),
                0,
                0U,
                static_cast<std::uint32_t>(*member_width),
                true,
                true,
            };
        }
        const auto dynamic_element_width
            = target_is_selected && index && !constant_selection
                && target_expression->vhdl != nullptr
            ? hir_expression_width(*target, hir_process_scope_)
            : std::nullopt;
        if (!dynamic_vhdl_selection && dynamic_element_width
            && *dynamic_element_width > 1U) {
            if (*dynamic_element_width
                    > std::numeric_limits<std::uint32_t>::max()
                || binding->width == 0U
                || binding->width - 1U
                    > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                return false;
            }
            const auto offset = lower_hir_vhdl_dynamic_element_offset(
                target_operands[0], target_operands[1],
                *dynamic_element_width);
            if (!offset) {
                return false;
            }
            dynamic_vhdl_selection = DynamicPartIndex {
                *offset,
                static_cast<std::int64_t>(binding->width - 1U),
                0,
                0U,
                static_cast<std::uint32_t>(*dynamic_element_width),
                true,
                true,
            };
        }
        const auto vhdl_dynamic_slice_width
            = target_is_selected && slice && !constant_selection
                && target_expression->vhdl != nullptr
                && (!hir_constant_integer(target_operands[1])
                    || !hir_constant_integer(target_operands[2]))
            ? hir_expression_width(*value, hir_process_scope_)
            : std::nullopt;
        if (!dynamic_vhdl_selection && vhdl_dynamic_slice_width) {
            if (*vhdl_dynamic_slice_width == 0U
                || *vhdl_dynamic_slice_width
                    > std::numeric_limits<std::uint32_t>::max()
                || (member_offset
                    && *member_offset
                        > std::numeric_limits<std::uint32_t>::max())) {
                report(
                    "FSIM-ELAB-VHSLICE-001",
                    "a dynamic VHDL assignment slice requires a statically "
                    "sized value",
                    span);
                return true;
            }
            const auto source_width = member_width
                ? *member_width
                : binding->width;
            dynamic_vhdl_selection = lower_hir_vhdl_dynamic_slice(
                *target,
                source_width,
                *vhdl_dynamic_slice_width,
                member_offset
                    ? static_cast<std::uint32_t>(*member_offset)
                    : 0U);
            if (!dynamic_vhdl_selection) {
                return !diagnostics_.empty();
            }
        }
        const auto dynamic_part_width = slice && !constant_selection
            ? hir_dynamic_part_width(*target, hir_process_scope_)
            : std::nullopt;
        auto assignment_width = std::optional<std::size_t> { };
        if (constant_selection) {
            assignment_width = constant_selection->width;
        } else if (dynamic_vhdl_selection) {
            assignment_width = dynamic_vhdl_selection->width;
        } else if (index) {
            assignment_width = 1U;
        } else if (dynamic_part_width) {
            assignment_width = dynamic_part_width;
        } else if (!target_is_selected) {
            assignment_width = member_width
                ? member_width
                : std::optional { binding->width };
        }
        if (!assignment_width || *assignment_width == 0U
            || *assignment_width
                > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        std::optional<DynamicIndex> dynamic_selection;
        if (target_is_selected && !constant_selection
            && !dynamic_vhdl_selection) {
            const auto source_width = member_width
                ? *member_width
                : binding->width;
            dynamic_selection = lower_hir_dynamic_index(
                target_operands[0],
                target_operands[1],
                source_width,
                member_offset
                    ? static_cast<std::uint32_t>(
                          *member_offset)
                    : 0U);
            if (!dynamic_selection) {
                return false;
            }
        }
        const auto value_expression
            = specialized_hir_unit_->find_expression(*value);
        const auto target_declaration
            = specialized_hir_unit_->find_declaration(
                binding->declaration);
        const auto packed_pattern_target
            = !target_is_selected && !member_width
            && target_declaration
            && target_declaration->systemverilog != nullptr
            && target_declaration->systemverilog->type;
        const auto packed_pattern = packed_pattern_target
            ? hir_systemverilog_packed_pattern_operand(
                  *value, *target_declaration->systemverilog->type)
            : std::nullopt;
        if (packed_pattern && *packed_pattern != *value
            && systemverilog_standard_
                != frontend::StandardRevision::SystemVerilog2023) {
            report(
                "FSIM-ELAB-SVCAST-005",
                "assignment-pattern static-cast contextual typing requires "
                "the exact SystemVerilog-2023 profile",
                hir_source_span(value_expression->systemverilog->source));
            return true;
        }
        auto target_subtype
            = target_expression->vhdl != nullptr
            ? hir_vhdl_expression_subtype(*target)
            : std::nullopt;
        bool vhdl_enumeration_target { };
        if (!target_subtype && target_expression->vhdl != nullptr
            && target_declaration
            && target_declaration->vhdl != nullptr
            && target_declaration->vhdl->subtype) {
            target_subtype = hir_effective_vhdl_subtype(
                *target_declaration->vhdl->subtype);
            if (!target_subtype) {
                target_subtype = target_declaration->vhdl->subtype;
            }
        }
        if (statement->vhdl != nullptr && target_subtype) {
            if (!validate_hir_vhdl_fixed_context(
                    *value, *target_subtype)
                || !validate_hir_vhdl_float_context(
                    *value, *target_subtype)) {
                return true;
            }
            const auto physical_root = [&](const auto& self,
                                           const semantic::vhdl::SubtypeIndication&
                                               subtype,
                                           std::unordered_set<std::uint32_t>&
                                               visiting)
                -> std::optional<semantic::TypeId> {
                if (!subtype.type_mark.target.valid()
                    || !visiting.insert(
                        subtype.type_mark.target.value()).second) {
                    return std::nullopt;
                }
                const auto type = specialized_hir_unit_->find_type(
                    subtype.type_mark.target);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (type->vhdl->form
                    == semantic::vhdl::TypeForm::physical) {
                    return type->vhdl->id;
                }
                if (type->vhdl->form
                        != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form
                        != semantic::vhdl::TypeForm::alias) {
                    return std::nullopt;
                }
                return self(self, type->vhdl->base, visiting);
            };
            const auto root = [&](
                                  const semantic::vhdl::SubtypeIndication&
                                      subtype) {
                std::unordered_set<std::uint32_t> visiting;
                return physical_root(physical_root, subtype, visiting);
            };
            const auto target_physical = root(*target_subtype);
            const auto value_subtype = hir_vhdl_expression_subtype(*value);
            const auto value_physical = value_subtype
                ? root(*value_subtype)
                : std::nullopt;
            if (target_physical && value_physical
                && *target_physical != *value_physical) {
                report(
                    "FSIM-ELAB-VHPHYSICAL-009",
                    "assignment between distinct nominal physical types "
                    "is not legal",
                    span);
                return true;
            }
            const auto enumeration_root = [&](const auto& self,
                                              const semantic::vhdl::SubtypeIndication& subtype,
                                              std::unordered_set<std::uint32_t>& visiting)
                -> std::optional<semantic::TypeId> {
                if (!subtype.type_mark.target.valid()
                    || !visiting.insert(
                        subtype.type_mark.target.value()).second) {
                    return std::nullopt;
                }
                const auto type = specialized_hir_unit_->find_type(
                    subtype.type_mark.target);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (type->vhdl->form
                    == semantic::vhdl::TypeForm::enumeration) {
                    return type->vhdl->id;
                }
                if (type->vhdl->form
                        != semantic::vhdl::TypeForm::subtype
                    && type->vhdl->form
                        != semantic::vhdl::TypeForm::alias) {
                    return std::nullopt;
                }
                return self(self, type->vhdl->base, visiting);
            };
            const auto enum_root = [&](const semantic::vhdl::SubtypeIndication& subtype) {
                std::unordered_set<std::uint32_t> visiting;
                return enumeration_root(
                    enumeration_root, subtype, visiting);
            };
            const auto target_enumeration = enum_root(*target_subtype);
            vhdl_enumeration_target = target_enumeration.has_value();
            const auto value_enumeration = value_subtype
                ? enum_root(*value_subtype)
                : std::nullopt;
            const auto contextual_literal = [&] {
                if (!target_enumeration || !value_expression
                    || value_expression->vhdl == nullptr
                    || value_expression->vhdl->kind
                        != semantic::vhdl::ExpressionKind::name) {
                    return false;
                }
                const auto type = specialized_hir_unit_->find_type(
                    *target_enumeration);
                if (!type || type->vhdl == nullptr) {
                    return false;
                }
                auto spelling = std::string_view {
                    value_expression->vhdl->text
                };
                if (const auto separator = spelling.find_last_of(".:");
                    separator != std::string_view::npos) {
                    spelling.remove_prefix(separator + 1U);
                }
                return std::ranges::any_of(
                    type->vhdl->enumeration_literals,
                    [&](const auto& literal) {
                        if (spelling.starts_with('\'')) {
                            return literal.spelling == spelling;
                        }
                        return std::ranges::equal(
                            literal.spelling, spelling,
                            [](const char left, const char right) {
                                return std::tolower(
                                           static_cast<unsigned char>(left))
                                    == std::tolower(
                                        static_cast<unsigned char>(right));
                            });
                    });
            }();
            const auto unresolved_literal
                = value_expression && value_expression->vhdl != nullptr
                && value_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::name
                && !contextual_literal
                && (!value_expression->vhdl->referenced_name
                    || (!value_expression->vhdl->referenced_name->selected
                        && value_expression->vhdl->referenced_name
                               ->overloads.empty()));
            if (target_enumeration && unresolved_literal) {
                report(
                    "FSIM-ELAB-VHENUM-001",
                    "contextual VHDL enumeration type has no matching "
                    "identifier or character literal",
                    hir_source_span(value_expression->vhdl->source));
                return true;
            }
            if (target_enumeration && value_enumeration
                && *target_enumeration != *value_enumeration) {
                const auto qualified_value = value_expression
                    && value_expression->vhdl != nullptr
                    && value_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::call
                    && value_expression->vhdl->text.starts_with(
                        "@vhdl-qualified:");
                const auto value_declaration_id = value
                    ? hir_referenced_declaration(*value)
                    : std::nullopt;
                const auto value_declaration = value_declaration_id
                    ? specialized_hir_unit_->find_declaration(
                          *value_declaration_id)
                    : std::nullopt;
                const auto conversion_value = value_expression
                    && value_expression->vhdl != nullptr
                    && value_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::call
                    && !qualified_value
                    && value_declaration
                    && value_declaration->vhdl != nullptr
                    && (value_declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::type
                        || value_declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::subtype);
                report(
                    qualified_value
                        ? "FSIM-ELAB-VHQUAL-004"
                        : conversion_value
                        ? "FSIM-ELAB-VHCONV-004"
                        : "FSIM-ELAB-VHENUM-002",
                    qualified_value
                        ? "VHDL qualified expression type is incompatible "
                          "with its assignment context"
                        : conversion_value
                        ? "VHDL conversion result type is incompatible with "
                          "its assignment context"
                        : "assignment mixes values from different nominal "
                          "VHDL enumeration types",
                    span);
                return true;
            }
            const auto enumeration_range = [&](const auto& self,
                                               const semantic::vhdl::SubtypeIndication& subtype,
                                               std::unordered_set<std::uint32_t>& visiting)
                -> std::optional<frontend::EnumerationRange> {
                const auto constraint = std::ranges::find_if(
                    subtype.constraints,
                    [](const auto& range) {
                        return range.kind
                                == semantic::vhdl::RangeKind::enumeration
                            || range.kind
                                == semantic::vhdl::RangeKind::discrete;
                    });
                if (constraint != subtype.constraints.end()) {
                    const auto left = constraint->left
                        ? constraint->left
                        : constraint->left_expression
                        ? specialized_hir_unit_
                              ->evaluate_integral_expression(
                                  *constraint->left_expression)
                        : std::nullopt;
                    const auto right = constraint->right
                        ? constraint->right
                        : constraint->right_expression
                        ? specialized_hir_unit_
                              ->evaluate_integral_expression(
                                  *constraint->right_expression)
                        : std::nullopt;
                    if (left && right) {
                        return frontend::EnumerationRange {
                            *left,
                            *right,
                            constraint->descending,
                        };
                    }
                }
                if (!subtype.type_mark.target.valid()
                    || !visiting.insert(
                        subtype.type_mark.target.value()).second) {
                    return std::nullopt;
                }
                const auto type = specialized_hir_unit_->find_type(
                    subtype.type_mark.target);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (type->vhdl->scalar_range) {
                    auto base = type->vhdl->base;
                    base.constraints = { *type->vhdl->scalar_range };
                    return self(self, base, visiting);
                }
                if (type->vhdl->form
                    == semantic::vhdl::TypeForm::enumeration) {
                    return type->vhdl->enumeration_literals.empty()
                        ? std::nullopt
                        : std::optional { frontend::EnumerationRange {
                              0,
                              static_cast<std::int64_t>(
                                  type->vhdl->enumeration_literals.size()
                                  - 1U),
                              false,
                          } };
                }
                return self(self, type->vhdl->base, visiting);
            };
            std::unordered_set<std::uint32_t> range_visiting;
            const auto target_enumeration_range = target_enumeration
                ? enumeration_range(
                      enumeration_range,
                      *target_subtype,
                      range_visiting)
                : std::nullopt;
            const auto ordinal = target_enumeration_range
                ? specialized_hir_unit_->evaluate_integral_expression(
                      *value)
                : std::nullopt;
            if (target_enumeration_range && ordinal
                && (*ordinal
                        < std::min(target_enumeration_range->left,
                            target_enumeration_range->right)
                    || *ordinal
                        > std::max(target_enumeration_range->left,
                            target_enumeration_range->right))) {
                report(
                    "FSIM-ELAB-VHENUMRANGE-004",
                    "assigned VHDL enumeration value lies outside the "
                    "target subtype range",
                    span);
                return true;
            }
            const auto integer_range = [&](const auto& self,
                                           const semantic::vhdl::SubtypeIndication& subtype,
                                           std::unordered_set<std::uint32_t>& visiting)
                -> std::optional<frontend::IntegerRange> {
                const auto constraint = std::ranges::find_if(
                    subtype.constraints,
                    [](const auto& range) {
                        return range.kind
                                == semantic::vhdl::RangeKind::integer
                            || range.kind
                                == semantic::vhdl::RangeKind::discrete;
                    });
                if (constraint != subtype.constraints.end()) {
                    const auto left = constraint->left
                        ? constraint->left
                        : constraint->left_expression
                        ? specialized_hir_unit_
                              ->evaluate_integral_expression(
                                  *constraint->left_expression)
                        : std::nullopt;
                    const auto right = constraint->right
                        ? constraint->right
                        : constraint->right_expression
                        ? specialized_hir_unit_
                              ->evaluate_integral_expression(
                                  *constraint->right_expression)
                        : std::nullopt;
                    if (left && right) {
                        return frontend::IntegerRange {
                            *left,
                            *right,
                            constraint->descending,
                        };
                    }
                }
                auto spelling = std::string_view {
                    subtype.type_mark.spelling
                };
                if (const auto separator = spelling.find_last_of(".:");
                    separator != std::string_view::npos) {
                    spelling.remove_prefix(separator + 1U);
                }
                const auto same_name = [](const std::string_view left,
                                          const std::string_view right) {
                    return std::ranges::equal(
                        left, right,
                        [](const char lhs, const char rhs) {
                            return std::tolower(
                                       static_cast<unsigned char>(lhs))
                                == std::tolower(
                                    static_cast<unsigned char>(rhs));
                        });
                };
                if (same_name(spelling, "natural")) {
                    return frontend::IntegerRange {
                        0,
                        std::numeric_limits<std::int64_t>::max(),
                        false,
                    };
                }
                if (same_name(spelling, "positive")) {
                    return frontend::IntegerRange {
                        1,
                        std::numeric_limits<std::int64_t>::max(),
                        false,
                    };
                }
                if (!subtype.type_mark.target.valid()
                    || !visiting.insert(
                        subtype.type_mark.target.value()).second) {
                    return std::nullopt;
                }
                const auto type = specialized_hir_unit_->find_type(
                    subtype.type_mark.target);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                auto base = type->vhdl->base;
                if (type->vhdl->scalar_range) {
                    base.constraints = { *type->vhdl->scalar_range };
                }
                return self(self, base, visiting);
            };
            std::unordered_set<std::uint32_t> integer_visiting;
            const auto& constrained_target_subtype
                = target_declaration
                        && target_declaration->vhdl != nullptr
                        && target_declaration->vhdl->subtype
                ? *target_declaration->vhdl->subtype
                : *target_subtype;
            const auto target_integer_range = integer_range(
                integer_range,
                constrained_target_subtype,
                integer_visiting);
            const auto effective_integer_range = target_integer_range
                ? target_integer_range
                : binding->integer_range;
            const auto integer_value = effective_integer_range
                ? specialized_hir_unit_->evaluate_integral_expression(
                      *value)
                : std::nullopt;
            if (effective_integer_range && integer_value
                && (*integer_value
                        < std::min(effective_integer_range->left,
                            effective_integer_range->right)
                    || *integer_value
                        > std::max(effective_integer_range->left,
                            effective_integer_range->right))) {
                report(
                    "FSIM-ELAB-INTEGER-004",
                    "assigned VHDL integer value lies outside the target "
                    "subtype range",
                    span);
                return true;
            }
        }
        if (statement->vhdl != nullptr) {
            const auto vhdl_concatenation = value_expression
                && value_expression->vhdl != nullptr
                && (value_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::concatenation
                    || (value_expression->vhdl->kind
                            == semantic::vhdl::ExpressionKind::binary
                        && value_expression->vhdl->text == "&"));
            const auto concatenation_width = vhdl_concatenation
                ? hir_expression_width(*value, hir_process_scope_)
                : std::nullopt;
            if (concatenation_width
                && *concatenation_width != *assignment_width) {
                report(
                    "FSIM-ELAB-VHCOMPOP-003",
                    "VHDL concatenation length does not match its assignment "
                    "target",
                    span);
                return true;
            }
            const auto target_root = hir_vhdl_composite_root_type(*target);
            const auto incompatible_source
                = [&](const semantic::ExpressionId source_id) {
                      const auto source_root
                          = hir_vhdl_composite_root_type(source_id);
                      if (!target_root || !source_root) {
                          return false;
                      }
                      if (target_root->first != source_root->first
                          || target_root->second
                              != source_root->second) {
                          return true;
                      }
                      if (target_root->first
                          != semantic::vhdl::TypeForm::array) {
                          return false;
                      }
                      const auto source_width = hir_expression_width(
                          source_id, hir_process_scope_);
                      return source_width
                          && *source_width != *assignment_width;
                  };
            bool incompatible = incompatible_source(*value);
            if (value_expression && value_expression->vhdl != nullptr
                && value_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::conditional
                && value_expression->vhdl->operands.size() == 3U) {
                incompatible = incompatible
                    || incompatible_source(
                        value_expression->vhdl->operands[1])
                    || incompatible_source(
                        value_expression->vhdl->operands[2]);
            }
            if (incompatible && target_root) {
                const auto record = target_root->first
                    == semantic::vhdl::TypeForm::record;
                report(
                    record ? "FSIM-ELAB-VHCOMPOP-002"
                           : "FSIM-ELAB-VHARRAY-006",
                    record
                        ? "VHDL record assignment requires the same nominal "
                          "record type"
                        : "VHDL array assignment requires compatible nominal "
                          "type and bounds",
                    span);
                return true;
            }
        }
        const auto vhdl_aggregate
            = value_expression && value_expression->vhdl != nullptr
            && value_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate
            && target_subtype;
        const auto scalar_context = statement->systemverilog != nullptr
            ? hir_systemverilog_scalar_kind(*target)
            : frontend::SystemVerilogScalarKind::None;
        const auto source_scalar_kind = statement->systemverilog != nullptr
            ? hir_systemverilog_scalar_kind(*value)
            : frontend::SystemVerilogScalarKind::None;
        const auto real_scalar = [](const auto kind) {
            using ScalarKind = frontend::SystemVerilogScalarKind;
            return kind == ScalarKind::ShortReal
                || kind == ScalarKind::Real
                || kind == ScalarKind::Realtime;
        };
        const auto scalar_conversion = real_scalar(scalar_context)
            && real_scalar(source_scalar_kind);
        const auto source_width = scalar_conversion
            ? source_scalar_kind
                    == frontend::SystemVerilogScalarKind::ShortReal
                ? 32U
                : 64U
            : *assignment_width;
        const auto source_scalar_context = scalar_conversion
            ? source_scalar_kind
            : scalar_context;
        if (statement->vhdl != nullptr && signal_assignment
            && statement->vhdl->waveform.size() > 1U) {
            if (!binding->signal || !vhdl_projected_assignment) {
                return false;
            }
            const auto waveform_domain = member_domain
                ? *member_domain
                : binding->domain;
            std::vector<ProjectedWaveformElement> waveform;
            waveform.reserve(statement->vhdl->waveform.size());
            std::optional<runtime::SimulationTick> previous_delay;
            for (const auto& element : statement->vhdl->waveform) {
                const auto diagnostics_before = diagnostics_.size();
                std::optional<RegisterId> element_value;
                if (element.disconnect) {
                    if (waveform_domain
                        != frontend::ValueDomain::Logic9) {
                        report(
                            "FSIM-ELAB-VHDLGUARD-002",
                            "guarded driver disconnection requires a "
                            "nine-state resolved signal target",
                            hir_source_span(element.source));
                        return true;
                    }
                    const auto disconnected = allocate_register(
                        *assignment_width,
                        frontend::ValueDomain::Logic9);
                    auto disconnected_bits = PackedLogic4(
                        *assignment_width, Logic4::z);
                    disconnected_bits.fill(runtime::Logic9::z);
                    process_.operations.emplace_back(LoadConstant {
                        disconnected, std::move(disconnected_bits) });
                    element_value = disconnected;
                } else {
                    const auto element_expression
                        = specialized_hir_unit_->find_expression(
                            element.value);
                    element_value = element_expression
                            && element_expression->vhdl != nullptr
                            && element_expression->vhdl->kind
                                == semantic::vhdl::ExpressionKind::aggregate
                            && target_subtype
                        ? lower_hir_vhdl_aggregate(
                              element.value,
                              *assignment_width,
                              &*target_subtype)
                        : lower_hir_expression(
                              element.value, *assignment_width);
                }
                if (!element_value
                    || register_width(*element_value)
                        != *assignment_width) {
                    return diagnostics_.size() != diagnostics_before;
                }
                if (register_domain(*element_value) != waveform_domain) {
                    const auto converted = allocate_register(
                        *assignment_width, waveform_domain);
                    process_.operations.emplace_back(CopyRegister {
                        converted, *element_value });
                    element_value = converted;
                }
                const auto delay = element.delay
                    ? delay_magnitude(element.delay->primary)
                    : std::optional<runtime::SimulationTick> { 0U };
                if (!delay || (previous_delay
                        && *delay <= *previous_delay)) {
                    report(
                        "FSIM-ELAB-055",
                        "VHDL waveform-element delays must be strictly "
                        "ascending",
                        hir_source_span(element.source));
                    return true;
                }
                previous_delay = *delay;
                waveform.push_back(ProjectedWaveformElement {
                    *element_value, *delay });
            }
            const auto& projected = *vhdl_projected_assignment;
            if (dynamic_vhdl_selection) {
                process_.operations.emplace_back(
                    WriteProjectedWaveformDynamicSlice {
                        *binding->signal,
                        std::move(waveform),
                        DynamicIndex {
                            dynamic_vhdl_selection->base,
                            dynamic_vhdl_selection->left,
                            dynamic_vhdl_selection->right,
                            dynamic_vhdl_selection->base_offset,
                            true,
                        },
                        projected.rejection,
                        projected.mode,
                    });
            } else if (dynamic_selection) {
                process_.operations.emplace_back(
                    WriteProjectedWaveformDynamicSlice {
                        *binding->signal,
                        std::move(waveform),
                        *dynamic_selection,
                        projected.rejection,
                        projected.mode,
                    });
            } else if (constant_selection) {
                process_.operations.emplace_back(
                    WriteProjectedWaveformSlice {
                        *binding->signal,
                        std::move(waveform),
                        static_cast<std::uint32_t>(
                            constant_selection->offset),
                        projected.rejection,
                        projected.mode,
                    });
            } else {
                process_.operations.emplace_back(WriteProjectedWaveform {
                    *binding->signal,
                    std::move(waveform),
                    projected.rejection,
                    projected.mode,
                });
            }
            return true;
        }
        std::optional<RegisterId> disconnected_value;
        const auto vhdl_disconnect
            = statement->vhdl != nullptr && signal_assignment
            && statement->vhdl->waveform.size() == 1U
            && statement->vhdl->waveform.front().disconnect;
        if (vhdl_disconnect) {
            const auto target_domain = member_domain
                ? *member_domain
                : binding->domain;
            if (target_domain != frontend::ValueDomain::Logic9) {
                report(
                    "FSIM-ELAB-VHDLGUARD-002",
                    "guarded driver disconnection requires a nine-state "
                    "resolved signal target",
                    span);
                return true;
            }
            const auto destination = allocate_register(
                *assignment_width, frontend::ValueDomain::Logic9);
            auto disconnected_bits = PackedLogic4(
                *assignment_width, Logic4::z);
            disconnected_bits.fill(runtime::Logic9::z);
            process_.operations.emplace_back(LoadConstant {
                destination, std::move(disconnected_bits) });
            disconnected_value = destination;
        }
        const auto diagnostics_before = diagnostics_.size();
        auto lowered = disconnected_value
            ? disconnected_value
            : packed_pattern
            ? lower_hir_systemverilog_packed_pattern(
                  *packed_pattern,
                  *target_declaration->systemverilog->type,
                  *assignment_width)
            : vhdl_aggregate
            ? lower_hir_vhdl_aggregate(
                  *value, *assignment_width, &*target_subtype)
            : statement->systemverilog != nullptr
            ? lower_statement_packed_expression(
                  *value, source_width, source_scalar_context)
            : lower_hir_expression(
                  *value, *assignment_width, scalar_context);
        if (!lowered) {
            if (packed_pattern && value_expression
                && value_expression->systemverilog != nullptr
                && value_expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::assignment_pattern) {
                report(
                    "FSIM-ELAB-SVPATTERN-001",
                    "an assignment pattern requires a compatible container "
                    "or packed aggregate target",
                    hir_source_span(
                        value_expression->systemverilog->source));
                return true;
            }
            return diagnostics_.size() != diagnostics_before;
        }
        if (scalar_conversion
            && source_scalar_kind != scalar_context) {
            const auto converted = allocate_register(
                *assignment_width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(SystemVerilogScalarBinary {
                runtime::SystemVerilogScalarBinaryOperator::Convert,
                converted,
                *lowered,
                *lowered,
                source_scalar_kind,
                source_scalar_kind,
                scalar_context,
            });
            lowered = converted;
        }
        if (register_width(*lowered) != *assignment_width) {
            lowered = resize_register(
                *lowered, *assignment_width,
                member_width
                    ? member_signed
                    : binding->signed_value);
        }
        const auto assignment_domain = member_domain
            ? *member_domain
            : binding->domain;
        if (register_domain(*lowered) != assignment_domain) {
            if (is_two_state_domain(assignment_domain)
                && !is_two_state_domain(register_domain(*lowered))) {
                lowered = convert_to_two_state(*lowered);
            } else {
                const auto converted = allocate_register(
                    *assignment_width, assignment_domain);
                process_.operations.emplace_back(CopyRegister {
                    converted, *lowered });
                lowered = converted;
            }
        }
        if (systemverilog_member && systemverilog_member->tagged
            && !target_is_selected) {
            const auto& tagged = *systemverilog_member->tagged;
            if (tagged.width == 0U || tagged.tag_width == 0U
                || tagged.width
                    > std::numeric_limits<std::uint32_t>::max()
                || tagged.tag_width
                    > std::numeric_limits<std::uint32_t>::max()
                || tagged.payload_width
                    > std::numeric_limits<std::uint32_t>::max()
                || tagged.offset > binding->width
                || tagged.width > binding->width - tagged.offset) {
                return false;
            }
            const auto tagged_value = allocate_register(
                tagged.width, binding->domain);
            process_.operations.emplace_back(LoadConstant {
                tagged_value,
                PackedLogic4(tagged.width, Logic4::zero),
            });
            process_.operations.emplace_back(Insert {
                tagged_value,
                tagged_value,
                *lowered,
                0U,
            });
            const auto tag_value = allocate_register(
                tagged.tag_width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                tag_value,
                unsigned_value(tagged.tag_value, tagged.tag_width),
            });
            process_.operations.emplace_back(Insert {
                tagged_value,
                tagged_value,
                tag_value,
                static_cast<std::uint32_t>(tagged.payload_width),
            });
            lowered = tagged_value;
            assignment_width = tagged.width;
            constant_selection
                = tagged.offset == 0U
                    && tagged.width == binding->width
                ? std::optional<HirConstantSelection> { }
                : std::optional { HirConstantSelection {
                      tagged.offset, tagged.width } };
        }
        if (!target_is_selected && !member_width && binding->integer_range
            && (binding->domain == frontend::ValueDomain::Integer
                || (binding->domain == frontend::ValueDomain::Bit2
                    && vhdl_enumeration_target))) {
            process_.operations.emplace_back(IntegerCheck {
                *lowered,
                std::min(binding->integer_range->left,
                    binding->integer_range->right),
                std::max(binding->integer_range->left,
                    binding->integer_range->right),
            });
        }
        if (!lower_intra_assignment_control()) {
            return false;
        }
        if (procedural_delay && !delayed_nonblocking_assignment) {
            emit_debug_point(DebugPointKind::wait, span);
            process_.operations.emplace_back(WaitFor {
                *procedural_delay });
        }
        if (binding->kind == HirRuntimeBindingKind::local) {
            if (nonblocking || continuous_assignment || !binding->local) {
                return false;
            }
            if (constant_selection) {
                process_.operations.emplace_back(Insert {
                    *binding->local,
                    *binding->local,
                    *lowered,
                    static_cast<std::uint32_t>(
                        constant_selection->offset),
                });
            } else if (index && dynamic_selection) {
                process_.operations.emplace_back(DynamicInsert {
                    *binding->local,
                    *binding->local,
                    *lowered,
                    *dynamic_selection,
                });
            } else if (slice && dynamic_selection && dynamic_part_width) {
                process_.operations.emplace_back(DynamicPartInsert {
                    *binding->local,
                    *binding->local,
                    *lowered,
                    DynamicPartIndex {
                        dynamic_selection->index,
                        dynamic_selection->left,
                        dynamic_selection->right,
                        dynamic_selection->base_offset,
                        static_cast<std::uint32_t>(*dynamic_part_width),
                        selection_operation == "+:",
                        dynamic_selection->left
                            >= dynamic_selection->right,
                    },
                });
            } else if (dynamic_vhdl_selection) {
                process_.operations.emplace_back(DynamicPartInsert {
                    *binding->local,
                    *binding->local,
                    *lowered,
                    *dynamic_vhdl_selection,
                });
            } else {
                process_.operations.emplace_back(
                    CopyRegister { *binding->local, *lowered });
            }
            return true;
        }
        if (!binding->signal) {
            return false;
        }
        const auto update = signal_assignment || nonblocking;
        if (constant_selection && vhdl_projected_assignment) {
            process_.operations.emplace_back(WriteProjectedSlice {
                *binding->signal,
                *lowered,
                static_cast<std::uint32_t>(constant_selection->offset),
                vhdl_projected_assignment->delay,
                vhdl_projected_assignment->rejection,
                vhdl_projected_assignment->mode,
            });
        } else if (index && dynamic_selection
            && vhdl_projected_assignment) {
            process_.operations.emplace_back(WriteProjectedDynamicSlice {
                *binding->signal,
                *lowered,
                *dynamic_selection,
                vhdl_projected_assignment->delay,
                vhdl_projected_assignment->rejection,
                vhdl_projected_assignment->mode,
            });
        } else if (dynamic_vhdl_selection
            && vhdl_projected_assignment) {
            process_.operations.emplace_back(WriteProjectedDynamicSlice {
                *binding->signal,
                *lowered,
                DynamicIndex {
                    dynamic_vhdl_selection->base,
                    dynamic_vhdl_selection->left,
                    dynamic_vhdl_selection->right,
                    dynamic_vhdl_selection->base_offset,
                    true,
                },
                vhdl_projected_assignment->delay,
                vhdl_projected_assignment->rejection,
                vhdl_projected_assignment->mode,
            });
        } else if (slice && dynamic_selection
            && vhdl_projected_assignment) {
            process_.operations.emplace_back(WriteProjectedDynamicSlice {
                *binding->signal,
                *lowered,
                *dynamic_selection,
                vhdl_projected_assignment->delay,
                vhdl_projected_assignment->rejection,
                vhdl_projected_assignment->mode,
            });
        } else if (vhdl_projected_assignment) {
            process_.operations.emplace_back(WriteProjected {
                *binding->signal,
                *lowered,
                vhdl_projected_assignment->delay,
                vhdl_projected_assignment->rejection,
                vhdl_projected_assignment->mode,
            });
        } else if (constant_selection && transition_delays) {
            process_.operations.emplace_back(WriteInertialSlice {
                *binding->signal,
                *lowered,
                static_cast<std::uint32_t>(constant_selection->offset),
                *transition_delays,
            });
        } else if (index && dynamic_selection && transition_delays) {
            process_.operations.emplace_back(WriteInertialDynamicSlice {
                *binding->signal,
                *lowered,
                *dynamic_selection,
                *transition_delays,
            });
        } else if (slice && dynamic_selection && dynamic_part_width
            && transition_delays) {
            process_.operations.emplace_back(
                WriteInertialDynamicPartSlice {
                    *binding->signal,
                    *lowered,
                    DynamicPartIndex {
                        dynamic_selection->index,
                        dynamic_selection->left,
                        dynamic_selection->right,
                        dynamic_selection->base_offset,
                        static_cast<std::uint32_t>(*dynamic_part_width),
                        selection_operation == "+:",
                        dynamic_selection->left
                            >= dynamic_selection->right,
                    },
                    *transition_delays,
                });
        } else if (dynamic_vhdl_selection && transition_delays) {
            process_.operations.emplace_back(
                WriteInertialDynamicPartSlice {
                    *binding->signal,
                    *lowered,
                    *dynamic_vhdl_selection,
                    *transition_delays,
                });
        } else if (transition_delays) {
            process_.operations.emplace_back(WriteInertial {
                *binding->signal, *lowered, *transition_delays });
        } else if (constant_selection && procedural_delay
            && delayed_nonblocking_assignment) {
            process_.operations.emplace_back(WriteAfterSlice {
                *binding->signal,
                *lowered,
                static_cast<std::uint32_t>(constant_selection->offset),
                *procedural_delay,
            });
        } else if (index && dynamic_selection && procedural_delay
            && delayed_nonblocking_assignment) {
            process_.operations.emplace_back(WriteAfterDynamicSlice {
                *binding->signal,
                *lowered,
                *dynamic_selection,
                *procedural_delay,
            });
        } else if (slice && dynamic_selection && dynamic_part_width
            && procedural_delay && delayed_nonblocking_assignment) {
            process_.operations.emplace_back(
                WriteAfterDynamicPartSlice {
                    *binding->signal,
                    *lowered,
                    DynamicPartIndex {
                        dynamic_selection->index,
                        dynamic_selection->left,
                        dynamic_selection->right,
                        dynamic_selection->base_offset,
                        static_cast<std::uint32_t>(*dynamic_part_width),
                        selection_operation == "+:",
                        dynamic_selection->left
                            >= dynamic_selection->right,
                    },
                    *procedural_delay,
                });
        } else if (dynamic_vhdl_selection && procedural_delay
            && delayed_nonblocking_assignment) {
            process_.operations.emplace_back(
                WriteAfterDynamicPartSlice {
                    *binding->signal,
                    *lowered,
                    *dynamic_vhdl_selection,
                    *procedural_delay,
                });
        } else if (procedural_delay && delayed_nonblocking_assignment) {
            process_.operations.emplace_back(WriteAfter {
                *binding->signal, *lowered, *procedural_delay });
        } else if (constant_selection && update) {
            process_.operations.emplace_back(WriteUpdateSlice {
                *binding->signal,
                *lowered,
                static_cast<std::uint32_t>(constant_selection->offset),
            });
        } else if (constant_selection) {
            process_.operations.emplace_back(WriteBlockingSlice {
                *binding->signal,
                *lowered,
                static_cast<std::uint32_t>(constant_selection->offset),
            });
        } else if (index && dynamic_selection && update) {
            process_.operations.emplace_back(WriteUpdateDynamicSlice {
                *binding->signal, *lowered, *dynamic_selection });
        } else if (index && dynamic_selection) {
            process_.operations.emplace_back(WriteBlockingDynamicSlice {
                *binding->signal, *lowered, *dynamic_selection });
        } else if (slice && dynamic_selection && dynamic_part_width) {
            const auto part = DynamicPartIndex {
                dynamic_selection->index,
                dynamic_selection->left,
                dynamic_selection->right,
                dynamic_selection->base_offset,
                static_cast<std::uint32_t>(*dynamic_part_width),
                selection_operation == "+:",
                dynamic_selection->left >= dynamic_selection->right,
            };
            if (update) {
                process_.operations.emplace_back(
                    WriteUpdateDynamicPartSlice {
                        *binding->signal, *lowered, part });
            } else {
                process_.operations.emplace_back(
                    WriteBlockingDynamicPartSlice {
                        *binding->signal, *lowered, part });
            }
        } else if (dynamic_vhdl_selection) {
            if (update) {
                process_.operations.emplace_back(
                    WriteUpdateDynamicPartSlice {
                        *binding->signal,
                        *lowered,
                        *dynamic_vhdl_selection,
                    });
            } else {
                process_.operations.emplace_back(
                    WriteBlockingDynamicPartSlice {
                        *binding->signal,
                        *lowered,
                        *dynamic_vhdl_selection,
                    });
            }
        } else if (update) {
            process_.operations.emplace_back(
                WriteUpdate { *binding->signal, *lowered });
        } else {
            process_.operations.emplace_back(
                WriteBlocking { *binding->signal, *lowered });
        }
        return true;
    };
    const auto lower_if = [&] {
        if (!condition) {
            return false;
        }
        if (statement->vhdl != nullptr) {
            const auto condition_domain = hir_expression_domain(
                *condition, hir_process_scope_);
            const auto condition_width = hir_expression_width(
                *condition, hir_process_scope_);
            const auto vhdl_2019_logic_condition
                = vhdl_standard_
                      == frontend::VhdlStandard::Vhdl2019
                && condition_width == std::optional<std::size_t> { 1U }
                && condition_domain
                && (*condition_domain == frontend::ValueDomain::Bit2
                    || *condition_domain
                        == frontend::ValueDomain::Logic4
                    || *condition_domain
                        == frontend::ValueDomain::Logic9);
            if ((!condition_domain
                    || *condition_domain
                        != frontend::ValueDomain::Boolean)
                && !vhdl_2019_logic_condition) {
                const auto assignment
                    = statement->vhdl->conditional_assignment;
                report(
                    assignment ? "FSIM-ELAB-092" : "FSIM-ELAB-048",
                    "a VHDL "
                        + std::string { assignment
                                ? "conditional-assignment"
                                : "if" }
                        + " condition must have type boolean",
                    span);
                return false;
            }
        }
        if (statement->systemverilog != nullptr) {
            const auto constant = specialized_hir_unit_
                                      ->evaluate_integral_expression(*condition);
            if (constant) {
                return lower_hir_statements(
                    *constant != 0 ? std::span { statements }
                                   : std::span { else_statements });
            }
        }
        const auto lowered = lower_condition(*condition);
        if (!lowered) {
            return false;
        }
        const auto branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            *lowered, 0U, 0U, UnknownBranchPolicy::when_false });
        const auto when_true = static_cast<InstructionIndex>(
            process_.operations.size());
        if (!lower_hir_statements(statements)) {
            return false;
        }
        const auto finish = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Jump { 0U });
        const auto when_false = static_cast<InstructionIndex>(
            process_.operations.size());
        if (!lower_hir_statements(else_statements)) {
            return false;
        }
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch] = Branch {
            *lowered,
            when_true,
            when_false,
            UnknownBranchPolicy::when_false,
        };
        process_.operations[finish] = Jump { end };
        return true;
    };
    const auto lower_case = [&](const auto& alternatives) {
        if (!condition) {
            return false;
        }
        auto match_kind = semantic::sv::CaseMatchKind::exact;
        auto qualifier = semantic::sv::CaseQualifier::none;
        const auto systemverilog = statement->systemverilog != nullptr;
        if (systemverilog) {
            match_kind = statement->systemverilog->case_match;
            qualifier = statement->systemverilog->case_qualifier;
        }
        BinaryOperator match_operation = BinaryOperator::case_equal;
        bool inside_matching = false;
        bool pattern_matching = false;
        const bool vhdl_matching = !systemverilog
            && statement->vhdl->matching_case;
        if (vhdl_matching) {
            match_operation = BinaryOperator::vhdl_match_equal;
        }
        if (systemverilog) {
            switch (match_kind) {
            case semantic::sv::CaseMatchKind::exact:
                break;
            case semantic::sv::CaseMatchKind::wildcard_z:
                match_operation = BinaryOperator::casez_equal;
                break;
            case semantic::sv::CaseMatchKind::wildcard_xz:
                match_operation = BinaryOperator::casex_equal;
                break;
            case semantic::sv::CaseMatchKind::inside:
                inside_matching = true;
                match_operation = BinaryOperator::wildcard_equal;
                break;
            case semantic::sv::CaseMatchKind::matches:
                pattern_matching = true;
                break;
            default:
                report(
                    "FSIM-ELAB-081",
                    "case statement has an invalid matching mode",
                    span);
                return true;
            }
        }
        std::string qualifier_name;
        bool diagnose_multiple = false;
        bool diagnose_no_match = false;
        switch (qualifier) {
        case semantic::sv::CaseQualifier::none:
            break;
        case semantic::sv::CaseQualifier::unique:
            qualifier_name = "unique";
            diagnose_multiple = true;
            diagnose_no_match = true;
            break;
        case semantic::sv::CaseQualifier::unique0:
            qualifier_name = "unique0";
            diagnose_multiple = true;
            break;
        case semantic::sv::CaseQualifier::priority:
            qualifier_name = "priority";
            diagnose_no_match = true;
            break;
        default:
            report(
                "FSIM-ELAB-SVCASEQUAL-001",
                "case statement has an invalid qualifier",
                span);
            return true;
        }
        if (qualifier != semantic::sv::CaseQualifier::none
            && language_ != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-SVCASEQUAL-002",
                "case qualifiers require SystemVerilog",
                span);
            return true;
        }
        if (inside_matching
            && language_ != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-SVCASEINSIDE-001",
                "case inside matching requires SystemVerilog",
                span);
            return true;
        }
        if (pattern_matching
            && language_ != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-SVMATCH-001",
                "case matches pattern matching requires SystemVerilog",
                span);
            return true;
        }
        const auto non_scalar_expression = [&](const semantic::ExpressionId expression_id) {
            const auto expression
                = specialized_hir_unit_->find_expression(expression_id);
            if (!expression || expression->systemverilog == nullptr) {
                return false;
            }
            const auto& systemverilog_expression
                = *expression->systemverilog;
            if (systemverilog_expression.kind
                == semantic::sv::ExpressionKind::assignment_pattern) {
                return true;
            }
            const auto declaration_id
                = hir_referenced_declaration(expression_id);
            const auto declaration = declaration_id
                ? specialized_hir_unit_->find_declaration(
                      *declaration_id)
                : std::nullopt;
            if (!declaration
                || declaration->systemverilog == nullptr
                || !declaration->systemverilog->type) {
                return false;
            }
            const auto& type = *declaration->systemverilog->type;
            if (type.container_form) {
                return true;
            }
            const auto non_scalar_form = [](const auto form) {
                using Form = semantic::sv::TypeForm;
                return form == Form::string
                    || form == Form::dynamic_array
                    || form == Form::queue
                    || form == Form::associative_array
                    || form == Form::static_array
                    || form == Form::unpacked_structure
                    || form == Form::unpacked_union
                    || form == Form::class_handle;
            };
            if (type.value_form
                && non_scalar_form(*type.value_form)) {
                return true;
            }
            if (!type.target.target.valid()) {
                return false;
            }
            const auto definition = specialized_hir_unit_->find_type(
                type.target.target);
            return definition
                && definition->systemverilog != nullptr
                && non_scalar_form(
                    definition->systemverilog->form);
        };
        if ((inside_matching || pattern_matching)
            && (hir_expression_is_string(
                    *condition, hir_process_scope_)
                || non_scalar_expression(*condition))) {
            const auto selector_expression
                = specialized_hir_unit_->find_expression(*condition);
            report(
                pattern_matching
                    ? "FSIM-ELAB-SVMATCH-002"
                    : "FSIM-ELAB-SVCASEINSIDE-002",
                pattern_matching
                    ? "bounded case matches requires a scalar integral selector"
                    : "bounded case inside requires a scalar integral selector",
                selector_expression
                        && selector_expression->systemverilog != nullptr
                    ? hir_source_span(
                          selector_expression->systemverilog->source)
                    : span);
            return true;
        }
        const auto selector_width = hir_expression_width(
            *condition, hir_process_scope_);
        if (!selector_width || *selector_width == 0U) {
            if (inside_matching || pattern_matching) {
                report(
                    pattern_matching
                        ? "FSIM-ELAB-SVMATCH-002"
                        : "FSIM-ELAB-SVCASEINSIDE-002",
                    pattern_matching
                        ? "the case matches selector width is not statically inferable"
                        : "the case inside selector width is not statically inferable",
                    span);
                return true;
            }
            return false;
        }
        if (!systemverilog) {
            struct ChoiceInterval {
                std::int64_t lower { };
                std::int64_t upper { };
            };
            const auto selector_domain = hir_expression_domain(
                *condition, hir_process_scope_);
            const auto selector_subtype = hir_vhdl_expression_subtype(
                *condition);
            const auto selector_span = [&] {
                const auto expression
                    = specialized_hir_unit_->find_expression(*condition);
                return expression && expression->vhdl != nullptr
                    ? hir_source_span(expression->vhdl->source)
                    : span;
            }();
            if (vhdl_matching
                && (!selector_domain
                    || (*selector_domain
                            != frontend::ValueDomain::Bit2
                        && *selector_domain
                            != frontend::ValueDomain::Logic9))) {
                report(
                    "FSIM-ELAB-VHDLMATCH-001",
                    "a matching case selector must be bit, std_ulogic, or "
                    "a bounded one-dimensional array of those element "
                    "types",
                    selector_span);
                return true;
            }
            const auto pattern_text = [&](
                                          const semantic::ExpressionId choice)
                -> std::optional<std::string> {
                const auto expression
                    = specialized_hir_unit_->find_expression(choice);
                if (!expression || expression->vhdl == nullptr
                    || (expression->vhdl->kind
                            != semantic::vhdl::ExpressionKind::logic_literal
                        && expression->vhdl->kind
                            != semantic::vhdl::ExpressionKind::string_literal)) {
                    return std::nullopt;
                }
                auto text = expression->vhdl->decoded_string.value_or(
                    expression->vhdl->text);
                if (text.size() >= 2U
                    && ((text.front() == '"' && text.back() == '"')
                        || (text.front() == '\''
                            && text.back() == '\''))) {
                    text = text.substr(1U, text.size() - 2U);
                }
                return text;
            };
            const auto matching_class = [](const char character) {
                switch (static_cast<char>(std::toupper(
                    static_cast<unsigned char>(character)))) {
                case '-':
                    return -1;
                case '0':
                case 'L':
                    return 0;
                case '1':
                case 'H':
                    return 1;
                default:
                    return 2;
                }
            };
            const auto patterns_overlap = [&](const std::string& left,
                                              const std::string& right) {
                if (left.size() != right.size()) {
                    return false;
                }
                for (std::size_t index { }; index < left.size(); ++index) {
                    const auto lhs = matching_class(left[index]);
                    const auto rhs = matching_class(right[index]);
                    if (lhs != -1 && rhs != -1
                        && (lhs == 2 || rhs == 2 || lhs != rhs)) {
                        return false;
                    }
                }
                return true;
            };
            if (vhdl_matching) {
                std::vector<std::string> prior_patterns;
                bool valid = true;
                for (const auto& alternative : alternatives) {
                    if (alternative.is_default) {
                        continue;
                    }
                    for (const auto choice : alternative.choices) {
                        const auto pattern = pattern_text(choice);
                        const auto expression
                            = specialized_hir_unit_->find_expression(choice);
                        const auto choice_span = expression
                                && expression->vhdl != nullptr
                            ? hir_source_span(expression->vhdl->source)
                            : hir_source_span(alternative.source);
                        if (!pattern
                            || pattern->size() != *selector_width) {
                            report(
                                "FSIM-ELAB-VHDLMATCH-002",
                                "bounded matching choices must be locally "
                                "static bit or std_ulogic literals with the "
                                "selector width",
                                choice_span);
                            valid = false;
                            continue;
                        }
                        if (std::ranges::any_of(
                                prior_patterns,
                                [&](const std::string& prior) {
                                    return patterns_overlap(prior, *pattern);
                                })) {
                            report(
                                "FSIM-ELAB-VHDLMATCH-003",
                                "matching case choices overlap after '-' "
                                "wildcard and L/H normalization",
                                choice_span);
                            valid = false;
                        }
                        prior_patterns.push_back(*pattern);
                    }
                }
                if (!valid) {
                    return true;
                }
            } else if (selector_domain) {
                std::optional<std::int64_t> domain_lower;
                std::optional<std::int64_t> domain_upper;
                if (*selector_domain == frontend::ValueDomain::Boolean) {
                    domain_lower = 0;
                    domain_upper = 1;
                }
                const semantic::vhdl::TypeDefinition* selector_type { };
                if (selector_subtype
                    && selector_subtype->type_mark.target.valid()) {
                    const auto type = specialized_hir_unit_->find_type(
                        selector_subtype->type_mark.target);
                    if (type && type->vhdl != nullptr) {
                        selector_type = type->vhdl;
                    }
                }
                if (selector_subtype) {
                    const auto range = std::ranges::find_if(
                        selector_subtype->constraints,
                        [](const auto& candidate) {
                            return candidate.left && candidate.right;
                        });
                    if (range != selector_subtype->constraints.end()) {
                        domain_lower = std::min(*range->left, *range->right);
                        domain_upper = std::max(*range->left, *range->right);
                    }
                }
                if ((!domain_lower || !domain_upper) && selector_type
                    && selector_type->scalar_range
                    && selector_type->scalar_range->left
                    && selector_type->scalar_range->right) {
                    domain_lower = std::min(
                        *selector_type->scalar_range->left,
                        *selector_type->scalar_range->right);
                    domain_upper = std::max(
                        *selector_type->scalar_range->left,
                        *selector_type->scalar_range->right);
                }
                if ((!domain_lower || !domain_upper) && selector_type
                    && !selector_type->enumeration_literals.empty()) {
                    domain_lower = 0;
                    domain_upper = static_cast<std::int64_t>(
                        selector_type->enumeration_literals.size() - 1U);
                }
                std::vector<ChoiceInterval> intervals;
                bool valid = true;
                bool has_default = false;
                for (const auto& alternative : alternatives) {
                    if (alternative.is_default) {
                        has_default = true;
                        continue;
                    }
                    for (const auto choice : alternative.choices) {
                        const auto expression
                            = specialized_hir_unit_->find_expression(choice);
                        const auto choice_span = expression
                                && expression->vhdl != nullptr
                            ? hir_source_span(expression->vhdl->source)
                            : hir_source_span(alternative.source);
                        const auto choice_domain = hir_expression_domain(
                            choice, hir_process_scope_);
                        std::optional<ChoiceInterval> interval;
                        if (expression && expression->vhdl != nullptr
                            && expression->vhdl->kind
                                == semantic::vhdl::ExpressionKind::call
                            && (expression->vhdl->text
                                    == "@vhdl-case-range-to"
                                || expression->vhdl->text
                                    == "@vhdl-case-range-downto")) {
                            const auto& range = *expression->vhdl;
                            const auto left = range.operands.size() == 2U
                                ? hir_constant_integer(range.operands[0])
                                : std::nullopt;
                            const auto right = range.operands.size() == 2U
                                ? hir_constant_integer(range.operands[1])
                                : std::nullopt;
                            if (!left || !right) {
                                report(
                                    "FSIM-ELAB-VHDLCASE-001",
                                    "VHDL case range bounds must be locally "
                                    "static values of the selector's "
                                    "discrete type",
                                    choice_span);
                                valid = false;
                                continue;
                            }
                            const auto descending = range.text
                                == "@vhdl-case-range-downto";
                            if ((descending && *left < *right)
                                || (!descending && *left > *right)) {
                                continue;
                            }
                            interval = ChoiceInterval {
                                std::min(*left, *right),
                                std::max(*left, *right),
                            };
                        } else {
                            const auto choice_value
                                = hir_constant_integer(choice);
                            if (!choice_value
                                || (choice_domain
                                    && *choice_domain
                                        != *selector_domain)) {
                                report(
                                    "FSIM-ELAB-VHDLCASE-001",
                                    "VHDL case choices must be locally "
                                    "static values of the selector's "
                                    "discrete type",
                                    choice_span);
                                valid = false;
                                continue;
                            }
                            interval = ChoiceInterval {
                                *choice_value, *choice_value
                            };
                        }
                        const auto& resolved_interval = *interval;
                        if (domain_lower && domain_upper
                            && (std::less<> { }(
                                    resolved_interval.lower, *domain_lower)
                                || std::greater<> { }(
                                    resolved_interval.upper,
                                    *domain_upper))) {
                            report(
                                "FSIM-ELAB-VHDLCASE-002",
                                "a VHDL case choice lies outside the "
                                "selector subtype range",
                                choice_span);
                            valid = false;
                        }
                        const auto overlap = std::ranges::find_if(
                            intervals,
                            [&](const ChoiceInterval& prior) {
                                return interval->lower <= prior.upper
                                    && prior.lower <= interval->upper;
                            });
                        if (overlap != intervals.end()) {
                            const auto duplicate
                                = overlap->lower == interval->lower
                                && overlap->upper == interval->upper;
                            report(
                                duplicate
                                    ? "FSIM-ELAB-VHDLCASE-003"
                                    : "FSIM-ELAB-VHDLCASE-004",
                                duplicate
                                    ? "a VHDL case statement repeats the "
                                      "same discrete choice"
                                    : "VHDL case choices or ranges overlap",
                                choice_span);
                            valid = false;
                        } else {
                            intervals.push_back(*interval);
                        }
                    }
                }
                if (!has_default && domain_lower && domain_upper) {
                    std::ranges::sort(
                        intervals, { }, &ChoiceInterval::lower);
                    auto expected = *domain_lower;
                    bool complete = false;
                    for (const auto& interval : intervals) {
                        if (interval.upper < expected) {
                            continue;
                        }
                        if (interval.lower > expected) {
                            break;
                        }
                        if (interval.upper >= *domain_upper) {
                            complete = true;
                            break;
                        }
                        expected = interval.upper + 1;
                    }
                    if (!complete) {
                        report(
                            "FSIM-ELAB-VHDLCASE-005",
                            "a VHDL case statement does not cover its "
                            "complete selector subtype and requires an "
                            "others alternative",
                            span);
                        valid = false;
                    }
                }
                if (!valid) {
                    return true;
                }
            }
        }
        const auto selector = lower_hir_expression(
            *condition, *selector_width);
        if (!selector) {
            return false;
        }
        const auto one = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LoadConstant {
            one, PackedLogic4(1U, Logic4::one) });
        const auto make_false = [&]() {
            const auto result = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                result, PackedLogic4(1U, Logic4::zero) });
            return result;
        };
        const auto definite_match = [&](const RegisterId raw_match) {
            const auto result = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal, result, raw_match, one });
            return result;
        };
        const auto lower_integral_operand = [&](const semantic::ExpressionId expression,
                                                const std::string_view code,
                                                const std::string_view message)
            -> std::optional<InsideIntegralOperand> {
            const auto operand_expression
                = specialized_hir_unit_->find_expression(expression);
            if (!operand_expression
                || operand_expression->systemverilog == nullptr
                || hir_expression_is_string(
                    expression, hir_process_scope_)
                || non_scalar_expression(expression)
                || operand_expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::assignment_pattern) {
                report(
                    std::string { code }, std::string { message },
                    operand_expression
                            && operand_expression->systemverilog != nullptr
                        ? hir_source_span(
                              operand_expression->systemverilog->source)
                        : span);
                return std::nullopt;
            }
            const auto width = hir_expression_width(
                expression, hir_process_scope_);
            if (!width || *width == 0U) {
                report(
                    std::string { code },
                    "an inside operand must have a statically inferable, "
                    "nonzero integral width",
                    hir_source_span(
                        operand_expression->systemverilog->source));
                return std::nullopt;
            }
            const auto lowered_value
                = lower_hir_expression(expression, *width);
            if (!lowered_value) {
                return std::nullopt;
            }
            return InsideIntegralOperand {
                *lowered_value,
                register_width(*lowered_value),
                hir_expression_signed(expression),
            };
        };
        const InsideIntegralOperand selector_operand {
            *selector,
            register_width(*selector),
            hir_expression_signed(*condition),
        };
        const auto selector_type = [&]()
            -> std::optional<semantic::sv::TypeReference> {
            if (!systemverilog) {
                return std::nullopt;
            }
            const auto declaration_id
                = hir_referenced_declaration(*condition);
            const auto declaration = declaration_id
                ? specialized_hir_unit_->find_declaration(*declaration_id)
                : std::nullopt;
            return declaration
                    && declaration->systemverilog != nullptr
                    && declaration->systemverilog->type
                ? declaration->systemverilog->type
                : std::nullopt;
        }();
        const auto bounded_pattern_constant = [&](const auto& self,
                                                  const semantic::ExpressionId expression_id) -> bool {
            const auto expression
                = specialized_hir_unit_->find_expression(expression_id);
            if (!expression || expression->systemverilog == nullptr) {
                return false;
            }
            const auto& systemverilog_expression
                = *expression->systemverilog;
            switch (systemverilog_expression.kind) {
            case semantic::sv::ExpressionKind::integer_literal:
            case semantic::sv::ExpressionKind::boolean_literal:
            case semantic::sv::ExpressionKind::logic_literal:
                return true;
            case semantic::sv::ExpressionKind::name: {
                const auto declaration_id
                    = hir_referenced_declaration(expression_id);
                const auto initializer = declaration_id
                    ? hir_constant_initializer(*declaration_id)
                    : std::nullopt;
                return initializer && self(self, *initializer);
            }
            case semantic::sv::ExpressionKind::unary:
            case semantic::sv::ExpressionKind::binary:
            case semantic::sv::ExpressionKind::concatenation:
            case semantic::sv::ExpressionKind::replication:
                return !systemverilog_expression.operands.empty()
                    && std::ranges::all_of(
                        systemverilog_expression.operands,
                        [&](const auto operand) {
                            return self(self, operand);
                        });
            default:
                return false;
            }
        };
        struct LoweredChoice {
            RegisterId matched { };
            std::vector<HirCasePatternBinding> bindings;
        };
        struct PatternOperand {
            RegisterId value { };
            std::size_t width { };
            frontend::ValueDomain domain {
                frontend::ValueDomain::Unknown
            };
            bool signed_value { };
            std::optional<semantic::sv::TypeReference> type;
        };
        const PatternOperand root_pattern_operand {
            *selector,
            register_width(*selector),
            register_domain(*selector),
            hir_expression_signed(*condition),
            selector_type,
        };
        const auto type_definition = [&](const PatternOperand& operand)
            -> const semantic::sv::TypeDefinition* {
            if (!operand.type) {
                return nullptr;
            }
            std::unordered_set<std::uint32_t> visited_types;
            std::unordered_set<std::uint32_t> visited_declarations;
            const auto resolve = [&](const auto& self,
                                     const semantic::sv::TypeReference& reference)
                -> const semantic::sv::TypeDefinition* {
                if (reference.target.target.valid()) {
                    if (!visited_types.insert(
                                          reference.target.target.value())
                            .second) {
                        return nullptr;
                    }
                    const auto type = specialized_hir_unit_->find_type(
                        reference.target.target);
                    if (!type || type->systemverilog == nullptr) {
                        return nullptr;
                    }
                    const auto& definition = *type->systemverilog;
                    if (definition.form
                            == semantic::sv::TypeForm::packed_structure
                        || definition.form
                            == semantic::sv::TypeForm::packed_union
                        || definition.form
                            == semantic::sv::TypeForm::tagged_union) {
                        return &definition;
                    }
                    return self(self, definition.base);
                }
                if (reference.target.spelling.empty()) {
                    return nullptr;
                }
                const auto declaration_id
                    = hir_systemverilog_named_type_declaration(
                        reference.target.spelling,
                        specialized_hir_unit_
                            ->find_expression(*condition)
                            ->systemverilog->scope);
                if (!declaration_id
                    || !visited_declarations.insert(
                                                declaration_id->value())
                        .second) {
                    return nullptr;
                }
                const auto declaration
                    = specialized_hir_unit_->find_declaration(
                        *declaration_id);
                if (!declaration
                    || declaration->systemverilog == nullptr) {
                    return nullptr;
                }
                const auto& declaration_source
                    = *declaration->systemverilog;
                return declaration_source.type
                    ? self(self, *declaration_source.type)
                    : declaration_source.default_type
                    ? self(self, *declaration_source.default_type)
                    : nullptr;
            };
            return resolve(resolve, *operand.type);
        };
        const auto combine_matches = [&](const RegisterId left,
                                         const RegisterId right) {
            const auto result = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::bit_and, result, left, right });
            return result;
        };
        const auto merge_bindings = [&](
                                        std::vector<HirCasePatternBinding>& destination,
                                        std::vector<HirCasePatternBinding> incoming,
                                        const semantic::SourceSpanId source_span) {
            for (auto& binding : incoming) {
                if (std::ranges::any_of(
                        destination,
                        [&](const auto& existing) {
                            return existing.name == binding.name;
                        })) {
                    report(
                        "FSIM-ELAB-SVMATCH-007",
                        "case matches pattern binds '" + binding.name
                            + "' more than once",
                        hir_source_span(source_span));
                    return false;
                }
                destination.push_back(std::move(binding));
            }
            return true;
        };
        const auto extract_operand = [&](
                                         const PatternOperand& operand,
                                         const semantic::sv::TypeDefinition& type,
                                         const std::size_t member_index) -> std::optional<PatternOperand> {
            if (member_index >= type.members.size()) {
                return std::nullopt;
            }
            const auto& member = type.members[member_index];
            const auto member_offset = hir_systemverilog_member_offset(
                type, member_index);
            auto width = hir_systemverilog_type_width(member.type);
            if (!width && member.type.executable_width
                && *member.type.executable_width != 0U
                && *member.type.executable_width
                    <= std::numeric_limits<std::size_t>::max()) {
                width = static_cast<std::size_t>(
                    *member.type.executable_width);
            }
            if (!member_offset || !width || *width == 0U
                || *member_offset
                    > std::numeric_limits<std::uint32_t>::max()
                || *width > std::numeric_limits<std::uint32_t>::max()
                || *member_offset > operand.width
                || *width > operand.width - *member_offset) {
                return std::nullopt;
            }
            const auto member_value = allocate_register(
                *width,
                hir_systemverilog_type_four_state(member.type)
                    ? frontend::ValueDomain::Logic4
                    : frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Extract {
                member_value,
                operand.value,
                static_cast<std::uint32_t>(*member_offset),
                static_cast<std::uint32_t>(*width),
            });
            return PatternOperand {
                member_value,
                *width,
                register_domain(member_value),
                member.type.signed_value,
                member.type,
            };
        };
        const auto lower_choice = [&](const semantic::ExpressionId choice,
                                      const PatternOperand& operand,
                                      const auto& self) -> std::optional<LoweredChoice> {
            const auto expression
                = specialized_hir_unit_->find_expression(choice);
            if (!expression) {
                return std::nullopt;
            }
            if (!systemverilog && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && (expression->vhdl->text
                        == "@vhdl-case-range-to"
                    || expression->vhdl->text
                        == "@vhdl-case-range-downto")) {
                const auto& range = *expression->vhdl;
                if (range.operands.size() != 2U) {
                    return std::nullopt;
                }
                auto left = lower_hir_expression(
                    range.operands[0], operand.width);
                auto right = lower_hir_expression(
                    range.operands[1], operand.width);
                if (!left || !right) {
                    return std::nullopt;
                }
                const auto signed_values = operand.signed_value
                    || hir_expression_signed(range.operands[0])
                    || hir_expression_signed(range.operands[1]);
                if (register_width(*left) != operand.width) {
                    left = resize_register(
                        *left, operand.width, signed_values);
                }
                if (register_width(*right) != operand.width) {
                    right = resize_register(
                        *right, operand.width, signed_values);
                }
                const auto descending = range.text
                    == "@vhdl-case-range-downto";
                const auto valid = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                const auto after_left = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                const auto before_right = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                const auto inside_left = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                const auto matched = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
                process_.operations.emplace_back(Binary {
                    descending
                        ? signed_values
                            ? BinaryOperator::greater_equal_signed
                            : BinaryOperator::greater_equal_unsigned
                        : signed_values
                        ? BinaryOperator::less_equal_signed
                        : BinaryOperator::less_equal_unsigned,
                    valid,
                    *left,
                    *right,
                });
                process_.operations.emplace_back(Binary {
                    descending
                        ? signed_values
                            ? BinaryOperator::less_equal_signed
                            : BinaryOperator::less_equal_unsigned
                        : signed_values
                        ? BinaryOperator::greater_equal_signed
                        : BinaryOperator::greater_equal_unsigned,
                    after_left,
                    operand.value,
                    *left,
                });
                process_.operations.emplace_back(Binary {
                    descending
                        ? signed_values
                            ? BinaryOperator::greater_equal_signed
                            : BinaryOperator::greater_equal_unsigned
                        : signed_values
                        ? BinaryOperator::less_equal_signed
                        : BinaryOperator::less_equal_unsigned,
                    before_right,
                    operand.value,
                    *right,
                });
                process_.operations.emplace_back(LogicalBinary {
                    LogicalBinaryOperator::logical_and,
                    inside_left,
                    valid,
                    after_left,
                });
                process_.operations.emplace_back(LogicalBinary {
                    LogicalBinaryOperator::logical_and,
                    matched,
                    inside_left,
                    before_right,
                });
                return LoweredChoice { matched, { } };
            }
            if (inside_matching && expression->systemverilog != nullptr
                && expression->systemverilog->kind
                    == semantic::sv::ExpressionKind::call
                && expression->systemverilog->text == "@inside-range") {
                const auto& range = *expression->systemverilog;
                if (range.operands.size() != 2U) {
                    report(
                        "FSIM-ELAB-SVCASEINSIDE-006",
                        "a case inside range requires exactly one low and high bound",
                        hir_source_span(range.source));
                    return LoweredChoice { make_false(), { } };
                }
                const auto low = lower_integral_operand(
                    range.operands[0],
                    "FSIM-ELAB-SVCASEINSIDE-003",
                    "case inside range bounds must be integral expressions");
                const auto high = lower_integral_operand(
                    range.operands[1],
                    "FSIM-ELAB-SVCASEINSIDE-003",
                    "case inside range bounds must be integral expressions");
                if (!low || !high) {
                    return LoweredChoice { make_false(), { } };
                }
                const auto valid_operands
                    = size_integral_comparison(*low, *high);
                const auto low_operands
                    = size_integral_comparison(selector_operand, *low);
                const auto high_operands
                    = size_integral_comparison(selector_operand, *high);
                const auto valid = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                const auto above_low = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                const auto below_high = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                const auto within_lower = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                const auto matched = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(Binary {
                    valid_operands.signed_value
                        ? BinaryOperator::less_equal_signed
                        : BinaryOperator::less_equal_unsigned,
                    valid,
                    valid_operands.lhs,
                    valid_operands.rhs,
                });
                process_.operations.emplace_back(Binary {
                    low_operands.signed_value
                        ? BinaryOperator::greater_equal_signed
                        : BinaryOperator::greater_equal_unsigned,
                    above_low,
                    low_operands.lhs,
                    low_operands.rhs,
                });
                process_.operations.emplace_back(Binary {
                    high_operands.signed_value
                        ? BinaryOperator::less_equal_signed
                        : BinaryOperator::less_equal_unsigned,
                    below_high,
                    high_operands.lhs,
                    high_operands.rhs,
                });
                process_.operations.emplace_back(LogicalBinary {
                    LogicalBinaryOperator::logical_and,
                    within_lower,
                    valid,
                    above_low,
                });
                process_.operations.emplace_back(LogicalBinary {
                    LogicalBinaryOperator::logical_and,
                    matched,
                    within_lower,
                    below_high,
                });
                return LoweredChoice { matched, { } };
            }
            if (pattern_matching && expression->systemverilog != nullptr) {
                const auto& pattern = *expression->systemverilog;
                if (pattern.kind == semantic::sv::ExpressionKind::call
                    && pattern.text == "@match-wildcard") {
                    return LoweredChoice { one, { } };
                }
                if (pattern.kind == semantic::sv::ExpressionKind::call
                    && pattern.text == "@match-guard") {
                    if (pattern.operands.size() != 2U) {
                        report(
                            "FSIM-ELAB-SVMATCH-005",
                            "guarded case matches HIR requires one pattern and one guard",
                            hir_source_span(pattern.source));
                        return LoweredChoice { make_false(), { } };
                    }
                    auto raw = self(
                        pattern.operands[0], operand, self);
                    if (!raw) {
                        return std::nullopt;
                    }
                    const auto result = make_false();
                    const auto pattern_matches = definite_match(
                        raw->matched);
                    const auto branch = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(Branch {
                        pattern_matches,
                        0U,
                        0U,
                        UnknownBranchPolicy::when_false,
                    });
                    const auto guard_start = static_cast<InstructionIndex>(
                        process_.operations.size());
                    hir_case_pattern_binding_frames_.push_back(
                        raw->bindings);
                    const auto guard = lower_condition(
                        pattern.operands[1]);
                    hir_case_pattern_binding_frames_.pop_back();
                    if (!guard) {
                        return std::nullopt;
                    }
                    process_.operations.emplace_back(CopyRegister {
                        result, definite_match(*guard) });
                    const auto end = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations[branch] = Branch {
                        pattern_matches,
                        guard_start,
                        end,
                        UnknownBranchPolicy::when_false,
                    };
                    raw->matched = result;
                    return raw;
                }
                constexpr auto bind_prefix
                    = std::string_view { "@match-bind:" };
                if (pattern.kind == semantic::sv::ExpressionKind::call
                    && pattern.text.starts_with(bind_prefix)) {
                    const auto name = std::string_view {
                        pattern.text
                    }
                                          .substr(bind_prefix.size());
                    if (name.empty()) {
                        report(
                            "FSIM-ELAB-SVMATCH-007",
                            "a case matches binding pattern requires a name",
                            hir_source_span(pattern.source));
                        return LoweredChoice { make_false(), { } };
                    }
                    return LoweredChoice {
                        one,
                        { HirCasePatternBinding {
                            std::string { name },
                            operand.value,
                            operand.width,
                            operand.domain,
                            operand.signed_value,
                        } },
                    };
                }
                constexpr auto tagged_prefix
                    = std::string_view { "@match-tagged:" };
                if (pattern.kind == semantic::sv::ExpressionKind::call
                    && pattern.text.starts_with(tagged_prefix)) {
                    const auto* type = type_definition(operand);
                    if (type == nullptr
                        || type->form
                            != semantic::sv::TypeForm::tagged_union
                        || type->members.empty()
                        || pattern.operands.size() > 1U) {
                        report(
                            "FSIM-ELAB-SVMATCH-008",
                            "a tagged case pattern requires a tagged-union selector and at most one nested pattern",
                            hir_source_span(pattern.source));
                        return LoweredChoice { make_false(), { } };
                    }
                    const auto member_name = std::string_view {
                        pattern.text
                    }
                                                 .substr(tagged_prefix.size());
                    const auto member = std::ranges::find(
                        type->members, member_name,
                        &semantic::sv::PackedMember::name);
                    if (member == type->members.end()) {
                        report(
                            "FSIM-ELAB-SVMATCH-008",
                            "tagged case pattern names unknown member '"
                                + std::string { member_name } + "'",
                            hir_source_span(pattern.source));
                        return LoweredChoice { make_false(), { } };
                    }
                    const auto tag_width = std::max<std::size_t>(
                        1U,
                        static_cast<std::size_t>(
                            std::bit_width(
                                type->members.size() - 1U)));
                    if (tag_width > operand.width
                        || operand.width - tag_width
                            > std::numeric_limits<std::uint32_t>::max()) {
                        report(
                            "FSIM-ELAB-SVMATCH-008",
                            "tagged case pattern has no executable tag layout",
                            hir_source_span(pattern.source));
                        return LoweredChoice { make_false(), { } };
                    }
                    const auto actual_tag = allocate_register(
                        tag_width, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Extract {
                        actual_tag,
                        operand.value,
                        static_cast<std::uint32_t>(
                            operand.width - tag_width),
                        static_cast<std::uint32_t>(tag_width),
                    });
                    const auto expected_tag = allocate_register(
                        tag_width, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        expected_tag,
                        unsigned_value(
                            static_cast<std::size_t>(std::distance(
                                type->members.begin(), member)),
                            tag_width),
                    });
                    const auto tag_matches = allocate_register(
                        1U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::case_equal,
                        tag_matches,
                        actual_tag,
                        expected_tag,
                    });
                    if (pattern.operands.empty()) {
                        return LoweredChoice { tag_matches, { } };
                    }
                    const auto member_index = static_cast<std::size_t>(
                        std::distance(type->members.begin(), member));
                    const auto member_operand = extract_operand(
                        operand, *type, member_index);
                    if (!member_operand) {
                        report(
                            "FSIM-ELAB-SVMATCH-008",
                            "tagged case pattern member has no executable packed layout",
                            hir_source_span(pattern.source));
                        return LoweredChoice { make_false(), { } };
                    }
                    auto nested = self(
                        pattern.operands.front(),
                        *member_operand,
                        self);
                    if (!nested) {
                        return std::nullopt;
                    }
                    nested->matched = combine_matches(
                        tag_matches, nested->matched);
                    return nested;
                }
                if (pattern.kind
                        == semantic::sv::ExpressionKind::assignment_pattern
                    && pattern.text == "@match-structure") {
                    const auto* type = type_definition(operand);
                    if (type == nullptr
                        || (type->form
                                != semantic::sv::TypeForm::packed_structure
                            && type->form
                                != semantic::sv::TypeForm::packed_union)
                        || pattern.associations.empty()) {
                        report(
                            "FSIM-ELAB-SVMATCH-009",
                            "a structured case pattern requires compatible packed aggregate metadata",
                            hir_source_span(pattern.source));
                        return LoweredChoice { make_false(), { } };
                    }
                    auto result = LoweredChoice { one, { } };
                    std::vector<bool> selected(type->members.size());
                    const auto named
                        = pattern.associations.front().choice_spelling
                        == "@key";
                    std::size_t positional { };
                    for (const auto& association :
                        pattern.associations) {
                        if ((association.choice_spelling == "@key")
                            != named) {
                            report(
                                "FSIM-ELAB-SVMATCH-009",
                                "structured case patterns cannot mix named and positional members",
                                hir_source_span(association.source));
                            return LoweredChoice { make_false(), { } };
                        }
                        std::size_t index { };
                        if (named) {
                            if (association.choices.size() != 1U) {
                                report(
                                    "FSIM-ELAB-SVMATCH-009",
                                    "a named structured case pattern requires one member name",
                                    hir_source_span(association.source));
                                return LoweredChoice {
                                    make_false(), { }
                                };
                            }
                            const auto key
                                = specialized_hir_unit_->find_expression(
                                    association.choices.front());
                            if (!key || key->systemverilog == nullptr
                                || key->systemverilog->kind
                                    != semantic::sv::ExpressionKind::name) {
                                report(
                                    "FSIM-ELAB-SVMATCH-009",
                                    "a named structured case pattern has an invalid member name",
                                    hir_source_span(association.source));
                                return LoweredChoice {
                                    make_false(), { }
                                };
                            }
                            const auto member = std::ranges::find(
                                type->members,
                                key->systemverilog->text,
                                &semantic::sv::PackedMember::name);
                            if (member == type->members.end()) {
                                report(
                                    "FSIM-ELAB-SVMATCH-009",
                                    "structured case pattern names unknown member '"
                                        + key->systemverilog->text + "'",
                                    hir_source_span(association.source));
                                return LoweredChoice {
                                    make_false(), { }
                                };
                            }
                            index = static_cast<std::size_t>(
                                std::distance(
                                    type->members.begin(), member));
                        } else {
                            index = positional++;
                        }
                        if (index >= type->members.size()
                            || selected[index]) {
                            report(
                                "FSIM-ELAB-SVMATCH-009",
                                "structured case pattern repeats or exceeds its packed members",
                                hir_source_span(association.source));
                            return LoweredChoice { make_false(), { } };
                        }
                        selected[index] = true;
                        const auto member_operand = extract_operand(
                            operand, *type, index);
                        if (!member_operand) {
                            report(
                                "FSIM-ELAB-SVMATCH-009",
                                "structured case pattern member has no executable packed layout",
                                hir_source_span(association.source));
                            return LoweredChoice { make_false(), { } };
                        }
                        auto nested = self(
                            association.value,
                            *member_operand,
                            self);
                        if (!nested) {
                            return std::nullopt;
                        }
                        result.matched = combine_matches(
                            result.matched, nested->matched);
                        if (!merge_bindings(
                                result.bindings,
                                std::move(nested->bindings),
                                association.source)) {
                            return LoweredChoice { make_false(), { } };
                        }
                    }
                    return result;
                }
                if (pattern.kind == semantic::sv::ExpressionKind::call
                    && pattern.text == "@match-unsupported") {
                    report(
                        "FSIM-ELAB-SVMATCH-003",
                        "case matches pattern is unsupported",
                        hir_source_span(pattern.source));
                    return LoweredChoice { make_false(), { } };
                }
            }
            if (inside_matching || pattern_matching) {
                if (pattern_matching
                    && !bounded_pattern_constant(
                        bounded_pattern_constant, choice)) {
                    report(
                        "FSIM-ELAB-SVMATCH-003",
                        "case matches pattern is not an integral constant, "
                        "wildcard, binding, tagged, or structured pattern",
                        expression->systemverilog != nullptr
                            ? hir_source_span(
                                  expression->systemverilog->source)
                            : span);
                    return LoweredChoice { make_false(), { } };
                }
                const auto lowered_operand = lower_integral_operand(
                    choice,
                    pattern_matching
                        ? "FSIM-ELAB-SVMATCH-003"
                        : "FSIM-ELAB-SVCASEINSIDE-003",
                    pattern_matching
                        ? "case matches constant patterns must be integral expressions"
                        : "case inside choices must be integral expressions");
                if (!lowered_operand) {
                    return LoweredChoice { make_false(), { } };
                }
                const auto pattern_operand = InsideIntegralOperand {
                    operand.value,
                    operand.width,
                    operand.signed_value,
                };
                const auto operands = size_integral_comparison(
                    pattern_operand, *lowered_operand);
                const auto matched = allocate_register(
                    1U, inside_matching ? frontend::ValueDomain::Logic4 : frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary {
                    inside_matching
                        ? BinaryOperator::wildcard_equal
                        : BinaryOperator::case_equal,
                    matched,
                    operands.lhs,
                    operands.rhs,
                });
                return LoweredChoice { matched, { } };
            }
            auto choice_width = hir_expression_width(
                choice, hir_process_scope_);
            if ((!choice_width || *choice_width == 0U)
                && !systemverilog && hir_constant_integer(choice)) {
                choice_width = selector_width;
            }
            if (!choice_width || *choice_width == 0U) {
                return std::nullopt;
            }
            if (*choice_width != *selector_width) {
                report(
                    "FSIM-ELAB-063",
                    "case item width "
                        + std::to_string(*choice_width)
                        + " does not match selector width "
                        + std::to_string(*selector_width),
                    expression->systemverilog != nullptr
                        ? hir_source_span(
                              expression->systemverilog->source)
                        : span);
                return LoweredChoice { make_false(), { } };
            }
            auto lowered = lower_hir_expression(choice, *choice_width);
            if (!lowered) {
                return std::nullopt;
            }
            auto comparison_selector = *selector;
            if (register_domain(*lowered)
                != register_domain(comparison_selector)) {
                const auto converted = allocate_register(
                    register_width(*lowered),
                    register_domain(comparison_selector));
                process_.operations.emplace_back(CopyRegister {
                    converted, *lowered });
                lowered = converted;
            }
            const auto matched = allocate_register(
                1U, systemverilog ? frontend::ValueDomain::Bit2 : frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary {
                match_operation,
                matched,
                comparison_selector,
                *lowered,
            });
            return LoweredChoice { matched, { } };
        };
        const auto lower_alternative_match = [&](const auto& alternative)
            -> std::optional<LoweredChoice> {
            if (inside_matching && alternative.choices.empty()) {
                report(
                    "FSIM-ELAB-SVCASEINSIDE-005",
                    "a case inside alternative requires at least one choice",
                    hir_source_span(alternative.source));
                return LoweredChoice { make_false(), { } };
            }
            if (pattern_matching
                && alternative.choices.size() != 1U) {
                report(
                    "FSIM-ELAB-SVMATCH-005",
                    "a case matches item requires exactly one pattern",
                    hir_source_span(alternative.source));
                return LoweredChoice { make_false(), { } };
            }
            if (alternative.choices.empty()) {
                return std::nullopt;
            }
            const auto matched = make_false();
            std::vector<HirCasePatternBinding> bindings;
            bool first_choice = true;
            for (const auto choice : alternative.choices) {
                const auto skip = !first_choice
                    ? std::optional { static_cast<InstructionIndex>(
                          process_.operations.size()) }
                    : std::nullopt;
                if (skip) {
                    process_.operations.emplace_back(Branch {
                        matched,
                        0U,
                        0U,
                        UnknownBranchPolicy::when_false,
                    });
                }
                const auto choice_start = static_cast<InstructionIndex>(
                    process_.operations.size());
                auto raw = lower_choice(
                    choice, root_pattern_operand, lower_choice);
                if (!raw) {
                    return std::nullopt;
                }
                const auto definite = definite_match(raw->matched);
                process_.operations.emplace_back(CopyRegister {
                    matched, definite });
                if (!merge_bindings(
                        bindings,
                        std::move(raw->bindings),
                        alternative.source)) {
                    return LoweredChoice { make_false(), { } };
                }
                const auto choice_end = static_cast<InstructionIndex>(
                    process_.operations.size());
                if (skip) {
                    process_.operations[*skip] = Branch {
                        matched,
                        choice_end,
                        choice_start,
                        UnknownBranchPolicy::when_false,
                    };
                }
                first_choice = false;
            }
            return LoweredChoice {
                matched, std::move(bindings)
            };
        };
        const auto lower_alternative_body = [&](const auto& alternative) {
            std::string alternative_scope;
            if (statement->vhdl != nullptr) {
                const auto alternative_span = hir_source_span(
                    alternative.source);
                alternative_scope = "$when_"
                    + std::to_string(alternative_span.begin.line) + "_"
                    + std::to_string(alternative_span.begin.column) + "_"
                    + std::to_string(alternative_span.begin.offset);
            }
            HirDebugScopeGuard guard {
                local_scope_, std::move(alternative_scope)
            };
            return lower_hir_statements(alternative.statements);
        };
        const auto* default_alternative = static_cast<const typename std::decay_t<
            decltype(alternatives)>::value_type*>(nullptr);
        for (const auto& alternative : alternatives) {
            if (alternative.is_default) {
                default_alternative = &alternative;
            }
        }
        const auto qualified
            = qualifier != semantic::sv::CaseQualifier::none;
        if (!qualified) {
            std::vector<InstructionIndex> end_jumps;
            for (const auto& alternative : alternatives) {
                if (alternative.is_default) {
                    continue;
                }
                auto matched = lower_alternative_match(alternative);
                if (!matched) {
                    return false;
                }
                const auto branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(Branch {
                    matched->matched,
                    0U,
                    0U,
                    UnknownBranchPolicy::when_false,
                });
                const auto body = static_cast<InstructionIndex>(
                    process_.operations.size());
                hir_case_pattern_binding_frames_.push_back(
                    std::move(matched->bindings));
                const auto lowered_body = lower_alternative_body(
                    alternative);
                hir_case_pattern_binding_frames_.pop_back();
                if (!lowered_body) {
                    return false;
                }
                end_jumps.push_back(static_cast<InstructionIndex>(
                    process_.operations.size()));
                process_.operations.emplace_back(Jump { 0U });
                const auto next = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations[branch] = Branch {
                    matched->matched,
                    body,
                    next,
                    UnknownBranchPolicy::when_false,
                };
            }
            if (default_alternative != nullptr
                && !lower_alternative_body(*default_alternative)) {
                return false;
            }
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            for (const auto jump : end_jumps) {
                process_.operations[jump] = Jump { end };
            }
            return true;
        }
        struct QualifiedAlternative {
            const typename std::decay_t<
                decltype(alternatives)>::value_type* source { };
            RegisterId matched { };
            std::vector<HirCasePatternBinding> bindings;
        };
        std::vector<QualifiedAlternative> qualified_alternatives;
        auto any_match = make_false();
        auto multiple_match = make_false();
        for (const auto& alternative : alternatives) {
            if (alternative.is_default) {
                continue;
            }
            auto matched = lower_alternative_match(alternative);
            if (!matched) {
                return false;
            }
            qualified_alternatives.push_back({
                &alternative,
                matched->matched,
                std::move(matched->bindings),
            });
            const auto overlap = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::bit_and,
                overlap,
                any_match,
                matched->matched,
            });
            const auto next_multiple = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::bit_or,
                next_multiple,
                multiple_match,
                overlap,
            });
            multiple_match = next_multiple;
            const auto next_any = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::bit_or,
                next_any,
                any_match,
                matched->matched,
            });
            any_match = next_any;
        }
        const auto warning_source = SourceLocation {
            span.source_name.str(),
            static_cast<std::uint32_t>(span.begin.line),
            static_cast<std::uint32_t>(span.begin.column),
        };
        const auto warn_if = [&](const RegisterId checked,
                                 std::string message) {
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                checked,
                0U,
                0U,
                UnknownBranchPolicy::when_false,
            });
            const auto warning = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Report {
                std::move(message),
                runtime::simir::AssertionSeverity::warning,
                warning_source,
            });
            const auto next = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                checked,
                warning,
                next,
                UnknownBranchPolicy::when_false,
            };
        };
        if (diagnose_multiple) {
            warn_if(
                multiple_match,
                qualifier_name + " case has multiple matching items");
        }
        if (diagnose_no_match && default_alternative == nullptr) {
            const auto no_match = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(LogicalNot {
                no_match, any_match });
            warn_if(
                no_match,
                qualifier_name + " case has no matching item");
        }
        std::vector<InstructionIndex> end_jumps;
        for (const auto& alternative : qualified_alternatives) {
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                alternative.matched,
                0U,
                0U,
                UnknownBranchPolicy::when_false,
            });
            const auto body = static_cast<InstructionIndex>(
                process_.operations.size());
            hir_case_pattern_binding_frames_.push_back(
                alternative.bindings);
            const auto lowered_body = lower_alternative_body(
                *alternative.source);
            hir_case_pattern_binding_frames_.pop_back();
            if (!lowered_body) {
                return false;
            }
            end_jumps.push_back(static_cast<InstructionIndex>(
                process_.operations.size()));
            process_.operations.emplace_back(Jump { 0U });
            const auto next = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                alternative.matched,
                body,
                next,
                UnknownBranchPolicy::when_false,
            };
        }
        if (default_alternative != nullptr
            && !lower_alternative_body(*default_alternative)) {
            return false;
        }
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump : end_jumps) {
            process_.operations[jump] = Jump { end };
        }
        return true;
    };
    const auto lower_loop = [&](const std::string_view label) {
        const auto saved_scope = hir_process_scope_;
        const auto saved_hir_locals = hir_local_registers_;
        const auto saved_hir_string_locals
            = hir_local_string_registers_;
        const auto saved_locals = locals_;
        const auto saved_string_locals = string_locals_;
        hir_process_scope_ = nested_scope.value_or(hir_process_scope_);
        const auto restore_scope = [&] {
            string_locals_ = saved_string_locals;
            locals_ = saved_locals;
            hir_local_string_registers_ = saved_hir_string_locals;
            hir_local_registers_ = saved_hir_locals;
            hir_process_scope_ = saved_scope;
        };
        if (!initialize_hir_declarations(declarations)) {
            restore_scope();
            return false;
        }
        const auto lower_loop_assignment = [&](
                                               const std::optional<semantic::ExpressionId> assignment_target,
                                               const std::optional<semantic::ExpressionId> assignment_value) {
            if (!assignment_target && !assignment_value) {
                return true;
            }
            if (!assignment_target || !assignment_value) {
                return false;
            }
            const auto saved_target = target;
            const auto saved_value = value;
            target = assignment_target;
            value = assignment_value;
            const auto lowered = lower_assignment(false, false);
            target = saved_target;
            value = saved_value;
            return lowered;
        };
        const auto expression_contains_systemverilog_call
            = [&](const auto& self,
                  const semantic::ExpressionId expression_id,
                  std::unordered_set<std::uint32_t>& visiting) -> bool {
            if (!visiting.insert(expression_id.value()).second) {
                return false;
            }
            const auto expression
                = specialized_hir_unit_->find_expression(expression_id);
            if (!expression || expression->systemverilog == nullptr) {
                return false;
            }
            if (expression->systemverilog->kind
                == semantic::sv::ExpressionKind::call) {
                return true;
            }
            return std::ranges::any_of(
                expression->systemverilog->operands,
                [&](const semantic::ExpressionId operand) {
                    return self(self, operand, visiting);
                });
        };
        const bool runtime_repeat_count = [&] {
            if (statement->systemverilog == nullptr
                || !statement->systemverilog->loop_repeat
                || !statement->systemverilog->loop_limit) {
                return false;
            }
            std::unordered_set<std::uint32_t> visiting;
            return expression_contains_systemverilog_call(
                expression_contains_systemverilog_call,
                *statement->systemverilog->loop_limit,
                visiting);
        }();
        auto static_loop_iterations = hir_loop_iteration_count(statement_id);
        if (runtime_repeat_count) {
            static_loop_iterations.reset();
        }
        const bool runtime_systemverilog_loop = [&] {
            if (statement->systemverilog == nullptr) {
                return false;
            }
            const auto& loop_statement = *statement->systemverilog;
            return !static_loop_iterations
                && (loop_statement.loop_runtime
                    || (loop_statement.loop_repeat
                        && loop_statement.loop_limit
                        && (runtime_repeat_count
                            || !hir_constant_integer(
                                *loop_statement.loop_limit)))
                    || (!loop_statement.loop_variable.empty()
                        && ((loop_statement.loop_initial
                                && !hir_constant_integer(
                                    *loop_statement.loop_initial))
                            || (loop_statement.loop_limit
                                && !hir_constant_integer(
                                    *loop_statement.loop_limit)))));
        }();
        const bool runtime_vhdl_loop = statement->vhdl != nullptr
            && statement->vhdl->loop_variable.empty();
        if (runtime_systemverilog_loop || runtime_vhdl_loop) {
            if (statement->systemverilog != nullptr) {
                const auto& loop_statement = *statement->systemverilog;
                const auto foreach_expression = loop_statement.condition
                    ? specialized_hir_unit_->find_expression(
                          *loop_statement.condition)
                    : std::nullopt;
                const bool systemverilog_foreach
                    = foreach_expression
                    && foreach_expression->systemverilog != nullptr
                    && foreach_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::call
                    && foreach_expression->systemverilog->text
                        == "@sv-foreach";
                if (systemverilog_foreach) {
                    const auto& foreach
                        = *foreach_expression->systemverilog;
                    if (foreach.operands.size() != 2U) {
                        report(
                            "FSIM-ELAB-SVFOREACH-001",
                            "procedural foreach currently requires exactly "
                            "one index variable",
                            span);
                        restore_scope();
                        return true;
                    }
                    if (systemverilog_standard_
                        != frontend::StandardRevision::SystemVerilog2023) {
                        report(
                            "FSIM-ELAB-SVFOREACH-002",
                            "procedural foreach iteration over a string "
                            "requires SystemVerilog-2023",
                            span);
                        restore_scope();
                        return true;
                    }
                    if (!hir_expression_is_string(
                            foreach.operands.front(),
                            hir_process_scope_)) {
                        report(
                            "FSIM-ELAB-SVFOREACH-003",
                            "procedural foreach runtime lowering currently "
                            "requires a string collection",
                            span);
                        restore_scope();
                        return true;
                    }
                    const auto parameter = std::ranges::find_if(
                        declarations,
                        [&](const semantic::DeclarationId declaration_id) {
                            const auto declaration = specialized_hir_unit_
                                ->find_declaration(declaration_id);
                            return declaration
                                && declaration->systemverilog != nullptr
                                && declaration->systemverilog->name
                                    == loop_statement.loop_variable;
                        });
                    if (parameter == declarations.end()) {
                        restore_scope();
                        return false;
                    }
                    const auto binding = hir_runtime_binding(
                        *parameter, hir_process_scope_, false);
                    if (!binding
                        || binding->kind != HirRuntimeBindingKind::local
                        || !binding->local || binding->width != 32U) {
                        restore_scope();
                        return false;
                    }
                    const auto assignment_to_index
                        = [&](auto&& self,
                              const std::span<const semantic::StatementId>
                                  children) -> bool {
                        for (const auto child_id : children) {
                            const auto child = specialized_hir_unit_
                                ->find_statement(child_id);
                            if (!child
                                || child->systemverilog == nullptr) {
                                continue;
                            }
                            const auto& child_source
                                = *child->systemverilog;
                            if (child_source.kind
                                    == semantic::sv::StatementKind::assignment
                                && child_source.target
                                && hir_target_declaration(
                                       *child_source.target)
                                    == *parameter) {
                                return true;
                            }
                            if (self(self, child_source.statements)
                                || self(
                                    self,
                                    child_source.else_statements)) {
                                return true;
                            }
                            for (const auto& alternative :
                                child_source.case_alternatives) {
                                if (self(self, alternative.statements)) {
                                    return true;
                                }
                            }
                        }
                        return false;
                    };
                    if (assignment_to_index(
                            assignment_to_index, statements)) {
                        report(
                            "FSIM-ELAB-SVFOREACH-004",
                            "a procedural foreach body cannot assign its "
                            "implicit index variable",
                            span);
                        restore_scope();
                        return true;
                    }
                    const auto collection = lower_hir_string_expression(
                        foreach.operands.front());
                    if (!collection) {
                        restore_scope();
                        return false;
                    }
                    const auto length = allocate_register(
                        32U, binding->domain);
                    const auto one = allocate_register(
                        32U, binding->domain);
                    process_.operations.emplace_back(StringLength {
                        length, *collection });
                    process_.operations.emplace_back(LoadConstant {
                        *binding->local, unsigned_value(0U, 32U) });
                    process_.operations.emplace_back(LoadConstant {
                        one, unsigned_value(1U, 32U) });
                    const auto loop_start = static_cast<InstructionIndex>(
                        process_.operations.size());
                    const auto active = allocate_register(
                        1U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::less_signed,
                        active,
                        *binding->local,
                        length,
                    });
                    const auto branch = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(Branch {
                        active, 0U, 0U,
                        UnknownBranchPolicy::when_false });
                    const auto body = static_cast<InstructionIndex>(
                        process_.operations.size());
                    LoopControlContext loop;
                    loop.label = label;
                    loop_controls_.push_back(std::move(loop));
                    if (!lower_hir_statements(statements)) {
                        loop_controls_.pop_back();
                        restore_scope();
                        return false;
                    }
                    auto control = std::move(loop_controls_.back());
                    loop_controls_.pop_back();
                    const auto increment = static_cast<InstructionIndex>(
                        process_.operations.size());
                    for (const auto jump : control.continue_jumps) {
                        process_.operations[jump] = Jump { increment };
                    }
                    const auto next = allocate_register(
                        32U, binding->domain);
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::add_signed,
                        next,
                        *binding->local,
                        one,
                    });
                    process_.operations.emplace_back(CopyRegister {
                        *binding->local, next });
                    process_.operations.emplace_back(Jump { loop_start });
                    const auto end = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations[branch] = Branch {
                        active, body, end,
                        UnknownBranchPolicy::when_false
                    };
                    for (const auto jump : control.break_jumps) {
                        process_.operations[jump] = Jump { end };
                    }
                    restore_scope();
                    return true;
                }
                if (loop_statement.loop_repeat) {
                    if (!loop_statement.loop_limit) {
                        restore_scope();
                        return false;
                    }
                    auto limit = lower_hir_expression(
                        *loop_statement.loop_limit, 32U);
                    if (!limit) {
                        restore_scope();
                        return false;
                    }
                    if (register_width(*limit) != 32U) {
                        limit = resize_register(
                            *limit,
                            32U,
                            hir_expression_signed(
                                *loop_statement.loop_limit));
                    }
                    const auto counter = allocate_register(
                        32U, frontend::ValueDomain::Bit2);
                    const auto one = allocate_register(
                        32U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        counter, unsigned_value(0U, 32U) });
                    process_.operations.emplace_back(LoadConstant {
                        one, unsigned_value(1U, 32U) });
                    const auto loop_start = static_cast<InstructionIndex>(
                        process_.operations.size());
                    const auto active = allocate_register(
                        1U,
                        is_two_state_domain(register_domain(*limit))
                            ? frontend::ValueDomain::Bit2
                            : frontend::ValueDomain::Logic4);
                    process_.operations.emplace_back(Binary {
                        hir_expression_signed(*loop_statement.loop_limit)
                            ? BinaryOperator::less_signed
                            : BinaryOperator::less_unsigned,
                        active,
                        counter,
                        *limit,
                    });
                    const auto branch = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(Branch {
                        active, 0U, 0U, UnknownBranchPolicy::when_false });
                    const auto body = static_cast<InstructionIndex>(
                        process_.operations.size());
                    LoopControlContext loop;
                    loop.label = label;
                    loop_controls_.push_back(std::move(loop));
                    if (!lower_hir_statements(statements)) {
                        loop_controls_.pop_back();
                        restore_scope();
                        return false;
                    }
                    auto control = std::move(loop_controls_.back());
                    loop_controls_.pop_back();
                    const auto increment = static_cast<InstructionIndex>(
                        process_.operations.size());
                    for (const auto jump : control.continue_jumps) {
                        process_.operations[jump] = Jump { increment };
                    }
                    const auto next = allocate_register(
                        32U, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::add_signed,
                        next,
                        counter,
                        one,
                    });
                    process_.operations.emplace_back(CopyRegister {
                        counter, next });
                    process_.operations.emplace_back(Jump { loop_start });
                    const auto end = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations[branch] = Branch {
                        active, body, end, UnknownBranchPolicy::when_false
                    };
                    for (const auto jump : control.break_jumps) {
                        process_.operations[jump] = Jump { end };
                    }
                    restore_scope();
                    return true;
                }

                const bool runtime_for
                    = !loop_statement.loop_variable.empty()
                    && loop_statement.target && loop_statement.loop_initial
                    && loop_statement.condition && loop_statement.value;
                if (runtime_for) {
                    if (!lower_loop_assignment(
                            loop_statement.target,
                            loop_statement.loop_initial)) {
                        restore_scope();
                        return false;
                    }
                    const auto loop_start = static_cast<InstructionIndex>(
                        process_.operations.size());
                    const auto active = lower_condition(
                        *loop_statement.condition);
                    if (!active) {
                        restore_scope();
                        return false;
                    }
                    const auto branch = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(Branch {
                        *active, 0U, 0U, UnknownBranchPolicy::when_false });
                    const auto body = static_cast<InstructionIndex>(
                        process_.operations.size());
                    LoopControlContext loop;
                    loop.label = label;
                    loop_controls_.push_back(std::move(loop));
                    if (!lower_hir_statements(statements)) {
                        loop_controls_.pop_back();
                        restore_scope();
                        return false;
                    }
                    auto control = std::move(loop_controls_.back());
                    loop_controls_.pop_back();
                    const auto update = static_cast<InstructionIndex>(
                        process_.operations.size());
                    for (const auto jump : control.continue_jumps) {
                        process_.operations[jump] = Jump { update };
                    }
                    if (!lower_loop_assignment(
                            loop_statement.loop_update_target
                                ? loop_statement.loop_update_target
                                : loop_statement.target,
                            loop_statement.value)
                        || !lower_hir_statements(
                            loop_statement.loop_updates)) {
                        restore_scope();
                        return false;
                    }
                    process_.operations.emplace_back(Jump { loop_start });
                    const auto end = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations[branch] = Branch {
                        *active, body, end, UnknownBranchPolicy::when_false
                    };
                    for (const auto jump : control.break_jumps) {
                        process_.operations[jump] = Jump { end };
                    }
                    restore_scope();
                    return true;
                }
            }

            if (!condition) {
                restore_scope();
                return false;
            }
            if (statement->vhdl != nullptr) {
                const auto condition_domain = hir_expression_domain(
                    *condition, hir_process_scope_);
                const auto condition_width = hir_expression_width(
                    *condition, hir_process_scope_);
                const auto vhdl_2019_logic_condition
                    = vhdl_standard_
                          == frontend::VhdlStandard::Vhdl2019
                    && condition_width
                        == std::optional<std::size_t> { 1U }
                    && condition_domain
                    && (*condition_domain
                            == frontend::ValueDomain::Bit2
                        || *condition_domain
                            == frontend::ValueDomain::Logic4
                        || *condition_domain
                            == frontend::ValueDomain::Logic9);
                if ((!condition_domain
                        || *condition_domain
                            != frontend::ValueDomain::Boolean)
                    && !vhdl_2019_logic_condition) {
                    report(
                        "FSIM-ELAB-077",
                        "a VHDL while-loop condition must have type "
                        "boolean",
                        span);
                    restore_scope();
                    return true;
                }
            }
            const auto loop_start = static_cast<InstructionIndex>(
                process_.operations.size());
            if (statement->systemverilog != nullptr
                && statement->systemverilog->loop_post_test) {
                LoopControlContext loop;
                loop.label = label;
                loop_controls_.push_back(std::move(loop));
                if (!lower_hir_statements(statements)) {
                    loop_controls_.pop_back();
                    restore_scope();
                    return false;
                }
                auto control = std::move(loop_controls_.back());
                loop_controls_.pop_back();
                const auto condition_start
                    = static_cast<InstructionIndex>(
                        process_.operations.size());
                for (const auto jump : control.continue_jumps) {
                    process_.operations[jump] = Jump { condition_start };
                }
                const auto active = lower_condition(*condition);
                if (!active) {
                    restore_scope();
                    return false;
                }
                const auto branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(Branch {
                    *active,
                    loop_start,
                    branch + 1U,
                    UnknownBranchPolicy::when_false,
                });
                const auto end = static_cast<InstructionIndex>(
                    process_.operations.size());
                for (const auto jump : control.break_jumps) {
                    process_.operations[jump] = Jump { end };
                }
                restore_scope();
                return true;
            }
            const auto active = lower_condition(*condition);
            if (!active) {
                restore_scope();
                return false;
            }
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto unknown_policy = statement->vhdl != nullptr
                ? UnknownBranchPolicy::error
                : UnknownBranchPolicy::when_false;
            process_.operations.emplace_back(Branch {
                *active, 0U, 0U, unknown_policy });
            const auto body = static_cast<InstructionIndex>(
                process_.operations.size());
            LoopControlContext loop;
            loop.label = label;
            loop_controls_.push_back(std::move(loop));
            if (!lower_hir_statements(statements)) {
                loop_controls_.pop_back();
                restore_scope();
                return false;
            }
            auto control = std::move(loop_controls_.back());
            loop_controls_.pop_back();
            for (const auto jump : control.continue_jumps) {
                process_.operations[jump] = Jump { loop_start };
            }
            process_.operations.emplace_back(Jump { loop_start });
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                *active, body, end, unknown_policy
            };
            for (const auto jump : control.break_jumps) {
                process_.operations[jump] = Jump { end };
            }
            restore_scope();
            return true;
        }

        std::optional<semantic::DeclarationId> loop_declaration;
        std::optional<semantic::DeclarationId> vhdl_loop_declaration;
        std::optional<HirPackedRange> vhdl_attribute_range;
        std::string_view loop_variable;
        if (statement->vhdl != nullptr
            && !statement->vhdl->loop_variable.empty()) {
            const auto& loop_statement = *statement->vhdl;
            loop_variable = loop_statement.loop_variable;
            if (loop_statement.loop_initial
                && !loop_statement.loop_limit) {
                const auto attribute = hir_vhdl_attribute_profile(
                    *loop_statement.loop_initial, loop_statement.scope);
                if (attribute) {
                    vhdl_attribute_range = attribute->discrete_range;
                }
            }
            const auto initial = vhdl_attribute_range
                ? std::optional { vhdl_attribute_range->left }
                : loop_statement.loop_initial
                ? hir_constant_integer(*loop_statement.loop_initial)
                : std::nullopt;
            if (!initial) {
                report(
                    "FSIM-ELAB-071",
                    "a sequential VHDL for-loop initial bound must be "
                    "locally static",
                    span);
                restore_scope();
                return true;
            }
            const auto limit = vhdl_attribute_range
                ? std::optional { vhdl_attribute_range->right }
                : loop_statement.loop_limit
                ? hir_constant_integer(*loop_statement.loop_limit)
                : std::nullopt;
            if (!limit) {
                report(
                    "FSIM-ELAB-072",
                    "a sequential VHDL for-loop final bound must be "
                    "locally static",
                    span);
                restore_scope();
                return true;
            }
            const auto parameter = std::ranges::find_if(
                declarations,
                [&](const semantic::DeclarationId declaration_id) {
                    const auto declaration
                        = specialized_hir_unit_->find_declaration(
                            declaration_id);
                    return declaration
                        && declaration->vhdl != nullptr
                        && declaration->vhdl->name
                        == loop_statement.loop_variable;
                });
            if (parameter == declarations.end()) {
                restore_scope();
                return false;
            }
            loop_declaration = *parameter;
            vhdl_loop_declaration = *parameter;
        } else if (statement->systemverilog != nullptr
            && statement->systemverilog->loop_variable_declared
            && !statement->systemverilog->loop_variable.empty()) {
            // Only a variable declared by the for initializer belongs to
            // this loop's declaration list. A predeclared index remains in
            // its enclosing process or callable frame.
            loop_variable = statement->systemverilog->loop_variable;
            const auto parameter = std::ranges::find_if(
                declarations,
                [&](const semantic::DeclarationId declaration_id) {
                    const auto declaration
                        = specialized_hir_unit_->find_declaration(
                            declaration_id);
                    return declaration
                        && declaration->systemverilog != nullptr
                        && declaration->systemverilog->name
                        == loop_variable;
                });
            if (parameter == declarations.end()) {
                restore_scope();
                return false;
            }
            loop_declaration = *parameter;
        }
        const auto assignment_to_parameter
            = [&](auto&& self,
                  const std::span<const semantic::StatementId> children)
            -> std::optional<semantic::SourceSpanId> {
            for (const auto child_id : children) {
                const auto child = specialized_hir_unit_->find_statement(
                    child_id);
                if (!child) {
                    continue;
                }
                std::optional<semantic::ExpressionId> child_target;
                semantic::SourceSpanId child_source;
                std::span<const semantic::StatementId> child_statements;
                std::span<const semantic::StatementId> child_else_statements;
                if (child->systemverilog != nullptr) {
                    const auto& input = *child->systemverilog;
                    if (input.kind
                        == semantic::sv::StatementKind::assignment) {
                        child_target = input.target;
                    }
                    child_source = input.source;
                    child_statements = input.statements;
                    child_else_statements = input.else_statements;
                } else {
                    const auto& input = *child->vhdl;
                    if (input.kind
                        == semantic::vhdl::StatementKind::variable_assignment) {
                        child_target = input.target;
                    }
                    child_source = input.source;
                    child_statements = input.statements;
                    child_else_statements = input.else_statements;
                }
                if (child_target
                    && hir_target_declaration(*child_target)
                        == loop_declaration) {
                    return child_source;
                }
                if (const auto nested = self(self, child_statements)) {
                    return nested;
                }
                if (const auto nested = self(self, child_else_statements)) {
                    return nested;
                }
                if (child->systemverilog != nullptr) {
                    for (const auto& alternative :
                        child->systemverilog->case_alternatives) {
                        if (const auto nested
                            = self(self, alternative.statements)) {
                            return nested;
                        }
                    }
                } else {
                    for (const auto& alternative :
                        child->vhdl->alternatives) {
                        if (const auto nested
                            = self(self, alternative.statements)) {
                            return nested;
                        }
                    }
                }
            }
            return std::nullopt;
        };
        if (loop_declaration) {
            if (const auto illegal_assignment
                = assignment_to_parameter(
                    assignment_to_parameter, statements)) {
                report(
                    "FSIM-ELAB-074",
                    vhdl_loop_declaration
                        ? "a sequential VHDL for-loop body cannot assign its "
                          "implicit loop constant '"
                            + std::string { loop_variable } + "'"
                        : "a sequential SystemVerilog for-loop body cannot "
                          "assign its statically substituted index '"
                            + std::string { loop_variable } + "'",
                    hir_source_span(*illegal_assignment));
                restore_scope();
                return true;
            }
        }
        const auto iterations = static_loop_iterations;
        if (!iterations) {
            if (vhdl_loop_declaration
                || statement->systemverilog != nullptr) {
                report(
                    "FSIM-ELAB-073",
                    vhdl_loop_declaration
                        ? "a sequential VHDL for-loop exceeds the bounded "
                          "one-million-iteration elaboration limit"
                        : "a sequential SystemVerilog loop exceeds the "
                          "bounded one-million-iteration elaboration limit",
                    span);
                restore_scope();
                return true;
            }
            restore_scope();
            return false;
        }
        std::span<const semantic::StatementId> updates;
        if (statement->systemverilog != nullptr) {
            const auto& loop_statement = *statement->systemverilog;
            if (!loop_statement.loop_repeat
                && !loop_statement.loop_variable.empty()
                && !lower_loop_assignment(
                    loop_statement.target, loop_statement.loop_initial)) {
                restore_scope();
                return false;
            }
            updates = loop_statement.loop_updates;
        }
        LoopControlContext loop;
        loop.label = label;
        loop_controls_.push_back(std::move(loop));
        std::optional<RegisterId> vhdl_loop_parameter;
        std::optional<std::int64_t> vhdl_loop_initial;
        bool vhdl_loop_descending { };
        if (vhdl_loop_declaration) {
            const auto& loop_statement = *statement->vhdl;
            if (!loop_statement.loop_initial) {
                loop_controls_.pop_back();
                restore_scope();
                return false;
            }
            const auto local = hir_local_registers_.find(
                vhdl_loop_declaration->value());
            vhdl_loop_initial = vhdl_attribute_range
                ? std::optional { vhdl_attribute_range->left }
                : hir_constant_integer(*loop_statement.loop_initial);
            if (local == hir_local_registers_.end()
                || !vhdl_loop_initial) {
                loop_controls_.pop_back();
                restore_scope();
                return false;
            }
            vhdl_loop_parameter = local->second;
            vhdl_loop_descending = vhdl_attribute_range
                ? vhdl_attribute_range->descending
                : loop_statement.loop_descending;
        }
        for (std::size_t iteration = 0U;
            iteration < *iterations; ++iteration) {
            if (vhdl_loop_parameter && vhdl_loop_initial) {
                const auto ordinal = static_cast<std::int64_t>(iteration);
                const auto iteration_value = vhdl_loop_descending
                    ? *vhdl_loop_initial - ordinal
                    : *vhdl_loop_initial + ordinal;
                process_.operations.emplace_back(LoadConstant {
                    *vhdl_loop_parameter,
                    integer_value(
                        iteration_value,
                        register_width(*vhdl_loop_parameter)) });
            }
            if (!lower_hir_statements(statements)) {
                loop_controls_.pop_back();
                restore_scope();
                return false;
            }
            const auto update_start = static_cast<InstructionIndex>(
                process_.operations.size());
            if (statement->systemverilog != nullptr) {
                const auto& loop_statement = *statement->systemverilog;
                if ((!loop_statement.loop_variable.empty()
                        && !lower_loop_assignment(
                            loop_statement.loop_update_target,
                            loop_statement.value))
                    || !lower_hir_statements(updates)) {
                    loop_controls_.pop_back();
                    restore_scope();
                    return false;
                }
            }
            for (const auto jump :
                loop_controls_.back().continue_jumps) {
                process_.operations[jump] = Jump { update_start };
            }
            loop_controls_.back().continue_jumps.clear();
        }
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump : loop_controls_.back().break_jumps) {
            process_.operations[jump] = Jump { end };
        }
        loop_controls_.pop_back();
        restore_scope();
        return true;
    };
    const auto lower_block = [&](const std::string_view label) {
        const auto saved_scope = hir_process_scope_;
        const auto saved_hir_locals = hir_local_registers_;
        const auto saved_hir_string_locals
            = hir_local_string_registers_;
        const auto saved_locals = locals_;
        const auto saved_string_locals = string_locals_;
        const auto declaration_scope = local_scope_;
        const auto block_begin = static_cast<InstructionIndex>(
            process_.operations.size());
        hir_process_scope_ = nested_scope.value_or(hir_process_scope_);
        const auto scope_name = !label.empty()
            ? std::string { label }
            : nested_scope
            ? "$block_" + std::to_string(nested_scope->value())
            : std::string { "$block" };
        local_scope_.push_back(scope_name);
        const auto restore_scope = [&] {
            local_scope_.pop_back();
            string_locals_ = saved_string_locals;
            locals_ = saved_locals;
            hir_local_string_registers_ = saved_hir_string_locals;
            hir_local_registers_ = saved_hir_locals;
            hir_process_scope_ = saved_scope;
        };
        const bool named = !label.empty();
        std::optional<std::size_t> named_control_index;
        if (named) {
            block_controls_.push_back({
                std::string { label },
                { },
                { },
                static_cast<InstructionIndex>(process_.operations.size()),
            });
            named_control_index = named_block_controls_.size();
            named_block_controls_.push_back({
                std::string { label }, declaration_scope, block_begin,
                std::nullopt, { },
            });
        }
        if (!initialize_hir_declarations(declarations)
            || !lower_hir_statements(statements)) {
            if (named) {
                block_controls_.pop_back();
                named_block_controls_.resize(*named_control_index);
            }
            restore_scope();
            return false;
        }
        if (!named) {
            restore_scope();
            return true;
        }
        auto control = std::move(block_controls_.back());
        block_controls_.pop_back();
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto operation : control.disable_jumps) {
            process_.operations[operation] = DisableBlock {
                control.begin, end
            };
        }
        auto& named_control = named_block_controls_.at(
            *named_control_index);
        named_control.end = end;
        for (const auto operation : named_control.disable_operations) {
            process_.operations[operation] = DisableBlock {
                named_control.begin, end
            };
        }
        restore_scope();
        return true;
    };
    const auto lower_fork = [&](const semantic::sv::Statement& input) {
        const bool function = active_hir_callable_
            && hir_callable_frames_[*active_hir_callable_].function;
        const bool legal_function_background = function
            && systemverilog_standard_
                == frontend::StandardRevision::SystemVerilog2023
            && process_kind_ == frontend::ProcessKind::Initial
            && input.fork_join == semantic::sv::ForkJoinKind::none;
        if ((function && !legal_function_background)
            || (active_hir_callable_ && !function
                && input.fork_join
                    != semantic::sv::ForkJoinKind::all)) {
            report(
                "FSIM-ELAB-107",
                function
                    ? "a function may spawn fork...join_none background "
                        "processes only in SystemVerilog-2023 procedural "
                        "code originating in an initial block"
                    : "fork branches cannot escape a callable frame",
                span);
            return true;
        }

        const auto saved_scope = hir_process_scope_;
        const auto saved_hir_locals = hir_local_registers_;
        const auto saved_hir_string_locals
            = hir_local_string_registers_;
        const auto saved_locals = locals_;
        const auto saved_string_locals = string_locals_;
        const auto named_fork_count = named_fork_controls_.size();
        hir_process_scope_ = input.nested_scope.value_or(
            hir_process_scope_);
        const auto restore_scope = [&] {
            string_locals_ = saved_string_locals;
            locals_ = saved_locals;
            hir_local_string_registers_ = saved_hir_string_locals;
            hir_local_registers_ = saved_hir_locals;
            hir_process_scope_ = saved_scope;
        };
        if (!initialize_hir_declarations(input.declarations)) {
            restore_scope();
            return false;
        }

        const auto fork_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Fork { });
        if (!input.label.empty()) {
            named_fork_controls_.push_back(
                { input.label, local_scope_, fork_site });
        }
        const auto continuation_jump = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Jump { 0U });

        std::vector<InstructionIndex> branches;
        branches.reserve(input.statements.size());
        auto outer_loop_controls = std::move(loop_controls_);
        auto outer_block_controls = std::move(block_controls_);
        for (const auto branch : input.statements) {
            branches.push_back(static_cast<InstructionIndex>(
                process_.operations.size()));
            loop_controls_.clear();
            block_controls_ = outer_block_controls;
            std::vector<std::size_t> inherited_disable_counts;
            inherited_disable_counts.reserve(block_controls_.size());
            for (const auto& control : block_controls_) {
                inherited_disable_counts.push_back(
                    control.disable_jumps.size());
            }
            if (!input.label.empty()) {
                block_controls_.push_back(
                    { input.label, { }, fork_site, fork_site });
            }
            if (!lower_hir_statement(branch)) {
                loop_controls_ = std::move(outer_loop_controls);
                block_controls_ = std::move(outer_block_controls);
                named_fork_controls_.resize(named_fork_count);
                restore_scope();
                return false;
            }
            for (std::size_t index { };
                index < inherited_disable_counts.size(); ++index) {
                const auto first = inherited_disable_counts[index];
                outer_block_controls[index].disable_jumps.insert(
                    outer_block_controls[index].disable_jumps.end(),
                    block_controls_[index].disable_jumps.begin()
                        + static_cast<std::vector<InstructionIndex>::difference_type>(
                            first),
                    block_controls_[index].disable_jumps.end());
            }
            process_.operations.emplace_back(ForkEnd { });
        }
        loop_controls_ = std::move(outer_loop_controls);
        block_controls_ = std::move(outer_block_controls);

        const auto continuation = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[continuation_jump] = Jump { continuation };
        auto join = runtime::simir::ForkJoinKind::all;
        switch (input.fork_join) {
        case semantic::sv::ForkJoinKind::all:
            break;
        case semantic::sv::ForkJoinKind::any:
            join = runtime::simir::ForkJoinKind::any;
            break;
        case semantic::sv::ForkJoinKind::none:
            join = runtime::simir::ForkJoinKind::none;
            break;
        }
        process_.operations[fork_site] = Fork {
            std::move(branches), join
        };
        restore_scope();
        return true;
    };

    const auto lower_output_value = [&](const std::optional<semantic::ExpressionId> expression,
                                        const semantic::sv::OutputFormat format,
                                        std::string prefix,
                                        std::string suffix,
                                        const bool newline,
                                        const bool postponed,
                                        const bool suppress_leading_zero,
                                        const std::uint32_t minimum_width,
                                        const bool left_justify,
                                        const bool zero_pad) -> bool {
        if (format == semantic::sv::OutputFormat::hierarchy) {
            emit_deferred_assertion_action_handoff();
            process_.operations.emplace_back(Display {
                std::move(prefix) + hierarchy_ + std::move(suffix),
                newline,
                postponed,
            });
            return true;
        }
        const auto expression_source = expression
            ? specialized_hir_unit_->find_expression(*expression)
            : std::nullopt;
        if (format == semantic::sv::OutputFormat::time
            && (!expression
                || (expression_source
                    && expression_source->systemverilog != nullptr
                    && expression_source->systemverilog->text
                        == "$time"))) {
            emit_deferred_assertion_action_handoff();
            process_.operations.emplace_back(TimeDisplay {
                std::move(prefix),
                std::move(suffix),
                newline,
                postponed,
                minimum_width,
                left_justify,
                zero_pad,
                use_systemverilog_timeformat_width(
                    minimum_width, suppress_leading_zero),
            });
            return true;
        }
        if (!expression) {
            return false;
        }
        const bool typename_call = expression_source
            && expression_source->systemverilog != nullptr
            && expression_source->systemverilog->kind
                == semantic::sv::ExpressionKind::call
            && expression_source->systemverilog->text == "$typename";
        if ((format == semantic::sv::OutputFormat::string
                || format == semantic::sv::OutputFormat::decimal)
            && (typename_call || hir_expression_is_string(
                    *expression, hir_process_scope_))) {
            const auto displayed_string
                = lower_hir_string_expression(*expression);
            if (!displayed_string) {
                return false;
            }
            emit_deferred_assertion_action_handoff();
            process_.operations.emplace_back(StringDisplay {
                *displayed_string,
                std::move(prefix),
                std::move(suffix),
                newline,
                postponed,
            });
            return true;
        }
        const auto width = hir_expression_width(
            *expression, hir_process_scope_)
                               .value_or(32U);
        const auto displayed_value
            = lower_hir_expression(*expression, width);
        if (!displayed_value) {
            return false;
        }
        const auto runtime_format = output_format(format);
        emit_deferred_assertion_action_handoff();
        process_.operations.emplace_back(FormatDisplay {
            *displayed_value,
            runtime_format,
            std::move(prefix),
            std::move(suffix),
            newline,
            postponed,
            runtime_format == runtime::simir::OutputFormat::decimal
                && hir_expression_signed(*expression),
            suppress_leading_zero,
            minimum_width,
            left_justify,
            zero_pad,
            hir_systemverilog_scalar_kind(*expression),
        });
        return true;
    };

    if (statement->systemverilog != nullptr) {
        const auto& input = *statement->systemverilog;
        switch (input.kind) {
        case semantic::sv::StatementKind::assignment: {
            const bool procedural_delay = input.assignment_control
                == semantic::sv::AssignmentControl::delay;
            const bool procedural_event = input.assignment_control
                == semantic::sv::AssignmentControl::event;
            if (input.assignment_control
                    != semantic::sv::AssignmentControl::none
                && input.assignment_kind
                    != semantic::sv::AssignmentKind::blocking
                && input.assignment_kind
                    != semantic::sv::AssignmentKind::nonblocking) {
                report(
                    "FSIM-ELAB-105",
                    "procedural assignment control is attached to a "
                    "nonprocedural assignment kind",
                    span);
                return true;
            }
            if (input.assignment_control
                    == semantic::sv::AssignmentControl::none
                && !input.sensitivities.empty()) {
                report(
                    "FSIM-ELAB-105",
                    "procedural assignment event metadata has no "
                    "event-control kind",
                    span);
                return true;
            }
            if (procedural_delay
                && (!input.delay || !input.sensitivities.empty())) {
                report(
                    "FSIM-ELAB-105",
                    "procedural assignment delay control has invalid delay "
                    "or sensitivity metadata",
                    span);
                return true;
            }
            if (procedural_event
                && (input.delay || input.sensitivities.empty())) {
                report(
                    "FSIM-ELAB-105",
                    "procedural assignment event control has invalid delay "
                    "or sensitivity metadata",
                    span);
                return true;
            }
            if (input.assignment_control_repeated
                && (!procedural_event || !input.loop_limit)) {
                report(
                    "FSIM-ELAB-105",
                    "repeated procedural assignment event control has "
                    "invalid control or count metadata",
                    span);
                return true;
            }
            return lower_assignment(
                input.assignment_kind
                    == semantic::sv::AssignmentKind::continuous,
                input.assignment_kind
                    == semantic::sv::AssignmentKind::nonblocking);
        }
        case semantic::sv::StatementKind::force:
            return input.target && input.value
                && lower_hir_force_release(
                    *input.target,
                    input.value,
                    std::nullopt,
                    true,
                    input.source);
        case semantic::sv::StatementKind::release:
            return input.target
                && lower_hir_force_release(
                    *input.target,
                    std::nullopt,
                    std::nullopt,
                    false,
                    input.source);
        case semantic::sv::StatementKind::procedural_assign:
        case semantic::sv::StatementKind::deassign:
            return lower_hir_procedural_continuous_assignment(
                statement_id);
        case semantic::sv::StatementKind::conditional:
            return lower_if();
        case semantic::sv::StatementKind::selection:
            return lower_case(input.case_alternatives);
        case semantic::sv::StatementKind::loop:
            return lower_loop(input.label);
        case semantic::sv::StatementKind::break_loop:
            return lower_loop_control({ }, true);
        case semantic::sv::StatementKind::continue_loop:
            return lower_loop_control({ }, false);
        case semantic::sv::StatementKind::block:
            return lower_block(input.label);
        case semantic::sv::StatementKind::fork:
            return lower_fork(input);
        case semantic::sv::StatementKind::wait_fork:
            process_.operations.emplace_back(WaitFork { });
            return true;
        case semantic::sv::StatementKind::disable_fork:
            process_.operations.emplace_back(DisableFork { });
            return true;
        case semantic::sv::StatementKind::disable: {
            const auto visible = [&](const auto& candidate) {
                return candidate.label == input.task.spelling
                    && candidate.scope.size() <= local_scope_.size()
                    && std::ranges::equal(
                        candidate.scope,
                        std::span { local_scope_ }.first(
                            candidate.scope.size()));
            };
            const auto control = std::ranges::find(
                block_controls_.rbegin(), block_controls_.rend(),
                input.task.spelling, &BlockControlContext::label);
            if (control != block_controls_.rend() && control->fork_site) {
                process_.operations.emplace_back(DisableFork {
                    control->fork_site });
                return true;
            }
            if (control != block_controls_.rend()) {
                control->disable_jumps.push_back(
                    static_cast<InstructionIndex>(
                        process_.operations.size()));
                process_.operations.emplace_back(DisableBlock {
                    control->begin, 0U });
                return true;
            }
            const auto named_fork = std::ranges::find_if(
                named_fork_controls_.rbegin(),
                named_fork_controls_.rend(),
                visible);
            if (named_fork != named_fork_controls_.rend()) {
                process_.operations.emplace_back(DisableFork {
                    named_fork->site });
                return true;
            }
            const auto named_block = std::ranges::find_if(
                named_block_controls_.rbegin(),
                named_block_controls_.rend(),
                visible);
            if (named_block != named_block_controls_.rend()) {
                const auto operation = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(DisableBlock {
                    named_block->begin,
                    named_block->end.value_or(0U),
                });
                if (!named_block->end) {
                    named_block->disable_operations.push_back(operation);
                }
                return true;
            }
            report(
                "FSIM-ELAB-SVDISABLE-001",
                "disable target '" + input.task.spelling
                    + "' is not visible as a named block",
                span);
            return true;
        }
        case semantic::sv::StatementKind::task_call: {
            constexpr auto container_push_prefix
                = std::string_view { "@sv-container-push-back:" };
            constexpr std::array assertion_control_tasks {
                std::string_view { "$assertcontrol" },
                std::string_view { "$asserton" },
                std::string_view { "$assertoff" },
                std::string_view { "$assertkill" },
                std::string_view { "$assertpasson" },
                std::string_view { "$assertpassoff" },
                std::string_view { "$assertfailon" },
                std::string_view { "$assertfailoff" },
                std::string_view { "$assertnonvacuouson" },
                std::string_view { "$assertvacuousoff" },
            };
            constexpr auto coverage_sample_procedural_prefix
                = std::string_view { "@sv-coverage-sample-procedural:" };
            constexpr auto coverage_sample_explicit_prefix
                = std::string_view { "@sv-coverage-sample-explicit:" };
            constexpr auto coverage_sample_event_prefix
                = std::string_view { "@sv-coverage-sample-event:" };
            constexpr auto coverage_control_start_prefix
                = std::string_view { "@sv-coverage-control-start:" };
            constexpr auto coverage_control_stop_prefix
                = std::string_view { "@sv-coverage-control-stop:" };
            const auto coverage_database_task
                = input.task.spelling == "$set_coverage_db_name"
                || input.task.spelling == "$load_coverage_db";
            if (hir_synchronization_task_profile(
                    input, hir_process_scope_)) {
                return lower_hir_synchronization_statement(input);
            }
            const auto process_method_separator
                = input.task.spelling.rfind('.');
            const auto process_method
                = process_method_separator == std::string::npos
                ? std::string_view { }
                : std::string_view { input.task.spelling }.substr(
                      process_method_separator);
            const auto process_method_no_argument
                = process_method == ".await"
                || process_method == ".kill"
                || process_method == ".suspend"
                || process_method == ".resume";
            const auto process_method_one_argument
                = process_method == ".set_randstate"
                || process_method == ".srandom";
            if (process_method_no_argument
                || process_method_one_argument) {
                const auto receiver_name = std::string_view {
                    input.task.spelling
                }.substr(0U, process_method_separator);
                auto receiver = lower_named_packed(receiver_name);
                if (!receiver || register_width(*receiver) != 64U
                    || input.task_arguments.size()
                        != (process_method_one_argument ? 1U : 0U)
                    || (process_method_one_argument
                        && (!input.task_arguments.front().actual
                            || input.task_arguments.front().formal))) {
                    report(
                        "FSIM-ELAB-SVPROCESS-002",
                        "process control method has an invalid receiver or "
                        "argument count",
                        span);
                    return true;
                }
                if (process_method == ".await") {
                    process_.operations.emplace_back(
                        ProcessAwait { *receiver });
                } else if (process_method == ".kill") {
                    process_.operations.emplace_back(
                        ProcessKill { *receiver });
                } else if (process_method == ".suspend") {
                    process_.operations.emplace_back(
                        ProcessSuspend { *receiver });
                } else if (process_method == ".resume") {
                    process_.operations.emplace_back(
                        ProcessResume { *receiver });
                } else if (process_method == ".set_randstate") {
                    const auto state = lower_hir_string_expression(
                        *input.task_arguments.front().actual);
                    if (!state) {
                        return false;
                    }
                    process_.operations.emplace_back(
                        ProcessSetRandState { *receiver, *state });
                } else {
                    const auto seed_expression
                        = *input.task_arguments.front().actual;
                    auto seed = lower_hir_expression(
                        seed_expression, 32U);
                    if (!seed) {
                        return false;
                    }
                    if (register_width(*seed) != 32U) {
                        seed = resize_register(
                            *seed,
                            32U,
                            hir_expression_signed(seed_expression));
                    }
                    process_.operations.emplace_back(
                        ProcessSrandom { *receiver, *seed });
                }
                return true;
            }
            if (input.task.spelling == "$srandom") {
                if (language_
                        != frontend::Language::SystemVerilog2017
                    || input.task_arguments.size() != 1U
                    || input.task_arguments.front().formal
                    || !input.task_arguments.front().actual) {
                    report(
                        "FSIM-ELAB-SVRAND-007",
                        "$srandom requires one integral seed in "
                        "SystemVerilog",
                        span);
                    return true;
                }
                const auto seed_expression
                    = *input.task_arguments.front().actual;
                auto seed = lower_hir_expression(seed_expression, 32U);
                if (!seed) {
                    report(
                        "FSIM-ELAB-SVRAND-007",
                        "$srandom seed must be an integral expression",
                        hir_source_span(
                            input.task_arguments.front().source));
                    return true;
                }
                if (register_width(*seed) != 32U) {
                    seed = resize_register(
                        *seed,
                        32U,
                        hir_expression_signed(seed_expression));
                }
                const auto process = allocate_register(
                    64U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(ProcessSelf { process });
                process_.operations.emplace_back(ProcessSrandom {
                    process, *seed });
                return true;
            }
            if (input.task.spelling == "$system") {
                const auto valid = input.task_arguments.size() <= 1U
                    && (input.task_arguments.empty()
                        || (!input.task_arguments.front().formal
                            && input.task_arguments.front().actual
                            && hir_expression_is_string(
                                *input.task_arguments.front().actual,
                                hir_process_scope_)));
                if (language_ != frontend::Language::SystemVerilog2017
                    || !valid) {
                    report(
                        "FSIM-ELAB-SVSYS-001",
                        "$system requires SystemVerilog and zero or one "
                        "command string",
                        span);
                    return true;
                }
                emit_debug_point(DebugPointKind::call, span);
                const auto command = input.task_arguments.empty()
                    ? std::optional<StringRegisterId> { }
                    : lower_hir_string_expression(
                          *input.task_arguments.front().actual);
                if (!input.task_arguments.empty() && !command) {
                    return false;
                }
                process_.operations.emplace_back(SystemCommand {
                    command, std::nullopt });
                return true;
            }
            if (input.task.spelling == "$printtimescale") {
                const bool named = std::ranges::any_of(
                    input.task_arguments,
                    [](const semantic::sv::TaskAssociation& argument) {
                        return argument.formal.has_value();
                    });
                const auto argument = input.task_arguments.empty()
                        || !input.task_arguments.front().actual
                    ? std::optional<semantic::CompiledExpressionView> { }
                    : specialized_hir_unit_->find_expression(
                          *input.task_arguments.front().actual);
                const bool valid_argument = input.task_arguments.empty()
                    || (argument && argument->systemverilog != nullptr
                        && argument->systemverilog->kind
                            == semantic::sv::ExpressionKind::name
                        && !argument->systemverilog->text.empty());
                if (language_ != frontend::Language::SystemVerilog2017
                    || input.task_arguments.size() > 1U || named
                    || !valid_argument) {
                    report(
                        "FSIM-ELAB-SVTIME-002",
                        "$printtimescale accepts at most one hierarchical "
                        "identifier in SystemVerilog",
                        span);
                    return true;
                }
                process_.operations.emplace_back(Display {
                    std::string {
                        "\x1f"
                        "fsim.printtimescale|" }
                        + (argument
                                ? argument->systemverilog->text
                                : std::string { }),
                    true,
                    false,
                });
                return true;
            }
            if (input.task.spelling == "$timeformat") {
                const bool complete = input.task_arguments.size() == 4U
                    && std::ranges::all_of(
                        input.task_arguments,
                        [](const semantic::sv::TaskAssociation& argument) {
                            return !argument.formal && argument.actual;
                        });
                if (language_ != frontend::Language::SystemVerilog2017
                    || !complete) {
                    report(
                        "FSIM-ELAB-SVTIME-001",
                        "$timeformat requires units, precision, suffix, and "
                        "minimum width in SystemVerilog",
                        span);
                    return true;
                }
                const auto units = lower_hir_expression(
                    *input.task_arguments[0].actual, 32U);
                const auto precision = lower_hir_expression(
                    *input.task_arguments[1].actual, 32U);
                const auto suffix = lower_hir_string_expression(
                    *input.task_arguments[2].actual);
                const auto minimum_width = lower_hir_expression(
                    *input.task_arguments[3].actual, 32U);
                if (!units || !precision || !suffix || !minimum_width) {
                    report(
                        "FSIM-ELAB-SVTIME-001",
                        "$timeformat arguments require integral, integral, "
                        "string, and integral profiles",
                        span);
                    return true;
                }
                process_.operations.emplace_back(TimeFormatControl {
                    *units,
                    *precision,
                    *suffix,
                    *minimum_width,
                });
                return true;
            }
            const bool stochastic_queue_task
                = input.task.spelling == "$q_initialize"
                || input.task.spelling == "$q_add"
                || input.task.spelling == "$q_remove"
                || input.task.spelling == "$q_exam";
            if (stochastic_queue_task) {
                const bool named = std::ranges::any_of(
                    input.task_arguments,
                    [](const semantic::sv::TaskAssociation& argument) {
                        return argument.formal.has_value();
                    });
                const bool complete = input.task_arguments.size() == 4U
                    && std::ranges::all_of(
                        input.task_arguments,
                        [](const semantic::sv::TaskAssociation& argument) {
                            return argument.actual.has_value();
                        });
                if (language_ != frontend::Language::SystemVerilog2017
                    || named || !complete) {
                    report(
                        "FSIM-ELAB-SVQUEUE-001",
                        input.task.spelling
                            + " requires four positional 32-bit integer "
                              "arguments",
                        span);
                    return true;
                }

                std::array<bool, 4U> output {
                    false, false, false, true
                };
                if (input.task.spelling == "$q_remove") {
                    output = { false, true, true, true };
                } else if (input.task.spelling == "$q_exam") {
                    output = { false, false, true, true };
                }
                std::array<RegisterId, 4U> arguments { };
                for (std::size_t index { };
                    index < arguments.size(); ++index) {
                    if (output[index]) {
                        arguments[index] = allocate_register(
                            32U, frontend::ValueDomain::Integer);
                        continue;
                    }
                    const auto expression
                        = *input.task_arguments[index].actual;
                    auto argument = lower_hir_expression(expression, 32U);
                    if (!argument) {
                        return false;
                    }
                    if (register_width(*argument) != 32U) {
                        argument = resize_register(
                            *argument,
                            32U,
                            hir_expression_signed(expression));
                    }
                    arguments[index] = *argument;
                }

                StochasticQueueOperation operation;
                operation.queue_id = arguments[0];
                operation.status = arguments[3];
                if (input.task.spelling == "$q_initialize") {
                    operation.kind = StochasticQueueKind::initialize;
                    operation.queue_type = arguments[1];
                    operation.maximum_length = arguments[2];
                } else if (input.task.spelling == "$q_add") {
                    operation.kind = StochasticQueueKind::add;
                    operation.job_id = arguments[1];
                    operation.information_id = arguments[2];
                } else if (input.task.spelling == "$q_remove") {
                    operation.kind = StochasticQueueKind::remove;
                    operation.job_id = arguments[1];
                    operation.information_id = arguments[2];
                } else {
                    operation.kind = StochasticQueueKind::examine;
                    operation.statistic_code = arguments[1];
                    operation.statistic_value = arguments[2];
                }
                process_.operations.emplace_back(std::move(operation));
                for (std::size_t index { };
                    index < arguments.size(); ++index) {
                    if (!output[index]) {
                        continue;
                    }
                    if (!lower_hir_packed_copy_out(
                            *input.task_arguments[index].actual,
                            arguments[index])) {
                        report(
                            "FSIM-ELAB-SVQUEUE-001",
                            input.task.spelling
                                + " output arguments must be writable "
                                  "32-bit integers",
                            hir_source_span(
                                input.task_arguments[index].source));
                        return true;
                    }
                }
                return true;
            }

            const bool vcd_control_task
                = input.task.spelling == "$dumpfile"
                || input.task.spelling == "$dumpvars"
                || input.task.spelling == "$dumpoff"
                || input.task.spelling == "$dumpon"
                || input.task.spelling == "$dumpall"
                || input.task.spelling == "$dumplimit"
                || input.task.spelling == "$dumpflush"
                || input.task.spelling == "$dumpports"
                || input.task.spelling == "$dumpportsoff"
                || input.task.spelling == "$dumpportson"
                || input.task.spelling == "$dumpportsall"
                || input.task.spelling == "$dumpportslimit"
                || input.task.spelling == "$dumpportsflush";
            if (vcd_control_task) {
                const bool named = std::ranges::any_of(
                    input.task_arguments,
                    [](const semantic::sv::TaskAssociation& argument) {
                        return argument.formal.has_value();
                    });
                if ((language_ != frontend::Language::SystemVerilog2017
                        && language_ != frontend::Language::Verilog2005)
                    || named) {
                    report(
                        "FSIM-ELAB-SVVCD-001",
                        input.task.spelling
                            + " requires positional Verilog/SystemVerilog "
                              "arguments",
                        span);
                    return true;
                }

                VcdControl operation;
                operation.scope = hierarchy_;
                const bool extended = input.task.spelling.starts_with(
                    "$dumpports");
                const auto expression_record = [&](const std::size_t index) {
                    return input.task_arguments[index].actual
                        ? specialized_hir_unit_->find_expression(
                              *input.task_arguments[index].actual)
                        : std::optional<semantic::CompiledExpressionView> { };
                };
                const auto filename_expression = [&](const std::size_t index) {
                    const auto expression = expression_record(index);
                    const auto declaration
                        = input.task_arguments[index].actual
                        ? hir_target_declaration(
                              *input.task_arguments[index].actual)
                        : std::nullopt;
                    const auto string_binding = declaration
                        ? hir_string_binding(
                              *declaration, hir_process_scope_, false)
                        : std::nullopt;
                    return expression
                        && expression->systemverilog != nullptr
                        && (expression->systemverilog->kind
                                != semantic::sv::ExpressionKind::name
                            || string_binding
                            || hir_expression_is_string(
                                *input.task_arguments[index].actual,
                                hir_process_scope_));
                };
                const auto lower_filename = [&](const std::size_t index)
                    -> std::optional<StringRegisterId> {
                    if (!input.task_arguments[index].actual) {
                        return std::nullopt;
                    }
                    return lower_hir_string_expression(
                        *input.task_arguments[index].actual);
                };
                const auto append_selection = [&](const std::size_t index) {
                    const auto expression = expression_record(index);
                    if (!expression || expression->systemverilog == nullptr
                        || expression->systemverilog->kind
                            != semantic::sv::ExpressionKind::name
                        || expression->systemverilog->text.empty()) {
                        return false;
                    }
                    operation.selections.push_back(
                        expression->systemverilog->text);
                    return true;
                };

                if (extended && input.task.spelling == "$dumpports") {
                    operation.kind = VcdControlKind::ports;
                    std::optional<std::size_t> filename_index;
                    auto selection_end = input.task_arguments.size();
                    if (!input.task_arguments.empty()
                        && !input.task_arguments.front().actual) {
                        if (input.task_arguments.size() != 2U
                            || !input.task_arguments[1].actual) {
                            report(
                                "FSIM-ELAB-SVVCD-001",
                                "$dumpports permits a null scope only "
                                "before one filename expression",
                                span);
                            return true;
                        }
                        filename_index = 1U;
                        selection_end = 0U;
                    } else if (!input.task_arguments.empty()
                        && filename_expression(
                            input.task_arguments.size() - 1U)) {
                        filename_index = input.task_arguments.size() - 1U;
                        selection_end = *filename_index;
                    }
                    for (std::size_t index { };
                        index < selection_end; ++index) {
                        if (!append_selection(index)) {
                            report(
                                "FSIM-ELAB-SVVCD-001",
                                "$dumpports scope selections must name "
                                "modules",
                                hir_source_span(
                                    input.task_arguments[index].source));
                            return true;
                        }
                    }
                    if (filename_index) {
                        operation.filename = lower_filename(*filename_index);
                        if (!operation.filename) {
                            return false;
                        }
                    }
                } else if (extended
                    && input.task.spelling == "$dumpportslimit") {
                    if (input.task_arguments.empty()
                        || input.task_arguments.size() > 2U
                        || !input.task_arguments.front().actual
                        || (input.task_arguments.size() == 2U
                            && !input.task_arguments[1].actual)) {
                        report(
                            "FSIM-ELAB-SVVCD-001",
                            "$dumpportslimit requires a byte count and "
                            "optional filename",
                            span);
                        return true;
                    }
                    operation.kind = VcdControlKind::ports_limit;
                    const auto expression
                        = *input.task_arguments.front().actual;
                    auto limit = lower_hir_expression(expression, 64U);
                    if (!limit) {
                        report(
                            "FSIM-ELAB-SVVCD-001",
                            "$dumpportslimit byte count must be an "
                            "integral expression",
                            hir_source_span(
                                input.task_arguments.front().source));
                        return true;
                    }
                    if (register_width(*limit) != 64U) {
                        limit = resize_register(*limit, 64U, false);
                    }
                    operation.value = *limit;
                    if (input.task_arguments.size() == 2U) {
                        operation.filename = lower_filename(1U);
                        if (!operation.filename) {
                            return false;
                        }
                    }
                } else if (extended) {
                    if (input.task_arguments.size() > 1U
                        || (!input.task_arguments.empty()
                            && !input.task_arguments.front().actual)) {
                        report(
                            "FSIM-ELAB-SVVCD-001",
                            input.task.spelling
                                + " accepts at most one filename expression",
                            span);
                        return true;
                    }
                    operation.kind
                        = input.task.spelling == "$dumpportsoff"
                        ? VcdControlKind::ports_off
                        : input.task.spelling == "$dumpportson"
                        ? VcdControlKind::ports_on
                        : input.task.spelling == "$dumpportsall"
                        ? VcdControlKind::ports_all
                        : VcdControlKind::ports_flush;
                    if (!input.task_arguments.empty()) {
                        operation.filename = lower_filename(0U);
                        if (!operation.filename) {
                            return false;
                        }
                    }
                } else if (input.task.spelling == "$dumpfile") {
                    if (input.task_arguments.size() > 1U
                        || (!input.task_arguments.empty()
                            && !input.task_arguments.front().actual)) {
                        report(
                            "FSIM-ELAB-SVVCD-001",
                            "$dumpfile accepts zero or one filename "
                            "expression",
                            span);
                        return true;
                    }
                    operation.kind = VcdControlKind::file;
                    if (!input.task_arguments.empty()) {
                        operation.filename = lower_filename(0U);
                        if (!operation.filename) {
                            return false;
                        }
                    }
                } else if (input.task.spelling == "$dumpvars") {
                    operation.kind = VcdControlKind::variables;
                    if (!input.task_arguments.empty()) {
                        if (input.task_arguments.size() < 2U
                            || !input.task_arguments.front().actual) {
                            report(
                                "FSIM-ELAB-SVVCD-001",
                                "$dumpvars arguments require levels and at "
                                "least one module or variable",
                                span);
                            return true;
                        }
                        const auto expression
                            = *input.task_arguments.front().actual;
                        auto levels = lower_hir_expression(expression, 64U);
                        if (!levels) {
                            report(
                                "FSIM-ELAB-SVVCD-001",
                                "$dumpvars levels must be an integral "
                                "expression",
                                hir_source_span(
                                    input.task_arguments.front().source));
                            return true;
                        }
                        if (register_width(*levels) != 64U) {
                            levels = resize_register(*levels, 64U, false);
                        }
                        operation.value = *levels;
                        for (std::size_t index { 1U };
                            index < input.task_arguments.size(); ++index) {
                            if (!append_selection(index)) {
                                report(
                                    "FSIM-ELAB-SVVCD-001",
                                    "$dumpvars selections must name modules "
                                    "or variables",
                                    hir_source_span(
                                        input.task_arguments[index].source));
                                return true;
                            }
                        }
                    }
                } else if (input.task.spelling == "$dumplimit") {
                    if (input.task_arguments.size() != 1U
                        || !input.task_arguments.front().actual) {
                        report(
                            "FSIM-ELAB-SVVCD-001",
                            "$dumplimit requires one byte-count expression",
                            span);
                        return true;
                    }
                    operation.kind = VcdControlKind::limit;
                    const auto expression
                        = *input.task_arguments.front().actual;
                    auto limit = lower_hir_expression(expression, 64U);
                    if (!limit) {
                        report(
                            "FSIM-ELAB-SVVCD-001",
                            "$dumplimit byte count must be an integral "
                            "expression",
                            hir_source_span(
                                input.task_arguments.front().source));
                        return true;
                    }
                    if (register_width(*limit) != 64U) {
                        limit = resize_register(*limit, 64U, false);
                    }
                    operation.value = *limit;
                } else {
                    if (!input.task_arguments.empty()) {
                        report(
                            "FSIM-ELAB-SVVCD-001",
                            input.task.spelling + " does not accept arguments",
                            span);
                        return true;
                    }
                    operation.kind = input.task.spelling == "$dumpoff"
                        ? VcdControlKind::off
                        : input.task.spelling == "$dumpon"
                        ? VcdControlKind::on
                        : input.task.spelling == "$dumpall"
                        ? VcdControlKind::all
                        : VcdControlKind::flush;
                }
                process_.operations.emplace_back(std::move(operation));
                return true;
            }

            bool pla_asynchronous { };
            auto pla_remainder = std::string_view { input.task.spelling };
            if (pla_remainder.starts_with("$async$")) {
                pla_asynchronous = true;
                pla_remainder.remove_prefix(7U);
            } else if (pla_remainder.starts_with("$sync$")) {
                pla_remainder.remove_prefix(6U);
            }
            std::optional<PlaLogicKind> pla_logic;
            if (pla_remainder.starts_with("and$")) {
                pla_logic = PlaLogicKind::and_logic;
                pla_remainder.remove_prefix(4U);
            } else if (pla_remainder.starts_with("nand$")) {
                pla_logic = PlaLogicKind::nand_logic;
                pla_remainder.remove_prefix(5U);
            } else if (pla_remainder.starts_with("or$")) {
                pla_logic = PlaLogicKind::or_logic;
                pla_remainder.remove_prefix(3U);
            } else if (pla_remainder.starts_with("nor$")) {
                pla_logic = PlaLogicKind::nor_logic;
                pla_remainder.remove_prefix(4U);
            }
            const bool pla_plane = pla_remainder == "plane";
            const bool pla_task = pla_logic
                && (pla_plane || pla_remainder == "array");
            if (pla_task) {
                const bool named = std::ranges::any_of(
                    input.task_arguments,
                    [](const semantic::sv::TaskAssociation& argument) {
                        return argument.formal.has_value();
                    });
                const bool complete = input.task_arguments.size() == 3U
                    && std::ranges::all_of(
                        input.task_arguments,
                        [](const semantic::sv::TaskAssociation& argument) {
                            return argument.actual.has_value();
                        });
                const auto memory_expression = complete
                    ? specialized_hir_unit_->find_expression(
                          *input.task_arguments[0].actual)
                    : std::nullopt;
                if (language_ != frontend::Language::SystemVerilog2017
                    || named || !complete || !memory_expression
                    || memory_expression->systemverilog == nullptr
                    || memory_expression->systemverilog->kind
                        != semantic::sv::ExpressionKind::name) {
                    report(
                        "FSIM-ELAB-SVPLA-001",
                        input.task.spelling
                            + " requires a fixed personality memory, packed "
                              "input terms, and a packed variable output",
                        span);
                    return true;
                }
                const auto memory = hir_container_object_binding(
                    *input.task_arguments[0].actual);
                if (!memory || memory->local || memory->type == nullptr) {
                    report(
                        "FSIM-ELAB-SVPLA-001",
                        "PLA personality requires a direct fixed-array "
                        "object",
                        hir_source_span(input.task_arguments[0].source));
                    return true;
                }
                const auto& memory_type = *memory->type;
                const bool packed_memory
                    = memory_type.element_kind
                        == ContainerElementKind::Packed
                    || memory_type.element_kind
                        == ContainerElementKind::Scalar;
                if (!memory_type.fixed || !packed_memory
                    || memory_type.dimensions.size() != 1U
                    || memory_type.dimensions.front().first
                        > memory_type.dimensions.front().second
                    || memory_type.element_width == 0U) {
                    report(
                        "FSIM-ELAB-SVPLA-001",
                        "PLA personality must be an ascending "
                        "one-dimensional fixed packed array",
                        hir_source_span(input.task_arguments[0].source));
                    return true;
                }
                const auto row_count = static_cast<std::uint64_t>(
                                           static_cast<std::int64_t>(
                                               memory_type.dimensions.front()
                                                   .second)
                                           - memory_type.dimensions.front()
                                                 .first)
                    + 1U;
                if (row_count == 0U
                    || row_count
                        > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-SVPLA-001",
                        "PLA output width is outside the runtime packed-"
                        "width representation",
                        hir_source_span(input.task_arguments[0].source));
                    return true;
                }
                const auto input_width
                    = static_cast<std::size_t>(memory_type.element_width);
                const auto output_width
                    = static_cast<std::size_t>(row_count);
                if (hir_expression_width(
                        *input.task_arguments[1].actual,
                        hir_process_scope_)
                        != std::optional { input_width }
                    || hir_expression_width(
                           *input.task_arguments[2].actual,
                           hir_process_scope_)
                        != std::optional { output_width }) {
                    report(
                        "FSIM-ELAB-SVPLA-001",
                        "PLA input and output widths must match the "
                        "personality columns and rows",
                        span);
                    return true;
                }
                auto input_signals = hir_signal_dependencies(
                    *input.task_arguments[1].actual,
                    hir_process_scope_);
                std::ranges::sort(input_signals);
                auto duplicate = std::ranges::unique(input_signals);
                input_signals.erase(duplicate.begin(), duplicate.end());
                const auto emit_evaluation = [&]() {
                    const auto input_expression
                        = *input.task_arguments[1].actual;
                    auto terms = lower_hir_expression(
                        input_expression, input_width);
                    if (!terms) {
                        return false;
                    }
                    if (register_width(*terms) != input_width) {
                        terms = resize_register(
                            *terms,
                            input_width,
                            hir_expression_signed(input_expression));
                    }
                    const auto output = allocate_register(
                        output_width, frontend::ValueDomain::Logic4);
                    process_.operations.emplace_back(PlaEvaluate {
                        memory->object,
                        *terms,
                        output,
                        static_cast<std::uint32_t>(input_width),
                        static_cast<std::uint32_t>(output_width),
                        *pla_logic,
                        pla_plane,
                    });
                    return lower_hir_packed_copy_out(
                        *input.task_arguments[2].actual, output);
                };
                if (!pla_asynchronous) {
                    if (!emit_evaluation()) {
                        report(
                            "FSIM-ELAB-SVPLA-001",
                            "PLA output must be a writable packed variable",
                            hir_source_span(input.task_arguments[2].source));
                    }
                    return true;
                }
                if (active_hir_callable_) {
                    report(
                        "FSIM-ELAB-SVPLA-001",
                        "an asynchronous PLA cannot escape a callable "
                        "frame",
                        span);
                    return true;
                }
                const auto fork_instruction
                    = static_cast<InstructionIndex>(
                        process_.operations.size());
                process_.operations.emplace_back(Fork { });
                const auto continuation_jump
                    = static_cast<InstructionIndex>(
                        process_.operations.size());
                process_.operations.emplace_back(Jump { });
                const auto branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                const auto loop = branch;
                if (!emit_evaluation()) {
                    report(
                        "FSIM-ELAB-SVPLA-001",
                        "PLA output must be a writable packed variable",
                        hir_source_span(input.task_arguments[2].source));
                    return true;
                }
                process_.operations.emplace_back(WaitPla {
                    memory->object, std::move(input_signals) });
                process_.operations.emplace_back(Jump { loop });
                const auto continuation = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations[continuation_jump]
                    = Jump { continuation };
                process_.operations[fork_instruction] = Fork {
                    { branch }, ForkJoinKind::none
                };
                return true;
            }
            if (std::ranges::find(
                    assertion_control_tasks, input.task.spelling)
                != assertion_control_tasks.end()) {
                auto marker = std::string { "\x1f"
                                            "fsim.assertion-control|" }
                    + input.task.spelling.substr(1U);
                if (input.task.spelling == "$assertcontrol"
                    && !input.task_arguments.empty()
                    && input.task_arguments.front().actual) {
                    const auto argument
                        = *input.task_arguments.front().actual;
                    const auto control_value = specialized_hir_unit_
                                                   ->evaluate_integral_expression(
                                                       argument);
                    if (control_value) {
                        marker += "|" + std::to_string(*control_value);
                    } else if (const auto expression
                        = specialized_hir_unit_->find_expression(argument);
                        expression
                        && expression->systemverilog != nullptr) {
                        marker += "|" + expression->systemverilog->text;
                    }
                }
                process_.operations.emplace_back(Display {
                    std::move(marker), false, false });
                return true;
            }
            if (input.task.spelling.starts_with(container_push_prefix)) {
                if (input.task_arguments.size() != 2U
                    || !input.task_arguments[0].actual
                    || !input.task_arguments[1].actual) {
                    return false;
                }
                const auto receiver = lower_hir_expression(
                    *input.task_arguments[0].actual, 64U);
                const auto pushed_value = lower_hir_expression(
                    *input.task_arguments[1].actual, 64U);
                if (!receiver || register_width(*receiver) != 64U
                    || !pushed_value
                    || register_width(*pushed_value) != 64U) {
                    return false;
                }
                const auto destination = allocate_register(
                    64U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(ClassMethodCall {
                    destination,
                    *receiver,
                    "@container-push-back:"
                        + input.task.spelling.substr(
                            container_push_prefix.size()),
                    { *pushed_value },
                    { "" },
                    { static_cast<std::uint8_t>(
                        frontend::PortDirection::Input) },
                    64U,
                    false,
                });
                return true;
            }
            if (input.task.spelling.starts_with(
                    coverage_control_start_prefix)
                || input.task.spelling.starts_with(
                    coverage_control_stop_prefix)) {
                if (!input.task_arguments.empty()) {
                    return false;
                }
                const auto start = input.task.spelling.starts_with(
                    coverage_control_start_prefix);
                const auto prefix = start
                    ? coverage_control_start_prefix
                    : coverage_control_stop_prefix;
                process_.operations.emplace_back(Display {
                    std::string { "\x1f"
                                  "fsim.coverage-control|" }
                        + (start ? "start|" : "stop|")
                        + input.task.spelling.substr(prefix.size()),
                    false,
                    false,
                });
                return true;
            }
            if (input.task.spelling.starts_with(
                    coverage_sample_procedural_prefix)
                || input.task.spelling.starts_with(
                    coverage_sample_explicit_prefix)
                || input.task.spelling.starts_with(
                    coverage_sample_event_prefix)) {
                const auto trigger = input.task.spelling.starts_with(
                                         coverage_sample_procedural_prefix)
                    ? CoverageSampleTrigger::procedural
                    : input.task.spelling.starts_with(
                          coverage_sample_event_prefix)
                    ? CoverageSampleTrigger::event
                    : CoverageSampleTrigger::explicit_sample;
                const auto prefix
                    = trigger == CoverageSampleTrigger::procedural
                    ? coverage_sample_procedural_prefix
                    : trigger == CoverageSampleTrigger::event
                    ? coverage_sample_event_prefix
                    : coverage_sample_explicit_prefix;
                std::vector<RegisterId> actuals;
                std::vector<std::uint32_t> widths;
                std::vector<std::uint8_t> signed_actuals;
                std::vector<frontend::SystemVerilogScalarKind>
                    scalar_kinds;
                actuals.reserve(input.task_arguments.size());
                widths.reserve(input.task_arguments.size());
                signed_actuals.reserve(input.task_arguments.size());
                scalar_kinds.reserve(input.task_arguments.size());
                for (const auto& argument : input.task_arguments) {
                    if (!argument.actual) {
                        return false;
                    }
                    const auto width = hir_expression_width(
                        *argument.actual, hir_process_scope_);
                    if (!width || *width == 0U
                        || *width
                            > std::numeric_limits<std::uint32_t>::max()) {
                        report(
                            "FSIM-ELAB-SVTASK-014",
                            "covergroup sample actual requires a positive "
                            "packed width",
                            hir_source_span(argument.source));
                        return true;
                    }
                    const auto actual = lower_hir_expression(
                        *argument.actual, *width);
                    if (!actual) {
                        return false;
                    }
                    const auto declaration_id
                        = hir_referenced_declaration(*argument.actual);
                    const auto declaration = declaration_id
                        ? specialized_hir_unit_->find_declaration(
                              *declaration_id)
                        : std::nullopt;
                    const auto spelling
                        = declaration
                            && declaration->systemverilog != nullptr
                            && declaration->systemverilog->type
                        ? std::string_view {
                              declaration->systemverilog->type
                                  ->target.spelling
                          }
                        : std::string_view { };
                    actuals.push_back(*actual);
                    widths.push_back(static_cast<std::uint32_t>(*width));
                    signed_actuals.push_back(
                        hir_expression_signed(*argument.actual) ? 1U : 0U);
                    auto scalar_kind
                        = systemverilog_scalar_kind(spelling);
                    const auto expression
                        = specialized_hir_unit_->find_expression(
                            *argument.actual);
                    if (scalar_kind
                            == frontend::SystemVerilogScalarKind::None
                        && expression
                        && expression->systemverilog != nullptr
                        && expression->systemverilog->kind
                            == semantic::sv::ExpressionKind::integer_literal
                        && systemverilog_real_literal(
                            expression->systemverilog->text)) {
                        scalar_kind
                            = frontend::SystemVerilogScalarKind::Real;
                    }
                    scalar_kinds.push_back(scalar_kind);
                }
                process_.operations.emplace_back(CoverageSample {
                    input.task.spelling.substr(prefix.size()),
                    std::move(actuals),
                    std::move(widths),
                    std::move(signed_actuals),
                    std::move(scalar_kinds),
                    trigger,
                });
                return true;
            }
            if (coverage_database_task) {
                if (input.task_arguments.size() != 1U
                    || input.task_arguments.front().formal
                    || !input.task_arguments.front().actual) {
                    report(
                        "FSIM-ELAB-SVCOV-002",
                        input.task.spelling
                            + " requires exactly one positional string "
                              "argument",
                        span);
                    return true;
                }
                const auto filename = lower_hir_string_expression(
                    *input.task_arguments.front().actual);
                if (!filename) {
                    return false;
                }
                process_.operations.emplace_back(CoverageDatabaseControl {
                    input.task.spelling == "$load_coverage_db"
                        ? CoverageDatabaseControlKind::load
                        : CoverageDatabaseControlKind::set_name,
                    *filename,
                });
                return true;
            }
            if (hir_string_format_task_status(
                    statement_id, hir_process_scope_)
                != HirStringFormatStatus::not_applicable) {
                return lower_hir_string_format_task(statement_id);
            }
            return lower_hir_class_task_call(statement_id);
        }
        case semantic::sv::StatementKind::display:
            if (input.output_monitor || input.output_postponed) {
                MonitorInstall monitor;
                monitor.newline = input.output_newline;
                monitor.one_shot = input.output_postponed
                    && !input.output_monitor;
                if (input.output_values.empty()
                    && !input.output_format) {
                    monitor.trailing_text = input.value
                        ? specialized_hir_unit_
                              ->evaluate_string_expression(*input.value)
                              .value_or(input.output_text)
                        : input.output_text;
                    process_.operations.emplace_back(std::move(monitor));
                    return true;
                }
                std::string pending_prefix;
                bool valid = true;
                const auto append = [&](const std::optional<semantic::ExpressionId> expression,
                                        const semantic::sv::OutputFormat format,
                                        std::string prefix,
                                        const bool suppress_leading_zero,
                                        const std::uint32_t minimum_width,
                                        const bool left_justify,
                                        const bool zero_pad) {
                    prefix = std::move(pending_prefix) + std::move(prefix);
                    if (format
                        == semantic::sv::OutputFormat::hierarchy) {
                        pending_prefix = std::move(prefix) + hierarchy_;
                        return;
                    }
                    MonitorValue monitor_value;
                    monitor_value.prefix = std::move(prefix);
                    monitor_value.minimum_width = minimum_width;
                    monitor_value.left_justify = left_justify;
                    monitor_value.zero_pad = zero_pad;
                    monitor_value.suppress_leading_zero
                        = suppress_leading_zero;
                    if (format == semantic::sv::OutputFormat::time) {
                        monitor_value.kind = MonitorValueKind::time;
                        monitor_value.use_timeformat_width
                            = use_systemverilog_timeformat_width(
                                minimum_width, suppress_leading_zero);
                        monitor.values.push_back(
                            std::move(monitor_value));
                        return;
                    }
                    const auto string_value = expression
                        ? specialized_hir_unit_
                              ->evaluate_string_expression(*expression)
                        : std::optional<std::string> { };
                    if (string_value) {
                        pending_prefix = std::move(monitor_value.prefix)
                            + *string_value;
                        return;
                    }
                    const auto declaration = expression
                        ? hir_target_declaration(*expression)
                        : std::nullopt;
                    const auto binding = declaration
                        ? hir_runtime_binding(
                              *declaration, hir_process_scope_, false)
                        : std::nullopt;
                    if (!expression || !binding || !binding->signal) {
                        report(
                            monitor.one_shot
                                ? "FSIM-ELAB-108"
                                : "FSIM-ELAB-103",
                            monitor.one_shot
                                ? "$strobe currently requires direct packed-"
                                  "signal value expressions"
                                : "$monitor currently requires direct packed-"
                                  "signal value expressions",
                            span);
                        valid = false;
                        return;
                    }
                    monitor_value.kind = MonitorValueKind::signal;
                    monitor_value.signal = *binding->signal;
                    monitor_value.format = output_format(format);
                    monitor_value.scalar_kind
                        = hir_systemverilog_scalar_kind(
                        *expression);
                    monitor_value.signed_decimal = monitor_value.format
                            == runtime::simir::OutputFormat::decimal
                        && hir_expression_signed(*expression);
                    monitor.values.push_back(std::move(monitor_value));
                };
                if (!input.output_values.empty()) {
                    for (const auto& output : input.output_values) {
                        append(
                            output.value,
                            output.format,
                            output.prefix,
                            output.suppress_leading_zero,
                            output.minimum_width,
                            output.left_justify,
                            output.zero_pad);
                    }
                    monitor.trailing_text = std::move(pending_prefix)
                        + input.output_trailing_text;
                } else {
                    append(
                        input.value,
                        *input.output_format,
                        input.output_prefix,
                        input.output_suppress_leading_zero,
                        input.output_minimum_width,
                        input.output_left_justify,
                        input.output_zero_pad);
                    monitor.trailing_text = std::move(pending_prefix)
                        + input.output_suffix;
                }
                if (valid) {
                    process_.operations.emplace_back(std::move(monitor));
                }
                return true;
            }
            if (!input.output_values.empty()) {
                for (std::size_t index = 0U;
                    index < input.output_values.size(); ++index) {
                    const auto& output = input.output_values[index];
                    const auto last
                        = index + 1U == input.output_values.size();
                    if (!lower_output_value(
                            output.value,
                            output.format,
                            output.prefix,
                            last ? input.output_trailing_text
                                 : std::string { },
                            last && input.output_newline,
                            input.output_postponed,
                            output.suppress_leading_zero,
                            output.minimum_width,
                            output.left_justify,
                            output.zero_pad)) {
                        return false;
                    }
                }
                return true;
            }
            if (!input.output_format) {
                if (input.value) {
                    const auto message
                        = specialized_hir_unit_->evaluate_string_expression(
                            *input.value);
                    if (!message) {
                        return true;
                    }
                    emit_deferred_assertion_action_handoff();
                    process_.operations.emplace_back(Display {
                        *message,
                        input.output_newline,
                        input.output_postponed,
                    });
                    return true;
                }
                emit_deferred_assertion_action_handoff();
                process_.operations.emplace_back(Display {
                    input.output_text,
                    input.output_newline,
                    input.output_postponed,
                });
                return true;
            }
            return input.value
                && lower_output_value(
                    input.value,
                    *input.output_format,
                    input.output_prefix,
                    input.output_suffix,
                    input.output_newline,
                    input.output_postponed,
                    input.output_suppress_leading_zero,
                    input.output_minimum_width,
                    input.output_left_justify,
                    input.output_zero_pad);
        case semantic::sv::StatementKind::report: {
            emit_deferred_assertion_action_handoff();
            const auto report_text = input.value
                ? specialized_hir_unit_->evaluate_string_expression(
                      *input.value)
                : std::optional<std::string> { };
            process_.operations.emplace_back(Report {
                report_text.value_or(input.output_text),
                assertion_severity(input.assertion_severity),
                SourceLocation {
                    span.source_name.str(),
                    static_cast<std::uint32_t>(span.begin.line),
                    static_cast<std::uint32_t>(span.begin.column),
                },
            });
            return true;
        }
        case semantic::sv::StatementKind::file_close:
        case semantic::sv::StatementKind::file_flush: {
            if (!input.file_handle) {
                if (input.kind
                    != semantic::sv::StatementKind::file_flush) {
                    return false;
                }
                process_.operations.emplace_back(FileFlush { 0U, true });
                return true;
            }
            const auto source_width = hir_expression_width(
                *input.file_handle, hir_process_scope_)
                                          .value_or(32U);
            auto handle = lower_hir_expression(
                *input.file_handle, source_width);
            if (!handle) {
                return false;
            }
            if (register_width(*handle) != 32U) {
                *handle = resize_register(
                    *handle, 32U,
                    hir_expression_signed(*input.file_handle));
            }
            if (input.kind == semantic::sv::StatementKind::file_close) {
                process_.operations.emplace_back(FileClose { *handle });
            } else {
                process_.operations.emplace_back(
                    FileFlush { *handle, false });
            }
            return true;
        }
        case semantic::sv::StatementKind::file_display:
        case semantic::sv::StatementKind::memory_transfer:
            return lower_hir_systemverilog_file_statement(statement_id);
        case semantic::sv::StatementKind::monitor_control:
            process_.operations.emplace_back(
                MonitorControl { input.monitor_enabled });
            return true;
        case semantic::sv::StatementKind::assertion: {
            if (!input.condition) {
                return false;
            }
            const bool deferred = input.delay.has_value()
                || input.output_postponed;
            if (input.delay
                && (!input.delay->additional.empty()
                    || input.delay->minimum
                    || input.delay->typical
                    || input.delay->maximum
                    || delay_magnitude(input.delay->primary)
                        != std::optional<std::uint64_t> { 0U })) {
                return false;
            }
            if (deferred && input.assertion_message.starts_with("concurrent assertion '")) {
                report(
                    "FSIM-ELAB-SVASSERT-001",
                    "a concurrent assertion cannot carry an immediate-"
                    "assertion deferred qualifier",
                    span);
                return true;
            }
            const auto action_requires_handoff
                = [&](const auto& actions) {
                      if (actions.empty()) {
                          return false;
                      }
                      const auto first = specialized_hir_unit_
                                             ->find_statement(actions.front());
                      return !first || first->systemverilog == nullptr
                          || first->systemverilog->kind
                          != semantic::sv::StatementKind::null_statement;
                  };
            const bool pass_requires_handoff = deferred
                && action_requires_handoff(input.statements);
            const bool failure_requires_handoff = deferred
                && (!input.assertion_has_failure_action
                    || action_requires_handoff(input.else_statements));
            std::optional<InstructionIndex> pass_disable;
            std::optional<InstructionIndex> failure_disable;
            if (pass_requires_handoff) {
                pass_disable = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(DisableFork { });
            }
            if (failure_requires_handoff) {
                failure_disable = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(DisableFork { });
            }
            const auto checked = lower_condition(*input.condition);
            if (!checked) {
                return false;
            }
            const bool sampled_reads
                = sample_concurrent_assertion_reads_;
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *checked,
                0U,
                0U,
                UnknownBranchPolicy::when_false,
            });
            const auto pass = static_cast<InstructionIndex>(
                process_.operations.size());
            if (deferred) {
                deferred_assertion_action_phase_
                    = input.output_postponed
                    ? runtime::SchedulerPhase::postponed
                    : runtime::SchedulerPhase::reactive;
            }
            if (!lower_hir_statements(input.statements)) {
                return false;
            }
            sample_concurrent_assertion_reads_ = sampled_reads;
            const auto pass_handoff
                = finish_deferred_assertion_action_handoff();
            const auto skip_failure = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0U });
            const auto failure = static_cast<InstructionIndex>(
                process_.operations.size());
            std::optional<InstructionIndex> failure_handoff;
            if (input.assertion_has_failure_action) {
                if (deferred) {
                    deferred_assertion_action_phase_
                        = input.output_postponed
                        ? runtime::SchedulerPhase::postponed
                        : runtime::SchedulerPhase::reactive;
                }
                if (!lower_hir_statements(input.else_statements)) {
                    return false;
                }
                failure_handoff
                    = finish_deferred_assertion_action_handoff();
            } else {
                if (deferred) {
                    deferred_assertion_action_phase_
                        = input.output_postponed
                        ? runtime::SchedulerPhase::postponed
                        : runtime::SchedulerPhase::reactive;
                    emit_deferred_assertion_action_handoff();
                }
                process_.operations.emplace_back(Report {
                    input.assertion_message.empty()
                        ? "assertion failed"
                        : input.assertion_message,
                    assertion_severity(input.assertion_severity),
                    SourceLocation {
                        span.source_name.str(),
                        static_cast<std::uint32_t>(span.begin.line),
                        static_cast<std::uint32_t>(span.begin.column),
                    },
                });
                failure_handoff
                    = finish_deferred_assertion_action_handoff();
            }
            sample_concurrent_assertion_reads_ = sampled_reads;
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                *checked,
                pass,
                failure,
                UnknownBranchPolicy::when_false,
            };
            process_.operations[skip_failure] = Jump { end };
            if (pass_disable && pass_handoff) {
                process_.operations[*pass_disable]
                    = DisableFork { *pass_handoff };
            }
            if (failure_disable && failure_handoff) {
                process_.operations[*failure_disable]
                    = DisableFork { *failure_handoff };
            }
            if ((pass_requires_handoff && !pass_handoff)
                || (failure_requires_handoff && !failure_handoff)) {
                report(
                    "FSIM-ELAB-SVASSERT-002",
                    "a deferred assertion action could not be lowered to a "
                    "scheduler handoff",
                    span);
            }
            return true;
        }
        case semantic::sv::StatementKind::delay_control: {
            if (!input.delay || !input.delay->additional.empty()) {
                return false;
            }
            emit_debug_point(DebugPointKind::wait, span);
            const auto& delay = input.delay->primary;
            if (!delay.expression) {
                process_.operations.emplace_back(WaitFor {
                    delay.magnitude });
                return lower_hir_statements(input.statements);
            }

            const auto constant
                = specialized_hir_unit_->evaluate_integral_expression(
                    *delay.expression);
            if (constant) {
                if (*constant < 0) {
                    report(
                        "FSIM-ELAB-SVDELAY-001",
                        "SystemVerilog delay expression must be a known "
                        "nonnegative locally constant integral value",
                        hir_source_span(delay.source));
                    return true;
                }
                const auto multiplier = static_cast<std::uint64_t>(*constant);
                if (multiplier != 0U
                    && delay.magnitude
                        > std::numeric_limits<runtime::SimulationTick>::max()
                            / multiplier) {
                    report(
                        "FSIM-ELAB-SVDELAY-002",
                        "SystemVerilog delay expression overflows the 64-bit "
                        "simulation time range after time-unit normalization",
                        hir_source_span(delay.source));
                    return true;
                }
                process_.operations.emplace_back(WaitFor {
                    delay.magnitude * multiplier });
                return lower_hir_statements(input.statements);
            }

            const auto scalar_kind = hir_systemverilog_scalar_kind(
                *delay.expression);
            if (scalar_kind
                == frontend::SystemVerilogScalarKind::Chandle) {
                report(
                    "FSIM-ELAB-SVDELAY-004",
                    "a runtime delay requires an integral, time, real, "
                    "shortreal, or realtime expression",
                    hir_source_span(delay.source));
                return true;
            }
            const auto width = scalar_kind
                    == frontend::SystemVerilogScalarKind::ShortReal
                ? std::size_t { 32U }
                : scalar_kind == frontend::SystemVerilogScalarKind::Real
                    || scalar_kind
                        == frontend::SystemVerilogScalarKind::Realtime
                    || scalar_kind
                        == frontend::SystemVerilogScalarKind::Time
                ? std::size_t { 64U }
                : hir_expression_width(
                      *delay.expression, hir_process_scope_)
                      .value_or(32U);
            const auto source_register = lower_hir_expression(
                *delay.expression, width, scalar_kind);
            if (!source_register) {
                report(
                    "FSIM-ELAB-SVDELAY-004",
                    "runtime delay expression cannot be lowered to a packed "
                    "scalar",
                    hir_source_span(delay.source));
                return true;
            }
            WaitFor wait;
            wait.delay = delay.magnitude;
            wait.source = *source_register;
            wait.source_width = static_cast<std::uint32_t>(
                register_width(*source_register));
            wait.source_kind = scalar_kind;
            wait.source_signed = hir_expression_signed(*delay.expression);
            process_.operations.emplace_back(wait);
            return lower_hir_statements(input.statements);
        }
        case semantic::sv::StatementKind::event_control: {
            const auto clocking_event = input.clocking_cycle_delay
                ? hir_default_clocking_event()
                : std::nullopt;
            if (input.clocking_cycle_delay && !clocking_event) {
                report(
                    "FSIM-ELAB-CLOCK-005",
                    "a procedural ## cycle delay requires a default "
                    "clocking block",
                    span);
                return true;
            }
            const auto lowered = input.clocking_cycle_delay
                ? input.clocking_cycle_count && clocking_event
                    && lower_repeated_event_wait(
                        *clocking_event,
                        std::nullopt,
                        *input.clocking_cycle_count,
                        input.statements)
                : input.assignment_control_repeated
                ? input.loop_limit
                    && lower_repeated_event_wait(
                        input.sensitivities,
                        std::nullopt,
                        *input.loop_limit,
                        input.statements)
                : lower_event_wait(
                      input.sensitivities,
                      std::nullopt,
                      input.statements);
            if (!lowered) {
                return false;
            }
            if (input.clocking_cycle_delay
                && !systemverilog_program_owner_) {
                // Clocking inputs are sampled in the observed region. A
                // module process resumed by ## must cross into the reactive
                // region before consuming the sampled value or driving a
                // clocking output. Program processes already execute there.
                process_.operations.emplace_back(
                    WaitRegion { runtime::SchedulerPhase::reactive });
            }
            return lower_hir_statements(input.statements);
        }
        case semantic::sv::StatementKind::wait_statement: {
            if (!input.condition) {
                return false;
            }
            emit_debug_point(DebugPointKind::wait, span);
            const auto condition_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto checked = lower_condition(*input.condition);
            if (!checked) {
                return false;
            }
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *checked,
                0U,
                0U,
                UnknownBranchPolicy::when_false,
            });
            const auto wait = static_cast<InstructionIndex>(
                process_.operations.size());
            auto dependencies = hir_signal_dependencies(
                *input.condition, hir_process_scope_);
            if (dependencies.empty()) {
                process_.operations.emplace_back(WaitForever { });
            } else {
                const auto dependency_count = dependencies.size();
                process_.operations.emplace_back(WaitOn {
                    std::move(dependencies),
                    std::vector<runtime::simir::EdgeKind>(
                        dependency_count,
                        runtime::simir::EdgeKind::any),
                });
                process_.operations.emplace_back(Jump { condition_start });
            }
            const auto satisfied = static_cast<InstructionIndex>(
                process_.operations.size());
            if (!lower_hir_statements(input.statements)) {
                return false;
            }
            process_.operations[branch] = Branch {
                *checked,
                satisfied,
                wait,
                UnknownBranchPolicy::when_false,
            };
            return true;
        }
        case semantic::sv::StatementKind::event_trigger: {
            if (!input.target) {
                return false;
            }
            const auto declaration = hir_target_declaration(*input.target);
            const auto binding = declaration
                ? hir_runtime_binding(
                      *declaration, hir_process_scope_, true)
                : std::nullopt;
            if (!binding || !binding->signal) {
                report(
                    "FSIM-ELAB-100",
                    "unknown named event trigger target",
                    span);
                return true;
            }
            const auto event = hir_systemverilog_event_signal(
                *input.target, hir_process_scope_);
            if (!event) {
                report(
                    "FSIM-ELAB-101",
                    "event trigger target is not declared as an event",
                    span);
                return true;
            }
            const auto current = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            const auto toggled = allocate_register(
                1U, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(ReadSignal {
                current, *event });
            process_.operations.emplace_back(UnaryNot {
                toggled, current });
            if (input.assignment_kind
                == semantic::sv::AssignmentKind::nonblocking) {
                if (input.delay) {
                    const auto delay = delay_magnitude(
                        input.delay->primary);
                    if (!delay || !input.delay->additional.empty()) {
                        return false;
                    }
                    process_.operations.emplace_back(WriteAfter {
                        *event, toggled, *delay });
                } else {
                    process_.operations.emplace_back(WriteUpdate {
                        *event, toggled });
                }
            } else {
                process_.operations.emplace_back(WriteBlocking {
                    *event, toggled });
            }
            return true;
        }
        case semantic::sv::StatementKind::wait_order: {
            if (input.sensitivities.empty()) {
                report(
                    "FSIM-ELAB-SVEVENT-005",
                    "wait_order requires at least one named event",
                    span);
                return true;
            }
            std::vector<SignalId> events;
            events.reserve(input.sensitivities.size());
            for (const auto& sensitivity : input.sensitivities) {
                if (sensitivity.signal.empty()
                    || sensitivity.expression) {
                    report(
                        "FSIM-ELAB-SVEVENT-005",
                        "wait_order operands must be named-event identifiers",
                        hir_source_span(sensitivity.source));
                    return true;
                }
                const auto found = signals_.find(sensitivity.signal);
                if (found == signals_.end()) {
                    report(
                        "FSIM-ELAB-SVEVENT-006",
                        "unknown wait_order event '" + sensitivity.signal
                            + "'",
                        hir_source_span(sensitivity.source));
                    return true;
                }
                if (design_.signal_info_.at(found->second).type_name
                    != "event") {
                    report(
                        "FSIM-ELAB-SVEVENT-007",
                        "wait_order operand '" + sensitivity.signal
                            + "' is not declared as an event",
                        hir_source_span(sensitivity.source));
                    return true;
                }
                events.push_back(found->second);
            }
            const auto result = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(WaitOrder {
                std::move(events), result });
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                result, 0U, 0U, UnknownBranchPolicy::error });
            const auto success = static_cast<InstructionIndex>(
                process_.operations.size());
            if (!lower_hir_statements(input.statements)) {
                return false;
            }
            const auto skip_failure = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0U });
            const auto failure = static_cast<InstructionIndex>(
                process_.operations.size());
            if (!lower_hir_statements(input.else_statements)) {
                return false;
            }
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                result, success, failure, UnknownBranchPolicy::error
            };
            process_.operations[skip_failure] = Jump { end };
            return true;
        }
        case semantic::sv::StatementKind::container_method: {
            const auto call = input.value
                ? specialized_hir_unit_->find_expression(*input.value)
                : std::nullopt;
            if (!call || call->systemverilog == nullptr
                || call->systemverilog->kind
                    != semantic::sv::ExpressionKind::call) {
                return false;
            }
            const auto synchronization
                = lower_hir_synchronization_expression(*input.value, 0U);
            if (synchronization.handled) {
                return synchronization.succeeded;
            }
            const auto& container_call = *call->systemverilog;
            const bool string_mutation
                = container_call.text == ".putc"
                || container_call.text == ".itoa"
                || container_call.text == ".hextoa"
                || container_call.text == ".octtoa"
                || container_call.text == ".bintoa"
                || container_call.text == ".realtoa";
            if (string_mutation) {
                const auto expected = container_call.text == ".putc"
                    ? 3U
                    : 2U;
                const auto receiver = !container_call.operands.empty()
                    ? specialized_hir_unit_->find_expression(
                          container_call.operands.front())
                    : std::nullopt;
                const auto declaration = !container_call.operands.empty()
                    ? hir_target_declaration(
                          container_call.operands.front())
                    : std::nullopt;
                const auto binding = declaration
                    ? hir_string_binding(
                          *declaration, hir_process_scope_, true)
                    : std::nullopt;
                if (!receiver || receiver->systemverilog == nullptr
                    || receiver->systemverilog->kind
                        != semantic::sv::ExpressionKind::name
                    || !binding) {
                    report(
                        "FSIM-ELAB-SVSTRING-018",
                        "mutating string methods require a direct writable "
                        "string object",
                        hir_source_span(container_call.source));
                    return true;
                }
                if (binding->object
                    && read_only_string_objects_.contains(
                        *binding->object)) {
                    report(
                        "FSIM-ELAB-SVPORT-011",
                        "an input mutable string port is read-only",
                        hir_source_span(receiver->systemverilog->source));
                    return true;
                }
                if (container_call.operands.size() != expected) {
                    report(
                        "FSIM-ELAB-SVSTRING-018",
                        "runtime string method '" + container_call.text
                            + "' has an incompatible argument count",
                        hir_source_span(container_call.source));
                    return true;
                }
                const auto string_target = lower_hir_string_expression(
                    container_call.operands.front());
                const auto first_width
                    = container_call.text == ".realtoa" ? 64U : 32U;
                auto first = lower_hir_expression(
                    container_call.operands[1], first_width);
                if (!string_target || !first) {
                    report(
                        "FSIM-ELAB-SVSTRING-018",
                        container_call.text == ".realtoa"
                            ? "realtoa argument must be a 64-bit real value"
                            : "mutating string method argument must be a "
                              "32-bit integral value",
                        hir_source_span(container_call.source));
                    return true;
                }
                if (register_width(*first) != first_width) {
                    first = resize_register(
                        *first,
                        first_width,
                        hir_expression_signed(container_call.operands[1]));
                }
                StringMethod method;
                method.source = *string_target;
                method.first = *first;
                if (container_call.text == ".putc") {
                    auto character = lower_hir_expression(
                        container_call.operands[2], 32U);
                    if (!character) {
                        report(
                            "FSIM-ELAB-SVSTRING-018",
                            "putc character must be a 32-bit integral byte "
                            "value",
                            hir_source_span(container_call.source));
                        return true;
                    }
                    if (register_width(*character) != 32U) {
                        character = resize_register(
                            *character,
                            32U,
                            hir_expression_signed(
                                container_call.operands[2]));
                    }
                    method.operation = StringMethodOperator::putc;
                    method.second = *character;
                } else {
                    method.operation = container_call.text == ".itoa"
                        ? StringMethodOperator::itoa
                        : container_call.text == ".hextoa"
                        ? StringMethodOperator::hextoa
                        : container_call.text == ".octtoa"
                        ? StringMethodOperator::octtoa
                        : container_call.text == ".bintoa"
                        ? StringMethodOperator::bintoa
                        : StringMethodOperator::realtoa;
                }
                process_.operations.emplace_back(method);
                if (binding->object) {
                    process_.operations.emplace_back(WriteStringObject {
                        *binding->object, *string_target });
                }
                return true;
            }
            const bool ordering_method = container_call.text == ".reverse"
                || container_call.text == ".sort"
                || container_call.text == ".rsort"
                || container_call.text == ".shuffle";
            const bool direct_mutation
                = container_call.text == ".delete"
                || container_call.text == ".insert"
                || container_call.text == ".push_front"
                || container_call.text == ".push_back"
                || container_call.text == ".pop_front"
                || container_call.text == ".pop_back";
            const bool reduction_method = container_call.text == ".sum"
                || container_call.text == ".product"
                || container_call.text == ".and"
                || container_call.text == ".or"
                || container_call.text == ".xor";
            const bool locator_method = container_call.text == ".min"
                || container_call.text == ".max"
                || container_call.text == ".unique"
                || container_call.text == ".unique_index"
                || container_call.text == ".find"
                || container_call.text == ".find_index"
                || container_call.text == ".find_first"
                || container_call.text == ".find_first_index"
                || container_call.text == ".find_last"
                || container_call.text == ".find_last_index";
            if (reduction_method) {
                report(
                    "FSIM-ELAB-SVREDUCE-003",
                    "a container reduction result must be used in an expression",
                    hir_source_span(container_call.source));
                return true;
            }
            if (locator_method) {
                report(
                    container_call.text.starts_with(".find")
                        ? "FSIM-ELAB-SVFIND-005"
                        : "FSIM-ELAB-SVLOCATOR-005",
                    "container locator results cannot be discarded",
                    hir_source_span(container_call.source));
                return true;
            }
            if (container_call.text == ".exists"
                || container_call.text == ".first"
                || container_call.text == ".last"
                || container_call.text == ".next"
                || container_call.text == ".prev") {
                const auto diagnostics_before = diagnostics_.size();
                const auto lowered
                    = lower_hir_systemverilog_container_expression(
                        *input.value, 32U);
                return lowered || diagnostics_.size() != diagnostics_before;
            }
            const auto container_binding = !container_call.operands.empty()
                ? hir_container_object_binding(
                      container_call.operands.front())
                : std::nullopt;
            if (direct_mutation && container_binding
                && container_binding->type != nullptr) {
                if (container_binding->read_only) {
                    report(
                        "FSIM-ELAB-SVPORT-009",
                        "an input container port is read-only within its module",
                        hir_source_span(container_call.source));
                    return true;
                }
                auto target_register = container_binding->local
                    ? *container_binding->local
                    : allocate_container_register(*container_binding->type);
                if (!container_binding->local) {
                    process_.operations.emplace_back(ReadContainerObject {
                        target_register, container_binding->object });
                }
                const auto publish = [&] {
                    if (!container_binding->local) {
                        process_.operations.emplace_back(
                            WriteContainerObject {
                                container_binding->object,
                                target_register,
                                std::nullopt,
                            });
                    }
                };
                if (container_call.text == ".pop_front"
                    || container_call.text == ".pop_back") {
                    if (!container_binding->type->queue
                        || container_call.operands.size() != 1U) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-019",
                            "pop_front/pop_back require a queue receiver",
                            hir_source_span(container_call.source));
                        return true;
                    }
                    const auto discarded = allocate_register(
                        container_binding->type->element_width,
                        container_binding->type->two_state
                            ? frontend::ValueDomain::Bit2
                            : frontend::ValueDomain::Logic4);
                    process_.operations.emplace_back(PopContainer {
                        discarded,
                        target_register,
                        container_call.text == ".pop_front",
                    });
                    publish();
                    return true;
                }
                if (container_call.text == ".delete") {
                    if (container_call.operands.size() > 2U) {
                        return false;
                    }
                    if (container_binding->type->fixed) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-021",
                            "delete cannot mutate a static unpacked array",
                            hir_source_span(container_call.source));
                        return true;
                    }
                    if (container_call.operands.size() == 2U
                        && !container_binding->type->associative
                        && !container_binding->type->queue) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-018",
                            "delete(index) requires a queue or "
                            "associative-array receiver",
                            hir_source_span(container_call.source));
                        return true;
                    }
                    std::optional<RegisterId> index;
                    if (container_call.operands.size() == 2U) {
                        const auto string_index
                            = container_binding->type->associative
                            && container_binding->type->string_indices;
                        const auto index_width = string_index
                            ? std::size_t { }
                            : container_binding->type->associative
                            ? static_cast<std::size_t>(
                                  container_binding->type->index_width)
                            : 32U;
                        index = string_index
                            ? lower_hir_string_expression(
                                  container_call.operands[1])
                            : lower_hir_expression(
                                  container_call.operands[1], index_width);
                        if (!index) {
                            return false;
                        }
                        if (!string_index
                            && register_width(*index) != index_width) {
                            index = resize_register(
                                *index,
                                index_width,
                                container_binding->type->associative
                                    && container_binding->type
                                        ->signed_indices);
                        }
                    }
                    process_.operations.emplace_back(DeleteContainer {
                        target_register,
                        index,
                        container_binding->type->associative
                            && container_binding->type->string_indices,
                    });
                    publish();
                    return true;
                }
                if (!container_binding->type->queue
                    || (container_call.text == ".insert"
                            ? container_call.operands.size() != 3U
                            : container_call.operands.size() != 2U)) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-019",
                        "insert and push operations require a queue receiver "
                        "and their required arguments",
                        hir_source_span(container_call.source));
                    return true;
                }
                const auto value_id = container_call.operands.back();
                const auto value_expression
                    = specialized_hir_unit_->find_expression(value_id);
                const auto element_type = hir_container_leaf_type(
                    *specialized_hir_unit_,
                    container_binding->declaration,
                    hir_generic_binding_frames_);
                const auto packed_pattern = element_type
                    && value_expression
                    && value_expression->systemverilog != nullptr
                    && (value_expression->systemverilog->kind
                            == semantic::sv::ExpressionKind::assignment_pattern
                        || (value_expression->systemverilog->kind
                                == semantic::sv::ExpressionKind::call
                            && value_expression->systemverilog->text.starts_with(
                                "@sv-tagged:")));
                auto pushed_value = packed_pattern
                    ? lower_hir_systemverilog_packed_pattern(
                          value_id,
                          *element_type,
                          container_binding->type->element_width)
                    : lower_hir_expression(
                          value_id,
                          container_binding->type->element_width);
                if (!pushed_value) {
                    return false;
                }
                if (register_width(*pushed_value)
                    != container_binding->type->element_width) {
                    pushed_value = resize_register(
                        *pushed_value,
                        container_binding->type->element_width,
                        hir_expression_signed(value_id));
                }
                std::optional<RegisterId> insertion_index;
                if (container_call.text == ".insert") {
                    insertion_index = lower_hir_expression(
                        container_call.operands[1], 32U);
                    if (!insertion_index) {
                        return false;
                    }
                    if (register_width(*insertion_index) != 32U) {
                        insertion_index = resize_register(
                            *insertion_index,
                            32U,
                            hir_expression_signed(
                                container_call.operands[1]));
                    }
                }
                process_.operations.emplace_back(PushContainer {
                    target_register,
                    *pushed_value,
                    container_call.text == ".push_front",
                    insertion_index,
                });
                publish();
                return true;
            }
            if (direct_mutation) {
                report(
                    "FSIM-ELAB-SVCONTAINER-007",
                    "container method requires a direct object receiver",
                    hir_source_span(container_call.source));
                return true;
            }
            if (ordering_method) {
                const bool keyed = container_call.text == ".sort"
                    || container_call.text == ".rsort";
                const bool valid_arity = keyed
                    ? container_call.operands.size() >= 1U
                        && container_call.operands.size() <= 3U
                    : container_call.operands.size() == 1U;
                if (!valid_arity) {
                    report(
                        "FSIM-ELAB-SVORDER-002",
                        keyed
                            ? "sort and rsort take no value arguments and "
                              "retain at most one iterator plus one "
                              "with-clause key"
                            : "reverse and shuffle take no arguments",
                        hir_source_span(container_call.source));
                    return true;
                }
                const bool explicit_iterator
                    = container_call.operands.size() == 3U;
                if (explicit_iterator) {
                    const auto iterator = specialized_hir_unit_->find_expression(
                        container_call.operands[1]);
                    if (!iterator || iterator->systemverilog == nullptr
                        || iterator->systemverilog->kind
                            != semantic::sv::ExpressionKind::name) {
                        report(
                            "FSIM-ELAB-SVORDER-008",
                            "a named container ordering iterator must be one "
                            "identifier",
                            iterator && iterator->systemverilog != nullptr
                                ? hir_source_span(
                                      iterator->systemverilog->source)
                                : hir_source_span(container_call.source));
                        return true;
                    }
                    if (iterator->systemverilog->referenced_name
                        && iterator->systemverilog->referenced_name->selected) {
                        report(
                            "FSIM-ELAB-SVORDER-008",
                            "named container ordering iterator '"
                                + iterator->systemverilog->text
                                + "' collides with a visible object",
                            hir_source_span(iterator->systemverilog->source));
                        return true;
                    }
                }
                const auto direct_receiver = container_binding
                    && container_binding->type != nullptr;
                auto selection = direct_receiver
                    ? std::optional { HirStaticContainerSelection {
                          *container_binding,
                          *container_binding->type,
                          { },
                          std::nullopt } }
                    : hir_static_container_selection(
                          container_call.operands.front());
                if (!selection) {
                    report(
                        "FSIM-ELAB-SVORDER-001",
                        "container ordering requires a direct writable "
                        "SystemVerilog unpacked-container receiver",
                        hir_source_span(container_call.source));
                    return true;
                }
                if (selection->base.read_only) {
                    report(
                        "FSIM-ELAB-SVPORT-009",
                        "an input container port is read-only within its module",
                        hir_source_span(container_call.source));
                    return true;
                }
                if (selection->selected_type.associative) {
                    report(
                        "FSIM-ELAB-SVORDER-003",
                        "container ordering does not support associative arrays",
                        hir_source_span(container_call.source));
                    return true;
                }
                if (selection->selected_type.element_kind
                    != ContainerElementKind::Packed) {
                    report(
                        "FSIM-ELAB-SVORDER-008",
                        "container ordering requires packed integral elements",
                        hir_source_span(container_call.source));
                    return true;
                }

                std::vector<ContainerPredicateNode> key;
                if (container_call.operands.size() >= 2U) {
                    using ValueKind = ContainerPredicateValueKind;
                    const auto iterator = explicit_iterator
                        ? specialized_hir_unit_->find_expression(
                              container_call.operands[1])
                        : std::nullopt;
                    const auto iterator_name = iterator
                            && iterator->systemverilog != nullptr
                        ? iterator->systemverilog->text
                        : std::string { "item" };
                    const auto key_expression_id
                        = container_call.operands[explicit_iterator ? 2U : 1U];
                    const auto key_root_expression
                        = specialized_hir_unit_->find_expression(
                            key_expression_id);
                    if (!key_root_expression
                        || key_root_expression->systemverilog == nullptr) {
                        return false;
                    }
                    const auto key_span = hir_source_span(
                        key_root_expression->systemverilog->source);
                    const auto append_node = [&](ContainerPredicateNode node)
                        -> std::optional<std::uint32_t> {
                        if (key.size()
                            >= maximum_container_predicate_nodes) {
                            report(
                                "FSIM-ELAB-SVORDER-006",
                                "container ordering key expression is limited "
                                "to 64 nodes",
                                key_span);
                            return std::nullopt;
                        }
                        key.push_back(std::move(node));
                        return static_cast<std::uint32_t>(key.size() - 1U);
                    };
                    const auto direct_kind = [&](
                                                 const semantic::sv::Expression& expression)
                        -> std::optional<ValueKind> {
                        if (expression.kind
                            != semantic::sv::ExpressionKind::name) {
                            return std::nullopt;
                        }
                        if (expression.text == iterator_name) {
                            return ValueKind::element;
                        }
                        if (expression.text
                            == iterator_name + ".index") {
                            return ValueKind::index;
                        }
                        return std::nullopt;
                    };
                    const auto contains_iterator = [&](const semantic::ExpressionId expression_id) {
                        std::vector<semantic::ExpressionId> pending {
                            expression_id
                        };
                        std::unordered_set<std::uint32_t> visited;
                        while (!pending.empty()) {
                            const auto current = pending.back();
                            pending.pop_back();
                            if (!visited.insert(current.value()).second) {
                                continue;
                            }
                            const auto expression
                                = specialized_hir_unit_->find_expression(
                                    current);
                            if (!expression
                                || expression->systemverilog == nullptr) {
                                continue;
                            }
                            const auto& candidate_expression
                                = *expression->systemverilog;
                            if (candidate_expression.kind
                                    == semantic::sv::ExpressionKind::name
                                && (candidate_expression.text == iterator_name
                                    || candidate_expression.text.starts_with(
                                        iterator_name + "."))) {
                                return true;
                            }
                            pending.insert(
                                pending.end(),
                                candidate_expression.operands.begin(),
                                candidate_expression.operands.end());
                        }
                        return false;
                    };
                    const auto report_reference = [&](const semantic::sv::Expression& expression,
                                                      const std::string_view reason) {
                        report(
                            "FSIM-ELAB-SVORDER-007",
                            "container iterator '" + iterator_name
                                + "' " + std::string { reason },
                            hir_source_span(expression.source));
                    };
                    const auto lower_constant = [&](const semantic::ExpressionId expression_id,
                                                    const ValueKind kind)
                        -> std::optional<std::uint32_t> {
                        const auto expression
                            = specialized_hir_unit_->find_expression(
                                expression_id);
                        const auto constant_value
                            = hir_constant_integer(expression_id);
                        if (!expression
                            || expression->systemverilog == nullptr
                            || !constant_value) {
                            report(
                                "FSIM-ELAB-SVORDER-006",
                                "container ordering key constants must be "
                                "locally constant and convertible to the "
                                "selected element type",
                                expression
                                        && expression->systemverilog != nullptr
                                    ? hir_source_span(
                                          expression->systemverilog->source)
                                    : key_span);
                            return std::nullopt;
                        }
                        ContainerPredicateNode node;
                        node.operation = ContainerPredicateOperator::constant;
                        node.value_kind = kind;
                        node.constant = integer_value(
                            *constant_value,
                            kind == ValueKind::index
                                ? 32U
                                : selection->selected_type.element_width);
                        return append_node(std::move(node));
                    };
                    const auto lower_value = [&](const semantic::ExpressionId expression_id,
                                                 const ValueKind expected)
                        -> std::optional<std::uint32_t> {
                        const auto expression
                            = specialized_hir_unit_->find_expression(
                                expression_id);
                        if (!expression
                            || expression->systemverilog == nullptr) {
                            return std::nullopt;
                        }
                        const auto& value_expression
                            = *expression->systemverilog;
                        if (const auto kind
                            = direct_kind(value_expression)) {
                            if (*kind != expected) {
                                report_reference(
                                    value_expression,
                                    "cannot mix element and index comparison operands");
                                return std::nullopt;
                            }
                            ContainerPredicateNode node;
                            node.operation = *kind == ValueKind::index
                                ? ContainerPredicateOperator::index
                                : ContainerPredicateOperator::item;
                            node.value_kind = *kind;
                            return append_node(std::move(node));
                        }
                        if (contains_iterator(expression_id)) {
                            report_reference(
                                value_expression,
                                "supports only its direct value or direct .index leaf");
                            return std::nullopt;
                        }
                        return lower_constant(expression_id, expected);
                    };
                    std::function<std::optional<std::uint32_t>(
                        semantic::ExpressionId)>
                        lower_key;
                    lower_key = [&](const semantic::ExpressionId expression_id)
                        -> std::optional<std::uint32_t> {
                        const auto expression
                            = specialized_hir_unit_->find_expression(
                                expression_id);
                        if (!expression
                            || expression->systemverilog == nullptr) {
                            return std::nullopt;
                        }
                        const auto& key_expression
                            = *expression->systemverilog;
                        if (const auto kind = direct_kind(key_expression)) {
                            ContainerPredicateNode node;
                            node.operation = *kind == ValueKind::index
                                ? ContainerPredicateOperator::index
                                : ContainerPredicateOperator::item;
                            node.value_kind = *kind;
                            return append_node(std::move(node));
                        }
                        if (!contains_iterator(expression_id)) {
                            return lower_constant(
                                expression_id, ValueKind::element);
                        }
                        if (key_expression.kind
                                == semantic::sv::ExpressionKind::unary
                            && key_expression.text == "!"
                            && key_expression.operands.size() == 1U) {
                            const auto operand = lower_key(
                                key_expression.operands.front());
                            if (!operand) {
                                return std::nullopt;
                            }
                            ContainerPredicateNode node;
                            node.operation
                                = ContainerPredicateOperator::logical_not;
                            node.left = *operand;
                            node.value_kind = ValueKind::logical;
                            return append_node(std::move(node));
                        }
                        if (key_expression.kind
                                == semantic::sv::ExpressionKind::call
                            && key_expression.text == "?:"
                            && key_expression.operands.size() == 3U) {
                            const auto condition_node = lower_key(
                                key_expression.operands[0]);
                            const auto when_true = lower_value(
                                key_expression.operands[1],
                                ValueKind::element);
                            const auto when_false = lower_value(
                                key_expression.operands[2],
                                ValueKind::element);
                            if (!condition_node || !when_true || !when_false) {
                                return std::nullopt;
                            }
                            ContainerPredicateNode node;
                            node.operation
                                = ContainerPredicateOperator::conditional;
                            node.left = *condition_node;
                            node.right = *when_true;
                            node.third = *when_false;
                            node.value_kind = ValueKind::element;
                            return append_node(std::move(node));
                        }
                        if (key_expression.kind
                                == semantic::sv::ExpressionKind::binary
                            && key_expression.operands.size() == 2U) {
                            const bool logical = key_expression.text == "&&"
                                || key_expression.text == "||";
                            const bool comparison
                                = key_expression.text == "=="
                                || key_expression.text == "!="
                                || key_expression.text == "<"
                                || key_expression.text == "<="
                                || key_expression.text == ">"
                                || key_expression.text == ">=";
                            if (logical || comparison) {
                                std::optional<std::uint32_t> left;
                                std::optional<std::uint32_t> right;
                                if (comparison) {
                                    const auto left_expression
                                        = specialized_hir_unit_->find_expression(
                                            key_expression.operands[0]);
                                    const auto right_expression
                                        = specialized_hir_unit_->find_expression(
                                            key_expression.operands[1]);
                                    const auto left_kind = left_expression
                                            && left_expression->systemverilog
                                        ? direct_kind(
                                              *left_expression->systemverilog)
                                        : std::nullopt;
                                    const auto right_kind = right_expression
                                            && right_expression->systemverilog
                                        ? direct_kind(
                                              *right_expression->systemverilog)
                                        : std::nullopt;
                                    if (left_kind && right_kind
                                        && *left_kind != *right_kind) {
                                        report_reference(
                                            key_expression,
                                            "cannot mix element and index "
                                            "comparison operands");
                                        return std::nullopt;
                                    }
                                    const auto kind = left_kind.value_or(
                                        right_kind.value_or(
                                            ValueKind::element));
                                    left = lower_value(
                                        key_expression.operands[0], kind);
                                    right = lower_value(
                                        key_expression.operands[1], kind);
                                } else {
                                    left = lower_key(
                                        key_expression.operands[0]);
                                    right = lower_key(
                                        key_expression.operands[1]);
                                }
                                if (!left || !right) {
                                    return std::nullopt;
                                }
                                ContainerPredicateNode node;
                                node.left = *left;
                                node.right = *right;
                                node.value_kind = ValueKind::logical;
                                if (key_expression.text == "==") {
                                    node.operation
                                        = ContainerPredicateOperator::equal;
                                } else if (key_expression.text == "!=") {
                                    node.operation
                                        = ContainerPredicateOperator::not_equal;
                                } else if (key_expression.text == "<") {
                                    node.operation
                                        = ContainerPredicateOperator::less;
                                } else if (key_expression.text == "<=") {
                                    node.operation
                                        = ContainerPredicateOperator::less_equal;
                                } else if (key_expression.text == ">") {
                                    node.operation
                                        = ContainerPredicateOperator::greater;
                                } else if (key_expression.text == ">=") {
                                    node.operation
                                        = ContainerPredicateOperator::greater_equal;
                                } else if (key_expression.text == "&&") {
                                    node.operation
                                        = ContainerPredicateOperator::logical_and;
                                } else {
                                    node.operation
                                        = ContainerPredicateOperator::logical_or;
                                }
                                return append_node(std::move(node));
                            }
                        }
                        report(
                            "FSIM-ELAB-SVORDER-006",
                            "container ordering key expression supports the "
                            "scoped iterator, its direct .index leaf, locally "
                            "constant operands, comparisons, logical "
                            "operators, and one conditional key selection",
                            hir_source_span(key_expression.source));
                        return std::nullopt;
                    };
                    const auto root = lower_key(key_expression_id);
                    if (!root) {
                        return true;
                    }
                    if (key[*root].value_kind != ValueKind::element) {
                        report(
                            "FSIM-ELAB-SVORDER-008",
                            "container ordering key root must have the receiver "
                            "element type",
                            key_span);
                        return true;
                    }
                }

                const auto base = selection->base.local
                    ? *selection->base.local
                    : allocate_container_register(*selection->base.type);
                if (!selection->base.local) {
                    process_.operations.emplace_back(ReadContainerObject {
                        base, selection->base.object });
                }
                auto operation = ContainerOrderingOperator::reverse;
                if (container_call.text == ".sort") {
                    operation = ContainerOrderingOperator::ascending;
                } else if (container_call.text == ".rsort") {
                    operation = ContainerOrderingOperator::descending;
                } else if (container_call.text == ".shuffle") {
                    operation = ContainerOrderingOperator::shuffle;
                }
                if (direct_receiver) {
                    process_.operations.emplace_back(OrderContainer {
                        operation, base, std::move(key) });
                    if (!selection->base.local) {
                        process_.operations.emplace_back(WriteContainerObject {
                            selection->base.object, base, std::nullopt });
                    }
                    return true;
                }
                const auto ordering_target = allocate_container_register(
                    selection->selected_type);
                copy_hir_static_container_ordinals(
                    ordering_target,
                    selection->selected_type,
                    base,
                    selection->selected_type);
                process_.operations.emplace_back(OrderContainer {
                    operation, ordering_target, std::move(key) });
                const auto replacement = allocate_container_register(
                    *selection->base.type);
                process_.operations.emplace_back(CopyContainerRegister {
                    replacement, base });
                copy_hir_static_container_ordinals(
                    replacement,
                    selection->selected_type,
                    ordering_target,
                    selection->selected_type);
                process_.operations.emplace_back(CopyContainerRegister {
                    base, replacement });
                if (!selection->base.local) {
                    process_.operations.emplace_back(WriteContainerObject {
                        selection->base.object, base, std::nullopt });
                }
                return true;
            }
            constexpr auto push_prefix = std::string_view {
                "@sv-container-method:push_back:"
            };
            if (!container_call.text.starts_with(push_prefix)
                || container_call.operands.size() != 2U) {
                return false;
            }
            const auto receiver = lower_hir_expression(
                container_call.operands[0], 64U);
            const auto pushed_value = lower_hir_expression(
                container_call.operands[1], 64U);
            if (!receiver || register_width(*receiver) != 64U
                || !pushed_value
                || register_width(*pushed_value) != 64U) {
                return false;
            }
            const auto destination = allocate_register(
                64U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(ClassMethodCall {
                destination,
                *receiver,
                "@container-push-back:"
                    + container_call.text.substr(
                        push_prefix.size()),
                { *pushed_value },
                { "" },
                { static_cast<std::uint8_t>(
                    frontend::PortDirection::Input) },
                64U,
                false,
            });
            return true;
        }
        case semantic::sv::StatementKind::return_statement:
            return lower_hir_callable_return(input.value);
        case semantic::sv::StatementKind::pause:
            process_.operations.emplace_back(Pause { });
            return true;
        case semantic::sv::StatementKind::finish:
            process_.operations.emplace_back(Stop { });
            return true;
        case semantic::sv::StatementKind::exit_program:
            if (!systemverilog_program_owner_) {
                report(
                    "FSIM-ELAB-SVEXIT-001",
                    "$exit is legal only in a SystemVerilog program",
                    span);
            }
            process_.operations.emplace_back(Halt { true });
            return true;
        case semantic::sv::StatementKind::null_statement:
            return true;
        default:
            return false;
        }
    }

    const auto& input = *statement->vhdl;
    switch (input.kind) {
    case semantic::vhdl::StatementKind::signal_assignment:
        return lower_assignment(true, true);
    case semantic::vhdl::StatementKind::variable_assignment:
        return lower_assignment(false, false);
    case semantic::vhdl::StatementKind::force:
        return input.target && input.value
            && lower_hir_force_release(
                *input.target, input.value, std::nullopt, true,
                input.source, input.force_driving_value);
    case semantic::vhdl::StatementKind::release:
        return input.target
            && lower_hir_force_release(
                *input.target, std::nullopt, std::nullopt, false,
                input.source, input.force_driving_value);
    case semantic::vhdl::StatementKind::procedure_call:
        return lower_hir_vhdl_procedure_call(statement_id);
    case semantic::vhdl::StatementKind::conditional:
        return lower_if();
    case semantic::vhdl::StatementKind::selection:
        return lower_case(input.alternatives);
    case semantic::vhdl::StatementKind::loop:
        return lower_loop(input.loop_label);
    case semantic::vhdl::StatementKind::exit_loop:
        return lower_loop_control(input.loop_control_label, true);
    case semantic::vhdl::StatementKind::next_loop:
        return lower_loop_control(input.loop_control_label, false);
    case semantic::vhdl::StatementKind::block:
        return lower_block(input.label);
    case semantic::vhdl::StatementKind::assertion:
    case semantic::vhdl::StatementKind::report: {
        bool valid_report_types = true;
        const auto report_expression = input.report
            ? specialized_hir_unit_->find_expression(*input.report)
            : std::nullopt;
        if (input.report
            && !hir_expression_is_string(
                *input.report, hir_process_scope_)) {
            report(
                "FSIM-ELAB-VHREPORT-001",
                "VHDL report expression must have a string type",
                report_expression && report_expression->vhdl != nullptr
                    ? hir_source_span(report_expression->vhdl->source)
                    : span);
            valid_report_types = false;
        }
        const auto message = input.report
            ? specialized_hir_unit_->evaluate_string_expression(
                  *input.report)
            : std::optional<std::string> { };
        const auto severity_value = input.severity
            ? specialized_hir_unit_->evaluate_integral_expression(
                  *input.severity)
            : std::optional<std::int64_t> { };
        const auto severity_expression = input.severity
            ? specialized_hir_unit_->find_expression(*input.severity)
            : std::nullopt;
        const auto standard_severity = input.severity
            ? hir_vhdl_standard_enumeration_literal(*input.severity)
            : std::nullopt;
        const bool static_message = !input.report
            || (report_expression && report_expression->vhdl != nullptr
                && report_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal);
        const bool static_severity = !input.severity
            || standard_severity.has_value();
        const auto severity_subtype = input.severity
            ? hir_vhdl_expression_subtype(*input.severity)
            : std::nullopt;
        const bool severity_type = !input.severity
            || standard_severity
            || (severity_subtype
                && severity_subtype->type_mark.spelling
                    == "severity_level");
        if (!severity_type) {
            report(
                "FSIM-ELAB-VHREPORT-002",
                "VHDL report severity must have type severity_level",
                severity_expression
                        && severity_expression->vhdl != nullptr
                    ? hir_source_span(severity_expression->vhdl->source)
                    : span);
            valid_report_types = false;
        }
        if (!valid_report_types) {
            return true;
        }
        // Predefined enumeration names can acquire a placeholder declaration
        // through the compiled resolver.  Its initializer is not the
        // language-defined ordinal, so classify standard literals before
        // consulting general constant evaluation.  This is the same shared
        // classification used for STD.ENV assert-API level operands.
        const auto effective_severity = standard_severity
            ? std::optional<std::int64_t> {
                  static_cast<std::int64_t>(*standard_severity) }
            : severity_value;
        const auto fallback
            = input.kind == semantic::vhdl::StatementKind::report
            ? runtime::simir::AssertionSeverity::note
            : runtime::simir::AssertionSeverity::error;
        const auto location = SourceLocation {
            span.source_name.str(),
            static_cast<std::uint32_t>(span.begin.line),
            static_cast<std::uint32_t>(span.begin.column),
        };
        const auto emit_report = [&]() -> bool {
            if ((!input.report || message)
                && (!input.severity || effective_severity)) {
                process_.operations.emplace_back(Report {
                    message.value_or(
                        input.kind
                                == semantic::vhdl::StatementKind::report
                            ? std::string { }
                            : std::string { "assertion violation" }),
                    effective_severity
                        ? assertion_severity(
                              *effective_severity, fallback)
                        : fallback,
                    location,
                });
                return true;
            }

            std::optional<StringRegisterId> message_register;
            if (input.report) {
                message_register = message
                    ? std::optional { allocate_string_register() }
                    : lower_hir_string_expression(*input.report);
                if (message && message_register) {
                    process_.operations.emplace_back(LoadStringConstant {
                        *message_register, *message });
                }
            } else {
                message_register = allocate_string_register();
                process_.operations.emplace_back(LoadStringConstant {
                    *message_register, "assertion violation" });
            }
            if (!message_register) {
                return false;
            }

            std::optional<RegisterId> severity_register;
            if (input.severity && !effective_severity) {
                severity_register = lower_hir_expression(
                    *input.severity, 2U);
                if (severity_register
                    && register_width(*severity_register) != 2U) {
                    severity_register = resize_register(
                        *severity_register, 2U, false);
                }
            } else {
                severity_register = allocate_register(
                    2U, frontend::ValueDomain::Bit2);
                const auto encoded = effective_severity
                    ? assertion_severity(*effective_severity, fallback)
                    : fallback;
                process_.operations.emplace_back(LoadConstant {
                    *severity_register,
                    unsigned_value(
                        static_cast<std::uint64_t>(encoded), 2U),
                });
            }
            if (!severity_register) {
                return false;
            }
            process_.operations.emplace_back(StringReport {
                *message_register,
                *severity_register,
                location,
                input.kind == semantic::vhdl::StatementKind::report,
            });
            return true;
        };
        if (input.kind == semantic::vhdl::StatementKind::report) {
            return emit_report();
        }
        if (!input.condition) {
            return false;
        }
        const auto condition_width = hir_expression_width(
            *input.condition, hir_process_scope_);
        const auto condition_domain = hir_expression_domain(
            *input.condition, hir_process_scope_);
        const auto boolean_condition = condition_width
                == std::optional<std::size_t> { 1U }
            && condition_domain
            && *condition_domain == frontend::ValueDomain::Boolean;
        const auto vhdl_2019_scalar_condition
            = vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
            && condition_width == std::optional<std::size_t> { 1U }
            && condition_domain
            && (*condition_domain == frontend::ValueDomain::Bit2
                || *condition_domain == frontend::ValueDomain::Logic4
                || *condition_domain == frontend::ValueDomain::Logic9);
        if (!boolean_condition && !vhdl_2019_scalar_condition) {
            const auto expression
                = specialized_hir_unit_->find_expression(*input.condition);
            const auto condition_source = expression
                    && expression->vhdl != nullptr
                ? hir_source_span(expression->vhdl->source)
                : span;
            report(
                "FSIM-ELAB-051",
                "a VHDL assertion condition must have type boolean",
                condition_source);
            return true;
        }
        const auto checked = lower_condition(*input.condition);
        if (!checked) {
            return false;
        }
        if (vhdl_standard_ < frontend::VhdlStandard::Vhdl2019
            && static_message && static_severity
            && (!input.report || message) && effective_severity) {
            process_.operations.emplace_back(Assert {
                *checked,
                message.value_or("assertion violation"),
                assertion_severity(*effective_severity, fallback),
                location,
            });
            return true;
        }
        const auto branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            *checked, 0U, 0U, UnknownBranchPolicy::when_false });
        const auto passed = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto skip_report = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Jump { 0U });
        const auto failed = static_cast<InstructionIndex>(
            process_.operations.size());
        if (!emit_report()) {
            return false;
        }
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch] = Branch {
            *checked, passed, failed, UnknownBranchPolicy::when_false
        };
        process_.operations[skip_report] = Jump { end };
        return true;
    }
    case semantic::vhdl::StatementKind::return_statement:
        return lower_hir_callable_return(input.value);
    case semantic::vhdl::StatementKind::wait_statement: {
        emit_debug_point(DebugPointKind::wait, span);
        if (input.condition) {
            const auto condition_domain = hir_expression_domain(
                *input.condition, hir_process_scope_);
            const auto condition_width = hir_expression_width(
                *input.condition, hir_process_scope_);
            const auto vhdl_2019_logic_condition
                = vhdl_standard_
                      == frontend::VhdlStandard::Vhdl2019
                && condition_width == std::optional<std::size_t> { 1U }
                && condition_domain
                && (*condition_domain == frontend::ValueDomain::Bit2
                    || *condition_domain
                        == frontend::ValueDomain::Logic4
                    || *condition_domain
                        == frontend::ValueDomain::Logic9);
            if ((!condition_domain
                    || *condition_domain
                        != frontend::ValueDomain::Boolean)
                && !vhdl_2019_logic_condition) {
                report(
                    "FSIM-ELAB-079",
                    "a VHDL wait-until condition must have type boolean",
                    span);
                return true;
            }
        }
        if (!input.condition && input.sensitivities.empty()
            && !input.delay) {
            process_.operations.emplace_back(WaitForever { });
            return true;
        }
        const auto timeout = input.delay
                && input.delay->additional.empty()
            ? delay_magnitude(input.delay->primary)
            : std::optional<runtime::SimulationTick> { };
        if (input.delay && !timeout) {
            report(
                "FSIM-ELAB-VHTIME-001",
                "a VHDL wait timeout requires a nonnegative locally static "
                "time value",
                span);
            return true;
        }
        if (!input.condition && input.sensitivities.empty()) {
            process_.operations.emplace_back(WaitFor { *timeout });
            return true;
        }
        {
            std::vector<SignalId> waited_signals;
            waited_signals.reserve(input.sensitivities.size());
            for (const auto& sensitivity : input.sensitivities) {
                std::optional<SignalId> signal;
                if (sensitivity.expression) {
                    const auto declaration = hir_referenced_declaration(
                        *sensitivity.expression);
                    const auto binding = declaration
                        ? hir_runtime_binding(
                              *declaration, hir_process_scope_, true)
                        : std::nullopt;
                    if (binding) {
                        signal = binding->signal;
                    }
                    if (!signal) {
                        auto dependencies = hir_signal_dependencies(
                            *sensitivity.expression,
                            hir_process_scope_);
                        waited_signals.insert(
                            waited_signals.end(),
                            dependencies.begin(),
                            dependencies.end());
                        continue;
                    }
                } else if (const auto found = signals_.find(
                               sensitivity.signal);
                    found != signals_.end()) {
                    signal = found->second;
                }
                if (!signal) {
                    report(
                        "FSIM-ELAB-059",
                        "unknown wait signal '" + sensitivity.signal + "'",
                        hir_source_span(sensitivity.source));
                    return true;
                }
                waited_signals.push_back(*signal);
            }
            if (waited_signals.empty() && input.condition) {
                waited_signals = hir_signal_dependencies(
                    *input.condition, hir_process_scope_);
            }
            std::ranges::sort(waited_signals);
            auto duplicate = std::ranges::unique(waited_signals);
            waited_signals.erase(duplicate.begin(), duplicate.end());
            if (waited_signals.empty()) {
                if (input.delay) {
                    process_.operations.emplace_back(WaitFor { *timeout });
                } else {
                    process_.operations.emplace_back(WaitForever { });
                }
                return true;
            }
            std::optional<RegisterId> timed_out;
            if (input.delay) {
                timed_out = allocate_register(
                    1U, frontend::ValueDomain::Boolean);
            }
            const auto wait_start = static_cast<InstructionIndex>(
                process_.operations.size());
            WaitOn wait { waited_signals };
            if (input.delay) {
                wait.timeout = *timeout;
                wait.timeout_result = timed_out;
            }
            process_.operations.emplace_back(std::move(wait));
            if (!input.condition) {
                return true;
            }
            std::optional<InstructionIndex> timeout_branch;
            if (timed_out) {
                timeout_branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(Branch {
                    *timed_out,
                    0U,
                    0U,
                    UnknownBranchPolicy::error,
                });
            }
            const auto condition_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto checked = lower_condition(*input.condition);
            if (!checked) {
                return false;
            }
            const auto condition_branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *checked,
                0U,
                0U,
                UnknownBranchPolicy::error,
            });
            const auto rewait = static_cast<InstructionIndex>(
                process_.operations.size());
            WaitOn repeated_wait { std::move(waited_signals) };
            if (input.delay) {
                repeated_wait.timeout = *timeout;
                repeated_wait.timeout_result = timed_out;
                repeated_wait.timeout_origin = wait_start;
            }
            process_.operations.emplace_back(std::move(repeated_wait));
            process_.operations.emplace_back(Jump {
                timeout_branch.value_or(condition_start) });
            const auto satisfied = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[condition_branch] = Branch {
                *checked,
                satisfied,
                rewait,
                UnknownBranchPolicy::error,
            };
            if (timeout_branch) {
                process_.operations[*timeout_branch] = Branch {
                    *timed_out,
                    satisfied,
                    condition_start,
                    UnknownBranchPolicy::error,
                };
            }
            return true;
        }
    }
    case semantic::vhdl::StatementKind::null_statement:
        return true;
    default:
        return false;
    }
    return false;
}

std::string Lowerer::hir_procedural_target_key(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return { };
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression) {
        return { };
    }
    std::string result;
    std::span<const semantic::ExpressionId> operands;
    if (expression->systemverilog != nullptr) {
        const auto& source = *expression->systemverilog;
        result = "sv:" + std::to_string(static_cast<unsigned>(source.kind)) + ':' + source.text;
        operands = source.operands;
    } else {
        const auto& source = *expression->vhdl;
        result = "vhdl:" + std::to_string(static_cast<unsigned>(source.kind)) + ':' + source.text;
        operands = source.operands;
    }
    result += '[';
    for (const auto operand : operands) {
        const auto key = hir_procedural_target_key(operand);
        result += std::to_string(key.size());
        result += ':';
        result += key;
    }
    result += ']';
    return result;
}

bool Lowerer::hir_procedural_target_is_visible(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return false;
    }
    const auto& source = *expression->systemverilog;
    if (source.kind == semantic::sv::ExpressionKind::concatenation) {
        return !source.operands.empty()
            && std::ranges::all_of(
                source.operands,
                [&](const semantic::ExpressionId operand) {
                    return hir_procedural_target_is_visible(operand);
                });
    }
    const auto declaration = hir_target_declaration(expression_id);
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, hir_process_scope_, false)
        : std::nullopt;
    return binding && binding->signal.has_value();
}

void Lowerer::prepare_hir_procedural_continuous_assignments(
    const std::span<const semantic::StatementId> statements)
{
    if (specialized_hir_unit_ == nullptr) {
        return;
    }
    const auto visit = [&](const auto& self,
                           const std::span<const semantic::StatementId> nodes)
        -> void {
        for (const auto statement_id : nodes) {
            const auto statement
                = specialized_hir_unit_->find_statement(statement_id);
            if (!statement || statement->systemverilog == nullptr) {
                continue;
            }
            const auto& source = *statement->systemverilog;
            if (source.kind
                    == semantic::sv::StatementKind::procedural_assign
                && source.target && source.value) {
                ProceduralContinuousAssignment assignment;
                assignment.statement = statement_id;
                assignment.target = *source.target;
                assignment.value = *source.value;
                assignment.source = source.source;
                assignment.statement_key = "hir:"
                    + std::to_string(statement_id.value());
                assignment.target_key
                    = hir_procedural_target_key(*source.target);
                assignment.valid
                    = hir_procedural_target_is_visible(*source.target);
                const auto index
                    = procedural_continuous_assignments_.size();
                assignment.active_name = process_.name
                    + ".$procedural_assign_"
                    + std::to_string(statement_id.value())
                    + ".active";
                if (!assignment.valid) {
                    // Direct statement lowering owns the source diagnostic.
                } else if (design_.signals_.size()
                    > std::numeric_limits<SignalId>::max()) {
                    report(
                        "FSIM-ELAB-SVPROCASSIGN-001",
                        "the design has too many signals for a procedural "
                        "continuous assignment driver",
                        hir_source_span(source.source));
                    assignment.valid = false;
                } else {
                    assignment.active = static_cast<SignalId>(
                        design_.signals_.size());
                    SignalInfo info;
                    info.id = assignment.active;
                    info.name = assignment.active_name;
                    info.width = 1U;
                    info.type_name = "logic";
                    info.source_domain = frontend::ValueDomain::Logic4;
                    info.declaration_span
                        = hir_source_span(source.source);
                    design_.signal_info_.push_back(std::move(info));
                    design_.signals_.push_back(Signal {
                        assignment.active_name,
                        PackedLogic4 { 1U, Logic4::zero },
                        ResolutionKind::none,
                        ValueKind::logic4,
                    });
                    design_.signal_by_name_.emplace(
                        assignment.active_name, assignment.active);
                }
                procedural_continuous_assignment_by_statement_.emplace(
                    assignment.statement_key, index);
                procedural_continuous_assignments_by_target_[assignment.target_key]
                    .push_back(index);
                procedural_continuous_assignments_.push_back(
                    std::move(assignment));
            }
            self(self, source.statements);
            self(self, source.else_statements);
            for (const auto& alternative : source.case_alternatives) {
                self(self, alternative.statements);
            }
            self(self, source.loop_updates);
        }
    };
    visit(visit, statements);
}

bool Lowerer::lower_hir_procedural_continuous_assignment(
    const semantic::StatementId statement_id)
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->systemverilog == nullptr
        || !statement->systemverilog->target) {
        return false;
    }
    const auto& source = *statement->systemverilog;
    const auto target = *source.target;
    const auto key = hir_procedural_target_key(target);
    const auto matches
        = procedural_continuous_assignments_by_target_.find(key);
    if (matches == procedural_continuous_assignments_by_target_.end()) {
        report(
            "FSIM-ELAB-SVPROCASSIGN-001",
            "procedural assign/deassign requires a visible packed variable "
            "target that is declared outside the process",
            hir_source_span(source.source));
        return true;
    }
    const auto zero = allocate_register(
        1U, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(
        LoadConstant { zero, PackedLogic4 { 1U, Logic4::zero } });
    for (const auto index : matches->second) {
        const auto& assignment
            = procedural_continuous_assignments_[index];
        if (assignment.valid) {
            process_.operations.emplace_back(
                WriteBlocking { assignment.active, zero });
        }
    }

    if (source.kind == semantic::sv::StatementKind::deassign) {
        const auto width = hir_expression_width(
            target, hir_process_scope_);
        auto preserved = width
            ? lower_hir_expression(target, *width)
            : std::nullopt;
        if (!width || !preserved
            || register_width(*preserved) != *width
            || !lower_hir_packed_copy_out(target, *preserved)) {
            report(
                "FSIM-ELAB-SVPROCASSIGN-001",
                "procedural deassign target has no executable packed layout",
                hir_source_span(source.source));
            return true;
        }
        return lower_hir_force_release(
            target, std::nullopt, std::nullopt, false, source.source);
    }

    const auto found
        = procedural_continuous_assignment_by_statement_.find(
            "hir:" + std::to_string(statement_id.value()));
    if (found == procedural_continuous_assignment_by_statement_.end()
        || !procedural_continuous_assignments_[found->second].valid
        || !source.value) {
        report(
            "FSIM-ELAB-SVPROCASSIGN-001",
            "procedural continuous assignment requires a visible packed "
            "variable target declared outside the process",
            hir_source_span(source.source));
        return true;
    }
    if (!lower_hir_force_release(
            target, source.value, std::nullopt, true, source.source)) {
        return false;
    }
    const auto one = allocate_register(
        1U, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(
        LoadConstant { one, PackedLogic4 { 1U, Logic4::one } });
    process_.operations.emplace_back(WriteBlocking {
        procedural_continuous_assignments_[found->second].active,
        one,
    });
    return true;
}

void Lowerer::materialize_hir_procedural_continuous_assignments()
{
    if (specialized_hir_unit_ == nullptr) {
        return;
    }
    auto extended_signals = signals_;
    for (const auto& assignment : procedural_continuous_assignments_) {
        if (assignment.valid) {
            extended_signals.emplace(
                assignment.active_name, assignment.active);
        }
    }
    for (std::size_t index { };
        index < procedural_continuous_assignments_.size(); ++index) {
        const auto& assignment
            = procedural_continuous_assignments_[index];
        if (!assignment.valid || !assignment.statement.valid()
            || !assignment.target.valid() || !assignment.value.valid()) {
            continue;
        }
        Lowerer driver_lowerer(
            design_, extended_signals, read_only_signals_, string_objects_,
            read_only_string_objects_, container_objects_,
            read_only_container_objects_,
            diagnostics_);
        driver_lowerer.set_specialized_hir_unit(specialized_hir_unit_);
        driver_lowerer.set_systemverilog_interface_handles(
            systemverilog_interface_handles_);
        driver_lowerer.set_systemverilog_program_owner(
            systemverilog_program_owner_);
        driver_lowerer.vhdl_standard_ = vhdl_standard_;
        HirProcessDescription description;
        description.source = assignment.source;
        const auto statement = specialized_hir_unit_->find_statement(
            assignment.statement);
        description.scope = statement
                && statement->systemverilog != nullptr
            ? statement->systemverilog->scope
            : hir_process_scope_;
        description.name = "$procedural_assign_"
            + std::to_string(index);
        description.procedural_assignment_active = assignment.active;
        description.procedural_assignment_target = assignment.target;
        description.procedural_assignment_value = assignment.value;
        description.kind
            = frontend::ProcessKind::SystemVerilogAlwaysComb;
        description.wildcard_sensitivity = true;
        description.require_wildcard_dependency = false;
        description.always_comb_or_latch = true;
        auto driver = driver_lowerer.lower_hir_process_body(
            description, language_, hierarchy_);
        if (!driver) {
            report(
                "FSIM-ELAB-SVPROCASSIGN-002",
                "procedural continuous assignment driver cannot be lowered "
                "from compiled HIR",
                hir_source_span(assignment.source));
            continue;
        }
        const auto process_index = design_.processes_.size()
            + 1U + generated_processes_.size();
        if (process_index > std::numeric_limits<ProcessId>::max()) {
            report(
                "FSIM-ELAB-SVPROCASSIGN-002",
                "the design has too many processes for a procedural "
                "continuous assignment driver",
                hir_source_span(assignment.source));
            continue;
        }
        driver->id = static_cast<ProcessId>(process_index);
        generated_processes_.push_back(std::move(*driver));
    }
}
} // namespace fsim::elaboration
