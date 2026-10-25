// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <string>
#include <vector>

namespace fsim::frontend {

/// Resolve SystemVerilog class declarations and handle types in one merged
/// project snapshot. Diagnostics are appended transactionally; declarations
/// retain their source spellings and gain canonical identities on success.
[[nodiscard]] bool resolve_systemverilog_classes(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics);

namespace generate_class_detail {

[[nodiscard]] inline std::string join_scope(
    const std::string& path, const std::string& label) {
  if (label.empty()) return path;
  return path.empty() ? label : path + "::" + label;
}

template <typename Body, typename Visitor>
void visit_body(Body& body, const std::string& path, Visitor& visit);

template <typename Regions, typename Visitor>
void visit_regions(
    Regions& regions, const std::string& path, Visitor& visit) {
  for (auto& region : regions) {
    visit_body(region.then_body, join_scope(path, region.then_scope), visit);
    visit_body(region.else_body, join_scope(path, region.else_scope), visit);
    for (auto& alternative : region.alternatives) {
      visit_body(
          alternative.body, join_scope(path, alternative.scope), visit);
    }
  }
}

template <typename Body, typename Visitor>
void visit_body(Body& body, const std::string& path, Visitor& visit) {
  for (auto& declaration : body.systemverilog_classes) {
    visit(declaration, path);
  }
  visit_regions(body.generate_regions, path, visit);
}

}  // namespace generate_class_detail

/// Calls `visit(declaration, scope_path)` for each class declared directly
/// in a generate body of `unit` (IEEE 1800-2017 27.3, 8.2). `scope_path` is
/// the chain of enclosing generate block labels joined by `::`, empty when
/// the body adds no named scope. A class declared in a generate block is
/// local to that block's scope, so its canonical identity is
/// `library::unit::scope_path::name`, which lets lexical lookup from the
/// block walk outward through the enclosing scopes.
template <typename Unit, typename Visitor>
void for_each_generate_class(Unit& unit, Visitor&& visit) {
  generate_class_detail::visit_regions(
      unit.generate_regions, std::string{}, visit);
}

}  // namespace fsim::frontend
