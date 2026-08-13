// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/sdf.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfScopeSelection {
    Single,
    Set,
    All,
};

enum class SdfScopeRootLanguage {
    Vhdl,
    Verilog,
    SystemVerilog,
    SystemC,
};

enum class SdfHierarchyCasePolicy {
    Sensitive,
    AsciiInsensitive,
};

struct SdfAnnotationScopeRequest {
    SdfScopeSelection selection { SdfScopeSelection::Single };
    std::vector<std::string> root_aliases;
    std::string expected_project_identity;
    std::string expected_design_identity;
};

struct SdfAnnotationScopeLimits {
    std::size_t max_roots { 4'096U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfAnnotationRoot {
    std::string alias;
    std::string selected_identity;
    SdfScopeRootLanguage language { SdfScopeRootLanguage::SystemVerilog };
    SdfHierarchyCasePolicy case_policy { SdfHierarchyCasePolicy::Sensitive };

    friend bool operator==(const SdfAnnotationRoot&,
        const SdfAnnotationRoot&) = default;
};

class SdfAnnotationScope final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const frontend::SdfIr>&
    normalized_ir() const noexcept;
    [[nodiscard]] std::string_view source_name() const noexcept;
    [[nodiscard]] std::string_view source_semantic_identity() const noexcept;
    [[nodiscard]] const std::optional<std::string>& sdf_design_name() const
        noexcept;
    [[nodiscard]] char hierarchy_divider() const noexcept;
    [[nodiscard]] std::string_view project_identity() const noexcept;
    [[nodiscard]] std::string_view design_identity() const noexcept;
    [[nodiscard]] std::span<const SdfAnnotationRoot> roots() const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfAnnotationScope(std::shared_ptr<const frontend::SdfIr> normalized_ir,
        std::string source_name, std::string source_semantic_identity,
        std::optional<std::string> sdf_design_name, char hierarchy_divider,
        std::string project_identity, std::string design_identity,
        std::vector<SdfAnnotationRoot> roots, std::string semantic_identity);

private:
    std::shared_ptr<const frontend::SdfIr> normalized_ir_;
    std::string source_name_;
    std::string source_semantic_identity_;
    std::optional<std::string> sdf_design_name_;
    char hierarchy_divider_ { '.' };
    std::string project_identity_;
    std::string design_identity_;
    std::vector<SdfAnnotationRoot> roots_;
    std::string semantic_identity_;
};

struct SdfAnnotationScopeResult {
    std::shared_ptr<const SdfAnnotationScope> scope;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfAnnotationScopeResult bind_sdf_annotation_scope(
    const frontend::SdfFile& sdf,
    const elaboration::ElaboratedDesign& elaborated,
    std::string_view project_identity, std::string_view design_identity,
    const SdfAnnotationScopeRequest& request,
    SdfAnnotationScopeLimits limits = { });

} // namespace fsim::app
