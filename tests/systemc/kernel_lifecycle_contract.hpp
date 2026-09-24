// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::systemc_lifecycle {

enum class Outcome { success, rejected, failed };
enum class Phase { vacant, constructing, elaborated, started, terminal, failed };
enum class Failure { none, state, payload, resource, upstream };
enum class Counter {
    live_marker,
    constructed_roots,
    destroyed_roots,
    before_elaboration,
    end_elaboration,
    start,
    initial_evaluation,
    end,
    observed_width,
};

struct Snapshot {
    Phase phase { Phase::vacant };
    bool published { };
    std::size_t staged_objects { };
    std::size_t staged_bindings { };
    std::size_t published_objects { };
    std::uint64_t generation { };
};

struct Result {
    Outcome outcome { Outcome::success };
    Failure failure { Failure::none };
    Snapshot snapshot;
};

struct Session {
    std::string identity;
    std::filesystem::path plugin;
    std::uint64_t time_resolution_fs { 1U };
};

struct Object {
    std::string hierarchy_path;
    std::string object_path;
    std::string factory;
    std::string instance;
    std::vector<std::pair<std::string, std::int64_t>> parameters;
};

struct EndpointBinding {
    std::string endpoint_path;
    std::string interface_path;
};

class Driver {
public:
    virtual ~Driver() = default;
    [[nodiscard]] virtual Result create_session(const Session&) = 0;
    [[nodiscard]] virtual Result create_object(const Object&) = 0;
    [[nodiscard]] virtual Result bind_endpoint(const EndpointBinding&) = 0;
    [[nodiscard]] virtual Result elaborate() = 0;
    [[nodiscard]] virtual Result start() = 0;
    [[nodiscard]] virtual Result shutdown() = 0;
};

class Probe {
public:
    virtual ~Probe() = default;
    virtual void reset() = 0;
    [[nodiscard]] virtual std::size_t count(Counter) const = 0;
    [[nodiscard]] virtual std::size_t live_contexts() const = 0;
};

template<typename MakeDriver>
void run_contract(MakeDriver&& make_driver, Probe& probe,
    const std::filesystem::path& plugin)
{
    probe.reset();
    {
        auto driver = make_driver();
        const Session session { "lifecycle-contract", plugin, 1U };
        const auto created = driver->create_session(session);
        assert(created.outcome == Outcome::success);
        assert(created.snapshot.phase == Phase::constructing);
        assert(!created.snapshot.published);
        assert(created.snapshot.generation != 0U);

        const Object object {
            "contract-roots", "contract_top", "session_root", "contract_top",
            { { "WIDTH", 13 }, { "FAIL", 0 } }
        };
        const auto constructed = driver->create_object(object);
        assert(constructed.outcome == Outcome::success);
        assert(constructed.snapshot.staged_objects == 1U);
        assert(constructed.snapshot.published_objects == 0U);

        const auto bound = driver->bind_endpoint(
            { "contract_top.leaf.input", "contract_top.channel" });
        assert(bound.outcome == Outcome::success);
        assert(bound.snapshot.staged_bindings == 1U);

        const auto elaborated = driver->elaborate();
        assert(elaborated.outcome == Outcome::success);
        assert(elaborated.snapshot.phase == Phase::elaborated);
        assert(!elaborated.snapshot.published);

        const auto started = driver->start();
        assert(started.outcome == Outcome::success);
        assert(started.snapshot.phase == Phase::started);
        assert(started.snapshot.published);
        assert(started.snapshot.published_objects == 1U);
        assert(probe.live_contexts() == 1U);
        assert(probe.count(Counter::live_marker) == 1U);
        assert(probe.count(Counter::constructed_roots) == 1U);
        assert(probe.count(Counter::before_elaboration) == 1U);
        assert(probe.count(Counter::end_elaboration) == 1U);
        assert(probe.count(Counter::start) == 1U);
        assert(probe.count(Counter::initial_evaluation) == 1U);
        assert(probe.count(Counter::observed_width) == 13U);

        const auto repeated_start = driver->start();
        assert(repeated_start.outcome == Outcome::rejected);
        assert(repeated_start.failure == Failure::state);
        assert(repeated_start.snapshot.phase == Phase::started);

        const auto stopped = driver->shutdown();
        assert(stopped.outcome == Outcome::success);
        assert(stopped.snapshot.phase == Phase::terminal);
        assert(!stopped.snapshot.published);
        assert(stopped.snapshot.staged_objects == 0U);
        assert(probe.live_contexts() == 0U);
        assert(probe.count(Counter::live_marker) == 0U);
        assert(probe.count(Counter::destroyed_roots) == 1U);
        assert(probe.count(Counter::end) == 1U);

        const auto ended = probe.count(Counter::end);
        const auto destroyed = probe.count(Counter::destroyed_roots);
        const auto repeated_shutdown = driver->shutdown();
        assert(repeated_shutdown.outcome == Outcome::success);
        assert(repeated_shutdown.snapshot.phase == Phase::terminal);
        assert(probe.count(Counter::end) == ended);
        assert(probe.count(Counter::destroyed_roots) == destroyed);
    }
    assert(probe.live_contexts() == 0U);

    probe.reset();
    {
        auto driver = make_driver();
        const auto created = driver->create_session(
            { "construction-rollback", plugin, 1U });
        assert(created.outcome == Outcome::success);
        const auto failed = driver->create_object({
            "failure-roots", "failing_top", "session_root", "failing_top",
            { { "WIDTH", 9 }, { "FAIL", 1 } }
        });
        assert(failed.outcome == Outcome::failed);
        assert(failed.failure == Failure::upstream);
        assert(failed.snapshot.phase == Phase::failed);
        assert(failed.snapshot.staged_objects == 0U);
        assert(!failed.snapshot.published);
        assert(probe.live_contexts() == 0U);
        assert(probe.count(Counter::live_marker) == 0U);
        assert(probe.count(Counter::constructed_roots) == 0U);
    }
    assert(probe.live_contexts() == 0U);

    probe.reset();
    {
        auto driver = make_driver();
        auto missing = plugin;
        missing += ".missing";
        const auto failed = driver->create_session(
            { "plugin-load-failure", missing, 1U });
        assert(failed.outcome == Outcome::failed);
        assert(failed.failure == Failure::upstream);
        assert(failed.snapshot.phase == Phase::vacant);
        assert(probe.live_contexts() == 0U);
    }
}

} // namespace fsim::tests::systemc_lifecycle
