// SPDX-License-Identifier: Apache-2.0
#pragma once

struct ClassAllocate {
  RegisterId destination{};
  std::string specialization_identity;
  std::string declared_type;
  std::vector<RegisterId> constructor_actuals;
  std::vector<std::uint8_t> constructor_actual_kinds;
  std::vector<std::string> constructor_actual_names;
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
};

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
