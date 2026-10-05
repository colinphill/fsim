// SPDX-License-Identifier: Apache-2.0
#include "../../src/runtime/simir_process_program.hpp"
#include "../../src/runtime/simir_process_storage.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"
#include "runtime_test_support.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using fsim::runtime::simir::ProcessStateStorage;

struct Record {
    inline static int constructions_before_failure { -1 };
    std::unique_ptr<int> value;

    Record()
    {
        if (constructions_before_failure == 0) {
            throw std::bad_alloc { };
        }
        if (constructions_before_failure > 0) {
            --constructions_before_failure;
        }
        value = std::make_unique<int>();
    }
    Record(Record&&) noexcept = default;
    Record& operator=(Record&&) noexcept = default;
};

void check_storage_addresses_and_failure()
{
    ProcessStateStorage<Record> records;
    records.reserve_initial(4U);
    std::vector<const Record*> addresses;
    for (std::size_t index { }; index < 1000U; ++index) {
        Record next;
        *next.value = static_cast<int>(index);
        records.push_back(std::move(next));
        addresses.push_back(&records[index]);
        for (std::size_t earlier { }; earlier <= index; ++earlier) {
            assert(&records[earlier] == addresses[earlier]);
            assert(*records[earlier].value == static_cast<int>(earlier));
        }
    }
    for (std::size_t index { }; index < 4U; ++index) {
        assert(&records[index] == &records[0U] + index);
    }
    std::size_t visited { };
    for (const auto& record : std::as_const(records)) {
        assert(&record == addresses[visited]);
        ++visited;
    }
    assert(visited == records.size());
    bool rejected { };
    try {
        records.reserve_initial(2000U);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    assert(rejected);
    Record::constructions_before_failure = 2;
    bool allocation_failed { };
    try {
        records.resize(1004U);
    } catch (const std::bad_alloc&) {
        allocation_failed = true;
    }
    Record::constructions_before_failure = -1;
    assert(allocation_failed);
    assert(records.size() == 1000U);
    for (std::size_t index { }; index < records.size(); ++index) {
        assert(&records[index] == addresses[index]);
        assert(*records[index].value == static_cast<int>(index));
    }
    records.resize(1004U);
    assert(records.size() == 1004U);
    bool bounds_rejected { };
    try {
        (void)records.at(1004U);
    } catch (const std::out_of_range&) {
        bounds_rejected = true;
    }
    assert(bounds_rejected);
    records.resize(2U);
    assert(&records[0U] == addresses[0U]);
    assert(&records[1U] == addresses[1U]);
    records.resize(6U);
    assert(&records[0U] == addresses[0U]);
    assert(&records[1U] == addresses[1U]);
}

void check_interpreter_registration()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Interpreter interpreter;
    interpreter.reserve_process_capacity(3U);
    auto& implementation = OwnedDriverDemotionTestAccess::implementation(interpreter);
    std::vector<const void*> states;
    std::vector<const bool*> queued;
    for (ProcessId index { }; index < 8U; ++index) {
        Process process;
        process.id = index;
        process.name = "slab." + std::to_string(index);
        process.operations = { Halt { } };
        assert(interpreter.add_process(std::move(process)) == index);
        states.push_back(&implementation.processes[index]);
        queued.push_back(&implementation.processes[index].queued);
        for (ProcessId earlier { }; earlier <= index; ++earlier) {
            assert(states[earlier] == &implementation.processes[earlier]);
            assert(queued[earlier] == &implementation.processes[earlier].queued);
        }
    }
    for (std::size_t index { }; index < 3U; ++index) {
        assert(&implementation.processes[index]
            == &implementation.processes[0U] + index);
    }
    bool rejected { };
    try {
        interpreter.reserve_process_capacity(16U);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    assert(rejected);
    interpreter.start();
    assert(interpreter.run().status == RunStatus::completed);
    for (ProcessId index { }; index < 8U; ++index) {
        assert(states[index] == &implementation.processes[index]);
        assert(implementation.processes[index].halted);
        assert(!*queued[index]);
    }
    Interpreter empty;
    empty.start();
    rejected = false;
    try {
        empty.reserve_process_capacity(1U);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    assert(rejected);
}

void check_process_program_sharing_and_facade()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Interpreter interpreter;
    const auto first_output = interpreter.add_signal(Signal {
        "program.first", PackedLogic4 { 4U, Logic4::zero } });
    const auto second_output = interpreter.add_signal(Signal {
        "program.second", PackedLogic4 { 4U, Logic4::zero } });

    Process first;
    first.id = 0U;
    first.name = "program.first_owner";
    first.register_count = 1U;
    first.register_value_kinds = { ValueKind::logic4 };
    DebugLocal local;
    local.name = "state";
    local.type_name = "logic";
    local.register_id = 0U;
    local.width = 1U;
    first.debug_locals = { local };
    first.language_standard = "systemverilog-2023";
    first.compatibility_profile = "sampled";
    first.operations = { Halt { } };
    first.static_sensitivity = { { first_output, EdgeKind::any } };
    first.driver_regions = { { first_output, 0U, 0U, true } };

    auto sibling = first;
    sibling.id = 1U;
    sibling.name = "program.second_owner";
    sibling.static_sensitivity = { { second_output, EdgeKind::any } };
    sibling.driver_regions = { { second_output, 0U, 0U, true } };
    sibling.initialize = false;

    auto incompatible = first;
    incompatible.id = 2U;
    incompatible.name = "program.other_domain";
    incompatible.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;

    assert(interpreter.add_process(std::move(first)) == 0U);
    assert(interpreter.add_process(std::move(sibling)) == 1U);
    assert(interpreter.add_process(std::move(incompatible)) == 2U);

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto& first_view = implementation.processes[0U].program();
    const auto& sibling_view = implementation.processes[1U].program();
    const auto& incompatible_view = implementation.processes[2U].program();
    assert(first_view.common_identity() == sibling_view.common_identity());
    assert(first_view.common_identity() != incompatible_view.common_identity());
    assert(first_view.id() == 0U && sibling_view.id() == 1U);
    assert(first_view.name() == "program.first_owner");
    assert(sibling_view.name() == "program.second_owner");
    assert(first_view.static_sensitivity().front().signal == first_output);
    assert(sibling_view.static_sensitivity().front().signal == second_output);
    assert(first_view.driver_regions().front().signal == first_output);
    assert(sibling_view.driver_regions().front().signal == second_output);
    assert(first_view.initialize());
    assert(!sibling_view.initialize());
    assert(!implementation.processes.public_facade_materialized(0U));
    assert(implementation.processes[0U].program().driver_regions().size()
        == 1U);
    assert(!implementation.processes.public_facade_materialized(0U));

    const auto setup_view = InterpreterProgramAccess::view(interpreter, 0U);
    assert(setup_view.valid());
    assert(setup_view.common_identity() == first_view.common_identity());
    assert(setup_view.operations().body_identity()
        == first_view.operations().body_identity());
    assert(!implementation.processes.public_facade_materialized(0U));

    const auto* const first_facade = &interpreter.process_program(0U);
    assert(implementation.processes[0U].cold().program_storage()
               .public_program_facade.get()
        == first_facade);
    const auto* const sibling_facade = &interpreter.process_program(1U);
    assert(first_facade == &interpreter.process_program(0U));
    assert(sibling_facade == &interpreter.process_program(1U));
    assert(first_facade->id == 0U);
    assert(first_facade->name == "program.first_owner");
    assert(first_facade->register_count == 1U);
    assert(first_facade->language_standard == "systemverilog-2023");
    assert(first_facade->compatibility_profile == "sampled");
    assert(first_facade->debug_locals.size() == 1U);
    assert(first_facade->debug_locals.front().name == "state");
    assert(first_facade->register_value_kinds
        == std::vector<ValueKind> { ValueKind::logic4 });
    assert(first_facade->static_sensitivity.front().signal == first_output);
    assert(first_facade->driver_regions.front().signal == first_output);
    assert(sibling_facade->id == 1U);
    assert(sibling_facade->name == "program.second_owner");
    assert(!sibling_facade->initialize);
    assert(sibling_facade->static_sensitivity.front().signal == second_output);
    assert(sibling_facade->driver_regions.front().signal == second_output);

    auto detached = *first_facade;
    detached.name = "program.detached_copy";
    detached.register_value_kinds[0U] = ValueKind::logic9;
    detached.static_sensitivity.front().signal = second_output;
    assert(first_facade->name == "program.first_owner");
    assert(first_facade->register_value_kinds[0U] == ValueKind::logic4);
    assert(first_facade->static_sensitivity.front().signal == first_output);
    assert(sibling_facade->name == "program.second_owner");
    assert(sibling_facade->register_value_kinds[0U] == ValueKind::logic4);
    assert(sibling_facade->static_sensitivity.front().signal == second_output);
}

[[nodiscard]] fsim::runtime::simir::DebugLocal make_logic_debug_local(
    std::string name, const fsim::runtime::simir::RegisterId register_id)
{
    fsim::runtime::simir::DebugLocal local;
    local.name = std::move(name);
    local.type_name = "logic";
    local.register_id = register_id;
    local.width = 1U;
    return local;
}

[[nodiscard]] fsim::runtime::simir::Process make_sampled_writer(
    const fsim::runtime::simir::ProcessId id,
    std::string name,
    const fsim::runtime::simir::SignalId data,
    const fsim::runtime::simir::SignalId clock,
    const fsim::runtime::simir::SignalId gate,
    const fsim::runtime::simir::SignalId output,
    const bool invert)
{
    using namespace fsim::runtime::simir;
    Process process;
    process.id = id;
    process.name = std::move(name);
    process.register_count = 2U;
    process.register_value_kinds
        = { ValueKind::logic4, ValueKind::logic4 };
    process.debug_locals = {
        make_logic_debug_local("sampled", 0U),
        make_logic_debug_local("result", 1U),
    };
    process.static_sensitivity = {
        { data, EdgeKind::any },
        { clock, EdgeKind::any },
        { gate, EdgeKind::any },
    };
    ReadSignal read;
    read.destination = 0U;
    read.signal = data;
    read.kind = SignalReadKind::sampled;
    read.clock = clock;
    read.gate = gate;
    if (invert) {
        process.operations = {
            read,
            UnaryNot { 1U, 0U },
            WriteUpdate { output, 1U },
            Halt { },
        };
    } else {
        process.operations = {
            read,
            CopyRegister { 1U, 0U },
            WriteUpdate { output, 1U },
            Halt { },
        };
    }
    return process;
}

[[nodiscard]] fsim::runtime::simir::Process make_transaction_probe()
{
    using namespace fsim::runtime::simir;
    Process process;
    process.id = 3U;
    process.name = "direct.transaction_probe";
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    process.debug_locals = {
        make_logic_debug_local("active", 0U),
    };
    process.static_sensitivity = {
        { 13U, EdgeKind::transaction },
        { 14U, EdgeKind::transaction },
    };
    process.operations = {
        SignalActive { 0U, 12U },
        WaitOn { std::vector<SignalId> { 13U },
            std::vector<EdgeKind> { EdgeKind::any } },
        Halt { },
    };
    return process;
}

void check_direct_template_instance_registration()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter direct;
    Interpreter legacy;
    std::vector<Signal> definitions;
    definitions.reserve(15U);
    const auto add_signal = [&](
        std::string name,
        const Logic4 initial,
        const ResolutionKind resolution = ResolutionKind::none) {
        Signal signal {
            std::move(name), PackedLogic4 { 1U, initial }, resolution,
            ValueKind::logic4
        };
        const auto expected = static_cast<SignalId>(definitions.size());
        assert(direct.add_signal(signal) == expected);
        assert(legacy.add_signal(signal) == expected);
        definitions.push_back(std::move(signal));
        return expected;
    };
    const auto data0 = add_signal("direct.data0", Logic4::one);
    const auto clock0 = add_signal("direct.clock0", Logic4::zero);
    const auto gate0 = add_signal("direct.gate0", Logic4::one);
    const auto output0 = add_signal(
        "direct.output0", Logic4::zero, ResolutionKind::sv_wire);
    const auto data1 = add_signal("direct.data1", Logic4::zero);
    const auto clock1 = add_signal("direct.clock1", Logic4::zero);
    const auto gate1 = add_signal("direct.gate1", Logic4::one);
    const auto output1 = add_signal(
        "direct.output1", Logic4::zero, ResolutionKind::sv_wire);
    const auto data2 = add_signal("direct.data2", Logic4::one);
    const auto clock2 = add_signal("direct.clock2", Logic4::zero);
    const auto gate2 = add_signal("direct.gate2", Logic4::one);
    const auto output2 = add_signal(
        "direct.output2", Logic4::zero, ResolutionKind::sv_wire);
    const auto active_signal = add_signal("direct.active_signal", Logic4::zero);
    const auto wait_signal = add_signal("direct.wait_signal", Logic4::zero);
    const auto sensitivity_signal
        = add_signal("direct.sensitivity_signal", Logic4::zero);

    auto first = make_sampled_writer(
        0U, "direct.first", data0, clock0, gate0, output0, false);
    auto sibling = make_sampled_writer(
        1U, "direct.sibling", data1, clock1, gate1, output1, false);
    auto independent = make_sampled_writer(
        2U, "direct.independent", data2, clock2, gate2, output2, true);
    auto transaction_probe = make_transaction_probe();
    assert(share_process_operations(first, sibling, definitions));
    assert(first.operations.shares_body_with(sibling.operations));
    assert(!first.operations.shares_body_with(independent.operations));

    const auto common
        = std::make_shared<const ProcessProgramTemplate>(first);
    const auto transaction_common
        = std::make_shared<const ProcessProgramTemplate>(transaction_probe);
    const auto first_instance = ProcessInstanceProgram { first };
    const auto sibling_instance = ProcessInstanceProgram { sibling };
    const auto independent_instance = ProcessInstanceProgram { independent };
    const auto transaction_instance
        = ProcessInstanceProgram { transaction_probe };

    auto& direct_impl = OwnedDriverDemotionTestAccess::implementation(direct);
    bool null_template_rejected { };
    try {
        (void)InterpreterProgramAccess::add_program(direct,
            std::shared_ptr<const ProcessProgramTemplate> { },
            first_instance);
    } catch (const std::invalid_argument&) {
        null_template_rejected = true;
    }
    assert(null_template_rejected);
    assert(direct_impl.processes.empty());

    assert(InterpreterProgramAccess::add_program(
               direct, common, first_instance)
        == 0U);
    auto malformed_sibling = sibling_instance;
    malformed_sibling.static_sensitivity.front().signal
        = static_cast<SignalId>(definitions.size());
    bool malformed_row_rejected { };
    try {
        (void)InterpreterProgramAccess::add_program(
            direct, common, malformed_sibling);
    } catch (const std::invalid_argument&) {
        malformed_row_rejected = true;
    }
    assert(malformed_row_rejected);
    assert(direct_impl.processes.size() == 1U);
    assert(direct_impl.signal_writer_counts[output1] == 0U);
    assert(direct_impl.driver_values[output1].find(1U) == nullptr);
    assert(InterpreterProgramAccess::add_program(
               direct, common, sibling_instance)
        == 1U);
    assert(InterpreterProgramAccess::add_program(
               direct, common, independent_instance)
        == 2U);
    assert(InterpreterProgramAccess::add_program(
               direct, transaction_common, transaction_instance)
        == 3U);

    Process legacy_first = first;
    Process legacy_sibling = sibling;
    Process legacy_independent = independent;
    Process legacy_transaction_probe = transaction_probe;
    assert(legacy.add_process(std::move(legacy_first)) == 0U);
    assert(legacy.add_process(std::move(legacy_sibling)) == 1U);
    assert(legacy.add_process(std::move(legacy_independent)) == 2U);
    assert(legacy.add_process(std::move(legacy_transaction_probe)) == 3U);

    const auto first_view = InterpreterProgramAccess::view(direct, 0U);
    const auto sibling_view = InterpreterProgramAccess::view(direct, 1U);
    const auto independent_view = InterpreterProgramAccess::view(direct, 2U);
    const auto transaction_view = InterpreterProgramAccess::view(direct, 3U);
    assert(first_view.common_identity() == common.get());
    assert(sibling_view.common_identity() == common.get());
    assert(independent_view.common_identity() == common.get());
    assert(transaction_view.common_identity() == transaction_common.get());
    assert(first_view.operations().shares_body_with(
        sibling_view.operations()));
    assert(!first_view.operations().shares_body_with(
        independent_view.operations()));
    assert(sibling_view.operations().signal_remap().empty());
    const auto overrides
        = sibling_view.operations().instance_operation_overrides();
    assert(overrides.size() == 2U);
    const auto* const mapped_read
        = operation_get_if<ReadSignal>(&sibling_view.operations()[0U]);
    const auto* const mapped_write
        = operation_get_if<WriteUpdate>(&sibling_view.operations()[2U]);
    assert(mapped_read != nullptr
        && mapped_read->signal == data1
        && mapped_read->clock == clock1
        && mapped_read->gate == gate1);
    assert(mapped_write != nullptr && mapped_write->signal == output1);
    assert(first_view.id() == 0U && sibling_view.id() == 1U
        && independent_view.id() == 2U);
    assert(first_view.name() == "direct.first");
    assert(sibling_view.name() == "direct.sibling");
    assert(independent_view.name() == "direct.independent");
    assert(first_view.static_sensitivity().front().signal == data0);
    assert(sibling_view.static_sensitivity().front().signal == data1);
    assert(independent_view.static_sensitivity().front().signal == data2);
    assert(first_view.driver_regions().empty());
    assert(sibling_view.driver_regions().empty());
    assert(independent_view.driver_regions().empty());

    auto& legacy_impl = OwnedDriverDemotionTestAccess::implementation(legacy);
    assert(direct_impl.sampled_value_dependency_mask
        == legacy_impl.sampled_value_dependency_mask);
    assert(direct_impl.signal_transaction_observed
        == legacy_impl.signal_transaction_observed);
    assert(direct_impl.signal_writer_counts
        == legacy_impl.signal_writer_counts);
    for (const auto signal : {
             data0, clock0, gate0, data1, clock1, gate1, data2, clock2, gate2 }) {
        assert(direct_impl.sampled_value_dependency_mask[signal] == 1U);
    }
    assert(direct_impl.requires_sampled_values);
    assert(direct_impl.sampled_value_dependency_mask[output0] == 0U);
    assert(direct_impl.sampled_value_dependency_mask[output1] == 0U);
    assert(direct_impl.sampled_value_dependency_mask[output2] == 0U);
    assert(direct_impl.signal_transaction_observed[active_signal]);
    assert(direct_impl.signal_transaction_observed[wait_signal]);
    assert(direct_impl.signal_transaction_observed[sensitivity_signal]);
    for (const auto& [signal, owner] : {
             std::pair { output0, ProcessId { 0U } },
             std::pair { output1, ProcessId { 1U } },
             std::pair { output2, ProcessId { 2U } } }) {
        assert(direct_impl.signal_writer_counts[signal] == 1U);
        assert(direct_impl.stable_single_writer_processes[signal] == owner);
        assert(direct_impl.driver_values[signal].find(owner) != nullptr);
    }
    for (ProcessId process { }; process < 4U; ++process) {
        assert(!direct_impl.processes.public_facade_materialized(process));
    }
    auto invalid_validation_row = first_instance;
    invalid_validation_row.id = 4U;
    bool mixed_validation_rejected { };
    try {
        (void)InterpreterProgramAccess::validate_program(
            direct, common, invalid_validation_row);
    } catch (const std::logic_error&) {
        mixed_validation_rejected = true;
    }
    assert(mixed_validation_rejected);
    assert(direct_impl.processes.size() == 4U);

    direct.start();
    legacy.start();
    const auto direct_result = direct.run();
    const auto legacy_result = legacy.run();
    assert(direct_result.status == RunStatus::completed);
    assert(direct_result.status == legacy_result.status);
    assert(direct_result.time == legacy_result.time);
    assert(direct_result.delta == legacy_result.delta);
    assert(direct_result.callbacks_executed
        == legacy_result.callbacks_executed);
    for (const auto signal : { output0, output1, output2 }) {
        assert(direct.signal_value(signal) == legacy.signal_value(signal));
    }
    for (ProcessId process { }; process < 3U; ++process) {
        assert(direct.read_debug_local(process, 0U)
            == legacy.read_debug_local(process, 0U));
        assert(direct.read_debug_local(process, 1U)
            == legacy.read_debug_local(process, 1U));
    }
    assert(direct.read_debug_local(3U, 0U)
        == legacy.read_debug_local(3U, 0U));
    for (ProcessId process { }; process < 4U; ++process) {
        assert(!direct_impl.processes.public_facade_materialized(process));
    }
    const auto& explicit_facade = direct.process_program(0U);
    assert(explicit_facade.id == 0U);
    assert(explicit_facade.name == "direct.first");
    assert(direct_impl.processes.public_facade_materialized(0U));
    for (ProcessId process { 1U }; process < 4U; ++process) {
        assert(!direct_impl.processes.public_facade_materialized(process));
    }
}

void check_direct_template_validation_mode()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Process source;
    source.id = 0U;
    source.name = "direct.validation";
    source.operations = { Halt { } };
    const auto common
        = std::make_shared<const ProcessProgramTemplate>(source);
    const auto valid_row = ProcessInstanceProgram { source };

    Interpreter validation_only;
    auto malformed_row = valid_row;
    malformed_row.static_sensitivity = {
        { 0U, EdgeKind::any },
    };
    bool malformed_validation_rejected { };
    try {
        (void)InterpreterProgramAccess::validate_program(
            validation_only, common, malformed_row);
    } catch (const std::invalid_argument&) {
        malformed_validation_rejected = true;
    }
    assert(malformed_validation_rejected);
    auto& validation_impl
        = OwnedDriverDemotionTestAccess::implementation(validation_only);
    assert(validation_impl.processes.empty());
    bool executable_add_rejected { };
    try {
        (void)InterpreterProgramAccess::add_program(
            validation_only, common, valid_row);
    } catch (const std::logic_error&) {
        executable_add_rejected = true;
    }
    assert(executable_add_rejected);
    assert(InterpreterProgramAccess::validate_program(
               validation_only, common, valid_row)
        == 0U);
    Process legacy_attempt = source;
    bool legacy_add_rejected { };
    try {
        (void)validation_only.add_process(std::move(legacy_attempt));
    } catch (const std::logic_error&) {
        legacy_add_rejected = true;
    }
    assert(legacy_add_rejected);
    bool direct_add_after_validation_rejected { };
    try {
        auto next_row = valid_row;
        next_row.id = 1U;
        (void)InterpreterProgramAccess::add_program(
            validation_only, common, std::move(next_row));
    } catch (const std::logic_error&) {
        direct_add_after_validation_rejected = true;
    }
    assert(direct_add_after_validation_rejected);

    Interpreter executable;
    assert(InterpreterProgramAccess::add_program(
               executable, common, valid_row)
        == 0U);
    auto& executable_impl
        = OwnedDriverDemotionTestAccess::implementation(executable);
    assert(!executable_impl.processes.public_facade_materialized(0U));
}

void check_vital_reannotation_updates_materialized_facade()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Interpreter interpreter;
    Process first;
    first.id = 0U;
    first.name = "program.vital_first";
    first.register_count = 1U;
    first.register_value_kinds = { ValueKind::logic4 };
    VitalDelay delay;
    delay.default_delays[0U] = 0U;
    first.operations = {
        LoadConstant { 0U,
            PackedLogic4::from_aval_bval(64U, 0U, 0U) },
        delay,
        Halt { },
    };
    auto sibling = first;
    sibling.id = 1U;
    sibling.name = "program.vital_sibling";

    assert(interpreter.add_process(std::move(first)) == 0U);
    assert(interpreter.add_process(std::move(sibling)) == 1U);
    const auto* const first_facade = &interpreter.process_program(0U);
    const auto* const sibling_facade = &interpreter.process_program(1U);
    const auto* initial_load = operation_get_if<LoadConstant>(
        &first_facade->operations[0U]);
    assert(initial_load != nullptr
        && initial_load->value
            == PackedLogic4::from_aval_bval(64U, 0U, 0U));

    Interpreter::VitalTimingReannotation annotation;
    annotation.process = 0U;
    annotation.instruction = 1U;
    annotation.values[0U] = 17U;
    annotation.value_count = 1U;
    interpreter.reannotate_vital_timing(
        std::span { &annotation, std::size_t { 1U } });

    assert(&interpreter.process_program(0U) == first_facade);
    const auto* updated_load = operation_get_if<LoadConstant>(
        &first_facade->operations[0U]);
    assert(updated_load != nullptr
        && updated_load->value
            == PackedLogic4::from_aval_bval(64U, 17U, 0U));
    const auto* sibling_load = operation_get_if<LoadConstant>(
        &sibling_facade->operations[0U]);
    assert(sibling_load != nullptr
        && sibling_load->value
            == PackedLogic4::from_aval_bval(64U, 0U, 0U));
}

void check_fork_overflow()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Interpreter interpreter;
    interpreter.reserve_process_capacity(1U);
    Process parent;
    parent.id = 0U;
    parent.name = "slab.fork";
    parent.operations = {
        Fork { { 2U, 4U }, ForkJoinKind::all }, Halt { },
        WaitFor { 1U }, ForkEnd { },
        WaitFor { 2U }, ForkEnd { },
    };
    assert(interpreter.add_process(std::move(parent)) == 0U);
    auto& implementation = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto* const parent_state = &implementation.processes[0U];
    const auto* const parent_program = &interpreter.process_program(0U);
    const auto result = interpreter.run();
    assert(result.status == RunStatus::completed && result.time == 2U);
    assert(implementation.processes.size() == 3U);
    assert(&implementation.processes[0U] == parent_state);
    assert(&interpreter.process_program(0U) == parent_program);
    assert(implementation.processes[0U].halted);
    const auto parent_common
        = implementation.processes[0U].program().common_identity();
    assert(implementation.processes[1U].program().common_identity()
        == parent_common);
    assert(implementation.processes[2U].program().common_identity()
        == parent_common);
    assert(implementation.processes[1U].program().name()
        == "slab.fork.$fork[0].child[0]");
    assert(implementation.processes[2U].program().name()
        == "slab.fork.$fork[0].child[1]");
    assert(!implementation.processes[1U].program().initialize());
    assert(!implementation.processes[2U].program().initialize());
    assert(!implementation.processes[1U].program().final());
    assert(!implementation.processes[2U].program().final());
    assert(implementation.processes[1U].program().operations().body_identity()
        == implementation.processes[0U].program().operations().body_identity());
    assert(interpreter.process_program(0U).name == "slab.fork");
    assert(interpreter.process_program(1U).name
        == "slab.fork.$fork[0].child[0]");
    assert(interpreter.process_program(2U).name
        == "slab.fork.$fork[0].child[1]");
}

} // namespace

void test_process_state_storage()
{
    check_storage_addresses_and_failure();
    check_interpreter_registration();
    check_process_program_sharing_and_facade();
    check_direct_template_instance_registration();
    check_direct_template_validation_mode();
    check_vital_reannotation_updates_materialized_facade();
    check_fork_overflow();
}

} // namespace fsim::tests::runtime
