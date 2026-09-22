// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::semantic {

std::string systemverilog_string_identity(const std::string_view bytes)
{
    static constexpr std::string_view hexadecimal { "0123456789abcdef" };
    std::string result { "svstring-v1;bytes=" };
    result += std::to_string(bytes.size());
    result += ";hex=";
    for (const char raw_byte : bytes) {
        const auto byte = static_cast<unsigned char>(raw_byte);
        result.push_back(hexadecimal[byte >> 4U]);
        result.push_back(hexadecimal[byte & 0x0fU]);
    }
    return result;
}

namespace {

std::optional<std::string> parse_systemverilog_string_identity(
    const std::string_view identity)
{
    static constexpr std::string_view prefix { "svstring-v1;bytes=" };
    static constexpr std::string_view separator { ";hex=" };
    if (!identity.starts_with(prefix)) {
        return std::nullopt;
    }
    auto remainder = identity.substr(prefix.size());
    const auto separator_offset = remainder.find(separator);
    if (separator_offset == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t byte_count { };
    const auto count = remainder.substr(0U, separator_offset);
    const auto parsed = std::from_chars(
        count.data(), count.data() + count.size(), byte_count);
    if (count.empty() || parsed.ec != std::errc { }
        || parsed.ptr != count.data() + count.size()) {
        return std::nullopt;
    }
    const auto hexadecimal = remainder.substr(
        separator_offset + separator.size());
    if (hexadecimal.size() % 2U != 0U
        || byte_count != hexadecimal.size() / 2U) {
        return std::nullopt;
    }
    const auto digit = [](const char character)
        -> std::optional<unsigned char> {
        if (character >= '0' && character <= '9') {
            return static_cast<unsigned char>(character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<unsigned char>(
                character - 'a' + 10);
        }
        if (character >= 'A' && character <= 'F') {
            return static_cast<unsigned char>(
                character - 'A' + 10);
        }
        return std::nullopt;
    };
    std::string bytes;
    bytes.reserve(byte_count);
    for (std::size_t offset = 0U; offset < hexadecimal.size();
         offset += 2U) {
        const auto high = digit(hexadecimal[offset]);
        const auto low = digit(hexadecimal[offset + 1U]);
        if (!high || !low) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<char>((*high << 4U) | *low));
    }
    return bytes;
}

std::string_view normalized_library(const std::string_view library)
{
    return library.empty() ? std::string_view { "work" } : library;
}

bool vhdl_name_equal(
    const std::string_view left, const std::string_view right)
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), right.end(),
            [](const unsigned char lhs, const unsigned char rhs) {
                return std::tolower(lhs) == std::tolower(rhs);
            });
}

bool systemverilog_actual_form(const sv::DeclarationForm form)
{
    return form == sv::DeclarationForm::parameter
        || form == sv::DeclarationForm::type_parameter;
}

bool vhdl_actual_form(const vhdl::DeclarationForm form)
{
    switch (form) {
    case vhdl::DeclarationForm::generic_constant:
    case vhdl::DeclarationForm::generic_type:
    case vhdl::DeclarationForm::generic_function:
    case vhdl::DeclarationForm::generic_procedure:
    case vhdl::DeclarationForm::generic_package:
        return true;
    default:
        return false;
    }
}

bool vhdl_evaluable_value_form(const vhdl::DeclarationForm form)
{
    return form == vhdl::DeclarationForm::generic_constant
        || form == vhdl::DeclarationForm::constant
        || form == vhdl::DeclarationForm::enumeration_literal
        || form == vhdl::DeclarationForm::port
        || form == vhdl::DeclarationForm::variable;
}

bool vhdl_type_declaration_form(const vhdl::DeclarationForm form)
{
    return form == vhdl::DeclarationForm::type
        || form == vhdl::DeclarationForm::subtype
        || form == vhdl::DeclarationForm::generic_type;
}

bool vhdl_attribute_prefix_form(const vhdl::DeclarationForm form)
{
    return vhdl_type_declaration_form(form)
        || vhdl_evaluable_value_form(form)
        || form == vhdl::DeclarationForm::signal
        || form == vhdl::DeclarationForm::alias;
}

bool declaration_belongs_to(const CompiledDesign& design,
    const DeclarationId declaration, const UnitId unit)
{
    if (!declaration.valid()
        || declaration.value() >= design.semantics.declarations().size()) {
        return false;
    }
    const auto scope
        = design.semantics.declarations()[declaration.value()].scope;
    return scope.valid() && scope.value() < design.semantics.scopes().size()
        && design.semantics.scopes()[scope.value()].unit == unit;
}

const vhdl::Unit* primary_entity(
    const CompiledDesign& design, const vhdl::Unit& architecture)
{
    if (architecture.kind != vhdl::UnitKind::architecture) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(
        design.vhdl_units(), [&](const vhdl::Unit& candidate) {
            return candidate.kind == vhdl::UnitKind::entity
                && vhdl_name_equal(
                    normalized_library(candidate.library),
                    normalized_library(architecture.library))
                && vhdl_name_equal(
                    candidate.name, architecture.primary_name);
        });
    return found == design.vhdl_units().end() ? nullptr : &*found;
}

void add_systemverilog_actuals(const CompiledDesign& design,
    const sv::Unit& unit, std::set<DeclarationId>& allowed)
{
    for (const auto id : unit.declarations) {
        const auto declaration = design.find_declaration(id);
        if (declaration_belongs_to(design, id, unit.id)
            && declaration && declaration->systemverilog != nullptr
            && systemverilog_actual_form(
                declaration->systemverilog->form)) {
            allowed.insert(id);
        }
    }
}

void add_vhdl_actuals(const CompiledDesign& design,
    const vhdl::Unit& unit, std::set<DeclarationId>& allowed)
{
    for (const auto id : unit.declarations) {
        const auto declaration = design.find_declaration(id);
        if (declaration_belongs_to(design, id, unit.id)
            && declaration && declaration->vhdl != nullptr
            && vhdl_actual_form(declaration->vhdl->form)) {
            allowed.insert(id);
        }
    }
}

std::optional<DeclarationId> find_systemverilog_actual(
    const CompiledDesign& design, const sv::Unit& unit,
    const std::string_view name)
{
    const CompiledDeclarationPredicate actual
        = [&](const CompiledDeclarationView& declaration) {
              return declaration.systemverilog != nullptr
                  && declaration_belongs_to(
                      design, declaration.systemverilog->id, unit.id)
                  && systemverilog_actual_form(
                      declaration.systemverilog->form);
          };
    return CompiledDesignResolver { design, unit.id }
        .resolve_systemverilog(name, unit.scope, actual, false)
        .unique();
}

std::optional<DeclarationId> find_vhdl_actual(
    const CompiledDesign& design, const vhdl::Unit& unit,
    const std::string_view name)
{
    for (const auto id : unit.declarations) {
        const auto declaration = design.find_declaration(id);
        if (declaration_belongs_to(design, id, unit.id)
            && declaration && declaration->vhdl != nullptr
            && vhdl_actual_form(declaration->vhdl->form)
            && vhdl_name_equal(declaration->vhdl->name, name)) {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<DeclarationId> find_named_actual(
    const CompiledDesign& design, const CompiledUnitView& selected,
    const std::string_view name)
{
    if (selected.systemverilog != nullptr) {
        return find_systemverilog_actual(
            design, *selected.systemverilog, name);
    }
    if (selected.vhdl == nullptr) {
        return std::nullopt;
    }
    if (const auto* entity = primary_entity(
            design, *selected.vhdl)) {
        if (const auto declaration = find_vhdl_actual(
                design, *entity, name)) {
            return declaration;
        }
    }
    return find_vhdl_actual(design, *selected.vhdl, name);
}

struct ActualDependencies {
    std::set<DeclarationId> declarations;
    std::set<TypeId> types;
};

std::optional<ActualDependencies> validate_actuals(
    const CompiledDesign& design,
    const std::set<DeclarationId>& allowed,
    const std::span<const SpecializedHirActualIdentity> actuals)
{
    ActualDependencies result;
    for (const auto& actual : actuals) {
        if (!allowed.contains(actual.declaration)
            || !result.declarations.insert(actual.declaration).second) {
            return std::nullopt;
        }
        const auto declaration = design.find_declaration(
            actual.declaration);
        if (!declaration) {
            return std::nullopt;
        }
        if (declaration->systemverilog != nullptr
            && declaration->systemverilog->declared_type) {
            result.types.insert(
                *declaration->systemverilog->declared_type);
        }
        if (declaration->vhdl != nullptr
            && declaration->vhdl->declared_type) {
            result.types.insert(*declaration->vhdl->declared_type);
        }
    }
    return result;
}

bool intersects(const ResidualDependencies& dependencies,
    const ActualDependencies& actuals)
{
    const auto declaration_intersects = [&](const auto& values) {
        return std::ranges::any_of(values, [&](const DeclarationId id) {
            return actuals.declarations.contains(id);
        });
    };
    return declaration_intersects(dependencies.parameters)
        || declaration_intersects(dependencies.generics)
        || std::ranges::any_of(
            dependencies.types, [&](const TypeId id) {
                return actuals.types.contains(id);
            });
}

bool expression_is_in_units(const CompiledDesign& design,
    const ScopeId scope, const std::set<UnitId>& units)
{
    return scope.valid() && scope.value() < design.semantics.scopes().size()
        && units.contains(design.semantics.scopes()[scope.value()].unit);
}

template <typename Expression>
void collect_expressions(const CompiledDesign& design,
    const std::vector<Expression>& expressions,
    const std::set<UnitId>& units, const ActualDependencies& actuals,
    std::vector<ExpressionId>& output)
{
    for (const auto& expression : expressions) {
        if (!expression.folded
            && expression_is_in_units(design, expression.scope, units)
            && intersects(expression.dependencies, actuals)) {
            output.push_back(expression.id);
        }
    }
}

template <typename Generate>
void collect_generate(const Generate& generate,
    const ActualDependencies& actuals,
    std::vector<DeclarationId>& output)
{
    if (intersects(generate.dependencies, actuals)) {
        output.push_back(generate.declaration);
    }
    for (const auto& nested : generate.nested) {
        collect_generate(nested, actuals, output);
    }
}

template <typename Unit>
void collect_generates(const Unit& unit,
    const ActualDependencies& actuals,
    std::vector<DeclarationId>& output)
{
    for (const auto& generate : unit.generates) {
        collect_generate(generate, actuals, output);
    }
}

template <typename Id>
void canonicalize(std::vector<Id>& ids)
{
    std::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

bool scope_belongs_to(const CompiledDesign& design, const ScopeId scope,
    const std::vector<UnitId>& units)
{
    return scope.valid() && scope.value() < design.semantics.scopes().size()
        && std::ranges::binary_search(
            units, design.semantics.scopes()[scope.value()].unit);
}

template <typename Record, typename Id>
const Record* find_replacement(
    const std::vector<Record>& records, const Id id)
{
    const auto found = std::ranges::lower_bound(
        records, id, { }, &Record::id);
    return found != records.end() && found->id == id
        ? &*found
        : nullptr;
}

template <typename Record>
bool insert_replacement(std::vector<Record>& records, Record replacement)
{
    const auto found = std::ranges::lower_bound(
        records, replacement.id, { }, &Record::id);
    if (found != records.end() && found->id == replacement.id) {
        return false;
    }
    records.insert(found, std::move(replacement));
    return true;
}

template <typename Record, typename Identity>
bool valid_scoped_replacement(const CompiledDesign& design,
    const std::vector<UnitId>& units, const Record& replacement,
    const Record* original, const std::vector<Identity>& identities)
{
    if (original == nullptr || !replacement.id.valid()
        || replacement.id.value() >= identities.size()) {
        return false;
    }
    const auto& identity = identities[replacement.id.value()];
    return identity.id == replacement.id
        && scope_belongs_to(design, identity.scope, units)
        && replacement.scope == original->scope
        && replacement.scope == identity.scope
        && replacement.source == original->source
        && replacement.origin == original->origin;
}

template <typename Record>
bool valid_type_replacement(const CompiledDesign& design,
    const std::vector<UnitId>& units, const Record& replacement,
    const Record* original)
{
    if (original == nullptr || !replacement.id.valid()
        || replacement.id.value() >= design.semantics.types().size()) {
        return false;
    }
    const auto& identity
        = design.semantics.types()[replacement.id.value()];
    return identity.id == replacement.id
        && scope_belongs_to(design, identity.scope, units)
        && replacement.declaration == original->declaration
        && replacement.source == original->source
        && replacement.origin == original->origin;
}

template <typename Generate>
bool contains_generate(const std::vector<Generate>& generates,
    const DeclarationId declaration)
{
    return std::ranges::any_of(generates, [&](const auto& generate) {
        return generate.declaration == declaration
            || contains_generate(generate.nested, declaration);
    });
}

bool is_generate_declaration(const CompiledDesign& design,
    const Language language, const std::vector<UnitId>& units,
    const DeclarationId declaration)
{
    return std::ranges::any_of(units, [&](const UnitId unit) {
        const auto view = design.find_unit(unit);
        if (!view) {
            return false;
        }
        if (language == Language::vhdl) {
            return view->vhdl != nullptr
                && contains_generate(
                    view->vhdl->generates, declaration);
        }
        return view->systemverilog != nullptr
            && contains_generate(
                view->systemverilog->generates, declaration);
    });
}

template <typename Generate, typename AppendGenerate>
void collect_generate_instance_ids(const Generate& generate,
    std::vector<InstanceId>& instances, AppendGenerate append_generate)
{
    instances.insert(instances.end(),
        generate.instances.begin(), generate.instances.end());
    append_generate(generate.declaration, generate.instances);
    for (const auto& nested : generate.nested) {
        collect_generate_instance_ids(
            nested, instances, append_generate);
    }
}

template <typename Unit, typename AppendGenerate>
void collect_unit_instance_ids(const Unit& unit,
    std::vector<InstanceId>& instances, AppendGenerate append_generate)
{
    instances.insert(instances.end(),
        unit.instances.begin(), unit.instances.end());
    for (const auto& generate : unit.generates) {
        collect_generate_instance_ids(
            generate, instances, append_generate);
    }
}

template <typename AppendGenerate>
std::vector<InstanceId> collect_instance_ids(const CompiledDesign& design,
    const Language language, const std::vector<UnitId>& units,
    AppendGenerate append_generate)
{
    std::vector<InstanceId> result;
    for (const auto unit : units) {
        const auto view = design.find_unit(unit);
        if (!view) {
            continue;
        }
        if (language == Language::vhdl) {
            if (view->vhdl != nullptr) {
                collect_unit_instance_ids(
                    *view->vhdl, result, append_generate);
            }
            continue;
        }
        if (view->systemverilog != nullptr) {
            collect_unit_instance_ids(
                *view->systemverilog, result, append_generate);
        }
    }
    canonicalize(result);
    return result;
}

std::string normalized_token(const std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (const char raw_character : input) {
        const auto character
            = static_cast<unsigned char>(raw_character);
        if (character != '_' && !std::isspace(character)) {
            result.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return result;
}

std::optional<std::uint64_t> parse_unsigned(
    const std::string_view text, const unsigned base)
{
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value { };
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value,
        static_cast<int>(base));
    if (parsed.ec != std::errc { }
        || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> signed_bits(
    const std::uint64_t value, const std::size_t width,
    const bool is_signed)
{
    if (width == 0U) {
        return std::nullopt;
    }
    if (width > 64U) {
        return value
                <= static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())
            ? std::optional { static_cast<std::int64_t>(value) }
            : std::nullopt;
    }
    if (!is_signed || width == 64U) {
        if (!is_signed
            && value > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        if (width == 64U && is_signed
            && (value & (std::uint64_t { 1 } << 63U)) != 0U) {
            const auto magnitude = (~value) + 1U;
            if (magnitude == (std::uint64_t { 1 } << 63U)) {
                return std::numeric_limits<std::int64_t>::min();
            }
            return -static_cast<std::int64_t>(magnitude);
        }
        return static_cast<std::int64_t>(value);
    }
    const auto sign = std::uint64_t { 1 } << (width - 1U);
    if ((value & sign) == 0U) {
        return static_cast<std::int64_t>(value);
    }
    const auto mask = (std::uint64_t { 1 } << width) - 1U;
    return -static_cast<std::int64_t>(((~value) & mask) + 1U);
}

std::optional<std::int64_t> parse_systemverilog_canonical(
    const std::string_view text)
{
    if (!text.starts_with("svconst-v3:")) {
        return std::nullopt;
    }
    const auto value_marker = text.rfind(":v=");
    if (value_marker == std::string_view::npos) {
        return std::nullopt;
    }
    const auto bits = text.substr(value_marker + 3U);
    if (bits.empty()
        || std::ranges::any_of(bits, [](const char bit) {
               return bit != '0' && bit != '1';
           })) {
        return std::nullopt;
    }
    const auto signed_value
        = text.find(":s=1:") != std::string_view::npos;
    auto narrowed = bits;
    auto narrowed_signed = signed_value;
    if (bits.size() > 64U) {
        const auto prefix = bits.substr(0U, bits.size() - 64U);
        const auto extension = signed_value && bits.front() == '1'
            ? '1'
            : '0';
        if (!std::ranges::all_of(
                prefix, [&](const char bit) { return bit == extension; })) {
            return std::nullopt;
        }
        narrowed = bits.substr(bits.size() - 64U);
        narrowed_signed = signed_value && extension == '1';
    }
    const auto value = parse_unsigned(narrowed, 2U);
    if (!value) {
        return std::nullopt;
    }
    return signed_bits(*value, narrowed.size(), narrowed_signed);
}

std::optional<std::int64_t> parse_based_literal(
    const std::string_view normalized)
{
    const auto quote = normalized.find('\'');
    if (quote == std::string_view::npos) {
        const auto first_hash = normalized.find('#');
        const auto second_hash = normalized.rfind('#');
        if (first_hash == std::string_view::npos
            || first_hash == second_hash) {
            return std::nullopt;
        }
        const auto base = parse_unsigned(
            normalized.substr(0U, first_hash), 10U);
        if (!base || *base < 2U || *base > 36U) {
            return std::nullopt;
        }
        const auto value = parse_unsigned(normalized.substr(
            first_hash + 1U, second_hash - first_hash - 1U),
            static_cast<unsigned>(*base));
        if (!value
            || *value > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(*value);
    }

    const auto width = quote == 0U
        ? std::optional<std::uint64_t> { }
        : parse_unsigned(normalized.substr(0U, quote), 10U);
    auto cursor = quote + 1U;
    bool is_signed { };
    if (cursor < normalized.size() && normalized[cursor] == 's') {
        is_signed = true;
        ++cursor;
    }
    if (cursor >= normalized.size()) {
        return std::nullopt;
    }
    if (normalized[cursor] == '0' || normalized[cursor] == '1') {
        return normalized[cursor] == '0' ? 0 : 1;
    }
    unsigned base { };
    switch (normalized[cursor++]) {
    case 'b':
        base = 2U;
        break;
    case 'o':
        base = 8U;
        break;
    case 'd':
        base = 10U;
        break;
    case 'h':
        base = 16U;
        break;
    default:
        return std::nullopt;
    }
    const auto digits = normalized.substr(cursor);
    if (digits.empty()
        || std::ranges::any_of(digits, [](const char digit) {
               return digit == 'x' || digit == 'z' || digit == '?';
           })) {
        return std::nullopt;
    }
    const auto value = parse_unsigned(digits, base);
    if (!value) {
        return std::nullopt;
    }
    const auto effective_width = width.value_or(
        std::max<std::uint64_t>(1U,
            base == 2U ? digits.size()
                      : base == 8U ? digits.size() * 3U
                                   : base == 16U ? digits.size() * 4U
                                                 : 64U));
    return signed_bits(*value,
        static_cast<std::size_t>(effective_width), is_signed);
}

std::optional<std::int64_t> parse_integral_identity(
    const std::string_view input)
{
    if (const auto canonical = parse_systemverilog_canonical(input)) {
        return canonical;
    }
    if (const auto marker = input.rfind(";value=");
        input.starts_with("vhdlconst-v1;")
        && marker != std::string_view::npos) {
        return parse_integral_identity(input.substr(marker + 7U));
    }
    const auto normalized = normalized_token(input);
    if (normalized == "true") {
        return 1;
    }
    if (normalized == "false") {
        return 0;
    }
    if (const auto based = parse_based_literal(normalized)) {
        return based;
    }
    std::int64_t value { };
    const auto parsed = std::from_chars(normalized.data(),
        normalized.data() + normalized.size(), value, 10);
    if (parsed.ec != std::errc { }
        || parsed.ptr != normalized.data() + normalized.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> systemverilog_unbounded_identity(
    const std::string_view identity)
{
    if (normalized_token(identity) == "$") {
        return true;
    }
    static constexpr std::string_view prefix { "svconst-v3:b=" };
    if (identity.starts_with(prefix)) {
        const auto remainder = identity.substr(prefix.size());
        if (remainder.size() < 2U || remainder[1U] != ':') {
            return std::nullopt;
        }
        if (remainder.front() == '0') {
            return false;
        }
        return remainder.front() == '1'
            ? std::optional<bool> { true }
            : std::nullopt;
    }
    if (parse_integral_identity(identity)
        || parse_systemverilog_string_identity(identity)) {
        return false;
    }
    return std::nullopt;
}

constexpr std::uint64_t maximum_systemverilog_constant_work_units
    = 64U * 1024U * 1024U;

std::optional<std::uint64_t> systemverilog_explicit_width(
    const std::string_view identity)
{
    if (identity.starts_with("svconst-v3:")) {
        constexpr std::string_view marker { ":w=" };
        const auto begin = identity.find(marker);
        if (begin == std::string_view::npos) {
            return std::nullopt;
        }
        const auto width_begin = begin + marker.size();
        const auto width_end = identity.find(':', width_begin);
        if (width_end == std::string_view::npos) {
            return std::nullopt;
        }
        return parse_unsigned(
            identity.substr(width_begin, width_end - width_begin), 10U);
    }
    const auto quote = identity.find('\'');
    if (quote == std::string_view::npos || quote == 0U) {
        return std::nullopt;
    }
    return parse_unsigned(identity.substr(0U, quote), 10U);
}

std::optional<std::uint64_t> systemverilog_expression_width(
    const SpecializedHirUnit& unit, const ExpressionId expression,
    std::set<ExpressionId>& active)
{
    if (!active.insert(expression).second) {
        return std::nullopt;
    }
    const auto finish = [&](const std::optional<std::uint64_t> width) {
        active.erase(expression);
        return width;
    };
    const auto view = unit.find_expression(expression);
    if (!view || view->systemverilog == nullptr) {
        return finish(std::nullopt);
    }
    const auto& record = *view->systemverilog;
    if (const auto width = systemverilog_explicit_width(record.text)) {
        return finish(width);
    }
    if (record.kind == sv::ExpressionKind::name
        && record.referenced_name && record.referenced_name->selected) {
        const auto declaration = unit.find_declaration(
            *record.referenced_name->selected);
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->type
            && declaration->systemverilog->type->executable_width) {
            return finish(
                declaration->systemverilog->type->executable_width);
        }
        const auto actual = std::ranges::find(
            unit.specialization().actual_identities,
            *record.referenced_name->selected,
            &SpecializedHirActualIdentity::declaration);
        if (actual != unit.specialization().actual_identities.end()) {
            return finish(systemverilog_explicit_width(actual->identity));
        }
    }
    if (record.kind == sv::ExpressionKind::call
        && record.text.starts_with("@sv-cast:")) {
        const auto cast_width = parse_unsigned(
            std::string_view { record.text }.substr(9U), 10U);
        if (cast_width) {
            return finish(cast_width);
        }
    }
    std::optional<std::uint64_t> width;
    for (const auto operand : record.operands) {
        const auto operand_width = systemverilog_expression_width(
            unit, operand, active);
        if (!operand_width) {
            continue;
        }
        width = std::max(width.value_or(0U), *operand_width);
    }
    return finish(width);
}

bool systemverilog_multiplication_exceeds_work_limit(
    const SpecializedHirUnit& unit, const ExpressionId expression)
{
    const auto view = unit.find_expression(expression);
    if (!view || view->systemverilog == nullptr
        || view->systemverilog->kind != sv::ExpressionKind::binary
        || view->systemverilog->text != "*"
        || view->systemverilog->operands.size() != 2U) {
        return false;
    }
    std::set<ExpressionId> active;
    const auto left = systemverilog_expression_width(
        unit, view->systemverilog->operands.front(), active);
    const auto right = systemverilog_expression_width(
        unit, view->systemverilog->operands.back(), active);
    if (!left || !right) {
        return false;
    }
    const auto width = std::max(*left, *right);
    return width != 0U
        && width > maximum_systemverilog_constant_work_units / width;
}

std::optional<std::int64_t> parse_vhdl_bit_string(
    const std::string_view input)
{
    const auto normalized = normalized_token(input);
    const auto first_quote = normalized.find('"');
    const auto last_quote = normalized.rfind('"');
    if (first_quote == std::string::npos || first_quote == last_quote
        || last_quote + 1U != normalized.size()) {
        return std::nullopt;
    }
    const auto prefix = normalized.substr(0U, first_quote);
    const auto digits = normalized.substr(
        first_quote + 1U, last_quote - first_quote - 1U);
    unsigned base { };
    std::size_t bits_per_digit { };
    if (prefix.empty() || prefix == "b" || prefix == "ub"
        || prefix == "sb") {
        base = 2U;
        bits_per_digit = 1U;
    } else if (prefix == "o" || prefix == "uo" || prefix == "so") {
        base = 8U;
        bits_per_digit = 3U;
    } else if (prefix == "x" || prefix == "ux" || prefix == "sx") {
        base = 16U;
        bits_per_digit = 4U;
    } else {
        return std::nullopt;
    }
    if (digits.empty()
        || digits.size() > 64U / bits_per_digit
        || std::ranges::any_of(digits, [](const char digit) {
               return digit == 'x' || digit == 'z'
                   || digit == 'u' || digit == 'w'
                   || digit == 'l' || digit == 'h'
                   || digit == '-';
           })) {
        return std::nullopt;
    }
    const auto value = parse_unsigned(digits, base);
    if (!value) {
        return std::nullopt;
    }
    return signed_bits(
        *value, digits.size() * bits_per_digit, true);
}

std::optional<std::int64_t> checked_add(
    const std::int64_t left, const std::int64_t right)
{
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
        || (right < 0
            && left < std::numeric_limits<std::int64_t>::min() - right)) {
        return std::nullopt;
    }
    return left + right;
}

std::optional<std::int64_t> checked_subtract(
    const std::int64_t left, const std::int64_t right)
{
    if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)
        || (right > 0
            && left < std::numeric_limits<std::int64_t>::min() + right)) {
        return std::nullopt;
    }
    return left - right;
}

std::optional<std::int64_t> checked_multiply(
    const std::int64_t left, const std::int64_t right)
{
    if (left == 0 || right == 0) {
        return 0;
    }
    if (left > 0
        && ((right > 0
                && left > std::numeric_limits<std::int64_t>::max() / right)
            || (right < 0
                && right < std::numeric_limits<std::int64_t>::min() / left))) {
        return std::nullopt;
    }
    if (left < 0
        && ((right > 0
                && left < std::numeric_limits<std::int64_t>::min() / right)
            || (right < 0
                && left < std::numeric_limits<std::int64_t>::max() / right))) {
        return std::nullopt;
    }
    return left * right;
}

std::optional<std::int64_t> evaluate_unary(
    const std::string_view raw_operator, const std::int64_t operand)
{
    const auto operation = normalized_token(raw_operator);
    if (operation.empty() || operation == "+") {
        return operand;
    }
    if (operation == "-") {
        return operand == std::numeric_limits<std::int64_t>::min()
            ? std::nullopt
            : std::optional<std::int64_t> { -operand };
    }
    if (operation == "!" || operation == "not") {
        return operand == 0 ? 1 : 0;
    }
    if (operation == "~") {
        return ~operand;
    }
    if (operation == "abs") {
        return operand == std::numeric_limits<std::int64_t>::min()
            ? std::nullopt
            : std::optional<std::int64_t> {
                  operand < 0 ? -operand : operand };
    }
    if (operation == "++" || operation == "post++") {
        return checked_add(operand, 1);
    }
    if (operation == "--" || operation == "post--") {
        return checked_subtract(operand, 1);
    }
    return std::nullopt;
}

std::optional<std::int64_t> evaluate_binary(
    const std::string_view raw_operator, const std::int64_t left,
    const std::int64_t right)
{
    const auto operation = normalized_token(raw_operator);
    if (operation == "+") {
        return checked_add(left, right);
    }
    if (operation == "-") {
        return checked_subtract(left, right);
    }
    if (operation == "*") {
        return checked_multiply(left, right);
    }
    if (operation == "/" || operation == "div") {
        if (right == 0
            || (left == std::numeric_limits<std::int64_t>::min()
                && right == -1)) {
            return std::nullopt;
        }
        return left / right;
    }
    if (operation == "%" || operation == "rem") {
        if (right == 0
            || (left == std::numeric_limits<std::int64_t>::min()
                && right == -1)) {
            return std::nullopt;
        }
        return left % right;
    }
    if (operation == "mod") {
        if (right == 0
            || (left == std::numeric_limits<std::int64_t>::min()
                && right == -1)) {
            return std::nullopt;
        }
        const auto remainder = left % right;
        return remainder != 0 && ((remainder < 0) != (right < 0))
            ? remainder + right
            : remainder;
    }
    if (operation == "<") {
        return left < right ? 1 : 0;
    }
    if (operation == "<=") {
        return left <= right ? 1 : 0;
    }
    if (operation == ">") {
        return left > right ? 1 : 0;
    }
    if (operation == ">=") {
        return left >= right ? 1 : 0;
    }
    if (operation == "==" || operation == "===" || operation == "=") {
        return left == right ? 1 : 0;
    }
    if (operation == "!=" || operation == "!==" || operation == "/=") {
        return left != right ? 1 : 0;
    }
    if (operation == "&&" || operation == "and") {
        return left != 0 && right != 0 ? 1 : 0;
    }
    if (operation == "||" || operation == "or") {
        return left != 0 || right != 0 ? 1 : 0;
    }
    if (operation == "&") {
        return left & right;
    }
    if (operation == "|") {
        return left | right;
    }
    if (operation == "^" || operation == "xor") {
        return left ^ right;
    }
    if (operation == "xnor") {
        return ~(left ^ right);
    }
    if (operation == "<<" || operation == "sll"
        || operation == "sla") {
        if (right < 0 || right >= 63 || left < 0
            || static_cast<std::uint64_t>(left)
                > (static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())
                    >> static_cast<unsigned>(right))) {
            return std::nullopt;
        }
        return left << static_cast<unsigned>(right);
    }
    if (operation == ">>" || operation == "srl"
        || operation == "sra") {
        if (right < 0 || right >= 63) {
            return std::nullopt;
        }
        return left >> static_cast<unsigned>(right);
    }
    return std::nullopt;
}

class HirIntegralEvaluator {
    struct CallFrame {
        DeclarationId callable;
        std::map<DeclarationId, std::optional<std::int64_t>> values;
        std::map<DeclarationId, std::optional<std::string>> string_values;
        std::map<std::string, std::optional<std::int64_t>> pattern_values;
        std::optional<std::int64_t> result;
    };

    enum class StatementFlow : std::uint8_t {
        normal,
        returned,
        broke,
        continued,
        failed,
    };

    using VhdlPackageSelection = CompiledVhdlPackageMember;

public:
    explicit HirIntegralEvaluator(const SpecializedHirUnit& unit,
        std::vector<SpecializedHirConstantEffect>* effects = nullptr,
        const std::string_view effect_code = "FSIM-ELAB-SVCONST-002",
        const std::string_view effect_context = "constant evaluation")
        : unit_ { unit }
        , effects_ { effects }
        , effect_code_ { effect_code }
        , effect_context_ { effect_context }
    {
        for (const auto& actual :
            unit.specialization().actual_identities) {
            actuals_.emplace(actual.declaration,
                coerce_systemverilog_value(actual.declaration,
                    parse_integral_identity(actual.identity)));
            string_actuals_.emplace(actual.declaration,
                parse_systemverilog_string_identity(actual.identity));
            if (actual.actual_declaration
                && *actual.actual_declaration != actual.declaration) {
                actual_declarations_.emplace(
                    actual.declaration, *actual.actual_declaration);
            }
        }
        for (const auto& identity :
            unit.specialization().hierarchy_identities) {
            hierarchy_identities_.insert_or_assign(identity.name,
                parse_integral_identity(identity.identity));
        }
    }

    [[nodiscard]] std::optional<std::int64_t> evaluate(
        const ExpressionId expression)
    {
        if (call_frames_.empty()
            && (expressions_.contains(expression))) {
            const auto cached = expressions_.find(expression);
            return cached->second;
        }
        if (!active_expressions_.insert(expression).second) {
            return std::nullopt;
        }
        const auto view = unit_.find_expression(expression);
        std::optional<std::int64_t> result;
        if (view && view->systemverilog != nullptr) {
            result = evaluate_expression(*view->systemverilog);
        } else if (view && view->vhdl != nullptr) {
            result = evaluate_expression(*view->vhdl);
        }
        active_expressions_.erase(expression);
        if (call_frames_.empty()) {
            expressions_.insert_or_assign(expression, result);
        }
        return result;
    }

    [[nodiscard]] std::optional<std::string> evaluate_string(
        const ExpressionId expression)
    {
        if (call_frames_.empty()
            && string_expressions_.contains(expression)) {
            const auto cached = string_expressions_.find(expression);
            return cached->second;
        }
        if (!active_string_expressions_.insert(expression).second) {
            return std::nullopt;
        }
        const auto view = unit_.find_expression(expression);
        std::optional<std::string> result;
        if (view && view->systemverilog != nullptr) {
            result = evaluate_string_expression(*view->systemverilog);
        } else if (view && view->vhdl != nullptr) {
            result = evaluate_string_expression(*view->vhdl);
        }
        active_string_expressions_.erase(expression);
        if (call_frames_.empty()) {
            string_expressions_.insert_or_assign(expression, result);
        }
        return result;
    }

    [[nodiscard]] std::optional<bool> evaluate_truth(
        const ExpressionId expression)
    {
        return evaluate_nonzero(expression);
    }

    [[nodiscard]] std::optional<std::string>
    evaluate_systemverilog_bits(const ExpressionId expression)
    {
        return canonical_systemverilog_bits(expression);
    }

    [[nodiscard]] bool execute_program_statements(
        const std::span<const StatementId> statements)
    {
        call_frames_.push_back(CallFrame { });
        for (const auto statement : statements) {
            if (execute_statement(statement) != StatementFlow::normal) {
                call_frames_.pop_back();
                return false;
            }
        }
        call_frames_.pop_back();
        return true;
    }

private:
    [[nodiscard]] std::optional<std::int64_t>
    coerce_systemverilog_value(const DeclarationId declaration,
        const std::optional<std::int64_t> value)
    {
        if (!value) {
            return std::nullopt;
        }
        const auto view = unit_.find_declaration(declaration);
        if (!view || view->systemverilog == nullptr
            || !view->systemverilog->type) {
            return value;
        }
        const auto& type = *view->systemverilog->type;
        auto width = type.executable_width;
        if (!width && type.packed_range) {
            const auto boundary = [&](const std::optional<std::int64_t> folded,
                                      const std::optional<ExpressionId> residual)
                -> std::optional<std::int64_t> {
                if (folded) {
                    return folded;
                }
                return residual ? evaluate(*residual) : std::nullopt;
            };
            const auto left = boundary(type.packed_range->left,
                type.packed_range->left_expression);
            const auto right = boundary(type.packed_range->right,
                type.packed_range->right_expression);
            if (left && right) {
                const auto distance = *left >= *right
                    ? static_cast<std::uint64_t>(*left)
                        - static_cast<std::uint64_t>(*right)
                    : static_cast<std::uint64_t>(*right)
                        - static_cast<std::uint64_t>(*left);
                if (distance < std::numeric_limits<std::uint64_t>::max()) {
                    width = distance + 1U;
                }
            }
        }
        if (!width) {
            return value;
        }
        if (*width == 0U) {
            return value;
        }
        const auto is_signed = type.signed_value;
        if (*width > 64U) {
            return is_signed || *value >= 0
                ? value
                : std::nullopt;
        }
        auto bits = static_cast<std::uint64_t>(*value);
        if (*width < 64U) {
            bits &= (std::uint64_t { 1U } << *width) - 1U;
        }
        return signed_bits(
            bits, static_cast<std::size_t>(*width), is_signed);
    }

    [[nodiscard]] std::optional<DeclarationId>
    resolve_vhdl_expression_declaration(const ExpressionId expression,
        const CompiledDeclarationPredicate& predicate) const
    {
        const auto resolution = CompiledDesignResolver { unit_ }
                                    .resolve_expression_name(
                                        expression, predicate);
        if (const auto unique = resolution.unique()) {
            return unique;
        }
        const auto view = unit_.find_expression(expression);
        if (!view || view->vhdl == nullptr) {
            return std::nullopt;
        }
        if (view->vhdl->referenced_name
            && view->vhdl->referenced_name->selected) {
            const auto selected
                = *view->vhdl->referenced_name->selected;
            const auto declaration = unit_.find_declaration(selected);
            if (declaration && (!predicate || predicate(*declaration))) {
                return selected;
            }
        }
        auto name = view->vhdl->referenced_name
            ? *view->vhdl->referenced_name
            : vhdl::Name { };
        if (name.spelling.empty()) {
            name.spelling = view->vhdl->text;
        }
        if (name.canonical.empty()) {
            name.canonical = name.spelling;
        }
        name.source = view->vhdl->source;
        return CompiledDesignResolver { unit_ }
            .resolve_vhdl(name, view->vhdl->scope, predicate)
            .unique();
    }

    [[nodiscard]] std::optional<DeclarationId> find_actual_declaration(
        const std::string_view name, const ScopeId use_scope,
        const bool systemverilog) const
    {
        const CompiledDesignResolver resolver { unit_ };
        if (systemverilog) {
            const CompiledDeclarationPredicate actual = [](const auto& view) {
                return view.systemverilog != nullptr
                    && systemverilog_actual_form(
                        view.systemverilog->form);
            };
            return resolver.resolve_systemverilog(
                name, use_scope, actual, false)
                .unique();
        }
        vhdl::Name reference;
        reference.spelling = std::string { name };
        reference.canonical = reference.spelling;
        const CompiledDeclarationPredicate actual = [](const auto& view) {
            return view.vhdl != nullptr
                && vhdl_actual_form(view.vhdl->form);
        };
        return resolver.resolve_vhdl(reference, use_scope, actual).unique();
    }

    [[nodiscard]] std::optional<DeclarationId>
    find_systemverilog_package_declaration(
        const std::string_view name, const ScopeId lookup_scope) const
    {
        return CompiledDesignResolver { unit_ }
            .resolve_systemverilog_constant(name, lookup_scope)
            .unique();
    }

    [[nodiscard]] std::optional<bool>
    evaluate_systemverilog_unbounded_declaration(
        const DeclarationId declaration)
    {
        if (!active_unbounded_declarations_.insert(declaration).second) {
            return std::nullopt;
        }
        const auto finish = [&](const std::optional<bool> result) {
            active_unbounded_declarations_.erase(declaration);
            return result;
        };
        const auto& actuals = unit_.specialization().actual_identities;
        const auto actual = std::ranges::find(actuals, declaration,
            &SpecializedHirActualIdentity::declaration);
        if (actual != actuals.end()) {
            if (const auto unbounded
                = systemverilog_unbounded_identity(actual->identity)) {
                return finish(unbounded);
            }
            if (actual->actual_expression) {
                return finish(evaluate_systemverilog_unbounded_expression(
                    *actual->actual_expression));
            }
            if (actual->actual_declaration) {
                return finish(evaluate_systemverilog_unbounded_declaration(
                    *actual->actual_declaration));
            }
            return finish(std::nullopt);
        }
        const auto view = unit_.find_declaration(declaration);
        if (!view || view->systemverilog == nullptr) {
            return finish(std::nullopt);
        }
        const auto form = view->systemverilog->form;
        const auto constant = form == sv::DeclarationForm::parameter
            || form == sv::DeclarationForm::local_parameter
            || form == sv::DeclarationForm::enumeration_literal;
        if (constant && view->systemverilog->initializer) {
            return finish(evaluate_systemverilog_unbounded_expression(
                *view->systemverilog->initializer));
        }
        return finish(evaluate_declaration(declaration)
                ? std::optional<bool> { false }
                : std::nullopt);
    }

    [[nodiscard]] std::optional<bool>
    evaluate_systemverilog_unbounded_expression(
        const ExpressionId expression)
    {
        if (!active_unbounded_expressions_.insert(expression).second) {
            return std::nullopt;
        }
        const auto finish = [&](const std::optional<bool> result) {
            active_unbounded_expressions_.erase(expression);
            return result;
        };
        const auto view = unit_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return finish(std::nullopt);
        }
        const auto& record = *view->systemverilog;
        if (record.kind == sv::ExpressionKind::name) {
            if (normalized_token(record.text) == "$") {
                return finish(true);
            }
            if (record.referenced_name
                && record.referenced_name->selected) {
                return finish(
                    evaluate_systemverilog_unbounded_declaration(
                        *record.referenced_name->selected));
            }
            if (const auto declaration = find_actual_declaration(
                    record.text, record.scope, true)) {
                return finish(
                    evaluate_systemverilog_unbounded_declaration(
                        *declaration));
            }
            if (const auto declaration
                = find_systemverilog_package_declaration(
                    record.text, record.scope)) {
                return finish(
                    evaluate_systemverilog_unbounded_declaration(
                        *declaration));
            }
        }
        if (evaluate(expression) || evaluate_string(expression)
            || canonical_systemverilog_bits(expression)) {
            return finish(false);
        }
        return finish(std::nullopt);
    }

    [[nodiscard]] bool push_vhdl_package_frame(
        const VhdlPackageSelection& selection)
    {
        if (!selection.package_instance) {
            return true;
        }
        call_frames_.push_back(CallFrame {
            *selection.package_instance, { }, { }, { }, std::nullopt });
        auto& frame = call_frames_.back();
        for (const auto& binding : selection.generic_bindings) {
            const auto declaration = unit_.find_declaration(binding.formal);
            if (!declaration || declaration->vhdl == nullptr) {
                call_frames_.pop_back();
                return false;
            }
            if (declaration->vhdl->form
                == vhdl::DeclarationForm::generic_constant) {
                if (!binding.expression) {
                    call_frames_.pop_back();
                    return false;
                }
                frame.values.emplace(
                    binding.formal, evaluate(*binding.expression));
                frame.string_values.emplace(
                    binding.formal, evaluate_string(*binding.expression));
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<std::int64_t>
    evaluate_vhdl_package_member(const VhdlPackageSelection& selection)
    {
        if (!selection.package_instance) {
            return evaluate_declaration(selection.member);
        }
        if (!push_vhdl_package_frame(selection)) {
            return std::nullopt;
        }
        const auto result = evaluate_declaration(selection.member);
        call_frames_.pop_back();
        return result;
    }

    [[nodiscard]] std::optional<std::string>
    evaluate_vhdl_package_string_member(
        const VhdlPackageSelection& selection)
    {
        if (!selection.package_instance) {
            return evaluate_string_declaration(selection.member);
        }
        if (!push_vhdl_package_frame(selection)) {
            return std::nullopt;
        }
        const auto result = evaluate_string_declaration(selection.member);
        call_frames_.pop_back();
        return result;
    }

    [[nodiscard]] std::optional<std::int64_t>
    evaluate_vhdl_enumeration_literal(
        const vhdl::SubtypeIndication& subtype,
        const ExpressionId expression) const
    {
        const auto candidate = unit_.find_expression(expression);
        if (!candidate || candidate->vhdl == nullptr
            || (candidate->vhdl->kind != vhdl::ExpressionKind::name
                && candidate->vhdl->kind
                    != vhdl::ExpressionKind::logic_literal)) {
            return std::nullopt;
        }
        const auto spelling = std::string_view { candidate->vhdl->text };
        const auto matches = [&](const std::string_view literal) {
            return spelling.starts_with('\'')
                ? spelling == literal
                : vhdl_name_equal(spelling, literal);
        };
        std::set<TypeId> visiting;
        const auto resolve = [&](const auto& self,
                                 const TypeId type_id)
            -> std::optional<std::int64_t> {
            if (!type_id.valid() || !visiting.insert(type_id).second) {
                return std::nullopt;
            }
            const auto type = unit_.find_type(type_id);
            if (!type || type->vhdl == nullptr) {
                return std::nullopt;
            }
            const auto literal = std::ranges::find_if(
                type->vhdl->enumeration_literals,
                [&](const vhdl::EnumerationLiteral& value) {
                    return matches(value.spelling);
                });
            if (literal != type->vhdl->enumeration_literals.end()) {
                return static_cast<std::int64_t>(literal->ordinal);
            }
            return type->vhdl->base.type_mark.target.valid()
                ? self(self, type->vhdl->base.type_mark.target)
                : std::nullopt;
        };
        return resolve(resolve, subtype.type_mark.target);
    }

public:
    [[nodiscard]] std::optional<std::int64_t> evaluate_declaration(
        const DeclarationId declaration)
    {
        for (auto frame = call_frames_.rbegin();
            frame != call_frames_.rend(); ++frame) {
            if (const auto value = frame->values.find(declaration);
                value != frame->values.end()) {
                return value->second;
            }
        }
        if (const auto actual = actuals_.find(declaration);
            actual != actuals_.end()) {
            if (actual->second) {
                return actual->second;
            }
            const auto mapped = actual_declarations_.find(declaration);
            return mapped == actual_declarations_.end()
                ? std::nullopt
                : evaluate_declaration(mapped->second);
        }
        if (call_frames_.empty() && declarations_.contains(declaration)) {
            const auto cached = declarations_.find(declaration);
            return cached->second;
        }
        if (!active_declarations_.insert(declaration).second) {
            return std::nullopt;
        }
        const auto view = unit_.find_declaration(declaration);
        std::optional<std::int64_t> result;
        if (view && view->systemverilog != nullptr) {
            const auto form = view->systemverilog->form;
            const auto constant
                = form == sv::DeclarationForm::parameter
                || form == sv::DeclarationForm::local_parameter
                || form == sv::DeclarationForm::enumeration_literal;
            if (constant && view->systemverilog->initializer) {
                result = evaluate(*view->systemverilog->initializer);
            } else if (constant) {
                result = parse_integral_identity(
                    view->systemverilog->name);
            }
            result = coerce_systemverilog_value(declaration, result);
        } else if (view && view->vhdl != nullptr) {
            const auto form = view->vhdl->form;
            const auto constant
                = form == vhdl::DeclarationForm::generic_constant
                || form == vhdl::DeclarationForm::constant
                || form == vhdl::DeclarationForm::enumeration_literal;
            if (form == vhdl::DeclarationForm::enumeration_literal
                && view->vhdl->subtype
                && view->vhdl->subtype->type_mark.target.valid()) {
                const auto type = unit_.find_type(
                    view->vhdl->subtype->type_mark.target);
                if (type && type->vhdl != nullptr) {
                    const auto literal = std::ranges::find(
                        type->vhdl->enumeration_literals, declaration,
                        &vhdl::EnumerationLiteral::declaration);
                    if (literal
                        != type->vhdl->enumeration_literals.end()) {
                        result = static_cast<std::int64_t>(
                            literal->ordinal);
                    }
                }
            }
            if (!result && constant && view->vhdl->initializer) {
                result = evaluate(*view->vhdl->initializer);
                if (!result && view->vhdl->subtype) {
                    result = evaluate_vhdl_enumeration_literal(
                        *view->vhdl->subtype,
                        *view->vhdl->initializer);
                }
            } else if (!result && constant) {
                result = parse_integral_identity(view->vhdl->name);
            }
        }
        active_declarations_.erase(declaration);
        if (call_frames_.empty()) {
            declarations_.insert_or_assign(declaration, result);
        }
        return result;
    }

    [[nodiscard]] std::optional<std::string>
    evaluate_string_declaration(const DeclarationId declaration)
    {
        for (auto frame = call_frames_.rbegin();
            frame != call_frames_.rend(); ++frame) {
            if (const auto value = frame->string_values.find(declaration);
                value != frame->string_values.end()) {
                return value->second;
            }
        }
        if (const auto actual = string_actuals_.find(declaration);
            actual != string_actuals_.end()) {
            if (actual->second) {
                return actual->second;
            }
            const auto mapped = actual_declarations_.find(declaration);
            return mapped == actual_declarations_.end()
                ? std::nullopt
                : evaluate_string_declaration(mapped->second);
        }
        if (call_frames_.empty()
            && string_declarations_.contains(declaration)) {
            const auto cached = string_declarations_.find(declaration);
            return cached->second;
        }
        if (!active_string_declarations_.insert(declaration).second) {
            return std::nullopt;
        }
        const auto view = unit_.find_declaration(declaration);
        std::optional<std::string> result;
        if (view && view->systemverilog != nullptr) {
            const auto form = view->systemverilog->form;
            const auto constant
                = form == sv::DeclarationForm::parameter
                || form == sv::DeclarationForm::local_parameter;
            if (constant && view->systemverilog->initializer) {
                result = evaluate_string(
                    *view->systemverilog->initializer);
            }
        } else if (view && view->vhdl != nullptr) {
            const auto form = view->vhdl->form;
            const auto constant
                = form == vhdl::DeclarationForm::generic_constant
                || form == vhdl::DeclarationForm::constant;
            if (constant && view->vhdl->initializer) {
                result = evaluate_string(*view->vhdl->initializer);
            }
        }
        active_string_declarations_.erase(declaration);
        if (call_frames_.empty()) {
            string_declarations_.insert_or_assign(declaration, result);
        }
        return result;
    }

private:

    [[nodiscard]] std::optional<std::string>
    canonical_systemverilog_bits(const ExpressionId expression)
    {
        const auto view = unit_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& expression_record = *view->systemverilog;
        if (expression_record.kind == sv::ExpressionKind::concatenation
            || expression_record.kind == sv::ExpressionKind::replication) {
            std::size_t first { };
            std::uint64_t repetitions { 1U };
            if (expression_record.kind
                == sv::ExpressionKind::replication) {
                if (expression_record.operands.size() < 2U) {
                    return std::nullopt;
                }
                const auto count = evaluate(
                    expression_record.operands.front());
                if (!count || *count < 0
                    || static_cast<std::uint64_t>(*count)
                        > maximum_systemverilog_constant_work_units) {
                    return std::nullopt;
                }
                repetitions = static_cast<std::uint64_t>(*count);
                first = 1U;
            }
            std::string element;
            for (auto index = first;
                index < expression_record.operands.size(); ++index) {
                const auto operand = canonical_systemverilog_bits(
                    expression_record.operands[index]);
                if (!operand
                    || operand->size()
                        > maximum_systemverilog_constant_work_units
                            - element.size()) {
                    return std::nullopt;
                }
                element += *operand;
            }
            if (element.empty() || repetitions == 0U) {
                return std::string { };
            }
            if (repetitions
                > maximum_systemverilog_constant_work_units
                    / element.size()) {
                return std::nullopt;
            }
            std::string result;
            result.reserve(element.size() * repetitions);
            for (std::uint64_t index { }; index < repetitions; ++index) {
                result += element;
            }
            return result;
        }
        auto identity = std::string_view { expression_record.text };
        if (expression_record.kind == sv::ExpressionKind::name
            && expression_record.referenced_name
            && expression_record.referenced_name->selected) {
            const auto declaration
                = *expression_record.referenced_name->selected;
            const auto& actuals
                = unit_.specialization().actual_identities;
            const auto actual = std::ranges::find(actuals, declaration,
                &SpecializedHirActualIdentity::declaration);
            if (actual == actuals.end()) {
                return std::nullopt;
            }
            identity = actual->identity;
        } else if (expression_record.kind
                != sv::ExpressionKind::integer_literal
            && expression_record.kind
                != sv::ExpressionKind::logic_literal
            && expression_record.kind
                != sv::ExpressionKind::boolean_literal) {
            return std::nullopt;
        }
        if (identity.starts_with("svconst-v3:")) {
            const auto marker = identity.rfind(":v=");
            if (marker == std::string::npos) {
                return std::nullopt;
            }
            auto bits = identity.substr(marker + 3U);
            if (bits.empty()
                || std::ranges::any_of(bits, [](const char bit) {
                       return bit != '0' && bit != '1';
                   })) {
                return std::nullopt;
            }
            return std::string { bits };
        }
        const auto quote = identity.find('\'');
        if (quote == std::string::npos || quote == 0U
            || quote + 2U > identity.size()) {
            return std::nullopt;
        }
        std::size_t width { };
        const auto parsed_width = std::from_chars(
            identity.data(), identity.data() + quote, width);
        if (parsed_width.ec != std::errc { }
            || parsed_width.ptr != identity.data() + quote
            || width == 0U || width > 1'048'576U) {
            return std::nullopt;
        }
        auto cursor = quote + 1U;
        if (cursor < identity.size()
            && (identity[cursor] == 's'
                || identity[cursor] == 'S')) {
            ++cursor;
        }
        if (cursor >= identity.size()) {
            return std::nullopt;
        }
        const auto base = static_cast<char>(std::tolower(
            static_cast<unsigned char>(identity[cursor++])));
        std::string bits;
        const auto append_digit = [&](const char digit,
                                      const unsigned digit_width) {
            unsigned value { };
            if (digit >= '0' && digit <= '9') {
                value = static_cast<unsigned>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                value = static_cast<unsigned>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                value = static_cast<unsigned>(digit - 'A' + 10);
            } else {
                return false;
            }
            if (value >= (1U << digit_width)) {
                return false;
            }
            for (auto bit = digit_width; bit > 0U; --bit) {
                bits.push_back(
                    (value & (1U << (bit - 1U))) != 0U ? '1' : '0');
            }
            return true;
        };
        for (; cursor < identity.size(); ++cursor) {
            const auto digit = identity[cursor];
            if (digit == '_') {
                continue;
            }
            if (base == 'b') {
                if (!append_digit(digit, 1U)) {
                    return std::nullopt;
                }
            } else if (base == 'o') {
                if (!append_digit(digit, 3U)) {
                    return std::nullopt;
                }
            } else if (base == 'h') {
                if (!append_digit(digit, 4U)) {
                    return std::nullopt;
                }
            } else if (base == 'd' && digit == '0') {
                bits.push_back('0');
            } else {
                return std::nullopt;
            }
        }
        if (bits.size() < width) {
            bits.insert(bits.begin(), width - bits.size(), '0');
        } else if (bits.size() > width) {
            bits.erase(0U, bits.size() - width);
        }
        return bits;
    }

    [[nodiscard]] std::optional<bool> evaluate_nonzero(
        const ExpressionId expression)
    {
        if (const auto narrowed = evaluate(expression)) {
            return *narrowed != 0;
        }
        if (const auto bits = canonical_systemverilog_bits(expression)) {
            return bits->find('1') != std::string_view::npos;
        }
        const auto view = unit_.find_expression(expression);
        if (!view || view->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *view->systemverilog;
        if (record.kind == sv::ExpressionKind::binary
            && record.text == ">>" && record.operands.size() == 2U) {
            const auto bits = canonical_systemverilog_bits(
                record.operands.front());
            const auto shift = evaluate(record.operands.back());
            if (!bits || !shift || *shift < 0) {
                return std::nullopt;
            }
            const auto count = static_cast<std::uint64_t>(*shift);
            if (count >= bits->size()) {
                return false;
            }
            return bits->substr(0U, bits->size() - count).find('1')
                != std::string_view::npos;
        }
        if (record.kind == sv::ExpressionKind::unary
            && record.text == "!" && record.operands.size() == 1U) {
            const auto operand = evaluate_nonzero(record.operands.front());
            return operand ? std::optional<bool> { !*operand }
                           : std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DeclarationId> expression_declaration(
        const ExpressionId expression) const
    {
        const auto view = unit_.find_expression(expression);
        if (!view || view->systemverilog == nullptr
            || !view->systemverilog->referenced_name) {
            return std::nullopt;
        }
        if (view->systemverilog->referenced_name->selected) {
            return view->systemverilog->referenced_name->selected;
        }
        if (call_frames_.empty()) {
            return std::nullopt;
        }
        const auto callable = unit_.find_declaration(
            call_frames_.back().callable);
        if (!callable || callable->systemverilog == nullptr) {
            return std::nullopt;
        }
        if (view->systemverilog->text
            == callable->systemverilog->name) {
            return call_frames_.back().callable;
        }
        const auto lexical_declaration = [&](const DeclarationId id) {
            const auto candidate = unit_.find_declaration(id);
            return candidate && candidate->systemverilog != nullptr
                && candidate->systemverilog->name
                    == view->systemverilog->text;
        };
        if (callable->systemverilog->callable) {
            for (const auto formal :
                callable->systemverilog->callable->formals) {
                if (lexical_declaration(formal)) {
                    return formal;
                }
            }
        }
        const auto child = std::ranges::find_if(
            callable->systemverilog->children, lexical_declaration);
        return child != callable->systemverilog->children.end()
            ? std::optional<DeclarationId> { *child }
            : std::nullopt;
    }

    [[nodiscard]] std::optional<DeclarationId> callable_declaration(
        const sv::Expression& expression) const
    {
        if (expression.referenced_name
            && expression.referenced_name->selected) {
            const auto selected
                = *expression.referenced_name->selected;
            const auto declaration = unit_.find_declaration(selected);
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->callable
                && declaration->systemverilog->callable->function) {
                return selected;
            }
        }
        const CompiledDeclarationPredicate function
            = [](const CompiledDeclarationView& candidate) {
                  return candidate.systemverilog != nullptr
                      && candidate.systemverilog->form
                          == sv::DeclarationForm::function
                      && candidate.systemverilog->callable
                      && candidate.systemverilog->callable->function;
              };
        return CompiledDesignResolver { unit_ }
            .resolve_systemverilog(
                expression.text, expression.scope, function, false)
            .unique();
    }

    [[nodiscard]] std::size_t expression_bit_width(
        const ExpressionId expression) const
    {
        const auto declaration = expression_declaration(expression);
        if (!declaration) {
            return 64U;
        }
        const auto view = unit_.find_declaration(*declaration);
        if (!view || view->systemverilog == nullptr
            || !view->systemverilog->type) {
            return 64U;
        }
        return static_cast<std::size_t>(
            view->systemverilog->type->executable_width.value_or(64U));
    }

    [[nodiscard]] static std::string formatted_integer(
        const std::int64_t value, const char format,
        const std::size_t minimum_width,
        const std::size_t expression_width,
        const bool zero_pad)
    {
        std::ostringstream stream;
        auto width = minimum_width;
        if (format == 'h' || format == 'x') {
            width = std::max(width, (expression_width + 3U) / 4U);
            stream << std::hex;
        } else if (format == 'o') {
            width = std::max(width, (expression_width + 2U) / 3U);
            stream << std::oct;
        }
        if (zero_pad || format == 'h' || format == 'x'
            || format == 'o') {
            stream << std::setfill('0');
        }
        stream << std::setw(static_cast<int>(width));
        if (format == 'c') {
            stream << static_cast<char>(value);
        } else if (format == 'h' || format == 'x'
            || format == 'o') {
            stream << static_cast<std::uint64_t>(value);
        } else {
            stream << value;
        }
        return stream.str();
    }

    [[nodiscard]] std::optional<std::string> format_effect(
        const sv::Statement& statement)
    {
        std::vector<ExpressionId> values;
        if (statement.value) {
            values.push_back(*statement.value);
        }
        for (const auto& argument : statement.task_arguments) {
            if (argument.actual) {
                values.push_back(*argument.actual);
            }
        }
        std::string output;
        std::size_t value_index { };
        const auto format = std::string_view { statement.output_text };
        for (std::size_t index { }; index < format.size(); ++index) {
            if (format[index] != '%') {
                output.push_back(format[index]);
                continue;
            }
            if (++index >= format.size()) {
                return std::nullopt;
            }
            if (format[index] == '%') {
                output.push_back('%');
                continue;
            }
            bool zero_pad { };
            std::size_t minimum_width { };
            if (format[index] == '0') {
                zero_pad = true;
                ++index;
            }
            while (index < format.size()
                && std::isdigit(static_cast<unsigned char>(format[index]))) {
                minimum_width = minimum_width * 10U
                    + static_cast<std::size_t>(format[index] - '0');
                ++index;
            }
            if (index >= format.size()) {
                return std::nullopt;
            }
            const auto conversion = static_cast<char>(std::tolower(
                static_cast<unsigned char>(format[index])));
            if (conversion == 'm') {
                output += call_frames_.empty()
                    ? std::string { "<elaboration>" }
                    : unit_.find_declaration(call_frames_.back().callable)
                              ->systemverilog->name;
                continue;
            }
            if (conversion == 't') {
                output += "0";
                continue;
            }
            if (value_index >= values.size()) {
                return std::nullopt;
            }
            const auto value_expression = values[value_index++];
            if (conversion == 's') {
                const auto value = evaluate_string(value_expression);
                if (!value) {
                    return std::nullopt;
                }
                output += *value;
                continue;
            }
            if (conversion != 'd' && conversion != 'h'
                && conversion != 'x' && conversion != 'o'
                && conversion != 'c' && conversion != 'b') {
                return std::nullopt;
            }
            const auto value = evaluate(value_expression);
            if (!value) {
                return std::nullopt;
            }
            if (conversion == 'b') {
                auto width = std::max(
                    minimum_width, expression_bit_width(value_expression));
                std::string bits(width, '0');
                auto raw = static_cast<std::uint64_t>(*value);
                for (std::size_t bit { }; bit < width && bit < 64U; ++bit) {
                    bits[width - bit - 1U]
                        = (raw & (std::uint64_t { 1U } << bit)) != 0U
                        ? '1'
                        : '0';
                }
                output += bits;
            } else {
                output += formatted_integer(*value, conversion,
                    minimum_width, expression_bit_width(value_expression),
                    zero_pad);
            }
        }
        for (; value_index < values.size(); ++value_index) {
            const auto value = evaluate(values[value_index]);
            if (!value) {
                return std::nullopt;
            }
            output += std::to_string(*value);
        }
        return output;
    }

    [[nodiscard]] StatementFlow execute_statement(
        const StatementId statement_id)
    {
        const auto view = unit_.find_statement(statement_id);
        if (!view || view->systemverilog == nullptr
            || call_frames_.empty()) {
            return StatementFlow::failed;
        }
        const auto& statement = *view->systemverilog;
        const auto execute = [&](const std::span<const StatementId> body) {
            for (const auto child : body) {
                const auto flow = execute_statement(child);
                if (flow != StatementFlow::normal) {
                    return flow;
                }
            }
            return StatementFlow::normal;
        };
        const auto assign = [&](const ExpressionId target,
                                const ExpressionId value) {
            const auto declaration = expression_declaration(target);
            if (!declaration) {
                return false;
            }
            auto integral = evaluate(value);
            auto string = evaluate_string(value);
            if (!integral && !string) {
                return false;
            }
            auto& frame = call_frames_.back();
            if (*declaration == frame.callable) {
                frame.result = integral;
            } else {
                frame.values.insert_or_assign(
                    *declaration, std::move(integral));
                frame.string_values.insert_or_assign(
                    *declaration, std::move(string));
            }
            return true;
        };
        const auto match_pattern = [&](const auto& self,
                                       const ExpressionId pattern_id,
                                       const std::int64_t selector,
                                       const bool pattern_matching,
                                       const bool inside_matching)
            -> std::optional<bool> {
            const auto pattern = unit_.find_expression(pattern_id);
            if (!pattern || pattern->systemverilog == nullptr) {
                return std::nullopt;
            }
            const auto& source = *pattern->systemverilog;
            if (pattern_matching
                && source.kind == sv::ExpressionKind::call) {
                if (source.text == "@match-wildcard") {
                    return true;
                }
                if (source.text == "@match-guard") {
                    if (source.operands.size() != 2U) {
                        return std::nullopt;
                    }
                    const auto bindings
                        = call_frames_.back().pattern_values;
                    const auto raw = self(
                        self, source.operands[0], selector, true, false);
                    if (!raw || !*raw) {
                        call_frames_.back().pattern_values = bindings;
                        return raw;
                    }
                    const auto guard = evaluate_nonzero(
                        source.operands[1]);
                    if (!guard || !*guard) {
                        call_frames_.back().pattern_values = bindings;
                    }
                    return guard;
                }
                constexpr auto bind_prefix
                    = std::string_view { "@match-bind:" };
                if (source.text.starts_with(bind_prefix)) {
                    const auto name = source.text.substr(
                        bind_prefix.size());
                    if (name.empty()) {
                        return std::nullopt;
                    }
                    call_frames_.back().pattern_values.insert_or_assign(
                        name, selector);
                    return true;
                }
                return std::nullopt;
            }
            if (inside_matching
                && source.kind == sv::ExpressionKind::call
                && source.text == "@inside-range") {
                if (source.operands.size() != 2U) {
                    return std::nullopt;
                }
                const auto low = evaluate(source.operands[0]);
                const auto high = evaluate(source.operands[1]);
                if (!low || !high) {
                    return std::nullopt;
                }
                return *low <= *high
                    && selector >= *low && selector <= *high;
            }
            const auto value = evaluate(pattern_id);
            return value ? std::optional { *value == selector }
                         : std::nullopt;
        };
        switch (statement.kind) {
        case sv::StatementKind::block:
            return execute(statement.statements);
        case sv::StatementKind::conditional: {
            if (!statement.condition) {
                return StatementFlow::failed;
            }
            const auto condition = evaluate_nonzero(*statement.condition);
            if (!condition) {
                return StatementFlow::failed;
            }
            return execute(*condition
                    ? std::span<const StatementId> { statement.statements }
                    : std::span<const StatementId> {
                        statement.else_statements });
        }
        case sv::StatementKind::selection: {
            if (!statement.condition) {
                return StatementFlow::failed;
            }
            const auto selector = evaluate(*statement.condition);
            if (!selector) {
                return StatementFlow::failed;
            }
            const auto pattern_matching
                = statement.case_match == sv::CaseMatchKind::matches;
            const auto inside_matching
                = statement.case_match == sv::CaseMatchKind::inside;
            const sv::CaseAlternative* default_alternative = nullptr;
            for (const auto& alternative :
                statement.case_alternatives) {
                if (alternative.is_default) {
                    default_alternative = &alternative;
                    continue;
                }
                for (const auto choice : alternative.choices) {
                    const auto bindings
                        = call_frames_.back().pattern_values;
                    const auto matched = match_pattern(
                        match_pattern, choice, *selector,
                        pattern_matching, inside_matching);
                    if (!matched) {
                        call_frames_.back().pattern_values = bindings;
                        return StatementFlow::failed;
                    }
                    if (!*matched) {
                        call_frames_.back().pattern_values = bindings;
                        continue;
                    }
                    const auto flow = execute(alternative.statements);
                    call_frames_.back().pattern_values = bindings;
                    return flow;
                }
            }
            return default_alternative != nullptr
                ? execute(default_alternative->statements)
                : StatementFlow::normal;
        }
        case sv::StatementKind::assignment: {
            if (!statement.target || !statement.value) {
                return StatementFlow::failed;
            }
            return assign(*statement.target, *statement.value)
                ? StatementFlow::normal : StatementFlow::failed;
        }
        case sv::StatementKind::loop: {
            if (statement.loop_runtime) {
                return StatementFlow::failed;
            }
            if (statement.loop_repeat) {
                if (!statement.loop_limit) {
                    return StatementFlow::failed;
                }
                const auto count = evaluate(*statement.loop_limit);
                if (!count || *count < 0
                    || static_cast<std::uint64_t>(*count)
                        > maximum_systemverilog_constant_work_units) {
                    return StatementFlow::failed;
                }
                for (std::int64_t iteration { };
                    iteration < *count; ++iteration) {
                    const auto flow = execute(statement.statements);
                    if (flow == StatementFlow::returned
                        || flow == StatementFlow::failed) {
                        return flow;
                    }
                    if (flow == StatementFlow::broke) {
                        return StatementFlow::normal;
                    }
                }
                return StatementFlow::normal;
            }
            if (statement.loop_initial) {
                const auto target = statement.target
                    ? statement.target : statement.loop_update_target;
                if (!target
                    || !assign(*target, *statement.loop_initial)) {
                    return StatementFlow::failed;
                }
            }
            for (std::uint64_t iteration { };
                iteration < maximum_systemverilog_constant_work_units;
                ++iteration) {
                if (!statement.loop_post_test && statement.condition) {
                    const auto condition = evaluate_nonzero(
                        *statement.condition);
                    if (!condition) {
                        return StatementFlow::failed;
                    }
                    if (!*condition) {
                        return StatementFlow::normal;
                    }
                }
                const auto body = execute(statement.statements);
                if (body == StatementFlow::returned
                    || body == StatementFlow::failed) {
                    return body;
                }
                if (body == StatementFlow::broke) {
                    return StatementFlow::normal;
                }
                if (statement.value) {
                    const auto target = statement.loop_update_target
                        ? statement.loop_update_target : statement.target;
                    if (!target || !assign(*target, *statement.value)) {
                        return StatementFlow::failed;
                    }
                }
                const auto update = execute(statement.loop_updates);
                if (update == StatementFlow::returned
                    || update == StatementFlow::failed) {
                    return update;
                }
                if (update == StatementFlow::broke) {
                    return StatementFlow::normal;
                }
                if (statement.loop_post_test && statement.condition) {
                    const auto condition = evaluate_nonzero(
                        *statement.condition);
                    if (!condition) {
                        return StatementFlow::failed;
                    }
                    if (!*condition) {
                        return StatementFlow::normal;
                    }
                }
            }
            return StatementFlow::failed;
        }
        case sv::StatementKind::break_loop:
            return StatementFlow::broke;
        case sv::StatementKind::continue_loop:
            return StatementFlow::continued;
        case sv::StatementKind::return_statement:
            if (statement.value) {
                call_frames_.back().result = evaluate(*statement.value);
            }
            return call_frames_.back().result
                ? StatementFlow::returned
                : StatementFlow::failed;
        case sv::StatementKind::display:
        case sv::StatementKind::report: {
            if (effects_ != nullptr && effects_->size() >= 4'096U) {
                return StatementFlow::failed;
            }
            const auto text = format_effect(statement);
            if (!text) {
                return StatementFlow::failed;
            }
            if (effects_ == nullptr) {
                return StatementFlow::normal;
            }
            const auto task = statement.assertion_severity
                    == sv::AssertionSeverity::note
                ? "$info"
                : statement.assertion_severity
                        == sv::AssertionSeverity::warning
                ? "$warning"
                : statement.assertion_severity
                        == sv::AssertionSeverity::failure
                ? "$fatal"
                : "$error";
            SpecializedHirConstantEffect effect;
            effect.severity = static_cast<
                SpecializedHirConstantEffectSeverity>(
                statement.assertion_severity);
            effect.code = effect_code_;
            effect.message = std::string { task }
                + " during " + effect_context_;
            if (!text->empty()) {
                effect.message += ": " + *text;
            }
            effect.source = statement.source;
            effects_->push_back(std::move(effect));
            return StatementFlow::normal;
        }
        case sv::StatementKind::null_statement:
            return StatementFlow::normal;
        default:
            return StatementFlow::failed;
        }
    }

    [[nodiscard]] std::optional<std::int64_t> evaluate_callable(
        const sv::Expression& expression)
    {
        const auto callable_id = callable_declaration(expression);
        if (!callable_id || call_frames_.size() >= 128U) {
            return std::nullopt;
        }
        const auto callable = unit_.find_declaration(*callable_id);
        if (!callable || callable->systemverilog == nullptr
            || !callable->systemverilog->callable) {
            return std::nullopt;
        }
        const auto& declaration = *callable->systemverilog;
        const auto& formals = declaration.callable->formals;
        if (expression.call_arguments.size() > formals.size()
            || (expression.call_arguments.empty()
                && expression.operands.size() > formals.size())) {
            return std::nullopt;
        }
        CallFrame frame;
        frame.callable = *callable_id;
        const auto actual_count = expression.call_arguments.empty()
            ? expression.operands.size()
            : expression.call_arguments.size();
        for (std::size_t index { }; index < actual_count; ++index) {
            const auto actual = expression.call_arguments.empty()
                ? std::optional<ExpressionId> { expression.operands[index] }
                : expression.call_arguments[index].actual;
            if (!actual) {
                return std::nullopt;
            }
            frame.values.emplace(formals[index], evaluate(*actual));
            frame.string_values.emplace(
                formals[index], evaluate_string(*actual));
        }
        for (std::size_t index = actual_count;
            index < formals.size(); ++index) {
            const auto formal = unit_.find_declaration(formals[index]);
            if (!formal || formal->systemverilog == nullptr
                || !formal->systemverilog->initializer) {
                return std::nullopt;
            }
            frame.values.emplace(formals[index], evaluate(
                *formal->systemverilog->initializer));
            frame.string_values.emplace(formals[index], evaluate_string(
                *formal->systemverilog->initializer));
        }
        call_frames_.push_back(std::move(frame));
        for (const auto child : declaration.children) {
            const auto local = unit_.find_declaration(child);
            if (!local || local->systemverilog == nullptr
                || local->systemverilog->form
                    != sv::DeclarationForm::variable) {
                continue;
            }
            const auto initializer = local->systemverilog->initializer;
            call_frames_.back().values.emplace(child,
                initializer ? evaluate(*initializer)
                            : std::optional<std::int64_t> { 0 });
            call_frames_.back().string_values.emplace(child,
                initializer ? evaluate_string(*initializer)
                            : std::nullopt);
        }
        const auto flow = [&]() {
            for (const auto statement : declaration.statements) {
                const auto result = execute_statement(statement);
                if (result != StatementFlow::normal) {
                    return result;
                }
            }
            return StatementFlow::normal;
        }();
        const auto result = flow == StatementFlow::returned
                || (flow == StatementFlow::normal
                    && call_frames_.back().result)
            ? call_frames_.back().result
            : std::nullopt;
        call_frames_.pop_back();
        return result;
    }

    [[nodiscard]] std::optional<DeclarationId> callable_declaration(
        const vhdl::Expression& expression) const
    {
        if (!expression.referenced_name) {
            return std::nullopt;
        }
        const auto resolved = CompiledDesignResolver { unit_ }
                                  .resolve_vhdl_callables(
                                      *expression.referenced_name,
                                      expression.scope)
                                  .unique();
        if (!resolved) {
            return std::nullopt;
        }
        const auto declaration = unit_.find_declaration(resolved->body);
        return declaration && declaration->vhdl != nullptr
                && declaration->vhdl->callable
                && declaration->vhdl->callable->function
            ? std::optional<DeclarationId> { resolved->body }
            : std::nullopt;
    }

    [[nodiscard]] StatementFlow execute_vhdl_statement(
        const StatementId statement_id)
    {
        const auto view = unit_.find_statement(statement_id);
        if (!view || view->vhdl == nullptr || call_frames_.empty()) {
            return StatementFlow::failed;
        }
        const auto& statement = *view->vhdl;
        const auto execute = [&](const std::span<const StatementId> body) {
            for (const auto child : body) {
                const auto flow = execute_vhdl_statement(child);
                if (flow != StatementFlow::normal) {
                    return flow;
                }
            }
            return StatementFlow::normal;
        };
        switch (statement.kind) {
        case vhdl::StatementKind::block:
            return execute(statement.statements);
        case vhdl::StatementKind::conditional: {
            if (!statement.condition) {
                return StatementFlow::failed;
            }
            const auto condition = evaluate(*statement.condition);
            if (!condition) {
                return StatementFlow::failed;
            }
            return execute(*condition != 0
                    ? std::span<const StatementId> { statement.statements }
                    : std::span<const StatementId> {
                        statement.else_statements });
        }
        case vhdl::StatementKind::variable_assignment: {
            if (!statement.target || !statement.value) {
                return StatementFlow::failed;
            }
            const auto target = unit_.find_expression(*statement.target);
            if (!target || target->vhdl == nullptr
                || !target->vhdl->referenced_name
                || !target->vhdl->referenced_name->selected) {
                return StatementFlow::failed;
            }
            call_frames_.back().values.insert_or_assign(
                *target->vhdl->referenced_name->selected,
                evaluate(*statement.value));
            return StatementFlow::normal;
        }
        case vhdl::StatementKind::return_statement:
            if (!statement.value) {
                return StatementFlow::failed;
            }
            call_frames_.back().result = evaluate(*statement.value);
            return call_frames_.back().result
                ? StatementFlow::returned
                : StatementFlow::failed;
        case vhdl::StatementKind::null_statement:
            return StatementFlow::normal;
        default:
            return StatementFlow::failed;
        }
    }

    [[nodiscard]] std::optional<std::int64_t> evaluate_callable(
        const vhdl::Expression& expression)
    {
        const auto callable_id = callable_declaration(expression);
        if (!callable_id || call_frames_.size() >= 128U) {
            return std::nullopt;
        }
        const auto callable = unit_.find_declaration(*callable_id);
        const auto& declaration = *callable->vhdl;
        const auto& formals = declaration.callable->formals;
        if (expression.operands.size() != formals.size()) {
            return std::nullopt;
        }
        CallFrame frame;
        frame.callable = *callable_id;
        for (std::size_t index { }; index < formals.size(); ++index) {
            frame.values.emplace(
                formals[index], evaluate(expression.operands[index]));
            frame.string_values.emplace(
                formals[index], evaluate_string(expression.operands[index]));
        }
        call_frames_.push_back(std::move(frame));
        for (const auto child : declaration.children) {
            const auto local = unit_.find_declaration(child);
            if (!local || local->vhdl == nullptr
                || local->vhdl->form != vhdl::DeclarationForm::variable) {
                continue;
            }
            call_frames_.back().values.emplace(child,
                local->vhdl->initializer
                    ? evaluate(*local->vhdl->initializer)
                    : std::optional<std::int64_t> { 0 });
        }
        StatementFlow flow = StatementFlow::normal;
        for (const auto statement : declaration.statements) {
            flow = execute_vhdl_statement(statement);
            if (flow != StatementFlow::normal) {
                break;
            }
        }
        const auto result = flow == StatementFlow::returned
            ? call_frames_.back().result
            : std::nullopt;
        call_frames_.pop_back();
        return result;
    }

    [[nodiscard]] std::optional<std::int64_t> evaluate_vhdl_attribute(
        const vhdl::Expression& expression)
    {
        if (expression.kind != vhdl::ExpressionKind::call
            || expression.operands.empty()) {
            return std::nullopt;
        }
        const bool supported = expression.text == "'left"
            || expression.text == "'right"
            || expression.text == "'low"
            || expression.text == "'high"
            || expression.text == "'length"
            || expression.text == "'ascending"
            || expression.text == "'pos"
            || expression.text == "'val";
        if (!supported) {
            return std::nullopt;
        }

        const auto prefix = unit_.find_expression(
            expression.operands.front());
        if (!prefix || prefix->vhdl == nullptr) {
            return std::nullopt;
        }
        const CompiledDeclarationPredicate attribute_prefix
            = [](const CompiledDeclarationView& candidate) {
                  return candidate.vhdl != nullptr
                      && vhdl_attribute_prefix_form(
                          candidate.vhdl->form);
              };
        const auto declaration_id = resolve_vhdl_expression_declaration(
            expression.operands.front(), attribute_prefix);
        const auto declaration = declaration_id
            ? unit_.find_declaration(*declaration_id)
            : std::nullopt;
        std::optional<vhdl::SubtypeIndication> effective_subtype;
        if (declaration && declaration->vhdl != nullptr) {
            if (declaration->vhdl->subtype) {
                effective_subtype = declaration->vhdl->subtype;
            } else if (declaration->vhdl->declared_type) {
                effective_subtype.emplace();
                effective_subtype->type_mark.target
                    = *declaration->vhdl->declared_type;
                effective_subtype->type_mark.spelling
                    = declaration->vhdl->name;
            }
        }
        if (effective_subtype) {
            effective_subtype = CompiledDesignResolver { unit_ }
                .effective_vhdl_subtype(
                    *effective_subtype, expression.scope);
        }
        const auto* subtype = effective_subtype
            ? &*effective_subtype
            : nullptr;
        auto type_id = subtype != nullptr
                && subtype->type_mark.target.valid()
            ? std::optional { subtype->type_mark.target }
            : declaration && declaration->vhdl != nullptr
            ? declaration->vhdl->declared_type
            : std::nullopt;
        const vhdl::TypeDefinition* definition { };
        std::set<TypeId> visited_types;
        while (type_id && visited_types.insert(*type_id).second) {
            const auto type = unit_.find_type(*type_id);
            if (!type || type->vhdl == nullptr) {
                break;
            }
            definition = type->vhdl;
            if ((definition->form != vhdl::TypeForm::subtype
                    && definition->form != vhdl::TypeForm::alias
                    && definition->form != vhdl::TypeForm::scalar)
                || !definition->base.type_mark.target.valid()) {
                break;
            }
            type_id = definition->base.type_mark.target;
        }

        const auto concrete_range = [&](const vhdl::RangeConstraint& range)
            -> std::optional<vhdl::RangeConstraint> {
            auto result = range;
            if (!result.left && result.left_expression) {
                result.left = evaluate(*result.left_expression);
            }
            if (!result.right && result.right_expression) {
                result.right = evaluate(*result.right_expression);
            }
            return result.left && result.right
                ? std::optional { std::move(result) }
                : std::nullopt;
        };

        std::optional<vhdl::RangeConstraint> range;
        const auto predefined_array = subtype != nullptr
            && (vhdl_name_equal(
                    subtype->type_mark.spelling, "bit_vector")
                || vhdl_name_equal(
                    subtype->type_mark.spelling, "std_logic_vector")
                || vhdl_name_equal(
                    subtype->type_mark.spelling, "std_ulogic_vector")
                || vhdl_name_equal(
                    subtype->type_mark.spelling, "signed")
                || vhdl_name_equal(
                    subtype->type_mark.spelling, "unsigned"));
        const auto constrained_array = subtype != nullptr
            && std::ranges::any_of(
                subtype->constraints,
                [](const vhdl::RangeConstraint& candidate) {
                    return candidate.kind == vhdl::RangeKind::array_index;
                });
        const auto array_subtype = subtype != nullptr
            && ((definition != nullptr
                    && definition->form == vhdl::TypeForm::array)
                || predefined_array || constrained_array);
        if (array_subtype) {
            std::int64_t dimension { 1 };
            if (expression.operands.size() == 2U) {
                const auto selected = evaluate(expression.operands[1]);
                if (!selected) {
                    return std::nullopt;
                }
                dimension = *selected;
            } else if (expression.operands.size() != 1U) {
                return std::nullopt;
            }
            if (dimension <= 0
                || static_cast<std::uint64_t>(dimension)
                    > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            const auto index = static_cast<std::size_t>(dimension - 1);
            if (index < subtype->constraints.size()) {
                range = concrete_range(subtype->constraints[index]);
            }
            if (!range && definition != nullptr
                && definition->form == vhdl::TypeForm::array
                && index < definition->array_dimensions.size()
                && definition->array_dimensions[index].constraint) {
                range = concrete_range(
                    *definition->array_dimensions[index].constraint);
            }
        } else if (subtype != nullptr) {
            const auto constraint = std::ranges::find_if(
                subtype->constraints,
                [](const vhdl::RangeConstraint& candidate) {
                    return candidate.kind == vhdl::RangeKind::integer
                        || candidate.kind == vhdl::RangeKind::enumeration
                        || candidate.kind == vhdl::RangeKind::discrete;
                });
            if (constraint != subtype->constraints.end()) {
                range = concrete_range(*constraint);
            }
            if (!range && definition != nullptr
                && definition->scalar_range) {
                range = concrete_range(*definition->scalar_range);
            }
            if (!range && definition != nullptr
                && !definition->enumeration_literals.empty()
                && definition->enumeration_literals.size() - 1U
                    <= static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                range = vhdl::RangeConstraint {
                    vhdl::RangeKind::enumeration,
                    0,
                    static_cast<std::int64_t>(
                        definition->enumeration_literals.size() - 1U),
                    std::nullopt,
                    std::nullopt,
                    false,
                    false,
                    { },
                };
            }
        }
        if (!range) {
            auto predefined_name = declaration
                    && declaration->vhdl != nullptr
                ? std::string_view { declaration->vhdl->name }
                : prefix->vhdl->referenced_name
                ? std::string_view {
                      prefix->vhdl->referenced_name->canonical.empty()
                          ? prefix->vhdl->referenced_name->spelling
                          : prefix->vhdl->referenced_name->canonical }
                : std::string_view { prefix->vhdl->text };
            if (const auto separator = predefined_name.find_last_of(".:");
                separator != std::string_view::npos) {
                predefined_name.remove_prefix(separator + 1U);
            }
            const bool integer = vhdl_name_equal(
                predefined_name, "integer");
            const bool natural = vhdl_name_equal(
                predefined_name, "natural");
            const bool positive = vhdl_name_equal(
                predefined_name, "positive");
            if (integer || natural || positive) {
                const auto selected_unit
                    = unit_.design().find_unit(unit_.unit());
                const bool vhdl_2019 = selected_unit
                    && selected_unit->vhdl != nullptr
                    && selected_unit->vhdl->standard == "2019";
                const auto first = integer
                    ? vhdl_2019
                        ? std::numeric_limits<std::int64_t>::min()
                        : static_cast<std::int64_t>(
                              std::numeric_limits<std::int32_t>::min())
                    : natural ? 0 : 1;
                const auto last = vhdl_2019
                    ? std::numeric_limits<std::int64_t>::max()
                    : static_cast<std::int64_t>(
                          std::numeric_limits<std::int32_t>::max());
                range = vhdl::RangeConstraint {
                    vhdl::RangeKind::integer,
                    first,
                    last,
                    std::nullopt,
                    std::nullopt,
                    false,
                    false,
                    prefix->vhdl->source,
                };
            }
        }
        if (!range || !range->left || !range->right) {
            return std::nullopt;
        }
        const auto left = *range->left;
        const auto right = *range->right;
        if (expression.text == "'pos" || expression.text == "'val") {
            if (expression.operands.size() != 2U) {
                return std::nullopt;
            }
            const auto value = evaluate(expression.operands[1]);
            if (!value || *value < std::min(left, right)
                || *value > std::max(left, right)) {
                return std::nullopt;
            }
            return value;
        }
        if (expression.text == "'left") {
            return left;
        }
        if (expression.text == "'right") {
            return right;
        }
        if (expression.text == "'low") {
            return std::min(left, right);
        }
        if (expression.text == "'high") {
            return std::max(left, right);
        }
        if (expression.text == "'ascending") {
            return range->descending ? 0 : 1;
        }
        const auto null_range = range->null
            || (!range->descending && left > right)
            || (range->descending && left < right);
        if (null_range) {
            return 0;
        }
        const auto distance = left >= right
            ? static_cast<std::uint64_t>(left)
                - static_cast<std::uint64_t>(right)
            : static_cast<std::uint64_t>(right)
                - static_cast<std::uint64_t>(left);
        return distance
                < static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())
            ? std::optional {
                  static_cast<std::int64_t>(distance + 1U) }
            : std::nullopt;
    }

    [[nodiscard]] std::optional<std::int64_t>
    evaluate_vhdl_physical(const vhdl::Expression& expression)
    {
        constexpr auto prefix = std::string_view { "@vhdl-physical:" };
        if (expression.kind != vhdl::ExpressionKind::call
            || !expression.text.starts_with(prefix)
            || expression.operands.size() != 1U) {
            return std::nullopt;
        }
        const auto unit_name = std::string_view { expression.text }.substr(
            prefix.size());
        const auto magnitude = evaluate(expression.operands.front());
        if (!magnitude) {
            return std::nullopt;
        }
        for (const auto& stored : unit_.vhdl_types()) {
            const auto type = unit_.find_type(stored.id);
            if (!type || type->vhdl == nullptr
                || type->vhdl->form != vhdl::TypeForm::physical) {
                continue;
            }
            const auto selected = std::ranges::find_if(
                type->vhdl->physical_units,
                [&](const vhdl::PhysicalUnit& candidate) {
                    return vhdl_name_equal(candidate.name, unit_name);
                });
            if (selected == type->vhdl->physical_units.end()) {
                continue;
            }
            auto scale = selected->scale_factor;
            if (!scale && !selected->scale) {
                scale = 1;
            }
            if (!scale && selected->scale) {
                scale = evaluate(*selected->scale);
            }
            if (!scale) {
                return std::nullopt;
            }
            const auto overflows = [&] {
                if (*magnitude > 0) {
                    return *scale > 0
                        ? *magnitude
                            > std::numeric_limits<std::int64_t>::max()
                                / *scale
                        : *scale
                            < std::numeric_limits<std::int64_t>::min()
                                / *magnitude;
                }
                if (*magnitude < 0) {
                    return *scale > 0
                        ? *magnitude
                            < std::numeric_limits<std::int64_t>::min()
                                / *scale
                        : *scale != 0
                            && *magnitude
                                < std::numeric_limits<std::int64_t>::max()
                                    / *scale;
                }
                return false;
            }();
            if (overflows) {
                return std::nullopt;
            }
            return *magnitude * *scale;
        }
        return std::nullopt;
    }

    template <typename Expression>
    [[nodiscard]] std::optional<std::int64_t> evaluate_expression(
        const Expression& expression)
    {
        using Kind = decltype(expression.kind);
        constexpr bool systemverilog
            = std::is_same_v<Expression, sv::Expression>;
        if (expression.kind == Kind::integer_literal
            || expression.kind == Kind::boolean_literal
            || expression.kind == Kind::logic_literal) {
            const auto literal = parse_integral_identity(expression.text);
            if (literal || systemverilog
                || expression.kind != Kind::logic_literal) {
                return literal;
            }
        }
        if constexpr (!systemverilog) {
            if (expression.kind == Kind::string_literal) {
                return parse_vhdl_bit_string(expression.text);
            }
        }
        if (expression.kind == Kind::name
            || (!systemverilog
                && expression.kind == Kind::logic_literal)) {
            for (auto frame = call_frames_.rbegin();
                frame != call_frames_.rend(); ++frame) {
                if (const auto value = frame->pattern_values.find(
                        expression.text);
                    value != frame->pattern_values.end()) {
                    return value->second;
                }
            }
            if constexpr (!systemverilog) {
                const CompiledDeclarationPredicate value_declaration
                    = [](const CompiledDeclarationView& candidate) {
                          return candidate.vhdl != nullptr
                              && vhdl_evaluable_value_form(
                                  candidate.vhdl->form);
                      };
                const CompiledDesignResolver resolver { unit_ };
                const auto declaration
                    = resolve_vhdl_expression_declaration(
                        expression.id, value_declaration);
                if (declaration) {
                    if (expression.referenced_name) {
                        const auto& reference
                            = *expression.referenced_name;
                        const auto package
                            = resolver.resolve_vhdl_package_members(
                                          reference, expression.scope,
                                          value_declaration)
                                  .unique();
                        if (package && package->member == *declaration) {
                            return evaluate_vhdl_package_member(*package);
                        }
                    }
                    return evaluate_declaration(*declaration);
                }
                if (const auto hierarchy = hierarchy_identities_.find(
                        std::string { expression.text });
                    hierarchy != hierarchy_identities_.end()) {
                    return hierarchy->second;
                }
                return parse_integral_identity(expression.text);
            }
            if (expression.referenced_name) {
                if (expression.referenced_name->selected) {
                    if (const auto value = evaluate_declaration(
                            *expression.referenced_name->selected)) {
                        return value;
                    }
                }
                std::optional<std::int64_t> overload_value;
                for (const auto declaration :
                    expression.referenced_name->overloads) {
                    const auto value = evaluate_declaration(declaration);
                    if (!value) {
                        continue;
                    }
                    if (overload_value && *overload_value != *value) {
                        return std::nullopt;
                    }
                    overload_value = value;
                }
                if (overload_value) {
                    return overload_value;
                }
            }
            if (const auto declaration = find_actual_declaration(
                    expression.text, expression.scope, systemverilog)) {
                return evaluate_declaration(*declaration);
            }
            if (const auto declaration
                = find_systemverilog_package_declaration(
                    expression.text, expression.scope)) {
                return evaluate_declaration(*declaration);
            }
            if (const auto hierarchy = hierarchy_identities_.find(
                    std::string { expression.text });
                hierarchy != hierarchy_identities_.end()) {
                return hierarchy->second;
            }
            return parse_integral_identity(expression.text);
        }
        if (expression.kind == Kind::unary
            || expression.kind == Kind::update) {
            if (expression.operands.size() != 1U) {
                return std::nullopt;
            }
            const auto operand = evaluate(expression.operands.front());
            return operand
                ? evaluate_unary(expression.text, *operand)
                : std::nullopt;
        }
        if (expression.kind == Kind::binary) {
            if (expression.operands.size() != 2U) {
                return std::nullopt;
            }
            const auto left = evaluate(expression.operands[0]);
            const auto right = evaluate(expression.operands[1]);
            if (left && right) {
                return evaluate_binary(expression.text, *left, *right);
            }
            const auto operation = normalized_token(expression.text);
            const auto string_comparison
                = operation == "==" || operation == "!="
                || operation == "===" || operation == "!=="
                || operation == "=" || operation == "/=";
            if (!string_comparison) {
                return std::nullopt;
            }
            const auto left_string = evaluate_string(
                expression.operands[0]);
            const auto right_string = evaluate_string(
                expression.operands[1]);
            if (!left_string || !right_string) {
                return std::nullopt;
            }
            const bool equal = *left_string == *right_string;
            return operation == "!=" || operation == "!=="
                    || operation == "/="
                ? !equal
                : equal;
        }
        if constexpr (systemverilog) {
            if (expression.kind == Kind::concatenation
                || expression.kind == Kind::replication) {
                const auto bits = canonical_systemverilog_bits(
                    expression.id);
                if (!bits || bits->size() > 63U) {
                    return std::nullopt;
                }
                std::uint64_t value { };
                for (const auto bit : *bits) {
                    value = (value << 1U)
                        | static_cast<std::uint64_t>(bit == '1');
                }
                return static_cast<std::int64_t>(value);
            }
            if (expression.kind == Kind::call
                && expression.text == "?:"
                && expression.operands.size() == 3U) {
                const auto condition = evaluate_nonzero(
                    expression.operands[0]);
                return condition
                    ? evaluate(expression.operands[*condition ? 1U : 2U])
                    : std::nullopt;
            }
            if (expression.kind == Kind::call
                && expression.text == "$isunbounded") {
                if (expression.operands.size() != 1U) {
                    return std::nullopt;
                }
                const auto unbounded
                    = evaluate_systemverilog_unbounded_expression(
                        expression.operands.front());
                return unbounded
                    ? std::optional<std::int64_t> { *unbounded ? 1 : 0 }
                    : std::nullopt;
            }
            if (expression.kind == Kind::call
                && (expression.text == "$signed"
                    || expression.text == "$unsigned")) {
                return expression.operands.size() == 1U
                    ? evaluate(expression.operands.front())
                    : std::nullopt;
            }
            if (expression.kind == Kind::call
                && expression.text == "$clog2") {
                if (expression.operands.size() != 1U) {
                    return std::nullopt;
                }
                const auto operand = evaluate(
                    expression.operands.front());
                if (!operand || *operand < 0) {
                    return std::nullopt;
                }
                auto magnitude = static_cast<std::uint64_t>(*operand);
                std::int64_t result { };
                if (magnitude > 1U) {
                    --magnitude;
                    while (magnitude != 0U) {
                        ++result;
                        magnitude >>= 1U;
                    }
                }
                return result;
            }
            if (expression.kind == Kind::call) {
                return evaluate_callable(expression);
            }
        }
        if constexpr (!systemverilog) {
            if (const auto physical = evaluate_vhdl_physical(expression)) {
                return physical;
            }
            if (const auto attribute = evaluate_vhdl_attribute(expression)) {
                return attribute;
            }
            if (expression.kind == Kind::call) {
                constexpr std::string_view qualified_prefix {
                    "@vhdl-qualified:"
                };
                const CompiledDeclarationPredicate type_declaration
                    = [](const CompiledDeclarationView& candidate) {
                          return candidate.vhdl != nullptr
                              && vhdl_type_declaration_form(
                                  candidate.vhdl->form);
                      };
                const auto conversion
                    = resolve_vhdl_expression_declaration(
                        expression.id, type_declaration);
                auto conversion_name
                    = std::string_view { expression.text };
                if (conversion_name.starts_with(qualified_prefix)) {
                    conversion_name.remove_prefix(
                        qualified_prefix.size());
                }
                if (const auto separator
                        = conversion_name.find_last_of(".:");
                    separator != std::string_view::npos) {
                    conversion_name.remove_prefix(separator + 1U);
                }
                const bool predefined_scalar_conversion
                    = vhdl_name_equal(conversion_name, "integer")
                    || vhdl_name_equal(conversion_name, "natural")
                    || vhdl_name_equal(conversion_name, "positive");
                if (expression.operands.size() == 1U
                    && (conversion
                        || predefined_scalar_conversion
                        || expression.text.starts_with(
                            qualified_prefix))) {
                    return evaluate(expression.operands.front());
                }
                return evaluate_callable(expression);
            }
            if (expression.kind == Kind::conditional
                && expression.operands.size() == 3U) {
                const auto condition = evaluate(expression.operands[0]);
                if (!condition) {
                    return std::nullopt;
                }
                return evaluate(expression.operands[*condition != 0
                        ? 1U
                        : 2U]);
            }
        }
        return std::nullopt;
    }

    template <typename Expression>
    [[nodiscard]] std::optional<std::string> evaluate_string_expression(
        const Expression& expression)
    {
        using Kind = decltype(expression.kind);
        constexpr bool systemverilog
            = std::is_same_v<Expression, sv::Expression>;
        if (expression.kind == Kind::string_literal) {
            return expression.decoded_string;
        }
        if (expression.kind == Kind::name) {
            if constexpr (!systemverilog) {
                const CompiledDeclarationPredicate value_declaration
                    = [](const CompiledDeclarationView& candidate) {
                          return candidate.vhdl != nullptr
                              && vhdl_evaluable_value_form(
                                  candidate.vhdl->form);
                      };
                const CompiledDesignResolver resolver { unit_ };
                const auto declaration
                    = resolve_vhdl_expression_declaration(
                        expression.id, value_declaration);
                if (!declaration) {
                    return std::nullopt;
                }
                if (expression.referenced_name) {
                    const auto& reference = *expression.referenced_name;
                    const auto package
                        = resolver.resolve_vhdl_package_members(
                                      reference, expression.scope,
                                      value_declaration)
                              .unique();
                    if (package && package->member == *declaration) {
                        return evaluate_vhdl_package_string_member(
                            *package);
                    }
                }
                return evaluate_string_declaration(*declaration);
            }
            if (expression.referenced_name) {
                if (expression.referenced_name->selected) {
                    if (const auto value = evaluate_string_declaration(
                            *expression.referenced_name->selected)) {
                        return value;
                    }
                }
                std::optional<std::string> overload_value;
                for (const auto declaration :
                    expression.referenced_name->overloads) {
                    const auto value = evaluate_string_declaration(
                        declaration);
                    if (!value) {
                        continue;
                    }
                    if (overload_value && *overload_value != *value) {
                        return std::nullopt;
                    }
                    overload_value = value;
                }
                if (overload_value) {
                    return overload_value;
                }
            }
            if (const auto declaration = find_actual_declaration(
                    expression.text, expression.scope, systemverilog)) {
                return evaluate_string_declaration(*declaration);
            }
            if (const auto declaration
                = find_systemverilog_package_declaration(
                    expression.text, expression.scope)) {
                return evaluate_string_declaration(*declaration);
            }
            return std::nullopt;
        }
        if (expression.kind == Kind::concatenation) {
            if (expression.operands.empty()) {
                return std::nullopt;
            }
            std::string result;
            for (const auto operand : expression.operands) {
                const auto value = evaluate_string(operand);
                if (!value
                    || value->size()
                        > std::numeric_limits<std::size_t>::max()
                            - result.size()) {
                    return std::nullopt;
                }
                result += *value;
            }
            return result;
        }
        if constexpr (systemverilog) {
            if (expression.kind == Kind::call
                && expression.text == "?:"
                && expression.operands.size() == 3U) {
                const auto condition = evaluate(expression.operands[0]);
                return condition
                    ? evaluate_string(expression.operands[*condition != 0
                            ? 1U
                            : 2U])
                    : std::nullopt;
            }
        } else {
            if (expression.kind == Kind::conditional
                && expression.operands.size() == 3U) {
                const auto condition = evaluate(expression.operands[0]);
                return condition
                    ? evaluate_string(expression.operands[*condition != 0
                            ? 1U
                            : 2U])
                    : std::nullopt;
            }
        }
        return std::nullopt;
    }

    const SpecializedHirUnit& unit_;
    std::map<DeclarationId, std::optional<std::int64_t>> actuals_;
    std::map<DeclarationId, std::optional<std::string>> string_actuals_;
    std::map<DeclarationId, DeclarationId> actual_declarations_;
    std::map<std::string, std::optional<std::int64_t>>
        hierarchy_identities_;
    std::map<DeclarationId, std::optional<std::int64_t>> declarations_;
    std::map<ExpressionId, std::optional<std::int64_t>> expressions_;
    std::map<DeclarationId, std::optional<std::string>>
        string_declarations_;
    std::map<ExpressionId, std::optional<std::string>> string_expressions_;
    std::set<DeclarationId> active_declarations_;
    std::set<ExpressionId> active_expressions_;
    std::set<DeclarationId> active_string_declarations_;
    std::set<ExpressionId> active_string_expressions_;
    std::set<DeclarationId> active_unbounded_declarations_;
    std::set<ExpressionId> active_unbounded_expressions_;
    std::vector<CallFrame> call_frames_;
    std::vector<SpecializedHirConstantEffect>* effects_ { };
    std::string effect_code_;
    std::string effect_context_;
};

struct GenerateSelection {
    std::vector<DeclarationId> selected;
    std::vector<InstanceId> fallback_instances;
};

template <typename Generate>
void preserve_generate_subtree(
    const Generate& generate, GenerateSelection& output)
{
    output.fallback_instances.insert(output.fallback_instances.end(),
        generate.instances.begin(), generate.instances.end());
    for (const auto& nested : generate.nested) {
        preserve_generate_subtree(nested, output);
    }
}

template <typename Choice>
std::optional<bool> choice_matches(const Choice& choice,
    const std::int64_t selector, HirIntegralEvaluator& evaluator)
{
    const auto left = evaluator.evaluate(choice.left);
    if (!left) {
        return std::nullopt;
    }
    auto right = left;
    if (choice.right) {
        right = evaluator.evaluate(*choice.right);
        if (!right) {
            return std::nullopt;
        }
        const bool empty = choice.descending
            ? *left < *right
            : *left > *right;
        if (empty) {
            return false;
        }
    }
    return std::min(*left, *right) <= selector
        && selector <= std::max(*left, *right);
}

std::optional<bool> systemverilog_string_choice_matches(
    const sv::GenerateChoice& choice, const std::string_view selector,
    HirIntegralEvaluator& evaluator)
{
    if (choice.right) {
        return std::nullopt;
    }
    const auto value = evaluator.evaluate_string(choice.left);
    return value ? std::optional<bool> { *value == selector }
                 : std::nullopt;
}

std::optional<bool> systemverilog_bits_choice_matches(
    const sv::GenerateChoice& choice, const std::string_view selector,
    HirIntegralEvaluator& evaluator)
{
    const auto normalize = [](std::string_view bits) {
        const auto first = bits.find('1');
        return first == std::string_view::npos
            ? bits.substr(bits.empty() ? 0U : bits.size() - 1U)
            : bits.substr(first);
    };
    const auto left = evaluator.evaluate_systemverilog_bits(choice.left);
    if (!left) {
        return std::nullopt;
    }
    const auto normalized_selector = normalize(selector);
    const auto normalized_left = normalize(*left);
    if (!choice.right) {
        return normalized_left == normalized_selector;
    }
    const auto right = evaluator.evaluate_systemverilog_bits(*choice.right);
    if (!right) {
        return std::nullopt;
    }
    const auto normalized_right = normalize(*right);
    const auto compare = [](const std::string_view lhs,
                            const std::string_view rhs) {
        if (lhs.size() != rhs.size()) {
            return lhs.size() < rhs.size() ? -1 : 1;
        }
        return lhs == rhs ? 0 : lhs < rhs ? -1 : 1;
    };
    const auto low = choice.descending
        ? normalized_right : normalized_left;
    const auto high = choice.descending
        ? normalized_left : normalized_right;
    return compare(low, normalized_selector) <= 0
        && compare(normalized_selector, high) <= 0;
}

void select_systemverilog_generate(const sv::GenerateRegion& generate,
    HirIntegralEvaluator& evaluator, GenerateSelection& output);
void select_vhdl_generate(const vhdl::GenerateRegion& generate,
    HirIntegralEvaluator& evaluator, GenerateSelection& output);

void select_systemverilog_children(
    const std::span<const sv::GenerateRegion> children,
    HirIntegralEvaluator& evaluator, GenerateSelection& output)
{
    for (const auto& child : children) {
        select_systemverilog_generate(child, evaluator, output);
    }
}

void select_systemverilog_generate(const sv::GenerateRegion& generate,
    HirIntegralEvaluator& evaluator, GenerateSelection& output)
{
    if (generate.kind == sv::GenerateKind::block) {
        output.selected.push_back(generate.declaration);
        select_systemverilog_children(generate.nested, evaluator, output);
        return;
    }
    if (generate.kind == sv::GenerateKind::iterative) {
        output.fallback_instances.insert(
            output.fallback_instances.end(),
            generate.instances.begin(), generate.instances.end());
        select_systemverilog_children(
            generate.nested, evaluator, output);
        return;
    }
    if (!generate.condition) {
        preserve_generate_subtree(generate, output);
        return;
    }
    const auto selector = evaluator.evaluate(*generate.condition);
    const auto bit_selector = selector
        ? std::optional<std::string> { }
        : evaluator.evaluate_systemverilog_bits(*generate.condition);
    if (generate.kind == sv::GenerateKind::conditional) {
        const auto condition = selector
            ? std::optional<bool> { *selector != 0 }
            : evaluator.evaluate_truth(*generate.condition);
        if (!condition) {
            preserve_generate_subtree(generate, output);
            return;
        }
        const auto else_discriminator
            = generate.alternative_discriminator.empty()
            ? std::string { "else" }
            : generate.alternative_discriminator + "/else";
        if (*condition) {
            output.selected.push_back(generate.declaration);
            for (const auto& nested : generate.nested) {
                if (nested.alternative_discriminator
                    == generate.alternative_discriminator) {
                    select_systemverilog_generate(
                        nested, evaluator, output);
                }
            }
            return;
        }
        const auto alternative = std::ranges::find(
            generate.nested, else_discriminator,
            &sv::GenerateRegion::alternative_discriminator);
        if (alternative != generate.nested.end()) {
            select_systemverilog_generate(
                *alternative, evaluator, output);
        }
        return;
    }

    const auto string_selector = selector || bit_selector
        ? std::optional<std::string> { }
        : evaluator.evaluate_string(*generate.condition);
    if (!selector && !bit_selector && !string_selector) {
        preserve_generate_subtree(generate, output);
        return;
    }

    const sv::GenerateAlternative* selected = nullptr;
    const sv::GenerateAlternative* fallback = nullptr;
    for (const auto& alternative : generate.alternatives) {
        if (alternative.is_default) {
            fallback = &alternative;
            continue;
        }
        for (const auto& choice : alternative.choices) {
            const auto matches = selector
                ? choice_matches(choice, *selector, evaluator)
                : bit_selector
                ? systemverilog_bits_choice_matches(
                    choice, *bit_selector, evaluator)
                : systemverilog_string_choice_matches(
                    choice, *string_selector, evaluator);
            if (!matches) {
                preserve_generate_subtree(generate, output);
                return;
            }
            if (*matches) {
                selected = &alternative;
                break;
            }
        }
        if (selected != nullptr) {
            break;
        }
    }
    selected = selected != nullptr ? selected : fallback;
    if (selected == nullptr) {
        return;
    }
    const auto nested = std::ranges::find(
        generate.nested, selected->alternative_discriminator,
        &sv::GenerateRegion::alternative_discriminator);
    if (nested == generate.nested.end()) {
        preserve_generate_subtree(generate, output);
        return;
    }
    select_systemverilog_generate(*nested, evaluator, output);
}

const vhdl::GenerateRegion* vhdl_else_generate(
    const vhdl::GenerateRegion& generate)
{
    const auto label = generate.alternative_label.empty()
        ? generate.label + ".else"
        : generate.alternative_label;
    const auto found = std::ranges::find(
        generate.nested, label, &vhdl::GenerateRegion::label);
    return found == generate.nested.end() ? nullptr : &*found;
}

const vhdl::GenerateRegion* vhdl_alternative_generate(
    const vhdl::GenerateRegion& generate,
    const vhdl::GenerateRegion::Alternative& alternative)
{
    const auto label = alternative.label.empty()
        ? generate.label + ".alternative"
        : alternative.label;
    const auto found = std::ranges::find(
        generate.nested, label, &vhdl::GenerateRegion::label);
    return found == generate.nested.end() ? nullptr : &*found;
}

void select_vhdl_generate(const vhdl::GenerateRegion& generate,
    HirIntegralEvaluator& evaluator, GenerateSelection& output)
{
    if (generate.kind == vhdl::GenerateKind::block) {
        output.selected.push_back(generate.declaration);
        for (const auto& nested : generate.nested) {
            select_vhdl_generate(nested, evaluator, output);
        }
        return;
    }
    if (generate.kind == vhdl::GenerateKind::iterative) {
        preserve_generate_subtree(generate, output);
        return;
    }
    if (!generate.condition) {
        preserve_generate_subtree(generate, output);
        return;
    }
    const auto selector = evaluator.evaluate(*generate.condition);
    if (!selector) {
        preserve_generate_subtree(generate, output);
        return;
    }
    if (generate.kind == vhdl::GenerateKind::conditional) {
        const auto* alternative = vhdl_else_generate(generate);
        if (*selector != 0) {
            output.selected.push_back(generate.declaration);
            for (const auto& nested : generate.nested) {
                if (&nested != alternative) {
                    select_vhdl_generate(nested, evaluator, output);
                }
            }
        } else if (alternative != nullptr) {
            select_vhdl_generate(*alternative, evaluator, output);
        }
        return;
    }

    const vhdl::GenerateRegion::Alternative* selected = nullptr;
    const vhdl::GenerateRegion::Alternative* fallback = nullptr;
    for (const auto& alternative : generate.alternatives) {
        if (alternative.is_default) {
            fallback = &alternative;
            continue;
        }
        for (const auto& choice : alternative.choices) {
            const auto matches = choice_matches(
                choice, *selector, evaluator);
            if (!matches) {
                preserve_generate_subtree(generate, output);
                return;
            }
            if (*matches) {
                selected = &alternative;
                break;
            }
        }
        if (selected != nullptr) {
            break;
        }
    }
    selected = selected != nullptr ? selected : fallback;
    if (selected == nullptr) {
        return;
    }
    const auto* nested = vhdl_alternative_generate(
        generate, *selected);
    if (nested == nullptr) {
        preserve_generate_subtree(generate, output);
        return;
    }
    select_vhdl_generate(*nested, evaluator, output);
}

GenerateSelection select_generates(SpecializedHirUnit& specialization,
    const std::span<const UnitId> units)
{
    GenerateSelection result;
    HirIntegralEvaluator evaluator { specialization };
    for (const auto unit : units) {
        const auto view = specialization.design().find_unit(unit);
        if (!view) {
            continue;
        }
        if (view->systemverilog != nullptr) {
            for (const auto& generate : view->systemverilog->generates) {
                select_systemverilog_generate(
                    generate, evaluator, result);
            }
        } else if (view->vhdl != nullptr) {
            for (const auto& generate : view->vhdl->generates) {
                select_vhdl_generate(generate, evaluator, result);
            }
        }
    }
    canonicalize(result.selected);
    canonicalize(result.fallback_instances);
    return result;
}

} // namespace

struct SpecializedHirUnitFactory {
    static SpecializedHirUnit make(const CompiledDesign& design,
        SpecializedHirOverlay specialization,
        std::vector<UnitId> replacement_units,
        const bool validated_lookup_indexes)
    {
        return SpecializedHirUnit { design, std::move(specialization),
            std::move(replacement_units), validated_lookup_indexes };
    }
};

namespace {

std::optional<SpecializedHirUnit> working_specialization(
    const CompiledDesign& design,
    std::optional<SpecializedHirOverlay> specialization,
    const bool validated_lookup_indexes)
{
    if (!specialization) {
        return std::nullopt;
    }
    const auto selected = design.find_unit(specialization->unit);
    if (!selected) {
        return std::nullopt;
    }
    std::vector<UnitId> replacement_units { specialization->unit };
    if (selected->vhdl != nullptr) {
        if (const auto* entity = primary_entity(
                design, *selected->vhdl)) {
            replacement_units.push_back(entity->id);
        }
    }
    return SpecializedHirUnitFactory::make(design,
        std::move(*specialization), std::move(replacement_units),
        validated_lookup_indexes);
}

struct AssociationFormal {
    DeclarationId declaration;
    std::string_view name;
    bool type_parameter { };
};

struct AssociationActual {
    std::optional<std::string_view> formal;
    SpecializedHirAssociationKind kind {
        SpecializedHirAssociationKind::expression
    };
    std::optional<ExpressionId> expression;
    const sv::TypeReference* systemverilog_type { };
    const vhdl::SubtypeIndication* vhdl_type { };
    SourceSpanId source;
};

void append_identity_component(
    std::string& output, const std::string_view component)
{
    output += std::to_string(component.size());
    output += ':';
    output += component;
    output += ';';
}

std::optional<std::string_view> parent_actual_identity(
    const SpecializedHirUnit* const parent,
    const DeclarationId declaration)
{
    if (parent == nullptr) {
        return std::nullopt;
    }
    const auto& actuals = parent->specialization().actual_identities;
    const auto found = std::ranges::find(
        actuals, declaration,
        &SpecializedHirActualIdentity::declaration);
    return found == actuals.end()
        ? std::optional<std::string_view> { }
        : std::optional<std::string_view> { found->identity };
}

DeclarationId concrete_actual_declaration(
    const SpecializedHirUnit* const parent,
    const DeclarationId declaration)
{
    if (parent == nullptr) {
        return declaration;
    }
    return CompiledDesignResolver { *parent }
        .actual_declaration(declaration)
        .value_or(declaration);
}

std::optional<DeclarationId> expression_declaration(
    const CompiledDesign& design, const ExpressionId expression,
    const SpecializedHirUnit* const parent)
{
    const auto view = design.find_expression(expression);
    if (!view) {
        return std::nullopt;
    }
    const auto effective = parent != nullptr
            && &parent->design() == &design
        ? parent
        : nullptr;
    auto selected_unit = effective != nullptr
        ? effective->unit()
        : UnitId { };
    const auto scope = view->systemverilog != nullptr
        ? view->systemverilog->scope
        : view->vhdl->scope;
    if (!selected_unit.valid() && scope.valid()
        && scope.value() < design.semantics.scopes().size()) {
        selected_unit = design.semantics.scopes()[scope.value()].unit;
    }
    if (!selected_unit.valid()) {
        return std::nullopt;
    }
    const CompiledDesignResolver resolver {
        design, selected_unit, effective
    };
    const auto selected
        = resolver.resolve_expression_name(expression).unique();
    if (!selected) {
        return std::nullopt;
    }
    return resolver.actual_declaration(*selected).value_or(*selected);
}

std::string expression_identity(const CompiledDesign& design,
    ExpressionId expression, const SpecializedHirUnit* parent,
    std::set<ExpressionId>& active);

std::string source_identity(
    const CompiledDesign& design, const SourceSpanId source)
{
    if (!source.valid()
        || source.value() >= design.semantics.source_spans().size()) {
        return "missing-source";
    }
    const auto& span = design.semantics.source_spans()[source.value()];
    const auto digest = span.file.valid()
            && span.file.value() < design.semantics.source_files().size()
        ? design.semantics.source_files()[span.file.value()].content_digest
        : std::string { };
    std::string result;
    append_identity_component(result, span.logical_name);
    append_identity_component(result, digest);
    append_identity_component(result, std::to_string(span.begin.offset));
    append_identity_component(result, std::to_string(span.end.offset));
    return result;
}

std::string vhdl_reference_spelling(const vhdl::Name& name)
{
    return name.canonical.empty() ? name.spelling : name.canonical;
}

std::string vhdl_reference_spelling(const TypeReference& reference)
{
    return reference.spelling;
}

std::string vhdl_declaration_identity(
    const CompiledDesign& design, const DeclarationId declaration_id,
    const SpecializedHirUnit* const parent,
    std::set<DeclarationId>& active_declarations)
{
    if (!active_declarations.insert(declaration_id).second) {
        return "recursive-vhdl-declaration";
    }
    const auto finish = [&](std::string value) {
        active_declarations.erase(declaration_id);
        return value;
    };
    const auto declaration = design.find_declaration(declaration_id);
    if (!declaration || declaration->vhdl == nullptr) {
        return finish({ });
    }
    const auto& record = *declaration->vhdl;
    using Form = vhdl::DeclarationForm;
    const bool generic_subprogram
        = record.form == Form::generic_function_instance
        || record.form == Form::generic_procedure_instance;
    const bool package_instance = record.form == Form::package_instance;
    const bool callable = record.form == Form::function
        || record.form == Form::procedure;
    if (!generic_subprogram && !package_instance && !callable) {
        return finish({ });
    }

    std::string result = generic_subprogram
        ? "vhdl-generic-subprogram-v1;"
        : package_instance ? "vhdl-package-instance-v1;"
                           : "vhdl-callable-v1;";
    append_identity_component(result, record.name);
    append_identity_component(result, source_identity(design, record.source));
    std::vector<std::pair<std::string, std::string>> display_actuals;
    if (record.package) {
        append_identity_component(result,
            vhdl_reference_spelling(record.package->template_name));
        append_identity_component(result,
            record.package->generic_map_box ? "box" : "explicit");
        for (const auto& association : record.package->generic_map) {
            append_identity_component(result,
                association.formal
                    ? vhdl_reference_spelling(*association.formal)
                    : std::string { });
            append_identity_component(result,
                std::to_string(static_cast<unsigned>(association.kind)));
            if (association.expression) {
                std::set<ExpressionId> active_expressions;
                append_identity_component(result,
                    expression_identity(design, *association.expression,
                        parent, active_expressions));
                if ((package_instance || generic_subprogram)
                    && association.formal
                    && parent != nullptr
                    && &parent->design() == &design) {
                    if (const auto value
                        = parent->evaluate_integral_expression(
                            *association.expression)) {
                        display_actuals.emplace_back(
                            vhdl_reference_spelling(*association.formal),
                            std::to_string(*value));
                    }
                }
            } else if (association.type) {
                append_identity_component(result,
                    vhdl_reference_spelling(association.type->type_mark));
                append_identity_component(result,
                    association.type->executable_width
                        ? std::to_string(
                              *association.type->executable_width)
                        : std::string { });
            }
        }
        if (package_instance) {
            result.append("template=");
            result.append(vhdl_reference_spelling(
                record.package->template_name));
            for (const auto& [name, value] : display_actuals) {
                result.push_back(';');
                result.append(name);
                result.push_back('=');
                result.append(value);
            }
            result.push_back(';');
        } else if (generic_subprogram) {
            result.append("kind=");
            result.append(record.form == Form::generic_function_instance
                    ? "function"
                    : "procedure");
            result.append(";template=");
            result.append(vhdl_reference_spelling(
                record.package->template_name));
            for (const auto& [name, value] : display_actuals) {
                result.append(";generic=");
                result.append(name);
                result.push_back('=');
                result.append(value);
            }
            result.push_back(';');
        }
    }

    // A callable declared in a package specification executes the matching
    // package-body declaration. Include that body's source digest directly in
    // the actual identity so changing only the body invalidates the cache.
    if (callable && record.callable && !record.callable->defined
        && record.scope.valid()
        && record.scope.value() < design.semantics.scopes().size()) {
        const auto owner_id
            = design.semantics.scopes()[record.scope.value()].unit;
        const auto owner = design.find_unit(owner_id);
        if (owner && owner->vhdl != nullptr
            && owner->vhdl->kind == vhdl::UnitKind::package) {
            for (const auto& unit : design.vhdl_units()) {
                if (unit.kind != vhdl::UnitKind::package
                    || unit.primary_name.empty()
                    || !vhdl_name_equal(unit.library, owner->vhdl->library)
                    || !vhdl_name_equal(unit.name, owner->vhdl->name)) {
                    continue;
                }
                for (const auto candidate_id : unit.declarations) {
                    const auto candidate
                        = design.find_declaration(candidate_id);
                    if (candidate && candidate->vhdl != nullptr
                        && candidate->vhdl->form == record.form
                        && candidate->vhdl->callable
                        && candidate->vhdl->callable->defined
                        && vhdl_name_equal(
                            candidate->vhdl->name, record.name)) {
                        append_identity_component(result,
                            source_identity(
                                design, candidate->vhdl->source));
                    }
                }
            }
        }
    }
    return finish(std::move(result));
}

std::string expression_identity(const CompiledDesign& design,
    const ExpressionId expression,
    const SpecializedHirUnit* const parent,
    std::set<ExpressionId>& active)
{
    if (parent != nullptr && &parent->design() == &design) {
        if (const auto value = parent->evaluate_integral_expression(
                expression)) {
            return std::to_string(*value);
        }
    }
    if (!active.insert(expression).second) {
        return "recursive-expression";
    }
    const auto view = design.find_expression(expression);
    if (!view) {
        active.erase(expression);
        return "missing-expression";
    }
    const auto selected = view->systemverilog != nullptr
            && view->systemverilog->kind == sv::ExpressionKind::name
            && view->systemverilog->referenced_name
        ? view->systemverilog->referenced_name->selected
        : view->vhdl != nullptr
                && view->vhdl->kind == vhdl::ExpressionKind::name
                && view->vhdl->referenced_name
        ? view->vhdl->referenced_name->selected
        : std::nullopt;
    if (selected) {
        if (const auto actual = parent_actual_identity(parent, *selected)) {
            active.erase(expression);
            return std::string { *actual };
        }
    }
    std::string result = view->systemverilog != nullptr
        ? "sv-expression-v1;"
        : "vhdl-expression-v1;";
    const auto append_expression = [&](const auto& record) {
        using Expression = std::remove_cvref_t<decltype(record)>;
        using Kind = decltype(record.kind);
        append_identity_component(result,
            std::to_string(static_cast<unsigned>(record.kind)));
        if (record.kind == Kind::name && record.referenced_name
            && record.referenced_name->selected) {
            if (const auto actual = parent_actual_identity(
                    parent, *record.referenced_name->selected)) {
                append_identity_component(result, *actual);
            } else {
                std::set<DeclarationId> active_declarations;
                const auto declaration_identity
                    = vhdl_declaration_identity(design,
                        *record.referenced_name->selected, parent,
                        active_declarations);
                append_identity_component(result,
                    declaration_identity.empty()
                        ? std::string_view { record.text }
                        : std::string_view { declaration_identity });
            }
        } else {
            append_identity_component(result, record.text);
        }
        append_identity_component(result, record.nominal_type);
        if (record.decoded_string) {
            append_identity_component(result, *record.decoded_string);
        }
        for (const auto operand : record.operands) {
            append_identity_component(result,
                expression_identity(design, operand, parent, active));
        }
        if constexpr (std::is_same_v<Expression, sv::Expression>) {
            append_identity_component(result, record.class_identity);
            append_identity_component(
                result, record.class_member_identity);
        }
    };
    if (view->systemverilog != nullptr) {
        if ((view->systemverilog->kind
                    == sv::ExpressionKind::integer_literal
                || view->systemverilog->kind
                    == sv::ExpressionKind::boolean_literal
                || view->systemverilog->kind
                    == sv::ExpressionKind::logic_literal)
            && parse_integral_identity(view->systemverilog->text)) {
            result = std::to_string(
                *parse_integral_identity(view->systemverilog->text));
        } else {
            append_expression(*view->systemverilog);
        }
    } else if ((view->vhdl->kind
                       == vhdl::ExpressionKind::integer_literal
                   || view->vhdl->kind
                       == vhdl::ExpressionKind::boolean_literal
                   || view->vhdl->kind
                       == vhdl::ExpressionKind::logic_literal)
        && parse_integral_identity(view->vhdl->text)) {
        result = std::to_string(
            *parse_integral_identity(view->vhdl->text));
    } else {
        append_expression(*view->vhdl);
    }
    active.erase(expression);
    return result;
}

std::string expression_identity(const CompiledDesign& design,
    const ExpressionId expression,
    const SpecializedHirUnit* const parent)
{
    std::set<ExpressionId> active;
    return expression_identity(design, expression, parent, active);
}

std::string systemverilog_type_identity(
    const CompiledDesign& design, const sv::TypeReference& type,
    const SpecializedHirUnit* const parent)
{
    std::string result = "sv-type-v1;";
    append_identity_component(result, type.target.spelling);
    append_identity_component(result, type.class_identity);
    append_identity_component(
        result, type.virtual_interface ? "virtual-interface" : "value");
    append_identity_component(result, type.interface_type);
    append_identity_component(result, type.interface_modport);
    for (const auto& actual : type.interface_parameter_actuals) {
        append_identity_component(
            result, actual.formal.value_or(std::string { }));
        if (actual.expression) {
            append_identity_component(result,
                expression_identity(design, *actual.expression, parent));
        } else if (actual.type) {
            append_identity_component(result,
                systemverilog_type_identity(
                    design, *actual.type, parent));
        } else {
            append_identity_component(result, "default");
        }
    }
    append_identity_component(result, type.systemverilog_net_type);
    append_identity_component(
        result, type.systemverilog_resolution_function);
    append_identity_component(result,
        type.value_form
            ? std::to_string(static_cast<unsigned>(*type.value_form))
            : std::string { });
    append_identity_component(result,
        type.executable_width
            ? std::to_string(*type.executable_width)
            : std::string { });
    append_identity_component(result, type.signed_value ? "signed" : "unsigned");
    append_identity_component(result, type.four_state ? "four" : "two");
    if (type.packed_range) {
        append_identity_component(result,
            type.packed_range->left
                ? std::to_string(*type.packed_range->left)
                : type.packed_range->left_expression
                ? expression_identity(design,
                    *type.packed_range->left_expression, parent)
                : std::string { });
        append_identity_component(result,
            type.packed_range->right
                ? std::to_string(*type.packed_range->right)
                : type.packed_range->right_expression
                ? expression_identity(design,
                    *type.packed_range->right_expression, parent)
                : std::string { });
        append_identity_component(result,
            type.packed_range->descending ? "downto" : "to");
    }
    for (const auto& dimension : type.unpacked_dimensions) {
        append_identity_component(result,
            dimension.left ? std::to_string(*dimension.left)
                           : std::string { });
        append_identity_component(result,
            dimension.right ? std::to_string(*dimension.right)
                            : std::string { });
        append_identity_component(result,
            dimension.descending ? "downto" : "to");
    }
    for (const auto& element : type.container_element_types) {
        append_identity_component(result,
            systemverilog_type_identity(design, element, parent));
    }
    return result;
}

std::string vhdl_type_definition_identity(const CompiledDesign& design,
    TypeId type, const SpecializedHirUnit* parent,
    std::set<TypeId>& active_types);

void append_vhdl_range_identity(std::string& result,
    const CompiledDesign& design,
    const vhdl::RangeConstraint& constraint,
    const SpecializedHirUnit* const parent)
{
    const auto boundary_identity = [&](const auto value,
                                       const auto expression) {
        if (value) {
            return std::to_string(*value);
        }
        return expression
            ? expression_identity(design, *expression, parent)
            : std::string { };
    };
    append_identity_component(result,
        std::to_string(static_cast<unsigned>(constraint.kind)));
    append_identity_component(result,
        boundary_identity(
            constraint.left, constraint.left_expression));
    append_identity_component(result,
        boundary_identity(
            constraint.right, constraint.right_expression));
    append_identity_component(result,
        constraint.descending ? "downto" : "to");
    append_identity_component(result,
        constraint.null ? "null" : "non-null");
}

std::string vhdl_subtype_identity(const CompiledDesign& design,
    const vhdl::SubtypeIndication& type,
    const SpecializedHirUnit* const parent,
    std::set<TypeId>& active_types)
{
    std::string result = "vhdl-type-v1;";
    if (type.type_mark.target.valid()) {
        append_identity_component(result,
            vhdl_type_definition_identity(design,
                type.type_mark.target, parent, active_types));
    } else {
        append_identity_component(result, type.type_mark.spelling);
    }
    append_identity_component(result,
        std::to_string(static_cast<unsigned>(type.domain)));
    append_identity_component(result,
        type.executable_width
            ? std::to_string(*type.executable_width)
            : std::string { });
    append_identity_component(result,
        type.signed_value ? "signed" : "unsigned");
    append_identity_component(result,
        type.unconstrained ? "unconstrained" : "constrained");
    append_identity_component(result,
        std::to_string(type.integer_storage_width));
    append_identity_component(result,
        std::to_string(static_cast<unsigned>(
            type.unspecified_class)));
    append_identity_component(result,
        std::to_string(type.unspecified_array_index_count));
    append_identity_component(result,
        type.unspecified_inference_identity);
    append_identity_component(result,
        type.resolution_function.canonical.empty()
            ? std::string_view { type.resolution_function.spelling }
            : std::string_view { type.resolution_function.canonical });
    append_identity_component(result,
        type.predefined_attribute
            ? std::to_string(static_cast<unsigned>(
                  *type.predefined_attribute))
            : std::string { });
    append_identity_component(result,
        type.predefined_attribute_dimension
            ? expression_identity(design,
                  *type.predefined_attribute_dimension, parent)
            : std::string { });
    for (const auto type_class :
        type.unspecified_component_classes) {
        append_identity_component(result,
            std::to_string(static_cast<unsigned>(type_class)));
    }
    for (const auto& component :
        type.unspecified_component_type_marks) {
        append_identity_component(result, component);
    }
    for (const auto& constraint : type.constraints) {
        append_vhdl_range_identity(
            result, design, constraint, parent);
    }
    return result;
}

std::string vhdl_type_definition_identity(const CompiledDesign& design,
    const TypeId type_id, const SpecializedHirUnit* const parent,
    std::set<TypeId>& active_types)
{
    if (!active_types.insert(type_id).second) {
        return "recursive-vhdl-type";
    }
    const auto finish = [&](std::string value) {
        active_types.erase(type_id);
        return value;
    };
    const auto selected = design.find_type(type_id);
    if (!selected || selected->vhdl == nullptr) {
        return finish("missing-vhdl-type");
    }
    // A named type actual must carry its declaration's shape rather than its
    // source-file digest. That makes the identity sensitive to the selected
    // type while keeping unrelated declarations in the same unit out of a
    // child's specialization key.
    const auto& type = *selected->vhdl;
    std::string result = "vhdl-type-v1;structural;";
    append_identity_component(result,
        std::to_string(static_cast<unsigned>(type.form)));
    append_identity_component(result, type.name);
    append_identity_component(result,
        vhdl_subtype_identity(
            design, type.base, parent, active_types));
    for (const auto& literal : type.enumeration_literals) {
        append_identity_component(result, literal.spelling);
        append_identity_component(result,
            std::to_string(literal.ordinal));
    }
    for (const auto& dimension : type.array_dimensions) {
        append_identity_component(result,
            dimension.index_subtype.canonical.empty()
                ? std::string_view {
                      dimension.index_subtype.spelling }
                : std::string_view {
                      dimension.index_subtype.canonical });
        append_identity_component(result,
            dimension.unconstrained
                ? "unconstrained"
                : "constrained");
        if (dimension.constraint) {
            append_vhdl_range_identity(
                result, design, *dimension.constraint, parent);
        } else {
            append_identity_component(result, "no-constraint");
        }
    }
    if (type.element_subtype) {
        append_identity_component(result,
            vhdl_subtype_identity(design, *type.element_subtype,
                parent, active_types));
    }
    for (const auto& element : type.record_elements) {
        append_identity_component(result, element.name);
        append_identity_component(result,
            vhdl_subtype_identity(design, element.subtype,
                parent, active_types));
    }
    if (type.designated_subtype) {
        append_identity_component(result,
            vhdl_subtype_identity(design,
                *type.designated_subtype, parent, active_types));
    }
    append_identity_component(
        result, std::to_string(type.maximum_objects));
    append_identity_component(result,
        type.deallocate_releases_storage ? "release" : "retain");
    append_identity_component(result,
        type.reclaim_when_unreachable ? "reclaim" : "explicit");
    for (const auto member : type.protected_members) {
        const auto declaration = design.find_declaration(member);
        append_identity_component(result,
            declaration && declaration->vhdl != nullptr
                ? std::string_view { declaration->vhdl->name }
                : std::string_view { "missing-member" });
    }
    for (const auto& unit : type.physical_units) {
        append_identity_component(result, unit.name);
        const auto scale_identity = unit.scale_factor
            ? std::to_string(*unit.scale_factor)
            : unit.scale
            ? expression_identity(design, *unit.scale, parent)
            : std::string { };
        append_identity_component(result,
            scale_identity);
    }
    for (const auto attribute : type.attributes) {
        append_identity_component(result,
            std::to_string(static_cast<unsigned>(attribute)));
    }
    if (type.scalar_range) {
        append_vhdl_range_identity(
            result, design, *type.scalar_range, parent);
    }
    using Form = vhdl::TypeForm;
    if (type.form == Form::protected_type
        || type.form == Form::protected_body) {
        append_identity_component(result,
            source_identity(design, type.source));
    }
    return finish(std::move(result));
}

std::string vhdl_type_identity(const CompiledDesign& design,
    const vhdl::SubtypeIndication& type,
    const SpecializedHirUnit* const parent)
{
    std::set<TypeId> active_types;
    auto result = vhdl_subtype_identity(
        design, type, parent, active_types);
    if (parent == nullptr) {
        return result;
    }
    for (const auto& constraint : type.constraints) {
        if ((constraint.kind != vhdl::RangeKind::discrete
                && constraint.kind != vhdl::RangeKind::enumeration)
            || (!constraint.left && !constraint.left_expression)
            || (!constraint.right && !constraint.right_expression)) {
            continue;
        }
        const auto left = constraint.left
            ? constraint.left
            : parent->evaluate_integral_expression(
                  *constraint.left_expression);
        const auto right = constraint.right
            ? constraint.right
            : parent->evaluate_integral_expression(
                  *constraint.right_expression);
        if (left && right) {
            result += ";enum-range=" + std::to_string(*left)
                + ":" + std::to_string(*right) + ":"
                + (constraint.descending ? "1" : "0");
        }
    }
    return result;
}

enum class InferredVhdlTypeClass : std::uint8_t {
    unknown,
    discrete,
    integer,
    physical,
    floating,
    array,
    access,
    file,
    composite,
    protected_type,
};

InferredVhdlTypeClass vhdl_builtin_type_class(
    const std::string_view spelling, const vhdl::ValueDomain domain)
{
    const auto separator = spelling.find_last_of('.');
    const auto name = spelling.substr(separator == std::string_view::npos
            ? 0U
            : separator + 1U);
    if (vhdl_name_equal(name, "bit_vector")
        || vhdl_name_equal(name, "std_logic_vector")
        || vhdl_name_equal(name, "std_ulogic_vector")
        || vhdl_name_equal(name, "signed")
        || vhdl_name_equal(name, "unsigned")
        || vhdl_name_equal(name, "string")
        || vhdl_name_equal(name, "boolean_vector")
        || vhdl_name_equal(name, "integer_vector")
        || vhdl_name_equal(name, "real_vector")
        || vhdl_name_equal(name, "time_vector")
        || vhdl_name_equal(name, "ufixed")
        || vhdl_name_equal(name, "sfixed")
        || vhdl_name_equal(name, "unresolved_ufixed")
        || vhdl_name_equal(name, "unresolved_sfixed")
        || vhdl_name_equal(name, "float")
        || vhdl_name_equal(name, "unresolved_float")
        || vhdl_name_equal(name, "u_float")) {
        return InferredVhdlTypeClass::array;
    }
    if (vhdl_name_equal(name, "integer")
        || vhdl_name_equal(name, "natural")
        || vhdl_name_equal(name, "positive")
        || vhdl_name_equal(name, "universal_integer")
        || domain == vhdl::ValueDomain::integer) {
        return InferredVhdlTypeClass::integer;
    }
    if (vhdl_name_equal(name, "real")
        || vhdl_name_equal(name, "universal_real")) {
        return InferredVhdlTypeClass::floating;
    }
    if (vhdl_name_equal(name, "time")) {
        return InferredVhdlTypeClass::physical;
    }
    if (vhdl_name_equal(name, "boolean")
        || vhdl_name_equal(name, "bit")
        || vhdl_name_equal(name, "character")
        || vhdl_name_equal(name, "severity_level")
        || vhdl_name_equal(name, "file_open_kind")
        || vhdl_name_equal(name, "file_open_status")
        || vhdl_name_equal(name, "std_logic")
        || vhdl_name_equal(name, "std_ulogic")) {
        return InferredVhdlTypeClass::discrete;
    }
    if (domain == vhdl::ValueDomain::boolean
        || domain == vhdl::ValueDomain::bit2
        || domain == vhdl::ValueDomain::logic4
        || domain == vhdl::ValueDomain::logic9) {
        return InferredVhdlTypeClass::discrete;
    }
    if (domain == vhdl::ValueDomain::string) {
        return InferredVhdlTypeClass::array;
    }
    return InferredVhdlTypeClass::unknown;
}

InferredVhdlTypeClass vhdl_subtype_class(
    const CompiledDesign& design, const vhdl::SubtypeIndication& subtype,
    std::set<TypeId>& active)
{
    if (subtype.type_mark.target.valid()
        && active.insert(subtype.type_mark.target).second) {
        const auto type = design.find_type(subtype.type_mark.target);
        if (type && type->vhdl != nullptr) {
            const auto& definition = *type->vhdl;
            auto result = InferredVhdlTypeClass::unknown;
            switch (definition.form) {
            case vhdl::TypeForm::enumeration:
                result = InferredVhdlTypeClass::discrete;
                break;
            case vhdl::TypeForm::array:
                result = InferredVhdlTypeClass::array;
                break;
            case vhdl::TypeForm::record:
                result = InferredVhdlTypeClass::composite;
                break;
            case vhdl::TypeForm::access:
                result = InferredVhdlTypeClass::access;
                break;
            case vhdl::TypeForm::file:
                result = InferredVhdlTypeClass::file;
                break;
            case vhdl::TypeForm::protected_type:
            case vhdl::TypeForm::protected_body:
                result = InferredVhdlTypeClass::protected_type;
                break;
            case vhdl::TypeForm::physical:
                result = InferredVhdlTypeClass::physical;
                break;
            case vhdl::TypeForm::subtype:
            case vhdl::TypeForm::alias:
                result = vhdl_subtype_class(
                    design, definition.base, active);
                break;
            case vhdl::TypeForm::scalar:
                result = vhdl_builtin_type_class(
                    definition.name, definition.base.domain);
                break;
            case vhdl::TypeForm::unresolved:
                result = InferredVhdlTypeClass::unknown;
                break;
            }
            active.erase(subtype.type_mark.target);
            if (result != InferredVhdlTypeClass::unknown) {
                return result;
            }
        } else {
            active.erase(subtype.type_mark.target);
        }
    }
    return vhdl_builtin_type_class(
        subtype.type_mark.spelling, subtype.domain);
}

InferredVhdlTypeClass vhdl_subtype_class(
    const CompiledDesign& design, const vhdl::SubtypeIndication& subtype)
{
    std::set<TypeId> active;
    return vhdl_subtype_class(design, subtype, active);
}

InferredVhdlTypeClass vhdl_declaration_type_class(
    const CompiledDesign& design, const DeclarationId declaration,
    const std::string_view fallback_spelling)
{
    const auto selected = design.find_declaration(declaration);
    return selected && selected->vhdl != nullptr
            && selected->vhdl->subtype
        ? vhdl_subtype_class(design, *selected->vhdl->subtype)
        : vhdl_builtin_type_class(
              fallback_spelling, vhdl::ValueDomain::unknown);
}

bool vhdl_unspecified_type_accepts(
    const vhdl::UnspecifiedTypeClass formal,
    const InferredVhdlTypeClass actual)
{
    if (formal == vhdl::UnspecifiedTypeClass::none) {
        return true;
    }
    // A restricted unspecified type generic is not a wildcard.  Failing to
    // recover the actual's class cannot make an otherwise incompatible
    // actual legal; doing so used to accept an array for "type T is range <>".
    if (actual == InferredVhdlTypeClass::unknown) {
        return false;
    }
    using Formal = vhdl::UnspecifiedTypeClass;
    using Actual = InferredVhdlTypeClass;
    switch (formal) {
    case Formal::private_type:
        return actual != Actual::file
            && actual != Actual::protected_type;
    case Formal::scalar:
        return actual == Actual::discrete
            || actual == Actual::integer
            || actual == Actual::physical
            || actual == Actual::floating;
    case Formal::discrete:
        return actual == Actual::discrete
            || actual == Actual::integer;
    case Formal::integer:
        return actual == Actual::integer;
    case Formal::physical:
        return actual == Actual::physical;
    case Formal::floating:
        return actual == Actual::floating;
    case Formal::array:
        return actual == Actual::array;
    case Formal::access:
        return actual == Actual::access;
    case Formal::file:
        return actual == Actual::file;
    case Formal::none:
        return true;
    }
    return false;
}

struct InferredVhdlTypeActual {
    std::string identity;
    std::optional<DeclarationId> declaration;
    InferredVhdlTypeClass type_class {
        InferredVhdlTypeClass::unknown
    };
    std::optional<ExpressionId> expression { };
    std::optional<vhdl::SubtypeIndication> subtype;
};

std::optional<InferredVhdlTypeActual> vhdl_type_mark_actual(
    const CompiledDesign& design, const vhdl::Expression& expression,
    const SpecializedHirUnit* const parent)
{
    vhdl::Name builtin_reference;
    if (!expression.referenced_name) {
        if (vhdl_builtin_type_class(
                expression.text, vhdl::ValueDomain::unknown)
            == InferredVhdlTypeClass::unknown) {
            return std::nullopt;
        }
        builtin_reference.spelling = expression.text;
        builtin_reference.canonical = expression.text;
        builtin_reference.source = expression.source;
    }
    const auto& reference = expression.referenced_name
        ? *expression.referenced_name
        : builtin_reference;
    const auto type_name = !expression.text.empty()
        ? std::string_view { expression.text }
        : reference.canonical.empty()
        ? std::string_view { reference.spelling }
        : std::string_view { reference.canonical };
    if (parent != nullptr && &parent->design() == &design) {
        const auto& actuals = parent->specialization().actual_identities;
        const auto actual = std::ranges::find_if(
            actuals, [&](const SpecializedHirActualIdentity& candidate) {
                if (reference.selected
                    && candidate.declaration == *reference.selected) {
                    return true;
                }
                const auto declaration = parent->find_declaration(
                    candidate.declaration);
                return declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->form
                        == vhdl::DeclarationForm::generic_type
                    && vhdl_name_equal(
                        declaration->vhdl->name, type_name);
            });
        if (actual != actuals.end()) {
            if (actual->identity.empty()) {
                return std::nullopt;
            }
            return InferredVhdlTypeActual {
                actual->identity, actual->actual_declaration,
                actual->actual_declaration
                    ? vhdl_declaration_type_class(design,
                          *actual->actual_declaration, type_name)
                    : InferredVhdlTypeClass::unknown,
                actual->actual_expression,
                actual->vhdl_type };
        }
    }

    std::optional<DeclarationId> declaration = reference.selected;
    std::optional<std::string> structural_identity;
    std::optional<vhdl::SubtypeIndication> subtype;
    if (!declaration) {
        auto scope = std::optional<ScopeId> { expression.scope };
        while (scope && !declaration) {
            std::optional<DeclarationId> candidate;
            for (const auto& record : design.vhdl_hir.declarations()) {
                if (record.scope != *scope
                    || !vhdl_type_declaration_form(record.form)
                    || !vhdl_name_equal(record.name, type_name)) {
                    continue;
                }
                if (candidate) {
                    candidate.reset();
                    break;
                }
                candidate = record.id;
            }
            if (candidate) {
                declaration = candidate;
                break;
            }
            const auto found = std::ranges::find(
                design.semantics.scopes(), *scope, &Scope::id);
            scope = found != design.semantics.scopes().end()
                ? found->parent
                : std::nullopt;
        }
    }
    if (!declaration) {
        auto simple_type_name = type_name;
        if (const auto separator = simple_type_name.find_last_of('.');
            separator != std::string_view::npos) {
            simple_type_name.remove_prefix(separator + 1U);
        }
        std::optional<DeclarationId> candidate;
        for (const auto& record : design.vhdl_hir.declarations()) {
            if (!vhdl_type_declaration_form(record.form)
                || !vhdl_name_equal(record.name, simple_type_name)) {
                continue;
            }
            if (candidate) {
                candidate.reset();
                break;
            }
            candidate = record.id;
        }
        declaration = candidate;
    }
    if (declaration) {
        const auto selected = design.find_declaration(*declaration);
        if (!selected || selected->vhdl == nullptr
            || !vhdl_type_declaration_form(
                selected->vhdl->form)) {
            return std::nullopt;
        }
        if (selected->vhdl->declared_type) {
            subtype.emplace();
            subtype->type_mark.target = *selected->vhdl->declared_type;
            subtype->type_mark.spelling = std::string { type_name };
            subtype->type_mark.source = expression.source;
            std::set<TypeId> active_types;
            structural_identity = vhdl_type_definition_identity(
                design, *selected->vhdl->declared_type,
                parent, active_types);
        } else if (selected->vhdl->subtype) {
            subtype = *selected->vhdl->subtype;
            structural_identity = vhdl_type_identity(
                design, *selected->vhdl->subtype, parent);
        }
    } else {
        if (vhdl_builtin_type_class(
                type_name, vhdl::ValueDomain::unknown)
            == InferredVhdlTypeClass::unknown) {
            return std::nullopt;
        }
        subtype.emplace();
        subtype->type_mark.spelling = std::string { type_name };
        subtype->type_mark.source = expression.source;
    }

    std::string identity;
    if (structural_identity) {
        identity = std::move(*structural_identity);
    } else {
        identity = "vhdl-type-v1;";
        append_identity_component(identity,
            reference.canonical.empty()
                ? type_name
                : std::string_view { reference.canonical });
    }
    return InferredVhdlTypeActual {
        std::move(identity), declaration,
        declaration
            ? vhdl_declaration_type_class(
                  design, *declaration, type_name)
            : vhdl_builtin_type_class(
                  type_name, vhdl::ValueDomain::unknown),
        std::nullopt,
        std::move(subtype) };
}

std::optional<InferredVhdlTypeActual> infer_vhdl_type_actual(
    const CompiledDesign& design, const ExpressionId expression_id,
    const SpecializedHirUnit* const parent)
{
    const auto expression = design.find_expression(expression_id);
    if (!expression || expression->vhdl == nullptr) {
        return std::nullopt;
    }
    const auto& record = *expression->vhdl;
    if (record.kind == vhdl::ExpressionKind::name) {
        auto inferred = vhdl_type_mark_actual(design, record, parent);
        if (inferred && !inferred->expression) {
            inferred->expression = expression_id;
        }
        return inferred;
    }

    const auto append_constraint = [&](std::string& identity,
                                       const vhdl::Expression& constraint)
        -> bool {
        if (constraint.kind != vhdl::ExpressionKind::binary
            || constraint.operands.size() != 2U
            || (!vhdl_name_equal(constraint.text, "to")
                && !vhdl_name_equal(
                    constraint.text, "downto"))) {
            return false;
        }
        append_identity_component(identity,
            vhdl_name_equal(constraint.text, "downto")
                ? "downto"
                : "to");
        append_identity_component(identity,
            expression_identity(
                design, constraint.operands[0], parent));
        append_identity_component(identity,
            expression_identity(
                design, constraint.operands[1], parent));
        return true;
    };

    if (record.kind == vhdl::ExpressionKind::slice
        && record.operands.size() == 3U
        && (vhdl_name_equal(record.text, "to")
            || vhdl_name_equal(record.text, "downto"))) {
        const auto type_mark = design.find_expression(
            record.operands.front());
        if (!type_mark || type_mark->vhdl == nullptr
            || type_mark->vhdl->kind
                != vhdl::ExpressionKind::name) {
            return std::nullopt;
        }
        auto inferred = vhdl_type_mark_actual(
            design, *type_mark->vhdl, parent);
        if (!inferred) {
            return std::nullopt;
        }
        std::string identity { "vhdl-type-constraint-v1;" };
        append_identity_component(identity, inferred->identity);
        append_identity_component(identity,
            vhdl_name_equal(record.text, "downto")
                ? "downto"
                : "to");
        append_identity_component(identity,
            expression_identity(design, record.operands[1], parent));
        append_identity_component(identity,
            expression_identity(design, record.operands[2], parent));
        if (parent != nullptr) {
            const auto left = parent->evaluate_integral_expression(
                record.operands[1]);
            const auto right = parent->evaluate_integral_expression(
                record.operands[2]);
            if (left && right) {
                identity += ";enum-range=" + std::to_string(*left)
                    + ":" + std::to_string(*right) + ":"
                    + (vhdl_name_equal(record.text, "downto")
                            ? "1"
                            : "0");
            }
        }
        if (inferred->subtype) {
            vhdl::RangeConstraint constraint;
            constraint.kind = vhdl::RangeKind::discrete;
            constraint.left_expression = record.operands[1];
            constraint.right_expression = record.operands[2];
            constraint.descending = vhdl_name_equal(
                record.text, "downto");
            constraint.source = record.source;
            inferred->subtype->constraints = { std::move(constraint) };
            inferred->subtype->unconstrained = false;
            inferred->subtype->executable_width.reset();
        }
        inferred->identity = std::move(identity);
        if (inferred->type_class == InferredVhdlTypeClass::unknown) {
            inferred->type_class = InferredVhdlTypeClass::array;
        }
        inferred->expression = expression_id;
        return inferred;
    }

    if (record.kind != vhdl::ExpressionKind::call
        || record.operands.empty()) {
        return std::nullopt;
    }
    auto inferred = vhdl_type_mark_actual(design, record, parent);
    if (!inferred) {
        return std::nullopt;
    }
    std::string identity { "vhdl-type-constraint-v1;" };
    append_identity_component(identity, inferred->identity);
    for (const auto operand : record.operands) {
        const auto constraint = design.find_expression(operand);
        if (!constraint || constraint->vhdl == nullptr
            || !append_constraint(identity, *constraint->vhdl)) {
            return std::nullopt;
        }
        if (inferred->subtype
            && constraint->vhdl->operands.size() == 2U) {
            vhdl::RangeConstraint range;
            range.kind = vhdl::RangeKind::discrete;
            range.left_expression = constraint->vhdl->operands[0];
            range.right_expression = constraint->vhdl->operands[1];
            range.descending = vhdl_name_equal(
                constraint->vhdl->text, "downto");
            range.source = constraint->vhdl->source;
            inferred->subtype->constraints.push_back(std::move(range));
            inferred->subtype->unconstrained = false;
            inferred->subtype->executable_width.reset();
        }
    }
    if (parent != nullptr && record.operands.size() == 1U) {
        const auto constraint = design.find_expression(
            record.operands.front());
        if (constraint && constraint->vhdl != nullptr
            && constraint->vhdl->kind
                == vhdl::ExpressionKind::binary
            && constraint->vhdl->operands.size() == 2U) {
            const auto left = parent->evaluate_integral_expression(
                constraint->vhdl->operands[0]);
            const auto right = parent->evaluate_integral_expression(
                constraint->vhdl->operands[1]);
            if (left && right) {
                identity += ";enum-range="
                    + std::to_string(*left) + ":"
                    + std::to_string(*right) + ":"
                    + (vhdl_name_equal(
                           constraint->vhdl->text, "downto")
                            ? "1"
                            : "0");
            }
        }
    }
    inferred->identity = std::move(identity);
    if (inferred->type_class == InferredVhdlTypeClass::unknown) {
        inferred->type_class = InferredVhdlTypeClass::array;
    }
    inferred->expression = expression_id;
    return inferred;
}

void append_systemverilog_formals(const CompiledDesign& design,
    const sv::Unit& unit,
    const SpecializedHirAssociationSurface surface,
    std::vector<AssociationFormal>& formals)
{
    for (const auto id : unit.declarations) {
        const auto declaration = design.find_declaration(id);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration_belongs_to(design, id, unit.id)) {
            continue;
        }
        const auto form = declaration->systemverilog->form;
        const bool parameter = form == sv::DeclarationForm::parameter
            || form == sv::DeclarationForm::type_parameter;
        const bool port = form == sv::DeclarationForm::port;
        if ((surface == SpecializedHirAssociationSurface::parameters
                && parameter)
            || (surface == SpecializedHirAssociationSurface::ports
                && port)) {
            formals.push_back({ id, declaration->systemverilog->name,
                form == sv::DeclarationForm::type_parameter });
        }
    }
}

void append_vhdl_formals(const CompiledDesign& design,
    const vhdl::Unit& unit,
    const SpecializedHirAssociationSurface surface,
    std::vector<AssociationFormal>& formals)
{
    for (const auto id : unit.declarations) {
        const auto declaration = design.find_declaration(id);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration_belongs_to(design, id, unit.id)) {
            continue;
        }
        const auto form = declaration->vhdl->form;
        const bool parameter = vhdl_actual_form(form);
        const bool port = form == vhdl::DeclarationForm::port;
        if ((surface == SpecializedHirAssociationSurface::parameters
                && parameter)
            || (surface == SpecializedHirAssociationSurface::ports
                && port)) {
            formals.push_back({ id, declaration->vhdl->name,
                form == vhdl::DeclarationForm::generic_type });
        }
    }
}

std::vector<AssociationFormal> association_formals(
    const CompiledDesign& design, const CompiledUnitView target,
    const SpecializedHirAssociationSurface surface)
{
    std::vector<AssociationFormal> result;
    if (target.systemverilog != nullptr) {
        append_systemverilog_formals(
            design, *target.systemverilog, surface, result);
        return result;
    }
    if (target.vhdl == nullptr) {
        return result;
    }
    if (const auto* entity = primary_entity(design, *target.vhdl)) {
        append_vhdl_formals(design, *entity, surface, result);
    }
    append_vhdl_formals(design, *target.vhdl, surface, result);
    return result;
}

bool association_formal_has_default(
    const CompiledDesign& design, const AssociationFormal& formal)
{
    const auto declaration = design.find_declaration(formal.declaration);
    if (!declaration) {
        return false;
    }
    if (declaration->systemverilog != nullptr) {
        return declaration->systemverilog->initializer.has_value()
            || declaration->systemverilog->default_type.has_value();
    }
    if (declaration->vhdl == nullptr) {
        return false;
    }
    const auto& record = *declaration->vhdl;
    if (record.initializer || record.default_type) {
        return true;
    }
    if (record.callable
        && (record.callable->default_callable
            || record.callable->default_box)) {
        return true;
    }
    return record.package && record.package->generic_map_box;
}

SpecializedHirAssociationKind association_kind(
    const sv::ActualKind kind)
{
    switch (kind) {
    case sv::ActualKind::expression:
        return SpecializedHirAssociationKind::expression;
    case sv::ActualKind::type:
        return SpecializedHirAssociationKind::type;
    case sv::ActualKind::default_value:
        return SpecializedHirAssociationKind::default_value;
    case sv::ActualKind::open:
        return SpecializedHirAssociationKind::open;
    }
    return SpecializedHirAssociationKind::expression;
}

SpecializedHirAssociationKind association_kind(
    const vhdl::AssociationKind kind)
{
    switch (kind) {
    case vhdl::AssociationKind::expression:
        return SpecializedHirAssociationKind::expression;
    case vhdl::AssociationKind::type:
        return SpecializedHirAssociationKind::type;
    case vhdl::AssociationKind::open:
        return SpecializedHirAssociationKind::open;
    case vhdl::AssociationKind::default_box:
        return SpecializedHirAssociationKind::default_value;
    }
    return SpecializedHirAssociationKind::expression;
}

std::vector<AssociationActual> association_actuals(
    const CompiledInstanceView instance,
    const SpecializedHirAssociationSurface surface)
{
    std::vector<AssociationActual> result;
    if (instance.systemverilog != nullptr) {
        const auto& actuals
            = surface == SpecializedHirAssociationSurface::parameters
            ? instance.systemverilog->parameters
            : instance.systemverilog->ports;
        result.reserve(actuals.size());
        for (const auto& actual : actuals) {
            result.push_back({
                actual.formal
                    ? std::optional<std::string_view> { *actual.formal }
                    : std::nullopt,
                association_kind(actual.kind), actual.expression,
                actual.type ? &*actual.type : nullptr, nullptr,
                actual.source });
        }
    } else if (instance.vhdl != nullptr) {
        const auto& actuals
            = surface == SpecializedHirAssociationSurface::parameters
            ? instance.vhdl->generic_map
            : instance.vhdl->port_map;
        result.reserve(actuals.size());
        for (const auto& actual : actuals) {
            result.push_back({
                actual.formal
                    ? std::optional<std::string_view> {
                          actual.formal->spelling }
                    : std::nullopt,
                association_kind(actual.kind), actual.expression,
                nullptr, actual.type ? &*actual.type : nullptr,
                actual.source });
        }
    }
    return result;
}

bool association_name_equal(const std::string_view formal,
    const std::string_view actual, const bool vhdl_names)
{
    return vhdl_names ? vhdl_name_equal(formal, actual)
                      : formal == actual;
}

std::optional<sv::TypeReference> specialized_systemverilog_type_actual(
    const sv::TypeReference& input,
    const SpecializedHirUnit* const parent)
{
    if (parent == nullptr) {
        return input;
    }
    auto result = input;
    std::set<DeclarationId> visited;
    for (;;) {
        const auto separator = result.target.spelling.rfind("::");
        const auto name = separator == std::string::npos
            ? std::string_view { result.target.spelling }
            : std::string_view { result.target.spelling }.substr(
                  separator + 2U);
        const SpecializedHirActualIdentity* selected = nullptr;
        for (const auto& actual :
            parent->specialization().actual_identities) {
            const auto declaration = parent->find_declaration(
                actual.declaration);
            if (!declaration || declaration->systemverilog == nullptr
                || declaration->systemverilog->form
                    != sv::DeclarationForm::type_parameter) {
                continue;
            }
            const auto& formal = *declaration->systemverilog;
            const bool identity_match = result.target.target.valid()
                && formal.declared_type
                && *formal.declared_type == result.target.target;
            const bool name_match = !name.empty() && formal.name == name;
            if (identity_match || name_match) {
                selected = &actual;
                break;
            }
        }
        if (selected == nullptr || !selected->systemverilog_type
            || !visited.insert(selected->declaration).second) {
            return result;
        }
        result = *selected->systemverilog_type;
    }
}

bool systemverilog_wildcard_object_form(
    const sv::DeclarationForm form)
{
    return form == sv::DeclarationForm::port
        || form == sv::DeclarationForm::net
        || form == sv::DeclarationForm::variable;
}

std::optional<DeclarationId> systemverilog_wildcard_actual(
    const CompiledDesign& design,
    const SpecializedHirUnit& parent,
    ScopeId scope,
    const std::string_view name)
{
    const CompiledDeclarationPredicate object
        = [](const CompiledDeclarationView& candidate) {
              return candidate.systemverilog != nullptr
                  && systemverilog_wildcard_object_form(
                      candidate.systemverilog->form);
          };
    return CompiledDesignResolver { design, parent.unit(), &parent }
        .resolve_systemverilog(
            name, scope, object, false, parent.scope())
        .unique();
}

} // namespace

namespace {

std::optional<SpecializedHirOverlay>
make_specialized_hir_overlay_from_validated_design(
    const CompiledDesign& design, const UnitId selected_unit,
    const std::span<const SpecializedHirActualIdentity> actuals)
{
    const auto selected = design.find_unit(selected_unit);
    if (!selected || selected->identity == nullptr) {
        return std::nullopt;
    }
    std::set<DeclarationId> allowed;
    std::set<UnitId> dependency_units { selected_unit };
    const vhdl::Unit* entity = nullptr;
    if (selected->systemverilog != nullptr) {
        add_systemverilog_actuals(
            design, *selected->systemverilog, allowed);
    } else if (selected->vhdl != nullptr
        && (selected->vhdl->kind == vhdl::UnitKind::entity
            || selected->vhdl->kind == vhdl::UnitKind::architecture)) {
        entity = primary_entity(design, *selected->vhdl);
        if (selected->vhdl->kind == vhdl::UnitKind::architecture
            && entity == nullptr) {
            return std::nullopt;
        }
        if (entity != nullptr) {
            add_vhdl_actuals(design, *entity, allowed);
            dependency_units.insert(entity->id);
        }
        add_vhdl_actuals(design, *selected->vhdl, allowed);
    } else {
        return std::nullopt;
    }
    const auto dependencies = validate_actuals(design, allowed, actuals);
    if (!dependencies) {
        return std::nullopt;
    }
    SpecializedHirOverlay result;
    result.unit = selected_unit;
    result.scope = selected->identity->scope;
    result.language = selected->language;
    result.actual_identities.assign(actuals.begin(), actuals.end());
    std::ranges::sort(result.actual_identities, {},
        &SpecializedHirActualIdentity::declaration);
    if (selected->systemverilog != nullptr) {
        collect_expressions(design,
            design.systemverilog_hir.expressions(), dependency_units,
            *dependencies, result.residual_expressions);
        collect_generates(*selected->systemverilog,
            *dependencies, result.dependent_generates);
    } else {
        collect_expressions(design, design.vhdl_hir.expressions(),
            dependency_units, *dependencies,
            result.residual_expressions);
        if (entity != nullptr) {
            collect_generates(
                *entity, *dependencies, result.dependent_generates);
        }
        collect_generates(*selected->vhdl,
            *dependencies, result.dependent_generates);
    }
    canonicalize(result.residual_expressions);
    canonicalize(result.dependent_generates);
    return result;
}

} // namespace

std::optional<SpecializedHirOverlay> make_specialized_hir_overlay(
    const CompiledDesign& design, const UnitId selected_unit,
    const std::span<const SpecializedHirActualIdentity> actuals)
{
    if (!design.valid()) {
        return std::nullopt;
    }
    return make_specialized_hir_overlay_from_validated_design(
        design, selected_unit, actuals);
}

std::optional<SpecializedHirOverlay> make_specialized_hir_overlay(
    const CompiledDesign& design, const UnitId selected_unit,
    const std::span<const SpecializedHirNamedIdentity> canonical_actuals,
    const std::span<const SpecializedHirNamedIdentity> fallback_actuals)
{
    const auto selected = design.find_unit(selected_unit);
    if (!selected || selected->identity == nullptr) {
        return std::nullopt;
    }
    std::vector<SpecializedHirActualIdentity> actuals;
    std::set<DeclarationId> resolved;
    const auto append = [&](const auto& named_actuals) {
        for (const auto& actual : named_actuals) {
            const auto declaration = find_named_actual(
                design, *selected, actual.name);
            if (!declaration
                || !resolved.insert(*declaration).second) {
                continue;
            }
            actuals.push_back({
                *declaration, actual.identity, std::nullopt });
        }
    };
    append(canonical_actuals);
    append(fallback_actuals);
    return make_specialized_hir_overlay(
        design, selected_unit, actuals);
}

SpecializedHirAssociationResult resolve_specialized_hir_associations_impl(
    const CompiledDesign& design, const UnitId target_unit,
    const CompiledInstanceView instance,
    const SpecializedHirAssociationSurface surface,
    const SpecializedHirUnit* const parent,
    const std::vector<AssociationFormal>* const formal_override)
{
    SpecializedHirAssociationResult result;
    const auto target = design.find_unit(target_unit);
    if (!target || target->identity == nullptr || !instance) {
        result.error
            = "compiled association has no valid instance or target unit";
        return result;
    }
    if (parent != nullptr && &parent->design() != &design) {
        result.error
            = "compiled association parent belongs to another design";
        return result;
    }
    const auto formals = formal_override != nullptr
        ? *formal_override
        : association_formals(design, *target, surface);
    const auto actuals = association_actuals(instance, surface);
    const bool vhdl_names = target->language == Language::vhdl
        || instance.vhdl != nullptr;
    std::vector<bool> bound(formals.size());
    std::size_t positional { };
    bool saw_named { };
    bool saw_systemverilog_wildcard { };
    std::optional<SourceSpanId> systemverilog_wildcard_source;
    const auto reject = [&](const AssociationActual& actual,
                            const SpecializedHirAssociationDiagnostic diagnostic,
                            std::string message,
                            const std::optional<DeclarationId> formal
                                = std::nullopt,
                            std::optional<bool> callable_function
                                = std::nullopt) {
        if (result.error.empty()) {
            result.error = message;
            result.error_source = actual.source;
            result.error_formal = formal;
        }
        if (!callable_function && formal) {
            const auto declaration = design.find_declaration(*formal);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->callable) {
                callable_function
                    = declaration->vhdl->callable->function;
            }
        }
        result.issues.push_back({ diagnostic, std::move(message),
            actual.source, formal, callable_function });
    };
    for (const auto& actual : actuals) {
        const bool systemverilog_wildcard
            = surface == SpecializedHirAssociationSurface::ports
            && instance.systemverilog != nullptr && actual.formal
            && *actual.formal == "*";
        if (systemverilog_wildcard) {
            saw_named = true;
            if (saw_systemverilog_wildcard) {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::duplicate_actual,
                    "duplicate SystemVerilog wildcard port association");
                continue;
            }
            saw_systemverilog_wildcard = true;
            systemverilog_wildcard_source = actual.source;
            continue;
        }
        std::size_t formal_index = formals.size();
        if (actual.formal) {
            saw_named = true;
            std::optional<std::size_t> found_index;
            bool ambiguous { };
            for (std::size_t index { }; index < formals.size(); ++index) {
                if (!association_name_equal(
                        formals[index].name, *actual.formal, vhdl_names)) {
                    continue;
                }
                if (found_index) {
                    ambiguous = true;
                    break;
                }
                found_index = index;
            }
            if (!found_index) {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::invalid_actual,
                    "unknown compiled association formal '"
                        + std::string { *actual.formal } + "'");
                continue;
            }
            if (ambiguous) {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::ambiguous_name,
                    "compiled association formal '"
                        + std::string { *actual.formal }
                        + "' ambiguously matches multiple target formals");
                continue;
            }
            formal_index = *found_index;
        } else {
            if (saw_named) {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::association_order,
                    "positional compiled association follows a named actual");
                continue;
            }
            while (positional < bound.size() && bound[positional]) {
                ++positional;
            }
            if (positional == bound.size()) {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::invalid_actual,
                    "too many positional compiled associations");
                continue;
            }
            formal_index = positional++;
        }
        if (bound[formal_index]) {
            reject(actual,
                SpecializedHirAssociationDiagnostic::duplicate_actual,
                "duplicate compiled association for formal '"
                    + std::string { formals[formal_index].name } + "'");
            continue;
        }
        bound[formal_index] = true;
        const auto& formal = formals[formal_index];
        if (surface == SpecializedHirAssociationSurface::parameters
            && (actual.kind == SpecializedHirAssociationKind::open
                || actual.kind
                    == SpecializedHirAssociationKind::default_value)
            && !association_formal_has_default(design, formal)) {
            reject(actual,
                SpecializedHirAssociationDiagnostic::invalid_actual,
                "open/default compiled association for formal '"
                    + std::string { formal.name }
                    + "' has no default",
                formal.declaration);
            continue;
        }
        if (actual.kind == SpecializedHirAssociationKind::type
            && (surface != SpecializedHirAssociationSurface::parameters
                || !formal.type_parameter)) {
            reject(actual,
                SpecializedHirAssociationDiagnostic::
                    type_actual_for_value_parameter,
                "type actual is not legal for compiled formal '"
                    + std::string { formal.name } + "'",
                formal.declaration);
            continue;
        }
        auto effective_kind = actual.kind;
        std::optional<std::string> inferred_type_identity;
        std::optional<DeclarationId> inferred_type_declaration;
        std::optional<ExpressionId> inferred_type_expression;
        std::optional<sv::TypeReference> inferred_systemverilog_type;
        std::optional<vhdl::SubtypeIndication> inferred_vhdl_type;
        auto inferred_vhdl_type_class
            = InferredVhdlTypeClass::unknown;
        bool inferred_vhdl_type_actual { };
        if (actual.kind == SpecializedHirAssociationKind::expression
            && formal.type_parameter) {
            const auto expression = actual.expression
                ? design.find_expression(*actual.expression)
                : std::nullopt;
            const auto* systemverilog_expression
                = expression && expression->systemverilog != nullptr
                ? expression->systemverilog
                : nullptr;
            const auto* vhdl_expression
                = expression && expression->vhdl != nullptr
                ? expression->vhdl
                : nullptr;
            const auto vhdl_type_actual
                = vhdl_expression != nullptr && actual.expression
                ? infer_vhdl_type_actual(
                    design, *actual.expression, parent)
                : std::nullopt;
            const bool systemverilog_type_name
                = systemverilog_expression != nullptr
                && systemverilog_expression->kind
                    == sv::ExpressionKind::name;
            if (!systemverilog_type_name && !vhdl_type_actual) {
                bool visible_vhdl_non_type_name { };
                if (vhdl_expression != nullptr
                    && vhdl_expression->kind
                        == vhdl::ExpressionKind::name
                    && vhdl_expression->referenced_name) {
                    const auto& name = *vhdl_expression->referenced_name;
                    const auto non_type = [&](const DeclarationId id) {
                        const auto declaration = design.find_declaration(id);
                        if (!declaration
                            || declaration->vhdl == nullptr) {
                            return false;
                        }
                        const auto form = declaration->vhdl->form;
                        return form != vhdl::DeclarationForm::type
                            && form != vhdl::DeclarationForm::subtype
                            && form
                                != vhdl::DeclarationForm::generic_type;
                    };
                    visible_vhdl_non_type_name
                        = (name.selected && non_type(*name.selected))
                        || std::ranges::any_of(name.overloads, non_type);
                }
                const auto diagnostic
                    = vhdl_expression != nullptr
                        && vhdl_expression->kind
                            == vhdl::ExpressionKind::name
                        && !visible_vhdl_non_type_name
                    ? SpecializedHirAssociationDiagnostic::invalid_actual
                    : SpecializedHirAssociationDiagnostic::
                          value_actual_for_type_parameter;
                reject(actual,
                    diagnostic,
                    "value actual is not legal for compiled type formal '"
                        + std::string { formal.name } + "'",
                    formal.declaration);
                continue;
            }
            effective_kind = SpecializedHirAssociationKind::type;
            if (vhdl_type_actual) {
                inferred_vhdl_type_actual = true;
                inferred_type_identity
                    = std::move(vhdl_type_actual->identity);
                inferred_type_declaration
                    = vhdl_type_actual->declaration;
                inferred_type_expression
                    = vhdl_type_actual->expression;
                inferred_vhdl_type = vhdl_type_actual->subtype;
                inferred_vhdl_type_class
                    = vhdl_type_actual->type_class;
            } else {
                inferred_type_declaration = expression_declaration(
                    design, *actual.expression, parent);
                if (!inferred_type_declaration) {
                    const auto qualified = std::string_view {
                        systemverilog_expression->text
                    };
                    auto lookup_unit = target_unit;
                    if (systemverilog_expression->scope.valid()
                        && systemverilog_expression->scope.value()
                            < design.semantics.scopes().size()) {
                        lookup_unit = design.semantics.scopes()
                                          [systemverilog_expression
                                                  ->scope.value()]
                                              .unit;
                    }
                    inferred_type_declaration
                        = CompiledDesignResolver { design, lookup_unit,
                              parent != nullptr
                                      && &parent->design() == &design
                                  ? parent
                                  : nullptr }
                              .resolve_systemverilog_named_type(
                                  qualified,
                                  systemverilog_expression->scope, true)
                              .unique();
                }
                if (inferred_type_declaration) {
                    const auto declaration = design.find_declaration(
                        *inferred_type_declaration);
                    if (declaration
                        && declaration->systemverilog != nullptr) {
                        const auto& record = *declaration->systemverilog;
                        inferred_systemverilog_type
                            = record.form == sv::DeclarationForm::type_parameter
                                && record.default_type
                            ? record.default_type
                            : record.type;
                    }
                }
                if (inferred_systemverilog_type) {
                    inferred_type_identity = systemverilog_type_identity(
                        design, *inferred_systemverilog_type, parent);
                } else {
                    inferred_type_identity = "type-name-v1;";
                    append_identity_component(*inferred_type_identity,
                        systemverilog_expression->text);
                }
            }
        }
        if (effective_kind == SpecializedHirAssociationKind::type) {
            const auto formal_declaration = design.find_declaration(
                formal.declaration);
            if (formal_declaration
                && formal_declaration->vhdl != nullptr
                && formal_declaration->vhdl->subtype) {
                const auto actual_type_class
                    = inferred_vhdl_type_actual
                    ? inferred_vhdl_type_class
                    : actual.vhdl_type != nullptr
                    ? vhdl_subtype_class(design, *actual.vhdl_type)
                    : InferredVhdlTypeClass::unknown;
                if (!vhdl_unspecified_type_accepts(
                        formal_declaration->vhdl->subtype
                            ->unspecified_class,
                        actual_type_class)) {
                    reject(actual,
                        SpecializedHirAssociationDiagnostic::invalid_actual,
                        "compiled VHDL type actual does not satisfy the "
                        "unspecified type class for formal '"
                            + std::string { formal.name } + "'",
                        formal.declaration);
                    continue;
                }
            }
        }
        SpecializedHirAssociationBinding binding;
        binding.formal = formal.declaration;
        binding.kind = effective_kind;
        binding.expression = inferred_type_expression
            ? inferred_type_expression
            : actual.expression;
        if (actual.expression) {
            binding.actual_declaration = inferred_type_declaration
                ? inferred_type_declaration
                : expression_declaration(
                      design, *actual.expression, parent);
            const auto formal_declaration = design.find_declaration(
                formal.declaration);
            const auto expression = design.find_expression(
                *actual.expression);
            if (formal_declaration
                && formal_declaration->vhdl != nullptr
                && formal_declaration->vhdl->form
                    == vhdl::DeclarationForm::generic_package
                && expression && expression->vhdl == nullptr) {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::
                        package_language_mismatch,
                    "VHDL interface package actual for '"
                        + std::string { formal.name }
                        + "' must denote a same-language VHDL package "
                          "instance",
                    formal.declaration);
                continue;
            }
            if (formal_declaration
                && formal_declaration->vhdl != nullptr
                && formal_declaration->vhdl->callable
                && expression && expression->vhdl == nullptr) {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::
                        callable_language_mismatch,
                    "VHDL interface subprogram actual for '"
                        + std::string { formal.name }
                        + "' must denote a same-language VHDL subprogram",
                    formal.declaration,
                    formal_declaration->vhdl->callable->function);
                continue;
            }
            if (formal_declaration
                && formal_declaration->vhdl != nullptr
                && formal_declaration->vhdl->callable
                && expression && expression->vhdl != nullptr) {
                const auto& source = *expression->vhdl;
                if (source.kind != vhdl::ExpressionKind::name
                    || !source.referenced_name) {
                    reject(actual,
                        SpecializedHirAssociationDiagnostic::
                            callable_name_required,
                        "VHDL interface subprogram actual for '"
                            + std::string { formal.name }
                            + "' must be a visible subprogram name",
                        formal.declaration);
                    continue;
                }
                const auto spelling = source.referenced_name->canonical.empty()
                    ? std::string_view {
                          source.referenced_name->spelling }
                    : std::string_view {
                          source.referenced_name->canonical };
                if (spelling.find('.') != std::string_view::npos) {
                    reject(actual,
                        SpecializedHirAssociationDiagnostic::
                            callable_scoped,
                        "scoped VHDL interface subprogram actual '"
                            + std::string { spelling }
                            + "' for '"
                            + std::string { formal.name }
                            + "' is outside the bounded subset",
                        formal.declaration);
                    continue;
                }
                CompiledBindingFrame current_bindings;
                current_bindings.reserve(result.bindings.size());
                for (const auto& current : result.bindings) {
                    current_bindings.push_back({ current.formal,
                        current.expression, current.actual_declaration,
                        current.systemverilog_type, current.vhdl_type });
                }
                const std::array frames { current_bindings };
                auto lookup_unit = target_unit;
                if (source.scope.valid()
                    && source.scope.value()
                        < design.semantics.scopes().size()) {
                    lookup_unit
                        = design.semantics.scopes()[source.scope.value()].unit;
                }
                const CompiledDesignResolver resolver { design, lookup_unit,
                    parent != nullptr && &parent->design() == &design
                        ? parent
                        : nullptr,
                    frames };
                const auto visible
                    = resolver.resolve_vhdl_callable_candidates(
                        *source.referenced_name, source.scope);
                const bool function
                    = formal_declaration->vhdl->callable->function;
                std::vector<DeclarationId> same_kind;
                std::vector<CompiledVhdlCallableResolution>
                    parameter_matches;
                std::vector<CompiledVhdlCallableResolution> matches;
                bool bodyless_match { };
                for (const auto candidate_id : visible.candidates) {
                    auto candidate = parent != nullptr
                            && &parent->design() == &design
                        ? parent->find_declaration(candidate_id)
                        : std::optional<CompiledDeclarationView> { };
                    if (!candidate) {
                        candidate = design.find_declaration(candidate_id);
                    }
                    if (!candidate || candidate->vhdl == nullptr) {
                        continue;
                    }
                    const auto resolved
                        = resolver.resolve_vhdl_callable(candidate_id);
                    if (candidate->vhdl->callable) {
                        if (candidate->vhdl->callable->function
                            != function) {
                            continue;
                        }
                        same_kind.push_back(candidate_id);
                        if (resolved.candidates.empty()) {
                            bodyless_match = bodyless_match
                                || resolver.vhdl_callable_profile_matches(
                                    formal.declaration, candidate_id,
                                    true);
                            continue;
                        }
                    }
                    for (const auto& resolution : resolved.candidates) {
                        auto body = parent != nullptr
                                && &parent->design() == &design
                            ? parent->find_declaration(resolution.body)
                            : std::optional<CompiledDeclarationView> { };
                        if (!body) {
                            body = design.find_declaration(resolution.body);
                        }
                        if (!body || body->vhdl == nullptr
                            || !body->vhdl->callable
                            || body->vhdl->callable->function
                                != function) {
                            continue;
                        }
                        same_kind.push_back(candidate_id);
                        std::vector<CompiledBindingFrame>
                            candidate_frames { current_bindings };
                        if (!resolution.generic_bindings.empty()) {
                            candidate_frames.push_back(
                                resolution.generic_bindings);
                        }
                        const CompiledDesignResolver profile_resolver {
                            design, lookup_unit,
                            parent != nullptr
                                    && &parent->design() == &design
                                ? parent
                                : nullptr,
                            candidate_frames
                        };
                        const auto profile
                            = resolution.generic_bindings.empty()
                                && candidate->vhdl->callable
                            ? candidate_id
                            : resolution.body;
                        if (profile_resolver.vhdl_callable_profile_matches(
                                formal.declaration, profile,
                                false)) {
                            parameter_matches.push_back(resolution);
                        }
                        if (profile_resolver.vhdl_callable_profile_matches(
                                formal.declaration, profile,
                                true)) {
                            matches.push_back(resolution);
                        }
                    }
                }
                normalize_vhdl_callable_resolutions(parameter_matches);
                normalize_vhdl_callable_resolutions(matches);
                if (matches.empty()) {
                    if (function && !parameter_matches.empty()) {
                        reject(actual,
                            SpecializedHirAssociationDiagnostic::
                                callable_result_profile,
                            "VHDL generic function actual for '"
                                + std::string { formal.name }
                                + "' has an incompatible result subtype",
                            formal.declaration);
                    }
                    if (bodyless_match) {
                        reject(actual,
                            SpecializedHirAssociationDiagnostic::
                                callable_body_missing,
                            "VHDL interface subprogram actual '"
                                + std::string { spelling }
                                + "' has no executable body",
                            formal.declaration);
                        continue;
                    }
                    reject(actual,
                        same_kind.empty() && !visible.candidates.empty()
                            ? SpecializedHirAssociationDiagnostic::
                                  callable_wrong_kind
                            : SpecializedHirAssociationDiagnostic::
                                  callable_no_match,
                        "VHDL interface subprogram actual '"
                            + std::string { spelling }
                            + "' is not visible with a conforming profile",
                        formal.declaration);
                    continue;
                }
                if (matches.size() != 1U) {
                    reject(actual,
                        SpecializedHirAssociationDiagnostic::
                            callable_ambiguous,
                        "VHDL interface subprogram actual '"
                            + std::string { spelling }
                            + "' is ambiguous among conforming candidates",
                        formal.declaration);
                    continue;
                }
                auto selected = parent != nullptr
                        && &parent->design() == &design
                    ? parent->find_declaration(matches.front().body)
                    : std::optional<CompiledDeclarationView> { };
                if (!selected) {
                    selected = design.find_declaration(
                        matches.front().body);
                }
                if (function && selected
                    && selected->vhdl != nullptr
                    && selected->vhdl->callable
                    && !selected->vhdl->callable->pure) {
                    reject(actual,
                        SpecializedHirAssociationDiagnostic::
                            callable_impure,
                        "VHDL interface function actual '"
                            + std::string { spelling }
                            + "' must be pure",
                        formal.declaration);
                    continue;
                }
                if (!function
                    && !resolver.vhdl_procedure_is_time_free(
                        matches.front().body)) {
                    reject(actual,
                        SpecializedHirAssociationDiagnostic::
                            callable_time_dependent,
                        "VHDL interface procedure actual '"
                            + std::string { spelling }
                            + "' must be time-free and may update only "
                              "variables and formals",
                        formal.declaration);
                    continue;
                }
                binding.actual_declaration = matches.front().key;
                if (parent != nullptr
                    && &parent->design() == &design) {
                    binding.actual_declaration
                        = concrete_actual_declaration(parent,
                            *binding.actual_declaration);
                }
            }
        }
        binding.source = actual.source;
        if (effective_kind
            == SpecializedHirAssociationKind::expression) {
            if (!actual.expression) {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::invalid_actual,
                    "compiled expression actual for formal '"
                        + std::string { formal.name }
                        + "' has no expression identity");
                continue;
            }
            const auto formal_declaration = design.find_declaration(
                formal.declaration);
            if (formal_declaration
                && formal_declaration->vhdl != nullptr
                && formal_declaration->vhdl->subtype
                && parent != nullptr) {
                const auto& subtype
                    = *formal_declaration->vhdl->subtype;
                const auto value = parent->evaluate_integral_expression(
                    *actual.expression);
                bool violates_constraint = value
                    && subtype.domain == vhdl::ValueDomain::boolean
                    && *value != 0 && *value != 1;
                if (value
                    && subtype.domain == vhdl::ValueDomain::integer) {
                    for (const auto& constraint : subtype.constraints) {
                        if (!constraint.left || !constraint.right
                            || constraint.null
                            || (constraint.kind
                                    != vhdl::RangeKind::integer
                                && constraint.kind
                                    != vhdl::RangeKind::discrete)) {
                            continue;
                        }
                        const auto lower = std::min(
                            *constraint.left, *constraint.right);
                        const auto upper = std::max(
                            *constraint.left, *constraint.right);
                        violates_constraint = violates_constraint
                            || *value < lower || *value > upper;
                    }
                }
                if (violates_constraint) {
                    reject(actual,
                        SpecializedHirAssociationDiagnostic::
                            subtype_constraint,
                        "compiled value actual violates the subtype "
                        "constraint for formal '"
                            + std::string { formal.name } + "'",
                        formal.declaration);
                    continue;
                }
            }
            const bool systemverilog_string_parameter
                = surface
                    == SpecializedHirAssociationSurface::parameters
                && formal_declaration
                && formal_declaration->systemverilog != nullptr
                && formal_declaration->systemverilog->type
                && formal_declaration->systemverilog->type->value_form
                    == sv::TypeForm::string;
            const auto string_value = systemverilog_string_parameter
                    && parent != nullptr
                ? parent->evaluate_string_expression(*actual.expression)
                : std::optional<std::string> { };
            binding.identity = string_value
                ? systemverilog_string_identity(*string_value)
                : expression_identity(
                    design, *actual.expression, parent);
            if (formal_declaration
                && formal_declaration->vhdl != nullptr) {
                using Form = vhdl::DeclarationForm;
                const auto form = formal_declaration->vhdl->form;
                const auto prefix = form == Form::generic_function
                    ? std::string_view { "vhdl-function-v1;" }
                    : form == Form::generic_procedure
                    ? std::string_view { "vhdl-procedure-v1;" }
                    : form == Form::generic_package
                    ? std::string_view { "vhdl-package-v1;" }
                    : std::string_view { };
                if (!prefix.empty()) {
                    binding.identity.insert(0U, prefix);
                }
            }
        } else if (effective_kind
            == SpecializedHirAssociationKind::type) {
            if (inferred_systemverilog_type) {
                binding.systemverilog_type
                    = std::move(*inferred_systemverilog_type);
            } else if (actual.systemverilog_type != nullptr) {
                binding.systemverilog_type
                    = specialized_systemverilog_type_actual(
                        *actual.systemverilog_type, parent);
            }
            if (actual.vhdl_type != nullptr) {
                binding.vhdl_type = *actual.vhdl_type;
            } else if (inferred_vhdl_type) {
                binding.vhdl_type = std::move(*inferred_vhdl_type);
            }
            if (inferred_type_identity) {
                binding.identity = std::move(*inferred_type_identity);
            } else if (binding.systemverilog_type) {
                binding.identity = systemverilog_type_identity(
                    design, *binding.systemverilog_type, parent);
            } else if (actual.vhdl_type != nullptr) {
                binding.identity = vhdl_type_identity(
                    design, *actual.vhdl_type, parent);
            } else {
                reject(actual,
                    SpecializedHirAssociationDiagnostic::invalid_actual,
                    "compiled type actual for formal '"
                        + std::string { formal.name }
                        + "' has no type identity");
                continue;
            }
        }
        result.bindings.push_back(std::move(binding));
    }

    if (systemverilog_wildcard_source) {
        if (parent == nullptr || instance.systemverilog == nullptr) {
            AssociationActual wildcard;
            wildcard.formal = "*";
            wildcard.source = *systemverilog_wildcard_source;
            reject(wildcard,
                SpecializedHirAssociationDiagnostic::invalid_actual,
                "SystemVerilog wildcard port association has no parent "
                "specialization");
        } else {
            for (std::size_t index = 0U; index < formals.size(); ++index) {
                if (bound[index]) {
                    continue;
                }
                const auto actual = systemverilog_wildcard_actual(
                    design, *parent, instance.systemverilog->scope,
                    formals[index].name);
                if (!actual) {
                    continue;
                }
                bound[index] = true;
                SpecializedHirAssociationBinding binding;
                binding.formal = formals[index].declaration;
                binding.kind = SpecializedHirAssociationKind::expression;
                binding.actual_declaration = *actual;
                binding.identity = "declaration-v1;"
                    + std::to_string(actual->value());
                binding.source = *systemverilog_wildcard_source;
                result.bindings.push_back(std::move(binding));
            }
        }
    }

    if (surface == SpecializedHirAssociationSurface::parameters
        && instance.vhdl != nullptr) {
        const auto callable_identity = [&](const DeclarationId selected,
                                           const bool function) {
            std::string identity = function
                ? "vhdl-function-v1;"
                : "vhdl-procedure-v1;";
            const auto declaration = design.find_declaration(selected);
            if (declaration && declaration->vhdl != nullptr) {
                append_identity_component(
                    identity, declaration->vhdl->name);
                append_identity_component(identity,
                    source_identity(design, declaration->vhdl->source));
            } else {
                append_identity_component(identity, "missing-callable");
            }
            return identity;
        };
        for (std::size_t index = 0U; index < formals.size(); ++index) {
            auto existing = std::ranges::find(
                result.bindings, formals[index].declaration,
                &SpecializedHirAssociationBinding::formal);
            const bool explicit_default = existing != result.bindings.end()
                && (existing->kind
                        == SpecializedHirAssociationKind::open
                    || existing->kind
                        == SpecializedHirAssociationKind::default_value)
                && !existing->actual_declaration;
            if (bound[index] && !explicit_default) {
                continue;
            }
            const auto formal_view = design.find_declaration(
                formals[index].declaration);
            if (!formal_view || formal_view->vhdl == nullptr
                || !formal_view->vhdl->callable
                || (!formal_view->vhdl->callable->default_callable
                    && !formal_view->vhdl->callable->default_box)) {
                continue;
            }
            CompiledBindingFrame current_bindings;
            current_bindings.reserve(result.bindings.size());
            for (const auto& current : result.bindings) {
                current_bindings.push_back({ current.formal,
                    current.expression, current.actual_declaration,
                    current.systemverilog_type, current.vhdl_type });
            }
            const std::array frames { current_bindings };
            auto lookup_unit = target_unit;
            if (instance.vhdl->scope.valid()
                && instance.vhdl->scope.value()
                    < design.semantics.scopes().size()) {
                lookup_unit = design.semantics.scopes()
                                  [instance.vhdl->scope.value()]
                                      .unit;
            }
            const CompiledDesignResolver resolver { design, lookup_unit,
                parent != nullptr && &parent->design() == &design
                    ? parent
                    : nullptr,
                frames };
            const auto candidates = resolver.resolve_vhdl_default_callable(
                formals[index].declaration, instance.vhdl->scope,
                formal_view->vhdl->callable->default_callable
                    ? &*formal_view->vhdl->callable->default_callable
                    : nullptr);
            if (candidates.status
                != CompiledResolutionStatus::unique) {
                AssociationActual default_actual;
                default_actual.formal = formals[index].name;
                default_actual.source = instance.vhdl->source;
                reject(default_actual,
                    candidates.status == CompiledResolutionStatus::ambiguous
                            || formal_view->vhdl->callable->default_box
                        ? SpecializedHirAssociationDiagnostic::
                              callable_ambiguous
                        : SpecializedHirAssociationDiagnostic::
                              callable_no_match,
                    "default VHDL interface subprogram for '"
                        + std::string { formals[index].name }
                        + "' is not uniquely resolvable",
                    formals[index].declaration);
                continue;
            }
            const auto selected = candidates.candidates.front().key;
            bound[index] = true;
            const bool function = formal_view
                && formal_view->vhdl != nullptr
                && formal_view->vhdl->callable
                && formal_view->vhdl->callable->function;
            if (explicit_default) {
                existing->kind
                    = SpecializedHirAssociationKind::default_value;
                existing->actual_declaration = selected;
                existing->identity = callable_identity(
                    selected, function);
            } else {
                SpecializedHirAssociationBinding binding;
                binding.formal = formals[index].declaration;
                binding.kind = SpecializedHirAssociationKind::default_value;
                binding.actual_declaration = selected;
                binding.identity = callable_identity(
                    selected, function);
                binding.source = instance.vhdl->source;
                result.bindings.push_back(std::move(binding));
            }
        }
    }
    if (surface == SpecializedHirAssociationSurface::parameters
        && instance.systemverilog != nullptr && parent != nullptr
        && &parent->design() == &design) {
        // Parameter defaults are expressions in the child unit, but their
        // names bind to the child's already resolved formals. Evaluating each
        // default only in the parent specialization leaves dependent defaults
        // (for example, {LABEL, "!"}) stuck on the declaration-time value.
        // Build a temporary child-formal overlay and converge identities in
        // declaration order. This remains an in-memory specialization view;
        // it neither copies HIR nor creates a persistent recipe/cache layer.
        for (std::size_t pass { }; pass < result.bindings.size(); ++pass) {
            std::vector<SpecializedHirActualIdentity> local_actuals;
            local_actuals.reserve(result.bindings.size());
            for (const auto& binding : result.bindings) {
                local_actuals.push_back({
                    binding.formal,
                    binding.identity,
                    binding.actual_declaration,
                    binding.expression,
                    binding.systemverilog_type,
                    binding.vhdl_type,
                    binding.source,
                });
            }
            const auto working
                = parent->with_local_actual_identities(local_actuals);
            bool changed { };
            for (auto& binding : result.bindings) {
                if (!binding.expression) {
                    continue;
                }
                const auto formal = design.find_declaration(binding.formal);
                if (!formal || formal->systemverilog == nullptr
                    || formal->systemverilog->form
                        != sv::DeclarationForm::parameter) {
                    continue;
                }
                const bool string_parameter
                    = formal->systemverilog->type
                    && formal->systemverilog->type->value_form
                        == sv::TypeForm::string;
                const auto identity = string_parameter
                    ? [&]() -> std::optional<std::string> {
                        const auto value = working.evaluate_string_expression(
                            *binding.expression);
                        return value
                            ? std::optional {
                                  systemverilog_string_identity(*value) }
                            : std::nullopt;
                    }()
                    : [&]() -> std::optional<std::string> {
                        const auto value
                            = working.evaluate_integral_expression(
                                *binding.expression);
                        return value
                            ? std::optional { std::to_string(*value) }
                            : std::nullopt;
                    }();
                if (identity && *identity != binding.identity) {
                    binding.identity = *identity;
                    changed = true;
                }
            }
            if (!changed) {
                break;
            }
        }
    }
    if (surface == SpecializedHirAssociationSurface::parameters
        && (instance.systemverilog != nullptr
            || instance.vhdl != nullptr)) {
        for (std::size_t index = 0U; index < formals.size(); ++index) {
            if (bound[index] || !formals[index].type_parameter
                || association_formal_has_default(design, formals[index])) {
                continue;
            }
            AssociationActual missing;
            missing.formal = formals[index].name;
            missing.source = instance.systemverilog != nullptr
                ? instance.systemverilog->source
                : instance.vhdl->source;
            reject(missing,
                SpecializedHirAssociationDiagnostic::missing_type_actual,
                std::string { instance.systemverilog != nullptr
                        ? "SystemVerilog type parameter '"
                        : "VHDL interface type generic '" }
                    + std::string { formals[index].name }
                    + "' requires an actual or default",
                formals[index].declaration);
        }
    }
    return result;
}

SpecializedHirAssociationResult resolve_specialized_hir_associations(
    const CompiledDesign& design, const UnitId target_unit,
    const CompiledInstanceView instance,
    const SpecializedHirAssociationSurface surface,
    const SpecializedHirUnit* const parent)
{
    return resolve_specialized_hir_associations_impl(
        design, target_unit, instance, surface, parent, nullptr);
}

SpecializedHirAssociationResult
resolve_specialized_hir_vhdl_block_associations(
    const CompiledDesign& design,
    const vhdl::GenerateRegion& block,
    const SpecializedHirUnit& parent)
{
    std::vector<AssociationFormal> formals;
    formals.reserve(block.declarations.size());
    for (const auto declaration_id : block.declarations) {
        const auto declaration = parent.find_declaration(declaration_id);
        if (!declaration || declaration->vhdl == nullptr
            || !vhdl_actual_form(declaration->vhdl->form)) {
            continue;
        }
        formals.push_back({ declaration_id, declaration->vhdl->name,
            declaration->vhdl->form
                == vhdl::DeclarationForm::generic_type });
    }
    vhdl::Instance instance;
    instance.scope = block.scope;
    instance.generic_map = block.generic_map;
    instance.source = block.source;
    return resolve_specialized_hir_associations_impl(
        design, parent.unit(), { nullptr, &instance },
        SpecializedHirAssociationSurface::parameters,
        &parent, &formals);
}

SpecializedHirUnit::SpecializedHirUnit(const CompiledDesign& design,
    SpecializedHirOverlay specialization,
    std::vector<UnitId> replacement_units,
    const bool validated_lookup_indexes)
    : design_ { &design }
    , validated_lookup_indexes_ { validated_lookup_indexes }
    , specialization_ { std::move(specialization) }
    , replacement_units_ { std::move(replacement_units) }
{
    canonicalize(replacement_units_);
    const auto append_generate = [&](const DeclarationId declaration,
                                     const auto& instances) {
        GenerateInstances entry;
        entry.declaration = declaration;
        entry.instances.assign(instances.begin(), instances.end());
        canonicalize(entry.instances);
        generate_instances_.push_back(std::move(entry));
    };
    instance_ids_ = collect_instance_ids(design,
        specialization_.language, replacement_units_, append_generate);
    std::ranges::sort(generate_instances_, { },
        &GenerateInstances::declaration);
    for (const auto unit : replacement_units_) {
        const auto view = design.find_unit(unit);
        if (!view) {
            continue;
        }
        if (view->systemverilog != nullptr) {
            active_instance_ids_.insert(active_instance_ids_.end(),
                view->systemverilog->instances.begin(),
                view->systemverilog->instances.end());
        } else if (view->vhdl != nullptr) {
            active_instance_ids_.insert(active_instance_ids_.end(),
                view->vhdl->instances.begin(),
                view->vhdl->instances.end());
        }
    }
    canonicalize(active_instance_ids_);
    auto generate_selection = select_generates(*this, replacement_units_);
    for (const auto declaration : generate_selection.selected) {
        static_cast<void>(select_generate(declaration));
    }
    active_instance_ids_.insert(active_instance_ids_.end(),
        generate_selection.fallback_instances.begin(),
        generate_selection.fallback_instances.end());
    canonicalize(active_instance_ids_);
}

const CompiledDesign& SpecializedHirUnit::design() const noexcept
{
    return *design_;
}

const SpecializedHirOverlay&
SpecializedHirUnit::specialization() const noexcept
{
    return specialization_;
}

UnitId SpecializedHirUnit::unit() const noexcept
{
    return specialization_.unit;
}

ScopeId SpecializedHirUnit::scope() const noexcept
{
    return specialization_.scope;
}

Language SpecializedHirUnit::language() const noexcept
{
    return specialization_.language;
}

std::optional<std::int64_t>
SpecializedHirUnit::evaluate_integral_expression(
    const ExpressionId expression) const
{
    if (systemverilog_multiplication_exceeds_work_limit(
            *this, expression)) {
        return std::nullopt;
    }
    HirIntegralEvaluator evaluator { *this };
    return evaluator.evaluate(expression);
}

std::optional<bool>
SpecializedHirUnit::evaluate_systemverilog_truth_expression(
    const ExpressionId expression) const
{
    HirIntegralEvaluator evaluator { *this };
    return evaluator.evaluate_truth(expression);
}

std::optional<std::string>
SpecializedHirUnit::evaluate_systemverilog_bits_expression(
    const ExpressionId expression) const
{
    HirIntegralEvaluator evaluator { *this };
    return evaluator.evaluate_systemverilog_bits(expression);
}

SpecializedHirIntegralEvaluation
SpecializedHirUnit::evaluate_integral_expression_with_effects(
    const ExpressionId expression) const
{
    SpecializedHirIntegralEvaluation result;
    if (systemverilog_multiplication_exceeds_work_limit(
            *this, expression)) {
        return result;
    }
    HirIntegralEvaluator evaluator { *this, &result.effects };
    result.value = evaluator.evaluate(expression);
    return result;
}

std::optional<std::vector<SpecializedHirConstantEffect>>
SpecializedHirUnit::evaluate_systemverilog_program_statements(
    const std::span<const StatementId> statements) const
{
    std::vector<SpecializedHirConstantEffect> effects;
    HirIntegralEvaluator evaluator { *this, &effects,
        "FSIM-ELAB-SVPROGRAM-002", "program elaboration" };
    if (!evaluator.execute_program_statements(statements)) {
        return std::nullopt;
    }
    return effects;
}

std::optional<std::int64_t>
SpecializedHirUnit::evaluate_integral_declaration(
    const DeclarationId declaration) const
{
    HirIntegralEvaluator evaluator { *this };
    return evaluator.evaluate_declaration(declaration);
}

std::optional<std::string>
SpecializedHirUnit::evaluate_string_expression(
    const ExpressionId expression) const
{
    HirIntegralEvaluator evaluator { *this };
    return evaluator.evaluate_string(expression);
}

std::optional<std::string>
SpecializedHirUnit::evaluate_string_declaration(
    const DeclarationId declaration) const
{
    HirIntegralEvaluator evaluator { *this };
    return evaluator.evaluate_string_declaration(declaration);
}

std::optional<std::string>
SpecializedHirUnit::vhdl_declaration_identity(
    const DeclarationId declaration) const
{
    std::set<DeclarationId> active;
    auto identity = semantic::vhdl_declaration_identity(
        *design_, declaration, this, active);
    return identity.empty()
        ? std::optional<std::string> { }
        : std::optional<std::string> { std::move(identity) };
}

std::vector<UnitId>
SpecializedHirUnit::vhdl_declaration_dependency_units(
    const DeclarationId declaration) const
{
    std::set<UnitId> units;
    std::set<DeclarationId> visited;
    const auto add_matching_package_units = [&](
                                                const std::string_view library,
                                                const std::string_view name) {
        for (const auto& candidate : design_->vhdl_units()) {
            if (candidate.kind == vhdl::UnitKind::package
                && vhdl_name_equal(normalized_library(candidate.library),
                    normalized_library(library))
                && vhdl_name_equal(candidate.name, name)) {
                units.insert(candidate.id);
            }
        }
    };
    const auto scope_within = [&](ScopeId scope, const ScopeId owner) {
        std::set<ScopeId> visited_scopes;
        while (scope.valid()
            && scope.value() < design_->semantics.scopes().size()
            && visited_scopes.insert(scope).second) {
            if (scope == owner) {
                return true;
            }
            const auto parent
                = design_->semantics.scopes()[scope.value()].parent;
            if (!parent) {
                break;
            }
            scope = *parent;
        }
        return false;
    };
    std::function<void(DeclarationId)> visit;
    std::function<void(const vhdl::SubtypeIndication&)> visit_subtype;
    visit_subtype = [&](const vhdl::SubtypeIndication& subtype) {
        if (subtype.type_mark.target.valid()) {
            const auto type = find_type(subtype.type_mark.target);
            if (type && type->vhdl != nullptr) {
                visit(type->vhdl->declaration);
            }
        }
        if (subtype.resolution_function.selected) {
            visit(*subtype.resolution_function.selected);
        }
    };
    visit = [&](const DeclarationId current) {
        if (!visited.insert(current).second) {
            return;
        }
        const auto view = find_declaration(current);
        if (!view || view->vhdl == nullptr) {
            return;
        }
        const auto& record = *view->vhdl;
        if (record.scope.valid()
            && record.scope.value() < design_->semantics.scopes().size()) {
            const auto owner_id
                = design_->semantics.scopes()[record.scope.value()].unit;
            const auto owner = design_->find_unit(owner_id);
            // The declaration identity and hierarchy provenance already
            // retain a local callable's physical source. Treating its entire
            // architecture as a dependency would also retain every unrelated
            // context import. Package members, by contrast, depend on the
            // matching declaration/body pair.
            if (owner && owner->vhdl != nullptr
                && owner->vhdl->kind == vhdl::UnitKind::package) {
                add_matching_package_units(
                    owner->vhdl->library, owner->vhdl->name);
            }
        }
        if (record.completion) {
            visit(*record.completion);
        }
        if (record.subtype) {
            visit_subtype(*record.subtype);
        }
        if (record.default_type) {
            visit_subtype(*record.default_type);
        }
        if (record.callable) {
            if (record.callable->return_type) {
                visit_subtype(*record.callable->return_type);
            }
            for (const auto formal : record.callable->formals) {
                visit(formal);
            }
        }
        if (record.package) {
            const auto& package = *record.package;
            if (package.template_name.selected) {
                visit(*package.template_name.selected);
            }
            auto target = std::string_view {
                package.template_name.canonical.empty()
                    ? package.template_name.spelling
                    : package.template_name.canonical };
            auto library = std::string_view { "work" };
            auto name = target;
            if (const auto separator = target.rfind('.');
                separator != std::string_view::npos) {
                library = target.substr(0U, separator);
                name = target.substr(separator + 1U);
            }
            add_matching_package_units(library, name);
            for (const auto& association : package.generic_map) {
                if (!association.expression) {
                    continue;
                }
                const auto expression
                    = find_expression(*association.expression);
                if (expression && expression->vhdl != nullptr
                    && expression->vhdl->referenced_name
                    && expression->vhdl->referenced_name->selected) {
                    visit(
                        *expression->vhdl->referenced_name->selected);
                }
            }
        }
        if (!record.nested_scope) {
            return;
        }
        for (const auto& expression : design_->vhdl_hir.expressions()) {
            if (!scope_within(expression.scope, *record.nested_scope)) {
                continue;
            }
            for (const auto package : expression.dependencies.packages) {
                units.insert(package);
            }
            for (const auto dependency :
                expression.dependencies.parameters) {
                visit(dependency);
            }
            for (const auto dependency :
                expression.dependencies.generics) {
                visit(dependency);
            }
            for (const auto type_id : expression.dependencies.types) {
                const auto type = find_type(type_id);
                if (type && type->vhdl != nullptr) {
                    visit(type->vhdl->declaration);
                }
            }
        }
    };
    visit(declaration);
    return { units.begin(), units.end() };
}

SpecializedHirUnit SpecializedHirUnit::with_hierarchy_identities(
    const std::span<const SpecializedHirNamedIdentity> identities) const
{
    auto overlay = specialization_;
    overlay.hierarchy_identities.assign(
        identities.begin(), identities.end());
    auto result = SpecializedHirUnit {
        *design_, std::move(overlay), replacement_units_,
        validated_lookup_indexes_
    };
    result.systemverilog_declarations_ = systemverilog_declarations_;
    result.vhdl_declarations_ = vhdl_declarations_;
    result.systemverilog_types_ = systemverilog_types_;
    result.vhdl_types_ = vhdl_types_;
    result.systemverilog_expressions_ = systemverilog_expressions_;
    result.vhdl_expressions_ = vhdl_expressions_;
    result.systemverilog_statements_ = systemverilog_statements_;
    result.vhdl_statements_ = vhdl_statements_;
    result.systemverilog_processes_ = systemverilog_processes_;
    result.vhdl_processes_ = vhdl_processes_;
    result.systemverilog_instances_ = systemverilog_instances_;
    result.vhdl_instances_ = vhdl_instances_;
    return result;
}

SpecializedHirUnit SpecializedHirUnit::with_local_actual_identities(
    const std::span<const SpecializedHirActualIdentity> actuals) const
{
    auto overlay = specialization_;
    for (const auto& actual : actuals) {
        const auto existing = std::ranges::find(
            overlay.actual_identities, actual.declaration,
            &SpecializedHirActualIdentity::declaration);
        if (existing == overlay.actual_identities.end()) {
            overlay.actual_identities.push_back(actual);
        } else {
            *existing = actual;
        }
    }
    auto result = SpecializedHirUnit {
        *design_, std::move(overlay), replacement_units_,
        validated_lookup_indexes_
    };
    result.systemverilog_declarations_ = systemverilog_declarations_;
    result.vhdl_declarations_ = vhdl_declarations_;
    result.systemverilog_types_ = systemverilog_types_;
    result.vhdl_types_ = vhdl_types_;
    result.systemverilog_expressions_ = systemverilog_expressions_;
    result.vhdl_expressions_ = vhdl_expressions_;
    result.systemverilog_statements_ = systemverilog_statements_;
    result.vhdl_statements_ = vhdl_statements_;
    result.systemverilog_processes_ = systemverilog_processes_;
    result.vhdl_processes_ = vhdl_processes_;
    result.systemverilog_instances_ = systemverilog_instances_;
    result.vhdl_instances_ = vhdl_instances_;
    return result;
}

std::span<const DeclarationId>
SpecializedHirUnit::selected_generates() const noexcept
{
    return selected_generates_;
}

bool SpecializedHirUnit::select_generate(
    const DeclarationId declaration)
{
    if (!declaration_belongs_to(
            *design_, declaration, specialization_.unit)
        && std::ranges::none_of(
            replacement_units_, [&](const UnitId unit) {
                return declaration_belongs_to(
                    *design_, declaration, unit);
            })) {
        return false;
    }
    if (!is_generate_declaration(*design_, specialization_.language,
            replacement_units_, declaration)) {
        return false;
    }
    const auto found = std::ranges::lower_bound(
        selected_generates_, declaration);
    if (found != selected_generates_.end() && *found == declaration) {
        return false;
    }
    selected_generates_.insert(found, declaration);
    const auto generated = generate_instance_ids(declaration);
    selected_generate_instance_ids_.insert(
        selected_generate_instance_ids_.end(),
        generated.begin(), generated.end());
    canonicalize(selected_generate_instance_ids_);
    active_instance_ids_.insert(active_instance_ids_.end(),
        generated.begin(), generated.end());
    canonicalize(active_instance_ids_);
    return true;
}

bool SpecializedHirUnit::replace(sv::Declaration replacement)
{
    if (specialization_.language == Language::vhdl) {
        return false;
    }
    const auto original = design_->find_declaration(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->systemverilog,
            design_->semantics.declarations())) {
        return false;
    }
    return insert_replacement(
        systemverilog_declarations_, std::move(replacement));
}

bool SpecializedHirUnit::replace(vhdl::Declaration replacement)
{
    if (specialization_.language != Language::vhdl) {
        return false;
    }
    const auto original = design_->find_declaration(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->vhdl,
            design_->semantics.declarations())) {
        return false;
    }
    return insert_replacement(
        vhdl_declarations_, std::move(replacement));
}

bool SpecializedHirUnit::replace(sv::TypeDefinition replacement)
{
    if (specialization_.language == Language::vhdl) {
        return false;
    }
    const auto original = design_->find_type(replacement.id);
    if (!original || !valid_type_replacement(*design_,
            replacement_units_, replacement, original->systemverilog)) {
        return false;
    }
    return insert_replacement(
        systemverilog_types_, std::move(replacement));
}

bool SpecializedHirUnit::replace(vhdl::TypeDefinition replacement)
{
    if (specialization_.language != Language::vhdl) {
        return false;
    }
    const auto original = design_->find_type(replacement.id);
    if (!original || !valid_type_replacement(*design_,
            replacement_units_, replacement, original->vhdl)) {
        return false;
    }
    return insert_replacement(vhdl_types_, std::move(replacement));
}

bool SpecializedHirUnit::replace(sv::Expression replacement)
{
    if (specialization_.language == Language::vhdl) {
        return false;
    }
    const auto original = design_->find_expression(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->systemverilog,
            design_->semantics.expression_identities())) {
        return false;
    }
    return insert_replacement(
        systemverilog_expressions_, std::move(replacement));
}

bool SpecializedHirUnit::replace(vhdl::Expression replacement)
{
    if (specialization_.language != Language::vhdl) {
        return false;
    }
    const auto original = design_->find_expression(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->vhdl,
            design_->semantics.expression_identities())) {
        return false;
    }
    return insert_replacement(
        vhdl_expressions_, std::move(replacement));
}

bool SpecializedHirUnit::replace(sv::Statement replacement)
{
    if (specialization_.language == Language::vhdl) {
        return false;
    }
    const auto original = design_->find_statement(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->systemverilog,
            design_->semantics.statement_identities())) {
        return false;
    }
    return insert_replacement(
        systemverilog_statements_, std::move(replacement));
}

bool SpecializedHirUnit::replace(vhdl::Statement replacement)
{
    if (specialization_.language != Language::vhdl) {
        return false;
    }
    const auto original = design_->find_statement(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->vhdl,
            design_->semantics.statement_identities())) {
        return false;
    }
    return insert_replacement(
        vhdl_statements_, std::move(replacement));
}

bool SpecializedHirUnit::replace(sv::Process replacement)
{
    if (specialization_.language == Language::vhdl) {
        return false;
    }
    const auto original = design_->find_process(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->systemverilog,
            design_->semantics.process_identities())) {
        return false;
    }
    return insert_replacement(
        systemverilog_processes_, std::move(replacement));
}

bool SpecializedHirUnit::replace(vhdl::Process replacement)
{
    if (specialization_.language != Language::vhdl) {
        return false;
    }
    const auto original = design_->find_process(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->vhdl,
            design_->semantics.process_identities())) {
        return false;
    }
    return insert_replacement(vhdl_processes_, std::move(replacement));
}

bool SpecializedHirUnit::replace(sv::Instance replacement)
{
    if (specialization_.language == Language::vhdl) {
        return false;
    }
    const auto original = design_->find_instance(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->systemverilog,
            design_->semantics.instances())) {
        return false;
    }
    return insert_replacement(
        systemverilog_instances_, std::move(replacement));
}

bool SpecializedHirUnit::replace(vhdl::Instance replacement)
{
    if (specialization_.language != Language::vhdl) {
        return false;
    }
    const auto original = design_->find_instance(replacement.id);
    if (!original || !valid_scoped_replacement(*design_,
            replacement_units_, replacement, original->vhdl,
            design_->semantics.instances())) {
        return false;
    }
    return insert_replacement(vhdl_instances_, std::move(replacement));
}

std::optional<CompiledDeclarationView>
SpecializedHirUnit::find_declaration(const DeclarationId id) const noexcept
{
    if (specialization_.language == Language::system_verilog) {
        if (const auto* replacement = find_replacement(
                systemverilog_declarations_, id)) {
            return CompiledDeclarationView { replacement, nullptr };
        }
    } else {
        if (const auto* replacement = find_replacement(
                vhdl_declarations_, id)) {
            return CompiledDeclarationView { nullptr, replacement };
        }
    }
    if (!validated_lookup_indexes_
        && !design_->lookup_indexes_current()) {
        return design_->find_declaration(id);
    }
    const auto indexed = design_->indexed_declaration(id);
    return indexed
        ? std::optional<CompiledDeclarationView> { indexed }
        : std::nullopt;
}

std::optional<CompiledTypeView>
SpecializedHirUnit::find_type(const TypeId id) const noexcept
{
    if (specialization_.language == Language::system_verilog) {
        if (const auto* replacement = find_replacement(
                systemverilog_types_, id)) {
            return CompiledTypeView { replacement, nullptr };
        }
    } else {
        if (const auto* replacement = find_replacement(vhdl_types_, id)) {
            return CompiledTypeView { nullptr, replacement };
        }
    }
    if (!validated_lookup_indexes_
        && !design_->lookup_indexes_current()) {
        return design_->find_type(id);
    }
    const auto indexed = design_->indexed_type(id);
    return indexed
        ? std::optional<CompiledTypeView> { indexed }
        : std::nullopt;
}

std::optional<CompiledExpressionView>
SpecializedHirUnit::find_expression(const ExpressionId id) const noexcept
{
    if (specialization_.language == Language::system_verilog) {
        if (const auto* replacement = find_replacement(
                systemverilog_expressions_, id)) {
            return CompiledExpressionView { replacement, nullptr };
        }
    } else {
        if (const auto* replacement = find_replacement(
                vhdl_expressions_, id)) {
            return CompiledExpressionView { nullptr, replacement };
        }
    }
    if (!validated_lookup_indexes_
        && !design_->lookup_indexes_current()) {
        return design_->find_expression(id);
    }
    const auto indexed = design_->indexed_expression(id);
    return indexed
        ? std::optional<CompiledExpressionView> { indexed }
        : std::nullopt;
}

std::optional<CompiledStatementView>
SpecializedHirUnit::find_statement(const StatementId id) const noexcept
{
    if (specialization_.language == Language::system_verilog) {
        if (const auto* replacement = find_replacement(
                systemverilog_statements_, id)) {
            return CompiledStatementView { replacement, nullptr };
        }
    } else {
        if (const auto* replacement = find_replacement(
                vhdl_statements_, id)) {
            return CompiledStatementView { nullptr, replacement };
        }
    }
    if (!validated_lookup_indexes_
        && !design_->lookup_indexes_current()) {
        return design_->find_statement(id);
    }
    const auto indexed = design_->indexed_statement(id);
    return indexed
        ? std::optional<CompiledStatementView> { indexed }
        : std::nullopt;
}

std::optional<CompiledProcessView>
SpecializedHirUnit::find_process(const ProcessId id) const noexcept
{
    if (specialization_.language == Language::system_verilog) {
        if (const auto* replacement = find_replacement(
                systemverilog_processes_, id)) {
            return CompiledProcessView { replacement, nullptr };
        }
    } else {
        if (const auto* replacement = find_replacement(
                vhdl_processes_, id)) {
            return CompiledProcessView { nullptr, replacement };
        }
    }
    if (!validated_lookup_indexes_
        && !design_->lookup_indexes_current()) {
        return design_->find_process(id);
    }
    const auto indexed = design_->indexed_process(id);
    return indexed
        ? std::optional<CompiledProcessView> { indexed }
        : std::nullopt;
}

std::optional<CompiledInstanceView>
SpecializedHirUnit::find_instance(const InstanceId id) const noexcept
{
    if (!std::ranges::binary_search(instance_ids_, id)) {
        return std::nullopt;
    }
    if (specialization_.language == Language::system_verilog) {
        if (const auto* replacement = find_replacement(
                systemverilog_instances_, id)) {
            return CompiledInstanceView { replacement, nullptr };
        }
    } else {
        if (const auto* replacement = find_replacement(
                vhdl_instances_, id)) {
            return CompiledInstanceView { nullptr, replacement };
        }
    }
    if (!validated_lookup_indexes_
        && !design_->lookup_indexes_current()) {
        return design_->find_instance(id);
    }
    const auto indexed = design_->indexed_instance(id);
    return indexed
        ? std::optional<CompiledInstanceView> { indexed }
        : std::nullopt;
}

std::optional<CompiledInstanceView>
SpecializedHirUnit::find_instance(const ScopeId scope,
    const std::string_view name) const noexcept
{
    for (const auto id : instance_ids_) {
        const auto view = find_instance(id);
        if (!view) {
            continue;
        }
        if (view->systemverilog != nullptr
            && view->systemverilog->scope == scope
            && view->systemverilog->name == name) {
            return view;
        }
        if (view->vhdl != nullptr && view->vhdl->scope == scope
            && vhdl_name_equal(view->vhdl->name, name)) {
            return view;
        }
    }
    return std::nullopt;
}

std::span<const InstanceId>
SpecializedHirUnit::instance_ids() const noexcept
{
    return instance_ids_;
}

std::vector<CompiledInstanceView> SpecializedHirUnit::instances() const
{
    std::vector<CompiledInstanceView> result;
    result.reserve(instance_ids_.size());
    for (const auto id : instance_ids_) {
        if (const auto view = find_instance(id)) {
            result.push_back(*view);
        }
    }
    return result;
}

std::span<const InstanceId>
SpecializedHirUnit::active_instance_ids() const noexcept
{
    return active_instance_ids_;
}

std::vector<CompiledInstanceView>
SpecializedHirUnit::active_instances() const
{
    std::vector<CompiledInstanceView> result;
    result.reserve(active_instance_ids_.size());
    for (const auto id : active_instance_ids_) {
        if (const auto view = find_instance(id)) {
            result.push_back(*view);
        }
    }
    return result;
}

std::span<const InstanceId>
SpecializedHirUnit::generate_instance_ids(
    const DeclarationId declaration) const noexcept
{
    const auto found = std::ranges::lower_bound(generate_instances_,
        declaration, { }, &GenerateInstances::declaration);
    if (found == generate_instances_.end()
        || found->declaration != declaration) {
        return { };
    }
    return found->instances;
}

std::span<const InstanceId>
SpecializedHirUnit::selected_generate_instance_ids() const noexcept
{
    return selected_generate_instance_ids_;
}

std::vector<CompiledInstanceView>
SpecializedHirUnit::generate_instances(
    const DeclarationId declaration) const
{
    std::vector<CompiledInstanceView> result;
    const auto ids = generate_instance_ids(declaration);
    result.reserve(ids.size());
    for (const auto id : ids) {
        if (const auto view = find_instance(id)) {
            result.push_back(*view);
        }
    }
    return result;
}

std::vector<CompiledInstanceView>
SpecializedHirUnit::selected_generate_instances() const
{
    std::vector<CompiledInstanceView> result;
    result.reserve(selected_generate_instance_ids_.size());
    for (const auto id : selected_generate_instance_ids_) {
        if (const auto view = find_instance(id)) {
            result.push_back(*view);
        }
    }
    return result;
}

std::span<const sv::Declaration>
SpecializedHirUnit::systemverilog_declarations() const noexcept
{
    return systemverilog_declarations_;
}

std::span<const vhdl::Declaration>
SpecializedHirUnit::vhdl_declarations() const noexcept
{
    return vhdl_declarations_;
}

std::span<const sv::TypeDefinition>
SpecializedHirUnit::systemverilog_types() const noexcept
{
    return systemverilog_types_;
}

std::span<const vhdl::TypeDefinition>
SpecializedHirUnit::vhdl_types() const noexcept
{
    return vhdl_types_;
}

std::span<const sv::Expression>
SpecializedHirUnit::systemverilog_expressions() const noexcept
{
    return systemverilog_expressions_;
}

std::span<const vhdl::Expression>
SpecializedHirUnit::vhdl_expressions() const noexcept
{
    return vhdl_expressions_;
}

std::span<const sv::Statement>
SpecializedHirUnit::systemverilog_statements() const noexcept
{
    return systemverilog_statements_;
}

std::span<const vhdl::Statement>
SpecializedHirUnit::vhdl_statements() const noexcept
{
    return vhdl_statements_;
}

std::span<const sv::Process>
SpecializedHirUnit::systemverilog_processes() const noexcept
{
    return systemverilog_processes_;
}

std::span<const vhdl::Process>
SpecializedHirUnit::vhdl_processes() const noexcept
{
    return vhdl_processes_;
}

std::span<const sv::Instance>
SpecializedHirUnit::systemverilog_instances() const noexcept
{
    return systemverilog_instances_;
}

std::span<const vhdl::Instance>
SpecializedHirUnit::vhdl_instances() const noexcept
{
    return vhdl_instances_;
}

std::optional<SpecializedHirUnit> make_specialized_hir_unit(
    const CompiledDesign& design, const UnitId selected_unit,
    const std::span<const SpecializedHirActualIdentity> actuals)
{
    return working_specialization(design,
        make_specialized_hir_overlay(
            design, selected_unit, actuals), false);
}

std::optional<SpecializedHirUnit> make_specialized_hir_unit(
    const CompiledDesign& design, const UnitId selected_unit,
    const std::span<const SpecializedHirNamedIdentity> canonical_actuals,
    const std::span<const SpecializedHirNamedIdentity> fallback_actuals)
{
    return working_specialization(design,
        make_specialized_hir_overlay(design, selected_unit,
            canonical_actuals, fallback_actuals), false);
}

std::optional<SpecializedHirUnit> make_specialized_hir_unit(
    const ValidatedCompiledDesign& validated,
    const UnitId selected_unit,
    const std::span<const SpecializedHirActualIdentity> actuals)
{
    const auto& design = validated.design();
    return working_specialization(design,
        make_specialized_hir_overlay_from_validated_design(
            design, selected_unit, actuals), true);
}

std::optional<SpecializedHirUnit> make_specialized_hir_unit(
    const ValidatedCompiledDesign& validated,
    const UnitId selected_unit,
    const std::span<const SpecializedHirNamedIdentity> canonical_actuals,
    const std::span<const SpecializedHirNamedIdentity> fallback_actuals)
{
    const auto& design = validated.design();
    const auto selected = design.find_unit(selected_unit);
    if (!selected || selected->identity == nullptr) {
        return std::nullopt;
    }
    std::vector<SpecializedHirActualIdentity> actuals;
    std::set<DeclarationId> resolved;
    const auto append = [&](const auto& named_actuals) {
        for (const auto& actual : named_actuals) {
            const auto declaration = find_named_actual(
                design, *selected, actual.name);
            if (!declaration
                || !resolved.insert(*declaration).second) {
                continue;
            }
            actuals.push_back({
                *declaration, actual.identity, std::nullopt });
        }
    };
    append(canonical_actuals);
    append(fallback_actuals);
    return make_specialized_hir_unit(
        validated, selected_unit, actuals);
}

} // namespace fsim::semantic
