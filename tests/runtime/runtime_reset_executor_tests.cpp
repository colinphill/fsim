// SPDX-License-Identifier: Apache-2.0
//
// Reset and executors that keep their own state (#128; IEEE 1800-2023 D.8,
// 38.4 vpiReset, IEEE 1076-2019 20.9). A reset takes the time-zero state from
// a twin interpreter and restores it through the restart path, which can only
// describe processes whose state lives in the process record. A process that
// runs under an alternate executor (a SystemC process or another foreign
// model) keeps registers and a program counter the image does not hold, so
// the engine refuses to reset instead of resetting part of the design.
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

class OwnStateExecutor final : public ProcessExecutor {
public:
    explicit OwnStateExecutor(const bool state_free)
        : state_free_ { state_free }
    {
    }
    [[nodiscard]] bool run_state_free() const noexcept override
    {
        return state_free_;
    }
    ProcessResumeResult resume(ProcessExecutionContext&, InstructionIndex) override
    {
        return ProcessResumeResult { 0U, 1U };
    }

private:
    bool state_free_ { };
};

// One process that waits 5 ticks forever; the reset source builds the same.
void populate(Interpreter& interpreter)
{
    Process waiter;
    waiter.id = 0;
    waiter.name = "waiter";
    waiter.register_count = 1;
    waiter.operations = { WaitFor { 5 }, Jump { 0 } };
    (void)interpreter.add_process(std::move(waiter));
}

[[nodiscard]] std::unique_ptr<Interpreter> make_twin()
{
    auto interpreter = std::make_unique<Interpreter>();
    populate(*interpreter);
    return interpreter;
}

}  // namespace

// Expected (derived from Interpreter::can_reset_run_state, which this test
// reads): with a reset source, a live process whose executor is not
// run_state_free() makes the run unresettable (IEEE 1800-2023 D.8: the state
// of the executor cannot be put back from outside), and a state-free executor
// does not. Without a reset source nothing resets. reset_run_state() returns
// false without touching the run when it cannot reset. The start of a run
// that resumes OwnStateExecutor is unverified: it relies on start() being
// able to resume an executor that finishes at once, as the save-refusal test
// of runtime_run_state_tests.cpp does.
void test_reset_executors()
{
    Interpreter own_state;
    populate(own_state);
    own_state.set_process_executor(0U, std::make_unique<OwnStateExecutor>(false));
    own_state.set_reset_source(make_twin);
    own_state.start();
    require(!own_state.can_reset_run_state(),
        "a process with its own executor state cannot reset");
    require(!own_state.reset_run_state(),
        "reset_run_state refuses without touching the run");

    Interpreter state_free;
    populate(state_free);
    state_free.set_process_executor(0U, std::make_unique<OwnStateExecutor>(true));
    state_free.set_reset_source(make_twin);
    state_free.start();
    require(state_free.can_reset_run_state(),
        "a state-free executor does not stop the reset");

    Interpreter no_source;
    populate(no_source);
    no_source.start();
    require(!no_source.can_reset_run_state(), "no reset source: no reset");
}

}  // namespace fsim::tests::runtime
