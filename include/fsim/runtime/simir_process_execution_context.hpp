// SPDX-License-Identifier: Apache-2.0
/// Compact, allocation-free nonblocking update produced by native code.
/// Batches preserve source execution order and are committed in that order.
struct ProcessUpdateWord {
    SignalId signal { };
    Logic4Word value;
    std::uint32_t offset { };
    bool slice { };
};

/// Mutable view of one callback-free native update accumulator. The view is
/// built once with the executor frame and lets the kernel consume all touched
/// words without first expanding masks into temporary ProcessUpdateWord
/// records.
struct ProcessUpdateSlotView {
    SignalId signal { };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t* active { };
    std::uint64_t* aval { };
    std::uint64_t* bval { };
    std::uint64_t* mask { };
};

/// One process's ordered slot set within a hierarchy-wide native cohort.
/// Batches retain the actual elaborated process identity rather than the
/// structurally shared process template identity.
struct ProcessUpdateSlotBatch {
    ProcessId process { };
    std::span<const ProcessUpdateSlotView> slots;
    std::span<std::uint64_t> active_words;
};

/// Mutable view of one callback-buffered single-word Logic9 update. The four
/// planes retain the exact std_ulogic values; mask selects bits written by the
/// current activation.
struct ProcessLogic9UpdateSlotView {
    SignalId signal { };
    std::uint32_t width { };
    std::uint64_t* planes { };
    std::uint64_t* mask { };
};

struct ProcessLogic9UpdateBatch {
    ProcessId process { };
    std::span<const ProcessLogic9UpdateSlotView> slots;
};

enum class CodeCoverageCounterRuntimeStatus : std::uint8_t {
    Recorded,
    Unavailable,
    OutOfRange,
};

/// Shared simulator-owned state for a generated native update phase. Empty
/// spans disable the phase path without changing the established callbacks.
struct ProcessNativeWordUpdate {
    std::uint64_t aval { };
    std::uint64_t bval { };
    std::uint32_t width { };
    ProcessId process { };
    std::uint32_t active { };
    std::uint32_t reserved { };
};

class ProcessExecutionContext {
public:
    virtual ~ProcessExecutionContext() = default;

    /// Exact static sensitivities which caused the current activation. The
    /// full bit is the conservative default for contexts which do not track
    /// scheduler triggers.
    [[nodiscard]] virtual std::uint64_t static_trigger_mask() const noexcept
    {
        return Process::full_static_trigger_mask;
    }

    /// True when a complete multiword Logic4 update may be staged as ordered
    /// word slices without changing module-path routing semantics. Native
    /// executors query this once per resume so wide updates can stay on the
    /// allocation-free update path.
    [[nodiscard]] virtual bool supports_direct_word_updates() const noexcept
    {
        return false;
    }

    [[nodiscard]] virtual PackedLogic4 read_signal(SignalId signal) const = 0;
    virtual void write_blocking(SignalId signal, PackedLogic4 value) = 0;

    [[nodiscard]] virtual std::string
    read_string_object(StringObjectId) const
    {
        throw std::logic_error {
            "alternate process executor cannot read string objects"
        };
    }
    virtual void write_string_object(StringObjectId, std::string_view)
    {
        throw std::logic_error {
            "alternate process executor cannot write string objects"
        };
    }
    [[nodiscard]] virtual ContainerValue
    read_container_object(ContainerObjectId) const
    {
        throw std::logic_error {
            "alternate process executor does not support container objects"
        };
    }
    [[nodiscard]] virtual bool copy_container_object(
        const ContainerObjectId object,
        ContainerValue& destination) const
    {
        auto source = read_container_object(object);
        if (destination.type != source.type) {
            return false;
        }
        destination = std::move(source);
        return true;
    }
    [[nodiscard]] virtual const ContainerValue*
    borrow_container_object(ContainerObjectId) const
    {
        return nullptr;
    }
    [[nodiscard]] virtual bool container_object_has_type(
        const ContainerObjectId object,
        const ContainerType& type) const
    {
        return read_container_object(object).type == type;
    }
    [[nodiscard]] virtual bool read_container_object_element(
        ContainerObjectId,
        std::size_t,
        PackedLogic4&) const
    {
        return false;
    }
    virtual void write_container_object(
        ContainerObjectId, const ContainerValue&)
    {
        throw std::logic_error {
            "alternate process executor does not support container objects"
        };
    }
    virtual void write_container_object_element(
        ContainerObjectId,
        const PackedLogic4&,
        bool,
        bool,
        const PackedLogic4&,
        ProcessId,
        InstructionIndex)
    {
        throw std::logic_error {
            "alternate process executor does not support direct container-object element writes"
        };
    }

    [[nodiscard]] virtual FileHandle open_file(
        std::string_view, std::string_view)
    {
        throw std::logic_error {
            "alternate process executor does not support file open"
        };
    }
    virtual void close_file(FileHandle)
    {
        throw std::logic_error {
            "alternate process executor does not support file close"
        };
    }
    virtual void write_file(
        FileHandle, std::string_view, bool)
    {
        throw std::logic_error {
            "alternate process executor does not support file writes"
        };
    }
    virtual void write_file_formatted(
        FileHandle,
        std::string_view,
        std::string_view,
        OutputFormat,
        const PackedLogic4&,
        bool,
        bool,
        std::uint32_t,
        bool,
        bool,
        SystemVerilogScalarKind = SystemVerilogScalarKind::None)
    {
        throw std::logic_error {
            "alternate process executor does not support formatted file writes"
        };
    }
    [[nodiscard]] virtual std::string read_file_line(
        FileHandle, std::uint32_t&)
    {
        throw std::logic_error { "alternate process executor does not support file reads" };
    }
    [[nodiscard]] virtual std::int32_t read_file_character(FileHandle)
    {
        throw std::logic_error { "alternate process executor does not support character reads" };
    }
    [[nodiscard]] virtual std::int32_t unread_file_character(
        FileHandle, std::int32_t)
    {
        throw std::logic_error { "alternate process executor does not support character pushback" };
    }
    [[nodiscard]] virtual bool file_end_of_file(FileHandle)
    {
        throw std::logic_error { "alternate process executor does not support file status" };
    }
    [[nodiscard]] virtual std::string file_error(
        FileHandle, bool&)
    {
        throw std::logic_error {
            "alternate process executor does not support file errors"
        };
    }
    [[nodiscard]] virtual std::int32_t position_file(
        FileHandle, FilePositionKind, std::int32_t, std::int32_t)
    {
        throw std::logic_error {
            "alternate process executor does not support file positioning"
        };
    }
    virtual void flush_file(std::optional<FileHandle>)
    {
        throw std::logic_error {
            "alternate process executor does not support file flushing"
        };
    }

    /// Allocation-free single-word access used by generated scalar/vector code.
    ///
    /// The default implementations preserve compatibility for alternate
    /// executors that only implement the object interface. The kernel overrides
    /// these methods to access its signal storage directly.
    [[nodiscard]] virtual Logic4Word
    read_signal_word(SignalId signal) const
    {
        return read_signal(signal).low_word();
    }
    /// Dense current-value planes for callback-free native reads. Empty spans
    /// preserve compatibility for alternate execution contexts.
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_signal_aval() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_signal_bval() const noexcept
    {
        return { };
    }
    /// Dense simulation-owned code-coverage counters. Generated code updates
    /// these directly; an empty span requests the checked rare-path service.
    [[nodiscard]] virtual std::span<std::uint64_t>
    direct_code_coverage_counters() noexcept
    {
        return { };
    }
    /// Checked fallback for saturation and unavailable/range diagnostics.
    [[nodiscard]] virtual CodeCoverageCounterRuntimeStatus
    record_code_coverage_counter(::fsim::runtime::CodeCoverageCounterId)
    {
        return CodeCoverageCounterRuntimeStatus::Unavailable;
    }
    /// Dense exact Logic9 planes for callback-free native reads of signals no
    /// wider than one word. Entries for non-Logic9 signals are zero.
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_signal_logic9_plane0() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_signal_logic9_plane1() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_signal_logic9_plane2() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_signal_logic9_plane3() const noexcept
    {
        return { };
    }
    /// Flattened current Logic4 planes and per-signal word offsets for
    /// callback-free arbitrary-width native reads. Empty spans preserve
    /// compatibility for alternate execution contexts.
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_wide_signal_aval() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_wide_signal_bval() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_wide_signal_logic9_plane2() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::span<const std::uint64_t>
    direct_wide_signal_logic9_plane3() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::span<const std::uint32_t>
    direct_wide_signal_offsets() const noexcept
    {
        return { };
    }
    /// Dense owner process for the currently valid unforced single-driver
    /// route of each signal. UINT32_MAX means that the generic update path is
    /// required. Native executors use this only to discard writes whose
    /// touched bits already match the published direct planes.
    [[nodiscard]] virtual std::span<const ProcessId>
    direct_single_driver_processes() const noexcept
    {
        return { };
    }
    /// Monotonic single-writer ownership derived from process output regions.
    /// Once a signal has multiple writers it remains ineligible, including
    /// after dynamically created processes terminate.
    [[nodiscard]] virtual std::span<const ProcessId>
    stable_single_writer_processes() const noexcept
    {
        return { };
    }
    [[nodiscard]] virtual std::uint64_t
    signal_writer_revision() const noexcept
    {
        return 0U;
    }
    [[nodiscard]] virtual Logic9Word
    read_signal_logic9_word(SignalId signal) const
    {
        return read_signal(signal).logic9_low_word();
    }
    /// Copy an arbitrary-width signal directly into caller-owned little-endian
    /// word planes. Generated code uses this path to avoid materializing an
    /// intermediate PackedLogic4 at the JIT/runtime boundary.
    virtual void read_signal_planes(
        SignalId signal,
        std::span<std::uint64_t> aval,
        std::span<std::uint64_t> bval,
        std::span<std::uint64_t> logic9_plane2,
        std::span<std::uint64_t> logic9_plane3) const
    {
        const auto value = read_signal(signal);
        const auto expected_words = (value.width() + 63U) / 64U;
        if (aval.size() != expected_words || bval.size() != expected_words
            || (!value.is_logic9()
                && (!logic9_plane2.empty() || !logic9_plane3.empty()))
            || (value.is_logic9()
                && (logic9_plane2.size() != expected_words
                    || logic9_plane3.size() != expected_words))) {
            throw std::logic_error {
                "arbitrary-width signal destination planes have an invalid size"
            };
        }
        std::ranges::copy(value.aval_words(), aval.begin());
        std::ranges::copy(value.bval_words(), bval.begin());
        if (value.is_logic9()) {
            std::ranges::copy(
                value.logic9_plane_words(2), logic9_plane2.begin());
            std::ranges::copy(
                value.logic9_plane_words(3), logic9_plane3.begin());
        }
    }
    virtual void write_blocking_word(
        SignalId signal, const Logic4Word value)
    {
        write_blocking(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval));
    }
    virtual void write_blocking_slice(
        SignalId signal,
        PackedLogic4 value,
        std::size_t offset) = 0;
    virtual void write_blocking_slice_word(
        SignalId signal,
        const Logic4Word value,
        std::uint32_t offset)
    {
        write_blocking_slice(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            offset);
    }

    virtual void force_signal_slice(
        SignalId,
        PackedLogic4,
        std::size_t)
    {
        throw std::logic_error {
            "alternate process executor does not support procedural force"
        };
    }
    virtual void release_signal_slice(
        SignalId,
        std::size_t,
        std::size_t)
    {
        throw std::logic_error {
            "alternate process executor does not support procedural release"
        };
    }
    virtual void force_driver_signal_slice(
        SignalId,
        PackedLogic4,
        std::size_t)
    {
        throw std::logic_error {
            "alternate process executor does not support driver-value force"
        };
    }
    virtual void release_driver_signal_slice(
        SignalId,
        std::size_t,
        std::size_t)
    {
        throw std::logic_error {
            "alternate process executor does not support driver-value release"
        };
    }

    virtual void write_update(SignalId signal, PackedLogic4 value) = 0;
    virtual void write_update_word(
        SignalId signal, const Logic4Word value)
    {
        write_update(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval));
    }
    virtual void write_update_slice(
        SignalId signal,
        PackedLogic4 value,
        std::size_t offset) = 0;
    virtual void write_update_slice_word(
        SignalId signal,
        const Logic4Word value,
        std::uint32_t offset)
    {
        write_update_slice(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            offset);
    }
    virtual void write_update_words(
        const std::span<const ProcessUpdateWord> updates)
    {
        for (const auto& update : updates) {
            if (update.slice) {
                write_update_slice_word(
                    update.signal, update.value, update.offset);
            } else {
                write_update_word(update.signal, update.value);
            }
        }
    }
    /// Consume a native batch whose signal identities, widths, and slice
    /// ranges were validated by the executor callback that created it.
    /// Alternate contexts retain the checked behavior by default.
    virtual void write_validated_update_words(
        const std::span<const ProcessUpdateWord> updates)
    {
        write_update_words(updates);
    }
    /// Stable identity for contexts which can consume one hierarchy-wide
    /// native update phase. A null identity requests the ordinary per-process
    /// path.
    [[nodiscard]] virtual const void*
    direct_update_domain() const noexcept
    {
        return nullptr;
    }
    /// Consume already validated native slot batches in canonical cohort
    /// order. Returns false without mutating slots when the context requires
    /// the ordinary checked path.
    virtual bool write_validated_update_slot_batches(
        std::span<const ProcessUpdateSlotBatch>)
    {
        return false;
    }
    /// Consume validated inline Logic9 accumulators without expanding them to
    /// temporary packed values. False leaves every mask untouched.
    virtual bool write_validated_logic9_update_batch(
        const ProcessLogic9UpdateBatch&)
    {
        return false;
    }
    /// Consume valid Logic9 batches in canonical cohort order. A false return
    /// permits partial consumption: consumed batches have their masks cleared,
    /// while rejected batches retain their masks for the generic fallback.
    virtual bool write_validated_logic9_update_batches(
        std::span<const ProcessLogic9UpdateBatch>)
    {
        return false;
    }
    virtual void write_after(SignalId signal, PackedLogic4 value,
        SimulationTick delay) = 0;
    virtual void write_after_word(
        SignalId signal, const Logic4Word value,
        SimulationTick delay)
    {
        write_after(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            delay);
    }
    virtual void write_after_slice(
        SignalId signal,
        PackedLogic4 value,
        std::size_t offset,
        SimulationTick delay) = 0;
    virtual void write_after_slice_word(
        SignalId signal,
        const Logic4Word value,
        std::uint32_t offset,
        SimulationTick delay)
    {
        write_after_slice(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            offset,
            delay);
    }
    virtual void write_inertial(
        SignalId signal,
        PackedLogic4 value,
        const TransitionDelays& delays) = 0;
    virtual void write_inertial_word(
        SignalId signal,
        const Logic4Word value,
        const TransitionDelays& delays)
    {
        write_inertial(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            delays);
    }
    virtual void write_inertial_slice(
        SignalId signal,
        PackedLogic4 value,
        std::size_t offset,
        const TransitionDelays& delays) = 0;
    virtual void write_inertial_slice_word(
        SignalId signal,
        const Logic4Word value,
        std::uint32_t offset,
        const TransitionDelays& delays)
    {
        write_inertial_slice(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            offset,
            delays);
    }
    virtual void write_projected(
        SignalId signal,
        PackedLogic4 value,
        SimulationTick delay,
        SimulationTick rejection,
        ProjectedDelayMode mode)
    {
        (void)signal;
        (void)value;
        (void)delay;
        (void)rejection;
        (void)mode;
        throw std::logic_error {
            "alternate process executor does not support projected writes"
        };
    }
    virtual void write_projected_word(
        SignalId signal,
        const Logic4Word value,
        SimulationTick delay,
        SimulationTick rejection,
        ProjectedDelayMode mode)
    {
        write_projected(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            delay,
            rejection,
            mode);
    }
    virtual void write_projected_slice(
        SignalId signal,
        PackedLogic4 value,
        std::size_t offset,
        SimulationTick delay,
        SimulationTick rejection,
        ProjectedDelayMode mode)
    {
        (void)signal;
        (void)value;
        (void)offset;
        (void)delay;
        (void)rejection;
        (void)mode;
        throw std::logic_error {
            "alternate process executor does not support projected slice writes"
        };
    }
    virtual void write_projected_slice_word(
        SignalId signal,
        const Logic4Word value,
        std::uint32_t offset,
        SimulationTick delay,
        SimulationTick rejection,
        ProjectedDelayMode mode)
    {
        write_projected_slice(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            offset,
            delay,
            rejection,
            mode);
    }
    virtual void write_projected_waveform(
        SignalId signal,
        std::vector<ProjectedWaveformValue> elements,
        SimulationTick rejection,
        ProjectedDelayMode mode)
    {
        (void)signal;
        (void)elements;
        (void)rejection;
        (void)mode;
        throw std::logic_error {
            "alternate process executor does not support projected waveforms"
        };
    }
    virtual void write_projected_waveform_slice(
        SignalId signal,
        std::vector<ProjectedWaveformValue> elements,
        std::size_t offset,
        SimulationTick rejection,
        ProjectedDelayMode mode)
    {
        (void)signal;
        (void)elements;
        (void)offset;
        (void)rejection;
        (void)mode;
        throw std::logic_error {
            "alternate process executor does not support projected slice "
            "waveforms"
        };
    }

    /// Notify a kernel-owned event identity from an alternate language
    /// executor. Immediate notifications re-enter the active worklist at the
    /// current timestamp. Delta notifications enter the next delta; non-zero
    /// delays enter the active worklist at the requested future timestamp.
    virtual void notify_event(
        SignalId,
        SimulationTick,
        EventNotificationKind)
    {
        throw std::logic_error {
            "alternate process executor does not support event notification"
        };
    }
    virtual void cancel_event(SignalId)
    {
        throw std::logic_error {
            "alternate process executor does not support event cancellation"
        };
    }

    /// True only in the evaluation delta caused by the signal's most recent
    /// committed value change.
    [[nodiscard]] virtual bool signal_event(SignalId) const
    {
        return false;
    }

    /// Return the effective value immediately before the signal's latest
    /// committed value change.
    [[nodiscard]] virtual Logic4Word signal_last_value_word(SignalId) const
    {
        throw std::logic_error {
            "alternate process executor does not support signal last-value reads"
        };
    }
    [[nodiscard]] virtual Logic9Word
    signal_last_value_logic9_word(SignalId) const
    {
        throw std::logic_error {
            "alternate process executor does not support exact signal "
            "last-value reads"
        };
    }

    /// Elapsed global-resolution ticks since the latest effective-value event,
    /// or the maximum tick value if the signal has never changed.
    [[nodiscard]] virtual SimulationTick signal_last_event(SignalId) const
    {
        return std::numeric_limits<SimulationTick>::max();
    }

    /// True in the evaluation delta caused by the signal's most recent
    /// committed transaction, including a transaction that did not change its
    /// effective value.
    [[nodiscard]] virtual bool signal_active(SignalId) const
    {
        return false;
    }

    /// Elapsed global-resolution ticks since the latest committed transaction,
    /// or the maximum tick value if the signal has never been active.
    [[nodiscard]] virtual SimulationTick signal_last_active(SignalId) const
    {
        return std::numeric_limits<SimulationTick>::max();
    }

    [[nodiscard]] virtual bool signal_driving(SignalId) const
    {
        return false;
    }

    [[nodiscard]] virtual Logic4Word signal_driving_value_word(SignalId) const
    {
        throw std::logic_error {
            "alternate process executor does not support signal driving-value reads"
        };
    }
    [[nodiscard]] virtual Logic9Word
    signal_driving_value_logic9_word(SignalId) const
    {
        throw std::logic_error {
            "alternate process executor does not support exact signal "
            "driving-value reads"
        };
    }

    /// Request one alternate-language primitive-channel update. `channel` is a
    /// stable executor-owned identity. The kernel deduplicates it until the
    /// corresponding update callback finishes and invokes that callback in the
    /// common update phase.
    virtual void request_channel_update(std::uint64_t)
    {
        throw std::logic_error {
            "alternate process executor does not support channel updates"
        };
    }

    virtual void display(std::string_view, bool) { }
    virtual void postpone_display(std::string_view, bool) { }
    [[nodiscard]] virtual SimulationTick current_time() const noexcept { return 0; }
    [[nodiscard]] virtual SystemVerilogTimeFormat
    systemverilog_time_format() const
    {
        return { };
    }
    virtual void display_formatted(
        std::string_view,
        std::string_view,
        OutputFormat,
        const PackedLogic4&,
        bool,
        bool,
        bool,
        bool,
        std::uint32_t,
        bool,
        bool,
        SystemVerilogScalarKind = SystemVerilogScalarKind::None) { }
    virtual void display_time(
        std::string_view,
        std::string_view,
        bool,
        bool,
        std::uint32_t,
        bool,
        bool,
        bool) { }
    virtual void install_monitor(const MonitorInstall&) { }
    virtual void set_monitor_enabled(bool) { }
    [[nodiscard]] virtual PackedLogic4 random_value(
        RandomKind,
        const std::optional<PackedLogic4>&,
        const std::optional<PackedLogic4>&)
    {
        throw std::logic_error {
            "alternate process executor does not support random values"
        };
    }
    virtual void report(
        std::string_view,
        AssertionSeverity,
        const SourceLocation&) { }
    /// Execute one VHDL-2019 report/assert through the scheduler-owned policy.
    /// The policy may suppress the report, format it, count it, or raise a
    /// FAILURE after the generated C ABI has returned to C++.
    virtual void vhdl_report(
        InstructionIndex instruction,
        std::string_view message,
        AssertionSeverity severity,
        const SourceLocation& source,
        bool standalone)
    {
        (void)instruction;
        (void)standalone;
        report(message, severity, source);
    }

    [[nodiscard]] virtual Logic9 evaluate_vital_timing_check(
        InstructionIndex,
        const VitalTimingCheck&)
    {
        throw std::logic_error {
            "alternate process executor does not support VITAL timing checks"
        };
    }
    virtual void execute_vital_delay(
        InstructionIndex,
        const VitalDelay&,
        const VitalDelayRuntimeValues&)
    {
        throw std::logic_error {
            "alternate process executor does not support VITAL delays"
        };
    }

    /// True when an embedding debugger currently requests source boundaries.
    [[nodiscard]] virtual bool execution_points_enabled() const noexcept
    {
        return false;
    }
};
