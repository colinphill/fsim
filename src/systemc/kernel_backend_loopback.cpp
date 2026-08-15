// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_loopback.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::systemc {
namespace {

    std::atomic_size_t gLiveLoopbackTransports { 0U };

    bool report_resource_error(
        diagnostic::Engine& diagnostics, const std::string_view message)
    {
        diagnostics.error("FSIM-SC-L003", std::string { message });
        return false;
    }

    bool valid_limits(const SystemCKernelProtocolLimits& protocol,
        const SystemCKernelLoopbackLimits& loopback,
        diagnostic::Engine& diagnostics)
    {
        if (loopback.max_replay_entries == 0U
            || loopback.max_buffer_bytes < kSystemCKernelMessageHeaderBytes
            || loopback.max_buffer_bytes > protocol.max_message_bytes
            || loopback.max_forwarded_exchanges == 0U) {
            return report_resource_error(diagnostics,
                "SystemC loopback limits are zero or exceed the protocol envelope");
        }
        return true;
    }

    struct ReplayKey {
        SystemCIslandId island;
        SystemCSequenceId sequence;

        friend auto operator<=>(const ReplayKey&, const ReplayKey&) = default;
    };

    struct ReplayEntry {
        std::vector<std::byte> request;
        SystemCKernelTransportResult response;
    };

    class LoopbackBackend final : public SystemCKernelLoopbackBackend {
    public:
        LoopbackBackend(std::unique_ptr<SystemCKernelBackend> peer,
            SystemCKernelProtocolLimits protocol_limits,
            SystemCKernelLoopbackLimits loopback_limits)
            : peer_ { std::move(peer) }
            , protocol_limits_ { protocol_limits }
            , loopback_limits_ { loopback_limits }
        {
            gLiveLoopbackTransports.fetch_add(1U, std::memory_order_relaxed);
        }

        ~LoopbackBackend() override
        {
            close();
            gLiveLoopbackTransports.fetch_sub(1U, std::memory_order_relaxed);
        }

        [[nodiscard]] SystemCKernelTransportResult exchange(
            const std::span<const std::byte> request_bytes) noexcept override
        {
            std::lock_guard lock { mutex_ };
            try {
                return exchange_locked(request_bytes);
            } catch (...) {
                return contain_peer_failure(SystemCKernelLoopbackCode::peer);
            }
        }

        void close() noexcept override
        {
            std::lock_guard lock { mutex_ };
            if (closed_) {
                return;
            }
            closed_ = true;
            if (peer_) {
                peer_->close();
                peer_.reset();
            }
            replay_.clear();
            replay_order_.clear();
            last_sequences_.clear();
        }

        [[nodiscard]] SystemCKernelLoopbackStats stats() const override
        {
            std::lock_guard lock { mutex_ };
            auto result = stats_;
            result.cached = replay_.size();
            return result;
        }

        [[nodiscard]] SystemCKernelLoopbackCode last_code()
            const noexcept override
        {
            return last_code_.load(std::memory_order_relaxed);
        }

    private:
        [[nodiscard]] SystemCKernelTransportResult reject(
            const SystemCKernelLoopbackCode code)
        {
            last_code_.store(code, std::memory_order_relaxed);
            ++stats_.rejected;
            return { SystemCKernelTransportStatus::rejected, { } };
        }

        [[nodiscard]] SystemCKernelTransportResult contain_peer_failure(
            const SystemCKernelLoopbackCode code)
        {
            last_code_.store(code, std::memory_order_relaxed);
            ++stats_.disconnected;
            closed_ = true;
            if (peer_) {
                peer_->close();
                peer_.reset();
            }
            replay_.clear();
            replay_order_.clear();
            last_sequences_.clear();
            return { SystemCKernelTransportStatus::disconnected, { } };
        }

        [[nodiscard]] bool replayable(
            const SystemCKernelMessageHeader& header) const noexcept
        {
            return (header.flags
                       & static_cast<std::uint32_t>(
                           SystemCKernelMessageFlag::replayable))
                != 0U;
        }

        [[nodiscard]] std::optional<SystemCKernelTransportResult>
        replay_or_reject(const SystemCKernelMessage& request,
            const std::span<const std::byte> request_bytes)
        {
            const ReplayKey key { request.header.island,
                request.header.sequence };
            const auto entry = replay_.find(key);
            if (entry != replay_.end() && replayable(request.header)
                && std::ranges::equal(entry->second.request, request_bytes)) {
                last_code_.store(SystemCKernelLoopbackCode::none,
                    std::memory_order_relaxed);
                ++stats_.replayed;
                return entry->second.response;
            }
            return reject(SystemCKernelLoopbackCode::order);
        }

        [[nodiscard]] std::optional<SystemCKernelTransportResult>
        validate_order(const SystemCKernelMessage& request,
            const std::span<const std::byte> request_bytes)
        {
            const auto previous = last_sequences_.find(request.header.island);
            if (previous == last_sequences_.end()) {
                return std::nullopt;
            }
            if (request.header.sequence == previous->second) {
                return replay_or_reject(request, request_bytes);
            }
            if (request.header.sequence.value < previous->second.value
                || previous->second.value
                    == std::numeric_limits<std::uint64_t>::max()
                || request.header.sequence.value != previous->second.value + 1U) {
                return reject(SystemCKernelLoopbackCode::order);
            }
            return std::nullopt;
        }

        [[nodiscard]] bool valid_response(const SystemCKernelMessage& request,
            const SystemCKernelMessage& response) const noexcept
        {
            return response.header.direction
                == SystemCKernelMessageDirection::response
                && response.header.operation == request.header.operation
                && response.header.correlation == request.header.sequence
                && response.header.island == request.header.island
                && response.header.hierarchy == request.header.hierarchy
                && response.header.object == request.header.object
                && response.header.endpoint == request.header.endpoint
                && response.header.transaction == request.header.transaction;
        }

        void cache_response(const SystemCKernelMessage& request,
            std::vector<std::byte> request_bytes,
            const SystemCKernelTransportResult& response)
        {
            if (!replayable(request.header)) {
                return;
            }
            if (replay_.size() == loopback_limits_.max_replay_entries) {
                replay_.erase(replay_order_.front());
                replay_order_.pop_front();
                ++stats_.evicted;
            }
            const ReplayKey key { request.header.island,
                request.header.sequence };
            replay_order_.push_back(key);
            replay_.emplace(key,
                ReplayEntry { std::move(request_bytes), response });
        }

        [[nodiscard]] SystemCKernelTransportResult exchange_locked(
            const std::span<const std::byte> request_bytes)
        {
            if (closed_ || peer_ == nullptr) {
                return { SystemCKernelTransportStatus::disconnected, { } };
            }
            if (request_bytes.size() > loopback_limits_.max_buffer_bytes) {
                return reject(SystemCKernelLoopbackCode::resource);
            }
            diagnostic::Engine diagnostics;
            const auto request = deserialize_systemc_kernel_message(
                request_bytes, protocol_limits_, diagnostics);
            if (!request || request->header.direction != SystemCKernelMessageDirection::request) {
                return reject(SystemCKernelLoopbackCode::order);
            }
            if (auto ordered = validate_order(*request, request_bytes)) {
                return std::move(*ordered);
            }
            if (stats_.forwarded >= loopback_limits_.max_forwarded_exchanges) {
                return reject(SystemCKernelLoopbackCode::resource);
            }

            std::vector<std::byte> ingress(
                request_bytes.begin(), request_bytes.end());
            auto peer_result = peer_->exchange(ingress);
            ++stats_.forwarded;
            if (peer_result.status == SystemCKernelTransportStatus::disconnected) {
                return contain_peer_failure(SystemCKernelLoopbackCode::peer);
            }
            if (peer_result.status != SystemCKernelTransportStatus::ok
                || peer_result.bytes.size() > loopback_limits_.max_buffer_bytes) {
                return contain_peer_failure(SystemCKernelLoopbackCode::response);
            }
            diagnostics = { };
            const auto response = deserialize_systemc_kernel_message(
                peer_result.bytes, protocol_limits_, diagnostics);
            if (!response || !valid_response(*request, *response)) {
                return contain_peer_failure(SystemCKernelLoopbackCode::response);
            }
            last_sequences_.insert_or_assign(
                request->header.island, request->header.sequence);
            cache_response(*request, std::move(ingress), peer_result);
            last_code_.store(SystemCKernelLoopbackCode::none,
                std::memory_order_relaxed);
            return peer_result;
        }

        std::unique_ptr<SystemCKernelBackend> peer_;
        SystemCKernelProtocolLimits protocol_limits_;
        SystemCKernelLoopbackLimits loopback_limits_;
        mutable std::mutex mutex_;
        bool closed_ { };
        SystemCKernelLoopbackStats stats_;
        std::atomic<SystemCKernelLoopbackCode> last_code_ {
            SystemCKernelLoopbackCode::none
        };
        std::map<SystemCIslandId, SystemCSequenceId> last_sequences_;
        std::map<ReplayKey, ReplayEntry> replay_;
        std::deque<ReplayKey> replay_order_;
    };

} // namespace

std::unique_ptr<SystemCKernelLoopbackBackend>
make_systemc_kernel_loopback_backend(
    std::unique_ptr<SystemCKernelBackend> peer,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelLoopbackLimits& loopback_limits,
    diagnostic::Engine& diagnostics)
{
    if (!peer || !valid_limits(protocol_limits, loopback_limits, diagnostics)) {
        if (!peer) {
            diagnostics.error("FSIM-SC-L004",
                "SystemC loopback requires a live serialized peer backend");
        }
        return nullptr;
    }
    return std::make_unique<LoopbackBackend>(std::move(peer),
        protocol_limits, loopback_limits);
}

std::unique_ptr<SystemCKernelLoopbackBackend>
make_systemc_kernel_loopback_session_backend(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const SystemCKernelLoopbackLimits& loopback_limits,
    diagnostic::Engine& diagnostics)
{
    auto peer = make_systemc_kernel_session_backend(
        protocol_limits, session_limits, execution_limits, diagnostics);
    if (!peer) {
        return nullptr;
    }
    return make_systemc_kernel_loopback_backend(std::move(peer),
        protocol_limits, loopback_limits, diagnostics);
}

const char* systemc_kernel_loopback_diagnostic_code(
    const SystemCKernelLoopbackCode code) noexcept
{
    switch (code) {
    case SystemCKernelLoopbackCode::none:
        return "";
    case SystemCKernelLoopbackCode::order:
        return "FSIM-SC-L001";
    case SystemCKernelLoopbackCode::response:
        return "FSIM-SC-L002";
    case SystemCKernelLoopbackCode::resource:
        return "FSIM-SC-L003";
    case SystemCKernelLoopbackCode::peer:
        return "FSIM-SC-L004";
    }
    return "FSIM-SC-L001";
}

std::size_t systemc_kernel_loopback_live_transports() noexcept
{
    return gLiveLoopbackTransports.load(std::memory_order_relaxed);
}

} // namespace fsim::systemc
