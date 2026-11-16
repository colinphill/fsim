// SPDX-License-Identifier: Apache-2.0
#pragma once

// How a class allocation runs the constructor.
enum class ClassConstruction : std::uint8_t {
  // The simulation runs the constructor chain.
  run_constructor,
  // The elaborated process runs the constructor itself; the object is
  // allocated with its property defaults.
  lowered_constructor,
  // A UVM object allocated for the API: its constructor chain runs as
  // generated code in the dispatch process.
  api,
  // The same for a host service a design process can call (clone, the
  // factory): the calling process runs the chain, so the object waits.
  service,
  last = service,
};

struct ClassAllocate {
  RegisterId destination{};
  std::string specialization_identity;
  std::string declared_type;
  std::vector<RegisterId> constructor_actuals;
  std::vector<std::uint8_t> constructor_actual_kinds;
  std::vector<std::string> constructor_actual_names;
  ClassConstruction construction{ClassConstruction::run_constructor};
};

struct ClassPropertyRead {
  RegisterId destination{};
  RegisterId receiver{};
  std::string property_identity;
  std::uint32_t width{};
};

struct ClassPropertyWrite {
  RegisterId receiver{};
  RegisterId source{};
  std::string property_identity;
};

/// Copy a value-container class property (an unpacked array, queue or
/// associative array of values) into a container register; a property never
/// written reads as its type's default value.
struct ClassPropertyContainerRead {
  ContainerRegisterId destination{};
  RegisterId receiver{};
  std::string property_identity;
};

/// Store a container register into a value-container class property.
struct ClassPropertyContainerWrite {
  RegisterId receiver{};
  ContainerRegisterId source{};
  std::string property_identity;
  // The write-back after a change of a method's cached copy of a `this`
  // property: done only while a value-change callback watches class
  // properties, so the callback follows each change (IEEE 1800-2023
  // 38.36.1); the copy is written back at the method's end anyway.
  bool when_watched{};
};

// A step the simulation runs for a ClassMethodCall instead of a class
// method. A lowered randomize() step (IEEE 1800-2023 18.6.1) has an empty
// method identity, or the callback's name for the callback steps. A built-in
// method of a class object (18.6, 18.8, 18.9, 18.13) has an empty identity,
// except for the mode calls, whose identity names the random variable or
// constraint block. The other steps take their payload in the method
// identity: the owner class of a checked cast, the property of a
// handle-container or string-property access; the shallow copy of
// `new obj` and the process kill markers (9.7) have none.
enum class ClassMethodHook : std::uint8_t {
  none,
  randomize_begin,
  randomize_member,
  randomize_end,
  randomize_callback_enter,
  randomize_callback_leave,
  randomize_callback,
  builtin_randomize,
  builtin_randomize_solve,
  builtin_rand_mode,
  builtin_constraint_mode,
  builtin_get_randstate,
  object_srandom,
  object_set_randstate,
  checked_cast,
  container_read,
  container_write,
  container_resize,
  container_push_back,
  container_size,
  container_pop_front,
  container_pop_back,
  string_property_read,
  string_property_write,
  shallow_copy,
  process_kill_begin,
  process_kill_end,
  last = process_kill_end,
};

// A handle-container access of a class property.
[[nodiscard]] constexpr bool class_container_hook(
    const ClassMethodHook hook) noexcept
{
  return hook >= ClassMethodHook::container_read
      && hook <= ClassMethodHook::container_pop_back;
}

// Whether `hook` is one of the lowered randomize() steps (not a built-in
// method of an object).
[[nodiscard]] constexpr bool randomize_step_hook(
    const ClassMethodHook hook) noexcept
{
  return hook >= ClassMethodHook::randomize_begin
      && hook <= ClassMethodHook::randomize_callback;
}

// The actual kind of a calling-scope value a randomize() with-clause reads
// (IEEE 1800-2017 18.7): its register is an actual after every argument, and
// the runtime binds its value into the call's inline constraints instead of
// passing it to the method.
inline constexpr std::uint8_t inline_value_actual_kind{8U};

struct ClassMethodCall {
  RegisterId destination{};
  RegisterId receiver{};
  std::string method_identity;
  std::vector<RegisterId> actuals;
  std::vector<std::string> actual_names;
  std::vector<std::uint8_t> actual_directions;
  std::uint32_t result_width{};
  bool virtual_dispatch{true};
  std::vector<std::uint8_t> actual_kinds{};
  std::vector<SystemVerilogConstraintTemplate> inline_constraints { };
  ClassMethodHook hook { ClassMethodHook::none };
  // `randomize(null)`: the checker call that randomizes no property (IEEE
  // 1800-2023 18.11.1).
  bool randomize_no_properties { };
};

struct ClassStaticPropertyRead {
  RegisterId destination{};
  std::string property_identity;
  std::uint32_t width{};
};

struct ClassStaticPropertyWrite {
  RegisterId source{};
  std::string property_identity;
};

struct ClassStaticMethodCall {
  RegisterId destination{};
  std::string method_identity;
  std::vector<RegisterId> actuals;
  std::vector<std::string> actual_names;
  std::vector<std::uint8_t> actual_directions;
  std::uint32_t result_width{};
  std::vector<std::uint8_t> actual_kinds{};
  /// The class specialization a static class method runs in: each
  /// specialization of a parameterized class has static properties and
  /// static methods of its own (IEEE 1800-2023 8.25.1). Empty for a call
  /// that is not one of a class's static methods.
  std::string class_specialization{};
  /// The access of a static string property (`string_property_read` or
  /// `string_property_write`), whose method identity is the property;
  /// `none` for a method or an imported function.
  ClassMethodHook hook{ClassMethodHook::none};
};

/// A dynamic array or queue of strings as the one string actual of an
/// open-array formal (an actual of kind 4, in, or 5, in and out; IEEE
/// 1800-2023 35.5.6.1, H.7.4, H.12): the element count in decimal, then each
/// element after a NUL, which a SystemVerilog string cannot hold (6.16).
[[nodiscard]] inline std::string encode_dpi_string_array(
    const std::vector<std::string>& elements)
{
  std::string text = std::to_string(elements.size());
  for (const auto& element : elements) {
    text.push_back('\0');
    text += element;
  }
  return text;
}

[[nodiscard]] inline std::vector<std::string> decode_dpi_string_array(
    const std::string_view text)
{
  std::vector<std::string> elements;
  auto position = text.find('\0');
  if (position == std::string_view::npos) {
    return elements;
  }
  while (position != std::string_view::npos) {
    const auto next = text.find('\0', position + 1U);
    elements.emplace_back(text.substr(
        position + 1U,
        next == std::string_view::npos ? std::string_view::npos
                                       : next - position - 1U));
    position = next;
  }
  return elements;
}

// Imported DPI functions intentionally use the existing mutable static-call
// boundary. This keeps native/interpreter handoff and copy-out semantics in a
// single SimIR operation while reserving an identity namespace that cannot
// collide with a SystemVerilog class method.
inline constexpr std::string_view systemverilog_dpi_function_prefix {
    // ITEM46-LEFT: the DPI import identity is a text descriptor parsed by the DPI service, the kernel plan and the VPI and VHPI loaders; a typed form needs a descriptor on the call operation
    "@dpi:" };

/// The most formals and argument-buffer words (eight bytes each) a planned
/// DPI call takes; a site beyond them is generic.
inline constexpr std::size_t dpi_planned_argument_limit{16};
inline constexpr std::size_t dpi_planned_buffer_words{64};

/// One argument of an imported DPI call, planned when the kernel image is
/// built (IEEE 1800-2023 35.5, H.7.4, H.10.1). `code` is the argument's
/// signature letter: c C s S i I l L (byte, shortint, int, longint, signed
/// or unsigned), p (chandle), d (real), f (shortreal), z (string), b (bit),
/// g (logic), B and V (packed bit and logic vectors, svBitVecVal and
/// svLogicVecVal). `offset` and `words` place the argument's C value in the
/// call's argument buffer, in eight-byte words.
struct DpiArgumentPlan {
  std::uint8_t code{};
  bool reference{};
  /// An output or inout formal: `reference`, or a vector, which passes by
  /// pointer in every direction and so carries no `&` (H.8.7 to H.8.10).
  bool copy_out{};
  std::uint32_t width{};
  RegisterId reg{};
  std::uint32_t offset{};
  std::uint32_t words{};
};

/// An imported DPI call site, fixed when the kernel image is built: the
/// import's identity and C signature, the argument directions and kinds of
/// the call operation, and, for a site whose arguments are all of the types
/// above (`generic` false), one plan per formal. A generic site (open
/// arrays, structures, string arrays, a context import that serves exports)
/// is called through the actual vectors.
struct DpiSitePlan {
  /// The call's identity without the DPI prefix, as the call hooks take it.
  std::string identity;
  std::string c_name;
  /// `<arguments>:<result>[@c]`.
  std::string signature;
  bool context{};
  bool generic{true};
  std::uint8_t result{};
  std::uint32_t result_width{};
  /// The words of the argument buffer.
  std::uint32_t buffer_words{};
  /// The string register a string result returns in (the last actual).
  StringRegisterId result_text{};
  std::vector<std::uint8_t> directions;
  std::vector<std::uint8_t> kinds;
  std::vector<DpiArgumentPlan> arguments;
};

/// The registers a planned DPI call reads its arguments from and writes its
/// copy-out values and result to.
class DpiFrame {
public:
  virtual ~DpiFrame() = default;
  [[nodiscard]] virtual const PackedLogic4& read(RegisterId id) = 0;
  virtual void write(RegisterId id, PackedLogic4 value) = 0;
  [[nodiscard]] virtual const std::string& read_text(StringRegisterId id) = 0;
  virtual void write_text(StringRegisterId id, std::string value) = 0;
};
