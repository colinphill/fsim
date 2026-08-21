// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace fsim::app::application_detail {

struct ContainerCallbackProfile {
    bool enabled = std::getenv("FSIM_PROFILE_CONTAINERS") != nullptr;
    std::uint64_t read_words { };
    std::uint64_t write_words { };
    std::uint64_t read_packed_elements { };
    std::uint64_t write_packed_elements { };
    std::uint64_t generic { };
    std::uint64_t copy_registers { };
    std::uint64_t read_objects { };
    std::uint64_t write_objects { };

    ~ContainerCallbackProfile()
    {
        if (enabled) {
            std::cerr << "fsim-profile: container read_words=" << read_words
                      << " write_words=" << write_words
                      << " read_packed_elements=" << read_packed_elements
                      << " write_packed_elements=" << write_packed_elements
                      << " generic=" << generic
                      << " copy_registers=" << copy_registers
                      << " read_objects=" << read_objects
                      << " write_objects=" << write_objects << '\n';
        }
    }
};

inline ContainerCallbackProfile& container_callback_profile()
{
    static ContainerCallbackProfile result;
    return result;
}

} // namespace fsim::app::application_detail
