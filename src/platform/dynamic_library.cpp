// SPDX-License-Identifier: Apache-2.0
#include "fsim/platform/dynamic_library.hpp"

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
#  if !defined(WIN32_LEAN_AND_MEAN)
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <utility>

namespace fsim::platform {
namespace {

#if defined(_WIN32)
std::string windows_error(const DWORD code) {
    if (code == 0) {
        return "unknown Windows loader error";
    }
    char* message = nullptr;
    const auto size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&message),
        0,
        nullptr);
    std::string result =
        size == 0 || message == nullptr ? "Windows loader error " + std::to_string(code)
                                       : std::string{message, size};
    if (message != nullptr) {
        LocalFree(message);
    }
    return result;
}
#endif

} // namespace

DynamicLibrary::DynamicLibrary() noexcept = default;

DynamicLibrary::DynamicLibrary(void* handle) noexcept : handle_(handle) {}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() {
    close();
}

std::unique_ptr<DynamicLibrary> DynamicLibrary::open(
    const std::filesystem::path& path, std::string& error) {
    error.clear();
#if defined(_WIN32)
    std::error_code path_error;
    const auto resolved = path.is_absolute()
        ? path.lexically_normal()
        : std::filesystem::absolute(path, path_error).lexically_normal();
    if (path_error) {
        error = "cannot resolve Windows library path: "
            + path_error.message();
        return nullptr;
    }
    // Resolve dependent DLLs from the loaded image's directory and the
    // process' safe default locations. This makes a cached plug-in independent
    // of the caller's current directory and avoids legacy search-path capture.
    const auto handle = LoadLibraryExW(
        resolved.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
            | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle == nullptr) {
        error = windows_error(GetLastError());
        return nullptr;
    }
    return std::unique_ptr<DynamicLibrary>{
        new DynamicLibrary{reinterpret_cast<void*>(handle)}};
#else
    dlerror();
    const auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const auto* message = dlerror();
        error = message == nullptr ? "unknown dynamic loader error" : message;
        return nullptr;
    }
    return std::unique_ptr<DynamicLibrary>{new DynamicLibrary{handle}};
#endif
}

bool DynamicLibrary::is_loaded(
    const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return false;
    }
#if defined(_WIN32)
    std::error_code path_error;
    const auto resolved = path.is_absolute()
        ? path.lexically_normal()
        : std::filesystem::absolute(path, path_error).lexically_normal();
    if (path_error) {
        return false;
    }
    HMODULE module = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            resolved.c_str(),
            &module)) {
        return true;
    }
    return false;
#elif defined(RTLD_NOLOAD)
    const auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) {
        return false;
    }
    dlclose(handle);
    return true;
#else
    (void)path;
    return false;
#endif
}

void* DynamicLibrary::symbol(const std::string_view name, std::string& error) const {
    error.clear();
    if (handle_ == nullptr || name.empty() || name.find('\0') != std::string_view::npos) {
        error = "invalid dynamic-library symbol request";
        return nullptr;
    }
    const std::string owned_name{name};
#if defined(_WIN32)
    const auto address =
        GetProcAddress(reinterpret_cast<HMODULE>(handle_), owned_name.c_str());
    if (address == nullptr) {
        error = windows_error(GetLastError());
        return nullptr;
    }
    return reinterpret_cast<void*>(address);
#else
    dlerror();
    const auto address = dlsym(handle_, owned_name.c_str());
    if (const auto* message = dlerror(); message != nullptr) {
        error = message;
        return nullptr;
    }
    return address;
#endif
}

bool DynamicLibrary::valid() const noexcept {
    return handle_ != nullptr;
}

void DynamicLibrary::close() noexcept {
    if (handle_ == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

} // namespace fsim::platform
