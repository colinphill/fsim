// SPDX-License-Identifier: Apache-2.0
// Included inside namespace fsim::runtime::simir by simir.hpp.

enum class ResolutionKind : std::uint8_t {
    none,
    sv_wire,
    std_logic,
    vhdl_user_or,
    vhdl_user_and,
    sv_wand,
    sv_wor,
    sv_user_first,
    // A VHDL resolution function run as the signal's resolver program.
    vhdl_user_function,
};

enum class ValueKind : std::uint8_t {
  logic4,
  logic9,
};

/// IEEE 1364 strength rank used by the common resolved-driver kernel.
enum class StrengthRank : std::uint8_t {
  highz,
  small,
  medium,
  weak,
  large,
  pull,
  strong,
  supply,
};

struct DriveStrength {
  StrengthRank zero{StrengthRank::strong};
  StrengthRank one{StrengthRank::strong};

  friend bool operator==(
      const DriveStrength&, const DriveStrength&) = default;
};

/// The program that resolves a VHDL signal with a user resolution function
/// (IEEE 1076-2008 14.7.3.2). The runtime writes the driving values of
/// `drivers`, in order and leftmost first, to register `input` of `process`,
/// runs one pass of the program and takes the effective value from `output`.
/// A resolver program for a given number of connected drivers.
struct VhdlResolverVariant {
  ProcessId process{};
  RegisterId input{};
  RegisterId output{};

  friend bool operator==(
      const VhdlResolverVariant&, const VhdlResolverVariant&) = default;
};

struct VhdlSignalResolver {
  ProcessId process{};
  RegisterId input{};
  RegisterId output{};
  std::vector<ProcessId> drivers;
  // A guarded signal (IEEE 1076-2008 6.4.2.3): 1 for a bus, 2 for a
  // register; 0 otherwise. Its drivers may be disconnected; variants[k]
  // resolves k connected drivers (k below the driver count).
  std::uint8_t guarded_kind{};
  std::vector<VhdlResolverVariant> variants{};
  // A port-association conversion (IEEE 1076-2008 6.5.7.1): the program
  // reads these signals and runs, in the same simulation cycle, whenever
  // one of them changes. Such a signal has no drivers of its own.
  std::vector<SignalId> sources{};

  friend bool operator==(
      const VhdlSignalResolver&, const VhdlSignalResolver&) = default;
};

struct Signal {
  std::string name;
  PackedLogic4 initial_value;
  ResolutionKind resolution{ResolutionKind::none};
  ValueKind value_kind{ValueKind::logic4};
  std::optional<Logic4> implicit_driver;
  DriveStrength implicit_drive_strength{
      StrengthRank::pull, StrengthRank::pull};
  std::optional<StrengthRank> charge_strength;
  std::optional<SimulationTick> charge_decay;
  SystemVerilogScalarKind systemverilog_scalar{
      SystemVerilogScalarKind::None};
  bool event_variable { };
  std::optional<VhdlSignalResolver> vhdl_resolver;
  // Nonzero for a VHDL scalar (integer, enumeration, physical or floating
  // point) wider than one bit: its bits form one scalar whose projected
  // transactions are kept or rejected together (IEEE 1076-2008 10.5.2.2).
  std::uint32_t vhdl_scalar_width { };

  Signal() = default;
  Signal(
      std::string signal_name,
      PackedLogic4 signal_initial_value,
      ResolutionKind signal_resolution = ResolutionKind::none,
      ValueKind signal_value_kind = ValueKind::logic4,
      std::optional<Logic4> signal_implicit_driver = std::nullopt,
      DriveStrength signal_implicit_drive_strength = {
          StrengthRank::pull, StrengthRank::pull},
      std::optional<StrengthRank> signal_charge_strength = std::nullopt,
      std::optional<SimulationTick> signal_charge_decay = std::nullopt,
      SystemVerilogScalarKind signal_systemverilog_scalar =
          SystemVerilogScalarKind::None)
      : name(std::move(signal_name)),
        initial_value(std::move(signal_initial_value)),
        resolution(signal_resolution),
        value_kind(signal_value_kind),
        implicit_driver(signal_implicit_driver),
        implicit_drive_strength(signal_implicit_drive_strength),
        charge_strength(signal_charge_strength),
        charge_decay(signal_charge_decay),
        systemverilog_scalar(signal_systemverilog_scalar) {}
};

struct SystemVerilogScalarSignalSnapshot {
  SignalId signal{};
  std::string name;
  SystemVerilogScalarValue value;
};
