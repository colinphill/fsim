// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <stdexcept>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] std::uint64_t debug_value(
    const Interpreter& interpreter, const RegisterId id) {
  try {
    return interpreter.read_debug_local(0, id).low_word().aval;
  } catch (const std::exception& error) {
    throw std::runtime_error{
        "debug register " + std::to_string(id) + ": " + error.what()};
  }
}

DebugLocal debug_local(
    const std::string& name, const RegisterId id, const std::uint32_t width) {
  return DebugLocal{
      name, "logic", id, width, {}, {}, {}, ValueKind::logic4, {}};
}

void test_mailbox_nonblocking_operations() {
  Interpreter interpreter;
  Process process;
  process.id = 0;
  process.name = "mailbox_nonblocking";
  process.register_count = 12;
  for (RegisterId id = 0; id < process.register_count; ++id) {
    const auto width = id == 1 ? 64U
        : (id == 2 || id == 3 || id == 8 || id == 10) ? 8U : 32U;
    process.debug_locals.push_back(
        debug_local("register_" + std::to_string(id), id, width));
  }
  process.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(32, 1, 0)},
      MailboxCreate{1, 0, 8},
      LoadConstant{2, PackedLogic4::from_aval_bval(8, 0x2a, 0)},
      MailboxPut{1, 2, 8, 4},
      MailboxNum{5, 1},
      MailboxPut{1, 2, 8, 6},
      MailboxGet{1, 3, 8, 7, true},
      MailboxGet{1, 8, 8, 9, false},
      MailboxGet{1, 10, 8, 11, false},
      MailboxNum{5, 1},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed
          && debug_value(interpreter, 4) == 1
          && debug_value(interpreter, 5) == 0
          && debug_value(interpreter, 6) == 0
          && debug_value(interpreter, 7) == 1
          && debug_value(interpreter, 3) == 0x2a
          && debug_value(interpreter, 9) == 1
          && debug_value(interpreter, 8) == 0x2a
          && debug_value(interpreter, 11) == 0,
      "typed bounded mailbox try, peek, get, and num operations");
}

void test_mailbox_waiter_fairness() {
  {
    Interpreter interpreter;
    Process process;
    process.id = 0;
    process.name = "mailbox_writer_fairness";
    process.register_count = 6;
    for (RegisterId id = 0; id < process.register_count; ++id) {
      process.debug_locals.push_back(
          debug_local("register_" + std::to_string(id), id, id == 1 ? 64U : 8U));
    }
    process.operations = {
        LoadConstant{0, PackedLogic4::from_aval_bval(32, 1, 0)},
        MailboxCreate{1, 0, 8},
        LoadConstant{2, PackedLogic4::from_aval_bval(8, 0x10, 0)},
        MailboxPut{1, 2, 8, std::nullopt},
        Fork{{11, 14}, ForkJoinKind::none},
        Yield{},
        MailboxGet{1, 3, 8, std::nullopt, false},
        MailboxGet{1, 4, 8, std::nullopt, false},
        MailboxGet{1, 5, 8, std::nullopt, false},
        WaitFork{},
        Halt{},
        LoadConstant{2, PackedLogic4::from_aval_bval(8, 0x20, 0)},
        MailboxPut{1, 2, 8, std::nullopt},
        ForkEnd{},
        LoadConstant{2, PackedLogic4::from_aval_bval(8, 0x30, 0)},
        MailboxPut{1, 2, 8, std::nullopt},
        ForkEnd{},
    };
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && debug_value(interpreter, 3) == 0x10
            && debug_value(interpreter, 4) == 0x20
            && debug_value(interpreter, 5) == 0x30,
        "bounded mailbox releases blocked writers in FIFO order");
  }

  {
    Interpreter interpreter;
    const auto first = interpreter.add_signal(
        Signal{"mailbox.first", PackedLogic4::from_msb_string("00000000")});
    const auto second = interpreter.add_signal(
        Signal{"mailbox.second", PackedLogic4::from_msb_string("00000000")});
    Process process;
    process.id = 0;
    process.name = "mailbox_reader_fairness";
    process.register_count = 5;
    process.operations = {
        LoadConstant{0, PackedLogic4::from_aval_bval(32, 1, 0)},
        MailboxCreate{1, 0, 8},
        Fork{{10, 13}, ForkJoinKind::none},
        Yield{},
        LoadConstant{2, PackedLogic4::from_aval_bval(8, 0x11, 0)},
        MailboxPut{1, 2, 8, std::nullopt},
        LoadConstant{2, PackedLogic4::from_aval_bval(8, 0x22, 0)},
        MailboxPut{1, 2, 8, std::nullopt},
        WaitFork{},
        Halt{},
        MailboxGet{1, 3, 8, std::nullopt, false},
        WriteBlocking{first, 3},
        ForkEnd{},
        MailboxGet{1, 4, 8, std::nullopt, false},
        WriteBlocking{second, 4},
        ForkEnd{},
    };
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && interpreter.signal_value(first).low_word().aval == 0x11
            && interpreter.signal_value(second).low_word().aval == 0x22,
        "mailbox wakes blocked readers in FIFO order");
  }
}

void test_semaphore_operations_and_fairness() {
  Interpreter interpreter;
  const auto first = interpreter.add_signal(
      Signal{"semaphore.first", PackedLogic4::from_msb_string("0")});
  const auto second = interpreter.add_signal(
      Signal{"semaphore.second", PackedLogic4::from_msb_string("0")});
  Process process;
  process.id = 0;
  process.name = "semaphore_fairness";
  process.register_count = 7;
  for (RegisterId id = 0; id < process.register_count; ++id) {
    process.debug_locals.push_back(debug_local(
        id == 5 ? "younger_before_release"
                : "register_" + std::to_string(id),
        id, id == 1 ? 64U : id == 5 ? 1U : 32U));
  }
  process.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(32, 0, 0)},
      SemaphoreCreate{1, 0},
      Fork{{12, 17}, ForkJoinKind::none},
      Yield{},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 1, 0)},
      SemaphorePut{1, 2},
      WaitFor{1},
      ReadSignal{5, second},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 2, 0)},
      SemaphorePut{1, 2},
      WaitFork{},
      Halt{},
      LoadConstant{3, PackedLogic4::from_aval_bval(32, 2, 0)},
      SemaphoreGet{1, 3, std::nullopt},
      LoadConstant{4, PackedLogic4::from_aval_bval(1, 1, 0)},
      WriteBlocking{first, 4},
      ForkEnd{},
      LoadConstant{3, PackedLogic4::from_aval_bval(32, 1, 0)},
      SemaphoreGet{1, 3, std::nullopt},
      LoadConstant{4, PackedLogic4::from_aval_bval(1, 1, 0)},
      WriteBlocking{second, 4},
      ForkEnd{},
  };
  (void)interpreter.add_process(std::move(process));
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 1
          && debug_value(interpreter, 5) == 0
          && interpreter.signal_value(first).low_word().aval == 1
          && interpreter.signal_value(second).low_word().aval == 1,
      "counting semaphore preserves FIFO waiters without key bypass");

  Interpreter try_interpreter;
  Process try_process;
  try_process.id = 0;
  try_process.name = "semaphore_try_get";
  try_process.register_count = 7;
  for (RegisterId id = 0; id < try_process.register_count; ++id) {
    try_process.debug_locals.push_back(
        debug_local("register_" + std::to_string(id), id, id == 1 ? 64U : 32U));
  }
  try_process.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(32, 2, 0)},
      SemaphoreCreate{1, 0},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 2, 0)},
      SemaphoreGet{1, 2, 3},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 1, 0)},
      SemaphoreGet{1, 2, 4},
      SemaphorePut{1, 2},
      SemaphoreGet{1, 2, 5},
      Halt{},
  };
  (void)try_interpreter.add_process(std::move(try_process));
  const auto try_result = try_interpreter.run();
  require(
      try_result.status == RunStatus::completed
          && debug_value(try_interpreter, 3) == 1
          && debug_value(try_interpreter, 4) == 0
          && debug_value(try_interpreter, 5) == 1,
      "counting semaphore try_get reports success without blocking");

  Interpreter signed_interpreter;
  Process signed_process;
  signed_process.id = 0;
  signed_process.name = "semaphore_signed_counts";
  signed_process.register_count = 5;
  for (RegisterId id = 0; id < signed_process.register_count; ++id) {
    signed_process.debug_locals.push_back(
        debug_local("register_" + std::to_string(id), id, id == 1 ? 64U : 32U));
  }
  signed_process.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(32, UINT32_MAX, 0)},
      SemaphoreCreate{1, 0},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 0, 0)},
      SemaphorePut{1, 2},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 2, 0)},
      SemaphorePut{1, 2},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 1, 0)},
      SemaphoreGet{1, 2, 3},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 0, 0)},
      SemaphoreGet{1, 2, 4},
      Halt{},
  };
  (void)signed_interpreter.add_process(std::move(signed_process));
  const auto signed_result = signed_interpreter.run();
  require(
      signed_result.status == RunStatus::completed
          && debug_value(signed_interpreter, 3) == 1
          && debug_value(signed_interpreter, 4) == 1,
      "counting semaphore accepts negative initial and zero operation counts");
}

}  // namespace

void test_simir_synchronization_objects() {
  const auto run = [](const char* name, const auto& test) {
    try {
      test();
    } catch (const std::exception& error) {
      throw std::runtime_error{std::string{name} + ": " + error.what()};
    }
  };
  run("mailbox nonblocking", test_mailbox_nonblocking_operations);
  run("mailbox fairness", test_mailbox_waiter_fairness);
  run("semaphore", test_semaphore_operations_and_fairness);
}

}  // namespace fsim::tests::runtime
