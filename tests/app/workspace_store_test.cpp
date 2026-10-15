// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_workspace_store.hpp"

#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

namespace workspace = fsim::app::workspace;
namespace fs = std::filesystem;

class TemporaryWorkspace {
public:
    TemporaryWorkspace()
    {
        static std::atomic_uint64_t sequence { 0U };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("fsim-workspace-store-"
            + std::to_string(stamp) + "-" + std::to_string(sequence++));
        fs::create_directories(root);
    }

    ~TemporaryWorkspace()
    {
        std::error_code error;
        fs::recursive_directory_iterator iterator { root, error };
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (!iterator->is_symlink())
                fs::permissions(iterator->path(), fs::perms::owner_all,
                    fs::perm_options::add, error);
            iterator.increment(error);
        }
        fs::remove_all(root, error);
    }

    fs::path root;
};

fsim::library::UnitIndexEntry unit(std::string name)
{
    fsim::library::UnitIndexEntry result;
    result.language = "systemverilog";
    result.kind = "module";
    result.name = std::move(name);
    result.artifact = "compiled-hir.bin";
    result.checksum = "checksum";
    result.standard = "1800-2023";
    return result;
}

workspace::ArtifactRecord artifact(workspace::LibraryTransaction& transaction,
    std::vector<std::string> sources, std::vector<workspace::OwnedUnit> units,
    const workspace::ArtifactKind kind = workspace::ArtifactKind::Hdl)
{
    std::string error;
    auto record = transaction.allocate_artifact(kind, error);
    assert(record && error.empty());
    record->sources = std::move(sources);
    record->units = std::move(units);
    record->fingerprint = record->id;
    const auto path = transaction.artifact_path(*record);
    assert(!fs::exists(path));
    fs::create_directory(path);
    std::ofstream { path / "payload" } << "payload";
    fs::permissions(path / "payload", fs::perms::owner_read, fs::perm_options::replace);
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_exec,
        fs::perm_options::replace);
    return std::move(*record);
}

void execute_sql(const fs::path& path, const char* sql)
{
    sqlite3* database = nullptr;
    assert(sqlite3_open_v2(fsim::support::path_to_utf8(path).c_str(), &database,
        SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    assert(sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(sqlite3_close(database) == SQLITE_OK);
}

std::string encode_path(const fs::path& path)
{
    constexpr char digits[] = "0123456789abcdef";
    const auto utf8 = fsim::support::path_to_utf8(path);
    std::string encoded;
    encoded.reserve(utf8.size() * 2U);
    for (const char character : utf8) {
        const auto byte = static_cast<unsigned char>(character);
        encoded += digits[byte >> 4U];
        encoded += digits[byte & 0x0fU];
    }
    return encoded;
}

std::optional<fs::path> decode_path(const std::string_view encoded)
{
    if (encoded.empty() || encoded.size() % 2U != 0U)
        return std::nullopt;
    const auto value = [](const char digit) -> int {
        if (digit >= '0' && digit <= '9')
            return digit - '0';
        if (digit >= 'a' && digit <= 'f')
            return digit - 'a' + 10;
        if (digit >= 'A' && digit <= 'F')
            return digit - 'A' + 10;
        return -1;
    };
    std::string utf8;
    utf8.reserve(encoded.size() / 2U);
    for (std::size_t offset = 0U; offset < encoded.size(); offset += 2U) {
        const int high = value(encoded[offset]);
        const int low = value(encoded[offset + 1U]);
        if (high < 0 || low < 0)
            return std::nullopt;
        utf8 += static_cast<char>((high << 4) | low);
    }
    return fsim::support::path_from_utf8(utf8);
}

int leave_hot_journal(const fs::path& database_path)
{
    sqlite3* database = nullptr;
    const int opened = sqlite3_open_v2(
        fsim::support::native_fs::utf8_for_native_io(database_path).c_str(), &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOFOLLOW, nullptr);
    if (opened != SQLITE_OK) {
        if (database)
            sqlite3_close(database);
        return 10;
    }
    const int updated = sqlite3_exec(database,
        "BEGIN IMMEDIATE; UPDATE metadata SET version=99 WHERE version=1",
        nullptr, nullptr, nullptr);
    if (updated != SQLITE_OK || sqlite3_changes(database) != 1
        || sqlite3_db_cacheflush(database) != SQLITE_OK) {
        sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(database);
        return 11;
    }
    // Exit without SQLite cleanup to model a writer process interrupted after
    // its rollback journal is durable and before COMMIT.
    std::_Exit(0);
}

#if defined(_WIN32)
std::wstring quote_windows_argument(const std::wstring_view argument)
{
    std::wstring result { L"\"" };
    std::size_t backslashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result += character;
        } else {
            result.append(backslashes, L'\\');
            result += character;
        }
        backslashes = 0U;
    }
    result.append(backslashes * 2U, L'\\');
    result += L'\"';
    return result;
}
#endif

fs::path current_executable([[maybe_unused]] const char* argument_zero)
{
#if defined(_WIN32)
    std::wstring path(32768U, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(),
        static_cast<DWORD>(path.size()));
    assert(size != 0U && size < path.size());
    path.resize(size);
    return fs::path { path };
#else
    return fs::absolute(fs::path { argument_zero });
#endif
}

bool launch_hot_journal_child(const fs::path& executable, const fs::path& database)
{
    const auto encoded = encode_path(database);
#if defined(_WIN32)
    std::wstring command = quote_windows_argument(executable.native());
    command += L" --leave-hot-journal ";
    for (const char character : encoded)
        command += static_cast<wchar_t>(character);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup { };
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process { };
    if (!CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr,
            FALSE, 0, nullptr, nullptr, &startup, &process))
        return false;
    const auto waited = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1U;
    const bool completed = waited == WAIT_OBJECT_0
        && GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return completed && exit_code == 0U;
#else
    const auto child = fork();
    if (child < 0)
        return false;
    if (child == 0) {
        execl(executable.c_str(), executable.c_str(), "--leave-hot-journal",
            encoded.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

std::string read_bytes(const fs::path& path)
{
    std::ifstream input { path, std::ios::binary };
    assert(input);
    return { std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { } };
}

void test_source_replacement_and_group_selection()
{
    TemporaryWorkspace temporary;
    workspace::Store store { temporary.root };
    std::string error;
    const auto missing = store.read_library("work", error);
    assert(missing && missing->artifacts.empty());
    assert(!fs::exists(temporary.root / ".fsim"));
    auto transaction = store.begin_library("work", error);
    assert(transaction);
    auto grouped = artifact(*transaction, { "a.sv", "b.sv" },
        { { unit("old_a"), "a.sv" }, { unit("keep_b"), "b.sv" } });
    const auto grouped_id = grouped.id;
    const auto grouped_path = transaction->artifact_path(grouped);
    assert(transaction->commit(std::move(grouped), error));
    transaction.reset();

    transaction = store.begin_library("work", error);
    auto first = artifact(*transaction, { "a.sv" }, { { unit("new_a"), "a.sv" } });
    auto second = artifact(*transaction, { "a.sv" }, { { unit("new_a2"), "a.sv" } });
    first.dependencies.push_back({ "support", unit("package"), "import-fingerprint" });
    assert(transaction->commit({ first, second }, error));
    transaction.reset();
    auto catalog = store.read_library("work", error);
    assert(catalog && catalog->artifacts.size() == 3U);
    const auto retained = std::ranges::find(catalog->artifacts, grouped_id,
        &workspace::ArtifactRecord::id);
    assert(retained != catalog->artifacts.end());
    assert(retained->sources == std::vector<std::string> { "b.sv" });
    assert(retained->units.size() == 1U && retained->units.front().unit.name == "keep_b");
    assert(fs::is_directory(grouped_path));
    const auto imported = std::ranges::find(catalog->artifacts, first.id,
        &workspace::ArtifactRecord::id);
    assert(imported->dependencies.front().fingerprint == "import-fingerprint");

    transaction = store.begin_library("work", error);
    const std::vector<std::string> removed { "a.sv" };
    assert(transaction->commit({ }, removed, error));
    transaction.reset();
    catalog = store.read_library("work", error);
    assert(catalog && catalog->artifacts.size() == 1U);
    assert(catalog->artifacts.front().id == grouped_id);
    assert(!fs::exists(catalog->location.directory / first.path));
    assert(!fs::exists(catalog->location.directory / second.path));
}

void test_collision_failure_and_sql_rollback()
{
    TemporaryWorkspace temporary;
    workspace::Store store { temporary.root };
    std::string error;
    auto transaction = store.begin_library("work", error);
    auto original = artifact(*transaction, { "original.sv" }, { { unit("top"), "original.sv" } });
    assert(transaction->commit(original, error));
    transaction.reset();
    transaction = store.begin_library("work", error);
    auto collision = artifact(*transaction, { "other.sv" }, { { unit("top"), "other.sv" } });
    const auto failed_path = transaction->artifact_path(collision);
    assert(!transaction->commit(std::move(collision), error));
    assert(error.find("collision") != std::string::npos);
    transaction.reset();
    assert(!fs::exists(failed_path));
    auto catalog = store.read_library("work", error);
    assert(catalog && catalog->artifacts.size() == 1U && catalog->artifacts.front().id == original.id);

    transaction = store.begin_library("work", error);
    auto native = artifact(*transaction, { "native.cpp" }, { }, workspace::ArtifactKind::SystemCObject);
    const auto native_path = transaction->artifact_path(native);
    // Duplicate IDs fail after the first INSERT, proving SQLite rolls back the
    // DELETE and partial replacement rather than publishing a partial catalog.
    assert(!transaction->commit({ native, native }, error));
    transaction.reset();
    assert(!fs::exists(native_path));
    catalog = store.read_library("work", error);
    assert(catalog && catalog->artifacts.size() == 1U && catalog->artifacts.front().id == original.id);
}

void test_native_invalidation_and_delete()
{
    TemporaryWorkspace temporary;
    workspace::Store store { temporary.root };
    std::string error;
    auto transaction = store.begin_library("native", error);
    auto object = artifact(*transaction, { "native.cpp" }, { }, workspace::ArtifactKind::SystemCObject);
    assert(transaction->commit(object, error));
    transaction.reset();
    transaction = store.begin_library("native", error);
    auto factory = unit("NativeFactory");
    factory.language = "systemc";
    auto plugin = artifact(*transaction, { "native.cpp" }, { { factory, "" } },
        workspace::ArtifactKind::SystemCPlugin);
    assert(transaction->commit(plugin, error));
    transaction.reset();
    auto catalog = store.read_library("native", error);
    assert(catalog && catalog->artifacts.size() == 2U);
    transaction = store.begin_library("native", error);
    auto replacement = artifact(*transaction, { "native.cpp" }, { }, workspace::ArtifactKind::SystemCObject);
    assert(transaction->commit(replacement, error));
    transaction.reset();
    catalog = store.read_library("native", error);
    assert(catalog && catalog->artifacts.size() == 1U);
    assert(catalog->artifacts.front().id == replacement.id);
    assert(!store.delete_artifact("native", "absent", error));
    assert(store.delete_artifact("native", replacement.id, error));
    catalog = store.read_library("native", error);
    assert(catalog && catalog->artifacts.empty());
    assert(store.delete_library("native", error));
    assert(!fs::exists(temporary.root / ".fsim/libraries/native"));
}

void test_mapping_and_corrupt_metadata()
{
    TemporaryWorkspace temporary;
    const auto external_root = temporary.root / "external";
    const auto consumer_root = temporary.root / "consumer";
    fs::create_directory(external_root);
    fs::create_directory(consumer_root);
    workspace::Store external { external_root };
    workspace::Store consumer { consumer_root };
    std::string error;
    auto transaction = external.begin_library("support", error);
    auto record = artifact(*transaction, { "package.sv" }, { { unit("package"), "package.sv" } });
    const auto location = transaction->catalog().location.directory;
    assert(transaction->commit(record, error));
    transaction.reset();
    assert(!consumer.map_library("wrong_name", location, error));
    assert(consumer.map_library("support", fs::relative(location, consumer_root), error));
    const auto mapped = consumer.read_library("support", error);
    assert(mapped && mapped->location.mapped && mapped->artifacts.size() == 1U);
    const auto locations = consumer.library_locations(error);
    assert(locations && locations->size() == 1U && locations->front().name == "support");
    assert(consumer.unmap_library("support", error));
    assert(fs::exists(location / "library.sqlite3"));
    assert(consumer.map_library("support", location, error));
    std::ofstream { location / "unrelated.txt" } << "keep me";
    assert(consumer.delete_library("support", error));
    assert(fs::exists(location / "unrelated.txt"));
    assert(!fs::exists(location / "library.sqlite3"));
    assert(consumer.library_locations(error)->empty());

    transaction = consumer.begin_library("work", error);
    record = artifact(*transaction, { "top.sv" }, { { unit("top"), "top.sv" } });
    const auto database = transaction->catalog().location.directory / "library.sqlite3";
    assert(transaction->commit(record, error));
    transaction.reset();
    execute_sql(database, "UPDATE metadata SET version=99");
    const auto modified = fs::last_write_time(database);
    assert(!consumer.read_library("work", error));
    assert(fs::last_write_time(database) == modified);
    execute_sql(database, "UPDATE metadata SET version=1; CREATE TABLE foreign_data(value TEXT)");
    assert(!consumer.read_library("work", error));
    assert(error.find("schema") != std::string::npos);
    execute_sql(database, "DROP TABLE foreign_data");
    execute_sql(database, "UPDATE metadata SET version=1; UPDATE artifacts SET path='../outside'");
    assert(!consumer.read_library("work", error));
    assert(error.find("unsafe") != std::string::npos);
    assert(!consumer.delete_library("work", error));
}

void test_writer_recovers_hot_journal_without_changing_reader_mode(
    const fs::path& executable)
{
    TemporaryWorkspace temporary;
    workspace::Store store { temporary.root };
    std::string error;
    auto transaction = store.begin_library("work", error);
    assert(transaction && error.empty());
    auto original = artifact(*transaction, { "top.sv" }, { { unit("top"), "top.sv" } });
    const auto database = transaction->catalog().location.directory / "library.sqlite3";
    assert(transaction->commit(std::move(original), error));
    transaction.reset();

    const auto journal = database.parent_path() / "library.sqlite3-journal";
    assert(launch_hot_journal_child(executable, database));
    assert(fs::exists(journal));
    const auto database_before_reader = read_bytes(database);
    const auto journal_before_reader = read_bytes(journal);
    assert(!journal_before_reader.empty());

    // A plain read remains read-only: it reports the hot journal and leaves
    // both files byte-for-byte intact. Recovery belongs to the locked writer.
    const auto unreadable = store.read_library("work", error);
    assert(!unreadable && error.find("readonly") != std::string::npos);
    assert(read_bytes(database) == database_before_reader);
    assert(read_bytes(journal) == journal_before_reader);

    auto recovered_writer = store.begin_library("work", error);
    assert(recovered_writer && error.empty());
    assert(recovered_writer->catalog().artifacts.size() == 1U);
    recovered_writer.reset();
    assert(!fs::exists(journal));
    const auto recovered = store.read_library("work", error);
    assert(recovered && error.empty() && recovered->artifacts.size() == 1U);
}

void test_snapshot_replacement_and_workspace_boundary()
{
    TemporaryWorkspace temporary;
    workspace::Store store { temporary.root };
    std::string error;
    auto transaction = store.begin_snapshot("default", error);
    assert(transaction && !fs::exists(transaction->artifact_path()));
    const auto first_path = transaction->artifact_path();
    fs::create_directory(first_path);
    std::ofstream { first_path / "runtime.bin" } << "first";
    assert(transaction->commit(error));
    transaction.reset();
    auto snapshot = store.read_snapshot("default", error);
    assert(snapshot && snapshot->path == first_path);
    transaction = store.begin_snapshot("default", error);
    const auto failed_path = transaction->artifact_path();
    fs::create_directory(failed_path);
    transaction.reset();
    assert(!fs::exists(failed_path));
    assert(store.read_snapshot("default", error)->path == first_path);
    transaction = store.begin_snapshot("default", error);
    const auto second_path = transaction->artifact_path();
    fs::create_directory(second_path);
    assert(transaction->commit(error));
    transaction.reset();
    assert(second_path != first_path && !fs::exists(first_path));
    assert(store.read_snapshot("default", error)->path == second_path);
    auto library = store.begin_library("work", error);
    auto record = artifact(*library, { "top.sv" }, { { unit("top"), "top.sv" } });
    assert(library->commit(std::move(record), error));
    library.reset();
    assert(store.delete_library("work", error));
    assert(store.read_snapshot("default", error)->path == second_path);
    assert(!store.begin_snapshot("../escape", error));
    assert(!store.begin_library("../escape", error));
    assert(!store.begin_library("CON", error));

    fs::create_directory(temporary.root / "nested");
    workspace::Store nested { temporary.root / "nested" };
    assert(!nested.read_snapshot("default", error));
    assert(nested.library_locations(error)->empty());
    std::ofstream { temporary.root / ".fsim/snapshots/default/snapshot.index" }
        << "FSIM-SNAPSHOT 1\n../../escape\n";
    assert(!store.read_snapshot("default", error));
}

void test_serialized_writers_and_symlinks()
{
    TemporaryWorkspace temporary;
    workspace::Store store { temporary.root };
    std::string error;
    auto first = store.begin_library("work", error);
    assert(first);
    std::promise<void> started;
    auto ready = started.get_future();
    auto second = std::async(std::launch::async, [&] {
        started.set_value();
        std::string second_error;
        return store.begin_library("work", second_error);
    });
    ready.wait();
    assert(second.wait_for(std::chrono::milliseconds { 100 }) == std::future_status::timeout);
    first.reset();
    assert(second.wait_for(std::chrono::seconds { 5 }) == std::future_status::ready);
    assert(second.get());

    const auto outside = temporary.root / "outside";
    fs::create_directory(outside);
    const auto artifacts = temporary.root / ".fsim/libraries/work/artifacts";
    std::error_code code;
    fs::create_directory_symlink(outside, artifacts, code);
    if (!code) {
        auto transaction = store.begin_library("work", error);
        assert(transaction);
        assert(!transaction->allocate_artifact(workspace::ArtifactKind::Hdl, error));
        assert(fs::is_empty(outside));
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view { argv[1] } == "--leave-hot-journal") {
        const auto database = decode_path(argv[2]);
        return database ? leave_hot_journal(*database) : 12;
    }
    assert(argc == 1);
    test_source_replacement_and_group_selection();
    test_collision_failure_and_sql_rollback();
    test_native_invalidation_and_delete();
    test_mapping_and_corrupt_metadata();
    test_writer_recovers_hot_journal_without_changing_reader_mode(
        current_executable(argv[0]));
    test_snapshot_replacement_and_workspace_boundary();
    test_serialized_writers_and_symlinks();
}
