// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include "fsim/semantic/systemverilog_type_descriptor.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fsim::elaboration {

std::optional<semantic::TypeId> HierarchyBuilder::systemverilog_user_nettype(
    const semantic::sv::TypeReference& type,
    const semantic::CompiledDesignResolver& resolver,
    const semantic::ScopeId scope) const
{
    if (compiled_ == nullptr) {
        return std::nullopt;
    }
    auto target = type.target.target;
    if (!target.valid() && type.named) {
        // A nettype of a package or of the compilation unit is named here
        // and found in the scope of use (IEEE 1800-2023 26.3).
        if (const auto effective
            = resolver.effective_systemverilog_type(type, scope)) {
            target = effective->target.target;
        }
    }
    if (!target.valid()) {
        return std::nullopt;
    }
    const auto definition = compiled_->find_type(target);
    if (!definition || definition->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto declaration = compiled_->find_declaration(
        definition->systemverilog->declaration);
    return declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->form
                == semantic::sv::DeclarationForm::nettype_declaration
        ? std::optional { target }
        : std::nullopt;
}

void HierarchyBuilder::register_systemverilog_nettype_net(
    const SignalId signal, const semantic::sv::Declaration& declaration,
    const SystemVerilogHirMaterialization& materialization)
{
    if (!declaration.type || materialization.specialization == nullptr) {
        return;
    }
    const auto& specialization = *materialization.specialization;
    const semantic::CompiledDesignResolver resolver { specialization };
    const auto nettype = systemverilog_user_nettype(
        *declaration.type, resolver, declaration.scope);
    if (!nettype) {
        return;
    }
    const auto binding = systemverilog_nettype_function(*nettype, resolver);
    const auto source = compiled_source_span(*compiled_, declaration.source);
    if (binding.error || binding.nettype == nullptr) {
        return;
    }
    if (!binding.function) {
        // A nettype without a resolution function: the net may have at
        // most one driver (IEEE 1800-2023 6.6.7). The elements of an array
        // of them are checked as the drivers of any unresolved array are
        // (FSIM-ELAB-DRV-001).
        if (declaration.type->unpacked_dimensions.empty()
            && !declaration.type->container_form) {
            systemverilog_unresolved_nettype_nets_.insert_or_assign(
                signal, declaration.source);
        }
        return;
    }
    // The function takes one dynamic array of the nettype's data type T and
    // returns T (6.6.7); the types match as 6.22.1 defines.
    const auto function_record
        = specialization.find_declaration(*binding.function);
    const auto* function = function_record
        ? function_record->systemverilog
        : nullptr;
    const auto nettype_declaration = specialization.find_declaration(
        binding.nettype->declaration);
    const auto data_type = nettype_declaration
            && nettype_declaration->systemverilog != nullptr
        ? semantic::describe_systemverilog_type(specialization,
              binding.nettype->base, nettype_declaration->systemverilog->scope)
        : std::nullopt;
    const auto formal_record = function != nullptr && function->callable
            && function->callable->formals.size() == 1U
        ? specialization.find_declaration(function->callable->formals.front())
        : std::nullopt;
    const auto* formal = formal_record ? formal_record->systemverilog : nullptr;
    auto formal_type = formal != nullptr && formal->type
        ? semantic::describe_systemverilog_type(
              specialization, *formal->type, formal->scope)
        : std::nullopt;
    const auto return_type = function != nullptr && function->callable
        ? semantic::describe_systemverilog_type(specialization,
              function->callable->return_type, function->scope)
        : std::nullopt;
    const bool dynamic_formal = formal_type && !formal_type->unpacked.empty()
        && formal_type->unpacked.front().container == "dynamic";
    if (dynamic_formal) {
        formal_type->unpacked.erase(formal_type->unpacked.begin());
    }
    if (function == nullptr || !function->callable
        || !function->callable->function || !data_type || !return_type
        || !dynamic_formal || formal->direction
            == semantic::sv::Direction::output
        || formal->direction == semantic::sv::Direction::inout
        || formal->direction == semantic::sv::Direction::ref
        || !semantic::systemverilog_types_match(*formal_type, *data_type)
        || !semantic::systemverilog_types_match(*return_type, *data_type)) {
        report("FSIM-ELAB-SVNETTYPE-003",
            "SystemVerilog nettype resolution function '"
                + binding.nettype->resolution_function
                + "' must take one dynamic array of the nettype's data type "
                  "and return that type",
            function != nullptr
                ? compiled_source_span(*compiled_, function->source)
                : source);
        return;
    }
    // The nets of one instance share one copy of its specialization, which
    // the resolver programs are lowered in after the hierarchy is built.
    if (systemverilog_resolver_specialization_key_ != &specialization
        || systemverilog_resolver_specialization_path_
            != materialization.path
        || !systemverilog_resolver_specialization_) {
        systemverilog_resolver_specialization_key_ = &specialization;
        systemverilog_resolver_specialization_path_ = materialization.path;
        systemverilog_resolver_specialization_
            = std::make_shared<const semantic::SpecializedHirUnit>(
                specialization);
    }
    // An array of nets of the nettype: its own unpacked dimensions, the
    // outermost of the declared type's, give the count of its nets.
    const bool array = !declaration.type->unpacked_dimensions.empty()
        || declaration.type->container_form.has_value();
    std::size_t elements { };
    if (array
        && (!declaration.type->container_form
            || *declaration.type->container_form
                == semantic::sv::TypeForm::static_array)) {
        const auto declared = semantic::describe_systemverilog_type(
            specialization, *declaration.type, declaration.scope);
        const auto own = declaration.type->unpacked_dimensions.size();
        if (declared && declared->unpacked.size() >= own) {
            elements = 1U;
            for (std::size_t index = 0U; index < own; ++index) {
                const auto& dimension = declared->unpacked[index];
                if (!dimension.left || !dimension.right
                    || !dimension.container.empty()) {
                    elements = 0U;
                    break;
                }
                elements *= static_cast<std::size_t>(
                    *dimension.left <= *dimension.right
                        ? *dimension.right - *dimension.left
                        : *dimension.left - *dimension.right) + 1U;
            }
        }
    }
    systemverilog_function_resolvers_.insert_or_assign(signal,
        PendingSystemVerilogResolver { *binding.function,
            binding.nettype->source, systemverilog_resolver_specialization_,
            array, elements });
}

void HierarchyBuilder::lower_compiled_systemverilog_resolvers(
    const std::unordered_map<SignalId, std::vector<ProcessId>>& drivers,
    const std::unordered_map<SignalId,
        std::vector<std::vector<Process::DriverRegion>>>& array_regions)
{
    for (const auto& [signal, source] :
        systemverilog_unresolved_nettype_nets_) {
        const auto found = drivers.find(signal);
        if (found != drivers.end() && found->second.size() > 1U) {
            report("FSIM-ELAB-SVNETTYPE-006",
                "net '" + design_.signal_info_.at(signal).name
                    + "' of a nettype without a resolution function has "
                    + std::to_string(found->second.size()) + " drivers",
                compiled_source_span(*compiled_, source));
        }
    }
    for (const auto& [signal, pending] : systemverilog_function_resolvers_) {
        if (signal >= design_.signals_.size()
            || design_.signals_[signal].resolution
                != ResolutionKind::user_function) {
            continue;
        }
        // The function runs even for a net without drivers, on an empty
        // array (IEEE 1800-2023 6.7.3).
        const auto found = drivers.find(signal);
        const auto net_drivers = found != drivers.end()
            ? found->second
            : std::vector<ProcessId> { };
        const SignalBindings signals;
        const ReadOnlySignalBindings read_only_signals;
        const StringObjectBindings string_objects;
        const ReadOnlyStringBindings read_only_strings;
        const ContainerObjectBindings container_objects;
        const ReadOnlyContainerBindings read_only_containers;
        Lowerer lowerer {
            design_,
            signals,
            read_only_signals,
            string_objects,
            read_only_strings,
            container_objects,
            read_only_containers,
            diagnostics_,
        };
        lowerer.set_specialized_hir_unit(pending.specialization.get());
        const auto& name = design_.signal_info_.at(signal).name;
        // An array of nets the element rewrite left whole (its elements
        // real or unpacked structures): each element resolves from the
        // drivers whose regions cover its bits (IEEE 1800-2023 6.6.7).
        std::vector<std::vector<std::size_t>> elements;
        if (pending.array) {
            const auto width = static_cast<std::uint64_t>(
                design_.signals_[signal].initial_value.width());
            if (pending.elements == 0U || width % pending.elements != 0U) {
                report("FSIM-ELAB-SVNETTYPE-004",
                    "the elements of net array '" + name
                        + "' could not be resolved as separate nets",
                    compiled_source_span(*compiled_, pending.source));
                continue;
            }
            const auto element_width = width / pending.elements;
            const auto regions = array_regions.find(signal);
            elements.resize(pending.elements);
            for (std::size_t driver = 0U; driver < net_drivers.size();
                 ++driver) {
                const auto* driven = regions != array_regions.end()
                        && driver < regions->second.size()
                    ? &regions->second[driver]
                    : nullptr;
                // A writer of elements claims the whole bridge too; its
                // element regions say which nets it drives.
                if (driven != nullptr
                    && std::ranges::none_of(*driven,
                        [](const Process::DriverRegion& region) {
                            return !region.whole;
                        })) {
                    driven = nullptr;
                }
                for (std::size_t element = 0U; element < pending.elements;
                     ++element) {
                    const auto low
                        = (pending.elements - 1U - element) * element_width;
                    const auto high = low + element_width;
                    const bool covers = driven == nullptr
                        || std::ranges::any_of(*driven,
                            [&](const Process::DriverRegion& region) {
                                return !region.whole
                                    && (region.offset < high
                                        && low < std::uint64_t { region.offset }
                                                + region.width);
                            });
                    if (covers) {
                        elements[element].push_back(driver);
                    }
                }
            }
        }
        const auto separator = name.find_last_of('.');
        const auto hierarchy = separator == std::string::npos
            ? std::string { }
            : name.substr(0U, separator);
        auto program = lowerer.lower_hir_systemverilog_resolver_process(
            signal, pending.function, net_drivers.size(), hierarchy,
            std::move(elements));
        if (!program) {
            report("FSIM-ELAB-SVNETTYPE-004",
                "the resolution function of net '" + name
                    + "' could not be lowered as a resolver program",
                compiled_source_span(*compiled_, pending.source));
            continue;
        }
        const auto process = program->process.id;
        design_.append_process_record(std::move(program->process));
        VhdlSignalResolver resolver {
            process, program->input, program->output, net_drivers };
        resolver.systemverilog_nettype = true;
        design_.signals_[signal].vhdl_resolver = std::move(resolver);
    }
}

} // namespace fsim::elaboration
