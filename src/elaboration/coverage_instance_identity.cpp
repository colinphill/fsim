// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_instance_identity.hpp"

#include "fsim/support/identity128.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace fsim::elaboration {
namespace {

    void update_string(support::Sha256& hash, const std::string_view value) noexcept
    {
        support::sha256_update_u64_be(hash, value.size());
        hash.update(value);
    }

    std::string_view language_identity(
        const frontend::Language language) noexcept
    {
        switch (language) {
        case frontend::Language::Vhdl2008:
            return "vhdl-2008";
        case frontend::Language::Verilog2005:
            return "verilog-2005";
        case frontend::Language::SystemVerilog2017:
            return "systemverilog-2017";
        }
        return { };
    }

    bool invalid_text(const std::string_view text) noexcept
    {
        return text.find('\0') != std::string_view::npos;
    }

    bool add_size(
        std::size_t& total, const std::size_t amount,
        const std::size_t limit) noexcept
    {
        if (amount > limit - total) {
            return false;
        }
        total += amount;
        return true;
    }

} // namespace

CoverageInstanceIdentityResult make_coverage_instance_identity(
    const CoverageInstanceIdentityInput& input,
    const CoverageInstanceIdentityLimits limits) noexcept
{
    using Error = CoverageInstanceIdentityError;
    if (input.hierarchy_path.empty()) {
        return { { }, Error::EmptyHierarchyPath };
    }
    if (input.hierarchy_path.size() > limits.maximum_hierarchy_bytes) {
        return { { }, Error::ResourceLimit };
    }
    if (invalid_text(input.hierarchy_path)) {
        return { { }, Error::InvalidHierarchyPath };
    }
    const auto language = language_identity(input.language);
    if (language.empty()) {
        return { { }, Error::InvalidLanguage };
    }
    if (input.library.empty()) {
        return { { }, Error::EmptyLibrary };
    }
    if (input.library.size() > limits.maximum_library_bytes) {
        return { { }, Error::ResourceLimit };
    }
    if (input.unit.empty()) {
        return { { }, Error::EmptyUnit };
    }
    if (input.unit.size() > limits.maximum_unit_bytes
        || input.parameter_identities.size()
            > limits.maximum_parameter_count) {
        return { { }, Error::ResourceLimit };
    }
    if (invalid_text(input.library) || invalid_text(input.unit)) {
        return { { }, Error::InvalidParameterIdentity };
    }

    try {
        std::vector<const std::pair<std::string, std::string>*> parameters;
        parameters.reserve(input.parameter_identities.size());
        std::size_t parameter_bytes = 0U;
        for (const auto& parameter : input.parameter_identities) {
            if (parameter.first.empty() || parameter.second.empty()
                || invalid_text(parameter.first)
                || invalid_text(parameter.second)) {
                return { { }, Error::InvalidParameterIdentity };
            }
            if (!add_size(parameter_bytes, parameter.first.size(),
                    limits.maximum_parameter_bytes)
                || !add_size(parameter_bytes, parameter.second.size(),
                    limits.maximum_parameter_bytes)) {
                return { { }, Error::ResourceLimit };
            }
            parameters.push_back(&parameter);
        }
        std::ranges::sort(parameters, [](const auto* left, const auto* right) {
            return std::tie(left->first, left->second)
                < std::tie(right->first, right->second);
        });
        for (std::size_t index = 1U; index < parameters.size(); ++index) {
            if (parameters[index - 1U]->first == parameters[index]->first) {
                return { { }, Error::DuplicateParameterName };
            }
        }

        support::Sha256 hash;
        update_string(hash, kCoverageInstanceIdentitySchema);
        update_string(hash, input.hierarchy_path);
        update_string(hash, language);
        update_string(hash, input.library);
        update_string(hash, input.unit);
        support::sha256_update_u64_be(hash, parameters.size());
        for (const auto* parameter : parameters) {
            update_string(hash, parameter->first);
            update_string(hash, parameter->second);
        }
        const auto digest = hash.finish();
        const CoverageInstanceIdentity identity {
            support::sha256_digest_word_be(digest, 0U),
            support::sha256_digest_word_be(digest, 8U)
        };
        if (!support::identity128_nonzero(identity)) {
            return { { }, Error::ZeroIdentity };
        }
        return { identity, Error::None };
    } catch (const std::bad_alloc&) {
        return { { }, Error::ResourceLimit };
    } catch (const std::length_error&) {
        return { { }, Error::ResourceLimit };
    }
}

std::string coverage_instance_identity_hex(
    const CoverageInstanceIdentity identity)
{
    return support::identity128_hex(identity);
}

} // namespace fsim::elaboration
