// SPDX-License-Identifier: Apache-2.0
#include "application_foreign_signal.hpp"

#include <array>
#include <atomic>
#include <csignal>
#include <mutex>

namespace fsim::app::application_detail {

namespace {

constexpr int signal_limit = 65;

using Handler = void (*)(int);

// Arrivals per signal; the handler only increments its counter, which is
// lock-free and safe to touch from a signal handler.
std::array<std::atomic<std::uint64_t>, signal_limit> arrivals {};

struct Installed {
    unsigned users { };
    Handler previous { };
};

std::mutex installed_mutex;
std::array<Installed, signal_limit> installed {};

extern "C" void record_signal(const int signal)
{
    if (signal > 0 && signal < signal_limit) {
        arrivals[static_cast<std::size_t>(signal)].fetch_add(
            1U, std::memory_order_relaxed);
    }
}

[[nodiscard]] bool trappable(const int signal) noexcept
{
    if (signal <= 0 || signal >= signal_limit) {
        return false;
    }
#if defined(SIGKILL)
    if (signal == SIGKILL) {
        return false;
    }
#endif
#if defined(SIGSTOP)
    if (signal == SIGSTOP) {
        return false;
    }
#endif
    return true;
}

} // namespace

ForeignSignalTrap::ForeignSignalTrap(const int signal) noexcept
    : signal_ { signal }
{
    if (!trappable(signal)) {
        return;
    }
    const auto index = static_cast<std::size_t>(signal);
    const std::scoped_lock lock { installed_mutex };
    auto& entry = installed[index];
    if (entry.users == 0U) {
        const auto previous = std::signal(signal, record_signal);
        if (previous == SIG_ERR) {
            return;
        }
        entry.previous = previous;
    }
    ++entry.users;
    seen_ = arrivals[index].load(std::memory_order_relaxed);
    valid_ = true;
}

ForeignSignalTrap::~ForeignSignalTrap()
{
    if (!valid_) {
        return;
    }
    const auto index = static_cast<std::size_t>(signal_);
    const std::scoped_lock lock { installed_mutex };
    auto& entry = installed[index];
    if (--entry.users != 0U) {
        return;
    }
    // Restore the earlier handling unless someone else has replaced the
    // trap since.
    const auto current = std::signal(signal_, entry.previous);
    if (current != record_signal && current != SIG_ERR) {
        (void)std::signal(signal_, current);
    }
}

bool ForeignSignalTrap::take() noexcept
{
    if (!valid_) {
        return false;
    }
    const auto now = arrivals[static_cast<std::size_t>(signal_)].load(
        std::memory_order_relaxed);
    if (now == seen_) {
        return false;
    }
    seen_ = now;
    return true;
}

} // namespace fsim::app::application_detail
