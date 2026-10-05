// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>

namespace fsim::app::allocation_profile {

struct Snapshot {
    std::uint64_t requests { };
    std::uint64_t requested_bytes { };
    std::uint64_t failures { };
};

[[nodiscard]] bool exchange_enabled(bool enabled) noexcept;
[[nodiscard]] Snapshot snapshot() noexcept;
[[nodiscard]] bool record_request(std::size_t bytes) noexcept;
void record_failure(bool counted) noexcept;

class Session {
public:
    explicit Session(bool enabled) noexcept;
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void after_setup() noexcept;
    void after_prepare() noexcept;
    void after_run() noexcept;
    void finish(std::ostream& output);

private:
    void restore() noexcept;

    bool active_ { };
    bool previous_enabled_ { };
    std::array<Snapshot, 5U> checkpoints_ { };
};

} // namespace fsim::app::allocation_profile
