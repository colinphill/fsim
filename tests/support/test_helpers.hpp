// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::test {

inline void require(const bool condition, const std::string_view message,
    const std::source_location location = std::source_location::current())
{
    if (!condition) {
        throw std::runtime_error(std::string { message } + " ("
            + location.file_name() + ':'
            + std::to_string(location.line()) + ')');
    }
}

template <std::ranges::range Diagnostics>
[[nodiscard]] const std::ranges::range_value_t<Diagnostics>*
find_diagnostic(const Diagnostics& diagnostics, const std::string_view code)
{
    const auto found = std::ranges::find_if(diagnostics,
        [code](const auto& diagnostic) { return diagnostic.code == code; });
    return found == std::ranges::end(diagnostics) ? nullptr : &*found;
}

class TemporaryDirectory final {
public:
    std::filesystem::path path;

    explicit TemporaryDirectory(const std::string_view prefix)
    {
        static std::atomic<std::uint64_t> serial { };
        const auto root = std::filesystem::temp_directory_path();
        for (std::size_t attempt = 0; attempt < 32U; ++attempt) {
            const auto nonce = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
            auto candidate = root / (std::string { prefix } + '-'
                + std::to_string(nonce) + '-'
                + std::to_string(serial.fetch_add(1U)));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path = std::move(candidate);
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error(
                    "cannot create test directory", candidate, error);
            }
        }
        throw std::runtime_error("cannot create unique test directory");
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

inline void write_text(const std::filesystem::path& path,
    const std::string_view contents)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot open test file " + path.string());
    }
    output.write(contents.data(),
        static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) {
        throw std::runtime_error("cannot write test file " + path.string());
    }
}

} // namespace fsim::test
