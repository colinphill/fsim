// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
#include "fsim/support/path.hpp"

#include <cctype>
#include <fstream>
#include <iterator>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view memory_simple_name(const std::string_view name)
{
    const auto separator = name.find_last_of(".:");
    return name.substr(
        separator == std::string_view::npos ? 0U : separator + 1U);
}

std::string_view memory_name(const semantic::vhdl::Name& name)
{
    return name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
}

bool same_memory_identifier(
    const std::string_view left, const std::string_view right)
{
    return left.size() == right.size()
        && std::ranges::equal(left, right, [](const char lhs, const char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs))
                   == std::tolower(static_cast<unsigned char>(rhs));
           });
}

std::string canonical_memory_identifier(const std::string_view name)
{
    auto result = std::string { name };
    std::ranges::transform(result, result.begin(), [](const char value) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    });
    return result;
}

bool has_vital_memory_qualification(const std::string_view name)
{
    const auto member_separator = name.find_last_of('.');
    if (member_separator == std::string_view::npos) {
        return true;
    }
    const auto owner = name.substr(0U, member_separator);
    const auto package_separator = owner.find_last_of('.');
    if (package_separator == std::string_view::npos) {
        return same_memory_identifier(owner, "vital_memory");
    }
    return same_memory_identifier(
               owner.substr(package_separator + 1U), "vital_memory")
        && same_memory_identifier(
            owner.substr(0U, package_separator), "ieee");
}

SourceLocation memory_source_location(const frontend::SourceSpan& span)
{
    return SourceLocation {
        span.source_name.str(),
        static_cast<std::uint32_t>(span.begin.line),
        static_cast<std::uint32_t>(span.begin.column),
    };
}

struct HirVitalMemoryActuals {
    std::optional<semantic::ExpressionId> words;
    std::optional<semantic::ExpressionId> word_width;
    std::optional<semantic::ExpressionId> subword_width;
    std::optional<semantic::ExpressionId> load_file;
    std::optional<semantic::ExpressionId> binary;
};

} // namespace

bool Lowerer::is_hir_vhdl_vital_memory_expression(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::call
        || !expression->vhdl->referenced_name) {
        return false;
    }
    const auto& source = *expression->vhdl;
    const auto& reference = *source.referenced_name;
    const auto qualified_name = memory_name(reference);
    if (!same_memory_identifier(
            memory_simple_name(qualified_name), "vitaldeclarememory")
        || !has_vital_memory_qualification(qualified_name)) {
        return false;
    }
    if (reference.selected || !reference.overloads.empty()) {
        return false;
    }
    semantic::CompiledDesignResolver resolver { *specialized_hir_unit_ };
    return resolver.resolve_vhdl_callable_candidates(
                       reference, source.scope)
               .candidates.empty()
        && resolver.vhdl_standard_package_member_visible(
            reference, source.scope);
}

std::optional<RegisterId>
Lowerer::lower_hir_vhdl_vital_memory_expression(
    const semantic::ExpressionId expression_id,
    const std::size_t expected_width)
{
    if (!is_hir_vhdl_vital_memory_expression(expression_id)) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->vhdl;
    const auto span = hir_source_span(source.source);
    const auto declared_result = memory_simple_name(source.nominal_type);
    if (expected_width != 32U
        || (!declared_result.empty()
            && !same_memory_identifier(
                declared_result, "vitalmemorydatatype"))) {
        report(
            "FSIM-ELAB-VITALMEM-001",
            "VitalDeclareMemory requires a VitalMemoryDataType result "
            "context",
            span);
        return std::nullopt;
    }
    if (source.operands.size() < 2U
        || source.operands.size() > 5U) {
        report(
            "FSIM-ELAB-VITALMEM-001",
            "VitalDeclareMemory requires two or three geometry actuals and "
            "optional load-file controls",
            span);
        return std::nullopt;
    }

    bool subword_profile { };
    for (const auto& argument_name : source.argument_names) {
        if (same_memory_identifier(
                argument_name, "noofbitspersubword")) {
            subword_profile = true;
        }
    }
    if (source.argument_names.empty()
        && source.operands.size() >= 3U
        && !hir_expression_is_string(
            source.operands[2], hir_process_scope_)) {
        subword_profile = true;
    }
    HirVitalMemoryActuals actuals;
    constexpr std::array<std::string_view, 5> with_subword {
        "noofwords", "noofbitsperword", "noofbitspersubword",
        "memoryloadfile", "binaryloadfile"
    };
    constexpr std::array<std::string_view, 4> without_subword {
        "noofwords", "noofbitsperword", "memoryloadfile",
        "binaryloadfile"
    };
    const auto expression_span = [&](const semantic::ExpressionId id) {
        const auto value = specialized_hir_unit_->find_expression(id);
        return value && value->vhdl != nullptr
            ? hir_source_span(value->vhdl->source)
            : span;
    };
    std::size_t positional { };
    for (std::size_t index { }; index < source.operands.size(); ++index) {
        auto name = index < source.argument_names.size()
            ? std::string_view { source.argument_names[index] }
            : std::string_view { };
        if (name.empty()) {
            const auto profile_size = subword_profile
                ? with_subword.size()
                : without_subword.size();
            if (positional >= profile_size) {
                report(
                    "FSIM-ELAB-VITALMEM-001",
                    "VitalDeclareMemory has too many positional actuals for "
                    "the selected profile",
                    expression_span(source.operands[index]));
                return std::nullopt;
            }
            name = subword_profile
                ? with_subword[positional++]
                : without_subword[positional++];
        }
        const auto canonical_name = canonical_memory_identifier(name);
        const auto assign = [&](std::optional<semantic::ExpressionId>& target) {
            if (target) {
                report(
                    "FSIM-ELAB-VITALMEM-001",
                    "VitalDeclareMemory has a duplicate '"
                        + canonical_name + "' association",
                    expression_span(source.operands[index]));
                return false;
            }
            target = source.operands[index];
            return true;
        };
        const bool accepted = canonical_name == "noofwords"
            ? assign(actuals.words)
            : canonical_name == "noofbitsperword"
            ? assign(actuals.word_width)
            : canonical_name == "noofbitspersubword"
            ? assign(actuals.subword_width)
            : canonical_name == "memoryloadfile"
            ? assign(actuals.load_file)
            : canonical_name == "binaryloadfile"
            ? assign(actuals.binary)
            : false;
        if (!accepted) {
            if (canonical_name != "noofwords"
                && canonical_name != "noofbitsperword"
                && canonical_name != "noofbitspersubword"
                && canonical_name != "memoryloadfile"
                && canonical_name != "binaryloadfile") {
                report(
                    "FSIM-ELAB-VITALMEM-001",
                    "VitalDeclareMemory has unknown association '"
                        + canonical_name + "'",
                    expression_span(source.operands[index]));
            }
            return std::nullopt;
        }
    }
    if (!actuals.words || !actuals.word_width
        || (subword_profile && !actuals.subword_width)) {
        report(
            "FSIM-ELAB-VITALMEM-001",
            "VitalDeclareMemory is missing required geometry actuals",
            span);
        return std::nullopt;
    }
    const auto words = hir_constant_integer(*actuals.words);
    const auto word_width = hir_constant_integer(*actuals.word_width);
    const auto subword_width = actuals.subword_width
        ? hir_constant_integer(*actuals.subword_width)
        : word_width;
    if (!words || !word_width || !subword_width
        || *words <= 0 || *word_width <= 0 || *subword_width <= 0
        || *subword_width > *word_width) {
        report(
            "FSIM-ELAB-VITALMEM-002",
            "VitalDeclareMemory geometry must be static and positive, with "
            "the subword no wider than the word",
            span);
        return std::nullopt;
    }

    StringRegisterId load_file { };
    if (actuals.load_file) {
        const auto lowered = lower_hir_string_expression(
            *actuals.load_file);
        if (!lowered) {
            report(
                "FSIM-ELAB-VITALMEM-003",
                "VitalDeclareMemory MemoryLoadFile must be a string "
                "expression",
                expression_span(*actuals.load_file));
            return std::nullopt;
        }
        load_file = *lowered;
    } else {
        load_file = allocate_string_register();
        process_.operations.emplace_back(
            LoadStringConstant { load_file, "" });
    }
    bool binary { };
    if (actuals.binary) {
        const auto value = hir_constant_integer(*actuals.binary);
        if (!value || (*value != 0 && *value != 1)) {
            report(
                "FSIM-ELAB-VITALMEM-003",
                "VitalDeclareMemory BinaryLoadFile must be a static Boolean",
                expression_span(*actuals.binary));
            return std::nullopt;
        }
        binary = *value != 0;
    }
    bool embedded_load { };
    std::string embedded_load_text;
    if (actuals.load_file) {
        const auto load_expression
            = specialized_hir_unit_->find_expression(*actuals.load_file);
        if (load_expression && load_expression->vhdl != nullptr
            && load_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::string_literal
            && load_expression->vhdl->decoded_string
            && !load_expression->vhdl->decoded_string->empty()) {
            auto path = fsim::support::path_from_utf8(
                *load_expression->vhdl->decoded_string);
            const auto load_span = expression_span(*actuals.load_file);
            if (path.is_relative()) {
                path = fsim::support::path_from_utf8(
                           frontend::physical_source(load_span))
                           .parent_path()
                    / path;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                report(
                    "FSIM-ELAB-VITALMEM-004",
                    "VitalDeclareMemory load file is unavailable: "
                        + path.lexically_normal().generic_string(),
                    load_span);
                return std::nullopt;
            }
            embedded_load_text.assign(
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
            if ((!input.good() && !input.eof())
                || embedded_load_text.size()
                    > runtime::simir::maximum_memory_file_bytes) {
                report(
                    "FSIM-ELAB-VITALMEM-004",
                    "VitalDeclareMemory load file is unreadable or exceeds "
                    "the 1 MiB input budget",
                    load_span);
                return std::nullopt;
            }
            embedded_load = true;
        }
    }
    const auto destination = allocate_register(
        32U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(VitalMemoryDeclare {
        destination,
        static_cast<std::uint64_t>(*words),
        static_cast<std::uint64_t>(*word_width),
        static_cast<std::uint64_t>(*subword_width),
        load_file,
        binary,
        embedded_load,
        std::move(embedded_load_text),
        memory_source_location(span),
    });
    return destination;
}

} // namespace fsim::elaboration
