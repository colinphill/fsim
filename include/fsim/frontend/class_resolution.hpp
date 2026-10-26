// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fsim::frontend {

/// Resolve SystemVerilog class declarations and handle types in one merged
/// project snapshot. Diagnostics are appended transactionally; declarations
/// retain their source spellings and gain canonical identities on success.
[[nodiscard]] bool resolve_systemverilog_classes(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics);

/// The owner of a class scope selection `C#(...)::m` (IEEE 1800-2017
/// 8.25.1): the parameterized selection bound to class `C`'s canonical
/// declaration identity. Consumers outside the frontend obtain the owner
/// type here rather than reconstructing frontend types themselves.
[[nodiscard]] std::shared_ptr<const Type> systemverilog_class_scope_owner(
    const Type& selection, std::string declaration_identity);

namespace generate_class_detail {

[[nodiscard]] inline std::string join_scope(
    const std::string& path, const std::string& label) {
  if (label.empty()) return path;
  return path.empty() ? label : path + "::" + label;
}

template <typename Body, typename Visitor>
void visit_body(Body& body, const std::string& path,
    const std::string& branch, Visitor& visit);

[[nodiscard]] inline std::string join_branch(
    const std::string& branch, const std::size_t region,
    const std::string_view arm) {
  auto step = std::to_string(region) + std::string{arm};
  return branch.empty() ? step : branch + "/" + step;
}

template <typename Regions, typename Visitor>
void visit_regions(Regions& regions, const std::string& path,
    const std::string& branch, Visitor& visit) {
  for (std::size_t index = 0; index < regions.size(); ++index) {
    auto& region = regions[index];
    visit_body(region.then_body, join_scope(path, region.then_scope),
        join_branch(branch, index, "t"), visit);
    visit_body(region.else_body, join_scope(path, region.else_scope),
        join_branch(branch, index, "e"), visit);
    for (std::size_t alternative = 0;
         alternative < region.alternatives.size(); ++alternative) {
      visit_body(region.alternatives[alternative].body,
          join_scope(path, region.alternatives[alternative].scope),
          join_branch(
              branch, index, "a" + std::to_string(alternative)),
          visit);
    }
  }
}

template <typename Body, typename Visitor>
void visit_body(Body& body, const std::string& path,
    const std::string& branch, Visitor& visit) {
  for (auto& declaration : body.systemverilog_classes) {
    if constexpr (std::is_invocable_v<Visitor&, decltype(declaration),
                      const std::string&, const std::string&>) {
      visit(declaration, path, branch);
    } else {
      visit(declaration, path);
    }
  }
  visit_regions(body.generate_regions, path, branch, visit);
}

}  // namespace generate_class_detail

/// Calls `visit(declaration, scope_path)` for each class declared directly
/// in a generate body of `unit` (IEEE 1800-2017 27.3, 8.2). `scope_path` is
/// the chain of enclosing generate block labels joined by `::`, empty when
/// the body adds no named scope. A class declared in a generate block is
/// local to that block's scope, so its canonical identity is
/// `library::unit::scope_path::name`, which lets lexical lookup from the
/// block walk outward through the enclosing scopes. A visitor taking a
/// third argument also receives the branch path: the arm chosen in each
/// enclosing conditional or case generate region. Arms of one region are
/// alternatives, at most one of which is instantiated (IEEE 1800-2017
/// 27.5), so classes under different arms may share a scope path.
template <typename Unit, typename Visitor>
void for_each_generate_class(Unit& unit, Visitor&& visit) {
  generate_class_detail::visit_regions(
      unit.generate_regions, std::string{}, std::string{}, visit);
}

}  // namespace fsim::frontend
