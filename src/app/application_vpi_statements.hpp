// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "sv_vpi_user.h"
#include "fsim/runtime/vpi_abi.h"

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
#include <cstddef>
#include <vpi_user.h>

namespace fsim::app::application_detail {

/// Where a VPI scope's contents are in the SystemVerilog HIR: its
/// instance's unit, then the generate blocks, task or function and named
/// blocks from the instance down to the scope.
struct VpiHirScope {
    enum class Step : std::uint8_t { generate, callable, block };
    fsim_vpi_handle_v1 handle { };
    std::uint32_t unit { };
    std::vector<std::pair<Step, std::string>> steps;
    // Per step, the generate region the elaborator selected for a generate
    // step (it tells the branches of an if that share one scope name apart,
    // IEEE 1800-2023 27.5); invalid for any other step or when unknown.
    std::vector<semantic::DeclarationId> regions;
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
        // The object a declaration is, for a name the link step resolved to
        // it (a package-qualified name, IEEE 1800-2023 26.3); 0 when the
        // declaration is no published object. May be empty.
        std::function<fsim_vpi_handle_v1(semantic::DeclarationId)> declared;
        // The object a use of `object` in an expression is: a ref obj for
        // the variable of a ref port (IEEE 1800-2023 37.15 detail 2), else
        // the object itself. May be empty.
        std::function<fsim_vpi_handle_v1(fsim_vpi_handle_v1)> reference;
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
        // The body expression of the let declaration a call or name seen
        // from a scope expands, when it is a let expression (IEEE 1800-2023
        // 11.12, 37.57). May be empty.
        std::function<std::optional<semantic::ExpressionId>(
            fsim_vpi_handle_v1, semantic::ExpressionId)>
            let_body;
        // The task or function object a user call seen from a scope invokes
        // (a call expression, or a task call statement when the flag is
        // set), as the elaborator bound it (IEEE 1800-2023 37.42
        // vpiFunction, 37.38 vpiTask); 0 when there is none. May be empty.
        std::function<fsim_vpi_handle_v1(fsim_vpi_handle_v1, bool,
            std::uint32_t)>
            callee;
        // The class method declaration a scope object is; none for any
        // other scope. May be empty.
        std::function<std::optional<semantic::DeclarationId>(
            fsim_vpi_handle_v1)>
            method;
        // The automatic variable of a declaration of the class method
        // `scope` in the frame of the call of that method the current
        // thread runs (37.43 vpiAutomatics); 0 outside such a frame. May be
        // empty.
        std::function<fsim_vpi_handle_v1(
            fsim_vpi_handle_v1, semantic::DeclarationId)>
            frame_local;
        // A property named through the implicit `this` in the body of
        // class method `scope`, by its member identity (8.11): the
        // property of the object the current thread's call of the method
        // runs on, else the class's property; 0 when unknown. May be empty.
        std::function<fsim_vpi_handle_v1(fsim_vpi_handle_v1, const std::string&)>
            member;
    };

    VpiStatementModel(const semantic::sv::Hir& hir,
        const semantic::Model& semantics, Hooks hooks);

    /// vpi_iterate(vpiProcess | vpiContAssign | vpiStmt, scope): the
    /// processes and continuous assignments of an instance or generate
    /// scope, or the statements of a named block, task or function.
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> iterate_scope(
        const VpiHirScope& scope, PLI_INT32 type);
    /// vpi_iterate(vpiAliasStmt, scope): the alias statements of an instance
    /// or generate scope, one per aliased terminal but the last (IEEE
    /// 1800-2023 37.76).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> alias_statements(
        const VpiHirScope& scope);
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
    /// The immediate and deferred assertion statements (vpiImmediateAssert,
    /// vpiImmediateAssume, vpiImmediateCover) of an instance or generate
    /// scope's processes and of its tasks and functions, in source order
    /// (IEEE 1800-2023 37.49, 16.3, 16.4).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> immediate_assertions(
        const VpiHirScope& scope);
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
    /// The body of class method declaration `method` (its vpiStmt, IEEE
    /// 1800-2023 37.31, 37.41), whose statements have `scope` (the method
    /// object) as their scope; 0 for an empty body.
    [[nodiscard]] fsim_vpi_handle_v1 method_body(
        semantic::DeclarationId method, fsim_vpi_handle_v1 scope);
    /// The formals (vpiIODecl) of class method declaration `method`, whose
    /// scope is `scope` (the method object) (IEEE 1800-2023 37.41).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> method_formals(
        semantic::DeclarationId method, fsim_vpi_handle_v1 scope);
    /// The variable that holds the value of class method declaration
    /// `method` (vpiReturn, 37.41 details 1-3); 0 for a task, a void
    /// function and a constructor, which returns no value (8.7).
    [[nodiscard]] fsim_vpi_handle_v1 method_return(
        semantic::DeclarationId method, fsim_vpi_handle_v1 scope);
    /// The statement of class method `method`'s body that begins at the
    /// source position, as statement_at finds one in a process.
    [[nodiscard]] StatementPoint method_statement_at(
        semantic::DeclarationId method, fsim_vpi_handle_v1 scope,
        std::string_view file, std::uint32_t line, std::uint32_t column);
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
    /// The HIR declaration a reference node's name resolved to when the
    /// design was linked (the declaration its expression selects); none for
    /// any other node and for a name that resolved to none.
    [[nodiscard]] std::optional<std::uint32_t> referenced_declaration(
        std::uint64_t serial) const;
    /// vpi_handle(vpiStmt | vpiReturn, scope) of a task, function or named
    /// block (IEEE 1800-2023 37.41, 37.42).
    [[nodiscard]] std::optional<fsim_vpi_handle_v1> scope_relation(
        const VpiHirScope& scope, PLI_INT32 type);
    /// vpi_iterate(vpiIODecl, scope) of a task or function: its formal
    /// arguments (IEEE 1800-2023 37.41).
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> scope_iterate_tf(
        const VpiHirScope& scope, PLI_INT32 type);
    /// vpi_iterate(vpiIODecl, import) and vpi_handle(vpiReturn, import) of a
    /// DPI import (IEEE 1800-2023 37.41): `unit` is the id of the unit that
    /// declares it and `index` its position among that unit's DPI
    /// declarations. An import has no HIR declaration, so its formals come
    /// from the tokens of the import; the result is none for a task and a
    /// void function.
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> dpi_formals(
        std::uint32_t unit, std::uint32_t index, fsim_vpi_handle_v1 scope);
    [[nodiscard]] fsim_vpi_handle_v1 dpi_result(
        std::uint32_t unit, std::uint32_t index, fsim_vpi_handle_v1 scope);
    /// The 37.41 properties of a task or function declared in the HIR:
    /// vpiSize, vpiSigned, vpiFuncType, vpiMethod, vpiVirtual,
    /// vpiVisibility, vpiAccessType; none for other properties and scopes.
    [[nodiscard]] std::optional<std::int64_t> scope_property(
        const VpiHirScope& scope, PLI_INT32 property);
    /// Whether the callable the scope names is an `extern` prototype of an
    /// interface (IEEE 1800-2023 25.7.3): the interface tf decl of 37.8.
    [[nodiscard]] bool extern_prototype(const VpiHirScope& scope);

    /// The built-in data type of a function's return value or a formal
    /// argument, with its packed dimensions, leftmost first (IEEE 1800-2023
    /// 37.41); `word` is "logic", "bit", "int", ... Types named by a typedef
    /// or class have none.
    struct DeclaredType {
        std::string word;
        bool is_signed { };
        std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
        /// The typedef a formal or result is declared with (an identifier,
        /// `word` is then empty), and the callable scope that declares it:
        /// the typespec is that typedef's (37.41, 37.25).
        std::string type_name;
        /// The callable and formal ("" for the result) the type belongs to:
        /// the key of the typespec the elaborator resolved for it.
        std::string callable;
        std::string formal;
        fsim_vpi_handle_v1 scope { };
        /// A class-handle type (37.41): the identity of its class (the
        /// specialization's, else the declaration's), whose typespec is the
        /// class model's.
        std::string class_name;
        /// A container type: the kind of array (0 none, 1 fixed-size
        /// unpacked, 2 dynamic array, 3 queue, 4 associative array), its
        /// dimensions when fixed-size, and its element type (and, for an
        /// associative array, the index type's word; empty for `[*]`).
        int array_kind { };
        std::vector<std::pair<std::int64_t, std::int64_t>> dimensions;
        std::vector<DeclaredType> element;
        std::string index_word;
        /// A virtual interface type (25.9): the interface's name, and the
        /// modport's when the type names one; the typespec is an interface
        /// typespec (37.30).
        std::string interface_name;
        std::string modport_name;
    };
    /// The return type of a function scope.
    [[nodiscard]] std::optional<DeclaredType> scope_return_type(
        const VpiHirScope& scope);
    /// The expression a variable of the scope is declared with, as a handle
    /// (the object a name denotes); 0 without an initializer.
    [[nodiscard]] fsim_vpi_handle_v1 declaration_initializer(
        const VpiHirScope& scope, const std::string& name);
    /// The type of a formal argument or a return variable node.
    /// The callable a ref obj node is a formal of, and the formal's position.
    struct RefFormal {
        std::string callable;
        std::size_t position { };
        fsim_vpi_handle_v1 scope { };
    };
    [[nodiscard]] std::optional<RefFormal> ref_formal(std::uint64_t serial) const;
    /// The actual passed for formal `position` by the call that begins at
    /// file:line:column, a call expression or a task call statement, seen
    /// from `scope`; 0 when none is found (IEEE 1800-2023 37.15 detail 3).
    [[nodiscard]] fsim_vpi_handle_v1 call_actual_at(std::string_view file,
        std::uint32_t line, std::uint32_t column, std::size_t position,
        fsim_vpi_handle_v1 scope);
    /// The declared type of a variable declaration, as a typespec is built
    /// from it (37.41); none for an unknown declaration.
    [[nodiscard]] std::optional<DeclaredType> declaration_type(
        semantic::DeclarationId declaration) const;
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
    /// With `leaf`, only a name that has a component spelled `leaf` can name
    /// the object: a scope other than the declaring one reaches it by a
    /// simple name from an enclosed scope or by a hierarchical name (IEEE
    /// 1800-2023 23.7, 37.21), and the test spares resolving every other name.
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> connections(
        const VpiHirScope& scope, fsim_vpi_handle_v1 object,
        Connection connection,
        std::vector<ConnectionOwner>* owners = nullptr,
        const std::string* leaf = nullptr);

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

    /// The HIR declaration of the `ordinal`th module path, or timing check,
    /// of the specify blocks of unit `unit`, in declaration order (the
    /// ordinal elaboration numbers them with); null when there is none. A
    /// path's condition, delays and a check's events, limits and flags are
    /// expressions of it that `expression_in_scope` turns into objects
    /// (IEEE 1800-2023 37.39, 37.40).
    [[nodiscard]] const semantic::sv::ModulePathDeclaration*
    module_path_declaration(std::uint32_t unit, std::size_t ordinal) const;
    [[nodiscard]] const semantic::sv::TimingCheckDeclaration*
    timing_check_declaration(std::uint32_t unit, std::size_t ordinal) const;

    /// The vpiListOp operation of an instance array's connections (IEEE
    /// 1800-2023 37.11 detail 1): `operands` are the connection expressions
    /// in port order and `array` is its vpiParent. One operation per array.
    [[nodiscard]] fsim_vpi_handle_v1 connection_list(
        std::vector<fsim_vpi_handle_v1> operands, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 array);

    /// The pass or fail action statement of a concurrent assertion (IEEE
    /// 1800-2023 37.50 vpiStmt, vpiElseStmt), seen from the module `scope`;
    /// `parent` is the assertion.
    [[nodiscard]] fsim_vpi_handle_v1 assertion_statement(
        semantic::StatementId id, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent)
    {
        return statement_handle(id, scope, parent);
    }
    /// The clocking event of a concurrent assertion (37.50 vpiClockingEvent):
    /// the one event as an edge operation or the expression it names, or
    /// the vpiEventOrOp of several. 0 for none. `events` must outlive the
    /// model (it is a part of the HIR).
    [[nodiscard]] fsim_vpi_handle_v1 assertion_clocking_event(
        const std::vector<semantic::sv::Sensitivity>& events,
        fsim_vpi_handle_v1 scope, fsim_vpi_handle_v1 parent);

    /// The delay object of a module path (IEEE 1800-2023 37.39 vpiDelay):
    /// the one delay, or the vpiListOp operation of several; 0 for none.
    /// `delays` must outlive the model (it is a part of the HIR).
    /// An operation object of vpiOpType `op` over `operands`, for an object
    /// that has no HIR expression, such as a primitive's delay (IEEE
    /// 1800-2023 37.35 vpiDelay, 37.80). `parent` is its vpiParent; one
    /// operation exists per (`parent`, `op`, `discriminator`).
    [[nodiscard]] fsim_vpi_handle_v1 operation(PLI_INT32 op,
        std::vector<fsim_vpi_handle_v1> operands, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent, std::uint32_t discriminator = 0U);
    [[nodiscard]] fsim_vpi_handle_v1 specify_delays(
        const std::vector<semantic::sv::Delay>& delays,
        fsim_vpi_handle_v1 scope, fsim_vpi_handle_v1 parent);

    /// `vpiProperty` of an inline `cover sequence` (IEEE 1800-2023 37.50):
    /// a property spec with the assertion's clocking event, disable
    /// condition and sequence expression; 0 when it has no tree.
    [[nodiscard]] fsim_vpi_handle_v1 cover_sequence_spec(
        const semantic::sv::Unit& unit,
        const semantic::sv::ConcurrentAssertion& assertion,
        fsim_vpi_handle_v1 scope, fsim_vpi_handle_v1 parent);
    /// The let declarations of an instance or generate scope, in source
    /// order (IEEE 1800-2023 37.12 vpiLetDecl, 37.57).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> let_declarations(
        const VpiHirScope& scope);
    /// The body of the let declaration a let expression names, with its
    /// formals bound to the expression's arguments: the expression whose
    /// value the let expression has (11.12, 37.57); 0 for any other node.
    [[nodiscard]] fsim_vpi_handle_v1 let_value_expression(std::uint64_t serial);

    /// The property declarations (`vpiPropertyDecl`) or sequence
    /// declarations (`vpiSequenceDecl`) of an instance or generate scope, in
    /// source order (IEEE 1800-2023 37.51, 37.53).
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> assertion_declarations(
        const VpiHirScope& scope, PLI_INT32 type);
    /// The property or sequence declaration `name` of the scope; 0 for none.
    [[nodiscard]] fsim_vpi_handle_v1 assertion_declaration_named(
        const VpiHirScope& scope, const std::string& name);
    /// `vpiProperty` of a concurrent assertion (IEEE 1800-2023 37.50): the
    /// property instance it names, or for an inline property the property
    /// spec of the declaration the frontend synthesized. `instance` is the
    /// assertion's published instance tree, null for an inline property;
    /// `unit` is the unit that declares the assertion.
    [[nodiscard]] fsim_vpi_handle_v1 assertion_property(
        const semantic::sv::Unit& unit,
        const semantic::sv::AssertionDeclaration& declaration,
        const semantic::sv::AssertionTree* instance, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent);

    [[nodiscard]] bool owns(std::uint64_t serial) const noexcept
    {
        return serial < nodes_.size();
    }
    /// The source extent of a continuous assignment (or one of its bits) and
    /// whether its right-hand side is a literal (a constant driver never
    /// changes, IEEE 1364-2005 Table 88: no change to report), for matching
    /// the statement to the process that executes it.
    [[nodiscard]] std::optional<semantic::SourceSpanId> assignment_source(
        std::uint64_t serial) const;
    [[nodiscard]] bool assignment_constant(std::uint64_t serial) const;
    /// The continuous assignment a continuous assignment bit belongs to; the
    /// handle itself for any other node.
    [[nodiscard]] fsim_vpi_handle_v1 assignment_whole(
        std::uint64_t serial, fsim_vpi_handle_v1 handle) const;
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
    /// vpi_iterate(vpiMatchItem, sequence expr) (IEEE 1800-2023 37.54): the
    /// assignment and subroutine call statements of a sequence operand
    /// written `(s, item, ...)`, in order; none for any other handle.
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> match_items(
        fsim_vpi_handle_v1 handle);
    [[nodiscard]] std::optional<VpiStatementValue> value(
        std::uint64_t serial) const;
    /// A cast operation's casting type as written (`int`, `signed`, `8`;
    /// IEEE 1800-2023 6.24.1), or the constant size expression of a size
    /// cast written with one (`(W+1)'(x)`) as `size`; none for another node.
    struct CastTarget {
        std::string type;
        fsim_vpi_handle_v1 size { };
    };
    /// The HIR expression of a cast operation node; none for another node.
    [[nodiscard]] std::optional<semantic::ExpressionId> cast_expression(
        std::uint64_t serial) const;
    [[nodiscard]] std::optional<CastTarget> cast_target(std::uint64_t serial);

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
        // A vpiNullOp operation at position `aux` of statement `id`: a
        // skipped loop variable of a foreach statement (IEEE 1800-2023 37.51
        // detail 2) or an empty argument of a system task call (37.42
        // detail 8).
        null_operation,
        // A for loop's initialization and increment as assignments.
        loop_init,
        loop_step,
        // A variable named where no scope object holds it: one a for
        // statement declares in its initialization (IEEE 1800-2023 12.7.1),
        // `aux` the for statement and the parent the for statement's node,
        // the variable's scope (37.12 detail 2); or a formal or local of a
        // class method (8.6), `aux` the method and the parent the method
        // object. `id` is the declaration.
        local_variable,
        // A display task's format string argument.
        format,
        // A formal argument of a task or function (vpiIODecl), and the
        // implicit variable holding a function's return value (vpiReturn);
        // `id` is the declaration, `aux` the callable.
        io_decl,
        return_variable,
        // The ref obj of a `ref` formal (IEEE 1800-2023 37.15 detail 1): `id`
        // is the declaration, `aux` the callable, as for io_decl.
        ref_formal,
        // A formal and the result variable of a DPI import, which has no
        // HIR declaration: `id` is the owning unit, `aux` packs the import's
        // index (high half) and the formal's index (low half, 0xffff for
        // the result) (IEEE 1800-2023 37.41).
        dpi_io_decl,
        dpi_return_variable,
        // A concurrent assertion's clocking event: `id` indexes
        // `assertion_events_`; the list is the vpiEventOrOp of several
        // events, an event (`aux` its position) an edge operation.
        assertion_event_list,
        assertion_event,
        // The delays of a module path (IEEE 1800-2023 37.39 vpiDelay); `id`
        // indexes `specify_delays_`. The list holds several delays (a
        // vpiListOp operation); a delay with a minimum:typical:maximum
        // triple is a vpiMinTypMaxOp operation (`aux` its position in the
        // list); a value (`aux` is position * 4 + 0 primary, 1 minimum,
        // 2 typical, 3 maximum) is a constant.
        specify_delay_list,
        specify_mtm_delay,
        specify_delay_value,
        // An operation over given operands for an object with no HIR
        // expression (a primitive's delay): `id` indexes `connection_lists_`,
        // `aux` is the vpiOpType.
        synthetic_operation,
        // A property or sequence declaration, one of its formals, its
        // property spec, and an operation or instance of its expression
        // tree (IEEE 1800-2023 37.50-37.54): `id` indexes
        // `assertion_entries_`, `aux` is the formal or the tree node.
        assertion_decl,
        assertion_formal,
        assertion_spec,
        assertion_op,
        // A local variable of a property or sequence declaration (37.51,
        // 37.53 vpiVariables): `id` indexes `assertion_entries_`, `aux` the
        // local variable.
        assertion_variable,
        // One alias of a net alias statement `alias a = b = c;` (IEEE
        // 1800-2023 10.12, 37.76): `id` indexes `alias_entries_`; every
        // alias but the last terminal has the last as its right-hand side.
        alias_stmt,
        // A let declaration (`id` indexes `let_entries_`), one of its
        // formals (`aux` the port), and a let expression (`id` the call or
        // name, `aux` the let frame it is seen in) (IEEE 1800-2023 37.57).
        let_decl,
        let_formal,
        let_expr,
        // The inline constraint of a `randomize() with {...}` call (IEEE
        // 1800-2023 18.7, 37.42 detail 1): `id` is the call expression; its
        // constraint items are the block's expressions.
        inline_constraint,
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
        // The property and sequence declarations of the unit or generate
        // block.
        const std::vector<semantic::sv::AssertionDeclaration>*
            assertion_declarations { };
        // The named block a block step reached.
        const semantic::sv::Statement* block { };
        // The net alias statements of the unit or generate block.
        const std::vector<semantic::sv::Alias>* aliases { };
        // The let declarations of the unit or generate block.
        const std::vector<semantic::sv::LetDeclaration>* lets { };
    };

    /// The DPI import a dpi_io_decl or dpi_return_variable node is of.
    [[nodiscard]] const semantic::sv::DpiDeclaration* dpi_import(
        const Node& entry) const;
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
    // The statement of `statements` (or of a statement nested in them) that
    // begins at the source position, with `within` its scope and `parent`
    // the parent of the list's statements; whether it is a later pass of a
    // loop (statement_at).
    [[nodiscard]] StatementPoint find_statement(
        const std::vector<semantic::StatementId>& statements,
        fsim_vpi_handle_v1 within, fsim_vpi_handle_v1 parent,
        std::string_view file, std::uint32_t line, std::uint32_t column);
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
    // The name a connections() scan of another scope requires of a name
    // that can name the object; empty when none is required.
    std::string name_filter_;
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
    std::map<std::tuple<fsim_vpi_handle_v1, PLI_INT32, std::uint32_t>,
        std::uint32_t>
        operation_index_;
    std::map<std::tuple<Kind, std::uint32_t, std::uint32_t,
                 fsim_vpi_handle_v1, bool>,
        std::uint64_t>
        index_;
    // The event lists of assertion clocking events (parts of the HIR).
    std::vector<const std::vector<semantic::sv::Sensitivity>*>
        assertion_events_;
    std::map<const void*, std::uint32_t> assertion_event_index_;
    [[nodiscard]] fsim_vpi_handle_v1 assertion_event_handle(
        std::uint32_t list, std::uint32_t position, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent);
    // The delay lists of module paths (parts of the HIR).
    std::vector<const std::vector<semantic::sv::Delay>*> specify_delays_;
    std::map<const void*, std::uint32_t> specify_delay_index_;
    [[nodiscard]] fsim_vpi_handle_v1 specify_delay_handle(std::uint32_t set,
        std::uint32_t position, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent);
    [[nodiscard]] fsim_vpi_handle_v1 specify_value_handle(std::uint32_t set,
        std::uint32_t position, std::uint32_t which, fsim_vpi_handle_v1 scope,
        fsim_vpi_handle_v1 parent);
    [[nodiscard]] const semantic::sv::DelayValue* specify_value(
        const Node& node) const;
    // Expression operands a name resolution produced, by expression node.
    std::map<std::uint64_t, fsim_vpi_handle_v1> resolved_;

    // A property or sequence declaration with the tree its objects index:
    // the declaration's own tree, or the instance tree of an assertion.
    struct AssertionEntry {
        const semantic::sv::AssertionDeclaration* declaration { };
        const semantic::sv::AssertionTree* tree { };
        const semantic::sv::Unit* unit { };
    };
    std::vector<AssertionEntry> assertion_entries_;
    /// The net alias statement and terminal position of each alias_stmt node.
    struct AliasEntry {
        const semantic::sv::Alias* alias { };
        std::uint32_t terminal { };
    };
    std::vector<AliasEntry> alias_entries_;
    std::map<std::tuple<const void*, const void*>, std::uint32_t>
        assertion_entry_index_;
    [[nodiscard]] std::uint32_t assertion_entry(
        const semantic::sv::AssertionDeclaration& declaration,
        const semantic::sv::AssertionTree* tree,
        const semantic::sv::Unit& unit);
    // The match items of the sequence operands handed out so far, by the
    // operand's handle, with the scope they are seen from (37.54).
    struct MatchItems {
        std::vector<semantic::StatementId> items;
        fsim_vpi_handle_v1 scope { };
    };
    std::map<fsim_vpi_handle_v1, MatchItems> match_items_;
    // The strength of each boolean leaf under strong() or weak(), by its
    // expression (37.52 detail 3).
    std::map<std::uint32_t, bool> operand_strength_;
    // The node of each handle the model gave out.
    std::unordered_map<fsim_vpi_handle_v1, std::uint64_t> serial_of_;
    // Let declarations (IEEE 1800-2023 11.12, 37.57). A let frame is the
    // context a let body is seen in: a let expression, whose formals stand
    // for its arguments, or the declaration itself, whose formals are ref
    // objs of the formal decls. The expression and reference nodes of a
    // body carry their frame in `aux` (0: no let), so each let expression
    // has its own; `frame_` is the frame of the node whose relations are
    // being made.
    // The declarations standing for inline cover sequences, by assertion.
    std::deque<semantic::sv::AssertionDeclaration> cover_sequences_;
    std::map<const semantic::sv::ConcurrentAssertion*, std::size_t>
        cover_sequence_of_;
    std::vector<const semantic::sv::LetDeclaration*> let_entries_;
    std::map<const semantic::sv::LetDeclaration*, std::uint32_t> let_entry_of_;
    std::map<std::uint32_t, const semantic::sv::LetDeclaration*> let_by_body_;
    bool let_bodies_indexed_ { };
    struct LetFrame {
        const semantic::sv::LetDeclaration* declaration { };
        // The let expression; none for the declaration's own frame.
        std::optional<semantic::ExpressionId> use;
        std::uint32_t outer { };
        fsim_vpi_handle_v1 scope { };
    };
    std::vector<LetFrame> let_frames_ { LetFrame { } };
    std::map<std::tuple<const semantic::sv::LetDeclaration*, std::uint32_t,
                 std::uint32_t, fsim_vpi_handle_v1>,
        std::uint32_t>
        let_frame_of_;
    std::uint32_t frame_ { };
    [[nodiscard]] std::uint32_t let_entry(
        const semantic::sv::LetDeclaration& declaration);
    // The task or function object a call expression invokes, as the
    // elaborator bound it; 0 when there is none.
    [[nodiscard]] fsim_vpi_handle_v1 call_callee(
        std::uint32_t expression, fsim_vpi_handle_v1 scope) const;
    // vpiDecompile of an expression (37.59 detail 2); `precedence`, when
    // given, receives the binding strength of its outermost operator.
    [[nodiscard]] std::optional<std::string> decompile(
        semantic::ExpressionId id, int* precedence) const;
    [[nodiscard]] const semantic::sv::LetDeclaration* let_of_body(
        semantic::ExpressionId body);
    [[nodiscard]] const semantic::sv::LetDeclaration* let_of_use(
        const Node& entry);
    [[nodiscard]] std::uint32_t let_frame(
        const semantic::sv::LetDeclaration& declaration,
        std::optional<semantic::ExpressionId> use, std::uint32_t outer,
        fsim_vpi_handle_v1 scope);
    // The argument of a let expression for the declaration's formal
    // `port`: the actual bound to it, else its default (37.57 detail 1).
    [[nodiscard]] std::optional<semantic::ExpressionId> let_argument(
        const semantic::sv::LetDeclaration& declaration,
        semantic::ExpressionId use, std::size_t port) const;
    /// The handle of an operand of a tree node: the node, or its expression.
    [[nodiscard]] fsim_vpi_handle_v1 assertion_operand(
        std::uint32_t entry, const semantic::sv::AssertionTreeOperand& operand,
        fsim_vpi_handle_v1 scope, fsim_vpi_handle_v1 parent);
    [[nodiscard]] fsim_vpi_handle_v1 assertion_decl_handle(
        const semantic::sv::AssertionDeclaration& declaration,
        const semantic::sv::Unit& unit, fsim_vpi_handle_v1 scope);
    /// The vpiOpType of an operator (IEEE 1800-2023 37.52, 37.54).
    [[nodiscard]] static PLI_INT32 assertion_op_type(
        semantic::sv::AssertionOperator op);
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

/// The vpiType of a variable declared with `type` (IEEE 1800-2023 37.17).
[[nodiscard]] PLI_INT32 vpi_hir_variable_type(
    const semantic::sv::TypeReference& type);

} // namespace fsim::app::application_detail
