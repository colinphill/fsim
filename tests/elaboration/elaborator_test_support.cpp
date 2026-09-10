// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

bool has_diagnostic(const fsim::elaboration::ElaborationResult& result,
    const std::string_view code)
{
    return std::ranges::any_of(result.diagnostics,
        [code](const auto& diagnostic) { return diagnostic.code == code; });
}

std::vector<fsim::elaboration::SystemCFactoryCandidate>
TestSystemCFactoryProvider::candidates() const
{
    return factory_candidates;
}

std::vector<std::string> TestSystemCFactoryProvider::libraries() const
{
    auto result = available_libraries;
    for (const auto& candidate : factory_candidates) {
        if (std::ranges::find(result, candidate.library) == result.end()) {
            result.push_back(candidate.library);
        }
    }
    return result;
}

std::optional<std::vector<
    fsim::elaboration::SystemCConstructionParameter>>
TestSystemCFactoryProvider::schema(std::string_view, std::string& error)
{
    if (!schema_failure.empty()) {
        error = schema_failure;
        return std::nullopt;
    }
    error.clear();
    return parameters;
}

std::optional<fsim::elaboration::SystemCInstanceDescription>
TestSystemCFactoryProvider::instantiate(const std::string_view path,
    const std::string_view target,
    const std::span<const std::pair<std::string, std::int64_t>> values,
    std::string& error)
{
    if (!construction_failure.empty()) {
        error = construction_failure;
        return std::nullopt;
    }
    error.clear();
    auto result = prototype;
    result.path = path;
    result.target = target;
    result.handle = next_handle++;
    result.construction_values.assign(values.begin(), values.end());
    last_values = result.construction_values;
    const auto width = std::ranges::find(values, "WIDTH",
        &std::pair<std::string, std::int64_t>::first);
    if (width != values.end() && width->second > 0) {
        for (auto& port : result.ports) {
            if (port.name == "value" && width->second > 1) {
                port.type.packed_range = fsim::frontend::PackedRange {
                    width->second - 1, 0, true
                };
            }
        }
    }
    return result;
}

} // namespace fsim::tests::elaboration
