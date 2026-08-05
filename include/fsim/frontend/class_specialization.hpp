// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fsim::frontend {

struct SystemVerilogClassPropertyLayout {
  std::string name;
  std::string owner_identity;
  Type type;
  std::size_t bit_offset{};
  std::size_t bit_width{};
  bool is_static{};
  std::optional<Expression> initializer;
};

struct SystemVerilogClassMethodProfile {
  std::string name;
  std::string canonical_identity;
  SystemVerilogClassMethodKind kind{
      SystemVerilogClassMethodKind::Function};
  Type return_type;
  SystemVerilogClassLifetime lifetime{
      SystemVerilogClassLifetime::Inherited};
  bool is_static{};
  std::string profile_identity;
  bool is_virtual{};
  bool is_pure{};
  std::optional<std::uint32_t> virtual_slot;
  std::vector<FunctionArgument> arguments;
  std::vector<VariableDeclaration> variables;
  std::vector<Statement> statements;
};

struct SystemVerilogClassSpecialization {
  std::string declaration_identity;
  std::string specialization_identity;
  std::vector<std::pair<std::string, std::string>> parameter_values;
  std::vector<std::pair<std::string, std::string>>
      parameter_identity_values;
  std::string base_specialization_identity;
  std::vector<SystemVerilogClassPropertyLayout> properties;
  std::vector<SystemVerilogClassMethodProfile> methods;
  std::size_t instance_bit_width{};
  std::size_t static_property_count{};
  std::vector<std::string> source_dependencies;
};

struct SystemVerilogClassSpecializationResult {
  std::vector<SystemVerilogClassSpecialization> specializations;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool ok() const;
};

/// Materialize default and explicitly referenced class specializations.
/// Inputs must first pass resolve_systemverilog_classes().
[[nodiscard]] SystemVerilogClassSpecializationResult
specialize_systemverilog_classes(const ParsedDesign& design);

}  // namespace fsim::frontend
