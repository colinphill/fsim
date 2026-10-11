// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace fsim::elaboration {

namespace {

// One scalar (or character-string) part of a typed file element, in the
// element's left-to-right order. The element's packed value holds the parts
// most significant first.
struct FileImageLeaf {
    enum class Kind : std::uint8_t {
        integer,
        real,
        enumeration,
        character,
        characters,
        physical,
        time,
    };
    Kind kind { Kind::integer };
    std::uint32_t width { };
    // Enumeration literal images: lower-case identifiers, or character
    // literals with their quotes.
    std::vector<std::string> literals;
    // Physical units (lower case) and their values in primary units; the
    // first unit is the primary unit.
    std::vector<std::pair<std::string, std::int64_t>> units;
    // The character count of a character string part.
    std::uint32_t count { 1 };
};

[[nodiscard]] std::string lower_case(const std::string_view text)
{
    std::string result { text };
    lowercase_ascii(result);
    return result;
}

[[nodiscard]] std::string simple_type_name(const std::string_view spelling)
{
    const auto separator = spelling.find_last_of(".:");
    return lower_case(separator == std::string_view::npos
            ? spelling
            : spelling.substr(separator + 1U));
}

} // namespace

// Typed VHDL files hold one element per line as text images: scalars as
// their images (enumeration literals, decimal integers, reals in standard
// form, physical values with a unit), composites as their scalar parts
// separated by spaces, and character strings as their characters. The
// representation of a typed file is implementation defined (IEEE 1076-2008
// 5.5.2); this one is the line-oriented text of the VESTs iofiles.
Lowerer::HirVhdlFileImageResult Lowerer::lower_hir_vhdl_file_image(
    const bool write,
    const RegisterId handle,
    const semantic::vhdl::SubtypeIndication& element,
    const semantic::ExpressionId value,
    const std::optional<semantic::ExpressionId> length,
    const frontend::SourceSpan& span)
{
    using Leaf = FileImageLeaf;
    using Kind = FileImageLeaf::Kind;
    if (specialized_hir_unit_ == nullptr) {
        return HirVhdlFileImageResult::unsupported;
    }

    // Flatten a subtype into its parts. An unconstrained array at the top
    // contributes the parts of one element, repeated per the actual.
    bool unconstrained_top { };
    const std::function<bool(const semantic::vhdl::SubtypeIndication&, std::size_t,
        bool, std::vector<Leaf>&)>
        flatten = [&](const semantic::vhdl::SubtypeIndication& indication,
                      const std::size_t depth, const bool top,
                      std::vector<Leaf>& leaves) -> bool {
        if (depth > 16U) {
            return false;
        }
        const auto subtype = hir_effective_vhdl_subtype(indication).value_or(indication);
        std::vector<std::string> names { simple_type_name(subtype.type_mark.spelling) };
        const semantic::vhdl::TypeDefinition* definition { };
        {
            auto type_id = subtype.type_mark.target;
            std::unordered_set<std::uint32_t> visited;
            while (type_id.valid() && visited.insert(type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    break;
                }
                definition = type->vhdl;
                names.push_back(simple_type_name(definition->name));
                if (definition->form != semantic::vhdl::TypeForm::subtype
                    && definition->form != semantic::vhdl::TypeForm::alias) {
                    break;
                }
                names.push_back(simple_type_name(definition->base.type_mark.spelling));
                type_id = definition->base.type_mark.target;
            }
        }
        const auto named = [&](const std::string_view name) {
            return std::ranges::find(names, name) != names.end();
        };
        const auto width = [&]() -> std::uint32_t {
            if (subtype.executable_width && *subtype.executable_width != 0U
                && *subtype.executable_width
                    <= std::numeric_limits<std::uint32_t>::max()) {
                return static_cast<std::uint32_t>(*subtype.executable_width);
            }
            if (subtype.domain == semantic::vhdl::ValueDomain::integer
                && subtype.integer_storage_width != 0U) {
                return subtype.integer_storage_width;
            }
            return 0U;
        }();
        const auto scalar = [&](Leaf leaf) {
            leaf.width = leaf.width != 0U ? leaf.width : width;
            if (leaf.width == 0U) {
                return false;
            }
            leaves.push_back(std::move(leaf));
            return true;
        };
        using Form = semantic::vhdl::TypeForm;
        if (definition != nullptr && definition->form == Form::record) {
            for (const auto& member : definition->record_elements) {
                if (!flatten(member.subtype, depth + 1U, false, leaves)) {
                    return false;
                }
            }
            return true;
        }
        const bool predefined_array = named("string") || named("bit_vector")
            || named("boolean_vector") || named("integer_vector")
            || named("real_vector") || named("time_vector");
        if ((definition != nullptr && definition->form == Form::array)
            || (predefined_array
                && (definition == nullptr || definition->form == Form::subtype
                    || definition->form == Form::alias))) {
            if (definition != nullptr && definition->form != Form::array) {
                definition = nullptr;
            }
            semantic::vhdl::SubtypeIndication member;
            if (definition != nullptr && definition->element_subtype) {
                member = *definition->element_subtype;
            } else {
                member.type_mark.spelling = named("string") ? "character"
                    : named("bit_vector")                   ? "bit"
                    : named("boolean_vector")               ? "boolean"
                    : named("integer_vector")               ? "integer"
                    : named("real_vector")                  ? "real"
                                                            : "time";
                if (member.type_mark.spelling == "integer") {
                    member.domain = semantic::vhdl::ValueDomain::integer;
                    member.integer_storage_width = 32U;
                }
            }
            std::vector<Leaf> member_leaves;
            if (!flatten(member, depth + 1U, false, member_leaves)
                || member_leaves.empty()) {
                return false;
            }
            std::uint64_t member_width { };
            for (const auto& leaf : member_leaves) {
                member_width += static_cast<std::uint64_t>(leaf.width) * leaf.count;
            }
            if (member_width == 0U) {
                return false;
            }
            const bool character_member = member_leaves.size() == 1U
                && member_leaves.front().kind == Kind::character;
            if (width == 0U || subtype.unconstrained) {
                if (!top) {
                    return false;
                }
                unconstrained_top = true;
                if (character_member) {
                    member_leaves.front().kind = Kind::characters;
                }
                leaves = std::move(member_leaves);
                return true;
            }
            if (width % member_width != 0U) {
                return false;
            }
            const auto count = width / member_width;
            if (character_member) {
                Leaf run;
                run.kind = Kind::characters;
                run.width = width;
                run.count = static_cast<std::uint32_t>(count);
                leaves.push_back(std::move(run));
                return true;
            }
            if (count * member_leaves.size() > 65536U) {
                return false;
            }
            for (std::uint64_t index { }; index < count; ++index) {
                leaves.insert(leaves.end(), member_leaves.begin(), member_leaves.end());
            }
            return true;
        }
        if (hir_vhdl_subtype_is_real(subtype)) {
            Leaf leaf;
            leaf.kind = Kind::real;
            leaf.width = 64U;
            return scalar(std::move(leaf));
        }
        if (named("time") || named("delay_length")) {
            Leaf leaf;
            leaf.kind = Kind::time;
            leaf.width = 64U;
            return scalar(std::move(leaf));
        }
        if (definition != nullptr && definition->form == Form::physical) {
            Leaf leaf;
            leaf.kind = Kind::physical;
            for (const auto& unit : definition->physical_units) {
                std::optional<std::int64_t> scale = unit.scale_factor;
                if (!scale && !unit.scale) {
                    scale = 1;
                }
                if (!scale && unit.scale) {
                    const auto expression = specialized_hir_unit_->find_expression(*unit.scale);
                    constexpr std::string_view prefix { "@vhdl-physical:" };
                    if (expression && expression->vhdl != nullptr
                        && expression->vhdl->text.starts_with(prefix)
                        && expression->vhdl->operands.size() == 1U) {
                        const auto referenced = lower_case(
                            std::string_view { expression->vhdl->text }.substr(prefix.size()));
                        const auto prior = std::ranges::find_if(leaf.units,
                            [&](const auto& candidate) { return candidate.first == referenced; });
                        const auto multiplier = specialized_hir_unit_->evaluate_integral_expression(
                            expression->vhdl->operands.front());
                        if (prior != leaf.units.end() && multiplier && *multiplier >= 0
                            && (*multiplier == 0
                                || prior->second
                                    <= std::numeric_limits<std::int64_t>::max() / *multiplier)) {
                            scale = prior->second * *multiplier;
                        }
                    }
                }
                if (!scale) {
                    return false;
                }
                leaf.units.emplace_back(lower_case(unit.name), *scale);
            }
            if (leaf.units.empty()) {
                return false;
            }
            return scalar(std::move(leaf));
        }
        if (named("character")) {
            Leaf leaf;
            leaf.kind = Kind::character;
            leaf.width = 8U;
            return scalar(std::move(leaf));
        }
        Leaf enumeration;
        enumeration.kind = Kind::enumeration;
        if (definition != nullptr && !definition->enumeration_literals.empty()) {
            for (const auto& literal : definition->enumeration_literals) {
                enumeration.literals.push_back(literal.spelling.starts_with('\'')
                        ? literal.spelling
                        : lower_case(literal.spelling));
            }
        } else if (named("boolean")) {
            enumeration.literals = { "false", "true" };
        } else if (named("bit")) {
            enumeration.literals = { "'0'", "'1'" };
        } else if (named("severity_level")) {
            enumeration.literals = { "note", "warning", "error", "failure" };
        }
        if (!enumeration.literals.empty()) {
            if (subtype.domain == semantic::vhdl::ValueDomain::logic9
                || subtype.domain == semantic::vhdl::ValueDomain::logic4) {
                return false;
            }
            if (width == 0U) {
                std::uint32_t bits = 1U;
                while ((std::uint64_t { 1 } << bits) < enumeration.literals.size()) {
                    ++bits;
                }
                enumeration.width = bits;
            }
            return scalar(std::move(enumeration));
        }
        if (subtype.domain == semantic::vhdl::ValueDomain::integer) {
            Leaf leaf;
            leaf.kind = Kind::integer;
            return scalar(std::move(leaf));
        }
        return false;
    };

    std::vector<Leaf> leaves;
    if (!flatten(element, 0U, true, leaves) || leaves.empty()) {
        return HirVhdlFileImageResult::unsupported;
    }
    std::uint64_t part_width { };
    for (const auto& leaf : leaves) {
        part_width += leaf.kind == Kind::characters && unconstrained_top
            ? 8U
            : leaf.width;
    }
    if (part_width == 0U) {
        return HirVhdlFileImageResult::unsupported;
    }

    const auto load = [&](const std::size_t bits, const std::uint64_t constant,
                          const frontend::ValueDomain domain) {
        const auto result = allocate_register(bits, domain);
        process_.operations.emplace_back(LoadConstant {
            result, PackedLogic4::from_aval_bval(static_cast<std::uint32_t>(bits),
                        constant, 0U) });
        return result;
    };
    const auto append_text = [&](const StringRegisterId target, std::string text) {
        const auto literal = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant { literal, std::move(text) });
        process_.operations.emplace_back(ConcatenateStrings { target, { target, literal } });
    };
    const auto append_formatted = [&](const StringRegisterId target, const RegisterId source,
                                      const std::size_t bits,
                                      const runtime::simir::OutputFormat format,
                                      const bool signed_decimal,
                                      const frontend::SystemVerilogScalarKind kind) {
        StringMethod operation;
        operation.operation = StringMethodOperator::format_packed;
        operation.source = target;
        operation.first = source;
        operation.second = load(32U, bits, frontend::ValueDomain::Bit2);
        operation.format = format;
        operation.signed_decimal = signed_decimal;
        operation.suppress_leading_zero = format == runtime::simir::OutputFormat::decimal;
        operation.scalar_kind = kind;
        process_.operations.emplace_back(operation);
    };
    // Select a constant by matching a token against texts: `on_match(index)`
    // emits the action for text `index`; unmatched tokens leave `matched`
    // false.
    const auto match_token = [&](const StringRegisterId token,
                                 const std::vector<std::string>& texts,
                                 const RegisterId matched,
                                 const std::function<void(std::size_t)>& on_match) {
        process_.operations.emplace_back(LoadConstant {
            matched, PackedLogic4::from_aval_bval(1U, 0U, 0U) });
        std::vector<InstructionIndex> exits;
        for (std::size_t index { }; index < texts.size(); ++index) {
            const auto literal = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant { literal, texts[index] });
            StringMethod compare;
            compare.operation = StringMethodOperator::icompare;
            compare.destination = allocate_register(32U, frontend::ValueDomain::Integer);
            compare.source = token;
            compare.argument = literal;
            process_.operations.emplace_back(compare);
            const auto equal = allocate_register(1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary { BinaryOperator::equal, equal,
                compare.destination, load(32U, 0U, frontend::ValueDomain::Integer) });
            const auto branch = static_cast<InstructionIndex>(process_.operations.size());
            process_.operations.emplace_back(Branch { equal, 0U, 0U, UnknownBranchPolicy::when_false });
            const auto selected = static_cast<InstructionIndex>(process_.operations.size());
            on_match(index);
            process_.operations.emplace_back(LoadConstant {
                matched, PackedLogic4::from_aval_bval(1U, 1U, 0U) });
            exits.push_back(static_cast<InstructionIndex>(process_.operations.size()));
            process_.operations.emplace_back(Jump { });
            process_.operations[branch] = Branch { equal, selected,
                static_cast<InstructionIndex>(process_.operations.size()),
                UnknownBranchPolicy::when_false };
        }
        const auto done = static_cast<InstructionIndex>(process_.operations.size());
        for (const auto exit : exits) {
            process_.operations[exit] = Jump { done };
        }
    };
    const bool string_element = leaves.size() == 1U
        && leaves.front().kind == Kind::characters;
    if (write && string_element && hir_expression_is_string(value, hir_process_scope_)) {
        const auto text = lower_hir_string_expression(value);
        if (!text) {
            return HirVhdlFileImageResult::failed;
        }
        process_.operations.emplace_back(FileWriteString { handle, *text, { }, { }, true, false });
        return HirVhdlFileImageResult::lowered;
    }
    if (write) {
        // The value, with an aggregate taking the element subtype as its
        // context when the subtype is constrained.
        std::optional<RegisterId> lowered;
        const auto expression = specialized_hir_unit_->find_expression(value);
        const bool aggregate = expression && expression->vhdl != nullptr
            && expression->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate;
        const bool character_literal = expression && expression->vhdl != nullptr
            && expression->vhdl->kind == semantic::vhdl::ExpressionKind::logic_literal
            && expression->vhdl->text.size() == 3U && expression->vhdl->text.front() == '\''
            && leaves.size() == 1U && leaves.front().kind == Kind::character;
        if (character_literal) {
            // A character literal in a CHARACTER context is its position.
            lowered = load(8U, static_cast<unsigned char>(expression->vhdl->text[1]),
                frontend::ValueDomain::Bit2);
        } else if (!unconstrained_top) {
            lowered = aggregate
                ? lower_hir_vhdl_aggregate(value, part_width, &element)
                : lower_hir_expression(value, part_width);
            if (lowered && register_width(*lowered) != part_width && !aggregate) {
                lowered = resize_register(*lowered, part_width,
                    leaves.front().kind == Kind::integer);
            }
        } else {
            auto value_width = aggregate
                ? std::optional<std::size_t> { }
                : hir_expression_width(value, hir_process_scope_);
            if (aggregate
                && std::ranges::all_of(expression->vhdl->associations,
                    [](const auto& association) {
                        return association.choices.empty()
                            && association.choice_spelling.empty();
                    })) {
                value_width = expression->vhdl->associations.size() * part_width;
            }
            if (!value_width || *value_width == 0U || *value_width % part_width != 0U) {
                return HirVhdlFileImageResult::unsupported;
            }
            if (aggregate) {
                // A positional aggregate takes its index range from the
                // index subtype, starting at its left bound (IEEE 1076-2008
                // 9.3.3.3); only the element values are written.
                auto context = hir_effective_vhdl_subtype(element).value_or(element);
                context.unconstrained = false;
                context.executable_width = *value_width;
                semantic::vhdl::RangeConstraint range;
                range.kind = semantic::vhdl::RangeKind::array_index;
                std::int64_t left { };
                if (const auto* array = [&]() -> const semantic::vhdl::TypeDefinition* {
                        auto type_id = context.type_mark.target;
                        std::unordered_set<std::uint32_t> visited;
                        while (type_id.valid() && visited.insert(type_id.value()).second) {
                            const auto type = specialized_hir_unit_->find_type(type_id);
                            if (!type || type->vhdl == nullptr) {
                                return nullptr;
                            }
                            if (type->vhdl->form == semantic::vhdl::TypeForm::array) {
                                return type->vhdl;
                            }
                            type_id = type->vhdl->base.type_mark.target;
                        }
                        return nullptr;
                    }();
                    array != nullptr && !array->array_dimensions.empty()
                    && simple_type_name(array->array_dimensions.front().index_subtype.spelling)
                        == "positive") {
                    left = 1;
                }
                range.left = left;
                range.right = left + static_cast<std::int64_t>(*value_width / part_width) - 1;
                context.constraints = { range };
                lowered = lower_hir_vhdl_aggregate(value, *value_width, &context);
            } else {
                lowered = lower_hir_expression(value, *value_width);
            }
        }
        if (!lowered) {
            return HirVhdlFileImageResult::failed;
        }
        const auto total = register_width(*lowered);
        if (total == 0U || total % part_width != 0U
            || (!unconstrained_top && total != part_width)) {
            return HirVhdlFileImageResult::unsupported;
        }
        const auto repeats = total / part_width;
        std::size_t used { };
        bool first = true;
        const bool scalar_element = leaves.size() == 1U && !unconstrained_top
            && leaves.front().kind != Kind::characters;
        for (std::size_t repeat { }; repeat < repeats; ++repeat) {
            for (const auto& leaf : leaves) {
                const auto leaf_width = leaf.kind == Kind::characters && unconstrained_top
                    ? std::size_t { 8U }
                    : std::size_t { leaf.width };
                used += leaf_width;
                const auto part = allocate_register(leaf_width,
                    leaf.kind == Kind::integer || leaf.kind == Kind::physical
                            || leaf.kind == Kind::time
                        ? frontend::ValueDomain::Integer
                        : frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Extract { part, *lowered,
                    static_cast<std::uint32_t>(total - used),
                    static_cast<std::uint32_t>(leaf_width) });
                const bool run_continues = leaf.kind == Kind::characters && unconstrained_top
                    && repeat != 0U;
                const bool separator = !first && !run_continues;
                first = false;
                // Each part is written as it is formatted, so a line is not
                // limited by the runtime string size.
                const auto text = allocate_string_register();
                process_.operations.emplace_back(LoadStringConstant { text, { } });
                switch (leaf.kind) {
                case Kind::integer:
                    append_formatted(text, part, leaf_width,
                        runtime::simir::OutputFormat::decimal, true,
                        frontend::SystemVerilogScalarKind::None);
                    break;
                case Kind::real:
                    append_formatted(text, part, 64U,
                        runtime::simir::OutputFormat::real_scientific, false,
                        frontend::SystemVerilogScalarKind::Real);
                    break;
                case Kind::character:
                case Kind::characters:
                    // A CHARACTER element on its own is written as its
                    // literal image ('c'), so the reader can tell a quote
                    // from a quoted character.
                    if (scalar_element) {
                        append_text(text, "'");
                    }
                    append_formatted(text, part, leaf_width,
                        runtime::simir::OutputFormat::character, false,
                        frontend::SystemVerilogScalarKind::None);
                    if (scalar_element) {
                        append_text(text, "'");
                    }
                    break;
                case Kind::physical:
                    append_formatted(text, part, leaf_width,
                        runtime::simir::OutputFormat::decimal, true,
                        frontend::SystemVerilogScalarKind::None);
                    append_text(text, " " + leaf.units.front().first);
                    break;
                case Kind::time: {
                    StringMethod operation;
                    operation.operation = StringMethodOperator::format_vhdl_time;
                    operation.source = text;
                    operation.first = part;
                    operation.precision = 0U;
                    process_.operations.emplace_back(operation);
                    break;
                }
                case Kind::enumeration: {
                    std::vector<InstructionIndex> exits;
                    for (std::size_t ordinal { }; ordinal < leaf.literals.size(); ++ordinal) {
                        auto image = leaf.literals[ordinal];
                        if (image.starts_with('\'')) {
                            if (!scalar_element && image.size() == 3U) {
                                image = image.substr(1U, 1U);
                            }
                        } else {
                            std::ranges::transform(image, image.begin(),
                                [](const unsigned char character) {
                                    return static_cast<char>(std::toupper(character));
                                });
                        }
                        const auto equal = allocate_register(1U, frontend::ValueDomain::Boolean);
                        process_.operations.emplace_back(Binary { BinaryOperator::equal, equal,
                            part, load(leaf_width, ordinal, frontend::ValueDomain::Bit2) });
                        const auto branch = static_cast<InstructionIndex>(process_.operations.size());
                        process_.operations.emplace_back(Branch { equal, 0U, 0U,
                            UnknownBranchPolicy::when_false });
                        const auto selected = static_cast<InstructionIndex>(process_.operations.size());
                        append_text(text, std::move(image));
                        exits.push_back(static_cast<InstructionIndex>(process_.operations.size()));
                        process_.operations.emplace_back(Jump { });
                        process_.operations[branch] = Branch { equal, selected,
                            static_cast<InstructionIndex>(process_.operations.size()),
                            UnknownBranchPolicy::when_false };
                    }
                    const auto done = static_cast<InstructionIndex>(process_.operations.size());
                    for (const auto exit : exits) {
                        process_.operations[exit] = Jump { done };
                    }
                    break;
                }
                }
                process_.operations.emplace_back(FileWriteString { handle, text,
                    separator ? std::string { " " } : std::string { }, { }, false, false });
            }
        }
        const auto end_of_line = allocate_string_register();
        process_.operations.emplace_back(LoadStringConstant { end_of_line, { } });
        process_.operations.emplace_back(FileWriteString { handle, end_of_line, { }, { }, true, false });
        return HirVhdlFileImageResult::lowered;
    }

    // READ: the element's line, then its parts scanned from it.
    const auto declaration = hir_target_declaration(value);
    if (string_element && declaration) {
        if (const auto text = hir_string_binding(*declaration, hir_process_scope_, true);
            text && text->local) {
            const auto line = allocate_string_register();
            process_.operations.emplace_back(FileReadLine { allocate_register(32U,
                                                                frontend::ValueDomain::Integer),
                handle, line, 0U, FileReadKind::line, true });
            const auto characters = allocate_register(32U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(StringLength { characters, line });
            process_.operations.emplace_back(CopyStringRegister { *text->local, line });
            if (length) {
                const auto length_declaration = hir_target_declaration(*length);
                const auto length_binding = length_declaration
                    ? hir_runtime_binding(*length_declaration, hir_process_scope_, true)
                    : std::nullopt;
                if (!length_binding || !length_binding->local) {
                    return HirVhdlFileImageResult::unsupported;
                }
                process_.operations.emplace_back(CopyRegister { *length_binding->local,
                    length_binding->width == 32U
                        ? characters
                        : resize_register(characters, length_binding->width, true) });
            }
            return HirVhdlFileImageResult::lowered;
        }
    }
    const auto value_subtype = hir_vhdl_expression_subtype(value);
    if (string_element && unconstrained_top && value_subtype
        && (value_subtype->domain == semantic::vhdl::ValueDomain::integer
            || value_subtype->domain == semantic::vhdl::ValueDomain::boolean
            || hir_vhdl_subtype_is_real(*value_subtype))) {
        report("FSIM-ELAB-VHFILE-012",
            "direct VHDL file read target must be a writable variable of "
            "the file element type",
            span);
        return HirVhdlFileImageResult::failed;
    }
    const auto target = declaration
        ? hir_runtime_binding(*declaration, hir_process_scope_, true)
        : std::nullopt;
    if (!target || !target->local || target->width == 0U
        || target->width > std::numeric_limits<std::uint32_t>::max()
        || target->width % part_width != 0U
        || (!unconstrained_top && target->width != part_width)) {
        report("FSIM-ELAB-VHFILE-012",
            "direct VHDL file read target must be a writable variable of "
            "the file element type",
            span);
        return HirVhdlFileImageResult::failed;
    }
    // A fixed-shape element is scanned from the file and the rest of its
    // line skipped; a variable-length one is scanned from its line, which
    // bounds its element count.
    std::optional<StringRegisterId> line;
    if (unconstrained_top) {
        line = allocate_string_register();
        process_.operations.emplace_back(FileReadLine { allocate_register(32U,
                                                            frontend::ValueDomain::Integer),
            handle, *line, 0U, FileReadKind::line, true });
    }
    const auto scan = [&](InputScanConversion conversion, const std::optional<RegisterId> ok) {
        process_.operations.emplace_back(FileScan {
            allocate_register(32U, frontend::ValueDomain::Integer),
            handle, line.value_or(0U), line.has_value(), { std::move(conversion) }, { },
            false, ok, line.has_value() });
    };
    const auto raw_character = [&](const RegisterId ok, const bool keep) {
        const auto result = allocate_register(8U, frontend::ValueDomain::Bit2);
        InputScanConversion conversion;
        conversion.format = InputScanFormat::character;
        conversion.maximum_characters = 1U;
        conversion.suppress = !keep;
        conversion.target = { InputScanTargetKind::packed_register, result, 8U, true };
        scan(std::move(conversion), keep ? std::optional { ok } : std::nullopt);
        return result;
    };
    const auto both = [&](const RegisterId left, const RegisterId right) {
        const auto result = allocate_register(1U, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(Binary { BinaryOperator::bit_and, result, left, right });
        return result;
    };
    std::optional<RegisterId> resolution;
    // Scan one part; returns its value and whether it converted.
    const auto scan_leaf = [&](const Leaf& leaf, const bool first_part)
        -> std::pair<RegisterId, RegisterId> {
        const auto ok = allocate_register(1U, frontend::ValueDomain::Boolean);
        const auto token = [&] {
            const auto text = allocate_string_register();
            InputScanConversion conversion;
            conversion.format = InputScanFormat::string;
            conversion.target = { InputScanTargetKind::string_register, text, 1U, true };
            scan(std::move(conversion), ok);
            return text;
        };
        const auto number = [&](const std::size_t bits, const bool real) {
            const auto result = allocate_register(bits,
                real ? frontend::ValueDomain::Bit2 : frontend::ValueDomain::Integer);
            InputScanConversion conversion;
            conversion.format = real ? InputScanFormat::real : InputScanFormat::decimal;
            conversion.target = { InputScanTargetKind::packed_register, result,
                static_cast<std::uint32_t>(bits), true,
                real ? frontend::SystemVerilogScalarKind::Real
                     : frontend::SystemVerilogScalarKind::None };
            scan(std::move(conversion), ok);
            return result;
        };
        switch (leaf.kind) {
        case Kind::integer: {
            if (leaf.width > 32U) {
                return { number(leaf.width, false), ok };
            }
            // A decimal image, or a one-digit character literal image.
            const auto text = token();
            StringMethod convert;
            convert.operation = StringMethodOperator::atoi;
            convert.destination = allocate_register(32U, frontend::ValueDomain::Integer);
            convert.source = text;
            process_.operations.emplace_back(convert);
            StringMethod first_character;
            first_character.operation = StringMethodOperator::getc;
            first_character.destination = allocate_register(32U, frontend::ValueDomain::Integer);
            first_character.source = text;
            first_character.first = load(32U, 0U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(first_character);
            const auto quoted = allocate_register(1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary { BinaryOperator::equal, quoted,
                first_character.destination, load(32U, '\'', frontend::ValueDomain::Integer) });
            const auto branch = static_cast<InstructionIndex>(process_.operations.size());
            process_.operations.emplace_back(Branch { quoted, 0U, 0U, UnknownBranchPolicy::when_false });
            const auto taken = static_cast<InstructionIndex>(process_.operations.size());
            StringMethod digit;
            digit.operation = StringMethodOperator::getc;
            digit.destination = allocate_register(32U, frontend::ValueDomain::Integer);
            digit.source = text;
            digit.first = load(32U, 1U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(digit);
            process_.operations.emplace_back(IntegerBinary { IntegerBinaryOperator::subtract,
                convert.destination, digit.destination,
                load(32U, '0', frontend::ValueDomain::Integer) });
            process_.operations[branch] = Branch { quoted, taken,
                static_cast<InstructionIndex>(process_.operations.size()),
                UnknownBranchPolicy::when_false };
            return { leaf.width == 32U
                    ? convert.destination
                    : resize_register(convert.destination, leaf.width, true),
                ok };
        }
        case Kind::real:
            return { number(64U, true), ok };
        case Kind::characters: {
            const auto bits = static_cast<std::size_t>(leaf.count) * 8U;
            const auto result = allocate_register(bits, frontend::ValueDomain::Bit2);
            if (!first_part) {
                (void)raw_character(ok, false);
            }
            InputScanConversion conversion;
            conversion.format = InputScanFormat::character;
            conversion.maximum_characters = leaf.count;
            conversion.target = { InputScanTargetKind::packed_register, result,
                static_cast<std::uint32_t>(bits), true };
            scan(std::move(conversion), ok);
            return { result, ok };
        }
        case Kind::character: {
            // The character, after one separator, as a character literal
            // image ('c') or bare: a CHARACTER part may be a space.
            if (!first_part) {
                (void)raw_character(ok, false);
            }
            const auto first_character = raw_character(ok, true);
            const auto quoted = allocate_register(1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(Binary { BinaryOperator::equal, quoted,
                first_character, load(8U, '\'', frontend::ValueDomain::Bit2) });
            const auto selected = allocate_register(8U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(CopyRegister { selected, first_character });
            const auto branch = static_cast<InstructionIndex>(process_.operations.size());
            process_.operations.emplace_back(Branch { quoted, 0U, 0U, UnknownBranchPolicy::when_false });
            const auto taken = static_cast<InstructionIndex>(process_.operations.size());
            const auto inner_ok = allocate_register(1U, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(CopyRegister { selected, raw_character(inner_ok, true) });
            (void)raw_character(inner_ok, false);
            process_.operations[branch] = Branch { quoted, taken,
                static_cast<InstructionIndex>(process_.operations.size()),
                UnknownBranchPolicy::when_false };
            return { selected, ok };
        }
        case Kind::enumeration: {
            const auto text = token();
            std::vector<std::string> texts;
            std::vector<std::size_t> ordinals;
            for (std::size_t ordinal { }; ordinal < leaf.literals.size(); ++ordinal) {
                texts.push_back(leaf.literals[ordinal]);
                ordinals.push_back(ordinal);
                if (leaf.literals[ordinal].size() == 3U
                    && leaf.literals[ordinal].starts_with('\'')) {
                    texts.push_back(leaf.literals[ordinal].substr(1U, 1U));
                    ordinals.push_back(ordinal);
                }
            }
            const auto result = allocate_register(leaf.width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                result, PackedLogic4::from_aval_bval(leaf.width, 0U, 0U) });
            const auto matched = allocate_register(1U, frontend::ValueDomain::Boolean);
            match_token(text, texts, matched, [&](const std::size_t index) {
                process_.operations.emplace_back(LoadConstant { result,
                    PackedLogic4::from_aval_bval(leaf.width, ordinals[index], 0U) });
            });
            return { result, both(ok, matched) };
        }
        case Kind::physical:
        case Kind::time: {
            const auto magnitude = number(64U, false);
            const auto unit = token();
            const auto scale = allocate_register(64U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant {
                scale, PackedLogic4::from_aval_bval(64U, 1U, 0U) });
            std::vector<std::string> names;
            std::vector<std::uint64_t> scales;
            if (leaf.kind == Kind::time) {
                for (const auto& [name, factor] : vhdl_time_units) {
                    names.emplace_back(name);
                    scales.push_back(factor);
                }
            } else {
                for (const auto& [name, factor] : leaf.units) {
                    names.push_back(name);
                    scales.push_back(static_cast<std::uint64_t>(factor));
                }
            }
            const auto matched = allocate_register(1U, frontend::ValueDomain::Boolean);
            match_token(unit, names, matched, [&](const std::size_t index) {
                process_.operations.emplace_back(LoadConstant { scale,
                    PackedLogic4::from_aval_bval(64U, scales[index], 0U) });
            });
            auto result = allocate_register(64U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerBinary {
                IntegerBinaryOperator::multiply, result, magnitude, scale });
            if (leaf.kind == Kind::time) {
                // TIME values count resolution ticks: the image of one tick
                // in fs gives the resolution.
                if (!resolution) {
                    const auto image = allocate_string_register();
                    process_.operations.emplace_back(LoadStringConstant { image, { } });
                    StringMethod operation;
                    operation.operation = StringMethodOperator::format_vhdl_time;
                    operation.source = image;
                    operation.first = load(64U, 1U, frontend::ValueDomain::Integer);
                    operation.precision = 0U;
                    process_.operations.emplace_back(operation);
                    resolution = allocate_register(64U, frontend::ValueDomain::Integer);
                    InputScanConversion conversion;
                    conversion.format = InputScanFormat::decimal;
                    conversion.target = { InputScanTargetKind::packed_register,
                        *resolution, 64U, true };
                    process_.operations.emplace_back(FileScan {
                        allocate_register(32U, frontend::ValueDomain::Integer),
                        handle, image, true, { std::move(conversion) }, { }, false,
                        std::nullopt, false });
                }
                const auto ticks = allocate_register(64U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(IntegerBinary {
                    IntegerBinaryOperator::divide, ticks, result, *resolution });
                result = ticks;
            }
            if (leaf.width != 64U) {
                result = resize_register(result, leaf.width, true);
            }
            return { result, both(ok, matched) };
        }
        }
        return { load(leaf.width, 0U, frontend::ValueDomain::Bit2), ok };
    };
    const auto assemble = [&](const std::vector<RegisterId>& parts, const std::size_t bits) {
        if (parts.size() == 1U) {
            return parts.front();
        }
        const auto result = allocate_register(bits, target->domain);
        process_.operations.emplace_back(Concatenate { result, parts,
            static_cast<std::uint32_t>(bits) });
        return result;
    };
    // A part that did not convert is an error, as for TEXTIO.READ without
    // GOOD (IEEE 1076-2008 16.4).
    const auto require = [&](const RegisterId ok) {
        process_.operations.emplace_back(Assert { ok,
            "VHDL file read did not convert the requested element",
            AssertionSeverity::failure,
            SourceLocation { span.source_name.str(),
                static_cast<std::uint32_t>(span.begin.line),
                static_cast<std::uint32_t>(span.begin.column) } });
    };
    if (!unconstrained_top) {
        std::vector<RegisterId> parts;
        bool first = true;
        for (const auto& leaf : leaves) {
            const auto [part, ok] = scan_leaf(leaf, first);
            first = false;
            require(ok);
            parts.push_back(part);
        }
        process_.operations.emplace_back(CopyRegister {
            *target->local, assemble(parts, part_width) });
        process_.operations.emplace_back(FileReadLine { allocate_register(32U,
                                                            frontend::ValueDomain::Integer),
            handle, allocate_string_register(), 0U, FileReadKind::line, false });
        return HirVhdlFileImageResult::lowered;
    }

    // An unconstrained array element: as many elements as the line holds,
    // up to the target's length, and their count in LENGTH.
    const auto count = target->width / part_width;
    if (count > 4096U) {
        return HirVhdlFileImageResult::unsupported;
    }
    const auto length_binding = length
        ? [&]() -> std::optional<HirRuntimeBinding> {
              const auto length_declaration = hir_target_declaration(*length);
              return length_declaration
                  ? hir_runtime_binding(*length_declaration, hir_process_scope_, true)
                  : std::nullopt;
          }()
        : std::nullopt;
    if (length && (!length_binding || !length_binding->local)) {
        return HirVhdlFileImageResult::unsupported;
    }
    const auto result = allocate_register(target->width, target->domain);
    process_.operations.emplace_back(CopyRegister { result, *target->local });
    const auto elements = allocate_register(32U, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        elements, PackedLogic4::from_aval_bval(32U, 0U, 0U) });
    std::vector<std::tuple<InstructionIndex, RegisterId, InstructionIndex>> exits;
    for (std::size_t index { }; index < count; ++index) {
        std::vector<RegisterId> parts;
        std::optional<RegisterId> converted;
        bool first = index == 0U;
        for (const auto& leaf : leaves) {
            auto single = leaf;
            if (single.kind == Kind::characters) {
                single.count = 1U;
                single.width = 8U;
            }
            const auto [part, ok] = scan_leaf(single, first);
            first = false;
            converted = converted ? both(*converted, ok) : ok;
            parts.push_back(part);
        }
        const auto branch = static_cast<InstructionIndex>(process_.operations.size());
        process_.operations.emplace_back(Branch { *converted, 0U, 0U, UnknownBranchPolicy::when_false });
        const auto stored = static_cast<InstructionIndex>(process_.operations.size());
        const auto element_value = assemble(parts, part_width);
        process_.operations.emplace_back(Insert { result, result, element_value,
            static_cast<std::uint32_t>(target->width - (index + 1U) * part_width) });
        process_.operations.emplace_back(LoadConstant { elements,
            PackedLogic4::from_aval_bval(32U, index + 1U, 0U) });
        exits.emplace_back(branch, *converted, stored);
    }
    const auto done = static_cast<InstructionIndex>(process_.operations.size());
    for (const auto& [branch, converted, stored] : exits) {
        process_.operations[branch] = Branch { converted, stored, done,
            UnknownBranchPolicy::when_false };
    }
    process_.operations.emplace_back(CopyRegister { *target->local, result });
    if (length_binding) {
        process_.operations.emplace_back(CopyRegister { *length_binding->local,
            length_binding->width == 32U
                ? elements
                : resize_register(elements, length_binding->width, true) });
    }
    return HirVhdlFileImageResult::lowered;
}

} // namespace fsim::elaboration
