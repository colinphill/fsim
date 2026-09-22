// SPDX-License-Identifier: Apache-2.0
#include "application_systemverilog_hir_internal.hpp"

namespace fsim::app::application_detail {
namespace sv = semantic::sv;
namespace systemverilog_hir_detail {

    namespace sv = semantic::sv;

    [[nodiscard]] std::string nested_alternative_discriminator(
        const std::string_view parent, const std::string_view child)
    {
        if (parent.empty())
            return std::string { child };
        return std::string { parent } + "/" + std::string { child };
    }

    [[nodiscard]] sv::UnitKind unit_kind(
        const frontend::UnitKind kind) noexcept
    {
        switch (kind) {
        case frontend::UnitKind::SystemVerilogPackage:
            return sv::UnitKind::package;
        case frontend::UnitKind::SystemVerilogInterface:
            return sv::UnitKind::interface;
        case frontend::UnitKind::SystemVerilogProgram:
            return sv::UnitKind::program;
        case frontend::UnitKind::SystemVerilogConfiguration:
            return sv::UnitKind::configuration;
        case frontend::UnitKind::SystemVerilogBind:
            return sv::UnitKind::bind;
        default:
            return sv::UnitKind::module;
        }
    }

    [[nodiscard]] sv::Direction direction(
        const frontend::PortDirection value) noexcept
    {
        switch (value) {
        case frontend::PortDirection::Input:
            return sv::Direction::input;
        case frontend::PortDirection::Output:
            return sv::Direction::output;
        case frontend::PortDirection::Inout:
            return sv::Direction::inout;
        case frontend::PortDirection::Ref:
            return sv::Direction::ref;
        default:
            return sv::Direction::unknown;
        }
    }

    [[nodiscard]] sv::EdgeKind edge_kind(
        const frontend::EdgeKind value) noexcept
    {
        switch (value) {
        case frontend::EdgeKind::Positive:
            return sv::EdgeKind::positive;
        case frontend::EdgeKind::Negative:
            return sv::EdgeKind::negative;
        default:
            return sv::EdgeKind::any;
        }
    }

    [[nodiscard]] sv::ConcurrentAssertionKind concurrent_assertion_kind(
        const frontend::SystemVerilogConcurrentAssertionKind kind) noexcept
    {
        switch (kind) {
        case frontend::SystemVerilogConcurrentAssertionKind::Assume:
            return sv::ConcurrentAssertionKind::assumption;
        case frontend::SystemVerilogConcurrentAssertionKind::Cover:
            return sv::ConcurrentAssertionKind::cover;
        case frontend::SystemVerilogConcurrentAssertionKind::Restrict:
            return sv::ConcurrentAssertionKind::restriction;
        default:
            return sv::ConcurrentAssertionKind::assertion;
        }
    }

    [[nodiscard]] sv::AssertionRegion assertion_region(
        const frontend::SystemVerilogAssertionRegion region) noexcept
    {
        switch (region) {
        case frontend::SystemVerilogAssertionRegion::Observed:
            return sv::AssertionRegion::observed;
        case frontend::SystemVerilogAssertionRegion::Reactive:
            return sv::AssertionRegion::reactive;
        default:
            return sv::AssertionRegion::preponed;
        }
    }

    [[nodiscard]] sv::Lifetime lifetime(
        const bool automatic,
        const bool explicit_lifetime) noexcept
    {
        if (automatic) {
            return sv::Lifetime::automatic;
        }
        return explicit_lifetime
            ? sv::Lifetime::static_lifetime
            : sv::Lifetime::implicit;
    }

    [[nodiscard]] semantic::TypeId find_systemverilog_hir_type(
        const semantic::Model& model,
        const sv::Hir& hir,
        const semantic::ScopeId scope,
        const std::string_view spelling) noexcept
    {
        const auto parent_scope = [&](const semantic::ScopeId candidate)
            -> std::optional<semantic::ScopeId> {
            if (!candidate.valid()
                || candidate.value() >= model.scopes().size()) {
                return std::nullopt;
            }
            return model.scopes()[candidate.value()].parent;
        };
        auto visible_scope = std::optional { scope };
        while (visible_scope) {
            const auto found = std::ranges::find_if(
                model.types(), [&](const semantic::Type& type) {
                    return type.scope == *visible_scope
                        && type.name == spelling;
                });
            if (found != model.types().end()) {
                return found->id;
            }
            visible_scope = parent_scope(*visible_scope);
        }

        const auto owner_id = scope.valid()
                && scope.value() < model.scopes().size()
            ? std::optional { model.scopes()[scope.value()].unit }
            : std::nullopt;
        if (!owner_id || owner_id->value() >= model.units().size()) {
            return { };
        }
        const auto& semantic_owner = model.units()[owner_id->value()];
        const auto owner = std::ranges::find(
            hir.units(), *owner_id, &sv::Unit::id);

        const auto separator = spelling.rfind("::");
        const auto package_name = separator == std::string_view::npos
            ? std::string_view { }
            : spelling.substr(0U, separator);
        const auto simple_name = separator == std::string_view::npos
            ? spelling
            : spelling.substr(separator + 2U);
        const auto find_package = [&](const std::string_view name) {
            return std::ranges::find_if(
                hir.units(), [&](const sv::Unit& candidate) {
                    return candidate.kind == sv::UnitKind::package
                        && candidate.library == semantic_owner.library
                        && candidate.name == name;
                });
        };
        const auto direct_type = [&](const sv::Unit& package) {
            const auto found = std::ranges::find_if(
                model.types(), [&](const semantic::Type& type) {
                    return type.scope == package.scope
                        && type.name == simple_name;
                });
            return found == model.types().end()
                ? semantic::TypeId { }
                : found->id;
        };
        const auto collect = [&](const auto& self,
                                 const sv::Unit& package,
                                 std::unordered_set<std::uint32_t>& visiting,
                                 std::vector<semantic::TypeId>& candidates)
            -> void {
            if (!visiting.insert(package.id.value()).second) {
                return;
            }
            if (const auto direct = direct_type(package); direct.valid()) {
                candidates.push_back(direct);
                visiting.erase(package.id.value());
                return;
            }
            for (const auto& exported : package.exports) {
                if ((exported.package.spelling == "*" && exported.member)
                    || (exported.member
                        && exported.member->spelling != simple_name)) {
                    continue;
                }
                for (const auto& imported : package.imports) {
                    if ((exported.package.spelling != "*"
                            && exported.package.spelling
                                != imported.package.spelling)
                        || (imported.member
                            && imported.member->spelling != simple_name)) {
                        continue;
                    }
                    const auto source = find_package(
                        imported.package.spelling);
                    if (source != hir.units().end()) {
                        self(self, *source, visiting, candidates);
                    }
                }
            }
            visiting.erase(package.id.value());
        };
        std::vector<semantic::TypeId> candidates;
        const auto append_package = [&](const std::string_view name) {
            const auto package = find_package(name);
            if (package == hir.units().end()) {
                return;
            }
            std::unordered_set<std::uint32_t> visiting;
            collect(collect, *package, visiting, candidates);
        };
        if (!package_name.empty()) {
            append_package(package_name);
        } else if (owner != hir.units().end()) {
            bool explicit_import { };
            for (const auto& imported : owner->imports) {
                if (imported.wildcard || !imported.member
                    || imported.member->spelling != simple_name) {
                    continue;
                }
                explicit_import = true;
                append_package(imported.package.spelling);
            }
            if (!explicit_import) {
                for (const auto& imported : owner->imports) {
                    if (imported.wildcard) {
                        append_package(imported.package.spelling);
                    }
                }
            }
        }
        std::ranges::sort(
            candidates, { }, [](const semantic::TypeId id) {
                return id.value();
            });
        const auto unique = std::ranges::unique(candidates);
        candidates.erase(unique.begin(), unique.end());
        return candidates.size() == 1U ? candidates.front()
                                       : semantic::TypeId { };
    }

    SystemVerilogHirBuilder::SystemVerilogHirBuilder(
        semantic::Model& model,
        sv::Hir& hir,
        const std::span<const frontend::SystemVerilogClassSpecialization>
            class_specializations)
        : model_(model)
        , hir_(hir)
        , class_specializations_(class_specializations)
    {
    }

    void SystemVerilogHirBuilder::add_design(const frontend::ParsedDesign& parsed)
    {
        for (std::size_t index = 0; index < parsed.units.size(); ++index) {
            const auto& unit = parsed.units[index];
            if (unit.language != frontend::Language::Vhdl2008) {
                add_unit(unit, semantic::UnitId::from_index(static_cast<std::uint32_t>(index)));
            }
        }
        for (const auto& unit : parsed.units) {
            if (unit.language == frontend::Language::SystemVerilog2017) {
                add_classes(unit.systemverilog_classes);
            }
        }
        for (const auto& declaration :
            parsed.systemverilog_dpi_declarations) {
            const auto library = declaration.library.empty()
                ? std::string { "work" }
                : declaration.library;
            auto parent_identity = library + "::$unit";
            if (!declaration.compilation_unit_identity.empty()) {
                parent_identity += "@"
                    + declaration.compilation_unit_identity;
            }
            const auto scope = ensure_compilation_unit(
                declaration.span,
                library,
                declaration.compilation_unit_identity,
                declaration.standard_revision,
                declaration.verilog_compatibility_profile,
                parent_identity);
            const auto parent = model_.scopes().at(scope.value()).unit;
            hir_.mutable_dpi_declarations().push_back(dpi_declaration(
                declaration,
                scope,
                model_.units().at(parent.value()).origin));
        }
        add_classes(parsed.systemverilog_classes);
        for (const auto& input : parsed.udp_declarations) {
            const auto udp_source = source(input.span);
            const auto udp_origin = model_.add_origin(
                semantic::OriginKind::parsed, udp_source, std::nullopt, input.name);
            const auto level = [](const frontend::VerilogUdpLevelSymbol value) {
                using Input = frontend::VerilogUdpLevelSymbol;
                switch (value) {
                case Input::Zero:
                    return sv::UdpLevel::zero;
                case Input::One:
                    return sv::UdpLevel::one;
                case Input::Unknown:
                    return sv::UdpLevel::unknown;
                case Input::DontCare:
                    return sv::UdpLevel::dont_care;
                case Input::Binary:
                    return sv::UdpLevel::binary;
                }
                return sv::UdpLevel::unknown;
            };
            const auto edge = [](const frontend::VerilogUdpEdgeSymbol value) {
                using Input = frontend::VerilogUdpEdgeSymbol;
                switch (value) {
                case Input::None:
                    return sv::UdpEdge::none;
                case Input::Rising:
                    return sv::UdpEdge::rising;
                case Input::Falling:
                    return sv::UdpEdge::falling;
                case Input::Positive:
                    return sv::UdpEdge::positive;
                case Input::Negative:
                    return sv::UdpEdge::negative;
                case Input::Any:
                    return sv::UdpEdge::any;
                case Input::Explicit:
                    return sv::UdpEdge::explicit_edge;
                }
                return sv::UdpEdge::none;
            };
            const auto output = [](const frontend::VerilogUdpOutputSymbol value) {
                using Input = frontend::VerilogUdpOutputSymbol;
                switch (value) {
                case Input::Zero:
                    return sv::UdpOutput::zero;
                case Input::One:
                    return sv::UdpOutput::one;
                case Input::Unknown:
                    return sv::UdpOutput::unknown;
                case Input::NoChange:
                    return sv::UdpOutput::no_change;
                }
                return sv::UdpOutput::unknown;
            };
            sv::UdpDeclaration declaration;
            declaration.language = input.language == frontend::Language::Verilog2005
                ? semantic::Language::verilog
                : semantic::Language::system_verilog;
            declaration.standard = std::string {
                frontend::to_string(input.standard_revision)
            };
            declaration.compatibility_profile = input.verilog_compatibility_profile;
            declaration.library = input.library;
            declaration.name = input.name;
            declaration.output = input.output;
            declaration.inputs = input.inputs;
            declaration.sequential = input.sequential;
            declaration.output_register = input.output_reg;
            if (input.initial_output) {
                declaration.initial_output = output(*input.initial_output);
            }
            for (const auto& input_row : input.rows) {
                sv::UdpTableRow row;
                row.source = source(input_row.span);
                if (input_row.current_state) {
                    row.current_state = level(*input_row.current_state);
                }
                row.output = output(input_row.output);
                for (const auto& input_pattern : input_row.inputs) {
                    row.inputs.push_back({ level(input_pattern.level),
                        edge(input_pattern.edge), level(input_pattern.previous),
                        level(input_pattern.current), source(input_pattern.span) });
                }
                declaration.rows.push_back(std::move(row));
            }
            declaration.time_unit = input.time_unit;
            declaration.time_precision = input.time_precision;
            declaration.source = udp_source;
            declaration.origin = udp_origin;
            hir_.mutable_udps().push_back(std::move(declaration));
        }
        if (parsed.systemverilog_covergroup_instances.size()
            != parsed.systemverilog_covergroup_instance_syntax.size()) {
            throw std::logic_error {
                "covergroup instance and syntax inventories disagree"
            };
        }
        for (const auto& input : parsed.systemverilog_covergroup_instances) {
            const auto syntax = std::ranges::find(
                parsed.systemverilog_covergroup_instance_syntax,
                input.runtime_identity,
                &frontend::SystemVerilogCovergroupInstanceSyntax::
                    runtime_identity);
            if (syntax
                    == parsed.systemverilog_covergroup_instance_syntax.end()
                || std::ranges::count(
                       parsed.systemverilog_covergroup_instance_syntax,
                       input.runtime_identity,
                       &frontend::SystemVerilogCovergroupInstanceSyntax::
                           runtime_identity)
                    != 1) {
                throw std::logic_error {
                    "covergroup instance has no unique compile-local syntax "
                    "sidecar"
                };
            }
            hir_.mutable_covergroup_instances().push_back(
                covergroup_instance(input, *syntax));
        }
        synchronize_generate_class_ownership();
        compose_constraints();
    }

    [[nodiscard]] semantic::SourceSpanId SystemVerilogHirBuilder::source(
        const frontend::SourceSpan& span)
    {
        return intern_semantic_span(model_, span);
    }

    [[nodiscard]] semantic::OriginId SystemVerilogHirBuilder::origin(
        const semantic::SourceSpanId span,
        const semantic::OriginId parent,
        const std::string_view detail)
    {
        return model_.add_origin(
            semantic::OriginKind::parsed, span, parent, std::string { detail });
    }

    [[nodiscard]] semantic::UnitId SystemVerilogHirBuilder::scope_unit(
        const semantic::ScopeId scope) const
    {
        return model_.scopes()[scope.value()].unit;
    }

    [[nodiscard]] semantic::ScopeId SystemVerilogHirBuilder::nested_scope(
        const semantic::ScopeId parent_scope,
        const std::string_view name,
        const semantic::SourceSpanId span,
        const semantic::OriginId scope_origin)
    {
        return model_.add_scope(
            scope_unit(parent_scope),
            parent_scope,
            std::string { name },
            span,
            scope_origin);
    }

    [[nodiscard]] sv::Name SystemVerilogHirBuilder::name(
        const std::string_view spelling,
        const frontend::SourceSpan& span,
        const semantic::ScopeId scope)
    {
        sv::Name result { std::string { spelling }, source(span), std::nullopt, { } };
        auto visible_scope = std::optional<semantic::ScopeId> { scope };
        while (visible_scope) {
            for (const auto& declaration : hir_.declarations()) {
                if (declaration.scope == *visible_scope
                    && declaration.name == spelling) {
                    result.overloads.push_back(declaration.id);
                }
            }
            if (!result.overloads.empty()) {
                break;
            }
            visible_scope = model_.scopes()[visible_scope->value()].parent;
        }
        if (result.overloads.size() == 1) {
            result.selected = result.overloads.front();
        }
        return result;
    }

    [[nodiscard]] std::optional<semantic::ExpressionId> SystemVerilogHirBuilder::expression(
        const frontend::Expression& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent)
    {
        if (input.kind == frontend::ExpressionKind::Invalid) {
            return std::nullopt;
        }
        const auto span = source(input.span);
        if (const auto found = expressions_.find(&input);
            found != expressions_.end()) {
            const auto retained = std::ranges::find(
                hir_.expressions(), found->second,
                &sv::Expression::id);
            if (retained != hir_.expressions().end()
                && retained->scope == scope
                && retained->source == span
                && retained->origin.valid()
                && retained->origin.value() < model_.origins().size()
                && model_.origins()[retained->origin.value()].parent
                    == parent) {
                return found->second;
            }
            // Class callable projections are compile-local values. Their
            // storage can be reused by a later projection even though it
            // denotes a different source expression. Do not let an obsolete
            // pointer alias the earlier HIR expression.
            expressions_.erase(found);
        }
        const auto expression_origin = origin(
            span, parent, "SystemVerilog expression");
        const auto id = model_.add_expression_identity(
            scope, span, expression_origin);
        expressions_.emplace(&input, id);
        sv::Expression output;
        output.id = id;
        output.scope = scope;
        output.kind = expression_kind(input);
        output.text = input.text;
        output.source = span;
        output.origin = expression_origin;
        output.nominal_type = input.nominal_type;
        output.class_identity = input.nominal_type;
        output.signed_value = input.kind
                == frontend::ExpressionKind::IntegerLiteral
            || (input.kind == frontend::ExpressionKind::LogicLiteral
                && (input.text.find("'s") != std::string::npos
                    || input.text.find("'S") != std::string::npos))
            || (input.call_result_width != 0U
                && input.call_result_signed);
        if (input.kind == frontend::ExpressionKind::Call
            && input.text == "@sv-clocking-event") {
            output.clocking_edge = edge_kind(
                static_cast<frontend::EdgeKind>(input.call_result_width));
        }
        output.decoded_string = input.decoded_string;
        if (input.systemverilog_decimal_literal) {
            const auto& literal = *input.systemverilog_decimal_literal;
            output.decimal_literal = sv::DecimalLiteral {
                literal.kind
                        == frontend::SystemVerilogDecimalLiteralKind::Time
                    ? sv::DecimalLiteralKind::time
                    : sv::DecimalLiteralKind::real,
                literal.digits,
                literal.decimal_exponent,
                literal.time_unit,
            };
        }
        output.scalar_kind = static_cast<sv::ScalarKind>(
            input.systemverilog_scalar_kind);
        output.generated_text
            = static_cast<sv::GeneratedTextKind>(input.generated_text);
        if (input.kind == frontend::ExpressionKind::Identifier
            || (input.kind == frontend::ExpressionKind::Call
                && !input.text.starts_with("@sv-"))) {
            output.referenced_name = name(input.text, input.span, scope);
        }
        output.argument_names = input.call_argument_names;
        for (std::size_t index = 0; index < input.operands.size(); ++index) {
            const auto& operand = input.operands[index];
            const auto operand_id = expression(
                operand, scope, expression_origin);
            if (operand_id) {
                output.operands.push_back(*operand_id);
            }
            if (input.kind == frontend::ExpressionKind::Call) {
                sv::CallAssociation association;
                if (index < input.call_argument_names.size()
                    && !input.call_argument_names[index].empty()) {
                    association.formal = input.call_argument_names[index];
                }
                association.actual = operand_id;
                association.source = operand.valid()
                    ? source(operand.span)
                    : span;
                output.call_arguments.push_back(std::move(association));
            }
        }
        if (input.kind == frontend::ExpressionKind::Aggregate) {
            for (std::size_t index = 0; index < output.operands.size(); ++index) {
                sv::AssignmentPatternAssociation association;
                association.value = output.operands[index];
                association.source = index < input.operands.size()
                    ? source(input.operands[index].span)
                    : span;
                if (index < input.aggregate_choices.size()) {
                    association.choice_spelling = input.aggregate_choices[index];
                }
                if (index < input.aggregate_choice_expressions.size()) {
                    for (const auto& choice :
                        input.aggregate_choice_expressions[index]) {
                        if (const auto choice_id = expression(
                                choice, scope, expression_origin)) {
                            association.choices.push_back(*choice_id);
                        }
                    }
                }
                output.associations.push_back(std::move(association));
            }
        }
        const auto inline_marker = std::ranges::find(
            input.aggregate_choices, "@sv-inline-constraint");
        if (inline_marker != input.aggregate_choices.end()) {
            const auto index = static_cast<std::size_t>(std::distance(
                input.aggregate_choices.begin(), inline_marker));
            if (index < input.aggregate_choice_expressions.size()) {
                sv::AssignmentPatternAssociation association;
                association.choice_spelling = "@sv-inline-constraint";
                association.source = span;
                for (const auto& constraint :
                    input.aggregate_choice_expressions[index]) {
                    if (const auto constraint_id = expression(
                            constraint, scope, expression_origin)) {
                        association.choices.push_back(*constraint_id);
                    }
                }
                if (!association.choices.empty()) {
                    association.value = association.choices.front();
                    output.associations.push_back(std::move(association));
                }
            }
        }
        hir_.mutable_expressions().push_back(std::move(output));
        return id;
    }

    [[nodiscard]] sv::ExpressionKind SystemVerilogHirBuilder::expression_kind(
        const frontend::Expression& expression) noexcept
    {
        if (expression.text == "@sv-null") {
            return sv::ExpressionKind::class_null;
        }
        if (expression.text.starts_with("@sv-new:")) {
            return sv::ExpressionKind::class_allocation;
        }
        if (expression.text.starts_with("@sv-dollar-cast:")) {
            return sv::ExpressionKind::class_cast;
        }
        if (expression.text.starts_with("@sv-property:")) {
            return sv::ExpressionKind::class_property;
        }
        if (expression.text.starts_with("@sv-static-property:")) {
            return sv::ExpressionKind::class_static_property;
        }
        if (expression.text.starts_with("@sv-method:")
            || expression.text.starts_with("@sv-base-method:")) {
            return sv::ExpressionKind::class_method_call;
        }
        if (expression.text.starts_with("@sv-static-method:")) {
            return sv::ExpressionKind::class_static_method_call;
        }
        using Input = frontend::ExpressionKind;
        switch (expression.kind) {
        case Input::Invalid:
            return sv::ExpressionKind::invalid;
        case Input::Identifier:
            return sv::ExpressionKind::name;
        case Input::IntegerLiteral:
            return sv::ExpressionKind::integer_literal;
        case Input::BooleanLiteral:
            return sv::ExpressionKind::boolean_literal;
        case Input::LogicLiteral:
            return sv::ExpressionKind::logic_literal;
        case Input::StringLiteral:
            return sv::ExpressionKind::string_literal;
        case Input::Unary:
            return sv::ExpressionKind::unary;
        case Input::Update:
            return sv::ExpressionKind::update;
        case Input::Binary:
            return sv::ExpressionKind::binary;
        case Input::Call:
            return sv::ExpressionKind::call;
        case Input::Index:
            return sv::ExpressionKind::index;
        case Input::Slice:
            return sv::ExpressionKind::slice;
        case Input::Aggregate:
            return sv::ExpressionKind::assignment_pattern;
        case Input::Concatenation:
            return sv::ExpressionKind::concatenation;
        case Input::Replication:
            return sv::ExpressionKind::replication;
        case Input::DefaultChoice:
            return sv::ExpressionKind::default_choice;
        case Input::Conditional:
            return sv::ExpressionKind::invalid;
        }
        return sv::ExpressionKind::invalid;
    }

    [[nodiscard]] semantic::TypeId SystemVerilogHirBuilder::find_type(
        const semantic::ScopeId scope,
        const std::string_view type_name,
        const std::optional<semantic::SourceSpanId> exact_source)
        const noexcept
    {
        auto visible_scope = std::optional<semantic::ScopeId> { scope };
        while (visible_scope) {
            for (const auto& type : model_.types()) {
                if (type.scope == *visible_scope && type.name == type_name
                    && (!exact_source || type.source == *exact_source)) {
                    return type.id;
                }
            }
            if (exact_source) {
                break;
            }
            if (!visible_scope->valid()
                || visible_scope->value() >= model_.scopes().size()) {
                break;
            }
            visible_scope = model_.scopes()[visible_scope->value()].parent;
        }
        const auto qualifier_separator = type_name.rfind("::");
        const auto simple_name = qualifier_separator
                == std::string_view::npos
            ? type_name
            : type_name.substr(qualifier_separator + 2U);
        if (exact_source) {
            semantic::TypeId selected;
            for (const auto& type : model_.types()) {
                if (type.source != *exact_source
                    || type.name != simple_name) {
                    continue;
                }
                if (selected.valid() && selected != type.id) {
                    return { };
                }
                selected = type.id;
            }
            if (selected.valid()) {
                return selected;
            }
        }
        if (exact_source) {
            return { };
        }
        if (const auto selected = find_systemverilog_hir_type(
                model_, hir_, scope, type_name);
            selected.valid()) {
            return selected;
        }
        for (const auto& imported : current_imports_) {
            if (!imported.name.empty() && imported.name != simple_name) {
                continue;
            }
            const auto qualified = imported.package + "::"
                + std::string { simple_name };
            if (const auto selected = find_systemverilog_hir_type(
                    model_, hir_, scope, qualified);
                selected.valid()) {
                return selected;
            }
        }
        return { };
    }

    [[nodiscard]] semantic::ValueId SystemVerilogHirBuilder::find_value(
        const semantic::ScopeId scope,
        const std::string_view value_name,
        const semantic::SourceSpanId exact_source) const noexcept
    {
        for (const auto& value : model_.values()) {
            if (value.scope == scope && value.source == exact_source
                && value.name == value_name) {
                return value.id;
            }
        }
        return { };
    }

    [[nodiscard]] sv::PackedRange SystemVerilogHirBuilder::packed_range(
        const frontend::PackedRange& range,
        const semantic::SourceSpanId span) const
    {
        return {
            range.left,
            range.right,
            std::nullopt,
            std::nullopt,
            range.descending,
            span
        };
    }

    [[nodiscard]] semantic::InstanceId SystemVerilogHirBuilder::ensure_instance(
        const frontend::Instance& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent)
    {
        const auto instance_source = source(input.span);
        semantic::InstanceId id;
        for (const auto& instance : model_.instances()) {
            if (instance.scope == scope && instance.source == instance_source
                && instance.name == input.name) {
                id = instance.id;
                break;
            }
        }
        const auto instance_origin = origin(instance_source, parent, input.name);
        if (!id.valid()) {
            id = model_.add_instance(scope, input.name, input.unit_name,
                instance_source, instance_origin);
        }
        const auto retained = std::ranges::find(
            hir_.instances(), id, &sv::Instance::id);
        if (retained != hir_.instances().end()) {
            return id;
        }
        sv::Instance output;
        output.id = id;
        output.scope = scope;
        output.target = name(input.unit_name, input.span, scope);
        output.name = input.name;
        output.anonymous = input.anonymous;
        output.udp = input.udp_instance;
        output.array_indices = input.array_indices;
        for (const auto& input_parameter : input.parameter_overrides) {
            sv::ActualAssociation parameter;
            parameter.formal = input_parameter.name;
            parameter.source = source(input_parameter.span);
            if (input_parameter.default_box) {
                parameter.kind = sv::ActualKind::default_value;
            } else if (input_parameter.type_value) {
                parameter.kind = sv::ActualKind::type;
                parameter.type = type_reference(*input_parameter.type_value,
                    input_parameter.span, scope, instance_origin);
            } else {
                parameter.expression = expression(
                    input_parameter.value, scope, instance_origin);
            }
            output.parameters.push_back(std::move(parameter));
        }
        for (const auto& input_port : input.connections) {
            sv::ActualAssociation port;
            port.formal = input_port.port;
            port.source = source(input_port.span);
            if (input_port.kind == frontend::PortActualKind::Open) {
                port.kind = sv::ActualKind::open;
            } else if (input_port.kind == frontend::PortActualKind::Default) {
                port.kind = sv::ActualKind::default_value;
            } else {
                port.expression = expression(
                    input_port.value, scope, instance_origin);
            }
            output.ports.push_back(std::move(port));
        }
        if (input.udp_delay) {
            output.udp_delay = instance_delay(
                *input.udp_delay, scope, instance_origin);
        }
        if (input.drive_strength) {
            output.drive_zero = static_cast<std::uint8_t>(
                input.drive_strength->zero);
            output.drive_one = static_cast<std::uint8_t>(
                input.drive_strength->one);
        }
        using Drive = frontend::VerilogUnconnectedDrive;
        output.unconnected_drive = input.unconnected_drive == Drive::Pull0
            ? sv::UnconnectedDrive::pull_zero
            : input.unconnected_drive == Drive::Pull1
            ? sv::UnconnectedDrive::pull_one
            : sv::UnconnectedDrive::none;
        output.source = instance_source;
        output.origin = instance_origin;
        hir_.mutable_instances().push_back(std::move(output));
        return id;
    }

    [[nodiscard]] semantic::ProcessId SystemVerilogHirBuilder::add_process_skeleton(
        const frontend::Process& input,
        const semantic::ScopeId parent_scope,
        const semantic::OriginId parent_origin)
    {
        const auto process_source = source(input.span);
        const auto process_name = input.name.empty()
            ? "<process>"
            : input.name;
        const auto process_origin = origin(
            process_source, parent_origin, process_name);
        const auto process_scope = nested_scope(
            parent_scope, process_name, process_source, process_origin);
        const auto id = model_.add_process_identity(
            process_scope,
            process_name,
            process_source,
            process_origin);
        std::vector<semantic::DeclarationId> unused;
        add_children(
            input.constants,
            process_scope,
            process_origin,
            unused,
            &SystemVerilogHirBuilder::add_parameter);
        add_children(
            input.type_aliases,
            process_scope,
            process_origin,
            unused,
            &SystemVerilogHirBuilder::add_type_declaration);
        add_children(
            input.variables,
            process_scope,
            process_origin,
            unused,
            &SystemVerilogHirBuilder::add_variable);
        add_children(
            input.functions,
            process_scope,
            process_origin,
            unused,
            &SystemVerilogHirBuilder::add_function);
        return id;
    }

    [[nodiscard]] sv::Alias SystemVerilogHirBuilder::add_alias(
        const frontend::SystemVerilogAliasDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent,
        const std::size_t index)
    {
        sv::Alias output;
        output.source = source(input.span);
        output.origin = origin(
            output.source, parent,
            "$alias$" + std::to_string(index + 1U));
        for (const auto& terminal : input.terminals) {
            const auto terminal_expression = expression(
                terminal, scope, output.origin);
            if (terminal_expression) {
                output.terminals.push_back(*terminal_expression);
            }
        }
        return output;
    }

    [[nodiscard]] sv::LetDeclaration SystemVerilogHirBuilder::add_let(
        const frontend::SystemVerilogLetDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent)
    {
        sv::LetDeclaration output;
        output.name = input.name;
        output.source = source(input.span);
        output.origin = origin(output.source, parent, output.name);
        for (const auto& input_port : input.ports) {
            sv::LetPort port;
            port.name = input_port.name;
            port.source = source(input_port.span);
            if (input_port.type) {
                port.type = type_reference(
                    *input_port.type, input_port.span, scope, output.origin);
            }
            if (input_port.default_value) {
                port.default_value = expression(
                    *input_port.default_value, scope, output.origin);
            }
            output.ports.push_back(std::move(port));
        }
        const auto let_expression = expression(
            input.expression, scope, output.origin);
        if (let_expression) {
            output.expression = *let_expression;
        }
        return output;
    }

    [[nodiscard]] std::optional<sv::Defparam>
    SystemVerilogHirBuilder::add_defparam(
        const frontend::VerilogDefparamDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent)
    {
        sv::Defparam output;
        output.source = source(input.span);
        output.origin = origin(output.source, parent, "defparam");
        const auto value = expression(input.value, scope, output.origin);
        if (!value) {
            return std::nullopt;
        }
        output.value = *value;
        for (const auto& input_segment : input.path) {
            sv::DefparamPathSegment segment;
            segment.name = input_segment.name;
            segment.source = source(input_segment.span);
            for (const auto& input_index : input_segment.indices) {
                if (const auto index = expression(
                        input_index, scope, output.origin)) {
                    segment.indices.push_back(*index);
                }
            }
            output.path.push_back(std::move(segment));
        }
        return output;
    }

    void SystemVerilogHirBuilder::add_generate_body(
        const frontend::GenerateBody& input,
        sv::GenerateRegion& output)
    {
        for (std::size_t index = 0;
            index < input.systemverilog_aliases.size(); ++index) {
            output.aliases.push_back(add_alias(
                input.systemverilog_aliases[index], output.scope,
                output.origin, index));
        }
        for (const auto& input_let : input.systemverilog_lets) {
            output.lets.push_back(add_let(
                input_let, output.scope, output.origin));
        }
        std::vector<Pending> pending;
        queue_declarations(input, output.scope, output.origin, pending);
        std::stable_sort(pending.begin(), pending.end(), pending_less);
        for (auto& item : pending) {
            output.declarations.push_back(item.build());
        }
        const auto first_class = hir_.classes().size();
        add_classes(
            input.systemverilog_classes, output.scope, output.declaration,
            output.alternative_discriminator);
        for (std::size_t index = first_class;
            index < hir_.classes().size(); ++index) {
            const auto& declaration = hir_.classes()[index];
            if (declaration.generate_owner == output.declaration) {
                output.class_declarations.push_back(
                    sv::class_declaration_identity(declaration));
            }
        }
        for (const auto& instance : input.instances) {
            output.instances.push_back(ensure_instance(
                instance, output.scope, output.origin));
        }
        for (const auto& process : input.processes) {
            output.processes.push_back(add_process_skeleton(
                process, output.scope, output.origin));
        }
        for (const auto& input_defparam : input.verilog_defparams) {
            if (auto defparam = add_defparam(
                    input_defparam, output.scope, output.origin)) {
                output.defparams.push_back(std::move(*defparam));
            }
        }
        for (const auto& nested : input.generate_regions) {
            output.nested.push_back(add_generate(
                nested, output.scope, output.origin,
                output.alternative_discriminator));
        }
    }

    [[nodiscard]] sv::GenerateRegion SystemVerilogHirBuilder::add_generate(
        const frontend::GenerateRegion& input,
        const semantic::ScopeId parent_scope,
        const semantic::OriginId parent_origin,
        std::string alternative_discriminator)
    {
        const bool owns_scope = !input.then_scope.empty();
        const auto label = owns_scope
            ? input.then_scope
            : "<generate>";
        const auto declaration_id = add_declaration_record(
            parent_scope,
            sv::DeclarationForm::generated,
            semantic::DeclarationKind::generate,
            label,
            input.span,
            parent_origin);
        const auto declaration_source = declaration(declaration_id).source;
        const auto declaration_origin = declaration(declaration_id).origin;
        sv::GenerateRegion output;
        output.declaration = declaration_id;
        output.scope = owns_scope
            ? nested_scope(
                  parent_scope, label, declaration_source, declaration_origin)
            : parent_scope;
        switch (input.kind) {
        case frontend::GenerateKind::StaticBlock:
            output.kind = sv::GenerateKind::block;
            break;
        case frontend::GenerateKind::Conditional:
            output.kind = sv::GenerateKind::conditional;
            break;
        case frontend::GenerateKind::Iterative:
            output.kind = sv::GenerateKind::iterative;
            break;
        case frontend::GenerateKind::Selection:
            output.kind = sv::GenerateKind::selection;
            break;
        }
        output.label = input.then_scope;
        output.alternative_label = input.else_scope;
        output.alternative_discriminator
            = std::move(alternative_discriminator);
        output.iterator = input.variable;
        output.initial = expression(
            input.initial, output.scope, declaration_origin);
        output.condition = expression(
            input.condition, output.scope, declaration_origin);
        output.iteration = expression(
            input.iteration, output.scope, declaration_origin);
        output.source = declaration_source;
        output.origin = declaration_origin;
        if (owns_scope) {
            declaration(declaration_id).nested_scope = output.scope;
        }
        add_generate_body(input.then_body, output);
        if (!input.else_scope.empty()
            || !input.else_body.constants.empty()
            || !input.else_body.type_aliases.empty()
            || !input.else_body.signals.empty()
            || !input.else_body.variables.empty()
            || !input.else_body.functions.empty()
            || !input.else_body.tasks.empty()
            || !input.else_body.systemverilog_aliases.empty()
            || !input.else_body.systemverilog_lets.empty()
            || !input.else_body.systemverilog_classes.empty()
            || !input.else_body.concurrent_statements.empty()
            || !input.else_body.processes.empty()
            || !input.else_body.instances.empty()
            || !input.else_body.generate_regions.empty()
            || !input.else_body.verilog_defparams.empty()) {
            frontend::GenerateRegion synthetic;
            synthetic.kind = frontend::GenerateKind::StaticBlock;
            const auto alternative_scope = input.else_scope.empty()
                ? label + ".else"
                : input.else_scope;
            const bool shares_scope = owns_scope
                && alternative_scope == input.then_scope;
            synthetic.then_scope = shares_scope
                ? std::string { }
                : alternative_scope;
            synthetic.then_body = input.else_body;
            synthetic.span = input.span;
            output.nested.push_back(add_generate(
                synthetic,
                shares_scope ? output.scope : parent_scope,
                declaration_origin,
                nested_alternative_discriminator(
                    output.alternative_discriminator, "else")));
        }
        for (std::size_t alternative_index = 0;
            alternative_index < input.alternatives.size();
            ++alternative_index) {
            const auto& alternative = input.alternatives[alternative_index];
            sv::GenerateAlternative converted;
            converted.source = source(alternative.span);
            converted.label = alternative.scope;
            converted.is_default = alternative.is_default;
            for (const auto& choice : alternative.choices) {
                const auto left = expression(
                    choice.left, output.scope, declaration_origin);
                if (!left) {
                    continue;
                }
                sv::GenerateChoice converted_choice;
                converted_choice.left = *left;
                converted_choice.descending = choice.descending;
                converted_choice.source = source(choice.span);
                if (choice.right) {
                    converted_choice.right = expression(
                        *choice.right, output.scope, declaration_origin);
                }
                converted.choices.push_back(
                    std::move(converted_choice));
            }
            frontend::GenerateRegion synthetic;
            synthetic.kind = frontend::GenerateKind::StaticBlock;
            const auto alternative_scope = alternative.scope.empty()
                ? label + ".alternative"
                : alternative.scope;
            const bool shares_scope = owns_scope
                && alternative_scope == input.then_scope;
            synthetic.then_scope = shares_scope
                ? std::string { }
                : alternative_scope;
            synthetic.then_body = alternative.body;
            synthetic.span = alternative.span;
            auto nested = add_generate(
                synthetic,
                shares_scope ? output.scope : parent_scope,
                declaration_origin,
                nested_alternative_discriminator(
                    output.alternative_discriminator,
                    "case-" + std::to_string(alternative_index)));
            converted.scope = nested.scope;
            converted.alternative_discriminator
                = nested.alternative_discriminator;
            converted.class_declarations
                = nested.class_declarations;
            output.nested.push_back(std::move(nested));
            output.alternatives.push_back(std::move(converted));
        }
        declaration(declaration_id).children = output.declarations;
        return output;
    }

    template <typename Input, typename Builder>
    void SystemVerilogHirBuilder::queue(
        const std::vector<Input>& inputs,
        const std::size_t category,
        const semantic::ScopeId scope,
        const semantic::OriginId parent,
        std::vector<Pending>& pending,
        Builder builder)
    {
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            const auto& item = inputs[index];
            pending.push_back({ frontend::physical_source(item.span),
                item.span.begin.offset,
                category,
                index,
                [this, item_ptr = &item, builder, scope, parent] {
                    return (this->*builder)(*item_ptr, scope, parent);
                } });
        }
    }

    void SystemVerilogHirBuilder::queue_declarations(
        const frontend::GenerateBody& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent,
        std::vector<Pending>& pending)
    {
        queue(input.constants, 0, scope, parent, pending,
            &SystemVerilogHirBuilder::add_parameter);
        queue(input.type_aliases, 1, scope, parent, pending,
            &SystemVerilogHirBuilder::add_type_declaration);
        queue(input.signals, 2, scope, parent, pending,
            &SystemVerilogHirBuilder::add_signal);
        queue(input.variables, 3, scope, parent, pending,
            &SystemVerilogHirBuilder::add_variable);
        queue(input.functions, 4, scope, parent, pending,
            &SystemVerilogHirBuilder::add_function);
        queue(input.tasks, 5, scope, parent, pending,
            &SystemVerilogHirBuilder::add_task);
    }

    bool SystemVerilogHirBuilder::pending_less(
        const Pending& left,
        const Pending& right) noexcept
    {
        return std::tuple {
            left.physical_source, left.offset, left.category, left.index
        }
        < std::tuple {
              right.physical_source,
              right.offset,
              right.category,
              right.index
          };
    }

    void SystemVerilogHirBuilder::add_unit(
        const frontend::DesignUnit& input,
        const semantic::UnitId unit_id)
    {
        current_imports_ = std::span { input.systemverilog_imports };
        const auto& common = model_.units()[unit_id.value()];
        sv::Unit output;
        output.id = unit_id;
        output.scope = common.scope;
        output.kind = unit_kind(input.kind);
        output.external = input.systemverilog_extern;
        output.library = input.library;
        output.name = input.name;
        output.source = common.source;
        output.origin = common.origin;
        output.compilation_unit_identity = input.compilation_unit_identity;
        output.standard = std::string {
            frontend::revision_string(input.standard_revision)
        };
        output.compatibility_profile = input.verilog_compatibility_profile;
        output.source_dependencies = input.source_dependencies;
        if (input.systemverilog_scheduling_declaration) {
            const auto& scheduling = *input.systemverilog_scheduling_declaration;
            output.scheduling_declaration = sv::DesignSchedulingDeclaration {
                scheduling.process_region
                        == frontend::SystemVerilogProcessRegion::Reactive
                    ? sv::DesignProcessRegion::reactive
                    : sv::DesignProcessRegion::active,
                scheduling.prototype,
                source(scheduling.span)
            };
        }
        if (input.systemverilog_standard_package) {
            output.standard_package = sv::StandardPackageProvenance {
                input.systemverilog_standard_package->revision,
                input.systemverilog_standard_package->declaration_identity
            };
        }
        output.compilation = {
            input.time_unit,
            input.time_precision,
            input.default_nettype,
            input.is_cell
        };
        if (input.systemverilog_configuration) {
            const auto& input_configuration
                = *input.systemverilog_configuration;
            sv::ConfigurationDeclaration configuration;
            configuration.source = source(input_configuration.span);
            configuration.origin = origin(configuration.source,
                output.origin, "$configuration$" + input.name);
            configuration.default_liblist
                = input_configuration.default_liblist;
            for (const auto& input_design : input_configuration.designs) {
                const auto design_source = source(input_design.span);
                configuration.designs.push_back({
                    input_design.library,
                    input_design.cell,
                    std::nullopt,
                    design_source,
                    origin(design_source, configuration.origin,
                        "$design$" + input_design.cell),
                });
            }
            for (const auto& input_rule : input_configuration.rules) {
                const auto rule_source = source(input_rule.span);
                configuration.rules.push_back({
                    input_rule.kind
                            == frontend::SystemVerilogConfigurationRuleKind::Instance
                        ? sv::ConfigurationRuleKind::instance
                        : sv::ConfigurationRuleKind::cell,
                    input_rule.selection
                            == frontend::SystemVerilogConfigurationSelectionKind::Use
                        ? sv::ConfigurationSelectionKind::use
                        : sv::ConfigurationSelectionKind::liblist,
                    input_rule.selector,
                    input_rule.use_library,
                    input_rule.use_cell,
                    input_rule.use_configuration,
                    input_rule.liblist,
                    std::nullopt,
                    rule_source,
                    origin(rule_source, configuration.origin,
                        "$rule$" + input_rule.selector),
                });
            }
            output.configuration = std::move(configuration);
        }
        for (const auto& input_import : input.systemverilog_imports) {
            output.imports.push_back({ name(input_import.package, input_import.span, output.scope),
                input_import.name.empty()
                    ? std::nullopt
                    : std::optional<sv::Name> { name(
                          input_import.name, input_import.span, output.scope) },
                input_import.name.empty(),
                source(input_import.span) });
        }
        for (const auto& input_export : input.systemverilog_exports) {
            output.exports.push_back({ name(input_export.package, input_export.span, output.scope),
                input_export.name.empty()
                    ? std::nullopt
                    : std::optional<sv::Name> { name(
                          input_export.name, input_export.span, output.scope) },
                input_export.name.empty(),
                source(input_export.span) });
        }
        for (std::size_t index = 0;
            index < input.systemverilog_aliases.size(); ++index) {
            output.aliases.push_back(add_alias(
                input.systemverilog_aliases[index], output.scope,
                output.origin, index));
        }
        for (const auto& input_let : input.systemverilog_lets) {
            output.lets.push_back(add_let(
                input_let, output.scope, output.origin));
        }

        std::vector<Pending> pending;
        queue(input.parameters, 0, output.scope, output.origin, pending,
            &SystemVerilogHirBuilder::add_parameter);
        queue(input.ports, 1, output.scope, output.origin, pending,
            &SystemVerilogHirBuilder::add_signal);
        queue(input.type_aliases, 2, output.scope, output.origin, pending,
            &SystemVerilogHirBuilder::add_type_declaration);
        queue(input.signals, 3, output.scope, output.origin, pending,
            &SystemVerilogHirBuilder::add_signal);
        queue(input.variables, 4, output.scope, output.origin, pending,
            &SystemVerilogHirBuilder::add_variable);
        queue(input.functions, 5, output.scope, output.origin, pending,
            &SystemVerilogHirBuilder::add_function);
        queue(input.tasks, 6, output.scope, output.origin, pending,
            &SystemVerilogHirBuilder::add_task);
        queue(input.systemverilog_modports, 7, output.scope, output.origin, pending,
            &SystemVerilogHirBuilder::add_modport);
        std::stable_sort(pending.begin(), pending.end(), pending_less);
        for (auto& item : pending) {
            output.declarations.push_back(item.build());
        }
        for (const auto& input_modport : input.systemverilog_modports) {
            const auto declaration_id = std::ranges::find_if(
                hir_.declarations(), [&](const sv::Declaration& declaration) {
                    return declaration.scope == output.scope
                        && declaration.form == sv::DeclarationForm::modport
                        && declaration.name == input_modport.name
                        && declaration.source == source(input_modport.span);
                })->id;
            sv::Modport modport;
            modport.declaration = declaration_id;
            modport.name = input_modport.name;
            modport.source = source(input_modport.span);
            modport.origin = declaration(declaration_id).origin;
            for (const auto& input_member : input_modport.members) {
                sv::ModportMemberKind kind = sv::ModportMemberKind::signal;
                switch (input_member.kind) {
                case frontend::SystemVerilogModportMemberKind::Signal:
                    kind = sv::ModportMemberKind::signal;
                    break;
                case frontend::SystemVerilogModportMemberKind::FunctionImport:
                    kind = sv::ModportMemberKind::function_import;
                    break;
                case frontend::SystemVerilogModportMemberKind::FunctionExport:
                    kind = sv::ModportMemberKind::function_export;
                    break;
                case frontend::SystemVerilogModportMemberKind::TaskImport:
                    kind = sv::ModportMemberKind::task_import;
                    break;
                case frontend::SystemVerilogModportMemberKind::TaskExport:
                    kind = sv::ModportMemberKind::task_export;
                    break;
                case frontend::SystemVerilogModportMemberKind::Clocking:
                    kind = sv::ModportMemberKind::clocking;
                    break;
                }
                modport.members.push_back({ kind,
                    name(input_member.name, input_member.span, output.scope),
                    direction(input_member.direction),
                    source(input_member.span) });
            }
            output.modports.push_back(std::move(modport));
        }
        for (const auto& instance : input.instances) {
            output.instances.push_back(ensure_instance(
                instance, output.scope, output.origin));
        }
        for (const auto& input_defparam : input.verilog_defparams) {
            if (auto defparam = add_defparam(
                    input_defparam, output.scope, output.origin)) {
                output.defparams.push_back(std::move(*defparam));
            }
        }
        for (const auto& input_bind : input.systemverilog_binds) {
            sv::BindDirective bind;
            bind.target = name(input_bind.target, input_bind.span, output.scope);
            bind.source = source(input_bind.span);
            bind.origin = origin(bind.source, output.origin, "bind");
            for (const auto& input_instance : input_bind.instances) {
                bind.instances.push_back(ensure_instance(
                    input_instance, output.scope, bind.origin));
            }
            output.binds.push_back(std::move(bind));
        }
        for (std::size_t index = 0;
            index < input.verilog_specify_blocks.size(); ++index) {
            output.timing.push_back(timing_record(
                input.verilog_specify_blocks[index],
                output.scope,
                output.origin,
                index));
        }
        for (const auto& input_coverage : input.systemverilog_covergroups) {
            output.coverage.push_back(covergroup_declaration(
                input_coverage, output.scope, output.origin));
        }
        for (const auto& input_dpi : input.systemverilog_dpi_declarations) {
            output.dpi_declarations.push_back(dpi_declaration(
                input_dpi, output.scope, output.origin));
        }
        for (const auto& input_clocking :
            input.systemverilog_clocking_blocks) {
            output.clocking_blocks.push_back(clocking_block(
                input_clocking, output.scope, output.origin));
        }
        if (input.systemverilog_default_clocking_block) {
            const auto default_source = source(
                input.systemverilog_default_clocking_span);
            output.default_clocking = sv::DefaultClockingReference {
                name(*input.systemverilog_default_clocking_block,
                    input.systemverilog_default_clocking_span,
                    output.scope),
                default_source,
                origin(default_source, output.origin, "default clocking")
            };
        }
        for (const auto& input_assertion :
            input.systemverilog_assertion_declarations) {
            output.assertion_declarations.push_back(assertion_declaration(
                input_assertion, output.scope, output.origin));
        }
        for (const auto& input_checker :
            input.systemverilog_checker_instances) {
            sv::CheckerInstance checker;
            checker.declaration = name(
                input_checker.declaration_name,
                input_checker.span,
                output.scope);
            checker.name = input_checker.name;
            checker.source = source(input_checker.span);
            checker.origin = origin(
                checker.source, output.origin, input_checker.name);
            for (const auto& input_connection : input_checker.connections) {
                checker.connections.push_back({ input_connection.formal_name,
                    source_tokens(input_connection.actual_tokens),
                    input_connection.open,
                    source(input_connection.span) });
            }
            output.checker_instances.push_back(std::move(checker));
        }
        for (const auto& process : input.processes) {
            output.processes.push_back(add_process_skeleton(
                process, output.scope, output.origin));
        }
        for (std::size_t index = 0;
            index < input.systemverilog_concurrent_assertions.size();
            ++index) {
            const auto& input_assertion = input.systemverilog_concurrent_assertions[index];
            output.concurrent_assertions.push_back(concurrent_assertion(
                input_assertion, index, output.origin));
        }
        for (const auto& generate : input.generate_regions) {
            output.generates.push_back(add_generate(
                generate, output.scope, output.origin));
        }
        hir_.mutable_units().push_back(std::move(output));
        current_imports_ = { };
    }

} // namespace systemverilog_hir_detail

semantic::sv::Hir build_systemverilog_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    const std::span<frontend::SystemVerilogClassSpecialization>
        class_specializations)
{
    semantic::sv::Hir result;
    systemverilog_hir_detail::SystemVerilogHirBuilder builder {
        semantics, result, class_specializations
    };
    builder.add_design(parsed);
    complete_systemverilog_executable_hir(parsed, semantics, result);
    for (auto& specialization : class_specializations) {
        specialization.constraint_modes.clear();
        const auto declaration = std::ranges::find(
            result.classes(),
            specialization.declaration_identity,
            &sv::ClassDeclaration::canonical_identity);
        if (declaration == result.classes().end())
            continue;
        for (const auto& constraint : declaration->composed_constraints) {
            if (constraint.override_legal) {
                specialization.constraint_modes.emplace_back(
                    constraint.selected_identity, constraint.mode_enabled);
            }
        }
    }
    return result;
}

} // namespace fsim::app::application_detail
