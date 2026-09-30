// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace native_fs = fsim::support::native_fs;
using fsim::support::path_for_native_io;
using fsim::support::path_from_native_io;

void check_spellings()
{
    assert(path_for_native_io(fs::path { }).empty());
#if defined(_WIN32)
    auto absolute = fs::absolute("native-filesystem-spelling").lexically_normal();
    absolute.make_preferred();
    assert(path_from_native_io(path_for_native_io("native-filesystem-spelling"))
        == absolute);
    for (const auto* device : { L"NUL", L"nul", L"CON", L"nul.txt", L"COM1",
             L"lpt9", L"CONOUT$", L"dir\\AUX" }) {
        const fs::path value { device };
        assert(path_for_native_io(value) == value);
    }
    assert(path_for_native_io(L"COM0").native().starts_with(L"\\\\?\\"));
    assert(path_for_native_io(L"console").native().starts_with(L"\\\\?\\"));
    const auto extended = path_for_native_io("native-filesystem-spelling");
    assert(extended.native().starts_with(L"\\\\?\\"));
    assert(path_for_native_io(extended) == extended);
    assert((extended / fs::path { "a/b" }).native().find(L'/') != std::wstring::npos);
    assert(path_for_native_io(extended / fs::path { "a/b" }).native().find(L'/')
        == std::wstring::npos);
    const fs::path unc { L"\\\\server\\share\\payload.bin" };
    assert(path_for_native_io(unc).native() == L"\\\\?\\UNC\\server\\share\\payload.bin");
    assert(path_from_native_io(path_for_native_io(unc)) == unc);
    assert(native_fs::utf8_for_native_io("native-filesystem-spelling").starts_with("\\\\?\\"));
#else
    assert(path_for_native_io("relative/name") == fs::path { "relative/name" });
    assert(native_fs::utf8_for_native_io("relative/name") == "relative/name");
#endif
}

void check_deep_tree(const fs::path& base)
{
    auto deep = base;
    for (const char marker : { 'a', 'b', 'c', 'd', 'e' })
        deep /= std::string(60U, marker);
    assert(fs::absolute(deep).native().size() > 300U);

    assert(!native_fs::exists(deep));
    assert(native_fs::create_directories(deep));
    assert(native_fs::is_directory(deep));
    assert(native_fs::is_empty(deep));

    const auto file = deep / "payload.txt";
    {
        auto output = native_fs::open_ofstream(file, std::ios::binary);
        output << "long path payload";
        assert(output.good());
    }
    assert(native_fs::exists(file));
    assert(native_fs::is_regular_file(file));
    assert(native_fs::is_regular_file(native_fs::status(file)));
    assert(!native_fs::is_symlink(native_fs::symlink_status(file)));
    assert(native_fs::file_size(file) == 17U);
    {
        auto input = native_fs::open_ifstream(file, std::ios::binary);
        const std::string text { std::istreambuf_iterator<char> { input },
            std::istreambuf_iterator<char> { } };
        assert(text == "long path payload");
    }
    {
        std::fstream stream;
        native_fs::open(stream, file, std::ios::in | std::ios::out | std::ios::binary);
        assert(stream.is_open());
    }
    {
        auto stream = native_fs::open_fstream(file, std::ios::in | std::ios::binary);
        assert(stream.is_open());
    }

    const auto stamp = native_fs::last_write_time(file) - std::chrono::hours { 1 };
    native_fs::last_write_time(file, stamp);
    assert(native_fs::last_write_time(file) == stamp);

    const auto renamed = deep / "renamed.txt";
    native_fs::rename(file, renamed);
    assert(!native_fs::exists(file) && native_fs::exists(renamed));
    assert(native_fs::equivalent(renamed, renamed));

    native_fs::permissions(renamed, fs::perms::owner_read, fs::perm_options::replace);
    native_fs::permissions(renamed, fs::perms::owner_all, fs::perm_options::add);

    const auto canonical = native_fs::canonical(renamed);
    assert(canonical.filename() == "renamed.txt");
    assert(canonical == path_from_native_io(canonical));
    assert(native_fs::weakly_canonical(deep / "missing" / "tail").filename() == "tail");

    // Entries come back extended; ordinary_entry_path restores the spelling
    // that std::filesystem gives an iteration over the same root.
    std::vector<fs::path> entries;
    for (const auto& entry : native_fs::directory_iterator(deep))
        entries.push_back(native_fs::ordinary_entry_path(deep, entry.path()));
    assert(entries == std::vector<fs::path> { deep / "renamed.txt" });
    std::size_t recursive = 0U;
    std::error_code error;
    for (auto iterator = native_fs::recursive_directory_iterator(
             base, fs::directory_options::skip_permission_denied, error),
         end = fs::recursive_directory_iterator {};
         !error && iterator != end; iterator.increment(error)) {
        const auto ordinary = native_fs::ordinary_entry_path(base, iterator->path());
        assert(ordinary.native().starts_with(base.native()));
        ++recursive;
    }
    assert(!error && recursive == 6U);

    // Errors name the ordinary path, not its extended-length spelling.
    const auto missing = deep / "missing.txt";
    try {
        (void)native_fs::file_size(missing);
        assert(false);
    } catch (const fs::filesystem_error& failure) {
        assert(failure.path1() == missing);
        const std::string what = failure.what();
        assert(what.find("\\\\?\\") == std::string::npos);
    }
    (void)native_fs::file_size(missing, error);
    assert(error);

    assert(native_fs::remove(renamed));
    assert(native_fs::remove_all(base) >= 6U);
    assert(!native_fs::exists(base));
}

} // namespace

int main()
{
    check_spellings();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    check_deep_tree(fs::temp_directory_path()
        / ("fsim-native-filesystem-" + std::to_string(stamp)));
    std::cout << "native filesystem tests passed\n";
    return 0;
}
