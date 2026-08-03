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

SC_MODULE(TimedMethodTriggers) {
  sc_core::sc_out<sc_dt::sc_uint<8>> single_seen{"single_seen"};
  sc_core::sc_out<sc_dt::sc_uint<8>> or_seen{"or_seen"};
  sc_core::sc_out<sc_dt::sc_uint<8>> and_seen{"and_seen"};
  sc_core::sc_event first{"first"};
  sc_core::sc_event second{"second"};
  sc_core::sc_event absent{"absent"};
  unsigned producer_state{};
  unsigned single_state{};
  unsigned or_state{};
  unsigned and_state{};

  SC_CTOR(TimedMethodTriggers) {
    SC_METHOD(produce);
    SC_METHOD(observe_single);
    SC_METHOD(observe_or);
    SC_METHOD(observe_and);
  }

  void produce() {
    ++producer_state;
    if (producer_state == 1) {
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 2) {
      first.notify();
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 3) {
      second.notify();
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    }
  }

  void observe_single() {
    if (single_state++ == 0) {
      next_trigger(sc_core::sc_time{100, sc_core::SC_NS}, absent);
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS}, first);
      return;
    }
    single_seen.write(sc_dt::sc_uint<8>{
        single_state == 2 ? 10U + producer_state
                          : 20U + producer_state});
    if (single_state == 2) {
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS}, absent);
    }
  }

  void observe_or() {
    if (or_state++ == 0) {
      next_trigger(sc_core::sc_time{100, sc_core::SC_NS}, absent);
      next_trigger(
          sc_core::sc_time{3, sc_core::SC_NS}, first | second);
      return;
    }
    or_seen.write(sc_dt::sc_uint<8>{
        or_state == 2 ? 10U + producer_state
                      : 20U + producer_state});
    if (or_state == 2) {
      next_trigger(
          sc_core::sc_time{1, sc_core::SC_NS}, absent | absent);
    }
  }

  void observe_and() {
    if (and_state++ == 0) {
      next_trigger(sc_core::sc_time{100, sc_core::SC_NS}, absent);
      next_trigger(
          sc_core::sc_time{4, sc_core::SC_NS}, first & second);
      return;
    }
    and_seen.write(sc_dt::sc_uint<8>{
        and_state == 2 ? 10U + producer_state
                       : 20U + producer_state});
    if (and_state == 2) {
      next_trigger(
          sc_core::sc_time{1, sc_core::SC_NS}, first & absent);
    }
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

class CrossChannel final : public sc_core::sc_prim_channel {
 public:
  CrossChannel(
      const char* name,
      DeferredChannel& target,
      sc_core::sc_event& event,
      sc_core::sc_out<sc_dt::sc_uint<8>>& updates)
      : sc_core::sc_prim_channel(name),
        target_(target),
        event_(event),
        updates_(updates) {}

  void schedule() { request_update(); }

 protected:
  void update() override {
    ++update_count_;
    updates_.write(sc_dt::sc_uint<8>{update_count_});
    request_update();
    target_.write(9);
    event_.notify();
  }

 private:
  DeferredChannel& target_;
  sc_core::sc_event& event_;
  sc_core::sc_out<sc_dt::sc_uint<8>>& updates_;
  unsigned update_count_{};
};

class ThrowingUpdateChannel final : public sc_core::sc_prim_channel {
 public:
  explicit ThrowingUpdateChannel(const char* name)
      : sc_core::sc_prim_channel(name) {}

  void schedule() { request_update(); }

 protected:
  void update() override {
    throw std::runtime_error{"intentional channel update failure"};
  }
};

SC_MODULE(ChannelUpdateFailure) {
  ThrowingUpdateChannel channel{"channel"};

  SC_CTOR(ChannelUpdateFailure) { SC_METHOD(run); }

  void run() { channel.schedule(); }
};

SC_MODULE(KernelChannels) {
  sc_core::sc_out<sc_dt::sc_uint<8>> value{"value"};
  sc_core::sc_out<sc_dt::sc_uint<8>> updates{"updates"};
  sc_core::sc_out<sc_dt::sc_uint<8>> event_count{"event_count"};
  sc_core::sc_out<sc_dt::sc_uint<8>> cross_updates{"cross_updates"};
  sc_core::sc_event pulse{"pulse"};
  DeferredChannel deferred;
  CrossChannel cross;
  unsigned producer_state{};
  unsigned observed_events{};

  SC_CTOR(KernelChannels)
      : deferred{"deferred", value, updates},
        cross{"cross", deferred, pulse, cross_updates} {
    SC_METHOD(produce);
    SC_METHOD(consume);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      deferred.write(1);
      deferred.write(2);
      cross.schedule();
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

struct MetadataInterface : sc_core::sc_interface {
  static const char* fsim_kind() noexcept {
    return "models.metadata_interface";
  }
  virtual unsigned inspect() const = 0;
};

struct MetadataChannel final
    : sc_core::sc_prim_channel,
      MetadataInterface {
  explicit MetadataChannel(const char* name)
      : sc_core::sc_prim_channel(
            name, "models.metadata_channel") {}

  unsigned inspect() const override { return 7; }
};

SC_MODULE(CustomMetadata) {
  sc_core::sc_port<MetadataInterface> endpoint{"endpoint"};
  sc_core::sc_export<MetadataInterface> exposed{"exposed"};
  MetadataChannel channel{"channel"};

  SC_CTOR(CustomMetadata) {}
};

SC_MODULE(CustomBindingFailure) {
  sc_core::sc_port<MetadataInterface> endpoint{"endpoint"};
  MetadataChannel channel{"channel"};

  SC_CTOR(CustomBindingFailure) {
    endpoint.bind(channel);
  }
};

SC_MODULE(CustomUpdateFailure) {
  MetadataChannel channel{"channel"};

  SC_CTOR(CustomUpdateFailure) {
    SC_METHOD(run);
  }

  void run() { channel.request_update(); }
};

SC_MODULE(CustomValueFailure) {
  sc_core::sc_port<MetadataInterface> endpoint{"endpoint"};

  SC_CTOR(CustomValueFailure) {
    SC_METHOD(run);
  }

  void run() { (void)endpoint->inspect(); }
};

SC_MODULE(EventTickFailure) {
  sc_core::sc_event pulse{"pulse"};

  SC_CTOR(EventTickFailure) { SC_METHOD(run); }

  void run() { pulse.notify(sc_core::sc_time{1, sc_core::SC_PS}); }
};

SC_MODULE(DelayedPendingFailure) {
  sc_core::sc_event pulse{"pulse"};

  SC_CTOR(DelayedPendingFailure) { SC_METHOD(run); }

  void run() {
    pulse.notify_delayed(sc_core::sc_time{2, sc_core::SC_NS});
    pulse.notify_delayed(sc_core::sc_time{1, sc_core::SC_NS});
  }
};

SC_MODULE(NamedObjectMatrix) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  sc_core::sc_signal<sc_dt::sc_logic> value_channel{
      "value_channel", sc_dt::sc_logic{'0'}};
  sc_core::sc_signal<sc_dt::sc_logic> inverted_channel{
      "inverted_channel", sc_dt::sc_logic{'1'}};
  sc_core::sc_export<sc_core::sc_signal_in_if<sc_dt::sc_logic>>
      value_endpoint{"value_endpoint"};
  sc_core::sc_export<sc_core::sc_signal_inout_if<sc_dt::sc_logic>>
      inverted_endpoint{"inverted_endpoint"};
  sc_core::sc_port<MetadataInterface> custom_port{"custom_port"};
  sc_core::sc_export<MetadataInterface> custom_export{"custom_export"};
  MetadataChannel custom_channel{"custom_channel"};
  sc_core::sc_event pulse{"pulse"};
  NativeLeaf leaf;
  bool elaborated{};
  bool started{};

  SC_CTOR(NamedObjectMatrix)
      : leaf{"leaf"} {
    value(value_channel);
    inverted(inverted_channel);
    value_endpoint(value_channel);
    inverted_endpoint(inverted_channel);
    leaf.value(value_endpoint);
    leaf.inverted(inverted_endpoint);
  }

 protected:
  void end_of_elaboration() override { elaborated = true; }
  void start_of_simulation() override {
    if (!elaborated) {
      throw std::runtime_error{"named matrix lifecycle order mismatch"};
    }
    started = true;
  }
  void end_of_simulation() override {
    if (!started) {
      throw std::runtime_error{"named matrix lifecycle did not start"};
    }
  }
};

SC_MODULE(UnboundExport) {
  sc_core::sc_export<
      sc_core::sc_signal_in_if<sc_dt::sc_logic>> dangling{
          "dangling"};

  SC_CTOR(UnboundExport) {}
};

SC_MODULE(UnboundNativeHierarchy) {
  NativeLeaf leaf;

  SC_CTOR(UnboundNativeHierarchy)
      : leaf{"leaf"} {}
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

SC_MODULE(LifecycleEventFailure) {
  sc_core::sc_event pulse{"pulse"};

  SC_CTOR(LifecycleEventFailure) {}

 protected:
  void start_of_simulation() override { pulse.notify(); }
};

SC_MODULE(LifecycleSuspendFailure) {
  SC_CTOR(LifecycleSuspendFailure) {}

 protected:
  void start_of_simulation() override {
    next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
  }
};

namespace {
void* create_model(void*, const char*, fsim_sc_handle_v1) {
  return reinterpret_cast<void*>(0x1);
}
void destroy_model(void*, void*) {}
void probe_update(void*) {}

fsim_sc_status_v1 elaborate_port_direction_probe(
    void* user,
    const char*,
    fsim_sc_handle_v1 module,
    fsim_sc_handle_v1,
    void** result) {
  const auto* host = static_cast<const fsim_sc_host_v1*>(user);
  if (host == nullptr || result == nullptr
      || host->register_port == nullptr
      || host->register_native_module == nullptr
      || host->bind_port == nullptr
      || host->register_primitive_channel == nullptr
      || host->register_signal == nullptr
      || host->register_export == nullptr
      || host->set_export_writable == nullptr
      || host->bind_export == nullptr) {
    return FSIM_SC_ABI_MISMATCH;
  }
  fsim_sc_handle_v1 parent_input = 0;
  fsim_sc_handle_v1 parent_output = 0;
  fsim_sc_handle_v1 child = 0;
  fsim_sc_handle_v1 child_input = 0;
  fsim_sc_handle_v1 child_output = 0;
  fsim_sc_handle_v1 export_input = 0;
  fsim_sc_handle_v1 channel = 0;
  fsim_sc_handle_v1 read_export = 0;
  fsim_sc_handle_v1 write_export = 0;
  static int channel_state = 0;
  const std::array<std::uint8_t, 2> initial{{0, 0}};
  const fsim_sc_value_view_v1 initial_view{
      sizeof(fsim_sc_value_view_v1),
      FSIM_SC_LOGIC4,
      1,
      initial.data(),
      initial.size()};
  if (host->register_port(
          host->context, module, "sink", FSIM_SC_INPUT,
          FSIM_SC_LOGIC4, 1, &parent_input) != FSIM_SC_OK
      || host->register_port(
          host->context, module, "source", FSIM_SC_OUTPUT,
          FSIM_SC_LOGIC4, 1, &parent_output) != FSIM_SC_OK
      || host->register_native_module(
          host->context, module, "child", &child) != FSIM_SC_OK
      || host->register_port(
          host->context, child, "sink", FSIM_SC_INPUT,
          FSIM_SC_LOGIC4, 1, &child_input) != FSIM_SC_OK
      || host->register_port(
          host->context, child, "source", FSIM_SC_OUTPUT,
          FSIM_SC_LOGIC4, 1, &child_output) != FSIM_SC_OK
      || host->register_port(
          host->context, child, "export_sink", FSIM_SC_INPUT,
          FSIM_SC_LOGIC4, 1, &export_input) != FSIM_SC_OK
      || host->register_primitive_channel(
          host->context, module, "channel", probe_update,
          &channel_state, &channel) != FSIM_SC_OK
      || host->register_signal(
          host->context, module, channel, "channel",
          FSIM_SC_LOGIC4, 1, &initial_view) != FSIM_SC_OK
      || host->register_export(
          host->context, module, "read_export", FSIM_SC_LOGIC4,
          1, &read_export) != FSIM_SC_OK
      || host->set_export_writable(
          host->context, read_export, 0) != FSIM_SC_OK
      || host->register_export(
          host->context, module, "write_export", FSIM_SC_LOGIC4,
          1, &write_export) != FSIM_SC_OK
      || host->set_export_writable(
          host->context, write_export, 1) != FSIM_SC_OK) {
    return FSIM_SC_RUNTIME_ERROR;
  }
  if (host->bind_port(
          host->context, child_output, parent_input)
          != FSIM_SC_INVALID_ARGUMENT
      || host->bind_export(
          host->context, write_export, read_export)
          != FSIM_SC_INVALID_ARGUMENT
      || host->bind_port(
          host->context, child_output, read_export)
          != FSIM_SC_INVALID_ARGUMENT) {
    return FSIM_SC_RUNTIME_ERROR;
  }
  if (host->bind_port(
          host->context, child_input, parent_input) != FSIM_SC_OK
      || host->bind_port(
          host->context, child_output, parent_output) != FSIM_SC_OK
      || host->bind_export(
          host->context, read_export, channel) != FSIM_SC_OK
      || host->bind_export(
          host->context, write_export, channel) != FSIM_SC_OK
      || host->bind_port(
          host->context, export_input, read_export) != FSIM_SC_OK) {
    return FSIM_SC_RUNTIME_ERROR;
  }
  *result = new int{1};
  return FSIM_SC_OK;
}

void destroy_port_direction_probe(void*, void* object) {
  delete static_cast<int*>(object);
}
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
  sc_core::sc_out<sc_dt::sc_lv<8>> static_count{"static_count"};
  sc_core::sc_out<sc_dt::sc_lv<8>> named_count{"named_count"};
  sc_core::sc_out<sc_dt::sc_lv<8>> timeout_count{"timeout_count"};
  sc_core::sc_event pulse{"pulse"};
  sc_core::sc_event absent{"absent"};

  SC_CTOR(FiberThreads) {
    SC_CTHREAD(clocked_run, clock.pos());
    SC_THREAD(timed_run);
    SC_METHOD(notify_run);
    sensitive << clock.pos();
    dont_initialize();
    SC_METHOD(named_run);
    sensitive << pulse << pulse;
    dont_initialize();
    SC_THREAD(static_run);
    sensitive << clock.neg() << clock.neg();
    dont_initialize();
    SC_THREAD(event_run);
    SC_THREAD(timeout_run);
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

  void named_run() {
    named_value_ = named_value_ + 1;
    named_count.write(sc_dt::sc_lv<8>{
        named_value_ == 1 ? "00000001" : "00000010"});
  }

  void static_run() {
    unsigned value = 0;
    while (true) {
      ++value;
      static_count.write(sc_dt::sc_lv<8>{"00000001"});
      sc_core::wait();
    }
  }

  void event_run() {
    sc_core::wait(pulse);
    event_count.write(sc_dt::sc_lv<8>{"00000001"});
    sc_core::wait(pulse);
    event_count.write(sc_dt::sc_lv<8>{"00000010"});
  }

  void timeout_run() {
    sc_core::wait(sc_core::sc_time{2, sc_core::SC_NS}, pulse);
    timeout_count.write(sc_dt::sc_lv<8>{"00000001"});
    sc_core::wait(sc_core::sc_time{1, sc_core::SC_NS}, absent);
    timeout_count.write(sc_dt::sc_lv<8>{"00000010"});
    sc_core::wait(
        sc_core::sc_time{2, sc_core::SC_NS}, pulse | absent);
    timeout_count.write(sc_dt::sc_lv<8>{"00000011"});
    sc_core::wait(
        sc_core::sc_time{1, sc_core::SC_NS}, pulse & absent);
    timeout_count.write(sc_dt::sc_lv<8>{"00000100"});
  }

 private:
  unsigned named_value_{};
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
  const auto direction_status =
      registrar->register_elaboration_factory(
          registrar->context,
          "port_direction_probe",
          elaborate_port_direction_probe,
          destroy_port_direction_probe,
          const_cast<fsim_sc_host_v1*>(host));
  if (direction_status != FSIM_SC_OK) {
    return direction_status;
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
  const auto timed_method_status =
      fsim::systemc::register_module_factory<TimedMethodTriggers>(
          host, registrar, "timed_method_triggers");
  if (timed_method_status != FSIM_SC_OK) {
    return timed_method_status;
  }
  const auto kernel_channel_status =
      fsim::systemc::register_module_factory<KernelChannels>(
          host, registrar, "kernel_channels");
  if (kernel_channel_status != FSIM_SC_OK) {
    return kernel_channel_status;
  }
  const auto channel_failure_status =
      fsim::systemc::register_module_factory<ChannelUpdateFailure>(
          host, registrar, "channel_update_failure");
  if (channel_failure_status != FSIM_SC_OK) {
    return channel_failure_status;
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
  const auto lifecycle_event_status =
      fsim::systemc::register_module_factory<LifecycleEventFailure>(
          host, registrar, "lifecycle_event_failure");
  if (lifecycle_event_status != FSIM_SC_OK) {
    return lifecycle_event_status;
  }
  const auto lifecycle_suspend_status =
      fsim::systemc::register_module_factory<LifecycleSuspendFailure>(
          host, registrar, "lifecycle_suspend_failure");
  if (lifecycle_suspend_status != FSIM_SC_OK) {
    return lifecycle_suspend_status;
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
  const auto unbound_native_status =
      fsim::systemc::register_module_factory<
          UnboundNativeHierarchy>(
              host, registrar, "unbound_native_hierarchy");
  if (unbound_native_status != FSIM_SC_OK) {
    return unbound_native_status;
  }
  const auto invalid_deep_status =
      fsim::systemc::register_module_factory<
      InvalidDeepBinding>(
          host, registrar, "invalid_deep_binding");
  if (invalid_deep_status != FSIM_SC_OK) {
    return invalid_deep_status;
  }
  const auto metadata_status =
      fsim::systemc::register_module_factory<CustomMetadata>(
          host, registrar, "custom_metadata");
  if (metadata_status != FSIM_SC_OK) {
    return metadata_status;
  }
  const auto custom_binding_status =
      fsim::systemc::register_module_factory<
          CustomBindingFailure>(
              host, registrar, "custom_binding_failure");
  if (custom_binding_status != FSIM_SC_OK) {
    return custom_binding_status;
  }
  const auto custom_update_status =
      fsim::systemc::register_module_factory<
      CustomUpdateFailure>(
          host, registrar, "custom_update_failure");
  if (custom_update_status != FSIM_SC_OK) {
    return custom_update_status;
  }
  const auto custom_value_status =
      fsim::systemc::register_module_factory<
      CustomValueFailure>(
          host, registrar, "custom_value_failure");
  if (custom_value_status != FSIM_SC_OK) {
    return custom_value_status;
  }
  const auto tick_failure_status =
      fsim::systemc::register_module_factory<EventTickFailure>(
          host, registrar, "event_tick_failure");
  if (tick_failure_status != FSIM_SC_OK) {
    return tick_failure_status;
  }
  const auto delayed_failure_status =
      fsim::systemc::register_module_factory<DelayedPendingFailure>(
          host, registrar, "delayed_pending_failure");
  if (delayed_failure_status != FSIM_SC_OK) {
    return delayed_failure_status;
  }
  return fsim::systemc::register_module_factory<NamedObjectMatrix>(
      host, registrar, "named_object_matrix");
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

module systemc_named_object_matrix_host;
  logic value;
  logic inverted;
  named_object_matrix_placeholder u_matrix(
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
  logic [7:0] static_count;
  logic [7:0] named_count;
  logic [7:0] timeout_count;
  fiber_threads_placeholder u_threads(
      .clock(clock),
      .count(count),
      .timed(timed),
      .event_count(event_count),
      .static_count(static_count),
      .named_count(named_count),
      .timeout_count(timeout_count));
  initial begin
    clock = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    #1 clock = 1'b1;
    #2 $finish;
  end
endmodule

module systemc_schedule_lifecycle_host;
  logic clock;
  logic trigger;
  logic ready;
  logic [7:0] count;
  logic [7:0] timed;
  logic [7:0] thread_event_count;
  logic [7:0] static_count;
  logic [7:0] named_count;
  logic [7:0] timeout_count;
  logic [7:0] channel_value;
  logic [7:0] channel_updates;
  logic [7:0] channel_event_count;
  logic [7:0] cross_updates;
  fiber_threads_placeholder u_threads(
      .clock(clock),
      .count(count),
      .timed(timed),
      .event_count(thread_event_count),
      .static_count(static_count),
      .named_count(named_count),
      .timeout_count(timeout_count));
  kernel_channels_placeholder u_channels(
      .value(channel_value),
      .updates(channel_updates),
      .event_count(channel_event_count),
      .cross_updates(cross_updates));
  lifecycle_module_placeholder u_lifecycle(
      .trigger(trigger),
      .ready(ready));
  initial begin
    clock = 1'b0;
    trigger = 1'b0;
    #1 clock = 1'b1;
    trigger = 1'b1;
    #1 clock = 1'b0;
    #1 clock = 1'b1;
    #2 $finish;
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

entity systemc_thread_vhdl_host is
end entity systemc_thread_vhdl_host;

architecture rtl of systemc_thread_vhdl_host is
  signal clock : std_logic;
  signal count : std_logic_vector(7 downto 0);
  signal timed : std_logic_vector(7 downto 0);
  signal event_count : std_logic_vector(7 downto 0);
  signal static_count : std_logic_vector(7 downto 0);
  signal named_count : std_logic_vector(7 downto 0);
  signal timeout_count : std_logic_vector(7 downto 0);
begin
  u_threads: fiber_threads_placeholder
    port map (
      clock => clock,
      count => count,
      timed => timed,
      event_count => event_count,
      static_count => static_count,
      named_count => named_count,
      timeout_count => timeout_count);
  drive: process
  begin
    clock <= '0';
    wait for 1 ns;
    clock <= '1';
    wait for 1 ns;
    clock <= '0';
    wait for 1 ns;
    clock <= '1';
    wait for 2 ns;
    wait;
  end process drive;
end architecture rtl;

entity systemc_schedule_lifecycle_vhdl_host is
end entity systemc_schedule_lifecycle_vhdl_host;

architecture rtl of systemc_schedule_lifecycle_vhdl_host is
  signal clock : std_logic;
  signal trigger : std_logic;
  signal ready : std_logic;
  signal count : std_logic_vector(7 downto 0);
  signal timed : std_logic_vector(7 downto 0);
  signal thread_event_count : std_logic_vector(7 downto 0);
  signal static_count : std_logic_vector(7 downto 0);
  signal named_count : std_logic_vector(7 downto 0);
  signal timeout_count : std_logic_vector(7 downto 0);
  signal channel_value : std_logic_vector(7 downto 0);
  signal channel_updates : std_logic_vector(7 downto 0);
  signal channel_event_count : std_logic_vector(7 downto 0);
  signal cross_updates : std_logic_vector(7 downto 0);
begin
  u_threads: fiber_threads_placeholder
    port map (
      clock => clock,
      count => count,
      timed => timed,
      event_count => thread_event_count,
      static_count => static_count,
      named_count => named_count,
      timeout_count => timeout_count);
  u_channels: kernel_channels_placeholder
    port map (
      value => channel_value,
      updates => channel_updates,
      event_count => channel_event_count,
      cross_updates => cross_updates);
  u_lifecycle: lifecycle_module_placeholder
    port map (trigger => trigger, ready => ready);
  drive: process
  begin
    clock <= '0';
    trigger <= '0';
    wait for 1 ns;
    clock <= '1';
    trigger <= '1';
    wait for 1 ns;
    clock <= '0';
    wait for 1 ns;
    clock <= '1';
    wait for 2 ns;
    wait;
  end process drive;
end architecture rtl;
)";
}

}

}  // namespace fsim::test
