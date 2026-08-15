// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/accellera.hpp"
#include "fsim/systemc/kernel_backend_value_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <type_traits>
#include <vector>

namespace fsim::systemc {

class backend_value_endpoint {
public:
    virtual ~backend_value_endpoint() = default;

    [[nodiscard]] virtual backend_endpoint_direction backend_direction()
        const noexcept = 0;
    [[nodiscard]] virtual bool apply_backend_value(
        const SystemCKernelValue& value) = 0;
    [[nodiscard]] virtual std::optional<SystemCKernelValue>
    sample_backend_value() const = 0;
};

namespace detail {

    template <typename T>
    struct backend_value_traits;

    inline bool canonical_vector_metadata(const SystemCKernelValue& value,
        const SystemCKernelValueKind kind, const std::uint32_t width,
        const std::size_t planes) noexcept
    {
        return value.kind == kind && value.width == width && !value.is_signed
            && value.range.left == static_cast<std::int64_t>(width - 1U)
            && value.range.right == 0
            && value.range.direction
            == SystemCKernelRangeDirection::descending
            && value.enum_literals.empty() && value.time_unit_fs == 0U
            && value.planes.size() == planes
            && std::ranges::all_of(value.planes, [&](const auto& plane) {
                   const auto words
                       = (static_cast<std::size_t>(width) + 63U) / 64U;
                   if (plane.size() != words) {
                       return false;
                   }
                   const auto remainder = width % 64U;
                   return remainder == 0U
                       || (plane.back() >> remainder) == 0U;
               });
    }

    template <int Width>
    struct backend_value_traits<sc_dt::sc_bv<Width>> {
        static_assert(Width > 0);

        static bool decode(const SystemCKernelValue& value,
            sc_dt::sc_bv<Width>& result)
        {
            if (!canonical_vector_metadata(value,
                    SystemCKernelValueKind::bit2,
                    static_cast<std::uint32_t>(Width), 1U)) {
                return false;
            }
            for (int bit = 0; bit < Width; ++bit) {
                const auto word = static_cast<std::size_t>(bit) / 64U;
                const auto shift = static_cast<unsigned>(bit) % 64U;
                result.set_bit(bit,
                    static_cast<typename sc_dt::sc_bv<Width>::value_type>(
                        (value.planes[0][word] >> shift) & 1U));
            }
            return true;
        }

        static SystemCKernelValue encode(const sc_dt::sc_bv<Width>& value)
        {
            SystemCKernelValue result;
            result.kind = SystemCKernelValueKind::bit2;
            result.width = static_cast<std::uint32_t>(Width);
            result.range = { Width - 1, 0,
                SystemCKernelRangeDirection::descending };
            result.planes.assign(1U,
                std::vector<std::uint64_t>(
                    (static_cast<std::size_t>(Width) + 63U) / 64U));
            for (int bit = 0; bit < Width; ++bit) {
                if (value.get_bit(bit) != 0) {
                    result.planes[0][static_cast<std::size_t>(bit) / 64U]
                        |= std::uint64_t { 1U }
                        << (static_cast<unsigned>(bit) % 64U);
                }
            }
            return result;
        }
    };

    template <int Width>
    struct backend_value_traits<sc_dt::sc_lv<Width>> {
        static_assert(Width > 0);

        static bool decode(const SystemCKernelValue& value,
            sc_dt::sc_lv<Width>& result)
        {
            if (!canonical_vector_metadata(value,
                    SystemCKernelValueKind::logic4,
                    static_cast<std::uint32_t>(Width), 2U)) {
                return false;
            }
            for (int bit = 0; bit < Width; ++bit) {
                const auto word = static_cast<std::size_t>(bit) / 64U;
                const auto shift = static_cast<unsigned>(bit) % 64U;
                const auto aval = (value.planes[0][word] >> shift) & 1U;
                const auto bval = (value.planes[1][word] >> shift) & 1U;
                const auto code = static_cast<unsigned>(aval | (bval << 1U));
                result.set_bit(bit,
                    static_cast<typename sc_dt::sc_lv<Width>::value_type>(code));
            }
            return true;
        }

        static SystemCKernelValue encode(const sc_dt::sc_lv<Width>& value)
        {
            SystemCKernelValue result;
            result.kind = SystemCKernelValueKind::logic4;
            result.width = static_cast<std::uint32_t>(Width);
            result.range = { Width - 1, 0,
                SystemCKernelRangeDirection::descending };
            result.planes.assign(2U,
                std::vector<std::uint64_t>(
                    (static_cast<std::size_t>(Width) + 63U) / 64U));
            for (int bit = 0; bit < Width; ++bit) {
                const auto code = static_cast<unsigned>(value.get_bit(bit));
                const auto word = static_cast<std::size_t>(bit) / 64U;
                const auto shift = static_cast<unsigned>(bit) % 64U;
                if ((code & 1U) != 0U) {
                    result.planes[0][word] |= std::uint64_t { 1U } << shift;
                }
                if ((code & 2U) != 0U) {
                    result.planes[1][word] |= std::uint64_t { 1U } << shift;
                }
            }
            return result;
        }
    };

} // namespace detail

template <typename T>
class backend_value_input final
    : public backend_port<sc_core::sc_signal_in_if<T>>,
      public backend_value_endpoint {
public:
    using base_type = backend_port<sc_core::sc_signal_in_if<T>>;
    using base_type::base_type;

    [[nodiscard]] bool bind_backend_interface(
        sc_core::sc_interface& target) override
    {
        target_ = dynamic_cast<sc_core::sc_signal_inout_if<T>*>(&target);
        return target_ != nullptr && base_type::bind_backend_interface(target);
    }

    [[nodiscard]] backend_endpoint_direction backend_direction()
        const noexcept override
    {
        return backend_endpoint_direction::input;
    }

    [[nodiscard]] bool apply_backend_value(
        const SystemCKernelValue& value) override
    {
        T native;
        if (target_ == nullptr
            || !detail::backend_value_traits<T>::decode(value, native)) {
            return false;
        }
        target_->write(native);
        return true;
    }

    [[nodiscard]] std::optional<SystemCKernelValue>
    sample_backend_value() const override
    {
        if (target_ == nullptr) {
            return std::nullopt;
        }
        return detail::backend_value_traits<T>::encode(target_->read());
    }

private:
    sc_core::sc_signal_inout_if<T>* target_ { };
};

template <typename T>
class backend_value_output final
    : public backend_port<sc_core::sc_signal_inout_if<T>>,
      public backend_value_endpoint {
public:
    using base_type = backend_port<sc_core::sc_signal_inout_if<T>>;
    using base_type::base_type;

    [[nodiscard]] bool bind_backend_interface(
        sc_core::sc_interface& target) override
    {
        target_ = dynamic_cast<sc_core::sc_signal_inout_if<T>*>(&target);
        return target_ != nullptr && base_type::bind_backend_interface(target);
    }

    [[nodiscard]] backend_endpoint_direction backend_direction()
        const noexcept override
    {
        return backend_endpoint_direction::output;
    }

    [[nodiscard]] bool apply_backend_value(
        const SystemCKernelValue&) override
    {
        return false;
    }

    [[nodiscard]] std::optional<SystemCKernelValue>
    sample_backend_value() const override
    {
        if (target_ == nullptr) {
            return std::nullopt;
        }
        return detail::backend_value_traits<T>::encode(target_->read());
    }

private:
    sc_core::sc_signal_inout_if<T>* target_ { };
};

} // namespace fsim::systemc
