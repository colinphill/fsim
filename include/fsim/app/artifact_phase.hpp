// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::app {

enum class ArtifactPhaseKind {
  compilation,
  elaboration,
};

// Stable summary shared by tooling that must inspect artifacts without loading
// executable state. A returned record is compatible with this runtime; unknown
// schemas or ABIs fail inspection transactionally.
struct ArtifactInspection {
  ArtifactPhaseKind phase{ArtifactPhaseKind::compilation};
  std::uint32_t format{};
  std::uint32_t runtime_abi{};
  bool compatible{};
  std::optional<std::string> language;
  std::optional<std::string> standard;
  std::optional<std::string> library;
  std::vector<std::string> roots;
  std::vector<std::string> digests;
  std::vector<std::string> units;
  std::uint64_t process_count{};
};

[[nodiscard]] bool compile_artifact(
    const project::Config& config,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool elaborate_artifact(
    const project::Config& config,
    std::span<const std::filesystem::path> objects,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<ArtifactInspection> inspect_artifact(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::app
