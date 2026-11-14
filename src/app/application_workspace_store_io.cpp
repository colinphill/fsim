// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_internal.hpp"

#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
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
    append_identity(result,
        std::to_string(static_cast<std::uint8_t>(unit.name_form)));
    append_identity(result,
        std::to_string(static_cast<std::uint8_t>(unit.primary_name_form)));
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
    const auto canonical = support::native_fs::weakly_canonical(absolute, code);
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
    const auto status = support::native_fs::symlink_status(path, code);
    if (!code && support::native_fs::is_directory(status))
        return true;
    if (support::native_fs::is_symlink(status)
        || (!code && support::native_fs::exists(status))) {
        error = "workspace directory is not a real directory: "
            + support::path_to_utf8(path);
        return false;
    }
    if (code && code != std::errc::no_such_file_or_directory) {
        error = "cannot inspect workspace directory: " + code.message();
        return false;
    }
    code.clear();
    if (!support::native_fs::create_directory(path, code) && code) {
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
        const auto status = support::native_fs::symlink_status(current, code);
        if (support::native_fs::is_symlink(status)) {
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

std::string_view artifact_extension(const ArtifactKind kind)
{
    switch (kind) {
    case ArtifactKind::Hdl:
        return ".fsimobj";
    case ArtifactKind::SystemCObject:
        return ".fsimscobj";
    case ArtifactKind::SystemCPlugin:
        return ".fsimscplugin";
    }
    return { };
}

// A file name stem from an identifier: [A-Za-z0-9_] stay, every other byte
// is %XX. '-' joins the parts of a secondary unit's name and '~' a collision
// counter; neither occurs in an HDL identifier.
std::string artifact_stem(const std::string_view name)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (const char character : name) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9') || byte == '_') {
            result += character;
        } else {
            result += '%';
            result += digits[byte >> 4U];
            result += digits[byte & 15U];
        }
    }
    return result;
}

std::string artifact_name(const ArtifactRecord& record)
{
    std::string name;
    if (record.kind == ArtifactKind::SystemCPlugin) {
        name = "systemc";
    } else if (record.kind == ArtifactKind::SystemCObject) {
        name = record.sources.empty()
            ? "systemc"
            : artifact_stem(support::path_to_utf8(
                  support::path_from_utf8(record.sources.front()).stem()));
    } else if (std::ranges::count_if(record.units, [](const auto& owned) {
                   return owned.unit.kind != "class";
               }) != 1 && !record.sources.empty()) {
        // A SystemVerilog compilation unit: the object that keeps shared
        // `$unit` state with every unit compiled beside it, or only `$unit`
        // classes. It is named after its first source file; "-" never occurs
        // in a unit name.
        name = artifact_stem(support::path_to_utf8(
                   support::path_from_utf8(record.sources.front()).stem()))
            + "-unit";
    } else {
        // Named after its primary design unit: the unit that is not a class
        // (classes belong to the unit that declares them).
        const auto unit = std::ranges::find_if(record.units, [](const auto& owned) {
            return owned.unit.kind != "class";
        });
        const auto& entry = unit != record.units.end() ? unit->unit
            : record.units.empty() ? library::UnitIndexEntry { } : record.units.front().unit;
        if (entry.kind == "architecture")
            name = artifact_stem(entry.primary_name) + "-" + artifact_stem(entry.name);
        else if (entry.kind == "package_body"
            || (entry.kind == "package" && !entry.primary_name.empty()))
            name = artifact_stem(entry.primary_name.empty() ? entry.name : entry.primary_name)
                + "-body";
        else
            name = artifact_stem(entry.name);
    }
    if (name.empty())
        name = "object";
    if (name.size() > 160U)
        name.resize(160U);
    // Windows reserves device names whatever their extension.
    std::string lower { name };
    std::ranges::transform(lower, lower.begin(), folded);
    const auto device = lower == "con" || lower == "prn" || lower == "aux" || lower == "nul"
        || (lower.size() == 4U && (lower.starts_with("com") || lower.starts_with("lpt"))
            && lower.back() >= '1' && lower.back() <= '9');
    return device ? "_" + name : name;
}

bool valid_artifact_id(const std::string_view id)
{
    return !id.empty() && id.size() <= 200U && id.front() != '.'
        && std::ranges::all_of(id, [](const char value) {
               return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                   || (value >= '0' && value <= '9') || value == '_' || value == '%'
                   || value == '~' || value == '-';
           });
}

bool managed_artifact_path(const ArtifactRecord& record)
{
    // One file name beside the catalog: a valid stem and the kind's
    // extension.
    const auto extension = artifact_extension(record.kind);
    const auto name = support::path_to_utf8(record.path);
    return !extension.empty() && valid_artifact_id(record.id)
        && record.path == record.path.filename() && name.size() > extension.size()
        && name.ends_with(extension)
        && valid_artifact_id(std::string_view { name }.substr(0U, name.size() - extension.size()));
}

bool managed_artifact_filename(const std::string_view name)
{
    // Pending and temporary files of an interrupted writer start with '.'.
    if (name.starts_with(".pending-") || (name.starts_with('.') && name.find(".tmp-") != std::string_view::npos))
        return true;
    for (const std::string_view extension : { ".fsimobj", ".fsimscobj", ".fsimscplugin" }) {
        if (name.size() > extension.size() && name.ends_with(extension)
            && valid_artifact_id(name.substr(0U, name.size() - extension.size())))
            return true;
    }
    return false;
}

void collect_artifacts(const std::filesystem::path& root,
    const std::set<std::string>& retained) noexcept
{
    std::error_code code;
    const auto status = support::native_fs::symlink_status(root, code);
    if (code || !support::native_fs::is_directory(status))
        return;
    std::vector<std::filesystem::path> unused;
    auto iterator = support::native_fs::directory_iterator(root, code);
    const std::filesystem::directory_iterator end;
    while (!code && iterator != end) {
        const auto filename = support::path_to_utf8(iterator->path().filename());
        if (managed_artifact_filename(filename) && !retained.contains(filename))
            unused.push_back(iterator->path());
        iterator.increment(code);
    }
    for (const auto& path : unused)
        remove_artifact(path);
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

std::string legacy_removal_advice(const std::filesystem::path& path)
{
    const auto text = support::path_to_utf8(path);
#ifdef _WIN32
    return "clear its read-only attributes (attrib -R \"" + text + "\\*\" /S /D), then delete \""
        + text + "\"";
#else
    return "make it writable (chmod -R u+w '" + text + "'), then delete it";
#endif
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

void remove_artifact(const std::filesystem::path& path) noexcept
{
    // A library still loaded on Windows keeps its file until a later
    // collection; failure here never invalidates the committed catalog.
    std::error_code code;
    support::native_fs::remove(path, code);
}

bool replace_text(const std::filesystem::path& destination,
    const std::string_view text, std::string& error)
{
    const auto temporary = destination.parent_path() / (new_revision() + ".tmp");
    auto output = support::native_fs::open_ofstream(temporary, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    std::error_code code;
    if (!output || !compiler::detail::atomic_replace_file(temporary, destination, code)) {
        error = "cannot atomically publish workspace metadata: " + code.message();
        support::native_fs::remove(temporary, code);
        return false;
    }
    return true;
}

std::optional<std::string> read_text(const std::filesystem::path& path,
    const std::size_t limit, std::string& error)
{
    std::error_code code;
    const auto status = support::native_fs::symlink_status(path, code);
    const auto size = support::native_fs::file_size(path, code);
    if (code || !support::native_fs::is_regular_file(status) || size > limit) {
        error = "workspace metadata is absent, unsafe, or exceeds its size limit";
        return std::nullopt;
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    auto input = support::native_fs::open_ifstream(path, std::ios::binary);
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
