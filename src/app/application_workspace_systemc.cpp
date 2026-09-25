// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_systemc.hpp"

#include "application_workspace_store.hpp"

#include "fsim/support/path.hpp"
#include "fsim/systemc/incremental.hpp"

#include <algorithm>
#include <ostream>
#include <set>
#include <string_view>
#include <utility>

namespace fsim::app::application_detail {

namespace {

    struct SourceInput {
        std::filesystem::path path;
        std::string identity;
    };

    bool report_store_failure(
        diagnostic::Engine& diagnostics, std::string message)
    {
        diagnostics.error("FSIM-WS-SC002", std::move(message));
        return false;
    }

    std::optional<std::vector<SourceInput>> source_inputs(
        const cli::Invocation& invocation, const project::Config& config,
        diagnostic::Engine& diagnostics)
    {
        if (invocation.files.empty()) {
            diagnostics.error("FSIM-WS-SC001",
                "SystemC compilation requires at least one source file");
            return std::nullopt;
        }
        std::vector<SourceInput> sources;
        std::set<std::string> identities;
        for (const auto& source : invocation.files) {
            const auto path = source.is_absolute()
                ? source : config.base_directory / source;
            std::string error;
            auto identity = workspace::source_identity(path, error);
            if (!identity) {
                report_store_failure(diagnostics, std::move(error));
                return std::nullopt;
            }
            if (!identities.insert(*identity).second) {
                diagnostics.error("FSIM-WS-SC001",
                    "SystemC source was supplied more than once: "
                        + support::path_to_utf8(source));
                return std::nullopt;
            }
            sources.push_back({ path, std::move(*identity) });
        }
        return sources;
    }

    void describe_compile(
        const cli::Invocation& invocation, const SourceInput& source,
        const systemc::IncrementalCompileRequest& request,
        std::ostream& output)
    {
        if (invocation.verbosity == cli::Verbosity::quiet) {
            return;
        }
        output << "compiling SystemC source "
               << support::path_to_utf8(source.path)
               << " into library '" << invocation.library << "'\n";
        if (invocation.verbosity != cli::Verbosity::verbose) {
            return;
        }
        output << "  compiler: "
               << (request.settings.compiler.empty()
                       ? "default" : request.settings.compiler)
               << "\n  object: " << support::path_to_utf8(request.output)
               << '\n';
        for (const auto& include : request.settings.include_directories) {
            output << "  include: " << support::path_to_utf8(include) << '\n';
        }
        for (const auto& define : request.settings.defines) {
            output << "  define: " << define << '\n';
        }
        for (const auto& option : request.settings.compile_options) {
            output << "  compile option: " << option << '\n';
        }
    }

    bool compile_source(
        const cli::Invocation& invocation, const project::Config& config,
        const SourceInput& source, const workspace::Store& store,
        workspace::LibraryTransaction& transaction,
        std::vector<workspace::ArtifactRecord>& records,
        diagnostic::Engine& diagnostics, std::ostream& output)
    {
        std::string error;
        auto record = transaction.allocate_artifact(
            workspace::ArtifactKind::SystemCObject, error);
        if (!record) {
            return report_store_failure(diagnostics, std::move(error));
        }
        systemc::IncrementalCompileRequest request;
        request.source = source.path;
        request.output = transaction.artifact_path(*record);
        request.settings = config.systemc;
        request.working_directory = config.base_directory;
        request.scratch_directory = store.managed_directory() / "scratch" / "systemc";
        describe_compile(invocation, source, request, output);
        if (!systemc::compile_incremental_object(request, diagnostics)) {
            return false;
        }
        const auto metadata = systemc::load_incremental_object_metadata(
            request.output, diagnostics);
        if (!metadata) {
            return false;
        }
        record->sources.push_back(source.identity);
        record->fingerprint = metadata->compilation_digest;
        records.push_back(std::move(*record));
        return true;
    }

    bool gather_objects(
        const workspace::Store& store,
        const workspace::LibraryCatalog& catalog,
        systemc::IncrementalLinkRequest& request,
        std::vector<std::string>& sources, diagnostic::Engine& diagnostics)
    {
        std::vector<const workspace::ArtifactRecord*> objects;
        for (const auto& artifact : catalog.artifacts) {
            if (artifact.kind == workspace::ArtifactKind::SystemCObject) {
                objects.push_back(&artifact);
            }
        }
        if (objects.empty()) {
            diagnostics.error("FSIM-WS-SC003",
                "library '" + catalog.location.name
                    + "' has no compiled SystemC sources");
            return false;
        }
        std::ranges::sort(objects, [](const auto* left, const auto* right) {
            return left->sources < right->sources;
        });
        for (const auto* record : objects) {
            std::string error;
            auto path = store.artifact_path(catalog.location, *record, error);
            if (!path) {
                return report_store_failure(diagnostics, std::move(error));
            }
            const auto metadata = systemc::load_incremental_object_metadata(
                *path, diagnostics);
            if (!metadata) {
                return false;
            }
            if (record->sources.size() != 1
                || record->fingerprint != metadata->compilation_digest) {
                diagnostics.error("FSIM-WS-SC003",
                    "SystemC object disagrees with the managed library index: "
                        + support::path_to_utf8(*path));
                return false;
            }
            request.objects.push_back(std::move(*path));
            sources.push_back(record->sources.front());
        }
        return true;
    }

    void describe_link(
        const cli::Invocation& invocation,
        const systemc::IncrementalLinkRequest& request,
        const std::vector<std::string>& sources, std::ostream& output)
    {
        if (invocation.verbosity == cli::Verbosity::quiet) {
            return;
        }
        output << "linking " << request.objects.size()
               << " SystemC translation unit(s) in library '"
               << invocation.library << "'\n";
        if (invocation.verbosity != cli::Verbosity::verbose) {
            return;
        }
        output << "  plugin: " << support::path_to_utf8(request.output) << '\n';
        for (const auto& source : sources) {
            output << "  source: " << source << '\n';
        }
        for (const auto& option : request.settings.link_options) {
            output << "  link option: " << option << '\n';
        }
        for (const auto& library : request.settings.libraries) {
            output << "  native library: " << library << '\n';
        }
    }

    void index_factories(
        workspace::ArtifactRecord& record,
        const systemc::IncrementalPluginMetadata& metadata)
    {
        record.fingerprint = metadata.link_digest;
        for (const auto& factory : metadata.factories) {
            library::UnitIndexEntry unit;
            unit.language = "systemc";
            unit.kind = "module";
            unit.name = factory.name;
            unit.artifact = metadata.library;
            unit.checksum = metadata.library_checksum;
            record.units.push_back({ std::move(unit), { } });
        }
    }

} // namespace

int handle_workspace_systemc_compile(
    const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream&)
{
    const auto sources = source_inputs(invocation, config, diagnostics);
    if (!sources) {
        return 1;
    }
    workspace::Store store(config.base_directory);
    std::string error;
    auto transaction = store.begin_library(invocation.library, error);
    if (!transaction) {
        report_store_failure(diagnostics, std::move(error));
        return 1;
    }
    std::vector<workspace::ArtifactRecord> records;
    for (const auto& source : *sources) {
        if (!compile_source(invocation, config, source, store, *transaction,
                records, diagnostics, output)) {
            return 1;
        }
    }
    if (!transaction->commit(std::move(records), error)) {
        report_store_failure(diagnostics, std::move(error));
        return 1;
    }
    if (invocation.verbosity != cli::Verbosity::quiet) {
        output << "compiled " << sources->size()
               << " SystemC translation unit(s) into library '"
               << invocation.library << "'; finalize with fsim systemc link --library "
               << invocation.library << '\n';
    }
    return 0;
}

int handle_workspace_systemc_link(
    const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream&)
{
    workspace::Store store(config.base_directory);
    std::string error;
    auto transaction = store.begin_library(invocation.library, error);
    if (!transaction) {
        report_store_failure(diagnostics, std::move(error));
        return 1;
    }
    systemc::IncrementalLinkRequest request;
    request.logical_library = invocation.library;
    request.settings = config.systemc;
    request.working_directory = config.base_directory;
    request.scratch_directory = store.managed_directory() / "scratch" / "systemc";
    std::vector<std::string> sources;
    if (!gather_objects(store, transaction->catalog(), request, sources, diagnostics)) {
        return 1;
    }
    auto record = transaction->allocate_artifact(
        workspace::ArtifactKind::SystemCPlugin, error);
    if (!record) {
        report_store_failure(diagnostics, std::move(error));
        return 1;
    }
    request.output = transaction->artifact_path(*record);
    describe_link(invocation, request, sources, output);
    if (!systemc::link_incremental_plugin(request, diagnostics)) {
        return 1;
    }
    const auto metadata = systemc::load_incremental_plugin_metadata(
        request.output, diagnostics);
    if (!metadata) {
        return 1;
    }
    record->sources = std::move(sources);
    index_factories(*record, *metadata);
    if (!transaction->commit(std::move(*record), error)) {
        report_store_failure(diagnostics, std::move(error));
        return 1;
    }
    if (invocation.verbosity != cli::Verbosity::quiet) {
        output << "registered " << metadata->factories.size()
               << " SystemC module(s) in library '" << invocation.library << "'\n";
        if (invocation.verbosity == cli::Verbosity::verbose) {
            for (const auto& factory : metadata->factories) {
                output << "  systemc:" << invocation.library << '.'
                       << factory.name << '\n';
            }
        }
    }
    return 0;
}

} // namespace fsim::app::application_detail
