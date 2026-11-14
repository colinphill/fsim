// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/foreign_call_process.hpp"

#include <atomic>

namespace fsim::runtime {
namespace {

thread_local std::uint64_t dpi_call_process {};
// No call has a position until the kernel names one (a disable covers it).
thread_local std::uint32_t dpi_call_position {
    DpiCallProcess::unknown_position
};
std::atomic<DpiCallProcess::Notice> dpi_disable_notice {};
std::atomic<void*> dpi_disable_user_data {};

} // namespace

void DpiCallProcess::set_current(const std::uint64_t process) noexcept
{
    dpi_call_process = process;
}

std::uint64_t DpiCallProcess::current() noexcept
{
    return dpi_call_process;
}

void DpiCallProcess::set_position(const std::uint32_t position) noexcept
{
    dpi_call_position = position;
}

std::uint32_t DpiCallProcess::position() noexcept
{
    return dpi_call_position;
}

void DpiCallProcess::install(const Notice notice, void* const user_data) noexcept
{
    dpi_disable_user_data.store(user_data, std::memory_order_release);
    dpi_disable_notice.store(notice, std::memory_order_release);
}

void DpiCallProcess::disabled(const std::uint64_t process)
{
    disabled(process, 0U, UINT32_MAX);
}

void DpiCallProcess::disabled(const std::uint64_t process,
    const std::uint32_t begin, const std::uint32_t end)
{
    const auto notice = dpi_disable_notice.load(std::memory_order_acquire);
    if (notice != nullptr) {
        notice(dpi_disable_user_data.load(std::memory_order_acquire), process,
            begin, end);
    }
}

} // namespace fsim::runtime
