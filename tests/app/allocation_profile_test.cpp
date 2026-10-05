// SPDX-License-Identifier: Apache-2.0
#include "allocation_profile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>

namespace {

std::size_t new_handler_calls { };

void clear_new_handler()
{
    ++new_handler_calls;
    static_cast<void>(std::set_new_handler(nullptr));
}

void throw_new_handler()
{
    ++new_handler_calls;
    throw std::bad_alloc { };
}

struct ThrowingConstructor {
    ThrowingConstructor() { throw 1; }
};

struct alignas(128) AlignedThrowingConstructor {
    AlignedThrowingConstructor() { throw 2; }
};

[[nodiscard]] bool same_delta(
    const fsim::app::allocation_profile::Snapshot& before,
    const fsim::app::allocation_profile::Snapshot& after,
    const std::uint64_t requests,
    const std::uint64_t bytes,
    const std::uint64_t failures) noexcept
{
    return after.requests - before.requests == requests
        && after.requested_bytes - before.requested_bytes == bytes
        && after.failures - before.failures == failures;
}

} // namespace

int main()
{
    using fsim::app::allocation_profile::exchange_enabled;
    using fsim::app::allocation_profile::snapshot;

    static_cast<void>(exchange_enabled(false));
    const auto before = snapshot();
    static_cast<void>(exchange_enabled(true));

    auto* const scalar = ::operator new(11U);
    auto* const array = ::operator new[](12U);
    auto* const sized_scalar = ::operator new(13U);
    auto* const sized_array = ::operator new[](14U);
    auto* const aligned_scalar
        = ::operator new(15U, std::align_val_t { 64U });
    auto* const aligned_array
        = ::operator new[](16U, std::align_val_t { 64U });
    auto* const sized_aligned_scalar
        = ::operator new(17U, std::align_val_t { 64U });
    auto* const sized_aligned_array
        = ::operator new[](18U, std::align_val_t { 64U });
    auto* const nothrow_scalar = ::operator new(19U, std::nothrow);
    auto* const nothrow_array = ::operator new[](20U, std::nothrow);
    auto* const nothrow_aligned_scalar = ::operator new(
        21U, std::align_val_t { 64U }, std::nothrow);
    auto* const nothrow_aligned_array = ::operator new[](
        22U, std::align_val_t { 64U }, std::nothrow);

    const bool aligned =
        reinterpret_cast<std::uintptr_t>(aligned_scalar) % 64U == 0U
        && reinterpret_cast<std::uintptr_t>(aligned_array) % 64U == 0U
        && reinterpret_cast<std::uintptr_t>(sized_aligned_scalar) % 64U == 0U
        && reinterpret_cast<std::uintptr_t>(sized_aligned_array) % 64U == 0U
        && reinterpret_cast<std::uintptr_t>(nothrow_aligned_scalar) % 64U == 0U
        && reinterpret_cast<std::uintptr_t>(nothrow_aligned_array) % 64U == 0U;

    ::operator delete(scalar);
    ::operator delete[](array);
#if defined(__cpp_sized_deallocation)
    ::operator delete(sized_scalar, 13U);
    ::operator delete[](sized_array, 14U);
#else
    ::operator delete(sized_scalar);
    ::operator delete[](sized_array);
#endif
    ::operator delete(aligned_scalar, std::align_val_t { 64U });
    ::operator delete[](aligned_array, std::align_val_t { 64U });
#if defined(__cpp_sized_deallocation)
    ::operator delete(sized_aligned_scalar, 17U, std::align_val_t { 64U });
    ::operator delete[](sized_aligned_array, 18U, std::align_val_t { 64U });
#else
    ::operator delete(sized_aligned_scalar, std::align_val_t { 64U });
    ::operator delete[](sized_aligned_array, std::align_val_t { 64U });
#endif
    ::operator delete(nothrow_scalar, std::nothrow);
    ::operator delete[](nothrow_array, std::nothrow);
    ::operator delete(nothrow_aligned_scalar,
        std::align_val_t { 64U }, std::nothrow);
    ::operator delete[](nothrow_aligned_array,
        std::align_val_t { 64U }, std::nothrow);

    bool constructor_failures_caught { };
    auto* const throwing_storage
        = ::operator new(sizeof(ThrowingConstructor), std::nothrow);
    if (throwing_storage != nullptr) {
        try {
            static_cast<void>(::new (throwing_storage) ThrowingConstructor);
            ::operator delete(throwing_storage);
        } catch (const int value) {
            constructor_failures_caught = value == 1;
            ::operator delete(throwing_storage, std::nothrow);
        } catch (...) {
            ::operator delete(throwing_storage, std::nothrow);
        }
    }
    auto* const aligned_throwing_storage = ::operator new(
        sizeof(AlignedThrowingConstructor),
        std::align_val_t { alignof(AlignedThrowingConstructor) },
        std::nothrow);
    if (aligned_throwing_storage != nullptr) {
        try {
            static_cast<void>(::new (aligned_throwing_storage)
                    AlignedThrowingConstructor);
            ::operator delete(aligned_throwing_storage,
                std::align_val_t { alignof(AlignedThrowingConstructor) });
        } catch (const int value) {
            constructor_failures_caught
                = constructor_failures_caught && value == 2;
            ::operator delete(aligned_throwing_storage,
                std::align_val_t { alignof(AlignedThrowingConstructor) },
                std::nothrow);
        } catch (...) {
            ::operator delete(aligned_throwing_storage,
                std::align_val_t { alignof(AlignedThrowingConstructor) },
                std::nothrow);
        }
    }

    const auto after_smoke = snapshot();
    static_cast<void>(exchange_enabled(false));
    constexpr auto smoke_bytes = UINT64_C(11) + 12U + 13U + 14U
        + 15U + 16U + 17U + 18U + 19U + 20U + 21U + 22U
        + sizeof(ThrowingConstructor)
        + sizeof(AlignedThrowingConstructor);
    const bool smoke_ok = aligned && constructor_failures_caught
        && same_delta(before, after_smoke, 14U, smoke_bytes, 0U);
    if (!smoke_ok) {
        std::fprintf(stderr,
            "allocation smoke failed: aligned=%d constructor=%d "
            "requests=%llu bytes=%llu failures=%llu expected_bytes=%llu\n",
            aligned ? 1 : 0,
            constructor_failures_caught ? 1 : 0,
            static_cast<unsigned long long>(after_smoke.requests - before.requests),
            static_cast<unsigned long long>(
                after_smoke.requested_bytes - before.requested_bytes),
            static_cast<unsigned long long>(after_smoke.failures - before.failures),
            static_cast<unsigned long long>(smoke_bytes));
        return 1;
    }

    auto previous_handler = std::set_new_handler(clear_new_handler);
    new_handler_calls = 0U;
    const auto nothrow_failure_before = snapshot();
    static_cast<void>(exchange_enabled(true));
    auto* const failed_nothrow = ::operator new(
        std::numeric_limits<std::size_t>::max(), std::nothrow);
    const auto nothrow_failure_after = snapshot();
    static_cast<void>(exchange_enabled(false));
    static_cast<void>(std::set_new_handler(previous_handler));
    if (failed_nothrow != nullptr) {
        ::operator delete(failed_nothrow);
    }
    if (failed_nothrow != nullptr || new_handler_calls != 1U
        || !same_delta(nothrow_failure_before, nothrow_failure_after,
            1U, std::numeric_limits<std::uint64_t>::max(), 1U)) {
        return 2;
    }

    previous_handler = std::set_new_handler(throw_new_handler);
    new_handler_calls = 0U;
    const auto throwing_failure_before = snapshot();
    static_cast<void>(exchange_enabled(true));
    bool bad_alloc_caught { };
    try {
        static_cast<void>(::operator new(
            std::numeric_limits<std::size_t>::max()));
    } catch (const std::bad_alloc&) {
        bad_alloc_caught = true;
    }
    const auto throwing_failure_after = snapshot();
    static_cast<void>(exchange_enabled(false));
    static_cast<void>(std::set_new_handler(previous_handler));
    return bad_alloc_caught && new_handler_calls == 1U
            && same_delta(throwing_failure_before, throwing_failure_after,
                1U, std::numeric_limits<std::uint64_t>::max(), 1U)
        ? 0 : 3;
}
