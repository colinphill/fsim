// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/object_cache.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::string read_bytes(const std::filesystem::path& path)
{
    std::ifstream stream { path, std::ios::binary };
    assert(stream);
    return { std::istreambuf_iterator<char> { stream },
        std::istreambuf_iterator<char> { } };
}

void write_bytes(const std::filesystem::path& path,
    const std::string_view bytes)
{
    std::ofstream stream { path, std::ios::binary | std::ios::trunc };
    assert(stream);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    assert(stream);
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-cache-wire-fixture-" + std::to_string(nonce));
    const fsim::compiler::ObjectCache cache { root };
    fsim::compiler::CacheKeyBuilder builder;
    const auto key = builder.add("codec-fixture", "v1").finish();
    const std::array payload {
        std::byte { 0x61 }, std::byte { 0x62 }, std::byte { 0x63 }
    };
    std::error_code error;
    assert(cache.store(key, payload, error) && !error);

    // Freeze the complete V1 envelope, including byte order, checksum text,
    // delimiter, and payload, before migrating persistence primitives.
    constexpr std::string_view expected =
        "FSIM-OBJECT-CACHE-V1\n"
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
        "abc";
    const auto path = cache.path_for(key);
    assert(read_bytes(path) == expected);
    const auto loaded = cache.load(key, error);
    assert(loaded && std::ranges::equal(*loaded, payload) && !error);

    const auto reject = [&](const std::string_view bytes) {
        write_bytes(path, bytes);
        assert(!cache.load(key, error));
        assert(error == std::errc::illegal_byte_sequence);
    };
    reject({ });
    reject(expected.substr(0U, 20U));
    reject(expected.substr(0U, expected.size() - 1U));
    auto corrupt = std::string { expected };
    corrupt[21U] = '0';
    reject(corrupt);
    reject(std::string { expected } + "x");

    write_bytes(path, expected);
    assert(cache.load(key, error) && !error);
    std::filesystem::remove_all(root, error);
}
