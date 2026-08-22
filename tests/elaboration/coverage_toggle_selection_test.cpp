// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_toggle_selection.hpp"
#include "fsim/frontend/coverage_source_identity.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::string_view kSource = R"(module semantic;
memory_decl
array_decl
process_local
process_memory
function_local
task_local
block_local
end
)";

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

std::filesystem::path checkout_root(const std::string_view leaf)
{
#if defined(_WIN32)
    return std::filesystem::path { "C:/fsim-toggle-selection" } / leaf;
#else
    return std::filesystem::path { "/fsim-toggle-selection" } / leaf;
#endif
}

fsim::elaboration::VerilogCoverageSource make_source(
    const std::filesystem::path& root)
{
    const auto path = root / "rtl/semantic.sv";
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, path, bytes(kSource));
    require(identity.ok(), "toggle selection source identity must be valid");
    return { path.generic_string(), std::move(*identity.identity) };
}

fsim::frontend::SourceSpan span(
    const std::string& source, const std::string_view token)
{
    const auto begin = kSource.find(token);
    require(begin != std::string_view::npos,
        "toggle selection token must exist");
    const auto line = static_cast<std::size_t>(
        1U + std::count(kSource.begin(), kSource.begin() + begin, '\n'));
    return { source, { begin, line, 1U },
        { begin + token.size(), line, token.size() + 1U }, { }, { } };
}

fsim::frontend::Type scalar_type()
{
    fsim::frontend::Type type;
    type.domain = fsim::frontend::ValueDomain::Logic4;
    type.spelling = "logic";
    return type;
}

fsim::frontend::Type container_type(
    const fsim::frontend::SystemVerilogContainerKind kind)
{
    auto type = scalar_type();
    type.systemverilog_container.emplace();
    type.systemverilog_container->kind = kind;
    type.systemverilog_container->element_types.push_back(scalar_type());
    return type;
}

fsim::frontend::VariableDeclaration variable(std::string name,
    fsim::frontend::Type type, const fsim::frontend::SourceSpan& source_span)
{
    fsim::frontend::VariableDeclaration result;
    result.name = std::move(name);
    result.type = std::move(type);
    result.span = source_span;
    return result;
}

fsim::frontend::DesignUnit semantic_unit(
    const fsim::elaboration::VerilogCoverageSource& source)
{
    using Kind = fsim::frontend::SystemVerilogContainerKind;
    fsim::frontend::DesignUnit unit;
    unit.kind = fsim::frontend::UnitKind::VerilogModule;
    unit.language = fsim::frontend::Language::SystemVerilog2017;
    unit.library = "work";
    unit.name = "semantic";
    unit.span = span(source.source_name, "module semantic");
    unit.variables.push_back(variable("memory",
        container_type(Kind::StaticArray),
        span(source.source_name, "memory_decl")));
    unit.variables.push_back(variable("queue",
        container_type(Kind::Queue), span(source.source_name, "array_decl")));
    unit.variables.push_back(variable("ordinary", scalar_type(), unit.span));

    fsim::frontend::Process process;
    process.kind = fsim::frontend::ProcessKind::Initial;
    process.name = "worker";
    process.span = span(source.source_name, "process_local");
    process.variables.push_back(variable("local", scalar_type(), process.span));
    process.variables.push_back(variable("local_memory",
        container_type(Kind::StaticArray),
        span(source.source_name, "process_memory")));
    fsim::frontend::Statement block;
    block.kind = fsim::frontend::StatementKind::Block;
    block.label = "nested";
    block.span = span(source.source_name, "block_local");
    block.declarations.push_back(variable("block_value", scalar_type(),
        block.span));
    process.statements.push_back(std::move(block));
    unit.processes.push_back(std::move(process));

    fsim::frontend::FunctionDeclaration function;
    function.name = "compute";
    function.language = fsim::frontend::Language::SystemVerilog2017;
    function.automatic = true;
    function.span = span(source.source_name, "function_local");
    function.variables.push_back(variable("function_value", scalar_type(),
        function.span));
    unit.functions.push_back(std::move(function));

    fsim::frontend::TaskDeclaration task;
    task.name = "drive";
    task.automatic = true;
    task.span = span(source.source_name, "task_local");
    task.variables.push_back(variable("task_value", scalar_type(), task.span));
    unit.tasks.push_back(std::move(task));
    return unit;
}

fsim::elaboration::CoverageInventoryOwner owner_for(
    const fsim::elaboration::VerilogCoverageSource& source,
    const std::string_view instance = "top.u",
    const std::uint32_t specialization = 5U)
{
    return { specialization, instance,
        fsim::frontend::Language::SystemVerilog2017, source.source_name, { },
        "work", "sv:work.semantic", { } };
}

const fsim::elaboration::CoverageToggleExclusion& find_exclusion(
    const fsim::elaboration::CoverageToggleSelection& selection,
    const std::string_view suffix)
{
    const auto found = std::ranges::find_if(selection.exclusions,
        [&](const auto& exclusion) {
            return exclusion.hierarchy_path.ends_with(suffix);
        });
    require(found != selection.exclusions.end(),
        "expected exclusion path suffix must exist");
    return *found;
}

void test_explicit_reasons_and_identity()
{
    using namespace fsim;
    using Reason = elaboration::CoverageToggleExclusionReason;
    const auto source = make_source(checkout_root("reasons"));
    const auto unit = semantic_unit(source);
    const auto built = elaboration::make_default_coverage_toggle_selection(
        unit, unit.ports, owner_for(source), std::span { &source, 1U });
    require(built.ok() && built.selection->exclusions.size() == 7U,
        "every default-excluded local, memory, and array must remain explicit");
    require(find_exclusion(*built.selection, ".memory").reasons
                == std::vector { Reason::Memory, Reason::Array }
            && find_exclusion(*built.selection, ".memory").shape.kind
                == elaboration::CoverageToggleExcludedContainerKind::StaticArray
            && find_exclusion(*built.selection, ".memory").shape.element_width
                == 1U
            && find_exclusion(*built.selection, ".queue").reasons
                == std::vector { Reason::Array }
            && find_exclusion(*built.selection, ".queue").shape.kind
                == elaboration::CoverageToggleExcludedContainerKind::Queue
            && find_exclusion(*built.selection, ".local").reasons
                == std::vector { Reason::ProceduralLocal }
            && find_exclusion(*built.selection, ".local_memory").reasons
                == std::vector { Reason::ProceduralLocal, Reason::Memory,
                    Reason::Array }
            && find_exclusion(*built.selection, ".function_value").reasons
                == std::vector { Reason::AutomaticLocal }
            && find_exclusion(*built.selection, ".task_value").reasons
                == std::vector { Reason::AutomaticLocal }
            && find_exclusion(*built.selection, ".block_value").reasons
                == std::vector { Reason::ProceduralLocal },
        "default exclusion reasons must be complete, ordered, and non-lossy");
    require(std::ranges::none_of(built.selection->exclusions,
                [](const auto& exclusion) {
                    return exclusion.hierarchy_path.ends_with("ordinary");
                }),
        "an ordinary selected scalar must not acquire a synthetic exclusion");
    for (const auto& exclusion : built.selection->exclusions) {
        require(runtime::is_code_coverage_identity_valid(exclusion.source_point)
                && runtime::is_code_coverage_identity_valid(exclusion.id)
                && exclusion.instance_identity
                    == built.selection->instance_identity
                && exclusion.specialization == 5U,
            "every exclusion must retain authenticated source and instance identity");
        for (const auto reason : exclusion.reasons) {
            require(elaboration::coverage_toggle_exclusion_reason_name(reason)
                    != "unknown",
                "every published exclusion reason must have a stable name");
        }
    }
}

void test_relocation_order_and_hierarchy()
{
    using namespace fsim;
    const auto source_a = make_source(checkout_root("relocated-a"));
    const auto source_b = make_source(checkout_root("relocated-b"));
    auto unit_a = semantic_unit(source_a);
    auto unit_b = semantic_unit(source_b);
    const auto first = elaboration::make_default_coverage_toggle_selection(
        unit_a, unit_a.ports, owner_for(source_a),
        std::span { &source_a, 1U });
    const auto relocated = elaboration::make_default_coverage_toggle_selection(
        unit_b, unit_b.ports, owner_for(source_b),
        std::span { &source_b, 1U });
    require(first.ok() && relocated.ok()
            && first.selection == relocated.selection,
        "default exclusions must be checkout-location independent");
    std::ranges::reverse(unit_a.variables);
    std::ranges::reverse(unit_a.processes);
    const auto reordered = elaboration::make_default_coverage_toggle_selection(
        unit_a, unit_a.ports, owner_for(source_a),
        std::span { &source_a, 1U });
    require(reordered.ok() && reordered.selection == first.selection,
        "exclusion order must not depend on semantic declaration containers");
    const auto sibling = elaboration::make_default_coverage_toggle_selection(
        unit_a, unit_a.ports, owner_for(source_a, "top.v", 6U),
        std::span { &source_a, 1U });
    require(sibling.ok()
            && sibling.selection->exclusions.front().source_point
                == first.selection->exclusions.front().source_point
            && sibling.selection->exclusions.front().id
                != first.selection->exclusions.front().id,
        "source exclusions may be shared while instance exclusion identities remain distinct");
}

void test_vhdl_array_defaults()
{
    using namespace fsim;
    using Reason = elaboration::CoverageToggleExclusionReason;
    const auto source = make_source(checkout_root("vhdl"));
    frontend::DesignUnit unit;
    unit.kind = frontend::UnitKind::VhdlArchitecture;
    unit.language = frontend::Language::Vhdl2008;
    unit.library = "work";
    unit.name = "rtl";
    unit.primary_name = "semantic";
    unit.span = span(source.source_name, "module semantic");
    auto array = scalar_type();
    array.domain = frontend::ValueDomain::Unknown;
    array.systemverilog_container.reset();
    array.vhdl_array.emplace();
    array.vhdl_array->dimensions.resize(2U);
    array.vhdl_array->element_types.push_back(scalar_type());
    frontend::SignalDeclaration signal;
    signal.name = "matrix";
    signal.type = array;
    signal.span = span(source.source_name, "array_decl");
    unit.signals.push_back(signal);
    auto shared = variable("shared_memory", std::move(array),
        span(source.source_name, "memory_decl"));
    shared.vhdl_shared = true;
    unit.variables.push_back(std::move(shared));
    const elaboration::CoverageInventoryOwner owner { 3U, "root",
        frontend::Language::Vhdl2008, source.source_name, { }, "work",
        "vhdl:work.semantic(rtl)", { } };
    const auto built = elaboration::make_default_coverage_toggle_selection(
        unit, { }, owner, std::span { &source, 1U });
    require(built.ok() && built.selection->exclusions.size() == 2U
            && find_exclusion(*built.selection, ".matrix").reasons
                == std::vector { Reason::Array }
            && find_exclusion(*built.selection, ".shared_memory").reasons
                == std::vector { Reason::Memory, Reason::Array },
        "non-vector VHDL arrays and retained array memories must be explicit default exclusions");
}

void test_rejections_and_resource_limits()
{
    using namespace fsim;
    using Error = elaboration::CoverageToggleSelectionError;
    const auto source = make_source(checkout_root("negative"));
    auto unit = semantic_unit(source);
    const auto owner = owner_for(source);
    const auto invoke = [&](const frontend::DesignUnit& input,
                            const elaboration::CoverageInventoryOwner& input_owner,
                            const elaboration::CoverageToggleSelectionLimits limits = { }) {
        return elaboration::make_default_coverage_toggle_selection(input,
            input.ports, input_owner, std::span { &source, 1U }, limits);
    };
    auto wrong = owner;
    wrong.unit = "sv:work.other";
    require(invoke(unit, wrong).error == Error::InstanceOwnerMismatch,
        "mismatched specialization owners must be rejected");
    auto duplicate = unit;
    duplicate.variables.push_back(duplicate.variables.front());
    require(invoke(duplicate, owner).error == Error::DuplicateObjectPath,
        "duplicate excluded paths must fail transactionally");
    auto unknown = unit;
    unknown.variables.front().span.source_name = "unknown.sv";
    unknown.variables.front().span.physical_source_name = "unknown.sv";
    require(invoke(unknown, owner).error == Error::UnknownObjectSource,
        "unmapped excluded declarations must be rejected");
    auto invalid_scope = unit;
    invalid_scope.processes.front().name.push_back('\0');
    require(invoke(invalid_scope, owner).error == Error::InvalidObjectName,
        "invalid callable or procedural scope text must be rejected");

    auto declarations = elaboration::CoverageToggleSelectionLimits { };
    declarations.maximum_declarations = 0U;
    auto exclusions = elaboration::CoverageToggleSelectionLimits { };
    exclusions.maximum_exclusions = 0U;
    auto reasons = elaboration::CoverageToggleSelectionLimits { };
    reasons.maximum_reasons = 0U;
    auto depth = elaboration::CoverageToggleSelectionLimits { };
    depth.maximum_scope_depth = 0U;
    require(invoke(unit, owner, declarations).error == Error::ResourceLimit
            && invoke(unit, owner, exclusions).error == Error::ResourceLimit
            && invoke(unit, owner, reasons).error == Error::ResourceLimit
            && invoke(unit, owner, depth).error == Error::ResourceLimit,
        "declaration, exclusion, reason, and lexical-depth ceilings must be enforced");
}

} // namespace

int main()
{
    test_explicit_reasons_and_identity();
    test_relocation_order_and_hierarchy();
    test_vhdl_array_defaults();
    test_rejections_and_resource_limits();
    std::cout << "Coverage toggle selection tests passed\n";
}
