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
