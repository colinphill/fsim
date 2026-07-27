// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit.hpp"

#include "fsim/compiler/object_cache.hpp"

#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::compiler {
namespace {

using runtime::simir::Assert;
using runtime::simir::Binary;
using runtime::simir::BinaryOperator;
using runtime::simir::Branch;
using runtime::simir::Concatenate;
using runtime::simir::ConditionalSelect;
using runtime::simir::CopyRegister;
using runtime::simir::DebugPoint;
using runtime::simir::EdgeKind;
using runtime::simir::Extract;
using runtime::simir::Halt;
using runtime::simir::InstructionIndex;
using runtime::simir::Jump;
using runtime::simir::LoadConstant;
using runtime::simir::LogicalBinary;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::LogicalNot;
using runtime::simir::Operation;
using runtime::simir::Process;
using runtime::simir::ReadSignal;
using runtime::simir::Reduction;
using runtime::simir::ReductionOperator;
using runtime::simir::RegisterId;
using runtime::simir::Shift;
using runtime::simir::ShiftOperator;
using runtime::simir::Stop;
using runtime::simir::UnaryNot;
using runtime::simir::UnknownBranchPolicy;
using runtime::simir::WaitFor;
using runtime::simir::WaitOn;
using runtime::simir::WaitSensitivity;
using runtime::simir::WriteAfter;
using runtime::simir::WriteBlocking;
using runtime::simir::WriteUpdate;
using runtime::simir::Yield;

using NativeProcess = fsim_jit_process_v1;

constexpr std::string_view kNativeObjectCacheSchema =
    "fsim-llvm-native-object-v3";

static_assert(std::is_standard_layout_v<fsim_jit_runtime_v1>);
static_assert(std::is_standard_layout_v<fsim_jit_frame_v1>);
static_assert(std::is_standard_layout_v<fsim_jit_resume_result_v1>);
static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(std::uint64_t) == 8);
static_assert(offsetof(fsim_jit_runtime_v1, abi_version) == 0);
static_assert(offsetof(fsim_jit_runtime_v1, struct_size) == 4);
static_assert(offsetof(fsim_jit_runtime_v1, context) == 8);
static_assert(offsetof(fsim_jit_runtime_v1, read_signal) == 16);
static_assert(offsetof(fsim_jit_runtime_v1, write_signal) == 24);
static_assert(offsetof(fsim_jit_runtime_v1, assert_failed) == 32);
static_assert(offsetof(fsim_jit_runtime_v1, write_update) == 40);
static_assert(offsetof(fsim_jit_runtime_v1, write_after) == 48);
static_assert(offsetof(fsim_jit_runtime_v1, flags) == 56);
static_assert(offsetof(fsim_jit_runtime_v1, reserved) == 60);
static_assert(sizeof(fsim_jit_runtime_v1) == 64);
static_assert(sizeof(fsim_jit_frame_v1) == 56);
static_assert(offsetof(fsim_jit_frame_v1, register_aval) == 40);
static_assert(offsetof(fsim_jit_frame_v1, register_bval) == 48);
static_assert(sizeof(fsim_jit_resume_result_v1) == 24);

constexpr auto kJitRuntimeV1PrefixSize =
    static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_update));

class PersistentLlvmObjectCache final : public llvm::ObjectCache {
public:
  PersistentLlvmObjectCache(std::filesystem::path root,
                            llvm::Triple target_triple)
      : storage_(std::move(root)), target_triple_(std::move(target_triple)) {}

  void notifyObjectCompiled(const llvm::Module *module,
                            const llvm::MemoryBufferRef object) override {
    if (module == nullptr ||
        !valid_cache_key(module->getModuleIdentifier())) {
      return;
    }
    try {
      const auto bytes = std::span<const std::byte>{
          reinterpret_cast<const std::byte *>(object.getBufferStart()),
          object.getBufferSize()};
      std::error_code error;
      if (storage_.store(module->getModuleIdentifier(), bytes, error)) {
        stores_.fetch_add(1, std::memory_order_relaxed);
      } else {
        store_failures_.fetch_add(1, std::memory_order_relaxed);
      }
    } catch (...) {
      store_failures_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  [[nodiscard]] std::unique_ptr<llvm::MemoryBuffer>
  getObject(const llvm::Module *module) override {
    if (module == nullptr ||
        !valid_cache_key(module->getModuleIdentifier())) {
      return nullptr;
    }
    try {
      std::error_code error;
      auto bytes = storage_.load(module->getModuleIdentifier(), error);
      if (!bytes) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        if (error &&
            error != std::errc::no_such_file_or_directory) {
          if (error == std::errc::illegal_byte_sequence) {
            rejected_entries_.fetch_add(1, std::memory_order_relaxed);
          } else {
            load_failures_.fetch_add(1, std::memory_order_relaxed);
          }
        }
        return nullptr;
      }
      if (!valid_native_object(*bytes)) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        rejected_entries_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
      }

      const auto data = llvm::StringRef{
          reinterpret_cast<const char *>(bytes->data()), bytes->size()};
      auto result = llvm::MemoryBuffer::getMemBufferCopy(
          data, module->getModuleIdentifier() + ".o");
      hits_.fetch_add(1, std::memory_order_relaxed);
      return result;
    } catch (...) {
      misses_.fetch_add(1, std::memory_order_relaxed);
      load_failures_.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
  }

  [[nodiscard]] LlvmJitCacheStatistics statistics() const noexcept {
    return {
        hits_.load(std::memory_order_relaxed),
        misses_.load(std::memory_order_relaxed),
        stores_.load(std::memory_order_relaxed),
        rejected_entries_.load(std::memory_order_relaxed),
        load_failures_.load(std::memory_order_relaxed),
        store_failures_.load(std::memory_order_relaxed),
    };
  }

private:
  [[nodiscard]] bool
  valid_native_object(const std::span<const std::byte> bytes) const {
    const auto data = llvm::StringRef{
        reinterpret_cast<const char *>(bytes.data()), bytes.size()};
    auto parsed = llvm::object::ObjectFile::createObjectFile(
        llvm::MemoryBufferRef{data, "fsim-cached-object"});
    if (!parsed) {
      llvm::consumeError(parsed.takeError());
      return false;
    }
    const auto object_triple = (*parsed)->makeTriple();
    return (*parsed)->isRelocatableObject() &&
           (*parsed)->getBytesInAddress() == sizeof(void *) &&
           (*parsed)->getArch() == target_triple_.getArch() &&
           object_triple.getObjectFormat() ==
               target_triple_.getObjectFormat();
  }

  fsim::compiler::ObjectCache storage_;
  llvm::Triple target_triple_;
  std::atomic_uint64_t hits_{};
  std::atomic_uint64_t misses_{};
  std::atomic_uint64_t stores_{};
  std::atomic_uint64_t rejected_entries_{};
  std::atomic_uint64_t load_failures_{};
  std::atomic_uint64_t store_failures_{};
};

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

[[nodiscard]] std::string llvm_error(llvm::Error error) {
  std::string message;
  llvm::raw_string_ostream stream(message);
  llvm::logAllUnhandledErrors(std::move(error), stream);
  stream.flush();
  return message;
}

template <typename T>
[[nodiscard]] T unwrap(llvm::Expected<T> expected,
                       const std::string_view action) {
  if (!expected) {
    throw LlvmJitError(std::string{action} + ": " +
                       llvm_error(expected.takeError()));
  }
  return std::move(*expected);
}

[[nodiscard]] bool valid_symbol(const std::string_view symbol) noexcept {
  if (symbol.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(symbol.front());
  if (std::isalpha(first) == 0 && symbol.front() != '_') {
    return false;
  }
  return std::all_of(symbol.begin() + 1, symbol.end(), [](const char value) {
    const auto character = static_cast<unsigned char>(value);
    return std::isalnum(character) != 0 || value == '_';
  });
}

[[nodiscard]] std::string instruction_error(const Process &process,
                                            const std::size_t instruction,
                                            const std::string_view message) {
  std::ostringstream result;
  result << "cannot JIT SimIR process " << process.id << " ('" << process.name
         << "'), instruction " << instruction << ": " << message;
  return result.str();
}

[[nodiscard]] std::string_view generated_runtime_error_reason(
    const JitGeneratedRuntimeErrorReason reason) noexcept {
  switch (reason) {
  case JitGeneratedRuntimeErrorReason::unknown_branch_condition:
    return "branch condition is unknown or high impedance";
  }
  return "unknown generated runtime error";
}

[[nodiscard]] std::string generated_runtime_error_message(
    const std::uint32_t instruction,
    const JitGeneratedRuntimeErrorReason reason) {
  return "generated SimIR process, instruction " +
         std::to_string(instruction) + ": " +
         std::string{generated_runtime_error_reason(reason)};
}

[[noreturn]] void reject(const Process &process, const std::size_t instruction,
                         const std::string_view message) {
  throw LlvmJitError(instruction_error(process, instruction, message));
}

[[noreturn]] void
reject_unsupported(const Process &process, const std::size_t instruction,
                   const std::string_view message) {
  throw LlvmJitUnsupportedError(
      instruction_error(process, instruction, message));
}

struct ValidatedProcess {
  std::vector<std::uint32_t> register_widths;
  bool requires_resume{};
  bool uses_write_update{};
  bool uses_write_after{};
  bool uses_debug_points{};
};

[[nodiscard]] ValidatedProcess
validate_process(const Process &process,
                 const std::span<const std::uint32_t> signal_widths) {
  if (process.operations.empty()) {
    throw LlvmJitError("cannot JIT an empty SimIR process");
  }
  if (process.operations.size() >
      static_cast<std::size_t>(
          std::numeric_limits<InstructionIndex>::max())) {
    throw LlvmJitUnsupportedError(
        "SimIR process has too many instructions for the resumable JIT ABI");
  }
  if (process.register_count >
      static_cast<std::size_t>(std::numeric_limits<RegisterId>::max())) {
    throw LlvmJitUnsupportedError(
        "SimIR process has too many registers for the JIT ABI");
  }

  ValidatedProcess result;
  result.register_widths.resize(process.register_count);
  std::vector<RegisterId> parents(process.register_count);
  std::vector<std::size_t> root_widths(process.register_count);
  for (std::size_t index = 0; index < parents.size(); ++index) {
    parents[index] = static_cast<RegisterId>(index);
  }
  std::vector<bool> defined(process.register_count);
  std::vector<std::optional<RegisterId>> instruction_definitions(
      process.operations.size());
  std::vector<std::vector<RegisterId>> instruction_uses(
      process.operations.size());
  std::optional<std::pair<std::size_t, std::string>> unsupported;

  const auto record_unsupported =
      [&](const std::size_t instruction, const std::string_view message) {
        if (!unsupported) {
          unsupported.emplace(instruction, message);
        }
      };

  const auto referenced_signal_width =
      [&](const std::uint32_t signal,
          const std::size_t instruction) -> std::uint32_t {
    if (signal >= signal_widths.size()) {
      reject(process, instruction, "signal ID is outside signal_widths");
    }
    const auto width = signal_widths[signal];
    if (width == 0) {
      reject(process, instruction, "signal width must be greater than zero");
    }
    return width;
  };

  const auto signal_width =
      [&](const std::uint32_t signal,
          const std::size_t instruction) -> std::uint32_t {
    const auto width = referenced_signal_width(signal, instruction);
    if (width > 64) {
      record_unsupported(
          instruction,
          "the LLVM scalar subset requires signal widths in [1, 64]");
    }
    return width;
  };

  const auto validate_register =
      [&](const RegisterId id, const std::size_t instruction,
          const std::string_view role) {
    if (id >= process.register_count) {
      reject(process, instruction,
             std::string{role} + " register ID is out of range");
    }
  };

  const auto find_root = [&](const RegisterId id) {
    auto root = id;
    while (parents[root] != root) {
      root = parents[root];
    }
    auto current = id;
    while (parents[current] != current) {
      const auto next = parents[current];
      parents[current] = root;
      current = next;
    }
    return root;
  };

  const auto constrain_width =
      [&](const RegisterId id, const std::size_t width,
          const std::size_t instruction) {
    validate_register(id, instruction, "constrained");
    if (width == 0) {
      reject(process, instruction, "register width must be greater than zero");
    }
    if (width > 64) {
      record_unsupported(
          instruction,
          "the LLVM scalar subset requires register widths in [1, 64]");
    }
    const auto root = find_root(id);
    if (root_widths[root] != 0 && root_widths[root] != width) {
      reject(process, instruction,
             "register width constraints are inconsistent");
    }
    root_widths[root] = width;
  };

  const auto unify_registers =
      [&](const RegisterId lhs, const RegisterId rhs,
          const std::size_t instruction) {
    validate_register(lhs, instruction, "left");
    validate_register(rhs, instruction, "right");
    auto left_root = find_root(lhs);
    auto right_root = find_root(rhs);
    if (left_root == right_root) {
      return;
    }
    if (root_widths[left_root] != 0 && root_widths[right_root] != 0 &&
        root_widths[left_root] != root_widths[right_root]) {
      reject(process, instruction,
             "register width constraints are inconsistent");
    }
    parents[right_root] = left_root;
    if (root_widths[left_root] == 0) {
      root_widths[left_root] = root_widths[right_root];
    }
  };

  const auto record_use = [&](const RegisterId id,
                              const std::size_t instruction) {
    validate_register(id, instruction, "source");
    instruction_uses[instruction].push_back(id);
  };

  const auto record_definition = [&](const RegisterId id,
                                     const std::size_t instruction) {
    validate_register(id, instruction, "destination");
    if (instruction_definitions[instruction]) {
      reject(process, instruction,
             "an instruction cannot define more than one register");
    }
    instruction_definitions[instruction] = id;
    defined[id] = true;
  };

  const auto validate_target = [&](const InstructionIndex target,
                                   const std::size_t instruction,
                                   const std::string_view kind) {
    if (target >= process.operations.size()) {
      reject(process, instruction,
             std::string{kind} + " target is outside the operation stream");
    }
  };

  const auto first_wait_sensitivity = std::find_if(
      process.operations.begin(), process.operations.end(),
      [](const Operation &operation) {
        return std::holds_alternative<WaitSensitivity>(operation);
      });
  const auto sensitivity_instruction =
      first_wait_sensitivity == process.operations.end()
          ? std::size_t{0}
          : static_cast<std::size_t>(
                std::distance(
                    process.operations.begin(), first_wait_sensitivity));
  for (const auto sensitivity : process.static_sensitivity) {
    const auto width = referenced_signal_width(
        sensitivity.signal, sensitivity_instruction);
    switch (sensitivity.edge) {
    case EdgeKind::any:
      break;
    case EdgeKind::posedge:
    case EdgeKind::negedge:
      if (width != 1) {
        reject(
            process, sensitivity_instruction,
            "edge sensitivity requires a scalar signal");
      }
      break;
    default:
      reject(
          process, sensitivity_instruction,
          "static sensitivity has an invalid edge kind");
    }
  }

  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    std::visit(
        Overloaded{
            [&](const LoadConstant &operation) {
              if (operation.value.width() == 0) {
                reject(process, index,
                       "LoadConstant width must be greater than zero");
              }
              if (operation.value.width() > 64) {
                record_unsupported(
                    index,
                    "LoadConstant width is outside the supported [1, 64] "
                    "range");
              }
              record_definition(operation.destination, index);
              constrain_width(operation.destination, operation.value.width(),
                              index);
            },
            [&](const ReadSignal &operation) {
              record_definition(operation.destination, index);
              constrain_width(operation.destination,
                              signal_width(operation.signal, index), index);
            },
            [&](const CopyRegister& operation) {
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              unify_registers(
                  operation.destination, operation.source, index);
            },
            [&](const UnaryNot &operation) {
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              unify_registers(operation.destination, operation.source, index);
            },
            [&](const LogicalNot& operation) {
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const LogicalBinary& operation) {
              switch (operation.operation) {
              case LogicalBinaryOperator::logical_and:
              case LogicalBinaryOperator::logical_or:
                break;
              default:
                reject(
                    process, index,
                    "LogicalBinary has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.lhs, index);
              record_use(operation.rhs, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const Reduction& operation) {
              switch (operation.operation) {
              case ReductionOperator::bit_and:
              case ReductionOperator::bit_or:
              case ReductionOperator::bit_xor:
                break;
              default:
                reject(
                    process, index,
                    "Reduction has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const Shift& operation) {
              switch (operation.operation) {
              case ShiftOperator::logical_left:
              case ShiftOperator::logical_right:
                break;
              default:
                reject(
                    process, index, "Shift has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.value, index);
              record_use(operation.amount, index);
              unify_registers(
                  operation.destination, operation.value, index);
            },
            [&](const Extract& operation) {
              if (operation.width == 0) {
                reject(
                    process, index,
                    "Extract width must be greater than zero");
              }
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(
                  operation.destination, operation.width, index);
            },
            [&](const Concatenate& operation) {
              if (operation.operands.empty()) {
                reject(
                    process, index,
                    "Concatenate requires at least one operand");
              }
              if (operation.width == 0) {
                reject(
                    process, index,
                    "Concatenate width must be greater than zero");
              }
              record_definition(operation.destination, index);
              for (const auto operand : operation.operands) {
                record_use(operand, index);
              }
              constrain_width(
                  operation.destination, operation.width, index);
            },
            [&](const Binary &operation) {
              switch (operation.operation) {
              case BinaryOperator::bit_and:
              case BinaryOperator::bit_or:
              case BinaryOperator::bit_xor:
              case BinaryOperator::add_unsigned:
              case BinaryOperator::subtract_unsigned:
              case BinaryOperator::multiply_unsigned:
              case BinaryOperator::divide_unsigned:
              case BinaryOperator::modulo_unsigned:
              case BinaryOperator::equal:
              case BinaryOperator::case_equal:
              case BinaryOperator::not_equal:
              case BinaryOperator::less_unsigned:
              case BinaryOperator::less_equal_unsigned:
              case BinaryOperator::greater_unsigned:
              case BinaryOperator::greater_equal_unsigned:
                break;
              default:
                reject(process, index,
                       "Binary has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.lhs, index);
              record_use(operation.rhs, index);
              unify_registers(operation.lhs, operation.rhs, index);
              if (operation.operation == BinaryOperator::equal
                  || operation.operation == BinaryOperator::case_equal
                  || operation.operation == BinaryOperator::not_equal
                  || operation.operation
                      == BinaryOperator::less_unsigned
                  || operation.operation
                      == BinaryOperator::less_equal_unsigned
                  || operation.operation
                      == BinaryOperator::greater_unsigned
                  || operation.operation
                      == BinaryOperator::greater_equal_unsigned) {
                constrain_width(operation.destination, 1U, index);
              } else {
                unify_registers(operation.destination, operation.lhs, index);
              }
            },
            [&](const ConditionalSelect& operation) {
              record_definition(operation.destination, index);
              record_use(operation.condition, index);
              record_use(operation.when_true, index);
              record_use(operation.when_false, index);
              constrain_width(operation.condition, 1U, index);
              unify_registers(
                  operation.when_true, operation.when_false, index);
              unify_registers(
                  operation.destination, operation.when_true, index);
            },
            [&](const WriteBlocking &operation) {
              record_use(operation.source, index);
              constrain_width(operation.source,
                              signal_width(operation.signal, index), index);
            },
            [&](const Assert &operation) {
              record_use(operation.condition, index);
              constrain_width(operation.condition, 1U, index);
            },
            [&](const DebugPoint&) {
              result.uses_debug_points = true;
            },
            [&](const Jump &operation) {
              validate_target(operation.target, index, "jump");
            },
            [&](const Branch &operation) {
              record_use(operation.condition, index);
              constrain_width(operation.condition, 1U, index);
              validate_target(operation.when_true, index, "branch true");
              validate_target(operation.when_false, index, "branch false");
              switch (operation.unknown_policy) {
              case UnknownBranchPolicy::error:
              case UnknownBranchPolicy::when_false:
                break;
              default:
                reject(process, index, "branch has an invalid unknown policy");
              }
            },
            [&](const Halt &) {},
            [&](const WriteUpdate &operation) {
              record_use(operation.source, index);
              constrain_width(operation.source,
                              signal_width(operation.signal, index), index);
              result.uses_write_update = true;
            },
            [&](const WriteAfter &operation) {
              record_use(operation.source, index);
              constrain_width(operation.source,
                              signal_width(operation.signal, index), index);
              result.uses_write_after = true;
            },
            [&](const WaitFor &) {},
            [&](const WaitOn &operation) {
              if (operation.signals.empty()) {
                reject(process, index,
                       "WaitOn requires at least one signal");
              }
              if (!operation.edges.empty()
                  && operation.edges.size()
                      != operation.signals.size()) {
                reject(
                    process, index,
                    "WaitOn edge count must match its signal count");
              }
              for (std::size_t signal_index = 0;
                   signal_index < operation.signals.size();
                   ++signal_index) {
                const auto width = referenced_signal_width(
                    operation.signals[signal_index], index);
                const auto edge =
                    operation.edges.empty()
                        ? EdgeKind::any
                        : operation.edges[signal_index];
                switch (edge) {
                case EdgeKind::any:
                  break;
                case EdgeKind::posedge:
                case EdgeKind::negedge:
                  if (width != 1) {
                    reject(
                        process, index,
                        "WaitOn edge requires a scalar signal");
                  }
                  break;
                default:
                  reject(
                      process, index,
                      "WaitOn has an invalid edge kind");
                }
              }
            },
            [&](const WaitSensitivity &) {
              if (process.static_sensitivity.empty()) {
                reject(process, index,
                       "WaitSensitivity requires a static sensitivity list");
              }
            },
            [&](const Yield &) {},
            [&](const Stop &) {}},
        process.operations[index]);
  }

  for (std::size_t index = 0; index < process.register_count; ++index) {
    const auto width =
        root_widths[find_root(static_cast<RegisterId>(index))];
    if (width <= std::numeric_limits<std::uint32_t>::max()) {
      result.register_widths[index] = static_cast<std::uint32_t>(width);
    }
  }
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (instruction_definitions[index] &&
        result.register_widths[*instruction_definitions[index]] == 0) {
      reject(process, index, "register width cannot be inferred");
    }
    for (const auto used : instruction_uses[index]) {
      if (!defined[used]) {
        reject(process, index, "source register is never defined");
      }
      if (result.register_widths[used] == 0) {
        reject(process, index, "source register width cannot be inferred");
      }
    }
    if (const auto* extract =
            std::get_if<Extract>(&process.operations[index])) {
      const auto source_width =
          result.register_widths[extract->source];
      if (extract->offset > source_width
          || extract->width
              > source_width - extract->offset) {
        reject(
            process, index,
            "Extract range is outside its source register");
      }
    }
    if (const auto* concatenate =
            std::get_if<Concatenate>(&process.operations[index])) {
      std::uint64_t width = 0;
      for (const auto operand : concatenate->operands) {
        width += result.register_widths[operand];
      }
      if (width != concatenate->width) {
        reject(
            process, index,
            "Concatenate operand widths do not match its result width");
      }
    }
  }

  std::vector<std::vector<std::size_t>> successors(
      process.operations.size());
  std::vector<std::vector<std::size_t>> predecessors(
      process.operations.size());
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    const auto &operation = process.operations[index];
    if (std::holds_alternative<Halt>(operation) ||
        std::holds_alternative<Stop>(operation)) {
      continue;
    }
    if (const auto *jump = std::get_if<Jump>(&operation)) {
      successors[index].push_back(jump->target);
    } else if (const auto *branch = std::get_if<Branch>(&operation)) {
      successors[index].push_back(branch->when_true);
      if (branch->when_false != branch->when_true) {
        successors[index].push_back(branch->when_false);
      }
    } else {
      if (index + 1 >= process.operations.size()) {
        reject(process, index,
               "control flow falls outside the operation stream");
      }
      successors[index].push_back(index + 1);
    }
    for (const auto successor : successors[index]) {
      predecessors[successor].push_back(index);
    }
  }

  std::vector<bool> reachable(process.operations.size());
  std::vector<std::size_t> pending{0};
  while (!pending.empty()) {
    const auto instruction = pending.back();
    pending.pop_back();
    if (reachable[instruction]) {
      continue;
    }
    reachable[instruction] = true;
    pending.insert(pending.end(), successors[instruction].begin(),
                   successors[instruction].end());
  }
  const auto is_suspension = [](const Operation &operation) {
    return std::holds_alternative<WaitFor>(operation) ||
           std::holds_alternative<WaitOn>(operation) ||
           std::holds_alternative<WaitSensitivity>(operation) ||
           std::holds_alternative<Yield>(operation);
  };
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (reachable[index] && is_suspension(process.operations[index])) {
      result.requires_resume = true;
    }
  }

  std::vector<std::vector<std::size_t>> invocation_successors(
      process.operations.size());
  std::vector<std::vector<std::size_t>> invocation_predecessors(
      process.operations.size());
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (!reachable[index] ||
        is_suspension(process.operations[index]) ||
        std::holds_alternative<Stop>(process.operations[index]) ||
        std::holds_alternative<Halt>(process.operations[index])) {
      continue;
    }
    invocation_successors[index] = successors[index];
    for (const auto successor : invocation_successors[index]) {
      invocation_predecessors[successor].push_back(index);
    }
  }
  std::vector<std::size_t> remaining_predecessors(
      process.operations.size());
  std::vector<std::size_t> acyclic_pending;
  std::size_t reachable_count = 0;
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (!reachable[index]) {
      continue;
    }
    ++reachable_count;
    remaining_predecessors[index] =
        static_cast<std::size_t>(std::count_if(
            invocation_predecessors[index].begin(),
            invocation_predecessors[index].end(),
            [&](const std::size_t predecessor) {
              return reachable[predecessor];
            }));
    if (remaining_predecessors[index] == 0) {
      acyclic_pending.push_back(index);
    }
  }
  std::size_t acyclic_count = 0;
  while (!acyclic_pending.empty()) {
    const auto instruction = acyclic_pending.back();
    acyclic_pending.pop_back();
    ++acyclic_count;
    for (const auto successor : invocation_successors[instruction]) {
      if (!reachable[successor]) {
        continue;
      }
      --remaining_predecessors[successor];
      if (remaining_predecessors[successor] == 0) {
        acyclic_pending.push_back(successor);
      }
    }
  }
  if (acyclic_count != reachable_count) {
    const auto cycle = std::find_if(
        remaining_predecessors.begin(), remaining_predecessors.end(),
        [](const std::size_t count) { return count != 0; });
    record_unsupported(
        static_cast<std::size_t>(
            std::distance(remaining_predecessors.begin(), cycle)),
        "reachable control-flow cycle has no suspension safe point");
  }

  std::vector<std::vector<bool>> definitely_defined_in(
      process.operations.size(),
      std::vector<bool>(process.register_count, true));
  std::vector<std::vector<bool>> definitely_defined_out =
      definitely_defined_in;
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
      if (!reachable[index]) {
        continue;
      }
      std::vector<bool> incoming(process.register_count, true);
      if (index == 0) {
        std::fill(incoming.begin(), incoming.end(), false);
      } else {
        bool saw_predecessor = false;
        for (const auto predecessor : predecessors[index]) {
          if (!reachable[predecessor]) {
            continue;
          }
          if (!saw_predecessor) {
            incoming = definitely_defined_out[predecessor];
            saw_predecessor = true;
          } else {
            for (std::size_t reg = 0; reg < process.register_count; ++reg) {
              incoming[reg] =
                  incoming[reg] &&
                  definitely_defined_out[predecessor][reg];
            }
          }
        }
      }
      auto outgoing = incoming;
      if (instruction_definitions[index]) {
        outgoing[*instruction_definitions[index]] = true;
      }
      if (incoming != definitely_defined_in[index] ||
          outgoing != definitely_defined_out[index]) {
        definitely_defined_in[index] = std::move(incoming);
        definitely_defined_out[index] = std::move(outgoing);
        changed = true;
      }
    }
  }

  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (!reachable[index]) {
      continue;
    }
    for (const auto used : instruction_uses[index]) {
      if (!definitely_defined_in[index][used]) {
        reject(process, index,
               "register may be used before definition on a control-flow "
               "path");
      }
    }
  }
  if (unsupported) {
    reject_unsupported(
        process, unsupported->first, unsupported->second);
  }
  return result;
}

void add_key_u64(CacheKeyBuilder &builder, const std::string_view label,
                 const std::uint64_t value) {
  std::array<std::byte, sizeof(value)> encoded{};
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    encoded[index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
  builder.add_bytes(label, encoded);
}

[[nodiscard]] std::string make_native_object_cache_key(
    const std::string_view symbol, const Process &process,
    const std::span<const std::uint32_t> signal_widths,
    const JitOptimizationLevel optimization,
    const llvm::Triple &target_triple, const llvm::DataLayout &data_layout,
    const std::string_view target_cpu,
    const std::span<const std::string> target_features) {
  CacheKeyBuilder builder;
  builder.add("llvm-object-schema", kNativeObjectCacheSchema);
  builder.add("llvm-version", LLVM_VERSION_STRING);
  builder.add("optimization",
              optimization == JitOptimizationLevel::o0 ? "o0" : "o2");
  add_key_u64(builder, "runtime-abi-version",
              FSIM_JIT_RUNTIME_ABI_VERSION_V1);
  add_key_u64(builder, "runtime-abi-structure-size",
              sizeof(fsim_jit_runtime_v1));
  add_key_u64(builder, "frame-abi-version",
              FSIM_JIT_FRAME_ABI_VERSION_V1);
  add_key_u64(builder, "frame-abi-structure-size",
              sizeof(fsim_jit_frame_v1));
  add_key_u64(builder, "resume-result-abi-version",
              FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1);
  add_key_u64(builder, "resume-result-abi-structure-size",
              sizeof(fsim_jit_resume_result_v1));
  builder.add("target-triple", target_triple.str());
  builder.add("data-layout", data_layout.getStringRepresentation());
  builder.add("target-cpu", target_cpu);
  add_key_u64(builder, "target-feature-count", target_features.size());
  for (const auto &feature : target_features) {
    builder.add("target-feature", feature);
  }

  builder.add("symbol", symbol);
  add_key_u64(builder, "process-id", process.id);
  builder.add("process-name", process.name);
  add_key_u64(builder, "register-count", process.register_count);
  add_key_u64(builder, "sensitivity-count",
              process.static_sensitivity.size());
  for (const auto &sensitivity : process.static_sensitivity) {
    add_key_u64(builder, "sensitivity-signal", sensitivity.signal);
    add_key_u64(
        builder, "sensitivity-edge",
        static_cast<std::underlying_type_t<runtime::simir::EdgeKind>>(
            sensitivity.edge));
  }

  add_key_u64(builder, "operation-count", process.operations.size());
  for (const auto &operation : process.operations) {
    std::visit(
        Overloaded{
            [&](const LoadConstant &value) {
              builder.add("operation", "LoadConstant");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "width", value.value.width());
              add_key_u64(builder, "aval",
                          value.value.aval_words().front());
              add_key_u64(builder, "bval",
                          value.value.bval_words().front());
            },
            [&](const ReadSignal &value) {
              builder.add("operation", "ReadSignal");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
            },
            [&](const CopyRegister& value) {
              builder.add("operation", "CopyRegister");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const UnaryNot &value) {
              builder.add("operation", "UnaryNot");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const LogicalNot& value) {
              builder.add("operation", "LogicalNot");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const LogicalBinary& value) {
              builder.add("operation", "LogicalBinary");
              add_key_u64(
                  builder, "logical-binary-operator",
                  static_cast<
                      std::underlying_type_t<LogicalBinaryOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "lhs", value.lhs);
              add_key_u64(builder, "rhs", value.rhs);
            },
            [&](const Reduction& value) {
              builder.add("operation", "Reduction");
              add_key_u64(
                  builder, "reduction-operator",
                  static_cast<
                      std::underlying_type_t<ReductionOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
            },
            [&](const Shift& value) {
              builder.add("operation", "Shift");
              add_key_u64(
                  builder, "shift-operator",
                  static_cast<std::underlying_type_t<ShiftOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "value", value.value);
              add_key_u64(builder, "amount", value.amount);
            },
            [&](const Extract& value) {
              builder.add("operation", "Extract");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "offset", value.offset);
              add_key_u64(builder, "width", value.width);
            },
            [&](const Concatenate& value) {
              builder.add("operation", "Concatenate");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "width", value.width);
              add_key_u64(
                  builder, "operand-count", value.operands.size());
              for (const auto operand : value.operands) {
                add_key_u64(builder, "operand", operand);
              }
            },
            [&](const Binary &value) {
              builder.add("operation", "Binary");
              add_key_u64(
                  builder, "binary-operator",
                  static_cast<std::underlying_type_t<BinaryOperator>>(
                      value.operation));
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "lhs", value.lhs);
              add_key_u64(builder, "rhs", value.rhs);
            },
            [&](const ConditionalSelect& value) {
              builder.add("operation", "ConditionalSelect");
              add_key_u64(builder, "destination", value.destination);
              add_key_u64(builder, "condition", value.condition);
              add_key_u64(builder, "when-true", value.when_true);
              add_key_u64(builder, "when-false", value.when_false);
            },
            [&](const WriteBlocking &value) {
              builder.add("operation", "WriteBlocking");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
            },
            [&](const WriteUpdate &value) {
              builder.add("operation", "WriteUpdate");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
            },
            [&](const WriteAfter &value) {
              builder.add("operation", "WriteAfter");
              add_key_u64(builder, "signal", value.signal);
              add_key_u64(
                  builder, "signal-width", signal_widths[value.signal]);
              add_key_u64(builder, "source", value.source);
              add_key_u64(builder, "delay", value.delay);
            },
            [&](const Assert &value) {
              builder.add("operation", "Assert");
              add_key_u64(builder, "condition", value.condition);
              builder.add("message", value.message);
              add_key_u64(
                  builder, "severity",
                  static_cast<std::underlying_type_t<
                      runtime::simir::AssertionSeverity>>(
                      value.severity));
              builder.add("source-path", value.source.path);
              add_key_u64(builder, "source-line", value.source.line);
              add_key_u64(builder, "source-column", value.source.column);
            },
            [&](const DebugPoint& value) {
              builder.add("operation", "DebugPoint");
              add_key_u64(
                  builder, "kind",
                  static_cast<std::underlying_type_t<
                      runtime::simir::DebugPointKind>>(value.kind));
              builder.add("source-path", value.source.path);
              add_key_u64(builder, "source-line", value.source.line);
              add_key_u64(builder, "source-column", value.source.column);
            },
            [&](const Jump &value) {
              builder.add("operation", "Jump");
              add_key_u64(builder, "target", value.target);
            },
            [&](const Branch &value) {
              builder.add("operation", "Branch");
              add_key_u64(builder, "condition", value.condition);
              add_key_u64(builder, "when-true", value.when_true);
              add_key_u64(builder, "when-false", value.when_false);
              add_key_u64(
                  builder, "unknown-policy",
                  static_cast<
                      std::underlying_type_t<UnknownBranchPolicy>>(
                      value.unknown_policy));
            },
            [&](const WaitFor &value) {
              builder.add("operation", "WaitFor");
              add_key_u64(builder, "delay", value.delay);
            },
            [&](const WaitOn &value) {
              builder.add("operation", "WaitOn");
              add_key_u64(
                  builder, "wait-on-signal-count", value.signals.size());
              for (std::size_t index = 0;
                   index < value.signals.size(); ++index) {
                const auto signal = value.signals[index];
                const auto edge =
                    value.edges.empty()
                        ? EdgeKind::any
                        : value.edges[index];
                add_key_u64(builder, "wait-on-signal", signal);
                add_key_u64(
                    builder, "wait-on-signal-width",
                    signal_widths[signal]);
                add_key_u64(
                    builder, "wait-on-edge",
                    static_cast<std::underlying_type_t<EdgeKind>>(
                        edge));
              }
            },
            [&](const WaitSensitivity &) {
              builder.add("operation", "WaitSensitivity");
              add_key_u64(
                  builder, "wait-sensitivity-count",
                  process.static_sensitivity.size());
              for (const auto sensitivity : process.static_sensitivity) {
                add_key_u64(
                    builder, "wait-sensitivity-signal",
                    sensitivity.signal);
                add_key_u64(
                    builder, "wait-sensitivity-signal-width",
                    signal_widths[sensitivity.signal]);
                add_key_u64(
                    builder, "wait-sensitivity-edge",
                    static_cast<std::underlying_type_t<EdgeKind>>(
                        sensitivity.edge));
              }
            },
            [&](const Yield &) {
              builder.add("operation", "Yield");
            },
            [&](const Stop &) {
              builder.add("operation", "Stop");
            },
            [&](const Halt &) {
              builder.add("operation", "Halt");
            },
            [&](const auto &) {
              llvm_unreachable(
                  "unsupported operations were rejected before cache keying");
            }},
        operation);
  }
  return builder.finish();
}

[[nodiscard]] std::string make_native_module_cache_key(
    const std::string_view module_identity,
    const std::span<const std::string> process_keys) {
  CacheKeyBuilder builder;
  builder.add("llvm-module-schema", kNativeObjectCacheSchema);
  builder.add("module-identity", module_identity);
  add_key_u64(builder, "process-count", process_keys.size());
  for (const auto& process_key : process_keys) {
    builder.add("process-key", process_key);
  }
  return builder.finish();
}

[[nodiscard]] std::uint64_t cache_key_word(
    const std::string_view key, const std::size_t offset) noexcept {
  std::uint64_t result = 0;
  for (const auto character : key.substr(offset, 16)) {
    const auto digit = static_cast<std::uint64_t>(
        character >= '0' && character <= '9'
            ? character - '0'
            : character - 'a' + 10);
    result = (result << 4U) | digit;
  }
  return result;
}

[[nodiscard]] JitProcessFrameLayout
make_frame_layout(const std::string_view cache_key,
                  const std::size_t register_count) {
  return {
      cache_key_word(cache_key, 0),
      cache_key_word(cache_key, 16),
      static_cast<std::uint32_t>(register_count),
  };
}

struct EncodedValue {
  llvm::Value *aval{};
  llvm::Value *bval{};
  std::uint32_t width{};
};

struct RegisterSlot {
  llvm::Value *aval{};
  llvm::Value *bval{};
  std::uint32_t width{};
};

[[nodiscard]] EncodedValue
load_register(llvm::IRBuilder<> &builder,
              const std::vector<RegisterSlot> &registers,
              const RegisterId id) {
  const auto &slot = registers[id];
  auto *i64 = llvm::Type::getInt64Ty(builder.getContext());
  return {
      builder.CreateLoad(i64, slot.aval, "register.aval"),
      builder.CreateLoad(i64, slot.bval, "register.bval"),
      slot.width,
  };
}

void store_register(llvm::IRBuilder<> &builder,
                    const std::vector<RegisterSlot> &registers,
                    const RegisterId id, const EncodedValue value) {
  builder.CreateStore(value.aval, registers[id].aval);
  builder.CreateStore(value.bval, registers[id].bval);
}

struct EncodedBit {
  llvm::Value *aval{};
  llvm::Value *bval{};
};

[[nodiscard]] std::uint64_t width_mask(const std::uint32_t width) noexcept {
  return width == 64 ? ~std::uint64_t{0}
                     : (std::uint64_t{1} << width) - 1U;
}

[[nodiscard]] llvm::ConstantInt *constant_i64(llvm::LLVMContext &context,
                                              const std::uint64_t value) {
  return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), value);
}

[[nodiscard]] EncodedBit bit_at(llvm::IRBuilder<> &builder,
                                const EncodedValue value,
                                const std::uint32_t bit) {
  auto &context = builder.getContext();
  auto *shift = constant_i64(context, bit);
  auto *aval = builder.CreateTrunc(builder.CreateLShr(value.aval, shift),
                                   llvm::Type::getInt1Ty(context));
  auto *bval = builder.CreateTrunc(builder.CreateLShr(value.bval, shift),
                                   llvm::Type::getInt1Ty(context));
  return {aval, bval};
}

[[nodiscard]] EncodedBit truth_bit(
    llvm::IRBuilder<>& builder,
    const EncodedValue value) {
  auto& context = builder.getContext();
  auto* mask = constant_i64(context, width_mask(value.width));
  auto* known_ones = builder.CreateAnd(
      builder.CreateAnd(value.aval, mask),
      builder.CreateNot(value.bval));
  auto* has_one = builder.CreateICmpNE(
      known_ones, constant_i64(context, 0));
  auto* has_unknown = builder.CreateICmpNE(
      builder.CreateAnd(value.bval, mask),
      constant_i64(context, 0));
  auto* unknown =
      builder.CreateAnd(builder.CreateNot(has_one), has_unknown);
  return {builder.CreateOr(has_one, unknown), unknown};
}

[[nodiscard]] EncodedBit bit_xor(llvm::IRBuilder<> &builder,
                                 const EncodedBit lhs,
                                 const EncodedBit rhs) {
  auto *known = builder.CreateAnd(builder.CreateNot(lhs.bval),
                                  builder.CreateNot(rhs.bval));
  auto *unknown = builder.CreateNot(known);
  auto *known_value =
      builder.CreateAnd(builder.CreateXor(lhs.aval, rhs.aval), known);
  return {builder.CreateOr(known_value, unknown), unknown};
}

[[nodiscard]] EncodedBit bit_and(llvm::IRBuilder<> &builder,
                                 const EncodedBit lhs,
                                 const EncodedBit rhs) {
  auto *lhs_known = builder.CreateNot(lhs.bval);
  auto *rhs_known = builder.CreateNot(rhs.bval);
  auto *lhs_zero =
      builder.CreateAnd(builder.CreateNot(lhs.aval), lhs_known);
  auto *rhs_zero =
      builder.CreateAnd(builder.CreateNot(rhs.aval), rhs_known);
  auto *known_zero = builder.CreateOr(lhs_zero, rhs_zero);
  auto *lhs_one = builder.CreateAnd(lhs.aval, lhs_known);
  auto *rhs_one = builder.CreateAnd(rhs.aval, rhs_known);
  auto *known_one = builder.CreateAnd(lhs_one, rhs_one);
  auto *unknown =
      builder.CreateNot(builder.CreateOr(known_zero, known_one));
  return {builder.CreateOr(known_one, unknown), unknown};
}

[[nodiscard]] EncodedBit bit_or(llvm::IRBuilder<> &builder,
                                const EncodedBit lhs,
                                const EncodedBit rhs) {
  auto *lhs_known = builder.CreateNot(lhs.bval);
  auto *rhs_known = builder.CreateNot(rhs.bval);
  auto *lhs_one = builder.CreateAnd(lhs.aval, lhs_known);
  auto *rhs_one = builder.CreateAnd(rhs.aval, rhs_known);
  auto *known_one = builder.CreateOr(lhs_one, rhs_one);
  auto *lhs_zero =
      builder.CreateAnd(builder.CreateNot(lhs.aval), lhs_known);
  auto *rhs_zero =
      builder.CreateAnd(builder.CreateNot(rhs.aval), rhs_known);
  auto *known_zero = builder.CreateAnd(lhs_zero, rhs_zero);
  auto *unknown =
      builder.CreateNot(builder.CreateOr(known_zero, known_one));
  return {builder.CreateOr(known_one, unknown), unknown};
}

[[nodiscard]] EncodedValue lower_binary(llvm::IRBuilder<> &builder,
                                        const BinaryOperator operation,
                                        const EncodedValue lhs,
                                        const EncodedValue rhs) {
  auto &context = builder.getContext();
  auto *mask = constant_i64(context, width_mask(lhs.width));
  switch (operation) {
  case BinaryOperator::bit_and: {
    auto *lhs_zero =
        builder.CreateAnd(builder.CreateNot(lhs.aval),
                          builder.CreateNot(lhs.bval));
    auto *rhs_zero =
        builder.CreateAnd(builder.CreateNot(rhs.aval),
                          builder.CreateNot(rhs.bval));
    auto *known_zero = builder.CreateOr(lhs_zero, rhs_zero);
    auto *lhs_one =
        builder.CreateAnd(lhs.aval, builder.CreateNot(lhs.bval));
    auto *rhs_one =
        builder.CreateAnd(rhs.aval, builder.CreateNot(rhs.bval));
    auto *known_one = builder.CreateAnd(lhs_one, rhs_one);
    auto *unknown =
        builder.CreateAnd(builder.CreateNot(
                              builder.CreateOr(known_zero, known_one)),
                          mask);
    return {builder.CreateAnd(builder.CreateOr(known_one, unknown), mask),
            unknown, lhs.width};
  }
  case BinaryOperator::bit_or: {
    auto *lhs_one =
        builder.CreateAnd(lhs.aval, builder.CreateNot(lhs.bval));
    auto *rhs_one =
        builder.CreateAnd(rhs.aval, builder.CreateNot(rhs.bval));
    auto *known_one = builder.CreateOr(lhs_one, rhs_one);
    auto *lhs_zero =
        builder.CreateAnd(builder.CreateNot(lhs.aval),
                          builder.CreateNot(lhs.bval));
    auto *rhs_zero =
        builder.CreateAnd(builder.CreateNot(rhs.aval),
                          builder.CreateNot(rhs.bval));
    auto *known_zero = builder.CreateAnd(lhs_zero, rhs_zero);
    auto *unknown =
        builder.CreateAnd(builder.CreateNot(
                              builder.CreateOr(known_zero, known_one)),
                          mask);
    return {builder.CreateAnd(builder.CreateOr(known_one, unknown), mask),
            unknown, lhs.width};
  }
  case BinaryOperator::bit_xor: {
    auto *known =
        builder.CreateAnd(builder.CreateNot(
                              builder.CreateOr(lhs.bval, rhs.bval)),
                          mask);
    auto *unknown = builder.CreateAnd(builder.CreateNot(known), mask);
    auto *known_value =
        builder.CreateAnd(builder.CreateXor(lhs.aval, rhs.aval), known);
    return {builder.CreateOr(known_value, unknown), unknown, lhs.width};
  }
  case BinaryOperator::add_unsigned: {
    auto *unknown = builder.CreateICmpNE(
        builder.CreateAnd(
            builder.CreateOr(lhs.bval, rhs.bval), mask),
        constant_i64(context, 0));
    llvm::Value *result_aval = constant_i64(context, 0);
    EncodedBit carry{llvm::ConstantInt::getFalse(context),
                     llvm::ConstantInt::getFalse(context)};
    for (std::uint32_t bit = 0; bit < lhs.width; ++bit) {
      const auto left = bit_at(builder, lhs, bit);
      const auto right = bit_at(builder, rhs, bit);
      const auto partial = bit_xor(builder, left, right);
      const auto sum = bit_xor(builder, partial, carry);
      const auto carry_generate = bit_and(builder, left, right);
      const auto carry_propagate =
          bit_and(builder, carry, bit_or(builder, left, right));
      carry = bit_or(builder, carry_generate, carry_propagate);

      auto *shift = constant_i64(context, bit);
      auto *aval = builder.CreateShl(
          builder.CreateZExt(sum.aval, llvm::Type::getInt64Ty(context)),
          shift);
      result_aval = builder.CreateOr(result_aval, aval);
    }
    return {
        builder.CreateSelect(
            unknown, mask, builder.CreateAnd(result_aval, mask)),
        builder.CreateSelect(
            unknown, mask, constant_i64(context, 0)),
        lhs.width};
  }
  case BinaryOperator::subtract_unsigned:
  case BinaryOperator::multiply_unsigned:
  case BinaryOperator::divide_unsigned:
  case BinaryOperator::modulo_unsigned: {
    auto *unknown = builder.CreateICmpNE(
        builder.CreateAnd(
            builder.CreateOr(lhs.bval, rhs.bval), mask),
        constant_i64(context, 0));
    auto *left = builder.CreateAnd(lhs.aval, mask);
    auto *right = builder.CreateAnd(rhs.aval, mask);
    auto *invalid = unknown;
    if (operation == BinaryOperator::divide_unsigned
        || operation == BinaryOperator::modulo_unsigned) {
      invalid = builder.CreateOr(
          invalid,
          builder.CreateICmpEQ(
              right, constant_i64(context, 0)));
      right = builder.CreateSelect(
          invalid, constant_i64(context, 1), right);
    }
    llvm::Value* known_result = nullptr;
    if (operation == BinaryOperator::subtract_unsigned) {
      known_result = builder.CreateSub(left, right);
    } else if (
        operation == BinaryOperator::multiply_unsigned) {
      known_result = builder.CreateMul(left, right);
    } else if (
        operation == BinaryOperator::divide_unsigned) {
      known_result = builder.CreateUDiv(left, right);
    } else {
      known_result = builder.CreateURem(left, right);
    }
    return {
        builder.CreateSelect(
            invalid, mask, builder.CreateAnd(known_result, mask)),
        builder.CreateSelect(
            invalid, mask, constant_i64(context, 0)),
        lhs.width};
  }
  case BinaryOperator::equal: {
    auto *unknown_bits =
        builder.CreateAnd(builder.CreateOr(lhs.bval, rhs.bval), mask);
    auto *unknown = builder.CreateICmpNE(unknown_bits,
                                        constant_i64(context, 0));
    auto *equal = builder.CreateICmpEQ(
        builder.CreateAnd(lhs.aval, mask),
        builder.CreateAnd(rhs.aval, mask));
    auto *aval = builder.CreateZExt(
        builder.CreateOr(unknown, equal), llvm::Type::getInt64Ty(context));
    auto *bval =
        builder.CreateZExt(unknown, llvm::Type::getInt64Ty(context));
    return {aval, bval, 1};
  }
  case BinaryOperator::case_equal: {
    auto *aval_equal = builder.CreateICmpEQ(
        builder.CreateAnd(lhs.aval, mask),
        builder.CreateAnd(rhs.aval, mask));
    auto *bval_equal = builder.CreateICmpEQ(
        builder.CreateAnd(lhs.bval, mask),
        builder.CreateAnd(rhs.bval, mask));
    auto *equal = builder.CreateAnd(aval_equal, bval_equal);
    return {
        builder.CreateZExt(equal, llvm::Type::getInt64Ty(context)),
        constant_i64(context, 0),
        1};
  }
  case BinaryOperator::not_equal:
  case BinaryOperator::less_unsigned:
  case BinaryOperator::less_equal_unsigned:
  case BinaryOperator::greater_unsigned:
  case BinaryOperator::greater_equal_unsigned: {
    auto *unknown_bits = builder.CreateAnd(
        builder.CreateOr(lhs.bval, rhs.bval), mask);
    auto *unknown = builder.CreateICmpNE(
        unknown_bits, constant_i64(context, 0));
    auto *left = builder.CreateAnd(lhs.aval, mask);
    auto *right = builder.CreateAnd(rhs.aval, mask);
    llvm::CmpInst::Predicate predicate = llvm::CmpInst::ICMP_NE;
    switch (operation) {
    case BinaryOperator::not_equal:
      predicate = llvm::CmpInst::ICMP_NE;
      break;
    case BinaryOperator::less_unsigned:
      predicate = llvm::CmpInst::ICMP_ULT;
      break;
    case BinaryOperator::less_equal_unsigned:
      predicate = llvm::CmpInst::ICMP_ULE;
      break;
    case BinaryOperator::greater_unsigned:
      predicate = llvm::CmpInst::ICMP_UGT;
      break;
    case BinaryOperator::greater_equal_unsigned:
      predicate = llvm::CmpInst::ICMP_UGE;
      break;
    case BinaryOperator::bit_and:
    case BinaryOperator::bit_or:
    case BinaryOperator::bit_xor:
    case BinaryOperator::add_unsigned:
    case BinaryOperator::subtract_unsigned:
    case BinaryOperator::multiply_unsigned:
    case BinaryOperator::divide_unsigned:
    case BinaryOperator::modulo_unsigned:
    case BinaryOperator::equal:
    case BinaryOperator::case_equal:
      llvm_unreachable("not a comparison operator");
    }
    auto *compared = builder.CreateICmp(predicate, left, right);
    return {
        builder.CreateZExt(
            builder.CreateOr(unknown, compared),
            llvm::Type::getInt64Ty(context)),
        builder.CreateZExt(
            unknown, llvm::Type::getInt64Ty(context)),
        1};
  }
  }
  llvm_unreachable("all BinaryOperator values are handled");
}

void optimize_module(llvm::Module &module,
                     const JitOptimizationLevel optimization) {
  llvm::LoopAnalysisManager loop_analyses;
  llvm::FunctionAnalysisManager function_analyses;
  llvm::CGSCCAnalysisManager cgscc_analyses;
  llvm::ModuleAnalysisManager module_analyses;
  llvm::PassBuilder builder;

  builder.registerModuleAnalyses(module_analyses);
  builder.registerCGSCCAnalyses(cgscc_analyses);
  builder.registerFunctionAnalyses(function_analyses);
  builder.registerLoopAnalyses(loop_analyses);
  builder.crossRegisterProxies(loop_analyses, function_analyses,
                              cgscc_analyses, module_analyses);

  const auto level = optimization == JitOptimizationLevel::o0
                         ? llvm::OptimizationLevel::O0
                         : llvm::OptimizationLevel::O2;
  auto pipeline = builder.buildPerModuleDefaultPipeline(level);
  pipeline.run(module, module_analyses);
}

[[nodiscard]] std::string verify_error(llvm::Module &module) {
  std::string message;
  llvm::raw_string_ostream stream(message);
  if (!llvm::verifyModule(module, &stream)) {
    return {};
  }
  stream.flush();
  return message;
}

void lower_process(llvm::Module &module, const std::string &symbol,
                   const Process &process,
                   const std::span<const std::uint32_t> signal_widths,
                   const ValidatedProcess &validated,
                   const bool debug_instrumentation) {
  auto &context = module.getContext();
  auto *i32 = llvm::Type::getInt32Ty(context);
  auto *i64 = llvm::Type::getInt64Ty(context);
  auto *pointer = llvm::PointerType::getUnqual(context);
  auto *runtime_type = llvm::StructType::create(
      context,
      {i32, i32, pointer, pointer, pointer, pointer, pointer, pointer,
       i32, i32},
      "fsim_jit_runtime_v1");
  auto *frame_type = llvm::StructType::create(
      context,
      {i32, i32, i64, i64, i32, i32, i32, i32, pointer, pointer},
      "fsim_jit_frame_v1");
  auto *result_type = llvm::StructType::create(
      context, {i32, i32, i32, i32, i64},
      "fsim_jit_resume_result_v1");
  auto *function_type =
      llvm::FunctionType::get(i32, {pointer, pointer, pointer}, false);
  auto *function = llvm::Function::Create(
      function_type, llvm::Function::ExternalLinkage, symbol, module);
  function->setCallingConv(llvm::CallingConv::C);
  function->getArg(0)->setName("runtime");
  function->getArg(1)->setName("frame");
  function->getArg(2)->setName("result");

  auto *entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::IRBuilder<> builder(entry);
  auto *runtime_argument = function->getArg(0);
  auto *frame_argument = function->getArg(1);
  auto *result_argument = function->getArg(2);
  auto *context_pointer = builder.CreateLoad(
      pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 2),
      "context");
  auto *read_callback = builder.CreateLoad(
      pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 3),
      "read_signal");
  auto *write_callback = builder.CreateLoad(
      pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 4),
      "write_signal");
  auto *assert_callback = builder.CreateLoad(
      pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 5),
      "assert_failed");
  llvm::Value *write_update_callback = nullptr;
  if (validated.uses_write_update) {
    write_update_callback = builder.CreateLoad(
        pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 6),
        "write_update");
  }
  llvm::Value *write_after_callback = nullptr;
  if (validated.uses_write_after) {
    write_after_callback = builder.CreateLoad(
        pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 7),
        "write_after");
  }
  llvm::Value* runtime_flags = nullptr;
  if (validated.uses_debug_points) {
    runtime_flags = builder.CreateLoad(
        i32, builder.CreateStructGEP(runtime_type, runtime_argument, 8),
        "runtime.flags");
  }

  auto *read_type =
      llvm::FunctionType::get(i64, {pointer, i32, pointer}, false);
  auto *write_type =
      llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                              {pointer, i32, i64, i64}, false);
  auto *assert_type =
      llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                              {pointer, i32, i32, pointer, i64}, false);
  auto *write_after_type =
      llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                              {pointer, i32, i64, i64, i64}, false);

  auto *register_aval = builder.CreateLoad(
      pointer, builder.CreateStructGEP(frame_type, frame_argument, 8),
      "register.aval.base");
  auto *register_bval = builder.CreateLoad(
      pointer, builder.CreateStructGEP(frame_type, frame_argument, 9),
      "register.bval.base");
  std::vector<RegisterSlot> registers(process.register_count);
  for (std::size_t index = 0; index < process.register_count; ++index) {
    const auto width = validated.register_widths[index];
    if (width == 0) {
      continue;
    }
    registers[index] = {
        builder.CreateGEP(
            i64, register_aval, constant_i64(context, index),
            "register." + std::to_string(index) + ".aval"),
        builder.CreateGEP(
            i64, register_bval, constant_i64(context, index),
            "register." + std::to_string(index) + ".bval"),
        width,
    };
  }
  auto *read_bval_slot = builder.CreateAlloca(i64, nullptr, "read.bval");

  const auto return_result =
      [&](const std::uint32_t status, const std::uint32_t instruction,
          const std::uint64_t delay, const std::uint32_t frame_state,
          const std::uint32_t next_pc) {
        builder.CreateStore(
            llvm::ConstantInt::get(i32, next_pc),
            builder.CreateStructGEP(frame_type, frame_argument, 5));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, frame_state),
            builder.CreateStructGEP(frame_type, frame_argument, 6));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, instruction),
            builder.CreateStructGEP(frame_type, frame_argument, 7));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, status),
            builder.CreateStructGEP(result_type, result_argument, 2));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, instruction),
            builder.CreateStructGEP(result_type, result_argument, 3));
        builder.CreateStore(
            constant_i64(context, delay),
            builder.CreateStructGEP(result_type, result_argument, 4));
        builder.CreateRet(llvm::ConstantInt::get(i32, status));
      };

  std::vector<llvm::BasicBlock *> instruction_blocks;
  instruction_blocks.reserve(process.operations.size());
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    instruction_blocks.push_back(llvm::BasicBlock::Create(
        context, "instruction." + std::to_string(index), function));
  }
  auto *invalid_pc =
      llvm::BasicBlock::Create(context, "invalid.pc", function);
  auto *program_counter = builder.CreateLoad(
      i32, builder.CreateStructGEP(frame_type, frame_argument, 5),
      "program.counter");
  auto *dispatch = builder.CreateSwitch(
      program_counter, invalid_pc,
      static_cast<unsigned>(process.operations.size()));
  for (std::size_t index = 0; index < instruction_blocks.size(); ++index) {
    dispatch->addCase(
        llvm::ConstantInt::get(i32, static_cast<std::uint32_t>(index)),
        instruction_blocks[index]);
  }

  builder.SetInsertPoint(invalid_pc);
  return_result(
      FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR, FSIM_JIT_INVALID_INSTRUCTION, 0,
      FSIM_JIT_FRAME_STATE_RUNTIME_ERROR, FSIM_JIT_INVALID_INSTRUCTION);

  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    const auto instruction = static_cast<InstructionIndex>(index);
    const auto next_instruction =
        static_cast<InstructionIndex>(index + 1U);
    builder.SetInsertPoint(instruction_blocks[index]);
    const auto branch_to_next = [&] {
      builder.CreateBr(instruction_blocks[index + 1]);
    };
    std::visit(
        Overloaded{
            [&](const LoadConstant &operation) {
              const auto aval = operation.value.aval_words().front();
              const auto bval = operation.value.bval_words().front();
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      constant_i64(context, aval),
                      constant_i64(context, bval),
                      validated.register_widths[operation.destination]});
              branch_to_next();
            },
            [&](const ReadSignal &operation) {
              builder.CreateStore(constant_i64(context, 0), read_bval_slot);
              auto *aval = builder.CreateCall(
                  read_type, read_callback,
                  {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
                   read_bval_slot},
                  "aval");
              auto *bval =
                  builder.CreateLoad(i64, read_bval_slot, "bval.value");
              const auto width = signal_widths[operation.signal];
              auto *mask = constant_i64(context, width_mask(width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      width});
              branch_to_next();
            },
            [&](const CopyRegister& operation) {
              store_register(
                  builder, registers, operation.destination,
                  load_register(
                      builder, registers, operation.source));
              branch_to_next();
            },
            [&](const UnaryNot &operation) {
              const auto source =
                  load_register(builder, registers, operation.source);
              auto *mask = constant_i64(context, width_mask(source.width));
              auto *aval = builder.CreateAnd(
                  builder.CreateOr(builder.CreateNot(source.aval),
                                   source.bval),
                  mask);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{aval, source.bval, source.width});
              branch_to_next();
            },
            [&](const LogicalNot& operation) {
              const auto source =
                  load_register(
                      builder, registers, operation.source);
              auto *mask =
                  constant_i64(context, width_mask(source.width));
              auto *known_ones = builder.CreateAnd(
                  builder.CreateAnd(source.aval, mask),
                  builder.CreateNot(source.bval));
              auto *has_one = builder.CreateICmpNE(
                  known_ones, constant_i64(context, 0));
              auto *has_unknown = builder.CreateICmpNE(
                  builder.CreateAnd(source.bval, mask),
                  constant_i64(context, 0));
              auto *not_true = builder.CreateNot(has_one);
              auto *unknown =
                  builder.CreateAnd(not_true, has_unknown);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(
                          not_true,
                          llvm::Type::getInt64Ty(context)),
                      builder.CreateZExt(
                          unknown,
                          llvm::Type::getInt64Ty(context)),
                      1});
              branch_to_next();
            },
            [&](const LogicalBinary& operation) {
              const auto left = truth_bit(
                  builder,
                  load_register(builder, registers, operation.lhs));
              const auto right = truth_bit(
                  builder,
                  load_register(builder, registers, operation.rhs));
              const auto result =
                  operation.operation
                          == LogicalBinaryOperator::logical_and
                      ? bit_and(builder, left, right)
                      : bit_or(builder, left, right);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(
                          result.aval,
                          llvm::Type::getInt64Ty(context)),
                      builder.CreateZExt(
                          result.bval,
                          llvm::Type::getInt64Ty(context)),
                      1});
              branch_to_next();
            },
            [&](const Reduction& operation) {
              const auto source =
                  load_register(
                      builder, registers, operation.source);
              EncodedBit result{
                  operation.operation == ReductionOperator::bit_and
                      ? llvm::ConstantInt::getTrue(context)
                      : llvm::ConstantInt::getFalse(context),
                  llvm::ConstantInt::getFalse(context)};
              for (std::uint32_t bit = 0; bit < source.width; ++bit) {
                const auto value = bit_at(builder, source, bit);
                if (operation.operation
                    == ReductionOperator::bit_and) {
                  result = bit_and(builder, result, value);
                } else if (
                    operation.operation
                    == ReductionOperator::bit_or) {
                  result = bit_or(builder, result, value);
                } else {
                  result = bit_xor(builder, result, value);
                }
              }
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(
                          result.aval,
                          llvm::Type::getInt64Ty(context)),
                      builder.CreateZExt(
                          result.bval,
                          llvm::Type::getInt64Ty(context)),
                      1});
              branch_to_next();
            },
            [&](const Shift& operation) {
              const auto value =
                  load_register(
                      builder, registers, operation.value);
              const auto amount =
                  load_register(
                      builder, registers, operation.amount);
              auto* value_mask =
                  constant_i64(context, width_mask(value.width));
              auto* amount_mask =
                  constant_i64(context, width_mask(amount.width));
              auto* amount_unknown = builder.CreateICmpNE(
                  builder.CreateAnd(amount.bval, amount_mask),
                  constant_i64(context, 0));
              auto* amount_bits =
                  builder.CreateAnd(amount.aval, amount_mask);
              auto* amount_too_large = builder.CreateICmpUGE(
                  amount_bits, constant_i64(context, value.width));
              auto* safe_amount = builder.CreateSelect(
                  amount_too_large,
                  constant_i64(context, 0),
                  amount_bits);
              auto* shifted_aval =
                  operation.operation == ShiftOperator::logical_left
                      ? builder.CreateShl(value.aval, safe_amount)
                      : builder.CreateLShr(value.aval, safe_amount);
              auto* shifted_bval =
                  operation.operation == ShiftOperator::logical_left
                      ? builder.CreateShl(value.bval, safe_amount)
                      : builder.CreateLShr(value.bval, safe_amount);
              auto* known_aval = builder.CreateSelect(
                  amount_too_large,
                  constant_i64(context, 0),
                  builder.CreateAnd(shifted_aval, value_mask));
              auto* known_bval = builder.CreateSelect(
                  amount_too_large,
                  constant_i64(context, 0),
                  builder.CreateAnd(shifted_bval, value_mask));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateSelect(
                          amount_unknown, value_mask, known_aval),
                      builder.CreateSelect(
                          amount_unknown, value_mask, known_bval),
                      value.width});
              branch_to_next();
            },
            [&](const Extract& operation) {
              const auto source =
                  load_register(
                      builder, registers, operation.source);
              auto* shift =
                  constant_i64(context, operation.offset);
              auto* mask =
                  constant_i64(context, width_mask(operation.width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(
                          builder.CreateLShr(source.aval, shift),
                          mask),
                      builder.CreateAnd(
                          builder.CreateLShr(source.bval, shift),
                          mask),
                      operation.width});
              branch_to_next();
            },
            [&](const Concatenate& operation) {
              llvm::Value* aval = constant_i64(context, 0);
              llvm::Value* bval = constant_i64(context, 0);
              std::uint32_t offset = 0;
              for (auto operand = operation.operands.rbegin();
                   operand != operation.operands.rend(); ++operand) {
                const auto source =
                    load_register(builder, registers, *operand);
                auto* source_mask =
                    constant_i64(context, width_mask(source.width));
                auto* source_aval =
                    builder.CreateAnd(source.aval, source_mask);
                auto* source_bval =
                    builder.CreateAnd(source.bval, source_mask);
                if (offset != 0) {
                  auto* shift = constant_i64(context, offset);
                  source_aval =
                      builder.CreateShl(source_aval, shift);
                  source_bval =
                      builder.CreateShl(source_bval, shift);
                }
                aval = builder.CreateOr(aval, source_aval);
                bval = builder.CreateOr(bval, source_bval);
                offset += source.width;
              }
              auto* mask =
                  constant_i64(context, width_mask(operation.width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      operation.width});
              branch_to_next();
            },
            [&](const Binary &operation) {
              const auto value =
                  lower_binary(
                      builder, operation.operation,
                      load_register(builder, registers, operation.lhs),
                      load_register(builder, registers, operation.rhs));
              store_register(
                  builder, registers, operation.destination, value);
              branch_to_next();
            },
            [&](const ConditionalSelect& operation) {
              const auto condition =
                  load_register(
                      builder, registers, operation.condition);
              const auto when_true =
                  load_register(
                      builder, registers, operation.when_true);
              const auto when_false =
                  load_register(
                      builder, registers, operation.when_false);
              auto *mask =
                  constant_i64(context, width_mask(when_true.width));
              auto *different = builder.CreateAnd(
                  builder.CreateOr(
                      builder.CreateXor(
                          when_true.aval, when_false.aval),
                      builder.CreateXor(
                          when_true.bval, when_false.bval)),
                  mask);
              auto *same = builder.CreateXor(different, mask);
              auto *merged_aval = builder.CreateOr(
                  builder.CreateAnd(when_true.aval, same),
                  different);
              auto *merged_bval = builder.CreateOr(
                  builder.CreateAnd(when_true.bval, same),
                  different);
              auto *unknown = builder.CreateICmpNE(
                  builder.CreateAnd(
                      condition.bval, constant_i64(context, 1)),
                  constant_i64(context, 0));
              auto *select_true = builder.CreateICmpNE(
                  builder.CreateAnd(
                      condition.aval, constant_i64(context, 1)),
                  constant_i64(context, 0));
              auto *known_aval = builder.CreateSelect(
                  select_true, when_true.aval, when_false.aval);
              auto *known_bval = builder.CreateSelect(
                  select_true, when_true.bval, when_false.bval);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateSelect(
                          unknown, merged_aval, known_aval),
                      builder.CreateSelect(
                          unknown, merged_bval, known_bval),
                      when_true.width});
              branch_to_next();
            },
            [&](const WriteBlocking &operation) {
              const auto source =
                  load_register(builder, registers, operation.source);
              builder.CreateCall(
                  write_type, write_callback,
                  {context_pointer,
                   llvm::ConstantInt::get(i32, operation.signal), source.aval,
                   source.bval});
              branch_to_next();
            },
            [&](const WriteUpdate &operation) {
              const auto source =
                  load_register(builder, registers, operation.source);
              builder.CreateCall(
                  write_type, write_update_callback,
                  {context_pointer,
                   llvm::ConstantInt::get(i32, operation.signal), source.aval,
                   source.bval});
              branch_to_next();
            },
            [&](const WriteAfter &operation) {
              const auto source =
                  load_register(builder, registers, operation.source);
              builder.CreateCall(
                  write_after_type, write_after_callback,
                  {context_pointer,
                   llvm::ConstantInt::get(i32, operation.signal), source.aval,
                   source.bval, constant_i64(context, operation.delay)});
              branch_to_next();
            },
            [&](const Assert &operation) {
              const auto condition =
                  load_register(builder, registers, operation.condition);
              auto *known = builder.CreateICmpEQ(
                  condition.bval, constant_i64(context, 0));
              auto *one = builder.CreateICmpEQ(condition.aval,
                                               constant_i64(context, 1));
              auto *passed = builder.CreateAnd(known, one);
              auto *failed_block =
                  llvm::BasicBlock::Create(
                      context, "assert.failed." + std::to_string(index),
                      function);
              builder.CreateCondBr(
                  passed, instruction_blocks[index + 1], failed_block);

              builder.SetInsertPoint(failed_block);
              const auto &message =
                  operation.message.empty()
                      ? std::string{"assertion failed"}
                      : operation.message;
              auto *message_pointer = builder.CreateGlobalString(
                  message, symbol + ".assert." + std::to_string(index));
              builder.CreateCall(
                  assert_type, assert_callback,
                  {context_pointer, llvm::ConstantInt::get(i32, process.id),
                   llvm::ConstantInt::get(i32, instruction), message_pointer,
                   constant_i64(context, message.size())});
              return_result(
                  FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED, instruction, 0,
                  FSIM_JIT_FRAME_STATE_ASSERTION_FAILED, instruction);
            },
            [&](const DebugPoint&) {
              if (debug_instrumentation) {
                return_result(
                    FSIM_JIT_RESUME_STATUS_DEBUG_POINT, instruction, 0,
                    FSIM_JIT_FRAME_STATE_READY, next_instruction);
              } else {
                auto* enabled = builder.CreateICmpNE(
                    builder.CreateAnd(
                        runtime_flags,
                        llvm::ConstantInt::get(
                            i32, FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS)),
                    llvm::ConstantInt::get(i32, 0));
                auto* enabled_block = llvm::BasicBlock::Create(
                    context,
                    "debug.enabled." + std::to_string(index),
                    function);
                builder.CreateCondBr(
                    enabled, enabled_block,
                    instruction_blocks[index + 1]);
                builder.SetInsertPoint(enabled_block);
                return_result(
                    FSIM_JIT_RESUME_STATUS_DEBUG_POINT, instruction, 0,
                    FSIM_JIT_FRAME_STATE_READY, next_instruction);
              }
            },
            [&](const Jump &operation) {
              builder.CreateBr(instruction_blocks[operation.target]);
            },
            [&](const Branch &operation) {
              const auto condition =
                  load_register(builder, registers, operation.condition);
              auto *known = builder.CreateICmpEQ(
                  condition.bval, constant_i64(context, 0));
              auto *one = builder.CreateICmpEQ(
                  condition.aval, constant_i64(context, 1));
              if (operation.unknown_policy ==
                  UnknownBranchPolicy::when_false) {
                builder.CreateCondBr(
                    builder.CreateAnd(known, one),
                    instruction_blocks[operation.when_true],
                    instruction_blocks[operation.when_false]);
                return;
              }

              auto *known_block = llvm::BasicBlock::Create(
                  context, "branch.known." + std::to_string(index), function);
              auto *unknown_block = llvm::BasicBlock::Create(
                  context, "branch.unknown." + std::to_string(index),
                  function);
              builder.CreateCondBr(known, known_block, unknown_block);

              builder.SetInsertPoint(known_block);
              builder.CreateCondBr(
                  one, instruction_blocks[operation.when_true],
                  instruction_blocks[operation.when_false]);

              builder.SetInsertPoint(unknown_block);
              return_result(
                  FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR, instruction, 0,
                  FSIM_JIT_FRAME_STATE_RUNTIME_ERROR, instruction);
            },
            [&](const WaitFor &operation) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_WAIT_FOR, instruction,
                  operation.delay, FSIM_JIT_FRAME_STATE_READY,
                  next_instruction);
            },
            [&](const WaitOn &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_WAIT_ON, instruction, 0,
                  FSIM_JIT_FRAME_STATE_READY, next_instruction);
            },
            [&](const WaitSensitivity &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY, instruction, 0,
                  FSIM_JIT_FRAME_STATE_READY, next_instruction);
            },
            [&](const Yield &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_YIELDED, instruction, 0,
                  FSIM_JIT_FRAME_STATE_READY, next_instruction);
            },
            [&](const Stop &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_STOPPED, instruction, 0,
                  FSIM_JIT_FRAME_STATE_STOPPED, next_instruction);
            },
            [&](const Halt &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_COMPLETED, instruction, 0,
                  FSIM_JIT_FRAME_STATE_COMPLETED, next_instruction);
            },
            [&](const auto &) {
              llvm_unreachable(
                  "unsupported operations were rejected before lowering");
            }},
        process.operations[index]);
  }

}

std::once_flag native_target_once;
std::string native_target_error;

void initialize_native_target() {
  std::call_once(native_target_once, [] {
    if (llvm::InitializeNativeTarget()) {
      native_target_error = "LLVM failed to initialize the native target";
      return;
    }
    if (llvm::InitializeNativeTargetAsmPrinter()) {
      native_target_error =
          "LLVM failed to initialize the native assembly printer";
    }
  });
  if (!native_target_error.empty()) {
    throw LlvmJitError(native_target_error);
  }
}

} // namespace

LlvmJitGeneratedRuntimeError::LlvmJitGeneratedRuntimeError(
    const std::uint32_t instruction,
    const JitGeneratedRuntimeErrorReason reason)
    : LlvmJitError(generated_runtime_error_message(instruction, reason)),
      instruction_(instruction), reason_(reason) {}

struct LlvmJit::Impl {
  struct ProcessInfo {
    JitProcessFrameLayout frame_layout;
    std::uint32_t operation_count{};
    bool requires_resume{};
    bool uses_write_update{};
    bool uses_write_after{};
    bool uses_debug_points{};
  };

  struct NativeEntry {
    NativeProcess *function{};
    ProcessInfo info;
  };

  LlvmJitOptions options;
  std::unique_ptr<PersistentLlvmObjectCache> object_cache;
  std::unique_ptr<llvm::orc::LLJIT> jit;
  std::string target_cpu;
  std::vector<std::string> target_features;
  std::unordered_set<std::string> module_identities;
  std::unordered_set<std::string> symbols;
  std::unordered_map<std::string, ProcessInfo> info_by_symbol;
  std::unordered_map<std::string, JitProcessHandle> handles_by_symbol;
  std::unordered_map<std::uint64_t, NativeEntry> functions;
  std::uint64_t next_handle = 1;
};

LlvmJit::LlvmJit(const LlvmJitOptions options)
    : impl_(std::make_unique<Impl>()) {
  initialize_native_target();
  impl_->options = options;

  auto target_builder = unwrap(
      llvm::orc::JITTargetMachineBuilder::detectHost(),
      "cannot detect the native LLVM target");
  target_builder.setCodeGenOptLevel(
      options.optimization == JitOptimizationLevel::o0
          ? llvm::CodeGenOptLevel::None
          : llvm::CodeGenOptLevel::Default);
  impl_->target_cpu = target_builder.getCPU();
  impl_->target_features = target_builder.getFeatures().getFeatures();
  std::sort(impl_->target_features.begin(), impl_->target_features.end());

  llvm::orc::LLJITBuilder builder;
  if (!options.cache_directory.empty()) {
    impl_->object_cache = std::make_unique<PersistentLlvmObjectCache>(
        options.cache_directory / "llvm" / "objects",
        target_builder.getTargetTriple());
    auto *const object_cache = impl_->object_cache.get();
    builder.setCompileFunctionCreator(
        [object_cache](llvm::orc::JITTargetMachineBuilder machine_builder)
            -> llvm::Expected<std::unique_ptr<
                llvm::orc::IRCompileLayer::IRCompiler>> {
          auto target_machine = machine_builder.createTargetMachine();
          if (!target_machine) {
            return target_machine.takeError();
          }
          std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler> compiler =
              std::make_unique<llvm::orc::TMOwningSimpleCompiler>(
                  std::move(*target_machine), object_cache);
          return compiler;
        });
  }
  builder.setJITTargetMachineBuilder(std::move(target_builder));
  impl_->jit = unwrap(builder.create(), "cannot create LLVM LLJIT");
}

LlvmJit::~LlvmJit() = default;
LlvmJit::LlvmJit(LlvmJit &&) noexcept = default;
LlvmJit &LlvmJit::operator=(LlvmJit &&) noexcept = default;

bool LlvmJit::supports_process(
    const Process& process,
    const std::span<const std::uint32_t> signal_widths) const {
  if (!impl_) {
    throw LlvmJitError("cannot use a moved-from LlvmJit");
  }
  try {
    (void)validate_process(process, signal_widths);
    return true;
  } catch (const LlvmJitUnsupportedError&) {
    return false;
  }
}

void LlvmJit::add_process_module(
    const std::string_view module_identity,
    const std::span<const JitProcessModuleEntry> entries,
    const std::span<const std::uint32_t> signal_widths) {
  if (!impl_) {
    throw LlvmJitError("cannot use a moved-from LlvmJit");
  }
  if (module_identity.empty()) {
    throw LlvmJitError("LLVM process module identity cannot be empty");
  }
  const std::string owned_module_identity{module_identity};
  if (entries.empty()) {
    throw LlvmJitError("LLVM process module cannot be empty");
  }

  struct PreparedProcess {
    std::string symbol;
    const Process* process{};
    ValidatedProcess validated;
    std::string cache_key;
    Impl::ProcessInfo info;
  };
  std::vector<PreparedProcess> prepared;
  prepared.reserve(entries.size());
  std::unordered_set<std::string> module_symbols;
  std::vector<std::string> process_keys;
  process_keys.reserve(entries.size());

  for (const auto& entry : entries) {
    if (entry.process == nullptr) {
      throw LlvmJitError("LLVM process module entry has no SimIR process");
    }
    if (!valid_symbol(entry.symbol)) {
      throw LlvmJitError(
          "LLVM process symbol must be a non-empty C identifier");
    }
    std::string owned_symbol{entry.symbol};
    if (impl_->symbols.contains(owned_symbol) ||
        !module_symbols.insert(owned_symbol).second) {
      throw LlvmJitError(
          "duplicate LLVM process symbol '" + owned_symbol + "'");
    }

    auto validated = validate_process(*entry.process, signal_widths);
    auto cache_key = make_native_object_cache_key(
        owned_symbol, *entry.process, signal_widths,
        impl_->options.optimization, impl_->jit->getTargetTriple(),
        impl_->jit->getDataLayout(), impl_->target_cpu,
        impl_->target_features);
    const Impl::ProcessInfo process_info{
        make_frame_layout(cache_key, entry.process->register_count),
        static_cast<std::uint32_t>(entry.process->operations.size()),
        validated.requires_resume,
        validated.uses_write_update,
        validated.uses_write_after,
        validated.uses_debug_points,
    };
    process_keys.push_back(cache_key);
    prepared.push_back(
        {std::move(owned_symbol), entry.process, std::move(validated),
         std::move(cache_key), process_info});
  }
  if (impl_->module_identities.contains(owned_module_identity)) {
    throw LlvmJitError(
        "duplicate LLVM process module identity '" +
        owned_module_identity + "'");
  }

  const auto module_cache_key = make_native_module_cache_key(
      owned_module_identity, process_keys);
  auto context = std::make_unique<llvm::LLVMContext>();
  auto module = std::make_unique<llvm::Module>(
      owned_module_identity + ".module", *context);
  module->setDataLayout(impl_->jit->getDataLayout());
  module->setTargetTriple(impl_->jit->getTargetTriple());
  for (const auto& item : prepared) {
    lower_process(
        *module, item.symbol, *item.process, signal_widths,
        item.validated,
        impl_->options.optimization == JitOptimizationLevel::o0);
  }
  if (auto message = verify_error(*module); !message.empty()) {
    throw LlvmJitError(
        "generated invalid LLVM IR for module '" +
        owned_module_identity + "': " + message);
  }
  if (impl_->object_cache) {
    module->setModuleIdentifier(module_cache_key);
  }
  optimize_module(*module, impl_->options.optimization);
  if (auto message = verify_error(*module); !message.empty()) {
    throw LlvmJitError(
        "LLVM optimization produced invalid IR for module '" +
        owned_module_identity + "': " + message);
  }

  if (auto error = impl_->jit->addIRModule(llvm::orc::ThreadSafeModule(
          std::move(module), std::move(context)))) {
    throw LlvmJitError(
        "cannot add LLVM process module '" + owned_module_identity +
        "': " + llvm_error(std::move(error)));
  }
  impl_->module_identities.insert(owned_module_identity);
  for (auto& item : prepared) {
    impl_->symbols.insert(item.symbol);
    impl_->info_by_symbol.emplace(
        std::move(item.symbol), item.info);
  }
}

void LlvmJit::add_process(
    const std::string_view symbol, const Process &process,
    const std::span<const std::uint32_t> signal_widths) {
  const std::array entries{
      JitProcessModuleEntry{symbol, &process}};
  add_process_module(symbol, entries, signal_widths);
}

JitProcessHandle LlvmJit::lookup(const std::string_view symbol) {
  if (!impl_) {
    throw LlvmJitError("cannot use a moved-from LlvmJit");
  }
  const std::string owned_symbol{symbol};
  if (!impl_->symbols.contains(owned_symbol)) {
    throw LlvmJitError("LLVM process symbol was not added: '" + owned_symbol +
                       "'");
  }
  if (const auto found = impl_->handles_by_symbol.find(owned_symbol);
      found != impl_->handles_by_symbol.end()) {
    return found->second;
  }

  auto address = unwrap(impl_->jit->lookup(owned_symbol),
                        "cannot materialize LLVM process '" + owned_symbol +
                            "'");
  auto *function = address.template toPtr<NativeProcess>();
  if (function == nullptr) {
    throw LlvmJitError("LLVM returned a null process address for '" +
                       owned_symbol + "'");
  }
  if (impl_->next_handle == 0) {
    throw LlvmJitError("LLVM process handle space is exhausted");
  }
  const JitProcessHandle handle{impl_->next_handle++};
  const auto info = impl_->info_by_symbol.find(owned_symbol);
  if (info == impl_->info_by_symbol.end()) {
    throw LlvmJitError("LLVM process frame metadata is missing for '" +
                       owned_symbol + "'");
  }
  impl_->functions.emplace(
      handle.value, Impl::NativeEntry{function, info->second});
  impl_->handles_by_symbol.emplace(owned_symbol, handle);
  return handle;
}

JitProcessFrameLayout
LlvmJit::frame_layout(const JitProcessHandle process) const {
  if (!impl_) {
    throw LlvmJitError("cannot use a moved-from LlvmJit");
  }
  const auto found = impl_->functions.find(process.value);
  if (process.value == 0 || found == impl_->functions.end()) {
    throw LlvmJitError("invalid LLVM process handle");
  }
  return found->second.info.frame_layout;
}

void LlvmJit::initialize_frame(
    const JitProcessHandle process, fsim_jit_frame_v1 &frame,
    const std::span<std::uint64_t> register_aval,
    const std::span<std::uint64_t> register_bval) const {
  const auto layout = frame_layout(process);
  if (register_aval.size() < layout.register_count ||
      register_bval.size() < layout.register_count) {
    throw LlvmJitError(
        "caller-owned JIT register storage is smaller than the frame layout");
  }
  if (layout.register_count != 0 &&
      register_aval.data() == register_bval.data()) {
    throw LlvmJitError(
        "caller-owned JIT aval and bval register storage must be distinct");
  }
  std::fill_n(register_aval.begin(), layout.register_count, UINT64_C(0));
  std::fill_n(register_bval.begin(), layout.register_count, UINT64_C(0));
  frame = {
      FSIM_JIT_FRAME_ABI_VERSION_V1,
      static_cast<std::uint32_t>(sizeof(fsim_jit_frame_v1)),
      layout.layout_id_low,
      layout.layout_id_high,
      layout.register_count,
      0,
      FSIM_JIT_FRAME_STATE_READY,
      FSIM_JIT_INVALID_INSTRUCTION,
      register_aval.data(),
      register_bval.data(),
  };
}

JitResumeStatus
LlvmJit::resume(const JitProcessHandle process,
                const fsim_jit_runtime_v1 &runtime,
                fsim_jit_frame_v1 &frame,
                fsim_jit_resume_result_v1 &result) const {
  if (!impl_) {
    throw LlvmJitError("cannot use a moved-from LlvmJit");
  }
  if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1) {
    throw LlvmJitError("JIT runtime ABI version mismatch");
  }
  if (runtime.struct_size < kJitRuntimeV1PrefixSize) {
    throw LlvmJitError("JIT runtime ABI structure is too small");
  }
  if (runtime.read_signal == nullptr || runtime.write_signal == nullptr ||
      runtime.assert_failed == nullptr) {
    throw LlvmJitError("JIT runtime ABI requires all v1 callbacks");
  }
  if (result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1) {
    throw LlvmJitError("JIT resume-result ABI version mismatch");
  }
  if (result.struct_size < sizeof(fsim_jit_resume_result_v1)) {
    throw LlvmJitError("JIT resume-result ABI structure is too small");
  }

  const auto found = impl_->functions.find(process.value);
  if (process.value == 0 || found == impl_->functions.end()) {
    throw LlvmJitError("invalid LLVM process handle");
  }
  const auto &entry = found->second;
  if (entry.info.uses_write_update) {
    if (runtime.struct_size <
        offsetof(fsim_jit_runtime_v1, write_after)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include write_update");
    }
    if (runtime.write_update == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_update for this process");
    }
  }
  if (entry.info.uses_write_after) {
    if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, flags)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include write_after");
    }
    if (runtime.write_after == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_after for this process");
    }
  }
  if (entry.info.uses_debug_points
      && runtime.struct_size
          < offsetof(fsim_jit_runtime_v1, reserved)) {
    throw LlvmJitError(
        "JIT runtime ABI structure does not include debug-point flags");
  }
  if (frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V1) {
    throw LlvmJitError("JIT frame ABI version mismatch");
  }
  if (frame.struct_size < sizeof(fsim_jit_frame_v1)) {
    throw LlvmJitError("JIT frame ABI structure is too small");
  }
  if (frame.layout_id_low != entry.info.frame_layout.layout_id_low ||
      frame.layout_id_high != entry.info.frame_layout.layout_id_high ||
      frame.register_count != entry.info.frame_layout.register_count) {
    throw LlvmJitError("JIT frame layout mismatch");
  }
  if (frame.register_count != 0 &&
      (frame.register_aval == nullptr || frame.register_bval == nullptr)) {
    throw LlvmJitError("JIT frame register storage is null");
  }
  if (frame.register_count != 0 &&
      frame.register_aval == frame.register_bval) {
    throw LlvmJitError(
        "JIT frame aval and bval register storage must be distinct");
  }

  const auto terminal_result =
      [&](const std::uint32_t status) -> JitResumeStatus {
    result.status = status;
    result.instruction = frame.last_instruction;
    result.delay = 0;
    return static_cast<JitResumeStatus>(status);
  };
  switch (frame.state) {
  case FSIM_JIT_FRAME_STATE_READY:
    if (frame.program_counter >= entry.info.operation_count) {
      throw LlvmJitError(
          "JIT frame program counter is outside the operation stream");
    }
    break;
  case FSIM_JIT_FRAME_STATE_COMPLETED:
    return terminal_result(FSIM_JIT_RESUME_STATUS_COMPLETED);
  case FSIM_JIT_FRAME_STATE_STOPPED:
    return terminal_result(FSIM_JIT_RESUME_STATUS_STOPPED);
  case FSIM_JIT_FRAME_STATE_ASSERTION_FAILED:
    return terminal_result(FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED);
  case FSIM_JIT_FRAME_STATE_RUNTIME_ERROR:
    throw LlvmJitGeneratedRuntimeError(
        frame.last_instruction,
        JitGeneratedRuntimeErrorReason::unknown_branch_condition);
  default:
    throw LlvmJitError("JIT frame state is invalid");
  }

  const auto raw_status =
      entry.function(&runtime, &frame, &result);
  if (raw_status != result.status) {
    throw LlvmJitError(
        "generated process returned an inconsistent resume status");
  }
  switch (raw_status) {
  case FSIM_JIT_RESUME_STATUS_COMPLETED:
    if (frame.state != FSIM_JIT_FRAME_STATE_COMPLETED) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::completed;
  case FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED:
    if (frame.state != FSIM_JIT_FRAME_STATE_ASSERTION_FAILED) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::assertion_failed;
  case FSIM_JIT_RESUME_STATUS_WAIT_FOR:
    if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::wait_for;
  case FSIM_JIT_RESUME_STATUS_WAIT_ON:
    if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::wait_on;
  case FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY:
    if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::wait_sensitivity;
  case FSIM_JIT_RESUME_STATUS_DEBUG_POINT:
    if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::debug_point;
  case FSIM_JIT_RESUME_STATUS_YIELDED:
    if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::yielded;
  case FSIM_JIT_RESUME_STATUS_STOPPED:
    if (frame.state != FSIM_JIT_FRAME_STATE_STOPPED) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::stopped;
  case FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR:
    if (frame.state != FSIM_JIT_FRAME_STATE_RUNTIME_ERROR) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    throw LlvmJitGeneratedRuntimeError(
        result.instruction,
        JitGeneratedRuntimeErrorReason::unknown_branch_condition);
  default:
    throw LlvmJitError("generated process returned an unknown resume status");
  }
}

JitExecutionStatus
LlvmJit::execute(const JitProcessHandle process,
                 const fsim_jit_runtime_v1 &runtime) const {
  if (!impl_) {
    throw LlvmJitError("cannot use a moved-from LlvmJit");
  }
  const auto found = impl_->functions.find(process.value);
  if (process.value == 0 || found == impl_->functions.end()) {
    throw LlvmJitError("invalid LLVM process handle");
  }
  if (found->second.info.requires_resume) {
    throw LlvmJitUnsupportedError(
        "compiled process can suspend; use initialize_frame() and resume()");
  }

  std::vector<std::uint64_t> register_aval(
      found->second.info.frame_layout.register_count);
  std::vector<std::uint64_t> register_bval(
      found->second.info.frame_layout.register_count);
  fsim_jit_frame_v1 frame{};
  initialize_frame(process, frame, register_aval, register_bval);
  fsim_jit_resume_result_v1 result{
      FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
      static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v1)),
      0,
      FSIM_JIT_INVALID_INSTRUCTION,
      0,
  };
  switch (resume(process, runtime, frame, result)) {
  case JitResumeStatus::completed:
    return JitExecutionStatus::completed;
  case JitResumeStatus::assertion_failed:
    return JitExecutionStatus::assertion_failed;
  case JitResumeStatus::stopped:
    return JitExecutionStatus::stopped;
  case JitResumeStatus::wait_for:
  case JitResumeStatus::wait_on:
  case JitResumeStatus::wait_sensitivity:
  case JitResumeStatus::yielded:
  case JitResumeStatus::debug_point:
    throw LlvmJitError(
        "compiled process suspended during one-shot execution");
  default:
    throw LlvmJitError("generated process returned an unknown resume status");
  }
}

LlvmJitCacheStatistics LlvmJit::cache_statistics() const noexcept {
  if (!impl_ || !impl_->object_cache) {
    return {};
  }
  return impl_->object_cache->statistics();
}

std::string_view LlvmJit::llvm_version() noexcept {
  return LLVM_VERSION_STRING;
}

} // namespace fsim::compiler
