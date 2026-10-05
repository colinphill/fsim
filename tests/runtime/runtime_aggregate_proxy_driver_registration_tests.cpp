// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] PackedLogic4 packed(const std::string_view bits)
{
    return PackedLogic4::from_msb_string(bits);
}

[[nodiscard]] ContainerType packed_array_type()
{
    ContainerType type;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = 8U;
    type.fixed = true;
    type.index_left = 1;
    type.index_right = 0;
    type.dimensions = { { 1, 0 } };
    return type;
}

class RegistrationWriter final : public ProcessExecutor {
public:
    using Write = std::function<void(ProcessExecutionContext&)>;

    explicit RegistrationWriter(Write write)
        : write_ { std::move(write) }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "proxy registration writer resumed away from entry"
            };
        }
        write_(context);
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    Write write_;
};

struct ProxyRegistrationFixture {
    Interpreter interpreter;
    SignalId first { };
    SignalId second { };
    SignalId proxy { };
    ContainerObjectId object { };
    ProcessId writer { };

    explicit ProxyRegistrationFixture(const bool proxy_writable = true)
    {
        const auto type = packed_array_type();
        first = interpreter.add_signal({
            "top.registration_array[1]", packed("ZZZZZZZZ"),
            ResolutionKind::sv_wire
        });
        second = interpreter.add_signal({
            "top.registration_array[0]", packed("ZZZZZZZZ"),
            ResolutionKind::sv_wire
        });
        proxy = interpreter.add_signal({
            "top.registration_array", packed("ZZZZZZZZZZZZZZZZ"),
            ResolutionKind::sv_wire
        });
        object = interpreter.add_container_object({
            "top.registration_array",
            ContainerValue { type, { packed("ZZZZZZZZ"), packed("ZZZZZZZZ") }, { } },
            std::nullopt
        });
        interpreter.add_container_element_signal_alias(
            { object, 0U, first, true, true });
        interpreter.add_container_element_signal_alias(
            { object, 1U, second, true, true });
        interpreter.add_container_aggregate_signal_alias(
            { object, proxy, true, proxy_writable });
    }

    [[nodiscard]] ProcessId add_writer(
        const Process::DriverRegion region)
    {
        Process process;
        process.id = 0U;
        process.name = "top.registration_array.writer";
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.driver_regions = { region };
        process.operations = { Halt { } };
        writer = interpreter.add_process(std::move(process));
        return writer;
    }

    void install(RegistrationWriter::Write write)
    {
        interpreter.set_process_executor(
            writer, std::make_unique<RegistrationWriter>(std::move(write)));
    }
};

void test_whole_proxy_region_registers_leaf_driver_family()
{
    ProxyRegistrationFixture fixture;
    const Process::DriverRegion region { fixture.proxy, 0U, 0U, true };
    (void)fixture.add_writer(region);

    require(fixture.interpreter.driver_value(fixture.writer, fixture.proxy)
                == packed("ZZZZZZZZZZZZZZZZ"),
        "a proxy-only whole region registers every leaf slot before first write");
    require(fixture.interpreter.process_program(fixture.writer).driver_regions
                == std::vector<Process::DriverRegion> { region },
        "runtime registration preserves original aggregate-region metadata");

    const auto value = packed("1011000110101100");
    Interpreter reference;
    const auto reference_proxy = reference.add_signal({
        "top.registration_array", packed("ZZZZZZZZZZZZZZZZ"),
        ResolutionKind::sv_wire
    });
    Process reference_process;
    reference_process.id = 0U;
    reference_process.name = "top.registration_array.reference_writer";
    reference_process.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    reference_process.driver_regions = {
        { reference_proxy, 0U, 0U, true }
    };
    reference_process.operations = { Halt { } };
    const auto reference_writer
        = reference.add_process(std::move(reference_process));
    require(fixture.interpreter.driver_value(fixture.writer, fixture.proxy)
                == reference.driver_value(reference_writer, reference_proxy),
        "whole proxy leaf slots begin with the packed reference driver's value");
    reference.set_process_executor(
        reference_writer,
        std::make_unique<RegistrationWriter>([&](ProcessExecutionContext& context) {
            context.write_blocking(reference_proxy, value);
        }));
    fixture.install([&](ProcessExecutionContext& context) {
        context.write_blocking(fixture.proxy, value);
    });
    const auto result = fixture.interpreter.run();
    const auto reference_result = reference.run();
    require(result.status == RunStatus::completed,
        "the whole proxy writer completes");
    require(reference_result.status == RunStatus::completed,
        "the retained packed reference writer completes");
    require(fixture.interpreter.driver_value(fixture.writer, fixture.first)
                == packed("10110001")
            && fixture.interpreter.driver_value(
                   fixture.writer, fixture.second) == packed("10101100")
            && fixture.interpreter.driver_value(
                   fixture.writer, fixture.proxy)
                == reference.driver_value(reference_writer, reference_proxy)
            && fixture.interpreter.signal_value(fixture.proxy)
                == reference.signal_value(reference_proxy),
        "proxy-only whole regions update the physical family like the packed reference");
}

void test_proxy_partial_region_initializes_leaf_and_slice_keeps_z_bits()
{
    ProxyRegistrationFixture fixture;
    const Process::DriverRegion region { fixture.proxy, 10U, 2U, false };
    (void)fixture.add_writer(region);

    require(fixture.interpreter.driver_value(fixture.writer, fixture.proxy)
                == packed("ZZZZZZZZZZZZZZZZ"),
        "a partial proxy region maps its initial driver slot to the intersecting leaf");
    require(fixture.interpreter.process_program(fixture.writer).driver_regions
                == std::vector<Process::DriverRegion> { region },
        "partial range translation does not rewrite process metadata");

    Interpreter reference;
    const auto reference_proxy = reference.add_signal({
        "top.registration_array", packed("ZZZZZZZZZZZZZZZZ"),
        ResolutionKind::sv_wire
    });
    Process reference_process;
    reference_process.id = 0U;
    reference_process.name = "top.registration_array.reference_slice_writer";
    reference_process.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    reference_process.driver_regions = {
        { reference_proxy, region.offset, region.width, region.whole }
    };
    reference_process.operations = { Halt { } };
    const auto reference_writer
        = reference.add_process(std::move(reference_process));
    require(fixture.interpreter.driver_value(fixture.writer, fixture.proxy)
                == reference.driver_value(reference_writer, reference_proxy),
        "partial proxy leaf slots begin with the packed reference driver's value");
    reference.set_process_executor(
        reference_writer,
        std::make_unique<RegistrationWriter>([&](ProcessExecutionContext& context) {
            context.write_blocking_slice(reference_proxy, packed("11"), 10U);
        }));
    fixture.install([&](ProcessExecutionContext& context) {
        context.write_blocking_slice(fixture.proxy, packed("11"), 10U);
    });
    const auto result = fixture.interpreter.run();
    const auto reference_result = reference.run();
    require(result.status == RunStatus::completed,
        "the proxy slice writer completes");
    require(reference_result.status == RunStatus::completed,
        "the retained packed reference slice writer completes");
    require(fixture.interpreter.driver_value(fixture.writer, fixture.first)
                == packed("ZZZZ11ZZ")
            && fixture.interpreter.driver_value(
                   fixture.writer, fixture.proxy)
                == reference.driver_value(reference_writer, reference_proxy)
            && fixture.interpreter.signal_value(fixture.proxy)
                == reference.signal_value(reference_proxy)
            && fixture.interpreter.signal_value(fixture.proxy)
                == packed("ZZZZ11ZZZZZZZZZZ"),
        "a sliced proxy update matches the packed reference and keeps unowned bits Z");
}

void test_read_only_proxy_region_rejects_before_process_registration()
{
    ProxyRegistrationFixture fixture { false };
    Process rejected;
    rejected.id = 0U;
    rejected.name = "top.registration_array.invalid_writer";
    rejected.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    rejected.driver_regions = { { fixture.proxy, 0U, 0U, true } };
    rejected.operations = { Halt { } };
    bool threw { };
    try {
        (void)fixture.interpreter.add_process(std::move(rejected));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw,
        "a process cannot register an output through a read-only aggregate proxy");

    Process accepted;
    accepted.id = 0U;
    accepted.name = "top.registration_array.next_writer";
    accepted.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    accepted.operations = { Halt { } };
    require(fixture.interpreter.add_process(std::move(accepted)) == 0U,
        "failed proxy validation leaves the process index available");
}

void test_out_of_range_proxy_region_rejects_before_process_registration()
{
    ProxyRegistrationFixture fixture;
    Process rejected;
    rejected.id = 0U;
    rejected.name = "top.registration_array.out_of_range_writer";
    rejected.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    rejected.driver_regions = { { fixture.proxy, 15U, 2U, false } };
    rejected.operations = { Halt { } };
    bool threw { };
    try {
        (void)fixture.interpreter.add_process(std::move(rejected));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw,
        "a proxy range outside the aggregate width is rejected");

    Process accepted;
    accepted.id = 0U;
    accepted.name = "top.registration_array.valid_writer";
    accepted.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    accepted.operations = { Halt { } };
    require(fixture.interpreter.add_process(std::move(accepted)) == 0U,
        "failed range validation leaves the process index available");
}

} // namespace

void test_aggregate_proxy_driver_region_registration()
{
    test_whole_proxy_region_registers_leaf_driver_family();
    test_proxy_partial_region_initializes_leaf_and_slice_keeps_z_bits();
    test_read_only_proxy_region_rejects_before_process_registration();
    test_out_of_range_proxy_region_rejects_before_process_registration();
}

} // namespace fsim::tests::runtime
