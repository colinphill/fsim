// SPDX-License-Identifier: Apache-2.0
#include "application_foreign_run_state.hpp"

#include "application_vhpi_foreign.hpp"
#include "application_vpi_foreign.hpp"

#include "fsim/runtime/run_state_archive.hpp"

#include <exception>
#include <type_traits>

#if defined(__linux__)
#include <dlfcn.h>
#include <link.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <cstring>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fsim::app::application_detail {
namespace {

#if defined(__linux__)
/// The load bias of an already loaded module (null when it is not loaded).
[[nodiscard]] std::uintptr_t load_bias(const char* const module)
{
    void* const handle = dlopen(module, RTLD_NOLOAD | RTLD_LAZY);
    if (handle == nullptr) {
        return 0U;
    }
    link_map* map = nullptr;
    std::uintptr_t bias = 0U;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) == 0 && map != nullptr) {
        bias = static_cast<std::uintptr_t>(map->l_addr);
    }
    dlclose(handle);
    return bias;
}
#elif defined(__APPLE__)
/// The header address of an already loaded image named `module` (null when
/// it is not loaded). dladdr reports the same header as dli_fbase, so an
/// offset taken from one resolves through the other.
[[nodiscard]] std::uintptr_t load_bias(const char* const module)
{
    const auto count = _dyld_image_count();
    for (std::uint32_t index = 0U; index < count; ++index) {
        const char* const name = _dyld_get_image_name(index);
        if (name != nullptr && std::strcmp(name, module) == 0) {
            return reinterpret_cast<std::uintptr_t>(
                _dyld_get_image_header(index));
        }
    }
    return 0U;
}
#endif

// Both sections are at version 2: the common part gained the call results
// (ForeignRunState::call_results, empty for VHPI), and "foreign.vpi" is
// followed by the delay edits of the VPI service
// (VpiForeignService::io_delay_edits).
template <typename Service>
void add_section(runtime::RunStateSectionRegistry& registry,
    const char* const name, Service* const service,
    const std::uint32_t version)
{
    if (service == nullptr) {
        return;
    }
    constexpr bool delay_edits
        = std::is_base_of_v<VpiForeignService, Service>;
    runtime::RunStateSectionOwner owner;
    owner.name = name;
    owner.version = version;
    owner.required = false;
    owner.save = [service](runtime::RunStateArchive& archive) {
        auto state = service->save_run_state();
        archive.io(state);
        if constexpr (delay_edits) {
            service->io_delay_edits(archive);
        }
    };
    owner.restore = [service](runtime::RunStateArchive& archive) {
        ForeignRunState state;
        archive.io(state);
        if constexpr (delay_edits) {
            // Before the restart callbacks are registered: the edits are
            // plain tables, and the safe-point hook for the queued ones is
            // armed by the reader.
            service->io_delay_edits(archive);
        }
        if (!archive.ok()) {
            return;
        }
        // A registration whose library is not loaded fails the restart with
        // FSIM-SAVE-0016 instead of losing the callback silently.
        try {
            service->restore_run_state(std::move(state));
        } catch (const std::exception& error) {
            archive.fail(error.what());
        }
    };
    owner.audit = [service](std::vector<std::string>& messages) {
        service->audit_rest_state(messages);
    };
    registry.add(std::move(owner));
}

} // namespace

std::optional<std::pair<std::string, std::uint64_t>> encode_callback_routine(
    const void* const routine)
{
#if defined(__linux__)
    Dl_info info { };
    if (routine == nullptr || dladdr(routine, &info) == 0
        || info.dli_fname == nullptr) {
        return std::nullopt;
    }
    const auto bias = load_bias(info.dli_fname);
    if (bias == 0U) {
        return std::nullopt;
    }
    return std::make_pair(std::string { info.dli_fname },
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(routine) - bias));
#elif defined(__APPLE__)
    Dl_info info { };
    if (routine == nullptr || dladdr(routine, &info) == 0
        || info.dli_fname == nullptr || info.dli_fbase == nullptr) {
        return std::nullopt;
    }
    return std::make_pair(std::string { info.dli_fname },
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(routine)
            - reinterpret_cast<std::uintptr_t>(info.dli_fbase)));
#elif defined(_WIN32)
    HMODULE module = nullptr;
    if (routine == nullptr
        || GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(routine), &module) == 0
        || module == nullptr) {
        return std::nullopt;
    }
    // The module base is the HMODULE; the name is the path the loader holds.
    std::string path(MAX_PATH, '\0');
    DWORD length = GetModuleFileNameA(module, path.data(),
        static_cast<DWORD>(path.size()));
    while (length == path.size() && path.size() < 32768U) {
        path.resize(path.size() * 2U);
        length = GetModuleFileNameA(module, path.data(),
            static_cast<DWORD>(path.size()));
    }
    if (length == 0U || length >= path.size()) {
        return std::nullopt;
    }
    path.resize(length);
    return std::make_pair(std::move(path),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(routine)
            - reinterpret_cast<std::uintptr_t>(module)));
#else
    (void)routine;
    return std::nullopt;
#endif
}

void* decode_callback_routine(
    const std::string& module, const std::uint64_t offset)
{
#if defined(__linux__)
    const auto bias = load_bias(module.c_str());
    if (bias == 0U) {
        return nullptr;
    }
    return reinterpret_cast<void*>(
        bias + static_cast<std::uintptr_t>(offset));
#elif defined(__APPLE__)
    const auto bias = load_bias(module.c_str());
    if (bias == 0U) {
        return nullptr;
    }
    return reinterpret_cast<void*>(
        bias + static_cast<std::uintptr_t>(offset));
#elif defined(_WIN32)
    // GetModuleHandleA does not load the module: a library the run did not
    // load again reports "not loaded", which the caller turns into
    // FSIM-SAVE-0016.
    HMODULE const base = GetModuleHandleA(module.c_str());
    if (base == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(base)
        + static_cast<std::uintptr_t>(offset));
#else
    (void)module;
    (void)offset;
    return nullptr;
#endif
}

void register_foreign_run_state_sections(
    runtime::RunStateSectionRegistry& registry, VpiForeignService* const vpi,
    VhpiForeignService* const vhpi)
{
    add_section(registry, "foreign.vpi", vpi, 2U);
    add_section(registry, "foreign.vhpi", vhpi, 2U);
}

} // namespace fsim::app::application_detail
