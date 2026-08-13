// SPDX-License-Identifier: Apache-2.0
class ProcessExecutionContext {
public:
    virtual ~ProcessExecutionContext() = default;

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
    virtual void write_container_object(
        ContainerObjectId, const ContainerValue&)
    {
        throw std::logic_error {
            "alternate process executor does not support container objects"
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
    [[nodiscard]] virtual Logic9Word
    read_signal_logic9_word(SignalId signal) const
    {
        return read_signal(signal).logic9_low_word();
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
