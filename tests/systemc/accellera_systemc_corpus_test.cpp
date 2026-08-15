// SPDX-License-Identifier: Apache-2.0

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

extern "C" const char* fsim_systemc_accellera_version() noexcept;
extern "C" const void* fsim_systemc_accellera_context() noexcept;

#if defined(_WIN32)
#define FSIM_SYSTEMC_CORPUS_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define FSIM_SYSTEMC_CORPUS_EXPORT __attribute__((visibility("default")))
#else
#define FSIM_SYSTEMC_CORPUS_EXPORT
#endif

namespace {

constexpr std::size_t kRoots = 2U;
constexpr std::size_t kSamples = 1'024U;
constexpr std::size_t kStackProbeBytes = 4'096U;
constexpr std::size_t kMaximumRootBytes = 1U << 20U;

std::size_t completed_roots = 0U;
std::size_t destroyed_roots = 0U;

[[nodiscard]] sc_dt::sc_bv<129> bit_value(const std::size_t sample)
{
    std::string text(129U, '0');
    for (std::size_t bit = 0U; bit < text.size(); ++bit) {
        text[bit] = ((sample + bit * 3U) % 7U) < 3U ? '1' : '0';
    }
    return sc_dt::sc_bv<129> { text.c_str() };
}

[[nodiscard]] sc_dt::sc_lv<257> logic_value(const std::size_t sample)
{
    constexpr std::array states { '0', '1', 'X', 'Z' };
    std::string text(257U, '0');
    for (std::size_t bit = 0U; bit < text.size(); ++bit) {
        text[bit] = states[(sample + bit) % states.size()];
    }
    return sc_dt::sc_lv<257> { text.c_str() };
}

class CorpusTarget final : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<CorpusTarget> socket { "socket" };
    std::size_t transactions { };

    SC_CTOR(CorpusTarget)
    {
        socket.register_b_transport(this, &CorpusTarget::transport);
    }

private:
    std::array<std::uint32_t, 64U> memory_ { };

    void transport(tlm::tlm_generic_payload& payload, sc_core::sc_time& delay)
    {
        const auto address = static_cast<std::size_t>(payload.get_address());
        auto* const data = payload.get_data_ptr();
        if (address >= memory_.size() || data == nullptr
            || payload.get_data_length() != sizeof(std::uint32_t)
            || payload.get_streaming_width() < payload.get_data_length()
            || payload.get_byte_enable_ptr() != nullptr) {
            payload.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        if (payload.is_write()) {
            std::memcpy(&memory_[address], data, sizeof(std::uint32_t));
        } else if (payload.is_read()) {
            std::memcpy(data, &memory_[address], sizeof(std::uint32_t));
        } else {
            payload.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        ++transactions;
        delay += sc_core::sc_time { 1, sc_core::SC_PS };
        payload.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

class CorpusForwarder final : public sc_core::sc_module {
public:
    sc_core::sc_in<sc_dt::sc_bv<129>> input { "input" };
    sc_core::sc_out<sc_dt::sc_bv<129>> output { "output" };

    SC_CTOR(CorpusForwarder)
    {
        SC_METHOD(forward);
        sensitive << input;
    }

private:
    void forward()
    {
        output.write(input.read());
    }
};

class CorpusRoot final : public sc_core::sc_module {
public:
    sc_core::sc_clock clock { "clock", 1, sc_core::SC_NS };
    sc_core::sc_signal<sc_dt::sc_bv<129>> input { "input" };
    sc_core::sc_signal<sc_dt::sc_bv<129>> output { "output" };
    sc_core::sc_signal<sc_dt::sc_lv<257>> logic { "logic" };
    sc_core::sc_export<sc_core::sc_signal_in_if<sc_dt::sc_bv<129>>>
        exported_output { "exported_output" };
    tlm::tlm_fifo<std::uint32_t> fifo { "fifo", 4 };
    tlm_utils::simple_initiator_socket<CorpusRoot> initiator { "initiator" };
    CorpusTarget target { "target" };
    CorpusForwarder forwarder { "forwarder" };

    std::size_t produced { };
    std::size_t consumed { };
    std::size_t observed { };
    std::size_t backpressure { };
    std::uint64_t stack_checksum { };
    bool observation_enabled { };

    SC_CTOR(CorpusRoot)
    {
        initiator.bind(target.socket);
        forwarder.input(input);
        forwarder.output(output);
        exported_output.bind(output);
        SC_THREAD(produce);
        SC_THREAD(consume);
        SC_METHOD(observe);
        sensitive << output;
        dont_initialize();
    }

    ~CorpusRoot() override
    {
        ++destroyed_roots;
    }

private:
    void transact(const std::size_t sample)
    {
        std::uint32_t data = static_cast<std::uint32_t>(sample);
        tlm::tlm_generic_payload payload;
        payload.set_address(sample % 64U);
        payload.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
        payload.set_data_length(sizeof(data));
        payload.set_streaming_width(sizeof(data));
        payload.set_command(tlm::TLM_WRITE_COMMAND);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        initiator->b_transport(payload, delay);
        assert(payload.is_response_ok());
        wait(delay);

        data = 0U;
        payload.set_command(tlm::TLM_READ_COMMAND);
        payload.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        delay = sc_core::SC_ZERO_TIME;
        initiator->b_transport(payload, delay);
        assert(payload.is_response_ok());
        wait(delay);
        assert(data == static_cast<std::uint32_t>(sample));
    }

    void produce()
    {
        std::array<std::byte, kStackProbeBytes> stack_probe { };
        for (std::size_t index = 0U; index < stack_probe.size(); ++index) {
            stack_probe[index] = std::byte { static_cast<unsigned char>(
                (index * 17U + 3U) & 0xffU) };
            stack_checksum += std::to_integer<unsigned char>(stack_probe[index]);
        }
        for (std::size_t sample = 0U; sample < kSamples; ++sample) {
            if (sample == 128U) {
                observation_enabled = true;
            }
            input.write(bit_value(sample));
            logic.write(logic_value(sample));
            if (!fifo.nb_can_put()) {
                ++backpressure;
            }
            fifo.put(static_cast<std::uint32_t>(sample));
            transact(sample);
            ++produced;
        }
    }

    void consume()
    {
        wait(sc_core::sc_time { 10, sc_core::SC_PS });
        for (std::size_t sample = 0U; sample < kSamples; ++sample) {
            const auto value = fifo.get();
            assert(value == static_cast<std::uint32_t>(sample));
            ++consumed;
            wait(sc_core::sc_time { 3, sc_core::SC_PS });
        }
        ++completed_roots;
        if (completed_roots == kRoots) {
            sc_core::sc_stop();
        }
    }

    void observe()
    {
        if (observation_enabled) {
            ++observed;
        }
    }
};

}  // namespace

extern "C" FSIM_SYSTEMC_CORPUS_EXPORT int sc_main(int, char*[])
{
    const auto started = std::chrono::steady_clock::now();
    const auto* const runtime_context = fsim_systemc_accellera_context();
    assert(runtime_context != nullptr);
    assert(runtime_context == sc_core::sc_get_curr_simcontext());
    assert(std::strstr(fsim_systemc_accellera_version(), "3.0.2-Accellera")
        != nullptr);
    assert(sizeof(CorpusRoot) <= kMaximumRootBytes);

    std::size_t events = 0U;
    std::size_t crossings = 0U;
    std::size_t tlm1_transactions = 0U;
    std::size_t tlm2_transactions = 0U;
    std::size_t pressure_events = 0U;
    std::uint64_t stack_checksum = 0U;
    {
        CorpusRoot left { "left" };
        CorpusRoot right { "right" };
        assert(sc_core::sc_get_top_level_objects().size() == kRoots);
        sc_core::sc_start();
        assert(sc_core::sc_get_status() == sc_core::SC_STOPPED);
        assert(sc_core::sc_time_stamp() > sc_core::SC_ZERO_TIME);
        assert(left.produced == kSamples && right.produced == kSamples);
        assert(left.consumed == kSamples && right.consumed == kSamples);
        assert(left.observed > 0U && right.observed > 0U);
        assert(left.backpressure > 0U && right.backpressure > 0U);
        assert(left.target.transactions == kSamples * 2U);
        assert(right.target.transactions == kSamples * 2U);
        assert(left.exported_output->read() == left.output.read());
        assert(right.exported_output->read() == right.output.read());
        assert(left.stack_checksum != 0U && right.stack_checksum != 0U);
        events = left.observed + right.observed;
        crossings = (left.produced + right.produced) * 2U;
        tlm1_transactions = left.consumed + right.consumed;
        tlm2_transactions
            = left.target.transactions + right.target.transactions;
        pressure_events = left.backpressure + right.backpressure;
        stack_checksum = left.stack_checksum + right.stack_checksum;
        assert(runtime_context == fsim_systemc_accellera_context());
    }
    assert(destroyed_roots == kRoots);
    const auto elapsed = std::max<std::int64_t>(1,
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    assert(events > 0U && crossings > 0U && tlm1_transactions > 0U
        && tlm2_transactions > 0U && pressure_events > 0U);
    std::cout << "FSIM SystemC corpus PASS"
              << " roots=" << kRoots
              << " samples=" << kSamples
              << " time=" << sc_core::sc_time_stamp()
              << " events=" << events
              << " crossings=" << crossings
              << " crossings_per_second="
              << crossings * 1'000'000U
                    / static_cast<std::size_t>(elapsed)
              << " tlm1=" << tlm1_transactions
              << " tlm2=" << tlm2_transactions
              << " stack_bytes=" << kStackProbeBytes * kRoots
              << " stack_checksum=" << stack_checksum
              << " object_bytes=" << sizeof(CorpusRoot) * kRoots
              << " backpressure=" << pressure_events << '\n';
    return 0;
}
