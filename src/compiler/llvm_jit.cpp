// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit.hpp"
#include "llvm_jit_internal.hpp"

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

using namespace llvm_detail;
using runtime::Logic9;
using runtime::simir::Process;
using runtime::simir::ValueKind;
using NativeProcess = fsim_jit_process_v1;

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
static_assert(offsetof(fsim_jit_runtime_v1, write_signal_slice) == 64);
static_assert(offsetof(fsim_jit_runtime_v1, write_update_slice) == 72);
static_assert(offsetof(fsim_jit_runtime_v1, write_after_slice) == 80);
static_assert(offsetof(fsim_jit_runtime_v1, signal_event) == 88);
static_assert(offsetof(fsim_jit_runtime_v1, signal_last_value) == 96);
static_assert(offsetof(fsim_jit_runtime_v1, signal_last_event) == 104);
static_assert(offsetof(fsim_jit_runtime_v1, signal_active) == 112);
static_assert(offsetof(fsim_jit_runtime_v1, write_output) == 120);
static_assert(offsetof(fsim_jit_runtime_v1, schedule_output) == 128);
static_assert(offsetof(fsim_jit_runtime_v1, write_report) == 136);
static_assert(offsetof(fsim_jit_runtime_v1, write_formatted) == 144);
static_assert(offsetof(fsim_jit_runtime_v1, write_time) == 152);
static_assert(offsetof(fsim_jit_runtime_v1, install_monitor) == 160);
static_assert(offsetof(fsim_jit_runtime_v1, control_monitor) == 168);
static_assert(offsetof(fsim_jit_runtime_v1, random_value) == 176);
static_assert(offsetof(fsim_jit_runtime_v1, write_inertial) == 184);
static_assert(
    offsetof(fsim_jit_runtime_v1, write_inertial_slice) == 192);
static_assert(offsetof(fsim_jit_runtime_v1, write_projected) == 200);
static_assert(
    offsetof(fsim_jit_runtime_v1, write_projected_slice) == 208);
static_assert(
    offsetof(fsim_jit_runtime_v1, write_projected_waveform) == 216);
static_assert(
    offsetof(fsim_jit_runtime_v1, write_projected_waveform_slice) == 224);
static_assert(
    offsetof(fsim_jit_runtime_v1, read_signal_logic9) == 232);
static_assert(
    offsetof(fsim_jit_runtime_v1, write_formatted_logic9) == 344);
static_assert(offsetof(fsim_jit_runtime_v1, load_string) == 352);
static_assert(
    offsetof(fsim_jit_runtime_v1, write_string_output) == 424);
static_assert(offsetof(fsim_jit_runtime_v1, file_open) == 432);
static_assert(offsetof(fsim_jit_runtime_v1, file_error) == 472);
static_assert(
    offsetof(fsim_jit_runtime_v1, container_operation) == 480);
static_assert(offsetof(fsim_jit_runtime_v1, force_signal_slice) == 488);
static_assert(
    offsetof(fsim_jit_runtime_v1, force_signal_slice_logic9) == 496);
static_assert(offsetof(fsim_jit_runtime_v1, release_signal_slice) == 504);
static_assert(sizeof(fsim_jit_runtime_v1) == 512);
static_assert(sizeof(fsim_jit_projected_element_v1) == 24);
static_assert(sizeof(fsim_jit_logic9_word_v1) == 32);
static_assert(sizeof(fsim_jit_logic9_projected_element_v1) == 40);
static_assert(sizeof(fsim_jit_frame_v1) == 80);
static_assert(offsetof(fsim_jit_frame_v1, register_aval) == 40);
static_assert(offsetof(fsim_jit_frame_v1, register_bval) == 48);
static_assert(offsetof(fsim_jit_frame_v1, register_initialized) == 56);
static_assert(
    offsetof(fsim_jit_frame_v1, register_logic9_plane2) == 64);
static_assert(
    offsetof(fsim_jit_frame_v1, register_logic9_plane3) == 72);
static_assert(sizeof(fsim_jit_resume_result_v1) == 24);

constexpr auto kJitRuntimeV1PrefixSize =
    static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_update));
constexpr auto kJitFrameV1PrefixSize =
    static_cast<std::uint32_t>(
        offsetof(fsim_jit_frame_v1, register_logic9_plane2));
constexpr auto kJitRuntimeLogic9Size =
    static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, load_string));
constexpr auto kJitRuntimeStringSize =
    static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, file_open));
constexpr auto kJitRuntimeFileSize =
    static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, container_operation));
constexpr auto kJitRuntimeForceSize =
    static_cast<std::uint32_t>(sizeof(fsim_jit_runtime_v1));

class PersistentLlvmObjectCache final : public llvm::ObjectCache {
public:
  PersistentLlvmObjectCache(std::filesystem::path root,
                            llvm::Triple target_triple,
                            const LlvmJitOptions& options)
      : storage_(std::move(root)), target_triple_(std::move(target_triple)) {
    ObjectCachePruneOptions prune_options;
    prune_options.maximum_bytes = options.cache_maximum_bytes;
    prune_options.maximum_entries = options.cache_maximum_entries;
    prune_options.maximum_age = options.cache_maximum_age;
    ObjectCachePruneResult result;
    std::error_code error;
    if (storage_.prune(prune_options, result, error)) {
      pruned_entries_.store(
          result.removed_entries, std::memory_order_relaxed);
      pruned_bytes_.store(
          result.bytes_removed, std::memory_order_relaxed);
      if (result.failed_removals != 0) {
        prune_failures_.store(
            result.failed_removals, std::memory_order_relaxed);
      }
    } else {
      prune_failures_.store(1, std::memory_order_relaxed);
    }
  }

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
        pruned_entries_.load(std::memory_order_relaxed),
        pruned_bytes_.load(std::memory_order_relaxed),
        prune_failures_.load(std::memory_order_relaxed),
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
    return (*parsed)->isRelocatableObject() &&
           (*parsed)->getBytesInAddress() == sizeof(void *) &&
           (*parsed)->getArch() == target_triple_.getArch() &&
           (*parsed)->getTripleObjectFormat() ==
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
  std::atomic_uint64_t pruned_entries_{};
  std::atomic<std::uintmax_t> pruned_bytes_{};
  std::atomic_uint64_t prune_failures_{};
};

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

}  // namespace

using namespace llvm_detail;

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
    bool uses_write_inertial{};
    bool uses_write_projected{};
    bool uses_write_projected_waveform{};
    bool uses_write_blocking_slice{};
    bool uses_write_update_slice{};
    bool uses_write_after_slice{};
    bool uses_write_inertial_slice{};
    bool uses_write_projected_slice{};
    bool uses_write_projected_waveform_slice{};
    bool uses_force_signal_slice{};
    bool uses_release_signal_slice{};
    bool uses_debug_points{};
    bool uses_signal_event{};
    bool uses_signal_last_value{};
    bool uses_signal_last_event{};
    bool uses_signal_active{};
    bool uses_output{};
    bool uses_postponed_output{};
    bool uses_report{};
    bool uses_formatted_output{};
    bool uses_time_output{};
    bool uses_monitor_install{};
    bool uses_monitor_control{};
    bool uses_random_value{};
    bool uses_strings{};
    bool uses_files{};
    bool uses_containers{};
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
        target_builder.getTargetTriple(),
        options);
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
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds) const {
  if (!impl_) {
    throw LlvmJitError("cannot use a moved-from LlvmJit");
  }
  try {
    (void)validate_process(
        process, signal_widths, signal_value_kinds);
    return true;
  } catch (const LlvmJitUnsupportedError&) {
    return false;
  }
}

void LlvmJit::add_process_module(
    const std::string_view module_identity,
    const std::span<const JitProcessModuleEntry> entries,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds) {
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

    auto validated = validate_process(
        *entry.process, signal_widths, signal_value_kinds);
    auto cache_key = make_native_object_cache_key(
        owned_symbol, *entry.process, signal_widths,
        signal_value_kinds,
        impl_->options.optimization, impl_->jit->getTargetTriple(),
        impl_->jit->getDataLayout(), impl_->target_cpu,
        impl_->target_features);
    const Impl::ProcessInfo process_info{
        make_frame_layout(
            cache_key,
            entry.process->register_count,
            entry.process->string_register_count,
            validated.uses_logic9),
        static_cast<std::uint32_t>(entry.process->operations.size()),
        validated.requires_resume,
        validated.uses_write_update,
        validated.uses_write_after,
        validated.uses_write_inertial,
        validated.uses_write_projected,
        validated.uses_write_projected_waveform,
        validated.uses_write_blocking_slice,
        validated.uses_write_update_slice,
        validated.uses_write_after_slice,
        validated.uses_write_inertial_slice,
        validated.uses_write_projected_slice,
        validated.uses_write_projected_waveform_slice,
        validated.uses_force_signal_slice,
        validated.uses_release_signal_slice,
        validated.uses_debug_points,
        validated.uses_signal_event,
        validated.uses_signal_last_value,
        validated.uses_signal_last_event,
        validated.uses_signal_active,
        validated.uses_output,
        validated.uses_postponed_output,
        validated.uses_report,
        validated.uses_formatted_output,
        validated.uses_time_output,
        validated.uses_monitor_install,
        validated.uses_monitor_control,
        validated.uses_random_value,
        validated.uses_strings,
        validated.uses_files,
        validated.uses_containers,
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
        signal_value_kinds,
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
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds) {
  const std::array entries{
      JitProcessModuleEntry{symbol, &process}};
  add_process_module(
      symbol, entries, signal_widths, signal_value_kinds);
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
    const std::span<std::uint64_t> register_bval,
    const std::span<std::uint8_t> register_initialized,
    const std::span<std::uint64_t> register_logic9_plane2,
    const std::span<std::uint64_t> register_logic9_plane3) const {
  const auto layout = frame_layout(process);
  if (register_aval.size() < layout.register_count ||
      register_bval.size() < layout.register_count
      || register_initialized.size() < layout.register_count
      || (layout.uses_logic9
          && (register_logic9_plane2.size() < layout.register_count
              || register_logic9_plane3.size()
                  < layout.register_count))) {
    throw LlvmJitError(
        "caller-owned JIT register storage is smaller than the frame layout");
  }
  if (layout.register_count != 0 &&
      (register_aval.data() == register_bval.data()
       || (layout.uses_logic9
           && (register_logic9_plane2.data()
                   == register_logic9_plane3.data()
               || register_logic9_plane2.data()
                   == register_aval.data()
               || register_logic9_plane2.data()
                   == register_bval.data()
               || register_logic9_plane3.data()
                   == register_aval.data()
               || register_logic9_plane3.data()
                   == register_bval.data())))) {
    throw LlvmJitError(
        "caller-owned JIT register planes must be distinct");
  }
  std::fill_n(register_aval.begin(), layout.register_count, UINT64_C(0));
  std::fill_n(register_bval.begin(), layout.register_count, UINT64_C(0));
  if (layout.uses_logic9) {
    std::fill_n(
        register_logic9_plane2.begin(),
        layout.register_count,
        UINT64_C(0));
    std::fill_n(
        register_logic9_plane3.begin(),
        layout.register_count,
        UINT64_C(0));
  }
  std::fill_n(
      register_initialized.begin(), layout.register_count, UINT8_C(0));
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
      register_initialized.data(),
      layout.uses_logic9
          ? register_logic9_plane2.data()
          : nullptr,
      layout.uses_logic9
          ? register_logic9_plane3.data()
          : nullptr,
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
  if (entry.info.uses_write_inertial) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, write_inertial_slice)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include write_inertial");
    }
    if (runtime.write_inertial == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_inertial for this process");
    }
  }
  if (entry.info.uses_write_blocking_slice) {
    if (runtime.struct_size
        < offsetof(
            fsim_jit_runtime_v1, write_update_slice)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include "
          "write_signal_slice");
    }
    if (runtime.write_signal_slice == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_signal_slice for this "
          "process");
    }
  }
  if (entry.info.uses_write_update_slice) {
    if (runtime.struct_size
        < offsetof(
            fsim_jit_runtime_v1, write_after_slice)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include "
          "write_update_slice");
    }
    if (runtime.write_update_slice == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_update_slice for this "
          "process");
    }
  }
  if (entry.info.uses_write_after_slice) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, signal_event)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include "
          "write_after_slice");
    }
    if (runtime.write_after_slice == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_after_slice for this "
          "process");
    }
  }
  if (entry.info.uses_force_signal_slice) {
    if (runtime.struct_size < kJitRuntimeForceSize) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include force_signal_slice");
    }
    if (runtime.force_signal_slice == nullptr
        || (entry.info.frame_layout.uses_logic9
            && runtime.force_signal_slice_logic9 == nullptr)) {
      throw LlvmJitError(
          "JIT runtime ABI requires force_signal_slice callbacks for this "
          "process");
    }
  }
  if (entry.info.uses_release_signal_slice) {
    if (runtime.struct_size < kJitRuntimeForceSize
        || runtime.release_signal_slice == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires release_signal_slice for this process");
    }
  }
  if (entry.info.uses_write_inertial_slice) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, write_projected)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include "
          "write_inertial_slice");
    }
    if (runtime.write_inertial_slice == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_inertial_slice for this "
          "process");
    }
  }
  if (entry.info.uses_write_projected) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, write_projected_slice)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include "
          "write_projected");
    }
    if (runtime.write_projected == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_projected for this process");
    }
  }
  if (entry.info.uses_write_projected_slice) {
    if (runtime.struct_size
        < offsetof(
            fsim_jit_runtime_v1, write_projected_waveform)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include "
          "write_projected_slice");
    }
    if (runtime.write_projected_slice == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_projected_slice for this "
          "process");
    }
  }
  if (entry.info.uses_write_projected_waveform) {
    if (runtime.struct_size
        < offsetof(
            fsim_jit_runtime_v1,
            write_projected_waveform_slice)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include "
          "write_projected_waveform");
    }
    if (runtime.write_projected_waveform == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_projected_waveform for this "
          "process");
    }
  }
  if (entry.info.uses_write_projected_waveform_slice) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, read_signal_logic9)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include "
          "write_projected_waveform_slice");
    }
    if (runtime.write_projected_waveform_slice == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_projected_waveform_slice for this "
          "process");
    }
  }
  if (entry.info.uses_debug_points
      && runtime.struct_size
          < offsetof(fsim_jit_runtime_v1, reserved)) {
    throw LlvmJitError(
        "JIT runtime ABI structure does not include debug-point flags");
  }
  if (entry.info.uses_signal_event) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, signal_last_value)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include signal_event");
    }
    if (runtime.signal_event == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires signal_event for this process");
    }
  }
  if (entry.info.uses_signal_last_value) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, signal_last_event)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include signal_last_value");
    }
    if (runtime.signal_last_value == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires signal_last_value for this process");
    }
  }
  if (entry.info.uses_signal_last_event) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, signal_active)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include signal_last_event");
    }
    if (runtime.signal_last_event == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires signal_last_event for this process");
    }
  }
  if (entry.info.uses_signal_active) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, write_output)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include signal_active");
    }
    if (runtime.signal_active == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires signal_active for this process");
    }
  }
  if (entry.info.uses_output) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, schedule_output)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include write_output");
    }
    if (runtime.write_output == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_output for this process");
    }
  }
  if (entry.info.uses_postponed_output) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, write_report)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include schedule_output");
    }
    if (runtime.schedule_output == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires schedule_output for this process");
    }
  }
  if (entry.info.uses_report) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, write_formatted)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include write_report");
    }
    if (runtime.write_report == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_report for this process");
    }
  }
  if (entry.info.uses_formatted_output) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, write_time)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include write_formatted");
    }
    if (runtime.write_formatted == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_formatted for this process");
    }
  }
  if (entry.info.uses_time_output) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, install_monitor)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include write_time");
    }
    if (runtime.write_time == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires write_time for this process");
    }
  }
  if (entry.info.uses_monitor_install) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, control_monitor)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include install_monitor");
    }
    if (runtime.install_monitor == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires install_monitor for this process");
    }
  }
  if (entry.info.uses_monitor_control) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, random_value)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include control_monitor");
    }
    if (runtime.control_monitor == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires control_monitor for this process");
    }
  }
  if (entry.info.uses_random_value) {
    if (runtime.struct_size
        < offsetof(fsim_jit_runtime_v1, write_inertial)) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include random_value");
    }
    if (runtime.random_value == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires random_value for this process");
    }
  }
  if (entry.info.frame_layout.uses_logic9) {
    if (runtime.struct_size < kJitRuntimeLogic9Size) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include Logic9 callbacks");
    }
    if (runtime.read_signal_logic9 == nullptr
        || runtime.write_signal_logic9 == nullptr
        || runtime.write_update_logic9 == nullptr
        || runtime.write_after_logic9 == nullptr
        || runtime.write_signal_slice_logic9 == nullptr
        || runtime.write_update_slice_logic9 == nullptr
        || runtime.write_after_slice_logic9 == nullptr
        || runtime.signal_last_value_logic9 == nullptr
        || runtime.write_inertial_logic9 == nullptr
        || runtime.write_inertial_slice_logic9 == nullptr
        || runtime.write_projected_logic9 == nullptr
        || runtime.write_projected_slice_logic9 == nullptr
        || runtime.write_projected_waveform_logic9 == nullptr
        || runtime.write_projected_waveform_slice_logic9 == nullptr
        || runtime.write_formatted_logic9 == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires Logic9 callbacks for this process");
    }
  }
  if (entry.info.uses_strings) {
    if (runtime.struct_size < kJitRuntimeStringSize) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include mutable-string "
          "callbacks");
    }
    if (runtime.load_string == nullptr
        || runtime.copy_string == nullptr
        || runtime.read_string_object == nullptr
        || runtime.write_string_object == nullptr
        || runtime.concatenate_strings == nullptr
        || runtime.compare_strings == nullptr
        || runtime.string_length == nullptr
        || runtime.string_index == nullptr
        || runtime.string_replace_byte == nullptr
        || runtime.write_string_output == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires mutable-string callbacks for this "
          "process");
    }
  }
  if (entry.info.uses_files) {
    if (runtime.struct_size < kJitRuntimeFileSize) {
      throw LlvmJitError(
          "JIT runtime ABI structure does not include text-file callbacks");
    }
    if (runtime.file_open == nullptr
        || runtime.file_close == nullptr
        || runtime.file_write == nullptr
        || runtime.file_read_line == nullptr
        || runtime.file_end_of_file == nullptr
        || runtime.file_error == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires text-file callbacks for this process");
    }
  }
  if (entry.info.uses_containers) {
    if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
        || runtime.container_operation == nullptr) {
      throw LlvmJitError(
          "JIT runtime ABI requires bounded-container callbacks for this "
          "process");
    }
  }
  if (frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V1) {
    throw LlvmJitError("JIT frame ABI version mismatch");
  }
  if (frame.struct_size < kJitFrameV1PrefixSize
      || (entry.info.frame_layout.uses_logic9
          && frame.struct_size < sizeof(fsim_jit_frame_v1))) {
    throw LlvmJitError("JIT frame ABI structure is too small");
  }
  if (frame.layout_id_low != entry.info.frame_layout.layout_id_low ||
      frame.layout_id_high != entry.info.frame_layout.layout_id_high ||
      frame.register_count != entry.info.frame_layout.register_count) {
    throw LlvmJitError("JIT frame layout mismatch");
  }
  if (frame.register_count != 0 &&
      (frame.register_aval == nullptr || frame.register_bval == nullptr
       || frame.register_initialized == nullptr)) {
    throw LlvmJitError("JIT frame register storage is null");
  }
  if (frame.register_count != 0 &&
      frame.register_aval == frame.register_bval) {
    throw LlvmJitError(
        "JIT frame aval and bval register storage must be distinct");
  }
  if (entry.info.frame_layout.uses_logic9
      && frame.register_count != 0
      && (frame.register_logic9_plane2 == nullptr
          || frame.register_logic9_plane3 == nullptr)) {
    throw LlvmJitError("JIT frame Logic9 register storage is null");
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
    if (const auto reason =
            decode_generated_runtime_error(frame.program_counter)) {
      throw LlvmJitGeneratedRuntimeError(
          frame.last_instruction, *reason);
    }
    throw LlvmJitError(
        "JIT frame contains an invalid generated runtime error reason");
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
  case FSIM_JIT_RESUME_STATUS_WAIT_FOREVER:
    if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::wait_forever;
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
  case FSIM_JIT_RESUME_STATUS_PAUSED:
    if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::paused;
  case FSIM_JIT_RESUME_STATUS_STOPPED:
    if (frame.state != FSIM_JIT_FRAME_STATE_STOPPED) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    return JitResumeStatus::stopped;
  case FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR:
    if (frame.state != FSIM_JIT_FRAME_STATE_RUNTIME_ERROR) {
      throw LlvmJitError("generated process returned an invalid frame state");
    }
    if (const auto reason =
            decode_generated_runtime_error(result.delay)) {
      throw LlvmJitGeneratedRuntimeError(
          result.instruction, *reason);
    }
    throw LlvmJitError(
        "generated process returned an invalid runtime error reason");
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
  std::vector<std::uint8_t> register_initialized(
      found->second.info.frame_layout.register_count);
  std::vector<std::uint64_t> register_logic9_plane2(
      found->second.info.frame_layout.uses_logic9
          ? found->second.info.frame_layout.register_count
          : 0);
  std::vector<std::uint64_t> register_logic9_plane3(
      found->second.info.frame_layout.uses_logic9
          ? found->second.info.frame_layout.register_count
          : 0);
  fsim_jit_frame_v1 frame{};
  initialize_frame(
      process,
      frame,
      register_aval,
      register_bval,
      register_initialized,
      register_logic9_plane2,
      register_logic9_plane3);
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
  case JitResumeStatus::wait_forever:
  case JitResumeStatus::yielded:
  case JitResumeStatus::debug_point:
  case JitResumeStatus::paused:
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
