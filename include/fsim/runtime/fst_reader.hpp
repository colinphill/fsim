// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/trace_model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

struct FstReaderLimits {
    std::size_t maximum_container_bytes { 512U << 20U };
    std::size_t maximum_block_bytes { 512U << 20U };
    std::size_t maximum_hierarchy_bytes { 256U << 20U };
    std::size_t maximum_scopes { 1U << 20U };
    std::size_t maximum_declarations { 1U << 20U };
    std::size_t maximum_timestamps { 1U << 20U };
    std::size_t maximum_values { 1U << 20U };
    std::size_t maximum_decoded_value_bytes { 512U << 20U };
    std::size_t maximum_text_bytes { 1U << 20U };
    std::size_t maximum_metadata_bytes { 1U << 20U };
};

enum class FstReaderCompression : std::uint8_t {
    None,
    Deterministic
};

struct FstReaderScope {
    std::uint8_t kind { };
    std::string path;
    std::string component;
    std::string owner_identity;
};

struct FstReaderSource {
    TraceSourceKind kind { TraceSourceKind::Hdl };
    TraceLanguage language { TraceLanguage::Unknown };
    std::string root_identity;
    std::string library;
    std::string owner_identity;
    std::string canonical_name;
};

struct FstReaderDeclaration {
    TraceDeclarationKind kind { TraceDeclarationKind::Variable };
    TraceSignalId signal;
    TraceSignalId target;
    std::uint64_t handle { };
    std::uint8_t fst_type { };
    std::size_t storage_width { };
    std::string path;
    std::string reference;
    TraceTypeKind type_kind { TraceTypeKind::Packed };
    SystemVerilogScalarKind scalar_kind { SystemVerilogScalarKind::None };
    std::size_t width { };
    std::string canonical_metadata;
    FstReaderSource source;
};

struct FstReaderValue {
    TraceSignalId signal;
    std::uint64_t handle { };
    SimulationTick time { };
    std::size_t handle_sequence { };
    FstPayloadKind payload_kind { FstPayloadKind::Symbols };
    std::string payload;
    std::uint64_t real_bits { };
};

struct FstReaderTrace {
    SimulationTick initial_time { };
    SimulationTick final_time { };
    std::int8_t timescale_exponent { };
    FstReaderCompression compression { FstReaderCompression::Deterministic };
    std::string profile;
    std::vector<FstReaderScope> scopes;
    std::vector<FstReaderDeclaration> declarations;
    std::vector<SimulationTick> timestamps;
    std::vector<FstReaderValue> values;
    std::string semantic_digest;
};

struct FstReaderDiagnostic {
    std::string code;
    std::string message;
    std::size_t offset { };
};

struct FstReaderResult {
    std::optional<FstReaderTrace> trace;
    std::vector<FstReaderDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept
    {
        return trace.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] FstReaderResult read_fst(
    std::span<const std::uint8_t> bytes,
    FstReaderLimits limits = { });
[[nodiscard]] FstReaderResult read_fst(
    std::string_view bytes,
    FstReaderLimits limits = { });
[[nodiscard]] FstReaderResult read_fst_file(
    const std::filesystem::path& path,
    FstReaderLimits limits = { });

} // namespace fsim::runtime
