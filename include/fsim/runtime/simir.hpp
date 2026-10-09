// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/runtime/constraint_solver.hpp"
#include "fsim/runtime/code_coverage.hpp"
#include "fsim/runtime/file_operations.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/runtime/simir_shared_vector.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/support/rare_vector.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <compare>
#include <filesystem>
#include <functional>
#include <limits>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
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

using fsim::support::RareVector;

class RegionKernelBackendProvider;
class InterpreterProgramAccess;

template <typename T>
auto archive_fields(const RareVector<T>& values)
{
    return std::tie(values.storage());
}

template <typename T>
auto archive_fields(RareVector<T>& values)
{
    return std::tie(values.mutable_storage());
}


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
    // Zero means the unit has no timescale and uses the project resolution.
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

struct StringReplaceByte {
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
    // %e/%f/%g digits after the point; the maximum value is automatic.
    std::uint32_t precision { std::numeric_limits<std::uint32_t>::max() };
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

/// Runtime base and fixed-width metadata shared by indexed part-select writes.
struct DynamicPartIndex {
    DynamicPartIndex() = default;
    DynamicPartIndex(
        const RegisterId dynamic_base,
        const std::int64_t dynamic_left,
        const std::int64_t dynamic_right,
        const std::uint32_t dynamic_base_offset,
        const std::uint32_t dynamic_width,
        const bool dynamic_increasing,
        const bool dynamic_source_descending)
        : left(dynamic_left)
        , right(dynamic_right)
        , base(dynamic_base)
        , base_offset(dynamic_base_offset)
        , width(dynamic_width)
        , increasing(dynamic_increasing)
        , source_descending(dynamic_source_descending)
    {
    }

    std::int64_t left { };
    std::int64_t right { };
    RegisterId base { };
    std::uint32_t base_offset { };
    std::uint32_t width { };
    bool increasing { };
    bool source_descending { };

    friend bool operator==(
        const DynamicPartIndex&, const DynamicPartIndex&) = default;
};

inline auto archive_fields(const DynamicPartIndex& selection)
{
    return std::tie(
        selection.base, selection.left, selection.right,
        selection.base_offset, selection.width,
        selection.increasing, selection.source_descending);
}

inline auto archive_fields(DynamicPartIndex& selection)
{
    return std::tie(
        selection.base, selection.left, selection.right,
        selection.base_offset, selection.width,
        selection.increasing, selection.source_descending);
}

/// Update one packed/scalar element of a container object without
/// materializing the complete object in a temporary container register.
struct WriteContainerObjectElement {
    ContainerObjectId object { };
    RegisterId index { };
    RegisterId source { };
    bool signed_index { true };
    bool linear_index { };
    /// Capture the index and value now, then publish the element write in the
    /// current time slot's nonblocking-update phase.
    bool nonblocking { };
    std::optional<SignalId> transaction_signal;
    /// Apply a packed part-write to the current element at update time. The
    /// base register is captured with the address and value for NBA writes.
    std::optional<DynamicPartIndex> dynamic_part;
};

struct ContainerStringRead {
    StringRegisterId destination { };
    ContainerRegisterId source { };
    RegisterId index { };
    bool signed_index { true };
    bool linear_index { };
    bool string_index { };
    std::vector<std::uint32_t> members;
};

/// Format a container value as an assignment pattern for `%p`
/// (IEEE 1800-2017 21.2.1.7).
struct FormatContainerPattern {
    StringRegisterId destination { };
    ContainerRegisterId source { };
};

struct ContainerStringWrite {
    ContainerRegisterId target { };
    RegisterId index { };
    StringRegisterId source { };
    bool signed_index { true };
    bool linear_index { };
    bool string_index { };
    // A string member path inside an unpacked structure element, as for
    // ContainerStringRead.
    std::vector<std::uint32_t> members;
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

/// Build a queue or dynamic array for an unpacked array concatenation
/// (IEEE 1800-2017 10.10): optionally clear the target, then append the
/// elements of `container`, one packed or real `value`, or one `text`.
struct AppendContainer {
    ContainerRegisterId target { };
    bool clear { };
    std::optional<ContainerRegisterId> container;
    std::optional<RegisterId> value;
    std::optional<StringRegisterId> text;
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
    /// VHDL indexes are checked constraints; SystemVerilog packed selects
    /// retain their X-on-read/no-op-on-write out-of-range behavior.
    bool strict { };

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

[[nodiscard]] PackedLogic4 binary_value(
    BinaryOperator operation,
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

enum class IntegerUnaryOperator : std::uint8_t {
    negate,
    absolute,
};

/// A checked operation on the profile-selected signed 32- or 64-bit VHDL
/// integer representation. Unknown operands and overflow are language errors.
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

/// Require a known value no wider than 64 bits to belong to an elaborated VHDL
/// scalar subtype before it is stored. Narrow nonnegative ranges represent
/// enumeration ordinals and are interpreted as unsigned.
struct IntegerCheck {
    RegisterId source { };
    std::int64_t lower { };
    std::int64_t upper { };
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

/// Scheduling provenance for deferred signal writes. Generic updates retain
/// the runtime's existing language-cycle behavior. SystemVerilog continuous
/// assignments mature in Active; nonblocking assignments mature in NBA.
enum class SignalUpdateDomain : std::uint8_t {
    generic,
    systemverilog_active,
    systemverilog_nba,
};

/// Queue a new value for the current timestamp's update phase.
struct WriteUpdate {
    SignalId signal { };
    RegisterId source { };
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
};

/// Queue a new value for a future timestamp's update phase.
struct WriteAfter {
    SignalId signal { };
    RegisterId source { };
    SimulationTick delay { };
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
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
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
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
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
};

/// Stage a contiguous packed range after a simulation-time delay.
struct WriteAfterSlice {
    SignalId signal { };
    RegisterId source { };
    std::uint32_t offset { };
    SimulationTick delay { };
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
};

struct WriteInertialSlice {
    SignalId signal { };
    RegisterId source { };
    std::uint32_t offset { };
    TransitionDelays delays;
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
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
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
};

struct WriteAfterDynamicSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicIndex selection;
    SimulationTick delay { };
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
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
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
};

struct WriteAfterDynamicPartSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicPartIndex selection;
    SimulationTick delay { };
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
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
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
};

struct WriteInertialDynamicPartSlice {
    SignalId signal { };
    RegisterId source { };
    DynamicPartIndex selection;
    TransitionDelays delays;
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
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

/// Queue a value after a delay computed at run time, as a continuous
/// assignment (inertial) or a nonblocking assignment (transport) with a
/// non-constant delay expression. `delay` carries the dynamic-delay metadata
/// of a WaitFor; an unknown integral delay counts as zero (IEEE 1800-2017
/// 9.4.1).
struct WriteDelayed {
    SignalId signal { };
    RegisterId source { };
    WaitFor delay;
    // The bit offset of a constant select target; whole signal when empty.
    std::optional<std::uint32_t> offset;
    bool inertial { };
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
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

    RareVector<SignalId> signals;
    RareVector<EdgeKind> edges;
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

#include "fsim/runtime/simir_operations_extended.hpp"

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

/// Bind one declared-order element of a logical fixed net array to its own
/// independently resolved signal.
struct ContainerElementSignalAlias {
    ContainerObjectId object { };
    std::uint32_t ordinal { };
    SignalId signal { };
    bool readable { };
    bool writable { };

    friend bool operator==(
        const ContainerElementSignalAlias&,
        const ContainerElementSignalAlias&) = default;
};

/// Preserve the public aggregate SignalId for a fixed array whose storage is
/// represented by independently resolved element signals. Lowered HDL accesses
/// use the element aliases; external readers may project the aggregate handle.
/// A non-writable alias is an observation-only checkpoint: mutations through
/// that SignalId are rejected until an aggregate update transaction is bound.
struct ContainerAggregateSignalAlias {
    ContainerObjectId object { };
    SignalId signal { };
    bool readable { };
    bool writable { };

    friend bool operator==(
        const ContainerAggregateSignalAlias&,
        const ContainerAggregateSignalAlias&) = default;
};

struct Sensitivity {
    SignalId signal { };
    EdgeKind edge = EdgeKind::any;
    /// LSB-normalized interval. Zero width means the complete signal.
    /// Ranges currently apply only to any-change sensitivity.
    std::uint32_t offset { };
    std::uint32_t width { };

    bool operator==(const Sensitivity&) const = default;
};

/// Merge overlapping or adjacent intervals before assigning trigger-mask bits.
/// Whole-signal sensitivity dominates ranges for the same signal and edge.
void normalize_sensitivities(std::vector<Sensitivity>&);

/// Compare the original value domains without allocating extracted values.
/// Invalid or unsupported ranges conservatively count as changed.
[[nodiscard]] bool sensitivity_range_changed(
    const PackedLogic4& previous, const PackedLogic4& current,
    std::uint32_t offset, std::uint32_t width) noexcept;

#include "fsim/runtime/simir_debug.hpp"
#include "fsim/runtime/simir_specify.hpp"
enum class ProcessSchedulingDomain : std::uint8_t {
    generic,
    systemverilog,
};

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
    CopyOnWriteVector<ContainerType> container_register_types;
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
    CopyOnWriteVector<StaticTriggerRegion> static_trigger_regions;
    OperationList operations;
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
    CopyOnWriteVector<ValueKind> register_value_kinds;
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
    ExpressionProfileList expression_profiles;
    ProcessSchedulingDomain scheduling_domain {
        ProcessSchedulingDomain::generic
    };
};

/// Replace candidate's expanded operation stream with representative's
/// immutable program when the two differ only by hierarchy-local signal,
/// debugger, assertion, or container-object identities.
[[nodiscard]] bool share_process_operations(
    const Process& representative,
    Process& candidate,
    std::span<const Signal> signals,
    OperationList::Storage* recycled_operations = nullptr);
[[nodiscard]] bool process_operations_shareable(const Process& process);

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
    /// Native executor verified the bound WaitSensitivity boundary and its
    /// nonempty static sensitivity before returning this result.
    validated_wait_sensitivity,
};

[[nodiscard]] constexpr bool is_static_wait_suspension(
    const ExternalSuspendKind kind)
{
    return kind == ExternalSuspendKind::wait_sensitivity
        || kind == ExternalSuspendKind::validated_wait_sensitivity;
}

[[nodiscard]] constexpr std::size_t external_suspension_profile_index(
    const ExternalSuspendKind kind)
{
    return static_cast<std::size_t>(
        kind == ExternalSuspendKind::validated_wait_sensitivity
            ? ExternalSuspendKind::wait_sensitivity
            : kind);
}

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

/// One-call view of state shared by every context in a cohort assembled by
/// the interpreter. The view is valid only during the resume call; alternate
/// executors may ignore it and use the ordinary cohort entry contract.
struct ProcessCohortNativeContext {
    const void* owner { };
    std::span<const std::uint64_t> signal_aval;
    std::span<const std::uint64_t> signal_bval;
    std::uint64_t signal_writer_revision { };
    bool supports_direct_word_updates { };
    bool execution_points_enabled { };
    std::span<const std::uint64_t> wide_signal_aval;
    std::span<const std::uint64_t> wide_signal_bval;
    std::span<const std::uint32_t> wide_signal_offsets;
    std::span<const std::uint64_t> signal_logic9_plane0;
    std::span<const std::uint64_t> signal_logic9_plane1;
    std::span<const std::uint64_t> signal_logic9_plane2;
    std::span<const std::uint64_t> signal_logic9_plane3;
    std::span<const std::uint64_t> wide_signal_logic9_plane2;
    std::span<const std::uint64_t> wide_signal_logic9_plane3;
};

/// Post-elaboration graph certificate for an existing atomic static cohort.
/// The runtime retains original process and driver identities on every path.
struct FusedStaticCohortCandidate {
    std::size_t cohort_id { };
    std::vector<ProcessId> members;
    std::vector<SignalId> outputs;
    std::vector<SignalId> private_outputs;
    std::vector<SignalId> boundary_outputs;
    bool projected { };
    /// The current executor uses a general masked kernel with every member
    /// selected. The application may set this after validating its mapping.
    bool masked_all_active { };
    /// Graph-time proof that the process bodies can use masked compilation.
    bool masked_all_active_eligible { };
    std::vector<std::uint64_t> masked_all_active_words;
};

struct FusedStaticCounters {
    std::uint64_t candidates { };
    std::uint64_t invocations { };
    std::uint64_t represented_members { };
    std::uint64_t owner_stage_calls_avoided { };
    std::uint64_t aggregate_signals_staged { };
    std::uint64_t masked_all_active_invocations { };
    std::uint64_t generic_update_commit_tickets { };
    std::uint64_t fallbacks { };
    std::uint64_t fork_events { };
    std::uint64_t fork_plans_invalidated { };
    std::uint64_t fork_plans_surviving_after_last { };
    std::uint64_t observation_invalidations { };
};

/// Exact VHDL projected write retained with its original elaborated signal.
/// The graph plan supplies the original process owner and projected timing.
struct FusedStaticProjectedWrite {
    SignalId signal { };
    PackedLogic4 value;
};

struct FusedStaticCohortResume {
    std::span<const ProcessUpdateSlotView> aggregate_slots;
    std::span<const FusedStaticProjectedWrite> projected_writes;
};

/// Return nullopt only before changing signal slots or process state. A
/// successful result borrows stable slot views until the runtime stages them.
class FusedStaticCohortExecutor {
public:
    virtual ~FusedStaticCohortExecutor() = default;
    [[nodiscard]] virtual std::optional<FusedStaticCohortResume>
    resume(const ProcessCohortNativeContext& context) = 0;
    /// Optional general masked entry. Its word span is supplied only after the
    /// runtime has found the complete static cohort ready. The default keeps
    /// the existing checked process path available.
    [[nodiscard]] virtual std::optional<FusedStaticCohortResume>
    resume_masked_all_active(const ProcessCohortNativeContext&,
        std::span<const std::uint64_t>)
    {
        return std::nullopt;
    }
};

/// Retained source-compatibility metadata for the retired masked-region route.
/// Current interpreters do not publish masked-region candidates.
struct FusedMaskedRegionCandidate {
    std::size_t region_id { };
    std::vector<ProcessId> members;
    std::vector<SignalId> outputs;
    // A complete, distinct Logic4 output with one original writer may share
    // an owned aggregate's masked native call. It still uses the ordinary
    // update/commit path under that writer's ProcessId.
    std::optional<SignalId> normal_single_writer_output;
    // Downstream pure readers folded into the same-snapshot masked region.
    std::vector<ProcessId> terminal_members;
    std::vector<SignalId> private_outputs;
    std::vector<SignalId> boundary_outputs;
    bool projected { };
};

struct FusedMaskedRegionCounters {
    std::uint64_t candidates { };
    std::uint64_t terminal_candidates { };
    std::uint64_t normalized_terminal_candidates { };
    std::uint64_t terminal_regions_bound { };
    std::uint64_t terminal_members_bound { };
    std::uint64_t terminal_activations { };
    std::uint64_t terminal_joint_activations { };
    std::uint64_t private_candidates { };
    std::uint64_t private_local_commits { };
    std::uint64_t private_fanout_entries_avoided { };
    std::uint64_t private_masked_notifications { };
    std::uint64_t private_owned_direct_commits { };
    std::uint64_t virtual_tasks { };
    std::uint64_t frontier_calls { };
    std::uint64_t global_frontier_callbacks { };
    std::uint64_t global_frontier_queue_entries { };
    std::uint64_t masked_calls { };
    std::uint64_t represented_members { };
    std::uint64_t owner_stage_calls_avoided { };
    std::uint64_t aggregate_signals_staged { };
    std::uint64_t prepared_fallback_tasks { };
    std::uint64_t ordinary_fallback_tasks { };
    std::uint64_t boundary_publications { };
    std::uint64_t private_publications_elided { };
    std::uint64_t demotions { };
};

/// A disengaged result must precede every mutation. On success the output
/// views remain valid until the interpreter validates and stages all slots.
class FusedMaskedRegionExecutor {
public:
    virtual ~FusedMaskedRegionExecutor() = default;
    [[nodiscard]] virtual std::optional<FusedStaticCohortResume>
    resume(const ProcessCohortNativeContext& context,
        std::span<const std::uint64_t> activation_words) = 0;
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
        std::string execution_compatibility_profile = { });
};

using ProcessSignalRemap
    = std::vector<std::pair<SignalId, SignalId>>;

/// Exact registered-program contract for an alternate executor that claims
/// its signal accesses match a compiled SimIR body. It covers signal effects
/// from every executor entrypoint, including resume, redirect, fork cloning,
/// and cohort preparation. It does not certify execution semantics; it only
/// lets the region graph reuse the body's enumerated signal accesses. The
/// executor remains responsible for honoring all other scheduler and
/// publication contracts.
class ProcessExecutorProgramBinding {
public:
    ProcessExecutorProgramBinding() = default;
    ProcessExecutorProgramBinding(const Process& registered_program,
        const Process& generated_program,
        ProcessId executor_generated_process,
        std::shared_ptr<const ProcessSignalRemap> signal_remap = { });

    [[nodiscard]] bool matches_registered_program(
        ProcessId process, const Process& program) const noexcept;
    /// A fork clone may keep the same executor binding while the interpreter
    /// assigns its child a new ProcessId. Compare the child's operation
    /// identity separately; the caller must also compare parent and child
    /// bindings so the generated body and signal remap remain unchanged.
    [[nodiscard]] bool matches_forked_program(
        ProcessId process, const Process& program) const noexcept;
    [[nodiscard]] bool same_execution_binding(
        const ProcessExecutorProgramBinding& other) const noexcept;
    [[nodiscard]] bool valid() const noexcept;

private:
    friend class ProcessProgramView;

    ProcessId registered_process_ { };
    const void* registered_body_ { };
    std::uint64_t registered_revision_ { };
    ProcessId generated_process_ { };
    ProcessId generated_program_id_ { };
    const void* generated_body_ { };
    std::uint64_t generated_revision_ { };
    std::shared_ptr<const ProcessSignalRemap> signal_remap_;
};

/// Caller-owned trust declaration for a deferred executor factory. The access
/// binding describes the exact registered/generated programs and remap. The
/// callback flag promises that ready/take neither observes nor mutates signal
/// state; the kernel flag separately promises that the installed executor is
/// semantically replaceable by its certified region-activation kernel.
struct DeferredProcessExecutorContract {
    std::optional<ProcessExecutorProgramBinding> expected_access;
    bool callbacks_observation_safe { };
    bool expected_region_kernel_equivalent { };
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

    /// Execute a scheduler-contiguous SV Active prefix. Every member retains
    /// its original ordered publications; no aggregate update or hidden-state
    /// shortcut is permitted. The executor must manage each accepted member's
    /// queued/waiting/status state, and report failures without replaying it.
    /// A throw is allowed only before accepting any entry.
    [[nodiscard]] virtual std::size_t resume_ordered_cohort(
        std::span<ProcessCohortResumeEntry>)
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

    virtual void write_container_register_storage(
        const ContainerRegisterId id,
        std::shared_ptr<ContainerValue> value)
    {
        if (!value) {
            throw std::invalid_argument {
                "alternate process executor received null container storage"
            };
        }
        write_container_register(id, *value);
    }

    /// Non-null only when this executor was built for the exact registered
    /// SimIR body/remap it executes. Arbitrary executors stay opaque by
    /// default, regardless of their cohort scheduling domain.
    [[nodiscard]] virtual const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept
    {
        return nullptr;
    }

    /// Opt in only when replacing this executor's operation execution with
    /// the interpreter's certified region-activation kernel preserves its
    /// observable execution semantics, including register/frame results and
    /// any private state observable by a later executor entrypoint. Access
    /// binding alone is insufficient.
    [[nodiscard]] virtual bool region_kernel_equivalent() const noexcept
    {
        return false;
    }

    /// Opt in only when this executor has no process-register values that a
    /// region activation must copy back at a static wait boundary. This is a
    /// separate promise from region_kernel_equivalent(): the latter permits
    /// region replacement only when executor-visible state remains equivalent,
    /// while this narrower promise allows the runtime to omit native
    /// completion staging after validating the member's bindings and boundary.
    [[nodiscard]] virtual bool
    region_kernel_completion_has_no_persistent_registers() const noexcept
    {
        return false;
    }

    /// A statically validated activation-kernel register mapping. Entries are
    /// in source-register order and activation IDs are strictly increasing.
    /// `defined` is true only when the source body assigns the register on
    /// every admitted activation; false entries preserve prior state.
    struct RegionRegisterBinding {
        RegisterId source_register { };
        RegisterId activation_register { };
        bool defined { };
        std::uint32_t width { };
        ValueKind value_kind { ValueKind::logic4 };

        bool operator==(const RegionRegisterBinding&) const = default;
    };

    /// Prepared copyback owns no semantic state until commit. Destroying it
    /// cancels it. A non-null stable identity names the executor-owned storage
    /// that commit mutates; an activation must decline duplicate identities.
    class PreparedRegionCompletion {
    public:
        virtual ~PreparedRegionCompletion() = default;

        [[nodiscard]] virtual const void* storage_identity() const noexcept = 0;
        virtual void commit() noexcept = 0;
    };

    /// Prepare a no-throw synchronization of the executed activation's
    /// defined packed registers into this executor's persistent frame. The
    /// executor must validate process identity, exact frame boundary, ABI and
    /// plane extents, register IDs, widths, and value kinds before returning a
    /// plan. Returning null declines the optimized activation. The source and
    /// activation spans must remain alive and unchanged until commit or plan
    /// destruction. Preparation may invalidate performance-only bindings but
    /// must not change register values or initialized state.
    [[nodiscard]] virtual std::unique_ptr<PreparedRegionCompletion>
    prepare_region_completion(
        ProcessId,
        InstructionIndex,
        InstructionIndex,
        std::span<const RegionRegisterBinding>,
        std::span<const PackedLogic4>)
    {
        return { };
    }

    /// Allocation-free split-phase completion used only by a native region
    /// route. The caller preflights before entering the backend, stages the
    /// returned activation values only after successful execution, and either
    /// commits or cancels synchronously. Other executor implementations keep
    /// the conservative default and use the public prepared-completion API.
    [[nodiscard]] virtual bool prepare_region_completion_native(
        ProcessId, InstructionIndex, InstructionIndex,
        std::span<const RegionRegisterBinding>, std::size_t,
        const void**) noexcept
    {
        return false;
    }
    [[nodiscard]] virtual bool stage_region_completion_native(
        std::span<const PackedLogic4>) noexcept
    {
        return false;
    }
    virtual void commit_region_completion_native() noexcept { }
    virtual void cancel_region_completion_native() noexcept { }
};

/// Optional read-only certificate for an executor already parked at the exact
/// static-wait completion boundary expected by a native region. The default
/// ProcessExecutor contract remains opaque; unsupported executors do not opt in.
class RegionKernelParkedExecutor {
public:
    virtual ~RegionKernelParkedExecutor() = default;

    [[nodiscard]] virtual bool region_kernel_completion_is_parked_native(
        ProcessId, InstructionIndex, InstructionIndex,
        std::span<const ProcessExecutor::RegionRegisterBinding>,
        std::size_t activation_register_count) const noexcept = 0;
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
struct NativeRegionAllocationTestAccess;
struct SystemCBridgeTestAccess;

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
    using ContainerElementChangeHook = std::function<void(
        ContainerObjectId, std::size_t, const PackedLogic4&, SimulationTick)>;
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
        std::span<const frontend::SystemVerilogScalarKind>,
        CoverageSampleTrigger)>;
    using CoverageQueryHook = std::function<PackedLogic4(CoverageQueryKind)>;
    using VhdlPslApiHook = std::function<bool(
        VhdlPslApiKind, std::optional<bool>)>;
    using CoverageControlHook
        = std::function<std::int32_t(const CoverageControlEvent&)>;
    using CoverageAccessHook
        = std::function<std::int32_t(const CoverageAccessEvent&)>;
    using CodeCoverageOverflowHook
        = std::function<void(::fsim::runtime::CodeCoverageCounterId)>;
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
    /// Execute one imported SystemVerilog DPI function. The identity is the
    /// declaration's C linkage name; mutable actual vectors carry output,
    /// inout, and ref copy-out values using the same representation as class
    /// call boundaries.
    using DpiFunctionCallHook = std::function<PackedLogic4(
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
    void add_container_element_signal_alias(
        ContainerElementSignalAlias alias);
    void add_container_aggregate_signal_alias(
        ContainerAggregateSignalAlias alias);
    /// Reserve contiguous hot records before the first process registration.
    /// Additional processes use stable overflow storage; existing records are
    /// never relocated. This is an optional setup hint, not a process limit.
    void reserve_process_capacity(std::size_t capacity);
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

    /// Install the application-owned optional region compiler before start.
    /// The provider is called only during the initial snapshot build; later
    /// quiet-point snapshots can reuse only exact matches from the runtime's
    /// persistent backend pool.
    void set_region_kernel_backend_provider(
        std::shared_ptr<RegionKernelBackendProvider> provider);

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
    /// The explicit contract enables only the capabilities the caller
    /// promises. The default overload keeps arbitrary factories conservative.
    void set_deferred_process_executor(
        ProcessId process,
        std::function<bool()> ready,
        std::function<std::unique_ptr<ProcessExecutor>()> take,
        DeferredProcessExecutorContract contract);

    /// Discard an installed or deferred alternate executor before start so the
    /// reference interpreter resumes ownership of the process. Returns false
    /// after start, when executor replacement can no longer be made safely.
    [[nodiscard]] bool clear_process_executor(ProcessId process);

    /// Install every ready deferred executor before simulation starts. This
    /// preserves eager debugger and fork semantics for callers that explicitly
    /// wait for native compilation while allowing other callers to overlap
    /// materialization with scheduler execution.
    void materialize_ready_process_executors();

    /// Available after start() has certified the complete elaborated graph.
    [[nodiscard]] std::vector<FusedStaticCohortCandidate>
    fused_static_cohort_candidates() const;
    [[nodiscard]] FusedStaticCounters fused_static_counters() const noexcept;
    void set_fused_static_counters_enabled(bool enabled);
    /// Bind only after start() and before the first run(). Unknown or stale
    /// cohort IDs reject; declined activations use their original process path.
    void install_fused_static_cohort(
        std::size_t cohort_id,
        std::unique_ptr<FusedStaticCohortExecutor> executor,
        bool use_masked_all_active = false);
    /// Compatibility query for the retired route; always returns an empty list.
    [[nodiscard]] std::vector<FusedMaskedRegionCandidate>
    fused_masked_region_candidates() const;
    /// Compatibility counters for the retired route; all fields remain zero.
    [[nodiscard]] FusedMaskedRegionCounters
    fused_masked_region_counters() const noexcept;
    /// Retained configuration entry point. It preserves the binding-window
    /// error behavior but has no masked route to configure.
    void set_fused_masked_region_counters_enabled(bool enabled);
    /// Retained source-compatible entry point. Since no masked region IDs are
    /// published, every call throws `invalid fused masked region binding`.
    void install_fused_masked_region(
        std::size_t region_id,
        std::vector<std::vector<Process::DriverRegion>> mandatory_writes,
        std::unique_ptr<FusedMaskedRegionExecutor> executor);

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

    /// After start(), materialize a signal's current/previous/original-driver
    /// and pending state before installing a late observer. Only dependent
    /// graph kernels are demoted. This does not publish pending transactions
    /// early or advance simulation. May throw on allocation failure; demoted
    /// certificates remain invalid and the operation may be retried. Before
    /// start, initial state is already public; the caller must expose its new
    /// registration to the startup observation inventory before calling start.
    void prepare_signal_observation(SignalId signal);

    /// Return a live reference; its backing remains on public storage because
    /// callers may retain it beyond this call.
    [[nodiscard]] const PackedLogic4& signal_value(SignalId signal) const;
    /// Return a value snapshot without retaining an observation reference.
    [[nodiscard]] PackedLogic4 signal_value_snapshot(SignalId signal) const;
    /// Return the underlying effective driven value without applying a force.
    /// This keeps public driver state distinct from the visible signal value.
    /// The returned reference pins its backing on public storage.
    [[nodiscard]] const PackedLogic4& stored_signal_value(
        SignalId signal) const;
    /// Return an underlying-value snapshot without retaining a reference.
    [[nodiscard]] PackedLogic4 stored_signal_value_snapshot(
        SignalId signal) const;
    [[nodiscard]] const std::string&
    string_object_value(StringObjectId object) const;
    void deposit_string_object(
        StringObjectId object, std::string_view value);
    /// Return the current stored container value by reference. Its backing and
    /// element storage remain stable, and aliased signal values refresh before
    /// current-change observers run. Use the snapshot getter when a detached
    /// value copy is preferable.
    [[nodiscard]] const ContainerValue&
    container_object_value(ContainerObjectId object) const;
    /// Return a container snapshot without pinning aliased signals.
    [[nodiscard]] ContainerValue container_object_value_snapshot(
        ContainerObjectId object) const;
    void deposit_container_object(
        ContainerObjectId object, ContainerValue value);
    void deposit_container_object_element(
        ContainerObjectId object, std::size_t ordinal,
        PackedLogic4 value);
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
    /// Compatibility accessor for the retired masked-member specialization.
    /// It returns the original registered process program.
    [[nodiscard]] const Process& fused_masked_member_program(
        ProcessId process) const;
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
    void set_container_element_change_hook(ContainerElementChangeHook hook);
#include "fsim/runtime/simir_scalar_interpreter.hpp"
    void set_execution_point_hook(ExecutionPointHook hook);
    void set_output_hook(OutputHook hook);
    /// Installs a sink for already-formatted text. A trusted sink may consume
    /// the supplied text and metadata without observing signal state. Before
    /// inspecting or mutating interpreter state, it must call
    /// prepare_output_callback_observation(). Unlike set_output_hook(), this
    /// skips the implicit signal-observation barrier before each callback.
    void set_trusted_text_output_hook(OutputHook hook);
    /// Prepare the complete signal-observation barrier before code that may
    /// inspect interpreter state, including code reached from a trusted text
    /// sink.
    void prepare_output_callback_observation();
    void set_report_hook(ReportHook hook);
    void set_coverage_sample_hook(CoverageSampleHook hook);
    void set_coverage_query_hook(CoverageQueryHook hook);
    void set_vhdl_psl_api_hook(VhdlPslApiHook hook);
    void set_coverage_control_hook(CoverageControlHook hook);
    void set_coverage_access_hook(CoverageAccessHook hook);
    /// Install or restore the dense simulation-owned code counters before
    /// start. An empty vector deliberately configures an empty table.
    void set_code_coverage_counters(std::vector<std::uint64_t> counters);
    [[nodiscard]] std::span<const std::uint64_t>
    code_coverage_counters() const noexcept;
    [[nodiscard]] bool code_coverage_counter_overflowed(
        ::fsim::runtime::CodeCoverageCounterId counter) const noexcept;
    [[nodiscard]] std::size_t code_coverage_overflow_count() const noexcept;
    [[nodiscard]] bool code_coverage_counters_configured() const noexcept;
    [[nodiscard]] bool code_coverage_collection_enabled() const noexcept;
    void set_code_coverage_collection_enabled(bool enabled) noexcept;
    [[nodiscard]] bool set_code_coverage_collection_enabled(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters,
        bool enabled) noexcept;
    void reset_code_coverage_counters() noexcept;
    [[nodiscard]] bool reset_code_coverage_counters(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
        noexcept;
    [[nodiscard]] std::optional<std::size_t>
    code_coverage_overflow_count(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
        const noexcept;
    void set_code_coverage_overflow_hook(CodeCoverageOverflowHook hook);
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
    void set_dpi_function_call_hook(DpiFunctionCallHook hook);

private:
    [[nodiscard]] ProcessId
    add_process_impl(const Process& process, Process* owned_process);
    friend struct OwnedDriverDemotionTestAccess;
    friend struct NativeRegionAllocationTestAccess;
    friend struct SystemCBridgeTestAccess;
    friend class InterpreterProgramAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime::simir
