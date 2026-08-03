// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace fsim::platform {

class DynamicLibrary final {
public:
    DynamicLibrary() noexcept;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    ~DynamicLibrary();

    [[nodiscard]] static std::unique_ptr<DynamicLibrary> open(
        const std::filesystem::path& path, std::string& error);
    [[nodiscard]] static bool is_loaded(
        const std::filesystem::path& path) noexcept;

    [[nodiscard]] void* symbol(std::string_view name, std::string& error) const;
    [[nodiscard]] bool valid() const noexcept;

private:
    explicit DynamicLibrary(void* handle) noexcept;
    void close() noexcept;

    void* handle_{};
};

} // namespace fsim::platform
