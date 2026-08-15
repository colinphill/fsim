// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_value_endpoint.hpp"

namespace {

class ValueRoot final : public sc_core::sc_module {
public:
    fsim::systemc::backend_value_input<sc_dt::sc_lv<257>> input { "input" };
    fsim::systemc::backend_value_output<sc_dt::sc_lv<257>> output { "output" };

    explicit ValueRoot(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        SC_METHOD(mirror);
        sensitive << input;
    }

private:
    sc_core::sc_signal<sc_dt::sc_lv<257>> input_channel_ { "input_channel" };
    sc_core::sc_signal<sc_dt::sc_lv<257>> output_channel_ { "output_channel" };

    void mirror() { output->write(input->read()); }
};

SC_FSIM_EXPORT_AS(ValueRoot, "value_root");

class BitValueRoot final : public sc_core::sc_module {
public:
    fsim::systemc::backend_value_input<sc_dt::sc_bv<129>> input { "input" };
    fsim::systemc::backend_value_output<sc_dt::sc_bv<129>> output { "output" };

    explicit BitValueRoot(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        SC_METHOD(mirror);
        sensitive << input;
    }

private:
    sc_core::sc_signal<sc_dt::sc_bv<129>> input_channel_ { "input_channel" };
    sc_core::sc_signal<sc_dt::sc_bv<129>> output_channel_ { "output_channel" };

    void mirror() { output->write(input->read()); }
};

SC_FSIM_EXPORT_AS(BitValueRoot, "bit_value_root");

} // namespace
