// SPDX-License-Identifier: Apache-2.0
#include "application_runtime_path_codec.hpp"

#include "application_design_artifact_codec_internal.hpp"
#include "application_hierarchy_path_codec.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <concepts>
#include <functional>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace fsim::app::runtime_path_codec {
namespace {

using State = elaboration::ElaboratedDesignState;
using Paths = semantic::HierarchyPathTable;
using PathId = semantic::HierarchyPathId;
using PathTablePayload = hierarchy_path_codec::Payload;
using codec_detail::DecodeBudget;
using codec_detail::Reader;
using codec_detail::Writer;

constexpr std::string_view kRuntimeMagic = "FSIMRUN1";
constexpr std::string_view kCode = "FSIM-ART-0013";
constexpr std::uint32_t kSchema = 64U;
constexpr std::uint8_t kInlinePathMode = 0U;
constexpr std::uint8_t kExternalPathMode = 1U;
constexpr std::uint32_t kInvalidPathId
    = std::numeric_limits<std::uint32_t>::max();

struct RuntimeStatePathWireDto {
    // IDs follow visit_runtime_paths() order. Path strings in state are empty
    // placeholders on the wire and are restored from these explicit IDs.
    std::vector<std::uint32_t> path_ids;
    State state;
};

struct PreparedRuntimeState {
    std::uint8_t path_mode { };
    std::string path_binding;
    RuntimeStatePathWireDto dto;
};

void report_error(
    diagnostic::Engine& diagnostics,
    std::string message,
    const std::string_view source_name = { })
{
    if (source_name.empty()) {
        diagnostics.error(std::string { kCode }, std::move(message));
        return;
    }
    diagnostics.error(std::string { kCode }, std::move(message),
        { std::string { source_name }, { 1, 1, 0 }, { 1, 1, 0 } });
}

[[nodiscard]] std::string_view path_codec_error(
    const hierarchy_path_codec::Error error) noexcept
{
    using Error = hierarchy_path_codec::Error;
    switch (error) {
    case Error::none: return "none";
    case Error::payload_limit: return "payload limit exceeded";
    case Error::path_count_limit: return "path count limit exceeded";
    case Error::path_length_limit: return "path length limit exceeded";
    case Error::total_path_bytes_limit:
        return "total path bytes limit exceeded";
    case Error::invalid_magic: return "invalid path-table magic";
    case Error::unsupported_schema:
        return "unsupported path-table schema";
    case Error::truncated: return "truncated path table";
    case Error::trailing_bytes: return "trailing path-table bytes";
    case Error::invalid_utf8: return "path is not valid UTF-8";
    case Error::embedded_nul: return "path contains an embedded NUL";
    case Error::duplicate_path: return "path table contains a duplicate";
    case Error::unsorted_path: return "path table is not canonical";
    case Error::invalid_reference: return "invalid path reference";
    case Error::unmapped_reference:
        return "path reference is absent from the canonical table";
    case Error::allocation_failure:
        return "path-table allocation failed or exceeded host limits";
    }
    return "unknown path-table error";
}

template <typename StateType, typename Callback>
bool visit_runtime_paths(StateType& state, Callback&& callback)
{
    const auto visit = [&](auto& path, const bool empty_allowed) {
        return std::invoke(callback, path, empty_allowed);
    };

    if (!visit(state.top, false)) {
        return false;
    }
    for (auto& root : state.roots) {
        if (!visit(root, false)) {
            return false;
        }
    }
    for (auto& signal : state.signal_info) {
        if (!visit(signal.name, false)) {
            return false;
        }
        for (auto& binding : signal.vhdl_mode_view_bindings) {
            for (auto& element : binding.elements) {
                if (!visit(element.formal_path, false)
                    || !visit(element.actual_path, false)) {
                    return false;
                }
            }
        }
    }
    for (auto& conversion : state.boundary_conversions) {
        if (!visit(conversion.path, false)) {
            return false;
        }
    }
    for (auto& signal : state.signals) {
        if (!visit(signal.name, false)) {
            return false;
        }
    }
    for (auto& object : state.string_object_info) {
        if (!visit(object.name, false)) {
            return false;
        }
    }
    for (auto& object : state.string_objects) {
        if (!visit(object.name, false)) {
            return false;
        }
    }
    for (auto& object : state.container_object_info) {
        if (!visit(object.name, false)) {
            return false;
        }
    }
    for (auto& object : state.container_objects) {
        if (!visit(object.name, false)) {
            return false;
        }
    }
    for (auto& object : state.vhdl_protected_object_info) {
        if (!visit(object.name, false)) {
            return false;
        }
    }
    for (auto& process : state.processes) {
        if (!visit(process.name, false)) {
            return false;
        }
        for (std::size_t index = 0;
            index < process.operations.size(); ++index) {
            auto operation = process.operations.expanded(index);
            bool has_path { };
            bool changed { };
            const auto visit_operation_path = [&](auto& path,
                                                  const bool empty_allowed) {
                has_path = true;
                const bool was_empty = path.empty();
                if (!visit(path, empty_allowed)) {
                    return false;
                }
                changed = changed || (was_empty != path.empty());
                return true;
            };
            const auto valid = runtime::simir::visit_operation(
                [&](auto& value) {
                    using Operation
                        = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<Operation,
                                      runtime::simir::CoverageControl>) {
                        return visit_operation_path(
                            value.instance_context, false);
                    } else if constexpr (std::same_as<Operation,
                                             runtime::simir::CoverageAccess>) {
                        return visit_operation_path(
                            value.instance_context,
                            !value.selector.has_value());
                    } else {
                        return true;
                    }
                },
                operation);
            if (!valid) {
                return false;
            }
            if constexpr (!std::is_const_v<StateType>) {
                if (has_path && changed) {
                    process.operations.replace(index, std::move(operation));
                }
            }
        }
    }
    for (auto& specialization : state.specializations) {
        if (!visit(specialization.instance, false)) {
            return false;
        }
    }
    for (auto& specify : state.verilog_specify_paths) {
        if (!visit(specify.instance, false)) {
            return false;
        }
    }
    for (auto& instance : state.systemc_instances) {
        if (!visit(instance.instance, false)) {
            return false;
        }
    }
    for (auto& object : state.systemc_objects) {
        if (!visit(object.name, false) || !visit(object.parent, true)) {
            return false;
        }
    }
    for (auto& entry : state.signal_names) {
        if (!visit(entry.first, false)) {
            return false;
        }
    }
    for (auto& entry : state.string_names) {
        if (!visit(entry.first, false)) {
            return false;
        }
    }
    for (auto& entry : state.container_names) {
        if (!visit(entry.first, false)) {
            return false;
        }
    }
    if (state.code_coverage_inventory) {
        for (auto& instance : state.code_coverage_inventory->instances) {
            if (!visit(instance.instance, false)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool account_path_table_allocations(
    DecodeBudget& budget,
    const std::string_view encoded_table)
{
    constexpr std::size_t kPathRecordOverhead
        = sizeof(std::string) + 2U * sizeof(void*);
    if (encoded_table.size() < 16U) {
        return false;
    }
    std::uint32_t path_count { };
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        path_count |= static_cast<std::uint32_t>(
            static_cast<unsigned char>(encoded_table[12U + shift / 8U]))
            << shift;
    }
    return budget.consume(encoded_table.size(), 2U)
        && budget.consume(path_count, kPathRecordOverhead);
}

[[nodiscard]] std::optional<PathTablePayload> canonical_payload_for_table(
    const Paths& paths,
    diagnostic::Engine& diagnostics,
    const std::string_view source_name = { })
{
    auto result = hierarchy_path_codec::encode_inline_hierarchy_paths(
        paths);
    if (!result) {
        report_error(diagnostics,
            "runtime hierarchy path table is invalid: "
                + std::string { path_codec_error(result.error) },
            source_name);
        return std::nullopt;
    }
    return std::move(result.payload);
}

[[nodiscard]] bool has_canonical_ids(
    const Paths& supplied, const Paths& canonical) noexcept
{
    if (supplied.size() != canonical.size()) {
        return false;
    }
    for (std::size_t index = 0; index < supplied.size(); ++index) {
        const auto id = PathId::from_index(
            static_cast<std::uint32_t>(index));
        if (supplied.view(id) != canonical.view(id)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<PreparedRuntimeState> prepare_runtime_state(
    const State& state,
    const Paths* const external_paths,
    diagnostic::Engine& diagnostics)
{
    try {
        PreparedRuntimeState prepared;
        prepared.path_mode = external_paths == nullptr
            ? kInlinePathMode
            : kExternalPathMode;
        semantic::HierarchyPathTable::Builder source_builder;
        auto& ids = prepared.dto.path_ids;
        std::optional<std::string> path_error;
        const bool paths_visited = visit_runtime_paths(
            state, [&](const auto& path, const bool empty_allowed) {
                const std::string_view spelling { path };
                if (spelling.empty()) {
                    if (!empty_allowed) {
                        path_error
                            = "runtime state contains an empty required "
                              "hierarchy path";
                        return false;
                    }
                    ids.push_back(kInvalidPathId);
                    return true;
                }
                const auto id = external_paths == nullptr
                    ? std::optional<PathId> { source_builder.intern(spelling) }
                    : external_paths->find(spelling);
                if (!id) {
                    path_error = "runtime state hierarchy path is missing "
                                 "from the external canonical table: ";
                    path_error->append(spelling);
                    return false;
                }
                ids.push_back(id->value());
                return ids.size() < kInvalidPathId;
            });
        if (!paths_visited) {
            report_error(diagnostics,
                path_error.value_or(
                    "runtime state hierarchy path collection failed"));
            return std::nullopt;
        }

        Paths path_table;
        if (external_paths == nullptr) {
            path_table = std::move(source_builder).freeze();
        } else {
            path_table = *external_paths;
        }
        auto payload = canonical_payload_for_table(
            path_table, diagnostics);
        if (!payload) {
            return std::nullopt;
        }
        if (external_paths != nullptr
            && !has_canonical_ids(path_table, payload->paths)) {
            report_error(diagnostics,
                "external runtime hierarchy path table IDs are not in "
                "canonical order");
            return std::nullopt;
        }
        for (auto& raw_id : ids) {
            if (raw_id == kInvalidPathId) {
                continue;
            }
            const auto spelling = path_table.view(
                PathId::from_index(raw_id));
            const auto canonical_id = payload->paths.find(spelling);
            if (!canonical_id) {
                report_error(diagnostics,
                    "runtime path could not be mapped into its canonical "
                    "path table");
                return std::nullopt;
            }
            raw_id = canonical_id->value();
        }

        if (external_paths == nullptr) {
            prepared.path_binding = std::move(payload->bytes);
        } else {
            prepared.path_binding = std::move(payload->digest);
        }
        prepared.dto.state = state;
        std::size_t cleared_paths { };
        const bool cleared = visit_runtime_paths(
            prepared.dto.state, [&](auto& path, const bool) {
                path.clear();
                ++cleared_paths;
                return true;
            });
        if (!cleared || cleared_paths != ids.size()) {
            report_error(diagnostics,
                "runtime state path projection did not match its ID table");
            return std::nullopt;
        }
        return prepared;
    } catch (const std::bad_alloc&) {
        report_error(diagnostics,
            "runtime state allocation failed while building path IDs");
    } catch (const std::length_error&) {
        report_error(diagnostics,
            "runtime state path-ID table exceeds the host container limit");
    } catch (const std::exception& error) {
        report_error(diagnostics,
            "runtime state path projection failed: "
                + std::string { error.what() });
    }
    return std::nullopt;
}

void write_prepared_runtime_state(
    Writer& writer, const PreparedRuntimeState& prepared)
{
    writer.raw(kRuntimeMagic);
    writer.write(kSchema);
    writer.write(prepared.path_mode);
    writer.write(prepared.path_binding);
    writer.write(prepared.dto.path_ids);
    writer.write(prepared.dto.state);
}

template <typename ReaderType>
std::optional<State> read_runtime_state(
    ReaderType& reader,
    DecodeBudget& budget,
    const std::string_view source_name,
    const Paths* const external_paths,
    diagnostic::Engine& diagnostics)
{
    std::uint32_t schema { };
    if (!reader.raw(kRuntimeMagic) || !reader.read(schema)) {
        report_error(diagnostics, reader.failure(), source_name);
        return std::nullopt;
    }
    if (schema != kSchema) {
        report_error(diagnostics,
            diagnostic::unsupported_artifact_identity(
                "runtime state FSIMRUN1", "schema " + std::to_string(schema),
                "schema " + std::to_string(kSchema), ".fsimdesign"),
            source_name);
        return std::nullopt;
    }

    std::uint8_t path_mode { };
    std::string path_binding;
    if (!reader.read(path_mode) || !reader.read(path_binding)) {
        report_error(diagnostics, reader.failure(), source_name);
        return std::nullopt;
    }
    if ((path_mode == kInlinePathMode) != (external_paths == nullptr)
        || (path_mode != kInlinePathMode
            && path_mode != kExternalPathMode)) {
        report_error(diagnostics,
            "runtime state hierarchy path-table mode does not match its "
            "enclosing artifact", source_name);
        return std::nullopt;
    }

    std::optional<PathTablePayload> path_payload;
    const Paths* path_table { };
    if (path_mode == kInlinePathMode) {
        if (!account_path_table_allocations(budget, path_binding)) {
            report_error(diagnostics,
                "runtime path table exceeds the aggregate decode allocation "
                "budget", source_name);
            return std::nullopt;
        }
        auto decoded
            = hierarchy_path_codec::decode_hierarchy_paths(path_binding);
        if (!decoded) {
            report_error(diagnostics,
                "runtime path table is invalid: "
                    + std::string { path_codec_error(decoded.error) },
                source_name);
            return std::nullopt;
        }
        path_payload = std::move(decoded.payload);
    } else {
        path_payload = canonical_payload_for_table(
            *external_paths, diagnostics, source_name);
        if (!path_payload) {
            return std::nullopt;
        }
        if (!has_canonical_ids(*external_paths, path_payload->paths)) {
            report_error(diagnostics,
                "external runtime hierarchy path table IDs are not in "
                "canonical order", source_name);
            return std::nullopt;
        }
        if (!budget.consume(path_payload->bytes.size(), 2U)
            || !budget.consume(path_payload->paths.size(),
                sizeof(std::string) + 2U * sizeof(void*))) {
            report_error(diagnostics,
                "external runtime path table exceeds the aggregate decode "
                "allocation budget", source_name);
            return std::nullopt;
        }
        if (path_binding != path_payload->digest) {
            report_error(diagnostics,
                "runtime state path-table digest does not match the "
                "enclosing artifact", source_name);
            return std::nullopt;
        }
    }
    path_table = &path_payload->paths;

    RuntimeStatePathWireDto dto;
    if (!reader.read(dto.path_ids) || !reader.read(dto.state)) {
        report_error(diagnostics, reader.failure(), source_name);
        return std::nullopt;
    }
    if (reader.remaining() != 0U) {
        report_error(diagnostics,
            "runtime state contains trailing bytes", source_name);
        return std::nullopt;
    }

    std::size_t next_path { };
    std::optional<std::string> reference_error;
    const bool restored = visit_runtime_paths(dto.state,
        [&](auto& path, const bool empty_allowed) {
            if (!path.empty()) {
                reference_error
                    = "runtime state path-bearing DTO field is not an "
                      "empty placeholder";
                return false;
            }
            if (next_path >= dto.path_ids.size()) {
                reference_error
                    = "runtime state path-ID vector is shorter than its "
                      "path-bearing records";
                return false;
            }
            const auto raw_id = dto.path_ids[next_path++];
            if (raw_id == kInvalidPathId) {
                if (!empty_allowed) {
                    reference_error
                        = "runtime state uses an empty-path sentinel for a "
                          "required hierarchy path";
                    return false;
                }
                path.clear();
                return true;
            }
            const auto id
                = hierarchy_path_codec::decode_path_reference(*path_table,
                    raw_id);
            if (!id) {
                reference_error
                    = "runtime state contains an invalid hierarchy path ID";
                return false;
            }
            const auto spelling = path_table->view(*id);
            if (spelling.empty()) {
                reference_error
                    = "runtime state uses an empty table entry for a "
                      "hierarchy path";
                return false;
            }
            if (!budget.consume(spelling.size(), sizeof(char))) {
                reference_error
                    = "runtime state path reconstruction exceeds the "
                      "aggregate decode allocation budget";
                return false;
            }
            path.assign(spelling);
            return true;
        });
    if (!restored) {
        report_error(diagnostics,
            reference_error.value_or(
                "runtime state hierarchy path reconstruction failed"),
            source_name);
        return std::nullopt;
    }
    if (next_path != dto.path_ids.size()) {
        report_error(diagnostics,
            "runtime state path-ID vector has trailing references",
            source_name);
        return std::nullopt;
    }
    return std::move(dto.state);
}

template <typename ReaderFactory>
std::optional<State> decode_runtime_payload(
    ReaderFactory&& make_reader,
    const std::uint64_t size,
    std::string source_name,
    const Paths* const external_paths,
    diagnostic::Engine& diagnostics)
{
    if (size > kMaximumPayloadBytes
        || size > std::numeric_limits<std::size_t>::max()) {
        report_error(diagnostics,
            "runtime state exceeds the maximum payload size",
            source_name);
        return std::nullopt;
    }
    DecodeBudget budget { kMaximumDecodeAllocationBytes };
    auto reader = std::invoke(std::forward<ReaderFactory>(make_reader), &budget);
    try {
        return read_runtime_state(reader, budget, source_name,
            external_paths, diagnostics);
    } catch (const std::bad_alloc&) {
        report_error(diagnostics,
            "runtime state allocation failed while decoding", source_name);
    } catch (const std::length_error&) {
        report_error(diagnostics,
            "runtime state allocation exceeds a host container limit",
            source_name);
    } catch (const std::exception& error) {
        report_error(diagnostics,
            "runtime state decoding failed: " + std::string { error.what() },
            source_name);
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> serialize_runtime_path_state(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable* const external_paths,
    diagnostic::Engine& diagnostics)
{
    auto prepared = prepare_runtime_state(state, external_paths, diagnostics);
    if (!prepared) {
        return std::nullopt;
    }
    const auto output_capacity = codec_detail::add_serialization_capacity(
        kRuntimeMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint8_t),
        codec_detail::add_serialization_capacity(
            prepared->path_binding.size(),
            codec_detail::add_serialization_capacity(
                codec_detail::serialization_capacity_hint(
                    prepared->dto.path_ids),
                codec_detail::serialization_capacity_hint(
                    prepared->dto.state))));
    Writer writer { output_capacity };
    write_prepared_runtime_state(writer, *prepared);
    if (!writer.failure().empty()) {
        report_error(diagnostics, writer.failure());
        return std::nullopt;
    }
    return std::move(writer).finish();
}

bool serialize_runtime_path_state(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable* const external_paths,
    std::ostream& output,
    diagnostic::Engine& diagnostics)
{
    auto prepared = prepare_runtime_state(state, external_paths, diagnostics);
    if (!prepared) {
        return false;
    }
    Writer writer { &output, nullptr };
    write_prepared_runtime_state(writer, *prepared);
    if (!writer.complete()) {
        report_error(diagnostics, writer.failure());
        return false;
    }
    return true;
}

std::optional<std::string> runtime_path_state_checksum(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable* const external_paths,
    diagnostic::Engine& diagnostics)
{
    auto prepared = prepare_runtime_state(state, external_paths, diagnostics);
    if (!prepared) {
        return std::nullopt;
    }
    support::Sha256 checksum;
    Writer writer { nullptr, &checksum };
    write_prepared_runtime_state(writer, *prepared);
    if (!writer.complete()) {
        report_error(diagnostics, writer.failure());
        return std::nullopt;
    }
    return support::Sha256::hex(checksum.finish());
}

std::optional<elaboration::ElaboratedDesignState>
deserialize_runtime_path_state(
    const std::string_view bytes,
    std::string source_name,
    const semantic::HierarchyPathTable* const external_paths,
    diagnostic::Engine& diagnostics)
{
    if (bytes.size() > kMaximumPayloadBytes) {
        report_error(diagnostics,
            "runtime state exceeds the maximum payload size", source_name);
        return std::nullopt;
    }
    return decode_runtime_payload(
        [&](DecodeBudget* const budget) {
            return Reader { bytes, budget };
        },
        bytes.size(), std::move(source_name), external_paths, diagnostics);
}

std::optional<elaboration::ElaboratedDesignState>
deserialize_runtime_path_state(
    std::istream& input,
    const std::uint64_t size,
    std::string source_name,
    const semantic::HierarchyPathTable* const external_paths,
    diagnostic::Engine& diagnostics)
{
    return decode_runtime_payload(
        [&](DecodeBudget* const budget) {
            return Reader { input, size, budget };
        },
        size, std::move(source_name), external_paths, diagnostics);
}

} // namespace fsim::app::runtime_path_codec
