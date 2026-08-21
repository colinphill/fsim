// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <boost/pfr/core.hpp>

#include <map>
#include <type_traits>

namespace fsim::elaboration::elaboration_detail {
namespace {

    template <typename T>
    struct IsVector : std::false_type { };

    template <typename T, typename Allocator>
    struct IsVector<std::vector<T, Allocator>> : std::true_type { };

    template <typename T>
    struct IsVector<support::RareVector<T>> : std::true_type { };

    template <typename T>
    struct IsOptional : std::false_type { };

    template <typename T>
    struct IsOptional<std::optional<T>> : std::true_type { };

    template <typename T>
    struct IsOptional<support::RareOptional<T>> : std::true_type { };

    template <typename T>
    struct IsPair : std::false_type { };

    template <typename First, typename Second>
    struct IsPair<std::pair<First, Second>> : std::true_type { };

    class LetExpander {
    public:
        LetExpander(
            const std::vector<frontend::SystemVerilogLetDeclaration>& declarations,
            std::vector<Diagnostic>& diagnostics)
            : diagnostics_(diagnostics)
        {
            for (const auto& declaration : declarations) {
                declarations_.try_emplace(declaration.name, &declaration);
            }
        }

        void expand(Expression& expression)
        {
            std::vector<std::string> stack;
            expand(expression, stack);
        }

    private:
        using Actuals = std::map<std::string, Expression>;

        void report(
            std::string code,
            std::string message,
            const frontend::SourceSpan& span)
        {
            diagnostics_.push_back(
                { std::move(code), std::move(message), span });
        }

        void replace_formals(Expression& expression, const Actuals& actuals)
        {
            if (expression.kind == ExpressionKind::Identifier) {
                if (const auto found = actuals.find(expression.text);
                    found != actuals.end()) {
                    const auto use_span = expression.span;
                    expression = found->second;
                    expression.span = use_span;
                    return;
                }
            }
            for (auto& association : expression.aggregate_choice_expressions) {
                for (auto& choice : association) {
                    replace_formals(choice, actuals);
                }
            }
            for (auto& operand : expression.operands) {
                replace_formals(operand, actuals);
            }
        }

        std::optional<Actuals> bind_actuals(
            const frontend::SystemVerilogLetDeclaration& declaration,
            const Expression& call,
            std::vector<std::string>& stack)
        {
            Actuals actuals;
            std::size_t positional = 0;
            bool saw_named = false;
            for (std::size_t index = 0; index < call.operands.size(); ++index) {
                const auto name = index < call.call_argument_names.size()
                    ? call.call_argument_names[index]
                    : std::string { };
                const frontend::SystemVerilogLetPort* formal = nullptr;
                if (name.empty()) {
                    if (saw_named || positional >= declaration.ports.size()) {
                        report(
                            "FSIM-ELAB-SVLET-001",
                            "let invocation '" + declaration.name
                                + "' has an invalid positional argument",
                            call.span);
                        return std::nullopt;
                    }
                    formal = &declaration.ports[positional++];
                } else {
                    saw_named = true;
                    const auto found = std::ranges::find(
                        declaration.ports, name,
                        &frontend::SystemVerilogLetPort::name);
                    if (found == declaration.ports.end()) {
                        report(
                            "FSIM-ELAB-SVLET-001",
                            "let invocation '" + declaration.name
                                + "' has no formal named '" + name + "'",
                            call.span);
                        return std::nullopt;
                    }
                    formal = &*found;
                }
                if (!actuals.emplace(formal->name, call.operands[index]).second) {
                    report(
                        "FSIM-ELAB-SVLET-001",
                        "let invocation '" + declaration.name
                            + "' binds formal '" + formal->name + "' more than once",
                        call.span);
                    return std::nullopt;
                }
            }
            for (const auto& formal : declaration.ports) {
                if (actuals.contains(formal.name)) {
                    continue;
                }
                if (!formal.default_value) {
                    report(
                        "FSIM-ELAB-SVLET-003",
                        "let invocation '" + declaration.name
                            + "' omits required formal '" + formal.name + "'",
                        call.span);
                    return std::nullopt;
                }
                auto value = *formal.default_value;
                replace_formals(value, actuals);
                expand(value, stack);
                actuals.emplace(formal.name, std::move(value));
            }
            return actuals;
        }

        void expand(Expression& expression, std::vector<std::string>& stack)
        {
            for (auto& association : expression.aggregate_choice_expressions) {
                for (auto& choice : association) {
                    expand(choice, stack);
                }
            }
            for (auto& operand : expression.operands) {
                expand(operand, stack);
            }

            const bool call = expression.kind == ExpressionKind::Call;
            const bool identifier = expression.kind == ExpressionKind::Identifier;
            if (!call && !identifier) {
                return;
            }
            const auto found = declarations_.find(expression.text);
            if (found == declarations_.end()) {
                return;
            }
            const auto& declaration = *found->second;
            if (identifier && !declaration.ports.empty()) {
                return;
            }
            if (std::ranges::find(stack, declaration.name) != stack.end()) {
                report(
                    "FSIM-ELAB-SVLET-002",
                    "recursive let expansion through '" + declaration.name + "'",
                    expression.span);
                return;
            }
            if (++expansion_count_ > maximum_expansions) {
                report(
                    "FSIM-ELAB-SVLET-004",
                    "SystemVerilog let expansion exceeded the governed per-unit work budget",
                    expression.span);
                return;
            }

            stack.push_back(declaration.name);
            auto actuals = bind_actuals(declaration, expression, stack);
            if (!actuals) {
                stack.pop_back();
                return;
            }
            auto expanded = declaration.expression;
            replace_formals(expanded, *actuals);
            expanded.span = expression.span;
            expand(expanded, stack);
            expression = std::move(expanded);
            stack.pop_back();
        }

        static constexpr std::size_t maximum_expansions = 1U << 20U;
        std::map<std::string, const frontend::SystemVerilogLetDeclaration*>
            declarations_;
        std::vector<Diagnostic>& diagnostics_;
        std::size_t expansion_count_ { };
    };

    template <typename T>
    void walk_expressions(T& value, LetExpander& expander)
    {
        using Value = std::remove_cvref_t<T>;
        if constexpr (std::same_as<Value, frontend::Expression>) {
            expander.expand(value);
        } else if constexpr (IsVector<Value>::value) {
            for (auto& element : value) {
                walk_expressions(element, expander);
            }
        } else if constexpr (IsOptional<Value>::value) {
            if (value) {
                walk_expressions(*value, expander);
            }
        } else if constexpr (IsPair<Value>::value) {
            walk_expressions(value.first, expander);
            walk_expressions(value.second, expander);
        } else if constexpr (
            std::is_aggregate_v<Value>
            && !std::same_as<Value, std::string>) {
            boost::pfr::for_each_field(
                value,
                [&](auto& field) { walk_expressions(field, expander); });
        }
    }

    void expand_generate_regions(
        std::vector<frontend::GenerateRegion>& regions,
        const std::vector<frontend::SystemVerilogLetDeclaration>& inherited,
        std::vector<Diagnostic>& diagnostics);

    void expand_generate_body(
        frontend::GenerateBody& body,
        const std::vector<frontend::SystemVerilogLetDeclaration>& inherited,
        std::vector<Diagnostic>& diagnostics)
    {
        auto declarations = std::move(body.systemverilog_lets);
        body.systemverilog_lets.clear();
        std::vector<frontend::SystemVerilogLetDeclaration> visible = declarations;
        for (const auto& declaration : inherited) {
            if (std::ranges::none_of(
                    visible, [&](const auto& candidate) {
                        return candidate.name == declaration.name;
                    })) {
                visible.push_back(declaration);
            }
        }
        auto nested = std::move(body.generate_regions);
        body.generate_regions.clear();
        if (!visible.empty()) {
            LetExpander expander { visible, diagnostics };
            walk_expressions(body, expander);
        }
        expand_generate_regions(nested, visible, diagnostics);
        body.generate_regions = std::move(nested);
        body.systemverilog_lets = std::move(declarations);
    }

    void expand_generate_regions(
        std::vector<frontend::GenerateRegion>& regions,
        const std::vector<frontend::SystemVerilogLetDeclaration>& inherited,
        std::vector<Diagnostic>& diagnostics)
    {
        for (auto& region : regions) {
            expand_generate_body(region.then_body, inherited, diagnostics);
            expand_generate_body(region.else_body, inherited, diagnostics);
            for (auto& alternative : region.alternatives) {
                expand_generate_body(
                    alternative.body, inherited, diagnostics);
            }
        }
    }

} // namespace

void expand_systemverilog_lets(
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics)
{
    auto declarations = std::move(unit.systemverilog_lets);
    unit.systemverilog_lets.clear();
    auto generate_regions = std::move(unit.generate_regions);
    unit.generate_regions.clear();
    if (!declarations.empty()) {
        LetExpander expander { declarations, diagnostics };
        walk_expressions(unit, expander);
    }
    expand_generate_regions(
        generate_regions, declarations, diagnostics);
    unit.generate_regions = std::move(generate_regions);
    unit.systemverilog_lets = std::move(declarations);
}

} // namespace fsim::elaboration::elaboration_detail
