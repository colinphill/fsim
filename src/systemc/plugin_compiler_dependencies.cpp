// SPDX-License-Identifier: Apache-2.0
#include "plugin_compiler_internal.hpp"

namespace fsim::systemc::plugin_detail {

[[nodiscard]] bool is_clang_cl_compiler(
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler) {
    const auto matches = [](const std::filesystem::path& candidate) {
        const auto filename = lowercase(candidate.filename().string());
        return filename == "clang-cl" || filename == "clang-cl.exe";
    };
    return matches(std::filesystem::path{compiler_name})
        || (!resolved_compiler.empty() && matches(resolved_compiler));
}

[[nodiscard]] std::uint64_t dependency_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

DependencyScratchDirectory::DependencyScratchDirectory(std::filesystem::path path)
        : path_(std::move(path))  {}

DependencyScratchDirectory::DependencyScratchDirectory(DependencyScratchDirectory&& other) noexcept
    : path_(std::exchange(other.path_, {}))  {}

DependencyScratchDirectory::~DependencyScratchDirectory()  {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

[[nodiscard]] const std::filesystem::path& DependencyScratchDirectory::path() const noexcept  {
        return path_;
    }

[[nodiscard]] std::optional<DependencyScratchDirectory>
create_dependency_scratch_directory(
    const std::filesystem::path& cache_directory,
    std::error_code& error) {
    (void)cache_directory;
    const auto root = std::filesystem::temp_directory_path(error)
        / "fsim-systemc-dependency-scans";
    if (error) {
        return std::nullopt;
    }
    std::filesystem::create_directories(root, error);
    if (error) {
        return std::nullopt;
    }

    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
        const auto serial =
            dependency_scan_counter.fetch_add(1, std::memory_order_relaxed);
        const auto name =
            std::to_string(dependency_process_id()) + "-"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(serial);
        const auto candidate = root / name;
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            return DependencyScratchDirectory{candidate};
        }
        if (error) {
            return std::nullopt;
        }
    }
    error = std::make_error_code(std::errc::file_exists);
    return std::nullopt;
}

void report_dependency_cache_disabled(
    diagnostic::Engine& diagnostics,
    std::string message,
    const std::filesystem::path& source,
    const CompilerCommand* command,
    const ProcessResult* process) {
    diagnostic::Diagnostic diagnostic;
    diagnostic.severity = diagnostic::Severity::warning;
    diagnostic.code = "FSIM-SC-C012";
    diagnostic.message =
        std::move(message) + "; persistent SystemC plug-in cache reuse is disabled";
    diagnostic.span = path_span(source);
    if (command != nullptr) {
        diagnostic.notes.push_back(
            {"dependency argv: " + format_compiler_command(*command), {}});
    }
    if (process != nullptr && !process->start_error.empty()) {
        diagnostic.notes.push_back(
            {"dependency compiler start failure: " + process->start_error, {}});
    }
    if (process != nullptr && !process->execution_error.empty()) {
        diagnostic.notes.push_back(
            {"dependency compiler execution failure: "
                 + process->execution_error,
             {}});
    }
    if (process != nullptr && !process->output.empty()) {
        diagnostic.notes.push_back(
            {"dependency compiler output:\n" + process->output, {}});
    }
    diagnostics.report(std::move(diagnostic));
}

[[nodiscard]] std::optional<std::string_view>
volatile_predefined_macro(const std::string_view contents) {
    constexpr std::array<std::string_view, 3> volatile_macros{
        "__DATE__",
        "__TIME__",
        "__TIMESTAMP__",
    };
    const auto without_comments = remove_cpp_comments(contents);
    const auto is_identifier_character = [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '_';
    };
    for (const auto macro : volatile_macros) {
        std::size_t position = 0;
        while ((position = without_comments.find(macro, position))
               != std::string::npos) {
            const auto end = position + macro.size();
            const bool begins_identifier =
                position != 0
                && is_identifier_character(without_comments[position - 1]);
            const bool ends_identifier =
                end != without_comments.size()
                && is_identifier_character(without_comments[end]);
            if (!begins_identifier && !ends_identifier) {
                return macro;
            }
            position = end;
        }
    }
    return std::nullopt;
}



[[nodiscard]] std::optional<VolatileMacroInput>
find_volatile_macro_input(
    const std::vector<std::filesystem::path>& inputs) {
    std::error_code error;
    for (const auto& input : inputs) {
        const auto contents = read_text_file(input, error);
        if (!contents) {
            // The normal dependency hashing path reports an unreadable input.
            continue;
        }
        if (const auto macro = volatile_predefined_macro(*contents)) {
            return VolatileMacroInput{input, std::string{*macro}};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::string>>
parse_makefile_dependencies(const std::string_view contents) {
    const auto colon = contents.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    std::vector<std::string> result;
    std::string token;
    const auto flush = [&]() {
        if (!token.empty()) {
            result.push_back(std::move(token));
            token.clear();
        }
    };
    for (std::size_t index = colon + 1; index < contents.size(); ++index) {
        const char character = contents[index];
        if (character == '\\') {
            if (index + 1 >= contents.size()) {
                return std::nullopt;
            }
            const char escaped = contents[index + 1];
            if (escaped == '\n') {
                ++index;
                continue;
            }
            if (escaped == '\r'
                && index + 2 < contents.size()
                && contents[index + 2] == '\n') {
                index += 2;
                continue;
            }
            // Clang's Windows Make depfiles spell native paths with ordinary
            // backslash separators. Preserve those separators while still
            // decoding the characters Make actually escapes in a filename.
            if (std::isspace(static_cast<unsigned char>(escaped)) != 0
                || escaped == '#' || escaped == ':' || escaped == '\\') {
                token.push_back(escaped);
                ++index;
            } else {
                token.push_back(character);
            }
            continue;
        }
        if (character == '$' && index + 1 < contents.size()
            && contents[index + 1] == '$') {
            token.push_back('$');
            ++index;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            flush();
            continue;
        }
        token.push_back(character);
    }
    flush();
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

namespace {

class JsonDependencyParser final {
public:
    explicit JsonDependencyParser(const std::string_view input) : input_(input) {}

    [[nodiscard]] std::optional<std::vector<std::filesystem::path>> parse() {
        if (input_.substr(0, 3) == "\xef\xbb\xbf") {
            position_ = 3;
        }
        skip_whitespace();
        if (!parse_value({}, 0)) {
            return std::nullopt;
        }
        skip_whitespace();
        if (position_ != input_.size() || !saw_data_ || !saw_source_) {
            return std::nullopt;
        }
        return std::move(paths_);
    }

private:
    static constexpr std::size_t kMaximumDepth = 128;

    void skip_whitespace() noexcept {
        while (position_ < input_.size()
               && std::isspace(
                      static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] bool consume(const char expected) noexcept {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] static bool append_utf8(
        std::string& output,
        const std::uint32_t code_point) {
        if (code_point <= 0x7fU) {
            output.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else if (code_point <= 0xffffU) {
            if (code_point >= 0xd800U && code_point <= 0xdfffU) {
                return false;
            }
            output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
            output.push_back(
                static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else if (code_point <= 0x10ffffU) {
            output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
            output.push_back(
                static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
            output.push_back(
                static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else {
            return false;
        }
        return true;
    }

    [[nodiscard]] std::optional<std::uint16_t> parse_hex_quad() {
        if (position_ + 4 > input_.size()) {
            return std::nullopt;
        }
        std::uint16_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            const char character = input_[position_++];
            value = static_cast<std::uint16_t>(value << 4U);
            if (character >= '0' && character <= '9') {
                value = static_cast<std::uint16_t>(value + character - '0');
            } else if (character >= 'a' && character <= 'f') {
                value = static_cast<std::uint16_t>(value + character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                value = static_cast<std::uint16_t>(value + character - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        return value;
    }

    [[nodiscard]] std::optional<std::string> parse_string() {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_++] != '"') {
            return std::nullopt;
        }
        std::string result;
        while (position_ < input_.size()) {
            const auto character =
                static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return result;
            }
            if (character < 0x20U) {
                return std::nullopt;
            }
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                return std::nullopt;
            }
            const char escape = input_[position_++];
            switch (escape) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                const auto first = parse_hex_quad();
                if (!first) {
                    return std::nullopt;
                }
                std::uint32_t code_point = *first;
                if (code_point >= 0xd800U && code_point <= 0xdbffU) {
                    if (position_ + 2 > input_.size()
                        || input_[position_] != '\\'
                        || input_[position_ + 1] != 'u') {
                        return std::nullopt;
                    }
                    position_ += 2;
                    const auto second = parse_hex_quad();
                    if (!second || *second < 0xdc00U || *second > 0xdfffU) {
                        return std::nullopt;
                    }
                    code_point = 0x10000U
                        + ((code_point - 0xd800U) << 10U)
                        + (*second - 0xdc00U);
                }
                if (!append_utf8(result, code_point)) {
                    return std::nullopt;
                }
                break;
            }
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool parse_number() noexcept {
        const auto begin = position_;
        if (position_ < input_.size() && input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size()) {
            return false;
        }
        if (input_[position_] == '0') {
            ++position_;
        } else if (input_[position_] >= '1' && input_[position_] <= '9') {
            do {
                ++position_;
            } while (position_ < input_.size()
                     && std::isdigit(
                            static_cast<unsigned char>(input_[position_])) != 0);
        } else {
            return false;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const auto digits = position_;
            while (position_ < input_.size()
                   && std::isdigit(
                          static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (position_ == digits) {
                return false;
            }
        }
        if (position_ < input_.size()
            && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size()
                && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            const auto digits = position_;
            while (position_ < input_.size()
                   && std::isdigit(
                          static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (position_ == digits) {
                return false;
            }
        }
        return position_ != begin;
    }

    [[nodiscard]] bool consume_literal(const std::string_view literal) noexcept {
        if (input_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    [[nodiscard]] bool record_path(
        const std::string_view key,
        const std::string& value) {
        if (key != "Source" && key != "Includes" && key != "Header"
            && key != "BMI" && key != "PCH") {
            return true;
        }
        std::u8string utf8_path;
        utf8_path.reserve(value.size());
        std::transform(
            value.begin(), value.end(), std::back_inserter(utf8_path),
            [](const char character) {
                return static_cast<char8_t>(
                    static_cast<unsigned char>(character));
            });
        auto path = std::filesystem::path{utf8_path};
        if (!path.is_absolute()) {
            return false;
        }
        if (key == "Source") {
            saw_source_ = true;
        }
        paths_.push_back(std::move(path));
        return true;
    }

    [[nodiscard]] bool parse_array(
        const std::string_view key,
        const std::size_t depth) {
        if (!consume('[')) {
            return false;
        }
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return true;
        }
        for (;;) {
            if (!parse_value(key, depth + 1)) {
                return false;
            }
            skip_whitespace();
            if (position_ < input_.size() && input_[position_] == ']') {
                ++position_;
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    [[nodiscard]] bool parse_object(const std::size_t depth) {
        if (!consume('{')) {
            return false;
        }
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return true;
        }
        for (;;) {
            const auto key = parse_string();
            if (!key || !consume(':')) {
                return false;
            }
            skip_whitespace();
            if (*key == "Data" && position_ < input_.size()
                && input_[position_] == '{') {
                saw_data_ = true;
            }
            if (!parse_value(*key, depth + 1)) {
                return false;
            }
            skip_whitespace();
            if (position_ < input_.size() && input_[position_] == '}') {
                ++position_;
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    [[nodiscard]] bool parse_value(
        const std::string_view key,
        const std::size_t depth) {
        if (depth > kMaximumDepth) {
            return false;
        }
        skip_whitespace();
        if (position_ >= input_.size()) {
            return false;
        }
        switch (input_[position_]) {
        case '{': return parse_object(depth);
        case '[': return parse_array(key, depth);
        case '"': {
            const auto value = parse_string();
            return value && record_path(key, *value);
        }
        case 't': return consume_literal("true");
        case 'f': return consume_literal("false");
        case 'n': return consume_literal("null");
        default: return parse_number();
        }
    }

    std::string_view input_;
    std::size_t position_{};
    std::vector<std::filesystem::path> paths_;
    bool saw_data_{};
    bool saw_source_{};
};

[[nodiscard]] bool add_dependency_files_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::vector<std::filesystem::path>& sources,
    std::vector<std::filesystem::path> dependencies,
    bool& cacheable,
    diagnostic::Engine& diagnostics) {
    std::sort(
        dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
            return left.generic_string() < right.generic_string();
        });
    dependencies.erase(
        std::unique(
            dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
                return left == right;
            }),
        dependencies.end());

    std::vector<std::filesystem::path> volatile_macro_inputs = sources;
    for (const auto& dependency : dependencies) {
        // MSVC reports compiled module, header-unit, and PCH images alongside
        // textual headers. Hash those binary artifacts, but do not read them
        // into the volatile-predefined-macro scanner.
        if (plausible_cpp_dependency(dependency)) {
            volatile_macro_inputs.push_back(dependency);
        }
    }
    if (const auto use = find_volatile_macro_input(volatile_macro_inputs)) {
        cacheable = false;
        builder.add("dependency.count", "0");
        report_dependency_cache_disabled(
            diagnostics,
            "source dependency uses volatile predefined macro '"
                + use->macro + "'",
            use->path);
        return true;
    }

    std::error_code error;
    std::unordered_set<std::string> source_names;
    for (const auto& source : sources) {
        auto normalized = std::filesystem::weakly_canonical(source, error);
        if (error) {
            error.clear();
            normalized = source.lexically_normal();
        }
        source_names.insert(normalized.generic_string());
    }
    dependencies.erase(
        std::remove_if(
            dependencies.begin(),
            dependencies.end(),
            [&](const auto& dependency) {
                return source_names.contains(dependency.generic_string());
            }),
        dependencies.end());

    builder.add("dependency.count", std::to_string(dependencies.size()));
    for (std::size_t index = 0; index < dependencies.size(); ++index) {
        if (builder.add_file(
                "dependency." + std::to_string(index),
                dependencies[index],
                error)) {
            continue;
        }
        cacheable = false;
        report_dependency_cache_disabled(
            diagnostics,
            "cannot hash compiler dependency: " + error.message(),
            dependencies[index]);
        return true;
    }
    return true;
}

} // namespace

[[nodiscard]] std::optional<std::vector<std::filesystem::path>>
parse_msvc_source_dependencies(const std::string_view contents) {
    return JsonDependencyParser{contents}.parse();
}

[[nodiscard]] bool add_makefile_dependencies_to_key(
    compiler::CacheKeyBuilder& builder,
    const HostToolchain toolchain,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& cache_directory,
    bool& cacheable,
    diagnostic::Engine& diagnostics) {
    const bool clang_cl = toolchain == HostToolchain::msvc;
    builder.add(
        "dependency.discovery",
        clang_cl ? "compiler-clangcl-make-v1" : "compiler-make-v1");
    if (!cacheable) {
        builder.add("dependency.count", "0");
        return true;
    }
    if (resolved_compiler.empty()) {
        cacheable = false;
        builder.add("dependency.count", "0");
        report_dependency_cache_disabled(
            diagnostics,
            "the selected compiler could not be resolved");
        return true;
    }

    std::error_code error;
    auto scratch =
        create_dependency_scratch_directory(cache_directory, error);
    if (!scratch) {
        cacheable = false;
        builder.add("dependency.count", "0");
        report_dependency_cache_disabled(
            diagnostics,
            "cannot create dependency scan directory: " + error.message(),
            cache_directory);
        return true;
    }

    std::vector<std::filesystem::path> dependencies;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto dependency_file =
            scratch->path() / ("source-" + std::to_string(index) + ".d");
        const auto object_file =
            scratch->path() / ("source-" + std::to_string(index) + ".obj");
        const auto program_database =
            scratch->path() / ("source-" + std::to_string(index) + ".pdb");
        auto argv = common_compile_argv(
            toolchain,
            compiler_name,
            resolved_compiler,
            includes,
            settings);
        if (clang_cl) {
            argv.emplace_back("/c");
            argv.push_back(path_argument(sources[index]));
            argv.push_back("/Fo" + path_argument(object_file));
            argv.push_back("/Fd" + path_argument(program_database));
            argv.emplace_back("/clang:-MD");
            argv.emplace_back("/clang:-MF");
            argv.push_back("/clang:" + path_argument(dependency_file));
            argv.emplace_back("/clang:-MT");
            argv.push_back(
                "/clang:fsim_dependency_target_" + std::to_string(index));
        } else {
            argv.emplace_back("-M");
            argv.emplace_back("-MF");
            argv.push_back(path_argument(dependency_file));
            argv.emplace_back("-MT");
            argv.push_back("fsim_dependency_target_" + std::to_string(index));
            argv.push_back(path_argument(sources[index]));
        }
        const CompilerCommand command{
            std::move(argv), working_directory, toolchain};
        const auto process = run_process(command);
        if (!process.started || process.exit_code != 0) {
            cacheable = false;
            builder.add("dependency.count", "0");
            report_dependency_cache_disabled(
                diagnostics,
                "the compiler could not emit a complete dependency closure",
                sources[index],
                &command,
                &process);
            return true;
        }

        const auto contents = read_text_file(dependency_file, error);
        const auto parsed =
            contents ? parse_makefile_dependencies(*contents) : std::nullopt;
        if (!parsed) {
            cacheable = false;
            builder.add("dependency.count", "0");
            report_dependency_cache_disabled(
                diagnostics,
                contents
                    ? "the compiler emitted an invalid dependency file"
                    : "cannot read compiler dependency file: " + error.message(),
                dependency_file,
                &command,
                &process);
            return true;
        }

        for (const auto& dependency_name : *parsed) {
            auto dependency =
                make_absolute(
                    std::filesystem::path{dependency_name},
                    working_directory);
            dependency = normalized_existing_path(dependency, error);
            if (dependency.empty()) {
                cacheable = false;
                builder.add("dependency.count", "0");
                report_dependency_cache_disabled(
                    diagnostics,
                    "compiler dependency is not a readable regular file",
                    std::filesystem::path{dependency_name});
                return true;
            }
            dependencies.push_back(std::move(dependency));
        }
    }

    return add_dependency_files_to_key(
        builder, sources, std::move(dependencies), cacheable, diagnostics);
}

[[nodiscard]] bool add_msvc_dependencies_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& cache_directory,
    bool& cacheable,
    diagnostic::Engine& diagnostics) {
    if (!cacheable || resolved_compiler.empty()) {
        builder.add("dependency.discovery", "unavailable");
        builder.add("dependency.count", "0");
        return true;
    }

    std::error_code error;
    auto scratch = create_dependency_scratch_directory(cache_directory, error);
    std::vector<std::filesystem::path> dependencies;
    bool emitted_complete_closure = scratch.has_value();
    for (std::size_t index = 0;
         emitted_complete_closure && index < sources.size();
         ++index) {
        const auto dependency_file =
            scratch->path() / ("source-" + std::to_string(index) + ".json");
        const auto object_file =
            scratch->path() / ("source-" + std::to_string(index) + ".obj");
        const auto program_database =
            scratch->path() / ("source-" + std::to_string(index) + ".pdb");
        auto argv = common_compile_argv(
            HostToolchain::msvc,
            compiler_name,
            resolved_compiler,
            includes,
            settings);
        argv.emplace_back("/c");
        argv.push_back(path_argument(sources[index]));
        argv.push_back("/Fo" + path_argument(object_file));
        argv.push_back("/Fd" + path_argument(program_database));
        argv.emplace_back("/sourceDependencies");
        argv.push_back(path_argument(dependency_file));
        const CompilerCommand command{
            std::move(argv), working_directory, HostToolchain::msvc};
        const auto process = run_process(command);
        if (!process.started || process.exit_code != 0) {
            emitted_complete_closure = false;
            break;
        }
        const auto contents = read_text_file(dependency_file, error);
        const auto parsed = contents
            ? parse_msvc_source_dependencies(*contents)
            : std::nullopt;
        if (!parsed) {
            emitted_complete_closure = false;
            break;
        }
        for (const auto& candidate : *parsed) {
            auto dependency = normalized_existing_path(candidate, error);
            if (dependency.empty()) {
                emitted_complete_closure = false;
                break;
            }
            dependencies.push_back(std::move(dependency));
        }
    }

    if (emitted_complete_closure) {
        builder.add("dependency.discovery", "compiler-msvc-json-v1");
        return add_dependency_files_to_key(
            builder,
            sources,
            std::move(dependencies),
            cacheable,
            diagnostics);
    }

    builder.add("dependency.discovery", "manifest-conservative-v1");
    const bool was_cacheable = cacheable;
    if (!add_transitive_dependencies_to_key(
            builder,
            sources,
            includes,
            settings.defines,
            cacheable,
            diagnostics)) {
        return false;
    }
    if (was_cacheable && !cacheable) {
        report_dependency_cache_disabled(
            diagnostics,
            "MSVC dependency discovery could not prove a complete include closure");
    }
    return true;
}

[[nodiscard]] bool add_compiler_dependencies_to_key(
    compiler::CacheKeyBuilder& builder,
    const HostToolchain toolchain,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& cache_directory,
    bool& cacheable,
    diagnostic::Engine& diagnostics) {
    if (toolchain == HostToolchain::gcc_like
        || is_clang_cl_compiler(compiler_name, resolved_compiler)) {
        return add_makefile_dependencies_to_key(
            builder,
            toolchain,
            compiler_name,
            resolved_compiler,
            sources,
            includes,
            settings,
            working_directory,
            cache_directory,
            cacheable,
            diagnostics);
    }

    return add_msvc_dependencies_to_key(
        builder,
        compiler_name,
        resolved_compiler,
        sources,
        includes,
        settings,
        working_directory,
        cache_directory,
        cacheable,
        diagnostics);
}



} // namespace fsim::systemc::plugin_detail
