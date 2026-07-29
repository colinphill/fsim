// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <cassert>
#include <fstream>
#include <string_view>

namespace fsim::test {

void ApplicationTestFixture::create_systemc_sources() {
systemc_source = directory / "model.cpp";
{
  std::ofstream output(systemc_source);
  output << R"(
#include <systemc>

SC_MODULE(MethodBridge) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  bool passthrough{};

  SC_CTOR(MethodBridge) {
    passthrough =
        fsim::systemc::construction_value<bool>("PASSTHROUGH");
    SC_METHOD(evaluate);
    sensitive << value;
  }

  void evaluate() {
    const auto input = value.read();
    if (passthrough) {
      inverted.write(input);
      return;
    }
    if (input == sc_dt::sc_logic{'0'}) {
      inverted.write(sc_dt::sc_logic{'1'});
    } else if (input == sc_dt::sc_logic{'1'}) {
      inverted.write(sc_dt::sc_logic{'0'});
    } else {
      inverted.write(sc_dt::sc_logic{'Z'});
    }
  }
};

SC_MODULE(NoInitBridge) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};

  SC_CTOR(NoInitBridge) {
    SC_METHOD(evaluate);
    sensitive << value;
    dont_initialize();
  }

  void evaluate() {
    inverted.write(sc_dt::sc_logic{'0'});
  }
};

SC_MODULE(EdgeCounter) {
  sc_core::sc_in<bool> clock{"clock"};
  sc_core::sc_out<sc_dt::sc_uint<8>> count{"count"};
  sc_dt::sc_uint<8> state{};

  SC_CTOR(EdgeCounter) {
    SC_METHOD(tick);
    sensitive << clock.pos();
    dont_initialize();
  }

  void tick() {
    state = state.to_uint64() + 1;
    count.write(state);
  }
};

SC_MODULE(ThrowingMethod) {
  SC_CTOR(ThrowingMethod) {
    SC_METHOD(fail);
  }

  void fail() {
    throw std::runtime_error{"intentional SystemC method failure"};
  }
};

SC_MODULE(DynamicEvents) {
  sc_core::sc_out<sc_dt::sc_uint<8>> count{"count"};
  sc_core::sc_event pulse{"pulse"};
  sc_dt::sc_uint<8> observed{};
  unsigned producer_state{};

  SC_CTOR(DynamicEvents) {
    SC_METHOD(produce);
    SC_METHOD(consume);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      pulse.notify(sc_core::sc_time{5, sc_core::SC_NS});
      pulse.notify(sc_core::sc_time{7, sc_core::SC_NS});
      pulse.notify(sc_core::SC_ZERO_TIME);
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 1) {
      ++producer_state;
      pulse.notify();
      next_trigger(sc_core::sc_time{2, sc_core::SC_NS});
    } else if (producer_state == 2) {
      ++producer_state;
      pulse.notify(sc_core::sc_time{1, sc_core::SC_NS});
      next_trigger(sc_core::sc_time{2, sc_core::SC_NS});
    } else if (producer_state == 3) {
      ++producer_state;
      pulse.notify(sc_core::sc_time{2, sc_core::SC_NS});
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else {
      pulse.cancel();
      pulse.notify(sc_core::sc_time{2, sc_core::SC_NS});
      pulse.notify();
    }
  }

  void consume() {
    if (observed.to_uint64() != 0) {
      count.write(observed);
    }
    observed = observed.to_uint64() + 1;
    next_trigger(pulse);
  }
};

SC_MODULE(EventLists) {
  sc_core::sc_out<sc_dt::sc_uint<8>> or_seen{"or_seen"};
  sc_core::sc_out<sc_dt::sc_uint<8>> and_seen{"and_seen"};
  sc_core::sc_event first{"first"};
  sc_core::sc_event second{"second"};
  sc_core::sc_event third{"third"};
  unsigned producer_state{};
  bool or_initialized{};
  bool and_initialized{};

  SC_CTOR(EventLists) {
    SC_METHOD(produce);
    SC_METHOD(observe_or);
    SC_METHOD(observe_and);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 1) {
      ++producer_state;
      first.notify();
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 2) {
      ++producer_state;
      second.notify();
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else {
      third.notify();
    }
  }

  void observe_or() {
    if (!or_initialized) {
      or_initialized = true;
      next_trigger(first | second);
      return;
    }
    or_seen.write(sc_dt::sc_uint<8>{producer_state - 1});
  }

  void observe_and() {
    if (!and_initialized) {
      and_initialized = true;
      next_trigger(first & second & third);
      return;
    }
    and_seen.write(sc_dt::sc_uint<8>{producer_state});
  }
};

class DeferredChannel final : public sc_core::sc_prim_channel {
 public:
  DeferredChannel(
      const char* name,
      sc_core::sc_out<sc_dt::sc_uint<8>>& value,
      sc_core::sc_out<sc_dt::sc_uint<8>>& updates)
      : sc_core::sc_prim_channel(name),
        value_(value),
        updates_(updates) {}

  void write(const unsigned value) {
    pending_ = value;
    request_update();
  }

 protected:
  void update() override {
    ++update_count_;
    value_.write(sc_dt::sc_uint<8>{pending_});
    updates_.write(sc_dt::sc_uint<8>{update_count_});
    request_update();
  }

 private:
  sc_core::sc_out<sc_dt::sc_uint<8>>& value_;
  sc_core::sc_out<sc_dt::sc_uint<8>>& updates_;
  unsigned pending_{};
  unsigned update_count_{};
};

SC_MODULE(KernelChannels) {
  sc_core::sc_out<sc_dt::sc_uint<8>> value{"value"};
  sc_core::sc_out<sc_dt::sc_uint<8>> updates{"updates"};
  sc_core::sc_out<sc_dt::sc_uint<8>> event_count{"event_count"};
  sc_core::sc_event pulse{"pulse"};
  DeferredChannel deferred;
  unsigned producer_state{};
  unsigned observed_events{};

  SC_CTOR(KernelChannels) : deferred{"deferred", value, updates} {
    SC_METHOD(produce);
    SC_METHOD(consume);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      deferred.write(1);
      deferred.write(2);
      pulse.notify_delayed(
          sc_core::sc_time{2, sc_core::SC_NS});
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 1) {
      ++producer_state;
      deferred.write(3);
      next_trigger(sc_core::sc_time{2, sc_core::SC_NS});
    } else {
      deferred.write(4);
    }
  }

  void consume() {
    if (observed_events != 0) {
      event_count.write(
          sc_dt::sc_uint<8>{observed_events});
    }
    ++observed_events;
    next_trigger(pulse);
  }
};

SC_MODULE(InternalSignals) {
  sc_core::sc_out<sc_dt::sc_uint<8>> observed{"observed"};
  sc_core::sc_out<sc_dt::sc_uint<8>> event_count{"event_count"};
  sc_core::sc_out<sc_dt::sc_uint<8>> dynamic_count{
      "dynamic_count"};
  sc_core::sc_signal<sc_dt::sc_uint<8>> internal{
      "internal", sc_dt::sc_uint<8>{5}};
  unsigned producer_state{};
  unsigned static_events{};
  unsigned dynamic_events{};
  bool dynamic_initialized{};

  SC_CTOR(InternalSignals) {
    SC_METHOD(produce);
    SC_METHOD(observe_static);
    sensitive << internal;
    dont_initialize();
    SC_METHOD(observe_dynamic);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      if (internal.read().to_uint64() != 5) {
        throw std::runtime_error{
            "SystemC internal signal lost its initial value"};
      }
      internal.write(sc_dt::sc_uint<8>{1});
      internal.write(sc_dt::sc_uint<8>{2});
      if (internal.read().to_uint64() != 5) {
        throw std::runtime_error{
            "SystemC signal write became visible before update"};
      }
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else {
      internal.write(sc_dt::sc_uint<8>{3});
    }
  }

  void observe_static() {
    observed.write(internal.read());
    if (internal.event()) {
      ++static_events;
    }
    event_count.write(sc_dt::sc_uint<8>{static_events});
  }

  void observe_dynamic() {
    if (dynamic_initialized) {
      ++dynamic_events;
      dynamic_count.write(
          sc_dt::sc_uint<8>{dynamic_events});
    }
    dynamic_initialized = true;
    next_trigger(internal.value_changed_event());
  }
};

SC_MODULE(BoundPorts) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  sc_core::sc_signal<sc_dt::sc_logic> value_channel{
      "value_channel", sc_dt::sc_logic{'0'}};
  sc_core::sc_signal<sc_dt::sc_logic> inverted_channel{
      "inverted_channel", sc_dt::sc_logic{'1'}};

  SC_CTOR(BoundPorts) {
    value(value_channel);
    inverted(inverted_channel);
    SC_METHOD(evaluate);
    sensitive << value_channel;
    dont_initialize();
  }

  void evaluate() {
    const auto input = value.read().to_char();
    inverted.write(
        sc_dt::sc_logic{
            input == '0' ? '1'
            : input == '1' ? '0'
                           : 'X'});
  }
};

)";
  output << R"(
SC_MODULE(NativeLeaf) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};

  SC_CTOR(NativeLeaf) {
    SC_METHOD(evaluate);
    sensitive << value;
    dont_initialize();
  }

  void evaluate() {
    const auto input = value.read().to_char();
    inverted.write(
        sc_dt::sc_logic{
            input == '0' ? '1'
            : input == '1' ? '0'
                           : 'X'});
  }
};

SC_MODULE(NativeHierarchy) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  sc_core::sc_signal<sc_dt::sc_logic> value_channel{
      "value_channel", sc_dt::sc_logic{'0'}};
  sc_core::sc_signal<sc_dt::sc_logic> inverted_channel{
      "inverted_channel", sc_dt::sc_logic{'1'}};
  NativeLeaf leaf;

  SC_CTOR(NativeHierarchy)
      : leaf{"leaf"} {
    value(value_channel);
    inverted(inverted_channel);
    leaf.value(value_channel);
    leaf.inverted(inverted_channel);
  }
};

SC_MODULE(DuplicateNativeHierarchy) {
  NativeLeaf first;
  NativeLeaf second;

  SC_CTOR(DuplicateNativeHierarchy)
      : first{"duplicate"},
        second{"duplicate"} {}
};

SC_MODULE(PortChainHierarchy) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  NativeLeaf leaf;

  SC_CTOR(PortChainHierarchy)
      : leaf{"leaf"} {
    leaf.value(value);
    leaf.inverted(inverted);
  }
};

SC_MODULE(ExportHierarchy) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  sc_core::sc_signal<sc_dt::sc_logic> value_channel{
      "value_channel", sc_dt::sc_logic{'0'}};
  sc_core::sc_signal<sc_dt::sc_logic> inverted_channel{
      "inverted_channel", sc_dt::sc_logic{'1'}};
  sc_core::sc_export<
      sc_core::sc_signal_in_if<sc_dt::sc_logic>> value_endpoint{
          "value_endpoint"};
  sc_core::sc_export<
      sc_core::sc_signal_in_if<sc_dt::sc_logic>> value_export{
          "value_export"};
  sc_core::sc_export<
      sc_core::sc_signal_inout_if<sc_dt::sc_logic>>
      inverted_endpoint{
          "inverted_endpoint"};
  sc_core::sc_export<
      sc_core::sc_signal_inout_if<sc_dt::sc_logic>>
      inverted_export{
          "inverted_export"};
  NativeLeaf leaf;

  SC_CTOR(ExportHierarchy)
      : leaf{"leaf"} {
    value(value_channel);
    inverted(inverted_channel);
    value_endpoint(value_channel);
    value_export(value_endpoint);
    inverted_endpoint(inverted_channel);
    inverted_export(inverted_endpoint);
    leaf.value(value_export);
    leaf.inverted(inverted_export);
  }
};

SC_MODULE(UnboundExport) {
  sc_core::sc_export<
      sc_core::sc_signal_in_if<sc_dt::sc_logic>> dangling{
          "dangling"};

  SC_CTOR(UnboundExport) {}
};

SC_MODULE(DeepMiddle) {
  NativeLeaf leaf;

  SC_CTOR(DeepMiddle)
      : leaf{"leaf"} {}
};

SC_MODULE(InvalidDeepBinding) {
  sc_core::sc_signal<sc_dt::sc_logic> root_signal{
      "root_signal", sc_dt::sc_logic{'0'}};
  DeepMiddle middle;

  SC_CTOR(InvalidDeepBinding)
      : middle{"middle"} {
    middle.leaf.value(root_signal);
  }
};

struct LifecycleLeaf : sc_core::sc_module {
  explicit LifecycleLeaf(
      sc_core::sc_module_name name,
      std::vector<int>& events)
      : sc_core::sc_module(name),
        events_(events) {}

 protected:
  void before_end_of_elaboration() override {
    require_size(1);
    events_.push_back(2);
  }

  void end_of_elaboration() override {
    require_size(3);
    events_.push_back(4);
  }

  void start_of_simulation() override {
    require_size(5);
    events_.push_back(6);
  }

  void end_of_simulation() override {
    require_size(6);
    events_.push_back(7);
  }

 private:
  void require_size(const std::size_t expected) const {
    if (events_.size() != expected) {
      throw std::runtime_error{
          "native SystemC lifecycle order mismatch"};
    }
  }

  std::vector<int>& events_;
};

SC_MODULE(LifecycleModule) {
  std::vector<int> lifecycle_events;
  sc_core::sc_in<sc_dt::sc_logic> trigger{"trigger"};
  sc_core::sc_out<sc_dt::sc_logic> ready{"ready"};
  LifecycleLeaf leaf;

  SC_CTOR(LifecycleModule)
      : leaf{"leaf", lifecycle_events} {
    SC_METHOD(observe);
    sensitive << trigger;
    dont_initialize();
  }

  void observe() {
    ready.write(
        sc_dt::sc_logic{
            lifecycle_events.size() == 6 ? '1' : '0'});
  }

 protected:
  void before_end_of_elaboration() override {
    require_size(0);
    lifecycle_events.push_back(1);
  }

  void end_of_elaboration() override {
    require_size(2);
    lifecycle_events.push_back(3);
  }

  void start_of_simulation() override {
    require_size(4);
    lifecycle_events.push_back(5);
  }

  void end_of_simulation() override {
    require_size(7);
    lifecycle_events.push_back(8);
  }

 private:
  void require_size(const std::size_t expected) const {
    if (lifecycle_events.size() != expected) {
      throw std::runtime_error{
          "root SystemC lifecycle order mismatch"};
    }
  }
};

SC_MODULE(ThrowingLifecycle) {
  SC_CTOR(ThrowingLifecycle) {}

 protected:
  void before_end_of_elaboration() override {
    throw std::runtime_error{
        "intentional lifecycle elaboration failure"};
  }
};

SC_MODULE(ThrowingEndLifecycle) {
  SC_CTOR(ThrowingEndLifecycle) {}

 protected:
  void end_of_simulation() override {
    throw std::runtime_error{
        "intentional lifecycle terminal failure"};
  }
};

namespace {
void* create_model(void*, const char*, fsim_sc_handle_v1) {
  return reinterpret_cast<void*>(0x1);
}
void destroy_model(void*, void*) {}
}

SC_MODULE(HdlBridge) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  fsim::systemc::hdl_instance u_hdl{"u_hdl"};

  SC_CTOR(HdlBridge) {
    u_hdl.set_actual(
        "INVERT",
        fsim::systemc::construction_value<int>("CHILD_INVERT"));
    u_hdl.bind_input("value", value);
    u_hdl.bind_output("inverted", inverted);
  }
};

SC_MODULE(FiberThreads) {
  sc_core::sc_in<sc_dt::sc_logic> clock{"clock"};
  sc_core::sc_out<sc_dt::sc_lv<8>> count{"count"};
  sc_core::sc_out<sc_dt::sc_lv<8>> timed{"timed"};
  sc_core::sc_out<sc_dt::sc_lv<8>> event_count{"event_count"};
  sc_core::sc_event pulse{"pulse"};

  SC_CTOR(FiberThreads) {
    SC_CTHREAD(clocked_run, clock.pos());
    SC_THREAD(timed_run);
    SC_METHOD(notify_run);
    sensitive << clock.pos();
    dont_initialize();
    SC_THREAD(event_run);
  }

  void clocked_run() {
    unsigned value = 0;
    while (true) {
      sc_core::wait();
      ++value;
      count.write(
          value == 1
              ? sc_dt::sc_lv<8>{"00000001"}
              : sc_dt::sc_lv<8>{"00000010"});
    }
  }

  void timed_run() {
    timed.write(sc_dt::sc_lv<8>{"00000001"});
    sc_core::wait(sc_core::sc_time{2, sc_core::SC_NS});
    timed.write(sc_dt::sc_lv<8>{"00000010"});
    sc_core::wait(sc_core::SC_ZERO_TIME);
    timed.write(sc_dt::sc_lv<8>{"00000011"});
  }

  void notify_run() {
    pulse.notify();
  }

  void event_run() {
    sc_core::wait(pulse);
    event_count.write(sc_dt::sc_lv<8>{"00000001"});
    sc_core::wait(pulse);
    event_count.write(sc_dt::sc_lv<8>{"00000010"});
  }
};

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  if (host == nullptr || registrar == nullptr
      || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || host->struct_size < sizeof(fsim_sc_host_v1)
      || registrar->struct_size < sizeof(fsim_sc_registrar_v1)
      || registrar->register_factory == nullptr
      || registrar->register_elaboration_factory == nullptr) {
    return FSIM_SC_ABI_MISMATCH;
  }
  const auto status = registrar->register_factory(
      registrar->context,
      "model",
      create_model,
      destroy_model,
      nullptr);
  if (status != FSIM_SC_OK) {
    return status;
  }
  constexpr std::array<fsim::systemc::factory_parameter, 1>
      bridge_parameters{{
          {"CHILD_INVERT",
           FSIM_SC_CONSTRUCTION_BOOLEAN,
           true,
           0},
      }};
  const auto bridge_status =
      fsim::systemc::register_module_factory<HdlBridge>(
          host, registrar, "bridge", bridge_parameters);
  if (bridge_status != FSIM_SC_OK) {
    return bridge_status;
  }
  const auto thread_status =
      fsim::systemc::register_module_factory<FiberThreads>(
          host, registrar, "fiber_threads");
  if (thread_status != FSIM_SC_OK) {
    return thread_status;
  }
  constexpr std::array<fsim::systemc::factory_parameter, 1>
      method_parameters{{
          {"PASSTHROUGH",
           FSIM_SC_CONSTRUCTION_BOOLEAN,
           true,
           0},
      }};
  const auto method_status =
      fsim::systemc::register_module_factory<MethodBridge>(
          host, registrar, "method_bridge", method_parameters);
  if (method_status != FSIM_SC_OK) {
    return method_status;
  }
  const auto noinit_status =
      fsim::systemc::register_module_factory<NoInitBridge>(
      host, registrar, "noinit_bridge");
  if (noinit_status != FSIM_SC_OK) {
    return noinit_status;
  }
  const auto edge_status =
      fsim::systemc::register_module_factory<EdgeCounter>(
          host, registrar, "edge_counter");
  if (edge_status != FSIM_SC_OK) {
    return edge_status;
  }
  const auto throwing_status =
      fsim::systemc::register_module_factory<ThrowingMethod>(
      host, registrar, "throwing_method");
  if (throwing_status != FSIM_SC_OK) {
    return throwing_status;
  }
  const auto dynamic_status =
      fsim::systemc::register_module_factory<DynamicEvents>(
          host, registrar, "dynamic_events");
  if (dynamic_status != FSIM_SC_OK) {
    return dynamic_status;
  }
  const auto event_list_status =
      fsim::systemc::register_module_factory<EventLists>(
          host, registrar, "event_lists");
  if (event_list_status != FSIM_SC_OK) {
    return event_list_status;
  }
  const auto kernel_channel_status =
      fsim::systemc::register_module_factory<KernelChannels>(
          host, registrar, "kernel_channels");
  if (kernel_channel_status != FSIM_SC_OK) {
    return kernel_channel_status;
  }
  const auto internal_signal_status =
      fsim::systemc::register_module_factory<InternalSignals>(
          host, registrar, "internal_signals");
  if (internal_signal_status != FSIM_SC_OK) {
    return internal_signal_status;
  }
  const auto bound_port_status =
      fsim::systemc::register_module_factory<BoundPorts>(
          host, registrar, "bound_ports");
  if (bound_port_status != FSIM_SC_OK) {
    return bound_port_status;
  }
  const auto native_hierarchy_status =
      fsim::systemc::register_module_factory<NativeHierarchy>(
          host, registrar, "native_hierarchy");
  if (native_hierarchy_status != FSIM_SC_OK) {
    return native_hierarchy_status;
  }
  const auto duplicate_status =
      fsim::systemc::register_module_factory<
      DuplicateNativeHierarchy>(
          host, registrar, "duplicate_native_hierarchy");
  if (duplicate_status != FSIM_SC_OK) {
    return duplicate_status;
  }
  const auto lifecycle_status =
      fsim::systemc::register_module_factory<LifecycleModule>(
          host, registrar, "lifecycle_module");
  if (lifecycle_status != FSIM_SC_OK) {
    return lifecycle_status;
  }
  const auto throwing_lifecycle_status =
      fsim::systemc::register_module_factory<ThrowingLifecycle>(
          host, registrar, "throwing_lifecycle");
  if (throwing_lifecycle_status != FSIM_SC_OK) {
    return throwing_lifecycle_status;
  }
  const auto throwing_end_status =
      fsim::systemc::register_module_factory<
          ThrowingEndLifecycle>(
              host, registrar, "throwing_end_lifecycle");
  if (throwing_end_status != FSIM_SC_OK) {
    return throwing_end_status;
  }
  const auto port_chain_status =
      fsim::systemc::register_module_factory<PortChainHierarchy>(
          host, registrar, "port_chain_hierarchy");
  if (port_chain_status != FSIM_SC_OK) {
    return port_chain_status;
  }
  const auto export_status =
      fsim::systemc::register_module_factory<ExportHierarchy>(
          host, registrar, "export_hierarchy");
  if (export_status != FSIM_SC_OK) {
    return export_status;
  }
  const auto unbound_export_status =
      fsim::systemc::register_module_factory<UnboundExport>(
          host, registrar, "unbound_export");
  if (unbound_export_status != FSIM_SC_OK) {
    return unbound_export_status;
  }
  return fsim::systemc::register_module_factory<
      InvalidDeepBinding>(
          host, registrar, "invalid_deep_binding");
}
)";
}

systemc_boundary_source =
    directory / "systemc_boundary.sv";
{
  std::ofstream output(systemc_boundary_source);
  output << R"(
module systemc_hdl_child #(
  parameter INVERT = 1
) (
  input logic value,
  output logic inverted
);
  assign inverted = INVERT ? ~value : value;
endmodule

module systemc_host #(
  parameter CHILD_MODE = 1
);
  logic value;
  logic inverted;
  bridge_placeholder #(
      .CHILD_INVERT(CHILD_MODE)
  ) u_bridge(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_method_host;
  logic value;
  logic inverted;
  logic observed;
  method_bridge_placeholder u_method(
      .value(value),
      .inverted(inverted));
  always_comb observed = inverted;
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_edge_host;
  bit clock;
  logic [7:0] count;
  edge_counter_placeholder u_counter(
      .clock(clock),
      .count(count));
  initial begin
    clock = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_bound_port_host;
  logic value;
  logic inverted;
  bound_ports_placeholder u_bound(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_native_hierarchy_host;
  logic value;
  logic inverted;
  native_hierarchy_placeholder u_native(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_lifecycle_host;
  logic trigger;
  logic ready;
  lifecycle_module_placeholder u_lifecycle(
      .trigger(trigger),
      .ready(ready));
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_port_chain_host;
  logic value;
  logic inverted;
  port_chain_hierarchy_placeholder u_chain(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_export_host;
  logic value;
  logic inverted;
  export_hierarchy_placeholder u_export(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_thread_host;
  logic clock;
  logic [7:0] count;
  logic [7:0] timed;
  logic [7:0] event_count;
  fiber_threads_placeholder u_threads(
      .clock(clock),
      .count(count),
      .timed(timed),
      .event_count(event_count));
  initial begin
    clock = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
}
systemc_method_vhdl_source =
    directory / "systemc_method.vhd";
{
  std::ofstream output(systemc_method_vhdl_source);
  output << R"(
entity systemc_method_vhdl_host is
end entity systemc_method_vhdl_host;

architecture rtl of systemc_method_vhdl_host is
  signal value : std_logic;
  signal inverted : std_logic;
begin
  value <= '1';
  u_method: method_bridge_placeholder
    generic map (passthrough => true)
    port map (value => value, inverted => inverted);
end architecture rtl;
)";
}

}

}  // namespace fsim::test
