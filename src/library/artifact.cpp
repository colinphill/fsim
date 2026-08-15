// SPDX-License-Identifier: Apache-2.0
#include "fsim/library/artifact.hpp"

#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace fsim::library {
namespace {

constexpr std::string_view kSyntaxCode = "FSIM-LIB-0001";
constexpr std::string_view kSchemaCode = "FSIM-LIB-0002";
constexpr std::string_view kValueCode = "FSIM-LIB-0003";
constexpr std::string_view kIoCode = "FSIM-LIB-0004";
constexpr std::string_view kPublishCode = "FSIM-LIB-0005";

enum class Section {
  root,
  standard,
  vhdl_package_dependency,
  source,
  unit,
  native_artifact
};

std::string trim(const std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r");
  return std::string{value.substr(first, last - first + 1)};
}

diagnostic::SourceSpan line_span(
    const std::string& source_name,
    const std::uint32_t line) {
  return {source_name, {line, 1, 0}, {line, 1, 0}};
}

void error(
    diagnostic::Engine& diagnostics,
    const std::string_view code,
    const std::string& message,
    const std::string& source_name,
    const std::uint32_t line) {
  diagnostics.error(
      std::string{code}, message, line_span(source_name, line));
}

std::optional<std::string> quoted_string(const std::string_view spelling) {
  if (spelling.size() < 2 || spelling.front() != '"'
      || spelling.back() != '"') {
    return std::nullopt;
  }
  std::string result;
  result.reserve(spelling.size() - 2);
  for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
    const char character = spelling[index];
    if (character != '\\') {
      result.push_back(character);
      continue;
    }
    if (++index + 1 >= spelling.size()) {
      return std::nullopt;
    }
    switch (spelling[index]) {
      case 'n':
        result.push_back('\n');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case 't':
        result.push_back('\t');
        break;
      case '"':
        result.push_back('"');
        break;
      case '\\':
        result.push_back('\\');
        break;
      default:
        return std::nullopt;
    }
  }
  return result;
}

std::optional<std::uint32_t> unsigned_value(
    const std::string_view spelling) {
  std::uint64_t value{};
  const auto [end, status] = std::from_chars(
      spelling.data(), spelling.data() + spelling.size(), value);
  if (status != std::errc{} || end != spelling.data() + spelling.size()
      || value > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(value);
}

std::string escape(const std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      default:
        result.push_back(character);
        break;
    }
  }
  return result;
}

bool safe_relative_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_name()
      || path.has_root_directory()) {
    return false;
  }
  return std::ranges::none_of(
      path,
      [](const auto& component) { return component == ".."; });
}

bool checksum_spelling(const std::string_view value) {
  return value.size() == 64
      && std::ranges::all_of(value, [](const unsigned char character) {
           return std::isdigit(character) != 0
               || (character >= 'a' && character <= 'f');
         });
}

bool library_name(const std::string_view value) {
  if (value.empty()
      || (std::isalpha(static_cast<unsigned char>(value.front())) == 0
          && value.front() != '_')) {
    return false;
  }
  return std::ranges::all_of(
      value,
      [](const unsigned char character) {
        return std::isalnum(character) != 0 || character == '_';
      });
}

std::filesystem::path staging_path(
    const std::filesystem::path& destination) {
  static std::atomic_uint64_t sequence{};
  const auto nonce = static_cast<std::uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  return destination.parent_path()
      / (destination.filename().string() + ".staging-"
         + std::to_string(nonce) + "-"
         + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
}

class StagingCleanup final {
 public:
  explicit StagingCleanup(std::filesystem::path path)
      : path_(std::move(path)) {}

  ~StagingCleanup() {
    if (!active_) {
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  void release() noexcept { active_ = false; }

 private:
  std::filesystem::path path_;
  bool active_{true};
};

bool write_file(
    const std::filesystem::path& path,
    const std::string_view contents,
    diagnostic::Engine& diagnostics) {
  std::error_code directory_error;
  std::filesystem::create_directories(path.parent_path(), directory_error);
  if (directory_error) {
    diagnostics.error(
        std::string{kPublishCode},
        "cannot create library artifact directory '"
            + fsim::support::path_to_utf8(path.parent_path())
            + "': " + directory_error.message());
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  output.close();
  if (!output) {
    diagnostics.error(
        std::string{kPublishCode},
        "cannot write library artifact file '"
            + fsim::support::path_to_utf8(path) + "'");
    return false;
  }
  return true;
}

bool make_tree_read_only(
    const std::filesystem::path& root,
    diagnostic::Engine& diagnostics) {
  std::error_code iteration_error;
  for (std::filesystem::recursive_directory_iterator iterator(
           root, iteration_error), end;
       !iteration_error && iterator != end;
       iterator.increment(iteration_error)) {
    std::error_code permission_error;
    const auto permissions = iterator->is_directory()
        ? std::filesystem::perms::owner_read
              | std::filesystem::perms::owner_exec
              | std::filesystem::perms::group_read
              | std::filesystem::perms::group_exec
              | std::filesystem::perms::others_read
              | std::filesystem::perms::others_exec
        : std::filesystem::perms::owner_read
              | std::filesystem::perms::group_read
              | std::filesystem::perms::others_read;
    std::filesystem::permissions(
        iterator->path(), permissions,
        std::filesystem::perm_options::replace, permission_error);
    if (permission_error) {
      diagnostics.error(
          std::string{kPublishCode},
          "cannot make library artifact read-only: "
              + fsim::support::path_to_utf8(iterator->path())
              + ": " + permission_error.message());
      return false;
    }
  }
  if (iteration_error) {
    diagnostics.error(
        std::string{kPublishCode},
        "cannot enumerate staged library artifact: "
            + iteration_error.message());
    return false;
  }
  std::error_code root_error;
  std::filesystem::permissions(
      root,
      std::filesystem::perms::owner_read
          | std::filesystem::perms::owner_exec
          | std::filesystem::perms::group_read
          | std::filesystem::perms::group_exec
          | std::filesystem::perms::others_read
          | std::filesystem::perms::others_exec,
      std::filesystem::perm_options::replace,
      root_error);
  if (root_error) {
    diagnostics.error(
        std::string{kPublishCode},
        "cannot make staged library directory read-only: "
            + root_error.message());
    return false;
  }
  return true;
}

}  // namespace

std::string serialize_metadata(const Metadata& metadata) {
  std::ostringstream output;
  output << "format = " << metadata.format << '\n'
         << "library = \"" << escape(metadata.library) << "\"\n"
         << "producer = \"" << escape(metadata.producer) << "\"\n"
         << "runtime_schema = " << metadata.runtime_schema << '\n'
         << "portable_schema = " << metadata.portable_schema << '\n'
         << "trace_archive = \"" << escape(metadata.trace_archive) << "\"\n";
  for (const auto& dependency : metadata.dependencies) {
    output << "\n[[dependency]]\n"
           << "library = \"" << escape(dependency) << "\"\n";
  }
  for (const auto& standard : metadata.standards) {
    output << "\n[[standard]]\n"
           << "language = \"" << escape(standard.language) << "\"\n"
           << "revision = \"" << escape(standard.revision) << "\"\n";
  }
  for (const auto& dependency : metadata.vhdl_package_dependencies) {
    output << "\n[[vhdl_package_dependency]]\n"
           << "standard = \"" << escape(dependency.standard) << "\"\n"
           << "predefined_environment = \""
           << escape(dependency.predefined_environment) << "\"\n"
           << "package = \"" << escape(dependency.package) << "\"\n"
           << "revision = \"" << escape(dependency.revision) << "\"\n"
           << "source_digest = \"" << escape(dependency.source_digest)
           << "\"\n";
  }
  for (const auto& source : metadata.sources) {
    output << "\n[[source]]\n"
           << "logical_name = \"" << escape(source.logical_name) << "\"\n";
    if (!source.language.empty()) {
      output << "language = \"" << escape(source.language) << "\"\n";
    }
    if (!source.standard.empty()) {
      output << "standard = \"" << escape(source.standard) << "\"\n"
             << "compatibility_profile = \""
             << escape(source.compatibility_profile) << "\"\n";
    }
    if (!source.artifact.empty()) {
      output << "artifact = \""
             << escape(fsim::support::path_to_utf8(source.artifact))
             << "\"\n"
             << "checksum = \"" << escape(source.checksum) << "\"\n";
    }
  }
  for (const auto& unit : metadata.units) {
    output << "\n[[unit]]\n"
           << "language = \"" << escape(unit.language) << "\"\n"
           << "kind = \"" << escape(unit.kind) << "\"\n"
           << "name = \"" << escape(unit.name) << "\"\n";
    if (!unit.primary_name.empty()) {
      output << "primary_name = \"" << escape(unit.primary_name) << "\"\n";
    }
    if (!unit.architecture.empty()) {
      output << "architecture = \"" << escape(unit.architecture) << "\"\n";
    }
    output << "artifact = \""
           << escape(fsim::support::path_to_utf8(unit.artifact)) << "\"\n"
           << "checksum = \"" << escape(unit.checksum) << "\"\n"
           << "standard = \"" << escape(unit.standard) << "\"\n"
           << "compatibility_profile = \""
           << escape(unit.compatibility_profile) << "\"\n";
  }
  for (const auto& native : metadata.native_artifacts) {
    output << "\n[[native]]\n"
           << "kind = \"" << escape(native.kind) << "\"\n"
           << "artifact = \""
           << escape(fsim::support::path_to_utf8(native.artifact)) << "\"\n"
           << "checksum = \"" << escape(native.checksum) << "\"\n"
           << "runtime_abi = " << native.runtime_abi << '\n';
    if (native.systemc_abi != 0) {
      output << "systemc_abi = " << native.systemc_abi << '\n';
    }
    const auto put = [&](const std::string_view key, const std::string& value) {
      if (!value.empty()) {
        output << key << " = \"" << escape(value) << "\"\n";
      }
    };
    put("compiler_fingerprint", native.compiler_fingerprint);
    put("llvm_version", native.llvm_version);
    put("target", native.target);
    put("data_layout", native.data_layout);
    put("cpu", native.cpu);
    put("features", native.features);
    put("optimization", native.optimization);
    put("cache_key", native.cache_key);
  }
  return output.str();
}

std::optional<Metadata> parse_metadata(
    const std::string_view source,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  Metadata metadata;
  metadata.format = 0;
  metadata.portable_schema = 0;
  Section section = Section::root;
  std::string* dependency = nullptr;
  std::unordered_set<std::string> root_keys;
  std::unordered_set<std::string> section_keys;
  std::istringstream input{std::string{source}};
  std::string line_text;
  std::uint32_t line_number = 0;
  while (std::getline(input, line_text)) {
    ++line_number;
    auto line = trim(line_text);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const auto comment = line.find('#');
    if (comment != std::string::npos) {
      line = trim(std::string_view{line}.substr(0, comment));
    }
    if (line == "[[dependency]]") {
      metadata.dependencies.emplace_back();
      dependency = &metadata.dependencies.back();
      section = Section::root;
      section_keys.clear();
      section_keys.insert("@dependency");
      continue;
    }
    if (line == "[[standard]]") {
      metadata.standards.emplace_back();
      dependency = nullptr;
      section = Section::standard;
      section_keys.clear();
      continue;
    }
    if (line == "[[vhdl_package_dependency]]") {
      metadata.vhdl_package_dependencies.emplace_back();
      dependency = nullptr;
      section = Section::vhdl_package_dependency;
      section_keys.clear();
      continue;
    }
    if (line == "[[source]]") {
      metadata.sources.emplace_back();
      dependency = nullptr;
      section = Section::source;
      section_keys.clear();
      continue;
    }
    if (line == "[[unit]]") {
      metadata.units.emplace_back();
      dependency = nullptr;
      section = Section::unit;
      section_keys.clear();
      continue;
    }
    if (line == "[[native]]") {
      metadata.native_artifacts.emplace_back();
      dependency = nullptr;
      section = Section::native_artifact;
      section_keys.clear();
      continue;
    }
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
      error(
          diagnostics, kSyntaxCode, "expected a key/value assignment",
          source_name, line_number);
      continue;
    }
    const auto key = trim(std::string_view{line}.substr(0, equals));
    const auto value = trim(std::string_view{line}.substr(equals + 1));
    auto& keys = section == Section::root && dependency == nullptr
        ? root_keys : section_keys;
    if (!keys.insert(key).second) {
      error(
          diagnostics, kSyntaxCode,
          "duplicate metadata key '" + key + "'", source_name, line_number);
      continue;
    }
    const auto text = quoted_string(value);
    if (dependency != nullptr) {
      if (key != "library" || !text.has_value()) {
        error(
            diagnostics, kSyntaxCode,
            "[[dependency]] accepts one quoted 'library' key",
            source_name, line_number);
      } else {
        *dependency = *text;
      }
      continue;
    }
    if (section == Section::root) {
      if (key == "format" || key == "runtime_schema"
          || key == "portable_schema") {
        const auto number = unsigned_value(value);
        if (!number.has_value()) {
          error(
              diagnostics, kSyntaxCode,
              "metadata key '" + key + "' requires an unsigned integer",
              source_name, line_number);
        } else if (key == "format") {
          metadata.format = *number;
        } else if (key == "runtime_schema") {
          metadata.runtime_schema = *number;
        } else {
          metadata.portable_schema = *number;
        }
      } else if ((key == "library" || key == "producer"
                     || key == "trace_archive")
                 && text.has_value()) {
        if (key == "library")
          metadata.library = *text;
        else if (key == "producer")
          metadata.producer = *text;
        else
          metadata.trace_archive = *text;
      } else {
        error(
            diagnostics, kSyntaxCode,
            "unknown or ill-typed root metadata key '" + key + "'",
            source_name, line_number);
      }
      continue;
    }
    if (section == Section::native_artifact
        && (key == "runtime_abi" || key == "systemc_abi")) {
      const auto number = unsigned_value(value);
      if (!number.has_value()) {
        error(
            diagnostics, kSyntaxCode,
            "metadata key '" + key + "' requires an unsigned integer",
            source_name, line_number);
      } else if (key == "runtime_abi") {
        metadata.native_artifacts.back().runtime_abi = *number;
      } else {
        metadata.native_artifacts.back().systemc_abi = *number;
      }
      continue;
    }
    if (!text.has_value()) {
      error(
          diagnostics, kSyntaxCode,
          "metadata key '" + key + "' requires a quoted string",
          source_name, line_number);
      continue;
    }
    if (section == Section::standard) {
      auto& standard = metadata.standards.back();
      if (key == "language") {
        standard.language = *text;
      } else if (key == "revision") {
        standard.revision = *text;
      } else {
        error(
            diagnostics, kSyntaxCode,
            "unknown [[standard]] key '" + key + "'",
            source_name, line_number);
      }
      continue;
    }
    if (section == Section::vhdl_package_dependency) {
      auto& package_dependency = metadata.vhdl_package_dependencies.back();
      if (key == "standard") {
        package_dependency.standard = *text;
      } else if (key == "predefined_environment") {
        package_dependency.predefined_environment = *text;
      } else if (key == "package") {
        package_dependency.package = *text;
      } else if (key == "revision") {
        package_dependency.revision = *text;
      } else if (key == "source_digest") {
        package_dependency.source_digest = *text;
      } else {
        error(
            diagnostics, kSyntaxCode,
            "unknown [[vhdl_package_dependency]] key '" + key + "'",
            source_name, line_number);
      }
      continue;
    }
    if (section == Section::source) {
      auto& source_entry = metadata.sources.back();
      if (key == "logical_name") {
        source_entry.logical_name = *text;
      } else if (key == "artifact") {
        source_entry.artifact = fsim::support::path_from_utf8(*text);
      } else if (key == "checksum") {
        source_entry.checksum = *text;
      } else if (key == "language") {
        source_entry.language = *text;
      } else if (key == "standard") {
        source_entry.standard = *text;
      } else if (key == "compatibility_profile") {
        source_entry.compatibility_profile = *text;
      } else {
        error(
            diagnostics, kSyntaxCode,
            "unknown [[source]] key '" + key + "'",
            source_name, line_number);
      }
      continue;
    }
    if (section == Section::native_artifact) {
      auto& native = metadata.native_artifacts.back();
      if (key == "kind") {
        native.kind = *text;
      } else if (key == "artifact") {
        native.artifact = fsim::support::path_from_utf8(*text);
      } else if (key == "checksum") {
        native.checksum = *text;
      } else if (key == "compiler_fingerprint") {
        native.compiler_fingerprint = *text;
      } else if (key == "llvm_version") {
        native.llvm_version = *text;
      } else if (key == "target") {
        native.target = *text;
      } else if (key == "data_layout") {
        native.data_layout = *text;
      } else if (key == "cpu") {
        native.cpu = *text;
      } else if (key == "features") {
        native.features = *text;
      } else if (key == "optimization") {
        native.optimization = *text;
      } else if (key == "cache_key") {
        native.cache_key = *text;
      } else {
        error(
            diagnostics, kSyntaxCode,
            "unknown [[native]] key '" + key + "'",
            source_name, line_number);
      }
      continue;
    }
    auto& unit = metadata.units.back();
    if (key == "language") {
      unit.language = *text;
    } else if (key == "kind") {
      unit.kind = *text;
    } else if (key == "name") {
      unit.name = *text;
    } else if (key == "primary_name") {
      unit.primary_name = *text;
    } else if (key == "architecture") {
      unit.architecture = *text;
    } else if (key == "artifact") {
      unit.artifact = fsim::support::path_from_utf8(*text);
    } else if (key == "checksum") {
      unit.checksum = *text;
    } else if (key == "standard") {
      unit.standard = *text;
    } else if (key == "compatibility_profile") {
      unit.compatibility_profile = *text;
    } else {
      error(
          diagnostics, kSyntaxCode,
          "unknown [[unit]] key '" + key + "'", source_name, line_number);
    }
  }

  const auto document_line = std::max<std::uint32_t>(line_number, 1);
  if (metadata.format != kFormatVersion) {
    error(
        diagnostics, kSchemaCode,
        "unsupported .fsimlib format " + std::to_string(metadata.format)
            + "; this build supports format "
            + std::to_string(kFormatVersion),
        source_name, document_line);
  }
  if (metadata.portable_schema != kPortableSchemaVersion) {
    error(
        diagnostics, kSchemaCode,
        "unsupported portable-unit schema "
            + std::to_string(metadata.portable_schema)
            + "; this build supports schema "
            + std::to_string(kPortableSchemaVersion),
        source_name, document_line);
  }
  if ((metadata.trace_archive.size() % 2U) != 0U
      || !std::ranges::all_of(metadata.trace_archive, [](const char value) {
           return (value >= '0' && value <= '9')
               || (value >= 'a' && value <= 'f');
         })) {
    error(diagnostics, kValueCode,
        "trace_archive must be empty or lowercase hexadecimal",
        source_name, document_line);
  }
  if (!library_name(metadata.library)) {
    error(
        diagnostics, kValueCode,
        "metadata requires a safe non-empty logical library name",
        source_name, document_line);
  }
  if (metadata.producer.empty() || metadata.runtime_schema == 0) {
    error(
        diagnostics, kValueCode,
        "metadata requires non-empty producer and nonzero runtime_schema values",
        source_name, document_line);
  }
  std::unordered_set<std::string> dependencies;
  for (const auto& item : metadata.dependencies) {
    if (!library_name(item) || !dependencies.insert(item).second
        || item == metadata.library) {
      error(
          diagnostics, kValueCode,
          "dependencies must be unique safe logical libraries distinct from the artifact library",
          source_name, document_line);
    }
  }
  for (const auto& standard : metadata.standards) {
    if (standard.language.empty() || standard.revision.empty()) {
      error(
          diagnostics, kValueCode,
          "every [[standard]] requires language and revision",
          source_name, document_line);
    }
  }
  std::unordered_set<std::string> vhdl_packages;
  for (const auto& item : metadata.vhdl_package_dependencies) {
    const bool known_standard = std::ranges::any_of(
        metadata.standards,
        [&](const LanguageStandard& standard) {
          return standard.language == "vhdl"
              && standard.revision == item.standard;
        });
    if (!known_standard || item.predefined_environment.empty()
        || item.package.empty() || item.revision.empty()
        || !checksum_spelling(item.source_digest)
        || !vhdl_packages.insert(item.package).second) {
      error(
          diagnostics, kValueCode,
          "every [[vhdl_package_dependency]] requires a unique package, a "
          "selected VHDL standard, predefined-environment and revision "
          "identities, and a lowercase SHA-256 source digest",
          source_name, document_line);
    }
  }
  std::unordered_set<std::string> source_names;
  std::unordered_set<std::string> payload_paths;
  for (const auto& source_entry : metadata.sources) {
    const bool no_text = source_entry.artifact.empty()
        && source_entry.checksum.empty();
    const bool with_text = safe_relative_path(source_entry.artifact)
        && checksum_spelling(source_entry.checksum);
    const bool known_language = source_entry.language.empty()
        || source_entry.language == "vhdl"
        || source_entry.language == "verilog"
        || source_entry.language == "systemverilog"
        || source_entry.language == "systemc";
    const bool known_standard = source_entry.language == "systemc"
        ? source_entry.standard.empty()
        : std::ranges::any_of(
              metadata.standards, [&](const LanguageStandard& standard) {
                return standard.language == source_entry.language
                    && standard.revision == source_entry.standard;
              });
    const bool unique_payload = source_entry.artifact.empty()
        || payload_paths.insert(
            fsim::support::path_to_utf8(source_entry.artifact)).second;
    if (source_entry.logical_name.empty()
        || !source_names.insert(source_entry.logical_name).second
        || (!no_text && !with_text) || !known_language || !known_standard
        || (source_entry.language != "systemc"
            && (source_entry.compatibility_profile.empty()
                || (source_entry.language == "vhdl"
                    && source_entry.compatibility_profile == "none")))
        || !unique_payload) {
      error(
          diagnostics, kValueCode,
          "every [[source]] requires a unique logical_name, matching standard "
          "and compatibility profile, and either no payload or a contained "
          "artifact with lowercase SHA-256 checksum",
          source_name, document_line);
    }
  }
  for (const auto& unit : metadata.units) {
    const bool known_standard = std::ranges::any_of(
        metadata.standards, [&](const LanguageStandard& standard) {
          return standard.language == unit.language
              && standard.revision == unit.standard;
        });
    if (unit.language.empty() || unit.kind.empty() || unit.name.empty()
        || !safe_relative_path(unit.artifact)
        || !checksum_spelling(unit.checksum) || !known_standard
        || unit.compatibility_profile.empty()
        || (unit.language == "vhdl"
            && unit.compatibility_profile == "none")) {
      error(
          diagnostics, kValueCode,
          "every [[unit]] requires language, selected standard, compatibility "
          "profile, kind, name, a contained relative artifact path, and a "
          "lowercase SHA-256 checksum",
          source_name, document_line);
    }
  }
  for (const auto& unit : metadata.units) {
    if (!payload_paths.insert(
            fsim::support::path_to_utf8(unit.artifact)).second) {
      error(
          diagnostics, kValueCode,
          "artifact payload paths must be unique across every index",
          source_name, document_line);
    }
  }
  for (const auto& native : metadata.native_artifacts) {
    const bool common = safe_relative_path(native.artifact)
        && checksum_spelling(native.checksum)
        && native.runtime_abi != 0 && !native.target.empty()
        && !native.cpu.empty();
    const bool systemc = native.kind == "systemc_plugin"
        && native.systemc_abi != 0
        && !native.compiler_fingerprint.empty()
        && native.llvm_version.empty() && native.optimization.empty()
        && native.cache_key.empty();
    const bool llvm = native.kind == "llvm_object"
        && native.systemc_abi == 0 && native.compiler_fingerprint.empty()
        && !native.llvm_version.empty() && !native.data_layout.empty()
        && (native.optimization == "O0" || native.optimization == "O2")
        && checksum_spelling(native.cache_key);
    if (!common || (!systemc && !llvm)
        || !payload_paths.insert(
            fsim::support::path_to_utf8(native.artifact)).second) {
      error(
          diagnostics, kValueCode,
          "every [[native]] requires a unique contained payload, checksum, exact host identity, and kind-specific compatibility fields",
          source_name, document_line);
    }
  }
  if (metadata.units.empty() && metadata.sources.empty()) {
    error(
        diagnostics, kValueCode,
        "metadata must index at least one portable unit or source payload",
        source_name, document_line);
  }
  return diagnostics.has_error()
      ? std::nullopt : std::optional{std::move(metadata)};
}

std::optional<Metadata> load_metadata(
    const std::filesystem::path& directory,
    const std::string_view expected_library,
    diagnostic::Engine& diagnostics) {
  const auto path = directory / kMetadataFilename;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.error(
        std::string{kIoCode},
        "cannot open mapped library metadata: "
            + fsim::support::path_to_utf8(path));
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    diagnostics.error(
        std::string{kIoCode},
        "cannot read mapped library metadata: "
            + fsim::support::path_to_utf8(path));
    return std::nullopt;
  }
  auto metadata = parse_metadata(
      contents.str(), fsim::support::path_to_utf8(path), diagnostics);
  if (metadata.has_value() && metadata->library != expected_library) {
    diagnostics.error(
        std::string{kValueCode},
        "mapped logical library '" + std::string{expected_library}
            + "' contains metadata for '" + metadata->library + "'");
    return std::nullopt;
  }
  return metadata;
}

bool publish(
    const std::filesystem::path& destination,
    const Metadata& metadata,
    const std::vector<PortablePayload>& payloads,
    diagnostic::Engine& diagnostics) {
  if (destination.empty()) {
    diagnostics.error(
        std::string{kPublishCode},
        "library export destination must not be empty");
    return false;
  }
  std::error_code exists_error;
  if (std::filesystem::exists(destination, exists_error) || exists_error) {
    diagnostics.error(
        std::string{kPublishCode},
        "library export destination already exists or cannot be inspected: "
            + fsim::support::path_to_utf8(destination));
    return false;
  }

  diagnostic::Engine metadata_diagnostics;
  const auto canonical = serialize_metadata(metadata);
  if (!parse_metadata(
          canonical, std::string{kMetadataFilename}, metadata_diagnostics)) {
    for (const auto& item : metadata_diagnostics.diagnostics()) {
      diagnostics.error(
          std::string{kPublishCode},
          "invalid library export metadata: " + item.message);
    }
    return false;
  }

  std::unordered_set<std::string> expected;
  std::unordered_map<std::string, std::string> expected_checksums;
  for (const auto& unit : metadata.units) {
    const auto path = fsim::support::path_to_utf8(unit.artifact);
    expected.insert(path);
    expected_checksums.emplace(path, unit.checksum);
  }
  for (const auto& source_entry : metadata.sources) {
    if (source_entry.artifact.empty()) {
      continue;
    }
    const auto path = fsim::support::path_to_utf8(source_entry.artifact);
    expected.insert(path);
    expected_checksums.emplace(path, source_entry.checksum);
  }
  for (const auto& native : metadata.native_artifacts) {
    const auto path = fsim::support::path_to_utf8(native.artifact);
    expected.insert(path);
    expected_checksums.emplace(path, native.checksum);
  }
  std::unordered_set<std::string> supplied;
  for (const auto& payload : payloads) {
    const auto path = fsim::support::path_to_utf8(payload.path);
    if (!safe_relative_path(payload.path) || !expected.contains(path)
        || !supplied.insert(path).second) {
      diagnostics.error(
          std::string{kPublishCode},
          "portable payload '" + path
              + "' is unsafe, duplicate, or absent from the unit index");
      return false;
    }
    const auto checksum = support::Sha256::hex(
        support::Sha256::digest(payload.bytes));
    const auto expected_checksum = expected_checksums.find(path);
    if (expected_checksum == expected_checksums.end()
        || expected_checksum->second != checksum) {
      diagnostics.error(
          std::string{kPublishCode},
          "portable payload checksum does not match metadata for '"
              + path + "'");
      return false;
    }
  }
  if (supplied.size() != expected.size()) {
    diagnostics.error(
        std::string{kPublishCode},
        "portable payload set does not cover every indexed unit");
    return false;
  }

  std::error_code parent_error;
  const auto parent = destination.parent_path().empty()
      ? std::filesystem::path{"."} : destination.parent_path();
  std::filesystem::create_directories(parent, parent_error);
  if (parent_error) {
    diagnostics.error(
        std::string{kPublishCode},
        "cannot create library export parent directory: "
            + parent_error.message());
    return false;
  }
  const auto staging = staging_path(destination);
  StagingCleanup cleanup(staging);
  std::error_code stage_error;
  if (!std::filesystem::create_directory(staging, stage_error)
      || stage_error) {
    diagnostics.error(
        std::string{kPublishCode},
        "cannot create library staging directory: "
            + stage_error.message());
    return false;
  }
  for (const auto& payload : payloads) {
    if (!write_file(staging / payload.path, payload.bytes, diagnostics)) {
      return false;
    }
  }
  if (!write_file(
          staging / kMetadataFilename, canonical, diagnostics)
      || !make_tree_read_only(staging, diagnostics)) {
    return false;
  }
  std::error_code install_error;
  std::filesystem::rename(staging, destination, install_error);
  if (install_error) {
    diagnostics.error(
        std::string{kPublishCode},
        "cannot atomically install library artifact: "
            + install_error.message());
    return false;
  }
  cleanup.release();
  return true;
}

}  // namespace fsim::library
