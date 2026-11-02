// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_internal.hpp"

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <sstream>

namespace fsim::app::workspace {
namespace {

std::string_view trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return { };
    value.remove_prefix(first);
    value.remove_suffix(value.size() - value.find_last_not_of(" \t\r\n") - 1U);
    return value;
}

std::optional<std::string> parse_mapping_path(std::string_view value)
{
    value = trim(value);
    if (value.empty() || (value.front() != '"' && value.front() != '\''))
        return std::nullopt;
    const char quote = value.front();
    std::string result;
    for (std::size_t index = 1U; index < value.size(); ++index) {
        char character = value[index];
        if (character == quote) {
            const auto remainder = trim(value.substr(index + 1U));
            if ((!remainder.empty() && remainder.front() != '#') || result.empty())
                return std::nullopt;
            return result;
        }
        if (character == '\\' && quote == '"') {
            if (++index == value.size())
                return std::nullopt;
            character = value[index];
            if (character != '\\' && character != '"')
                return std::nullopt;
        }
        if (static_cast<unsigned char>(character) < 32U)
            return std::nullopt;
        result += character;
    }
    return std::nullopt;
}

std::string quote_mapping_path(const std::string_view value)
{
    std::string result { "\"" };
    for (const char character : value) {
        if (character == '\\' || character == '"')
            result += '\\';
        result += character;
    }
    return result + '"';
}

std::string exported_library_error(
    const std::string_view name, const std::filesystem::path& path)
{
    return "library '" + std::string { name }
        + "' is mapped to the exported library " + support::path_to_utf8(path)
        + "; it holds no workspace objects and takes no compiles";
}

bool ensure_workspace(const Store& store, std::string& error)
{
    return detail::ensure_directory(store.managed_directory(), error)
        && detail::ensure_directory(store.managed_directory() / "libraries", error);
}

} // namespace

namespace detail {

std::optional<MappingFile> read_mapping_file(const Store& store, std::string& error)
{
    const auto path = store.managed_directory() / "libraries.toml";
    if (!safe_descendant(store.root(), ".fsim/libraries.toml", error))
        return std::nullopt;
    std::error_code code;
    if (!support::native_fs::exists(path, code) && !code)
        return MappingFile { };
    const auto text = read_text(path, kMaximumTextBytes, error);
    if (!text)
        return std::nullopt;
    MappingFile result;
    LibraryMappings* section = nullptr;
    std::set<std::string, std::less<>> seen_sections;
    std::istringstream input { *text };
    for (std::string line; std::getline(input, line);) {
        const auto value = trim(line);
        if (value.empty() || value.front() == '#')
            continue;
        if (value.front() == '[') {
            const auto close = value.find(']');
            const auto header = close == std::string_view::npos
                ? std::string_view { } : value.substr(0U, close + 1U);
            const auto remainder = close == std::string_view::npos
                ? std::string_view { } : trim(value.substr(close + 1U));
            LibraryMappings* next = header == "[libraries]" ? &result.libraries
                : header == "[exported]"                     ? &result.exported
                                                             : nullptr;
            if (next == nullptr || !seen_sections.emplace(header).second
                || (!remainder.empty() && remainder.front() != '#')) {
                error = "invalid or repeated mapping section in .fsim/libraries.toml";
                return std::nullopt;
            }
            section = next;
            continue;
        }
        const auto equal = value.find('=');
        const auto name = trim(value.substr(0U, equal));
        const auto mapping = equal == std::string_view::npos
            ? std::nullopt : parse_mapping_path(value.substr(equal + 1U));
        if (section == nullptr || !valid_name(name) || !mapping
            || result.libraries.size() + result.exported.size() >= kMaximumRecords
            || result.libraries.contains(std::string { name })
            || result.exported.contains(std::string { name })
            || !section->emplace(name, support::path_from_utf8(*mapping)).second) {
            error = "invalid or duplicate library mapping in .fsim/libraries.toml";
            return std::nullopt;
        }
    }
    if (!seen_sections.contains("[libraries]") && !text->empty()) {
        error = "library mapping file requires a [libraries] section";
        return std::nullopt;
    }
    return result;
}

bool write_mapping_file(const Store& store, const MappingFile& file,
    std::string& error)
{
    std::string text { "[libraries]\n" };
    for (const auto& [name, path] : file.libraries)
        text += name + " = " + quote_mapping_path(support::path_to_utf8(path)) + '\n';
    if (!file.exported.empty()) {
        text += "[exported]\n";
        for (const auto& [name, path] : file.exported)
            text += name + " = " + quote_mapping_path(support::path_to_utf8(path)) + '\n';
    }
    return replace_text(store.managed_directory() / "libraries.toml", text, error);
}

std::optional<LibraryMappings> read_mappings(const Store& store, std::string& error)
{
    auto file = read_mapping_file(store, error);
    if (!file)
        return std::nullopt;
    return std::move(file->libraries);
}

bool write_mappings(const Store& store, const LibraryMappings& mappings,
    std::string& error)
{
    auto file = read_mapping_file(store, error);
    if (!file)
        return false;
    file->libraries = mappings;
    return write_mapping_file(store, *file, error);
}

std::optional<LibraryLocation> locate_library(const Store& store,
    const std::string_view name, const bool create, std::string& error)
{
    if (!valid_name(name)) {
        error = "invalid logical library name: " + std::string { name };
        return std::nullopt;
    }
    const auto file = read_mapping_file(store, error);
    if (!file)
        return std::nullopt;
    if (const auto exported = file->exported.find(std::string { name });
        exported != file->exported.end()) {
        error = exported_library_error(name, exported->second);
        return std::nullopt;
    }
    const auto* mappings = &file->libraries;
    LibraryLocation location { std::string { name },
        store.managed_directory() / "libraries" / name, false };
    const auto mapped = mappings->find(std::string { name });
    if (mapped != mappings->end()) {
        location.directory = mapped->second.is_absolute()
            ? mapped->second : store.root() / mapped->second;
        std::error_code code;
        location.directory = support::native_fs::canonical(location.directory, code);
        if (code) {
            error = "mapped library directory cannot be resolved: " + code.message();
            return std::nullopt;
        }
        location.mapped = true;
    } else {
        if (create && (!ensure_workspace(store, error)
                          || !ensure_directory(location.directory, error)))
            return std::nullopt;
        if (!safe_descendant(store.root(),
                std::filesystem::path { ".fsim/libraries" } / name, error))
            return std::nullopt;
    }
    std::error_code code;
    if (!support::native_fs::is_directory(location.directory, code) || code) {
        error = "library directory does not exist: " + std::string { name };
        return std::nullopt;
    }
    return location;
}

} // namespace detail

Store::Store(std::filesystem::path workspace)
    : root_(std::filesystem::absolute(std::move(workspace)).lexically_normal())
{
}

const std::filesystem::path& Store::root() const
{
    return root_;
}

std::filesystem::path Store::managed_directory() const
{
    return root_ / ".fsim";
}

std::optional<std::vector<LibraryLocation>> Store::library_locations(
    std::string& error) const
{
    error.clear();
    const auto mappings = detail::read_mappings(*this, error);
    if (!mappings)
        return std::nullopt;
    std::set<std::string> names;
    for (const auto& [name, unused] : *mappings) {
        (void)unused;
        names.insert(name);
    }
    if (!detail::safe_descendant(root_, ".fsim/libraries", error))
        return std::nullopt;
    std::error_code code;
    const auto local = managed_directory() / "libraries";
    if (support::native_fs::exists(local, code)) {
        auto iterator = support::native_fs::directory_iterator(local, code);
        const std::filesystem::directory_iterator end;
        while (!code && iterator != end) {
            const auto name = support::path_to_utf8(iterator->path().filename());
            const auto status = iterator->symlink_status(code);
            if (!code && valid_name(name) && support::native_fs::is_symlink(status)) {
                error = "local library directory must not be a symbolic link";
                return std::nullopt;
            }
            if (!code && valid_name(name) && support::native_fs::is_directory(status)
                && support::native_fs::exists(iterator->path() / "library.sqlite3", code))
                names.insert(name);
            if (!code)
                iterator.increment(code);
        }
    }
    if (code) {
        error = "cannot enumerate workspace libraries: " + code.message();
        return std::nullopt;
    }
    std::vector<LibraryLocation> result;
    for (const auto& name : names) {
        auto location = detail::locate_library(*this, name, false, error);
        if (!location
            || !detail::load_catalog(*location, false,
                detail::CatalogAccess::read_only, error))
            return std::nullopt;
        result.push_back(std::move(*location));
    }
    return result;
}

std::optional<LibraryCatalog> Store::read_library(
    const std::string_view name, std::string& error) const
{
    error.clear();
    if (!valid_name(name)) {
        error = "invalid logical library name";
        return std::nullopt;
    }
    const auto file = detail::read_mapping_file(*this, error);
    if (!file)
        return std::nullopt;
    if (const auto exported = file->exported.find(std::string { name });
        exported != file->exported.end()) {
        error = exported_library_error(name, exported->second);
        return std::nullopt;
    }
    const auto* mappings = &file->libraries;
    const auto local = managed_directory() / "libraries" / name;
    std::error_code code;
    if (!mappings->contains(std::string { name })
        && !support::native_fs::exists(local / "library.sqlite3", code) && !code) {
        if (!detail::safe_descendant(root_,
                std::filesystem::path { ".fsim/libraries" } / name, error))
            return std::nullopt;
        return LibraryCatalog { { std::string { name }, local, false }, { } };
    }
    const auto location = detail::locate_library(*this, name, false, error);
    if (!location)
        return std::nullopt;
    return detail::load_catalog(*location, false, detail::CatalogAccess::read_only, error);
}

std::optional<std::filesystem::path> Store::artifact_path(
    const LibraryLocation& location, const ArtifactRecord& record,
    std::string& error) const
{
    error.clear();
    if (!detail::managed_artifact_path(record)
        || !detail::safe_descendant(location.directory, record.path, error)) {
        if (error.empty())
            error = "catalog artifact path is not an fsim-managed revision";
        return std::nullopt;
    }
    return location.directory / record.path;
}

bool Store::map_library(const std::string_view name,
    const std::filesystem::path& path, std::string& error) const
{
    error.clear();
    if (!valid_name(name) || path.empty()) {
        error = "library mapping requires a valid name and a directory or .fsimlib file";
        return false;
    }
    if (!ensure_workspace(*this, error))
        return false;
    auto lock = detail::lock_directory(managed_directory() / ".mapping-lock", error);
    auto file = detail::read_mapping_file(*this, error);
    if (!lock || !file)
        return false;
    std::error_code code;
    const auto target = support::native_fs::canonical(
        path.is_absolute() ? path : root_ / path, code);
    if (code) {
        error = "mapped library path cannot be resolved: " + code.message();
        return false;
    }
    const auto local = managed_directory() / "libraries" / name;
    const std::string key { name };
    if (support::native_fs::is_regular_file(target, code)) {
        // An exported library is read in place: its metadata must name this
        // logical library, and no local library may share the name.
        if (support::native_fs::exists(local, code)) {
            error = "library mapping conflicts with an existing local library";
            return false;
        }
        diagnostic::Engine diagnostics;
        if (!library::load_metadata(target, name, diagnostics)) {
            error = diagnostics.diagnostics().empty()
                ? "mapped file is not an exported library: "
                    + support::path_to_utf8(target)
                : diagnostics.diagnostics().front().message;
            return false;
        }
        file->libraries.erase(key);
        file->exported[key] = path.lexically_normal();
        return detail::write_mapping_file(*this, *file, error);
    }
    if (support::native_fs::exists(local, code)
        && !support::native_fs::equivalent(local, target, code)) {
        error = "library mapping conflicts with an existing local library";
        return false;
    }
    // A mapping aliases a physical location, never its logical library name.
    // A directory without a catalog becomes an empty library of this name, so
    // a library is mapped before anything is compiled into it.
    auto writer = detail::lock_directory(target / ".writer-lock", error);
    if (!writer
        || !detail::load_catalog({ key, target, true }, true,
            detail::CatalogAccess::read_only, error))
        return false;
    writer.reset();
    file->exported.erase(key);
    file->libraries[key] = path.lexically_normal();
    return detail::write_mapping_file(*this, *file, error);
}

std::optional<std::vector<ExportedLibrary>> Store::exported_libraries(
    std::string& error) const
{
    error.clear();
    const auto file = detail::read_mapping_file(*this, error);
    if (!file)
        return std::nullopt;
    std::vector<ExportedLibrary> result;
    for (const auto& [name, path] : file->exported)
        result.push_back({ name, (path.is_absolute() ? path : root_ / path).lexically_normal() });
    return result;
}

bool Store::unmap_library(const std::string_view name, std::string& error) const
{
    error.clear();
    if (!valid_name(name) || !ensure_workspace(*this, error)) {
        if (error.empty())
            error = "invalid logical library name";
        return false;
    }
    auto lock = detail::lock_directory(managed_directory() / ".mapping-lock", error);
    auto file = detail::read_mapping_file(*this, error);
    if (!lock || !file)
        return false;
    if (file->libraries.erase(std::string { name }) == 0U
        && file->exported.erase(std::string { name }) == 0U) {
        error = "library has no external mapping: " + std::string { name };
        return false;
    }
    return detail::write_mapping_file(*this, *file, error);
}

} // namespace fsim::app::workspace
