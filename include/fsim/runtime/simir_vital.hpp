// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal declaration fragment included by simir.hpp after common IDs and
// assertion metadata have been declared.

class ProcessExecutionContext;

struct VitalMemoryDeclare {
  RegisterId destination{};
  std::uint64_t word_count{};
  std::uint64_t word_width{};
  std::uint64_t subword_width{};
  StringRegisterId load_file{};
  bool binary{};
  bool embedded_load{};
  std::string embedded_load_text;
  SourceLocation source;
};

enum class VitalTimingCheckKind : std::uint8_t {
  setup_hold,
  recovery_removal,
  period_pulse,
  in_phase_skew,
  out_phase_skew,
};

[[nodiscard]] constexpr std::size_t vital_x01_ordinal(
    const Logic9 value) noexcept {
  if (value == Logic9::zero || value == Logic9::l) return 1U;
  if (value == Logic9::one || value == Logic9::h) return 2U;
  return 0U;
}

[[nodiscard]] constexpr std::uint16_t vital_edge_symbol_mask(
    const Logic9 previous,
    const Logic9 current) noexcept {
  constexpr std::array<std::array<std::uint16_t, 3>, 3> masks{{
      {{0U, 0xDA08U, 0xB504U}},
      {{0xA150U, 0U, 0x8145U}},
      {{0xC2A0U, 0x828AU, 0U}},
  }};
  return masks[vital_x01_ordinal(previous)][vital_x01_ordinal(current)];
}

[[nodiscard]] constexpr bool vital_edge_symbol_matches(
    const Logic9 previous,
    const Logic9 current,
    const std::uint16_t selected_symbols) noexcept {
  return (vital_edge_symbol_mask(previous, current)
          & selected_symbols) != 0U;
}

/// One intrinsic VITAL timing-check call site.
///
/// Signal delays are represented by ordinary elaborated VHDL 'delayed
/// signals. The operation therefore receives already sampled signal IDs and
/// contains only immutable check metadata; its instruction index supplies the
/// stable per-call state identity.
struct VitalTimingCheck {
  RegisterId destination{};
  VitalTimingCheckKind kind{VitalTimingCheckKind::setup_hold};
  SignalId test_signal{};
  std::uint32_t test_offset{};
  std::optional<SignalId> reference_signal;
  std::uint32_t reference_offset{};
  std::optional<SignalId> trigger_signal;
  std::array<SimulationTick, 4> limits{};
  std::uint16_t reference_edges{0xffffU};
  bool active_low{};
  bool check_enabled{true};
  std::array<bool, 4> enables{true, true, true, true};
  bool x_on{true};
  bool message_on{true};
  AssertionSeverity severity{AssertionSeverity::warning};
  std::string message;
  SourceLocation source;
};

struct VitalTimingState {
  bool initialized{};
  bool last_violation{};
  bool setup_enabled{};
  bool hold_enabled{};
  Logic9 test{Logic9::u};
  Logic9 reference{Logic9::u};
  std::optional<SimulationTick> test_event;
  std::optional<SimulationTick> reference_event;
  std::array<std::optional<SimulationTick>, 2> test_direction;
  std::array<std::optional<SimulationTick>, 2> reference_direction;
  std::array<std::optional<SimulationTick>, 4> skew_deadlines;
  std::optional<SimulationTick> scheduled_trigger;
  std::optional<SimulationTick> trigger_request;
  bool trigger_level{};
};

enum class VitalDelayKind : std::uint8_t { signal, wire, path };
enum class VitalDelayShape : std::uint8_t { single, delay01, delay01z };
enum class VitalGlitchMode : std::uint8_t {
  on_event,
  on_detect,
  inertial,
  transport,
};

struct VitalPathCandidate {
  RegisterId input_change_time{};
  RegisterId condition{};
  std::array<RegisterId, 6> delays{};
};

/// One intrinsic VITAL signal, wire, or path-delay call site.
struct VitalDelay {
  VitalDelayKind kind{VitalDelayKind::signal};
  VitalDelayShape shape{VitalDelayShape::single};
  SignalId output{};
  RegisterId source{};
  std::optional<RegisterId> glitch_data;
  std::vector<VitalPathCandidate> paths;
  std::array<RegisterId, 6> default_delays{};
  VitalGlitchMode mode{VitalGlitchMode::on_event};
  RegisterId output_map{};
  bool x_on{true};
  bool message_on{true};
  bool negative_preemption{};
  bool ignore_default_delay{};
  bool reject_fast_path{};
  AssertionSeverity severity{AssertionSeverity::warning};
  std::string message;
  SourceLocation source_location;
};

struct VitalDelayState {
  bool initialized{};
  bool last_glitch{};
  Logic9 last_value{Logic9::u};
  Logic9 scheduled_value{Logic9::u};
  SimulationTick scheduled_time{};
  SimulationTick glitch_time{};
};

/// One resource-governed VITAL memory object. Word width and depth are
/// language values; storage is bounded only by the common host-resource
/// budget and therefore has no fixed semantic width/depth ceiling.
struct VitalMemoryState {
  std::size_t word_count{};
  std::uint32_t word_width{};
  std::uint32_t subword_width{};
  std::uint32_t bits_per_enable{};
  /// Small memories use contiguous storage. Large logical memories leave this
  /// empty and materialize only words that differ from default_word.
  std::vector<PackedLogic4> words;
  PackedLogic4 default_word;
  std::unordered_map<std::size_t, PackedLogic4> sparse_words;
};

[[nodiscard]] VitalMemoryState make_vital_memory(
    std::uint64_t word_count,
    std::uint64_t word_width,
    std::uint64_t subword_width);

[[nodiscard]] const PackedLogic4& vital_memory_word(
    const VitalMemoryState& memory,
    std::size_t address);

[[nodiscard]] PackedLogic4& vital_memory_word(
    VitalMemoryState& memory,
    std::size_t address);

void load_vital_memory_text(
    VitalMemoryState& memory,
    std::string_view text,
    bool binary);

enum class VitalMemoryAddressState : std::uint8_t {
  good,
  unknown,
  invalid,
  good_transition,
  unknown_transition,
  invalid_transition,
};

struct VitalMemoryAddress {
  VitalMemoryAddressState state{VitalMemoryAddressState::unknown};
  std::optional<std::size_t> value;
};

/// Decode a packed VHDL address bus. Packed VHDL arrays store the rightmost
/// element at offset zero, so MSB-to-LSB traversal follows the declared range
/// from left to right for both ascending and descending ranges.
[[nodiscard]] VitalMemoryAddress decode_vital_memory_address(
    const PackedLogic4& previous,
    const PackedLogic4& current,
    std::size_t word_count);

struct VitalMemoryTableRow {
  std::vector<char> controls;
  std::vector<char> enables;
  char address{'-'};
  char data{'-'};
  char memory_action{'s'};
  char data_action{'S'};
};

struct VitalMemoryTableResult {
  char memory_action{'s'};
  char data_action{'S'};
  PackedLogic4 memory_corrupt_mask;
  PackedLogic4 data_corrupt_mask;
  std::optional<std::size_t> matched_row;
  bool invalid_input_symbol{};
};

[[nodiscard]] VitalMemoryTableResult lookup_vital_memory_table(
    const std::vector<VitalMemoryTableRow>& rows,
    const PackedLogic4& previous_controls,
    const PackedLogic4& controls,
    VitalMemoryAddressState address_state,
    VitalMemoryAddressState data_state,
    std::size_t word_width);

struct VitalMemorySubwordTableResult {
  std::vector<VitalMemoryTableResult> subwords;
};

[[nodiscard]] VitalMemorySubwordTableResult
lookup_vital_memory_subword_table(
    const std::vector<VitalMemoryTableRow>& rows,
    const PackedLogic4& previous_controls,
    const PackedLogic4& controls,
    const std::vector<PackedLogic4>& previous_enables,
    const std::vector<PackedLogic4>& enables,
    const PackedLogic4& previous_data,
    const PackedLogic4& data,
    VitalMemoryAddressState address_state,
    std::size_t word_width,
    std::size_t subword_width);

enum class VitalMemoryPortState : std::uint8_t {
  undefined,
  read,
  write,
  corrupt,
  high_z,
};

struct VitalMemoryPortFlag {
  VitalMemoryPortState memory_current{VitalMemoryPortState::undefined};
  VitalMemoryPortState memory_previous{VitalMemoryPortState::undefined};
  VitalMemoryPortState data_current{VitalMemoryPortState::undefined};
  VitalMemoryPortState data_previous{VitalMemoryPortState::undefined};
  bool output_disable{};
};

void apply_vital_memory_table_actions(
    VitalMemoryState& memory,
    PackedLogic4& data_output,
    const PackedLogic4& data_input,
    std::optional<std::size_t> address,
    std::size_t low_bit,
    std::size_t high_bit,
    const VitalMemoryTableResult& actions,
    VitalMemoryPortFlag& port_flag);

struct VitalMemoryTableState {
  bool initialized{};
  PackedLogic4 previous_controls;
  std::vector<PackedLogic4> previous_enables;
  PackedLogic4 previous_data;
  PackedLogic4 previous_address;
  std::vector<VitalMemoryPortFlag> port_flags;
};

[[nodiscard]] VitalMemoryAddress execute_vital_memory_word_table(
    VitalMemoryState& memory,
    VitalMemoryTableState& state,
    PackedLogic4& data_output,
    const PackedLogic4& controls,
    const PackedLogic4& data_input,
    const PackedLogic4& address_bus,
    const std::vector<VitalMemoryTableRow>& rows);

[[nodiscard]] VitalMemoryAddress execute_vital_memory_subword_table(
    VitalMemoryState& memory,
    VitalMemoryTableState& state,
    PackedLogic4& data_output,
    const PackedLogic4& controls,
    const std::vector<PackedLogic4>& enables,
    const PackedLogic4& data_input,
    const PackedLogic4& address_bus,
    const std::vector<VitalMemoryTableRow>& rows);

enum class VitalMemoryCrossPortMode : std::uint8_t {
  cross_read,
  write_contention,
  read_write_contention,
  cross_read_and_write_contention,
  cross_read_and_read_contention,
};

struct VitalMemoryCrossPort {
  std::optional<std::size_t> address;
  std::vector<VitalMemoryPortFlag> flags;
};

void apply_vital_memory_cross_ports(
    VitalMemoryState& memory,
    PackedLogic4& data_output,
    std::vector<VitalMemoryPortFlag>& same_port_flags,
    std::optional<std::size_t> same_port_address,
    const std::vector<VitalMemoryCrossPort>& cross_ports,
    VitalMemoryCrossPortMode mode);

void apply_vital_memory_write_contention(
    VitalMemoryState& memory,
    const std::vector<VitalMemoryCrossPort>& cross_ports);

enum class VitalMemoryPortType : std::uint8_t {
  undefined,
  read,
  write,
  read_write,
};

struct VitalMemoryViolationResult {
  VitalMemoryTableResult actions;
  bool violation{};
  bool message_requested{};
};

[[nodiscard]] VitalMemoryViolationResult lookup_vital_memory_violation(
    const std::vector<VitalMemoryTableRow>& rows,
    const PackedLogic4& scalar_flags,
    const PackedLogic4& vector_flags,
    const std::vector<std::size_t>& vector_flag_sizes,
    std::size_t word_width,
    std::size_t subword_width,
    bool message_on = true);

[[nodiscard]] VitalMemoryViolationResult apply_vital_memory_violation(
    VitalMemoryState& memory,
    PackedLogic4& data_output,
    std::vector<VitalMemoryPortFlag>& port_flags,
    const PackedLogic4& data_input,
    std::optional<std::size_t> address,
    const PackedLogic4& scalar_flags,
    const PackedLogic4& vector_flags,
    const std::vector<std::size_t>& vector_flag_sizes,
    const std::vector<VitalMemoryTableRow>& rows,
    VitalMemoryPortType port_type,
    bool message_on = true);

enum class VitalMemoryTimingArc : std::uint8_t {
  parallel,
  cross,
  subword,
};

enum class VitalMemoryMessageFormat : std::uint8_t {
  vector,
  scalar,
  vector_enumerated,
};

struct VitalMemorySetupHoldEntry {
  SimulationTick test_delay{};
  SimulationTick reference_delay{};
  std::array<SimulationTick, 4> limits{};
  bool check_enabled{true};
};

struct VitalMemoryVectorTimingState {
  std::vector<VitalTimingState> elements;
};

struct VitalMemoryVectorTimingResult {
  PackedLogic4 test_violations;
  PackedLogic4 reference_violations;
  bool violation{};
  bool message_requested{};
  std::vector<std::size_t> violated_checks;
};

[[nodiscard]] VitalMemoryVectorTimingResult
evaluate_vital_memory_setup_hold(
    VitalMemoryVectorTimingState& state,
    SimulationTick now,
    const PackedLogic4& test,
    const PackedLogic4& reference,
    const std::vector<VitalMemorySetupHoldEntry>& entries,
    VitalMemoryTimingArc arc,
    std::size_t bits_per_subword,
    std::uint16_t reference_edges,
    std::array<bool, 4> direction_enables = {true, true, true, true},
    bool x_on = true,
    bool message_on = true,
    VitalMemoryMessageFormat message_format =
        VitalMemoryMessageFormat::vector);

[[nodiscard]] PackedLogic4 aggregate_vital_memory_violations(
    const PackedLogic4& violations,
    std::size_t group_size);

struct VitalMemoryPeriodPulseEntry {
  SimulationTick test_delay{};
  SimulationTick period{};
  SimulationTick pulse_width_high{};
  SimulationTick pulse_width_low{};
  bool check_enabled{true};
};

[[nodiscard]] VitalMemoryVectorTimingResult
evaluate_vital_memory_period_pulse(
    VitalMemoryVectorTimingState& state,
    SimulationTick now,
    const PackedLogic4& test,
    const std::vector<VitalMemoryPeriodPulseEntry>& entries,
    bool x_on = true,
    bool message_on = true,
    VitalMemoryMessageFormat message_format =
        VitalMemoryMessageFormat::vector);

enum class VitalMemoryPathDelayShape : std::uint8_t {
  single,
  delay01,
  delay01z,
  delay01zx,
};

enum class VitalMemoryRetainBehavior : std::uint8_t {
  bit_corrupt,
  word_corrupt,
};

/// Normalized form of every scalar and vector VITAL memory path delay.
/// Values follow VitalTransitionType order (01, 10, 0Z, Z1, 1Z, Z0,
/// 0X, X1, 1X, X0, XZ, ZX); unused suffix elements are zero.
struct VitalMemoryPathDelay {
  VitalMemoryPathDelayShape shape{VitalMemoryPathDelayShape::single};
  std::array<SimulationTick, 12> values{};
};

struct VitalMemoryScheduleData {
  Logic9 output_data{Logic9::u};
  std::optional<std::size_t> bits_per_subword;
  SimulationTick schedule_time{};
  Logic9 schedule_value{Logic9::u};
  Logic9 last_output_value{Logic9::u};
  SimulationTick propagation_delay{
      std::numeric_limits<SimulationTick>::max()};
  SimulationTick output_retain_delay{
      std::numeric_limits<SimulationTick>::max()};
  SimulationTick input_age{
      std::numeric_limits<SimulationTick>::max()};
};

/// Initialize scalar or vector schedule data for one path-delay evaluation.
/// An absent subword size represents DefaultNumBitsPerSubword.
void initialize_vital_memory_path_delay(
    std::vector<VitalMemoryScheduleData>& schedule,
    const PackedLogic4& output_data,
    std::optional<std::size_t> bits_per_subword,
    SimulationTick now);

/// Add one normalized scalar/vector input path. Input change times are
/// absolute simulation timestamps, as maintained by the VITAL public
/// InputChangeTime variables. Conditions may be scalar, per output bit, or
/// per subword. Delay rows use output-major order inside each input group.
void add_vital_memory_path_delay(
    std::vector<VitalMemoryScheduleData>& schedule,
    const std::vector<SimulationTick>& input_change_times,
    const std::vector<VitalMemoryPathDelay>& delays,
    VitalMemoryTimingArc arc,
    const std::vector<bool>& conditions,
    SimulationTick now,
    bool output_retain = false,
    VitalMemoryRetainBehavior retain_behavior =
        VitalMemoryRetainBehavior::bit_corrupt);

struct VitalMemoryScheduledValue {
  std::size_t bit{};
  Logic9 value{Logic9::u};
  SimulationTick delay{};
};

/// Convert selected paths into transport waveform elements. Port flags may be
/// scalar, per bit, or per subword. The returned elements are ordered by bit
/// and then delay and can be passed directly to the projected-waveform
/// scheduler by the interpreter or JIT host callback.
[[nodiscard]] std::vector<VitalMemoryScheduledValue>
schedule_vital_memory_path_delay(
    std::vector<VitalMemoryScheduleData>& schedule,
    const std::vector<VitalMemoryPortFlag>& port_flags,
    const std::array<Logic9, 9>& output_map,
    SimulationTick now);

/// Schedule the selected per-bit waveforms through the kernel-owned projected
/// transport scheduler used by interpreter and alternate executors.
void project_vital_memory_path_delay(
    ProcessExecutionContext& context,
    SignalId output,
    std::vector<VitalMemoryScheduleData>& schedule,
    const std::vector<VitalMemoryPortFlag>& port_flags,
    const std::array<Logic9, 9>& output_map,
    SimulationTick now);

struct VitalPathRuntimeValue {
  SimulationTick input_change_time{};
  bool condition{};
  std::array<SimulationTick, 6> delays{};
};

struct VitalDelayRuntimeValues {
  Logic9 source{Logic9::u};
  std::vector<VitalPathRuntimeValue> paths;
  std::array<SimulationTick, 6> default_delays{};
  std::array<Logic9, 9> output_map{};
};
