// SPDX-License-Identifier: Apache-2.0
#pragma once

/// The register file a debug local's register_id numbers: packed registers,
/// or, for a call-result slot of a function returning a string or a
/// container (IEEE 1800-2023 37.42, 38.15), string or container registers.
enum class DebugLocalStorage : std::uint8_t {
  packed,
  string,
  container,
};

/// What a debug local stands for: a declared variable or formal, the value a
/// system function with state returned (a probe-only slot that the execution
/// probes read and nothing else depends on), or a local of a call frame that
/// the frame-local table holds.
enum class DebugLocalRole : std::uint8_t {
  variable,
  call_value,
  frame_local,
};

/// What a local of a call frame is for the frame it belongs to.
enum class DebugFrameRole : std::uint8_t {
  // Not a local of a call frame (a process local).
  none,
  // A variable the body of a task or function declares.
  variable,
  // A formal of a task or function.
  formal,
  // The class handle of the object a method runs on (`this`, IEEE 1800-2023
  // 8.11); no declared variable.
  receiver,
  // The register that holds the value a function returns.
  result,
};

/// The frame a debug local belongs to, set by the lowering that creates the
/// local so that no consumer has to read it back from the local's displayed
/// name. The name only tells locals apart for a user.
struct DebugFrameLocal {
  // The invocation identity of the lowering of the callable that owns the
  // local; 0 for a local that belongs to no call frame.
  std::uint32_t lowering{};
  // The HIR declaration of that callable (index plus one, 0 for none).
  std::uint32_t callable_declaration{};
  // The simple name the declaration of the local has in the source; empty
  // for the receiver and the result.
  std::string variable;
  DebugFrameRole role{DebugFrameRole::none};
  // The displayed name carries the lowering's suffix because another local
  // already had the plain name.
  bool renamed{};

  friend bool operator==(
      const DebugFrameLocal&, const DebugFrameLocal&) = default;
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
  SystemVerilogScalarKind systemverilog_scalar{
      SystemVerilogScalarKind::None};
  DebugLocalStorage storage{DebugLocalStorage::packed};
  // The built-in data type the local is declared with (IEEE 1800-2023 6.11,
  // 6.12); none for a user-defined type, whose name type_name holds.
  support::SystemVerilogBuiltinType systemverilog_builtin{
      support::SystemVerilogBuiltinType::none};
  DebugLocalRole role{DebugLocalRole::variable};
  DebugFrameLocal frame{};
};

/// Whether the local is the slot of the value a system function with state
/// returned: a probe-only slot that the execution probes read and nothing
/// else depends on.
[[nodiscard]] inline bool is_system_call_value_slot(const DebugLocal& local)
{
  return local.role == DebugLocalRole::call_value;
}

/// Whether a call-result table holds the result register of a user function
/// (which the sharing and fusion gates keep observable); the probe-only slots
/// of system function values do not count, so a process that calls `$random`
/// is shared and fused as it is without them.
[[nodiscard]] inline bool has_function_results(
    const std::vector<DebugLocal>& results)
{
  for (const auto& local : results) {
    if (!is_system_call_value_slot(local)) {
      return true;
    }
  }
  return false;
}

/// A call site of a VHDL subprogram in a process and the frame it binds
/// (IEEE 1076-2019 14.6 b: each call dynamically elaborates its own
/// declarations). `site` is the HIR identity of the call, a function call's
/// expression or a procedure call's statement; `caller` is the invocation
/// identity of the frame whose body holds the call (0 for the process
/// itself) and `frame` that of the frame the call runs.
struct CallSiteFrame {
  bool expression{};
  std::uint32_t site{};
  std::uint32_t caller{};
  std::uint32_t frame{};

  friend bool operator==(
      const CallSiteFrame&, const CallSiteFrame&) = default;
};

/// The table of a process's debug locals a frame-local binding indexes when
/// it is not in the frame-local table: the packed locals, the string locals
/// or the container locals.
enum class FrameLocalStorage : std::uint8_t { packed, string, container };

/// A formal, local or loop parameter of a frame: its HIR declaration, and
/// its slot in the debug locals (of `storage`) or in the frame-local table
/// (`Process::frame_locals`).
struct FrameLocalBinding {
  std::uint32_t frame{};
  std::uint32_t declaration{};
  bool frame_table{};
  std::uint32_t index{};
  FrameLocalStorage storage{FrameLocalStorage::packed};
  /// The declared type resolves to `event` (IEEE 1800-2023 6.18, 15.5): the
  /// local holds an event object's handle, or an array of them.
  bool event{};
  /// The local holds the class handle of the object a class method runs on
  /// (`this`, IEEE 1800-2023 8.11); `declaration` is then the method's.
  bool receiver{};

  friend bool operator==(
      const FrameLocalBinding&, const FrameLocalBinding&) = default;
};

/// The identity of the frame-local sets of a process: the frame each call
/// site runs, and the slots each frame owns. Static data, written by the
/// lowering; a process an optimizer rewrote has none.
struct CallFrameMap {
  std::vector<CallSiteFrame> sites;
  std::vector<FrameLocalBinding> locals;

  [[nodiscard]] bool empty() const noexcept
  {
    return sites.empty() && locals.empty();
  }
  friend bool operator==(const CallFrameMap&, const CallFrameMap&) = default;
};

struct DebugStringLocal {
  std::string name;
  StringRegisterId register_id{};
  SourceLocation source;
  DebugFrameLocal frame{};
};

struct DebugContainerLocal {
  std::string name;
  ContainerRegisterId register_id{};
  ContainerType type;
  SourceLocation source;
  DebugFrameLocal frame{};
};

/// A static variable declared in a block of the process (IEEE 1800-2023
/// 6.21): its storage is the instance's signal, string or container object,
/// not a register of the process.
struct DebugStaticLocal {
  std::string name;
  std::optional<SignalId> signal;
  std::optional<StringObjectId> string;
  std::optional<ContainerObjectId> container;
  SourceLocation source;
};
