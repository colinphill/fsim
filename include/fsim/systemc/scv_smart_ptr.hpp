// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/systemc/scv_backend_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::systemc {

struct ScvNativeSmartPtrHandle {
    std::uint64_t slot { };
    std::uint64_t generation { };

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return slot != 0U && generation != 0U;
    }

    friend constexpr bool operator==(
        const ScvNativeSmartPtrHandle&, const ScvNativeSmartPtrHandle&) = default;
};

enum class ScvNativeValueKind : std::uint8_t {
    scalar = 1,
    aggregate = 2,
    introspection = 3,
};

enum class ScvNativeExtensionStepKind : std::uint8_t {
    field = 1,
    element = 2,
};

struct ScvNativeExtensionStep {
    ScvNativeExtensionStepKind kind { ScvNativeExtensionStepKind::field };
    std::size_t index { };
};

enum class ScvNativeExtensionKind : std::uint8_t {
    boolean = 1,
    signed_integer = 2,
    unsigned_integer = 3,
    record = 4,
    array = 5,
    enumeration = 6,
    string = 7,
    bit_vector = 8,
    logic_vector = 9,
    floating_point = 10,
    unsupported = 11,
};

struct ScvNativeExtensionInfo {
    ScvNativeExtensionKind kind { ScvNativeExtensionKind::unsupported };
    std::string name;
    std::string type_name;
    std::size_t children { };
    std::size_t bit_width { };
    bool randomization_enabled { };
    bool signed_type { };
    std::optional<std::int64_t> signed_value;
    std::optional<std::uint64_t> unsigned_value;
    std::optional<std::int64_t> enum_value;
    std::string enum_name;
    std::string string_value;
    std::vector<std::uint64_t> aval_words;
    std::vector<std::uint64_t> bval_words;
};

struct ScvNativeSmartPtrInfo {
    ScvNativeSmartPtrHandle handle;
    ScvObjectId object;
    ScvNativeValueKind kind { ScvNativeValueKind::scalar };
    std::string name;
};

struct ScvNativeSmartPtrLimits {
    std::size_t max_handles { 65536U };
    std::size_t max_name_bytes { 4096U };
    std::size_t max_extension_depth { 64U };
};

class ScvNativeSmartPtrRegistry {
public:
    explicit ScvNativeSmartPtrRegistry(
        ScvNativeSmartPtrLimits limits = { });
    ~ScvNativeSmartPtrRegistry();
    ScvNativeSmartPtrRegistry(ScvNativeSmartPtrRegistry&&) noexcept;
    ScvNativeSmartPtrRegistry& operator=(
        ScvNativeSmartPtrRegistry&&) noexcept;
    ScvNativeSmartPtrRegistry(const ScvNativeSmartPtrRegistry&) = delete;
    ScvNativeSmartPtrRegistry& operator=(
        const ScvNativeSmartPtrRegistry&) = delete;

    [[nodiscard]] std::optional<ScvNativeSmartPtrHandle> create(
        ScvNativeValueKind kind,
        ScvObjectId object,
        std::string name,
        std::uint64_t random_seed,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] std::optional<ScvNativeSmartPtrHandle> copy(
        ScvNativeSmartPtrHandle source,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool assign(
        ScvNativeSmartPtrHandle destination,
        ScvNativeSmartPtrHandle source,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool release(
        ScvNativeSmartPtrHandle handle,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] std::optional<ScvNativeSmartPtrInfo> info(
        ScvNativeSmartPtrHandle handle,
        diagnostic::Engine& diagnostics) const;
    [[nodiscard]] std::optional<ScvNativeExtensionInfo> extension(
        ScvNativeSmartPtrHandle handle,
        std::span<const ScvNativeExtensionStep> path,
        diagnostic::Engine& diagnostics) const;
    [[nodiscard]] bool assign_signed(
        ScvNativeSmartPtrHandle handle,
        std::span<const ScvNativeExtensionStep> path,
        std::int64_t value,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool set_randomization(
        ScvNativeSmartPtrHandle handle,
        std::span<const ScvNativeExtensionStep> path,
        bool enabled,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool randomize(
        ScvNativeSmartPtrHandle handle,
        std::span<const ScvNativeExtensionStep> path,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] std::size_t live_handles() const noexcept;
    [[nodiscard]] static std::size_t live_native_payloads() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::systemc
