// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/runtime/constraint_solver.hpp"
#include "fsim/runtime/file_operations.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
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
#include <unordered_map>
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
    RegisterId destination { };
    PackedLogic4 value;
};
#include "fsim/runtime/simir_signal_queries.hpp"

struct CopyRegister {
    RegisterId destination { };
    RegisterId source { };
};

struct ConvertToTwoState {
    RegisterId destination { };
    RegisterId source { };
};

inline constexpr std::size_t maximum_string_bytes = 4096;
inline constexpr std::size_t maximum_container_predicate_nodes = 64;
inline constexpr std::size_t maximum_memory_file_bytes = 1024U * 1024U;
inline constexpr std::size_t maximum_open_file_handles = 4'096;

struct SystemVerilogTimeFormat {
    std::int32_t units { -15 };
    std::uint32_t precision { };
    std::string suffix;
    std::uint32_t minimum_width { 20 };
    std::uint64_t resolution_femtoseconds { 1 };
};

struct LoadStringConstant {
    StringRegisterId destination { };
    std::string value;
};
struct CopyStringRegister {
    StringRegisterId destination { };
    StringRegisterId source { };
};
struct ReadStringObject {
    StringRegisterId destination { };
    StringObjectId object { };
};
struct WriteStringObject {
    StringObjectId object { };
    StringRegisterId source { };
};
struct ConcatenateStrings {
    StringRegisterId destination { };
    std::vector<StringRegisterId> operands;
};

struct CompareStrings {
    RegisterId destination { };
    StringRegisterId lhs { };
    StringRegisterId rhs { };
    bool not_equal { };
};

struct SystemVerilogScalarBinary {
    SystemVerilogScalarBinaryOperator operation {
        SystemVerilogScalarBinaryOperator::Add
    };
    RegisterId destination { };
    RegisterId lhs { };
    RegisterId rhs { };
    SystemVerilogScalarKind lhs_kind { SystemVerilogScalarKind::None };
    SystemVerilogScalarKind rhs_kind { SystemVerilogScalarKind::None };
    SystemVerilogScalarKind result_kind { SystemVerilogScalarKind::None };
};

struct SystemVerilogMath {
    SystemVerilogMathFunction function { SystemVerilogMathFunction::Rtoi };
    RegisterId destination { };
    RegisterId first { };
    RegisterId second { };
    std::uint32_t first_width { };
    std::uint32_t second_width { };
    SystemVerilogScalarKind first_kind { SystemVerilogScalarKind::None };
    SystemVerilogScalarKind second_kind { SystemVerilogScalarKind::None };
    bool first_signed { };
    bool second_signed { };
    std::uint64_t time_unit_femtoseconds { 1 };
    std::uint64_t time_precision_femtoseconds { 1 };
};

struct StringLength {
    RegisterId destination { };
    StringRegisterId source { };
};

struct StringIndex {
    RegisterId destination { };
    StringRegisterId source { };
    RegisterId index { };
    bool signed_index { true };
};

struct StringReplaceCodePoint {
    StringRegisterId target { };
    RegisterId index { };
    RegisterId source { };
    bool signed_index { true };
};
enum class StringMethodOperator : std::uint8_t {
    getc,
    putc,
    toupper,
    tolower,
    compare,
    icompare,
    substr,
    atoi,
    atohex,
    atooct,
    atobin,
    atoreal,
    itoa,
    hextoa,
    octtoa,
    bintoa,
    realtoa,
    format_packed,
    format_string,
    format_time
};
struct StringMethod {
    StringMethodOperator operation { };
    RegisterId destination { }, first { }, second { };
    StringRegisterId string_destination { }, source { }, argument { };
    OutputFormat format { };
    std::uint32_t minimum_width { };
    bool signed_decimal { }, suppress_leading_zero { }, left_justify { }, zero_pad { };
    SystemVerilogScalarKind scalar_kind { SystemVerilogScalarKind::None };
    bool use_timeformat_width { };
};
struct ResizeContainer {
    ContainerRegisterId target { };
    RegisterId size { };
    std::optional<ContainerRegisterId> initializer { };
    bool allow_queue { };
};

struct CopyContainerRegister {
    ContainerRegisterId destination { };
    ContainerRegisterId source { };
};

/// Select one of two exactly compatible container snapshots. A known scalar
/// condition copies one alternative. X/Z merges equal-shape four-state
/// elements bitwise and produces the empty value for differing nonstatic
/// shapes; fixed arrays always have equal shape.
struct ConditionalContainerSelect {
    ContainerRegisterId destination { };
    RegisterId condition { };
    ContainerRegisterId when_true { };
    ContainerRegisterId when_false { };
};

struct CompareContainers {
    RegisterId destination { };
    ContainerRegisterId lhs { };
    ContainerRegisterId rhs { };
    bool case_equal { };
};

struct ReadContainerObject {
    ContainerRegisterId destination { };
    ContainerObjectId object { };
};

struct WriteContainerObject {
    ContainerObjectId object { };
    ContainerRegisterId source { };
    std::optional<SignalId> transaction_signal;
};

struct ContainerSize {
    RegisterId destination { };
    ContainerRegisterId source { };
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
    shuffle,
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
    ContainerPredicateOperator operation {
        ContainerPredicateOperator::item
    };
    std::uint32_t left { };
    std::uint32_t right { };
    PackedLogic4 constant;
    ContainerPredicateValueKind value_kind {
        ContainerPredicateValueKind::element
    };
    std::uint32_t third { };
};

struct OrderContainer {
    ContainerOrderingOperator operation {
        ContainerOrderingOperator::reverse
    };
    ContainerRegisterId target { };
    std::vector<ContainerPredicateNode> key;
};

/// Reorder container elements using the deterministic SystemVerilog subset
/// policy. A nonempty key graph is evaluated once per original element.
/// Container type, size, bounds, and associative keys are unchanged.
void order_container_value(
    ContainerValue& value,
    ContainerOrderingOperator operation,
    std::span<const ContainerPredicateNode> key = { },
    const std::function<std::uint32_t()>& random = { });

struct ContainerReduction {
    ContainerReductionOperator operation {
        ContainerReductionOperator::sum
    };
    RegisterId destination { };
    ContainerRegisterId source { };
    std::vector<ContainerPredicateNode> transformation;
};

/// Reduce container elements in their canonical storage order. Empty
/// containers use the SystemVerilog identity for the selected operation.
/// A nonempty transformation is evaluated once per source element before
/// applying the reduction.
[[nodiscard]] PackedLogic4 reduce_container_value(
    const ContainerValue& value,
    ContainerReductionOperator operation,
    std::span<const ContainerPredicateNode> transformation = { });

struct LocateContainer {
    ContainerLocatorOperator operation {
        ContainerLocatorOperator::minimum
    };
    ContainerRegisterId destination { };
    ContainerRegisterId source { };
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
    std::span<const ContainerPredicateNode> predicate = { },
    std::span<const ContainerPredicateNode> transformation = { });

struct ContainerRead {
    RegisterId destination { };
    ContainerRegisterId source { };
    RegisterId index { };
    bool signed_index { true };
    bool linear_index { };
    bool string_index { };
};
struct ContainerWrite {
    ContainerRegisterId target { };
    RegisterId index { };
    RegisterId source { };
    bool signed_index { true };
    bool linear_index { };
    bool string_index { };
};

/// Update one packed/scalar element of a container object without
/// materializing the complete object in a temporary container register.
struct WriteContainerObjectElement {
    ContainerObjectId object { };
    RegisterId index { };
    RegisterId source { };
    bool signed_index { true };
    bool linear_index { };
    std::optional<SignalId> transaction_signal;
};

struct ContainerStringRead {
    StringRegisterId destination { };
    ContainerRegisterId source { };
    RegisterId index { };
    bool signed_index { true };
    bool linear_index { };
    bool string_index { };
};

struct ContainerStringWrite {
    ContainerRegisterId target { };
    RegisterId index { };
    StringRegisterId source { };
    bool signed_index { true };
    bool linear_index { };
    bool string_index { };
};

struct ContainerElementRead {
    ContainerRegisterId destination { };
    ContainerRegisterId source { };
    RegisterId index { };
    bool signed_index { true };
};

struct ContainerElementWrite {
    ContainerRegisterId target { };
    RegisterId index { };
    ContainerRegisterId source { };
    bool signed_index { true };
};

struct ContainerAggregateRead {
    RegisterId destination { };
    ContainerRegisterId source { };
    RegisterId index { };
    std::vector<std::uint32_t> members;
    bool signed_index { true };
    bool linear_index { };
};

struct ContainerAggregateWrite {
    ContainerRegisterId target { };
    RegisterId index { };
    RegisterId source { };
    std::vector<std::uint32_t> members;
    bool signed_index { true };
    bool linear_index { };
};

struct CopyContainerAggregateElement {
    ContainerRegisterId target { };
    RegisterId target_index { };
    ContainerRegisterId source { };
    RegisterId source_index { };
    bool target_signed_index { true };
    bool source_signed_index { true };
};

struct DeleteContainer {
    ContainerRegisterId target { };
    std::optional<RegisterId> index { };
    bool string_index { };
};

struct ContainerExists {
    RegisterId destination { };
    ContainerRegisterId source { };
    RegisterId index { };
    bool string_index { };
};

enum class ContainerTraversal : std::uint8_t {
    first,
    last,
    next,
    previous,
};

struct TraverseContainer {
    RegisterId destination { };
    ContainerRegisterId source { };
    RegisterId index { };
    ContainerTraversal traversal { ContainerTraversal::first };
    bool string_index { };
};

struct LoadMemory {
    ContainerRegisterId target { };
    StringRegisterId path { };
    std::optional<RegisterId> start;
    std::optional<RegisterId> finish;
    bool hexadecimal { };
    bool write { };
};

/// Return the fixed packed width of one memory element when the element is
/// recursively representable as packed leaves.
[[nodiscard]] std::optional<std::size_t>
container_packed_element_width(const ContainerType& type);

/// Return the packed signal width required to represent a fixed container.
[[nodiscard]] std::optional<std::size_t>
container_signal_bridge_width(const ContainerType& type);

/// Pack and unpack fixed-container leaves in declaration order for a signal
/// alias used by elaborated static unpacked arrays.
[[nodiscard]] PackedLogic4 pack_container_signal_value(
    const ContainerValue& value, bool logic9);
void unpack_container_signal_value(
    ContainerValue& value, const PackedLogic4& packed);

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
    ContainerRegisterId target { };
    RegisterId source { };
    bool front { };
    std::optional<RegisterId> index { };
};

struct PopContainer {
    RegisterId destination { };
    ContainerRegisterId target { };
    bool front { };
};

struct UnaryNot {
    RegisterId destination { };
    RegisterId source { };
};

/// SystemVerilog logical negation. The source may be a packed vector; the
/// destination is a scalar four-state truth value.
struct LogicalNot {
    RegisterId destination { };
    RegisterId source { };
};

enum class LogicalBinaryOperator : std::uint8_t {
    logical_and,
    logical_or,
};

/// SystemVerilog logical conjunction/disjunction. Each operand is reduced to
/// a scalar truth value independently, so operand widths may differ.
struct LogicalBinary {
    LogicalBinaryOperator operation {
        LogicalBinaryOperator::logical_and
    };
    RegisterId destination { };
    RegisterId lhs { };
    RegisterId rhs { };
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
    ReductionOperator operation { ReductionOperator::bit_and };
    RegisterId destination { };
    RegisterId source { };
};

/// Count exact `1` elements of one packed operand. `X` and `Z` do not
/// contribute. The destination is a 32-bit two-state SystemVerilog `int`.
struct CountOnes {
    RegisterId destination { };
    RegisterId source { };
};

/// Count elements whose exact four-state value is selected by state_mask.
/// Bits 0 through 3 select `0`, `1`, `X`, and `Z`, respectively.
struct CountBits {
    RegisterId destination { };
    RegisterId source { };
    std::uint8_t state_mask { };
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
    ShiftOperator operation { ShiftOperator::logical_left };
    RegisterId destination { };
    RegisterId value { };
    RegisterId amount { };
    /// Interpret amount as two's-complement and reverse the operation for a
    /// negative value. This models VHDL's signed INTEGER shift counts without
    /// changing the packed value representation.
    bool signed_amount { };
};

/// Extract a contiguous normalized bit range from one packed value.
struct Extract {
    RegisterId destination { };
    RegisterId source { };
    std::uint32_t offset { };
    std::uint32_t width { 1 };
};

/// Normalize a runtime signed index against an elaborated packed range.
///
/// The right bound occupies normalized offset zero in the packed runtime,
/// independent of whether the source range is ascending or descending.
struct DynamicIndex {
    RegisterId index { };
    std::int64_t left { };
    std::int64_t right { };
    std::uint32_t base_offset { };

    friend bool operator==(const DynamicIndex&, const DynamicIndex&) = default;
};

/// Extract one scalar element selected by a runtime index.
struct DynamicExtract {
    RegisterId destination { };
    RegisterId source { };
    DynamicIndex selection;
};

/// Extract a fixed-width indexed part-select from a runtime signed base;
/// out-of-range bits become X, or zero for a two-state source.
struct DynamicPartSelect {
    RegisterId destination { };
    RegisterId source { };
    RegisterId base { };
    std::int64_t left { };
    std::int64_t right { };
    std::uint32_t width { };
    bool increasing { };
    bool source_descending { };
    bool two_state { };
    std::uint32_t base_offset { };
};

/// Apply the language-neutral dynamic part-select rules to an already
/// materialized packed value. This is shared by the interpreter and narrow
/// native callbacks so indexed reads never require a whole-array frame copy.
[[nodiscard]] PackedLogic4 dynamic_part_select_value(
    const PackedLogic4& source,
    const PackedLogic4& base,
    std::int64_t left,
    std::int64_t right,
    std::uint32_t base_offset,
    std::uint32_t width,
    bool increasing,
    bool source_descending,
    bool two_state);

/// Runtime base and fixed-width metadata shared by indexed part-select writes.
struct DynamicPartIndex {
    RegisterId base { };
    std::int64_t left { };
    std::int64_t right { };
    std::uint32_t base_offset { };
    std::uint32_t width { };
    bool increasing { };
    bool source_descending { };

    friend bool operator==(
        const DynamicPartIndex&, const DynamicPartIndex&) = default;
};

/// The representable intersection of an indexed part-select write.
struct DynamicPartWrite {
    PackedLogic4 value;
    std::uint32_t offset { };
};

/// Resolve a runtime bit-select to a normalized least-significant offset.
[[nodiscard]] std::uint32_t dynamic_index_offset(
    const PackedLogic4& index,
    const DynamicIndex& selection);

/// Resolve an indexed part-select write without imposing a host-word limit.
[[nodiscard]] std::optional<DynamicPartWrite>
dynamic_part_write_value(
    const PackedLogic4& source,
    const PackedLogic4& base,
    const DynamicPartIndex& selection);

/// Replace a contiguous normalized range in a packed value.
struct Insert {
    RegisterId destination { };
    RegisterId target { };
    RegisterId source { };
    std::uint32_t offset { };
};

/// Replace one scalar element selected by a runtime index.
struct DynamicInsert {
    RegisterId destination { };
    RegisterId target { };
    RegisterId source { };
    DynamicIndex selection;
};

/// Replace representable bits; unknown or wholly out-of-range bases do nothing.
struct DynamicPartInsert {
    RegisterId destination { };
    RegisterId target { };
    RegisterId source { };
    DynamicPartIndex selection;
};
/// Concatenate packed operands in source order. The first operand occupies
/// the most-significant result bits.
struct Concatenate {
    RegisterId destination { };
    std::vector<RegisterId> operands;
    std::uint32_t width { };
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
    RegisterId destination { };
    RegisterId lhs { };
    RegisterId rhs { };
};

enum class IntegerUnaryOperator : std::uint8_t {
    negate,
    absolute,
};

/// A checked operation on the portable signed 32-bit VHDL integer
/// representation. Unknown operands and overflow are language errors.
struct IntegerUnary {
    IntegerUnaryOperator operation { IntegerUnaryOperator::negate };
    RegisterId destination { };
    RegisterId source { };
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
    IntegerBinaryOperator operation { IntegerBinaryOperator::add };
    RegisterId destination { };
    RegisterId lhs { };
    RegisterId rhs { };
};

/// Require a known signed 32-bit value to belong to an elaborated VHDL
/// scalar subtype before it is stored.
struct IntegerCheck {
    RegisterId source { };
    std::int32_t lower { };
    std::int32_t upper { };
};

/// Select between equal-width values using SystemVerilog conditional
/// semantics. An X/Z condition merges matching bits and produces X for
/// differing bits.
struct ConditionalSelect {
    RegisterId destination { };
    RegisterId condition { };
    RegisterId when_true { };
    RegisterId when_false { };
};

/// Commit a new value immediately in the active phase.
struct WriteBlocking {
    SignalId signal { };
    RegisterId source { };
};

/// Queue a new value for the current timestamp's update phase.
struct WriteUpdate {
    SignalId signal { };
    RegisterId source { };
};

/// Queue a new value for a future timestamp's update phase.
struct WriteAfter {
    SignalId signal { };
    RegisterId source { };
    SimulationTick delay { };
};

struct TransitionDelays {
    SimulationTick rise { };
    SimulationTick fall { };
    SimulationTick turnoff { };

    friend bool operator==(
        const TransitionDelays&,
        const TransitionDelays&) = default;
};

/// Queue a continuous-assignment value with transition-specific inertial
/// delay. A later evaluation of the same driver supersedes its pending value.
struct WriteInertial {
    SignalId signal { };
    RegisterId source { };
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
    SignalId signal { };
    RegisterId source { };
    SimulationTick delay { };
    SimulationTick rejection { };
    ProjectedDelayMode mode { ProjectedDelayMode::inertial };
};

struct ProjectedWaveformElement {
    RegisterId source { };
    SimulationTick delay { };

    friend bool operator==(
        const ProjectedWaveformElement&,
        const ProjectedWaveformElement&) = default;
};

/// Atomically replace a VHDL driver's projected output waveform with an
/// ordered group of new transactions.
struct WriteProjectedWaveform {
    SignalId signal { };
    std::vector<ProjectedWaveformElement> elements;
    SimulationTick rejection { };
    ProjectedDelayMode mode { ProjectedDelayMode::inertial };
};

/// Replace a contiguous packed range immediately in the active phase.
struct WriteBlockingSlice {
    SignalId signal { };
    RegisterId source { };
    std::uint32_t offset { };
};

/// Stage a contiguous packed range for the common update phase.
struct WriteUpdateSlice {
    SignalId signal { };
    RegisterId source { };
    std::uint32_t offset { };
};

/// Stage a contiguous packed range after a simulation-time delay.
struct WriteAfterSlice {
    SignalId signal { };
    RegisterId source { };
    std::uint32_t offset { };
    SimulationTick delay { };
};

struct WriteInertialSlice {
    SignalId signal { };
    RegisterId source { };
    std::uint32_t offset { };
    TransitionDelays delays;
};

struct WriteProjectedSlice {
    SignalId signal { };
    RegisterId source { };
    std::uint32_t offset { };
    SimulationTick delay { };
    SimulationTick rejection { };
    ProjectedDelayMode mode { ProjectedDelayMode::inertial };
};

struct WriteProjectedWaveformSlice {
    SignalId signal { };
    std::vector<ProjectedWaveformElement> elements;
    std::uint32_t offset { };
    SimulationTick rejection { };
    ProjectedDelayMode mode { ProjectedDelayMode::inertial };
};

struct WriteBlockingDynamicSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicIndex selection;
};

struct WriteUpdateDynamicSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicIndex selection;
};

struct WriteAfterDynamicSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicIndex selection;
    SimulationTick delay { };
};

struct WriteBlockingDynamicPartSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicPartIndex selection;
};

struct WriteUpdateDynamicPartSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicPartIndex selection;
};

struct WriteAfterDynamicPartSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicPartIndex selection;
    SimulationTick delay { };
};

/// Force a static packed region while drivers continue beneath its mask.
struct ForceSignalSlice {
    SignalId signal { };
    RegisterId source { };
    std::uint32_t offset { };
    std::optional<DynamicIndex> selection;
    bool driving_value { };
};

/// Release a static packed force region and reveal current driven bits.
struct ReleaseSignalSlice {
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    std::optional<DynamicIndex> selection;
    bool driving_value { };
};
struct WriteInertialDynamicSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicIndex selection;
    TransitionDelays delays;
};

struct WriteInertialDynamicPartSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicPartIndex selection;
    TransitionDelays delays;
};

struct WriteProjectedDynamicSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicIndex selection;
    SimulationTick delay { };
    SimulationTick rejection { };
    ProjectedDelayMode mode { ProjectedDelayMode::inertial };
};

struct WriteProjectedWaveformDynamicSlice {
    SignalId signal { };
    std::vector<ProjectedWaveformElement> elements;
    DynamicIndex selection;
    SimulationTick rejection { };
    ProjectedDelayMode mode { ProjectedDelayMode::inertial };
};

struct ProjectedWaveformValue {
    PackedLogic4 value;
    SimulationTick delay { };
};

/// Return the shortest delay required by the bits that actually change.
/// A transition to X uses the shortest rise/fall/turnoff delay. No value is
/// returned when the packed values are equal.
[[nodiscard]] std::optional<SimulationTick> transition_delay(
    const PackedLogic4& current,
    const PackedLogic4& next,
    const TransitionDelays& delays);

struct WaitFor {
    WaitFor() = default;
    explicit WaitFor(const SimulationTick static_delay)
        : delay(static_delay)
    {
    }
    // A static delay when source is empty; otherwise the normalized tick scale
    // applied to the runtime value held in source.
    SimulationTick delay { };
    std::optional<RegisterId> source;
    std::uint32_t source_width { };
    SystemVerilogScalarKind source_kind { SystemVerilogScalarKind::None };
    bool source_signed { };
    SimulationTick rounding_quantum { 1 };
};

/// Suspend and resume later in the current time slot. This is used when one
/// language construct has distinct evaluation and action regions.
struct WaitRegion {
    SchedulerPhase phase { SchedulerPhase::reactive };
};

enum class EdgeKind : std::uint8_t {
    any,
    posedge,
    negedge,
    transaction,
};

/// Suspend until a listed signal has its corresponding edge. An empty edge
/// list means any change for every signal.
struct WaitOn {
    WaitOn() = default;
    explicit WaitOn(std::vector<SignalId> waited_signals)
        : signals(std::move(waited_signals))
    {
    }
    WaitOn(
        std::vector<SignalId> waited_signals,
        std::vector<EdgeKind> waited_edges)
        : signals(std::move(waited_signals))
        , edges(std::move(waited_edges))
    {
    }

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

/// Suspend until either a packed input term changes or the selected fixed
/// personality memory is written. This is the asynchronous PLA rearm point.
struct WaitPla {
    ContainerObjectId memory { };
    std::vector<SignalId> signals;
};

/// Suspend until named events occur in the specified order. The result is one
/// when the complete sequence is observed and zero when another listed event
/// occurs before the next expected event.
struct WaitOrder {
    std::vector<SignalId> events;
    RegisterId result { };
};

/// Query whether a named event has triggered anywhere in the current time
/// step. Unlike SignalEvent, this remains true across subsequent deltas at the
/// same simulation time.
struct EventTriggered {
    RegisterId destination { };
    SignalId event { };
};

/// Assign one named-event variable handle to another. The target subsequently
/// observes the same synchronization object without itself triggering it.
struct EventAlias {
    SignalId target { };
    SignalId source { };
    bool has_source { };
};

/// Suspend until this process's static sensitivity condition is met.
struct WaitSensitivity { };

/// Suspend permanently without completing the process. This models bare
/// waits and dependency-free condition waits while preserving
/// debugger-visible suspended state.
struct WaitForever { };

/// Suspend and resume in the active phase of the next delta cycle.
struct Yield { };
enum class ForkJoinKind : std::uint8_t { all,
    any,
    none };
/// Spawn child PCs sharing the lexical frame; each ends at ForkEnd.
struct Fork {
    std::vector<InstructionIndex> branches;
    ForkJoinKind join { ForkJoinKind::all };
};
struct ForkEnd { };
struct WaitFork { };
/// Cancel every descendant fork, or only children spawned by one named fork
/// site when `site` is present.
struct DisableFork {
    std::optional<InstructionIndex> site;
};
/// Terminate one dynamically active named sequential block. `begin` and `end`
/// delimit its lexical operation interval; execution resumes at `end`.
struct DisableBlock {
    InstructionIndex begin { };
    InstructionIndex end { };
};
/// Values returned by the SystemVerilog process status query.
enum class ProcessStatus : std::uint8_t {
    finished = 0,
    running = 1,
    waiting = 2,
    suspended = 3,
    killed = 4,
};
/// Materialize a generation-safe handle for the currently executing process.
struct ProcessSelf {
    RegisterId destination { };
};
/// Query the SystemVerilog process status ordinal for a handle.
struct ProcessStatusQuery {
    RegisterId destination { };
    RegisterId source { };
};
/// Query whether a process has finished normally or was killed.
struct ProcessCompleted {
    RegisterId destination { };
    RegisterId source { };
};
/// Suspend the current process until the target reaches a terminal state.
struct ProcessAwait {
    RegisterId source { };
};
/// Recursively terminate the target process and its dynamic descendants.
struct ProcessKill {
    RegisterId source { };
};
/// Suspend a live process until a matching resume request. A suspended wait
/// remains armed and records a wakeup without running the process.
struct ProcessSuspend {
    RegisterId source { };
};
/// Resume a suspended process, preserving an untriggered wait or making a
/// process runnable when it was suspended while active or subsequently woke.
struct ProcessResume {
    RegisterId source { };
};
/// Return the target process's opaque deterministic random-state token.
struct ProcessGetRandState {
    StringRegisterId destination { };
    RegisterId source { };
};
/// Restore a random-state token previously returned for any process.
struct ProcessSetRandState {
    RegisterId source { };
    StringRegisterId state { };
};
/// Seed the target process's deterministic random stream from a 32-bit value.
struct ProcessSrandom {
    RegisterId source { };
    RegisterId seed { };
};

/// Construct a typed mailbox. A zero capacity selects the bounded unbounded
/// form; nonzero capacities are exact maximum entry counts.
struct MailboxCreate {
    RegisterId destination { };
    RegisterId capacity { };
    std::uint32_t element_width { };
};

/// Put one packed value. A result register selects nonblocking `try_put` and
/// receives one on success; no result register selects blocking `put`.
struct MailboxPut {
    RegisterId receiver { };
    RegisterId source { };
    std::uint32_t element_width { };
    std::optional<RegisterId> result;
};

/// Get or peek one packed value. A result register selects the corresponding
/// nonblocking `try_*` form and receives one on success.
struct MailboxGet {
    RegisterId receiver { };
    RegisterId destination { };
    std::uint32_t element_width { };
    std::optional<RegisterId> result;
    bool peek { };
};

struct MailboxNum {
    RegisterId destination { };
    RegisterId receiver { };
};

struct SemaphoreCreate {
    RegisterId destination { };
    RegisterId keys { };
};

/// Acquire keys. A result register selects nonblocking `try_get`.
struct SemaphoreGet {
    RegisterId receiver { };
    RegisterId keys { };
    std::optional<RegisterId> result;
};

struct SemaphorePut {
    RegisterId receiver { };
    RegisterId keys { };
};
struct Jump {
    InstructionIndex target { };
};

/// Persistent hidden-register storage used by resumable SimIR subroutines.
///
/// The stack pointer and each entry are 32-bit, two-state registers. The
/// lowering which owns the process must initialize all of them before the
/// first call. Keeping this state in ordinary process registers gives the
/// interpreter and generated code the same suspension-safe representation.
struct CallStack {
    RegisterId pointer { };
    RegisterId entries { };
    std::uint32_t capacity { };
};

/// Enter a non-suspending or resumable SimIR subroutine.
///
/// return_target is pushed before control transfers to target. A frontend is
/// may select a runtime-owned dynamic stack by leaving CallStack zeroed, or a
/// fixed-register stack for an explicitly bounded ABI.
struct Call {
    InstructionIndex target { };
    InstructionIndex return_target { };
    CallStack stack;
};

/// Return to the most recently pushed Call return_target.
struct Return {
    CallStack stack;
};

/// Save one automatic callable's invocation-owned registers before its
/// formals and locals are reused by a nested call. The runtime owns the
/// dynamically sized snapshot stack; the explicit register lists keep
/// lexical storage outside the callable shared exactly as before.
struct CallableFramePush {
    std::uint32_t identity { };
    std::vector<RegisterId> packed;
    std::vector<StringRegisterId> strings;
    std::vector<ContainerRegisterId> containers;
    // The elaborator assigns disjoint registers to each callable identity.
    // Optimized native lowering may therefore keep an acyclic call chain in
    // generated code without taking host-owned register snapshots. Hand-built
    // SimIR remains conservative unless it explicitly proves this property.
    bool native_isolated { };
};

/// Restore the most recently saved invocation of one automatic callable while
/// retaining call-result shuttle registers written by the completed callee.
struct CallableFramePop {
    std::uint32_t identity { };
    std::vector<RegisterId> preserve_packed;
    std::vector<StringRegisterId> preserve_strings;
    std::vector<ContainerRegisterId> preserve_containers;
};

enum class UnknownBranchPolicy : std::uint8_t {
    error,
    when_false,
};

/// Branch on a scalar one; zero selects when_false. X/Z handling follows the
/// operation's explicit language policy.
struct Branch {
    RegisterId condition { };
    InstructionIndex when_true { };
    InstructionIndex when_false { };
    UnknownBranchPolicy unknown_policy { UnknownBranchPolicy::error };
};

enum class AssertionSeverity : std::uint8_t {
    note,
    warning,
    error,
    failure,
};

struct SourceLocation {
    std::string path;
    std::uint32_t line { 1 };
    std::uint32_t column { 1 };

    friend bool operator==(const SourceLocation&,
        const SourceLocation&) = default;
};

#include "fsim/runtime/simir_vital.hpp"

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
    std::uint32_t width { };
    bool is_signed { };
    ExpressionSizingKind sizing { ExpressionSizingKind::self_determined };
    ExpressionValueDomain domain { ExpressionValueDomain::four_state };
};

enum class DebugPointKind : std::uint8_t {
    statement,
    call,
    wait,
    assertion,
    process_entry,
};

struct DebugPoint {
    DebugPointKind kind { DebugPointKind::statement };
    SourceLocation source;
    // Canonical hierarchy-qualified lexical execution scope. This is metadata
    // rather than a scheduler operand, but remains part of native provenance.
    std::string scope;

    DebugPoint() = default;
    DebugPoint(
        const DebugPointKind point_kind,
        SourceLocation point_source,
        std::string point_scope = { })
        : kind(point_kind)
        , source(std::move(point_source))
        , scope(std::move(point_scope))
    {
    }
};

struct Assert {
    RegisterId condition { };
    std::string message;
    AssertionSeverity severity { AssertionSeverity::error };
    SourceLocation source;
};

/// Emit already-formatted language output synchronously on the simulation
/// thread. The embedding layer owns the destination stream.
struct Display {
    std::string text;
    bool newline { true };
    bool postponed { };
};

enum class OutputFormat : std::uint8_t {
    binary,
    hexadecimal,
    octal,
    decimal,
    character,
    string,
    real_scientific,
    real_fixed,
    real_general,
    time,
};

/// Format one runtime value between literal prefix/suffix text.
struct FormatDisplay {
    RegisterId source { };
    OutputFormat format { OutputFormat::binary };
    std::string prefix;
    std::string suffix;
    bool newline { true };
    bool postponed { };
    bool signed_decimal { };
    bool suppress_leading_zero { };
    std::uint32_t minimum_width { };
    bool left_justify { };
    bool zero_pad { };
    SystemVerilogScalarKind scalar_kind { SystemVerilogScalarKind::None };
};

struct StringDisplay {
    StringRegisterId source { };
    std::string prefix;
    std::string suffix;
    bool newline { true };
    bool postponed { };
};

/// Emit the current global simulation tick between literal prefix/suffix
/// text. The tick is captured when this operation executes.
struct TimeDisplay {
    std::string prefix;
    std::string suffix;
    bool newline { true };
    bool postponed { };
    std::uint32_t minimum_width { };
    bool left_justify { };
    bool zero_pad { };
    bool use_timeformat_width { };
};

enum class MonitorValueKind : std::uint8_t {
    signal,
    time,
};

struct MonitorValue {
    MonitorValueKind kind { MonitorValueKind::signal };
    SignalId signal { };
    OutputFormat format { OutputFormat::decimal };
    std::string prefix;
    bool signed_decimal { };
    bool suppress_leading_zero { };
    std::uint32_t minimum_width { };
    bool left_justify { };
    bool zero_pad { };
    SystemVerilogScalarKind scalar_kind { SystemVerilogScalarKind::None };
    bool use_timeformat_width { };
};

/// Replace the global Verilog/SystemVerilog monitor registration and publish
/// its current value once in the postponed phase.
struct MonitorInstall {
    std::vector<MonitorValue> values;
    std::string trailing_text;
    bool newline { true };
    bool one_shot { };
    std::optional<RegisterId> file_handle;
};

/// Enable or disable the current monitor without discarding its registration.
struct MonitorControl {
    bool enabled { };
};

/// Replace the simulation-wide SystemVerilog %t formatting profile. Units is
/// the decimal exponent relative to seconds (-15 through 0), precision is the
/// number of fractional digits, and minimum_width includes the suffix.
struct TimeFormatControl {
    RegisterId units { };
    RegisterId precision { };
    StringRegisterId suffix { };
    RegisterId minimum_width { };
};

/// Query the ordered simulation command line for a plusarg. The query is a
/// byte string without the leading '+'. When selected is present, the first
/// matching complete plusarg is copied there for a following formatted scan.
struct PlusArgSelect {
    RegisterId destination { };
    StringRegisterId query { };
    std::optional<StringRegisterId> selected;
};

/// Execute the standard SystemVerilog $system host boundary. An absent command
/// preserves the C system(NULL) query, while an absent destination represents
/// task use where the raw C int return value is discarded.
struct SystemCommand {
    std::optional<StringRegisterId> command;
    std::optional<RegisterId> destination;
};

enum class VcdControlKind : std::uint8_t {
    file,
    variables,
    begin_variables,
    off,
    on,
    all,
    limit,
    flush,
    ports,
    begin_ports,
    ports_off,
    ports_on,
    ports_all,
    ports_limit,
    ports_flush,
};

/// Control the IEEE four-state value-change dump owned by the HDL model.
/// Hierarchy selections retain their source spelling and are resolved by the
/// application against DesignIR when all same-time $dumpvars calls have run.
struct VcdControl {
    VcdControlKind kind { VcdControlKind::variables };
    std::optional<StringRegisterId> filename;
    std::optional<RegisterId> value;
    std::vector<std::string> selections;
    std::string scope;
};

struct VcdControlEvent {
    VcdControlKind kind { VcdControlKind::variables };
    std::string filename;
    std::uint64_t value { };
    std::vector<std::string> selections;
    std::string scope;
    SimulationTick time { };
    std::uint64_t delta { };
};

enum class CoverageDatabaseControlKind : std::uint8_t {
    set_name,
    load,
};

/// Select the final functional-coverage database or merge a previously saved
/// database into the live elaborated coverage state.
struct CoverageDatabaseControl {
    CoverageDatabaseControlKind kind {
        CoverageDatabaseControlKind::set_name
    };
    StringRegisterId filename { };
};

struct CoverageDatabaseControlEvent {
    CoverageDatabaseControlKind kind {
        CoverageDatabaseControlKind::set_name
    };
    std::string filename;
};

enum class StochasticQueueKind : std::uint8_t {
    initialize,
    add,
    remove,
    full,
    examine,
};

/// Execute one IEEE stochastic queue operation. All operands use the standard
/// 32-bit signed integer profile. Optional fields are present only for the
/// operation kinds which consume or produce them.
struct StochasticQueueOperation {
    StochasticQueueKind kind { StochasticQueueKind::initialize };
    RegisterId queue_id { };
    std::optional<RegisterId> queue_type;
    std::optional<RegisterId> maximum_length;
    std::optional<RegisterId> job_id;
    std::optional<RegisterId> information_id;
    std::optional<RegisterId> statistic_code;
    std::optional<RegisterId> statistic_value;
    RegisterId status { };
    std::optional<RegisterId> result;
};

enum class PlaLogicKind : std::uint8_t {
    and_logic,
    nand_logic,
    or_logic,
    nor_logic,
};

/// Evaluate one IEEE programmable-logic-array personality. The fixed memory
/// contains one input-width word per output bit in declared array order.
struct PlaEvaluate {
    ContainerObjectId memory { };
    RegisterId input { };
    RegisterId output { };
    std::uint32_t input_width { };
    std::uint32_t output_width { };
    PlaLogicKind logic { PlaLogicKind::and_logic };
    bool plane { };
};

/// Sample one resolved SystemVerilog covergroup instance through the host
/// coverage service. Packed actuals retain their complete four/nine-state
/// register values; signed_actuals carries only source sizing semantics.
enum class CoverageSampleTrigger : std::uint8_t {
    explicit_sample,
    procedural,
    event
};

struct CoverageSample {
    std::string instance_identity;
    std::vector<RegisterId> actuals;
    std::vector<std::uint32_t> actual_widths;
    std::vector<std::uint8_t> signed_actuals;
    CoverageSampleTrigger trigger { CoverageSampleTrigger::explicit_sample };
};

enum class CoverageQueryKind : std::uint8_t {
    overall_type,
    overall_instance,
};

/// Query the simulation-owned aggregate functional coverage. The destination
/// receives the exact 64-bit SystemVerilog real payload for a percentage in
/// the inclusive range 0 through 100.
struct CoverageQuery {
    RegisterId destination { };
    CoverageQueryKind kind { CoverageQueryKind::overall_type };
};

enum class RandomKind : std::uint8_t {
    urandom,
    random,
    urandom_range,
};

/// Produce one deterministic 32-bit random value from the current process's
/// project-seeded stream.
struct RandomValue {
    RegisterId destination { };
    RandomKind kind { RandomKind::urandom };
    std::optional<RegisterId> maximum;
    std::optional<RegisterId> minimum;
};

enum class RandomDistributionKind : std::uint8_t {
    uniform,
    normal,
    exponential,
    poisson,
    chi_square,
    student_t,
    erlang,
};

/// Evaluate one IEEE random-distribution system function. Seed is an inout
/// 32-bit signed integer; first and optional second are the distribution
/// parameters after the seed argument.
struct RandomDistribution {
    RegisterId destination { };
    RegisterId seed { };
    RandomDistributionKind kind { RandomDistributionKind::uniform };
    RegisterId first { };
    std::optional<RegisterId> second;
};
#include "fsim/runtime/simir_randomize.hpp"
/// Emit a nonfatal VHDL report with retained severity and source metadata.
struct Report {
    std::string message;
    AssertionSeverity severity { AssertionSeverity::note };
    SourceLocation source;
};

/// Emit a VHDL assertion/report whose message and severity were evaluated at
/// the statement execution point.
struct StringReport {
    StringRegisterId message { };
    RegisterId severity { };
    SourceLocation source;
    bool standalone { };
};

/// Stop the complete simulation, as requested by `$finish` or an equivalent
/// language construct.
struct Stop { };

/// Pause simulation at a resumable boundary, as requested by `$stop`.
struct Pause { };

struct Halt {
    bool program_exit { };
};

#include "fsim/runtime/simir_class.hpp"
#include "fsim/runtime/simir_operation_storage.hpp"

#include "fsim/runtime/simir_signal.hpp"

struct StringObject {
    std::string name;
    std::string initial_value;
};

/// One fixed-array object view backed by a selected range of an earlier
/// object. The view's own ContainerValue type supplies the child/formal
/// declared range; selected_left/right name the parent range. Reads and
/// writes map equal-count elements ordinally.
struct ContainerSliceAlias {
    ContainerObjectId object { };
    std::int32_t selected_left { };
    std::int32_t selected_right { };

    friend bool operator==(
        const ContainerSliceAlias&,
        const ContainerSliceAlias&) = default;
};

struct ContainerObject {
    std::string name;
    ContainerValue initial_value;
    std::optional<ContainerSliceAlias> slice_alias;
};

struct ContainerSignalAlias {
    ContainerObjectId object { };
    SignalId signal { };
    bool readable { };
    bool writable { };
};

struct Sensitivity {
    SignalId signal { };
    EdgeKind edge = EdgeKind::any;

    bool operator==(const Sensitivity&) const = default;
};

#include "fsim/runtime/simir_debug.hpp"
#include "fsim/runtime/simir_specify.hpp"
struct Process {
    static constexpr std::uint64_t full_static_trigger_mask
        = UINT64_C(1) << 63U;
    ProcessId id { };
    std::string name;
    /// Canonical source-language identity for native-code cache separation.
    /// Non-VHDL processes leave both profile fields empty.
    std::string language_standard { };
    std::string compatibility_profile { };
    std::size_t register_count { };
    std::size_t string_register_count { };
    std::size_t container_register_count { };
    std::vector<DebugLocal> debug_locals;
    std::vector<DebugStringLocal> debug_string_locals;
    std::vector<DebugContainerLocal> debug_container_locals;
    std::vector<ContainerType> container_register_types;
    std::vector<Sensitivity> static_sensitivity;
    /// Callback-free statement ranges which may be skipped by a compiled
    /// executor when none of their exact static sensitivities triggered the
    /// current activation. Bit 63 requests a full activation; bits 0..62 map
    /// to the canonical sorted static_sensitivity vector. The interpreter
    /// deliberately ignores this optional lowering hint.
    struct StaticTriggerRegion {
        InstructionIndex begin { };
        InstructionIndex end { };
        std::uint64_t mask { };
        friend bool operator==(
            const StaticTriggerRegion&, const StaticTriggerRegion&) = default;
    };
    std::vector<StaticTriggerRegion> static_trigger_regions;
    std::vector<Operation> operations;
    /// Static packed regions driven by this process. Dynamic or whole-object
    /// targets retain one `whole` region for conservative ownership.
    struct DriverRegion {
        SignalId signal { };
        std::uint32_t offset { };
        std::uint32_t width { };
        bool whole { };
        friend bool operator==(const DriverRegion&, const DriverRegion&) = default;
    };
    std::vector<DriverRegion> driver_regions;
    DriveStrength drive_strength;
    std::optional<SignalId> switch_source;
    std::optional<SignalId> switch_target;
    std::optional<SignalId> switch_control;
    /// A zero width retains the ordinary whole-terminal switch rules,
    /// including scalar-to-vector broadcast. A nonzero width connects the
    /// two statically selected packed regions lane-for-lane.
    std::uint64_t switch_source_offset { };
    std::uint64_t switch_target_offset { };
    std::uint64_t switch_width { };
    bool switch_active_high { true };
    bool switch_bidirectional { };
    bool switch_resistive { };
    std::vector<ValueKind> register_value_kinds;
    bool initialize { true };
    // Clocking input samplers execute after ordinary updates and before
    // program/reactive code observes the sampled values.
    bool observed { };
    // Program-owned processes execute in the SystemVerilog reactive region
    // after active/inactive updates and before postponed observation.
    bool reactive { };
    // Stable elaborated-program instance identity. All static and dynamically
    // spawned processes owned by one SystemVerilog program share this value.
    std::optional<std::uint32_t> program_owner;
    // VHDL postponed processes execute after all ordinary update/reactive work
    // for the current simulation cycle.
    bool postponed { };
    // A SystemVerilog final process is excluded from ordinary initialization
    // and queued exactly once when ordinary simulation terminates.
    bool final { };
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

#include "fsim/runtime/simir_process_execution_context.hpp"

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
    ExternalSuspendKind kind { ExternalSuspendKind::simir_boundary };
    SimulationTick delay { };
    std::vector<Sensitivity> sensitivity;
    bool wait_all { };
    std::optional<SimulationTick> timeout;
};
/// Alternate-executor boundary and its exact sequential resume PC.
struct ProcessResumeResult {
    ProcessResumeResult() = default;
    constexpr ProcessResumeResult(
        const InstructionIndex boundary_instruction,
        const InstructionIndex resume_instruction) noexcept
        : instruction(boundary_instruction)
        , next_instruction(resume_instruction)
    {
    }
    InstructionIndex instruction { };
    InstructionIndex next_instruction { };
    ExternalSuspension external;
};

class ProcessExecutor;

/// One independently owned process activation within a scheduler cohort.
///
/// The scheduler preserves process order and process-local execution contexts;
/// an alternate executor may use this view to amortize an execution boundary
/// across processes that became runnable from the same static event.
struct ProcessCohortResumeEntry {
    ProcessExecutor* executor { };
    ProcessExecutionContext* context { };
    InstructionIndex start_instruction { };
    ProcessResumeResult result;
    std::exception_ptr failure;
    bool* queued { };
    bool* waiting_on_static { };
    ProcessStatus* status { };
    std::uint8_t* active { };
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
    ProcessId process { }, design_process { };
    InstructionIndex instruction { };
    ExecutionPointKind kind { ExecutionPointKind::statement };
    SourceLocation source;
    std::string scope;
    std::string language_standard;
    std::string compatibility_profile;

    ExecutionPoint() = default;
    ExecutionPoint(
        const ProcessId execution_process,
        const ProcessId source_process,
        const InstructionIndex execution_instruction,
        const ExecutionPointKind execution_kind,
        SourceLocation execution_source,
        std::string execution_scope = { },
        std::string execution_language_standard = { },
        std::string execution_compatibility_profile = { })
        : process(execution_process)
        , design_process(source_process)
        , instruction(execution_instruction)
        , kind(execution_kind)
        , source(std::move(execution_source))
        , scope(std::move(execution_scope))
        , language_standard(std::move(execution_language_standard))
        , compatibility_profile(
              std::move(execution_compatibility_profile))
    {
    }
};
class ProcessExecutor {
public:
    static constexpr auto native_register_width = std::numeric_limits<std::size_t>::max();

    virtual ~ProcessExecutor() = default;
    /// Share the lexical frame with a child owning an independent PC.
    [[nodiscard]] virtual std::unique_ptr<ProcessExecutor> fork_clone(
        InstructionIndex)
    {
        throw std::logic_error { "executor does not support fork cloning" };
    }
    /// Redirect the executor-owned program counter after a kernel-level
    /// nonlocal control transfer.
    virtual void redirect(InstructionIndex)
    {
        throw std::logic_error { "executor does not support control redirection" };
    }
    /// Execute from start_instruction until the next SimIR kernel boundary.
    ///
    /// Implementations own their register/frame storage. Exceptions must be
    /// raised only in C++ after any generated plain-C call has returned.
    [[nodiscard]] virtual ProcessResumeResult
    resume(ProcessExecutionContext& context,
        InstructionIndex start_instruction) = 0;

    /// Resume an ordered set of independently owned processes as one cohort.
    /// Returning zero declines the cohort and leaves every entry untouched.
    /// A nonzero return is the number of leading entries executed; failures
    /// are captured in the corresponding entry so earlier results remain
    /// visible to the scheduler in canonical order.
    [[nodiscard]] virtual std::size_t resume_cohort(
        std::span<ProcessCohortResumeEntry>)
    {
        return 0U;
    }

    /// Resume the selected members of a stable region. Each entry supplies a
    /// persistent active byte; returning zero declines region execution.
    [[nodiscard]] virtual std::size_t resume_region(
        std::span<ProcessCohortResumeEntry>,
        std::span<const std::size_t>)
    {
        return 0U;
    }

    /// Whether resume_cohort() applies each entry's scheduler-state
    /// transition immediately around that member's native activation.
    /// A state-aware executor must leave every unexecuted entry untouched.
    [[nodiscard]] virtual bool cohort_manages_process_state() const noexcept
    {
        return false;
    }

    /// Opaque execution-domain identity used only to form compatible cohort
    /// runs. A null domain declines cohort execution.
    [[nodiscard]] virtual const void* cohort_domain() const noexcept
    {
        return nullptr;
    }

    /// Execute a previously requested primitive-channel update. Only
    /// alternate-language executors which expose such channels override this.
    virtual void update_channel(
        std::uint64_t,
        ProcessExecutionContext&)
    {
        throw std::logic_error {
            "alternate process executor has no primitive-channel callback"
        };
    }

    /// The native-width sentinel requests the exact executor-frame width for a
    /// polymorphic packed-value boundary.
    [[nodiscard]] virtual PackedLogic4
    read_register(RegisterId, std::size_t) const
    {
        throw std::logic_error {
            "alternate process executor does not expose register values"
        };
    }

    /// Snapshot lexical storage for an automatic callable. An alternate
    /// executor may materialize an as-yet-uninitialized register as its
    /// language-default unknown value instead of treating this internal save
    /// as a debugger read.
    [[nodiscard]] virtual PackedLogic4
    snapshot_register(const RegisterId id) const
    {
        return read_register(id, native_register_width);
    }

    virtual void write_register(
        RegisterId, const PackedLogic4&)
    {
        throw std::logic_error {
            "alternate process executor does not expose writable registers"
        };
    }

    [[nodiscard]] virtual std::string
    read_string_register(StringRegisterId) const
    {
        throw std::logic_error {
            "alternate process executor does not expose string registers"
        };
    }

    virtual void write_string_register(
        StringRegisterId, std::string_view)
    {
        throw std::logic_error {
            "alternate process executor does not expose writable string "
            "registers"
        };
    }

    [[nodiscard]] virtual ContainerValue
    read_container_register(ContainerRegisterId) const
    {
        throw std::logic_error {
            "alternate process executor does not expose container registers"
        };
    }

    virtual void write_container_register(
        ContainerRegisterId, const ContainerValue&)
    {
        throw std::logic_error {
            "alternate process executor does not expose writable container "
            "registers"
        };
    }
};

class InterpreterError : public std::runtime_error {
public:
    InterpreterError(ProcessId process, InstructionIndex instruction,
        std::string message);

    [[nodiscard]] ProcessId process() const noexcept { return process_; }
    [[nodiscard]] InstructionIndex instruction() const noexcept
    {
        return instruction_;
    }

private:
    ProcessId process_ { };
    InstructionIndex instruction_ { };
};

class AssertionError final : public InterpreterError {
public:
    AssertionError(ProcessId process, InstructionIndex instruction,
        std::string message, AssertionSeverity severity,
        SourceLocation source, bool reported = false);

    [[nodiscard]] AssertionSeverity severity() const noexcept
    {
        return severity_;
    }
    [[nodiscard]] const SourceLocation& source() const noexcept
    {
        return source_;
    }
    [[nodiscard]] bool reported() const noexcept
    {
        return reported_;
    }

private:
    AssertionSeverity severity_ { AssertionSeverity::error };
    SourceLocation source_;
    bool reported_ { };
};

/// Small reference interpreter for differential testing of generated code.
class Interpreter {
public:
    using SignalChangeHook = std::function<void(SignalId, const PackedLogic4&, SimulationTick)>;
    /// Returns true when an installed application bridge currently has a real
    /// observer for this signal. This lets dormant VPI/trace bridge hooks stay
    /// installed without forcing native updates through packed publication.
    using NativeSignalObservationRequiredHook
        = std::function<bool(SignalId)>;
    /// Returns true when any installed application bridge currently observes
    /// native signal publication. The update phase uses this scheduler-safe
    /// aggregate query to avoid repeating the per-signal callback when no
    /// observer exists anywhere.
    using NativeSignalObservationAnyHook = std::function<bool()>;
    /// Observes a change to the stored, unforced driver state. Unlike
    /// SignalChangeHook this also fires when a force masks the effective value.
    using StoredSignalChangeHook
        = std::function<void(SignalId, SimulationTick)>;
    /// Observes one resolved signal driver's underlying contribution even when
    /// the aggregate resolved value does not change.
    using DriverChangeHook
        = std::function<void(ProcessId, SignalId, SimulationTick)>;
    /// Observes an immediate, delta, or timed named-event trigger at delivery.
    using EventTriggerHook = std::function<void(SignalId, SimulationTick)>;
    /// Observes an actual write to a base container object after its value and
    /// any signal alias have been committed.
    using ContainerObjectChangeHook
        = std::function<void(ContainerObjectId, SimulationTick)>;
    using ExecutionPointHook = std::function<void(Scheduler&, const ExecutionPoint&)>;
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
    using CoverageSampleHook = std::function<void(
        std::string_view,
        std::span<const PackedLogic4>,
        std::span<const std::uint8_t>,
        CoverageSampleTrigger)>;
    using CoverageQueryHook = std::function<PackedLogic4(CoverageQueryKind)>;
    using SystemCommandHook = std::function<std::int32_t(
        std::optional<std::string_view>)>;
    using VcdControlHook = std::function<void(const VcdControlEvent&)>;
    using CoverageDatabaseControlHook
        = std::function<void(const CoverageDatabaseControlEvent&)>;
    /// Decide whether one dynamic fork operation may create its children.
    /// The identity is the static design process that owns the fork, including
    /// when a dynamic child creates a nested fork.
    using ForkSpawnFilter = std::function<bool(ProcessId)>;
    using ClassAllocateHook = std::function<std::uint64_t(
        std::string_view, std::string_view,
        std::string_view,
        std::span<const PackedLogic4>,
        std::span<const std::string>,
        std::span<const std::string>)>;
    using ClassPropertyReadHook = std::function<PackedLogic4(
        std::uint64_t, std::string_view)>;
    using ClassPropertyWriteHook = std::function<void(
        std::uint64_t, std::string_view, const PackedLogic4&)>;
    using ClassMethodCallHook = std::function<PackedLogic4(
        std::uint64_t,
        std::string_view,
        std::vector<PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>,
        std::span<const SystemVerilogConstraintTemplate>,
        bool)>;
    using ClassStaticPropertyReadHook = std::function<PackedLogic4(std::string_view)>;
    using ClassStaticPropertyWriteHook = std::function<void(
        std::string_view, const PackedLogic4&)>;
    using ClassStaticMethodCallHook = std::function<PackedLogic4(
        std::string_view,
        std::vector<PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>)>;

    explicit Interpreter(
        SchedulerOptions options = { },
        std::uint64_t seed = 1);
    ~Interpreter();
    Interpreter(Interpreter&&) noexcept;
    Interpreter& operator=(Interpreter&&) noexcept;
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    [[nodiscard]] SignalId add_signal(Signal signal);
    [[nodiscard]] StringObjectId add_string_object(StringObject object);
    [[nodiscard]] ContainerObjectId add_container_object(
        ContainerObject object);
    void add_container_signal_alias(ContainerSignalAlias alias);
    [[nodiscard]] ProcessId add_process(Process process);
    /// Register and validate one process topology without retaining its
    /// program or allocating its execution frame. The interpreter becomes a
    /// validation-only builder and cannot subsequently start.
    [[nodiscard]] ProcessId validate_process(const Process& process);
    [[nodiscard]] std::uint32_t add_module_path(ModulePath path);
    [[nodiscard]] std::uint32_t add_module_timing_check(
        ModuleTimingCheck check);
    /// Atomically replace topology-compatible module-path and timing-check
    /// timing. After start this is legal only from a scheduler safe-point hook.
    /// Pending path writes and timing-check history retain their old state;
    /// future events use the replacement timing.
    void reannotate_module_timing(
        std::span<const ModulePath> paths,
        std::span<const ModuleTimingCheck> checks);

    struct VitalTimingReannotation {
        ProcessId process { };
        InstructionIndex instruction { };
        bool timing_check { };
        std::array<SimulationTick, 6> values { };
        std::size_t value_count { };
    };

    /// Atomically replace topology-compatible VITAL delay/check timing. After
    /// start this is legal only from a scheduler safe-point hook. Pending
    /// projected writes retain their scheduled time; timing-check state is
    /// preserved unless reset_timing_state is requested.
    void reannotate_vital_timing(
        std::span<const VitalTimingReannotation> annotations,
        bool reset_timing_state = false);

    /// Restrict all HDL file operations to paths below this root. Must be set
    /// before start; an empty root leaves file operations disabled.
    void set_file_root(std::filesystem::path root);

    /// Replace the ordered, simulation-owned Verilog/SystemVerilog plusargs.
    /// Arguments may include their conventional leading '+'. Must be set
    /// before start.
    void set_plusargs(std::span<const std::string> plusargs);

    /// Set the duration represented by one scheduler tick. Must be set before
    /// start and is used by SystemVerilog $timeformat/%t services.
    void set_time_resolution_femtoseconds(std::uint64_t femtoseconds);

    /// Replace one process's reference evaluator with an alternate executor.
    ///
    /// The interpreter remains the sole scheduler and signal store. Installation
    /// is allowed only before start and at most once per process.
    void set_process_executor(
        ProcessId process, std::unique_ptr<ProcessExecutor> executor);

    /// Prepare an alternate executor without delaying simulation startup.
    /// The interpreter remains active until ready() reports completion, then
    /// migrates the lexical frame and installs the prepared executor at the
    /// next scheduler-owned process boundary. Both callbacks must be safe to
    /// destroy without first being invoked.
    void set_deferred_process_executor(
        ProcessId process,
        std::function<bool()> ready,
        std::function<std::unique_ptr<ProcessExecutor>()> take);

    /// Install every ready deferred executor before simulation starts. This
    /// preserves eager debugger and fork semantics for callers that explicitly
    /// wait for native compilation while allowing other callers to overlap
    /// materialization with scheduler execution.
    void materialize_ready_process_executors();

    void start();
    [[nodiscard]] RunResult
    run(std::optional<SimulationTick> until = std::nullopt);
    /// Discard ordinary pending work and execute final processes exactly once.
    /// This is used by an external finish request after the scheduler reaches
    /// its safe stopped boundary.
    [[nodiscard]] RunResult finish();

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

    [[nodiscard]] const PackedLogic4& signal_value(SignalId signal) const;
    /// Return the underlying effective driven value without applying a force.
    /// This keeps public driver state distinct from the visible signal value.
    [[nodiscard]] const PackedLogic4& stored_signal_value(
        SignalId signal) const;
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
    [[nodiscard]] PackedLogic4 driver_value(
        ProcessId process, SignalId signal) const;
    /// Return the strongest currently active zero and one contributions used
    /// by resolved-port observers such as extended VCD.
    [[nodiscard]] DriveStrength signal_strength(SignalId signal) const;
    /// Return the static design process that owns a dynamic fork child. Static
    /// processes map to themselves.
    [[nodiscard]] ProcessId design_process(ProcessId process) const;
    /// Return the immutable SimIR program owned by a static design process.
    /// The reference remains valid for the lifetime of this interpreter.
    [[nodiscard]] const Process& process_program(ProcessId process) const;
    /// Return the next SimIR instruction for a scheduler-owned process.
    [[nodiscard]] InstructionIndex process_instruction(
        ProcessId process) const;
    /// Enable and inspect lightweight interpreter-operation accounting for a
    /// process awaiting adaptive native promotion. Accounting is local to the
    /// scheduler thread and is sampled only from deferred-executor callbacks.
    void track_process_interpreter_operations(ProcessId process);
    [[nodiscard]] std::uint64_t process_interpreter_operations(
        ProcessId process) const;
    /// Return the top-level dynamic child that owns an execution descendant.
    /// Static processes map to themselves. This is the stable identity of one
    /// overlapping temporal attempt across nested lowering forks.
    [[nodiscard]] ProcessId dynamic_process_root(ProcessId process) const;
    /// Kill every live dynamic fork attempt owned by one of the selected
    /// static design processes. Static processes remain available for later
    /// assertion re-enablement.
    void kill_dynamic_processes(
        std::span<const ProcessId> design_processes);
    [[nodiscard]] PackedLogic4 read_debug_local(
        ProcessId process, std::size_t local_index) const;
    [[nodiscard]] std::string read_debug_string_local(
        ProcessId process, std::size_t local_index) const;
    [[nodiscard]] ContainerValue read_debug_container_local(
        ProcessId process, std::size_t local_index) const;
    /// True once a language-level Stop operation (`$finish` or equivalent) has
    /// executed. External scheduler stop requests do not set this flag.
    [[nodiscard]] bool stopped_by_design() const noexcept;
    [[nodiscard]] Scheduler& scheduler() noexcept;
    [[nodiscard]] const Scheduler& scheduler() const noexcept;
    void set_signal_change_hook(SignalChangeHook hook);
    void set_native_signal_observation_required_hook(
        NativeSignalObservationRequiredHook hook);
    void set_native_signal_observation_any_hook(
        NativeSignalObservationAnyHook hook);
    void set_stored_signal_change_hook(StoredSignalChangeHook hook);
    void set_driver_change_hook(DriverChangeHook hook);
    void set_event_trigger_hook(EventTriggerHook hook);
    void set_container_object_change_hook(ContainerObjectChangeHook hook);
#include "fsim/runtime/simir_scalar_interpreter.hpp"
    void set_execution_point_hook(ExecutionPointHook hook);
    void set_output_hook(OutputHook hook);
    void set_report_hook(ReportHook hook);
    void set_coverage_sample_hook(CoverageSampleHook hook);
    void set_coverage_query_hook(CoverageQueryHook hook);
    void set_system_command_hook(SystemCommandHook hook);
    void set_vcd_control_hook(VcdControlHook hook);
    void set_coverage_database_control_hook(
        CoverageDatabaseControlHook hook);
    void set_fork_spawn_filter(ForkSpawnFilter filter);
    void set_class_allocate_hook(ClassAllocateHook hook);
    void set_class_property_read_hook(ClassPropertyReadHook hook);
    void set_class_property_write_hook(ClassPropertyWriteHook hook);
    void set_class_method_call_hook(ClassMethodCallHook hook);
    void set_class_static_property_read_hook(
        ClassStaticPropertyReadHook hook);
    void set_class_static_property_write_hook(
        ClassStaticPropertyWriteHook hook);
    void set_class_static_method_call_hook(ClassStaticMethodCallHook hook);

private:
    [[nodiscard]] ProcessId
    add_process_impl(const Process& process, Process* owned_process);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime::simir
