// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/design_artifact.hpp"

#include <boost/pfr/core.hpp>

#include <array>
#include <bit>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::app {
namespace {

constexpr std::size_t kMaximumNesting = 1024;
constexpr std::string_view kCode = "FSIM-ART-0013";

template <typename T>
struct IsVector : std::false_type {};
template <typename T, typename Allocator>
struct IsVector<std::vector<T, Allocator>> : std::true_type {};

template <typename T>
struct IsOptional : std::false_type {};
template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <typename T>
struct IsVariant : std::false_type {};
template <typename... Values>
struct IsVariant<std::variant<Values...>> : std::true_type {};

template <typename T>
struct IsPair : std::false_type {};
template <typename First, typename Second>
struct IsPair<std::pair<First, Second>> : std::true_type {};

template <typename T>
struct IsArray : std::false_type {};
template <typename Value, std::size_t Size>
struct IsArray<std::array<Value, Size>> : std::true_type {};

template <typename T>
struct IsSharedPtr : std::false_type {};
template <typename T>
struct IsSharedPtr<std::shared_ptr<T>> : std::true_type {};

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::Expression>
auto archive_fields(T& value) {
  return std::tie(
      value.kind, value.text, value.operands, value.span,
      value.aggregate_choices, value.aggregate_choice_expressions,
      value.nominal_type, value.decoded_string, value.call_argument_names);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::Type>
auto archive_fields(T& value) {
  return std::tie(
      value.domain, value.spelling, value.packed_range, value.is_signed,
      value.packed_range_expression, value.named_type, value.named_type_span,
      value.nominal_type, value.vhdl_type_declaration,
      value.vhdl_resolution_function, value.enumeration_literals,
      value.enumeration_range, value.enumeration_range_expression,
      value.enumeration_base_range, value.enumeration_base_range_expression,
      value.packed_members, value.packed_aggregate, value.integer_range,
      value.integer_range_expression, value.integer_base_range,
      value.integer_base_range_expression, value.discrete_range_expression,
      value.vhdl_array, value.vhdl_access, value.vhdl_file,
      value.vhdl_physical, value.vhdl_protected,
      value.vhdl_array_constraints, value.systemverilog_container);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::SignalDeclaration>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.direction, value.is_port, value.span,
      value.net_delay, value.interface_type, value.modport,
      value.default_value);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::VariableDeclaration>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.initializer, value.span,
      value.vhdl_shared, value.vhdl_file, value.vhdl_file_open_kind);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::FunctionArgument>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.direction, value.span, value.reference,
      value.default_value, value.vhdl_file);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::ParameterOverride>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.value, value.span, value.type_value,
      value.default_box);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::ParameterDeclaration>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.default_value, value.local, value.span,
      value.kind, value.default_type, value.function_profile,
      value.procedure_profile, value.package_profile, value.object_class,
      value.direction);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::TaskArgument>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.direction, value.span, value.reference,
      value.default_value);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, runtime::simir::FileOpen>
auto archive_fields(T& value) {
  return std::tie(
      value.destination, value.path, value.mode, value.status, value.vhdl);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, runtime::simir::FileClose>
auto archive_fields(T& value) {
  return std::tie(value.handle, value.clear_handle, value.ignore_zero);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, runtime::simir::FileEndOfFile>
auto archive_fields(T& value) {
  return std::tie(value.destination, value.handle, value.lookahead);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, runtime::simir::FileScan>
auto archive_fields(T& value) {
  return std::tie(
      value.destination, value.handle, value.source, value.string_source,
      value.conversions, value.trailing_text, value.require_assignments,
      value.success, value.consume_string_source);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, runtime::simir::WaitOn>
auto archive_fields(T& value) {
  return std::tie(
      value.signals, value.edges, value.timeout, value.timeout_result,
      value.timeout_origin);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, runtime::simir::DebugPoint>
auto archive_fields(T& value) {
  return std::tie(value.kind, value.source, value.scope);
}

template <typename T>
concept DenseId = requires(T value, std::uint32_t index) {
  { value.valid() } -> std::same_as<bool>;
  { value.value() } -> std::same_as<std::uint32_t>;
  { T::from_index(index) } -> std::same_as<T>;
};

template <typename T>
concept StorageWrapper = requires(T value) {
  value.storage;
} && IsVariant<std::remove_cvref_t<decltype(std::declval<T>().storage)>>::value;

class Writer {
 public:
  void raw(const std::string_view bytes) { bytes_.append(bytes); }
  void u64(const std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
      bytes_.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
  }

  template <typename T>
  void write(const T& value) {
    if (!failure_.empty()) {
      return;
    }
    if (++depth_ > kMaximumNesting) {
      --depth_;
      failure_ = "design state exceeds the safe structural nesting depth";
      return;
    }
    using Value = std::remove_cv_t<T>;
    if constexpr (std::same_as<Value, bool>) {
      bytes_.push_back(value ? '\1' : '\0');
    } else if constexpr (std::is_enum_v<Value>) {
      write(static_cast<std::underlying_type_t<Value>>(value));
    } else if constexpr (std::is_integral_v<Value>) {
      if constexpr (std::is_signed_v<Value>) {
        u64(std::bit_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)));
      } else {
        u64(static_cast<std::uint64_t>(value));
      }
    } else if constexpr (std::same_as<Value, std::string>) {
      u64(value.size());
      raw(value);
    } else if constexpr (std::same_as<Value, std::filesystem::path>) {
      write(value.generic_string());
    } else if constexpr (std::same_as<Value, runtime::PackedLogic4>) {
      write(value.is_logic9());
      write(value.to_msb_string());
    } else if constexpr (DenseId<Value>) {
      write(value.value());
    } else if constexpr (IsVector<Value>::value) {
      u64(value.size());
      for (const auto& item : value) {
        write(item);
      }
    } else if constexpr (IsOptional<Value>::value) {
      write(value.has_value());
      if (value) {
        write(*value);
      }
    } else if constexpr (IsVariant<Value>::value) {
      u64(value.index());
      std::visit([&](const auto& item) { write(item); }, value);
    } else if constexpr (IsPair<Value>::value) {
      write(value.first);
      write(value.second);
    } else if constexpr (IsArray<Value>::value) {
      for (const auto& item : value) {
        write(item);
      }
    } else if constexpr (IsSharedPtr<Value>::value) {
      write(static_cast<bool>(value));
      if (value) {
        write(*value);
      }
    } else if constexpr (StorageWrapper<Value>) {
      write(value.storage);
    } else if constexpr (requires { archive_fields(value); }) {
      std::apply(
          [&](const auto&... fields) { (write(fields), ...); },
          archive_fields(value));
    } else if constexpr (std::is_aggregate_v<Value>) {
      boost::pfr::for_each_field(
          value, [&](const auto& field) { write(field); });
    } else {
      static_assert(sizeof(Value) == 0, "unsupported design-state field");
    }
    --depth_;
  }

  std::string finish() && { return std::move(bytes_); }
  const std::string& failure() const noexcept { return failure_; }

 private:
  std::string bytes_;
  std::size_t depth_{};
  std::string failure_;
};

class Reader {
 public:
  explicit Reader(const std::string_view bytes) : bytes_(bytes) {}

  bool raw(const std::string_view expected) {
    if (remaining() < expected.size()
        || bytes_.substr(position_, expected.size()) != expected) {
      return fail("design state has an invalid magic header");
    }
    position_ += expected.size();
    return true;
  }

  bool u64(std::uint64_t& value) {
    if (remaining() < 8) {
      return fail("design state is truncated");
    }
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
      value |= static_cast<std::uint64_t>(
          static_cast<unsigned char>(bytes_[position_++])) << shift;
    }
    return true;
  }

  template <std::size_t Index = 0, typename... Values>
  bool read_variant(
      std::variant<Values...>& value,
      const std::size_t selected) {
    if constexpr (Index == sizeof...(Values)) {
      return fail("design state contains an invalid variant alternative");
    } else {
      if (selected == Index) {
        value.template emplace<Index>();
        return read(std::get<Index>(value));
      }
      return read_variant<Index + 1>(value, selected);
    }
  }

  template <typename T>
  bool read(T& value) {
    using Value = std::remove_cv_t<T>;
    if (!enter()) {
      return false;
    }
    const bool result = [&]() {
      if constexpr (std::same_as<Value, bool>) {
        if (remaining() == 0
            || static_cast<unsigned char>(bytes_[position_]) > 1) {
          return fail("design state contains an invalid boolean");
        }
        value = bytes_[position_++] != 0;
        return true;
      } else if constexpr (std::is_enum_v<Value>) {
        std::underlying_type_t<Value> decoded{};
        if (!read(decoded)) {
          return false;
        }
        value = static_cast<Value>(decoded);
        return true;
      } else if constexpr (std::is_integral_v<Value>) {
        std::uint64_t encoded{};
        if (!u64(encoded)) {
          return false;
        }
        if constexpr (std::is_signed_v<Value>) {
          const auto decoded = std::bit_cast<std::int64_t>(encoded);
          if (decoded < std::numeric_limits<Value>::min()
              || decoded > std::numeric_limits<Value>::max()) {
            return fail("design state integer is outside the host range");
          }
          value = static_cast<Value>(decoded);
        } else {
          if (encoded > std::numeric_limits<Value>::max()) {
            return fail("design state integer is outside the host range");
          }
          value = static_cast<Value>(encoded);
        }
        return true;
      } else if constexpr (std::same_as<Value, std::string>) {
        std::uint64_t size{};
        if (!u64(size) || size > remaining()) {
          return fail("design state string exceeds the payload");
        }
        value.assign(bytes_.substr(position_, static_cast<std::size_t>(size)));
        position_ += static_cast<std::size_t>(size);
        return true;
      } else if constexpr (std::same_as<Value, std::filesystem::path>) {
        std::string spelling;
        if (!read(spelling)) {
          return false;
        }
        value = std::filesystem::path{spelling};
        return true;
      } else if constexpr (std::same_as<Value, runtime::PackedLogic4>) {
        bool logic9{};
        std::string spelling;
        if (!read(logic9) || !read(spelling)) {
          return false;
        }
        value = logic9
            ? runtime::PackedLogic4::from_logic9_msb_string(spelling)
            : runtime::PackedLogic4::from_msb_string(spelling);
        return true;
      } else if constexpr (DenseId<Value>) {
        std::uint32_t index{};
        if (!read(index)) {
          return false;
        }
        value = Value::from_index(index);
        return true;
      } else if constexpr (IsVector<Value>::value) {
        std::uint64_t size{};
        if (!u64(size) || size > static_cast<std::uint64_t>(remaining()) + 1U
            || size > value.max_size()) {
          return fail("design state vector exceeds the payload");
        }
        value.clear();
        value.reserve(static_cast<std::size_t>(size));
        for (std::uint64_t index = 0; index < size; ++index) {
          value.emplace_back();
          if (!read(value.back())) {
            return false;
          }
        }
        return true;
      } else if constexpr (IsOptional<Value>::value) {
        bool present{};
        if (!read(present)) {
          return false;
        }
        if (!present) {
          value.reset();
          return true;
        }
        value.emplace();
        return read(*value);
      } else if constexpr (IsVariant<Value>::value) {
        std::uint64_t selected{};
        return u64(selected)
            && selected < std::variant_size_v<Value>
            && read_variant(value, static_cast<std::size_t>(selected));
      } else if constexpr (IsPair<Value>::value) {
        return read(value.first) && read(value.second);
      } else if constexpr (IsArray<Value>::value) {
        for (auto& item : value) {
          if (!read(item)) {
            return false;
          }
        }
        return true;
      } else if constexpr (IsSharedPtr<Value>::value) {
        bool present{};
        if (!read(present)) {
          return false;
        }
        if (!present) {
          value.reset();
          return true;
        }
        value = std::make_shared<typename Value::element_type>();
        return read(*value);
      } else if constexpr (StorageWrapper<Value>) {
        return read(value.storage);
      } else if constexpr (requires { archive_fields(value); }) {
        bool ok = true;
        std::apply(
            [&](auto&... fields) { ((ok = ok && read(fields)), ...); },
            archive_fields(value));
        return ok;
      } else if constexpr (std::is_aggregate_v<Value>) {
        bool ok = true;
        boost::pfr::for_each_field(
            value, [&](auto& field) { ok = ok && read(field); });
        return ok;
      } else {
        static_assert(sizeof(Value) == 0, "unsupported design-state field");
      }
    }();
    leave();
    return result;
  }

  std::size_t remaining() const noexcept { return bytes_.size() - position_; }
  const std::string& failure() const noexcept { return failure_; }

 private:
  bool enter() {
    if (++depth_ > kMaximumNesting) {
      --depth_;
      return fail("design state exceeds the safe structural nesting depth");
    }
    return true;
  }
  void leave() noexcept { --depth_; }
  bool fail(std::string message) {
    if (failure_.empty()) {
      failure_ = std::move(message);
    }
    return false;
  }

  std::string_view bytes_;
  std::size_t position_{};
  std::size_t depth_{};
  std::string failure_;
};

struct DesignIrRecords {
  std::string top;
  std::vector<std::string> roots;
  std::vector<semantic::design::Specialization> specializations;
  std::vector<semantic::design::InstanceOccurrence> instances;
  std::vector<semantic::design::Object> objects;
  std::vector<semantic::design::Port> ports;
  std::vector<semantic::design::ProcessOccurrence> processes;
  std::vector<semantic::design::Sensitivity> sensitivities;
  std::vector<semantic::design::Driver> drivers;
  std::vector<semantic::design::Transaction> transactions;
  std::vector<semantic::design::Conversion> conversions;
  std::vector<semantic::design::Boundary> boundaries;
};

template <typename Value>
std::optional<std::string> serialize(
    const std::string_view magic,
    const std::uint32_t schema,
    const Value& value,
    diagnostic::Engine& diagnostics) {
  Writer writer;
  writer.raw(magic);
  writer.write(schema);
  writer.write(value);
  if (!writer.failure().empty()) {
    diagnostics.error(std::string{kCode}, writer.failure());
    return std::nullopt;
  }
  return std::move(writer).finish();
}

template <typename Value>
std::optional<Value> deserialize(
    const std::string_view magic,
    const std::uint32_t expected_schema,
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  Reader reader{bytes};
  std::uint32_t schema{};
  Value value;
  if (!reader.raw(magic) || !reader.read(schema)
      || schema != expected_schema || !reader.read(value)
      || reader.remaining() != 0) {
    auto message = reader.failure();
    if (message.empty() && schema != expected_schema) {
      message = "unsupported design-state schema " + std::to_string(schema);
    } else if (message.empty()) {
      message = "design state contains trailing bytes";
    }
    diagnostics.error(
        std::string{kCode}, std::move(message),
        {std::move(source_name), {1, 1, 0}, {1, 1, 0}});
    return std::nullopt;
  }
  return value;
}

bool portable_semantics(
    const semantic::ModelRecords& records,
    diagnostic::Engine& diagnostics) {
  for (const auto& file : records.source_files) {
    if (std::filesystem::path(file.physical_name).is_absolute()) {
      diagnostics.error(
          std::string{kCode},
          "semantic state contains a producer-absolute source path: "
              + file.physical_name);
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<std::string> serialize_runtime_state(
    const elaboration::ElaboratedDesign& design,
    diagnostic::Engine& diagnostics) {
  return serialize(
      "FSIMRUN1", kRuntimeStateSchema, design.state(), diagnostics);
}

std::optional<elaboration::ElaboratedDesign> deserialize_runtime_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  auto state = deserialize<elaboration::ElaboratedDesignState>(
      "FSIMRUN1", kRuntimeStateSchema, bytes, std::move(source_name),
      diagnostics);
  if (!state) {
    return std::nullopt;
  }
  auto design = elaboration::ElaboratedDesign::from_state(std::move(*state));
  if (!design) {
    diagnostics.error(
        std::string{kCode},
        "runtime state is structurally invalid");
    return std::nullopt;
  }
  return design;
}

std::optional<std::string> serialize_semantic_state(
    const semantic::Model& model,
    diagnostic::Engine& diagnostics) {
  auto records = model.records();
  if (!model.valid() || !portable_semantics(records, diagnostics)) {
    return std::nullopt;
  }
  return serialize(
      "FSIMSEM1", kSemanticStateSchema, records, diagnostics);
}

std::optional<semantic::Model> deserialize_semantic_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  auto records = deserialize<semantic::ModelRecords>(
      "FSIMSEM1", kSemanticStateSchema, bytes, std::move(source_name),
      diagnostics);
  if (!records || !portable_semantics(*records, diagnostics)) {
    return std::nullopt;
  }
  auto model = semantic::Model::from_records(std::move(*records));
  if (!model) {
    diagnostics.error(std::string{kCode}, "semantic state is structurally invalid");
  }
  return model;
}

std::optional<std::string> serialize_design_ir_state(
    const semantic::design::DesignIr& design,
    diagnostic::Engine& diagnostics) {
  if (!design.valid()) {
    diagnostics.error(std::string{kCode}, "cannot serialize invalid DesignIR");
    return std::nullopt;
  }
  const DesignIrRecords records{
      design.top(), design.roots(), design.specializations(),
      design.instances(), design.objects(), design.ports(), design.processes(),
      design.sensitivities(), design.drivers(), design.transactions(),
      design.conversions(), design.boundaries()};
  return serialize(
      "FSIMDIR1", kDesignIrStateSchema, records, diagnostics);
}

std::optional<semantic::design::DesignIr> deserialize_design_ir_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  auto records = deserialize<DesignIrRecords>(
      "FSIMDIR1", kDesignIrStateSchema, bytes, std::move(source_name),
      diagnostics);
  if (!records) {
    return std::nullopt;
  }
  semantic::design::DesignIr design;
  design.mutable_top() = std::move(records->top);
  design.mutable_roots() = std::move(records->roots);
  design.mutable_specializations() = std::move(records->specializations);
  design.mutable_instances() = std::move(records->instances);
  design.mutable_objects() = std::move(records->objects);
  design.mutable_ports() = std::move(records->ports);
  design.mutable_processes() = std::move(records->processes);
  design.mutable_sensitivities() = std::move(records->sensitivities);
  design.mutable_drivers() = std::move(records->drivers);
  design.mutable_transactions() = std::move(records->transactions);
  design.mutable_conversions() = std::move(records->conversions);
  design.mutable_boundaries() = std::move(records->boundaries);
  if (!design.valid()) {
    diagnostics.error(std::string{kCode}, "DesignIR state is structurally invalid");
    return std::nullopt;
  }
  return design;
}

}  // namespace fsim::app
