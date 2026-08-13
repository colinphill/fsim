// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_randomize.hpp"
#include "fsim/runtime/constraint_solver.hpp"
#include "fsim/runtime/scope_randomize.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::runtime {

namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition)
            throw std::runtime_error(std::string { message });
    }

    fsim::runtime::PackedLogic4 value(
        const std::size_t width,
        const std::uint64_t number)
    {
        return fsim::runtime::PackedLogic4::from_aval_bval(
            width, number, 0);
    }

    fsim::runtime::SystemVerilogConstraintVariable variable(
        std::string identity,
        const fsim::runtime::SystemVerilogConstraintDomainKind kind,
        const std::size_t width,
        std::string nominal_type,
        std::initializer_list<std::uint64_t> domain,
        const bool signed_value = false)
    {
        fsim::runtime::SystemVerilogConstraintVariable result;
        result.canonical_identity = std::move(identity);
        result.profile = { kind, width, signed_value, std::move(nominal_type) };
        for (const auto item : domain)
            result.domain.push_back(value(width, item));
        return result;
    }

} // namespace

void test_systemverilog_constraint_solver()
{
    using namespace fsim::runtime;
    SystemVerilogConstraintSolver solver;
    const auto bits = solver.add_variable(variable(
        "work::packet::bits",
        SystemVerilogConstraintDomainKind::BitVector,
        2,
        "logic[1:0]",
        { 0, 1, 2 }));
    const auto enumeration = solver.add_variable(variable(
        "work::packet::mode",
        SystemVerilogConstraintDomainKind::Enumeration,
        2,
        "work::mode_t",
        { 0, 1, 2 }));
    const auto integer = solver.add_variable(variable(
        "work::packet::count",
        SystemVerilogConstraintDomainKind::Integer,
        64,
        "integer",
        { 7, 9 },
        true));
    const auto equals = [](const auto id, const std::uint64_t expected) {
        return [=](const SystemVerilogConstraintAssignment& assignment) {
            if (!assignment.assigned(id)) {
                return SystemVerilogConstraintClauseState::Undetermined;
            }
            return assignment.value(id).low_word().aval == expected
                ? SystemVerilogConstraintClauseState::Satisfied
                : SystemVerilogConstraintClauseState::Violated;
        };
    };
    solver.add_clause({ "bits-equal-two", { bits }, equals(bits, 2) });
    solver.add_clause(
        { "mode-equal-one", { enumeration }, equals(enumeration, 1) });
    solver.add_clause({ "count-equal-nine", { integer }, equals(integer, 9) });
    const auto solved = solver.solve();
    const auto replayed = solver.solve();
    require(
        solved.status == SystemVerilogConstraintSolveStatus::Satisfied
            && solved.exhausted_resource
                == SystemVerilogConstraintResource::None
            && solved.values.size() == 3
            && solved.values[bits].low_word().aval == 2
            && solved.values[enumeration].low_word().aval == 1
            && solved.values[integer].low_word().aval == 9
            && solved.search_steps == 7
            && replayed.status == solved.status
            && replayed.values == solved.values
            && replayed.search_steps == solved.search_steps
            && replayed.clause_evaluations == solved.clause_evaluations,
        "the finite-domain solver must deterministically backtrack across exact bit-vector, integer, and enum profiles");
    require(
        solver.variables()[integer].profile.signed_value
            && solver.variables()[enumeration].profile.nominal_type
                == "work::mode_t",
        "constraint variables must preserve signedness and nominal type identity");

    SystemVerilogConstraintSolver unsatisfiable;
    const auto impossible = unsatisfiable.add_variable(variable(
        "impossible", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0, 1 }));
    unsatisfiable.add_clause(
        { "never", { impossible },
            [](const SystemVerilogConstraintAssignment& assignment) {
                return assignment.assigned(0)
                    ? SystemVerilogConstraintClauseState::Violated
                    : SystemVerilogConstraintClauseState::Undetermined;
            } });
    const auto no_solution = unsatisfiable.solve();
    require(
        no_solution.status
                == SystemVerilogConstraintSolveStatus::Unsatisfiable
            && no_solution.values.empty()
            && no_solution.search_steps == 2,
        "an unsatisfiable finite domain must terminate without publishing values");

    SystemVerilogConstraintSolver search_limited { { 10, 10, 10, 1 } };
    const auto limited = search_limited.add_variable(variable(
        "limited", SystemVerilogConstraintDomainKind::Integer,
        64, "integer", { 0, 1 }));
    search_limited.add_clause(
        { "requires-one", { limited }, equals(limited, 1) });
    const auto search_exhausted = search_limited.solve();
    require(
        search_exhausted.status
                == SystemVerilogConstraintSolveStatus::ResourceExhausted
            && search_exhausted.exhausted_resource
                == SystemVerilogConstraintResource::SearchSteps
            && search_exhausted.values.empty()
            && search_exhausted.search_steps == 1,
        "search exhaustion must be explicit and must not expose a partial assignment");

    SystemVerilogConstraintSolver elapsed_limited({ 10, 10, 10, 10, std::chrono::steady_clock::duration::zero() });
    (void)elapsed_limited.add_variable(variable(
        "elapsed", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0 }));
    const auto elapsed_exhausted = elapsed_limited.solve();
    require(
        elapsed_exhausted.status
                == SystemVerilogConstraintSolveStatus::ResourceExhausted
            && elapsed_exhausted.exhausted_resource
                == SystemVerilogConstraintResource::ElapsedWork
            && elapsed_exhausted.search_steps == 0,
        "elapsed-work exhaustion must be distinct from the deterministic search budget");

    SystemVerilogConstraintSolver registration_limited({ 1, 1, 2 });
    (void)registration_limited.add_variable(variable(
        "first", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0, 1 }));
    bool variable_budget = false;
    try {
        (void)registration_limited.add_variable(variable(
            "second", SystemVerilogConstraintDomainKind::BitVector,
            1, "bit", { 0 }));
    } catch (const SystemVerilogConstraintResourceError& error) {
        variable_budget = error.resource()
            == SystemVerilogConstraintResource::Variables;
    }
    registration_limited.add_clause(
        { "first-clause", { 0 }, equals(0, 0) });
    bool clause_budget = false;
    try {
        registration_limited.add_clause(
            { "second-clause", { 0 }, equals(0, 1) });
    } catch (const SystemVerilogConstraintResourceError& error) {
        clause_budget = error.resource()
            == SystemVerilogConstraintResource::Clauses;
    }
    SystemVerilogConstraintSolver domain_limited({ 2, 2, 1 });
    bool domain_budget = false;
    try {
        (void)domain_limited.add_variable(variable(
            "too-wide-domain", SystemVerilogConstraintDomainKind::BitVector,
            1, "bit", { 0, 1 }));
    } catch (const SystemVerilogConstraintResourceError& error) {
        domain_budget = error.resource()
            == SystemVerilogConstraintResource::DomainValues;
    }
    require(
        variable_budget && clause_budget && domain_budget
            && registration_limited.variables().size() == 1
            && registration_limited.clauses().size() == 1
            && domain_limited.variables().empty(),
        "variable, clause, and domain registration budgets must fail transactionally");

    bool invalid_dependency = false;
    try {
        domain_limited.add_clause(
            { "invalid", { 0 }, equals(0, 0) });
    } catch (const std::invalid_argument&) {
        invalid_dependency = true;
    }
    auto wrong_width = variable(
        "wrong-width", SystemVerilogConstraintDomainKind::Enumeration,
        2, "work::mode_t", { 0 });
    wrong_width.domain.front() = value(1, 0);
    bool invalid_profile = false;
    try {
        (void)domain_limited.add_variable(std::move(wrong_width));
    } catch (const std::invalid_argument&) {
        invalid_profile = true;
    }
    require(
        invalid_dependency && invalid_profile
            && domain_limited.variables().empty()
            && domain_limited.clauses().empty(),
        "invalid solver profiles and dependency graphs must reject before publication");

    SystemVerilogConstraintSolver corrupted_solver;
    const auto corrupted_variable = corrupted_solver.add_variable(variable(
        "corrupted", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0 }));
    corrupted_solver.add_clause({ "corrupted-clause", { corrupted_variable },
        [](const SystemVerilogConstraintAssignment&) {
            return SystemVerilogConstraintClauseState::Undetermined;
        } });
    bool corrupted_solver_failed = false;
    try {
        (void)corrupted_solver.solve();
    } catch (const std::logic_error&) {
        corrupted_solver_failed = true;
    }
    require(
        corrupted_solver_failed
            && corrupted_solver.variables().size() == 1
            && corrupted_solver.clauses().size() == 1,
        "a corrupt clause that remains undetermined at a complete assignment must fail without mutating the solver graph");

    const auto expression_profile = [](
                                        const std::size_t width, const bool signed_value = false,
                                        const bool four_state = false) {
        return SystemVerilogConstraintVariableProfile {
            SystemVerilogConstraintDomainKind::BitVector,
            width,
            signed_value,
            signed_value ? "logic signed" : "logic",
            four_state
        };
    };
    SystemVerilogConstraintSolver expression_solver;
    auto signed_left_variable = variable(
        "signed-left", SystemVerilogConstraintDomainKind::BitVector,
        4, "logic signed", { 15 }, true);
    signed_left_variable.profile.four_state = true;
    const auto signed_left = expression_solver.add_variable(
        std::move(signed_left_variable));
    auto signed_right_variable = variable(
        "signed-right", SystemVerilogConstraintDomainKind::BitVector,
        4, "logic signed", { 1 }, true);
    signed_right_variable.profile.four_state = true;
    const auto signed_right = expression_solver.add_variable(
        std::move(signed_right_variable));
    SystemVerilogConstraintExpressionBuilder signed_builder;
    const auto signed_left_node = signed_builder.variable(
        signed_left, expression_solver.variables()[signed_left].profile);
    const auto signed_right_node = signed_builder.variable(
        signed_right, expression_solver.variables()[signed_right].profile);
    const auto signed_less = signed_builder.binary(
        SystemVerilogConstraintExpressionOperator::Less,
        signed_left_node, signed_right_node);
    expression_solver.add_clause(systemverilog_constraint_expression_clause(
        "signed-less", { signed_left, signed_right },
        std::move(signed_builder).finish(signed_less)));

    SystemVerilogConstraintExpressionBuilder arithmetic_builder;
    const auto fifteen = arithmetic_builder.constant(
        value(4, 15), expression_profile(4));
    const auto one = arithmetic_builder.constant(
        value(4, 1), expression_profile(4));
    const auto zero = arithmetic_builder.constant(
        value(4, 0), expression_profile(4));
    const auto wrapped = arithmetic_builder.binary(
        SystemVerilogConstraintExpressionOperator::Add, fifteen, one);
    const auto wrapped_equal = arithmetic_builder.binary(
        SystemVerilogConstraintExpressionOperator::Equal, wrapped, zero);
    expression_solver.add_clause(systemverilog_constraint_expression_clause(
        "exact-width-wrap", { },
        std::move(arithmetic_builder).finish(wrapped_equal)));

    SystemVerilogConstraintExpressionBuilder divide_builder;
    const auto seven = divide_builder.constant(
        value(4, 7), expression_profile(4));
    const auto three = divide_builder.constant(
        value(4, 3), expression_profile(4));
    const auto two = divide_builder.constant(
        value(4, 2), expression_profile(4));
    const auto divide_one = divide_builder.constant(
        value(4, 1), expression_profile(4));
    const auto eight = divide_builder.constant(
        value(4, 8), expression_profile(4));
    const auto divide = divide_builder.binary(
        SystemVerilogConstraintExpressionOperator::Divide, seven, three);
    const auto modulo = divide_builder.binary(
        SystemVerilogConstraintExpressionOperator::Modulo, seven, three);
    const auto quotient_equal = divide_builder.binary(
        SystemVerilogConstraintExpressionOperator::Equal, divide, two);
    const auto remainder_equal = divide_builder.binary(
        SystemVerilogConstraintExpressionOperator::Equal, modulo, divide_one);
    const auto divide_and_modulo = divide_builder.binary(
        SystemVerilogConstraintExpressionOperator::LogicalAnd,
        quotient_equal, remainder_equal);
    const auto power = divide_builder.binary(
        SystemVerilogConstraintExpressionOperator::Power, two, three);
    const auto power_equal = divide_builder.binary(
        SystemVerilogConstraintExpressionOperator::Equal, power, eight);
    const auto arithmetic_results = divide_builder.binary(
        SystemVerilogConstraintExpressionOperator::LogicalAnd,
        divide_and_modulo, power_equal);
    expression_solver.add_clause(systemverilog_constraint_expression_clause(
        "divide-modulo", { },
        std::move(divide_builder).finish(arithmetic_results)));

    SystemVerilogConstraintExpressionBuilder four_state_builder;
    const auto unknown_condition = four_state_builder.constant(
        PackedLogic4(1, Logic4::x), expression_profile(1, false, true));
    const auto true_value = four_state_builder.constant(
        PackedLogic4::from_msb_string("10"),
        expression_profile(2, false, true));
    const auto false_value = four_state_builder.constant(
        PackedLogic4::from_msb_string("11"),
        expression_profile(2, false, true));
    const auto merged = four_state_builder.conditional(
        unknown_condition, true_value, false_value);
    const auto expected_merge = four_state_builder.constant(
        PackedLogic4::from_msb_string("1X"),
        expression_profile(2, false, true));
    const auto exact_merge = four_state_builder.binary(
        SystemVerilogConstraintExpressionOperator::CaseEqual,
        merged, expected_merge);
    expression_solver.add_clause(systemverilog_constraint_expression_clause(
        "four-state-conditional", { },
        std::move(four_state_builder).finish(exact_merge)));
    const auto expression_result = expression_solver.solve();
    require(
        expression_result.status
                == SystemVerilogConstraintSolveStatus::Satisfied
            && expression_result.values[signed_left].low_word().aval == 15
            && expression_result.values[signed_right].low_word().aval == 1,
        "constraint lowering must preserve signed comparison, exact-width arithmetic, division, modulo, logical, and conditional semantics");

    SystemVerilogConstraintSolver illegal_four_state;
    SystemVerilogConstraintExpressionBuilder illegal_builder;
    const auto unknown = illegal_builder.constant(
        PackedLogic4(1, Logic4::x), expression_profile(1, false, true));
    const auto known_zero = illegal_builder.constant(
        value(1, 0), expression_profile(1, false, true));
    const auto illegal_equal = illegal_builder.binary(
        SystemVerilogConstraintExpressionOperator::Equal,
        unknown, known_zero);
    illegal_four_state.add_clause(systemverilog_constraint_expression_clause(
        "illegal-four-state", { },
        std::move(illegal_builder).finish(illegal_equal)));
    bool illegal_result = false;
    try {
        (void)illegal_four_state.solve();
    } catch (const std::invalid_argument&) {
        illegal_result = true;
    }
    require(
        illegal_result,
        "a four-state predicate that remains unknown must be rejected explicitly");

    SystemVerilogConstraintSolver soft_solver;
    const auto soft_value = soft_solver.add_variable(variable(
        "soft-value", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0, 1 }));
    soft_solver.add_clause({ "earlier-soft", { soft_value }, equals(soft_value, 0), true });
    soft_solver.add_clause({ "later-soft", { soft_value }, equals(soft_value, 1), true });
    const auto soft_result = soft_solver.solve();
    require(
        soft_result.status == SystemVerilogConstraintSolveStatus::Satisfied
            && soft_result.values[soft_value].low_word().aval == 1
            && soft_result.search_steps == 2,
        "conflicting soft constraints must resolve deterministically in later declaration priority");

    SystemVerilogConstraintSolver hard_over_soft;
    const auto hard_value = hard_over_soft.add_variable(variable(
        "hard-value", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0, 1 }));
    hard_over_soft.add_clause(
        { "hard-zero", { hard_value }, equals(hard_value, 0) });
    hard_over_soft.add_clause(
        { "soft-one", { hard_value }, equals(hard_value, 1), true });
    const auto hard_result = hard_over_soft.solve();
    require(
        hard_result.status == SystemVerilogConstraintSolveStatus::Satisfied
            && hard_result.values[hard_value].low_word().aval == 0,
        "hard constraints must dominate every conflicting soft preference");

    SystemVerilogConstraintSolver distribution_solver;
    const auto distributed = distribution_solver.add_variable(variable(
        "distributed", SystemVerilogConstraintDomainKind::BitVector,
        2, "logic[1:0]", { 0, 1, 2, 3 }));
    distribution_solver.add_distribution({ "weighted-domain",
        distributed,
        {
            { value(2, 0), value(2, 0), 1,
                SystemVerilogConstraintDistributionWeight::PerValue },
            { value(2, 1), value(2, 3), 6,
                SystemVerilogConstraintDistributionWeight::AcrossRange },
        } });
    const auto normalized = distribution_solver.distribution_weights("weighted-domain");
    const auto weighted_first = distribution_solver.solve(1234);
    const auto weighted_replay = distribution_solver.solve(1234);
    require(
        normalized.size() == 4
            && normalized[0] == 3
            && normalized[1] == 6
            && normalized[2] == 6
            && normalized[3] == 6
            && weighted_first.status
                == SystemVerilogConstraintSolveStatus::Satisfied
            && weighted_first.values == weighted_replay.values
            && weighted_first.search_steps == weighted_replay.search_steps,
        "dist := and :/ weights must normalize exactly and select deterministically from the replay input");

    SystemVerilogConstraintSolver conflicting_distributions;
    const auto conflict_value = conflicting_distributions.add_variable(variable(
        "conflict-value", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0, 1 }));
    conflicting_distributions.add_distribution({ "only-zero", conflict_value,
        { { value(1, 0), value(1, 0), 1 } } });
    conflicting_distributions.add_distribution({ "only-one", conflict_value,
        { { value(1, 1), value(1, 1), 1 } } });
    require(
        conflicting_distributions.solve().status
            == SystemVerilogConstraintSolveStatus::Unsatisfiable,
        "conflicting distributions must produce a deterministic empty domain");

    SystemVerilogConstraintSolver overflowing_distribution;
    const auto overflow_value = overflowing_distribution.add_variable(variable(
        "overflow-value", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0 }));
    bool weight_overflow = false;
    try {
        overflowing_distribution.add_distribution({ "overflowing", overflow_value,
            {
                { value(1, 0), value(1, 0),
                    std::numeric_limits<std::uint64_t>::max() },
                { value(1, 0), value(1, 0), 1 },
            } });
    } catch (const std::overflow_error&) {
        weight_overflow = true;
    }
    require(
        weight_overflow
            && overflowing_distribution.distribution_count() == 0,
        "distribution weight overflow must reject transactionally");

    SystemVerilogConstraintSolver foreach_solver;
    SystemVerilogConstraintExpressionBuilder foreach_builder;
    std::vector<SystemVerilogConstraintVariableId> foreach_variables;
    std::vector<SystemVerilogConstraintExpressionId> element_predicates;
    constexpr std::size_t foreach_elements = 300;
    foreach_variables.reserve(foreach_elements);
    element_predicates.reserve(foreach_elements);
    const auto element_profile = expression_profile(1);
    for (std::size_t index = 0; index < foreach_elements; ++index) {
        const auto element = foreach_solver.add_variable(variable(
            "array[" + std::to_string(index) + "]",
            SystemVerilogConstraintDomainKind::BitVector,
            1, "logic", { 1 }));
        foreach_variables.push_back(element);
        const auto element_node = foreach_builder.variable(
            element, foreach_solver.variables()[element].profile);
        const auto one_node = foreach_builder.constant(
            value(1, 1), element_profile);
        element_predicates.push_back(foreach_builder.binary(
            SystemVerilogConstraintExpressionOperator::Equal,
            element_node, one_node));
    }
    const auto all_elements = foreach_builder.conjunction(element_predicates);
    foreach_solver.add_clause(systemverilog_constraint_expression_clause(
        "foreach-all-elements",
        foreach_variables,
        std::move(foreach_builder).finish(all_elements)));
    const auto foreach_result = foreach_solver.solve();
    require(
        foreach_result.status
                == SystemVerilogConstraintSolveStatus::Satisfied
            && foreach_result.values.size() == foreach_elements
            && foreach_result.search_steps == foreach_elements,
        "bounded foreach lowering must traverse every materialized element without an arbitrary 256-element ceiling");

    SystemVerilogConstraintSolver ordered_solver;
    const auto z_order = ordered_solver.add_variable(variable(
        "zeta", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0 }));
    const auto a_order = ordered_solver.add_variable(variable(
        "alpha", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0 }));
    const auto m_order = ordered_solver.add_variable(variable(
        "middle", SystemVerilogConstraintDomainKind::BitVector,
        1, "bit", { 0 }));
    require(
        ordered_solver.search_order()
            == std::vector<SystemVerilogConstraintVariableId> {
                a_order, m_order, z_order },
        "unconstrained solver search order must use canonical identities instead of declaration order");
    ordered_solver.add_solve_before(z_order, a_order);
    const auto directed_order = ordered_solver.search_order();
    require(
        directed_order
            == std::vector<SystemVerilogConstraintVariableId> {
                m_order, z_order, a_order },
        "solve-before edges must topologically override canonical tie breaking");
    bool solve_cycle = false;
    try {
        ordered_solver.add_solve_before(a_order, z_order);
    } catch (const std::invalid_argument& error) {
        solve_cycle = std::string_view { error.what() }.find(
                          "cycle reaches 'alpha'")
            != std::string_view::npos;
    }
    require(
        solve_cycle && ordered_solver.search_order() == directed_order,
        "solve-before cycles must diagnose canonically and leave the ordering graph unchanged");

    SystemVerilogClassDescriptor random_descriptor;
    random_descriptor.declared_type = "work::Packet";
    random_descriptor.dynamic_type = "work::Packet";
    random_descriptor.specialization_identity = "work::Packet";
    random_descriptor.random_root_identity = "randomize-test";
    const auto random_property = [](
                                     std::string name, const std::uint64_t initial) {
        SystemVerilogClassPropertyDescriptor result {
            std::move(name), SystemVerilogClassPropertyKind::Bit2, 2
        };
        result.initial_packed = value(2, initial);
        result.random_kind = SystemVerilogClassRandomKind::Rand;
        result.nominal_type = "bit[1:0]";
        return result;
    };
    random_descriptor.properties.push_back(
        random_property("work::Packet::a", 3));
    random_descriptor.properties.push_back(
        random_property("work::Packet::b", 2));
    SystemVerilogClassPropertyDescriptor fixed_property {
        "work::Packet::fixed", SystemVerilogClassPropertyKind::Bit2, 2
    };
    fixed_property.initial_packed = value(2, 1);
    random_descriptor.properties.push_back(std::move(fixed_property));

    const auto equal_clause = [](
                                  std::string identity,
                                  const SystemVerilogConstraintVariableId variable,
                                  const std::uint64_t expected) {
        return SystemVerilogConstraintClause {
            std::move(identity), { variable },
            [=](const SystemVerilogConstraintAssignment& assignment) {
                if (!assignment.assigned(variable)) {
                    return SystemVerilogConstraintClauseState::Undetermined;
                }
                return assignment.value(variable).low_word().aval == expected
                    ? SystemVerilogConstraintClauseState::Satisfied
                    : SystemVerilogConstraintClauseState::Violated;
            }
        };
    };
    SystemVerilogClassHeap random_heap { { }, 91 };
    const auto random_handle = random_heap.allocate(random_descriptor);
    SystemVerilogClassRandomizeRequest random_request;
    random_request.call_identity = "work::Packet::randomize@joint";
    random_request.limits.maximum_domain_values = 32;
    random_request.class_constraints = [&](auto& configured, const auto& ids) {
        require(ids.size() == 3, "constraints must see selected and fixed object properties");
        configured.add_clause(equal_clause(
            "class-a", ids.at("work::Packet::a"), 1));
        configured.add_clause(equal_clause(
            "class-fixed", ids.at("work::Packet::fixed"), 1));
    };
    random_request.inline_constraints = [&](auto& configured, const auto& ids) {
        configured.add_clause(equal_clause(
            "inline-b", ids.at("work::Packet::b"), 2));
    };
    const auto randomized = randomize_systemverilog_class_object(
        random_heap, random_handle, random_request);
    require(
        randomized.language_result() == 1
            && random_heap.property(random_handle, "a").packed == value(2, 1)
            && random_heap.property(random_handle, "b").packed == value(2, 2)
            && random_heap.random_state(random_handle, "a").revision == 1
            && random_heap.random_state(random_handle, "b").revision == 1,
        "object randomize must jointly solve class and inline constraints before committing every selected property");

    SystemVerilogClassRandomizeRequest selected_request;
    selected_request.variable_list = { "a" };
    selected_request.call_identity = "work::Packet::randomize@selected";
    selected_request.limits.maximum_domain_values = 16;
    selected_request.inline_constraints = [&](auto& configured, const auto& ids) {
        configured.add_clause(equal_clause(
            "selected-a", ids.at("work::Packet::a"), 3));
        configured.add_clause(equal_clause(
            "retained-b", ids.at("work::Packet::b"), 2));
    };
    const auto selected_randomized = randomize_systemverilog_class_object(
        random_heap, random_handle, selected_request);
    require(
        selected_randomized.language_result() == 1
            && random_heap.property(random_handle, "a").packed == value(2, 3)
            && random_heap.property(random_handle, "b").packed == value(2, 2)
            && random_heap.random_state(random_handle, "a").revision == 2
            && random_heap.random_state(random_handle, "b").revision == 1,
        "an object randomize variable list must publish only the selected property while constraints observe fixed current values");

    const auto before_a = random_heap.property(random_handle, "a").packed;
    const auto before_b = random_heap.property(random_handle, "b").packed;
    const auto before_a_revision = random_heap.random_state(
                                                  random_handle, "a")
                                       .revision;
    const auto before_b_revision = random_heap.random_state(
                                                  random_handle, "b")
                                       .revision;
    SystemVerilogClassRandomizeRequest impossible_request;
    impossible_request.call_identity = "work::Packet::randomize@impossible";
    impossible_request.limits.maximum_domain_values = 32;
    impossible_request.class_constraints = [&](auto& configured, const auto& ids) {
        configured.add_clause(equal_clause(
            "hard-a-zero", ids.at("work::Packet::a"), 0));
    };
    impossible_request.inline_constraints = [&](auto& configured, const auto& ids) {
        configured.add_clause(equal_clause(
            "inline-a-one", ids.at("work::Packet::a"), 1));
    };
    const auto impossible_randomized = randomize_systemverilog_class_object(
        random_heap, random_handle, impossible_request);
    require(
        impossible_randomized.status
                == SystemVerilogConstraintSolveStatus::Unsatisfiable
            && impossible_randomized.language_result() == 0
            && random_heap.property(random_handle, "a").packed == before_a
            && random_heap.property(random_handle, "b").packed == before_b
            && random_heap.random_state(random_handle, "a").revision
                == before_a_revision
            && random_heap.random_state(random_handle, "b").revision
                == before_b_revision,
        "an unsatisfiable object randomize call must return zero without partial property or revision writes");

    SystemVerilogClassRandomizeRequest domain_limited_request;
    domain_limited_request.call_identity = "work::Packet::randomize@domain-limit";
    domain_limited_request.limits.maximum_domain_values = 3;
    const auto domain_limited_randomized = randomize_systemverilog_class_object(
        random_heap, random_handle, domain_limited_request);
    require(
        domain_limited_randomized.status
                == SystemVerilogConstraintSolveStatus::ResourceExhausted
            && domain_limited_randomized.exhausted_resource
                == SystemVerilogConstraintResource::DomainValues
            && domain_limited_randomized.language_result() == 0
            && random_heap.property(random_handle, "a").packed == before_a
            && random_heap.property(random_handle, "b").packed == before_b
            && random_heap.random_state(random_handle, "a").revision
                == before_a_revision
            && random_heap.random_state(random_handle, "b").revision
                == before_b_revision,
        "domain exhaustion must be a typed zero result with no object mutation");

    SystemVerilogClassRandomizeRequest search_limited_request;
    search_limited_request.call_identity = "work::Packet::randomize@search-limit";
    search_limited_request.limits.maximum_domain_values = 32;
    search_limited_request.limits.maximum_search_steps = 0;
    const auto search_limited_randomized = randomize_systemverilog_class_object(
        random_heap, random_handle, search_limited_request);
    require(
        search_limited_randomized.status
                == SystemVerilogConstraintSolveStatus::ResourceExhausted
            && search_limited_randomized.exhausted_resource
                == SystemVerilogConstraintResource::SearchSteps
            && random_heap.property(random_handle, "a").packed == before_a
            && random_heap.random_state(random_handle, "a").revision
                == before_a_revision,
        "search exhaustion must preserve the complete pre-call object state");

    bool configuration_failed = false;
    SystemVerilogClassRandomizeRequest throwing_request;
    throwing_request.call_identity = "work::Packet::randomize@throw";
    throwing_request.limits.maximum_domain_values = 32;
    throwing_request.inline_constraints = [](auto&, const auto&) {
        throw std::runtime_error { "inline constraint construction failed" };
    };
    try {
        (void)randomize_systemverilog_class_object(
            random_heap, random_handle, throwing_request);
    } catch (const std::runtime_error&) {
        configuration_failed = true;
    }
    require(
        configuration_failed
            && random_heap.property(random_handle, "a").packed == before_a
            && random_heap.random_state(random_handle, "a").revision
                == before_a_revision,
        "constraint-construction failure must occur before random stream or object-state publication");

    SystemVerilogClassHeap replay_left { { }, 177 };
    SystemVerilogClassHeap replay_right { { }, 177 };
    const auto replay_left_handle = replay_left.allocate(random_descriptor);
    const auto replay_right_handle = replay_right.allocate(random_descriptor);
    SystemVerilogClassRandomizeRequest replay_request;
    replay_request.call_identity = "work::Packet::randomize@replay";
    replay_request.limits.maximum_domain_values = 32;
    const auto replay_left_result = randomize_systemverilog_class_object(
        replay_left, replay_left_handle, replay_request);
    const auto replay_right_result = randomize_systemverilog_class_object(
        replay_right, replay_right_handle, replay_request);
    require(
        replay_left_result.language_result() == 1
            && replay_right_result.language_result() == 1
            && replay_left.property(replay_left_handle, "a").packed
                == replay_right.property(replay_right_handle, "a").packed
            && replay_left.property(replay_left_handle, "b").packed
                == replay_right.property(replay_right_handle, "b").packed,
        "equal object and call streams must replay the same unconstrained randomize assignment");

    SystemVerilogClassDescriptor randc_descriptor;
    randc_descriptor.declared_type = "work::Cycle";
    randc_descriptor.dynamic_type = "work::Cycle";
    randc_descriptor.specialization_identity = "work::Cycle";
    randc_descriptor.random_root_identity = "randc-test";
    auto cycle_property = random_property("work::Cycle::value", 0);
    cycle_property.random_kind = SystemVerilogClassRandomKind::Randc;
    randc_descriptor.properties.push_back(std::move(cycle_property));
    SystemVerilogClassRandomizeRequest cycle_request;
    cycle_request.call_identity = "work::Cycle::randomize";
    cycle_request.limits.maximum_domain_values = 8;
    SystemVerilogClassHeap cycle_heap { { }, 271 };
    const auto cycle_handle = cycle_heap.allocate(randc_descriptor);
    const auto initial_cycle_storage = cycle_heap.storage_bytes();
    std::vector<std::uint64_t> cycle_values;
    for (std::size_t call = 0; call < 8; ++call) {
        const auto result = randomize_systemverilog_class_object(
            cycle_heap, cycle_handle, cycle_request);
        require(result.language_result() == 1, "randc cycle solve must succeed");
        cycle_values.push_back(
            cycle_heap.property(cycle_handle, "value").packed.low_word().aval);
    }
    for (std::size_t cycle = 0; cycle < 2; ++cycle) {
        std::set<std::uint64_t> values {
            cycle_values.begin() + static_cast<std::ptrdiff_t>(cycle * 4U),
            cycle_values.begin() + static_cast<std::ptrdiff_t>((cycle + 1U) * 4U)
        };
        require(
            values == std::set<std::uint64_t> { 0, 1, 2, 3 },
            "each randc permutation cycle must visit its exact domain once");
    }
    require(
        cycle_heap.random_state(cycle_handle, "value").randc_cycle == 1
            && cycle_heap.random_state(
                             cycle_handle, "value")
                    .randc_used_values.size()
                == 4
            && cycle_heap.storage_bytes()
                == initial_cycle_storage + 4U * sizeof(std::uint64_t),
        "randc cycle state must retain a portable ordinal and used-value indices");

    SystemVerilogClassHeap constrained_cycle_heap { { }, 271 };
    const auto constrained_cycle_handle = constrained_cycle_heap.allocate(randc_descriptor);
    auto constrained_cycle_request = cycle_request;
    constrained_cycle_request.inline_constraints = [](
                                                       auto& configured, const auto& ids) {
        const auto variable = ids.at("work::Cycle::value");
        configured.add_clause({ "cycle-domain", { variable },
            [=](const SystemVerilogConstraintAssignment& assignment) {
                if (!assignment.assigned(variable)) {
                    return SystemVerilogConstraintClauseState::Undetermined;
                }
                const auto selected = assignment.value(variable).low_word().aval;
                return selected == 1 || selected == 2
                    ? SystemVerilogConstraintClauseState::Satisfied
                    : SystemVerilogConstraintClauseState::Violated;
            } });
    };
    std::vector<std::uint64_t> constrained_values;
    for (std::size_t call = 0; call < 4; ++call) {
        const auto result = randomize_systemverilog_class_object(
            constrained_cycle_heap,
            constrained_cycle_handle,
            constrained_cycle_request);
        require(result.language_result() == 1, "constrained randc cycle must restart");
        constrained_values.push_back(constrained_cycle_heap.property(
                                                               constrained_cycle_handle, "value")
                .packed.low_word()
                .aval);
    }
    require(
        constrained_values[0] != constrained_values[1]
            && constrained_values[2] != constrained_values[3]
            && std::ranges::all_of(constrained_values, [](const auto selected) {
                   return selected == 1 || selected == 2;
               }),
        "constraint-limited randc cycles must exhaust every reachable value before restarting");

    SystemVerilogClassHeap domain_cycle_heap { { }, 271 };
    const auto domain_cycle_handle = domain_cycle_heap.allocate(randc_descriptor);
    auto first_domain = cycle_request;
    first_domain.property_domains["value"] = { value(2, 0), value(2, 1) };
    require(
        randomize_systemverilog_class_object(
            domain_cycle_heap, domain_cycle_handle, first_domain)
                .language_result()
            == 1,
        "the first exact randc domain must solve");
    const auto first_signature = domain_cycle_heap.random_state(
                                                      domain_cycle_handle, "value")
                                     .randc_domain_signature;
    auto changed_domain = cycle_request;
    changed_domain.property_domains["value"] = { value(2, 2), value(2, 3) };
    require(
        randomize_systemverilog_class_object(
            domain_cycle_heap, domain_cycle_handle, changed_domain)
                    .language_result()
                == 1
            && domain_cycle_heap.property(
                                    domain_cycle_handle, "value")
                    .packed.low_word()
                    .aval
                >= 2
            && domain_cycle_heap.random_state(
                                    domain_cycle_handle, "value")
                    .randc_domain_signature
                != first_signature
            && domain_cycle_heap.random_state(
                                    domain_cycle_handle, "value")
                    .randc_cycle
                == 0
            && domain_cycle_heap.random_state(
                                    domain_cycle_handle, "value")
                    .randc_used_values.size()
                == 1,
        "an exact randc domain change must invalidate the incompatible cycle");

    cycle_heap.reseed_random(cycle_handle, 991);
    require(
        cycle_heap.storage_bytes() == initial_cycle_storage,
        "reseed must release accounted sparse randc cycle storage");
    const auto reseeded_first = randomize_systemverilog_class_object(
        cycle_heap, cycle_handle, cycle_request);
    const auto reseeded_value = cycle_heap.property(
                                              cycle_handle, "value")
                                    .packed;
    cycle_heap.reset_randc_cycle(cycle_handle, "value");
    const auto reset_result = randomize_systemverilog_class_object(
        cycle_heap, cycle_handle, cycle_request);
    require(
        reseeded_first.language_result() == 1
            && reset_result.language_result() == 1
            && cycle_heap.property(cycle_handle, "value").packed
                == reseeded_value,
        "explicit reset must replay the first permutation value under the retained seed");
    cycle_heap.reseed_random(cycle_handle, 991);
    require(
        randomize_systemverilog_class_object(
            cycle_heap, cycle_handle, cycle_request)
                    .language_result()
                == 1
            && cycle_heap.property(cycle_handle, "value").packed
                == reseeded_value,
        "object reseed must clear call ordinals and derive a repeatable randc cycle");

    const auto before_failed_cycle = cycle_heap.random_state(
        cycle_handle, "value");
    auto failed_cycle_request = cycle_request;
    failed_cycle_request.inline_constraints = [&](auto& configured, const auto& ids) {
        configured.add_clause(equal_clause(
            "cycle-zero", ids.at("work::Cycle::value"), 0));
        configured.add_clause(equal_clause(
            "cycle-one", ids.at("work::Cycle::value"), 1));
    };
    require(
        randomize_systemverilog_class_object(
            cycle_heap, cycle_handle, failed_cycle_request)
                    .language_result()
                == 0
            && cycle_heap.random_state(
                             cycle_handle, "value")
                    .randc_domain_signature
                == before_failed_cycle.randc_domain_signature
            && cycle_heap.random_state(cycle_handle, "value").randc_cycle
                == before_failed_cycle.randc_cycle
            && cycle_heap.random_state(
                             cycle_handle, "value")
                    .randc_used_values
                == before_failed_cycle.randc_used_values,
        "an unsatisfiable randc solve must preserve the complete cycle state");

    SystemVerilogClassHeap storage_probe { { }, 271 };
    const auto storage_probe_handle = storage_probe.allocate(randc_descriptor);
    const auto exact_cycle_object_bytes = storage_probe.object(storage_probe_handle).accounted_bytes;
    SystemVerilogClassHeap cycle_storage_limited {
        { 1, exact_cycle_object_bytes }, 271
    };
    const auto cycle_storage_handle = cycle_storage_limited.allocate(randc_descriptor);
    const auto storage_before = cycle_storage_limited.property(
                                                         cycle_storage_handle, "value")
                                    .packed;
    bool cycle_storage_failed = false;
    try {
        (void)randomize_systemverilog_class_object(
            cycle_storage_limited, cycle_storage_handle, cycle_request);
    } catch (const std::length_error&) {
        cycle_storage_failed = true;
    }
    require(
        cycle_storage_failed
            && cycle_storage_limited.property(
                                        cycle_storage_handle, "value")
                    .packed
                == storage_before
            && cycle_storage_limited.random_state(
                                        cycle_storage_handle, "value")
                    .revision
                == 0
            && cycle_storage_limited.random_state(
                                        cycle_storage_handle, "value")
                .randc_used_values.empty()
            && cycle_storage_limited.storage_bytes() == exact_cycle_object_bytes,
        "randc storage exhaustion must reject before publishing assignment, revision, cycle, or accounting changes");

    SystemVerilogClassHeap stale_cycle_heap { { }, 271 };
    const auto stale_cycle_handle = stale_cycle_heap.allocate(randc_descriptor);
    require(
        stale_cycle_heap.release(stale_cycle_handle),
        "the stale randc negative requires a released handle");
    bool null_randomize_failed = false;
    bool stale_randomize_failed = false;
    try {
        (void)randomize_systemverilog_class_object(
            stale_cycle_heap, 0, cycle_request);
    } catch (const std::out_of_range&) {
        null_randomize_failed = true;
    }
    try {
        (void)randomize_systemverilog_class_object(
            stale_cycle_heap, stale_cycle_handle, cycle_request);
    } catch (const std::out_of_range&) {
        stale_randomize_failed = true;
    }
    require(
        null_randomize_failed && stale_randomize_failed
            && stale_cycle_heap.live_objects() == 0
            && stale_cycle_heap.storage_bytes() == 0,
        "null and stale class randomize handles must reject without publishing heap state");

    auto local_integral = value(3, 0);
    auto local_enum = value(2, 1);
    auto container_zero = value(2, 0);
    auto container_one = value(2, 3);
    SystemVerilogScopeRandomizeRequest scope_request;
    scope_request.selection = 771;
    scope_request.limits.maximum_domain_values = 32;
    scope_request.variables = {
        { "process::integral",
            { SystemVerilogConstraintDomainKind::Integer, 3, false, "int3" },
            { }, &local_integral },
        { "process::mode",
            { SystemVerilogConstraintDomainKind::Enumeration, 2, false,
                "work::mode_t" },
            { value(2, 1), value(2, 3) }, &local_enum },
        { "process::items[0]",
            { SystemVerilogConstraintDomainKind::BitVector, 2, false, "logic[1:0]" },
            { value(2, 0), value(2, 1), value(2, 2) }, &container_zero },
        { "process::items[1]",
            { SystemVerilogConstraintDomainKind::BitVector, 2, false, "logic[1:0]" },
            { value(2, 1), value(2, 2), value(2, 3) }, &container_one }
    };
    scope_request.inline_constraints = [&](auto& configured, const auto& ids) {
        configured.add_clause(equal_clause(
            "inline-integral", ids.at("process::integral"), 5));
        configured.add_clause(equal_clause(
            "inline-enum", ids.at("process::mode"), 3));
        configured.add_clause(equal_clause(
            "inline-item-zero", ids.at("process::items[0]"), 1));
        configured.add_clause(equal_clause(
            "inline-item-one", ids.at("process::items[1]"), 2));
    };
    const auto scoped = randomize_systemverilog_scope(scope_request);
    require(
        scoped.language_result() == 1
            && local_integral == value(3, 5)
            && local_enum == value(2, 3)
            && container_zero == value(2, 1)
            && container_one == value(2, 2),
        "std::randomize scope execution must jointly solve bounded integral, exact enum, and materialized container-element domains");

    const auto retained_integral = local_integral;
    const auto retained_enum = local_enum;
    const auto retained_zero = container_zero;
    const auto retained_one = container_one;
    scope_request.inline_constraints = [&](auto& configured, const auto& ids) {
        configured.add_clause(equal_clause(
            "impossible-integral-five", ids.at("process::integral"), 5));
        configured.add_clause(equal_clause(
            "impossible-integral-six", ids.at("process::integral"), 6));
    };
    const auto scoped_impossible = randomize_systemverilog_scope(scope_request);
    require(
        scoped_impossible.language_result() == 0
            && scoped_impossible.status
                == SystemVerilogConstraintSolveStatus::Unsatisfiable
            && local_integral == retained_integral
            && local_enum == retained_enum
            && container_zero == retained_zero
            && container_one == retained_one,
        "an unsatisfiable std::randomize scope transaction must preserve every scalar and container element");

    scope_request.inline_constraints = [](auto&, const auto&) { };
    scope_request.limits.maximum_domain_values = 7;
    const auto scoped_exhausted = randomize_systemverilog_scope(scope_request);
    require(
        scoped_exhausted.language_result() == 0
            && scoped_exhausted.status
                == SystemVerilogConstraintSolveStatus::ResourceExhausted
            && scoped_exhausted.exhausted_resource
                == SystemVerilogConstraintResource::DomainValues
            && local_integral == retained_integral
            && container_one == retained_one,
        "scope domain exhaustion must return zero before publishing any target");

    auto wide = PackedLogic4(137, Logic4::zero);
    auto wide_repeated = PackedLogic4(137, Logic4::zero);
    SystemVerilogScopeRandomizeRequest wide_request;
    wide_request.selection = 991;
    wide_request.limits.maximum_domain_values = 0;
    wide_request.variables = {
        { "process::wide",
            { SystemVerilogConstraintDomainKind::BitVector, 137, false,
                "logic[136:0]" },
            { }, &wide }
    };
    const auto wide_result = randomize_systemverilog_scope(wide_request);
    wide_request.variables.front().target = &wide_repeated;
    const auto wide_repeated_result = randomize_systemverilog_scope(wide_request);
    require(
        wide_result.language_result() == 1
            && wide_repeated_result.language_result() == 1
            && wide == wide_repeated
            && wide != PackedLogic4(137, Logic4::zero)
            && std::ranges::all_of(
                wide.bval_words(), [](const auto word) { return word == 0; }),
        "unconstrained std::randomize must generate deterministic arbitrary-width known packed values without enumerating the domain");

    auto constrained_wide_value = PackedLogic4(137, Logic4::zero);
    constrained_wide_value.set(136, Logic4::one);
    constrained_wide_value.set(72, Logic4::one);
    constrained_wide_value.set(3, Logic4::one);
    constrained_wide_value.set(0, Logic4::one);
    auto constrained_wide = PackedLogic4(137, Logic4::zero);
    SystemVerilogConstraintTemplate wide_name;
    wide_name.kind = SystemVerilogConstraintTemplateKind::Name;
    wide_name.text = "wide";
    SystemVerilogConstraintTemplate wide_constant;
    wide_constant.kind = SystemVerilogConstraintTemplateKind::Constant;
    wide_constant.constant = constrained_wide_value;
    wide_constant.profile = {
        SystemVerilogConstraintDomainKind::BitVector,
        constrained_wide_value.width(), false, "logic[136:0]", true
    };
    SystemVerilogConstraintTemplate wide_equal;
    wide_equal.kind = SystemVerilogConstraintTemplateKind::Binary;
    wide_equal.text = "==";
    wide_equal.operands = { std::move(wide_name), std::move(wide_constant) };
    SystemVerilogScopeRandomizeRequest constrained_wide_request;
    constrained_wide_request.selection = 991;
    constrained_wide_request.limits.maximum_domain_values = 1;
    constrained_wide_request.variables = {
        { "process::wide",
            { SystemVerilogConstraintDomainKind::BitVector, 137, false,
                "logic[136:0]" },
            { constrained_wide_value }, &constrained_wide }
    };
    constrained_wide_request.inline_constraints = [&](auto& configured,
                                                      const auto& ids) {
        configure_systemverilog_inline_constraints(
            configured, ids, std::span { &wide_equal, 1U },
            "process::wide-inline");
    };
    const auto constrained_wide_result
        = randomize_systemverilog_scope(constrained_wide_request);
    require(
        constrained_wide_result.language_result() == 1
            && constrained_wide == constrained_wide_value,
        "portable inline constraint templates must preserve exact 137-bit values without a host-word cap");
}

} // namespace fsim::tests::runtime
