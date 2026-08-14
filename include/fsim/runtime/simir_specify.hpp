// SPDX-License-Identifier: Apache-2.0
// Included inside namespace fsim::runtime::simir by simir.hpp.

enum class ModulePathEdge : std::uint8_t {
    none,
    posedge,
    negedge,
    edge,
};

enum class ModulePathPolarity : std::uint8_t {
    none,
    positive,
    negative,
};

enum class ModulePathPulseStyle : std::uint8_t {
    onevent,
    ondetect,
};

struct ModulePathTerminal {
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    friend bool operator==(
        const ModulePathTerminal&,
        const ModulePathTerminal&) = default;
};

inline constexpr std::size_t maximum_module_path_expression_storage_bytes = 256U * 1024U * 1024U;

enum class ModulePathExpressionOperator : std::uint8_t {
    constant,
    terminal,
    bit_not,
    logical_not,
    reduction,
    binary,
    logical_binary,
    shift,
    conditional,
    concatenate,
};

/// One source-ordered node in a scheduler-owned specify expression. Operands
/// refer only to earlier nodes, so validation and evaluation are bounded and
/// recursion-free even for adversarial artifact input.
struct ModulePathExpressionNode {
    ModulePathExpressionOperator operation {
        ModulePathExpressionOperator::constant
    };
    std::vector<std::uint32_t> operands;
    PackedLogic4 constant;
    ModulePathTerminal terminal;
    BinaryOperator binary { BinaryOperator::bit_and };
    LogicalBinaryOperator logical {
        LogicalBinaryOperator::logical_and
    };
    ShiftOperator shift { ShiftOperator::logical_left };
    ReductionOperator reduction { ReductionOperator::bit_and };
    std::uint32_t width { };
    bool is_signed { };
};

struct ModulePathExpression {
    std::vector<ModulePathExpressionNode> nodes;
    std::uint32_t root { };

    [[nodiscard]] bool empty() const noexcept { return nodes.empty(); }
};

/// Select one IEEE 1364 module-path delay from a 1/2/3/6/12-value table.
/// Unknown transitions use the standard derived min/max rules when the table
/// omits their explicit entries. An unchanged value has no transition.
[[nodiscard]] std::optional<SimulationTick> module_path_transition_delay(
    Logic4 before,
    Logic4 after,
    std::span<const SimulationTick> delays);

/// Scheduler-owned normalized Verilog module path. Conditional/data programs
/// are appended without changing the stable terminal/delay prefix.
struct ModulePath {
    std::uint32_t id { };
    /// Instance-qualified declaration identity reserved for SDF annotation.
    /// Unlike the dense runtime ID, this is stable when unrelated roots change.
    std::string identity;
    std::vector<ModulePathTerminal> sources;
    std::vector<ModulePathTerminal> destinations;
    std::vector<ProcessId> drivers;
    std::vector<SimulationTick> delays;
    ModulePathExpression condition;
    ModulePathExpression data_source;
    std::uint32_t selection_group { };
    ModulePathEdge source_edge { ModulePathEdge::none };
    ModulePathPolarity polarity { ModulePathPolarity::none };
    ModulePathPulseStyle pulse_style { ModulePathPulseStyle::onevent };
    bool show_cancelled { };
    std::optional<SimulationTick> pulse_reject_limit;
    std::optional<SimulationTick> pulse_error_limit;
    std::vector<SimulationTick> pulse_reject_delays;
    std::vector<SimulationTick> pulse_error_delays;
    std::vector<SimulationTick> retain_delays;
    bool full { };
    bool conditional { };
    bool ifnone { };
    SourceLocation source;
};

enum class ModuleTimingCheckKind : std::uint8_t {
    setup,
    hold,
    recovery,
    removal,
    skew,
    period,
    width,
    setuphold,
    recrem,
    timeskew,
    fullskew,
    nochange,
};

struct ModuleTimingEvent {
    ModulePathTerminal terminal;
    ModulePathEdge edge { ModulePathEdge::none };
    std::vector<std::string> edge_descriptors;
    ModulePathExpression condition;
};

struct ModuleTimingCheck {
    std::uint32_t id { };
    /// Instance-qualified declaration identity reserved for SDF annotation.
    /// Unlike the dense runtime ID, this is stable when unrelated roots change.
    std::string identity;
    ModuleTimingCheckKind kind { ModuleTimingCheckKind::setup };
    ModuleTimingEvent reference;
    std::optional<ModuleTimingEvent> data;
    std::vector<std::int64_t> limits;
    std::optional<SimulationTick> threshold;
    std::optional<SignalId> notifier;
    ModulePathExpression timestamp_condition;
    ModulePathExpression timecheck_condition;
    std::optional<ModulePathTerminal> delayed_reference;
    std::optional<ModulePathTerminal> delayed_data;
    bool event_based { };
    bool remain_active { };
    SourceLocation source;
};
