// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/accellera.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

std::atomic_size_t live_markers { 0U };
std::atomic_size_t constructed_roots { 0U };
std::atomic_size_t destroyed_roots { 0U };
std::atomic_size_t before_elaboration_calls { 0U };
std::atomic_size_t end_elaboration_calls { 0U };
std::atomic_size_t start_calls { 0U };
std::atomic_size_t initial_evaluations { 0U };
std::atomic_size_t end_calls { 0U };
std::atomic_uint32_t observed_width { 0U };

struct LiveMarker final {
    LiveMarker() { live_markers.fetch_add(1U); }
    ~LiveMarker() { live_markers.fetch_sub(1U); }

    LiveMarker(const LiveMarker&) = delete;
    LiveMarker& operator=(const LiveMarker&) = delete;
};

class SessionLeaf final : public sc_core::sc_module {
public:
    fsim::systemc::backend_input<std::uint32_t> input { "input" };

    explicit SessionLeaf(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
    }
};

class SessionRoot final : public sc_core::sc_module {
public:
    inline static constexpr auto fsim_factory_parameters = fsim::systemc::make_factory_parameters(
        fsim::systemc::factory_parameter { "WIDTH",
            FSIM_SC_CONSTRUCTION_POSITIVE, true, 8 },
        fsim::systemc::factory_parameter { "FAIL",
            FSIM_SC_CONSTRUCTION_BOOLEAN, true, 0 });

    explicit SessionRoot(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
        , width_ { fsim::systemc::construction_value<std::uint32_t>(
              "WIDTH") }
        , fail_ { fsim::systemc::construction_value<bool>("FAIL") }
    {
        if (fail_) {
            throw std::runtime_error { "requested session factory failure" };
        }
        SC_METHOD(evaluate);
        constructed_roots.fetch_add(1U);
        observed_width.store(width_);
    }

    ~SessionRoot() override
    {
        destroyed_roots.fetch_add(1U);
    }

private:
    void before_end_of_elaboration() override
    {
        before_elaboration_calls.fetch_add(1U);
    }

    void end_of_elaboration() override
    {
        end_elaboration_calls.fetch_add(1U);
    }

    void start_of_simulation() override
    {
        start_calls.fetch_add(1U);
    }

    void end_of_simulation() override
    {
        end_calls.fetch_add(1U);
    }

    void evaluate()
    {
        observed_width.store(width_ + leaf_.input->read());
        initial_evaluations.fetch_add(1U);
    }

    LiveMarker marker_;
    sc_core::sc_signal<std::uint32_t> channel_ { "channel" };
    SessionLeaf leaf_ { "leaf" };
    std::uint32_t width_ { };
    bool fail_ { };
};

SC_FSIM_EXPORT_AS(SessionRoot, "session_root");

} // namespace

extern "C" FSIM_SC_EXPORT void fsim_test_session_reset_v1() noexcept
{
    constructed_roots.store(0U);
    destroyed_roots.store(0U);
    before_elaboration_calls.store(0U);
    end_elaboration_calls.store(0U);
    start_calls.store(0U);
    initial_evaluations.store(0U);
    end_calls.store(0U);
    observed_width.store(0U);
}

extern "C" FSIM_SC_EXPORT std::size_t
fsim_test_session_counter_v1(const std::uint32_t counter) noexcept
{
    switch (counter) {
    case 0U:
        return live_markers.load();
    case 1U:
        return constructed_roots.load();
    case 2U:
        return destroyed_roots.load();
    case 3U:
        return before_elaboration_calls.load();
    case 4U:
        return end_elaboration_calls.load();
    case 5U:
        return start_calls.load();
    case 6U:
        return initial_evaluations.load();
    case 7U:
        return end_calls.load();
    case 8U:
        return observed_width.load();
    default:
        return 0U;
    }
}
