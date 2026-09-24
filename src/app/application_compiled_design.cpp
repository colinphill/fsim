// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_design_artifact_codec_internal.hpp"

#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/path.hpp"

#include <boost/pfr/core.hpp>

namespace fsim::app::application_detail {
namespace {

struct SourceIdentity {
    std::string physical_name;
    std::string content_digest;

    friend bool operator==(const SourceIdentity&, const SourceIdentity&)
        = default;
};

struct SourceMappingIndex {
    std::vector<library::SourceNameMapping> ordered;
    std::map<std::string, std::string, std::less<>> producers;
    std::set<std::string, std::less<>> logical_names;
};

constexpr std::string_view kPendingAssertionMarker {
    "\x1f"
    "fsim.concurrent-assertion-pending|"
};

std::string normalized_source_name(const std::string_view name)
{
    return source_path_key(support::path_from_utf8(name));
}

std::optional<SourceMappingIndex> prepare_source_mappings(
    const std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics)
{
    SourceMappingIndex result;
    result.ordered.reserve(mappings.size());
    for (const auto& mapping : mappings) {
        const auto producer = normalized_source_name(mapping.producer_name);
        const auto logical = normalized_source_name(mapping.logical_name);
        const auto producer_inserted = !producer.empty()
            && result.producers.try_emplace(
                producer, mapping.logical_name).second;
        const auto logical_inserted = !logical.empty()
            && result.logical_names.insert(logical).second;
        if (!producer_inserted || !logical_inserted) {
            diagnostics.error(
                "FSIM-ART-HIR-001",
                "compiled-HIR source relocation mapping is not bijective");
            return std::nullopt;
        }
        result.ordered.push_back({
            support::path_to_utf8(
                support::path_from_utf8(mapping.producer_name)
                    .lexically_normal()),
            support::path_to_utf8(
                support::path_from_utf8(mapping.logical_name)
                    .lexically_normal())
        });
    }
    return result;
}

bool is_embedded_source_boundary(const char value)
{
    switch (value) {
    case ' ':
    case '\'':
    case '"':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '<':
    case '>':
    case ',':
    case ':':
        return true;
    default:
        return false;
    }
}

bool decimal_location_component(const std::string_view value)
{
    return !value.empty()
        && std::ranges::all_of(value, [](const unsigned char character) {
            return std::isdigit(character) != 0;
        });
}

std::optional<std::filesystem::path> source_path_from_location(
    const std::string_view location)
{
    const auto column_separator = location.rfind(':');
    if (column_separator == std::string_view::npos
        || !decimal_location_component(
            location.substr(column_separator + 1U))) {
        return std::nullopt;
    }
    const auto line_separator = location.rfind(':', column_separator - 1U);
    if (line_separator == std::string_view::npos
        || !decimal_location_component(location.substr(
            line_separator + 1U,
            column_separator - line_separator - 1U))) {
        return std::nullopt;
    }
    return support::path_from_utf8(location.substr(0, line_separator));
}

std::vector<std::filesystem::path> expansion_source_paths(
    const std::string_view description)
{
    std::vector<std::filesystem::path> result;
    constexpr std::string_view included_marker { "included '" };
    if (const auto included = description.find(included_marker);
        included != std::string_view::npos) {
        const auto begin = included + included_marker.size();
        if (const auto end = description.find('\'', begin);
            end != std::string_view::npos) {
            result.push_back(
                support::path_from_utf8(description.substr(begin, end - begin)));
        }
    }
    constexpr std::array location_markers {
        std::string_view { " from " },
        std::string_view { " defined at " },
        std::string_view { ", expanded at " },
    };
    for (const auto marker : location_markers) {
        std::size_t search_offset = 0;
        while (true) {
            const auto marker_offset
                = description.find(marker, search_offset);
            if (marker_offset == std::string_view::npos) {
                break;
            }
            const auto begin = marker_offset + marker.size();
            auto end = description.size();
            if (marker == " defined at ") {
                const auto expanded = description.find(
                    ", expanded at ", begin);
                if (expanded != std::string_view::npos) {
                    end = expanded;
                }
            }
            if (const auto path = source_path_from_location(
                    description.substr(begin, end - begin))) {
                result.push_back(*path);
            }
            search_offset = begin;
        }
    }
    return result;
}

template <typename T>
struct IsVector : std::false_type { };

template <typename T, typename Allocator>
struct IsVector<std::vector<T, Allocator>> : std::true_type { };

template <typename T>
struct IsOptional : std::false_type { };

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type { };

template <typename T>
struct IsPair : std::false_type { };

template <typename First, typename Second>
struct IsPair<std::pair<First, Second>> : std::true_type { };

template <typename Value>
void relocate_generated_text(
    Value& value,
    const std::span<const std::string> producer_source_names,
    const std::span<const semantic::SourceSpan> relocated_spans,
    bool& valid);

std::optional<std::pair<std::string_view, std::string_view>>
generated_text_source_names(
    const semantic::SourceSpanId source,
    const std::span<const std::string> producer_source_names,
    const std::span<const semantic::SourceSpan> relocated_spans)
{
    if (!source.valid() || source.value() >= producer_source_names.size()
        || source.value() >= relocated_spans.size()) {
        return std::nullopt;
    }
    return std::pair {
        std::string_view { producer_source_names[source.value()] },
        std::string_view { relocated_spans[source.value()].logical_name }
    };
}

void replace_all(
    std::string& value,
    const std::string_view producer,
    const std::string_view relocated)
{
    if (producer.empty() || producer == relocated) {
        return;
    }
    std::size_t offset = 0;
    while ((offset = value.find(producer, offset)) != std::string::npos) {
        value.replace(offset, producer.size(), relocated);
        offset += relocated.size();
    }
}

template <typename Value>
bool contains_relocatable_generated_text(const Value& value)
{
    using Type = std::remove_cvref_t<Value>;
    if constexpr (std::same_as<Type, semantic::sv::SourceToken>) {
        return value.generated_text
            != semantic::sv::GeneratedTextKind::none;
    } else if constexpr (std::same_as<Type, semantic::sv::Expression>) {
        return value.generated_text
            != semantic::sv::GeneratedTextKind::none;
    } else if constexpr (std::same_as<Type, semantic::sv::Statement>) {
        return value.output_generated_text
                != semantic::sv::GeneratedTextKind::none
            || value.output_text.starts_with(kPendingAssertionMarker);
    } else if constexpr (IsVector<Type>::value) {
        return std::ranges::any_of(value, [](const auto& item) {
            return contains_relocatable_generated_text(item);
        });
    } else if constexpr (IsOptional<Type>::value) {
        return value && contains_relocatable_generated_text(*value);
    } else if constexpr (IsPair<Type>::value) {
        return contains_relocatable_generated_text(value.first)
            || contains_relocatable_generated_text(value.second);
    } else if constexpr (std::is_aggregate_v<Type>) {
        bool found = false;
        boost::pfr::for_each_field(value, [&](const auto& field) {
            found = found || contains_relocatable_generated_text(field);
        });
        return found;
    } else {
        return false;
    }
}

template <typename T, typename Visitor>
void visit_projected_generated_records(
    codec_detail::ProjectedHirRecords<T>& records, Visitor&& visitor)
{
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (contains_relocatable_generated_text(records.at(index))) {
            std::invoke(visitor, records.project(index));
        }
    }
}

bool relocate_generated_literal(
    std::string& spelling,
    std::optional<std::string>* decoded,
    const semantic::sv::GeneratedTextKind kind,
    const semantic::SourceSpanId source,
    const std::span<const std::string> producer_source_names,
    const std::span<const semantic::SourceSpan> relocated_spans)
{
    if (kind == semantic::sv::GeneratedTextKind::none) {
        return true;
    }
    const auto names = generated_text_source_names(
        source, producer_source_names, relocated_spans);
    if (!names) {
        return false;
    }
    std::string value;
    if (kind == semantic::sv::GeneratedTextKind::systemverilog_file_macro) {
        value = names->second;
    } else {
        if (decoded && *decoded) {
            value = **decoded;
        } else {
            auto decoded_spelling
                = frontend::decode_systemverilog_string_literal(spelling);
            if (!decoded_spelling) {
                return false;
            }
            value = std::move(*decoded_spelling);
        }
        replace_all(value, names->first, names->second);
    }
    spelling = frontend::systemverilog_string_literal_spelling(value);
    if (decoded) {
        *decoded = std::move(value);
    }
    return true;
}

void relocate_generated_statement(
    semantic::sv::Statement& statement,
    const std::span<const std::string> producer_source_names,
    const std::span<const semantic::SourceSpan> relocated_spans,
    bool& valid)
{
    if (statement.output_generated_text
        == semantic::sv::GeneratedTextKind::none) {
        return;
    }
    const auto names = generated_text_source_names(
        statement.source, producer_source_names, relocated_spans);
    if (!names) {
        valid = false;
        return;
    }
    replace_all(statement.output_text, names->first, names->second);
    replace_all(statement.output_prefix, names->first, names->second);
    replace_all(statement.output_suffix, names->first, names->second);
    replace_all(statement.output_trailing_text, names->first, names->second);
    for (auto& output : statement.output_values) {
        replace_all(output.prefix, names->first, names->second);
    }
}

template <typename Value>
void relocate_generated_text(
    Value& value,
    const std::span<const std::string> producer_source_names,
    const std::span<const semantic::SourceSpan> relocated_spans,
    bool& valid)
{
    using Type = std::remove_cvref_t<Value>;
    if constexpr (std::same_as<Type, semantic::sv::SourceToken>) {
        valid = relocate_generated_literal(
                    value.text, nullptr, value.generated_text, value.source,
                    producer_source_names, relocated_spans)
            && valid;
    } else if constexpr (std::same_as<Type, semantic::sv::Expression>) {
        valid = relocate_generated_literal(
                    value.text, &value.decoded_string,
                    value.generated_text, value.source,
                    producer_source_names, relocated_spans)
            && valid;
    } else if constexpr (std::same_as<Type, semantic::sv::Statement>) {
        relocate_generated_statement(value, producer_source_names,
            relocated_spans, valid);
    } else if constexpr (IsVector<Type>::value) {
        for (auto& item : value) {
            relocate_generated_text(item, producer_source_names,
                relocated_spans, valid);
        }
    } else if constexpr (IsOptional<Type>::value) {
        if (value) {
            relocate_generated_text(*value, producer_source_names,
                relocated_spans, valid);
        }
    } else if constexpr (IsPair<Type>::value) {
        relocate_generated_text(value.first, producer_source_names,
            relocated_spans, valid);
        relocate_generated_text(value.second, producer_source_names,
            relocated_spans, valid);
    } else if constexpr (std::is_aggregate_v<Type>) {
        boost::pfr::for_each_field(value, [&](auto& field) {
            relocate_generated_text(field, producer_source_names,
                relocated_spans, valid);
        });
    }
}

bool relocate_embedded_source_names(
    std::string& name,
    const SourceMappingIndex& mappings)
{
    const auto find_source = [&](const std::string_view producer,
                                 const std::size_t begin) {
#if defined(_WIN32)
        for (auto offset = begin;
             offset + producer.size() <= name.size(); ++offset) {
            const auto matches = std::ranges::equal(
                producer,
                std::string_view { name }.substr(offset, producer.size()),
                [](const char left, const char right) {
                    const auto fold = [](const char character) {
                        return character >= 'A' && character <= 'Z'
                            ? static_cast<char>(character - 'A' + 'a')
                            : character;
                    };
                    return fold(left) == fold(right);
                });
            if (matches) {
                return offset;
            }
        }
        return std::string::npos;
#else
        return name.find(producer, begin);
#endif
    };
    bool relocated = false;
    for (const auto& mapping : mappings.ordered) {
        const auto& producer = mapping.producer_name;
        const auto& logical = mapping.logical_name;
        std::size_t offset = 0;
        while (!producer.empty()
            && (offset = find_source(producer, offset)) != std::string::npos) {
            const auto end = offset + producer.size();
            const auto bounded_before = offset == 0
                || is_embedded_source_boundary(name[offset - 1]);
            const auto bounded_after = end == name.size()
                || is_embedded_source_boundary(name[end]);
            if (!bounded_before || !bounded_after) {
                offset = end;
                continue;
            }
            name.replace(offset, producer.size(), logical);
            offset += logical.size();
            relocated = true;
        }
    }
    return relocated;
}

bool relocate_equivalent_embedded_source_names(
    std::string& name,
    const SourceMappingIndex& mappings)
{
    bool relocated = false;
    for (const auto& embedded : expansion_source_paths(name)) {
        const auto mapping = std::ranges::find_if(
            mappings.ordered, [&](const auto& candidate) {
                return same_source_path(embedded,
                    support::path_from_utf8(candidate.producer_name));
            });
        if (mapping == mappings.ordered.end()) {
            continue;
        }
        const auto encoded = support::path_to_utf8(embedded);
        std::size_t offset = 0;
        while (!encoded.empty()
            && (offset = name.find(encoded, offset)) != std::string::npos) {
            name.replace(offset, encoded.size(), mapping->logical_name);
            offset += mapping->logical_name.size();
            relocated = true;
        }
    }
    return relocated;
}

struct SourceNameProjection {
    std::string value;
    bool valid { true };
};

struct ProjectedSourceNames {
    std::vector<std::string> values;
    bool valid { true };
};

struct ProjectedSemanticSourceRecords {
    semantic::ModelRecords records;
    std::vector<SourceIdentity> source_identities;
    bool valid { true };
};

bool validate_relocated_source_identities(
    const std::span<const SourceIdentity> source_identities,
    const std::span<const semantic::SourceFile> relocated_sources,
    diagnostic::Engine& diagnostics)
{
    if (source_identities.size() != relocated_sources.size()) {
        return false;
    }
    std::map<std::string, SourceIdentity, std::less<>> unique_sources;
    for (std::size_t index = 0; index < relocated_sources.size(); ++index) {
        const auto& file = relocated_sources[index];
        const auto [position, inserted] = unique_sources.try_emplace(
            normalized_source_name(file.physical_name), source_identities[index]);
        if (!inserted && position->second != source_identities[index]) {
            diagnostics.error(
                "FSIM-ART-HIR-001",
                "compiled-HIR source relocation produced colliding source "
                "identities for '"
                    + file.physical_name + "'");
            return false;
        }
    }
    return true;
}

SourceNameProjection project_source_name(
    const std::string_view name,
    const SourceMappingIndex& mappings)
{
    if (name.empty()) {
        return { std::string { name }, true };
    }
    const auto path = normalized_source_name(name);
    if (const auto mapping = mappings.producers.find(path);
        mapping != mappings.producers.end()) {
        return { mapping->second, true };
    }
    const auto native_name = support::path_from_utf8(name);
    const auto equivalent = std::ranges::find_if(
        mappings.ordered, [&](const auto& mapping) {
            return same_source_path(native_name,
                support::path_from_utf8(mapping.producer_name));
        });
    if (equivalent != mappings.ordered.end()) {
        return { equivalent->logical_name, true };
    }
    std::string projected { name };
    (void)relocate_embedded_source_names(projected, mappings);
    (void)relocate_equivalent_embedded_source_names(projected, mappings);
    const auto relocated_path = std::filesystem::path { projected };
    const auto mapped_destination = [&](const auto& candidate) {
        return mappings.logical_names.contains(
            source_path_key(candidate));
    };
    const auto embedded_absolute = std::ranges::any_of(
        expansion_source_paths(projected), [&](const auto& embedded) {
            return support::path_is_portably_absolute(embedded)
                && !mapped_destination(embedded);
        });
    if ((!support::path_is_portably_absolute(relocated_path)
            || mapped_destination(relocated_path))
        && !embedded_absolute) {
        return { std::move(projected), true };
    }
    return { std::move(projected), false };
}

bool relocate_name(
    std::string& name,
    const SourceMappingIndex& mappings,
    diagnostic::Engine& diagnostics)
{
    auto projection = project_source_name(name, mappings);
    name = std::move(projection.value);
    if (projection.valid) {
        return true;
    }
    diagnostics.error(
        "FSIM-ART-HIR-001",
        "compiled HIR retains an unmapped producer source path: " + name);
    return false;
}

std::vector<std::string> split_fields(
    const std::string_view value, const char separator)
{
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(separator, begin);
        fields.emplace_back(value.substr(begin,
            end == std::string_view::npos
                ? value.size() - begin
                : end - begin));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return fields;
}

std::optional<std::string> decode_hex(const std::string_view encoded)
{
    const auto nibble = [](const char character) -> std::optional<unsigned> {
        if (character >= '0' && character <= '9') {
            return static_cast<unsigned>(character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<unsigned>(character - 'a' + 10);
        }
        return std::nullopt;
    };
    if (encoded.size() % 2U != 0U) {
        return std::nullopt;
    }
    std::string result;
    result.reserve(encoded.size() / 2U);
    for (std::size_t index = 0; index < encoded.size(); index += 2U) {
        const auto high = nibble(encoded[index]);
        const auto low = nibble(encoded[index + 1U]);
        if (!high || !low) {
            return std::nullopt;
        }
        result.push_back(static_cast<char>((*high << 4U) | *low));
    }
    return result;
}

std::string encode_hex(const std::string_view value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2U);
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

std::string join_fields(
    const std::span<const std::string> fields, const char separator)
{
    std::string result;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            result.push_back(separator);
        }
        result += fields[index];
    }
    return result;
}

bool relocate_pending_assertion_marker(
    semantic::sv::Statement& statement,
    const SourceMappingIndex& mappings,
    diagnostic::Engine& diagnostics)
{
    if (!statement.output_text.starts_with(kPendingAssertionMarker)) {
        return true;
    }
    const auto malformed = [&] {
        diagnostics.error(
            "FSIM-ART-HIR-001",
            "compiled HIR contains a malformed pending assertion action");
        return false;
    };
    auto fields = split_fields(
        std::string_view { statement.output_text }
            .substr(kPendingAssertionMarker.size()), '|');
    if (fields.size() < 4U) {
        return malformed();
    }
    for (std::size_t index = 4; index < fields.size(); ++index) {
        auto action = split_fields(fields[index], ',');
        if (action.size() != 8U) {
            return malformed();
        }
        auto message = decode_hex(action[3]);
        auto path = decode_hex(action[4]);
        std::uint32_t generated_value { };
        const auto generated_result = std::from_chars(
            action[7].data(), action[7].data() + action[7].size(),
            generated_value);
        const auto generated = static_cast<semantic::sv::GeneratedTextKind>(
            generated_value);
        if (!message || !path || generated_result.ec != std::errc { }
            || generated_result.ptr != action[7].data() + action[7].size()
            || !semantic::sv::generated_text_kind_valid(generated)) {
            return malformed();
        }
        const auto producer_path = *path;
        if (!relocate_name(*path, mappings, diagnostics)) {
            return false;
        }
        if (generated
            == semantic::sv::GeneratedTextKind::systemverilog_file_macro) {
            *message = *path;
        } else if (generated == semantic::sv::GeneratedTextKind::
                                    systemverilog_file_macro_derived) {
            replace_all(*message, producer_path, *path);
        }
        action[3] = encode_hex(*message);
        action[4] = encode_hex(*path);
        fields[index] = join_fields(action, ',');
    }
    statement.output_text
        = std::string { kPendingAssertionMarker } + join_fields(fields, '|');
    return true;
}

std::optional<std::filesystem::path> relative_within_base(
    const std::filesystem::path& path,
    const std::filesystem::path& base_directory)
{
    if (path.empty() || base_directory.empty()) {
        return std::nullopt;
    }
    const auto normalized
        = support::detail::normalized_absolute_path(path);
    const auto normalized_base
        = support::detail::normalized_absolute_path(base_directory);
    auto path_component = normalized.begin();
    auto base_component = normalized_base.begin();
    while (path_component != normalized.end()
        && base_component != normalized_base.end()
        && source_path_key(*path_component)
            == source_path_key(*base_component)) {
        ++path_component;
        ++base_component;
    }
    if (base_component != normalized_base.end()) {
        return std::nullopt;
    }
    std::filesystem::path relative;
    for (; path_component != normalized.end(); ++path_component) {
        relative /= *path_component;
    }
    return relative.empty() ? std::filesystem::path { "." }
                            : std::move(relative);
}

std::string fallback_logical_name(
    const std::string_view category,
    const std::string_view digest,
    const std::filesystem::path& path)
{
    auto result = std::filesystem::path { "cache-sources" }
        / std::string { category };
    if (!digest.empty()) {
        result /= std::string { digest };
    }
    const auto filename = path.filename();
    result /= filename.empty() ? std::filesystem::path { "source" }
                               : filename;
    return support::path_to_utf8(result);
}

ProjectedSourceNames project_source_names(
    const std::span<const std::string_view> names,
    const SourceMappingIndex& mappings,
    diagnostic::Engine& diagnostics)
{
    ProjectedSourceNames projected_names;
    projected_names.values.reserve(names.size());
    for (const auto name : names) {
        auto projection = project_source_name(name, mappings);
        if (!projection.valid) {
            diagnostics.error(
                "FSIM-ART-HIR-001",
                "compiled HIR retains an unmapped producer source path: "
                    + projection.value);
            projected_names.valid = false;
        }
        projected_names.values.push_back(std::move(projection.value));
    }
    return projected_names;
}

ProjectedSemanticSourceRecords project_semantic_source_names(
    const semantic::Model& semantics,
    const SourceMappingIndex& mappings,
    diagnostic::Engine& diagnostics)
{
    ProjectedSemanticSourceRecords projected_semantics;
    projected_semantics.records = semantics.records();
    auto& records = projected_semantics.records;
    auto& source_identities = projected_semantics.source_identities;
    source_identities.reserve(records.source_files.size());
    for (const auto& file : records.source_files) {
        source_identities.push_back({
            normalized_source_name(file.physical_name), file.content_digest });
    }

    std::vector<std::string_view> source_names;
    source_names.reserve(records.source_files.size()
        + records.expansions.size() + records.source_spans.size());
    for (const auto& file : records.source_files) {
        source_names.push_back(file.physical_name);
    }
    for (const auto& expansion : records.expansions) {
        source_names.push_back(expansion.description);
    }
    for (const auto& span : records.source_spans) {
        source_names.push_back(span.logical_name);
    }
    auto projected_names = project_source_names(
        source_names, mappings, diagnostics);
    projected_semantics.valid = projected_names.valid;

    auto projected_name = projected_names.values.begin();
    for (auto& file : records.source_files) {
        file.physical_name = std::move(*projected_name++);
    }
    for (auto& expansion : records.expansions) {
        expansion.description = std::move(*projected_name++);
    }
    for (auto& span : records.source_spans) {
        span.logical_name = std::move(*projected_name++);
    }
    return projected_semantics;
}

} // namespace

std::string stable_cache_source_name(
    const std::filesystem::path& path,
    const std::filesystem::path& base_directory)
{
    const auto normalized = path.lexically_normal();
    if (const auto relative = relative_within_base(
            normalized, base_directory)) {
        return support::path_to_utf8(
            std::filesystem::path { "project" } / *relative);
    }
    return support::path_to_utf8(normalized);
}

std::optional<std::vector<std::string>>
project_compiled_design_source_names(
    const std::span<const std::string_view> names,
    const std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics)
{
    const auto mapping_index = prepare_source_mappings(mappings, diagnostics);
    if (!mapping_index) {
        return std::nullopt;
    }
    auto projected_names = project_source_names(
        names, *mapping_index, diagnostics);
    if (!projected_names.valid) {
        return std::nullopt;
    }
    return std::move(projected_names.values);
}

std::optional<semantic::ModelRecords> project_compiled_semantic_source_names(
    const semantic::Model& semantics,
    const std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics)
{
    const auto mapping_index = prepare_source_mappings(mappings, diagnostics);
    if (!mapping_index) {
        return std::nullopt;
    }
    auto projected_semantics = project_semantic_source_names(
        semantics, *mapping_index, diagnostics);
    if (!projected_semantics.valid
        || !validate_relocated_source_identities(
            projected_semantics.source_identities,
            projected_semantics.records.source_files, diagnostics)) {
        return std::nullopt;
    }
    auto validated_semantics = semantic::Model::from_records(
        std::move(projected_semantics.records));
    if (!validated_semantics) {
        diagnostics.error(
            "FSIM-ART-HIR-001",
            "compiled-HIR source relocation invalidated semantic identities");
        return std::nullopt;
    }
    return std::move(*validated_semantics).take_records();
}

std::optional<std::string>
serialize_cache_compiled_hir_bundle_with_source_projection(
    const semantic::CompiledDesign& design,
    const std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics)
{
    const auto mapping_index = prepare_source_mappings(mappings, diagnostics);
    if (!mapping_index) {
        return std::nullopt;
    }
    std::vector<std::string> producer_source_names;
    producer_source_names.reserve(design.semantics.source_spans().size());
    for (const auto& span : design.semantics.source_spans()) {
        producer_source_names.push_back(span.logical_name);
    }
    auto projected_semantics = project_semantic_source_names(
        design.semantics, *mapping_index, diagnostics);

    std::vector<semantic::CompiledDependency> projected_dependencies {
        design.dependencies().begin(), design.dependencies().end() };
    std::vector<semantic::sv::Unit> projected_systemverilog_units {
        design.systemverilog_hir.units().begin(),
        design.systemverilog_hir.units().end() };
    std::vector<semantic::vhdl::Unit> projected_vhdl_units {
        design.vhdl_hir.units().begin(), design.vhdl_hir.units().end() };

    codec_detail::SystemVerilogConstraintHirView projected_systemverilog {
        std::span<const semantic::sv::Unit> { projected_systemverilog_units },
        design.systemverilog_hir.declarations(),
        design.systemverilog_hir.types(),
        design.systemverilog_hir.expressions(),
        design.systemverilog_hir.statements(),
        design.systemverilog_hir.processes(),
        design.systemverilog_hir.classes(),
        design.systemverilog_hir.instances(),
        design.systemverilog_hir.udps(),
        design.systemverilog_hir.dpi_declarations(),
        design.systemverilog_hir.covergroup_instances()
    };

    std::vector<std::string_view> dependency_names;
    dependency_names.reserve(design.dependencies().size());
    for (const auto& dependency : design.dependencies()) {
        dependency_names.push_back(dependency.logical_name);
    }
    for (const auto& unit : design.systemverilog_hir.units()) {
        for (const auto& dependency : unit.source_dependencies) {
            dependency_names.push_back(dependency);
        }
    }
    for (const auto& unit : design.vhdl_hir.units()) {
        for (const auto& dependency : unit.source_dependencies) {
            dependency_names.push_back(dependency);
        }
    }
    auto projected_names = project_source_names(
        dependency_names, *mapping_index, diagnostics);

    auto projected_name = projected_names.values.begin();
    for (auto& dependency : projected_dependencies) {
        dependency.logical_name = std::move(*projected_name++);
    }
    for (auto& unit : projected_systemverilog_units) {
        for (auto& dependency : unit.source_dependencies) {
            dependency = std::move(*projected_name++);
        }
    }
    for (auto& unit : projected_vhdl_units) {
        for (auto& dependency : unit.source_dependencies) {
            dependency = std::move(*projected_name++);
        }
    }

    if (!projected_semantics.valid || !projected_names.valid) {
        return std::nullopt;
    }

    bool valid = true;
    for (std::size_t index = 0;
         index < projected_systemverilog.statements.size(); ++index) {
        if (projected_systemverilog.statements[index].output_text
            .starts_with(kPendingAssertionMarker)) {
            valid = relocate_pending_assertion_marker(
                        projected_systemverilog.statements.project(index),
                        *mapping_index, diagnostics)
                && valid;
        }
    }
    if (!valid) {
        return std::nullopt;
    }

    const auto relocate_projected_records = [&](auto& records) {
        visit_projected_generated_records(records, [&](auto& record) {
            relocate_generated_text(record, producer_source_names,
                projected_semantics.records.source_spans, valid);
        });
    };
    relocate_projected_records(projected_systemverilog.units);
    relocate_projected_records(projected_systemverilog.declarations);
    relocate_projected_records(projected_systemverilog.types);
    relocate_projected_records(projected_systemverilog.expressions);
    relocate_projected_records(projected_systemverilog.statements);
    relocate_projected_records(projected_systemverilog.processes);
    relocate_projected_records(projected_systemverilog.classes);
    relocate_projected_records(projected_systemverilog.instances);
    relocate_projected_records(projected_systemverilog.udps);
    relocate_projected_records(projected_systemverilog.dpi_declarations);
    relocate_projected_records(
        projected_systemverilog.covergroup_instances);
    if (!valid) {
        diagnostics.error(
            "FSIM-ART-HIR-001",
            "compiled-HIR generated source-name text has invalid provenance");
        return std::nullopt;
    }

    if (!validate_relocated_source_identities(
            projected_semantics.source_identities,
            projected_semantics.records.source_files, diagnostics)) {
        return std::nullopt;
    }
    auto validated_semantics = semantic::Model::from_records(
        std::move(projected_semantics.records));
    if (!validated_semantics) {
        diagnostics.error(
            "FSIM-ART-HIR-001",
            "compiled-HIR source relocation invalidated semantic identities");
        return std::nullopt;
    }

    return serialize_cache_compiled_hir_bundle_with_projected_sources(
        design, std::move(*validated_semantics), projected_dependencies,
        projected_systemverilog, projected_vhdl_units, diagnostics);
}

std::optional<std::vector<library::SourceNameMapping>>
compiled_cache_source_mappings(
    const CheckedProject& checked,
    const std::filesystem::path& base_directory,
    diagnostic::Engine& diagnostics)
{
    std::vector<library::SourceNameMapping> result;
    std::vector<std::filesystem::path> producers;
    std::set<std::string> logical_names;
    bool valid = true;
    const auto add = [&](const std::filesystem::path& path,
                         auto&& make_logical) {
        if (path.empty()) {
            return;
        }
        const auto producer = support::path_to_utf8(path.lexically_normal());
        if (std::ranges::any_of(producers, [&](const auto& producer) {
                return same_source_path(producer, path);
            })) {
            return;
        }
        producers.push_back(path);
        auto logical = support::path_to_utf8(
            std::filesystem::path { make_logical() }.lexically_normal());
        if (logical.empty()
            || !logical_names.insert(source_path_key(
                    support::path_from_utf8(logical))).second) {
            diagnostics.error(
                "FSIM-CACHE-0001",
                "compiled-HIR cache source relocation is not bijective for '"
                    + producer + "'");
            valid = false;
            return;
        }
        result.push_back({ producer, std::move(logical) });
    };
    const auto logical_name = [&](const std::string_view role,
                                  const std::size_t ordinal,
                                  const std::filesystem::path& path,
                                  const std::string_view digest) {
        const auto category = std::string { role } + "/"
            + std::to_string(ordinal);
        const auto stable = stable_cache_source_name(path, base_directory);
        if (support::path_is_portably_absolute(
                support::path_from_utf8(stable))) {
            return fallback_logical_name(category, digest, path);
        }
        return support::path_to_utf8(
            std::filesystem::path { "cache-sources" }
            / category / stable);
    };
    const auto add_source = [&](const CheckedSource& source,
                                const std::string_view category,
                                const std::size_t source_index) {
        add(source.path, [&] {
            return logical_name(category, source_index,
                source.path, source.content_digest);
        });
        for (std::size_t index = 0;
             index < source.dependencies.size(); ++index) {
            const auto& dependency = source.dependencies[index];
            const auto role = std::string { category } + "/"
                + std::to_string(source_index) + "/dependency";
            add(dependency.path, [&] {
                return logical_name(role, index, dependency.path,
                    dependency.content_digest);
            });
        }
    };
    for (std::size_t index = 0; index < checked.hdl_sources.size(); ++index) {
        add_source(checked.hdl_sources[index], "hdl", index);
    }
    for (std::size_t index = 0;
         index < checked.systemc_sources.size(); ++index) {
        add_source(checked.systemc_sources[index], "systemc", index);
    }
    for (std::size_t index = 0;
         index < checked.standard_sources.size(); ++index) {
        add_source(checked.standard_sources[index], "standard", index);
    }

    for (std::size_t index = 0;
         index < checked.semantics.source_files().size(); ++index) {
        const auto& file = checked.semantics.source_files()[index];
        const auto path = support::path_from_utf8(file.physical_name);
        if (support::path_is_portably_absolute(path)) {
            add(path, [&] {
                return logical_name(
                    "semantic", index, path, file.content_digest);
            });
        }
    }
    for (const auto& span : checked.semantics.source_spans()) {
        const auto path = support::path_from_utf8(span.logical_name);
        if (support::path_is_portably_absolute(path)) {
            const auto& file = checked.semantics.source_files().at(
                span.file.value());
            add(path, [&] {
                return logical_name(
                    "span", span.id.value(), path, file.content_digest);
            });
        }
    }
    for (std::size_t index = 0;
         index < checked.semantics.expansions().size(); ++index) {
        const auto& expansion = checked.semantics.expansions()[index];
        std::size_t source_index = 0;
        for (const auto& path : expansion_source_paths(
                 expansion.description)) {
            if (support::path_is_portably_absolute(path)) {
                const auto role = std::string { "expansion/" }
                    + std::to_string(index);
                const auto ordinal = source_index++;
                add(path, [&] {
                    return logical_name(role, ordinal, path,
                        std::string_view { });
                });
            }
        }
    }
    for (std::size_t index = 0;
         index < checked.dependencies().size(); ++index) {
        const auto& dependency = checked.dependencies()[index];
        const auto path = support::path_from_utf8(
            dependency.logical_name);
        if (support::path_is_portably_absolute(path)) {
            add(path, [&] {
                return logical_name(
                    "dependency", index, path,
                    dependency.content_digest);
            });
        }
    }
    const auto add_dependencies = [&](const auto& units,
                                      const std::string_view language) {
        for (std::size_t unit_index = 0;
             unit_index < units.size(); ++unit_index) {
            for (std::size_t dependency_index = 0;
                 dependency_index
                     < units[unit_index].source_dependencies.size();
                 ++dependency_index) {
                const auto path = support::path_from_utf8(
                    units[unit_index]
                        .source_dependencies[dependency_index]);
                if (!support::path_is_portably_absolute(path)) {
                    continue;
                }
                const auto role = std::string { language } + "/"
                    + std::to_string(unit_index) + "/dependency";
                add(path, [&] {
                    return logical_name(role, dependency_index, path,
                        std::string_view { });
                });
            }
        }
    };
    add_dependencies(checked.systemverilog_hir.units(), "systemverilog");
    add_dependencies(checked.vhdl_hir.units(), "vhdl");
    return valid
        ? std::optional { std::move(result) }
        : std::nullopt;
}

bool normalize_compiled_design_source_paths(
    CheckedProject& checked,
    const std::filesystem::path& base_directory,
    diagnostic::Engine& diagnostics)
{
    const auto mappings = compiled_cache_source_mappings(
        checked, base_directory, diagnostics);
    if (!mappings) {
        return false;
    }
    std::vector<library::SourceNameMapping> normalized;
    normalized.reserve(mappings->size());
    for (const auto& mapping : *mappings) {
        normalized.push_back(
            { mapping.producer_name, mapping.producer_name });
    }
    return relocate_compiled_design_sources(
        checked, normalized, diagnostics);
}

semantic::CompiledLinkResult install_linked_compiled_design(
    CheckedProject& checked,
    std::vector<semantic::CompiledDesign> inputs)
{
    auto linked = semantic::link_compiled_designs(std::move(inputs));
    if (!linked.ok()) {
        return linked;
    }
    static_cast<semantic::CompiledDesign&>(checked) = std::move(*linked.design);
    return linked;
}

bool relocate_compiled_design_sources(
    semantic::CompiledDesign& design,
    const std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics)
{
    const auto mapping_index = prepare_source_mappings(mappings, diagnostics);
    if (!mapping_index) {
        return false;
    }
    auto records = std::move(design.semantics).take_records();
    std::vector<std::string> producer_source_names;
    producer_source_names.reserve(records.source_spans.size());
    for (const auto& span : records.source_spans) {
        producer_source_names.push_back(span.logical_name);
    }
    std::vector<SourceIdentity> source_identities;
    source_identities.reserve(records.source_files.size());
    for (const auto& file : records.source_files) {
        source_identities.push_back({
            normalized_source_name(file.physical_name),
            file.content_digest
        });
    }
    bool valid = true;
    for (auto& file : records.source_files) {
        valid = relocate_name(
                    file.physical_name, *mapping_index, diagnostics)
            && valid;
    }
    for (auto& expansion : records.expansions) {
        valid = relocate_name(
                    expansion.description, *mapping_index, diagnostics)
            && valid;
    }
    for (auto& span : records.source_spans) {
        valid = relocate_name(
                    span.logical_name, *mapping_index, diagnostics)
            && valid;
    }
    for (auto& dependency : design.mutable_dependencies()) {
        valid = relocate_name(
                    dependency.logical_name, *mapping_index, diagnostics)
            && valid;
    }
    for (auto& unit : design.mutable_systemverilog().mutable_units()) {
        for (auto& dependency : unit.source_dependencies) {
            valid = relocate_name(dependency, *mapping_index, diagnostics)
                && valid;
        }
    }
    for (auto& unit : design.mutable_vhdl().mutable_units()) {
        for (auto& dependency : unit.source_dependencies) {
            valid = relocate_name(dependency, *mapping_index, diagnostics)
                && valid;
        }
    }
    for (auto& statement :
        design.mutable_systemverilog().mutable_statements()) {
        valid = relocate_pending_assertion_marker(
                    statement, *mapping_index, diagnostics)
            && valid;
    }
    if (!valid) {
        return false;
    }
    const auto relocate_generated_collection = [&](auto& collection) {
        relocate_generated_text(collection, producer_source_names,
            records.source_spans, valid);
    };
    auto& systemverilog = design.mutable_systemverilog();
    relocate_generated_collection(systemverilog.mutable_units());
    relocate_generated_collection(systemverilog.mutable_declarations());
    relocate_generated_collection(systemverilog.mutable_types());
    relocate_generated_collection(systemverilog.mutable_expressions());
    relocate_generated_collection(systemverilog.mutable_statements());
    relocate_generated_collection(systemverilog.mutable_processes());
    relocate_generated_collection(systemverilog.mutable_classes());
    relocate_generated_collection(systemverilog.mutable_instances());
    relocate_generated_collection(systemverilog.mutable_udps());
    relocate_generated_collection(
        systemverilog.mutable_dpi_declarations());
    relocate_generated_collection(
        systemverilog.mutable_covergroup_instances());
    if (!valid) {
        diagnostics.error(
            "FSIM-ART-HIR-001",
            "compiled-HIR generated source-name text has invalid provenance");
        return false;
    }
    if (!validate_relocated_source_identities(
            source_identities, records.source_files, diagnostics)) {
        return false;
    }
    auto semantics = semantic::Model::from_records(std::move(records));
    if (!semantics) {
        diagnostics.error(
            "FSIM-ART-HIR-001",
            "compiled-HIR source relocation invalidated semantic identities");
        return false;
    }
    design.semantics = std::move(*semantics);
    if (!design.valid()) {
        diagnostics.error(
            "FSIM-ART-HIR-001",
            "compiled-HIR source relocation produced an invalid bundle");
        return false;
    }
    design.refresh_lookup_indexes();
    return true;
}

} // namespace fsim::app::application_detail
