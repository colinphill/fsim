// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace fsim::compiler::llvm_detail {

/// Weak single-flight ownership for immutable compiled bodies. The registry
/// does not extend a body's lifetime; active executor instances own it.
template<typename Body>
class WeakSingleFlightRegistry final {
private:
    struct Flight final {
        std::condition_variable changed;
        std::condition_variable waiters_changed;
        std::shared_ptr<Body> body;
        std::exception_ptr error;
        std::size_t waiters { };
        bool complete { };
    };

public:
    template<typename Factory>
    [[nodiscard]] std::shared_ptr<Body> get_or_create(
        std::string key, Factory&& factory)
    {
        std::shared_ptr<Flight> flight;
        {
            std::unique_lock lock { mutex_ };
            if (const auto existing = bodies_.find(key);
                existing != bodies_.end()) {
                if (auto body = existing->second.lock()) {
                    return body;
                }
            }
            if (const auto existing = flights_.find(key);
                existing != flights_.end()) {
                flight = existing->second;
                ++flight->waiters;
                flight->waiters_changed.notify_all();
                flight->changed.wait(lock,
                    [&flight] { return flight->complete; });
                if (flight->error) {
                    std::rethrow_exception(flight->error);
                }
                return flight->body;
            }
            flight = std::make_shared<Flight>();
            flights_.emplace(key, flight);
        }

        std::shared_ptr<Body> body;
        std::exception_ptr error;
        try {
            body = std::forward<Factory>(factory)();
        } catch (...) {
            error = std::current_exception();
        }

        {
            const std::lock_guard lock { mutex_ };
            if (!error) {
                try {
                    if (body != nullptr) {
                        bodies_.insert_or_assign(key, body);
                    }
                } catch (...) {
                    error = std::current_exception();
                }
            }
            flight->body = error == nullptr ? body : nullptr;
            flight->error = error;
            flight->complete = true;
            flights_.erase(key);
            if (bodies_.size() > maximum_weak_entries_) {
                for (auto entry = bodies_.begin(); entry != bodies_.end();) {
                    if (entry->second.expired()) {
                        entry = bodies_.erase(entry);
                    } else {
                        ++entry;
                    }
                }
            }
        }
        flight->changed.notify_all();
        if (error) {
            std::rethrow_exception(error);
        }
        return body;
    }

    [[nodiscard]] bool wait_for_test_waiters(
        const std::string& key, const std::size_t count,
        const std::chrono::milliseconds timeout)
    {
        std::unique_lock lock { mutex_ };
        const auto found = flights_.find(key);
        if (found == flights_.end()) {
            return false;
        }
        const auto flight = found->second;
        return flight->waiters_changed.wait_for(lock, timeout, [&] {
            return flight->waiters >= count || flight->complete;
        }) && flight->waiters >= count;
    }

private:
    static constexpr std::size_t maximum_weak_entries_ { 128U };
    std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<Body>> bodies_;
    std::unordered_map<std::string, std::shared_ptr<Flight>> flights_;
};

} // namespace fsim::compiler::llvm_detail
