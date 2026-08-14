// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mixed_systemc.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

fsim::frontend::SourceSpan source_span()
{
    fsim::frontend::SourceSpan result;
    result.source_name = "mixed_systemc.sdf";
    result.begin = { 7U, 4U, 2U };
    result.end = { 15U, 4U, 10U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    elaboration::SignalInfo info;
    info.id = 0U;
    info.name = "top.u_sc.data";
    info.width = 257U;
    info.type_name = "sc_lv<257>";
    info.source_domain = frontend::ValueDomain::Logic9;
    info.is_port = true;
    info.direction = frontend::PortDirection::Input;
    info.resolution = runtime::simir::ResolutionKind::std_logic;
    state.signal_info.push_back(std::move(info));
    state.signals.emplace_back("top.u_sc.data", runtime::PackedLogic4(257U),
        runtime::simir::ResolutionKind::std_logic,
        runtime::simir::ValueKind::logic9);
    state.signal_names.emplace_back("top.u_sc.data", 0U);

    elaboration::SystemCNamedObjectInfo module;
    module.kind = elaboration::SystemCNamedObjectKind::module;
    module.native_handle = 10U;
    module.name = "top.u_sc";
    module.parent = "top";
    module.type_name = "native_proxy";
    state.systemc_objects.push_back(std::move(module));
    elaboration::SystemCNamedObjectInfo port;
    port.kind = elaboration::SystemCNamedObjectKind::port;
    port.native_handle = 11U;
    port.name = "top.u_sc.data";
    port.parent = "top.u_sc";
    port.type_name = "sc_in<sc_lv<257>>";
    port.signal = 0U;
    state.systemc_objects.push_back(std::move(port));
    elaboration::SystemCNamedObjectInfo custom;
    custom.kind = elaboration::SystemCNamedObjectKind::primitive_channel;
    custom.native_handle = 12U;
    custom.name = "top.u_sc.custom";
    custom.parent = "top.u_sc";
    custom.type_name = "vendor_custom_channel";
    state.systemc_objects.push_back(std::move(custom));
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "SystemC proxy fixture must be valid");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfInterconnectTimingApplication> make_timing()
{
    using namespace fsim;
    app::SdfResolvedEndpoint endpoint;
    endpoint.role = app::SdfEndpointRole::InterconnectDestination;
    endpoint.object_kind = app::SdfEndpointObjectKind::SystemCPort;
    endpoint.instance_path = "top.u_sc";
    endpoint.object_path = "top.u_sc.data";
    endpoint.signal = 0U;
    endpoint.object_width = 257U;
    endpoint.language = app::SdfScopeRootLanguage::SystemC;
    endpoint.direction = frontend::PortDirection::Input;
    app::SdfAppliedInterconnectTiming applied;
    applied.target_kind = app::SdfTimingTargetKind::Interconnect;
    applied.mode = app::SdfDelayApplicationMode::Absolute;
    applied.target_instance_path = "top.u_sc";
    applied.target_identity = "hdl-to-systemc-data";
    applied.endpoints.push_back(std::move(endpoint));
    applied.transition_delays = { 3U, 5U };
    applied.annotation_source = source_span();
    applied.annotation_identity = "mixed-systemc-annotation";
    applied.canonical_identity = "mixed-systemc-interconnect";
    return std::make_shared<const app::SdfInterconnectTimingApplication>(
        nullptr, std::vector<app::SdfAppliedInterconnectTiming> { std::move(applied) },
        "mixed-systemc-timing");
}

fsim::app::SdfMixedSystemCBinding binding(std::string path)
{
    using namespace fsim;
    app::SdfMixedSystemCBinding result;
    result.target_identity = "hdl-to-systemc-data";
    result.object_path = std::move(path);
    result.samples = {
        { 10U, 2U, runtime::PackedLogic9(257U, runtime::Logic9::z) },
        { 10U, 3U, runtime::PackedLogic9(257U, runtime::Logic9::one) },
        { 12U, 0U, runtime::PackedLogic9(257U, runtime::Logic9::x) },
    };
    result.source = source_span();
    return result;
}

void test_arbitrary_width_logic9_and_time_delta_identity()
{
    using namespace fsim;
    const auto design = make_design();
    const auto timing = make_timing();
    const std::vector requested { binding("top.u_sc.data") };
    const auto result
        = app::apply_sdf_mixed_systemc(timing, design, requested);
    require(result.ok() && result.application->proxies().size() == 1U,
        "typed SystemC proxy timing must publish");
    const auto& proxy = result.application->proxies().front();
    require(proxy.native_handle == 11U && proxy.signal == 0U
            && proxy.source_language == app::SdfScopeRootLanguage::Vhdl
            && proxy.destination_language == app::SdfScopeRootLanguage::SystemC
            && proxy.width == 257U
            && proxy.domain == frontend::ValueDomain::Logic9
            && proxy.resolution == runtime::simir::ResolutionKind::std_logic
            && proxy.transition_delays
                == std::vector<runtime::SimulationTick> { 3U, 5U }
            && proxy.samples[0].tick == 10U && proxy.samples[0].delta == 2U
            && proxy.samples[0].value.width() == 257U
            && proxy.samples[0].value.get(256U) == runtime::Logic9::z
            && proxy.samples[1].delta == 3U
            && proxy.samples[2].tick == 12U,
        "Logic9 width, delay, time, delta, and native identity must remain exact");
    const auto repeated
        = app::apply_sdf_mixed_systemc(timing, design, requested);
    require(repeated.ok()
            && repeated.application->semantic_identity()
                == result.application->semantic_identity(),
        "SystemC proxy publication identity must be deterministic");
}

void test_custom_channel_sequence_and_resource_rejection()
{
    using namespace fsim;
    const auto design = make_design();
    const auto timing = make_timing();
    std::vector requested { binding("top.u_sc.custom") };
    auto result = app::apply_sdf_mixed_systemc(timing, design, requested);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-SYSTEMC-003",
        "metadata-only custom channels must be rejected explicitly");

    requested = { binding("top.u_sc.data") };
    requested.front().samples[1].delta = 2U;
    result = app::apply_sdf_mixed_systemc(timing, design, requested);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-SYSTEMC-003",
        "same-time samples must retain strictly increasing delta identity");

    requested = { binding("top.u_sc.data") };
    requested.front().samples[0].value
        = runtime::PackedLogic9(256U, runtime::Logic9::z);
    result = app::apply_sdf_mixed_systemc(timing, design, requested);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-SYSTEMC-003",
        "arbitrary-width samples must not be truncated or widened");

    requested = { binding("top.u_sc.data") };
    app::SdfMixedSystemCLimits limits;
    limits.max_samples_per_proxy = 2U;
    result = app::apply_sdf_mixed_systemc(timing, design, requested, limits);
    require(!result.ok() && result.application == nullptr
            && result.diagnostics.front().code
                == "FSIM-SDF-MIXED-SYSTEMC-004",
        "sample resource overflow must roll back atomically");
}
} // namespace

int main()
{
    try {
        test_arbitrary_width_logic9_and_time_delta_identity();
        test_custom_channel_sequence_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << "sdf mixed SystemC test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "sdf mixed SystemC tests passed\n";
    return 0;
}
