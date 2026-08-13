// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string_view>

namespace fsim::runtime {

struct VcdSignal {
    std::uint32_t index { };
    friend bool operator==(VcdSignal, VcdSignal) = default;
};

/// Buffered Value Change Dump writer.
///
/// Declarations must be completed before begin() is called. A hierarchical
/// name is dot-separated; the last component is the signal reference and all
/// preceding components become nested VCD scopes.
class VcdWriter {
public:
    explicit VcdWriter(std::ostream& output, std::string_view timescale = "1ns",
        std::size_t buffer_capacity = 64 * 1024);
    ~VcdWriter();
    VcdWriter(VcdWriter&&) noexcept;
    VcdWriter& operator=(VcdWriter&&) noexcept;
    VcdWriter(const VcdWriter&) = delete;
    VcdWriter& operator=(const VcdWriter&) = delete;

    [[nodiscard]] VcdSignal declare_signal(std::string_view hierarchical_name,
        std::size_t width);
    /// Real-family values use VCD `real`; exact `time` and opaque `chandle`
    /// values remain 64-bit vectors so their identities are never rounded.
    [[nodiscard]] VcdSignal declare_systemverilog_scalar(
        std::string_view hierarchical_name,
        SystemVerilogScalarKind kind);

    /// Emit the header and position the dump at initial_time.
    void begin(SimulationTick initial_time = 0);

    void change(VcdSignal signal, Logic4 value);
    void change(VcdSignal signal, Logic9 value);
    void change(VcdSignal signal, const PackedBit2& value);
    void change(VcdSignal signal, const PackedLogic4& value);
    void change(VcdSignal signal, const PackedLogic9& value);
    void change(
        VcdSignal signal,
        const SystemVerilogScalarValue& value);

    /// Emit one standard checkpoint command. Changes made before
    /// end_checkpoint() are unconditional, even when equal to the last value.
    void begin_checkpoint(std::string_view command);
    void end_checkpoint();
    void change_unknown(VcdSignal signal);
    void comment(std::string_view text);

    /// Advance the output timestamp. Time may remain equal but never decrease.
    void set_time(SimulationTick time);
    void flush();

    [[nodiscard]] bool begun() const noexcept;
    [[nodiscard]] SimulationTick time() const noexcept;
    [[nodiscard]] std::uint64_t bytes_written() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime
