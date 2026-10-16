// SPDX-License-Identifier: Apache-2.0
#include "../../src/runtime/simir_internal.hpp"

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct PackageStaticStartupRuntimeTestAccess final {
    [[nodiscard]] static bool graph_has_writer(
        const Interpreter& interpreter,
        const ProcessId process,
        const SignalId signal)
    {
        const auto& state = *interpreter.impl_;
        if (!state.region_graph) {
            return true;
        }
        const auto node = std::ranges::find(
            state.region_graph->processes(), process,
            &RegionProcessNode::process);
        return node == state.region_graph->processes().end()
            || std::ranges::any_of(node->writes,
                [signal](const auto& write) {
                    return write.signal == signal;
                });
    }

    static void write_package_string_as(
        Interpreter& interpreter,
        const ProcessId process,
        const StringObjectId object)
    {
        interpreter.impl_->initialize_package_static_string_object(
            object, "forged", process);
    }
};

} // namespace fsim::runtime::simir

namespace fsim::tests::runtime {

namespace {

    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    constexpr std::uint32_t kPackageOwnerId = 1U;

    void require(const bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error { message };
        }
    }

    template <typename Action>
    void require_logic_error(
        Action&& action,
        const std::string_view expected_message)
    {
        try {
            std::forward<Action>(action)();
        } catch (const std::logic_error& error) {
            const std::string_view message { error.what() };
            require(message.find(expected_message) != std::string_view::npos,
                "startup failure has the expected diagnostic");
            return;
        }
        throw std::runtime_error { "expected a startup logic_error" };
    }

    template <typename Action>
    void require_invalid_argument(
        Action&& action,
        const std::string_view expected_message)
    {
        try {
            std::forward<Action>(action)();
        } catch (const std::invalid_argument& error) {
            const std::string_view message { error.what() };
            require(message.find(expected_message) != std::string_view::npos,
                "startup metadata failure has the expected diagnostic");
            return;
        }
        throw std::runtime_error {
            "expected a startup metadata invalid_argument" };
    }

    class NativeStartupWriter final : public ProcessExecutor {
    public:
        NativeStartupWriter(
            const Interpreter& interpreter,
            const SignalId source,
            const SignalId target,
            std::size_t& resume_count,
            bool& startup_active_seen)
            : interpreter_ { interpreter }
            , source_ { source }
            , target_ { target }
            , resume_count_ { resume_count }
            , startup_active_seen_ { startup_active_seen }
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            const InstructionIndex start) override
        {
            require(start == 0U, "startup native writer starts at PC zero");
            startup_active_seen_
                = interpreter_.package_static_initialization_active();
            ++resume_count_;
            const auto source = context.read_signal_word(source_);
            context.write_blocking_word(
                target_, Logic4Word { 8U, source.aval, source.bval });
            context.write_blocking_slice_word(
                target_, Logic4Word { 2U, 2U, 0U }, 1U);
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        const Interpreter& interpreter_;
        SignalId source_ { };
        SignalId target_ { };
        std::size_t& resume_count_;
        bool& startup_active_seen_;
    };

    enum class StartupSliceMutation : std::uint8_t {
        force_signal,
        release_signal,
        force_driver_signal,
        release_driver_signal,
    };

    class ContextSliceMutationStartupExecutor final : public ProcessExecutor {
    public:
        ContextSliceMutationStartupExecutor(
            const SignalId signal,
            const StartupSliceMutation mutation,
            std::size_t& resume_count)
            : signal_ { signal }
            , mutation_ { mutation }
            , resume_count_ { resume_count }
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            const InstructionIndex start) override
        {
            require(start == 0U,
                "startup slice mutation starts at PC zero");
            ++resume_count_;
            switch (mutation_) {
            case StartupSliceMutation::force_signal:
                context.force_signal_slice(
                    signal_, PackedLogic4::from_msb_string("1"), 1U);
                break;
            case StartupSliceMutation::release_signal:
                context.release_signal_slice(signal_, 1U, 1U);
                break;
            case StartupSliceMutation::force_driver_signal:
                context.force_driver_signal_slice(
                    signal_, PackedLogic4::from_msb_string("1"), 1U);
                break;
            case StartupSliceMutation::release_driver_signal:
                context.release_driver_signal_slice(signal_, 1U, 1U);
                break;
            }
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        SignalId signal_ { };
        StartupSliceMutation mutation_ { };
        std::size_t& resume_count_;
    };

    class StringWriteStartupExecutor final : public ProcessExecutor {
    public:
        explicit StringWriteStartupExecutor(const StringObjectId object)
            : object_ { object }
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            const InstructionIndex start) override
        {
            require(start == 0U,
                "startup string writer starts at PC zero");
            context.write_string_object(object_, "after");
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        StringObjectId object_ { };
    };

    class ForeignStringWriteStartupExecutor final : public ProcessExecutor {
    public:
        ForeignStringWriteStartupExecutor(
            const StringObjectId object,
            std::string_view value)
            : object_ { object }
            , value_ { value }
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            const InstructionIndex start) override
        {
            require(start == 0U,
                "foreign startup string writer starts at PC zero");
            context.write_string_object(object_, value_);
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        StringObjectId object_ { };
        std::string value_;
    };

    class MismatchedProcessStringWriteStartupExecutor final
        : public ProcessExecutor {
    public:
        MismatchedProcessStringWriteStartupExecutor(
            Interpreter& interpreter,
            const ProcessId process,
            const StringObjectId object)
            : interpreter_ { interpreter }
            , process_ { process }
            , object_ { object }
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext&,
            const InstructionIndex start) override
        {
            require(start == 0U,
                "mismatched startup string writer starts at PC zero");
            PackageStaticStartupRuntimeTestAccess::write_package_string_as(
                interpreter_, process_, object_);
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        Interpreter& interpreter_;
        ProcessId process_ { };
        StringObjectId object_ { };
    };

    class PublicStringDepositStartupExecutor final : public ProcessExecutor {
    public:
        PublicStringDepositStartupExecutor(
            Interpreter& interpreter,
            const StringObjectId object)
            : interpreter_ { interpreter }
            , object_ { object }
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext&,
            const InstructionIndex start) override
        {
            require(start == 0U,
                "public startup string deposit starts at PC zero");
            interpreter_.deposit_string_object(object_, "borrowed");
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        Interpreter& interpreter_;
        StringObjectId object_ { };
    };

    class PublicContainerDepositStartupExecutor final : public ProcessExecutor {
    public:
        PublicContainerDepositStartupExecutor(
            Interpreter& interpreter,
            const ContainerObjectId object,
            ContainerValue value)
            : interpreter_ { interpreter }
            , object_ { object }
            , value_ { std::move(value) }
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext&,
            const InstructionIndex start) override
        {
            require(start == 0U,
                "public startup container deposit starts at PC zero");
            interpreter_.deposit_container_object(object_, value_);
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        Interpreter& interpreter_;
        ContainerObjectId object_ { };
        ContainerValue value_;
    };

    class ContainerWriteStartupExecutor final : public ProcessExecutor {
    public:
        ContainerWriteStartupExecutor(
            const ContainerObjectId object,
            ContainerValue value)
            : object_ { object }
            , value_ { std::move(value) }
        {
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext& context,
            const InstructionIndex start) override
        {
            require(start == 0U,
                "startup container writer starts at PC zero");
            context.write_container_object(object_, value_, false);
            ProcessResumeResult result { 0U, 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        ContainerObjectId object_ { };
        ContainerValue value_;
    };

    [[nodiscard]] Process startup_process(
        const ProcessId id,
        const SignalId target,
        const std::uint64_t order)
    {
        Process process;
        process.id = id;
        process.name = "package_static_startup_test";
        process.register_count = 1U;
        process.operations = { Halt { } };
        process.driver_regions.push_back({ target, 0U, 0U, true });
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.startup_phase
            = ProcessStartupPhase::package_static_initializer;
        process.startup_order = order;
        process.startup_package_owner_id = kPackageOwnerId;
        return process;
    }

    void verify_ordered_actions_and_silent_baseline()
    {
        Interpreter interpreter;
        const auto package_value = interpreter.add_signal({
            "package.value", PackedLogic4::from_msb_string("00000000") });
        const auto native_value = interpreter.add_signal({
            "package.native_value", PackedLogic4::from_msb_string("00000000") });
        const auto order_observed = interpreter.add_signal({
            "top.order_observed", PackedLogic4::from_msb_string("00000000") });
        const auto event_value = interpreter.add_signal({
            "top.event_value", PackedLogic4::from_msb_string("0") });
        const auto active_value = interpreter.add_signal({
            "top.active_value", PackedLogic4::from_msb_string("0") });
        const auto previous_value = interpreter.add_signal({
            "top.previous_value", PackedLogic4::from_msb_string("00000000") });
        const auto last_event = interpreter.add_signal({
            "top.last_event", PackedLogic4::from_aval_bval(64U, 0U, 0U) });
        const auto last_active = interpreter.add_signal({
            "top.last_active", PackedLogic4::from_aval_bval(64U, 0U, 0U) });
        const auto native_event = interpreter.add_signal({
            "top.native_event", PackedLogic4::from_msb_string("0") });
        const auto native_active = interpreter.add_signal({
            "top.native_active", PackedLogic4::from_msb_string("0") });
        const auto native_previous = interpreter.add_signal({
            "top.native_previous", PackedLogic4::from_msb_string("00000000") });
        const auto native_last_event = interpreter.add_signal({
            "top.native_last_event", PackedLogic4::from_aval_bval(64U, 0U, 0U) });
        const auto native_last_active = interpreter.add_signal({
            "top.native_last_active", PackedLogic4::from_aval_bval(64U, 0U, 0U) });

        const auto text_object = interpreter.add_string_object({
            "package.text", "before" });
        ContainerType queue_type;
        queue_type.element_width = 8U;
        queue_type.queue = true;
        const auto queue_object = interpreter.add_container_object({
            "package.queue",
            ContainerValue {
                queue_type,
                { PackedLogic4::from_msb_string("00000000") }, { } },
            std::nullopt });

        auto native = startup_process(0U, native_value, 20U);
        const auto native_process = interpreter.add_process(std::move(native));
        std::size_t native_resume_count { };
        bool native_observed_startup_active { };
        interpreter.set_process_executor(
            native_process,
            std::make_unique<NativeStartupWriter>(
                interpreter, package_value, native_value,
                native_resume_count, native_observed_startup_active));

        Process observer;
        observer.id = 1U;
        observer.name = "ordinary_startup_observer";
        observer.register_count = 11U;
        observer.operations = {
            SignalEvent { 0U, package_value },
            WriteBlocking { event_value, 0U },
            SignalActive { 1U, package_value },
            WriteBlocking { active_value, 1U },
            SignalLastValue { 2U, package_value },
            WriteBlocking { previous_value, 2U },
            SignalLastEvent { 3U, package_value },
            WriteBlocking { last_event, 3U },
            SignalLastActive { 4U, package_value },
            WriteBlocking { last_active, 4U },
            SignalEvent { 5U, native_value },
            WriteBlocking { native_event, 5U },
            SignalActive { 6U, native_value },
            WriteBlocking { native_active, 6U },
            SignalLastValue { 7U, native_value },
            WriteBlocking { native_previous, 7U },
            SignalLastEvent { 8U, native_value },
            WriteBlocking { native_last_event, 8U },
            SignalLastActive { 9U, native_value },
            WriteBlocking { native_last_active, 9U },
            ReadSignal { 10U, native_value },
            WriteBlocking { order_observed, 10U },
            Halt { },
        };
        (void)interpreter.add_process(std::move(observer));

        std::array signal_allowlist { package_value };
        std::array string_allowlist { text_object };
        std::array container_allowlist { queue_object };
        std::size_t package_callback_count { };
        std::size_t class_property_callback_count { };
        const ContainerValue initialized_queue {
            queue_type,
            { PackedLogic4::from_msb_string("00000001"),
                PackedLogic4::from_msb_string("00000010") },
            { } };

        // Register actions in the reverse of their semantic order. The class
        // callback models a package-owned static property initializer with no
        // target signal; it runs after the tagged SimIR action.
        interpreter.register_package_static_startup_callback(
            30U, std::span { signal_allowlist }, { }, { }, [&] {
                ++class_property_callback_count;
                require(interpreter.package_static_initialization_active(),
                    "package callbacks observe startup phase");
                interpreter.deposit_signal(
                    package_value,
                    PackedLogic4::from_msb_string("00000010"));
            });
        interpreter.register_package_static_startup_callback(
            10U, std::span { signal_allowlist },
            std::span { string_allowlist },
            std::span { container_allowlist }, [&] {
                ++package_callback_count;
                require(interpreter.package_static_initialization_active(),
                    "package callbacks observe startup phase");
                interpreter.deposit_signal(
                    package_value,
                    PackedLogic4::from_msb_string("00000001"));
                interpreter.deposit_string_object(text_object, "initialized");
                interpreter.deposit_container_object(
                    queue_object, initialized_queue);
            });
        signal_allowlist[0U] = native_value;
        string_allowlist[0U] = std::numeric_limits<StringObjectId>::max();
        container_allowlist[0U]
            = std::numeric_limits<ContainerObjectId>::max();

        interpreter.start();
        interpreter.start();
        require(native_observed_startup_active
                && !interpreter.package_static_initialization_active(),
            "startup phase query covers tagged executors and clears afterward");
        require(package_callback_count == 1U
                && class_property_callback_count == 1U
                && native_resume_count == 1U,
            "ordered startup callbacks run once even when start is repeated");
        require(interpreter.process_program(native_process).startup_phase
                    == ProcessStartupPhase::package_static_initializer
                && interpreter.process_program(native_process).startup_order
                    == 20U,
            "the tagged process retains its phase and global order");
        require(!PackageStaticStartupRuntimeTestAccess::graph_has_writer(
                    interpreter, native_process, native_value),
            "a completed package initializer is not an ongoing graph writer");

        const auto result = interpreter.run();
        require(result.status == RunStatus::completed,
            "ordinary time-zero work completes after package startup");
        require(interpreter.signal_value(native_value).to_msb_string()
                    == "00000101"
                && interpreter.signal_value(package_value).to_msb_string()
                    == "00000010"
                && interpreter.signal_value(order_observed).to_msb_string()
                    == "00000101",
            "callbacks and the tagged process follow their shared startup order");
        require(interpreter.string_object_value(text_object) == "initialized"
                && interpreter.container_object_value_snapshot(queue_object)
                    == initialized_queue,
            "typed startup allowlists establish package string and container values");

        const auto never_time
            = PackedLogic4::from_aval_bval(
                64U,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()),
                0U);
        require(interpreter.signal_value(event_value).to_msb_string() == "0"
                && interpreter.signal_value(active_value).to_msb_string() == "0"
                && interpreter.signal_value(previous_value).to_msb_string()
                    == "00000010"
                && interpreter.signal_value(last_event) == never_time
                && interpreter.signal_value(last_active) == never_time,
            "callback writes establish a silent signal history baseline");
        require(interpreter.signal_value(native_event).to_msb_string() == "0"
                && interpreter.signal_value(native_active).to_msb_string()
                    == "0"
                && interpreter.signal_value(native_previous).to_msb_string()
                    == "00000101"
                && interpreter.signal_value(native_last_event) == never_time
                && interpreter.signal_value(native_last_active) == never_time,
            "compiled word and slice paths fall back to silent startup commits");
        (void)interpreter.run();
        require(package_callback_count == 1U
                && class_property_callback_count == 1U
                && native_resume_count == 1U,
            "completed startup actions are not repeated by later runs");
    }

    void verify_duplicate_order_rejected_before_execution()
    {
        Interpreter interpreter;
        const auto target = interpreter.add_signal({
            "package.duplicate_target", PackedLogic4::from_msb_string("0") });
        auto process = startup_process(0U, target, 7U);
        process.operations = {
            LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
            WriteBlocking { target, 0U },
            Halt { },
        };
        (void)interpreter.add_process(std::move(process));
        std::size_t callback_count { };
        interpreter.register_package_static_startup_callback(
            7U, { }, { }, { }, [&] { ++callback_count; });
        (void)require_logic_error(
            [&] { interpreter.start(); },
            "startup action order is not unique");
        require(callback_count == 0U
                && interpreter.signal_value(target).to_msb_string() == "0",
            "duplicate order is rejected before any startup side effect");
    }

    void verify_callback_allowlist_and_failed_start_are_one_shot()
    {
        Interpreter interpreter;
        const auto allowed = interpreter.add_signal({
            "package.allowed", PackedLogic4::from_msb_string("0") });
        const auto denied = interpreter.add_signal({
            "package.denied", PackedLogic4::from_msb_string("0") });
        const std::array allowed_ids { allowed };
        std::size_t callback_count { };
        interpreter.register_package_static_startup_callback(
            3U, std::span { allowed_ids }, { }, { }, [&] {
                ++callback_count;
                interpreter.deposit_signal(
                    denied, PackedLogic4::from_msb_string("1"));
            });
        (void)require_logic_error(
            [&] { interpreter.start(); },
            "outside its package write closure");
        require(callback_count == 1U
                && interpreter.signal_value(allowed).to_msb_string() == "0"
                && interpreter.signal_value(denied).to_msb_string() == "0",
            "an out-of-closure callback write does not modify signal state");
        (void)require_logic_error(
            [&] { interpreter.start(); },
            "cannot be retried after package startup actions ran");
        require(callback_count == 1U,
            "a failed startup action is never replayed on retry");
    }

    void verify_completed_action_is_not_replayed_after_later_failure()
    {
        Interpreter interpreter;
        const auto allowed = interpreter.add_signal({
            "package.completed_action",
            PackedLogic4::from_msb_string("0") });
        const auto denied = interpreter.add_signal({
            "package.failing_action",
            PackedLogic4::from_msb_string("0") });
        const std::array allowed_ids { allowed };
        std::size_t completed_action_count { };
        std::size_t failing_action_count { };
        interpreter.register_package_static_startup_callback(
            1U, std::span { allowed_ids }, { }, { }, [&] {
                ++completed_action_count;
                interpreter.deposit_signal(
                    allowed, PackedLogic4::from_msb_string("1"));
            });
        interpreter.register_package_static_startup_callback(
            2U, { }, { }, { }, [&] {
                ++failing_action_count;
                interpreter.deposit_signal(
                    denied, PackedLogic4::from_msb_string("1"));
            });
        (void)require_logic_error(
            [&] { interpreter.start(); },
            "outside its package write closure");
        require(completed_action_count == 1U
                && failing_action_count == 1U
                && interpreter.signal_value(allowed).to_msb_string() == "1"
                && interpreter.signal_value(denied).to_msb_string() == "0",
            "failure preserves completed baseline effects "
            "without partial denied writes");
        (void)require_logic_error(
            [&] { interpreter.start(); },
            "cannot be retried after package startup actions ran");
        require(completed_action_count == 1U && failing_action_count == 1U,
            "retry cannot replay completed or failed startup actions");
    }

    void verify_process_allowlist_and_retry_guard()
    {
        Interpreter interpreter;
        const auto allowed = interpreter.add_signal({
            "package.process_allowed",
            PackedLogic4::from_msb_string("0") });
        const auto denied = interpreter.add_signal({
            "package.process_denied",
            PackedLogic4::from_msb_string("0") });
        auto process = startup_process(0U, allowed, 4U);
        process.register_count = 1U;
        process.operations = {
            LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
            WriteBlocking { denied, 0U },
            Halt { },
        };
        (void)interpreter.add_process(std::move(process));
        (void)require_logic_error(
            [&] { interpreter.start(); },
            "outside its package write closure");
        require(interpreter.signal_value(allowed).to_msb_string() == "0"
                && interpreter.signal_value(denied).to_msb_string() == "0",
            "an out-of-closure process write is rejected without publication");
        (void)require_logic_error(
            [&] { interpreter.start(); },
            "cannot be retried after package startup actions ran");
    }

    void verify_startup_string_object_process_writes_fail_closed()
    {
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.interpreted_string_guard_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_string_object({
                "package.interpreted_string_guard_object", "before",
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.register_count = 1U;
            process.string_register_count = 1U;
            process.operations = {
                LoadStringConstant { 0U, "after" },
                WriteStringObject { object, 0U },
                LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
                WriteBlocking { target, 0U },
                Halt { },
            };
            process.startup_writable_string_object_ids = { object };
            (void)interpreter.add_process(std::move(process));
            interpreter.start();
            require(interpreter.string_object_value(object) == "after"
                    && interpreter.signal_value(target).to_msb_string() == "1",
                "typed package process writes its direct package string in order");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.executor_string_guard_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_string_object({
                "package.executor_string_guard_object", "before",
                kPackageOwnerId });
            const auto foreign = interpreter.add_string_object({
                "package.foreign_string_guard_object", "foreign-before",
                kPackageOwnerId + 1U });
            auto process = startup_process(0U, target, 1U);
            process.string_register_count = 1U;
            process.operations = {
                LoadStringConstant { 0U, "owned" },
                WriteStringObject { object, 0U },
                Halt { },
            };
            process.startup_writable_string_object_ids = { object };
            const auto id = interpreter.add_process(std::move(process));
            interpreter.set_process_executor(
                id, std::make_unique<ForeignStringWriteStartupExecutor>(
                    foreign, "after"));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "outside its direct string closure");
            require(interpreter.string_object_value(object) == "before"
                    && interpreter.string_object_value(foreign)
                        == "foreign-before"
                    && interpreter.signal_value(target).to_msb_string() == "0",
                "an executor cannot borrow another object's process closure");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.identity_string_guard_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_string_object({
                "package.identity_string_guard_object", "before",
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.operations = { Halt { } };
            const auto id = interpreter.add_process(std::move(process));
            interpreter.set_process_executor(
                id,
                std::make_unique<MismatchedProcessStringWriteStartupExecutor>(
                    interpreter, static_cast<ProcessId>(id + 1U), object));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "process identity is not active");
            require(interpreter.string_object_value(object) == "before",
                "a noncurrent process ID cannot authorize a startup string write");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.public_string_deposit_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_string_object({
                "package.public_string_deposit", "before",
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.string_register_count = 1U;
            process.operations = {
                LoadStringConstant { 0U, "direct" },
                WriteStringObject { object, 0U },
                Halt { },
            };
            process.startup_writable_string_object_ids = { object };
            const auto id = interpreter.add_process(std::move(process));
            interpreter.set_process_executor(
                id, std::make_unique<PublicStringDepositStartupExecutor>(
                    interpreter, object));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "outside its startup callback");
            require(interpreter.string_object_value(object) == "before",
                "public string deposit cannot borrow a process write closure");
        }
    }

    void verify_package_owner_identity_ignores_display_names()
    {
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "top.same_library_package_target",
                PackedLogic4::from_msb_string("0") });
            // These two package declarations have the same display spelling
            // but different semantic UnitIds (for example, from two
            // libraries). The runtime owner tokens are UnitId + 1.
            const auto local = interpreter.add_string_object({
                "startup_pkg.message", "local", 7U });
            const auto foreign = interpreter.add_string_object({
                "startup_pkg.message", "foreign", 19U });
            (void)local;
            auto process = startup_process(0U, target, 1U);
            process.startup_package_owner_id = 7U;
            process.string_register_count = 1U;
            process.operations = {
                LoadStringConstant { 0U, "must-not-publish" },
                WriteStringObject { foreign, 0U },
                Halt { },
            };
            process.startup_writable_string_object_ids = { foreign };
            require_invalid_argument(
                [&] { (void)interpreter.add_process(std::move(process)); },
                "foreign owner");
            require(interpreter.string_object_value(foreign) == "foreign",
                "equal display names in distinct semantic packages do not share ownership");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "top.escaped_package_target",
                PackedLogic4::from_msb_string("0") });
            constexpr std::uint32_t escaped_package_owner = 43U;
            const auto object = interpreter.add_string_object({
                "escaped_pkg.\\message.with.dot", "before",
                escaped_package_owner });
            auto process = startup_process(0U, target, 1U);
            process.startup_package_owner_id = escaped_package_owner;
            process.string_register_count = 1U;
            process.operations = {
                LoadStringConstant { 0U, "after" },
                WriteStringObject { object, 0U },
                Halt { },
            };
            process.startup_writable_string_object_ids = { object };
            (void)interpreter.add_process(std::move(process));
            interpreter.start();
            require(interpreter.string_object_value(object) == "after",
                "escaped package member spelling does not affect owner checks");
        }
    }

    void verify_startup_container_process_write_closure()
    {
        ContainerType queue_type;
        queue_type.element_width = 8U;
        queue_type.queue = true;
        const ContainerValue empty_queue { queue_type, { }, { } };
        const ContainerValue changed_queue {
            queue_type,
            { PackedLogic4::from_msb_string("00101010") },
            { } };
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.container_startup_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.items", empty_queue, std::nullopt,
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.container_register_count = 1U;
            process.container_register_types.push_back(queue_type);
            process.register_count = 1U;
            process.startup_writable_container_object_ids = { object };
            process.operations = {
                ReadContainerObject { 0U, object },
                LoadConstant {
                    0U, PackedLogic4::from_msb_string("00101010") },
                PushContainer { 0U, 0U, false, std::nullopt },
                WriteContainerObject { object, 0U, std::nullopt, false },
                Halt { },
            };
            (void)interpreter.add_process(std::move(process));
            interpreter.start();
            require(interpreter.container_object_value_snapshot(object)
                        == changed_queue,
                "typed package process publishes its direct whole-container write");
        }
        {
            ContainerType fixed_array_type;
            fixed_array_type.element_width = 8U;
            fixed_array_type.fixed = true;
            fixed_array_type.index_left = 0;
            fixed_array_type.index_right = 0;
            fixed_array_type.dimensions = { { 0, 0 } };
            const ContainerValue initial_array {
                fixed_array_type,
                { PackedLogic4::from_msb_string("00010001") }, { } };
            const ContainerValue updated_array {
                fixed_array_type,
                { PackedLogic4::from_msb_string("01011010") }, { } };
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.container_rmw_startup_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.items", initial_array, std::nullopt,
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.register_count = 2U;
            process.container_register_count = 1U;
            process.container_register_types.push_back(fixed_array_type);
            process.startup_writable_container_object_ids = { object };
            process.operations = {
                ReadContainerObject { 0U, object },
                LoadConstant { 0U, PackedLogic4::from_msb_string("0") },
                LoadConstant {
                    1U, PackedLogic4::from_msb_string("01011010") },
                ContainerWrite { 0U, 0U, 1U, true, false, false },
                WriteContainerObject { object, 0U, std::nullopt, false },
                Halt { },
            };
            (void)interpreter.add_process(std::move(process));
            interpreter.start();
            require(interpreter.container_object_value_snapshot(object)
                        == updated_array,
                "a direct package element read-modify-write publishes through its typed whole-object closure");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.foreign_container_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.items", empty_queue, std::nullopt,
                kPackageOwnerId });
            const auto foreign = interpreter.add_container_object({
                "package.foreign_items", empty_queue, std::nullopt,
                kPackageOwnerId + 1U });
            auto process = startup_process(0U, target, 1U);
            process.container_register_count = 1U;
            process.container_register_types.push_back(queue_type);
            process.operations = {
                ReadContainerObject { 0U, object },
                WriteContainerObject { object, 0U, std::nullopt, false },
                Halt { },
            };
            process.startup_writable_container_object_ids = { object };
            const auto id = interpreter.add_process(std::move(process));
            interpreter.set_process_executor(
                id, std::make_unique<ContainerWriteStartupExecutor>(
                    foreign, changed_queue));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "outside its direct container closure");
            require(interpreter.container_object_value_snapshot(foreign)
                        == empty_queue,
                "an executor cannot borrow a foreign container closure");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.deferred_container_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.deferred_items", empty_queue, std::nullopt,
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.container_register_count = 1U;
            process.container_register_types.push_back(queue_type);
            process.startup_writable_container_object_ids = { object };
            process.operations = {
                ReadContainerObject { 0U, object },
                WriteContainerObject { object, 0U, std::nullopt, true },
                Halt { },
            };
            require_invalid_argument(
                [&] { (void)interpreter.add_process(std::move(process)); },
                "cannot defer a whole-container write");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.transaction_container_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.transaction_items", empty_queue, std::nullopt,
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.container_register_count = 1U;
            process.container_register_types.push_back(queue_type);
            process.startup_writable_container_object_ids = { object };
            process.operations = {
                ReadContainerObject { 0U, object },
                WriteContainerObject { object, 0U, target, false },
                Halt { },
            };
            require_invalid_argument(
                [&] { (void)interpreter.add_process(std::move(process)); },
                "cannot defer a whole-container write");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.element_container_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.element_items", empty_queue, std::nullopt,
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.register_count = 1U;
            process.container_register_count = 1U;
            process.container_register_types.push_back(queue_type);
            process.operations = {
                WriteContainerObjectElement {
                    object, 0U, 0U, true, false, false,
                    std::nullopt, std::nullopt, { } },
                Halt { },
            };
            require_invalid_argument(
                [&] { (void)interpreter.add_process(std::move(process)); },
                "cannot use direct container-element writes");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.aliased_container_target",
                PackedLogic4::from_msb_string("0") });
            ContainerType fixed_array_type;
            fixed_array_type.element_width = 8U;
            fixed_array_type.fixed = true;
            fixed_array_type.index_left = 0;
            fixed_array_type.index_right = 0;
            fixed_array_type.dimensions = { { 0, 0 } };
            const ContainerValue fixed_array {
                fixed_array_type,
                { PackedLogic4::from_msb_string("00000000") }, { } };
            const auto parent = interpreter.add_container_object({
                "package.parent_items", fixed_array, std::nullopt,
                kPackageOwnerId });
            const auto alias = interpreter.add_container_object({
                "package.alias_items", fixed_array,
                ContainerSliceAlias { parent, 0, 0 }, kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.container_register_count = 1U;
            process.container_register_types.push_back(fixed_array_type);
            process.operations = {
                ReadContainerObject { 0U, alias },
                WriteContainerObject { alias, 0U, std::nullopt, false },
                Halt { },
            };
            process.startup_writable_container_object_ids = { alias };
            require_invalid_argument(
                [&] { (void)interpreter.add_process(std::move(process)); },
                "not direct owned storage");
        }
        {
            ContainerType fixed_array_type;
            fixed_array_type.element_width = 8U;
            fixed_array_type.fixed = true;
            fixed_array_type.index_left = 0;
            fixed_array_type.index_right = 0;
            fixed_array_type.dimensions = { { 0, 0 } };
            const ContainerValue initial_array {
                fixed_array_type,
                { PackedLogic4::from_msb_string("00010001") }, { } };
            const auto changed_array = ContainerValue {
                fixed_array_type,
                { PackedLogic4::from_msb_string("01011010") }, { } };
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.aliased_child_target",
                PackedLogic4::from_msb_string("0") });
            const auto parent = interpreter.add_container_object({
                "package.parent_items", initial_array, std::nullopt,
                kPackageOwnerId });
            const auto child = interpreter.add_container_object({
                "package.child_items",
                initial_array,
                ContainerSliceAlias { parent, 0, 0 },
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.container_register_count = 1U;
            process.container_register_types.push_back(fixed_array_type);
            process.startup_writable_container_object_ids = { parent };
            process.operations = {
                ReadContainerObject { 0U, parent },
                WriteContainerObject { parent, 0U, std::nullopt, false },
                Halt { },
            };
            const auto process_id
                = interpreter.add_process(std::move(process));
            interpreter.set_process_executor(
                process_id,
                std::make_unique<ContainerWriteStartupExecutor>(
                    parent, changed_array));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "cannot update aliased storage");
            require(interpreter.container_object_value_snapshot(parent)
                        == initial_array,
                "a distinct alias child prevents a startup write to its parent before mutation");
            require(interpreter.container_object_value_snapshot(child)
                        == initial_array,
                "rejected startup parent writes preserve the aliased child view");
        }
        {
            ContainerType wrong_queue_type;
            wrong_queue_type.element_width = 16U;
            wrong_queue_type.queue = true;
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.wrong_container_type_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.items", empty_queue, std::nullopt,
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.container_register_count = 1U;
            process.container_register_types.push_back(wrong_queue_type);
            process.startup_writable_container_object_ids = { object };
            process.operations = {
                WriteContainerObject { object, 0U, std::nullopt, false },
                Halt { },
            };
            (void)interpreter.add_process(std::move(process));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "requires direct unaliased storage");
            require(interpreter.container_object_value_snapshot(object)
                        == empty_queue,
                "a mismatched package container type is rejected without changing the object");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.exposed_container_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.items", empty_queue, std::nullopt,
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.operations = {
                ReadContainerObject { 0U, object },
                WriteContainerObject { object, 0U, std::nullopt, false },
                Halt { },
            };
            process.container_register_count = 1U;
            process.container_register_types.push_back(queue_type);
            process.startup_writable_container_object_ids = { object };
            const auto id = interpreter.add_process(std::move(process));
            interpreter.set_process_executor(
                id, std::make_unique<ContainerWriteStartupExecutor>(
                    object, changed_queue));
            const auto& exposed = interpreter.container_object_value(object);
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "requires direct unaliased storage");
            require(exposed == empty_queue,
                "startup whole-container writes preserve exposed references");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.public_container_deposit_target",
                PackedLogic4::from_msb_string("0") });
            const auto object = interpreter.add_container_object({
                "package.public_deposit_items", empty_queue, std::nullopt,
                kPackageOwnerId });
            auto process = startup_process(0U, target, 1U);
            process.container_register_count = 1U;
            process.container_register_types.push_back(queue_type);
            process.operations = {
                ReadContainerObject { 0U, object },
                WriteContainerObject { object, 0U, std::nullopt, false },
                Halt { },
            };
            process.startup_writable_container_object_ids = { object };
            const auto id = interpreter.add_process(std::move(process));
            interpreter.set_process_executor(
                id, std::make_unique<PublicContainerDepositStartupExecutor>(
                    interpreter, object, changed_queue));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "outside its startup callback");
            require(interpreter.container_object_value_snapshot(object)
                        == empty_queue,
                "public container deposit cannot borrow a process write closure");
        }
        {
            Interpreter interpreter;
            const auto object = interpreter.add_string_object({
                "package.ordinary_metadata_text", "before" });
            Process process;
            process.id = 0U;
            process.name = "ordinary-with-package-permission";
            process.string_register_count = 1U;
            process.operations = {
                LoadStringConstant { 0U, "after" },
                WriteStringObject { object, 0U },
                Halt { },
            };
            process.startup_writable_string_object_ids = { object };
            require_invalid_argument(
                [&] { (void)interpreter.add_process(std::move(process)); },
                "ordinary SimIR process carries package startup object permissions");
        }
    }

    void verify_nonbaseline_mutations_are_rejected()
    {
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.force_target", PackedLogic4::from_msb_string("0") });
            const std::array allowed { target };
            interpreter.register_package_static_startup_callback(
                1U, std::span { allowed }, { }, { }, [&] {
                    interpreter.force_signal(
                        target, PackedLogic4::from_msb_string("1"));
                });
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "cannot force a signal");
            require(interpreter.signal_value(target).to_msb_string() == "0"
                    && !interpreter.signal_is_forced(target),
                "force APIs cannot publish a nonbaseline startup mutation");
        }
        {
            Interpreter interpreter;
            ContainerType queue_type;
            queue_type.element_width = 8U;
            queue_type.queue = true;
            const ContainerValue initial {
                queue_type,
                { PackedLogic4::from_msb_string("00000000") }, { } };
            const auto allowed = interpreter.add_container_object({
                "package.allowed_queue", initial, std::nullopt });
            const auto denied = interpreter.add_container_object({
                "package.denied_queue", initial, std::nullopt });
            const std::array allowed_ids { allowed };
            interpreter.register_package_static_startup_callback(
                1U, { }, { }, std::span { allowed_ids }, [&] {
                    interpreter.deposit_container_object_element(
                        denied, 0U,
                        PackedLogic4::from_msb_string("00000001"));
                });
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "outside its container closure");
            require(interpreter.container_object_value_snapshot(denied)
                        == initial,
                "element deposits pass through the typed startup allowlist");
        }
    }

    void verify_execution_context_slice_forces_are_rejected()
    {
        constexpr std::array mutations {
            StartupSliceMutation::force_signal,
            StartupSliceMutation::release_signal,
            StartupSliceMutation::force_driver_signal,
            StartupSliceMutation::release_driver_signal,
        };
        for (const auto mutation : mutations) {
            Interpreter interpreter;
            require(!interpreter.package_static_initialization_active(),
                "startup phase is inactive before start");
            const auto target = interpreter.add_signal({
                "package.context_force_target",
                PackedLogic4::from_msb_string("0000") });
            const bool is_release
                = mutation == StartupSliceMutation::release_signal
                || mutation == StartupSliceMutation::release_driver_signal;
            if (is_release) {
                interpreter.force_signal(
                    target, PackedLogic4::from_msb_string("1111"));
            }
            auto process = startup_process(0U, target, 1U);
            process.operations = { Halt { } };
            const auto process_id
                = interpreter.add_process(std::move(process));
            std::size_t resume_count { };
            interpreter.set_process_executor(
                process_id,
                std::make_unique<ContextSliceMutationStartupExecutor>(
                    target, mutation, resume_count));

            const auto expected_message = is_release
                ? std::string_view { "cannot release" }
                : std::string_view { "cannot force" };
            (void)require_logic_error(
                [&] { interpreter.start(); }, expected_message);
            require(!interpreter.package_static_initialization_active(),
                "failed startup clears the active phase");
            require(resume_count == 1U,
                "startup executor reached the guarded context method");
            require(interpreter.signal_value(target).to_msb_string()
                        == (is_release ? "1111" : "0000")
                    && interpreter.signal_is_forced(target) == is_release,
                "context slice force/release fails before mutating signal state");
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "cannot be retried after package startup actions ran");
            require(resume_count == 1U,
                "failed slice action is not resumed on retry");
        }
    }

    void verify_empty_startup_phase_query()
    {
        Interpreter interpreter;
        require(!interpreter.package_static_initialization_active(),
            "empty interpreter has no active package startup phase");
        interpreter.start();
        require(!interpreter.package_static_initialization_active(),
            "ordinary startup does not activate the package startup phase");
    }

    void verify_suspended_and_deferred_actions_fail_closed()
    {
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.suspended_target",
                PackedLogic4::from_msb_string("0") });
            auto process = startup_process(0U, target, 1U);
            process.operations = { WaitFor { 1U }, Halt { } };
            (void)interpreter.add_process(std::move(process));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "did not finish synchronously");
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "cannot be retried after package startup actions ran");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.deferred_target",
                PackedLogic4::from_msb_string("0") });
            auto process = startup_process(0U, target, 1U);
            process.operations = {
                LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
                WriteAfter { target, 0U, 1U },
                Halt { },
            };
            (void)interpreter.add_process(std::move(process));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "created deferred work");
            require(interpreter.signal_value(target).to_msb_string() == "0",
                "a deferred initializer write has not run during startup");
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "cannot be retried after package startup actions ran");
        }
        {
            Interpreter interpreter;
            const auto target = interpreter.add_signal({
                "package.deferred_update_target",
                PackedLogic4::from_msb_string("0") });
            auto process = startup_process(0U, target, 1U);
            process.operations = {
                LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
                WriteUpdate {
                    target, 0U, SignalUpdateDomain::systemverilog_active },
                Halt { },
            };
            (void)interpreter.add_process(std::move(process));
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "created deferred work");
            require(interpreter.signal_value(target).to_msb_string() == "0",
                "a deferred update is not normalized into a startup commit");
            (void)require_logic_error(
                [&] { interpreter.start(); },
                "cannot be retried after package startup actions ran");
        }
    }

} // namespace

void test_package_static_startup_runtime()
{
    verify_ordered_actions_and_silent_baseline();
    verify_duplicate_order_rejected_before_execution();
    verify_callback_allowlist_and_failed_start_are_one_shot();
    verify_completed_action_is_not_replayed_after_later_failure();
    verify_process_allowlist_and_retry_guard();
    verify_startup_string_object_process_writes_fail_closed();
    verify_package_owner_identity_ignores_display_names();
    verify_startup_container_process_write_closure();
    verify_nonbaseline_mutations_are_rejected();
    verify_execution_context_slice_forces_are_rejected();
    verify_empty_startup_phase_query();
    verify_suspended_and_deferred_actions_fail_closed();
}

} // namespace fsim::tests::runtime
