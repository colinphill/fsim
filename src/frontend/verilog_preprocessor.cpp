// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/preprocessor.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {
namespace {

    [[nodiscard]] StandardRevision default_standard_revision(
        const Language language)
    {
        return language == Language::Verilog2005
            ? StandardRevision::Verilog2005
            : StandardRevision::SystemVerilog2017;
    }

    [[nodiscard]] bool is_verilog_standard(const StandardRevision standard)
    {
        return standard == StandardRevision::Verilog1995
            || standard == StandardRevision::Verilog2001
            || standard == StandardRevision::Verilog2001NoConfig
            || standard == StandardRevision::Verilog2005;
    }

    [[nodiscard]] bool is_system_verilog_standard(
        const StandardRevision standard)
    {
        return standard == StandardRevision::SystemVerilog2005
            || standard == StandardRevision::SystemVerilog2009
            || standard == StandardRevision::SystemVerilog2012
            || standard == StandardRevision::SystemVerilog2017
            || standard == StandardRevision::SystemVerilog2023;
    }

    [[nodiscard]] unsigned standard_rank(const StandardRevision standard)
    {
        switch (standard) {
        case StandardRevision::Verilog1995:
            return 0;
        case StandardRevision::Verilog2001:
        case StandardRevision::Verilog2001NoConfig:
            return 1;
        case StandardRevision::Verilog2005:
            return 2;
        case StandardRevision::SystemVerilog2005:
            return 3;
        case StandardRevision::SystemVerilog2009:
            return 4;
        case StandardRevision::SystemVerilog2012:
            return 5;
        case StandardRevision::SystemVerilog2017:
            return 6;
        case StandardRevision::SystemVerilog2023:
            return 7;
        default:
            return 0;
        }
    }

    [[nodiscard]] std::optional<std::int32_t>
    systemverilog_coverage_constant(const std::string_view name) noexcept
    {
        if (name == "SV_COV_START")
            return 0;
        if (name == "SV_COV_STOP")
            return 1;
        if (name == "SV_COV_RESET")
            return 2;
        if (name == "SV_COV_CHECK")
            return 3;
        if (name == "SV_COV_MODULE")
            return 10;
        if (name == "SV_COV_HIER")
            return 11;
        if (name == "SV_COV_ASSERTION")
            return 20;
        if (name == "SV_COV_FSM_STATE")
            return 21;
        if (name == "SV_COV_STATEMENT")
            return 22;
        if (name == "SV_COV_TOGGLE")
            return 23;
        if (name == "SV_COV_OVERFLOW")
            return -2;
        if (name == "SV_COV_ERROR")
            return -1;
        if (name == "SV_COV_NOCOV")
            return 0;
        if (name == "SV_COV_OK")
            return 1;
        if (name == "SV_COV_PARTIAL")
            return 2;
        return std::nullopt;
    }

    // Preserve the published diagnostic identities after adopting true textual
    // include semantics. They must not be reused for a different failure class.
    [[maybe_unused]] constexpr std::string_view
        legacy_include_open_conditional_code = "FSIM-SV-PP-045";
    [[maybe_unused]] constexpr std::string_view
        legacy_include_cross_conditional_code = "FSIM-SV-PP-046";

    struct MacroParameter {
        std::string name;
        std::optional<std::vector<Token>> default_value;
    };

    struct Macro {
        std::string name;
        std::optional<std::vector<MacroParameter>> parameters;
        std::vector<Token> replacement;
        SourceSpan definition;
    };

    [[nodiscard]] bool is_line_continuation(const Token& token)
    {
        return !token.text.empty() && token.text.front() == '\\'
            && (token.text.find('\n') != std::string::npos
                || token.text.find('\r') != std::string::npos);
    }

    [[nodiscard]] bool same_tokens(
        const std::vector<Token>& left,
        const std::vector<Token>& right)
    {
        return left.size() == right.size()
            && std::equal(
                left.begin(), left.end(), right.begin(),
                [](const Token& lhs, const Token& rhs) {
                    return lhs.kind == rhs.kind && lhs.text == rhs.text;
                });
    }

    [[nodiscard]] bool same_macro(
        const Macro& left,
        const Macro& right)
    {
        if (left.parameters.has_value() != right.parameters.has_value()
            || !same_tokens(left.replacement, right.replacement)) {
            return false;
        }
        if (!left.parameters) {
            return true;
        }
        if (left.parameters->size() != right.parameters->size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.parameters->size(); ++index) {
            const auto& lhs = (*left.parameters)[index];
            const auto& rhs = (*right.parameters)[index];
            if (lhs.name != rhs.name
                || lhs.default_value.has_value()
                    != rhs.default_value.has_value()
                || (lhs.default_value
                    && !same_tokens(*lhs.default_value, *rhs.default_value))) {
                return false;
            }
        }
        return true;
    }

    struct Conditional {
        bool parent_active { };
        bool active { };
        bool branch_taken { };
        bool saw_else { };
        SourceSpan opening;
    };

    struct SourceMapping {
        std::string physical_source;
        std::string logical_source;
        std::size_t physical_anchor_line { 1 };
        std::size_t logical_anchor_line { 1 };
        unsigned level { };
    };

    struct ProcessedTokens {
        std::vector<SourceMapping> mappings;
        Token eof;
    };

    [[nodiscard]] bool identifier_spelling(const std::string_view spelling)
    {
        if (spelling.empty()) {
            return false;
        }
        const auto first = static_cast<unsigned char>(spelling.front());
        if (std::isalpha(first) == 0 && spelling.front() != '_'
            && spelling.front() != '$') {
            return false;
        }
        return std::all_of(
            spelling.begin() + 1,
            spelling.end(),
            [](const char character) {
                const auto value = static_cast<unsigned char>(character);
                return std::isalnum(value) != 0 || character == '_'
                    || character == '$';
            });
    }

    [[nodiscard]] std::string string_literal_spelling(
        const std::string_view value)
    {
        std::string result;
        result.reserve(value.size() + 2);
        result.push_back('"');
        for (const char character : value) {
            switch (character) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result.push_back(character);
                break;
            }
        }
        result.push_back('"');
        return result;
    }

    [[nodiscard]] std::optional<std::string> decode_string_literal(
        const std::string_view spelling)
    {
        if (spelling.size() < 2 || spelling.front() != '"'
            || spelling.back() != '"') {
            return std::nullopt;
        }
        std::string result;
        result.reserve(spelling.size() - 2);
        for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
            const char character = spelling[index];
            if (character != '\\') {
                if (character == '\n' || character == '\r') {
                    return std::nullopt;
                }
                result.push_back(character);
                continue;
            }
            if (++index + 1 > spelling.size()) {
                return std::nullopt;
            }
            switch (spelling[index]) {
            case '\\':
                result.push_back('\\');
                break;
            case '"':
                result.push_back('"');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                return std::nullopt;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::size_t> decimal_number(
        const Token& token,
        const bool require_positive)
    {
        if (token.kind != TokenKind::Number || token.text.empty()) {
            return std::nullopt;
        }
        std::size_t result { };
        const auto parsed = std::from_chars(
            token.text.data(), token.text.data() + token.text.size(), result, 10);
        if (parsed.ec != std::errc { }
            || parsed.ptr != token.text.data() + token.text.size()
            || (require_positive && result == 0)) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] SourceLocation remap_location(
        SourceLocation location,
        const SourceMapping& mapping)
    {
        if (location.line < mapping.physical_anchor_line) {
            return location;
        }
        const auto delta = location.line - mapping.physical_anchor_line;
        if (delta
            > std::numeric_limits<std::size_t>::max()
                - mapping.logical_anchor_line) {
            location.line = std::numeric_limits<std::size_t>::max();
        } else {
            location.line = mapping.logical_anchor_line + delta;
        }
        return location;
    }

    [[nodiscard]] SourceSpan remap_span(
        SourceSpan span,
        const SourceMapping& mapping)
    {
        if (physical_source(span) != mapping.physical_source) {
            return span;
        }
        if (span.physical_source_name.empty()) {
            span.physical_source_name = mapping.physical_source;
        }
        span.source_name = mapping.logical_source;
        span.begin = remap_location(span.begin, mapping);
        span.end = remap_location(span.end, mapping);
        return span;
    }

    [[nodiscard]] std::string location_text(const SourceSpan& span)
    {
        return span.source_name + ':' + std::to_string(span.begin.line)
            + ':' + std::to_string(span.begin.column);
    }

    [[nodiscard]] std::filesystem::path normalized_path(
        const std::filesystem::path& path)
    {
        std::error_code error;
        auto absolute = std::filesystem::absolute(path, error);
        if (error) {
            return path.lexically_normal();
        }
        auto canonical = std::filesystem::weakly_canonical(absolute, error);
        return error ? absolute.lexically_normal() : canonical;
    }

    class VerilogPreprocessor {
    public:
        VerilogPreprocessor(
            const Language language,
            const PreprocessorOptions& options)
            : language_(language)
            , standard_revision_(options.standard_revision.value_or(
                  default_standard_revision(language)))
            , options_(options)
        {
            for (const auto& directory : options_.include_directories) {
                include_directories_.push_back(normalized_path(directory));
            }
        }

        PreprocessResult run(const std::filesystem::path& root)
        {
            auto compilation = run(
                std::vector<std::filesystem::path> { root });
            return single_result(std::move(compilation));
        }

        PreprocessCompilationUnitResult run(
            const std::vector<std::filesystem::path>& roots)
        {
            const auto fallback = roots.empty()
                ? std::filesystem::path { "<empty>.sv" }
                : roots.back();
            if (!valid_language(fsim::support::path_to_utf8(fallback))) {
                return finish_compilation_unit(fallback);
            }
            if (roots.empty()) {
                diagnose(
                    "FSIM-SV-PP-031",
                    "a Verilog compilation unit requires at least one root file",
                    { fsim::support::path_to_utf8(fallback), { }, { },
                        fsim::support::path_to_utf8(fallback), { } });
                return finish_compilation_unit(fallback);
            }
            define_command_line_macros();
            for (const auto& root : roots) {
                process_root(normalized_path(root));
            }
            diagnose_open_conditionals();
            return finish_compilation_unit(roots.back());
        }

        PreprocessResult run(SourceText source)
        {
            const auto root = normalized_path(
                source.name.empty()
                    ? std::filesystem::path { "<memory>.sv" }
                    : fsim::support::path_from_utf8(source.name));
            if (!valid_language(source.name)) {
                return single_result(
                    finish_compilation_unit(root));
            }
            define_command_line_macros();
            const auto name = fsim::support::path_to_utf8(root);
            source_snapshots_.emplace(name, std::move(source.text));
            inputs_.push_back(
                { root, source_snapshots_.find(name)->second, standard_revision_ });
            process_root(root);
            diagnose_open_conditionals();
            return single_result(
                finish_compilation_unit(root));
        }

    private:
        [[nodiscard]] bool valid_language(
            const std::string& source_name)
        {
            if (language_ == Language::Verilog2005
                || language_ == Language::SystemVerilog2017) {
                const bool matching_standard = (language_ == Language::Verilog2005
                                                   && is_verilog_standard(standard_revision_))
                    || (language_ == Language::SystemVerilog2017
                        && is_system_verilog_standard(standard_revision_));
                if (matching_standard) {
                    return true;
                }
                diagnose(
                    "FSIM-SV-PP-052",
                    "selected standard '" + std::string(to_string(standard_revision_))
                        + "' does not belong to this Verilog/SystemVerilog language",
                    { source_name, { }, { }, source_name, { } });
                return false;
            }
            diagnose(
                "FSIM-SV-PP-001",
                "the Verilog preprocessor requires Verilog-2005 or "
                "SystemVerilog-2017 input",
                { source_name, { }, { }, source_name, { } });
            return false;
        }

        void diagnose_open_conditionals()
        {
            for (const auto& conditional : conditionals_) {
                diagnose(
                    "FSIM-SV-PP-002",
                    "unterminated conditional compilation block",
                    conditional.opening);
            }
            conditionals_.clear();
        }

        void process_root(const std::filesystem::path& root)
        {
            current_dependency_names_.clear();
            current_dependencies_.clear();
            process_file(root, 0);
            const auto found = source_snapshots_.find(fsim::support::path_to_utf8(root));
            roots_.push_back({ root,
                found == source_snapshots_.end()
                    ? std::string { }
                    : found->second,
                std::move(current_dependencies_),
                standard_revision_ });
        }

        [[nodiscard]] bool active() const noexcept
        {
            return conditionals_.empty() || conditionals_.back().active;
        }

        PreprocessCompilationUnitResult finish_compilation_unit(
            const std::filesystem::path& root)
        {
            validate_lexical_revision();
            const auto root_name = fsim::support::path_to_utf8(normalized_path(root));
            SourceSpan eof_span { root_name, { }, { }, root_name, { } };
            if (root_eof_) {
                eof_span = root_eof_->span;
            }
            output_.push_back(
                Token { TokenKind::EndOfFile, { }, std::move(eof_span), { } });
            return {
                LexResult { std::move(output_), std::move(diagnostics_) },
                std::move(roots_),
                std::move(inputs_)
            };
        }

        static PreprocessResult single_result(
            PreprocessCompilationUnitResult compilation)
        {
            PreprocessResult result;
            result.lexed = std::move(compilation.lexed);
            if (!compilation.roots.empty()) {
                auto& root = compilation.roots.front();
                result.dependencies.push_back(
                    { std::move(root.path), std::move(root.contents),
                        root.standard_revision });
                result.dependencies.insert(
                    result.dependencies.end(),
                    std::make_move_iterator(root.dependencies.begin()),
                    std::make_move_iterator(root.dependencies.end()));
            }
            return result;
        }

        void diagnose(
            std::string code,
            std::string message,
            SourceSpan span,
            std::vector<std::string> expansion_stack = { })
        {
            diagnostics_.push_back({ DiagnosticSeverity::Error,
                std::move(code),
                std::move(message),
                std::move(span),
                std::move(expansion_stack) });
        }

        [[nodiscard]] bool require_standard(
            const std::string_view feature,
            const StandardRevision required,
            const Token& token)
        {
            if (standard_rank(standard_revision_) >= standard_rank(required)) {
                return true;
            }
            diagnose(
                "FSIM-SV-PP-052",
                std::string(feature) + " requires " + std::string(to_string(required))
                    + "; selected " + std::string(to_string(standard_revision_))
                    + ". Select that or a later standard revision, or rewrite the form",
                token.span,
                token.expansion_stack);
            return false;
        }

        void validate_lexical_revision()
        {
            const auto is_timescale_argument = [&](const std::size_t position) {
                const auto& candidate = output_[position];
                for (auto prior = position; prior != 0U;) {
                    --prior;
                    const auto& token = output_[prior];
                    if (token.span.source_name != candidate.span.source_name
                        || token.span.begin.line != candidate.span.begin.line) {
                        break;
                    }
                    if (token.kind == TokenKind::Backtick) {
                        return prior + 1U < output_.size()
                            && output_[prior + 1U].text == "timescale";
                    }
                }
                return false;
            };
            for (std::size_t index = 0; index < output_.size(); ++index) {
                const auto& token = output_[index];
                if (token.kind == TokenKind::Number) {
                    if (standard_revision_ == StandardRevision::Verilog1995
                        && (token.text.find("'s") != std::string::npos
                            || token.text.find("'S") != std::string::npos)) {
                        (void)require_standard(
                            "a signed based literal", StandardRevision::Verilog2001,
                            token);
                    }
                    if (token.text.size() == 2 && token.text.front() == '\''
                        && (token.text[1] == '0' || token.text[1] == '1'
                            || token.text[1] == 'x' || token.text[1] == 'X'
                            || token.text[1] == 'z' || token.text[1] == 'Z')) {
                        (void)require_standard(
                            "an unbased unsized literal",
                            StandardRevision::SystemVerilog2005, token);
                    }
                    if (index + 1 < output_.size()
                        && output_[index + 1].kind == TokenKind::Identifier
                        && (output_[index + 1].text == "s"
                            || output_[index + 1].text == "ms"
                            || output_[index + 1].text == "us"
                            || output_[index + 1].text == "ns"
                            || output_[index + 1].text == "ps"
                            || output_[index + 1].text == "fs")
                        && token.span.source_name == output_[index + 1].span.source_name
                        && token.span.end.offset
                            == output_[index + 1].span.begin.offset
                        && !is_timescale_argument(index)) {
                        (void)require_standard(
                            "a time-unit literal", StandardRevision::SystemVerilog2005,
                            token);
                    }
                }
                if (token.kind == TokenKind::StringLiteral) {
                    for (std::size_t character = 1;
                        character + 1 < token.text.size(); ++character) {
                        if (token.text[character] != '\\') {
                            continue;
                        }
                        ++character;
                        if (token.text[character] == 'a' || token.text[character] == 'v'
                            || token.text[character] == 'f'
                            || token.text[character] == 'x') {
                            (void)require_standard(
                                "this string escape", StandardRevision::SystemVerilog2005,
                                token);
                            break;
                        }
                    }
                }
            }
        }

        void define_command_line_macros()
        {
            for (const auto& definition : options_.defines) {
                const auto separator = definition.find('=');
                const auto name = definition.substr(0, separator);
                const auto replacement = separator == std::string::npos
                    ? std::string { }
                    : definition.substr(separator + 1);
                const SourceSpan span {
                    "<command-line:-D" + definition + ">",
                    SourceLocation { },
                    SourceLocation { },
                    "<command-line:-D" + definition + ">",
                    { }
                };
                if (!identifier_spelling(name)) {
                    diagnose(
                        "FSIM-SV-PP-003",
                        "invalid command-line macro definition '" + definition + "'",
                        span);
                    continue;
                }
                auto lexed = lex(
                    SourceText { span.source_name, replacement }, language_);
                for (auto& diagnostic : lexed.diagnostics) {
                    diagnostics_.push_back(std::move(diagnostic));
                }
                if (!lexed.tokens.empty()) {
                    lexed.tokens.pop_back();
                }
                macros_[name] = Macro { name, std::nullopt, std::move(lexed.tokens), span };
            }
        }

        [[nodiscard]] std::optional<std::string> read_file(
            const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) {
                return std::nullopt;
            }
            std::string contents {
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>()
            };
            if (!stream.good() && !stream.eof()) {
                return std::nullopt;
            }
            return contents;
        }

        void process_file(
            const std::filesystem::path& path,
            const std::size_t depth)
        {
            const auto normalized = normalized_path(path);
            const auto name = fsim::support::path_to_utf8(normalized);
            if (depth > options_.maximum_include_depth) {
                diagnose(
                    "FSIM-SV-PP-004",
                    "maximum Verilog include depth exceeded while opening '"
                        + name + "'",
                    include_invocation_.value_or(
                        SourceSpan { name, { }, { }, name, { } }));
                return;
            }
            if (std::find(include_stack_.begin(), include_stack_.end(), normalized)
                != include_stack_.end()) {
                diagnose(
                    "FSIM-SV-PP-005",
                    "recursive Verilog include of '" + name + "'",
                    include_invocation_.value_or(
                        SourceSpan { name, { }, { }, name, { } }));
                return;
            }
            auto snapshot = source_snapshots_.find(name);
            if (snapshot == source_snapshots_.end()) {
                auto contents = read_file(normalized);
                if (!contents) {
                    diagnose(
                        "FSIM-FE-IO-001",
                        "unable to open source file",
                        include_invocation_.value_or(
                            SourceSpan { name, { }, { }, name, { } }));
                    return;
                }
                snapshot = source_snapshots_
                               .emplace(name, std::move(*contents))
                               .first;
                inputs_.push_back({ normalized, snapshot->second, standard_revision_ });
            }
            if (depth != 0
                && current_dependency_names_.insert(name).second) {
                current_dependencies_.push_back(
                    { normalized, snapshot->second, standard_revision_ });
            }

            auto lexed = lex(
                SourceText { name, snapshot->second }, language_);
            const auto lexical_diagnostics_begin = diagnostics_.size();
            for (auto& diagnostic : lexed.diagnostics) {
                diagnostic.expansion_stack.insert(
                    diagnostic.expansion_stack.begin(),
                    include_ancestry_.begin(),
                    include_ancestry_.end());
                diagnostics_.push_back(std::move(diagnostic));
            }
            const auto lexical_diagnostics_end = diagnostics_.size();
            if (!include_ancestry_.empty()) {
                for (auto& token : lexed.tokens) {
                    token.expansion_stack.insert(
                        token.expansion_stack.begin(),
                        include_ancestry_.begin(),
                        include_ancestry_.end());
                }
            }
            include_stack_.push_back(normalized);
            auto processed = process_tokens(lexed.tokens, depth);
            include_stack_.pop_back();
            for (auto diagnostic_index = lexical_diagnostics_begin;
                diagnostic_index < lexical_diagnostics_end; ++diagnostic_index) {
                auto& diagnostic = diagnostics_[diagnostic_index];
                for (auto mapping = processed.mappings.rbegin();
                    mapping != processed.mappings.rend(); ++mapping) {
                    if (diagnostic.span.source_name == mapping->physical_source
                        && diagnostic.span.begin.line
                            >= mapping->physical_anchor_line) {
                        diagnostic.span = remap_span(std::move(diagnostic.span), *mapping);
                        break;
                    }
                }
            }
            if (depth == 0) {
                root_eof_ = std::move(processed.eof);
            }
        }

        [[nodiscard]] static std::size_t line_end(
            const std::vector<Token>& tokens,
            const std::size_t begin)
        {
            if (begin >= tokens.size()
                || tokens[begin].kind == TokenKind::EndOfFile) {
                return begin;
            }
            auto line = tokens[begin].span.begin.line;
            std::size_t end = begin;
            while (end < tokens.size()
                && tokens[end].kind != TokenKind::EndOfFile) {
                if (tokens[end].span.begin.line != line) {
                    if (tokens[end].span.begin.line != line + 1
                        || end == begin
                        || !is_line_continuation(tokens[end - 1])) {
                        break;
                    }
                    line = tokens[end].span.begin.line;
                }
                ++end;
            }
            return end;
        }

        [[nodiscard]] ProcessedTokens process_tokens(
            const std::vector<Token>& tokens,
            const std::size_t include_depth)
        {
            ProcessedTokens processed;
            if (tokens.empty()) {
                return processed;
            }
            std::vector<Token> mapped_tokens = tokens;
            SourceMapping mapping {
                tokens.front().span.source_name,
                tokens.front().span.source_name,
                1,
                1,
                0
            };
            processed.mappings.push_back(mapping);
            const auto remap_tail = [&](const std::size_t begin) {
                for (auto position = begin; position < tokens.size(); ++position) {
                    mapped_tokens[position].span =
                        remap_span(tokens[position].span, mapping);
                }
            };
            remap_tail(0);
            std::size_t index = 0;
            while (index < tokens.size()
                && tokens[index].kind != TokenKind::EndOfFile) {
                if (protected_envelope_) {
                    if (mapped_tokens[index].kind == TokenKind::Backtick
                        && index + 1 < tokens.size()
                        && mapped_tokens[index + 1].kind == TokenKind::Identifier
                        && mapped_tokens[index + 1].text == "pragma") {
                        const auto end = line_end(tokens, index);
                        handle_directive(
                            mapped_tokens, index, end, include_depth);
                        index = end;
                    } else {
                        ++index;
                    }
                    continue;
                }
                if (mapped_tokens[index].kind == TokenKind::Backtick
                    && index + 1 < tokens.size()
                    && mapped_tokens[index + 1].kind == TokenKind::Identifier) {
                    const auto directive = mapped_tokens[index + 1].text;
                    if (is_directive(directive)) {
                        const auto end = line_end(tokens, index);
                        if (directive == "line" && active()) {
                            const auto next_physical_line = end == index
                                ? tokens[index].span.end.line + 1
                                : tokens[end - 1].span.end.line + 1;
                            if (auto next_mapping = line_mapping(
                                    mapped_tokens,
                                    index + 2,
                                    end,
                                    mapped_tokens[index + 1],
                                    mapping.physical_source,
                                    next_physical_line)) {
                                mapping = std::move(*next_mapping);
                                processed.mappings.push_back(mapping);
                                remap_tail(end);
                            }
                            index = end;
                            continue;
                        }
                        handle_directive(
                            mapped_tokens, index, end, include_depth);
                        index = end;
                        continue;
                    }
                    if (!active()) {
                        ++index;
                        continue;
                    }
                    auto expanded = expand_invocation(mapped_tokens, index, 0, { });
                    output_.insert(
                        output_.end(),
                        std::make_move_iterator(expanded.begin()),
                        std::make_move_iterator(expanded.end()));
                    continue;
                }
                if (is_line_continuation(mapped_tokens[index])) {
                    ++index;
                    continue;
                }
                if (active()) {
                    output_.push_back(mapped_tokens[index]);
                }
                ++index;
            }
            mapped_tokens.back().span = remap_span(tokens.back().span, mapping);
            processed.eof = std::move(mapped_tokens.back());
            return processed;
        }

        [[nodiscard]] std::optional<SourceMapping> line_mapping(
            const std::vector<Token>& tokens,
            const std::size_t begin,
            const std::size_t end,
            const Token& directive,
            std::string physical_source,
            const std::size_t next_physical_line)
        {
            using Difference = std::vector<Token>::difference_type;
            const std::vector<Token> arguments {
                tokens.begin() + static_cast<Difference>(begin),
                tokens.begin() + static_cast<Difference>(end)
            };
            auto expanded = expand_sequence(arguments, 0, { });
            if (expanded.size() != 3) {
                diagnose(
                    "FSIM-SV-PP-039",
                    "`line requires a line number, quoted file name, and level",
                    directive.span);
                return std::nullopt;
            }
            const auto logical_line = decimal_number(expanded[0], true);
            if (!logical_line
                || *logical_line
                    > std::numeric_limits<std::uint32_t>::max()) {
                diagnose(
                    "FSIM-SV-PP-040",
                    "`line requires a positive decimal line number",
                    expanded[0].span,
                    expanded[0].expansion_stack);
                return std::nullopt;
            }
            if (expanded[1].kind != TokenKind::StringLiteral) {
                diagnose(
                    "FSIM-SV-PP-041",
                    "`line requires a quoted logical file name",
                    expanded[1].span,
                    expanded[1].expansion_stack);
                return std::nullopt;
            }
            auto logical_source = decode_string_literal(expanded[1].text);
            if (!logical_source) {
                diagnose(
                    "FSIM-SV-PP-042",
                    "`line logical file name contains an invalid escape",
                    expanded[1].span,
                    expanded[1].expansion_stack);
                return std::nullopt;
            }
            const auto level = decimal_number(expanded[2], false);
            if (!level || *level > 2) {
                diagnose(
                    "FSIM-SV-PP-043",
                    "`line level must be 0, 1, or 2",
                    expanded[2].span,
                    expanded[2].expansion_stack);
                return std::nullopt;
            }
            return SourceMapping {
                std::move(physical_source),
                std::move(*logical_source),
                next_physical_line,
                *logical_line,
                static_cast<unsigned>(*level)
            };
        }

        [[nodiscard]] static bool is_directive(
            const std::string_view name)
        {
            constexpr std::string_view directives[] = {
                "define",
                "undef",
                "undefineall",
                "include",
                "ifdef",
                "ifndef",
                "elsif",
                "else",
                "endif",
                "timescale",
                "default_nettype",
                "resetall",
                "celldefine",
                "endcelldefine",
                "begin_keywords",
                "end_keywords",
                "line",
                "pragma",
                "unconnected_drive",
                "nounconnected_drive"
            };
            return std::find(
                       std::begin(directives),
                       std::end(directives),
                       name)
                != std::end(directives);
        }

        void handle_directive(
            const std::vector<Token>& tokens,
            const std::size_t begin,
            const std::size_t end,
            const std::size_t include_depth)
        {
            const auto& tick = tokens[begin];
            const auto& name_token = tokens[begin + 1];
            const auto name = name_token.text;
            const auto arguments = begin + 2;

            if (name == "ifdef" || name == "ifndef") {
                const auto condition = directive_condition(
                    tokens, arguments, end, name_token);
                const bool parent = active();
                const bool take = condition
                    && (name == "ifdef" ? *condition : !*condition);
                conditionals_.push_back(
                    { parent, parent && take, parent && take, false,
                        cover(tick.span, name_token.span) });
                return;
            }
            if (name == "elsif") {
                if (conditionals_.empty()) {
                    diagnose(
                        "FSIM-SV-PP-006",
                        "`elsif without a matching `ifdef or `ifndef",
                        name_token.span);
                    return;
                }
                auto& conditional = conditionals_.back();
                if (conditional.saw_else) {
                    diagnose(
                        "FSIM-SV-PP-007",
                        "`elsif cannot follow `else in one conditional block",
                        name_token.span);
                }
                const auto condition = directive_condition(
                    tokens, arguments, end, name_token);
                const bool take = conditional.parent_active && !conditional.branch_taken
                    && condition && *condition;
                conditional.active = take;
                conditional.branch_taken = conditional.branch_taken || take;
                return;
            }
            if (name == "else") {
                if (conditionals_.empty()) {
                    diagnose(
                        "FSIM-SV-PP-008",
                        "`else without a matching `ifdef or `ifndef",
                        name_token.span);
                    return;
                }
                auto& conditional = conditionals_.back();
                if (conditional.saw_else) {
                    diagnose(
                        "FSIM-SV-PP-009",
                        "duplicate `else in one conditional block",
                        name_token.span);
                }
                conditional.saw_else = true;
                conditional.active = conditional.parent_active && !conditional.branch_taken;
                conditional.branch_taken = true;
                reject_extra_directive_tokens(
                    tokens, arguments, end, name_token);
                return;
            }
            if (name == "endif") {
                if (conditionals_.empty()) {
                    diagnose(
                        "FSIM-SV-PP-010",
                        "`endif without a matching `ifdef or `ifndef",
                        name_token.span);
                    return;
                }
                reject_extra_directive_tokens(
                    tokens, arguments, end, name_token);
                conditionals_.pop_back();
                return;
            }

            if (!active()) {
                return;
            }
            if (name == "pragma") {
                if (!require_standard(
                        "`pragma", StandardRevision::SystemVerilog2005,
                        name_token)) {
                    return;
                }
                if (arguments >= end
                    || tokens[arguments].kind != TokenKind::Identifier
                    || tokens[arguments].text != "protect") {
                    return;
                }
                if (arguments + 1 >= end
                    || tokens[arguments + 1].kind != TokenKind::Identifier) {
                    diagnose(
                        "FSIM-SV-PP-011",
                        "`pragma protect requires a protect keyword",
                        name_token.span);
                    return;
                }
                const auto keyword = tokens[arguments + 1].text;
                if (keyword == "begin_protected") {
                    if (protected_envelope_) {
                        diagnose(
                            "FSIM-SV-PP-011",
                            "a protected envelope cannot be nested",
                            name_token.span);
                        return;
                    }
                    diagnose(
                        "FSIM-SV-PP-011",
                        "encrypted protected-envelope payload requires a decryption "
                        "provider",
                        name_token.span);
                    protected_envelope_ = true;
                    return;
                }
                if (keyword == "end_protected") {
                    if (!protected_envelope_) {
                        diagnose(
                            "FSIM-SV-PP-011",
                            "`pragma protect end_protected has no matching "
                            "begin_protected",
                            name_token.span);
                        return;
                    }
                    protected_envelope_ = false;
                }
                return;
            }
            if (name == "define") {
                define_macro(tokens, arguments, end, name_token);
                return;
            }
            if (name == "undef") {
                const auto macro_name = directive_macro_name(tokens, arguments, end, name_token);
                if (macro_name) {
                    macros_.erase(*macro_name);
                }
                return;
            }
            if (name == "undefineall") {
                if (!require_standard(
                        "`undefineall", StandardRevision::SystemVerilog2005,
                        name_token)) {
                    return;
                }
                reject_extra_directive_tokens(
                    tokens, arguments, end, name_token);
                macros_.clear();
                return;
            }
            if (name == "include") {
                include_file(
                    tokens, arguments, end, name_token, include_depth);
                return;
            }
            if ((name == "begin_keywords" || name == "end_keywords")
                && !require_standard(
                    "`" + name, StandardRevision::SystemVerilog2005,
                    name_token)) {
                return;
            }
            if (name == "timescale" || name == "default_nettype"
                || name == "resetall" || name == "celldefine"
                || name == "endcelldefine" || name == "begin_keywords"
                || name == "end_keywords" || name == "unconnected_drive"
                || name == "nounconnected_drive") {
                output_.push_back(tokens[begin]);
                output_.push_back(tokens[begin + 1]);
                using Difference = std::vector<Token>::difference_type;
                const std::vector<Token> directive_arguments {
                    tokens.begin() + static_cast<Difference>(arguments),
                    tokens.begin() + static_cast<Difference>(end)
                };
                auto expanded = expand_sequence(directive_arguments, 0, { });
                output_.insert(
                    output_.end(),
                    std::make_move_iterator(expanded.begin()),
                    std::make_move_iterator(expanded.end()));
                return;
            }
            diagnose(
                "FSIM-SV-PP-011",
                "recognized but unsupported compiler directive `" + name + "'",
                name_token.span);
        }

        [[nodiscard]] std::optional<std::string> directive_macro_name(
            const std::vector<Token>& tokens,
            const std::size_t begin,
            const std::size_t end,
            const Token& directive)
        {
            if (begin >= end || tokens[begin].kind != TokenKind::Identifier) {
                diagnose(
                    "FSIM-SV-PP-012",
                    "compiler directive `" + directive.text
                        + " requires a macro identifier",
                    directive.span);
                return std::nullopt;
            }
            if (begin + 1 != end) {
                diagnose(
                    "FSIM-SV-PP-013",
                    "unexpected tokens after `" + directive.text
                        + " macro identifier",
                    tokens[begin + 1].span);
            }
            return tokens[begin].text;
        }

        [[nodiscard]] std::optional<bool> directive_condition(
            const std::vector<Token>& tokens,
            const std::size_t begin,
            const std::size_t end,
            const Token& directive)
        {
            if (begin >= end
                || tokens[begin].kind != TokenKind::LeftParen) {
                const auto macro_name = directive_macro_name(
                    tokens, begin, end, directive);
                return macro_name
                    ? std::optional<bool> { macros_.contains(*macro_name) }
                    : std::nullopt;
            }
            if (!require_standard(
                    "parenthesized conditional-compilation expression",
                    StandardRevision::SystemVerilog2023,
                    tokens[begin])) {
                return std::nullopt;
            }

            auto position = begin;
            bool malformed = false;
            const auto reject = [&](const Token& token) {
                if (!malformed) {
                    diagnose(
                        "FSIM-SV-PP-053",
                        "conditional-compilation expression requires macro "
                        "identifiers joined by !, &&, ||, ->, or <->",
                        token.span,
                        token.expansion_stack);
                }
                malformed = true;
            };
            std::function<std::optional<bool>()> parse_or;
            std::function<std::optional<bool>()> parse_implication;
            std::function<std::optional<bool>()> parse_primary;
            std::function<std::optional<bool>()> parse_and;
            parse_primary = [&]() -> std::optional<bool> {
                if (position >= end) {
                    reject(directive);
                    return std::nullopt;
                }
                if (tokens[position].kind == TokenKind::Identifier) {
                    return macros_.contains(tokens[position++].text);
                }
                if (tokens[position].kind == TokenKind::Bang) {
                    ++position;
                    auto value = parse_primary();
                    if (value) {
                        *value = !*value;
                    }
                    return value;
                }
                if (tokens[position].kind == TokenKind::LeftParen) {
                    ++position;
                    auto value = parse_implication();
                    if (position >= end
                        || tokens[position].kind
                            != TokenKind::RightParen) {
                        reject(position < end ? tokens[position] : directive);
                        return std::nullopt;
                    }
                    ++position;
                    return value;
                }
                reject(tokens[position++]);
                return std::nullopt;
            };
            parse_and = [&]() -> std::optional<bool> {
                auto value = parse_primary();
                while (position < end
                    && tokens[position].kind == TokenKind::AndAnd) {
                    ++position;
                    const auto rhs = parse_primary();
                    if (!value || !rhs) {
                        value.reset();
                    } else {
                        *value = *value && *rhs;
                    }
                }
                return value;
            };
            parse_or = [&]() -> std::optional<bool> {
                auto value = parse_and();
                while (position < end
                    && tokens[position].kind == TokenKind::OrOr) {
                    ++position;
                    const auto rhs = parse_and();
                    if (!value || !rhs) {
                        value.reset();
                    } else {
                        *value = *value || *rhs;
                    }
                }
                return value;
            };
            parse_implication = [&]() -> std::optional<bool> {
                auto value = parse_or();
                while (position < end) {
                    bool implication = false;
                    bool equivalence = false;
                    if (tokens[position].kind == TokenKind::ThinArrow) {
                        implication = true;
                        ++position;
                    } else if (position + 1 < end
                        && tokens[position].kind == TokenKind::Less
                        && tokens[position + 1].kind == TokenKind::ThinArrow
                        && tokens[position].span.source_name
                            == tokens[position + 1].span.source_name
                        && tokens[position].span.end.offset
                            == tokens[position + 1].span.begin.offset) {
                        equivalence = true;
                        position += 2;
                    } else {
                        break;
                    }
                    const auto rhs = parse_or();
                    if (!value || !rhs) {
                        value.reset();
                    } else if (implication) {
                        *value = !*value || *rhs;
                    } else if (equivalence) {
                        *value = *value == *rhs;
                    }
                }
                return value;
            };

            auto value = parse_implication();
            if (position != end) {
                reject(tokens[position]);
            }
            return malformed ? std::nullopt : value;
        }

        void reject_extra_directive_tokens(
            const std::vector<Token>& tokens,
            const std::size_t begin,
            const std::size_t end,
            const Token& directive)
        {
            if (begin < end) {
                diagnose(
                    "FSIM-SV-PP-014",
                    "compiler directive `" + directive.text
                        + " does not accept arguments",
                    tokens[begin].span);
            }
        }

        void define_macro(
            const std::vector<Token>& tokens,
            const std::size_t begin,
            const std::size_t end,
            const Token& directive)
        {
            if (begin >= end || tokens[begin].kind != TokenKind::Identifier) {
                diagnose(
                    "FSIM-SV-PP-015",
                    "`define requires a macro identifier",
                    directive.span);
                return;
            }
            const auto& name = tokens[begin];
            std::size_t replacement_begin = begin + 1;
            std::optional<std::vector<MacroParameter>> parameters;
            if (replacement_begin < end
                && tokens[replacement_begin].kind == TokenKind::LeftParen
                && name.span.source_name
                    == tokens[replacement_begin].span.source_name
                && name.span.end.offset
                    == tokens[replacement_begin].span.begin.offset) {
                parameters.emplace();
                ++replacement_begin;
                while (replacement_begin < end
                    && tokens[replacement_begin].kind
                        != TokenKind::RightParen) {
                    if (tokens[replacement_begin].kind
                        != TokenKind::Identifier) {
                        diagnose(
                            "FSIM-SV-PP-016",
                            "function-like macro parameter list is malformed",
                            tokens[replacement_begin].span);
                        return;
                    }
                    MacroParameter parameter;
                    parameter.name = tokens[replacement_begin++].text;
                    if (std::any_of(
                            parameters->begin(),
                            parameters->end(),
                            [&](const MacroParameter& existing) {
                                return existing.name == parameter.name;
                            })) {
                        diagnose(
                            "FSIM-SV-PP-017",
                            "duplicate macro parameter '" + parameter.name + "'",
                            tokens[replacement_begin - 1].span);
                        return;
                    }
                    if (replacement_begin < end
                        && tokens[replacement_begin].kind
                            == TokenKind::Assign) {
                        (void)require_standard(
                            "a default macro argument",
                            StandardRevision::SystemVerilog2005,
                            tokens[replacement_begin]);
                        ++replacement_begin;
                        parameter.default_value.emplace();
                        std::size_t parentheses = 0;
                        std::size_t brackets = 0;
                        std::size_t braces = 0;
                        while (replacement_begin < end) {
                            const auto& token = tokens[replacement_begin];
                            if (parentheses == 0 && brackets == 0 && braces == 0
                                && (token.kind == TokenKind::Comma
                                    || token.kind == TokenKind::RightParen)) {
                                break;
                            }
                            if (token.kind == TokenKind::LeftParen) {
                                ++parentheses;
                            } else if (
                                token.kind == TokenKind::RightParen
                                && parentheses != 0) {
                                --parentheses;
                            } else if (token.kind == TokenKind::LeftBracket) {
                                ++brackets;
                            } else if (
                                token.kind == TokenKind::RightBracket
                                && brackets != 0) {
                                --brackets;
                            } else if (token.kind == TokenKind::LeftBrace) {
                                ++braces;
                            } else if (
                                token.kind == TokenKind::RightBrace
                                && braces != 0) {
                                --braces;
                            }
                            if (is_line_continuation(tokens[replacement_begin])) {
                                ++replacement_begin;
                                continue;
                            }
                            parameter.default_value->push_back(
                                tokens[replacement_begin++]);
                        }
                    }
                    parameters->push_back(std::move(parameter));
                    if (replacement_begin < end
                        && tokens[replacement_begin].kind == TokenKind::Comma) {
                        ++replacement_begin;
                        if (replacement_begin >= end
                            || tokens[replacement_begin].kind
                                == TokenKind::RightParen) {
                            diagnose(
                                "FSIM-SV-PP-016",
                                "function-like macro parameter list has a trailing comma",
                                tokens[replacement_begin - 1].span);
                            return;
                        }
                    } else if (
                        replacement_begin < end
                        && tokens[replacement_begin].kind
                            != TokenKind::RightParen) {
                        diagnose(
                            "FSIM-SV-PP-016",
                            "function-like macro parameters must be comma-separated",
                            tokens[replacement_begin].span);
                        return;
                    }
                }
                if (replacement_begin >= end
                    || tokens[replacement_begin].kind
                        != TokenKind::RightParen) {
                    diagnose(
                        "FSIM-SV-PP-018",
                        "unterminated function-like macro parameter list",
                        name.span);
                    return;
                }
                ++replacement_begin;
            }
            std::vector<Token> replacement;
            for (auto position = replacement_begin;
                position < end; ++position) {
                // The lexer represents a preprocessor line continuation as an escaped
                // identifier containing the backslash and newline. It joins physical
                // lines but contributes no replacement token.
                if (is_line_continuation(tokens[position])) {
                    continue;
                }
                replacement.push_back(tokens[position]);
            }
            for (std::size_t position = 0; position < replacement.size(); ++position) {
                if (replacement[position].kind != TokenKind::Backtick
                    || position + 1 >= replacement.size()) {
                    continue;
                }
                if (replacement[position + 1].kind == TokenKind::Backtick) {
                    (void)require_standard(
                        "macro token concatenation",
                        StandardRevision::SystemVerilog2005,
                        replacement[position]);
                } else if (replacement[position + 1].kind
                        == TokenKind::StringLiteral
                    && replacement[position + 1].text.size() >= 3
                    && replacement[position + 1]
                            .text[replacement[position + 1].text.size() - 2]
                        == '`') {
                    (void)require_standard(
                        "macro argument stringification",
                        StandardRevision::SystemVerilog2005,
                        replacement[position]);
                }
            }
            Macro definition {
                name.text, std::move(parameters), std::move(replacement),
                name.span
            };
            if (const auto existing = macros_.find(name.text);
                existing != macros_.end()
                && !same_macro(existing->second, definition)) {
                diagnose(
                    "FSIM-SV-PP-044",
                    "macro `" + name.text
                        + "' is redefined with a different parameter list or "
                          "replacement",
                    name.span,
                    { "previous definition at "
                        + location_text(existing->second.definition) });
                return;
            }
            macros_[name.text] = std::move(definition);
        }

        [[nodiscard]] std::optional<std::filesystem::path> resolve_include(
            const std::string_view requested,
            const bool quoted) const
        {
            std::error_code error;
            if (quoted && !include_stack_.empty()) {
                const auto local = normalized_path(
                    include_stack_.back().parent_path()
                    / fsim::support::path_from_utf8(requested));
                if (std::filesystem::is_regular_file(local, error) && !error) {
                    return local;
                }
                error.clear();
            }
            for (const auto& directory : include_directories_) {
                const auto candidate = normalized_path(
                    directory / fsim::support::path_from_utf8(requested));
                if (std::filesystem::is_regular_file(candidate, error) && !error) {
                    return candidate;
                }
                error.clear();
            }
            return std::nullopt;
        }

        void include_file(
            const std::vector<Token>& tokens,
            const std::size_t begin,
            const std::size_t end,
            const Token& directive,
            const std::size_t include_depth)
        {
            if (begin >= end) {
                diagnose(
                    "FSIM-SV-PP-019",
                    "`include requires a quoted or angle-bracket file name",
                    directive.span);
                return;
            }
            using Difference = std::vector<Token>::difference_type;
            const std::vector<Token> include_arguments {
                tokens.begin() + static_cast<Difference>(begin),
                tokens.begin() + static_cast<Difference>(end)
            };
            auto replacement = expand_sequence(include_arguments, 0, { });
            if (replacement.empty()) {
                diagnose(
                    "FSIM-SV-PP-020",
                    "macro-expanded `include name produced no tokens",
                    tokens[begin].span);
                return;
            }

            bool quoted = false;
            std::string requested;
            if (replacement.size() == 1
                && replacement.front().kind == TokenKind::StringLiteral
                && replacement.front().text.size() >= 2) {
                quoted = true;
                requested = replacement.front().text.substr(
                    1, replacement.front().text.size() - 2);
            } else if (
                replacement.size() >= 3
                && replacement.front().kind == TokenKind::Less
                && replacement.back().kind == TokenKind::Greater) {
                for (std::size_t index = 1;
                    index + 1 < replacement.size(); ++index) {
                    requested += replacement[index].text;
                }
            } else {
                diagnose(
                    "FSIM-SV-PP-021",
                    "`include name must be a string literal or <file>",
                    replacement.front().span,
                    replacement.front().expansion_stack);
                return;
            }
            const auto resolved = resolve_include(requested, quoted);
            if (!resolved) {
                diagnose(
                    "FSIM-SV-PP-022",
                    "cannot resolve Verilog include '" + requested + "'",
                    directive.span);
                return;
            }
            const auto previous_invocation = include_invocation_;
            include_invocation_ = directive.span;
            include_ancestry_.push_back(
                "included '" + fsim::support::path_to_utf8(*resolved) + "' from "
                + location_text(directive.span));
            process_file(*resolved, include_depth + 1);
            include_ancestry_.pop_back();
            include_invocation_ = previous_invocation;
        }

        [[nodiscard]] std::vector<std::vector<Token>> parse_arguments(
            const std::vector<Token>& tokens,
            std::size_t& index,
            const Macro& macro,
            const Token& invocation)
        {
            std::vector<std::vector<Token>> arguments;
            if (index >= tokens.size()
                || tokens[index].kind != TokenKind::LeftParen) {
                diagnose(
                    "FSIM-SV-PP-023",
                    "function-like macro `" + macro.name
                        + " requires an argument list",
                    invocation.span,
                    invocation.expansion_stack);
                return arguments;
            }
            ++index;
            std::vector<Token> current;
            std::size_t parentheses = 1;
            std::size_t brackets = 0;
            std::size_t braces = 0;
            bool closed = false;
            while (index < tokens.size()
                && tokens[index].kind != TokenKind::EndOfFile) {
                const auto& token = tokens[index++];
                if (token.kind == TokenKind::LeftParen) {
                    ++parentheses;
                } else if (token.kind == TokenKind::RightParen) {
                    if (parentheses == 1 && brackets == 0 && braces == 0) {
                        closed = true;
                        break;
                    }
                    --parentheses;
                } else if (token.kind == TokenKind::LeftBracket) {
                    ++brackets;
                } else if (token.kind == TokenKind::RightBracket
                    && brackets != 0) {
                    --brackets;
                } else if (token.kind == TokenKind::LeftBrace) {
                    ++braces;
                } else if (token.kind == TokenKind::RightBrace && braces != 0) {
                    --braces;
                }
                if (token.kind == TokenKind::Comma && parentheses == 1
                    && brackets == 0 && braces == 0) {
                    arguments.push_back(std::move(current));
                    current.clear();
                } else {
                    current.push_back(token);
                }
            }
            if (!closed) {
                diagnose(
                    "FSIM-SV-PP-024",
                    "unterminated argument list for macro `" + macro.name + "'",
                    invocation.span,
                    invocation.expansion_stack);
                return { };
            }
            if (!current.empty() || !arguments.empty()
                || (macro.parameters && !macro.parameters->empty())) {
                arguments.push_back(std::move(current));
            }
            return arguments;
        }

        [[nodiscard]] std::vector<Token> concatenate_tokens(
            std::vector<Token> tokens,
            const Token& invocation,
            const std::vector<std::string>& expansion_stack)
        {
            std::vector<Token> result;
            for (std::size_t index = 0; index < tokens.size(); ++index) {
                if (index + 1 < tokens.size()
                    && tokens[index].kind == TokenKind::Backtick
                    && tokens[index + 1].kind == TokenKind::Backtick) {
                    if (result.empty() || index + 2 >= tokens.size()) {
                        diagnose(
                            "FSIM-SV-PP-025",
                            "token concatenation requires tokens on both sides",
                            invocation.span,
                            expansion_stack);
                        index += 1;
                        continue;
                    }
                    const auto joined = result.back().text + tokens[index + 2].text;
                    auto lexed = lex(SourceText { invocation.span.source_name, joined }, language_);
                    if (lexed.ok() && lexed.tokens.size() == 3
                        && lexed.tokens[0].kind == result.back().kind
                        && lexed.tokens[0].text == result.back().text
                        && lexed.tokens[1].kind == tokens[index + 2].kind
                        && lexed.tokens[1].text == tokens[index + 2].text) {
                        result.push_back(std::move(tokens[index + 2]));
                        index += 2;
                        continue;
                    }
                    if (!lexed.ok() || lexed.tokens.size() != 2
                        || lexed.tokens.front().kind == TokenKind::EndOfFile) {
                        diagnose(
                            "FSIM-SV-PP-026",
                            "token concatenation produced invalid token '" + joined + "'",
                            invocation.span,
                            expansion_stack);
                        index += 2;
                        continue;
                    }
                    result.back().kind = lexed.tokens.front().kind;
                    result.back().text = joined;
                    result.back().span = invocation.span;
                    result.back().expansion_stack = expansion_stack;
                    index += 2;
                    continue;
                }
                result.push_back(std::move(tokens[index]));
            }
            return result;
        }

        [[nodiscard]] std::vector<Token> expand_sequence(
            const std::vector<Token>& tokens,
            const std::size_t depth,
            const std::vector<std::string>& expansion_stack)
        {
            std::vector<Token> result;
            std::size_t index = 0;
            while (index < tokens.size()) {
                if (tokens[index].kind == TokenKind::Backtick
                    && index + 1 < tokens.size()
                    && tokens[index + 1].kind == TokenKind::Identifier) {
                    auto expanded = expand_invocation(
                        tokens, index, depth, expansion_stack);
                    result.insert(
                        result.end(),
                        std::make_move_iterator(expanded.begin()),
                        std::make_move_iterator(expanded.end()));
                } else {
                    result.push_back(tokens[index++]);
                }
            }
            return result;
        }

        [[nodiscard]] std::vector<Token> select_replacement_conditionals(
            const Macro& macro,
            const SourceSpan& invocation,
            const std::vector<std::string>& expansion_stack)
        {
            struct ReplacementConditional {
                bool parent_active { };
                bool active { };
                bool branch_taken { };
                bool saw_else { };
            };
            std::vector<ReplacementConditional> conditionals;
            std::vector<Token> selected;
            const auto active_replacement = [&]() {
                return conditionals.empty() || conditionals.back().active;
            };
            const auto replacement_condition = [&](const std::size_t begin,
                                                   const std::size_t line,
                                                   const Token& directive)
                -> std::pair<std::optional<bool>, std::size_t> {
                if (begin >= macro.replacement.size()
                    || macro.replacement[begin].span.begin.line != line) {
                    return { std::nullopt, begin };
                }
                if (macro.replacement[begin].kind == TokenKind::Identifier) {
                    return {
                        macros_.contains(macro.replacement[begin].text), begin + 1
                    };
                }
                if (macro.replacement[begin].kind != TokenKind::LeftParen) {
                    return { std::nullopt, begin };
                }

                std::size_t depth = 0;
                auto end = begin;
                for (; end < macro.replacement.size(); ++end) {
                    const auto& token = macro.replacement[end];
                    if (token.span.begin.line != line) {
                        break;
                    }
                    if (token.kind == TokenKind::LeftParen) {
                        ++depth;
                    } else if (token.kind == TokenKind::RightParen) {
                        if (depth == 0) {
                            break;
                        }
                        --depth;
                        if (depth == 0) {
                            ++end;
                            return {
                                directive_condition(
                                    macro.replacement, begin, end, directive),
                                end
                            };
                        }
                    }
                }
                return { std::nullopt, begin };
            };
            for (std::size_t index = 0; index < macro.replacement.size();) {
                const auto directive = macro.replacement[index].kind == TokenKind::Backtick
                        && index + 1 < macro.replacement.size()
                        && macro.replacement[index + 1].kind
                            == TokenKind::Identifier
                    ? macro.replacement[index + 1].text
                    : std::string { };
                const bool conditional_directive = directive == "ifdef" || directive == "ifndef"
                    || directive == "elsif" || directive == "else"
                    || directive == "endif";
                if (!conditional_directive) {
                    if (active_replacement()) {
                        selected.push_back(macro.replacement[index]);
                    }
                    ++index;
                    continue;
                }

                const auto directive_line = macro.replacement[index + 1].span.begin.line;
                const auto malformed = [&](const std::string& message) {
                    diagnose(
                        "FSIM-SV-PP-049", message, invocation,
                        expansion_stack);
                };
                if (directive == "ifdef" || directive == "ifndef") {
                    const auto [condition, next] = replacement_condition(
                        index + 2, directive_line,
                        macro.replacement[index + 1]);
                    if (!condition) {
                        malformed(
                            "conditional directive inside macro `" + macro.name
                            + "' requires an identifier or a valid 2023 Boolean expression");
                        index += 2;
                        continue;
                    }
                    const bool parent = active_replacement();
                    const bool take = directive == "ifdef" ? *condition : !*condition;
                    conditionals.push_back(
                        { parent, parent && take, parent && take, false });
                    index = next;
                    continue;
                } else if (directive == "elsif") {
                    const auto [condition, next] = replacement_condition(
                        index + 2, directive_line,
                        macro.replacement[index + 1]);
                    if (conditionals.empty()) {
                        malformed("`elsif inside a macro has no matching conditional");
                    } else if (!condition) {
                        malformed(
                            "`elsif inside a macro requires an identifier or a valid "
                            "2023 Boolean expression");
                    } else {
                        auto& conditional = conditionals.back();
                        if (conditional.saw_else) {
                            malformed("`elsif inside a macro cannot follow `else");
                        }
                        const bool take = conditional.parent_active && !conditional.branch_taken
                            && *condition;
                        conditional.active = take;
                        conditional.branch_taken = conditional.branch_taken || take;
                    }
                    index = condition ? next : index + 2;
                    continue;
                } else if (directive == "else") {
                    if (conditionals.empty()) {
                        malformed("`else inside a macro has no matching conditional");
                    } else {
                        auto& conditional = conditionals.back();
                        if (conditional.saw_else) {
                            malformed("duplicate `else inside a macro conditional");
                        }
                        conditional.saw_else = true;
                        conditional.active = conditional.parent_active && !conditional.branch_taken;
                        conditional.branch_taken = true;
                    }
                } else {
                    if (conditionals.empty()) {
                        malformed("`endif inside a macro has no matching conditional");
                    } else {
                        conditionals.pop_back();
                    }
                }
                index += 2;
            }
            if (!conditionals.empty()) {
                diagnose(
                    "FSIM-SV-PP-050",
                    "macro `" + macro.name
                        + "' contains an unterminated conditional directive",
                    invocation, expansion_stack);
            }
            return selected;
        }

        [[nodiscard]] std::optional<std::string> stringify_replacement(
            const std::string_view body,
            const Macro& macro,
            const std::vector<std::vector<Token>>& arguments,
            const SourceSpan& invocation,
            const std::size_t depth,
            const std::vector<std::string>& expansion_stack)
        {
            std::string result { "\"" };
            const auto append_escaped = [&](const std::string_view spelling) {
                for (const char character : spelling) {
                    if (character == '\\' || character == '"') {
                        result.push_back('\\');
                    }
                    result.push_back(character);
                }
            };
            const auto parameter_index = [&](const std::string_view name)
                -> std::optional<std::size_t> {
                if (!macro.parameters) {
                    return std::nullopt;
                }
                const auto found = std::find_if(
                    macro.parameters->begin(), macro.parameters->end(),
                    [&](const MacroParameter& parameter) {
                        return parameter.name == name;
                    });
                if (found == macro.parameters->end()) {
                    return std::nullopt;
                }
                return static_cast<std::size_t>(
                    std::distance(macro.parameters->begin(), found));
            };
            const auto append_argument = [&](const std::size_t parameter) {
                for (std::size_t index = 0;
                    index < arguments[parameter].size(); ++index) {
                    if (index != 0) {
                        result.push_back(' ');
                    }
                    append_escaped(arguments[parameter][index].text);
                }
            };

            std::size_t index = 0;
            while (index < body.size()) {
                if (body[index] == '`') {
                    if (index + 1 < body.size() && body[index + 1] == '`') {
                        index += 2;
                        continue;
                    }
                    auto name_end = index + 1;
                    while (name_end < body.size()
                        && (std::isalnum(static_cast<unsigned char>(body[name_end]))
                                != 0
                            || body[name_end] == '_' || body[name_end] == '$')) {
                        ++name_end;
                    }
                    if (name_end == index + 1) {
                        diagnose(
                            "FSIM-SV-PP-051",
                            "special macro string contains a backtick without an identifier",
                            invocation, expansion_stack);
                        return std::nullopt;
                    }
                    std::vector<Token> nested {
                        { TokenKind::Backtick, "`", invocation, expansion_stack },
                        { TokenKind::Identifier,
                            std::string { body.substr(index + 1, name_end - index - 1) },
                            invocation, expansion_stack }
                    };
                    std::size_t nested_index = 0;
                    auto expanded = expand_invocation(
                        nested, nested_index, depth + 1, expansion_stack);
                    for (const auto& token : expanded) {
                        append_escaped(token.text);
                    }
                    index = name_end;
                    continue;
                }
                const auto first = static_cast<unsigned char>(body[index]);
                if (std::isalpha(first) != 0 || body[index] == '_'
                    || body[index] == '$') {
                    auto name_end = index + 1;
                    while (name_end < body.size()
                        && (std::isalnum(static_cast<unsigned char>(body[name_end]))
                                != 0
                            || body[name_end] == '_' || body[name_end] == '$')) {
                        ++name_end;
                    }
                    if (const auto parameter = parameter_index(body.substr(index, name_end - index))) {
                        append_argument(*parameter);
                    } else {
                        result.append(body.substr(index, name_end - index));
                    }
                    index = name_end;
                    continue;
                }
                result.push_back(body[index++]);
            }
            result.push_back('"');
            return result;
        }

        [[nodiscard]] std::vector<Token> expand_invocation(
            const std::vector<Token>& tokens,
            std::size_t& index,
            const std::size_t depth,
            const std::vector<std::string>& inherited_stack)
        {
            const auto tick = tokens[index++];
            if (index >= tokens.size()
                || tokens[index].kind != TokenKind::Identifier) {
                diagnose(
                    "FSIM-SV-PP-027",
                    "a backtick must be followed by a macro identifier",
                    tick.span,
                    inherited_stack);
                return { };
            }
            const auto name_token = tokens[index++];
            const auto invocation_span = cover(tick.span, name_token.span);

            if (const auto value
                = systemverilog_coverage_constant(name_token.text)) {
                if (!require_standard(
                        "`" + name_token.text,
                        StandardRevision::SystemVerilog2005,
                        name_token)) {
                    return { };
                }
                if (*value < 0) {
                    return {
                        Token { TokenKind::Minus, "-", invocation_span,
                            inherited_stack },
                        Token { TokenKind::Number,
                            std::to_string(-*value), invocation_span,
                            inherited_stack }
                    };
                }
                return { Token { TokenKind::Number,
                    std::to_string(*value), invocation_span,
                    inherited_stack } };
            }

            if (name_token.text == "__FILE__") {
                if (!require_standard(
                        "`__FILE__", StandardRevision::SystemVerilog2005,
                        name_token)) {
                    return { };
                }
                Token token {
                    TokenKind::StringLiteral,
                    string_literal_spelling(invocation_span.source_name),
                    invocation_span,
                    inherited_stack
                };
                return { std::move(token) };
            }
            if (name_token.text == "__LINE__") {
                if (!require_standard(
                        "`__LINE__", StandardRevision::SystemVerilog2005,
                        name_token)) {
                    return { };
                }
                Token token {
                    TokenKind::Number,
                    std::to_string(invocation_span.begin.line),
                    invocation_span,
                    inherited_stack
                };
                return { std::move(token) };
            }
            const auto found = macros_.find(name_token.text);
            if (found == macros_.end()) {
                diagnose(
                    "FSIM-SV-PP-028",
                    "undefined Verilog macro or compiler directive `"
                        + name_token.text + "'",
                    invocation_span,
                    inherited_stack);
                return { };
            }
            const auto& macro = found->second;
            if (depth >= options_.maximum_macro_expansion_depth
                || expanding_.contains(macro.name)) {
                diagnose(
                    "FSIM-SV-PP-029",
                    "recursive or excessively deep expansion of macro `"
                        + macro.name + "'",
                    invocation_span,
                    inherited_stack);
                return { };
            }

            std::vector<std::vector<Token>> arguments;
            if (macro.parameters) {
                arguments = parse_arguments(tokens, index, macro, name_token);
                const auto provided_arguments = arguments.size();
                if (provided_arguments > macro.parameters->size()) {
                    diagnose(
                        "FSIM-SV-PP-030",
                        "macro `" + macro.name + "' expects "
                            + std::to_string(macro.parameters->size())
                            + " argument(s), received "
                            + std::to_string(provided_arguments),
                        invocation_span,
                        inherited_stack);
                    return { };
                }
                arguments.resize(macro.parameters->size());
                for (std::size_t parameter = 0;
                    parameter < macro.parameters->size(); ++parameter) {
                    const bool missing = parameter >= provided_arguments;
                    const bool omitted = missing || arguments[parameter].empty();
                    if (omitted
                        && (*macro.parameters)[parameter].default_value) {
                        arguments[parameter] = *(*macro.parameters)[parameter].default_value;
                    } else if (missing) {
                        diagnose(
                            "FSIM-SV-PP-030",
                            "macro `" + macro.name
                                + "' is missing required argument '"
                                + (*macro.parameters)[parameter].name + "'",
                            invocation_span,
                            inherited_stack);
                        return { };
                    }
                }
            }

            auto expansion_stack = inherited_stack;
            expansion_stack.push_back(
                "macro `" + macro.name + "' defined at "
                + location_text(macro.definition) + ", expanded at "
                + location_text(invocation_span));
            expanding_.insert(macro.name);
            auto expanded_arguments = arguments;
            for (auto& argument : expanded_arguments) {
                argument = expand_sequence(argument, depth + 1, expansion_stack);
            }
            const auto replacement_tokens = select_replacement_conditionals(
                macro, invocation_span, expansion_stack);
            std::vector<Token> substituted;
            for (std::size_t replacement_index = 0;
                replacement_index < replacement_tokens.size();
                ++replacement_index) {
                const auto& replacement = replacement_tokens[replacement_index];
                if (replacement.kind == TokenKind::Backtick
                    && replacement_index + 1 < replacement_tokens.size()
                    && replacement_tokens[replacement_index + 1].kind
                        == TokenKind::StringLiteral) {
                    const auto& string_token = replacement_tokens[replacement_index + 1];
                    if (string_token.text.size() >= 3
                        && string_token.text[string_token.text.size() - 2]
                            == '`') {
                        if (auto stringified = stringify_replacement(
                                std::string_view { string_token.text }.substr(
                                    1, string_token.text.size() - 3),
                                macro, expanded_arguments, invocation_span, depth,
                                expansion_stack)) {
                            substituted.push_back({ TokenKind::StringLiteral,
                                std::move(*stringified),
                                invocation_span,
                                expansion_stack });
                            ++replacement_index;
                            continue;
                        }
                    }
                }
                std::optional<std::size_t> parameter;
                if (macro.parameters
                    && replacement.kind == TokenKind::Identifier) {
                    const auto match = std::find_if(
                        macro.parameters->begin(),
                        macro.parameters->end(),
                        [&](const MacroParameter& candidate) {
                            return candidate.name == replacement.text;
                        });
                    if (match != macro.parameters->end()) {
                        parameter = static_cast<std::size_t>(
                            std::distance(macro.parameters->begin(), match));
                    }
                }
                if (parameter) {
                    for (auto argument : expanded_arguments[*parameter]) {
                        argument.expansion_stack = expansion_stack;
                        substituted.push_back(std::move(argument));
                    }
                } else {
                    auto token = replacement;
                    token.span = invocation_span;
                    token.expansion_stack = expansion_stack;
                    substituted.push_back(std::move(token));
                }
            }
            substituted = concatenate_tokens(
                std::move(substituted), name_token, expansion_stack);
            auto expanded = expand_sequence(substituted, depth + 1, expansion_stack);
            expanding_.erase(macro.name);
            return expanded;
        }

        Language language_;
        StandardRevision standard_revision_;
        PreprocessorOptions options_;
        std::vector<std::filesystem::path> include_directories_;
        std::unordered_map<std::string, Macro> macros_;
        std::unordered_set<std::string> expanding_;
        std::vector<Conditional> conditionals_;
        std::vector<std::filesystem::path> include_stack_;
        std::optional<SourceSpan> include_invocation_;
        std::vector<std::string> include_ancestry_;
        std::unordered_map<std::string, std::string> source_snapshots_;
        std::unordered_set<std::string> current_dependency_names_;
        std::vector<PreprocessedDependency> current_dependencies_;
        std::vector<PreprocessedRoot> roots_;
        std::vector<PreprocessedDependency> inputs_;
        std::vector<Token> output_;
        std::vector<Diagnostic> diagnostics_;
        std::optional<Token> root_eof_;
        bool protected_envelope_ { };
    };

} // namespace

PreprocessResult preprocess_verilog_file(
    const std::filesystem::path& path,
    const Language language,
    const PreprocessorOptions& options)
{
    return VerilogPreprocessor(language, options).run(path);
}

PreprocessResult preprocess_verilog(
    SourceText source,
    const Language language,
    const PreprocessorOptions& options)
{
    return VerilogPreprocessor(language, options).run(std::move(source));
}

PreprocessCompilationUnitResult preprocess_verilog_compilation_unit(
    const std::vector<std::filesystem::path>& paths,
    const Language language,
    const PreprocessorOptions& options)
{
    return VerilogPreprocessor(language, options).run(paths);
}

} // namespace fsim::frontend
