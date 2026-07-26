// SPDX-License-Identifier: Apache-2.0
#include <systemc>

#include <cassert>
#include <iostream>
#include <string_view>

SC_MODULE(Counter) {
    sc_core::sc_in<bool> clock{"clock"};
    sc_core::sc_out<sc_dt::sc_uint<8>> value{"value"};

    SC_CTOR(Counter) {
        SC_METHOD(tick);
        sensitive << clock;
        dont_initialize();
    }

    void tick() {
        count = count.to_uint64() + 1;
        value.write(count);
    }

    sc_dt::sc_uint<8> count{};
};

SC_MODULE(ClockedThread) {
    sc_core::sc_in<bool> clock{"clock"};

    SC_CTOR(ClockedThread) {
        SC_CTHREAD(run, clock.pos());
    }

    void run() {}
};

struct ValueInterface : sc_core::sc_interface {
    [[nodiscard]] virtual unsigned value() const = 0;
};

struct ValueImplementation final : ValueInterface {
    [[nodiscard]] unsigned value() const override { return 42; }
};

int main() {
    const sc_core::sc_time period{10, sc_core::SC_NS};
    assert(period.value() == 10'000'000);
    assert((sc_core::sc_time{0.1, sc_core::SC_NS}.value() == 100'000));
    assert((sc_core::sc_time{1.1, sc_core::SC_NS}.value() == 1'100'000));
    const sc_core::sc_time large{1.0e19, sc_core::SC_FS};
    assert(large.value() == UINT64_C(10000000000000000000));
    bool rejected_fraction = false;
    try {
        (void)sc_core::sc_time{0.5, sc_core::SC_FS};
    } catch (const std::invalid_argument&) {
        rejected_fraction = true;
    }
    assert(rejected_fraction);
    bool rejected_time_unit = false;
    try {
        (void)sc_core::sc_time{
            1.0, static_cast<sc_core::sc_time_unit>(6)};
    } catch (const std::invalid_argument&) {
        rejected_time_unit = true;
    }
    assert(rejected_time_unit);

    const sc_dt::sc_logic unknown{'X'};
    assert(unknown.to_char() == 'X');
    bool rejected_logic = false;
    try {
        (void)sc_dt::sc_logic{
            static_cast<sc_dt::sc_logic::value_t>(255)};
    } catch (const std::invalid_argument&) {
        rejected_logic = true;
    }
    assert(rejected_logic);

    const sc_dt::sc_bv<4> bits{"1010"};
    assert(bits.to_string() == "1010");

    sc_dt::sc_int<4> negative{-1};
    assert(negative.to_int64() == -1);

    sc_core::sc_signal<bool> clock;
    sc_core::sc_signal<sc_dt::sc_uint<8>> value;
    Counter counter{"counter"};
    counter.clock(clock);
    counter.value(value);
    assert(value.read().to_uint64() == 0);

    ClockedThread thread{"thread"};
    thread.clock(clock);
    sc_core::sc_inout<bool> bidirectional{"bidirectional"};
    bidirectional(clock);
    assert(
        bidirectional.pos().edge_kind() == FSIM_SC_POSEDGE
        && bidirectional.neg().edge_kind() == FSIM_SC_NEGEDGE);
    sc_core::sc_event event;
    bool rejected_notify = false;
    try {
        event.notify();
    } catch (const std::logic_error&) {
        rejected_notify = true;
    }
    assert(rejected_notify);

    ValueImplementation implementation;
    sc_core::sc_export<ValueInterface> exported{"exported"};
    bool rejected_unbound_export = false;
    try {
        (void)sc_core::sc_export<ValueInterface>{}.get_interface();
    } catch (const std::logic_error&) {
        rejected_unbound_export = true;
    }
    assert(rejected_unbound_export);
    exported.bind(implementation);
    assert(exported->value() == 42);

    const char* first_name = sc_core::sc_gen_unique_name("object");
    const char* second_name = sc_core::sc_gen_unique_name("object");
    assert(std::string_view{first_name} == "object_0");
    assert(std::string_view{second_name} == "object_1");
    assert(std::string_view{first_name} == "object_0");
    bool rejected_null_name = false;
    try {
        (void)sc_core::sc_gen_unique_name(nullptr);
    } catch (const std::invalid_argument&) {
        rejected_null_name = true;
    }
    assert(rejected_null_name);
    bool rejected_null_module_name = false;
    try {
        (void)Counter{sc_core::sc_module_name{nullptr}};
    } catch (const std::invalid_argument&) {
        rejected_null_module_name = true;
    }
    assert(rejected_null_module_name);

    std::cout << "SystemC header tests passed\n";
}
