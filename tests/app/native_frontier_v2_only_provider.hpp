// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "fsim/runtime/simir_region_kernel_backend.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fsim::runtime::simir::test {

/// Test adapter that preserves the production LLVM activation and V2
/// frontier backends while deliberately withholding its optional flattened
/// cone-forwarding capability. V2 pending-frame lifecycle tests use this to
/// remain focused on typed descriptors; default-route forwarding is covered
/// by the separate A2 app witness.
class V2FrontierOnlyRegionKernelBackendProvider final
    : public RegionKernelBackendProvider
    , public RegionFrontierBackendProvider {
public:
    explicit V2FrontierOnlyRegionKernelBackendProvider(
        std::shared_ptr<RegionKernelBackendProvider> delegate)
        : delegate_(std::move(delegate))
    {
        if (!delegate_) {
            throw std::invalid_argument {
                "V2-only test provider requires an installed LLVM provider"
            };
        }
        frontier_provider_
            = dynamic_cast<RegionFrontierBackendProvider*>(delegate_.get());
        if (frontier_provider_ == nullptr
            || dynamic_cast<RegionConeForwardingBackendProvider*>(
                   delegate_.get()) == nullptr) {
            throw std::invalid_argument {
                "V2-only test provider requires the production LLVM V2 and "
                "flattened forwarding capabilities"
            };
        }
        identity_.assign(delegate_->identity());
        identity_.append("|test:v2-frontier-only");
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return identity_;
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        return delegate_->create(kernel);
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierBackend> create_frontier(
        const RegionConeActivationKernel& kernel) override
    {
        return frontier_provider_->create_frontier(kernel);
    }

private:
    std::shared_ptr<RegionKernelBackendProvider> delegate_;
    RegionFrontierBackendProvider* frontier_provider_ { };
    std::string identity_;
};

static_assert(!std::is_base_of_v<RegionConeForwardingBackendProvider,
    V2FrontierOnlyRegionKernelBackendProvider>);

} // namespace fsim::runtime::simir::test
