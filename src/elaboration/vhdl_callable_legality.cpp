// SPDX-License-Identifier: Apache-2.0
#include "vhdl_callable_legality.hpp"

#include <unordered_set>

namespace fsim::elaboration {
namespace {

    using Failure = VhdlCallableLegalityFailure;

    semantic::vhdl::ValueDomain expression_domain(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::ExpressionId expression_id)
    {
        const auto expression = specialization.find_expression(
            expression_id);
        if (!expression || expression->vhdl == nullptr) {
            return semantic::vhdl::ValueDomain::unknown;
        }
        using Kind = semantic::vhdl::ExpressionKind;
        switch (expression->vhdl->kind) {
        case Kind::integer_literal:
            return semantic::vhdl::ValueDomain::integer;
        case Kind::boolean_literal:
            return semantic::vhdl::ValueDomain::boolean;
        case Kind::logic_literal:
            return semantic::vhdl::ValueDomain::bit2;
        case Kind::string_literal:
            return semantic::vhdl::ValueDomain::string;
        default:
            break;
        }
        const auto selected = expression->vhdl->referenced_name
            ? expression->vhdl->referenced_name->selected
            : std::nullopt;
        const auto declaration = selected
            ? specialization.find_declaration(*selected)
            : std::nullopt;
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->subtype) {
            return declaration->vhdl->subtype->domain;
        }
        return semantic::vhdl::ValueDomain::unknown;
    }

    bool default_matches_formal(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Declaration& formal)
    {
        if (!formal.initializer || !formal.subtype) {
            return true;
        }
        const auto actual = expression_domain(
            specialization, *formal.initializer);
        const auto expected = formal.subtype->domain;
        return actual == semantic::vhdl::ValueDomain::unknown
            || expected == semantic::vhdl::ValueDomain::unknown
            || actual == expected;
    }

    class Validator final {
    public:
        explicit Validator(
            const semantic::SpecializedHirUnit& specialization)
            : specialization_ { specialization }
        {
        }

        std::vector<Failure> validate(
            const std::span<const semantic::DeclarationId> declarations)
        {
            for (const auto declaration : declarations) {
                validate_declaration(declaration);
            }
            return std::move(failures_);
        }

    private:
        void validate_declaration(
            const semantic::DeclarationId declaration_id)
        {
            if (!visited_declarations_.insert(
                    declaration_id.value()).second) {
                return;
            }
            const auto declaration = specialization_.find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                return;
            }
            const auto& source = *declaration->vhdl;
            if (source.callable && source.callable->defined) {
                validate_defaults(source);
                if (source.callable->function
                    && source.callable->pure) {
                    validate_purity(source);
                }
            }
            for (const auto child : source.children) {
                validate_declaration(child);
            }
        }

        void validate_defaults(
            const semantic::vhdl::Declaration& callable)
        {
            for (const auto formal_id : callable.callable->formals) {
                const auto formal = specialization_.find_declaration(
                    formal_id);
                if (!formal || formal->vhdl == nullptr
                    || !formal->vhdl->initializer
                    || default_matches_formal(
                        specialization_, *formal->vhdl)) {
                    continue;
                }
                const auto initializer = specialization_.find_expression(
                    *formal->vhdl->initializer);
                failures_.push_back({
                    callable.callable->function
                        ? "FSIM-ELAB-VHLEGAL-007"
                        : "FSIM-ELAB-VHLEGAL-008",
                    "default for VHDL "
                        + std::string {
                            callable.callable->function
                                ? "function"
                                : "procedure"
                        }
                        + " formal '" + formal->vhdl->name
                        + "' does not match its subtype",
                    initializer && initializer->vhdl != nullptr
                        ? initializer->vhdl->source
                        : formal->vhdl->source,
                });
            }
        }

        void validate_purity(
            const semantic::vhdl::Declaration& function)
        {
            std::unordered_set<std::uint32_t> formals;
            for (const auto formal : function.callable->formals) {
                formals.insert(formal.value());
            }
            bool reads_signal { };
            std::string signal_name;
            bool calls_procedure { };
            std::unordered_set<std::uint32_t> statements;
            std::unordered_set<std::uint32_t> expressions;
            const auto inspect_expression = [&](
                                                const auto& self,
                                                const semantic::ExpressionId expression_id) -> void {
                if (!expressions.insert(expression_id.value()).second) {
                    return;
                }
                const auto expression = specialization_.find_expression(
                    expression_id);
                if (!expression || expression->vhdl == nullptr) {
                    return;
                }
                const auto selected = expression->vhdl->referenced_name
                    ? expression->vhdl->referenced_name->selected
                    : std::nullopt;
                const auto declaration = selected
                    ? specialization_.find_declaration(*selected)
                    : std::nullopt;
                if (declaration && declaration->vhdl != nullptr
                    && !formals.contains(declaration->vhdl->id.value())
                    && (declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::signal
                        || declaration->vhdl->object_class
                            == semantic::vhdl::ObjectClass::signal)) {
                    reads_signal = true;
                    signal_name = declaration->vhdl->name;
                }
                for (const auto operand : expression->vhdl->operands) {
                    self(self, operand);
                }
                for (const auto& association :
                    expression->vhdl->associations) {
                    self(self, association.value);
                    for (const auto choice : association.choices) {
                        self(self, choice);
                    }
                }
            };
            const auto inspect_statement = [&](
                                               const auto& self,
                                               const semantic::StatementId statement_id) -> void {
                if (!statements.insert(statement_id.value()).second) {
                    return;
                }
                const auto statement = specialization_.find_statement(
                    statement_id);
                if (!statement || statement->vhdl == nullptr) {
                    return;
                }
                const auto& record = *statement->vhdl;
                calls_procedure = calls_procedure
                    || record.kind
                        == semantic::vhdl::StatementKind::procedure_call;
                const auto inspect_optional = [&](const auto expression) {
                    if (expression) {
                        inspect_expression(inspect_expression, *expression);
                    }
                };
                inspect_optional(record.target);
                inspect_optional(record.value);
                inspect_optional(record.condition);
                inspect_optional(record.loop_initial);
                inspect_optional(record.loop_limit);
                inspect_optional(record.guard);
                inspect_optional(record.report);
                inspect_optional(record.severity);
                for (const auto& actual : record.procedure_arguments) {
                    inspect_expression(inspect_expression, actual.actual);
                }
                for (const auto& element : record.waveform) {
                    inspect_expression(inspect_expression, element.value);
                }
                for (const auto nested : record.statements) {
                    self(self, nested);
                }
                for (const auto nested : record.else_statements) {
                    self(self, nested);
                }
                for (const auto& alternative : record.alternatives) {
                    for (const auto choice : alternative.choices) {
                        inspect_expression(inspect_expression, choice);
                    }
                    for (const auto nested : alternative.statements) {
                        self(self, nested);
                    }
                }
            };
            for (const auto statement : function.statements) {
                inspect_statement(inspect_statement, statement);
            }
            if (reads_signal) {
                failures_.push_back({
                    "FSIM-ELAB-VHLEGAL-005",
                    "pure VHDL function '" + function.name
                        + "' reads signal '" + signal_name + "'",
                    function.source,
                });
            }
            if (calls_procedure) {
                failures_.push_back({
                    "FSIM-ELAB-VHLEGAL-006",
                    "pure VHDL function '" + function.name
                        + "' calls a procedure",
                    function.source,
                });
            }
        }

        const semantic::SpecializedHirUnit& specialization_;
        std::unordered_set<std::uint32_t> visited_declarations_;
        std::vector<Failure> failures_;
    };

} // namespace

std::vector<VhdlCallableLegalityFailure>
validate_vhdl_callable_legality(
    const semantic::SpecializedHirUnit& specialization,
    const std::span<const semantic::DeclarationId> declarations)
{
    return Validator { specialization }.validate(declarations);
}

} // namespace fsim::elaboration
