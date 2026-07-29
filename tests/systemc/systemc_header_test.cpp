// SPDX-License-Identifier: Apache-2.0
#include <systemc>

#include <cassert>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

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

struct TestPrimitiveChannel final : sc_core::sc_prim_channel {
    TestPrimitiveChannel() : sc_core::sc_prim_channel("test_channel") {}
    void update() override {}
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
    constexpr sc_dt::sc_logic zero{false};
    constexpr sc_dt::sc_logic one{true};
    constexpr sc_dt::sc_logic high_impedance{
        sc_dt::sc_logic::Log_Z};
    static_assert((~zero).value() == sc_dt::sc_logic::Log_1);
    static_assert((one & high_impedance).value()
                  == sc_dt::sc_logic::Log_X);
    static_assert((zero & high_impedance).value()
                  == sc_dt::sc_logic::Log_0);
    static_assert((one | high_impedance).value()
                  == sc_dt::sc_logic::Log_1);
    static_assert((zero | high_impedance).value()
                  == sc_dt::sc_logic::Log_X);
    static_assert((one ^ zero).value()
                  == sc_dt::sc_logic::Log_1);
    static_assert((one ^ high_impedance).value()
                  == sc_dt::sc_logic::Log_X);
    auto assigned_logic = high_impedance;
    assigned_logic &= zero;
    assert(assigned_logic.to_char() == '0');
    assigned_logic = 'Z';
    assigned_logic |= one;
    assert(assigned_logic.to_char() == '1');
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
    sc_dt::sc_bv<4> mutable_bits{UINT64_C(5)};
    assert(
        mutable_bits.to_string() == "0101"
        && mutable_bits.to_uint64() == 5
        && sc_dt::sc_bv<4>::length() == 4);
    mutable_bits[1] = true;
    mutable_bits[0].flip();
    assert(mutable_bits.to_string() == "0110");
    assert(
        (mutable_bits & sc_dt::sc_bv<4>{"1010"}).to_string()
        == "0010");
    assert(
        (mutable_bits | sc_dt::sc_bv<4>{"1001"}).to_string()
        == "1111");
    assert(
        (mutable_bits ^ sc_dt::sc_bv<4>{"1111"}).to_string()
        == "1001");
    assert((~mutable_bits).to_string() == "1001");
    assert((mutable_bits << 2).to_string() == "1000");
    assert((mutable_bits >> 2).to_string() == "0001");
    assert(sc_dt::sc_bv<4>{"1111"}.and_reduce());
    assert(sc_dt::sc_bv<4>{"1000"}.or_reduce());
    assert(sc_dt::sc_bv<4>{"1011"}.xor_reduce());
    sc_dt::sc_bv<80> wide_bits{UINT64_C(0x8000000000000001)};
    wide_bits[79] = true;
    assert(
        wide_bits.to_uint64() == UINT64_C(0x8000000000000001)
        && wide_bits.to_string().front() == '1');
    bool rejected_bv_index = false;
    try {
        (void)mutable_bits[4];
    } catch (const std::out_of_range&) {
        rejected_bv_index = true;
    }
    assert(rejected_bv_index);

    sc_dt::sc_lv<4> logic_bits{"10XZ"};
    assert(!logic_bits.is_01());
    assert((~logic_bits).to_string() == "01XX");
    assert(
        (logic_bits & sc_dt::sc_lv<4>{"1100"}).to_string()
        == "1000");
    assert(
        (logic_bits | sc_dt::sc_lv<4>{"0011"}).to_string()
        == "1011");
    assert(
        (logic_bits ^ sc_dt::sc_lv<4>{"1001"}).to_string()
        == "00XX");
    assert((logic_bits << 1).to_string() == "0XZ0");
    assert((logic_bits >> 2).to_string() == "0010");
    assert(
        sc_dt::sc_lv<4>{"1111"}.and_reduce().to_char()
        == '1');
    assert(
        sc_dt::sc_lv<4>{"00XZ"}.or_reduce().to_char()
        == 'X');
    logic_bits[0] = sc_dt::sc_logic{'1'};
    assert(logic_bits.to_string() == "10X1");
    bool rejected_lv_conversion = false;
    try {
        (void)logic_bits.to_uint64();
    } catch (const std::logic_error&) {
        rejected_lv_conversion = true;
    }
    assert(rejected_lv_conversion);

    sc_dt::sc_uint<4> unsigned_value{15};
    assert(
        (unsigned_value + 1).to_uint64() == 0
        && (unsigned_value - sc_dt::sc_uint<4>{2}).to_uint64()
            == 13
        && (unsigned_value * sc_dt::sc_uint<4>{3}).to_uint64()
            == 13);
    unsigned_value = 6;
    assert(
        (unsigned_value / sc_dt::sc_uint<4>{4}).to_uint64()
            == 1
        && (unsigned_value % sc_dt::sc_uint<4>{4}).to_uint64()
            == 2);
    unsigned_value[3] = true;
    unsigned_value[1].flip();
    assert(
        unsigned_value.to_string() == "1100"
        && unsigned_value.raw_bits() == 12);
    assert(
        (~unsigned_value).to_string() == "0011"
        && (unsigned_value & sc_dt::sc_uint<4>{10}).to_uint64()
            == 8
        && (unsigned_value | sc_dt::sc_uint<4>{3}).to_uint64()
            == 15
        && (unsigned_value ^ sc_dt::sc_uint<4>{15}).to_uint64()
            == 3);
    assert(
        (unsigned_value << 2).to_uint64() == 0
        && (unsigned_value >> 2).to_uint64() == 3);
    assert(unsigned_value.or_reduce());
    assert(!unsigned_value.and_reduce());
    assert(!unsigned_value.xor_reduce());
    assert(unsigned_value > 5 && 12 == unsigned_value);
    const auto unsigned_before_increment = unsigned_value++;
    assert(
        unsigned_before_increment.to_uint64() == 12
        && unsigned_value.to_uint64() == 13);
    --unsigned_value;
    assert(unsigned_value.to_uint64() == 12);
    const sc_dt::sc_uint<4> unsigned_from_vector{
        sc_dt::sc_bv<6>{"101101"}};
    assert(unsigned_from_vector.to_uint64() == 13);
    bool rejected_uint_division = false;
    try {
        (void)(unsigned_value / sc_dt::sc_uint<4>{0});
    } catch (const std::domain_error&) {
        rejected_uint_division = true;
    }
    assert(rejected_uint_division);
    bool rejected_uint_shift = false;
    try {
        (void)(unsigned_value << -1);
    } catch (const std::invalid_argument&) {
        rejected_uint_shift = true;
    }
    assert(rejected_uint_shift);
    bool rejected_uint_index = false;
    try {
        (void)unsigned_value[4];
    } catch (const std::out_of_range&) {
        rejected_uint_index = true;
    }
    assert(rejected_uint_index);

    sc_dt::sc_int<4> negative{-1};
    assert(negative.to_int64() == -1);
    assert(
        (sc_dt::sc_int<4>{7} + sc_dt::sc_int<4>{1}).to_int64()
        == -8);
    assert(
        (sc_dt::sc_int<4>{-8} - sc_dt::sc_int<4>{1}).to_int64()
        == 7);
    assert(
        (sc_dt::sc_int<4>{-3} * sc_dt::sc_int<4>{3}).to_int64()
        == 7);
    assert(
        (sc_dt::sc_int<4>{-7} / sc_dt::sc_int<4>{2}).to_int64()
            == -3
        && (sc_dt::sc_int<4>{-7} % sc_dt::sc_int<4>{2})
                   .to_int64()
            == -1);
    assert(
        (sc_dt::sc_int<4>{-4} >> 1).to_int64() == -2
        && (sc_dt::sc_int<4>{-4} << 1).to_int64() == -8);
    auto signed_bits = sc_dt::sc_int<4>{-2};
    signed_bits[3] = false;
    signed_bits[0].flip();
    assert(
        signed_bits.to_int64() == 7
        && signed_bits.to_string() == "0111");
    assert(
        (~signed_bits).to_int64() == -8
        && (-signed_bits).to_int64() == -7);
    assert(sc_dt::sc_int<4>{-1}.and_reduce());
    assert(sc_dt::sc_int<4>{-8}.or_reduce());
    assert(sc_dt::sc_int<4>{7}.xor_reduce());
    assert(
        sc_dt::sc_int<4>{-1} < sc_dt::sc_int<4>{1}
        && sc_dt::sc_int<4>{-1} < 0
        && -1 == sc_dt::sc_int<4>{-1});
    const sc_dt::sc_int<4> signed_from_vector{
        sc_dt::sc_bv<4>{"1001"}};
    assert(signed_from_vector.to_int64() == -7);
    sc_dt::sc_int<64> minimum{
        std::numeric_limits<std::int64_t>::min()};
    assert(
        (minimum / sc_dt::sc_int<64>{-1}).to_int64()
        == std::numeric_limits<std::int64_t>::min());
    assert(
        (minimum % sc_dt::sc_int<64>{-1}).to_int64() == 0);
    bool rejected_int_modulo = false;
    try {
        (void)(negative % sc_dt::sc_int<4>{0});
    } catch (const std::domain_error&) {
        rejected_int_modulo = true;
    }
    assert(rejected_int_modulo);
    bool rejected_int_shift = false;
    try {
        (void)(negative >> -1);
    } catch (const std::invalid_argument&) {
        rejected_int_shift = true;
    }
    assert(rejected_int_shift);
    bool rejected_int_index = false;
    try {
        (void)negative[4];
    } catch (const std::out_of_range&) {
        rejected_int_index = true;
    }
    assert(rejected_int_index);

    sc_core::sc_signal<bool> clock;
    sc_core::sc_signal<sc_dt::sc_uint<8>> value;
    static_assert(
        std::is_base_of_v<
            sc_core::sc_prim_channel,
            sc_core::sc_signal<bool>>);
    static_assert(
        std::is_base_of_v<
            sc_core::sc_signal_in_if<bool>,
            sc_core::sc_signal<bool>>);
    static_assert(
        std::is_base_of_v<
            sc_core::sc_signal_inout_if<bool>,
            sc_core::sc_signal<bool>>);
    sc_core::sc_signal<sc_dt::sc_uint<8>> initialized{
        "initialized", sc_dt::sc_uint<8>{7}};
    assert(initialized.read().to_uint64() == 7);
    initialized.write(sc_dt::sc_uint<8>{9});
    assert(
        initialized.read().to_uint64() == 9
        && initialized.event());
    assert(
        initialized.value_changed_event().native_handle()
        == initialized.native_handle());
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
    bool rejected_notify_delayed = false;
    try {
        event.notify_delayed(sc_core::SC_ZERO_TIME);
    } catch (const std::logic_error&) {
        rejected_notify_delayed = true;
    }
    assert(rejected_notify_delayed);
    bool rejected_cancel = false;
    try {
        event.cancel();
    } catch (const std::logic_error&) {
        rejected_cancel = true;
    }
    assert(rejected_cancel);
    sc_core::sc_event second_event;
    bool rejected_or_trigger = false;
    try {
        sc_core::next_trigger(event | second_event);
    } catch (const std::logic_error&) {
        rejected_or_trigger = true;
    }
    assert(rejected_or_trigger);
    bool rejected_and_trigger = false;
    try {
        sc_core::next_trigger(event & second_event);
    } catch (const std::logic_error&) {
        rejected_and_trigger = true;
    }
    assert(rejected_and_trigger);
    TestPrimitiveChannel channel;
    assert(!channel.update_requested());
    bool rejected_channel_update = false;
    try {
        channel.request_update();
    } catch (const std::logic_error&) {
        rejected_channel_update = true;
    }
    assert(rejected_channel_update);

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
    sc_core::sc_export<ValueInterface> chained_export{
        "chained_export"};
    chained_export.bind(exported);
    assert(chained_export->value() == 42);
    bool rejected_export_cycle = false;
    try {
        exported.bind(chained_export);
    } catch (const std::logic_error&) {
        rejected_export_cycle = true;
    }
    assert(rejected_export_cycle);

    sc_core::sc_export<
        sc_core::sc_signal_in_if<bool>> signal_export{
            "signal_export"};
    signal_export.bind(clock);
    sc_core::sc_in<bool> exported_input{"exported_input"};
    exported_input.bind(signal_export);
    clock.write(true);
    assert(exported_input.read());

    sc_core::sc_out<bool> parent_output{"parent_output"};
    sc_core::sc_out<bool> child_output{"child_output"};
    parent_output.bind(clock);
    child_output.bind(parent_output);
    child_output.write(false);
    assert(!clock.read());
    bool rejected_port_cycle = false;
    try {
        parent_output.bind(child_output);
    } catch (const std::logic_error&) {
        rejected_port_cycle = true;
    }
    assert(rejected_port_cycle);

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
