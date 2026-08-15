// SPDX-License-Identifier: Apache-2.0
#include "application_trace_hierarchy.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace fsim::app::application_detail {
namespace {

    [[nodiscard]] runtime::TraceLanguage trace_language(
        const semantic::Language language)
    {
        switch (language) {
        case semantic::Language::vhdl:
            return runtime::TraceLanguage::Vhdl;
        case semantic::Language::verilog:
            return runtime::TraceLanguage::Verilog;
        case semantic::Language::system_verilog:
            return runtime::TraceLanguage::SystemVerilog;
        case semantic::Language::systemc:
            return runtime::TraceLanguage::SystemC;
        }
        throw std::invalid_argument("FST trace source has an unknown language");
    }

    [[nodiscard]] bool owns_path(
        const std::string_view owner,
        const std::string_view path) noexcept
    {
        return path == owner
            || (path.size() > owner.size() && path.starts_with(owner)
                && path[owner.size()] == '.');
    }

} // namespace

FstTraceObject canonical_fst_trace_object(
    const semantic::design::DesignIr& design,
    const semantic::design::Object& object)
{
    if (!object.specialization.valid()
        || object.specialization.value() >= design.specializations().size()) {
        throw std::invalid_argument(
            "FST trace object lacks a stable specialization");
    }
    const auto& specialization
        = design.specializations()[object.specialization.value()];
    if (!specialization.instance.valid()
        || specialization.instance.value() >= design.instances().size()) {
        throw std::invalid_argument(
            "FST trace specialization lacks an owning instance");
    }
    const auto& instances = design.instances();
    const auto* occurrence
        = &instances[specialization.instance.value()];
    const auto* root_occurrence = occurrence;
    for (std::size_t depth = 0; root_occurrence->parent; ++depth) {
        if (depth >= instances.size()
            || !root_occurrence->parent->valid()
            || root_occurrence->parent->value() >= instances.size()) {
            throw std::invalid_argument(
                "FST trace instance ancestry is invalid");
        }
        root_occurrence
            = &instances[root_occurrence->parent->value()];
    }
    if (std::ranges::find(design.roots(), root_occurrence->path)
        == design.roots().end()) {
        throw std::invalid_argument(
            "FST trace owning instance is outside every design root");
    }
    if (specialization.library.empty() || specialization.name.empty()) {
        throw std::invalid_argument(
            "FST trace specialization lacks library or unit provenance");
    }
    if (occurrence->path.empty() || object.path.empty()) {
        throw std::invalid_argument(
            "FST trace object lacks canonical hierarchy identity");
    }

    FstTraceObject result;
    result.path = owns_path(occurrence->path, object.path)
        ? object.path
        : occurrence->path + "." + object.path;
    result.source.kind = specialization.language == semantic::Language::systemc
        ? runtime::TraceSourceKind::SystemC
        : runtime::TraceSourceKind::Hdl;
    result.source.language = trace_language(specialization.language);
    result.source.root_identity = root_occurrence->path;
    result.source.library = specialization.library;
    result.source.owner_identity = specialization.library + ":"
        + specialization.name + "@" + occurrence->path;
    return result;
}

} // namespace fsim::app::application_detail
