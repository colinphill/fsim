// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_checkpoint.hpp"

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/vpi_time.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {

namespace {

    using namespace fsim::runtime;

    void require_vpi_checkpoint(
        const bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    SystemVerilogVpiStoredValue checkpoint_value(
        const std::uint32_t width, const Logic4 fill)
    {
        SystemVerilogVpiStoredValue value;
        value.payload = PackedLogic4 { width, fill };
        return value;
    }

    SystemVerilogVpiStoredValue checkpoint_value(
        const std::string_view spelling)
    {
        SystemVerilogVpiStoredValue value;
        value.payload = PackedLogic4::from_msb_string(spelling);
        return value;
    }

    SystemVerilogVpiCheckpointCompatibility checkpoint_compatibility()
    {
        return {
            "design-content-157",
            "native-cache-llvm22-debug",
            { {
                "checkpoint-plugin",
                "plugins/checkpoint.vpi",
                "plugin-content-157",
                "host-abi-content-157",
            } },
        };
    }

    struct CheckpointFixture {
        std::filesystem::path root;
        std::string output;
        SystemVerilogVpiObjectRegistry objects;
        Scheduler scheduler;
        SystemVerilogVpiTimeService time;
        SystemVerilogVpiCallbackManager callbacks;
        SystemVerilogVpiSystemRegistry systems;
        SystemVerilogVpiIoService io;
        fsim_vpi_handle_v1 top { };
        fsim_vpi_handle_v1 value { };
        fsim_vpi_handle_v1 wide_enum { };

        explicit CheckpointFixture(const std::uint64_t identity)
            : root(prepare_root(identity))
            , objects(identity)
            , time(scheduler, SystemVerilogVpiTimeProfile { -9, -12 })
            , callbacks(objects, scheduler, time, 70'000)
            , systems(objects)
            , io(make_io(identity, root, output))
        {
            const auto created_top = objects.create(SystemVerilogVpiObjectKind::Root, 0, "top");
            SystemVerilogVpiTypeInfo type;
            type.category = SystemVerilogVpiValueCategory::Logic4;
            type.width = 137;
            type.is_signed = true;
            const auto created_value = objects.create(SystemVerilogVpiObjectDescriptor {
                SystemVerilogVpiObjectKind::Variable,
                created_top.value,
                "value",
                std::nullopt,
                type,
            });
            SystemVerilogVpiTypeDescriptor enum_descriptor;
            enum_descriptor.kind = SystemVerilogVpiDescriptorKind::Enum;
            enum_descriptor.category
                = SystemVerilogVpiValueCategory::Logic4;
            enum_descriptor.width = 129;
            enum_descriptor.is_signed = true;
            enum_descriptor.enum_literals = {
                { "Known", PackedLogic4::from_msb_string("1" + std::string(128, '0')) },
                { "Unknown", PackedLogic4::from_msb_string("X" + std::string(127, '0') + "Z") },
            };
            SystemVerilogVpiTypeInfo enum_type;
            enum_type.category = enum_descriptor.category;
            enum_type.width = enum_descriptor.width;
            enum_type.is_signed = enum_descriptor.is_signed;
            enum_type.descriptor
                = std::make_shared<SystemVerilogVpiTypeDescriptor>(
                    std::move(enum_descriptor));
            const auto created_enum = objects.create(
                SystemVerilogVpiObjectDescriptor {
                    SystemVerilogVpiObjectKind::Variable,
                    created_top.value,
                    "wide_enum",
                    std::nullopt,
                    std::move(enum_type),
                });
            require_vpi_checkpoint(
                created_top && created_value && created_enum,
                "VPI checkpoint fixture hierarchy creation failed");
            top = created_top.value;
            value = created_value.value;
            wide_enum = created_enum.value;
            require_vpi_checkpoint(
                objects.bind_value(value, checkpoint_value(137, Logic4::zero))
                    == SystemVerilogVpiValueError::None,
                "VPI checkpoint fixture initial value bind failed");
            require_vpi_checkpoint(
                objects.bind_value(wide_enum,
                    checkpoint_value(
                        "X" + std::string(127, '0') + "Z"))
                    == SystemVerilogVpiValueError::None,
                "VPI checkpoint fixture exact enum value bind failed");
        }

        static std::filesystem::path prepare_root(
            const std::uint64_t identity)
        {
            auto result = std::filesystem::temp_directory_path()
                / ("fsim-vpi-checkpoint-" + std::to_string(identity));
            std::error_code error;
            std::filesystem::remove_all(result, error);
            error.clear();
            require_vpi_checkpoint(
                std::filesystem::create_directories(result, error) && !error,
                "VPI checkpoint fixture root creation failed");
            return result;
        }

        ~CheckpointFixture()
        {
            io.teardown();
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        static SystemVerilogVpiIoConfiguration make_io(
            const std::uint64_t identity,
            const std::filesystem::path& root,
            std::string& output)
        {
            SystemVerilogVpiIoConfiguration configuration;
            configuration.simulation_identity = identity;
            configuration.file_root = root;
            configuration.output = [&output](const std::string_view text) {
                output.append(text);
            };
            return configuration;
        }
    };

    const SystemVerilogVpiObjectState& value_state(
        const SystemVerilogVpiObjectStateSnapshot& snapshot)
    {
        for (const auto& state : snapshot.objects) {
            if (state.full_name == "top.value") {
                return state;
            }
        }
        throw std::runtime_error("VPI checkpoint value state is missing");
    }

    SystemVerilogVpiObjectState& value_state(
        SystemVerilogVpiObjectStateSnapshot& snapshot)
    {
        for (auto& state : snapshot.objects) {
            if (state.full_name == "top.value") {
                return state;
            }
        }
        throw std::runtime_error("VPI checkpoint value state is missing");
    }

    void require_value_state(
        const SystemVerilogVpiObjectRegistry& objects,
        const SystemVerilogVpiStoredValue& value,
        const SystemVerilogVpiStoredValue& forced,
        const char* message)
    {
        const auto snapshot = objects.snapshot_values();
        require_vpi_checkpoint(
            snapshot && value_state(snapshot).value == value
                && value_state(snapshot).forced_value == forced,
            message);
    }

} // namespace

void test_systemverilog_vpi_checkpoint_restart_and_artifact()
{
    CheckpointFixture source { 930 };
    const auto saved_value = checkpoint_value(
        "1" + std::string(63, '0') + "X"
        + std::string(63, '0') + "Z10101010");
    const auto saved_force = checkpoint_value(
        "Z" + std::string(62, '0') + "1"
        + std::string(63, '0') + "X010101010");
    require_vpi_checkpoint(
        source.objects.deposit_value(source.value, saved_value)
                == SystemVerilogVpiValueError::None
            && source.objects.force_value(source.value, saved_force)
                == SystemVerilogVpiValueError::None,
        "VPI checkpoint saved value and force setup failed");

    std::uint64_t callback_user_data { };
    std::uintptr_t system_user_data { };
    const auto callback = source.callbacks.register_callback({
        SystemVerilogVpiCallbackKind::StartOfRestart,
        std::nullopt,
        std::nullopt,
        0x157U,
        [&callback_user_data](const auto& event) {
            callback_user_data = event.user_data;
        },
    });
    SystemVerilogVpiSystemRegistration registration;
    registration.name = "$checkpoint_task";
    registration.user_data = 0x1'0157U;
    registration.compiletf = [](const auto&) {
        return SystemVerilogVpiSystemCallbackResult { };
    };
    registration.calltf = [&system_user_data](const auto& invocation) {
        system_user_data = invocation.registration_user_data;
        return SystemVerilogVpiSystemCallbackResult { };
    };
    const auto system = source.systems.register_callable(
        std::move(registration));
    const auto call = source.systems.execute("$checkpoint_task", source.top);
    const auto descriptor = source.io.open_file_descriptor("retained.log", "w");
    require_vpi_checkpoint(
        callback && system && call && descriptor,
        "VPI checkpoint external state setup failed");

    const auto compatibility = checkpoint_compatibility();
    const auto captured = capture_systemverilog_vpi_checkpoint(
        source.objects,
        source.callbacks,
        source.systems,
        source.io,
        compatibility);
    require_vpi_checkpoint(
        captured
            && captured.artifact.external_state.callbacks == 1
            && captured.artifact.external_state.system_registrations == 1
            && captured.artifact.external_state.system_calls == 1
            && captured.artifact.external_state.open_descriptors == 1,
        "VPI checkpoint did not capture external ownership summary");
    const auto& captured_value = value_state(captured.artifact.objects);
    require_vpi_checkpoint(
        captured_value.type.width == 137
            && captured_value.type.is_signed
            && captured_value.value == saved_value
            && captured_value.forced_value == saved_force,
        "VPI checkpoint did not retain signed wide type and X/Z value state");

    require_vpi_checkpoint(
        source.objects.release_forced_value(source.value)
                == SystemVerilogVpiValueError::None
            && source.objects.deposit_value(
                   source.value, checkpoint_value(137, Logic4::zero))
                == SystemVerilogVpiValueError::None,
        "VPI checkpoint restart mutation setup failed");
    const auto restarted = restore_systemverilog_vpi_checkpoint(
        captured.artifact,
        SystemVerilogVpiCheckpointFlow::InProcessRestart,
        source.objects,
        source.callbacks,
        source.systems,
        source.io,
        compatibility);
    require_vpi_checkpoint(
        restarted && restarted.invalidations.empty()
            && !restarted.handles.empty()
            && source.callbacks.status(callback.value)
            && source.systems.registrations() == 1
            && source.systems.calls() == 1,
        "VPI in-process restart did not preserve live external owners");
    for (const auto& handle : restarted.handles) {
        require_vpi_checkpoint(
            handle.source == handle.target,
            "VPI in-process restart changed an object handle");
    }
    require_value_state(
        source.objects,
        saved_value,
        saved_force,
        "VPI in-process restart did not restore forced value state");
    require_vpi_checkpoint(
        source.callbacks.dispatch_lifecycle(
            SystemVerilogVpiCallbackKind::StartOfRestart)
            == SystemVerilogVpiCallbackError::None,
        "VPI in-process restart callback dispatch failed");
    (void)source.scheduler.run();
    require_vpi_checkpoint(
        callback_user_data == 0x157U,
        "VPI in-process restart did not preserve callback user data");
    require_vpi_checkpoint(
        source.systems.execute("$checkpoint_task", source.top)
                    .error
                == SystemVerilogVpiSystemError::None
            && system_user_data == 0x1'0157U
            && source.io.write(descriptor.value, "restart\n")
                == SystemVerilogVpiIoError::None,
        "VPI in-process restart did not preserve system user data or descriptor ownership");

    CheckpointFixture target { 931 };
    const auto before = target.objects.snapshot_values();
    auto mismatch = captured.artifact;
    mismatch.schema += 1;
    require_vpi_checkpoint(
        restore_systemverilog_vpi_checkpoint(
            mismatch,
            SystemVerilogVpiCheckpointFlow::PortableArtifact,
            target.objects,
            target.callbacks,
            target.systems,
            target.io,
            compatibility)
                .error
            == SystemVerilogVpiCheckpointError::SchemaMismatch,
        "VPI artifact schema mismatch was accepted");
    mismatch = captured.artifact;
    mismatch.host_abi += 1;
    require_vpi_checkpoint(
        restore_systemverilog_vpi_checkpoint(
            mismatch,
            SystemVerilogVpiCheckpointFlow::PortableArtifact,
            target.objects,
            target.callbacks,
            target.systems,
            target.io,
            compatibility)
                .error
            == SystemVerilogVpiCheckpointError::HostAbiMismatch,
        "VPI artifact host ABI mismatch was accepted");
    mismatch = captured.artifact;
    mismatch.plugin_abi += 1;
    require_vpi_checkpoint(
        restore_systemverilog_vpi_checkpoint(
            mismatch,
            SystemVerilogVpiCheckpointFlow::PortableArtifact,
            target.objects,
            target.callbacks,
            target.systems,
            target.io,
            compatibility)
                .error
            == SystemVerilogVpiCheckpointError::PluginAbiMismatch,
        "VPI artifact plug-in ABI mismatch was accepted");
    auto expected = compatibility;
    expected.content_fingerprint = "different-content";
    require_vpi_checkpoint(
        restore_systemverilog_vpi_checkpoint(
            captured.artifact,
            SystemVerilogVpiCheckpointFlow::PortableArtifact,
            target.objects,
            target.callbacks,
            target.systems,
            target.io,
            expected)
                .error
            == SystemVerilogVpiCheckpointError::ContentMismatch,
        "VPI artifact content mismatch was accepted");
    expected = compatibility;
    expected.cache_fingerprint = "different-cache";
    require_vpi_checkpoint(
        restore_systemverilog_vpi_checkpoint(
            captured.artifact,
            SystemVerilogVpiCheckpointFlow::PortableArtifact,
            target.objects,
            target.callbacks,
            target.systems,
            target.io,
            expected)
                .error
            == SystemVerilogVpiCheckpointError::CacheMismatch,
        "VPI artifact cache mismatch was accepted");
    expected = compatibility;
    expected.plugins.front().content_fingerprint = "different-plugin";
    require_vpi_checkpoint(
        restore_systemverilog_vpi_checkpoint(
            captured.artifact,
            SystemVerilogVpiCheckpointFlow::PortableArtifact,
            target.objects,
            target.callbacks,
            target.systems,
            target.io,
            expected)
                .error
            == SystemVerilogVpiCheckpointError::PluginMismatch,
        "VPI artifact plug-in provenance mismatch was accepted");
    mismatch = captured.artifact;
    value_state(mismatch.objects).full_name = "top.missing";
    require_vpi_checkpoint(
        restore_systemverilog_vpi_checkpoint(
            mismatch,
            SystemVerilogVpiCheckpointFlow::PortableArtifact,
            target.objects,
            target.callbacks,
            target.systems,
            target.io,
            compatibility)
                .error
            == SystemVerilogVpiCheckpointError::ObjectStateMismatch,
        "VPI artifact missing-object mismatch was accepted");
    mismatch = captured.artifact;
    mismatch.objects.objects.pop_back();
    require_vpi_checkpoint(
        restore_systemverilog_vpi_checkpoint(
            mismatch,
            SystemVerilogVpiCheckpointFlow::PortableArtifact,
            target.objects,
            target.callbacks,
            target.systems,
            target.io,
            compatibility)
                .error
            == SystemVerilogVpiCheckpointError::ObjectStateMismatch,
        "VPI artifact omitted-object mismatch was accepted");
    require_vpi_checkpoint(
        value_state(target.objects.snapshot_values()).value
            == value_state(before).value,
        "VPI rejected artifact changed target state");

    const auto restored = restore_systemverilog_vpi_checkpoint(
        captured.artifact,
        SystemVerilogVpiCheckpointFlow::PortableArtifact,
        target.objects,
        target.callbacks,
        target.systems,
        target.io,
        compatibility);
    require_vpi_checkpoint(
        restored && restored.handles.size() == captured.artifact.objects.objects.size()
            && restored.invalidations.size() == 7,
        "VPI portable artifact restore did not remap or invalidate state");
    require_value_state(
        target.objects,
        saved_value,
        saved_force,
        "VPI portable artifact restore lost forced value state");
    const auto restored_enum = target.objects.type_info(target.wide_enum);
    require_vpi_checkpoint(
        systemverilog_vpi_checkpoint_schema == 2 && restored_enum
            && restored_enum.value->descriptor
            && restored_enum.value->descriptor->enum_literals.size() == 2
            && restored_enum.value->descriptor->enum_literals[1]
                    .value.to_msb_string()
                == "X" + std::string(127, '0') + "Z",
        "VPI portable artifact restore lost exact wide enum identity");
    const auto remapped = std::find_if(
        restored.handles.begin(),
        restored.handles.end(),
        [&](const auto& handle) {
            return handle.source == source.value;
        });
    require_vpi_checkpoint(
        remapped != restored.handles.end()
            && remapped->target == target.value
            && remapped->source != remapped->target,
        "VPI portable artifact restore did not remap object handles");
}

} // namespace fsim::tests::runtime
