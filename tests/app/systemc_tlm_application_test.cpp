// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/systemc/accellera.hpp"
#include "fsim/systemc/kernel_backend_observation.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

fsim::project::Config make_config(const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "systemc-native-tlm";
    config.project.top = "systemc:models.tlm_root";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0 ? "cache-o0"
                                                           : "cache-o2");
    config.run.max_deltas = 1000U;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::systemc;
    sources.standard = "2023-subset";
    sources.library = "models";
    sources.files.push_back(source);
    sources.include_directories.emplace_back(
        std::filesystem::path { FSIM_TEST_SOURCE_DIR } / "include");
    config.source_sets.push_back(std::move(sources));
    return config;
}

void verify(const fsim::project::Config& config)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(project);
    assert(project->systemc_plugins.size() == 1U);
    assert(project->design.systemc_instances().size() == 1U);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter
    };
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    assert(result.time == 5U);
}

void verify_observation()
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const systemc::SystemCKernelProtocolLimits protocol_limits;
    const auto island = systemc::make_systemc_island_id(
        "application-tlm-observation", protocol_limits, diagnostics);
    const auto hierarchy = systemc::make_systemc_hierarchy_id(
        *island, "models.tlm_root", protocol_limits, diagnostics);
    const auto object = systemc::make_systemc_object_id(
        *hierarchy, "models.tlm_root.transport", protocol_limits,
        diagnostics);
    const auto initiator = systemc::make_systemc_endpoint_id(
        *object, "initiator", protocol_limits, diagnostics);
    const auto target = systemc::make_systemc_endpoint_id(
        *object, "target", protocol_limits, diagnostics);
    assert(island && hierarchy && object && initiator && target);

    systemc::SystemCKernelChannelInventory inventory { *island, *hierarchy };
    assert(inventory.freeze(diagnostics));
    systemc::SystemCKernelSafePointObserver observer { inventory.snapshot() };
    const auto query_sequence
        = systemc::make_systemc_sequence_id(*island, 1U, diagnostics);
    const auto report_sequence
        = systemc::make_systemc_sequence_id(*island, 2U, diagnostics);
    assert(observer.query_inventory(
        { 0U, 0U, systemc::SystemCAccelleraRegion::quiescent, *island,
            *query_sequence },
        diagnostics));
    assert(observer.report(
        { 0U, 0U, systemc::SystemCAccelleraRegion::quiescent, *island,
            *report_sequence },
        diagnostics));

    const auto tlm1_sequence
        = systemc::make_systemc_sequence_id(*island, 10U, diagnostics);
    const auto tlm1_transaction = systemc::make_systemc_transaction_id(
        *initiator, *tlm1_sequence, diagnostics);
    systemc::SystemCKernelTlm1Transaction analysis;
    analysis.transaction = *tlm1_transaction;
    analysis.endpoint = *initiator;
    analysis.peer = *target;
    analysis.sequence = *tlm1_sequence;
    analysis.operation = systemc::SystemCKernelTlm1Operation::analysis;
    analysis.state = systemc::SystemCKernelTlm1State::completed;
    analysis.time_fs = 5000U;
    const auto analysis_value = systemc::make_systemc_kernel_scalar_value(
        { 107U, 32U, false }, { }, diagnostics);
    assert(analysis_value);
    analysis.request = *analysis_value;
    assert(observer.observe_tlm1(analysis,
        systemc::SystemCKernelObservationKind::tlm1_end, diagnostics));

    systemc::SystemCKernelTlm2Payload payload;
    payload.address = 4U;
    payload.command = systemc::SystemCKernelTlm2Command::read;
    payload.response = systemc::SystemCKernelTlm2Response::ok;
    payload.dmi_allowed = true;
    payload.streaming_width = 4U;
    payload.data.resize(4U);
    const auto dmi_sequence
        = systemc::make_systemc_sequence_id(*island, 11U, diagnostics);
    const auto dmi_transaction = systemc::make_systemc_transaction_id(
        *initiator, *dmi_sequence, diagnostics);
    systemc::SystemCKernelTlm2Transaction dmi;
    dmi.transaction = *dmi_transaction;
    dmi.endpoint = *initiator;
    dmi.peer = *target;
    dmi.sequence = *dmi_sequence;
    dmi.operation = systemc::SystemCKernelTlm2Operation::direct_memory;
    dmi.state = systemc::SystemCKernelTlm2State::completed;
    dmi.sync = systemc::SystemCKernelTlm2Sync::completed;
    dmi.time_fs = 5000U;
    dmi.payload = payload;
    dmi.dmi = { 0U, 15U, systemc::SystemCKernelTlm2DmiAccess::read_write,
        0U, 0U };
    assert(observer.observe_tlm2(dmi,
        systemc::SystemCKernelObservationKind::tlm2_dmi, diagnostics));

    const auto debug_sequence
        = systemc::make_systemc_sequence_id(*island, 12U, diagnostics);
    const auto debug_transaction = systemc::make_systemc_transaction_id(
        *initiator, *debug_sequence, diagnostics);
    auto debug = dmi;
    debug.transaction = *debug_transaction;
    debug.sequence = *debug_sequence;
    debug.operation = systemc::SystemCKernelTlm2Operation::debug_transport;
    debug.transferred = 4U;
    debug.dmi.reset();
    assert(observer.observe_tlm2(debug,
        systemc::SystemCKernelObservationKind::tlm2_debug, diagnostics));
    assert(!diagnostics.has_error());
    assert(observer.batch().records.size() == 5U);
    assert(observer.batch().records[2].kind
        == systemc::SystemCKernelObservationKind::tlm1_end);
    assert(observer.batch().records[3].kind
        == systemc::SystemCKernelObservationKind::tlm2_dmi);
    assert(observer.batch().records[4].kind
        == systemc::SystemCKernelObservationKind::tlm2_debug);
    assert(!observer.batch().records[4].value);
}

} // namespace

int main()
{
    const auto nonce
        = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory { std::filesystem::temp_directory_path()
        / ("fsim-systemc-tlm-" + std::to_string(nonce)) };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "tlm_root.cpp";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"cpp(#include "fsim/systemc/accellera.hpp"
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/tlm_quantumkeeper.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

struct RouteExtension final : tlm::tlm_extension<RouteExtension> {
  std::uint32_t route{};
  tlm::tlm_extension_base* clone() const override {
    auto* result = new RouteExtension;
    result->route = route;
    return result;
  }
  void copy_from(const tlm::tlm_extension_base& other) override {
    route = static_cast<const RouteExtension&>(other).route;
  }
};

class Root final : public sc_core::sc_module {
public:
  tlm::tlm_fifo<std::uint32_t> fifo{"fifo", 1};
  tlm_utils::simple_initiator_socket<Root, 32> initiator{"initiator"};
  tlm_utils::simple_target_socket<Root, 32> target{"target"};
  std::array<unsigned char, 16> memory{};

  explicit Root(sc_core::sc_module_name name) : sc_core::sc_module{name} {
    initiator.bind(target);
    initiator.register_nb_transport_bw(this, &Root::nb_transport_bw);
    initiator.register_invalidate_direct_mem_ptr(this, &Root::invalidate_dmi);
    target.register_b_transport(this, &Root::b_transport);
    target.register_nb_transport_fw(this, &Root::nb_transport_fw);
    target.register_get_direct_mem_ptr(this, &Root::get_dmi);
    target.register_transport_dbg(this, &Root::transport_dbg);
    SC_THREAD(produce);
    SC_THREAD(consume);
    SC_THREAD(transport);
  }

private:
  bool invalidated{};

  void configure(tlm::tlm_generic_payload& payload, unsigned char* data,
      tlm::tlm_command command, RouteExtension& extension) {
    payload.set_address(4U);
    payload.set_command(command);
    payload.set_data_ptr(data);
    payload.set_data_length(4U);
    payload.set_streaming_width(4U);
    payload.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    payload.set_extension(&extension);
  }
  void b_transport(tlm::tlm_generic_payload& payload, sc_core::sc_time& delay) {
    auto* extension = payload.get_extension<RouteExtension>();
    assert(extension != nullptr && extension->route == 23U);
    if (payload.is_write()) {
      std::memcpy(memory.data() + payload.get_address(), payload.get_data_ptr(), 4U);
    } else {
      std::memcpy(payload.get_data_ptr(), memory.data() + payload.get_address(), 4U);
    }
    delay += sc_core::sc_time{2.0, sc_core::SC_NS};
    payload.set_dmi_allowed(true);
    payload.set_response_status(tlm::TLM_OK_RESPONSE);
  }
  tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload& payload,
      tlm::tlm_phase& phase, sc_core::sc_time& delay) {
    assert(phase == tlm::BEGIN_REQ);
    phase = tlm::END_REQ;
    delay += sc_core::sc_time{1.0, sc_core::SC_NS};
    tlm::tlm_phase backward = tlm::BEGIN_RESP;
    assert(target->nb_transport_bw(payload, backward, delay) == tlm::TLM_COMPLETED);
    payload.set_response_status(tlm::TLM_OK_RESPONSE);
    return tlm::TLM_UPDATED;
  }
  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload&,
      tlm::tlm_phase& phase, sc_core::sc_time&) {
    assert(phase == tlm::BEGIN_RESP);
    return tlm::TLM_COMPLETED;
  }
  bool get_dmi(tlm::tlm_generic_payload&, tlm::tlm_dmi& dmi) {
    dmi.set_dmi_ptr(memory.data());
    dmi.set_start_address(0U);
    dmi.set_end_address(memory.size() - 1U);
    dmi.allow_read_write();
    target->invalidate_direct_mem_ptr(4U, 7U);
    return true;
  }
  void invalidate_dmi(sc_dt::uint64 start, sc_dt::uint64 end) {
    invalidated = start == 4U && end == 7U;
  }
  unsigned int transport_dbg(tlm::tlm_generic_payload& payload) {
    std::memcpy(payload.get_data_ptr(), memory.data() + payload.get_address(), 4U);
    return 4U;
  }
  void produce() {
    assert(fifo.nb_put(2U));
    fifo.put(3U);
  }
  void consume() {
    wait(5.0, sc_core::SC_NS);
    std::uint32_t value{};
    assert(fifo.nb_peek(value) && value == 2U);
    assert(fifo.nb_get(value) && value == 2U);
    wait(sc_core::SC_ZERO_TIME);
    assert(fifo.get() == 3U);
  }
  void transport() {
    tlm_utils::tlm_quantumkeeper::set_global_quantum(
        sc_core::sc_time{10.0, sc_core::SC_NS});
    tlm_utils::tlm_quantumkeeper quantum;
    quantum.reset();
    RouteExtension extension;
    extension.route = 23U;
    std::array<unsigned char, 4> written{5U, 6U, 7U, 8U};
    tlm::tlm_generic_payload write;
    configure(write, written.data(), tlm::TLM_WRITE_COMMAND, extension);
    auto delay = sc_core::SC_ZERO_TIME;
    initiator->b_transport(write, delay);
    assert(write.is_response_ok() && write.is_dmi_allowed());
    quantum.inc(delay);
    std::array<unsigned char, 4> readback{};
    tlm::tlm_generic_payload read;
    configure(read, readback.data(), tlm::TLM_READ_COMMAND, extension);
    delay = sc_core::SC_ZERO_TIME;
    initiator->b_transport(read, delay);
    quantum.inc(delay);
    tlm::tlm_phase phase = tlm::BEGIN_REQ;
    delay = sc_core::SC_ZERO_TIME;
    assert(initiator->nb_transport_fw(read, phase, delay) == tlm::TLM_UPDATED);
    assert(phase == tlm::END_REQ);
    quantum.inc(delay);
    tlm::tlm_dmi dmi;
    assert(initiator->get_direct_mem_ptr(read, dmi));
    assert(dmi.is_read_allowed() && dmi.is_write_allowed() && invalidated);
    std::array<unsigned char, 4> debug{};
    tlm::tlm_generic_payload debug_payload;
    configure(debug_payload, debug.data(), tlm::TLM_READ_COMMAND, extension);
    assert(initiator->transport_dbg(debug_payload) == 4U);
    assert(readback == written && debug == written);
    write.clear_extension<RouteExtension>();
    read.clear_extension<RouteExtension>();
    debug_payload.clear_extension<RouteExtension>();
    quantum.sync();
    sc_core::sc_stop();
  }
};

SC_FSIM_EXPORT_AS(Root, "tlm_root");
)cpp";
        assert(output.good());
    }

    verify(make_config(
        directory.path, source, fsim::project::Optimization::o0));
    verify_observation();
    return 0;
}
