// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_phase.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fsim::runtime {

struct SystemVerilogUvmPhaseService::Domain {
    SystemVerilogUvmDomainHandle handle;
    std::string identity;
    SystemVerilogUvmDomainKind kind { SystemVerilogUvmDomainKind::Custom };
    std::uint64_t registration_order { };
    std::vector<std::uint64_t> phases;
    std::map<std::string, std::uint64_t, std::less<>> phases_by_identity;
    std::vector<SystemVerilogUvmRootHandle> roots;
    std::optional<std::uint64_t> with_phase;
};

struct SystemVerilogUvmPhaseService::Phase {
    SystemVerilogUvmPhaseHandle handle;
    SystemVerilogUvmDomainHandle domain;
    SystemVerilogUvmPhaseKind kind { SystemVerilogUvmPhaseKind::Custom };
    SystemVerilogUvmPhaseExecutionKind execution {
        SystemVerilogUvmPhaseExecutionKind::Function
    };
    SystemVerilogUvmPhaseTraversal traversal {
        SystemVerilogUvmPhaseTraversal::BottomUp
    };
    SystemVerilogUvmPhaseState state { SystemVerilogUvmPhaseState::Dormant };
    std::string identity;
    std::uint64_t registration_order { };
    std::optional<std::uint64_t> parent;
    std::vector<std::uint64_t> predecessors;
    std::vector<std::uint64_t> successors;
    std::vector<std::uint64_t> synchronized;
    std::vector<std::uint64_t> processes;
    std::optional<SystemVerilogUvmPhaseExecutionResult> task_result;
    std::size_t ready_to_end_attempts { };
};

struct SystemVerilogUvmPhaseService::PhaseProcess {
    SystemVerilogUvmPhaseProcessHandle handle;
    SystemVerilogUvmPhaseHandle phase;
    SystemVerilogUvmRootHandle root { };
    SystemVerilogClassHandle component { };
    SystemVerilogUvmPhaseProcessState state {
        SystemVerilogUvmPhaseProcessState::Running
    };
    std::uint64_t registration_order { };
    std::optional<std::uint64_t> parent;
    std::vector<std::uint64_t> children;
    std::size_t depth { };
    ScheduledTaskHandle task;
};

} // namespace fsim::runtime
