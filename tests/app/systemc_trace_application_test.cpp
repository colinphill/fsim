// SPDX-License-Identifier: Apache-2.0
#include "application_systemc_trace.hpp"
#include "fsim/systemc/kernel_backend_binding_inventory_accellera.hpp"
#include "fsim/systemc/kernel_backend_inventory_accellera.hpp"

#include <systemc>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fsim::app::application_detail::AccelleraSystemCTraceHook;
using fsim::app::application_detail::SystemCTraceDirtyValue;
using fsim::app::application_detail::SystemCTraceLimits;
using fsim::app::application_detail::SystemCTracePipeline;
using fsim::app::application_detail::SystemCTraceRoute;

template <typename Exception, typename Function>
void expect_throws(Function&& function)
{
    bool rejected = false;
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        rejected = true;
    }
    assert(rejected);
}

[[nodiscard]] sc_dt::sc_lv<257> wide_value(const char seed)
{
    sc_dt::sc_lv<257> value;
    for (int bit = 0; bit < 257; ++bit) {
        const std::array states { '0', '1', 'X', 'Z' };
        value[bit] = states[(static_cast<unsigned>(bit)
                                + static_cast<unsigned>(seed))
            % states.size()];
    }
    return value;
}

SC_MODULE(TraceFixture)
{
    sc_core::sc_buffer<sc_dt::sc_lv<257>> wide { "wide" };
    sc_core::sc_signal<bool> flag { "flag" };
    sc_core::sc_in<sc_dt::sc_lv<257>> wide_alias { "wide_alias" };

    SystemCTracePipeline* pipeline { };
    fsim::systemc::SystemCEndpointId wide_endpoint;
    fsim::systemc::SystemCEndpointId flag_endpoint;
    bool completed { };

    SC_CTOR(TraceFixture)
    {
        wide_alias.bind(wide);
        SC_THREAD(run);
    }

    void run()
    {
        assert(pipeline != nullptr);
        wide.write(wide_value(0));
        flag.write(true);
        wait(sc_core::SC_ZERO_TIME);

        wait(1, sc_core::SC_NS);
        assert(pipeline->set_enabled(wide_endpoint, true,
            sc_core::sc_time_stamp().value(), sc_core::sc_delta_count(),
            fsim::app::application_detail::systemc_trace_value(wide.read())));
        assert(pipeline->set_enabled(flag_endpoint, true,
            sc_core::sc_time_stamp().value(), sc_core::sc_delta_count(),
            fsim::app::application_detail::systemc_trace_value(flag.read())));

        const auto repeated = wide_value(1);
        wide.write(repeated);
        flag.write(false);
        wait(sc_core::SC_ZERO_TIME);
        wide.write(repeated);
        wait(sc_core::SC_ZERO_TIME);

        const auto status = pipeline->status();
        assert(status.pending_batches == 5U);
        const auto overflow = fsim::app::application_detail::systemc_trace_value(
            wide_value(2));
        assert(!pipeline->try_post_update(wide_endpoint,
            sc_core::sc_time_stamp().value(), sc_core::sc_delta_count(),
            overflow));
        assert(pipeline->status().backpressure_events == 1U);
        pipeline->flush();
        assert(pipeline->try_post_update(wide_endpoint,
            sc_core::sc_time_stamp().value(), sc_core::sc_delta_count(),
            overflow));

        assert(pipeline->set_enabled(wide_endpoint, false,
            sc_core::sc_time_stamp().value(), sc_core::sc_delta_count(),
            overflow));
        wide.write(wide_value(3));
        wait(sc_core::SC_ZERO_TIME);
        completed = true;
        sc_core::sc_stop();
    }
};

[[nodiscard]] fsim::systemc::SystemCEndpointId endpoint_for(
    const fsim::systemc::SystemCKernelChannelInventorySnapshot& inventory,
    const std::string_view path)
{
    for (const auto& channel : inventory.channels) {
        if (channel.descriptor.canonical_path == path) {
            return channel.channel;
        }
    }
    throw std::runtime_error("missing channel endpoint");
}

void test_official_post_update_pipeline()
{
    using namespace fsim;
    using namespace app::application_detail;
    using namespace systemc;

    sc_core::sc_set_time_resolution(1, sc_core::SC_NS);
    TraceFixture fixture { "fixture" };

    diagnostic::Engine diagnostics;
    const auto island = make_systemc_island_id(
        "trace-island", { }, diagnostics)
                            .value();
    const auto hierarchy = make_systemc_hierarchy_id(
        island, fixture.name(), { }, diagnostics)
                               .value();
    SystemCKernelChannelInventory inventory { island, hierarchy };
    assert(inventory.register_channel(
        describe_accellera_channel(fixture.wide), diagnostics));
    assert(inventory.register_channel(
        describe_accellera_channel(fixture.flag), diagnostics));
    assert(inventory.freeze(diagnostics));
    const auto wide_endpoint = endpoint_for(
        inventory.snapshot(), fixture.wide.name());
    const auto flag_endpoint = endpoint_for(
        inventory.snapshot(), fixture.flag.name());

    SystemCKernelBindingInventory bindings { inventory.snapshot() };
    assert(bindings.register_binding(describe_accellera_binding(
                                         fixture.wide_alias,
                                         { make_accellera_binding_target(fixture.wide) }),
        diagnostics));
    assert(bindings.freeze(diagnostics));
    assert(!diagnostics.has_error());

    runtime::TraceDeclarationBuilder builder;
    runtime::TraceSourceMetadata source;
    source.kind = runtime::TraceSourceKind::SystemC;
    source.language = runtime::TraceLanguage::SystemC;
    source.root_identity = fixture.name();
    source.library = "systemc";
    source.owner_identity = "systemc:trace-island";
    const auto wide_signal = builder.add_typed_variable(fixture.wide.name(),
        runtime::TraceTypeKind::Packed, 257U,
        runtime::SystemVerilogScalarKind::None, { }, source);
    const auto wide_alias = builder.add_alias(
        fixture.wide_alias.name(), wide_signal, source);
    const auto flag_signal = builder.add_typed_variable(fixture.flag.name(),
        runtime::TraceTypeKind::Packed, 1U,
        runtime::SystemVerilogScalarKind::None, { }, source);
    const auto declarations = std::move(builder).freeze();

    const std::array routes {
        SystemCTraceRoute { wide_endpoint, wide_signal, { wide_alias }, false },
        SystemCTraceRoute { flag_endpoint, flag_signal, { }, false },
    };
    SystemCTraceLimits limits;
    limits.maximum_routes = 2U;
    limits.maximum_aliases_per_route = 1U;
    limits.maximum_pending_batches = 5U;
    limits.maximum_values_per_batch = 2U;
    limits.maximum_bits_per_batch = 512U;
    limits.maximum_records = 16U;
    std::ostringstream vcd;
    std::ostringstream fst(std::ios::binary);
    SystemCTracePipeline pipeline(declarations, inventory.snapshot(),
        bindings.snapshot(), routes, vcd, fst, limits);
    fixture.pipeline = &pipeline;
    fixture.wide_endpoint = wide_endpoint;
    fixture.flag_endpoint = flag_endpoint;

    AccelleraSystemCTraceHook<sc_dt::sc_lv<257>> wide_hook {
        "wide_trace_hook", fixture.wide, pipeline, wide_endpoint
    };
    AccelleraSystemCTraceHook<bool> flag_hook {
        "flag_trace_hook", fixture.flag, pipeline, flag_endpoint
    };

    sc_core::sc_start();
    assert(fixture.completed);
    assert(wide_hook.capture_attempts() == 2U);
    assert(flag_hook.capture_attempts() == 1U);
    assert(wide_hook.callback_failures() == 0U);
    assert(flag_hook.callback_failures() == 0U);
    assert(wide_hook.backpressure_events() == 0U);

    expect_throws<std::invalid_argument>([&] {
        static_cast<void>(pipeline.try_post_update(flag_endpoint, 0U, 0U,
            systemc_trace_value(false)));
    });
    expect_throws<std::invalid_argument>([&] {
        static_cast<void>(pipeline.try_post_update(flag_endpoint,
            sc_core::sc_time_stamp().value(), sc_core::sc_delta_count(),
            runtime::PackedLogic4::from_msb_string("10")));
    });
    const std::array duplicated {
        SystemCTraceDirtyValue { flag_endpoint, systemc_trace_value(false) },
        SystemCTraceDirtyValue { flag_endpoint, systemc_trace_value(false) },
    };
    expect_throws<std::invalid_argument>([&] {
        static_cast<void>(pipeline.try_post_update_batch(
            sc_core::sc_time_stamp().value(), sc_core::sc_delta_count(),
            duplicated));
    });
    expect_throws<std::invalid_argument>([&] {
        static_cast<void>(pipeline.try_post_update(
            systemc::SystemCEndpointId { 1U, 2U },
            sc_core::sc_time_stamp().value(), sc_core::sc_delta_count(),
            systemc_trace_value(false)));
    });
    assert(pipeline.status().pending_batches == 1U);
    pipeline.close(sc_core::sc_time_stamp().value());
    const auto observations = pipeline.observations();
    assert(observations.size() == 6U);
    assert(observations[0].region == runtime::TraceRegion::Snapshot);
    assert(observations[1].region == runtime::TraceRegion::Snapshot);
    std::size_t wide_records = 0U;
    std::size_t flag_records = 0U;
    std::vector<runtime::PackedLogic4> wide_values;
    for (const auto& observation : observations) {
        assert(observation.values.size() == 1U);
        if (observation.values.front().value.width() == 257U) {
            ++wide_records;
            wide_values.push_back(observation.values.front().value);
        } else {
            assert(observation.values.front().value.width() == 1U);
            ++flag_records;
        }
    }
    assert(wide_records == 4U);
    assert(flag_records == 2U);
    assert(wide_values[1] == wide_values[2]);
    assert(pipeline.status().state == SystemCTraceState::closed);
    assert(pipeline.status().accepted_batches == 6U);
    assert(pipeline.status().delivered_batches == 6U);
    assert(vcd.str().find("wide_alias") != std::string::npos);
    assert(vcd.str().find("$dumpall") != std::string::npos);
    assert(!fst.str().empty());

    expect_throws<std::logic_error>([&] {
        static_cast<void>(pipeline.try_post_update(wide_endpoint, 4U, 0U,
            systemc_trace_value(wide_value(0))));
    });
    assert(std::string_view { systemc_trace_diagnostic_code(
               SystemCTraceCode::backpressure) }
        == "FSIM-SC-Z003");
}

void test_metadata_and_ordering_negatives()
{
    using namespace fsim;
    systemc::SystemCKernelValue invalid;
    invalid.kind = systemc::SystemCKernelValueKind::enumeration;
    invalid.width = 1U;
    invalid.range = { 0, 0,
        systemc::SystemCKernelRangeDirection::descending };
    invalid.enum_literals = { "zero", "one" };
    invalid.planes = { { 0U } };
    expect_throws<std::invalid_argument>([&] {
        static_cast<void>(
            app::application_detail::systemc_trace_value(invalid));
    });
    assert(std::string_view {
               app::application_detail::systemc_trace_diagnostic_code(
                   app::application_detail::SystemCTraceCode::metadata) }
        == "FSIM-SC-Z001");
    assert(std::string_view {
               app::application_detail::systemc_trace_diagnostic_code(
                   app::application_detail::SystemCTraceCode::lifecycle) }
        == "FSIM-SC-Z002");
    assert(std::string_view {
               app::application_detail::systemc_trace_diagnostic_code(
                   app::application_detail::SystemCTraceCode::writer) }
        == "FSIM-SC-Z004");
}

} // namespace

int sc_main(int, char**)
{
    test_official_post_update_pipeline();
    test_metadata_and_ordering_negatives();
    return 0;
}
