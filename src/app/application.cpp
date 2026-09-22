// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

BinaryPayloadReadResult read_binary_payload(
    const std::filesystem::path& path,
    const std::optional<std::uintmax_t> maximum_bytes)
{
    std::ifstream input { path, std::ios::binary };
    if (!input) {
        return { std::nullopt, BinaryPayloadReadFailure::open };
    }
    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(path, size_error);
    const bool budget_exceeded = !size_error && maximum_bytes
        && file_size > *maximum_bytes;
    if (size_error || budget_exceeded
        || file_size > std::string { }.max_size()
        || file_size
            > static_cast<std::uintmax_t>(
                std::numeric_limits<std::streamsize>::max())) {
        return { std::nullopt, BinaryPayloadReadFailure::size,
            budget_exceeded };
    }
    try {
        std::string bytes(static_cast<std::size_t>(file_size), '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())
            || input.bad()) {
            return { std::nullopt, BinaryPayloadReadFailure::read };
        }
        return { std::move(bytes) };
    } catch (const std::bad_alloc&) {
        return { std::nullopt, BinaryPayloadReadFailure::size };
    } catch (const std::length_error&) {
        return { std::nullopt, BinaryPayloadReadFailure::size };
    }
}

} // namespace fsim::app::application_detail
