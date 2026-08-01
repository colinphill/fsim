// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/source.hpp"
#include "fsim/frontend/token.hpp"

#include <cstdint>
#include <memory>
#include <optional>
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
// source order, so this compact tree can be lowered without
// language-specific nodes.
struct Expression {
  ExpressionKind kind{ExpressionKind::Invalid};
  std::string text;
  std::vector<Expression> operands;
  SourceSpan span;
  // VHDL aggregate choices parallel operands. Empty denotes a positional
  // association; otherwise the canonical record element name, `others`, or
  // the internal `@array` marker is retained. SystemVerilog keyed patterns use
  // the internal `@key` marker and defaulted patterns use `default`. Each
  // corresponding entry in aggregate_choice_expressions retains the parsed
  // discrete/range choices; a SystemVerilog default association retains one
  // source-spanned DefaultChoice node, record aggregates normally have one
  // identifier choice, positional associations have none, and non-aggregate
  // expressions leave both vectors empty.
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
  // SystemVerilog user-function actual names parallel operands. Empty entries
  // are positional; nonempty entries retain `.formal(expression)` syntax.
  std::vector<std::string> call_argument_names;

  Expression() = default;

  Expression(ExpressionKind expression_kind, std::string expression_text,
             std::vector<Expression> expression_operands,
             SourceSpan expression_span,
             std::vector<std::string> expression_aggregate_choices = {},
             std::vector<std::vector<Expression>>
                 expression_aggregate_choice_expressions = {},
             std::string expression_nominal_type = {},
             std::optional<std::string> expression_decoded_string =
                 std::nullopt)
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
  // Present for a locally constant SystemVerilog delay expression. The
  // expression remains specialization-aware while `magnitude` is normalized
  // into the number of project ticks per expression unit.
  std::optional<Expression> expression;
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

  [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

/// Source-level metadata for a one-dimensional VHDL array type.
///
/// The common runtime may store a supported scalar-element array in the same
/// packed representation as a built-in vector, but the frontend retains the
/// nominal array declaration, index subtype, element subtype, and constraint
/// state so legality and hierarchy checks never infer compatibility from width
/// alone.
struct VhdlArrayInfo {
  std::string index_subtype;
  SourceSpan index_span;
  std::optional<IntegerRange> index_base_range;
  std::string element_spelling;
  std::string element_named_type;
  SourceSpan element_span;
  ValueDomain element_domain{ValueDomain::Unknown};
  bool unconstrained{};
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
  SystemVerilogContainerKind kind{
      SystemVerilogContainerKind::DynamicArray};
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
  SourceSpan span;
};

enum class PackedAggregateKind {
  None,
  Struct,
  Union,
  UnpackedStruct,
};

struct Type {
  ValueDomain domain{ValueDomain::Unknown};
  std::string spelling;
  std::optional<PackedRange> packed_range;
  bool is_signed{};
  // Retained until elaboration even when packed_range is already known, so a
  // parameterized unit can be specialized independently at every instance.
  std::optional<PackedRangeExpression> packed_range_expression;
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
  // Concrete or specialization-dependent constraint over the declaration
  // ordinals above. A base enumeration covers its complete ascending range;
  // derived subtypes retain their own direction and inclusive bounds.
  std::optional<EnumerationRange> enumeration_range;
  std::optional<DiscreteRangeExpression> enumeration_range_expression;
  // A derived enumeration constraint retains its resolved base independently
  // so specialization can prove containment after folding bound constants.
  std::optional<EnumerationRange> enumeration_base_range;
  std::optional<DiscreteRangeExpression>
      enumeration_base_range_expression;
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
  // Present only for a source-level VHDL array declaration or a type/subtype
  // resolved from one. The packed range above is the concrete object
  // constraint; this metadata preserves nominal array semantics.
  std::optional<VhdlArrayInfo> vhdl_array;
  // Present only for a SystemVerilog dynamic array, queue, or associative
  // array. All scalar fields above describe one element, not the container.
  std::optional<SystemVerilogContainerInfo> systemverilog_container;

  Type() = default;
  Type(
      ValueDomain domain_value,
      std::string spelling_value,
      std::optional<PackedRange> range_value,
      bool signed_value,
      std::optional<PackedRangeExpression> range_expression = {})
      : domain(domain_value),
        spelling(std::move(spelling_value)),
        packed_range(std::move(range_value)),
        is_signed(signed_value),
        packed_range_expression(std::move(range_expression)) {}

  [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

struct EnumLiteralDeclaration {
  std::string name;
  Expression value;
  SourceSpan span;
};

enum class TypeDeclarationKind {
  Alias,
  VhdlEnumeration,
  VhdlArray,
  VhdlRecord,
  VhdlSubtype,
  SystemVerilogTypedef,
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
  TypeDeclarationKind declaration_kind{
      TypeDeclarationKind::Alias};
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
      std::string signal_name,
      Type signal_type,
      PortDirection signal_direction,
      bool signal_is_port,
      SourceSpan signal_span,
      std::optional<Delay> signal_net_delay = std::nullopt,
      std::string signal_interface_type = {},
      std::string signal_modport = {},
      std::optional<Expression> signal_default_value = std::nullopt)
      : name(std::move(signal_name)), type(std::move(signal_type)),
        direction(signal_direction), is_port(signal_is_port),
        span(std::move(signal_span)),
        net_delay(std::move(signal_net_delay)),
        interface_type(std::move(signal_interface_type)),
        modport(std::move(signal_modport)),
        default_value(std::move(signal_default_value)) {}
};

struct VariableDeclaration {
  std::string name;
  Type type;
  std::optional<Expression> initializer;
  SourceSpan span;
};

struct FunctionArgument {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Input};
  SourceSpan span;
  bool reference{};
  std::optional<Expression> default_value;

  FunctionArgument() = default;
  FunctionArgument(
      std::string argument_name,
      Type argument_type,
      PortDirection argument_direction,
      SourceSpan argument_span,
      bool argument_reference = false,
      std::optional<Expression> argument_default = std::nullopt)
      : name(std::move(argument_name)), type(std::move(argument_type)),
        direction(argument_direction), span(std::move(argument_span)),
        reference(argument_reference),
        default_value(std::move(argument_default)) {}
};

enum class InterfaceObjectClass {
  Constant,
  Variable,
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

  ParameterOverride(
      std::optional<std::string> parameter_name,
      Expression parameter_value,
      SourceSpan parameter_span,
      std::optional<Type> parameter_type_value = std::nullopt,
      const bool parameter_default_box = false)
      : name(std::move(parameter_name)),
        value(std::move(parameter_value)),
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
  ParameterDeclaration() = default;

  ParameterDeclaration(
      std::string parameter_name,
      Type parameter_type,
      Expression parameter_default,
      bool parameter_local,
      SourceSpan parameter_span,
      ParameterKind parameter_kind = ParameterKind::Value,
      std::optional<Type> parameter_default_type = std::nullopt,
      std::optional<InterfaceFunctionProfile>
          parameter_function_profile = std::nullopt,
      std::optional<InterfaceProcedureProfile>
          parameter_procedure_profile = std::nullopt,
      std::optional<InterfacePackageProfile>
          parameter_package_profile = std::nullopt,
      InterfaceObjectClass parameter_object_class =
          InterfaceObjectClass::Constant,
      PortDirection parameter_direction = PortDirection::Input)
      : name(std::move(parameter_name)),
        type(std::move(parameter_type)),
        default_value(std::move(parameter_default)),
        local(parameter_local),
        span(std::move(parameter_span)),
        kind(parameter_kind),
        default_type(std::move(parameter_default_type)),
        function_profile(std::move(parameter_function_profile)),
        procedure_profile(std::move(parameter_procedure_profile)),
        package_profile(std::move(parameter_package_profile)),
        object_class(parameter_object_class),
        direction(parameter_direction) {}
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
  // Empty for a scalar instance. A bounded SystemVerilog instance array is
  // expanded in this exact declared order after specialization.
  std::vector<std::int64_t> array_indices;
  std::vector<ParameterOverride> parameter_overrides;
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
  VerilogUnconnectedDrive unconnected_drive{
      VerilogUnconnectedDrive::None};
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
  // Empty entries are positional; nonempty entries retain named task actuals.
  std::vector<std::string> task_argument_names;
  // A VHDL sequential procedure call remains distinct from a SystemVerilog
  // task call and retains each positional or named association span.
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
  ProceduralUpdateKind procedural_update_kind{
      ProceduralUpdateKind::None};
  std::string procedural_update_operator;
  std::optional<Delay> delay;
  // Present only on VHDL signal assignments. VHDL variable assignments and
  // assignments from the Verilog/SystemVerilog frontends leave this empty.
  std::optional<VhdlDelayMechanism> vhdl_delay_mechanism;
  std::optional<Delay> vhdl_rejection_limit;
  // VHDL signal-assignment leaves preserve their complete ordered waveform.
  // `value` and `delay` mirror the first element for source compatibility
  // with consumers that have not yet opted into the multi-element form.
  std::vector<VhdlWaveformElement> vhdl_waveform;
  bool vhdl_unaffected{};
  std::vector<Sensitivity> sensitivities;
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

struct Process {
  ProcessKind kind{ProcessKind::VhdlProcess};
  std::string name;
  std::vector<VariableDeclaration> variables;
  std::vector<Sensitivity> sensitivities;
  std::vector<Statement> statements;
  SourceSpan span;
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
};

struct TaskArgument {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Input};
  SourceSpan span;
  bool reference{};
  std::optional<Expression> default_value;

  TaskArgument() = default;
  TaskArgument(
      std::string argument_name,
      Type argument_type,
      PortDirection argument_direction,
      SourceSpan argument_span,
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
  std::vector<VariableDeclaration> variables;
  std::vector<Statement> statements;
  bool automatic{};
  bool lifetime_explicit{};
  SourceSpan span;
};

/// Typed source-level VHDL procedure.
///
/// Procedures are retained independently from functions and SystemVerilog
/// tasks. The bounded v1 slice is same-language, scalar, time-free, and uses
/// deterministic copy-in/copy-out for variable-class formals.
struct ProcedureDeclaration {
  std::string name;
  std::vector<ProcedureArgument> arguments;
  std::vector<VariableDeclaration> variables;
  std::vector<Statement> statements;
  Language language{Language::Vhdl2008};
  bool defined{true};
  // Nonempty only for an elaborated generic-subprogram instance.
  std::string specialization_identity;
  SourceSpan span;
  // Package/body/context sources required to specialize this callable.
  std::vector<std::string> source_dependencies;
  // Nonempty on an elaboration copy made visible through a package. Used to
  // distinguish use-visible homographs from duplicates in one local region.
  std::string visibility_owner;
};

/// Retained VHDL-2008 generic function template.
///
/// A template is not callable. Its ordinary function declaration/body is
/// retained separately so elaboration can specialize it with the generic
/// interface before publishing a callable instance.
struct GenericFunctionTemplate {
  std::vector<ParameterDeclaration> generic_parameters;
  FunctionDeclaration function;
  // The complete generic-clause plus subprogram span. The nested function
  // span may instead identify a matching package-body implementation.
  SourceSpan span;
};

/// Retained VHDL-2008 generic procedure template.
struct GenericProcedureTemplate {
  std::vector<ParameterDeclaration> generic_parameters;
  ProcedureDeclaration procedure;
  SourceSpan span;
};

/// A declarative VHDL-2008 generic subprogram instantiation.
///
/// Function and procedure instances use separate DesignUnit collections so
/// their kind remains explicit even before template lookup succeeds.
struct GenericSubprogramInstantiation {
  std::string name;
  std::string template_name;
  std::vector<ParameterOverride> generic_map;
  bool generic_map_box{};
  SourceSpan span;
};

enum class GenerateKind {
  StaticBlock,
  Conditional,
  Iterative,
  Selection,
};

struct GenerateRegion;

struct GenerateBody {
  // Locally static VHDL constants and SystemVerilog parameters/localparams.
  // These are evaluated in declaration order during generate expansion and
  // are not externally overridable specialization parameters.
  std::vector<ParameterDeclaration> constants;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<SignalDeclaration> signals;
  std::vector<SignalAliasDeclaration> signal_aliases;
  std::vector<FunctionDeclaration> functions;
  std::vector<TaskDeclaration> tasks;
  std::vector<VhdlComponentDeclaration> vhdl_component_declarations;
  std::vector<Statement> concurrent_statements;
  std::vector<Process> processes;
  std::vector<Instance> instances;
  std::vector<GenerateRegion> generate_regions;
};

struct GenerateChoice {
  Expression left;
  std::optional<Expression> right;
  bool descending{};
  SourceSpan span;
};

struct GenerateAlternative {
  std::string scope;
  std::vector<GenerateChoice> choices;
  bool is_default{};
  GenerateBody body;
  SourceSpan span;
};

/// Elaboration-time hierarchy region. Conditional regions use `condition`
/// and both branches. Iterative regions use `variable`, `initial`,
/// `condition`, and `iteration`, with their body in `then_body`.
/// Selection regions use `condition` as the selector plus `alternatives`.
/// Static regions always elaborate `then_body`, optionally beneath
/// `then_scope`; a valid `condition` retains a VHDL block guard expression.
/// Regions recursively compose while the current executable subset admits
/// module/entity instances as leaf items.
struct GenerateRegion {
  GenerateKind kind{GenerateKind::Conditional};
  std::string then_scope;
  std::string else_scope;
  std::string variable;
  Expression initial;
  Expression condition;
  Expression iteration;
  GenerateBody then_body;
  GenerateBody else_body;
  std::vector<GenerateAlternative> alternatives;
  // VHDL block-header interfaces and association aspects. Other generate
  // kinds and SystemVerilog static regions leave these collections empty.
  std::vector<ParameterDeclaration> block_generics;
  std::vector<ParameterOverride> block_generic_map;
  std::vector<SignalDeclaration> block_ports;
  std::vector<PortConnection> block_port_map;
  SourceSpan span;
};

enum class VhdlContextItemKind {
  LibraryClause,
  UseClause,
  ContextReference,
};

struct VhdlContextItem {
  VhdlContextItemKind kind{VhdlContextItemKind::LibraryClause};
  std::vector<std::string> selected_names;
  SourceSpan span;
};

struct SystemVerilogImport {
  std::string package;
  // Empty means wildcard import.
  std::string name;
  SourceSpan span;
};

enum class SystemVerilogModportMemberKind {
  Signal,
  FunctionImport,
  FunctionExport,
  TaskImport,
  TaskExport,
};

struct SystemVerilogModportMember {
  std::string name;
  PortDirection direction{PortDirection::Unknown};
  SourceSpan span;
  SystemVerilogModportMemberKind kind{
      SystemVerilogModportMemberKind::Signal};
};

struct SystemVerilogModport {
  std::string name;
  std::vector<SystemVerilogModportMember> members;
  SourceSpan span;
};

struct SystemVerilogExport {
  std::string package;
  // Empty means every explicitly imported item from this package.
  std::string name;
  SourceSpan span;
};

struct DesignUnit {
  UnitKind kind{UnitKind::VerilogModule};
  Language language{Language::SystemVerilog2017};
  std::string library;
  std::string name;
  // For a VHDL architecture, `name` is the architecture and `primary_name`
  // is the entity it implements.
  std::string primary_name;
  // Verilog/SystemVerilog compilation-unit timing context. Empty when no
  // `timescale directive precedes this unit.
  std::string time_unit;
  std::string time_precision;
  // Verilog/SystemVerilog compilation-directive state at unit declaration.
  std::string default_nettype;
  bool is_cell{};
  // VHDL context items immediately preceding this library unit, or the
  // reusable items contained by a bounded VHDL context declaration.
  std::vector<VhdlContextItem> vhdl_context;
  // Compilation-unit or unit-local SystemVerilog package imports.
  std::vector<SystemVerilogImport> systemverilog_imports;
  // Package export/re-export declarations and interface modport views.
  std::vector<SystemVerilogExport> systemverilog_exports;
  std::vector<SystemVerilogModport> systemverilog_modports;
  // Bounded SystemVerilog packed integral typedef declarations.
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<ParameterDeclaration> parameters;
  std::vector<SignalDeclaration> ports;
  std::vector<SignalDeclaration> signals;
  std::vector<SignalAliasDeclaration> signal_aliases;
  // SystemVerilog module-scope variable objects that do not have net/signal
  // semantics. Mutable strings live here so later DesignIR lowering can give
  // them stable object identities without pretending they are packed nets.
  std::vector<VariableDeclaration> variables;
  std::vector<FunctionDeclaration> functions;
  std::vector<TaskDeclaration> tasks;
  std::vector<ProcedureDeclaration> procedures;
  // Generic subprogram templates are never directly callable. Successful
  // instances are materialized into functions/procedures during elaboration.
  std::vector<GenericFunctionTemplate> generic_function_templates;
  std::vector<GenericProcedureTemplate> generic_procedure_templates;
  std::vector<GenericSubprogramInstantiation> generic_function_instances;
  std::vector<GenericSubprogramInstantiation> generic_procedure_instances;
  // Local VHDL generic-package instances declared in this unit's declarative
  // region. Interface package formals remain ParameterKind::Package entries.
  std::vector<PackageInstantiation> package_instances;
  // Architecture-local component declarations retained in source declaration
  // order for component instantiation and configuration binding.
  std::vector<VhdlComponentDeclaration> vhdl_component_declarations;
  // Architecture declarative configuration specifications. A configuration
  // declaration instead uses vhdl_configuration on its own design unit.
  std::vector<VhdlComponentConfiguration>
      vhdl_configuration_specifications;
  std::optional<VhdlConfigurationDeclaration> vhdl_configuration;
  std::vector<Statement> concurrent_statements;
  std::vector<Process> processes;
  std::vector<Instance> instances;
  std::vector<GenerateRegion> generate_regions;
  // Semantically imported design-unit sources that affect specialization and
  // native-cache identity (for example bounded VHDL package constants).
  std::vector<std::string> source_dependencies;
  SourceSpan span;
};

struct ParsedDesign {
  std::vector<DesignUnit> units;

  [[nodiscard]] const DesignUnit* find(UnitKind kind,
                                       std::string_view name) const noexcept;
};

}  // namespace fsim::frontend
