// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/source.hpp"
#include "fsim/frontend/systemverilog_scalars.hpp"
#include "fsim/frontend/token.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace fsim::frontend {
enum class UnitKind {
  VhdlEntity,
  VhdlArchitecture,
  VhdlConfiguration,
  VhdlPackage,
  VhdlContext,
  SystemVerilogPackage,
  SystemVerilogInterface,
  VerilogModule,
  SystemVerilogProgram,
  VhdlPslVerificationUnit,
};
enum class PortDirection {
  Unknown,
  Input,
  Output,
  Inout,
  Ref,
  Buffer,
};
enum class ValueDomain {
  Unknown,
  Bit2,
  Logic4,
  Logic9,
  Boolean,
  Integer,
  String,
};

struct PackedRange {
  std::int64_t left{};
  std::int64_t right{};
  bool descending{true};

  [[nodiscard]] std::uint64_t width() const noexcept;
};

enum class ExpressionKind {
  Invalid,
  Identifier,
  IntegerLiteral,
  BooleanLiteral,
  LogicLiteral,
  StringLiteral,
  Unary,
  Update,
  Binary,
  Call,
  Index,
  Slice,
  Aggregate,
  Concatenation,
  Replication,
  // Source-spanned SystemVerilog assignment-pattern `default` choice. This
  // is association metadata, never an ordinary identifier expression.
  DefaultChoice,
};

// `text` contains the identifier/literal/operator/callee. Operands retain
// source order for language-independent lowering.
struct Expression {
  ExpressionKind kind{ExpressionKind::Invalid};
  std::string text;
  std::vector<Expression> operands;
  SourceSpan span;
  // Aggregate choices parallel operands. Empty denotes positional; named,
  // `others`, `@array`, SystemVerilog `@key`, and `default` associations retain
  // their source choices in aggregate_choice_expressions. A SystemVerilog
  // default retains one source-spanned DefaultChoice node.
  std::vector<std::string> aggregate_choices{};
  std::vector<std::vector<Expression>> aggregate_choice_expressions{};
  // Set only on elaboration-internal folded VHDL enumeration constants so
  // contextual nominal typing survives substitution into comparisons and
  // conditional expressions.
  std::string nominal_type;
  // Verilog/SystemVerilog source string literals are decoded once by the
  // parser. The original token spelling remains in text for diagnostics and
  // cache/source provenance.
  std::optional<std::string> decoded_string;
  std::optional<SystemVerilogDecimalLiteral> systemverilog_decimal_literal;
  std::vector<std::string> call_argument_names;
  std::vector<PortDirection> call_argument_directions;
  std::uint64_t call_result_width{};
  ValueDomain call_result_domain{ValueDomain::Unknown};
  bool call_result_signed{};
  SystemVerilogScalarKind systemverilog_scalar_kind{
      SystemVerilogScalarKind::None};
  Expression() = default;

  Expression(
      ExpressionKind expression_kind, std::string expression_text,
      std::vector<Expression> expression_operands, SourceSpan expression_span,
      std::vector<std::string> expression_aggregate_choices = {},
      std::vector<std::vector<Expression>>
          expression_aggregate_choice_expressions = {},
      std::string expression_nominal_type = {},
      std::optional<std::string> expression_decoded_string = std::nullopt)
      : kind(expression_kind), text(std::move(expression_text)),
        operands(std::move(expression_operands)),
        span(std::move(expression_span)),
        aggregate_choices(std::move(expression_aggregate_choices)),
        aggregate_choice_expressions(
            std::move(expression_aggregate_choice_expressions)),
        nominal_type(std::move(expression_nominal_type)),
        decoded_string(std::move(expression_decoded_string)) {}

  [[nodiscard]] bool valid() const noexcept {
    return kind != ExpressionKind::Invalid;
  }
};

struct DelayAlternative {
  std::uint64_t magnitude{};
  std::uint64_t divisor{1};
  std::string unit;
  // A locally constant SystemVerilog delay expression. Literal delays keep
  // this empty. Before elaboration, `magnitude` is the physical-unit scale;
  // after project time normalization it is the tick scale applied to the
  // independently specialized expression value.
  std::optional<Expression> expression;
  SourceSpan span;
};

struct Delay {
  std::uint64_t magnitude{};
  // Exact decimal denominator retained until project time normalization.
  // Integer/VHDL delays use one.
  std::uint64_t divisor{1};
  // Empty when the source supplies no physical unit or active `timescale.
  std::string unit;
  // Present for a locally constant SystemVerilog delay expression or a VHDL
  // physical-time expression that is not the legacy integer/unit literal.
  // Language-specific semantic analysis resolves it into project ticks.
  std::optional<Expression> expression;
  // Runtime expression delays are rounded to this many project ticks after
  // applying `magnitude` as their normalized tick scale. Literal delays keep
  // the default because their rounding is completed during normalization.
  std::uint64_t rounding_quantum{1};
  // Present together only for a parenthesized min:typ:max delay triple.
  std::optional<DelayAlternative> minimum;
  std::optional<DelayAlternative> typical;
  std::optional<DelayAlternative> maximum;
  // Parenthesized transition-delay values after the first. Continuous
  // assignments accept fall and turnoff values; supported gate primitives
  // accept a fall value.
  std::vector<Delay> additional_values;
  SourceSpan span;
};

/// IEEE 1364 drive/charge strength rank, ordered from no drive to supply.
enum class VerilogStrength : std::uint8_t {
  HighZ,
  Small,
  Medium,
  Weak,
  Large,
  Pull,
  Strong,
  Supply,
};

/// Distinct strengths contributed when a Verilog driver produces zero or one.
struct VerilogDriveStrength {
  VerilogStrength zero{VerilogStrength::Strong};
  VerilogStrength one{VerilogStrength::Strong};
  SourceSpan span;

  friend bool operator==(const VerilogDriveStrength &,
                         const VerilogDriveStrength &) = default;
};

/// Charge retained by a trireg after its active drivers disconnect.
struct VerilogChargeStrength {
  VerilogStrength rank{VerilogStrength::Medium};
  SourceSpan span;

  friend bool operator==(const VerilogChargeStrength &,
                         const VerilogChargeStrength &) = default;
};

struct PackedRangeExpression {
  Expression left;
  Expression right;
  SourceSpan span;
  // VHDL ranges retain their explicit `downto`/`to` direction. Verilog and
  // SystemVerilog leave this empty and derive direction from evaluated bounds.
  std::optional<bool> descending;
};

/// A concrete scalar constraint for VHDL's bounded integer family.
///
/// This is deliberately separate from PackedRange: an integer always occupies
/// the runtime's signed 32-bit representation, regardless of the number of
/// values admitted by its subtype.
struct IntegerRange {
  std::int64_t left{};
  std::int64_t right{};
  bool descending{};

  [[nodiscard]] bool contains(const std::int64_t value) const noexcept {
    const auto lower = descending ? right : left;
    const auto upper = descending ? left : right;
    return value >= lower && value <= upper;
  }
};

struct IntegerRangeExpression {
  Expression left;
  Expression right;
  SourceSpan span;
  bool descending{};
};

/// A concrete constraint over declaration-order VHDL enumeration ordinals.
///
/// The base enumeration's literal table and nominal identity remain on Type.
/// Direction affects left/right adjacency and default initialization, while
/// membership is the inclusive interval between the two ordinals.
struct EnumerationRange {
  std::int64_t left{};
  std::int64_t right{};
  bool descending{};

  [[nodiscard]] bool contains(const std::int64_t value) const noexcept {
    const auto lower = descending ? right : left;
    const auto upper = descending ? left : right;
    return value >= lower && value <= upper;
  }
};

/// A not-yet-resolved scalar range on a named VHDL subtype indication.
///
/// Elaboration determines whether the named base is integer-family or an
/// enumeration, then moves the expression into the corresponding typed range.
struct DiscreteRangeExpression {
  Expression left;
  Expression right;
  SourceSpan span;
  bool descending{};
};

struct Type;
struct VhdlProtectedInfo;

struct PackedMember {
  std::string name;
  ValueDomain domain{ValueDomain::Unknown};
  std::string spelling;
  std::optional<PackedRange> packed_range;
  bool is_signed{};
  std::optional<PackedRangeExpression> packed_range_expression;
  // Normalized offset from the least-significant bit of the containing
  // packed aggregate. Filled once every member width is concrete.
  std::uint64_t lsb_offset{};
  SourceSpan span;
  // Empty for a scalar leaf and exactly one element for a nested packed
  // aggregate or enum member. Vector-backed recursion keeps Type value-copy
  // semantics without embedding another Type in every member.
  std::vector<Type> nested_types;
  // Optional SystemVerilog member default. Parameter substitution retains a
  // normalized contextual packed constant before runtime default construction.
  std::optional<Expression> initializer;

  [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

/// One source-ordered index subtype and optional constraint from a VHDL array
/// type declaration. The complete dimension list remains frontend metadata
/// until elaboration constructs a concrete multidimensional layout.
struct VhdlArrayDimension {
  std::string index_subtype;
  SourceSpan index_span;
  std::optional<IntegerRange> index_base_range;
  std::optional<DiscreteRangeExpression> constraint;
  // Concrete source-direction range after specialization. Null ranges remain
  // present so later semantics can distinguish them from unconstrained ones.
  std::optional<IntegerRange> range;
  bool null{};
  // Packed-bit distance between adjacent elements in this dimension. The
  // rightmost dimension varies fastest.
  std::uint64_t stride{};
  bool unconstrained{};
};

/// Source-level metadata for a VHDL array type.
///
/// The common runtime may store a supported scalar-element array in the same
/// packed representation as a built-in vector, but the frontend retains the
/// nominal array declaration, every index subtype, the complete element type,
/// and constraint state so legality and hierarchy checks never infer
/// compatibility from width alone. The scalar fields mirror the first
/// dimension and scalar element for the existing one-dimensional execution
/// path; new code should use dimensions and element_types.
struct VhdlArrayInfo {
  std::string index_subtype;
  SourceSpan index_span;
  std::optional<IntegerRange> index_base_range;
  std::string element_spelling;
  std::string element_named_type;
  SourceSpan element_span;
  ValueDomain element_domain{ValueDomain::Unknown};
  bool unconstrained{};
  // Concrete packed width across every dimension and the complete element
  // subtype. Zero denotes a concrete null array; empty denotes an indefinite
  // array whose layout still depends on an object or subtype constraint.
  std::optional<std::uint64_t> flat_width;
  std::vector<VhdlArrayDimension> dimensions;
  // Exactly one entry for a retained declaration. Vector-backed recursion
  // follows PackedMember::nested_types without embedding Type directly.
  std::vector<Type> element_types;
};

/// Source-level metadata for a VHDL access type declaration.
///
/// Vector-backed recursion follows VhdlArrayInfo::element_types: a retained
/// declaration has exactly one designated subtype, while malformed source may
/// leave the vector empty without inventing a usable pointee type.
struct VhdlAccessInfo {
  std::vector<Type> designated_types;
  SourceSpan designated_span;
  // Access values use a stable integer handle. Zero is null; positive values
  // are monotonically assigned object identities and never expose host
  // addresses. The frontend limit defaults to the full non-null 32-bit handle
  // domain; elaboration additionally applies the runtime owning-storage budget.
  std::uint32_t handle_width{32};
  std::uint32_t maximum_objects{std::numeric_limits<std::uint32_t>::max()};
  bool nullable{true};
  bool owns_designated_object{true};
  // Explicit deallocation is outside the bounded v1 subset, so allocated
  // objects remain alive through the enclosing simulation lifetime.
  bool simulation_lifetime{true};
};

/// Source-level metadata for a VHDL file type declaration. The element
/// subtype remains vector-backed for the same value-copyable recursion used
/// by access and array types; a well-formed declaration has exactly one
/// entry.
struct VhdlFileInfo {
  std::vector<Type> element_types;
  SourceSpan element_span;
};

/// One source-ordered unit from a VHDL physical type declaration. The primary
/// unit has no scale expression. A secondary unit retains its complete
/// physical literal expression until semantic analysis resolves unit ratios.
struct VhdlPhysicalUnit {
  std::string name;
  std::optional<Expression> scale;
  // Primary-unit multiplier after semantic analysis. The primary unit is 1;
  // each secondary unit is expressed exactly in primary-unit ticks.
  std::optional<std::int64_t> scale_factor;
  SourceSpan span;
};

/// Source-level metadata for a VHDL physical type declaration.
struct VhdlPhysicalInfo {
  std::optional<DiscreteRangeExpression> range;
  std::vector<VhdlPhysicalUnit> units;
  std::optional<IntegerRange> resolved_range;
};

enum class SystemVerilogContainerKind {
  DynamicArray,
  Queue,
  AssociativeArray,
  StaticArray,
};

/// Source-level metadata for one bounded SystemVerilog unpacked container.
///
/// The surrounding Type continues to describe one packed integral element.
/// Keeping the container kind and optional queue maximum separate prevents an
/// unpacked object from being mistaken for a wider packed vector.
struct SystemVerilogContainerInfo {
  SystemVerilogContainerKind kind{SystemVerilogContainerKind::DynamicArray};
  // Present for `[$:N]`. The expression remains specialization-aware until
  // elaboration converts the maximum index to a maximum element count.
  std::optional<Expression> queue_maximum;
  // Present for `element_type object[index_type]`. A shared indirection keeps
  // the recursive Type representation value-copyable while retaining the
  // index width, state domain, signedness, and named-type provenance.
  std::shared_ptr<Type> associative_index_type;
  // Present for a static unpacked `[left:right]` dimension. Expressions remain
  // specialization-aware until elaboration produces a bounded dense layout.
  std::optional<PackedRange> static_range;
  // Declaration-ordered static unpacked dimensions. Vector-backed storage
  // preserves value-copy isolation without embedding expression trees in
  // every Type; nonstatic containers keep this empty.
  std::vector<PackedRangeExpression> static_range_expressions;
  // Exactly one retained element type. This permits scalar/string elements
  // and recursively nested unpacked containers without making Type directly
  // self-recursive. Older flat static-array views still retain every range
  // above while this entry describes the scalar leaf.
  std::vector<Type> element_types;
  SourceSpan span;
};

struct SystemVerilogClassTypeActual {
  std::optional<std::string> name;
  Expression value;
  std::shared_ptr<Type> type_actual;
  SourceSpan span;
};

enum class PackedAggregateKind {
  None,
  Struct,
  Union,
  UnpackedStruct,
  TaggedUnion,
  UnpackedUnion,
};

struct Type {
  ValueDomain domain{ValueDomain::Unknown};
  std::string spelling;
  SystemVerilogScalarKind systemverilog_scalar{SystemVerilogScalarKind::None};
  std::string systemverilog_net_type;
  std::optional<PackedRange> packed_range;
  bool is_signed{};
  // Retained until elaboration even when packed_range is already known, so a
  // parameterized unit can be specialized independently at every instance.
  std::optional<PackedRangeExpression> packed_range_expression;
  // Source-ordered SystemVerilog packed dimensions. A single dimension keeps
  // using packed_range_expression for compatibility; two or more dimensions
  // are retained here while packed_range mirrors their flattened width once
  // every bound is concrete.
  std::vector<PackedRangeExpression> systemverilog_packed_dimensions;
  // Non-empty for a SystemVerilog user-defined type reference. Package
  // qualification is retained verbatim (for example `values::word_t`) until
  // elaboration resolves the alias in the owning specialization.
  std::string named_type;
  SourceSpan named_type_span;
  // Non-empty for a nominal VHDL type declaration. The identity follows
  // copied/imported type views and is intentionally distinct from spelling,
  // so two equally sized enumeration types never become assignment
  // compatible by accident.
  std::string nominal_type;
  // Identity of the VHDL type or subtype declaration selected by this view.
  // Unlike nominal_type, this changes when a non-nominal subtype declaration
  // is selected and is used for source/cache provenance rather than type
  // compatibility.
  std::string vhdl_type_declaration;
  // Canonical selected function name from a VHDL subtype resolution
  // indication. Empty denotes an unresolved subtype.
  std::string vhdl_resolution_function;
  // Declaration-order spelling of a VHDL enumeration's literals. Identifier
  // literals are canonicalized case-insensitively; character literals retain
  // their quoted spelling. The ordinal is the vector index.
  std::vector<std::string> enumeration_literals;
  // SystemVerilog enums retain each explicit or inferred source value in
  // declaration order. This also gives anonymous enum objects a complete
  // contextual default without synthesizing a nominal typedef.
  std::vector<Expression> systemverilog_enumeration_values;
  // Concrete or specialization-dependent constraint over the declaration
  // ordinals above. A base enumeration covers its complete ascending range;
  // derived subtypes retain their own direction and inclusive bounds.
  std::optional<EnumerationRange> enumeration_range;
  std::optional<DiscreteRangeExpression> enumeration_range_expression;
  // A derived enumeration constraint retains its resolved base independently
  // so specialization can prove containment after folding bound constants.
  std::optional<EnumerationRange> enumeration_base_range;
  std::optional<DiscreteRangeExpression> enumeration_base_range_expression;
  // Non-empty for a bounded packed struct or union. A member may retain one
  // nested aggregate or enum type in PackedMember::nested_types.
  std::vector<PackedMember> packed_members;
  PackedAggregateKind packed_aggregate{PackedAggregateKind::None};
  // Concrete or specialization-dependent VHDL scalar constraint. This never
  // changes the fixed 32-bit runtime representation returned by width().
  std::optional<IntegerRange> integer_range;
  std::optional<IntegerRangeExpression> integer_range_expression;
  // When a derived VHDL subtype adds an integer range, retain the resolved
  // base subtype's range independently so specialization can prove that the
  // derived constraint remains inside it.
  std::optional<IntegerRange> integer_base_range;
  std::optional<IntegerRangeExpression> integer_base_range_expression;
  // A range parsed on an unresolved named VHDL type. Type resolution moves
  // this to integer_range_expression or enumeration_range_expression.
  std::optional<DiscreteRangeExpression> discrete_range_expression;
  // Present only for a source-level VHDL array declaration or resolved view.
  std::optional<VhdlArrayInfo> vhdl_array;
  // Present only for a source-level VHDL access declaration or a resolved
  // view of one. Allocation and ownership semantics are added during
  // elaboration; the frontend never loses the designated subtype.
  std::optional<VhdlAccessInfo> vhdl_access;
  // Present only for a source-level VHDL file type declaration or a resolved
  // view of one. Runtime lifetime and services are semantic concerns; the
  // frontend retains the exact element subtype.
  std::optional<VhdlFileInfo> vhdl_file;
  // Present only for a source-level VHDL physical type declaration or a
  // resolved view of one. Source units and exact scale expressions remain
  // declaration ordered.
  std::optional<VhdlPhysicalInfo> vhdl_physical;
  // Present on a VHDL protected declaration or body. A shared indirection is
  // required because protected method profiles contain Type values.
  std::shared_ptr<VhdlProtectedInfo> vhdl_protected;
  // Source-ordered constraints on a named VHDL array subtype indication.
  // One-dimensional executable views also mirror their sole entry through
  // packed_range_expression for compatibility with the existing packed path.
  std::vector<DiscreteRangeExpression> vhdl_array_constraints;
  // Present only for a SystemVerilog dynamic array, queue, or associative
  // array. All scalar fields above describe one element, not the container.
  std::optional<SystemVerilogContainerInfo> systemverilog_container;
  // Resolved SystemVerilog class handles are nullable and never host pointers.
  std::string systemverilog_class_name;
  std::string systemverilog_class_declaration;
  // A virtual-interface variable is a nullable handle to one interface
  // instance, optionally restricted to a named modport view.
  bool systemverilog_virtual_interface{};
  std::string systemverilog_interface_type;
  std::string systemverilog_interface_modport;
  std::vector<SystemVerilogClassTypeActual>
      systemverilog_class_parameter_actuals;

  Type() = default;
  Type(ValueDomain domain_value, std::string spelling_value,
       std::optional<PackedRange> range_value, bool signed_value,
       std::optional<PackedRangeExpression> range_expression = {})
      : domain(domain_value), spelling(std::move(spelling_value)),
        packed_range(std::move(range_value)), is_signed(signed_value),
        packed_range_expression(std::move(range_expression)) {}

  [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

/// Compare two VHDL subtype indications for declaration/body conformance.
/// Source locations and resolved cache provenance do not participate; the
/// selected type mark, scalar domain, direction, and retained constraints do.
[[nodiscard]] bool vhdl_subtype_indications_conform(const Type &left,
                                                    const Type &right);

struct EnumLiteralDeclaration {
  std::string name;
  Expression value;
  SourceSpan span;
};

enum class TypeDeclarationKind {
  Alias,
  VhdlEnumeration,
  VhdlArray,
  VhdlAccess,
  VhdlFile,
  VhdlProtected,
  VhdlProtectedBody,
  VhdlPhysical,
  VhdlRecord,
  VhdlSubtype,
  SystemVerilogTypedef,
  VhdlIncomplete,
};

struct TypeAliasDeclaration {
  std::string name;
  Type type;
  SourceSpan span;
  // Non-empty for SystemVerilog enums and VHDL enumerations. Values retain
  // declaration order and source spans. SystemVerilog matching immutable
  // local parameters carry explicit values through specialization; VHDL
  // literals remain contextual and use declaration-order ordinals.
  std::vector<EnumLiteralDeclaration> enum_literals;
  TypeDeclarationKind declaration_kind{TypeDeclarationKind::Alias};
};

struct VhdlAttributeDeclaration {
  std::string name;
  Type type;
  bool specification{};
  std::vector<std::string> entity_names;
  std::string entity_class;
  Expression value;
  SourceSpan span;
};

struct VhdlGroupDeclaration {
  std::string name;
  bool template_declaration{};
  std::string template_name;
  std::vector<std::string> entries;
  SourceSpan span;
};

struct SignalDeclaration {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Unknown};
  bool is_port{};
  SourceSpan span;
  // A SystemVerilog net-declaration propagation delay. Elaboration applies
  // it to every continuous driver targeting this net after specialization.
  std::optional<Delay> net_delay;
  // Verilog net-declaration drive strength and trireg charge strength.
  std::optional<VerilogDriveStrength> drive_strength;
  std::optional<VerilogChargeStrength> charge_strength;
  std::optional<Delay> charge_decay;
  // Non-empty only for a SystemVerilog interface port. The optional modport
  // names the view selected after `interface_type.`; these declarations are
  // hierarchy bundles rather than independently allocated packed signals.
  std::string interface_type;
  std::string modport;
  // VHDL input-port default retained in the entity interface. Other
  // languages and non-input VHDL ports leave this empty.
  std::optional<Expression> default_value;

  SignalDeclaration() = default;
  SignalDeclaration(
      std::string signal_name, Type signal_type, PortDirection signal_direction,
      bool signal_is_port, SourceSpan signal_span,
      std::optional<Delay> signal_net_delay = std::nullopt,
      std::string signal_interface_type = {}, std::string signal_modport = {},
      std::optional<Expression> signal_default_value = std::nullopt)
      : name(std::move(signal_name)), type(std::move(signal_type)),
        direction(signal_direction), is_port(signal_is_port),
        span(std::move(signal_span)), net_delay(std::move(signal_net_delay)),
        interface_type(std::move(signal_interface_type)),
        modport(std::move(signal_modport)),
        default_value(std::move(signal_default_value)) {}
};

struct VariableDeclaration {
  std::string name;
  Type type;
  std::optional<Expression> initializer;
  SourceSpan span;
  // True only for a VHDL `shared variable` object. Ordinary process and
  // callable variables, and SystemVerilog module variables, leave this false.
  bool vhdl_shared{};
  // VHDL file objects reuse `type` for the named file subtype and
  // `initializer` for the optional external logical-name expression.
  bool vhdl_file{};
  std::optional<Expression> vhdl_file_open_kind;
  // True only for a SystemVerilog const variable declaration. Class const
  // properties retain the same qualifier on SystemVerilogClassProperty.
  bool systemverilog_const{};

  VariableDeclaration() = default;
  VariableDeclaration(
      std::string variable_name, Type variable_type,
      std::optional<Expression> variable_initializer, SourceSpan variable_span,
      bool variable_vhdl_shared = false, bool variable_vhdl_file = false,
      std::optional<Expression> variable_file_open_kind = std::nullopt)
      : name(std::move(variable_name)), type(std::move(variable_type)),
        initializer(std::move(variable_initializer)),
        span(std::move(variable_span)), vhdl_shared(variable_vhdl_shared),
        vhdl_file(variable_vhdl_file),
        vhdl_file_open_kind(std::move(variable_file_open_kind)) {}
};

struct FunctionArgument {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Input};
  SourceSpan span;
  bool reference{};
  std::optional<Expression> default_value;
  bool vhdl_file{};

  FunctionArgument() = default;
  FunctionArgument(std::string argument_name, Type argument_type,
                   PortDirection argument_direction, SourceSpan argument_span,
                   bool argument_reference = false,
                   std::optional<Expression> argument_default = std::nullopt,
                   bool argument_vhdl_file = false)
      : name(std::move(argument_name)), type(std::move(argument_type)),
        direction(argument_direction), span(std::move(argument_span)),
        reference(argument_reference),
        default_value(std::move(argument_default)),
        vhdl_file(argument_vhdl_file) {}
};

enum class InterfaceObjectClass {
  Constant,
  Variable,
  File,
};

struct ProcedureArgument {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Input};
  InterfaceObjectClass object_class{InterfaceObjectClass::Constant};
  SourceSpan span;
  std::optional<Expression> default_value;
};

// A lexical VHDL block-port alias created during generate expansion. The
// alias name is scope-qualified while `actual` names the enclosing signal.
struct SignalAliasDeclaration {
  std::string name;
  std::string actual;
  Type type;
  PortDirection direction{PortDirection::Unknown};
  SourceSpan span;
};

struct InterfaceFunctionProfile {
  Type return_type;
  std::vector<FunctionArgument> arguments;
  bool pure{true};
  std::optional<std::string> default_name;
  bool default_box{};
  SourceSpan span;
};

struct InterfaceProcedureProfile {
  std::vector<ProcedureArgument> arguments;
  std::optional<std::string> default_name;
  bool default_box{};
  SourceSpan span;
};

struct ParameterOverride {
  // Empty for a positional parameter override or VHDL generic actual.
  std::optional<std::string> name;
  Expression value;
  SourceSpan span;
  // An unambiguously parsed data-type or subtype-indication actual. Identifier
  // type marks and syntactically ambiguous parenthesized VHDL constraints
  // remain in value until formal-aware elaboration disambiguates them.
  std::optional<Type> type_value;
  // VHDL box association (`<>`) requests the corresponding template
  // generic's default rather than supplying an expression or subtype.
  bool default_box{};

  ParameterOverride() = default;

  ParameterOverride(std::optional<std::string> parameter_name,
                    Expression parameter_value, SourceSpan parameter_span,
                    std::optional<Type> parameter_type_value = std::nullopt,
                    const bool parameter_default_box = false)
      : name(std::move(parameter_name)), value(std::move(parameter_value)),
        span(std::move(parameter_span)),
        type_value(std::move(parameter_type_value)),
        default_box(parameter_default_box) {}
};

struct InterfacePackageProfile {
  // Canonical VHDL selected name of the generic package template.
  std::string template_name;
  // Explicit template-generic associations retained in declaration order.
  std::vector<ParameterOverride> generic_map;
  // `generic map (<>)` leaves the complete template map open for the
  // package-instance actual selected at the enclosing generic association.
  bool generic_map_box{};
  SourceSpan span;
};

struct PackageInstantiation {
  std::string name;
  std::string template_name;
  std::vector<ParameterOverride> generic_map;
  bool generic_map_box{};
  SourceSpan span;
};

enum class PortActualKind {
  Expression,
  Default,
  Open,
};

struct PortConnection {
  // Empty for a positional connection.
  std::optional<std::string> port;
  Expression value;
  // VHDL `open` is retained independently from an invalid or unsupported
  // expression. Omitted formals remain absent until elaboration normalizes
  // them against the selected component profile.
  PortActualKind kind{PortActualKind::Expression};
  SourceSpan span;
};

enum class VhdlInstantiationSelectionKind {
  Labels,
  All,
  Others,
};

enum class VhdlBindingAspectKind {
  Entity,
  Configuration,
  Open,
};

/// A bounded VHDL binding indication retained by either an architecture
/// configuration specification or a component configuration.
struct VhdlBindingIndication {
  VhdlBindingAspectKind kind{VhdlBindingAspectKind::Entity};
  // Canonical selected entity name, normally `library.entity`.
  std::string entity_name;
  std::string architecture_name;
  // Canonical selected configuration name, normally `library.configuration`.
  std::string configuration_name;
  std::vector<ParameterOverride> generic_map;
  std::vector<PortConnection> port_map;
  SourceSpan span;
};

/// A component instantiation-list plus its selected entity aspect.
struct VhdlComponentConfiguration {
  VhdlInstantiationSelectionKind selection{
      VhdlInstantiationSelectionKind::Labels};
  std::vector<std::string> labels;
  std::string component_name;
  VhdlBindingIndication binding;
  SourceSpan span;
};

/// The bounded block configuration admitted by the v1 configuration slice.
struct VhdlBlockConfiguration {
  std::string block_name;
  // An explicitly selected one-dimensional generate occurrence.
  std::optional<Expression> generate_index;
  std::vector<VhdlComponentConfiguration> component_configurations;
  std::vector<VhdlBlockConfiguration> block_configurations;
  SourceSpan span;
};

/// Payload of a VHDL configuration declaration DesignUnit.
struct VhdlConfigurationDeclaration {
  VhdlBlockConfiguration block;
  SourceSpan span;
};

/// One port formal retained by a VHDL component declaration. A default is
/// resolved in the declaration's visibility and is materialized only when an
/// input formal is omitted or explicitly associated with `open`.
struct VhdlComponentPort {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Unknown};
  std::optional<Expression> default_value;
  SourceSpan span;
};

enum class ParameterKind {
  Value,
  Type,
  Function,
  Procedure,
  Package,
};

struct ParameterDeclaration {
  std::string name;
  Type type;
  // Invalid denotes a VHDL generic without a default. SystemVerilog value
  // parameters currently require a parsed default.
  Expression default_value;
  bool local{};
  SourceSpan span;
  // VHDL-2008 interface type generics are type formals rather than
  // constant/value formals. SystemVerilog type parameters use the same
  // language-neutral distinction.
  ParameterKind kind{ParameterKind::Value};
  // SystemVerilog type parameters retain a data-type default separately from
  // value expressions. Empty denotes a required type actual.
  std::optional<Type> default_type;
  // VHDL-2008 interface function generics retain their complete supported
  // profile and optional named/box default independently from value/type
  // defaults.
  std::optional<InterfaceFunctionProfile> function_profile;
  // VHDL-2008 interface procedure generics retain formal object classes,
  // modes, types, and optional named/box defaults separately from functions.
  std::optional<InterfaceProcedureProfile> procedure_profile;
  // VHDL-2008 interface package generics retain the selected template and
  // template-generic compatibility map independently from every other
  // generic family.
  std::optional<InterfacePackageProfile> package_profile;
  // VHDL value generics normalize implicit and explicit interface object
  // syntax to constant class and input mode.
  InterfaceObjectClass object_class{InterfaceObjectClass::Constant};
  PortDirection direction{PortDirection::Input};
  // A constant declaration without an initializer in a package declaration.
  // Its full declaration must appear in the corresponding package body.
  bool vhdl_deferred{};
  std::optional<SourceSpan> vhdl_completion_span;
  ParameterDeclaration() = default;

  ParameterDeclaration(std::string parameter_name, Type parameter_type,
                       Expression parameter_default, bool parameter_local,
                       SourceSpan parameter_span)
      : name(std::move(parameter_name)), type(std::move(parameter_type)),
        default_value(std::move(parameter_default)), local(parameter_local),
        span(std::move(parameter_span)) {}

  ParameterDeclaration(std::string parameter_name, Type parameter_type,
                       Expression parameter_default, bool parameter_local,
                       SourceSpan parameter_span, ParameterKind parameter_kind,
                       std::optional<Type> parameter_default_type)
      : name(std::move(parameter_name)), type(std::move(parameter_type)),
        default_value(std::move(parameter_default)), local(parameter_local),
        span(std::move(parameter_span)), kind(parameter_kind),
        default_type(std::move(parameter_default_type)) {}

  ParameterDeclaration(std::string parameter_name, Type parameter_type,
                       Expression parameter_default, bool parameter_local,
                       SourceSpan parameter_span, ParameterKind parameter_kind,
                       std::optional<Type> parameter_default_type,
                       InterfaceObjectClass parameter_object_class,
                       PortDirection parameter_direction)
      : name(std::move(parameter_name)), type(std::move(parameter_type)),
        default_value(std::move(parameter_default)), local(parameter_local),
        span(std::move(parameter_span)), kind(parameter_kind),
        default_type(std::move(parameter_default_type)),
        object_class(parameter_object_class), direction(parameter_direction) {}
};

enum class VhdlComponentDeclarationRegion {
  Architecture,
  Entity,
  Package,
  Block,
  Generate,
};

/// A VHDL component declaration retained in declaration and lexical order.
///
/// Generic formals reuse ParameterDeclaration so value types and defaults have
/// the same representation as entity generics. The optional end name is
/// retained even though a mismatched name is diagnosed by the frontend.
struct VhdlComponentDeclaration {
  std::string name;
  std::vector<ParameterDeclaration> generics;
  std::vector<VhdlComponentPort> ports;
  std::optional<std::string> end_name;
  VhdlComponentDeclarationRegion region{
      VhdlComponentDeclarationRegion::Architecture};
  // Empty for a design-unit declarative region. Generated/block declarations
  // use the canonical elaborated scope (`block.loop[1]`, for example).
  std::string scope_path;
  // Canonical library/unit owner, populated for entity/package declarations
  // and retained through direct package visibility.
  std::string owner_library;
  std::string owner_name;
  std::size_t declaration_order{};
  SourceSpan span;
};

enum class VerilogUnconnectedDrive {
  None,
  Pull0,
  Pull1,
};

struct Instance {
  // The source-language unit name is retained even when an explicit
  // cross-language manifest binding overrides it.
  std::string unit_name;
  std::string name;
  // Verilog UDPs permit an omitted instance name. The parser assigns a
  // deterministic internal hierarchy name while retaining this distinction;
  // ordinary module instances reject the form after target resolution.
  bool anonymous{};
  // True when compilation-unit declaration lookup identifies unit_name as a
  // user-defined primitive. Cross-file project resolution revalidates the
  // canonical declaration before elaboration.
  bool udp_instance{};
  // Empty for a scalar instance. A bounded SystemVerilog instance array is
  // expanded in this exact declared order after specialization.
  std::vector<std::int64_t> array_indices;
  std::vector<ParameterOverride> parameter_overrides;
  // Populated for UDP propagation-delay syntax after declaration-aware
  // frontend normalization. Module parameter overrides remain separate.
  std::optional<Delay> udp_delay;
  std::optional<VerilogDriveStrength> drive_strength;
  std::vector<PortConnection> connections;
  // True for `label: component_name ...`; false for direct entity/module
  // instantiation. VHDL configuration specifications apply only to the
  // component form.
  bool vhdl_component_instance{};
  // True for `label: configuration library.name`; the selected declaration
  // is resolved before specialization and supplies nested binding rules.
  bool vhdl_configuration_instance{};
  // Compilation-directive state at the instance declaration. Pull values
  // apply only to omitted input ports.
  VerilogUnconnectedDrive unconnected_drive{VerilogUnconnectedDrive::None};
  SourceSpan span;
};

enum class AssignmentKind {
  VhdlSignal,
  Blocking,
  NonBlocking,
  Continuous,
};

enum class ProceduralAssignmentControl {
  None,
  Delay,
  Event,
};

enum class ProceduralUpdateKind {
  None,
  Compound,
  Prefix,
  Postfix,
};

enum class VhdlDelayMechanism {
  ImplicitInertial,
  Inertial,
  Transport,
};

struct VhdlWaveformElement {
  Expression value;
  std::optional<Delay> delay;
  bool disconnect{};
  SourceSpan span;
};

struct VhdlDisconnectionSpecification {
  std::vector<std::string> signals;
  std::string type_mark;
  Delay delay;
  bool all{};
  bool others{};
  SourceSpan span;
};

enum class StatementKind {
  Assignment,
  Force,
  Release,
  If,
  Case,
  Loop,
  Break,
  Continue,
  Return,
  TaskCall,
  ProcedureCall,
  Assert,
  Delay,
  WaitOn,
  WaitUntil,
  EventTrigger,
  Fork,
  WaitFork,
  DisableFork,
  Display,
  FileClose,
  FileFlush,
  FileDisplay,
  MemoryLoad,
  ContainerMethod,
  MonitorControl,
  Report,
  Pause,
  Finish,
  Block,
  Null,
};

enum class ForkJoinKind {
  All,
  Any,
  None,
};

struct SubprogramAssociation {
  // Empty for a positional association.
  std::optional<std::string> formal;
  Expression value;
  SourceSpan span;
};

enum class CaseMatchKind {
  Exact,
  WildcardZ,
  WildcardXZ,
  Inside,
  Matches,
  VhdlMatching,
};

enum class CaseQualifier {
  None,
  Unique,
  Unique0,
  Priority,
};

enum class AssertionSeverity {
  Note,
  Warning,
  Error,
  Failure,
};

enum class OutputFormat {
  Binary,
  Hexadecimal,
  Octal,
  Decimal,
  Character,
  String,
  RealScientific,
  RealFixed,
  RealGeneral,
  Hierarchy,
  Time,
};

struct OutputValue {
  Expression value;
  OutputFormat format{OutputFormat::Decimal};
  std::string prefix;
  bool suppress_leading_zero{};
  std::uint32_t minimum_width{};
  bool left_justify{};
  bool zero_pad{};
};

enum class EdgeKind {
  Any,
  Positive,
  Negative,
};

struct Sensitivity {
  EdgeKind edge{EdgeKind::Any};
  std::string signal;
  SourceSpan span;
  // General packed event expressions retain their exact value graph. Direct
  // signal and wildcard controls keep the compact `signal` representation.
  Expression expression;
};

struct CaseAlternative;

enum class SystemVerilogAssertionControlKind {
  None,
  Control,
  On,
  Off,
  Kill,
  PassOn,
  PassOff,
  FailOn,
  FailOff,
  NonvacuousOn,
  VacuousOff,
};

struct Statement {
  StatementKind kind{StatementKind::Null};
  SourceSpan span;
  // Canonical source label when the language permits a labeled statement.
  // Concurrent VHDL assertions use this for stable process/debug naming.
  std::string label;

  AssignmentKind assignment_kind{AssignmentKind::Blocking};
  Expression target;
  Expression value;
  Expression condition;
  // SystemVerilog user-task invocation. Kept separate from expression calls
  // because task formals have direction and copy-out semantics.
  std::string task_name;
  std::vector<Expression> task_arguments;
  // Procedural assertion-control system tasks retain their exact policy while
  // sharing the ordinary task-call argument representation.
  SystemVerilogAssertionControlKind assertion_control{
      SystemVerilogAssertionControlKind::None};
  // Empty entries are positional; nonempty entries retain named task actuals.
  std::vector<std::string> task_argument_names;
  std::vector<FunctionArgument> class_method_arguments;
  // VHDL procedure calls retain positional or named association spans.
  std::string procedure_name;
  std::vector<SubprogramAssociation> procedure_arguments;
  // A VHDL sequential for-loop retains its implicit constant name and
  // locally-static discrete range until elaboration unrolls the body.
  std::string loop_variable;
  // SystemVerilog procedural for-loop variable declared in its initializer.
  bool loop_variable_declared{};
  // Canonical VHDL opening label for a loop, and the optional label selected
  // by exit/next on a loop-control statement.
  std::string loop_label;
  std::string loop_control_label;
  Expression loop_initial;
  Expression loop_limit;
  // Runtime SystemVerilog for loops may initialize and update different
  // objects. Canonical bounded loops retain the same identifier in both.
  Expression loop_update_target;
  // Additional comma-separated SystemVerilog for-loop updates execute in
  // source order after the legacy primary target/value pair.
  std::vector<Statement> loop_updates;
  bool loop_descending{};
  // SystemVerilog `<`/`>` loop conditions exclude the retained limit;
  // VHDL discrete ranges and SV `<=`/`>=` include it.
  bool loop_limit_exclusive{};
  // Verilog/SystemVerilog repeat statements use an anonymous ascending loop
  // from zero to the exclusive, locally-static repeat count.
  bool loop_repeat{};
  // Runtime loop forms lower to an executable SimIR backedge instead of
  // elaboration-time unrolling. Their condition is stored in `condition`.
  bool loop_runtime{};
  // SystemVerilog do-while evaluates its condition after the body. Other
  // runtime loops use the default pre-test form.
  bool loop_post_test{};
  // A parallel procedural block stores one child statement per branch.
  // Declarations and label belong to the lexical fork scope.
  ForkJoinKind fork_join_kind{ForkJoinKind::All};
  // True only for an If node synthesized from a VHDL conditional signal
  // assignment, preserving its distinct legality diagnostic.
  bool vhdl_conditional_assignment{};
  // A concurrent guarded assignment retains the keyword plus the implicit
  // block GUARD expression attached during generated-scope expansion.
  bool vhdl_guarded_assignment{};
  // A postponed concurrent assertion or procedure call executes in the same
  // read-only region as a postponed process.
  bool vhdl_postponed{};
  // Delay selected by a declarative VHDL disconnection specification. This
  // is independent of the guarded assignment's ordinary waveform delay.
  std::optional<Delay> vhdl_disconnection_delay;
  Expression vhdl_guard;
  // Verilog/SystemVerilog intra-assignment timing. The associated `delay` or
  // `sensitivities` payload is distinct from statement-level timing controls.
  ProceduralAssignmentControl procedural_assignment_control{
      ProceduralAssignmentControl::None};
  // A repeated intra-assignment event control stores its single-evaluation
  // count in loop_limit and otherwise shares ordinary event metadata.
  bool procedural_assignment_repeat{};
  // SystemVerilog update syntax remains explicit even though `value` retains
  // the normalized binary expression used by older consumers. Elaboration
  // uses this metadata to capture the lvalue once for read-modify-write.
  ProceduralUpdateKind procedural_update_kind{ProceduralUpdateKind::None};
  std::string procedural_update_operator;
  std::optional<Delay> delay;
  // A SystemVerilog ## control counts occurrences of the enclosing/default
  // clocking event rather than project-time ticks. It remains a WaitOn node
  // so existing suspension and callable-legality checks stay conservative.
  bool clocking_cycle_delay{};
  Expression clocking_cycle_count;
  // Static strength for a Verilog continuous/gate/UDP driver. Empty denotes
  // the language default strong0/strong1 contribution.
  std::optional<VerilogDriveStrength> verilog_drive_strength;
  // One directional half of a bidirectional Verilog transmission device.
  // The source expression is retained separately so elaboration/runtime can
  // break feedback cycles and propagate resistive strength without exposing
  // an artificial hierarchy object.
  bool verilog_switch_driver{};
  bool verilog_switch_bidirectional{};
  bool verilog_switch_resistive{};
  Expression verilog_switch_source;
  Expression verilog_switch_control;
  bool verilog_switch_active_high{true};
  // Present only on VHDL signal assignments. VHDL variable assignments and
  // assignments from the Verilog/SystemVerilog frontends leave this empty.
  std::optional<VhdlDelayMechanism> vhdl_delay_mechanism;
  std::optional<Delay> vhdl_rejection_limit;
  // VHDL force/release `out` mode targets this process-owned driver rather
  // than the signal's effective value. Default and `in` leave this false.
  bool vhdl_force_driving_value{};
  // VHDL signal-assignment leaves preserve their complete ordered waveform.
  // `value` and `delay` mirror the first element for source compatibility
  // with consumers that have not yet opted into the multi-element form.
  std::vector<VhdlWaveformElement> vhdl_waveform;
  bool vhdl_unaffected{};
  std::vector<Sensitivity> sensitivities;
  // Exact VHDL report and severity expressions. Literal text and predefined
  // severity literals also mirror into the compact legacy fields below.
  Expression vhdl_report_expression;
  Expression vhdl_severity_expression;
  std::string assertion_message;
  AssertionSeverity assertion_severity{AssertionSeverity::Error};
  // SystemVerilog immediate assertions retain explicit action-block
  // presence separately from their statement vectors because a null action
  // is semantically different from an omitted failure action.
  bool assertion_has_pass_action{};
  bool assertion_has_failure_action{};
  // Bounded literal language output used by Verilog/SystemVerilog output
  // tasks and VHDL report statements. Formatting operands are added
  // separately.
  std::string output_text;
  bool output_newline{true};
  bool output_postponed{};
  std::optional<OutputFormat> output_format;
  std::string output_prefix;
  std::string output_suffix;
  bool output_suppress_leading_zero{};
  std::uint32_t output_minimum_width{};
  bool output_left_justify{};
  bool output_zero_pad{};
  bool output_monitor{};
  bool monitor_enabled{};
  // Bounded SystemVerilog file tasks keep the integral handle separate from
  // ordinary output values. FileDisplay reuses the output formatting fields;
  // FileClose uses only this expression.
  Expression file_handle;
  // MemoryLoad reuses value for the file-name expression, target for the
  // static-array object, and the first two task_arguments for optional start
  // and finish bounds. The flags distinguish radix and read/write direction.
  // Keeping those operands in common slots avoids inflating this recursive
  // node for one task family.
  bool memory_hex{};
  bool memory_write{};
  // Multi-conversion and additional unformatted arguments retain source
  // order here. The legacy singular fields above remain the compact form for
  // one conversion and one value.
  std::vector<OutputValue> output_values;
  std::string output_trailing_text;

  // Block contents or the true branch/delayed statement.
  std::vector<Statement> statements;
  // The false branch of an If statement.
  std::vector<Statement> else_statements;
  // Ordered alternatives and matching policy of a Verilog/SystemVerilog case
  // statement. VHDL sequential case retains the exact default.
  CaseMatchKind case_match_kind{CaseMatchKind::Exact};
  CaseQualifier case_qualifier{CaseQualifier::None};
  std::vector<CaseAlternative> case_alternatives;
  // Declarations directly owned by a procedural block.
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<VariableDeclaration> declarations;
};

struct CaseAlternative {
  std::vector<Expression> choices;
  std::vector<Statement> statements;
  bool is_default{};
  SourceSpan span;
};

enum class ProcessKind {
  VhdlProcess,
  VerilogAlways,
  SystemVerilogAlwaysFF,
  SystemVerilogAlwaysComb,
  SystemVerilogAlwaysLatch,
  Initial,
  Final,
};

struct FunctionDeclaration;
struct ProcedureDeclaration;

struct Process {
  ProcessKind kind{ProcessKind::VhdlProcess};
  bool vhdl_postponed{};
  std::string name;
  std::vector<ParameterDeclaration> constants;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<SignalAliasDeclaration> signal_aliases;
  std::vector<PackageInstantiation> package_instances;
  std::vector<FunctionDeclaration> functions;
  std::vector<ProcedureDeclaration> procedures;
  std::vector<VariableDeclaration> variables;
  std::vector<Sensitivity> sensitivities;
  std::vector<Statement> statements;
  SourceSpan span;
  std::vector<VhdlAttributeDeclaration> vhdl_attributes;
  std::vector<VhdlGroupDeclaration> vhdl_groups;
};

/// Typed source-level HDL function.
///
/// Supported SystemVerilog functions retain explicit or implicit lifetime,
/// typed formal association metadata, and a time-free body. Frontends retain
/// the declaration separately from processes so elaboration can use eligible
/// bodies for constant evaluation and executable SimIR subroutines.
struct FunctionDeclaration {
  std::string name;
  Type return_type;
  std::vector<FunctionArgument> arguments;
  std::vector<ParameterDeclaration> constants;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<SignalAliasDeclaration> signal_aliases;
  std::vector<PackageInstantiation> package_instances;
  std::vector<FunctionDeclaration> functions;
  std::vector<ProcedureDeclaration> procedures;
  std::vector<VariableDeclaration> variables;
  std::vector<Statement> statements;
  bool automatic{};
  bool lifetime_explicit{};
  Language language{Language::SystemVerilog2017};
  bool pure{};
  bool defined{true};
  // Nonempty only for an elaborated generic-subprogram instance.
  std::string specialization_identity;
  SourceSpan span;
  // Package/body/context sources required to specialize this callable.
  std::vector<std::string> source_dependencies;
  // Nonempty on an elaboration copy made visible through a package. Used to
  // distinguish use-visible homographs from duplicates in one local region.
  std::string visibility_owner;
  std::vector<VhdlAttributeDeclaration> vhdl_attributes;
  std::vector<VhdlGroupDeclaration> vhdl_groups;
};

struct TaskArgument {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Input};
  SourceSpan span;
  bool reference{};
  std::optional<Expression> default_value;

  TaskArgument() = default;
  TaskArgument(std::string argument_name, Type argument_type,
               PortDirection argument_direction, SourceSpan argument_span,
               bool argument_reference = false,
               std::optional<Expression> argument_default = std::nullopt)
      : name(std::move(argument_name)), type(std::move(argument_type)),
        direction(argument_direction), span(std::move(argument_span)),
        reference(argument_reference),
        default_value(std::move(argument_default)) {}
};

/// Typed source-level SystemVerilog task.
///
/// Tasks are retained independently from functions because they have no
/// result object and their output/inout formals require copy-out semantics.
/// The current bounded subset admits automatic tasks with supported scheduler
/// controls and nonsuspending static or implicit-lifetime task bodies.
struct TaskDeclaration {
  std::string name;
  std::vector<TaskArgument> arguments;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<VariableDeclaration> variables;
  std::vector<Statement> statements;
  bool automatic{};
  bool lifetime_explicit{};
  SourceSpan span;
};

enum class SystemVerilogClassLifetime : std::uint8_t {
  Inherited,
  Static,
  Automatic,
};

enum class SystemVerilogClassVisibility : std::uint8_t {
  Public,
  Protected,
  Local,
};

enum class SystemVerilogClassMethodKind : std::uint8_t {
  Constructor,
  Function,
  Task,
};

/// One value or type actual in a parameterized class base selection.
///
/// Exactly one of value/type_actual is populated after successful parsing.
/// Keeping type actuals separate prevents a type spelling from being folded
/// through the value-expression machinery.
struct SystemVerilogClassParameterActual {
  std::string name;
  Expression value;
  std::optional<Type> type_actual;
  SourceSpan span;
};

struct SystemVerilogClassBase {
  std::string name;
  std::vector<SystemVerilogClassParameterActual> parameter_actuals;
  SourceSpan span;
  // Populated by class-name resolution. This is a canonical declaration
  // identity, not a source-level selected-name spelling.
  std::string declaration_identity;
};

struct SystemVerilogClassProperty {
  VariableDeclaration declaration;
  SystemVerilogClassVisibility visibility{SystemVerilogClassVisibility::Public};
  bool is_static{};
  bool is_const{};
  bool is_rand{};
  bool is_randc{};
  SourceSpan span;
};

/// A source-level class method or prototype.
///
/// Constructors have kind Constructor and an Unknown return type. Tasks and
/// functions share the same argument representation so default values and
/// reference directions remain source ordered. Extern declarations retain no
/// body; an out-of-block definition is linked by canonical_identity during
/// semantic analysis.
struct SystemVerilogClassMethod {
  std::string name;
  std::string canonical_identity;
  std::string library;
  std::string compilation_unit_identity;
  SystemVerilogClassMethodKind kind{SystemVerilogClassMethodKind::Function};
  Type return_type;
  std::vector<FunctionArgument> arguments;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<VariableDeclaration> variables;
  std::vector<Statement> statements;
  SystemVerilogClassVisibility visibility{SystemVerilogClassVisibility::Public};
  SystemVerilogClassLifetime lifetime{SystemVerilogClassLifetime::Inherited};
  bool is_static{};
  bool is_virtual{};
  bool is_pure{};
  bool is_final{};
  bool is_extern{};
  bool out_of_block_definition{};
  bool defined{true};
  SourceSpan span;
};

struct SystemVerilogClassConstraint {
  std::string name;
  std::string canonical_identity;
  std::vector<Expression> expressions;
  SystemVerilogClassVisibility visibility{SystemVerilogClassVisibility::Public};
  bool is_static{};
  bool is_pure{};
  bool is_extern{};
  bool defined{true};
  SourceSpan span;
};

} // namespace fsim::frontend
