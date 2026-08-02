// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/runtime/file_operations.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::runtime::simir {
using RegisterId = std::uint32_t;
using StringRegisterId = std::uint32_t;
using StringObjectId = std::uint32_t;
using ContainerRegisterId = std::uint32_t;
using ContainerObjectId = std::uint32_t;
using FileHandle = std::uint32_t;
using SignalId = std::uint32_t;
using ProcessId = std::uint32_t;
using InstructionIndex = std::uint32_t;
enum class OutputFormat : std::uint8_t;

struct LoadConstant {
  RegisterId destination{};
  PackedLogic4 value;
};
struct ReadSignal {
  RegisterId destination{};
  SignalId signal{};
};

/// True only during the delta in which the signal most recently changed.
struct SignalEvent {
  RegisterId destination{};
  SignalId signal{};
};

/// The effective value immediately before the signal's most recent event.
struct SignalLastValue {
  RegisterId destination{};
  SignalId signal{};
};

/// Elapsed ticks since the signal's latest event, or TIME'HIGH if none.
struct SignalLastEvent {
  RegisterId destination{};
  SignalId signal{};
};

/// True during the delta following any committed signal transaction.
struct SignalActive {
  RegisterId destination{};
  SignalId signal{};
};

struct CopyRegister {
  RegisterId destination{};
  RegisterId source{};
};

inline constexpr std::size_t maximum_string_bytes = 4096;
inline constexpr std::size_t maximum_container_elements = 4096;
inline constexpr std::size_t maximum_container_predicate_nodes = 64;
inline constexpr std::size_t maximum_memory_file_bytes =
    1024U * 1024U;

using ContainerDimension = std::pair<std::int32_t, std::int32_t>;
struct ContainerType {
  std::uint32_t element_width{1};
  bool two_state{};
  bool signed_elements{};
  bool queue{};
  bool associative{};
  bool fixed{};
  std::uint32_t index_width{32};
  bool two_state_indices{};
  bool signed_indices{true};
  std::int32_t index_left{};
  std::int32_t index_right{};
  std::optional<std::uint32_t> maximum_elements;
  std::vector<ContainerDimension> dimensions;
  std::string element_nominal_type;
  friend bool operator==(const ContainerType&,
                         const ContainerType&) = default;
};
struct ContainerValue {
  ContainerType type;
  std::vector<PackedLogic4> elements;
  // Associative-array keys are kept in canonical numeric order and are
  // positionally paired with elements. Other container kinds keep this empty.
  std::vector<PackedLogic4> keys;
  friend bool operator==(const ContainerValue&,
                         const ContainerValue&) = default;
};

/// Construct the language-defined initial value for a container type. Fixed
/// unpacked arrays are materialized densely in declared-index order.
[[nodiscard]] ContainerValue
default_container_value(const ContainerType& type);

/// Apply SystemVerilog four-state conditional selection to exactly
/// compatible bounded container values.
void select_container_value(
    ContainerValue& destination,
    const PackedLogic4& condition,
    const ContainerValue& when_true,
    const ContainerValue& when_false);

/// Compare exactly compatible bounded containers with SystemVerilog logical
/// or case-equality semantics.
[[nodiscard]] PackedLogic4 compare_container_values(
    const ContainerValue& lhs,
    const ContainerValue& rhs,
    bool case_equal);

struct LoadStringConstant {
  StringRegisterId destination{};
  std::string value;
};
struct CopyStringRegister {
  StringRegisterId destination{};
  StringRegisterId source{};
};
struct ReadStringObject {
  StringRegisterId destination{};
  StringObjectId object{};
};
struct WriteStringObject {
  StringObjectId object{};
  StringRegisterId source{};
};
struct ConcatenateStrings {
  StringRegisterId destination{};
  std::vector<StringRegisterId> operands;
};

struct CompareStrings {
  RegisterId destination{};
  StringRegisterId lhs{};
  StringRegisterId rhs{};
  bool not_equal{};
};

struct StringLength {
  RegisterId destination{};
  StringRegisterId source{};
};

struct StringIndex {
  RegisterId destination{};
  StringRegisterId source{};
  RegisterId index{};
  bool signed_index{true};
};

struct StringReplaceByte {
  StringRegisterId target{};
  RegisterId index{};
  RegisterId source{};
  bool signed_index{true};
};
enum class StringMethodOperator : std::uint8_t {
  getc, putc, toupper, tolower, compare, icompare, substr, atoi, atohex,
  atooct, atobin, itoa, hextoa, octtoa, bintoa, format_packed, format_string,
  format_time};
struct StringMethod {
  StringMethodOperator operation{}; RegisterId destination{}, first{}, second{};
  StringRegisterId string_destination{}, source{}, argument{};
  OutputFormat format{}; std::uint32_t minimum_width{};
  bool signed_decimal{}, suppress_leading_zero{}, left_justify{}, zero_pad{};
};
struct ResizeContainer {
  ContainerRegisterId target{};
  RegisterId size{};
  std::optional<ContainerRegisterId> initializer{};
};

struct CopyContainerRegister {
  ContainerRegisterId destination{};
  ContainerRegisterId source{};
};

/// Select one of two exactly compatible container snapshots. A known scalar
/// condition copies one alternative. X/Z merges equal-shape four-state
/// elements bitwise and produces the empty value for differing nonstatic
/// shapes; fixed arrays always have equal shape.
struct ConditionalContainerSelect {
  ContainerRegisterId destination{};
  RegisterId condition{};
  ContainerRegisterId when_true{};
  ContainerRegisterId when_false{};
};

struct CompareContainers {
  RegisterId destination{};
  ContainerRegisterId lhs{};
  ContainerRegisterId rhs{};
  bool case_equal{};
};

struct ReadContainerObject {
  ContainerRegisterId destination{};
  ContainerObjectId object{};
};

struct WriteContainerObject {
  ContainerObjectId object{};
  ContainerRegisterId source{};
};

struct ContainerSize {
  RegisterId destination{};
  ContainerRegisterId source{};
};

enum class ContainerReductionOperator : std::uint8_t {
  sum,
  product,
  bit_and,
  bit_or,
  bit_xor,
};

enum class ContainerOrderingOperator : std::uint8_t {
  reverse,
  ascending,
  descending,
};

enum class ContainerLocatorOperator : std::uint8_t {
  minimum,
  maximum,
  unique,
  unique_index,
  find,
  find_index,
  find_first,
  find_first_index,
  find_last,
  find_last_index,
};

enum class ContainerPredicateOperator : std::uint8_t {
  item,
  index,
  constant,
  equal,
  not_equal,
  less,
  less_equal,
  greater,
  greater_equal,
  logical_and,
  logical_or,
  logical_not,
  conditional,
};

enum class ContainerPredicateValueKind : std::uint8_t {
  element,
  index,
  logical,
};

/// One node in a validated, source-ordered bounded container expression.
/// Non-leaf operands refer only to earlier nodes. The final node is the root.
struct ContainerPredicateNode {
  ContainerPredicateOperator operation{
      ContainerPredicateOperator::item};
  std::uint32_t left{};
  std::uint32_t right{};
  PackedLogic4 constant;
  ContainerPredicateValueKind value_kind{
      ContainerPredicateValueKind::element};
  std::uint32_t third{};
};

struct OrderContainer {
  ContainerOrderingOperator operation{
      ContainerOrderingOperator::reverse};
  ContainerRegisterId target{};
  std::vector<ContainerPredicateNode> key;
};

/// Reorder container elements using the deterministic SystemVerilog subset
/// policy. A nonempty key graph is evaluated once per original element.
/// Container type, size, bounds, and associative keys are unchanged.
void order_container_value(
    ContainerValue& value,
    ContainerOrderingOperator operation,
    std::span<const ContainerPredicateNode> key = {});

struct ContainerReduction {
  ContainerReductionOperator operation{
      ContainerReductionOperator::sum};
  RegisterId destination{};
  ContainerRegisterId source{};
  std::vector<ContainerPredicateNode> transformation;
};

/// Reduce container elements in their canonical storage order. Empty
/// containers use the SystemVerilog identity for the selected operation.
/// A nonempty transformation is evaluated once per source element before
/// applying the reduction.
[[nodiscard]] PackedLogic4 reduce_container_value(
    const ContainerValue& value,
    ContainerReductionOperator operation,
    std::span<const ContainerPredicateNode> transformation = {});

struct LocateContainer {
  ContainerLocatorOperator operation{
      ContainerLocatorOperator::minimum};
  ContainerRegisterId destination{};
  ContainerRegisterId source{};
  std::vector<ContainerPredicateNode> predicate;
  std::vector<ContainerPredicateNode> transformation;
};

/// Populate a queue with extrema, unique values, first-occurrence indices, or
/// predicate-selected values/indices using the deterministic SystemVerilog
/// subset policy. A nonempty transformation is evaluated once per original
/// source element and selects the comparison/identity key.
void locate_container_values(
    ContainerValue& destination,
    const ContainerValue& source,
    ContainerLocatorOperator operation,
    std::span<const ContainerPredicateNode> predicate = {},
    std::span<const ContainerPredicateNode> transformation = {});

struct ContainerRead {
  RegisterId destination{};
  ContainerRegisterId source{};
  RegisterId index{};
  bool signed_index{true};
  bool linear_index{};
};
struct ContainerWrite {
  ContainerRegisterId target{};
  RegisterId index{};
  RegisterId source{};
  bool signed_index{true};
  bool linear_index{};
};

struct DeleteContainer {
  ContainerRegisterId target{};
  std::optional<RegisterId> index{};
};

struct ContainerExists {
  RegisterId destination{};
  ContainerRegisterId source{};
  RegisterId index{};
};

enum class ContainerTraversal : std::uint8_t {
  first,
  last,
  next,
  previous,
};

struct TraverseContainer {
  RegisterId destination{};
  ContainerRegisterId source{};
  RegisterId index{};
  ContainerTraversal traversal{ContainerTraversal::first};
};

struct LoadMemory {
  ContainerRegisterId target{};
  StringRegisterId path{};
  std::optional<RegisterId> start;
  std::optional<RegisterId> finish;
  bool hexadecimal{};
  bool write{};
};

/// Parse bounded IEEE-style read-memory text into a fixed unpacked array.
/// Tokens may contain underscores, X/Z/? digits, comments, and @addresses.
void load_memory_text(
    ContainerValue& target,
    std::string_view text,
    bool hexadecimal,
    std::optional<std::int32_t> start = std::nullopt,
    std::optional<std::int32_t> finish = std::nullopt);

/// Serialize a selected fixed unpacked-array range in deterministic
/// IEEE-style binary or hexadecimal memory-file form.
[[nodiscard]] std::string write_memory_text(
    const ContainerValue& source,
    bool hexadecimal,
    std::optional<std::int32_t> start = std::nullopt,
    std::optional<std::int32_t> finish = std::nullopt);

struct PushContainer {
  ContainerRegisterId target{};
  RegisterId source{};
  bool front{};
  std::optional<RegisterId> index{};
};

struct PopContainer {
  RegisterId destination{};
  ContainerRegisterId target{};
  bool front{};
};

struct UnaryNot {
  RegisterId destination{};
  RegisterId source{};
};

/// SystemVerilog logical negation. The source may be a packed vector; the
/// destination is a scalar four-state truth value.
struct LogicalNot {
  RegisterId destination{};
  RegisterId source{};
};

enum class LogicalBinaryOperator : std::uint8_t {
  logical_and,
  logical_or,
};

/// SystemVerilog logical conjunction/disjunction. Each operand is reduced to
/// a scalar truth value independently, so operand widths may differ.
struct LogicalBinary {
  LogicalBinaryOperator operation{
      LogicalBinaryOperator::logical_and};
  RegisterId destination{};
  RegisterId lhs{};
  RegisterId rhs{};
};

enum class ReductionOperator : std::uint8_t {
  bit_and,
  bit_or,
  bit_xor,
  one_hot,
  one_hot_or_zero,
};

/// SystemVerilog unary reduction over every bit of one packed operand.
struct Reduction {
  ReductionOperator operation{ReductionOperator::bit_and};
  RegisterId destination{};
  RegisterId source{};
};

/// Count exact `1` elements of one packed operand. `X` and `Z` do not
/// contribute. The destination is a 32-bit two-state SystemVerilog `int`.
struct CountOnes {
  RegisterId destination{};
  RegisterId source{};
};

/// Count elements whose exact four-state value is selected by state_mask.
/// Bits 0 through 3 select `0`, `1`, `X`, and `Z`, respectively.
struct CountBits {
  RegisterId destination{};
  RegisterId source{};
  std::uint8_t state_mask{};
};

enum class ShiftOperator : std::uint8_t {
  logical_left,
  logical_right,
  arithmetic_right,
  arithmetic_left,
  rotate_left,
  rotate_right,
};

/// Packed shift with independently sized value and shift-count operands.
struct Shift {
  ShiftOperator operation{ShiftOperator::logical_left};
  RegisterId destination{};
  RegisterId value{};
  RegisterId amount{};
  /// Interpret amount as two's-complement and reverse the operation for a
  /// negative value. This models VHDL's signed INTEGER shift counts without
  /// changing the packed value representation.
  bool signed_amount{};
};

/// Extract a contiguous normalized bit range from one packed value.
struct Extract {
  RegisterId destination{};
  RegisterId source{};
  std::uint32_t offset{};
  std::uint32_t width{1};
};

/// Normalize a runtime signed index against an elaborated packed range.
///
/// The right bound occupies normalized offset zero in the packed runtime,
/// independent of whether the source range is ascending or descending.
struct DynamicIndex {
  RegisterId index{};
  std::int64_t left{};
  std::int64_t right{};
  std::uint32_t base_offset{};

  friend bool operator==(const DynamicIndex&, const DynamicIndex&) = default;
};

/// Extract one scalar element selected by a runtime index.
struct DynamicExtract {
  RegisterId destination{};
  RegisterId source{};
  DynamicIndex selection;
};

/// Extract a fixed-width indexed part-select from a runtime signed base;
/// out-of-range bits become X, or zero for a two-state source.
struct DynamicPartSelect {
  RegisterId destination{};
  RegisterId source{};
  RegisterId base{};
  std::int64_t left{};
  std::int64_t right{};
  std::uint32_t width{};
  bool increasing{};
  bool source_descending{};
  bool two_state{};
  std::uint32_t base_offset{};
};

/// Runtime base and fixed-width metadata shared by indexed part-select writes.
struct DynamicPartIndex {
  RegisterId base{};
  std::int64_t left{};
  std::int64_t right{};
  std::uint32_t base_offset{};
  std::uint32_t width{};
  bool increasing{};
  bool source_descending{};

  friend bool operator==(
      const DynamicPartIndex&, const DynamicPartIndex&) = default;
};
/// Replace a contiguous normalized range in a packed value.
struct Insert {
  RegisterId destination{};
  RegisterId target{};
  RegisterId source{};
  std::uint32_t offset{};
};

/// Replace one scalar element selected by a runtime index.
struct DynamicInsert {
  RegisterId destination{};
  RegisterId target{};
  RegisterId source{};
  DynamicIndex selection;
};

/// Replace representable bits; unknown or wholly out-of-range bases do nothing.
struct DynamicPartInsert {
  RegisterId destination{};
  RegisterId target{};
  RegisterId source{};
  DynamicPartIndex selection;
};
/// Concatenate packed operands in source order. The first operand occupies
/// the most-significant result bits.
struct Concatenate {
  RegisterId destination{};
  std::vector<RegisterId> operands;
  std::uint32_t width{};
};

enum class BinaryOperator : std::uint8_t {
  bit_and,
  bit_or,
  bit_xor,
  add_unsigned,
  subtract_unsigned,
  multiply_unsigned,
  power_unsigned,
  divide_unsigned,
  modulo_unsigned,
  add_signed,
  subtract_signed,
  multiply_signed,
  power_signed,
  divide_signed,
  remainder_signed,
  modulo_signed,
  equal,
  case_equal,
  // Statement-level wildcard matching. Both operands may supply wildcards;
  // the result is always a known scalar.
  casez_equal,
  casex_equal,
  // SystemVerilog ==? masks X/Z only in the right operand and may return X
  // for an unmasked X/Z in the left operand.
  wildcard_equal,
  not_equal,
  less_unsigned,
  less_equal_unsigned,
  greater_unsigned,
  greater_equal_unsigned,
  less_signed,
  less_equal_signed,
  greater_signed,
  greater_equal_signed,
  // VHDL-2008 matching equality. '-' in either operand is a wildcard,
  // 0/L and 1/H form equivalence classes, and the result is always known.
  vhdl_match_equal,
};

struct Binary {
  BinaryOperator operation = BinaryOperator::bit_and;
  RegisterId destination{};
  RegisterId lhs{};
  RegisterId rhs{};
};

enum class IntegerUnaryOperator : std::uint8_t {
  negate,
  absolute,
};

/// A checked operation on the portable signed 32-bit VHDL integer
/// representation. Unknown operands and overflow are language errors.
struct IntegerUnary {
  IntegerUnaryOperator operation{IntegerUnaryOperator::negate};
  RegisterId destination{};
  RegisterId source{};
};

enum class IntegerBinaryOperator : std::uint8_t {
  add,
  subtract,
  multiply,
  power,
  divide,
  remainder,
  modulo,
};

struct IntegerBinary {
  IntegerBinaryOperator operation{IntegerBinaryOperator::add};
  RegisterId destination{};
  RegisterId lhs{};
  RegisterId rhs{};
};

/// Require a known signed 32-bit value to belong to an elaborated VHDL
/// scalar subtype before it is stored.
struct IntegerCheck {
  RegisterId source{};
  std::int32_t lower{};
  std::int32_t upper{};
};

/// Select between equal-width values using SystemVerilog conditional
/// semantics. An X/Z condition merges matching bits and produces X for
/// differing bits.
struct ConditionalSelect {
  RegisterId destination{};
  RegisterId condition{};
  RegisterId when_true{};
  RegisterId when_false{};
};

/// Commit a new value immediately in the active phase.
struct WriteBlocking {
  SignalId signal{};
  RegisterId source{};
};

/// Queue a new value for the current timestamp's update phase.
struct WriteUpdate {
  SignalId signal{};
  RegisterId source{};
};

/// Queue a new value for a future timestamp's update phase.
struct WriteAfter {
  SignalId signal{};
  RegisterId source{};
  SimulationTick delay{};
};

struct TransitionDelays {
  SimulationTick rise{};
  SimulationTick fall{};
  SimulationTick turnoff{};

  friend bool operator==(
      const TransitionDelays&,
      const TransitionDelays&) = default;
};

/// Queue a continuous-assignment value with transition-specific inertial
/// delay. A later evaluation of the same driver supersedes its pending value.
struct WriteInertial {
  SignalId signal{};
  RegisterId source{};
  TransitionDelays delays;
};

enum class ProjectedDelayMode : std::uint8_t {
  transport,
  inertial,
};

/// Edit a VHDL driver's projected output waveform and queue the resulting
/// scalar transactions. For inertial mode, rejection is the explicit or
/// default rejection limit already normalized to simulation ticks.
struct WriteProjected {
  SignalId signal{};
  RegisterId source{};
  SimulationTick delay{};
  SimulationTick rejection{};
  ProjectedDelayMode mode{ProjectedDelayMode::inertial};
};

struct ProjectedWaveformElement {
  RegisterId source{};
  SimulationTick delay{};

  friend bool operator==(
      const ProjectedWaveformElement&,
      const ProjectedWaveformElement&) = default;
};

/// Atomically replace a VHDL driver's projected output waveform with an
/// ordered group of new transactions.
struct WriteProjectedWaveform {
  SignalId signal{};
  std::vector<ProjectedWaveformElement> elements;
  SimulationTick rejection{};
  ProjectedDelayMode mode{ProjectedDelayMode::inertial};
};

/// Replace a contiguous packed range immediately in the active phase.
struct WriteBlockingSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
};

/// Stage a contiguous packed range for the common update phase.
struct WriteUpdateSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
};

/// Stage a contiguous packed range after a simulation-time delay.
struct WriteAfterSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
  SimulationTick delay{};
};

struct WriteInertialSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
  TransitionDelays delays;
};

struct WriteProjectedSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
  SimulationTick delay{};
  SimulationTick rejection{};
  ProjectedDelayMode mode{ProjectedDelayMode::inertial};
};

struct WriteProjectedWaveformSlice {
  SignalId signal{};
  std::vector<ProjectedWaveformElement> elements;
  std::uint32_t offset{};
  SimulationTick rejection{};
  ProjectedDelayMode mode{ProjectedDelayMode::inertial};
};

struct WriteBlockingDynamicSlice {
  SignalId signal{};
  RegisterId source{};
  DynamicIndex selection;
};

struct WriteUpdateDynamicSlice {
  SignalId signal{};
  RegisterId source{};
  DynamicIndex selection;
};

struct WriteAfterDynamicSlice {
  SignalId signal{};
  RegisterId source{};
  DynamicIndex selection;
  SimulationTick delay{};
};

struct WriteBlockingDynamicPartSlice {
  SignalId signal{};
  RegisterId source{};
  DynamicPartIndex selection;
};

struct WriteUpdateDynamicPartSlice {
  SignalId signal{};
  RegisterId source{};
  DynamicPartIndex selection;
};

struct WriteAfterDynamicPartSlice {
  SignalId signal{};
  RegisterId source{};
  DynamicPartIndex selection;
  SimulationTick delay{};
};

/// Force a static packed region while drivers continue beneath its mask.
struct ForceSignalSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
};

/// Release a static packed force region and reveal current driven bits.
struct ReleaseSignalSlice {
  SignalId signal{};
  std::uint32_t offset{};
  std::uint32_t width{};
};
struct WriteInertialDynamicSlice {
  SignalId signal{};
  RegisterId source{};
  DynamicIndex selection;
  TransitionDelays delays;
};

struct WriteProjectedDynamicSlice {
  SignalId signal{};
  RegisterId source{};
  DynamicIndex selection;
  SimulationTick delay{};
  SimulationTick rejection{};
  ProjectedDelayMode mode{ProjectedDelayMode::inertial};
};

struct WriteProjectedWaveformDynamicSlice {
  SignalId signal{};
  std::vector<ProjectedWaveformElement> elements;
  DynamicIndex selection;
  SimulationTick rejection{};
  ProjectedDelayMode mode{ProjectedDelayMode::inertial};
};

struct ProjectedWaveformValue {
  PackedLogic4 value;
  SimulationTick delay{};
};

/// Return the shortest delay required by the bits that actually change.
/// A transition to X uses the shortest rise/fall/turnoff delay. No value is
/// returned when the packed values are equal.
[[nodiscard]] std::optional<SimulationTick> transition_delay(
    const PackedLogic4& current,
    const PackedLogic4& next,
    const TransitionDelays& delays);

struct WaitFor {
  SimulationTick delay{};
};

enum class EdgeKind : std::uint8_t {
  any,
  posedge,
  negedge,
};

/// Suspend until a listed signal has its corresponding edge. An empty edge
/// list means any change for every signal.
struct WaitOn {
  WaitOn() = default;
  explicit WaitOn(std::vector<SignalId> waited_signals)
      : signals(std::move(waited_signals)) {}
  WaitOn(
      std::vector<SignalId> waited_signals,
      std::vector<EdgeKind> waited_edges)
      : signals(std::move(waited_signals)),
        edges(std::move(waited_edges)) {}

  std::vector<SignalId> signals;
  std::vector<EdgeKind> edges;
  // An optional timeout races the listed signal events. A condition-wait
  // lowering uses timeout_result to distinguish timeout resumption from an
  // event resumption. A rearmed wait preserves the absolute deadline
  // established by the operation at timeout_origin instead of restarting the
  // timeout after a false condition.
  std::optional<SimulationTick> timeout;
  std::optional<RegisterId> timeout_result;
  std::optional<InstructionIndex> timeout_origin;
};

/// Suspend until this process's static sensitivity condition is met.
struct WaitSensitivity {};

/// Suspend permanently without completing the process. This models bare
/// waits and dependency-free condition waits while preserving
/// debugger-visible suspended state.
struct WaitForever {};

/// Suspend and resume in the active phase of the next delta cycle.
struct Yield {};
enum class ForkJoinKind : std::uint8_t { all, any, none };
/// Spawn child PCs sharing the lexical frame; each ends at ForkEnd.
struct Fork {
  std::vector<InstructionIndex> branches;
  ForkJoinKind join{ForkJoinKind::all};
};
struct ForkEnd {};
struct WaitFork {};
struct DisableFork {};
struct Jump {
  InstructionIndex target{};
};

/// Persistent hidden-register storage used by resumable SimIR subroutines.
///
/// The stack pointer and each entry are 32-bit, two-state registers. The
/// lowering which owns the process must initialize all of them before the
/// first call. Keeping this state in ordinary process registers gives the
/// interpreter and generated code the same suspension-safe representation.
struct CallStack {
  RegisterId pointer{};
  RegisterId entries{};
  std::uint32_t capacity{};
};

/// Enter a non-suspending or resumable SimIR subroutine.
///
/// return_target is pushed before control transfers to target. A frontend is
/// responsible for rejecting recursive source-language call graphs when its
/// language does not support recursion.
struct Call {
  InstructionIndex target{};
  InstructionIndex return_target{};
  CallStack stack;
};

/// Return to the most recently pushed Call return_target.
struct Return {
  CallStack stack;
};

enum class UnknownBranchPolicy : std::uint8_t {
  error,
  when_false,
};

/// Branch on a scalar one; zero selects when_false. X/Z handling follows the
/// operation's explicit language policy.
struct Branch {
  RegisterId condition{};
  InstructionIndex when_true{};
  InstructionIndex when_false{};
  UnknownBranchPolicy unknown_policy{UnknownBranchPolicy::error};
};

enum class AssertionSeverity : std::uint8_t {
  note,
  warning,
  error,
  failure,
};

struct SourceLocation {
  std::string path;
  std::uint32_t line{1};
  std::uint32_t column{1};

  friend bool operator==(const SourceLocation&,
                         const SourceLocation&) = default;
};

enum class ExpressionSizingKind : std::uint8_t {
  self_determined,
  context_determined,
};

enum class ExpressionValueDomain : std::uint8_t {
  two_state,
  four_state,
  nine_state,
  integer,
  boolean,
};

/// Resolved source-expression profile retained independently of transient
/// host-C++ inference. Repeated source spans are intentional when distinct
/// control-flow paths lower the same lexical expression.
struct ExpressionProfile {
  SourceLocation source;
  std::uint32_t width{};
  bool is_signed{};
  ExpressionSizingKind sizing{ExpressionSizingKind::self_determined};
  ExpressionValueDomain domain{ExpressionValueDomain::four_state};
};

enum class DebugPointKind : std::uint8_t {
  statement,
  call,
  wait,
  assertion,
  process_entry,
};

struct DebugPoint {
  DebugPointKind kind{DebugPointKind::statement};
  SourceLocation source;
  // Canonical hierarchy-qualified lexical execution scope. This is metadata
  // rather than a scheduler operand, but remains part of native provenance.
  std::string scope;

  DebugPoint() = default;
  DebugPoint(
      const DebugPointKind point_kind,
      SourceLocation point_source,
      std::string point_scope = {})
      : kind(point_kind), source(std::move(point_source)),
        scope(std::move(point_scope)) {}
};

struct Assert {
  RegisterId condition{};
  std::string message;
  AssertionSeverity severity{AssertionSeverity::error};
  SourceLocation source;
};

/// Emit already-formatted language output synchronously on the simulation
/// thread. The embedding layer owns the destination stream.
struct Display {
  std::string text;
  bool newline{true};
  bool postponed{};
};

enum class OutputFormat : std::uint8_t {
  binary,
  hexadecimal,
  octal,
  decimal,
  character,
  string,
};

/// Format one runtime value between literal prefix/suffix text.
struct FormatDisplay {
  RegisterId source{};
  OutputFormat format{OutputFormat::binary};
  std::string prefix;
  std::string suffix;
  bool newline{true};
  bool postponed{};
  bool signed_decimal{};
  bool suppress_leading_zero{};
  std::uint32_t minimum_width{};
  bool left_justify{};
  bool zero_pad{};
};

struct StringDisplay {
  StringRegisterId source{};
  std::string prefix;
  std::string suffix;
  bool newline{true};
  bool postponed{};
};

/// Emit the current global simulation tick between literal prefix/suffix
/// text. The tick is captured when this operation executes.
struct TimeDisplay {
  std::string prefix;
  std::string suffix;
  bool newline{true};
  bool postponed{};
  std::uint32_t minimum_width{};
  bool left_justify{};
  bool zero_pad{};
};

enum class MonitorValueKind : std::uint8_t {
  signal,
  time,
};

struct MonitorValue {
  MonitorValueKind kind{MonitorValueKind::signal};
  SignalId signal{};
  OutputFormat format{OutputFormat::decimal};
  std::string prefix;
  bool signed_decimal{};
  bool suppress_leading_zero{};
  std::uint32_t minimum_width{};
  bool left_justify{};
  bool zero_pad{};
};

/// Replace the global Verilog/SystemVerilog monitor registration and publish
/// its current value once in the postponed phase.
struct MonitorInstall {
  std::vector<MonitorValue> values;
  std::string trailing_text;
  bool newline{true};
  bool one_shot{};
};

/// Enable or disable the current monitor without discarding its registration.
struct MonitorControl {
  bool enabled{};
};

enum class RandomKind : std::uint8_t {
  urandom,
  random,
  urandom_range,
};

/// Produce one deterministic 32-bit random value from the current process's
/// project-seeded stream.
struct RandomValue {
  RegisterId destination{};
  RandomKind kind{RandomKind::urandom};
  std::optional<RegisterId> maximum;
  std::optional<RegisterId> minimum;
};

/// Emit a nonfatal VHDL report with retained severity and source metadata.
struct Report {
  std::string message;
  AssertionSeverity severity{AssertionSeverity::note};
  SourceLocation source;
};

/// Stop the complete simulation, as requested by `$finish` or an equivalent
/// language construct.
struct Stop {};

/// Pause simulation at a resumable boundary, as requested by `$stop`.
struct Pause {};

struct Halt {};

#include "fsim/runtime/simir_operation_storage.hpp"

enum class ResolutionKind : std::uint8_t {
  none,
  sv_wire,
  std_logic,
  vhdl_user_or,
  vhdl_user_and,
};

enum class ValueKind : std::uint8_t {
  logic4,
  logic9,
};

struct Signal {
  std::string name;
  PackedLogic4 initial_value;
  ResolutionKind resolution{ResolutionKind::none};
  ValueKind value_kind{ValueKind::logic4};
};

struct StringObject {
  std::string name;
  std::string initial_value;
};

/// One fixed-array object view backed by a selected range of an earlier
/// object. The view's own ContainerValue type supplies the child/formal
/// declared range; selected_left/right name the parent range. Reads and
/// writes map equal-count elements ordinally.
struct ContainerSliceAlias {
  ContainerObjectId object{};
  std::int32_t selected_left{};
  std::int32_t selected_right{};

  friend bool operator==(
      const ContainerSliceAlias&,
      const ContainerSliceAlias&) = default;
};

struct ContainerObject {
  std::string name;
  ContainerValue initial_value;
  std::optional<ContainerSliceAlias> slice_alias;
};

struct Sensitivity {
  SignalId signal{};
  EdgeKind edge = EdgeKind::any;

  bool operator==(const Sensitivity&) const = default;
};

struct DebugLocal {
  std::string name;
  std::string type_name;
  RegisterId register_id{};
  std::size_t width{};
  SourceLocation source;
  std::optional<std::int32_t> integer_lower;
  std::optional<std::int32_t> integer_upper;
  ValueKind value_kind{ValueKind::logic4};
  std::vector<std::string> enumeration_literals;
};

struct DebugStringLocal {
  std::string name;
  StringRegisterId register_id{};
  SourceLocation source;
};

struct DebugContainerLocal {
  std::string name;
  ContainerRegisterId register_id{};
  ContainerType type;
  SourceLocation source;
};

struct Process {
  ProcessId id{};
  std::string name;
  std::size_t register_count{};
  std::size_t string_register_count{};
  std::size_t container_register_count{};
  std::vector<DebugLocal> debug_locals;
  std::vector<DebugStringLocal> debug_string_locals;
  std::vector<DebugContainerLocal> debug_container_locals;
  std::vector<ContainerType> container_register_types;
  std::vector<Sensitivity> static_sensitivity;
  std::vector<Operation> operations;
  std::vector<ValueKind> register_value_kinds;
  bool initialize{true};
  // A SystemVerilog final process is excluded from ordinary initialization
  // and queued exactly once when ordinary simulation terminates.
  bool final{};
  std::vector<ExpressionProfile> expression_profiles;
};

/// Narrow signal/update surface available to an alternate process executor.
///
/// The simulation kernel retains all scheduler, wait, fanout, force, and
/// lifecycle ownership. An executor may evaluate ordinary operations through
/// this interface, then must return at a validated SimIR boundary operation.
/// Executors must not retain a ProcessExecutionContext beyond the resume()
/// call that supplies it.
enum class EventNotificationKind : std::uint8_t {
  immediate,
  delta,
  timed,
  delayed,
};

class ProcessExecutionContext {
public:
  virtual ~ProcessExecutionContext() = default;

  [[nodiscard]] virtual PackedLogic4 read_signal(SignalId signal) const = 0;
  virtual void write_blocking(SignalId signal, PackedLogic4 value) = 0;

  [[nodiscard]] virtual std::string
  read_string_object(StringObjectId) const {
    throw std::logic_error{
        "alternate process executor cannot read string objects"};
  }
  virtual void write_string_object(StringObjectId, std::string_view) {
    throw std::logic_error{
        "alternate process executor cannot write string objects"};
  }
  [[nodiscard]] virtual ContainerValue
  read_container_object(ContainerObjectId) const {
    throw std::logic_error{
        "alternate process executor does not support container objects"};
  }
  virtual void write_container_object(
      ContainerObjectId, const ContainerValue&) {
    throw std::logic_error{
        "alternate process executor does not support container objects"};
  }

  [[nodiscard]] virtual FileHandle open_file(
      std::string_view, std::string_view) {
    throw std::logic_error{
        "alternate process executor does not support file open"};
  }
  virtual void close_file(FileHandle) {
    throw std::logic_error{
        "alternate process executor does not support file close"};
  }
  virtual void write_file(
      FileHandle, std::string_view, bool) {
    throw std::logic_error{
        "alternate process executor does not support file writes"};
  }
  virtual void write_file_formatted(
      FileHandle,
      std::string_view,
      std::string_view,
      OutputFormat,
      const PackedLogic4&,
      bool,
      bool,
      std::uint32_t,
      bool,
      bool) {
    throw std::logic_error{
        "alternate process executor does not support formatted file writes"};
  }
  [[nodiscard]] virtual std::string read_file_line(
      FileHandle, std::uint32_t&) {
    throw std::logic_error{"alternate process executor does not support file reads"};
  }
  [[nodiscard]] virtual std::int32_t read_file_character(FileHandle) {
    throw std::logic_error{"alternate process executor does not support character reads"};
  }
  [[nodiscard]] virtual std::int32_t unread_file_character(
      FileHandle, std::int32_t) {
    throw std::logic_error{"alternate process executor does not support character pushback"};
  }
  [[nodiscard]] virtual bool file_end_of_file(FileHandle) {
    throw std::logic_error{"alternate process executor does not support file status"};
  }
  [[nodiscard]] virtual std::string file_error(
      FileHandle, bool&) {
    throw std::logic_error{
        "alternate process executor does not support file errors"};
  }
  [[nodiscard]] virtual std::int32_t position_file(
      FileHandle, FilePositionKind, std::int32_t, std::int32_t) {
    throw std::logic_error{
        "alternate process executor does not support file positioning"};
  }
  virtual void flush_file(std::optional<FileHandle>) {
    throw std::logic_error{
        "alternate process executor does not support file flushing"};
  }

  /// Allocation-free single-word access used by generated scalar/vector code.
  ///
  /// The default implementations preserve compatibility for alternate
  /// executors that only implement the object interface. The kernel overrides
  /// these methods to access its signal storage directly.
  [[nodiscard]] virtual Logic4Word
  read_signal_word(SignalId signal) const {
    return read_signal(signal).low_word();
  }
  [[nodiscard]] virtual Logic9Word
  read_signal_logic9_word(SignalId signal) const {
    return read_signal(signal).logic9_low_word();
  }
  virtual void write_blocking_word(
      SignalId signal, const Logic4Word value) {
    write_blocking(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }
  virtual void write_blocking_slice(
      SignalId signal,
      PackedLogic4 value,
      std::size_t offset) = 0;
  virtual void write_blocking_slice_word(
      SignalId signal,
      const Logic4Word value,
      std::uint32_t offset) {
    write_blocking_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
  }

  virtual void force_signal_slice(
      SignalId,
      PackedLogic4,
      std::size_t) {
    throw std::logic_error{
        "alternate process executor does not support procedural force"};
  }
  virtual void release_signal_slice(
      SignalId,
      std::size_t,
      std::size_t) {
    throw std::logic_error{
        "alternate process executor does not support procedural release"};
  }

  virtual void write_update(SignalId signal, PackedLogic4 value) = 0;
  virtual void write_update_word(
      SignalId signal, const Logic4Word value) {
    write_update(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }
  virtual void write_update_slice(
      SignalId signal,
      PackedLogic4 value,
      std::size_t offset) = 0;
  virtual void write_update_slice_word(
      SignalId signal,
      const Logic4Word value,
      std::uint32_t offset) {
    write_update_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
  }

  virtual void write_after(SignalId signal, PackedLogic4 value,
                           SimulationTick delay) = 0;
  virtual void write_after_word(
      SignalId signal, const Logic4Word value,
      SimulationTick delay) {
    write_after(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        delay);
  }
  virtual void write_after_slice(
      SignalId signal,
      PackedLogic4 value,
      std::size_t offset,
      SimulationTick delay) = 0;
  virtual void write_after_slice_word(
      SignalId signal,
      const Logic4Word value,
      std::uint32_t offset,
      SimulationTick delay) {
    write_after_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delay);
  }
  virtual void write_inertial(
      SignalId signal,
      PackedLogic4 value,
      const TransitionDelays& delays) = 0;
  virtual void write_inertial_word(
      SignalId signal,
      const Logic4Word value,
      const TransitionDelays& delays) {
    write_inertial(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        delays);
  }
  virtual void write_inertial_slice(
      SignalId signal,
      PackedLogic4 value,
      std::size_t offset,
      const TransitionDelays& delays) = 0;
  virtual void write_inertial_slice_word(
      SignalId signal,
      const Logic4Word value,
      std::uint32_t offset,
      const TransitionDelays& delays) {
    write_inertial_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delays);
  }
  virtual void write_projected(
      SignalId signal,
      PackedLogic4 value,
      SimulationTick delay,
      SimulationTick rejection,
      ProjectedDelayMode mode) {
    (void)signal;
    (void)value;
    (void)delay;
    (void)rejection;
    (void)mode;
    throw std::logic_error{
        "alternate process executor does not support projected writes"};
  }
  virtual void write_projected_word(
      SignalId signal,
      const Logic4Word value,
      SimulationTick delay,
      SimulationTick rejection,
      ProjectedDelayMode mode) {
    write_projected(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        delay,
        rejection,
        mode);
  }
  virtual void write_projected_slice(
      SignalId signal,
      PackedLogic4 value,
      std::size_t offset,
      SimulationTick delay,
      SimulationTick rejection,
      ProjectedDelayMode mode) {
    (void)signal;
    (void)value;
    (void)offset;
    (void)delay;
    (void)rejection;
    (void)mode;
    throw std::logic_error{
        "alternate process executor does not support projected slice writes"};
  }
  virtual void write_projected_slice_word(
      SignalId signal,
      const Logic4Word value,
      std::uint32_t offset,
      SimulationTick delay,
      SimulationTick rejection,
      ProjectedDelayMode mode) {
    write_projected_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delay,
        rejection,
        mode);
  }
  virtual void write_projected_waveform(
      SignalId signal,
      std::vector<ProjectedWaveformValue> elements,
      SimulationTick rejection,
      ProjectedDelayMode mode) {
    (void)signal;
    (void)elements;
    (void)rejection;
    (void)mode;
    throw std::logic_error{
        "alternate process executor does not support projected waveforms"};
  }
  virtual void write_projected_waveform_slice(
      SignalId signal,
      std::vector<ProjectedWaveformValue> elements,
      std::size_t offset,
      SimulationTick rejection,
      ProjectedDelayMode mode) {
    (void)signal;
    (void)elements;
    (void)offset;
    (void)rejection;
    (void)mode;
    throw std::logic_error{
        "alternate process executor does not support projected slice "
        "waveforms"};
  }

  /// Notify a kernel-owned event identity from an alternate language
  /// executor. Immediate notifications re-enter the active worklist at the
  /// current timestamp. Delta notifications enter the next delta; non-zero
  /// delays enter the active worklist at the requested future timestamp.
  virtual void notify_event(
      SignalId,
      SimulationTick,
      EventNotificationKind) {
    throw std::logic_error{
        "alternate process executor does not support event notification"};
  }
  virtual void cancel_event(SignalId) {
    throw std::logic_error{
        "alternate process executor does not support event cancellation"};
  }

  /// True only in the evaluation delta caused by the signal's most recent
  /// committed value change.
  [[nodiscard]] virtual bool signal_event(SignalId) const {
    return false;
  }

  /// Return the effective value immediately before the signal's latest
  /// committed value change.
  [[nodiscard]] virtual Logic4Word signal_last_value_word(SignalId) const {
    throw std::logic_error{
        "alternate process executor does not support signal last-value reads"};
  }
  [[nodiscard]] virtual Logic9Word
  signal_last_value_logic9_word(SignalId) const {
    throw std::logic_error{
        "alternate process executor does not support exact signal "
        "last-value reads"};
  }

  /// Elapsed global-resolution ticks since the latest effective-value event,
  /// or the maximum tick value if the signal has never changed.
  [[nodiscard]] virtual SimulationTick signal_last_event(SignalId) const {
    return std::numeric_limits<SimulationTick>::max();
  }

  /// True in the evaluation delta caused by the signal's most recent
  /// committed transaction, including a transaction that did not change its
  /// effective value.
  [[nodiscard]] virtual bool signal_active(SignalId) const {
    return false;
  }

  /// Request one alternate-language primitive-channel update. `channel` is a
  /// stable executor-owned identity. The kernel deduplicates it until the
  /// corresponding update callback finishes and invokes that callback in the
  /// common update phase.
  virtual void request_channel_update(std::uint64_t) {
    throw std::logic_error{
        "alternate process executor does not support channel updates"};
  }

  virtual void display(std::string_view, bool) {}
  virtual void postpone_display(std::string_view, bool) {}
  [[nodiscard]] virtual SimulationTick current_time() const noexcept { return 0; }
  virtual void display_formatted(
      std::string_view,
      std::string_view,
      OutputFormat,
      const PackedLogic4&,
      bool,
      bool,
      bool,
      bool,
      std::uint32_t,
      bool,
      bool) {}
  virtual void display_time(
      std::string_view,
      std::string_view,
      bool,
      bool,
      std::uint32_t,
      bool,
      bool) {}
  virtual void install_monitor(const MonitorInstall&) {}
  virtual void set_monitor_enabled(bool) {}
  [[nodiscard]] virtual PackedLogic4 random_value(
      RandomKind,
      const std::optional<PackedLogic4>&,
      const std::optional<PackedLogic4>&) {
    throw std::logic_error{
        "alternate process executor does not support random values"};
  }
  virtual void report(
      std::string_view,
      AssertionSeverity,
      const SourceLocation&) {}

  /// True when an embedding debugger currently requests source boundaries.
  [[nodiscard]] virtual bool execution_points_enabled() const noexcept {
    return false;
  }
};

enum class ExternalSuspendKind : std::uint8_t {
  simir_boundary,
  wait_for,
  wait_on,
  wait_sensitivity,
  yield,
  halt,
};

/// Common-kernel suspension selected by an alternate language executor.
struct ExternalSuspension {
  ExternalSuspendKind kind{ExternalSuspendKind::simir_boundary};
  SimulationTick delay{};
  std::vector<Sensitivity> sensitivity;
  bool wait_all{};
};
/// Alternate-executor boundary and its exact sequential resume PC.
struct ProcessResumeResult {
  ProcessResumeResult() = default;
  constexpr ProcessResumeResult(
      const InstructionIndex boundary_instruction,
      const InstructionIndex resume_instruction) noexcept
      : instruction(boundary_instruction),
        next_instruction(resume_instruction) {}
  InstructionIndex instruction{};
  InstructionIndex next_instruction{};
  ExternalSuspension external;
};

enum class ExecutionPointKind : std::uint8_t {
  statement,
  call,
  wait,
  assertion,
  process_entry,
  process_suspend,
};

struct ExecutionPoint {
  ProcessId process{}, design_process{};
  InstructionIndex instruction{};
  ExecutionPointKind kind{ExecutionPointKind::statement};
  SourceLocation source;
  std::string scope;

  ExecutionPoint() = default;
  ExecutionPoint(
      const ProcessId execution_process,
      const ProcessId source_process,
      const InstructionIndex execution_instruction,
      const ExecutionPointKind execution_kind,
      SourceLocation execution_source,
      std::string execution_scope = {})
      : process(execution_process), design_process(source_process),
        instruction(execution_instruction), kind(execution_kind),
        source(std::move(execution_source)),
        scope(std::move(execution_scope)) {}
};
class ProcessExecutor {
public:
  virtual ~ProcessExecutor() = default;
  /// Share the lexical frame with a child owning an independent PC.
  [[nodiscard]] virtual std::unique_ptr<ProcessExecutor> fork_clone(
      InstructionIndex) {
    throw std::logic_error{"executor does not support fork cloning"};
  }
  /// Execute from start_instruction until the next SimIR kernel boundary.
  ///
  /// Implementations own their register/frame storage. Exceptions must be
  /// raised only in C++ after any generated plain-C call has returned.
  [[nodiscard]] virtual ProcessResumeResult
  resume(ProcessExecutionContext& context,
         InstructionIndex start_instruction) = 0;

  /// Execute a previously requested primitive-channel update. Only
  /// alternate-language executors which expose such channels override this.
  virtual void update_channel(
      std::uint64_t,
      ProcessExecutionContext&) {
    throw std::logic_error{
        "alternate process executor has no primitive-channel callback"};
  }

  [[nodiscard]] virtual PackedLogic4
  read_register(RegisterId, std::size_t) const {
    throw std::logic_error{
        "alternate process executor does not expose register values"};
  }

  virtual void write_register(
      RegisterId, const PackedLogic4&) {
    throw std::logic_error{
        "alternate process executor does not expose writable registers"};
  }

  [[nodiscard]] virtual std::string
  read_string_register(StringRegisterId) const {
    throw std::logic_error{
        "alternate process executor does not expose string registers"};
  }

  virtual void write_string_register(
      StringRegisterId, std::string_view) {
    throw std::logic_error{
        "alternate process executor does not expose writable string "
        "registers"};
  }

  [[nodiscard]] virtual ContainerValue
  read_container_register(ContainerRegisterId) const {
    throw std::logic_error{
        "alternate process executor does not expose container registers"};
  }

  virtual void write_container_register(
      ContainerRegisterId, const ContainerValue&) {
    throw std::logic_error{
        "alternate process executor does not expose writable container "
        "registers"};
  }
};

class InterpreterError : public std::runtime_error {
public:
  InterpreterError(ProcessId process, InstructionIndex instruction,
                   std::string message);

  [[nodiscard]] ProcessId process() const noexcept { return process_; }
  [[nodiscard]] InstructionIndex instruction() const noexcept {
    return instruction_;
  }

private:
  ProcessId process_{};
  InstructionIndex instruction_{};
};

class AssertionError final : public InterpreterError {
public:
  AssertionError(ProcessId process, InstructionIndex instruction,
                 std::string message, AssertionSeverity severity,
                 SourceLocation source, bool reported = false);

  [[nodiscard]] AssertionSeverity severity() const noexcept {
    return severity_;
  }
  [[nodiscard]] const SourceLocation& source() const noexcept {
    return source_;
  }
  [[nodiscard]] bool reported() const noexcept {
    return reported_;
  }

private:
  AssertionSeverity severity_{AssertionSeverity::error};
  SourceLocation source_;
  bool reported_{};
};

/// Small reference interpreter for differential testing of generated code.
class Interpreter {
public:
  using SignalChangeHook =
      std::function<void(SignalId, const PackedLogic4 &, SimulationTick)>;
  using ExecutionPointHook =
      std::function<void(Scheduler&, const ExecutionPoint&)>;
  using OutputHook = std::function<void(
      ProcessId,
      std::string_view,
      bool,
      SimulationTick,
      std::uint64_t)>;
  using ReportHook = std::function<void(
      ProcessId,
      std::string_view,
      AssertionSeverity,
      const SourceLocation&,
      SimulationTick,
      std::uint64_t)>;

  explicit Interpreter(
      SchedulerOptions options = {},
      std::uint64_t seed = 1);
  ~Interpreter();
  Interpreter(Interpreter &&) noexcept;
  Interpreter &operator=(Interpreter &&) noexcept;
  Interpreter(const Interpreter &) = delete;
  Interpreter &operator=(const Interpreter &) = delete;

  [[nodiscard]] SignalId add_signal(Signal signal);
  [[nodiscard]] StringObjectId add_string_object(StringObject object);
  [[nodiscard]] ContainerObjectId add_container_object(
      ContainerObject object);
  [[nodiscard]] ProcessId add_process(Process process);

  /// Restrict all HDL file operations to paths below this root. Must be set
  /// before start; an empty root leaves file operations disabled.
  void set_file_root(std::filesystem::path root);

  /// Replace one process's reference evaluator with an alternate executor.
  ///
  /// The interpreter remains the sole scheduler and signal store. Installation
  /// is allowed only before start and at most once per process.
  void set_process_executor(
      ProcessId process, std::unique_ptr<ProcessExecutor> executor);

  void start();
  [[nodiscard]] RunResult
  run(std::optional<SimulationTick> until = std::nullopt);

  /// Deposit immediately and activate sensitive processes in the next delta.
  void deposit_signal(SignalId signal, PackedLogic4 value);

  /// Override the visible value while preserving subsequently driven values.
  /// Releasing the force publishes the most recent underlying driven value.
  void force_signal(SignalId signal, PackedLogic4 value);
  void release_signal(SignalId signal);
  void force_signal_slice(
      SignalId signal, PackedLogic4 value, std::size_t offset);
  void release_signal_slice(
      SignalId signal, std::size_t offset, std::size_t width);
  [[nodiscard]] bool signal_is_forced(SignalId signal) const;

  /// Schedule an external drive in the update phase of an absolute timestamp.
  void schedule_signal_at(SignalId signal, PackedLogic4 value,
                          SimulationTick time, StableOrder order = 0);

  /// Schedule an external drive relative to the scheduler's current time.
  void schedule_signal_after(SignalId signal, PackedLogic4 value,
                             SimulationTick delay, StableOrder order = 0);

  [[nodiscard]] const PackedLogic4 &signal_value(SignalId signal) const;
  [[nodiscard]] const std::string&
  string_object_value(StringObjectId object) const;
  void deposit_string_object(
      StringObjectId object, std::string_view value);
  [[nodiscard]] const ContainerValue&
  container_object_value(ContainerObjectId object) const;
  void deposit_container_object(
      ContainerObjectId object, ContainerValue value);
  /// Return one process-owned driver slot. For an unresolved signal this is
  /// the single underlying driven value.
  [[nodiscard]] const PackedLogic4& driver_value(
      ProcessId process, SignalId signal) const;
  [[nodiscard]] PackedLogic4 read_debug_local(
      ProcessId process, std::size_t local_index) const;
  [[nodiscard]] std::string read_debug_string_local(
      ProcessId process, std::size_t local_index) const;
  [[nodiscard]] ContainerValue read_debug_container_local(
      ProcessId process, std::size_t local_index) const;
  /// True once a language-level Stop operation (`$finish` or equivalent) has
  /// executed. External scheduler stop requests do not set this flag.
  [[nodiscard]] bool stopped_by_design() const noexcept;
  [[nodiscard]] Scheduler &scheduler() noexcept;
  [[nodiscard]] const Scheduler &scheduler() const noexcept;
  void set_signal_change_hook(SignalChangeHook hook);
  void set_execution_point_hook(ExecutionPointHook hook);
  void set_output_hook(OutputHook hook);
  void set_report_hook(ReportHook hook);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime::simir
