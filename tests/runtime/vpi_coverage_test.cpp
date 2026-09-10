// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_coverage.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace fsim::runtime;

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

SystemVerilogVpiStoredValue state_value(const std::string_view bits)
{
    return { PackedBit2::from_msb_string(bits), std::nullopt };
}

void test_properties_controls_and_failures()
{
    static_assert(static_cast<std::int32_t>(
                      SystemVerilogVpiCoverageControl::Start)
            == 750
        && static_cast<std::int32_t>(
               SystemVerilogVpiCoverageControl::Save)
            == 755
        && static_cast<std::int32_t>(
               SystemVerilogVpiCoverageType::Assertion)
            == static_cast<std::int32_t>(
                SystemVerilogVpiCoverageProperty::AssertCoverage)
        && static_cast<std::int32_t>(
               SystemVerilogVpiCoverageType::FsmState)
            == static_cast<std::int32_t>(
                SystemVerilogVpiCoverageProperty::FsmStateCoverage)
        && static_cast<std::int32_t>(
               SystemVerilogVpiCoverageType::Statement)
            == static_cast<std::int32_t>(
                SystemVerilogVpiCoverageProperty::StatementCoverage)
        && static_cast<std::int32_t>(
               SystemVerilogVpiCoverageType::Toggle)
            == static_cast<std::int32_t>(
                SystemVerilogVpiCoverageProperty::ToggleCoverage));
    SystemVerilogVpiObjectRegistry objects { 101U };
    const auto root = objects.create(
        SystemVerilogVpiObjectKind::Root, 0U, "top");
    const auto module = objects.create(
        SystemVerilogVpiObjectKind::Module, root.value, "dut");
    const auto signal = objects.create(
        SystemVerilogVpiObjectKind::Variable, module.value, "state");
    const auto assertion = objects.create(
        SystemVerilogVpiObjectKind::Assertion, module.value, "p_ready");
    require(root && module && signal && assertion,
        "coverage fixture publishes VPI objects");

    std::map<fsim_vpi_handle_v1, SystemVerilogVpiCoverageStatistics>
        statistics;
    statistics[module.value] = { 3U, 2U, 9U };
    statistics[signal.value] = { 2U, 2U, 7U };
    statistics[assertion.value]
        = { 1U, 1U, 4U, 5U, 4U, 1U, 2U, 1U, 3U };
    std::vector<SystemVerilogVpiCoverageControlRequest> controls;
    SystemVerilogVpiCoverageService service(objects,
        [&](const auto handle)
            -> std::optional<SystemVerilogVpiCoverageStatistics> {
            const auto found = statistics.find(handle);
            return found == statistics.end()
                ? std::nullopt
                : std::optional { found->second };
        },
        [&](const auto& request) {
            controls.push_back(request);
            return 1;
        });
    require(service.valid() && service.simulation_identity() == 101U,
        "coverage service retains simulation ownership");

    const std::array statement { SystemVerilogVpiCoverageType::Statement };
    const std::array toggle { SystemVerilogVpiCoverageType::Toggle };
    const std::array assert_type { SystemVerilogVpiCoverageType::Assertion };
    require(service.publish_target(module.value, statement)
            == SystemVerilogVpiCoverageError::None
        && service.publish_target(signal.value, toggle)
            == SystemVerilogVpiCoverageError::None
        && service.publish_target(assertion.value, assert_type)
            == SystemVerilogVpiCoverageError::None,
        "coverage targets publish transactionally");
    require(service.target_count() == 3U,
        "coverage target count is exact");
    require(service.property(
                SystemVerilogVpiCoverageProperty::StatementCoverage,
                module.value)
                .value
            == 1
        && service.property(
               SystemVerilogVpiCoverageProperty::ToggleCoverage,
               module.value)
               .value
            == 0,
        "coverage type properties distinguish metric families");
    require(service.property(
                SystemVerilogVpiCoverageProperty::CoveredMax,
                module.value)
                .value
            == 3
        && service.property(
               SystemVerilogVpiCoverageProperty::CoveredCount,
               module.value)
               .value
            == 9
        && service.property(
               SystemVerilogVpiCoverageProperty::Covered,
               module.value)
               .value
            == 0,
        "coverage status properties retain max, count, and all-covered state");
    require(service.property(
                SystemVerilogVpiCoverageProperty::AssertAttemptCovered,
                assertion.value)
                .value
            == 5
        && service.property(
               SystemVerilogVpiCoverageProperty::AssertSuccessCovered,
               assertion.value)
               .value
            == 4
        && service.property(
               SystemVerilogVpiCoverageProperty::AssertFailureCovered,
               assertion.value)
               .value
            == 1
        && service.property(
               SystemVerilogVpiCoverageProperty::AssertVacuousSuccessCovered,
               assertion.value)
               .value
            == 2
        && service.property(
               SystemVerilogVpiCoverageProperty::AssertDisableCovered,
               assertion.value)
               .value
            == 1
        && service.property(
               SystemVerilogVpiCoverageProperty::AssertKillCovered,
               assertion.value)
               .value
            == 3,
        "assertion-specific properties preserve independent counters");

    for (const auto control : {
             SystemVerilogVpiCoverageControl::Start,
             SystemVerilogVpiCoverageControl::Stop,
             SystemVerilogVpiCoverageControl::Reset,
             SystemVerilogVpiCoverageControl::Check }) {
        const auto result = service.control({ control,
            SystemVerilogVpiCoverageType::Statement, module.value, { } });
        require(result && result.value == 1,
            "every non-file coverage control reaches the provider");
    }
    for (const auto control : {
             SystemVerilogVpiCoverageControl::Merge,
             SystemVerilogVpiCoverageControl::Save }) {
        for (const auto type : {
                 SystemVerilogVpiCoverageType::Assertion,
                 SystemVerilogVpiCoverageType::FsmState,
                 SystemVerilogVpiCoverageType::Statement,
                 SystemVerilogVpiCoverageType::Toggle }) {
            const auto result = service.control(
                { control, type, std::nullopt, "run.fsimcov" });
            require(result && result.value == 1,
                "every standardized metric type reaches file controls");
        }
    }
    require(controls.size() == 12U
            && controls.front().object == module.value
            && controls.back().filename == "run.fsimcov",
        "coverage controls preserve every control, type, and argument");
    require(service.control({
                SystemVerilogVpiCoverageControl::Save,
                SystemVerilogVpiCoverageType::Statement,
                module.value,
                "run.fsimcov",
            })
                .error
            == SystemVerilogVpiCoverageError::InvalidRequest,
        "file controls reject an object selector");
    require(service.control({
                SystemVerilogVpiCoverageControl::Check,
                SystemVerilogVpiCoverageType::FsmState,
                module.value,
                { },
            })
                .error
            == SystemVerilogVpiCoverageError::NotFound,
        "control rejects an unavailable metric family");
    require(service.control({
                static_cast<SystemVerilogVpiCoverageControl>(749),
                SystemVerilogVpiCoverageType::Statement,
                module.value,
                { },
            })
                .error
            == SystemVerilogVpiCoverageError::InvalidControl
        && service.control({
               SystemVerilogVpiCoverageControl::Check,
               static_cast<SystemVerilogVpiCoverageType>(759),
               module.value,
               { },
           })
               .error
            == SystemVerilogVpiCoverageError::InvalidCoverageType,
        "unknown control and coverage-type identities are rejected");
    require(service.publish_target(module.value, statement)
            == SystemVerilogVpiCoverageError::DuplicateObject,
        "duplicate target publication is rejected");

    statistics[module.value].covered_items = 4U;
    require(service.property(
                SystemVerilogVpiCoverageProperty::Covered,
                module.value)
                .error
            == SystemVerilogVpiCoverageError::InvalidStatistics,
        "invalid provider statistics cannot escape the service");
    statistics[module.value] = { 1U, 1U,
        static_cast<std::uint64_t>(INT32_MAX) + 1U };
    require(service.property(
                SystemVerilogVpiCoverageProperty::CoveredCount,
                module.value)
                .error
            == SystemVerilogVpiCoverageError::Overflow,
        "VPI integer property overflow is explicit");
}

void test_provider_failure_containment()
{
    SystemVerilogVpiObjectRegistry objects { 404U };
    const auto root = objects.create(
        SystemVerilogVpiObjectKind::Root, 0U, "top");
    require(static_cast<bool>(root),
        "provider containment fixture publishes a root");
    SystemVerilogVpiCoverageService service(objects,
        [](const auto)
            -> std::optional<SystemVerilogVpiCoverageStatistics> {
            throw std::runtime_error { "statistics failure" };
        },
        [](const auto&) -> std::int32_t {
            throw std::runtime_error { "control failure" };
        });
    const std::array types {
        SystemVerilogVpiCoverageType::Assertion,
        SystemVerilogVpiCoverageType::FsmState,
        SystemVerilogVpiCoverageType::Statement,
        SystemVerilogVpiCoverageType::Toggle,
    };
    require(service.publish_target(root.value, types)
            == SystemVerilogVpiCoverageError::None
        && service.property(SystemVerilogVpiCoverageProperty::CoveredMax,
               root.value)
               .error
            == SystemVerilogVpiCoverageError::ProviderFailure
        && service.control({ SystemVerilogVpiCoverageControl::Check,
               SystemVerilogVpiCoverageType::Statement, root.value, { } })
               .error
            == SystemVerilogVpiCoverageError::ProviderFailure,
        "statistics and control exceptions are contained at the API boundary");
}

void test_fsm_relations_values_and_iterators()
{
    SystemVerilogVpiObjectRegistry objects { 202U };
    const auto root = objects.create(
        SystemVerilogVpiObjectKind::Root, 0U, "top");
    const auto module = objects.create(
        SystemVerilogVpiObjectKind::Module, root.value, "dut");
    SystemVerilogVpiObjectDescriptor expression_descriptor;
    expression_descriptor.kind = SystemVerilogVpiObjectKind::Variable;
    expression_descriptor.parent = module.value;
    expression_descriptor.name = "state";
    expression_descriptor.type.emplace();
    expression_descriptor.type->category
        = SystemVerilogVpiValueCategory::Bit2;
    expression_descriptor.type->width = 2U;
    const auto expression = objects.create(expression_descriptor);
    require(root && module && expression,
        "FSM fixture publishes hierarchy and state expression");
    std::map<fsim_vpi_handle_v1, SystemVerilogVpiCoverageStatistics>
        statistics;
    SystemVerilogVpiCoverageService service(objects,
        [&](const auto handle)
            -> std::optional<SystemVerilogVpiCoverageStatistics> {
            const auto found = statistics.find(handle);
            return found == statistics.end()
                ? std::nullopt
                : std::optional { found->second };
        },
        [](const auto&) { return 1; });
    auto fsm = service.publish_fsm({
        module.value,
        expression.value,
        "controller",
        {
            { "idle", state_value("00") },
            { "busy", state_value("01") },
        },
    });
    require(fsm && service.fsm_count() == 1U
            && service.state_count() == 2U,
        "FSM publication creates one machine and exact legal states");
    require(service.handle(
                SystemVerilogVpiCoverageRelation::FsmHandle,
                expression.value)
                .value
            == fsm.value
        && service.handle(
               SystemVerilogVpiCoverageRelation::FsmStateExpression,
               fsm.value)
               .value
            == expression.value,
        "FSM relations are bidirectional");

    const auto fsm_iterator = service.iterate(
        SystemVerilogVpiCoverageRelation::Fsm, module.value);
    require(objects.lookup(fsm.value).error
                == SystemVerilogVpiObjectError::InvalidHandle
            && objects.scan(fsm_iterator.value).error
                == SystemVerilogVpiIteratorError::InvalidHandle,
        "coverage handles remain disjoint from ordinary VPI handles");
    require(fsm_iterator && service.scan(fsm_iterator.value).value == fsm.value
            && service.scan(fsm_iterator.value).error
                == SystemVerilogVpiCoverageError::End,
        "instance FSM traversal is creation ordered and bounded");
    const auto state_iterator = service.iterate(
        SystemVerilogVpiCoverageRelation::FsmStates, fsm.value);
    require(static_cast<bool>(state_iterator),
        "FSM state iterator is published");
    const auto idle = service.scan(state_iterator.value);
    const auto busy = service.scan(state_iterator.value);
    require(idle && busy
            && service.scan(state_iterator.value).error
                == SystemVerilogVpiCoverageError::End,
        "FSM state traversal returns every legal state exactly once");
    const auto idle_value = service.value(idle.value);
    const auto busy_value = service.value(busy.value);
    require(idle_value && busy_value
            && std::get<PackedBit2>(idle_value.value->payload).to_msb_string()
                == "00"
            && std::get<PackedBit2>(busy_value.value->payload).to_msb_string()
                == "01",
        "FSM state value queries preserve exact state encodings");

    statistics[fsm.value] = { 2U, 1U, 3U };
    statistics[idle.value] = { 1U, 1U, 2U };
    statistics[busy.value] = { 1U, 0U, 0U };
    require(service.property(
                SystemVerilogVpiCoverageProperty::FsmStateCoverage,
                fsm.value)
                .value
            == 1
        && service.property(
               SystemVerilogVpiCoverageProperty::CoveredMax,
               fsm.value)
               .value
            == 2
        && service.property(
               SystemVerilogVpiCoverageProperty::Covered,
               idle.value)
               .value
            == 1
        && service.property(
               SystemVerilogVpiCoverageProperty::Covered,
               busy.value)
               .value
            == 0,
        "FSM and state handles expose standard coverage properties");
    require(service.release_iterator(state_iterator.value)
            == SystemVerilogVpiCoverageError::None
        && service.scan(state_iterator.value).error
            == SystemVerilogVpiCoverageError::ReleasedHandle,
        "released coverage iterators reject subsequent scans");

    SystemVerilogVpiObjectRegistry foreign_objects { 303U };
    const auto foreign_root = foreign_objects.create(
        SystemVerilogVpiObjectKind::Root, 0U, "foreign");
    require(service.iterate(
                SystemVerilogVpiCoverageRelation::Fsm,
                foreign_root.value)
                .error
            == SystemVerilogVpiCoverageError::CrossSimulation,
        "coverage traversal rejects cross-simulation object handles");
    require(service.publish_fsm({
                module.value,
                expression.value,
                "duplicate",
                { { "only", state_value("00") } },
            })
                .error
            == SystemVerilogVpiCoverageError::DuplicateObject,
        "one state expression cannot ambiguously own multiple FSMs");
    expression_descriptor.name = "state_alias";
    const auto second_expression = objects.create(expression_descriptor);
    require(static_cast<bool>(second_expression),
        "duplicate FSM value fixture publishes a second expression");
    require(service.publish_fsm({
                module.value,
                second_expression.value,
                "ambiguous",
                {
                    { "zero", state_value("00") },
                    { "also_zero", state_value("00") },
                },
            })
                .error
            == SystemVerilogVpiCoverageError::DuplicateState,
        "FSM publication rejects duplicate legal-state encodings");

    SystemVerilogVpiCoverageLimits limits;
    limits.maximum_states = 1U;
    SystemVerilogVpiCoverageService bounded(objects,
        [](const auto)
            -> std::optional<SystemVerilogVpiCoverageStatistics> {
            return std::nullopt;
        },
        [](const auto&) { return 1; }, limits);
    require(bounded.publish_fsm({
                module.value,
                expression.value,
                "bounded",
                {
                    { "zero", state_value("0") },
                    { "one", state_value("1") },
                },
            })
                .error
            == SystemVerilogVpiCoverageError::ResourceLimit
        && bounded.fsm_count() == 0U,
        "FSM resource failure publishes no partial machine");

    limits.maximum_states = 2U;
    SystemVerilogVpiCoverageService cumulative(objects,
        [](const auto)
            -> std::optional<SystemVerilogVpiCoverageStatistics> {
            return std::nullopt;
        },
        [](const auto&) { return 1; }, limits);
    const auto first = cumulative.publish_fsm({
        module.value,
        expression.value,
        "first",
        {
            { "zero", state_value("00") },
            { "one", state_value("01") },
        },
    });
    require(static_cast<bool>(first)
            && cumulative.publish_fsm({
                   module.value,
                   second_expression.value,
                   "second",
                   { { "two", state_value("10") } },
               })
                    .error
                == SystemVerilogVpiCoverageError::ResourceLimit
            && cumulative.fsm_count() == 1U
            && cumulative.state_count() == 2U,
        "FSM state ceiling is cumulative and transactional");
}

} // namespace

int main()
{
    test_properties_controls_and_failures();
    test_fsm_relations_values_and_iterators();
    test_provider_failure_containment();
}
