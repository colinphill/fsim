// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/accellera.hpp"

#include <cstdint>
#include <stdexcept>

namespace {

class ExecutionRoot final : public sc_core::sc_module {
public:
    fsim::systemc::backend_input<std::uint32_t> input { "input" };
    fsim::systemc::backend_output<std::uint32_t> output { "output" };

    explicit ExecutionRoot(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        SC_METHOD(transform);
        sensitive << input;
        SC_METHOD(timed_activity);
    }

private:
    sc_core::sc_signal<std::uint32_t> input_channel_ { "input_channel" };
    sc_core::sc_signal<std::uint32_t> output_channel_ { "output_channel" };

    void transform()
    {
        const auto value = input->read();
        if (value == 101U) {
            throw std::runtime_error { "requested execution failure" };
        }
        output->write(value * 3U + 1U);
        if (value == 99U) {
            sc_core::sc_pause();
        } else if (value == 100U) {
            sc_core::sc_stop();
        }
    }

    void timed_activity()
    {
        next_trigger(sc_core::sc_time { 5.0, sc_core::SC_NS });
    }
};

SC_FSIM_EXPORT_AS(ExecutionRoot, "execution_root");

} // namespace
