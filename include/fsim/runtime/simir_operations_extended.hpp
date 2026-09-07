// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal implementation fragment included by simir.hpp after the core
// value, signal, and process-control operation declarations.

/// Optional integral ID encoded in its otherwise-invalid maximum value. The
/// archive codec preserves the ordinary optional wire representation.
template <std::unsigned_integral T>
class RareOptionalId {
public:
    using rare_optional_value_type = T;

    constexpr RareOptionalId() noexcept = default;
    constexpr RareOptionalId(const std::nullopt_t) noexcept { }
    constexpr RareOptionalId(const T value) noexcept : value_(value) { }
    constexpr RareOptionalId& operator=(const std::nullopt_t) noexcept
    {
        reset();
        return *this;
    }
    constexpr RareOptionalId& operator=(const T value) noexcept
    {
        value_ = value;
        return *this;
    }
    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return value_ != absent;
    }
    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return has_value();
    }
    [[nodiscard]] constexpr T operator*() const noexcept { return value_; }
    [[nodiscard]] constexpr T value_or(const T fallback) const noexcept
    {
        return has_value() ? value_ : fallback;
    }
    constexpr void reset() noexcept { value_ = absent; }
    constexpr T& emplace()
    {
        value_ = T { };
        return value_;
    }

    friend constexpr bool operator==(
        const RareOptionalId&, const RareOptionalId&) noexcept = default;

private:
    static constexpr T absent = std::numeric_limits<T>::max();
    T value_ { absent };
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
    RareVector<RegisterId> packed;
    RareVector<StringRegisterId> strings;
    RareVector<ContainerRegisterId> containers;
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
    RareVector<RegisterId> preserve_packed;
    RareVector<StringRegisterId> preserve_strings;
    RareVector<ContainerRegisterId> preserve_containers;
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

/// Immutable, process-wide interned diagnostic text. Source paths and lexical
/// scopes are repeated at every DebugPoint; retaining one allocation per
/// distinct spelling avoids making debug provenance the dominant runtime
/// memory consumer while preserving its full text.
class InternedString {
public:
    InternedString() = default;
    InternedString(std::string value);
    InternedString(std::string_view value);
    InternedString(const char* value);

    InternedString& operator=(std::string value);
    InternedString& operator=(std::string_view value);
    InternedString& operator=(const char* value);

    [[nodiscard]] const std::string& str() const noexcept;
    [[nodiscard]] const char* c_str() const noexcept { return str().c_str(); }
    [[nodiscard]] bool empty() const noexcept { return str().empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return str().size(); }
    [[nodiscard]] auto begin() const noexcept { return str().begin(); }
    [[nodiscard]] auto end() const noexcept { return str().end(); }
    [[nodiscard]] auto find(
        const std::string_view value,
        const std::size_t offset = 0) const noexcept
    {
        return str().find(value, offset);
    }
    [[nodiscard]] bool starts_with(const std::string_view value) const noexcept
    {
        return str().starts_with(value);
    }
    [[nodiscard]] bool ends_with(const std::string_view value) const noexcept
    {
        return str().ends_with(value);
    }
    [[nodiscard]] operator std::string_view() const noexcept { return str(); }
    [[nodiscard]] operator std::string() const { return str(); }

    friend std::ostream& operator<<(
        std::ostream& output, const InternedString& value)
    {
        return output << value.str();
    }

    friend bool operator==(const InternedString& left,
        const InternedString& right) noexcept
    {
        return left.value_ == right.value_ || left.str() == right.str();
    }
    friend bool operator==(const InternedString& left,
        const std::string_view right) noexcept
    {
        return left.str() == right;
    }
    friend bool operator==(const InternedString& left,
        const std::string& right) noexcept
    {
        return left.str() == right;
    }
    friend bool operator==(const InternedString& left,
        const char* right) noexcept
    {
        return left.str() == right;
    }
    friend bool operator==(const std::string_view left,
        const InternedString& right) noexcept
    {
        return left == right.str();
    }
    friend bool operator==(const std::string& left,
        const InternedString& right) noexcept
    {
        return left == right.str();
    }
    friend bool operator==(const char* left,
        const InternedString& right) noexcept
    {
        return left == right.str();
    }
    friend std::string operator+(
        const InternedString& left, const std::string_view right)
    {
        return left.str() + std::string { right };
    }
    friend std::string operator+(
        const std::string_view left, const InternedString& right)
    {
        return std::string { left } + right.str();
    }

private:
    static std::shared_ptr<const std::string> intern(std::string value);
    std::shared_ptr<const std::string> value_;
};

struct SourceLocation {
    InternedString path;
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

    friend bool operator==(
        const ExpressionProfile&, const ExpressionProfile&) = default;
};

/// Copy-on-write expression metadata. Generated hierarchy instances commonly
/// lower the same lexical process body; retain one immutable profile sequence
/// for those equivalent processes while preserving the serialized sequence.
class ExpressionProfileList {
public:
    using Storage = std::vector<ExpressionProfile>;
    using value_type = ExpressionProfile;
    using const_iterator = Storage::const_iterator;

    ExpressionProfileList() = default;
    ExpressionProfileList(std::initializer_list<ExpressionProfile> profiles)
        : storage_ { std::make_shared<Storage>(profiles) }
    {
    }
    explicit ExpressionProfileList(Storage storage)
        : storage_ { std::make_shared<Storage>(std::move(storage)) }
    {
    }

    ExpressionProfileList& operator=(
        std::initializer_list<ExpressionProfile> profiles)
    {
        storage_ = std::make_shared<Storage>(profiles);
        return *this;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return storage_ ? storage_->size() : 0U;
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0U; }
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return storage().begin();
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return storage().end();
    }
    void push_back(ExpressionProfile profile)
    {
        writable_storage().push_back(std::move(profile));
    }
    void share_from(const ExpressionProfileList& representative) noexcept
    {
        storage_ = representative.storage_;
    }
    [[nodiscard]] operator std::span<const ExpressionProfile>() const noexcept
    {
        return storage();
    }

    friend bool operator==(
        const ExpressionProfileList& left,
        const ExpressionProfileList& right)
    {
        return left.storage() == right.storage();
    }

private:
    [[nodiscard]] const Storage& storage() const noexcept
    {
        static const Storage empty_storage;
        return storage_ ? *storage_ : empty_storage;
    }
    Storage& writable_storage()
    {
        if (!storage_) {
            storage_ = std::make_shared<Storage>();
        } else if (storage_.use_count() != 1) {
            storage_ = std::make_shared<Storage>(*storage_);
        }
        return *storage_;
    }

    std::shared_ptr<Storage> storage_;
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
    InternedString scope;

    DebugPoint() = default;
    DebugPoint(
        const DebugPointKind point_kind,
        SourceLocation point_source,
        InternedString point_scope = { })
        : kind(point_kind)
        , source(std::move(point_source))
        , scope(std::move(point_scope))
    {
    }
};

struct Assert {
    Assert() = default;
    Assert(
        const RegisterId assertion_condition,
        InternedString assertion_message,
        const AssertionSeverity assertion_severity,
        SourceLocation assertion_source)
        : source(std::move(assertion_source))
        , message(std::move(assertion_message))
        , condition(assertion_condition)
        , severity(assertion_severity)
    {
    }

    SourceLocation source;
    InternedString message;
    RegisterId condition { };
    AssertionSeverity severity { AssertionSeverity::error };
};

inline auto archive_fields(const Assert& assertion)
{
    return std::tie(
        assertion.condition, assertion.message,
        assertion.severity, assertion.source);
}

inline auto archive_fields(Assert& assertion)
{
    return std::tie(
        assertion.condition, assertion.message,
        assertion.severity, assertion.source);
}

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
    RareOptionalId<RegisterId> queue_type;
    RareOptionalId<RegisterId> maximum_length;
    RareOptionalId<RegisterId> job_id;
    RareOptionalId<RegisterId> information_id;
    RareOptionalId<RegisterId> statistic_code;
    RareOptionalId<RegisterId> statistic_value;
    RegisterId status { };
    RareOptionalId<RegisterId> result;
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

enum class SystemVerilogCoverageCommand : std::int32_t {
    start = 0,
    stop = 1,
    reset = 2,
    check = 3,
};

enum class SystemVerilogCoverageScope : std::int32_t {
    module = 10,
    hierarchy = 11,
};

enum class SystemVerilogCoverageType : std::int32_t {
    assertion = 20,
    fsm_state = 21,
    statement = 22,
    toggle = 23,
};

enum class SystemVerilogCoverageStatus : std::int32_t {
    overflow = -2,
    error = -1,
    no_coverage = 0,
    ok = 1,
    partial = 2,
};

struct CoverageControlEvent {
    std::int32_t command { };
    std::int32_t coverage_type { };
    std::int32_t scope { };
    std::string selector;
    std::string instance_context;
    bool selector_is_instance { };
};

/// Execute the standardized SystemVerilog coverage control for one module
/// definition name or one elaborated instance path.
struct CoverageControl {
    RegisterId destination { };
    RegisterId command { };
    RegisterId coverage_type { };
    RegisterId scope { };
    StringRegisterId selector { };
    std::string instance_context;
    bool selector_is_instance { };
};

enum class SystemVerilogCoverageAccessKind : std::uint8_t {
    get,
    get_max,
    merge,
    save,
};

struct CoverageAccessEvent {
    SystemVerilogCoverageAccessKind kind {
        SystemVerilogCoverageAccessKind::get
    };
    std::int32_t coverage_type { };
    std::optional<std::int32_t> scope;
    std::string selector;
    std::string instance_context;
    bool selector_is_instance { };
    std::string filename;
};

/// Query one selected standardized coverage view, or persist the aggregate
/// database for one coverage family.
struct CoverageAccess {
    RegisterId destination { };
    SystemVerilogCoverageAccessKind kind {
        SystemVerilogCoverageAccessKind::get
    };
    RegisterId coverage_type { };
    std::optional<RegisterId> scope;
    std::optional<StringRegisterId> selector;
    std::string instance_context;
    bool selector_is_instance { };
    std::optional<StringRegisterId> filename;
};

/// Record one hit against the exact code-coverage point and dense counter
/// owned by the elaborated design instance. This operation intentionally has
/// no register, signal, object, or scheduling operand.
struct CodeCoverageHit {
    ::fsim::runtime::CodeCoveragePointId point;
    ::fsim::runtime::CodeCoverageMetric metric {
        ::fsim::runtime::CodeCoverageMetric::Statement
    };
    ::fsim::runtime::CodeCoverageCounterId counter;

    friend constexpr bool operator==(
        const CodeCoverageHit&, const CodeCoverageHit&) = default;
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
    InternedString message;
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
