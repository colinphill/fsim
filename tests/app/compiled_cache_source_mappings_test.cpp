// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/support/path.hpp"

#include "../../src/app/application_internal.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::filesystem::path path)
        : path { std::move(path) }
    {
        std::filesystem::create_directories(this->path);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

void write_file(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output { path };
    assert(output);
    output << "// source mapping fixture\n";
    assert(output);
}

fsim::app::CheckedSource make_source(
    const std::filesystem::path& path,
    const std::string& digest)
{
    fsim::app::CheckedSource result;
    result.path = path;
    result.language = "systemverilog";
    result.standard = "2017";
    result.content_digest = digest;
    return result;
}

void assert_mapping_bijection(
    const std::vector<fsim::library::SourceNameMapping>& mappings)
{
    std::set<std::string> producers;
    std::set<std::string> logical_names;
    for (const auto& mapping : mappings) {
        assert(producers.insert(fsim::app::application_detail::source_path_key(
                   fsim::support::path_from_utf8(mapping.producer_name)))
            .second);
        assert(logical_names.insert(fsim::app::application_detail::source_path_key(
                   fsim::support::path_from_utf8(mapping.logical_name)))
            .second);
        assert(!fsim::support::path_is_portably_absolute(
            fsim::support::path_from_utf8(mapping.logical_name)));
    }
    assert(producers.size() == mappings.size());
    assert(logical_names.size() == mappings.size());
}

void test_source_path_key_semantics()
{
    using fsim::app::application_detail::source_path_key;
    assert(source_path_key("build/./sources/../top.sv")
        == source_path_key("build/top.sv"));
#if defined(_WIN32)
    assert(source_path_key("C:\\Build\\Sources\\Top.sv")
        == source_path_key("c:/build/sources/top.sv"));
#else
    assert(source_path_key("Build/Top.sv")
        != source_path_key("build/top.sv"));
#endif
}

void test_duplicate_producers_preserve_first_order(
    const std::filesystem::path& directory)
{
    const auto first_path = directory / "project" / "src" / "first.sv";
    const auto shared_path = directory / "project" / "include" / "shared.svh";
    const auto second_path = directory / "project" / "src" / "second.sv";
    write_file(first_path);
    write_file(shared_path);
    write_file(second_path);

    fsim::app::CheckedProject checked;
    auto first = make_source(first_path, "first-digest");
    first.dependencies.push_back({ shared_path, "shared-digest" });
    checked.hdl_sources.push_back(first);
    checked.hdl_sources.push_back(first);
    auto second = make_source(second_path, "second-digest");
    second.dependencies.push_back({ shared_path, "shared-digest" });
    checked.hdl_sources.push_back(second);

    fsim::diagnostic::Engine diagnostics;
    const auto mappings
        = fsim::app::application_detail::compiled_cache_source_mappings(
            checked, directory / "project", diagnostics);
    assert(mappings && !diagnostics.has_error());
    assert(mappings->size() == 3U);
    assert_mapping_bijection(*mappings);
    assert((*mappings)[0].producer_name
        == fsim::support::path_to_utf8(first_path.lexically_normal()));
    assert((*mappings)[1].producer_name
        == fsim::support::path_to_utf8(shared_path.lexically_normal()));
    assert((*mappings)[2].producer_name
        == fsim::support::path_to_utf8(second_path.lexically_normal()));
}

void test_nonlexical_alias_fallback(const std::filesystem::path& directory)
{
    const auto source_path = directory / "project" / "src" / "top.sv";
    const auto alias_path = directory / "alias" / "top-alias.sv";
    write_file(source_path);
    std::filesystem::create_directories(alias_path.parent_path());
    std::error_code error;
    std::filesystem::create_hard_link(source_path, alias_path, error);
    if (error) {
        error.clear();
        std::filesystem::create_symlink(source_path, alias_path, error);
    }
    assert(!error);

    fsim::app::CheckedProject checked;
    checked.hdl_sources.push_back(make_source(source_path, "top-digest"));
    checked.hdl_sources.push_back(make_source(alias_path, "top-digest"));

    fsim::diagnostic::Engine diagnostics;
    const auto mappings
        = fsim::app::application_detail::compiled_cache_source_mappings(
            checked, directory / "project", diagnostics);
    assert(mappings && !diagnostics.has_error());
    assert(mappings->size() == 1U);
    assert_mapping_bijection(*mappings);
    assert(mappings->front().producer_name
        == fsim::support::path_to_utf8(source_path.lexically_normal()));
}

void test_empty_paths_are_ignored(const std::filesystem::path& directory)
{
    const auto source_path = directory / "project" / "src" / "valid.sv";
    const auto dependency_path
        = directory / "project" / "include" / "valid.svh";
    write_file(source_path);
    write_file(dependency_path);

    fsim::app::CheckedProject checked;
    checked.hdl_sources.push_back(make_source({}, "empty-source-digest"));
    auto valid = make_source(source_path, "valid-source-digest");
    valid.dependencies.push_back({ {}, "empty-dependency-digest" });
    valid.dependencies.push_back({ dependency_path, "valid-dependency-digest" });
    checked.hdl_sources.push_back(std::move(valid));

    fsim::diagnostic::Engine diagnostics;
    const auto mappings
        = fsim::app::application_detail::compiled_cache_source_mappings(
            checked, directory / "project", diagnostics);
    assert(mappings && !diagnostics.has_error());
    assert(mappings->size() == 2U);
    assert_mapping_bijection(*mappings);
    assert((*mappings)[0].producer_name
        == fsim::support::path_to_utf8(source_path.lexically_normal()));
    assert((*mappings)[1].producer_name
        == fsim::support::path_to_utf8(dependency_path.lexically_normal()));
}

} // namespace

int main()
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory temporary {
        std::filesystem::temp_directory_path()
        / ("fsim-compiled-cache-source-mappings-" + unique)
    };
    test_source_path_key_semantics();
    test_duplicate_producers_preserve_first_order(temporary.path / "order");
    test_nonlexical_alias_fallback(temporary.path / "aliases");
    test_empty_paths_are_ignored(temporary.path / "empty-paths");
    return 0;
}
