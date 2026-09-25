// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_internal.hpp"

#include "fsim/support/path.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>

namespace fsim::app::workspace {
namespace {

char folded(const unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 'a' - 'A')
                                      : static_cast<char>(value);
}

void append_identity(std::string& result, const std::string& value)
{
    result += std::to_string(value.size()) + ":" + value;
}

bool safe_payload_path(const std::filesystem::path& path)
{
    if (path.is_absolute() || path.has_root_name())
        return false;
    for (const auto& component : path) {
        const auto text = support::path_to_utf8(component);
        if (text.empty() || text == "." || text == ".."
            || text.find_first_of("\\:\0", 0U, 3U) != std::string::npos)
            return false;
    }
    return true;
}

bool managed_revision_filename(const std::string_view name)
{
    if (name.size() < 34U || name.front() != 'r')
        return false;
    for (const char character : name.substr(1U, 32U)) {
        if (!(character >= '0' && character <= '9')
            && !(character >= 'a' && character <= 'f'))
            return false;
    }
    const auto extension = name.substr(33U);
    return extension == ".fsimobj" || extension == ".fsimscobj"
        || extension == ".fsimscplugin" || extension == ".fsimdesign";
}

} // namespace

bool valid_name(const std::string_view name)
{
    if (name.empty() || name.size() > 128U)
        return false;
    const auto letter = [](const char value) {
        return (value >= 'a' && value <= 'z')
            || (value >= 'A' && value <= 'Z') || value == '_';
    };
    if (!letter(name.front()))
        return false;
    for (const char value : name) {
        if (!letter(value) && !(value >= '0' && value <= '9') && value != '-')
            return false;
    }
    std::string lower { name };
    std::ranges::transform(lower, lower.begin(), folded);
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul")
        return false;
    return !(lower.size() == 4U
        && (lower.starts_with("com") || lower.starts_with("lpt"))
        && lower.back() >= '1' && lower.back() <= '9');
}

std::string unit_identity(const library::UnitIndexEntry& unit)
{
    std::string result;
    for (const auto* field : { &unit.language, &unit.kind, &unit.name,
             &unit.primary_name, &unit.architecture })
        append_identity(result, *field);
    return result;
}

std::optional<std::string> source_identity(
    const std::filesystem::path& path, std::string& error)
{
    error.clear();
    std::error_code code;
    if (path.empty()) {
        error = "cannot identify an empty source path";
        return std::nullopt;
    }
    const auto absolute = std::filesystem::absolute(path, code);
    if (code) {
        error = "cannot resolve source path: " + code.message();
        return std::nullopt;
    }
    const auto canonical = std::filesystem::weakly_canonical(absolute, code);
    if (code) {
        error = "cannot identify source path: " + code.message();
        return std::nullopt;
    }
    auto result = support::path_to_utf8(canonical);
#ifdef _WIN32
    std::ranges::transform(result, result.begin(), folded);
#endif
    return result;
}

namespace detail {

bool ensure_directory(const std::filesystem::path& path, std::string& error)
{
    std::error_code code;
    const auto status = std::filesystem::symlink_status(path, code);
    if (!code && std::filesystem::is_directory(status))
        return true;
    if (std::filesystem::is_symlink(status)
        || (!code && std::filesystem::exists(status))) {
        error = "workspace directory is not a real directory: "
            + support::path_to_utf8(path);
        return false;
    }
    if (code && code != std::errc::no_such_file_or_directory) {
        error = "cannot inspect workspace directory: " + code.message();
        return false;
    }
    code.clear();
    if (!std::filesystem::create_directory(path, code) && code) {
        error = "cannot create workspace directory: " + code.message();
        return false;
    }
    return true;
}

bool safe_descendant(const std::filesystem::path& root,
    const std::filesystem::path& relative, std::string& error)
{
    if (relative.empty() || relative.is_absolute() || relative.has_root_name()) {
        error = "catalog artifact path must be relative to its library";
        return false;
    }
    auto current = root;
    for (const auto& component : relative) {
        const auto spelling = support::path_to_utf8(component);
        if (spelling.empty() || spelling == "." || spelling == ".."
            || spelling.find_first_of("\\:\0", 0U, 3U) != std::string::npos) {
            error = "catalog artifact path contains an unsafe component";
            return false;
        }
        current /= component;
        std::error_code code;
        const auto status = std::filesystem::symlink_status(current, code);
        if (std::filesystem::is_symlink(status)) {
            error = "catalog artifact path traverses a symbolic link";
            return false;
        }
        if (code && code != std::errc::no_such_file_or_directory) {
            error = "cannot inspect catalog artifact path: " + code.message();
            return false;
        }
    }
    return true;
}

bool managed_artifact_path(const ArtifactRecord& record)
{
    if (record.id.size() != 33U || record.id.front() != 'r')
        return false;
    if (!std::ranges::all_of(record.id.substr(1), [](const char value) {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        }))
        return false;
    std::string extension;
    switch (record.kind) {
    case ArtifactKind::Hdl:
        extension = ".fsimobj";
        break;
    case ArtifactKind::SystemCObject:
        extension = ".fsimscobj";
        break;
    case ArtifactKind::SystemCPlugin:
        extension = ".fsimscplugin";
        break;
    default:
        return false;
    }
    return record.path == std::filesystem::path { "artifacts" }
        / (record.id + extension);
}

bool valid_record(const ArtifactRecord& record, std::string& error)
{
    if (!managed_artifact_path(record) || record.fingerprint.empty()
        || record.fingerprint.size() > kMaximumTextBytes
        || record.sources.size() > kMaximumRecords
        || record.units.size() > kMaximumRecords
        || record.dependencies.size() > kMaximumRecords) {
        error = "invalid or oversized workspace artifact record";
        return false;
    }
    std::set<std::string> sources;
    for (const auto& source : record.sources) {
        if (source.empty() || source.size() > kMaximumTextBytes
            || !sources.insert(source).second) {
            error = "workspace artifact has duplicate or invalid source ownership";
            return false;
        }
    }
    std::set<std::string> units;
    for (const auto& owned : record.units) {
        if (owned.unit.name.empty() || owned.unit.language.empty()
            || owned.unit.kind.empty()
            || !safe_payload_path(owned.unit.artifact)
            || !units.insert(unit_identity(owned.unit)).second
            || (!sources.contains(owned.source_key)
                && !(owned.source_key.empty()
                    && record.kind == ArtifactKind::SystemCPlugin))) {
            error = "workspace artifact has duplicate or invalid unit ownership";
            return false;
        }
    }
    for (const auto& dependency : record.dependencies) {
        if (!valid_name(dependency.library) || dependency.unit.name.empty()
            || dependency.unit.language.empty() || dependency.unit.kind.empty()
            || !safe_payload_path(dependency.unit.artifact)
            || dependency.fingerprint.empty()) {
            error = "workspace artifact has an invalid imported-unit dependency";
            return false;
        }
    }
    return true;
}

std::string new_revision()
{
    static std::atomic_uint64_t sequence { 0U };
    static const std::uint64_t salt = [] {
        std::random_device random;
        return (static_cast<std::uint64_t>(random()) << 32U) ^ random();
    }();
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream output;
    output << 'r' << std::hex;
    output.width(16);
    output.fill('0');
    output << (now ^ salt);
    output.width(16);
    output << sequence.fetch_add(1U);
    return output.str();
}

void remove_revision(const std::filesystem::path& path) noexcept
{
    std::error_code code;
    const auto status = std::filesystem::symlink_status(path, code);
    if (code)
        return;
    if (std::filesystem::is_symlink(status)) {
        std::filesystem::remove(path, code);
        return;
    }
    std::filesystem::permissions(path, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add, code);
    std::filesystem::recursive_directory_iterator iterator { path, code };
    const std::filesystem::recursive_directory_iterator end;
    while (!code && iterator != end) {
        const auto entry_status = iterator->symlink_status(code);
        if (!code && !std::filesystem::is_symlink(entry_status))
            std::filesystem::permissions(iterator->path(),
                std::filesystem::perms::owner_all,
                std::filesystem::perm_options::add, code);
        if (!code)
            iterator.increment(code);
    }
    code.clear();
    // Loaded DLLs may remain locked on Windows. A later successful publication
    // retries collection; failure here never invalidates the committed catalog.
    std::filesystem::remove_all(path, code);
}

void collect_revisions(const std::filesystem::path& root,
    const std::set<std::string>& retained) noexcept
{
    std::error_code code;
    const auto status = std::filesystem::symlink_status(root, code);
    if (code || !std::filesystem::is_directory(status))
        return;
    std::filesystem::directory_iterator iterator { root, code };
    const std::filesystem::directory_iterator end;
    while (!code && iterator != end) {
        const auto filename = support::path_to_utf8(iterator->path().filename());
        if (managed_revision_filename(filename) && !retained.contains(filename))
            remove_revision(iterator->path());
        iterator.increment(code);
    }
}

bool replace_text(const std::filesystem::path& destination,
    const std::string_view text, std::string& error)
{
    const auto temporary = destination.parent_path() / (new_revision() + ".tmp");
    std::ofstream output { temporary, std::ios::binary | std::ios::trunc };
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    std::error_code code;
    if (!output || !compiler::detail::atomic_replace_file(temporary, destination, code)) {
        error = "cannot atomically publish workspace metadata: " + code.message();
        std::filesystem::remove(temporary, code);
        return false;
    }
    return true;
}

std::optional<std::string> read_text(const std::filesystem::path& path,
    const std::size_t limit, std::string& error)
{
    std::error_code code;
    const auto status = std::filesystem::symlink_status(path, code);
    const auto size = std::filesystem::file_size(path, code);
    if (code || !std::filesystem::is_regular_file(status) || size > limit) {
        error = "workspace metadata is absent, unsafe, or exceeds its size limit";
        return std::nullopt;
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    std::ifstream input { path, std::ios::binary };
    input.read(result.data(), static_cast<std::streamsize>(result.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        error = "cannot read complete workspace metadata";
        return std::nullopt;
    }
    return result;
}

std::unique_ptr<compiler::detail::CacheDirectoryLock> lock_directory(
    const std::filesystem::path& path, std::string& error)
{
    if (!safe_descendant(path.parent_path(), path.filename(), error))
        return nullptr;
    std::error_code code;
    auto lock = std::make_unique<compiler::detail::CacheDirectoryLock>(path, code);
    if (!lock->held()) {
        error = "cannot lock workspace metadata: " + code.message();
        return nullptr;
    }
    return lock;
}

} // namespace detail
} // namespace fsim::app::workspace
