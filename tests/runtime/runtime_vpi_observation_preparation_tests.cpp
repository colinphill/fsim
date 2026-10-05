// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_callback.hpp"

#include <new>
#include <stdexcept>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_vpi_observation_preparation()
{
    SystemVerilogVpiObjectRegistry registry { 905U };
    Scheduler scheduler;
    SystemVerilogVpiTimeService time_service {
        scheduler, SystemVerilogVpiTimeProfile { -9, -12 }
    };
    const auto root = registry.create(SystemVerilogVpiObjectKind::Root, 0, "top");
    require(static_cast<bool>(root), "observation fixture root creation failed");
    const auto signal = registry.create(
        SystemVerilogVpiObjectKind::Variable, root.value, "signal");
    require(static_cast<bool>(signal), "observation fixture signal creation failed");

    SystemVerilogVpiCallbackManager* manager_pointer { };
    std::size_t preparations { };
    std::size_t callbacks { };
    unsigned failure { 1U };
    bool prepared { };
    bool saw_global { };
    SystemVerilogVpiCallbackManager manager {
        registry, scheduler, time_service, 60'000U,
        [&](SystemVerilogVpiCallbackKind kind,
            std::optional<fsim_vpi_handle_v1> object) {
            ++preparations;
            // This locks the manager: preparation must run outside that lock.
            require(manager_pointer->registrations() == (prepared ? 1U : 0U),
                "registration became visible before observation preparation");
            require(manager_pointer->has_registrations() == prepared,
                "activity became visible before observation preparation");
            if (object) {
                require(*object == signal.value
                        && kind == SystemVerilogVpiCallbackKind::ValueChange,
                    "preparation must receive the exact observed object");
            } else {
                saw_global = true;
            }
            if (failure == 1U) {
                throw std::bad_alloc { };
            }
            if (failure == 2U) {
                throw std::runtime_error("materialization failed");
            }
            prepared = true;
        }
    };
    manager_pointer = &manager;
    const auto callback = [&](const SystemVerilogVpiCallbackEvent&) {
        ++callbacks;
    };
    auto registration = SystemVerilogVpiCallbackRegistration {
        SystemVerilogVpiCallbackKind::ValueChange, signal.value,
        std::nullopt, 0U, callback
    };
    auto invalid = registration;
    invalid.callback = { };
    require(!manager.register_callback(invalid) && preparations == 0U,
        "invalid registrations must not prepare observation");
    const auto allocation_failure = manager.register_callback(registration);
    require(!allocation_failure
            && allocation_failure.error == SystemVerilogVpiCallbackError::ResourceLimit
            && manager.registrations() == 0U && !manager.has_registrations(),
        "allocation failure must leave no visible registration");
    failure = 2U;
    const auto preparation_failure = manager.register_callback(registration);
    require(!preparation_failure
            && preparation_failure.error
                == SystemVerilogVpiCallbackError::ObservationPreparationFailure
            && manager.registrations() == 0U && !manager.has_registrations(),
        "host preparation failure must leave no visible registration");
    failure = 0U;
    const auto registered = manager.register_callback(registration);
    require(registered && registered.value.id == 1U && prepared
            && manager.registrations() == 1U && manager.has_registrations(),
        "successful retry must prepare before publishing the first callback");

    failure = 2U;
    const auto failed_lifecycle = manager.register_callback({
        SystemVerilogVpiCallbackKind::StartOfSimulation, std::nullopt,
        std::nullopt, 0U, callback });
    require(!failed_lifecycle && saw_global && preparations == 4U
            && manager.registrations() == 1U,
        "global registration failure must retain existing registrations");
    require(manager.dispatch_lifecycle_now(SystemVerilogVpiCallbackKind::StartOfSimulation)
                == SystemVerilogVpiCallbackError::None
            && scheduler.run().status == RunStatus::completed && callbacks == 0U,
        "failed preparation must leave no callback to dispatch");
}

} // namespace fsim::tests::runtime
