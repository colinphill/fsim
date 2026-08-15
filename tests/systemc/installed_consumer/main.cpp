// SPDX-License-Identifier: Apache-2.0

#include <systemc>
#include <tlm>

#include <cstring>

int sc_main(int, char*[])
{
    tlm::tlm_generic_payload payload;
    payload.set_command(tlm::TLM_READ_COMMAND);
    payload.set_response_status(tlm::TLM_OK_RESPONSE);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    return payload.is_read() && payload.is_response_ok()
            && std::strstr(sc_core::sc_version(), "3.0.2-Accellera")
        ? 0
        : 1;
}
