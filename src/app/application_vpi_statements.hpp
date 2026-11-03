// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/vpi_bridge.h"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "sv_vpi_user.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fsim::app::application_detail {

/// Where a VPI scope's contents are in the SystemVerilog HIR: its
/// instance's unit, then the generate blocks, task or function and named
/// blocks from the instance down to the scope.
struct VpiHirScope {
    enum class Step : std::uint8_t { generate, callable, block };
    fsim_vpi_handle_v1 handle { };
    std::uint32_t unit { };
    std::vector<std::pair<Step, std::string>> steps;
};

/// A constant expression's value (IEEE 1800-2023 37.81 vpiConstant).
struct VpiStatementValue {
    enum class Kind : std::uint8_t { bits, real, string };
    Kind kind { Kind::bits };
    runtime::PackedLogic4 bits;
    double real { };
    std::string text;
    bool is_signed { };
};

/// The processes, statements, expressions and continuous assignments of a
/// design, as VPI objects (IEEE 1800-2023 37.43-37.79), built on demand
/// from the HIR. A node is identified by its HIR construct and the scope it
/// occurs in, so the same occurrence always has the same handle.
class VpiStatementModel final {
public:
    struct Hooks {
        // The handle of node `serial`.
        std::function<fsim_vpi_handle_v1(std::uint64_t)> handle;
        // An object a name refers to, seen from a scope; 0 when unknown.
        std::function<fsim_vpi_handle_v1(fsim_vpi_handle_v1,
            const std::string&)>
            resolve;
        // A named block's or a callable's scope object under a scope.
        std::function<fsim_vpi_handle_v1(fsim_vpi_handle_v1,
            const std::string&)>
            child_scope;
        // The full name of a scope object.
        std::function<std::string(fsim_vpi_handle_v1)> full_name;
    };

    VpiStatementModel(const semantic::sv::Hir& hir,
        const semantic::Model& semantics, Hooks hooks);

    /// vpi_iterate(vpiProcess | vpiContAssign | vpiStmt, scope): the
    /// processes and continuous assignments of an instance or generate
    /// scope, or the statements of a named block, task or function.
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> iterate_scope(
        const VpiHirScope& scope, PLI_INT32 type);
    /// A force, release, procedural assign or deassign statement of a
    /// scope's processes and callables, with its callback reason (IEEE
    /// 1800-2023 38.36.1).
    struct ForceSite {
        fsim_vpi_handle_v1 statement { };
        PLI_INT32 reason { };
        std::uint32_t process { };
    };
    /// The statements of an instance or generate scope that force or
    /// assign (`release` false) or release or deassign an object for which
    /// `targets` holds.
    [[nodiscard]] std::vector<ForceSite> force_sites(const VpiHirScope& scope,
        bool release,
        const std::function<bool(fsim_vpi_handle_v1)>& targets);
    /// vpi_handle(vpiStmt, scope) of a task, function or named block.
    [[nodiscard]] std::optional<fsim_vpi_handle_v1> scope_relation(
        const VpiHirScope& scope, PLI_INT32 type);

    [[nodiscard]] bool owns(std::uint64_t serial) const noexcept
    {
        return serial < nodes_.size();
    }
    [[nodiscard]] PLI_INT32 type(std::uint64_t serial) const;
    [[nodiscard]] std::optional<std::int64_t> property(
        std::uint64_t serial, PLI_INT32 property) const;
    [[nodiscard]] std::optional<std::string> string_property(
        std::uint64_t serial, PLI_INT32 property) const;
    [[nodiscard]] std::optional<fsim_vpi_handle_v1> relation(
        std::uint64_t serial, PLI_INT32 type);
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> iterate(
        std::uint64_t serial, PLI_INT32 type);
    [[nodiscard]] std::optional<VpiStatementValue> value(
        std::uint64_t serial) const;

private:
    enum class Kind : std::uint8_t {
        process,
        // The event control of a process with a sensitivity list.
        process_control,
        statement,
        // The event control of an assignment's intra-assignment event.
        assignment_control,
        case_item,
        expression,
        // The event expression of a sensitivity list: one sensitivity, or
        // their vpiEventOrOp.
        sensitivity_list,
        sensitivity,
        // A delay given by a value rather than an expression.
        delay,
        cont_assign,
        // A name that resolves to no object.
        reference,
        // The implicit unnamed block of a statement list that is not one
        // statement: a process, callable, branch or case item body.
        body,
        // A for loop's initialization and increment as assignments.
        loop_init,
        loop_step,
        // A display task's format string argument.
        format,
    };

    struct Node {
        Kind kind { };
        std::uint32_t id { };
        std::uint32_t aux { };
        // The innermost scope object of the occurrence.
        fsim_vpi_handle_v1 scope { };
        fsim_vpi_handle_v1 parent { };
        // Process-level sensitivity lists belong to the process; others
        // to a statement.
        bool of_process { };
    };

    struct Container {
        std::vector<semantic::ProcessId> processes;
        std::vector<semantic::StatementId> concurrent;
        std::vector<semantic::StatementId> statements;
        std::vector<semantic::DeclarationId> declarations;
        const std::vector<semantic::sv::GenerateRegion>* generates { };
        std::optional<semantic::DeclarationId> callable;
    };

    [[nodiscard]] fsim_vpi_handle_v1 node(Kind kind, std::uint32_t id,
        std::uint32_t aux, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent, bool of_process = false);
    [[nodiscard]] fsim_vpi_handle_v1 statement_handle(
        semantic::StatementId statement, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent);
    [[nodiscard]] fsim_vpi_handle_v1 expression_handle(
        semantic::ExpressionId expression, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent);
    [[nodiscard]] std::optional<Container> locate(const VpiHirScope& scope);
    // The handle of a statement list: none, its one statement, or the
    // implicit block holding several; `owner` is a body code.
    [[nodiscard]] fsim_vpi_handle_v1 sequence(
        const std::vector<semantic::StatementId>& statements, std::uint32_t id,
        std::uint32_t owner, fsim_vpi_handle_v1 scope, fsim_vpi_handle_v1 parent,
        bool of_process = false);
    [[nodiscard]] const std::vector<semantic::StatementId>* body_statements(
        const Node& node) const;
    [[nodiscard]] std::optional<std::string> format_text(
        const semantic::sv::Statement& statement) const;
    [[nodiscard]] const semantic::sv::Statement* find_block(
        const std::vector<semantic::StatementId>& statements,
        const std::string& label) const;

    [[nodiscard]] const semantic::sv::Statement* statement(
        std::uint32_t id) const;
    [[nodiscard]] const semantic::sv::Expression* expression(
        std::uint32_t id) const;
    [[nodiscard]] const semantic::sv::Process* process(std::uint32_t id) const;
    [[nodiscard]] PLI_INT32 statement_type(
        const semantic::sv::Statement& statement) const;
    [[nodiscard]] PLI_INT32 expression_type(
        const semantic::sv::Expression& expression) const;
    [[nodiscard]] std::optional<semantic::SourceSpanId> source(
        const Node& node) const;
    [[nodiscard]] std::optional<std::string> system_task_name(
        const semantic::sv::Statement& statement) const;

    const semantic::sv::Hir& hir_;
    const semantic::Model& semantics_;
    Hooks hooks_;
    std::unordered_map<std::uint32_t, const semantic::sv::Statement*>
        statements_;
    std::unordered_map<std::uint32_t, const semantic::sv::Process*>
        processes_;
    std::vector<Node> nodes_;
    std::map<std::tuple<Kind, std::uint32_t, std::uint32_t,
                 fsim_vpi_handle_v1, bool>,
        std::uint64_t>
        index_;
    // Expression operands a name resolution produced, by expression node.
    std::map<std::uint64_t, fsim_vpi_handle_v1> resolved_;
};

} // namespace fsim::app::application_detail
