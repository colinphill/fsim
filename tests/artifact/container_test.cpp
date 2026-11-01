// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/container.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path scratch_directory()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto directory = std::filesystem::temp_directory_path()
        / ("fsim-container-test-" + std::to_string(stamp));
    std::filesystem::create_directories(directory);
    return directory;
}

std::size_t file_count(const std::filesystem::path& directory)
{
    std::size_t count = 0U;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        (void)entry;
        ++count;
    }
    return count;
}

void round_trip(const std::filesystem::path& directory)
{
    const auto path = directory / "unit.fsimobj";
    std::string error;
    // A payload that spans several chunks, written both whole and streamed.
    std::string large(fsim::artifact::kContainerChunkBytes * 2U + 17U, 'x');
    for (std::size_t index = 0U; index < large.size(); index += 4099U)
        large[index] = static_cast<char>('a' + index % 26U);
    {
        auto writer = fsim::artifact::ContainerWriter::create(
            path, "fsim-object", fsim::artifact::ContainerPages::bulk, error);
        assert(writer && error.empty());
        assert(writer->meta("unit", "work.e", error));
        assert(writer->put("metadata", "abc", error));
        assert(writer->put("empty", "", error));
        assert(writer->put("large", large, error));
        assert(writer->put_stream("streamed", [&](std::ostream& output) {
            output.write(large.data(), static_cast<std::streamsize>(large.size()));
            return true;
        }, error));
        // Nothing is visible before publication.
        assert(!std::filesystem::exists(path));
        assert(writer->publish(error));
    }
    assert(file_count(directory) == 1U);

    auto reader = fsim::artifact::ContainerReader::open(path, "fsim-object", error);
    assert(reader && error.empty());
    assert(reader->meta("unit", error) == "work.e");
    assert(reader->read("metadata", 1024U, error) == "abc");
    assert(reader->read("empty", 0U, error) == "");
    assert(reader->read("large", large.size(), error) == large);
    assert(reader->read("streamed", large.size(), error) == large);
    const auto entries = reader->entries(error);
    assert(entries && entries->size() == 4U);
    const auto large_entry = reader->entry("large", error);
    assert(large_entry && large_entry->chunks == 3U && large_entry->size == large.size());
    const auto streamed_entry = reader->entry("streamed", error);
    assert(streamed_entry && streamed_entry->checksum == large_entry->checksum);

    // Size limits apply before any chunk is read; absent payloads fail.
    assert(!reader->read("large", large.size() - 1U, error) && !error.empty());
    error.clear();
    assert(!reader->entry("missing", error) && error.empty());
    assert(!reader->read("missing", 1U, error) && !error.empty());

    // The kind is checked.
    error.clear();
    assert(!fsim::artifact::ContainerReader::open(path, "fsim-design", error));
    assert(error.find("fsim-object") != std::string::npos);
}

void replace_and_abandon(const std::filesystem::path& directory)
{
    const auto path = directory / "replaced.fsimdesign";
    std::string error;
    for (const auto* value : { "first", "second" }) {
        auto writer = fsim::artifact::ContainerWriter::create(
            path, "fsim-design", fsim::artifact::ContainerPages::bulk, error);
        assert(writer && writer->put("state", value, error) && writer->publish(error));
    }
    auto reader = fsim::artifact::ContainerReader::open(path, "fsim-design", error);
    assert(reader && reader->read("state", 16U, error) == "second");

    // An unpublished writer leaves no file behind.
    const auto before = file_count(directory);
    {
        auto writer = fsim::artifact::ContainerWriter::create(
            directory / "abandoned.fsimobj", "fsim-object",
            fsim::artifact::ContainerPages::compact, error);
        assert(writer && writer->put("metadata", "x", error));
    }
    assert(file_count(directory) == before);
}

void rejects_foreign_files(const std::filesystem::path& directory)
{
    const auto path = directory / "not-an-artifact.fsimobj";
    {
        std::ofstream output { path, std::ios::binary };
        output << "plain text";
    }
    std::string error;
    assert(!fsim::artifact::ContainerReader::open(path, "fsim-object", error));
    assert(!error.empty());
}

} // namespace

int main()
{
    const auto directory = scratch_directory();
    for (const auto* name : { "round-trip", "replace", "foreign" })
        std::filesystem::create_directories(directory / name);
    round_trip(directory / "round-trip");
    replace_and_abandon(directory / "replace");
    rejects_foreign_files(directory / "foreign");
    std::filesystem::remove_all(directory);
    return 0;
}
