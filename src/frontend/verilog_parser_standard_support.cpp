// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

namespace detail {

    class OutputUnsignedInteger {
    public:
        [[nodiscard]] static std::optional<OutputUnsignedInteger> parse(
            const std::string_view digits,
            const unsigned base)
        {
            OutputUnsignedInteger result;
            bool saw_digit = false;
            for (const char character : digits) {
                if (character == '_') {
                    continue;
                }
                unsigned digit { };
                if (character >= '0' && character <= '9') {
                    digit = static_cast<unsigned>(character - '0');
                } else if (character >= 'a' && character <= 'f') {
                    digit = 10U + static_cast<unsigned>(character - 'a');
                } else if (character >= 'A' && character <= 'F') {
                    digit = 10U + static_cast<unsigned>(character - 'A');
                } else {
                    return std::nullopt;
                }
                if (digit >= base) {
                    return std::nullopt;
                }
                saw_digit = true;
                result.multiply_add(base, digit);
            }
            return saw_digit
                ? std::optional { std::move(result) }
                : std::nullopt;
        }

        [[nodiscard]] std::size_t bit_width() const
        {
            if (words_.empty()) {
                return 1U;
            }
            auto high = words_.back();
            std::size_t high_width = 0;
            while (high != 0U) {
                ++high_width;
                high >>= 1U;
            }
            return (words_.size() - 1U) * word_bits + high_width;
        }

        [[nodiscard]] bool test_bit(const std::size_t bit) const
        {
            const auto word = bit / word_bits;
            return word < words_.size()
                && ((words_[word] >> (bit % word_bits)) & 1U) != 0U;
        }

        void truncate(const std::size_t width)
        {
            if (width >= bit_width()) {
                return;
            }
            const auto word_count = (width + word_bits - 1U) / word_bits;
            words_.resize(word_count);
            if ((width % word_bits) != 0U) {
                words_.back() &= (std::uint32_t { 1 } << (width % word_bits)) - 1U;
            }
            trim();
        }

        void twos_complement_magnitude(const std::size_t width)
        {
            const auto word_count = (width + word_bits - 1U) / word_bits;
            words_.resize(word_count, 0U);
            for (auto& word : words_) {
                word = ~word;
            }
            mask_to_width(width);
            std::uint64_t carry = 1U;
            for (auto& word : words_) {
                const auto sum = static_cast<std::uint64_t>(word) + carry;
                word = static_cast<std::uint32_t>(sum);
                carry = sum >> word_bits;
                if (carry == 0U) {
                    break;
                }
            }
            mask_to_width(width);
            trim();
        }

        [[nodiscard]] std::string decimal_string() const
        {
            if (words_.empty()) {
                return "0";
            }
            auto quotient = words_;
            std::vector<std::uint32_t> chunks;
            while (!quotient.empty()) {
                std::uint64_t remainder = 0U;
                for (auto word = quotient.rbegin(); word != quotient.rend(); ++word) {
                    const auto dividend = (remainder << word_bits) | *word;
                    *word = static_cast<std::uint32_t>(dividend / decimal_chunk_base);
                    remainder = dividend % decimal_chunk_base;
                }
                chunks.push_back(static_cast<std::uint32_t>(remainder));
                while (!quotient.empty() && quotient.back() == 0U) {
                    quotient.pop_back();
                }
            }
            auto result = std::to_string(chunks.back());
            for (auto chunk = chunks.rbegin() + 1U; chunk != chunks.rend(); ++chunk) {
                const auto text = std::to_string(*chunk);
                result.append(decimal_chunk_digits - text.size(), '0');
                result += text;
            }
            return result;
        }

    private:
        static constexpr std::size_t word_bits = 32U;
        static constexpr std::uint64_t decimal_chunk_base = 1'000'000'000U;
        static constexpr std::size_t decimal_chunk_digits = 9U;

        void multiply_add(const unsigned multiplier, const unsigned addend)
        {
            std::uint64_t carry = addend;
            for (auto& word : words_) {
                const auto product = static_cast<std::uint64_t>(word) * multiplier + carry;
                word = static_cast<std::uint32_t>(product);
                carry = product >> word_bits;
            }
            if (carry != 0U) {
                words_.push_back(static_cast<std::uint32_t>(carry));
            }
        }

        void mask_to_width(const std::size_t width)
        {
            if ((width % word_bits) != 0U) {
                words_.back() &= (std::uint32_t { 1 } << (width % word_bits)) - 1U;
            }
        }

        void trim()
        {
            while (!words_.empty() && words_.back() == 0U) {
                words_.pop_back();
            }
        }

        std::vector<std::uint32_t> words_;
    };

    struct OutputLiteralWidth {
        std::size_t bits { };
        bool exceeds_host_size { };
    };

    [[nodiscard]] std::optional<OutputLiteralWidth>
    output_literal_width(const std::string_view spelling)
    {
        OutputLiteralWidth result;
        bool saw_digit = false;
        bool nonzero = false;
        for (const char character : spelling) {
            if (character == '_') {
                continue;
            }
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            saw_digit = true;
            const auto digit = static_cast<std::size_t>(character - '0');
            nonzero = nonzero || digit != 0U;
            if (!result.exceeds_host_size) {
                if (result.bits
                    > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
                    result.exceeds_host_size = true;
                } else {
                    result.bits = result.bits * 10U + digit;
                }
            }
        }
        return saw_digit && nonzero
            ? std::optional { result }
            : std::nullopt;
    }
} // namespace detail

std::optional<std::size_t> output_unsigned_integer_bit_width(
    const std::string_view spelling, const unsigned base)
{
    const auto value = detail::OutputUnsignedInteger::parse(spelling, base);
    return value
        ? std::optional<std::size_t> { value->bit_width() }
        : std::nullopt;
}

[[nodiscard]] std::optional<std::string>
constant_output_number(const std::string_view spelling)
{
    const auto quote = spelling.find('\'');
    if (quote == std::string_view::npos) {
        const auto value = detail::OutputUnsignedInteger::parse(spelling, 10U);
        return value
            ? std::optional { value->decimal_string() }
            : std::nullopt;
    }
    std::optional<detail::OutputLiteralWidth> explicit_width;
    const auto width_text = spelling.substr(0, quote);
    if (!width_text.empty()) {
        explicit_width = detail::output_literal_width(width_text);
        if (!explicit_width) {
            return std::nullopt;
        }
    }
    auto digits = spelling.substr(quote + 1);
    bool is_signed = false;
    if (!digits.empty()
        && (digits.front() == 's' || digits.front() == 'S')) {
        is_signed = true;
        digits.remove_prefix(1);
    }
    if (digits.size() < 2) {
        return std::nullopt;
    }
    const char radix = detail::ascii_lower(digits.substr(0, 1)).front();
    digits.remove_prefix(1);
    unsigned base { };
    std::size_t digit_width { };
    switch (radix) {
    case 'b':
        base = 2U;
        digit_width = 1U;
        break;
    case 'o':
        base = 8U;
        digit_width = 3U;
        break;
    case 'd':
        base = 10U;
        break;
    case 'h':
        base = 16U;
        digit_width = 4U;
        break;
    default:
        return std::nullopt;
    }
    auto value = detail::OutputUnsignedInteger::parse(digits, base);
    if (!value) {
        return std::nullopt;
    }
    if (explicit_width && !explicit_width->exceeds_host_size) {
        value->truncate(explicit_width->bits);
    }
    std::size_t selected_width { };
    bool selected_width_exceeds_host = false;
    if (explicit_width) {
        selected_width = explicit_width->bits;
        selected_width_exceeds_host = explicit_width->exceeds_host_size;
    } else if (base == 10U) {
        selected_width = std::max(
            std::size_t { 32 },
            value->bit_width() + (is_signed ? 1U : 0U));
    } else {
        const auto digit_count = static_cast<std::size_t>(std::ranges::count_if(
            digits,
            [](const char character) { return character != '_'; }));
        if (digit_count > std::numeric_limits<std::size_t>::max() / digit_width) {
            selected_width_exceeds_host = true;
        } else {
            selected_width = std::max(std::size_t { 32 }, digit_count * digit_width);
        }
    }
    const bool negative = is_signed
        && !selected_width_exceeds_host
        && value->test_bit(selected_width - 1U);
    if (negative) {
        value->twos_complement_magnitude(selected_width);
    }
    return (negative ? "-" : "") + value->decimal_string();
}

[[nodiscard]] std::optional<std::int64_t> simple_verilog_integer_constant(
    const Expression& expression) {
  if (expression.kind == ExpressionKind::IntegerLiteral) {
    return decimal_i64(expression.text);
  }
  if (expression.kind == ExpressionKind::LogicLiteral) {
      const auto value = constant_output_number(expression.text);
      return value ? decimal_i64(*value) : std::nullopt;
  }
  if (expression.kind == ExpressionKind::Unary
      && expression.operands.size() == 1
      && (expression.text == "+" || expression.text == "-")) {
    const auto magnitude =
        simple_verilog_integer_constant(expression.operands.front());
    if (!magnitude) {
      return std::nullopt;
    }
    if (expression.text == "+") {
      return magnitude;
    }
    if (*magnitude == std::numeric_limits<std::int64_t>::min()) {
      return std::nullopt;
    }
    return -*magnitude;
  }
  return std::nullopt;
}

[[nodiscard]] Language language_for_standard_revision(
    const StandardRevision standard) {
  switch (standard) {
    case StandardRevision::Verilog1995:
    case StandardRevision::Verilog2001:
    case StandardRevision::Verilog2001NoConfig:
    case StandardRevision::Verilog2005:
      return Language::Verilog2005;
    case StandardRevision::SystemVerilog2005:
    case StandardRevision::SystemVerilog2009:
    case StandardRevision::SystemVerilog2012:
    case StandardRevision::SystemVerilog2017:
    case StandardRevision::SystemVerilog2023:
      return Language::SystemVerilog2017;
    default:
      return Language::Vhdl2008;
  }
}

[[nodiscard]] KeywordSet keyword_set_for_standard_revision(
    const StandardRevision standard) {
  switch (standard) {
    case StandardRevision::Verilog1995:
      return KeywordSet::Verilog1995;
    case StandardRevision::Verilog2001:
      return KeywordSet::Verilog2001;
    case StandardRevision::Verilog2001NoConfig:
      return KeywordSet::Verilog2001NoConfig;
    case StandardRevision::Verilog2005:
      return KeywordSet::Verilog2005;
    case StandardRevision::SystemVerilog2005:
      return KeywordSet::SystemVerilog2005;
    case StandardRevision::SystemVerilog2009:
      return KeywordSet::SystemVerilog2009;
    case StandardRevision::SystemVerilog2012:
      return KeywordSet::SystemVerilog2012;
    case StandardRevision::SystemVerilog2017:
      return KeywordSet::SystemVerilog2017;
    case StandardRevision::SystemVerilog2023:
      return KeywordSet::SystemVerilog2023;
    default:
      return KeywordSet::SystemVerilog2017;
  }
}

[[nodiscard]] unsigned keyword_set_rank(const KeywordSet set) {
  switch (set) {
    case KeywordSet::Verilog1995:
      return 0;
    case KeywordSet::Verilog2001:
    case KeywordSet::Verilog2001NoConfig:
      return 1;
    case KeywordSet::Verilog2005:
      return 2;
    case KeywordSet::SystemVerilog2005:
      return 3;
    case KeywordSet::SystemVerilog2009:
      return 4;
    case KeywordSet::SystemVerilog2012:
      return 5;
    case KeywordSet::SystemVerilog2017:
      return 6;
    case KeywordSet::SystemVerilog2023:
      return 7;
  }
  return 6;
}

[[nodiscard]] std::optional<StandardRevision>
declaration_word_standard(const std::string_view word) {
  if (word == "automatic" || word == "genvar" || word == "localparam"
      || word == "signed" || word == "unsigned") {
    return StandardRevision::Verilog2001;
  }
  if (word == "uwire") {
    return StandardRevision::Verilog2005;
  }
  if (word == "bit" || word == "byte" || word == "chandle"
      || word == "const" || word == "enum" || word == "int"
      || word == "logic" || word == "longint" || word == "process"
      || word == "shortint" || word == "shortreal" || word == "static"
      || word == "string" || word == "struct" || word == "type"
      || word == "typedef" || word == "union" || word == "var") {
    return StandardRevision::SystemVerilog2005;
  }
  if (word == "untyped") {
    return StandardRevision::SystemVerilog2009;
  }
  if (word == "interconnect" || word == "nettype") {
    return StandardRevision::SystemVerilog2012;
  }
  return std::nullopt;
}

[[nodiscard]] bool contains_word(
    const std::initializer_list<std::string_view> words,
    const std::string_view word) {
  return std::find(words.begin(), words.end(), word) != words.end();
}

[[nodiscard]] bool is_verilog_1995_keyword(
    const std::string_view word) {
  return contains_word(
      {"always", "and", "assign", "begin", "buf", "bufif0",
       "bufif1", "case", "casex", "casez", "cmos", "deassign",
       "default", "defparam", "disable", "edge", "else", "end",
       "endcase", "endfunction", "endmodule", "endprimitive",
       "endspecify", "endtable", "endtask", "event", "for", "force",
       "forever", "fork", "function", "highz0", "highz1", "if",
       "ifnone", "initial", "inout", "input", "integer", "join",
       "large", "macromodule", "medium", "module", "nand", "negedge",
       "nmos", "nor", "not", "notif0", "notif1", "or", "output",
       "parameter", "pmos", "posedge", "primitive", "pull0", "pull1",
       "pulldown", "pullup", "rcmos", "real", "realtime", "reg",
       "release", "repeat", "rnmos", "rpmos", "rtran", "rtranif0",
       "rtranif1", "scalared", "small", "specify", "specparam",
       "strong0", "strong1", "supply0", "supply1", "table", "task",
       "time", "tran", "tranif0", "tranif1", "tri", "tri0", "tri1",
       "triand", "trior", "trireg", "vectored", "wait", "wand",
       "weak0", "weak1", "while", "wire", "wor", "xnor", "xor"},
      word);
}

[[nodiscard]] bool is_verilog_2001_keyword(
    const std::string_view word,
    const bool include_config) {
  if (is_verilog_1995_keyword(word)) {
    return true;
  }
  if (contains_word(
          {"automatic", "endgenerate", "generate", "genvar",
           "localparam", "noshowcancelled", "pulsestyle_ondetect",
           "pulsestyle_onevent", "showcancelled", "signed", "unsigned"},
          word)) {
    return true;
  }
  return include_config
      && contains_word(
          {"cell", "config", "design", "endconfig", "incdir",
           "include", "instance", "liblist", "library", "use"},
          word);
}

[[nodiscard]] bool is_system_verilog_2005_keyword(
    const std::string_view word) {
  return is_verilog_2001_keyword(word, true)
      || word == "uwire"
      || contains_word(
          {"alias", "always_comb", "always_ff", "always_latch",
           "assert", "assume", "before", "bind", "bins", "binsof",
           "bit", "break", "byte", "chandle", "class", "clocking",
           "const", "constraint", "context", "continue", "cover",
           "covergroup", "coverpoint", "cross", "dist", "do",
           "endclass", "endclocking", "endgroup", "endinterface",
           "endpackage", "endprogram", "endproperty", "endsequence",
           "enum", "expect", "export", "extends", "extern", "final",
           "first_match", "foreach", "forkjoin", "iff", "ignore_bins",
           "illegal_bins", "import", "inside", "int", "interface",
           "intersect", "join_any", "join_none", "local", "logic",
           "longint", "matches", "modport", "new", "null", "package",
           "packed", "priority", "program", "property", "protected",
           "process", "pure", "rand", "randc", "randcase", "randsequence", "ref",
           "return", "sequence", "shortint", "shortreal", "solve",
           "static", "string", "struct", "super", "tagged", "this",
           "throughout", "timeprecision", "timeunit", "type", "typedef",
           "union", "unique", "var", "virtual", "void", "wait_order",
           "wildcard", "with", "within"},
          word);
}

[[nodiscard]] bool is_system_verilog_2009_keyword(
    const std::string_view word) {
  return is_system_verilog_2005_keyword(word)
      || contains_word(
          {"accept_on", "checker", "endchecker", "eventually", "global",
           "implies", "let", "nexttime", "reject_on", "restrict",
           "s_always", "s_eventually", "s_nexttime", "s_until",
           "s_until_with", "strong", "sync_accept_on", "sync_reject_on",
           "unique0", "until", "until_with", "untyped", "weak"},
          word);
}

[[nodiscard]] bool is_system_verilog_2012_keyword(
    const std::string_view word) {
  return is_system_verilog_2009_keyword(word)
      || contains_word(
          {"implements", "interconnect", "nettype", "soft"},
          word);
}

[[nodiscard]] bool keyword_reserved(
    const KeywordSet set,
    const std::string_view word) {
  switch (set) {
    case KeywordSet::Verilog1995:
      return is_verilog_1995_keyword(word);
    case KeywordSet::Verilog2001:
      return is_verilog_2001_keyword(word, true);
    case KeywordSet::Verilog2001NoConfig:
      return is_verilog_2001_keyword(word, false);
    case KeywordSet::Verilog2005:
      return is_verilog_2001_keyword(word, true) || word == "uwire";
    case KeywordSet::SystemVerilog2005:
      return is_system_verilog_2005_keyword(word);
    case KeywordSet::SystemVerilog2009:
      return is_system_verilog_2009_keyword(word);
    case KeywordSet::SystemVerilog2012:
    case KeywordSet::SystemVerilog2017:
    case KeywordSet::SystemVerilog2023:
      return is_system_verilog_2012_keyword(word);
  }
  return false;
}

[[nodiscard]] std::optional<KeywordSet> parse_keyword_set(
    const std::string_view spelling) {
  if (spelling == "1364-1995") {
    return KeywordSet::Verilog1995;
  }
  if (spelling == "1364-2001") {
    return KeywordSet::Verilog2001;
  }
  if (spelling == "1364-2001-noconfig") {
    return KeywordSet::Verilog2001NoConfig;
  }
  if (spelling == "1364-2005") {
    return KeywordSet::Verilog2005;
  }
  if (spelling == "1800-2005") {
    return KeywordSet::SystemVerilog2005;
  }
  if (spelling == "1800-2009") {
    return KeywordSet::SystemVerilog2009;
  }
  if (spelling == "1800-2012") {
    return KeywordSet::SystemVerilog2012;
  }
  if (spelling == "1800-2017") {
    return KeywordSet::SystemVerilog2017;
  }
  if (spelling == "1800-2023") {
    return KeywordSet::SystemVerilog2023;
  }
  return std::nullopt;
}
} // namespace fsim::frontend
