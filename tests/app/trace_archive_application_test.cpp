// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/trace_archive.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

[[nodiscard]] std::filesystem::path producer_root()
{
    return (std::filesystem::temp_directory_path()
        / "fsim-trace-archive-origin").lexically_normal();
}

[[nodiscard]] std::filesystem::path consumer_root()
{
    return (std::filesystem::temp_directory_path()
        / "fsim-trace-archive-replay").lexically_normal();
}

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

fsim::app::TraceArchiveSnapshot snapshot()
{
    using namespace fsim;
    app::TraceControlRequest request;
    request.surface = app::TraceControlSurface::NonProjectCompile;
    request.phase = app::TraceControlPhase::Compile;
    request.output = producer_root() / "traces" / "waves.fst";
    request.format = project::TraceFormat::fst;
    request.compression = project::TraceCompression::none;
    request.selection = { "top.clock", "top.payload" };
    request.lifecycle = app::TraceLifecycle::Configured;
    request.generation = 7U;
    const auto applied = app::apply_trace_control(std::move(request));
    require(applied.ok(), "trace control fixture must be valid");
    return app::make_trace_archive_snapshot(*applied.application, producer_root());
}

void test_round_trip_all_boundaries()
{
    using namespace fsim;
    constexpr std::array kinds { app::TraceArchiveKind::Object,
        app::TraceArchiveKind::Design, app::TraceArchiveKind::Library,
        app::TraceArchiveKind::NativeCache,
        app::TraceArchiveKind::Checkpoint };
    const auto expected = snapshot();
    for (const auto kind : kinds) {
        const auto encoded = app::encode_trace_archive(expected, kind);
        require(encoded.ok(), "trace archive encoding must succeed");
        const auto decoded = app::decode_trace_archive(encoded.archive, kind,
            project::TraceFormat::fst, expected.profile_identity);
        require(decoded.ok() && decoded.snapshot == expected,
            "artifact, cache and checkpoint round trips must be exact");
        require(decoded.archive_identity == encoded.archive_identity,
            "cold archive identity must remain deterministic");
        const auto warm = app::decode_trace_archive(encoded.archive, kind,
            project::TraceFormat::fst, expected.profile_identity);
        require(warm.ok() && warm.archive_identity == decoded.archive_identity,
            "warm replay admission must match cold replay");
    }
}

void test_relocation_and_path_hiding()
{
    using namespace fsim;
    const auto archived = snapshot();
    require(archived.output_intent
            == std::filesystem::path { "traces" } / "waves.fst",
        "producer root must be removed from durable output intent");
    require(archived.semantic_identity.find(producer_root().generic_string())
            == std::string::npos,
        "semantic identity must not expose a producer path");
    const auto restored = app::restore_trace_archive_control(archived, consumer_root());
    require(restored.ok(), "relocated trace control must restore");
    require(restored.application->request().output
            == consumer_root() / "traces" / "waves.fst",
        "output intent must resolve beneath the consumer root");

    auto leaking = archived;
    leaking.output_intent = producer_root() / "secret" / "waves.fst";
    require(!app::encode_trace_archive(
                leaking, app::TraceArchiveKind::Design)
                .ok(),
        "absolute producer paths must not enter an archive");
    leaking.output_intent = "../secret/waves.fst";
    require(!app::encode_trace_archive(
                leaking, app::TraceArchiveKind::Design)
                .ok(),
        "parent traversal must not enter an archive");
    require(!app::restore_trace_archive_control(archived, consumer_root(),
                { .max_selection_count = 1U })
                .ok(),
        "restore must enforce consumer resource limits");
}

void test_profile_compatibility()
{
    using namespace fsim;
    const auto compiled = snapshot();
    app::TraceControlRequest request;
    request.surface = app::TraceControlSurface::ProjectCli;
    request.phase = app::TraceControlPhase::Simulate;
    request.output = producer_root() / "traces" / "waves.fst";
    request.format = project::TraceFormat::fst;
    request.compression = project::TraceCompression::none;
    request.selection = { "top.clock", "top.payload" };
    request.lifecycle = app::TraceLifecycle::Configured;
    request.generation = 7U;
    const auto applied = app::apply_trace_control(std::move(request));
    require(applied.ok(), "replay trace control fixture must be valid");
    const auto replay = app::make_trace_archive_snapshot(
        *applied.application, producer_root());
    require(compiled != replay
            && app::trace_archive_profiles_compatible(compiled, replay),
        "phase and surface changes must preserve a compatible profile");
    auto conflicting = replay;
    conflicting.output_intent = "other.fst";
    require(!app::trace_archive_profiles_compatible(compiled, conflicting),
        "changed output intent must conflict");
}

void test_transactional_rejection()
{
    using namespace fsim;
    const auto expected = snapshot();
    const auto encoded = app::encode_trace_archive(
        expected, app::TraceArchiveKind::Design);
    require(encoded.ok(), "negative fixture must encode");
    require(!app::decode_trace_archive(encoded.archive,
                app::TraceArchiveKind::Object)
                .ok(),
        "cross-kind replay must reject");
    require(!app::decode_trace_archive(encoded.archive,
                app::TraceArchiveKind::Design,
                project::TraceFormat::vcd)
                .ok(),
        "cross-format replay must reject");
    require(!app::decode_trace_archive(encoded.archive,
                app::TraceArchiveKind::Design,
                project::TraceFormat::fst, "trace-profile-v0")
                .ok(),
        "stale profile replay must reject");

    auto future = encoded.archive;
    future[8] = std::byte { 2U };
    require(!app::decode_trace_archive(future,
                app::TraceArchiveKind::Design)
                .ok(),
        "future envelope schema must reject");
    auto corrupt = encoded.archive;
    corrupt.back() ^= std::byte { 1U };
    require(!app::decode_trace_archive(corrupt,
                app::TraceArchiveKind::Design)
                .ok(),
        "corrupt payload must reject");
    corrupt.push_back(std::byte { 0U });
    require(!app::decode_trace_archive(corrupt,
                app::TraceArchiveKind::Design)
                .ok(),
        "trailing payload must reject");

    auto stale = expected;
    stale.profile_schema = 2U;
    require(!app::encode_trace_archive(
                stale, app::TraceArchiveKind::Checkpoint)
                .ok(),
        "future profile schema must reject");
}

void test_text_transport()
{
    using namespace fsim;
    const auto encoded = app::encode_trace_archive(
        snapshot(), app::TraceArchiveKind::Library);
    const auto text = app::trace_archive_hex(encoded.archive);
    require(!text.empty() && app::trace_archive_from_hex(text) == encoded.archive,
        "mapped-library text transport must be exact");
    require(app::trace_archive_from_hex("abc").empty()
            && app::trace_archive_from_hex("xz").empty(),
        "malformed text transport must reject without partial output");
}

} // namespace

int main()
{
    test_round_trip_all_boundaries();
    test_relocation_and_path_hiding();
    test_profile_compatibility();
    test_transactional_rejection();
    test_text_transport();
    return 0;
}
