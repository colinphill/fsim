// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/systemverilog_hir.hpp"

namespace fsim::semantic::sv {

const std::vector<Unit>& Hir::units() const noexcept { return units_; }
const std::vector<Declaration>& Hir::declarations() const noexcept {
    return declarations_;
}
const std::vector<TypeDefinition>& Hir::types() const noexcept { return types_; }
const std::vector<Expression>& Hir::expressions() const noexcept {
    return expressions_;
}
const std::vector<Statement>& Hir::statements() const noexcept {
    return statements_;
}
const std::vector<Process>& Hir::processes() const noexcept {
    return processes_;
}
const std::vector<ClassDeclaration>& Hir::classes() const noexcept {
    return classes_;
}
std::vector<Unit>& Hir::mutable_units() noexcept { return units_; }
std::vector<Declaration>& Hir::mutable_declarations() noexcept {
    return declarations_;
}
std::vector<TypeDefinition>& Hir::mutable_types() noexcept { return types_; }
std::vector<Expression>& Hir::mutable_expressions() noexcept {
    return expressions_;
}
std::vector<Statement>& Hir::mutable_statements() noexcept {
    return statements_;
}
std::vector<Process>& Hir::mutable_processes() noexcept { return processes_; }
std::vector<ClassDeclaration>& Hir::mutable_classes() noexcept {
    return classes_;
}

} // namespace fsim::semantic::sv
