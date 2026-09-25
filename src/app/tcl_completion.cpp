// SPDX-License-Identifier: Apache-2.0
#include "tcl_completion.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <tuple>
#include <utility>

namespace fsim::app::tcl_completion {
namespace {

    constexpr std::size_t kMaximumInputBytes = 1024U * 1024U;
    constexpr std::size_t kMaximumTokens = 65536U;
    constexpr std::size_t kMaximumProviderItems = 65536U;
    constexpr std::size_t kMaximumSpans = 4096U;
    constexpr std::size_t kMaximumCandidates = 256U;
    constexpr std::size_t kMaximumNesting = 128U;

    enum class QuoteKind : std::uint8_t {
        bare,
        quoted,
        braced,
    };

    struct Word {
        std::size_t begin { };
        std::size_t end { };
        std::size_t content_begin { };
        std::size_t content_end { };
        QuoteKind quote { QuoteKind::bare };
    };

    struct CursorCapture {
        bool found { false };
        bool has_word { false };
        std::size_t nesting { };
        std::vector<Word> preceding_words;
        Word word;
    };

    bool is_ascii_space(const char value)
    {
        return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
    }

    bool is_word_separator(const char value)
    {
        return is_ascii_space(value) || value == ';';
    }

    bool is_utf8_continuation(const unsigned char value)
    {
        return (value & 0xC0U) == 0x80U;
    }

    std::optional<std::size_t> invalid_utf8_offset(const std::string_view text)
    {
        for (std::size_t index = 0; index < text.size();) {
            const auto lead = static_cast<unsigned char>(text[index]);
            if (lead <= 0x7FU) {
                ++index;
                continue;
            }

            std::size_t length = 0;
            std::uint32_t codepoint = 0;
            if (lead >= 0xC2U && lead <= 0xDFU) {
                length = 2U;
                codepoint = lead & 0x1FU;
            } else if (lead >= 0xE0U && lead <= 0xEFU) {
                length = 3U;
                codepoint = lead & 0x0FU;
            } else if (lead >= 0xF0U && lead <= 0xF4U) {
                length = 4U;
                codepoint = lead & 0x07U;
            } else {
                return index;
            }
            if (index + length > text.size()) {
                return index;
            }
            for (std::size_t offset = 1U; offset < length; ++offset) {
                const auto continuation = static_cast<unsigned char>(text[index + offset]);
                if (!is_utf8_continuation(continuation)) {
                    return index + offset;
                }
                codepoint = (codepoint << 6U) | (continuation & 0x3FU);
            }
            if ((length == 3U && codepoint < 0x800U)
                || (length == 4U && codepoint < 0x10000U)
                || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)
                || codepoint > 0x10FFFFU) {
                return index;
            }
            ++index;
            if (length > 1U) {
                index += length - 1U;
            }
        }
        return { };
    }

    bool is_utf8_boundary(const std::string_view text, const std::size_t offset)
    {
        return offset <= text.size()
            && (offset == text.size() || !is_utf8_continuation(static_cast<unsigned char>(text[offset])));
    }

    bool has_capabilities(const CapabilityMask available, const CapabilityMask required)
    {
        return (available & required) == required;
    }

    bool starts_with(const std::string_view value, const std::string_view prefix)
    {
        return value.size() >= prefix.size() && value.substr(0U, prefix.size()) == prefix;
    }

    std::string_view normalized_command_name(std::string_view value)
    {
        if (starts_with(value, "::")) {
            value.remove_prefix(2U);
        }
        return value;
    }

    std::string decode_prefix(const std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (std::size_t index = 0; index < value.size(); ++index) {
            const char current = value[index];
            if (current != '\\' || index + 1U >= value.size()) {
                result.push_back(current);
                continue;
            }
            const char escaped = value[++index];
            switch (escaped) {
            case '\n':
                result.push_back(' ');
                while (index + 1U < value.size()
                    && (value[index + 1U] == ' ' || value[index + 1U] == '\t')) {
                    ++index;
                }
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
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'v':
                result.push_back('\v');
                break;
            default:
                result.push_back(escaped);
                break;
            }
        }
        return result;
    }

    bool is_variable_name_byte(const unsigned char value)
    {
        if (value >= 0x80U) {
            return true;
        }
        return std::isalnum(value) != 0 || value == '_' || value == ':';
    }

    bool is_escaped(const std::string_view text, const std::size_t position)
    {
        std::size_t slash_count = 0U;
        while (position > slash_count
            && text[position - slash_count - 1U] == '\\') {
            ++slash_count;
        }
        return (slash_count & 1U) != 0U;
    }

    void append_span(
        CompletionResult& result,
        const std::size_t begin,
        const std::size_t end,
        const SpanKind kind,
        const Severity severity = Severity::none,
        std::string message = { })
    {
        if (begin > end) {
            return;
        }
        if (result.spans.size() >= kMaximumSpans) {
            result.spans_truncated = true;
            return;
        }
        result.spans.push_back(Span { begin, end, kind, severity, std::move(message) });
    }

    class TclParser {
    public:
        TclParser(const std::string_view input, const std::size_t cursor, CompletionResult& result)
            : m_input(input)
            , m_cursor(cursor)
            , m_result(result)
        {
        }

        void run()
        {
            std::size_t position = 0U;
            parse_script(position, 0U, false, 0U);
            if (!m_cursor_capture.found && m_cursor == m_input.size()) {
                capture(m_cursor, 0U, { }, { });
            }
        }

        [[nodiscard]] const CursorCapture& cursor_capture() const
        {
            return m_cursor_capture;
        }

        [[nodiscard]] bool cursor_in_comment() const
        {
            return m_cursor_in_comment;
        }

        [[nodiscard]] bool complexity_limited() const
        {
            return m_complexity_limited;
        }

    private:
        void capture(
            const std::size_t offset,
            const std::size_t nesting,
            const std::vector<Word>& preceding,
            const std::optional<Word>& current)
        {
            if (offset != m_cursor) {
                return;
            }
            const bool better = !m_cursor_capture.found
                || nesting > m_cursor_capture.nesting
                || (nesting == m_cursor_capture.nesting && current.has_value() && !m_cursor_capture.has_word);
            if (!better) {
                return;
            }
            m_cursor_capture.found = true;
            m_cursor_capture.nesting = nesting;
            m_cursor_capture.has_word = current.has_value();
            m_cursor_capture.preceding_words = preceding;
            m_cursor_capture.word = current.value_or(Word { });
        }

        bool parse_script(
            std::size_t& position,
            const std::size_t nesting,
            const bool bracketed,
            const std::size_t opener)
        {
            if (nesting > kMaximumNesting) {
                m_complexity_limited = true;
                position = m_input.size();
                return false;
            }

            std::vector<Word> words;
            while (position < m_input.size()) {
                if (m_cursor == position) {
                    capture(position, nesting, words, { });
                }
                const char current = m_input[position];
                if (bracketed && current == ']') {
                    ++position;
                    append_span(m_result, opener, position, SpanKind::command_substitution);
                    return true;
                }
                if (current == ' ' || current == '\t' || current == '\r' || current == '\f' || current == '\v') {
                    ++position;
                    continue;
                }
                if (current == '\n' || current == ';') {
                    words.clear();
                    ++position;
                    continue;
                }
                if (current == '#' && words.empty()) {
                    const auto comment_begin = position;
                    while (position < m_input.size() && m_input[position] != '\n') {
                        ++position;
                    }
                    append_span(m_result, comment_begin, position, SpanKind::comment);
                    if (m_cursor >= comment_begin && m_cursor <= position) {
                        m_cursor_in_comment = true;
                    }
                    continue;
                }
                if (m_token_count >= kMaximumTokens) {
                    m_complexity_limited = true;
                    break;
                }

                const auto prior_words = words;
                const auto word = parse_word(position, nesting);
                if (m_cursor >= word.begin && m_cursor <= word.end) {
                    capture(m_cursor, nesting, prior_words, word);
                }
                if (words.empty()) {
                    append_span(m_result, word.begin, word.end, SpanKind::command);
                } else if (word.content_begin < word.content_end
                    && m_input[word.content_begin] == '-') {
                    append_span(m_result, word.begin, word.end, SpanKind::option);
                }
                words.push_back(word);
                ++m_token_count;
                if (position <= word.begin) {
                    ++position;
                }
            }

            if (m_cursor == m_input.size()) {
                capture(m_cursor, nesting, words, { });
            }
            if (bracketed) {
                append_span(
                    m_result,
                    opener,
                    m_input.size(),
                    SpanKind::command_substitution,
                    Severity::none);
                append_span(
                    m_result,
                    opener,
                    m_input.size(),
                    SpanKind::diagnostic,
                    Severity::warning,
                    "incomplete command substitution");
                return false;
            }
            return true;
        }

        Word parse_word(std::size_t& position, const std::size_t nesting)
        {
            Word word;
            word.begin = position;
            word.content_begin = position;
            if (m_input[position] == '{') {
                word.quote = QuoteKind::braced;
                word.content_begin = position + 1U;
                std::size_t brace_depth = 1U;
                ++position;
                while (position < m_input.size()) {
                    const char current = m_input[position];
                    if (current == '\\' && position + 1U < m_input.size()) {
                        position += 2U;
                        continue;
                    }
                    if (current == '{') {
                        ++brace_depth;
                    } else if (current == '}' && --brace_depth == 0U) {
                        word.content_end = position;
                        ++position;
                        word.end = position;
                        append_span(m_result, word.begin, word.end, SpanKind::braced_word);
                        return word;
                    }
                    ++position;
                }
                word.content_end = m_input.size();
                word.end = m_input.size();
                append_span(m_result, word.begin, word.end, SpanKind::braced_word);
                append_span(
                    m_result,
                    word.begin,
                    word.end,
                    SpanKind::diagnostic,
                    Severity::warning,
                    "incomplete braced word");
                return word;
            }

            if (m_input[position] == '"') {
                word.quote = QuoteKind::quoted;
                word.content_begin = position + 1U;
                ++position;
                while (position < m_input.size()) {
                    const char current = m_input[position];
                    if (current == '\\' && position + 1U < m_input.size()) {
                        position += 2U;
                        continue;
                    }
                    if (current == '"') {
                        word.content_end = position;
                        ++position;
                        word.end = position;
                        append_span(m_result, word.begin, word.end, SpanKind::quoted_string);
                        return word;
                    }
                    if (current == '[') {
                        const auto opener = position;
                        ++position;
                        parse_script(position, nesting + 1U, true, opener);
                        continue;
                    }
                    if (current == '$') {
                        add_variable_span(position, m_input.size());
                    }
                    ++position;
                }
                word.content_end = m_input.size();
                word.end = m_input.size();
                append_span(m_result, word.begin, word.end, SpanKind::quoted_string);
                append_span(
                    m_result,
                    word.begin,
                    word.end,
                    SpanKind::diagnostic,
                    Severity::warning,
                    "incomplete quoted word");
                return word;
            }

            while (position < m_input.size()) {
                const char current = m_input[position];
                if (is_word_separator(current) || (nesting > 0U && current == ']')) {
                    break;
                }
                if (current == '\\' && position + 1U < m_input.size()) {
                    position += 2U;
                    continue;
                }
                if (current == '[') {
                    const auto opener = position;
                    ++position;
                    parse_script(position, nesting + 1U, true, opener);
                    continue;
                }
                if (current == '$') {
                    add_variable_span(position, m_input.size());
                }
                ++position;
            }
            word.content_end = position;
            word.end = position;
            return word;
        }

        void add_variable_span(const std::size_t dollar, const std::size_t limit)
        {
            if (is_escaped(m_input, dollar) || dollar + 1U >= limit) {
                return;
            }
            std::size_t end = dollar + 1U;
            if (m_input[end] == '{') {
                ++end;
                while (end < limit && m_input[end] != '}') {
                    ++end;
                }
                if (end < limit) {
                    ++end;
                }
                append_span(m_result, dollar, end, SpanKind::variable);
                return;
            }
            while (end < limit && is_variable_name_byte(static_cast<unsigned char>(m_input[end]))) {
                ++end;
            }
            if (end > dollar + 1U) {
                append_span(m_result, dollar, end, SpanKind::variable);
            }
        }

        std::string_view m_input;
        std::size_t m_cursor { };
        CompletionResult& m_result;
        CursorCapture m_cursor_capture;
        std::size_t m_token_count { };
        bool m_cursor_in_comment { false };
        bool m_complexity_limited { false };
    };

    std::string decode_word(const std::string_view input, const Word& word)
    {
        if (word.content_begin > word.content_end || word.content_end > input.size()) {
            return { };
        }
        return decode_prefix(input.substr(word.content_begin, word.content_end - word.content_begin));
    }

    std::string quote_tcl_word(const std::string_view value)
    {
        if (value.empty()) {
            return "{}";
        }
        std::string result;
        result.reserve(value.size() * 2U);
        for (const char current : value) {
            if (is_ascii_space(current) || current == ';' || current == '[' || current == ']'
                || current == '$' || current == '"' || current == '{' || current == '}'
                || current == '\\') {
                result.push_back('\\');
            }
            result.push_back(current);
        }
        return result;
    }

    struct VariableContext {
        bool found { false };
        std::size_t name_begin { };
        std::size_t name_end { };
        std::string prefix;
    };

    VariableContext variable_context(
        const std::string_view input,
        const Word& word,
        const std::size_t cursor)
    {
        VariableContext result;
        if (word.quote == QuoteKind::braced || cursor < word.content_begin) {
            return result;
        }

        const auto limit = std::min(word.content_end, cursor);
        std::size_t index = word.content_begin;
        while (index < limit) {
            if (input[index] == '\\') {
                index = std::min(index + 2U, limit);
                continue;
            }
            if (input[index] == '[') {
                std::size_t bracket_depth = 1U;
                ++index;
                while (index < limit && bracket_depth > 0U) {
                    if (input[index] == '\\') {
                        index = std::min(index + 2U, limit);
                    } else if (input[index] == '[') {
                        ++bracket_depth;
                        ++index;
                    } else if (input[index] == ']') {
                        --bracket_depth;
                        ++index;
                    } else {
                        ++index;
                    }
                }
                continue;
            }
            if (input[index] != '$') {
                ++index;
                continue;
            }

            const auto dollar = index;
            if (index + 1U < word.content_end && input[index + 1U] == '{') {
                const auto name_begin = index + 2U;
                auto name_end = name_begin;
                while (name_end < word.content_end && input[name_end] != '}') {
                    ++name_end;
                }
                if (cursor >= name_begin && cursor <= name_end) {
                    result.found = true;
                    result.name_begin = name_begin;
                    result.name_end = name_end;
                    result.prefix = decode_prefix(input.substr(name_begin, cursor - name_begin));
                }
                index = name_end < word.content_end ? name_end + 1U : name_end;
                continue;
            }

            const auto name_begin = dollar + 1U;
            auto name_end = name_begin;
            while (name_end < word.content_end
                && is_variable_name_byte(static_cast<unsigned char>(input[name_end]))) {
                ++name_end;
            }
            if (cursor >= name_begin && cursor <= name_end) {
                result.found = true;
                result.name_begin = name_begin;
                result.name_end = name_end;
                result.prefix = decode_prefix(input.substr(name_begin, cursor - name_begin));
            }
            index = name_end > dollar + 1U ? name_end : dollar + 1U;
        }
        return result;
    }

    std::string display_command_name(const std::string_view name, const std::string_view prefix)
    {
        std::string result(name);
        if (!starts_with(prefix, "::") && starts_with(result, "::")) {
            result.erase(0U, 2U);
        } else if (starts_with(prefix, "::") && !starts_with(result, "::")) {
            result.insert(0U, "::");
        }
        return result;
    }

    bool item_allowed(const CompletionRequest& request, const CompletionItem& item)
    {
        return has_capabilities(request.capabilities, item.required_capabilities);
    }

    std::string command_help(const CompletionCommand& command)
    {
        std::string result;
        if (!command.usage.empty()) {
            result.append(command.usage);
        }
        if (!command.help.empty()) {
            if (!result.empty()) {
                result.append("\n");
            }
            result.append(command.help);
        }
        return result;
    }

    std::string subcommand_help(const CompletionSubcommand& subcommand)
    {
        std::string result;
        if (!subcommand.usage.empty()) {
            result.append(subcommand.usage);
        }
        if (!subcommand.help.empty()) {
            if (!result.empty()) {
                result.append("\n");
            }
            result.append(subcommand.help);
        }
        return result;
    }

    const CompletionCommand* find_command(
        const CompletionSnapshot& snapshot,
        const std::string_view name)
    {
        const auto normalized = normalized_command_name(name);
        for (const auto& command : snapshot.commands) {
            if (normalized_command_name(command.name) == normalized) {
                return &command;
            }
        }
        return nullptr;
    }

    std::span<const CompletionItem> items_for_domain(
        const CompletionSnapshot& snapshot,
        const ArgumentDomain domain)
    {
        switch (domain) {
        case ArgumentDomain::library:
            return snapshot.libraries;
        case ArgumentDomain::snapshot:
            return snapshot.snapshots;
        case ArgumentDomain::compiled_definition:
            return snapshot.compiled_definitions;
        case ArgumentDomain::design_object:
            return snapshot.design_objects;
        case ArgumentDomain::package:
            return snapshot.packages;
        case ArgumentDomain::type:
            return snapshot.types;
        case ArgumentDomain::debugger_entity:
            return snapshot.debugger_entities;
        case ArgumentDomain::tcl_namespace:
            return snapshot.namespaces;
        case ArgumentDomain::tcl_procedure:
            return snapshot.procedures;
        case ArgumentDomain::tcl_variable:
            return snapshot.variables;
        case ArgumentDomain::source_path:
        case ArgumentDomain::directory_path:
            return snapshot.paths;
        case ArgumentDomain::literal:
            return { };
        }
        return { };
    }

    constexpr std::string_view kTclBuiltins[] {
        "after", "append", "apply", "array", "auto_execok", "auto_import", "auto_load",
        "auto_load_index", "auto_qualify", "auto_reset", "bgerror", "binary", "break",
        "catch", "cd", "chan", "clock", "close", "concat", "continue", "dict",
        "encoding", "eof", "error", "eval", "exec", "exit", "expr", "fblocked",
        "fconfigure", "fcopy", "file", "fileevent", "flush", "for", "foreach", "format",
        "gets", "glob", "global", "history", "incr", "info", "interp", "join", "lappend",
        "lassign", "lindex", "linsert", "list", "llength", "load", "lmap", "lrange",
        "lrepeat", "lreplace", "lreverse", "lsearch", "lset", "lsort", "namespace", "open",
        "package", "parray", "pid", "proc", "puts", "pwd", "read", "regexp", "regsub",
        "rename", "return", "scan", "seek", "set", "socket", "source", "split", "string",
        "subst", "switch", "tailcall", "tell", "time", "trace", "try", "unset", "update",
        "uplevel", "upvar", "variable", "vwait", "while", "zlib", "::tcl::mathop::+",
        "::tcl::mathop::-", "::tcl::mathop::*", "::tcl::mathop::/", "::tcl::mathfunc::abs",
        "::tcl::mathfunc::acos", "::tcl::mathfunc::asin", "::tcl::mathfunc::atan",
        "::tcl::mathfunc::bool", "::tcl::mathfunc::ceil", "::tcl::mathfunc::cos",
        "::tcl::mathfunc::cosh", "::tcl::mathfunc::double", "::tcl::mathfunc::entier",
        "::tcl::mathfunc::exp", "::tcl::mathfunc::floor", "::tcl::mathfunc::fmod",
        "::tcl::mathfunc::hypot", "::tcl::mathfunc::int", "::tcl::mathfunc::isqrt",
        "::tcl::mathfunc::log", "::tcl::mathfunc::log10", "::tcl::mathfunc::max",
        "::tcl::mathfunc::min", "::tcl::mathfunc::pow", "::tcl::mathfunc::rand",
        "::tcl::mathfunc::round", "::tcl::mathfunc::sin", "::tcl::mathfunc::sinh",
        "::tcl::mathfunc::sqrt", "::tcl::mathfunc::srand", "::tcl::mathfunc::tan",
        "::tcl::mathfunc::tanh", "::tcl::mathfunc::wide"
    };

    struct CandidateCollector {
        const CompletionRequest& request;
        CompletionResult& result;
        std::string_view prefix;
        std::size_t examined { };
        std::vector<Candidate> candidates;
        bool exhausted { false };

        bool claim_item()
        {
            if (examined >= kMaximumProviderItems) {
                exhausted = true;
                result.candidates_truncated = true;
                return false;
            }
            ++examined;
            return true;
        }

        void add(
            const std::string_view text,
            const std::string_view help,
            const CandidateKind kind,
            const bool variable_name = false)
        {
            if (!claim_item()) {
                return;
            }
            if (!starts_with(text, prefix)) {
                return;
            }
            Candidate candidate;
            candidate.text = variable_name ? std::string(text) : quote_tcl_word(text);
            candidate.display = text;
            candidate.help = help;
            candidate.kind = kind;
            candidates.push_back(std::move(candidate));
        }

        void add_command(const CompletionCommand& command, const std::string_view name)
        {
            if (!claim_item()) {
                return;
            }
            add_command_claimed(command, name);
        }

        void add_command_claimed(const CompletionCommand& command, const std::string_view name)
        {
            if (!has_capabilities(request.capabilities, command.required_capabilities)
                || (request.context == RequestContext::callback && !command.callback_safe)) {
                return;
            }
            const auto display = display_command_name(name, prefix);
            if (!starts_with(display, prefix)) {
                return;
            }
            Candidate candidate;
            candidate.text = quote_tcl_word(display);
            candidate.display = display;
            candidate.help = command_help(command);
            candidate.kind = CandidateKind::fsim_command;
            candidates.push_back(std::move(candidate));
        }

        void add_subcommand(const CompletionSubcommand& subcommand)
        {
            if (!claim_item()
                || !has_capabilities(request.capabilities, subcommand.required_capabilities)
                || !starts_with(subcommand.name, prefix)) {
                return;
            }
            Candidate candidate;
            candidate.text = quote_tcl_word(subcommand.name);
            candidate.display = subcommand.name;
            candidate.help = subcommand_help(subcommand);
            candidate.kind = CandidateKind::literal;
            candidates.push_back(std::move(candidate));
        }

        void add_choice(const CompletionChoice& choice)
        {
            add(choice.text, choice.help, CandidateKind::literal);
        }

        void finish()
        {
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
                return std::tie(left.display, left.kind, left.text, left.help)
                    < std::tie(right.display, right.kind, right.text, right.help);
            });
            candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
                return left.text == right.text;
            }),
                candidates.end());
            const auto limit = std::min(request.max_candidates, kMaximumCandidates);
            if (candidates.size() > limit) {
                candidates.resize(limit);
                result.candidates_truncated = true;
            }
            result.candidates = std::move(candidates);
        }

        void add_claimed(
            const std::string_view text,
            const std::string_view help,
            const CandidateKind kind,
            const bool variable_name = false)
        {
            if (!starts_with(text, prefix)) {
                return;
            }
            Candidate candidate;
            candidate.text = variable_name ? std::string(text) : quote_tcl_word(text);
            candidate.display = text;
            candidate.help = help;
            candidate.kind = kind;
            candidates.push_back(std::move(candidate));
        }
    };

    void add_items(
        CandidateCollector& collector,
        const std::span<const CompletionItem> items,
        const std::optional<CandidateKind> required_kind = { })
    {
        for (const auto& item : items) {
            if (!collector.claim_item()) {
                return;
            }
            if (required_kind && item.kind != *required_kind) {
                continue;
            }
            if (item_allowed(collector.request, item)) {
                collector.add_claimed(item.text, item.help, item.kind);
            }
        }
    }

    const CompletionArgument* argument_for_index(
        const std::span<const CompletionArgument> arguments,
        const std::size_t index)
    {
        if (index < arguments.size()) {
            return &arguments[index];
        }
        if (!arguments.empty() && arguments.back().repeatable) {
            return &arguments.back();
        }
        return nullptr;
    }

    const CompletionOption* find_option(
        const std::span<const CompletionOption> options,
        const std::string_view text)
    {
        const auto found = std::find_if(options.begin(), options.end(), [text](const CompletionOption& option) {
            return option.text == text;
        });
        return found == options.end() ? nullptr : &*found;
    }

    std::string command_word(std::string_view input, const Word& word);

    void add_domain_items(
        CandidateCollector&,
        const CompletionSnapshot&,
        ArgumentDomain);

    void collect_argument_candidates(
        CandidateCollector& collector,
        const CompletionSnapshot& snapshot,
        const CompletionArgument* argument)
    {
        if (argument == nullptr) {
            return;
        }
        if (!argument->choices.empty()) {
            for (const auto& choice : argument->choices) {
                collector.add_choice(choice);
            }
            return;
        }
        add_domain_items(collector, snapshot, argument->domain);
    }

    void collect_option_value_candidates(
        CandidateCollector& collector,
        const CompletionSnapshot& snapshot,
        const CompletionOption& option)
    {
        if (!option.value_choices.empty()) {
            for (const auto& choice : option.value_choices) {
                collector.add_choice(choice);
            }
        } else if (option.value_domain) {
            add_domain_items(collector, snapshot, *option.value_domain);
        }
    }

    const CompletionOption* pending_option_value(
        const std::span<const CompletionOption> options,
        const std::vector<Word>& argument_words,
        const std::string_view input)
    {
        std::size_t index = 0U;
        const CompletionOption* pending = nullptr;
        while (index < argument_words.size()) {
            const auto token = command_word(input, argument_words[index]);
            if (token == "--") {
                pending = nullptr;
                ++index;
                while (index < argument_words.size()) {
                    ++index;
                }
                break;
            }
            if (starts_with(token, "-")) {
                pending = find_option(options, token);
                if (pending != nullptr
                    && (pending->value_domain.has_value() || !pending->value_choices.empty())) {
                    if (index + 1U == argument_words.size()) {
                        return pending;
                    }
                    index += 2U;
                } else {
                    pending = nullptr;
                    ++index;
                }
                continue;
            }
            pending = nullptr;
            ++index;
        }
        return nullptr;
    }

    std::size_t positional_argument_count(
        const std::span<const CompletionOption> options,
        const std::vector<Word>& argument_words,
        const std::string_view input)
    {
        std::size_t index = 0U;
        std::size_t positional_count = 0U;
        while (index < argument_words.size()) {
            const auto token = command_word(input, argument_words[index]);
            if (token == "--") {
                positional_count += argument_words.size() - index - 1U;
                break;
            }
            if (starts_with(token, "-")) {
                const auto* option = find_option(options, token);
                if (option != nullptr
                    && (option->value_domain.has_value() || !option->value_choices.empty())) {
                    index += std::min<std::size_t>(2U, argument_words.size() - index);
                } else {
                    ++index;
                }
                continue;
            }
            ++positional_count;
            ++index;
        }
        return positional_count;
    }

    std::string command_word(const std::string_view input, const Word& word)
    {
        return decode_word(input, word);
    }

    void add_hint(CompletionResult& result, const std::string_view text)
    {
        if (text.empty()) {
            return;
        }
        const auto duplicate = std::find_if(result.hints.begin(), result.hints.end(), [text](const Hint& hint) {
            return hint.text == text;
        });
        if (duplicate == result.hints.end() && result.hints.size() < 32U) {
            result.hints.push_back(Hint { std::string(text) });
        }
    }

    void add_static_builtins(CandidateCollector& collector)
    {
        for (const auto builtin : kTclBuiltins) {
            collector.add(builtin, "Tcl built-in command", CandidateKind::builtin_command);
        }
    }

    void add_domain_items(
        CandidateCollector& collector,
        const CompletionSnapshot& snapshot,
        const ArgumentDomain domain)
    {
        if (domain == ArgumentDomain::compiled_definition) {
            add_items(collector, snapshot.compiled_definitions);
            add_items(collector, snapshot.packages);
            add_items(collector, snapshot.types);
            return;
        }
        const auto items = items_for_domain(snapshot, domain);
        if (domain == ArgumentDomain::directory_path) {
            add_items(collector, items, CandidateKind::directory);
            return;
        }
        if (domain == ArgumentDomain::source_path) {
            for (const auto& item : items) {
                if (!collector.claim_item()) {
                    return;
                }
                if (item.kind == CandidateKind::path || item.kind == CandidateKind::directory) {
                    if (item_allowed(collector.request, item)) {
                        collector.add_claimed(item.text, item.help, item.kind);
                    }
                }
            }
            return;
        }
        add_items(collector, items);
    }

    void collect_variable_candidates(
        CandidateCollector& collector,
        const CompletionSnapshot& snapshot)
    {
        for (const auto& item : snapshot.variables) {
            if (!collector.claim_item()) {
                return;
            }
            if (item_allowed(collector.request, item)) {
                collector.add_claimed(item.text, item.help, CandidateKind::variable, true);
            }
        }
    }

    void collect_command_position(
        CandidateCollector& collector,
        const CompletionSnapshot& snapshot)
    {
        add_static_builtins(collector);
        add_items(collector, snapshot.namespaces);
        add_items(collector, snapshot.procedures);
        for (const auto& command : snapshot.commands) {
            collector.add_command(command, command.name);
            if (collector.exhausted) {
                break;
            }
        }
    }

    std::string variable_prefix_for_command(const std::vector<Word>& preceding, const std::string_view input)
    {
        if (preceding.empty()) {
            return { };
        }
        return command_word(input, preceding.front());
    }

    void collect_command_subcommands(
        CandidateCollector& collector,
        const CompletionSnapshot& snapshot)
    {
        constexpr std::string_view fsim_prefix = "fsim::";
        for (const auto& command : snapshot.commands) {
            if (collector.exhausted) {
                break;
            }
            const auto name = normalized_command_name(command.name);
            if (!collector.claim_item()) {
                break;
            }
            if (!starts_with(name, fsim_prefix)) {
                continue;
            }
            // The descriptor has already consumed this bounded scan slot.
            collector.add_command_claimed(command, name.substr(fsim_prefix.size()));
            if (collector.exhausted) {
                break;
            }
        }
    }

} // namespace

CompletionResult complete(
    const CompletionRequest& request,
    const CompletionSnapshot& snapshot)
{
    CompletionResult result;
    result.generations = request.generations;
    result.replace_begin_byte = request.cursor_byte;
    result.replace_end_byte = request.cursor_byte;

    if (request.generations != snapshot.generations) {
        result.status = CompletionStatus::stale_snapshot;
        return result;
    }
    if (request.buffer.size() > kMaximumInputBytes) {
        result.status = CompletionStatus::input_too_large;
        return result;
    }
    if (const auto invalid = invalid_utf8_offset(request.buffer)) {
        result.status = CompletionStatus::invalid_utf8;
        result.replace_begin_byte = *invalid;
        result.replace_end_byte = std::min(*invalid + 1U, request.buffer.size());
        append_span(
            result,
            result.replace_begin_byte,
            result.replace_end_byte,
            SpanKind::diagnostic,
            Severity::error,
            "input is not valid UTF-8");
        return result;
    }
    if (request.cursor_byte > request.buffer.size()
        || !is_utf8_boundary(request.buffer, request.cursor_byte)) {
        result.status = CompletionStatus::invalid_cursor;
        return result;
    }

    TclParser parser(request.buffer, request.cursor_byte, result);
    parser.run();
    const auto& cursor = parser.cursor_capture();
    if (parser.complexity_limited()) {
        result.status = CompletionStatus::complexity_limit;
    }
    if (parser.cursor_in_comment() || !cursor.found) {
        std::sort(result.spans.begin(), result.spans.end(), [](const Span& left, const Span& right) {
            return std::tie(left.begin_byte, left.end_byte, left.kind, left.message)
                < std::tie(right.begin_byte, right.end_byte, right.kind, right.message);
        });
        return result;
    }

    std::string prefix;
    if (cursor.has_word) {
        result.replace_begin_byte = cursor.word.begin;
        result.replace_end_byte = cursor.word.end;
        const auto prefix_end = std::min(request.cursor_byte, cursor.word.content_end);
        const auto prefix_begin = std::min(cursor.word.content_begin, prefix_end);
        prefix = decode_prefix(request.buffer.substr(prefix_begin, prefix_end - prefix_begin));
    }

    CandidateCollector collector { request, result, prefix, 0U, { } };
    bool variables_selected = false;
    if (cursor.has_word) {
        const auto variable = variable_context(request.buffer, cursor.word, request.cursor_byte);
        if (variable.found) {
            variables_selected = true;
            result.replace_begin_byte = variable.name_begin;
            result.replace_end_byte = variable.name_end;
            collector.prefix = variable.prefix;
            collect_variable_candidates(collector, snapshot);
            add_hint(result, "Tcl variable");
        }
    }

    if (!variables_selected) {
        const auto& preceding = cursor.preceding_words;
        if (preceding.empty()) {
            collect_command_position(collector, snapshot);
            add_hint(result, "Tcl command name");
        } else {
            auto command_name = variable_prefix_for_command(preceding, request.buffer);
            std::size_t command_arguments_begin = 1U;
            bool fsim_subcommand_position = false;
            if (command_name == "fsim") {
                if (preceding.size() == 1U) {
                    fsim_subcommand_position = true;
                } else {
                    const auto subcommand = command_word(request.buffer, preceding[1]);
                    command_name = "fsim::" + subcommand;
                    command_arguments_begin = 2U;
                }
            }

            if (fsim_subcommand_position) {
                collect_command_subcommands(collector, snapshot);
                add_hint(result, "fsim subcommand");
            } else {
                const auto* command = find_command(snapshot, command_name);
                if (command != nullptr) {
                    if (request.context != RequestContext::callback || command->callback_safe) {
                        auto arguments = command->arguments;
                        auto options = command->options;
                        std::vector<Word> argument_words;
                        if (command_arguments_begin < preceding.size()) {
                            argument_words.assign(
                                preceding.begin() + static_cast<std::ptrdiff_t>(command_arguments_begin),
                                preceding.end());
                        }

                        bool subcommand_selected = false;
                        if (!command->subcommands.empty()) {
                            const auto first_word = argument_words.empty()
                                ? std::string { }
                                : command_word(request.buffer, argument_words.front());
                            const auto subcommand = std::find_if(
                                command->subcommands.begin(), command->subcommands.end(),
                                [first_word](const CompletionSubcommand& candidate) {
                                    return candidate.name == first_word;
                                });
                            if (argument_words.empty()) {
                                for (const auto& candidate : command->subcommands) {
                                    collector.add_subcommand(candidate);
                                }
                                add_hint(result, "subcommand");
                            } else if (subcommand != command->subcommands.end()
                                && has_capabilities(
                                    request.capabilities,
                                    subcommand->required_capabilities)) {
                                arguments = subcommand->arguments;
                                options = subcommand->options;
                                argument_words.erase(argument_words.begin());
                                subcommand_selected = true;
                                add_hint(result, subcommand->usage);
                                add_hint(result, subcommand->help);
                            } else {
                                add_hint(result, command->usage);
                            }
                        }

                        if (command->subcommands.empty() || subcommand_selected) {
                            if (starts_with(prefix, "-")) {
                                for (const auto& option : options) {
                                    collector.add(option.text, option.help, CandidateKind::option);
                                }
                            }
                            if (const auto* pending = pending_option_value(
                                    options, argument_words, request.buffer)) {
                                collect_option_value_candidates(collector, snapshot, *pending);
                            } else {
                                const auto argument_index = positional_argument_count(
                                    options, argument_words, request.buffer);
                                collect_argument_candidates(
                                    collector,
                                    snapshot,
                                    argument_for_index(arguments, argument_index));
                            }
                        }
                        add_hint(result, command->usage);
                        add_hint(result, command->help);
                    }
                } else {
                    const auto argument_index = preceding.size() - 1U;
                    const bool variable_command = command_name == "set"
                        || command_name == "unset"
                        || command_name == "global"
                        || command_name == "variable"
                        || command_name == "upvar";
                    if (variable_command && argument_index == 0U) {
                        collect_variable_candidates(collector, snapshot);
                    }
                    if (command_name == "namespace" || command_name == "info") {
                        add_items(collector, snapshot.namespaces);
                        add_items(collector, snapshot.procedures);
                    }
                }
            }
        }
    }

    if (result.status == CompletionStatus::ok) {
        collector.finish();
    } else {
        collector.candidates.clear();
        collector.finish();
    }
    std::sort(result.hints.begin(), result.hints.end(), [](const Hint& left, const Hint& right) {
        return left.text < right.text;
    });
    std::sort(result.spans.begin(), result.spans.end(), [](const Span& left, const Span& right) {
        return std::tie(left.begin_byte, left.end_byte, left.kind, left.message)
            < std::tie(right.begin_byte, right.end_byte, right.kind, right.message);
    });
    return result;
}

bool is_current(const CompletionResult& result, const Generations& live_generations) noexcept
{
    return result.status == CompletionStatus::ok && result.generations == live_generations;
}

} // namespace fsim::app::tcl_completion
