// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/vpi_bridge.h"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "sv_vpi_user.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
        // The unpacked dimensions of a variable array object; 0 for any
        // other object (IEEE 1800-2023 37.17, 37.19).
        std::function<std::size_t(fsim_vpi_handle_v1)> unpacked_dimensions;
        // The bits of a net or variable object, or of a select of one; 0
        // when unknown (IEEE 1800-2023 37.47 cont assign bit).
        std::function<std::size_t(fsim_vpi_handle_v1)> bit_width;
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
    /// The statement of HIR process `process` of a scope, or of one of the
    /// scope's tasks and functions, that begins at `line`:`column`: the
    /// statement an execution point reports (IEEE 1800-2023 38.36.1.1
    /// cbStmt); `file` is the point's source name. 0 when none does;
    /// blocks have no execution point.
    /// A loop's later passes report the loop's end (`pass`).
    struct StatementPoint {
        fsim_vpi_handle_v1 statement { };
        bool pass { };
    };
    [[nodiscard]] StatementPoint statement_at(const VpiHirScope& scope,
        std::uint32_t process, std::string_view file, std::uint32_t line,
        std::uint32_t column);
    /// The source file and line of a named block scope (vpiFile and
    /// vpiLineNo, IEEE 1800-2023 37.3); none for other scopes.
    struct BlockSource {
        std::string file;
        std::uint32_t line { };
    };
    [[nodiscard]] std::optional<BlockSource> block_source(
        const VpiHirScope& scope);
    /// The source extent of a named block, task or function scope; none for
    /// other scopes. Used to tell whether an execution point lies inside the
    /// scope (cbDisable activity, IEEE 1800-2023 38.36.1).
    struct ScopeSpan {
        std::string logical_name;
        std::string physical_name;
        std::uint32_t begin_line { };
        std::uint32_t begin_column { };
        std::uint32_t end_line { };
        std::uint32_t end_column { };

        /// Whether `line`:`column` of `file` lies within the extent.
        [[nodiscard]] bool contains(std::string_view file,
            std::uint32_t line, std::uint32_t column) const
        {
            if (!file.empty() && file != logical_name
                && file != physical_name) {
                return false;
            }
            const auto at = std::pair { line, column };
            return at >= std::pair { begin_line, begin_column }
                && at <= std::pair { end_line, end_column };
        }
    };
    [[nodiscard]] std::optional<ScopeSpan> scope_span(const VpiHirScope& scope);
    /// Whether a task, function or named block scope declares `name`
    /// itself, as a formal or a local (IEEE 1800-2023 23.9): such a name
    /// hides the same name of the enclosing scopes.
    [[nodiscard]] bool declares(
        const VpiHirScope& scope, const std::string& name);
    /// Whether the formal or local `name` of a task or function has the
    /// automatic lifetime (IEEE 1800-2023 6.21, 13.3.1): a variable declared
    /// `automatic`, or one with no lifetime keyword in an automatic callable.
    /// None when the scope is not a callable of the HIR or declares no such
    /// name.
    [[nodiscard]] std::optional<bool> local_automatic(
        const VpiHirScope& scope, const std::string& name);
    /// vpi_handle(vpiStmt | vpiReturn, scope) of a task, function or named
    /// block (IEEE 1800-2023 37.41, 37.42).
    [[nodiscard]] std::optional<fsim_vpi_handle_v1> scope_relation(
        const VpiHirScope& scope, PLI_INT32 type);
    /// vpi_iterate(vpiIODecl, scope) of a task or function: its formal
    /// arguments (IEEE 1800-2023 37.41).
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> scope_iterate_tf(
        const VpiHirScope& scope, PLI_INT32 type);
    /// The 37.41 properties of a task or function declared in the HIR:
    /// vpiSize, vpiSigned, vpiFuncType, vpiMethod, vpiVirtual,
    /// vpiVisibility, vpiAccessType; none for other properties and scopes.
    [[nodiscard]] std::optional<std::int64_t> scope_property(
        const VpiHirScope& scope, PLI_INT32 property);

    /// The built-in data type of a function's return value or a formal
    /// argument, with its packed dimensions, leftmost first (IEEE 1800-2023
    /// 37.41); `word` is "logic", "bit", "int", ... Types named by a typedef
    /// or class have none.
    struct DeclaredType {
        std::string word;
        bool is_signed { };
        std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
    };
    /// The return type of a function scope.
    [[nodiscard]] std::optional<DeclaredType> scope_return_type(
        const VpiHirScope& scope);
    /// The type of a formal argument or a return variable node.
    [[nodiscard]] std::optional<DeclaredType> declared_type(
        std::uint64_t serial) const;

    /// What `connections` collects (IEEE 1800-2023 37.21, 37.46, 37.58):
    /// the continuous assignments, forces and procedural continuous assigns
    /// that write an object, those that read it, or every statement and
    /// continuous assignment that mentions it.
    enum class Connection : std::uint8_t { driver, load, use };
    /// The continuous assignments, forces and procedural assigns of `scope`,
    /// its tasks and functions and the statements below them that are
    /// drivers or loads of `object`, or the statements that use it. A
    /// blocking or nonblocking assignment is only a use. `scope` is the
    /// scope that declares the object. Primitive terminals are not
    /// modelled.
    /// With `owners`, one entry per returned statement says what executes
    /// it: a continuous assignment, a HIR process of the scope, or a callable
    /// (run by whichever process calls it).
    struct ConnectionOwner {
        enum class Kind : std::uint8_t { continuous, process, callable };
        Kind kind { Kind::callable };
        /// The HIR process id of Kind::process.
        std::uint32_t process { };
        /// The position of a continuous assignment among the scope's
        /// concurrent statements.
        std::uint32_t order { };
        /// Whether the statement's target names this object alone.
        bool single_target { };
    };
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> connections(
        const VpiHirScope& scope, fsim_vpi_handle_v1 object,
        Connection connection,
        std::vector<ConnectionOwner>* owners = nullptr);

    /// The handle of HIR expression `id` seen from `scope`: the object a
    /// name denotes, otherwise an expression node whose vpiParent is
    /// `parent`. A port's high connection is such an expression (IEEE
    /// 1800-2023 37.14, 37.78).
    [[nodiscard]] fsim_vpi_handle_v1 expression_in_scope(
        semantic::ExpressionId id, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent)
    {
        return expression_handle(id, scope, parent);
    }

    /// The vpiListOp operation of an instance array's connections (IEEE
    /// 1800-2023 37.11 detail 1): `operands` are the connection expressions
    /// in port order and `array` is its vpiParent. One operation per array.
    [[nodiscard]] fsim_vpi_handle_v1 connection_list(
        std::vector<fsim_vpi_handle_v1> operands, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 array);

    [[nodiscard]] bool owns(std::uint64_t serial) const noexcept
    {
        return serial < nodes_.size();
    }
    [[nodiscard]] PLI_INT32 type(std::uint64_t serial) const;
    /// A statement or an implicit block (the stmt class, IEEE 1800-2023
    /// 37.43); not a process, expression or other part of one.
    [[nodiscard]] bool is_statement(std::uint64_t serial) const;
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

    /// The delay of a continuous assignment or one of its bits (IEEE
    /// 1800-2023 37.47): the rise, fall and turn-off values written, in
    /// simulation ticks, and the scope object that declares the assignment.
    /// None for another node and for a delay computed at run time.
    struct ContinuousDelay {
        std::vector<std::uint64_t> values;
        fsim_vpi_handle_v1 scope { };
    };
    [[nodiscard]] std::optional<ContinuousDelay> continuous_delay(
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
        // A delay given by a value rather than an expression: `aux` 0 is the
        // first delay of a delay list, `aux` n its nth additional one.
        delay,
        // The delays of a continuous assignment that specifies more than one,
        // an operation of vpiOpType vpiListOp (IEEE 1800-2023 37.47, 37.80).
        delay_list,
        // The connections of an instance array, an operation of vpiOpType
        // vpiListOp (IEEE 1800-2023 37.11 detail 1): `id` indexes
        // `connection_lists_`.
        connection_list,
        cont_assign,
        // One bit of a continuous assignment's left-hand side (IEEE
        // 1800-2023 37.47): `id` is the assignment, `aux` the bit's offset.
        cont_assign_bit,
        // A name that resolves to no object.
        reference,
        // The implicit unnamed block of a statement list that is not one
        // statement: a process, callable, branch or case item body.
        body,
        // A skipped loop variable of a foreach statement: a vpiNullOp
        // operation at position `aux` (IEEE 1800-2023 37.51 detail 2).
        foreach_null,
        // A for loop's initialization and increment as assignments.
        loop_init,
        loop_step,
        // A display task's format string argument.
        format,
        // A formal argument of a task or function (vpiIODecl), and the
        // implicit variable holding a function's return value (vpiReturn);
        // `id` is the declaration, `aux` the callable.
        io_decl,
        return_variable,
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
        // The named block a block step reached.
        const semantic::sv::Statement* block { };
    };

    [[nodiscard]] fsim_vpi_handle_v1 node(Kind kind, std::uint32_t id,
        std::uint32_t aux, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent, bool of_process = false);
    // The statement a foreach's body is within the loops it was desugared
    // to, found by where its first token is (IEEE 1800-2023 12.7.3, 37.51).
    [[nodiscard]] std::optional<semantic::StatementId> foreach_body(
        const semantic::sv::Statement& record) const;
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
    /// expression_type, except that an element select of an unpacked array
    /// variable is a vpiVarSelect (IEEE 1800-2023 37.19).
    [[nodiscard]] PLI_INT32 select_aware_type(const Node& node,
        const semantic::sv::Expression& expression) const;
    /// The array variable and the index expressions (in dimension order) of
    /// an element select that is a var select; none otherwise.
    struct VarSelect {
        fsim_vpi_handle_v1 array { };
        std::vector<semantic::ExpressionId> indices;
    };
    [[nodiscard]] std::optional<VarSelect> var_select(const Node& node,
        const semantic::sv::Expression& expression) const;
    /// The declaration of the callable a scope step path ends at.
    [[nodiscard]] const semantic::sv::Declaration* callable_declaration(
        const VpiHirScope& scope);
    /// Whether the expression `id`, seen from `within`, names `object` or
    /// a part of it (an element, a member, or a select).
    [[nodiscard]] bool names_object(const semantic::sv::Expression& name,
        fsim_vpi_handle_v1 within, fsim_vpi_handle_v1 object) const;
    [[nodiscard]] bool mentions(std::optional<semantic::ExpressionId> id,
        fsim_vpi_handle_v1 within, fsim_vpi_handle_v1 object) const;
    /// Whether an assignment target selects within `object` (the root of
    /// its selects names it).
    [[nodiscard]] bool targets_object(std::optional<semantic::ExpressionId> id,
        fsim_vpi_handle_v1 within, fsim_vpi_handle_v1 object) const;
    /// Whether a continuous assignment is a net declaration assignment.
    [[nodiscard]] bool declared_net_assignment(
        const semantic::sv::Statement& record) const;
    [[nodiscard]] std::optional<semantic::SourceSpanId> source(
        const Node& node) const;
    [[nodiscard]] std::optional<std::string> system_task_name(
        const semantic::sv::Statement& statement) const;

    const semantic::sv::Hir& hir_;
    const semantic::Model& semantics_;
    Hooks hooks_;
    std::unordered_map<std::uint32_t, const semantic::sv::Statement*>
        statements_;
    // The net declaration assignments that take the delay of their net's
    // declaration (`wire #3 w = e;`), copies of the HIR statements with it
    // set; `statements_` points at them (IEEE 1800-2023 37.46 vpiDelay).
    std::deque<semantic::sv::Statement> declaration_assignments_;
    std::unordered_map<std::uint32_t, const semantic::sv::Process*>
        processes_;
    std::vector<Node> nodes_;
    // The operands of each instance array's connection list.
    std::deque<std::vector<fsim_vpi_handle_v1>> connection_lists_;
    std::map<fsim_vpi_handle_v1, std::uint32_t> connection_list_of_array_;
    std::map<std::tuple<Kind, std::uint32_t, std::uint32_t,
                 fsim_vpi_handle_v1, bool>,
        std::uint64_t>
        index_;
    // Expression operands a name resolution produced, by expression node.
    std::map<std::uint64_t, fsim_vpi_handle_v1> resolved_;
};

/// The vpiFuncType, vpiSize and vpiSigned of the result of a DPI import
/// (IEEE 1800-2023 37.40, 37.41); a task has none, a void result is size 0.
struct VpiDpiResult {
    PLI_INT32 func_type { vpiOtherFunc };
    std::int64_t size { };
    bool is_signed { };
};
[[nodiscard]] VpiDpiResult vpi_dpi_result(
    const semantic::sv::DpiDeclaration& declaration);

} // namespace fsim::app::application_detail
