// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/runtime/constraint_solver.hpp"
#include "fsim/runtime/code_coverage.hpp"
#include "fsim/runtime/file_operations.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/runtime/simir_hooks.hpp"
#include "fsim/runtime/simir_ids.hpp"
#include "fsim/runtime/simir_shared_vector.hpp"
#include "fsim/runtime/sva_storage.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/support/rare_vector.hpp"
#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
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

namespace fsim::runtime {
class RunStateImage;
}

namespace fsim::runtime::simir {
/// What a save or restore reports: each reason as one FSIM-SAVE-nnnn line.
struct RunStateReport {
    std::vector<std::string> messages;
    /// Every reason is work of the current time slot that drains before the
    /// slot ends, so a save request waits for the next quiet point.
    bool deferrable { };
};

enum class OutputFormat : std::uint8_t;

using fsim::support::RareVector;

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

inline constexpr std::size_t maximum_string_bytes = 1U << 20U;
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
    /// Capture the value now and publish it in the current time slot's
    /// nonblocking-update phase.
    bool nonblocking { };
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
    format_time,
    // VHDL TIME value (register `first`, in simulation ticks) written in the
    // unit whose index in fs, ps, ns, us, ms, sec, min, hr is `precision`
    // (the primary unit fs when out of range), followed by the unit name
    // (IEEE 1076-2008 5.2.4.2, 16.4 WRITE).
    format_vhdl_time,
    // T'VALUE of an integer type: the integer literal in the source string
    // (IEEE 1076-2008 16.2.2), as a 64-bit value in `destination`.
    vhdl_integer_value,
    // T'VALUE of an enumeration type: the position of the literal named by
    // the source string among the newline-separated `argument` literals.
    vhdl_enumeration_value,
    // VHDL TextIO WRITE field widths evaluated at run time. The string form
    // stores the field register in `first`; the packed form uses `second`.
    format_packed_dynamic_field,
    format_string_dynamic_field,
    // T'VALUE of a physical type: the physical literal in the source string
    // as a count of primary units, using the newline-separated
    // `name=scale` units of `argument` (IEEE 1076-2008 16.2.2, 5.2.4).
    vhdl_physical_value,
    // T'VALUE of a composite type: the representation of element `first`
    // of the `second` elements in the source string, a parenthesized list
    // or, for an array of a character type, a character string whose
    // elements are returned as character literals (IEEE 1076-2019 5.7,
    // 16.2.2).
    vhdl_composite_element,
    vhdl_character_element,
    // `first` (a count) copies of the character whose position is `second`:
    // the value of an allocator `new string(1 to n)` (IEEE 1076-2008 9.3.7).
    vhdl_string_fill,
    // TO_HSTRING and TO_OSTRING of STD_ULOGIC_VECTOR (IEEE 1076-2019 16.7):
    // the source string holds the element characters of the vector, leftmost
    // first, and the result string its hexadecimal or octal digits.
    vhdl_logic9_hstring,
    vhdl_logic9_ostring,
    // The same for NUMERIC_STD and NUMERIC_BIT SIGNED values, which are
    // extended on the left with their sign element instead (numeric_std
    // TO_HSTRING and TO_OSTRING of UNRESOLVED_SIGNED).
    vhdl_logic9_hstring_signed,
    vhdl_logic9_ostring_signed
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
    // For format_time with a value: nonzero is the time unit of the
    // integral value in `first` (IEEE 1800-2017 21.2.1.3); zero prints the
    // current time.
    std::uint64_t time_unit_femtoseconds { };
    bool time_signed { };
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
    /// Capture the value now and publish it in the current time slot's
    /// nonblocking-update phase.
    bool nonblocking { };
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
    /// Set the associative array's default for missing keys instead of an
    /// element; the index is ignored (IEEE 1800-2017 7.9.11).
    bool associative_default { };
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
/// A container level a write descends through before its element: the
/// nested element `index` selects, then `members` within it (an array of
/// arrays `m[i][j]`, or `s.q[i]`, IEEE 1800-2017 7.4.5).
struct ContainerPathLevel {
    RegisterId index { };
    bool signed_index { true };
    bool linear_index { };
    std::vector<std::uint32_t> members;

    friend bool operator==(
        const ContainerPathLevel&, const ContainerPathLevel&) = default;
};

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
    /// A member path inside an unpacked aggregate element: the element's
    /// member is updated when the write is published.
    std::vector<std::uint32_t> members;
    /// The container levels between the object and the element's
    /// container; empty when the object holds the element directly. Their
    /// indices are captured with the element's.
    std::vector<ContainerPathLevel> levels;
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
    /// As for ContainerWrite.
    bool associative_default { };
};

/// Write one string element of a string container object: the element
/// `index` selects (a string key when `string_index`) takes `source`. A
/// nonblocking write captures the index and the value now and publishes the
/// element in the current time slot's nonblocking-update phase, so writes to
/// different elements of one container do not overwrite each other (IEEE
/// 1800-2023 7.4, 10.4.2).
/// A container level a string element write descends through before the leaf
/// container: the nested container `index` selects (a string key when
/// `string_index`), IEEE 1800-2023 7.4.5.
struct ContainerStringPathLevel {
    RegisterId index { };
    bool signed_index { true };
    bool string_index { };
    bool linear_index { };
    /// A member path followed within the selected element (an unpacked
    /// structure member, IEEE 1800-2023 7.2).
    std::vector<std::uint32_t> members;

    friend bool operator==(
        const ContainerStringPathLevel&,
        const ContainerStringPathLevel&) = default;
};

/// What a WriteContainerObjectString writes: string text, a packed or scalar
/// value, or a container or unpacked aggregate value.
enum class ContainerElementSourceKind : std::uint8_t {
    text,
    packed,
    container,
};

/// A write of one element of a container object whose value and indices are
/// captured when the operation executes (a nonblocking assignment, IEEE
/// 1800-2023 10.4.2): string text from `source`, or per `source_kind` a
/// packed value from `packed_source` (a numeric element of a string-keyed
/// associative array) or a container value from `container_source` (an
/// unpacked aggregate or nested container element, 7.2, 7.4.5).
struct WriteContainerObjectString {
    ContainerObjectId object { };
    RegisterId index { };
    StringRegisterId source { };
    bool signed_index { true };
    bool linear_index { };
    bool string_index { };
    bool nonblocking { };
    /// The containers between the object and the leaf container, outermost
    /// first; empty when the object holds the string elements directly.
    std::vector<ContainerStringPathLevel> levels;
    ContainerElementSourceKind source_kind { ContainerElementSourceKind::text };
    RegisterId packed_source { };
    ContainerRegisterId container_source { };
    /// A member path followed within the selected element before a
    /// container-valued write (`s[i].items <= q`, IEEE 1800-2023 7.2).
    std::vector<std::uint32_t> members;
};

/// Read one element whose value is a container or an unpacked aggregate,
/// then follow `members` through nested aggregate values. A source that is
/// itself an aggregate value has no elements; the index is ignored and the
/// path starts at the source.
struct ContainerElementRead {
    ContainerRegisterId destination { };
    ContainerRegisterId source { };
    RegisterId index { };
    bool signed_index { true };
    std::vector<std::uint32_t> members;
    // `index` is the element's position in a multidimensional fixed array,
    // leftmost element first, as for ContainerRead.
    bool linear_index { };
    /// `index` names a string register: a string-keyed associative array
    /// (IEEE 1800-2017 7.8.1).
    bool string_index { };
};

/// Replace the value selected as for ContainerElementRead.
struct ContainerElementWrite {
    ContainerRegisterId target { };
    RegisterId index { };
    ContainerRegisterId source { };
    bool signed_index { true };
    std::vector<std::uint32_t> members;
    // `index` is the element's position in a multidimensional fixed array,
    // leftmost element first, as for ContainerWrite.
    bool linear_index { };
    /// As for ContainerElementRead.
    bool string_index { };
};

/// Bit-stream packing and unpacking through a queue of single-bit elements
/// (IEEE 1800-2017 6.24.3, 11.4.14): clear the queue; append a packed value
/// or every bit of a container, most significant bit and leftmost element
/// first; reverse it in `slice`-bit slices taken from its right end; or
/// extract it into a packed value (left-justified, zero-filled) or into a
/// container's elements (resizing a dynamic array or queue). The take
/// forms unpack into one of several stream targets in turn: they remove the
/// bits they use from the front, and a fixed-size target needs all of them.
enum class ContainerBitstreamOperation : std::uint8_t {
    clear,
    append_value,
    append_container,
    reverse,
    to_value,
    to_container,
    take_value,
    take_container,
};
struct ContainerBitstream {
    ContainerBitstreamOperation operation {
        ContainerBitstreamOperation::clear
    };
    ContainerRegisterId bits { };
    RegisterId value { };
    ContainerRegisterId container { };
    // The slice size of `reverse`, the value width of to_value and
    // take_value, or the bits take_container leaves for later targets.
    std::uint32_t slice { };
};

/// A VHDL array whose index ranges are known only at run time (IEEE
/// 1076-2008 5.3.2.2) lives in a container register: one packed payload in
/// the VHDL packed layout and one VhdlShapeRange per dimension.
enum class VhdlShapedOperation : std::uint8_t {
    // target := a new value of `type`; `operands` hold each dimension's
    // left and right bound, `directions` whether it is descending, and
    // every element is `value`.
    create,
    // value := `attribute` of dimension `dimension` of source (16.2.3).
    query,
    // value := source(operands...), each index checked (8.4). With
    // `inner`, the trailing operands index the element's own statically
    // shaped dimensions and the result is a `width`-bit leaf. Fewer
    // operands than dimensions select a subarray (the element of an array
    // of arrays): into `target`, keeping the remaining ranges, when target
    // is not source; otherwise into value, which must be `width` bits.
    element_read,
    // target(operands...) := value, addressed as for element_read.
    element_write,
    // value := the payload of source, which must be `width` bits. A record
    // source (an aggregate value of packed and shaped elements) yields its
    // elements' payloads in order, the first most significant.
    to_packed,
    // target keeps its bounds; its payload := value, of equal length
    // (10.6.2.1). A record target takes its elements' payloads from value
    // in that order.
    from_packed,
    // target keeps its bounds; its payload := source's, every dimension of
    // equal length (10.6.2.1, 14.7.3.4).
    assign,
    // value := source(operands[0] to/downto operands[1]) of a one-dimensional
    // value, `directions[0]` set for downto; the slice must be `width` bits
    // (8.5).
    slice_read,
    // target(operands[0] to/downto operands[1]) := value, of equal length.
    slice_write,
    // value := whether source and target have equal lengths in every
    // dimension and equal elements; bounds do not matter (9.2.3).
    equal,
    // value := whether source's payload equals the packed operands[0].
    equal_packed,
    // target := target & source of a one-dimensional value (9.2.5): the
    // left bound and direction are target's unless it is null.
    append,
    // target := target & the packed operands[0], a whole number of
    // elements.
    append_packed,
    // target := source(operands[0] to/downto operands[1]) of a
    // one-dimensional value, `directions[0]` set for downto, as a value
    // with the slice's own index range (8.5).
    slice,
};

enum class VhdlShapedAttribute : std::uint8_t {
    left,
    right,
    low,
    high,
    length,
    ascending,
};

struct VhdlShaped {
    VhdlShapedOperation operation { VhdlShapedOperation::create };
    ContainerRegisterId target { };
    ContainerRegisterId source { };
    RegisterId value { };
    std::vector<RegisterId> operands;
    std::vector<std::uint8_t> directions;
    ContainerType type;
    std::uint32_t dimension { };
    VhdlShapedAttribute attribute { VhdlShapedAttribute::length };
    std::uint32_t width { };
    std::vector<VhdlShapeRange> inner;
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
struct MemoryLoadResult {
    /// No-address files whose data count differs from the selected range
    /// complete with a warning; the supplied data still load successfully.
    bool word_count_mismatch { };
};

[[nodiscard]] MemoryLoadResult load_memory_text_with_status(
    ContainerValue& target,
    std::string_view text,
    bool hexadecimal,
    std::optional<std::int32_t> start = std::nullopt,
    std::optional<std::int32_t> finish = std::nullopt);

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

/// `value` shifted by `amount_value`; an unknown amount gives all X.
[[nodiscard]] PackedLogic4 shift_value(
    ShiftOperator operation,
    const PackedLogic4& value,
    const PackedLogic4& amount_value,
    bool signed_amount);

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
    /// A delay computed when the assignment executes (IEEE 1076-2008
    /// 10.5.2.1): `delay` is then the tick scale applied to the TIME value
    /// held in delay_source, as for a dynamic WaitFor.
    std::optional<RegisterId> delay_source { };
    std::uint32_t delay_source_width { };
    /// A null waveform element (IEEE 1076-2019 10.5.2.2, 11.6): its
    /// transaction disconnects the driver of a guarded signal instead of
    /// giving it a value (`source` still holds a value of the target's width,
    /// which is not used). 1: the signal is a bus, 2: a register (6.4.2.3),
    /// which decides the signal's value while no driver is connected
    /// (14.7.3.2). 0: an ordinary element.
    std::uint8_t null { };

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

/// The twelve transition delays of an interconnect delay (IEEE 1364-2005
/// 16.2.4) with the pulse reject and error limits of each, in simulation
/// ticks and in the order of a module path (IEEE 1800-2023 30.5.1 Table
/// 30-2): 0->1, 1->0, 0->z, z->1, 1->z, z->0, 0->x, x->1, 1->x, x->0, x->z,
/// z->x. A pulse narrower than a transition's reject limit is dropped, one
/// between the reject and the error limit becomes x, and a wider one
/// propagates (30.7); a limit that equals the delay makes the write inertial.
struct NetDelayTable {
    std::array<SimulationTick, 12> delay { };
    std::array<SimulationTick, 12> reject { };
    std::array<SimulationTick, 12> error { };

    friend bool operator==(
        const NetDelayTable&,
        const NetDelayTable&) = default;

    /// The 36 values a WriteNetDelay stores: the delays, then the reject
    /// limits, then the error limits.
    [[nodiscard]] std::vector<SimulationTick> values() const
    {
        std::vector<SimulationTick> result;
        result.reserve(36U);
        result.insert(result.end(), delay.begin(), delay.end());
        result.insert(result.end(), reject.begin(), reject.end());
        result.insert(result.end(), error.begin(), error.end());
        return result;
    }

    /// The table of a gate's rise, fall and turn-off delays (IEEE 1800-2023
    /// 28.16): a change to 1 takes the rise delay, to 0 the fall delay, to z
    /// the turn-off delay and to x the smallest of the three, whatever the
    /// old value; every limit equals its delay, so the write is inertial.
    [[nodiscard]] static NetDelayTable from_gate_delays(
        const TransitionDelays& gate)
    {
        const auto smallest = std::min({ gate.rise, gate.fall, gate.turnoff });
        NetDelayTable result;
        result.delay = { gate.rise, gate.fall, gate.turnoff, gate.rise,
            gate.turnoff, gate.fall, smallest, gate.rise, smallest, gate.fall,
            gate.turnoff, smallest };
        result.reject = result.delay;
        result.error = result.delay;
        return result;
    }

    /// The table of `values` (36 long, as values() gives them).
    [[nodiscard]] static NetDelayTable from_values(
        const std::span<const SimulationTick> values)
    {
        NetDelayTable result;
        if (values.size() == 36U) {
            std::copy_n(values.begin(), 12U, result.delay.begin());
            std::copy_n(values.begin() + 12, 12U, result.reject.begin());
            std::copy_n(values.begin() + 24, 12U, result.error.begin());
        }
        return result;
    }
};

/// Drive a net's delayed copy with an interconnect delay: each bit's change
/// takes the delay of its own transition, and any number of changes may be
/// pending at once (a transport delay), each with its own pulse limits
/// (IEEE 1364-2005 16.2.4). The packed range at `offset` (the whole signal
/// when empty) is the driver's contribution. `table` holds the 36 values of
/// a NetDelayTable (NetDelayTable::values, kept out of line so that the
/// operation stays small). A later table that vpi_put_delays or
/// acc_replace_delays gave the driver (Interpreter::set_net_delay_table)
/// replaces it from the next write on.
struct WriteNetDelay {
    SignalId signal { };
    RegisterId source { };
    std::optional<std::uint32_t> offset;
    std::vector<SimulationTick> table;
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
};

/// One copy driver of a group-mode SDF load (split_sdf_net_delays with
/// per-source delays): it writes the bits [offset, offset + width) of the
/// load's copy with `table`, the delays it was built with (a run without a
/// delay has the zero table). A table that vpi_put_delays or
/// acc_replace_delays gave `driver` later (Interpreter::set_net_delay_table)
/// replaces it.
struct ForeignNetDelayGroup {
    ProcessId driver { };
    std::uint32_t offset { };
    std::uint32_t width { };
    NetDelayTable table;
};

/// The kind of a foreign write of a net with group-mode loads
/// (Interpreter::add_foreign_net_delay_load).
enum class ForeignNetDelayKind : std::uint8_t {
    deposit,
    force,
    release,
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
    // The target names a port-connected net through a hierarchical reference
    // (`force sub.q`), so it forces that net's segment inside the child rather
    // than the net by its own name (IEEE 1800-2023 10.6.2, 23.3.3).
    bool port_segment { };
};

/// Release a static packed force region and reveal current driven bits.
struct ReleaseSignalSlice {
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    std::optional<DynamicIndex> selection;
    bool driving_value { };
    // See ForceSignalSlice::port_segment.
    bool port_segment { };
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
    /// A null transaction of a bus (1) or register (2) driver
    /// (ProjectedWaveformElement::null); `value` is then not used.
    std::uint8_t null { };
};

/// One pending transaction of a driver's projected output waveform (IEEE
/// 1076-2019 10.5.2.2, 14.7.2): the absolute time it is projected to occur
/// at, its value component and whether it is a null transaction (a
/// disconnection of a guarded signal's driver, which has no value).
struct DriverTransaction {
    SimulationTick time { };
    PackedLogic4 value;
    bool null { };
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
    // Inertial fall and turn-off delays of a continuous assignment whose
    // rise and fall delays differ; `delay` is then the rise delay and the
    // turn-off delay defaults to the smaller of the two (28.16).
    std::optional<WaitFor> fall;
    std::optional<WaitFor> turnoff;
    /// A primitive driver's delayed write in a run with a VPI or PLI library:
    /// the delays above are the declared ones, and the driver's delays set
    /// through vpi_put_delays (Interpreter::set_driver_delays) replace them.
    /// Set only by ElaboratedDesign::bind_primitive_delay_slots.
    bool slotted { };
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
    /// VHDL RISING_EDGE and FALLING_EDGE of a sensitivity narrowed to the
    /// clock: '0' or 'L' to '1' or 'H', and the reverse (IEEE 1076-2008
    /// 16.7). A Verilog edge also counts 0 to X and X to 1.
    rising,
    falling,
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
    // timeout after a false condition. When timeout_source is set, timeout
    // is zero and the deadline is the signed TIME value held in that register
    // when the origin wait suspends.
    std::optional<SimulationTick> timeout;
    std::optional<RegisterId> timeout_result;
    std::optional<InstructionIndex> timeout_origin;
    std::optional<RegisterId> timeout_source;
    std::uint32_t timeout_source_width { };
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

/// What an EventArrayElement operation does with the element it selects.
enum class EventArrayOperation : std::uint8_t {
    /// `@(e[i])`: suspend until the element is triggered (IEEE 1800-2023
    /// 9.4.2, 15.5.2).
    wait,
    /// `-> e[i]`: trigger the element (15.5.1).
    trigger,
    /// `->> e[i]`, with `delay` when the statement has one: trigger the
    /// element in the NBA region (15.5.1).
    trigger_nonblocking,
    /// `e[i].triggered`: whether the element was triggered in the current
    /// time step (15.5.3), into `destination`.
    triggered,
};

/// An operation on the element of a named-event array that a run-time index
/// selects (IEEE 1800-2023 7.4, 15.5). `elements` are the array's events in
/// declared order from its left index `left` toward its right one, one index
/// apart (`descending` when the left index is the larger). An index with an
/// unknown or high-impedance bit or outside the declared range selects no
/// element (7.4.6): a wait then never resumes, a trigger does nothing and
/// the triggered state is 0 (event_array_element_signal).
struct EventArrayElement {
    EventArrayOperation operation { EventArrayOperation::wait };
    RegisterId index { };
    std::uint32_t index_width { };
    bool index_signed { };
    bool descending { };
    std::int64_t left { };
    std::vector<SignalId> elements;
    std::optional<RegisterId> destination;
    std::optional<SimulationTick> delay;
};

/// The element of an EventArrayElement's array that the index value
/// selects, or none (IEEE 1800-2023 7.4.6).
[[nodiscard]] std::optional<SignalId> event_array_element_signal(
    const EventArrayElement& operation, const PackedLogic4& index);

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
/// Spawn child PCs at one fork site; each ends at ForkEnd. Ordinary forks
/// share the lexical frame. Concurrent assertion attempts request a private
/// register-frame snapshot for each child.
struct Fork {
    std::vector<InstructionIndex> branches;
    ForkJoinKind join { ForkJoinKind::all };
    bool isolate_register_frame { };
    /// The children are pending updates (a clocking drive with a cycle
    /// delay), which disable fork and wait fork leave alone.
    bool detached { };
    /// The children are pending deferred assertion reports (IEEE 1800-2023
    /// 16.4.1): a DisableFork with `deferred_reports` flushes them.
    bool deferred_report { };
};
struct ForkEnd { };
struct WaitFork { };
/// Cancel every descendant fork, or only children spawned by one named fork
/// site when `site` is present.
struct DisableFork {
    std::optional<InstructionIndex> site;
    /// A deferred assertion flush point (IEEE 1800-2023 16.4.2): ends the
    /// pending reports (the children of Forks with `deferred_report`) of
    /// this process and no other child; `site` is empty.
    bool deferred_reports { };
};
/// Terminate one dynamically active named sequential block. `begin` and `end`
/// delimit its lexical operation interval; execution resumes at `end`.
struct DisableBlock {
    InstructionIndex begin { };
    InstructionIndex end { };
    /// Candidate hierarchical names of a block or task outside this
    /// process's lexical scope, most local first; the first that names a
    /// block_entry/block_exit interval in any process is disabled there
    /// (IEEE 1800-2017 9.6.2). `begin` and `end` are unused then.
    std::vector<std::string> scopes { };
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

/// A string port connected to one element of a fixed string array: the
/// port's object is that element's storage (IEEE 1800-2017 23.3.3.5).
struct StringElementAlias {
    ContainerObjectId object { };
    std::uint32_t ordinal { };

    friend bool operator==(
        const StringElementAlias&,
        const StringElementAlias&) = default;
};

struct StringObject {
    std::string name;
    std::string initial_value;
    // Opaque design-local semantic unit identity for package-owned storage.
    // Zero marks objects without a SystemVerilog package owner.
    std::uint32_t package_owner_id { };
    std::optional<StringElementAlias> element_alias { };
    // Toggled on each change, by the design's writes and by a deposit; the
    // object's waiters wait on it (IEEE 1800-2023 9.4.2, 38.34).
    std::optional<SignalId> change_signal { };
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
    // Opaque design-local semantic unit identity for package-owned storage,
    // and for a class's static container that of the unit declaring the
    // class (IEEE 1800-2023 8.9). Zero marks objects without such an owner.
    std::uint32_t package_owner_id { };
    // A one-bit signal toggled at each change of an object that has no
    // readable signal alias, so a process with an implicit event list can
    // wait on it (IEEE 1800-2023 9.2.2.2.1, 10.3.2).
    std::optional<SignalId> change_signal { };
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
    /// Edge-qualified sensitivity observes only the least-significant bit.
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

/// Startup-only execution phase for process instances that initialize
/// SystemVerilog package static variables before ordinary time-zero work.
enum class ProcessStartupPhase : std::uint8_t {
    ordinary,
    package_static_initializer,
    // An ordinary process that time zero starts after every other ordinary
    // process: an always_comb or always_latch runs once after all initial
    // and always procedures have started (IEEE 1800-2017 9.2.2.2.2).
    after_procedures,
    // Runs one class static property's initializer in its specialization,
    // in the same startup order as package static initializers and before
    // any procedure (IEEE 1800-2017 8.9, 6.21). It writes only class static
    // properties, so it drives no signal.
    class_static_initializer,
};

/// Whether a process of `phase` runs once in the startup order, before the
/// ordinary time-zero processes.
[[nodiscard]] constexpr bool startup_initializer_phase(
    const ProcessStartupPhase phase) noexcept
{
    return phase == ProcessStartupPhase::package_static_initializer
        || phase == ProcessStartupPhase::class_static_initializer;
}

/// Whether a startup process owned by `own` (zero for a class outside every
/// package) with the canonical `others` list may write an object of `owner`.
/// Objects outside every package (owner zero) are never writable.
[[nodiscard]] inline bool startup_owner_writable(const std::uint32_t own,
    const std::span<const std::uint32_t> others,
    const std::uint64_t owner) noexcept
{
    return owner != 0U
        && (owner == own || std::ranges::binary_search(others, owner));
}

/// Whether a startup owner list is canonical: ascending, unique, nonzero.
[[nodiscard]] inline bool canonical_startup_owner_ids(
    const std::span<const std::uint32_t> owners) noexcept
{
    for (std::size_t index { }; index < owners.size(); ++index) {
        if (owners[index] == 0U
            || (index != 0U && owners[index] <= owners[index - 1U])) {
            return false;
        }
    }
    return true;
}

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
    /// The result register of each user function this process lowers, by
    /// callee (IEEE 1800-2023 37.42, 38.15): a table of its own, so the
    /// debug-local listings and the debug-local gates of the optimizers do
    /// not see it. A run with the execution probes reads a slot at the
    /// callee's frame exit (`Interpreter::read_call_result`).
    std::vector<DebugLocal> call_results;
    /// The formals and locals of the VHDL functions this process lowers, and
    /// the parameters of its `for` loops (IEEE 1076-2019 14.6 a, b), in a
    /// table of their own: it is no debug local, so no optimizer gate sees
    /// it, and a VHPI program with the execution probes reads it by name
    /// (`<subprogram>.<name>`, suffixed `@<line>:<column>` with the place of
    /// the call that made the frame). A process the optimizers rewrote has
    /// none.
    std::vector<DebugLocal> frame_locals;
    /// Which frame each call site of a VHDL subprogram runs and which slots
    /// of `debug_locals` and `frame_locals` each frame owns: the identity of
    /// the frame-local set a VHPI call object reads (IEEE 1076-2019 14.6 b).
    CallFrameMap call_frames;
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
        /// A region that is no source of its signal until a program creates
        /// the driver at run time: the placeholder process of a foreign
        /// architecture holds one for each signal of the architecture (IEEE
        /// 1076-2019 20.4.2, 23.6; its ports are sources from the start,
        /// 6.4.2.3). It takes no part in resolution or initialization before
        /// then (14.7.3.1); the first created driver of the signal that
        /// reuses the placeholder activates it (Interpreter::add_host_driver).
        bool dormant { };
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
    ProcessStartupPhase startup_phase { ProcessStartupPhase::ordinary };
    std::uint64_t startup_order { };
    /// Opaque design-local semantic unit identity of a package initializer.
    /// Zero is reserved for processes outside package startup.
    std::uint32_t startup_package_owner_id { };
    /// The other packages whose objects this startup process may write,
    /// as canonical owner tokens: those a class's code depends on (a class
    /// static initializer, or the constructor a package class-handle
    /// initializer runs), and for a class outside every package the unit
    /// that declares it, whose class static containers it writes (8.9).
    /// Empty for a package's own variable initializers unless they write the
    /// class static containers of a package they depend on.
    std::vector<std::uint32_t> startup_writable_package_owner_ids { };
    /// Direct package objects this process may write synchronously during
    /// its own startup action. These IDs are instance-owned and canonical.
    std::vector<StringObjectId> startup_writable_string_object_ids { };
    std::vector<ContainerObjectId> startup_writable_container_object_ids { };
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

enum class ExecutionPointKind : std::uint8_t {
    statement,
    call,
    wait,
    assertion,
    process_entry,
    process_suspend,
    call_return,
    // A SystemVerilog task, function or method frame is activated, and is
    // about to end (IEEE 1800-2023 37.43, 38.36.1 cbStartOfFrame and
    // cbEndOfFrame); `callee` names the callable. Probe builds only.
    frame_start,
    frame_end,
    // A thread (a process or a fork branch) is created, is deleted, and
    // resumes execution (IEEE 1800-2023 37.44, 38.36.1 cbStartOfThread,
    // cbEndOfThread, cbEnterThread); `process` identifies the thread and
    // `parent_process` the thread that forked it. Probe builds only.
    thread_start,
    thread_end,
    thread_enter,
    // A lowered class constructor has completed (IEEE 1800-2023 38.36.1
    // cbCreateObj). Probe builds only.
    object_created,
    // An immediate or deferred assertion passed or failed (IEEE 1800-2023
    // 39.4.1). Probe builds only.
    assertion_pass,
    assertion_fail,
    // A thread leaves a named block or task body that a disable statement
    // names (IEEE 1800-2023 9.6.2, 38.36.1 cbDisable: only an active target
    // takes the callback); `source` is where the block begins. Probe builds
    // only.
    block_exit,
    // A system function with state returned a value (IEEE 1800-2023 38.15);
    // `source` is the call and `callee` its name. Probe builds only.
    call_value,
};

struct ExecutionPoint {
    ProcessId process { }, design_process { };
    InstructionIndex instruction { };
    ExecutionPointKind kind { ExecutionPointKind::statement };
    SourceLocation source;
    std::string scope;
    std::string language_standard;
    std::string compatibility_profile;
    // The callable of a frame point.
    std::string callee;
    // A frame point's DebugPoint::callee_declaration and callee_receiver.
    std::uint32_t callee_declaration { };
    std::string callee_receiver;
    // The forking thread of a thread_start point; none for a process.
    std::optional<ProcessId> parent_process;

    ExecutionPoint() = default;
    ExecutionPoint(
        const ProcessId execution_process,
        const ProcessId source_process,
        const InstructionIndex execution_instruction,
        const ExecutionPointKind execution_kind,
        SourceLocation execution_source,
        std::string execution_scope = { },
        std::string execution_language_standard = { },
        std::string execution_compatibility_profile = { },
        std::string execution_callee = { },
        std::optional<ProcessId> execution_parent = std::nullopt);
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
};

class ProcessExecutor {
public:
    static constexpr auto native_register_width = std::numeric_limits<std::size_t>::max();

    virtual ~ProcessExecutor() = default;
    /// True when the executor keeps no run state of its own: every register,
    /// frame and program counter of its process lives in the process record
    /// and the owners' sections (the static kernel's host stub). A run whose
    /// live processes all have such executors can be saved (#128); any other
    /// executor keeps state that the run-state image has no description of.
    [[nodiscard]] virtual bool run_state_free() const noexcept
    {
        return false;
    }
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

    /// Clone a child executor with private register, string, and container
    /// register storage. Executors that cannot provide an isolated lexical
    /// activation must reject this fork form.
    [[nodiscard]] virtual std::unique_ptr<ProcessExecutor>
    fork_clone_isolated_register_frame(InstructionIndex)
    {
        throw std::logic_error {
            "executor does not support isolated fork register frames"
        };
    }
};

/// What splitting one more load port in a running simulation appends and
/// rewrites (split_sdf_net_delays, at run time): the delayed copy of the net,
/// the processes that write it with WriteNetDelay, and the processes of the
/// load instance with their reads of the net renamed to the copy (same
/// operation count and positions, so a suspended process keeps its place).
/// `signals` and `processes` take the ids that follow the ones the
/// interpreter has.
struct NetDelayGraft {
    std::vector<Signal> signals;
    std::vector<Process> processes;
    std::vector<std::pair<ProcessId, Process>> rewritten;
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

class Interpreter;

} // namespace fsim::runtime::simir
