// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_binding_identity()
{
    Process registered;
    registered.id = 7U;
    registered.name = "registered_executor_body";
    registered.operations = { ReadSignal { 0U, 1U }, Halt { } };
    const ProcessExecutorProgramBinding binding {
        registered, registered, registered.id
    };
    require(binding.matches_registered_program(registered.id, registered),
        "the exact body and revision match their binding");

    auto wrong_registered = registered;
    wrong_registered.operations.replace(0U, ReadSignal { 0U, 2U });
    require(!binding.matches_registered_program(
                wrong_registered.id, wrong_registered),
        "a changed registered operation body and revision are rejected");
    require(!binding.matches_registered_program(
                registered.id + 1U, registered),
        "a binding is not accepted for another process identity");

    auto wrong_generated = registered;
    wrong_generated.operations.replace(0U, ReadSignal { 0U, 2U });
    const ProcessExecutorProgramBinding wrong_generated_binding {
        registered, wrong_generated, registered.id
    };
    require(!binding.same_execution_binding(wrong_generated_binding),
        "deferred comparison distinguishes generated body revisions");

    const auto first_remap = std::make_shared<const ProcessSignalRemap>(
        ProcessSignalRemap { { 1U, 3U } });
    const auto second_remap = std::make_shared<const ProcessSignalRemap>(
        ProcessSignalRemap { { 1U, 4U } });
    const ProcessExecutorProgramBinding first_remapped {
        registered, registered, registered.id, first_remap
    };
    const ProcessExecutorProgramBinding second_remapped {
        registered, registered, registered.id, second_remap
    };
    require(!first_remapped.same_execution_binding(second_remapped),
        "deferred comparison includes the exact signal remap");
}


} // namespace

void test_executor_provenance_contract()
{
    check_binding_identity();
}

} // namespace fsim::tests::runtime
