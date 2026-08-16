// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_trace_control.hpp"
#include "application_trace_hierarchy.hpp"
#include "application_trace_observation.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/support/path.hpp"

namespace fsim::app::application_detail {

HdlVcdState::~HdlVcdState()
{
    if (remove_observer && observer != 0) {
        remove_observer(observer);
    }
    if (current_time) {
        for (auto& file : extended_files) {
            if (!file->begun || file->closed) {
                continue;
            }
            const auto now = current_time();
            if (now > std::numeric_limits<SimulationTick>::max()
                    / file->tick_multiplier) {
                continue;
            }
            file->stream << "$vcdclose #"
                         << now * file->tick_multiplier << " $end\n";
            file->stream.flush();
            file->closed = true;
        }
    }
}

namespace {

    [[nodiscard]] std::uint64_t stable_trace_hash(
        const std::string_view text) noexcept
    {
        std::uint64_t result { UINT64_C(14695981039346656037) };
        for (const auto byte : text) {
            result ^= static_cast<unsigned char>(byte);
            result *= UINT64_C(1099511628211);
        }
        return result;
    }

    [[nodiscard]] runtime::PackedLogic4 trace_bits(
        const std::size_t width,
        const std::uint64_t value)
    {
        return runtime::PackedLogic4::from_aval_bval(width, value, 0U);
    }

    void write_trace_observation(
        TraceState& state,
        const TraceObservationRecord& record);

    void write_uvm_activity_trace_event(
        TraceState& state,
        const runtime::SystemVerilogUvmActivityEvent& event)
    {
        const std::array values {
            trace_bits(64, event.sequence),
            trace_bits(4, static_cast<std::uint8_t>(event.kind)),
            trace_bits(4, static_cast<std::uint8_t>(event.action)),
            trace_bits(64, event.root),
            trace_bits(64, event.value),
            trace_bits(64, stable_trace_hash(event.identity)),
            trace_bits(64, stable_trace_hash(event.detail))
        };
        std::vector<TraceObservationValue> observed;
        observed.reserve(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            observed.push_back({ state.uvm_activity_trace_ids[index], values[index], std::nullopt });
        }
        static_cast<void>(state.observations->accept(
            TraceObservationKind::Uvm, event.time, event.delta,
            runtime::TraceRegion::Callback,
            "uvm:" + event.identity, observed));
    }

} // namespace

std::string make_cache_key(
    const project::Config& config,
    const CheckedProject& checked,
    const std::span<const project::ProjectSection::TopLevel> tops,
    const std::string_view resolution,
    const std::string_view systemc_plugin_key,
    diagnostic::Engine& diagnostics)
{
    compiler::CacheKeyBuilder key;
    key.add("fsim-version", version);
    key.add("runtime-abi", std::to_string(runtime_abi_version));
    key.add("target", target_name());
    key.add("root-count", std::to_string(tops.size()));
    for (const auto& top : tops) {
        key.add("root-alias", top.alias);
        key.add("root-target", top.target);
    }
    key.add("time-resolution", resolution);
    key.add("delay-mode", project::to_string(config.run.delay_mode));
    key.add("optimization", project::to_string(config.build.optimization));
    key.add("llvm", production_llvm_version);
    key.add("standard-library", standard_library_cache_version);
    key.add("vhdl-compatibility-profile", vhdl_compatibility_profile());
    key.add(
        "verilog-preprocessor",
        frontend::verilog_preprocessor_cache_version);
    key.add("systemc-plugin", systemc_plugin_key);
    key.add(
        "uvm-release",
        project::to_string(checked.systemverilog_uvm_provenance.release));
    key.add(
        "uvm-source-identity",
        checked.systemverilog_uvm_provenance.source_identity);
    if (checked.trace_archive) {
        key.add("trace-profile", checked.trace_archive->semantic_identity);
        key.add("trace-declarations",
            checked.trace_archive->declaration_identity);
    }
    std::vector<std::string> search_libraries;
    search_libraries.reserve(
        config.elaboration.search_libraries.size());
    for (const auto& library : config.elaboration.search_libraries) {
        if (std::ranges::find(search_libraries, library)
            == search_libraries.end()) {
            search_libraries.push_back(library);
            key.add("elaboration-search-library", library);
        }
    }
    std::size_t hdl_source_index = 0;
    for (const auto& set : config.source_sets) {
        key.add("language", project::to_string(set.language));
        key.add("standard", set.standard);
        key.add(
            "compatibility-profile",
            project::compatibility_profile(set.compatibility_switches));
        key.add("library", set.library);
        key.add("uvm-release", project::to_string(set.uvm_release));
        for (const auto& define : set.defines) {
            key.add("define", define);
        }
        for (const auto& include : set.include_directories) {
            key.add("include", fsim::support::path_to_utf8(include));
        }
        for (const auto& file : set.files) {
            key.add(
                "source-path",
                fsim::support::path_to_utf8(file.lexically_normal()));
            if (set.language == project::Language::systemc) {
                std::error_code error;
                if (!key.add_file("source-content", file, error)) {
                    diagnostics.error(
                        "FSIM-CACHE-0001",
                        "cannot hash source file '"
                            + fsim::support::path_to_utf8(file)
                            + "': " + error.message());
                    return { };
                }
                continue;
            }
            if (hdl_source_index >= checked.hdl_sources.size()
                || !same_source_path(
                    checked.hdl_sources[hdl_source_index].path, file)) {
                diagnostics.error(
                    "FSIM-CACHE-0001",
                    "parsed source identity is inconsistent for '"
                        + fsim::support::path_to_utf8(file) + "'");
                return { };
            }
            key.add(
                "source-content",
                checked.hdl_sources[hdl_source_index].content_digest);
            key.add(
                "source-compilation-unit",
                checked.hdl_sources[hdl_source_index]
                    .compilation_unit_digest);
            for (const auto& dependency :
                checked.hdl_sources[hdl_source_index].dependencies) {
                key.add(
                    "dependency-path",
                    fsim::support::path_to_utf8(
                        dependency.path.lexically_normal()));
                key.add(
                    "dependency-content",
                    dependency.content_digest);
            }
            ++hdl_source_index;
        }
    }
    for (const auto& mapped : checked.mapped_libraries) {
        key.add("mapped-library", mapped.library);
        key.add("mapped-metadata", mapped.metadata_digest);
        for (const auto& checksum : mapped.unit_checksums) {
            key.add("mapped-unit", checksum);
        }
        key.add("mapped-native-accepted", mapped.native_accepted ? "1" : "0");
        key.add("mapped-native-kind", mapped.native_kind);
        key.add("mapped-native-fingerprint", mapped.native_fingerprint);
    }
    for (const auto& object : checked.objects) {
        key.add("object-metadata", object.metadata_digest);
        key.add("object-compilation", object.compilation_digest);
        key.add("object-language", object.language);
        key.add("object-standard", object.standard);
        key.add("object-compatibility-profile", object.compatibility_profile);
        key.add("object-library", object.library);
        for (const auto& checksum : object.unit_checksums) {
            key.add("object-unit", checksum);
        }
    }
    for (; hdl_source_index < checked.hdl_sources.size(); ++hdl_source_index) {
        const auto& source = checked.hdl_sources[hdl_source_index];
        key.add(
            "mapped-source-path",
            fsim::support::path_to_utf8(source.path.lexically_normal()));
        key.add("mapped-source-content", source.content_digest);
        key.add("mapped-source-compilation-unit", source.compilation_unit_digest);
    }
    for (const auto& source : checked.standard_sources) {
        key.add(
            "standard-source-path",
            fsim::support::path_to_utf8(source.path.lexically_normal()));
        key.add("standard-source-content", source.content_digest);
        key.add(
            "standard-source-compilation-unit",
            source.compilation_unit_digest);
    }
    for (const auto& binding : config.bindings) {
        key.add("binding-instance", binding.instance);
        key.add("binding-target", binding.target.value_or("<inferred>"));
        key.add("binding-resolver", binding.resolver.value_or(""));
    }
    return key.finish();
}

std::optional<std::vector<std::string>>
make_specialization_cache_keys(
    const project::Config& config,
    const CheckedProject& checked,
    const semantic::design::DesignIr& design,
    const std::string_view systemc_plugin_key,
    diagnostic::Engine& diagnostics)
{
    struct SourceSettings {
        const project::SourceSet* source_set { };
        const CheckedSource* checked_source { };
    };
    const project::SourceSet standard_source_set = [] {
        project::SourceSet source_set;
        source_set.language = project::Language::vhdl;
        source_set.standard = "2008";
        source_set.library = "ieee";
        source_set.compilation_unit = "file";
        return source_set;
    }();
    const auto settings_for =
        [&](const semantic::design::Specialization& specialization,
            const std::string_view source,
            const bool require_specialization_library)
        -> std::optional<SourceSettings> {
        const auto source_path = fsim::support::path_from_utf8(source)
                                     .lexically_normal();
        const CheckedSource* checked_source = nullptr;
        for (const auto& candidate : checked.hdl_sources) {
            if (same_source_path(candidate.path, source_path)
                || std::any_of(
                    candidate.dependencies.begin(),
                    candidate.dependencies.end(),
                    [&](const CheckedSource::Dependency& dependency) {
                        return same_source_path(
                            dependency.path, source_path);
                    })) {
                checked_source = &candidate;
                break;
            }
        }
        if (checked_source != nullptr) {
            for (const auto& source_set : config.source_sets) {
                if (source_set.language == project::Language::systemc) {
                    continue;
                }
                const auto source_language = source_set.language == project::Language::vhdl
                    ? semantic::Language::vhdl
                    : source_set.language == project::Language::verilog
                    ? semantic::Language::verilog
                    : semantic::Language::system_verilog;
                if (source_language != specialization.language) {
                    continue;
                }
                if (require_specialization_library
                    && (source_set.library.empty()
                               ? "work"
                               : source_set.library)
                        != specialization.library) {
                    continue;
                }
                if (std::any_of(
                        source_set.files.begin(),
                        source_set.files.end(),
                        [&](const std::filesystem::path& candidate) {
                            return same_source_path(
                                candidate, checked_source->path);
                        })) {
                    return SourceSettings { &source_set, checked_source };
                }
            }
            for (const auto& mapped : checked.mapped_libraries) {
                for (const auto& source_set : mapped.source_settings) {
                    const auto source_language = source_set.language == project::Language::vhdl
                        ? semantic::Language::vhdl
                        : source_set.language == project::Language::verilog
                        ? semantic::Language::verilog
                        : semantic::Language::system_verilog;
                    if (source_language != specialization.language
                        || (require_specialization_library
                            && source_set.library != specialization.library)) {
                        continue;
                    }
                    if (std::ranges::any_of(
                            source_set.files,
                            [&](const auto& candidate) {
                                return same_source_path(
                                    candidate, checked_source->path);
                            })) {
                        return SourceSettings { &source_set, checked_source };
                    }
                }
            }
            for (const auto& object : checked.objects) {
                const auto& source_set = object.source_settings;
                const auto source_language = source_set.language == project::Language::vhdl
                    ? semantic::Language::vhdl
                    : source_set.language == project::Language::verilog
                    ? semantic::Language::verilog
                    : semantic::Language::system_verilog;
                if (source_language != specialization.language
                    || (require_specialization_library
                        && source_set.library != specialization.library)) {
                    continue;
                }
                if (std::ranges::any_of(
                        source_set.files,
                        [&](const auto& candidate) {
                            return same_source_path(
                                candidate, checked_source->path);
                        })) {
                    return SourceSettings { &source_set, checked_source };
                }
            }
        }
        if (specialization.language != semantic::Language::vhdl
            || (require_specialization_library
                && specialization.library != "ieee")) {
            return std::nullopt;
        }
        for (const auto& candidate : checked.standard_sources) {
            if (same_source_path(candidate.path, source_path)) {
                return SourceSettings { &standard_source_set, &candidate };
            }
        }
        return std::nullopt;
    };

#if defined(FSIM_HAS_LLVM)
    const auto llvm_native_host_fingerprint = compiler::LlvmJit::native_host_identity(
        config.build.optimization == project::Optimization::o0
            ? compiler::JitOptimizationLevel::o0
            : compiler::JitOptimizationLevel::o2)
                                                  .fingerprint;
#endif
    std::vector<std::string> result;
    result.reserve(design.specializations().size());
    for (const auto& specialization : design.specializations()) {
        if (specialization.language == semantic::Language::systemc) {
            continue;
        }
        const auto source_name = specialization.source
            ? checked.semantics.source_files()[checked.semantics.source_spans()[specialization.source->value()].file.value()]
                  .physical_name
            : std::string { };
        const auto settings = settings_for(
            specialization, source_name, true);
        if (!settings) {
            diagnostics.error(
                "FSIM-CACHE-0001",
                "cannot associate elaborated specialization '"
                    + specialization.name + "' with semantic source '"
                    + source_name + "'");
            return std::nullopt;
        }

        compiler::CacheKeyBuilder key;
        key.add(
            "specialization-provenance-schema",
            "fsim-specialization-provenance-v9-standard-compatibility");
        key.add("fsim-version", version);
        key.add("standard-library", standard_library_cache_version);
        key.add(
            "vhdl-compatibility-profile",
            vhdl_compatibility_profile());
        key.add("delay-mode", project::to_string(config.run.delay_mode));
        key.add(
            "verilog-preprocessor",
            frontend::verilog_preprocessor_cache_version);
        key.add("unit", specialization.name);
        key.add("selected-unit-identity", specialization.name);
        key.add("selected-logical-library", specialization.library);
        key.add(
            "uvm-release",
            project::to_string(checked.systemverilog_uvm_provenance.release));
        key.add(
            "uvm-source-identity",
            checked.systemverilog_uvm_provenance.source_identity);
#if defined(FSIM_HAS_LLVM)
        key.add("llvm-native-host", llvm_native_host_fingerprint);
#endif
        std::vector<std::string> specialization_search_libraries;
        specialization_search_libraries.reserve(
            config.elaboration.search_libraries.size());
        for (const auto& library :
            config.elaboration.search_libraries) {
            if (std::ranges::find(
                    specialization_search_libraries, library)
                == specialization_search_libraries.end()) {
                specialization_search_libraries.push_back(library);
                key.add("elaboration-search-library", library);
            }
        }
        key.add(
            "source-path",
            fsim::support::path_to_utf8(
                settings->checked_source->path.lexically_normal()));
        key.add(
            "source-content",
            settings->checked_source->content_digest);
        key.add(
            "source-compilation-unit",
            settings->checked_source->compilation_unit_digest);
        for (const auto& dependency :
            settings->checked_source->dependencies) {
            key.add(
                "dependency-path",
                fsim::support::path_to_utf8(
                    dependency.path.lexically_normal()));
            key.add(
                "dependency-content",
                dependency.content_digest);
        }
        for (const auto& dependency_source :
            specialization.source_dependencies) {
            const auto dependency_settings = settings_for(
                specialization, dependency_source, false);
            if (!dependency_settings) {
                diagnostics.error(
                    "FSIM-CACHE-0001",
                    "cannot associate elaborated specialization dependency '"
                        + dependency_source + "' for '"
                        + specialization.name + "' with a checked source");
                return std::nullopt;
            }
            key.add(
                "semantic-dependency-source-path",
                fsim::support::path_to_utf8(
                    dependency_settings->checked_source->path
                        .lexically_normal()));
            key.add(
                "semantic-dependency-source-content",
                dependency_settings->checked_source->content_digest);
            key.add(
                "semantic-dependency-source-compilation-unit",
                dependency_settings->checked_source
                    ->compilation_unit_digest);
            key.add(
                "semantic-dependency-language",
                project::to_string(
                    dependency_settings->source_set->language));
            key.add(
                "semantic-dependency-standard",
                dependency_settings->source_set->standard);
            key.add(
                "semantic-dependency-compatibility-profile",
                project::compatibility_profile(
                    dependency_settings->source_set
                        ->compatibility_switches));
            key.add(
                "semantic-dependency-library",
                dependency_settings->source_set->library);
            key.add(
                "semantic-dependency-compilation-unit",
                dependency_settings->source_set->compilation_unit);
            key.add(
                "semantic-dependency-uvm-release",
                project::to_string(
                    dependency_settings->source_set->uvm_release));
            for (const auto& define :
                dependency_settings->source_set->defines) {
                key.add("semantic-dependency-define", define);
            }
            for (const auto& include :
                dependency_settings->source_set
                    ->include_directories) {
                key.add(
                    "semantic-dependency-include",
                    fsim::support::path_to_utf8(include.lexically_normal()));
            }
            for (const auto& dependency :
                dependency_settings->checked_source->dependencies) {
                key.add(
                    "semantic-dependency-transitive-path",
                    fsim::support::path_to_utf8(
                        dependency.path.lexically_normal()));
                key.add(
                    "semantic-dependency-transitive-content",
                    dependency.content_digest);
            }
        }
        key.add(
            "language",
            project::to_string(settings->source_set->language));
        key.add("standard", settings->source_set->standard);
        key.add(
            "compatibility-profile",
            project::compatibility_profile(
                settings->source_set->compatibility_switches));
        key.add("library", settings->source_set->library);
        key.add(
            "compilation-unit",
            settings->source_set->compilation_unit);
        for (const auto& define : settings->source_set->defines) {
            key.add("define", define);
        }
        for (const auto& include :
            settings->source_set->include_directories) {
            key.add(
                "include",
                fsim::support::path_to_utf8(include.lexically_normal()));
        }
        for (const auto& parameter : specialization.parameters) {
            key.add("parameter-name", parameter.name);
            key.add("parameter-value", parameter.identity);
        }
        // Native HDL objects address the common runtime by dense signal/process
        // IDs. A SystemC image or hierarchy edit can therefore change the
        // meaning of otherwise identical HDL SimIR. Keep transient plug-in
        // pointers and native handles out of the key (they are rebound into each
        // fresh interpreter), but compose every stable runtime mapping into the
        // specialization provenance used by LLVM's persistent object cache.
        key.add("systemc-runtime-schema", "fsim-systemc-runtime-v2");
        key.add("systemc-plugin", systemc_plugin_key);
        key.add(
            "systemc-instance-count",
            std::to_string(std::ranges::count_if(
                design.specializations(), [](const auto& item) {
                    return item.language == semantic::Language::systemc;
                })));
        for (const auto& systemc : design.specializations()) {
            if (systemc.language != semantic::Language::systemc) {
                continue;
            }
            const auto& instance = design.instances()[systemc.instance.value()];
            key.add("systemc-instance-id", std::to_string(systemc.id.value()));
            key.add("systemc-factory-target", systemc.name);
            key.add("systemc-instance-path", instance.path);
            key.add(
                "systemc-construction-count",
                std::to_string(systemc.parameters.size()));
            for (const auto& parameter : systemc.parameters) {
                key.add("systemc-construction-name", parameter.name);
                key.add("systemc-construction-value", parameter.identity);
            }
        }
        key.add("systemc-boundary-count", std::to_string(std::ranges::count_if(design.boundaries(), [](const auto& boundary) {
            return boundary.kind
                != semantic::design::BoundaryKind::language_conversion;
        })));
        for (const auto& boundary : design.boundaries()) {
            if (boundary.kind
                == semantic::design::BoundaryKind::language_conversion) {
                continue;
            }
            key.add("systemc-boundary-kind", std::to_string(static_cast<unsigned>(boundary.kind)));
            key.add("systemc-boundary-id", std::to_string(boundary.id.value()));
            key.add("systemc-boundary-path", boundary.path);
            key.add("systemc-boundary-object", boundary.object ? std::to_string(boundary.object->value()) : "none");
            key.add("systemc-boundary-process", boundary.process ? std::to_string(design.processes()[boundary.process->value()].runtime_index) : "none");
            key.add("systemc-boundary-port-writable",
                boundary.port && design.ports()[boundary.port->value()].writable
                    ? "true"
                    : "false");
        }
        key.add("systemc-object-count", std::to_string(std::ranges::count_if(design.objects(), [](const auto& object) {
            return object.kind >= semantic::design::ObjectKind::systemc_module;
        })));
        for (const auto& object : design.objects()) {
            if (object.kind < semantic::design::ObjectKind::systemc_module) {
                continue;
            }
            key.add("systemc-object-kind", std::to_string(static_cast<unsigned>(object.kind)));
            key.add("systemc-object-id", std::to_string(object.id.value()));
            key.add("systemc-object-path", object.path);
            key.add("systemc-object-type", object.external_type);
            key.add("systemc-object-runtime", std::to_string(object.runtime_index));
        }
        result.push_back(key.finish());
    }
    return result;
}

bool wildcard_match(std::string_view pattern, std::string_view text)
{
    std::size_t pattern_index = 0;
    std::size_t text_index = 0;
    std::size_t star = std::string_view::npos;
    std::size_t retry = 0;
    while (text_index < text.size()) {
        if (pattern_index < pattern.size()
            && (pattern[pattern_index] == '?'
                || pattern[pattern_index] == text[text_index])) {
            ++pattern_index;
            ++text_index;
        } else if (
            pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star = pattern_index++;
            retry = text_index;
        } else if (star != std::string_view::npos) {
            pattern_index = star + 1;
            text_index = ++retry;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

bool trace_selected(
    const std::vector<std::string>& filters,
    const std::string_view name)
{
    return filters.empty()
        || std::any_of(filters.begin(), filters.end(), [&](const auto& filter) {
               return wildcard_match(filter, name);
           });
}

std::optional<VcdScale> vcd_scale(
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)
{
    if (resolution == "auto") {
        return VcdScale { "1ns", 1 };
    }
    std::string compact;
    for (const char character : resolution) {
        if (std::isspace(static_cast<unsigned char>(character)) == 0) {
            compact.push_back(character);
        }
    }
    const auto unit_begin = std::find_if(
        compact.begin(), compact.end(), [](const char character) {
            return character < '0' || character > '9';
        });
    std::uint64_t magnitude = 0;
    const auto* magnitude_end = compact.data() + std::distance(compact.begin(), unit_begin);
    const auto [end, conversion_error] = std::from_chars(compact.data(), magnitude_end, magnitude);
    const std::string_view unit { unit_begin, compact.end() };
    const bool valid_unit = unit == "fs" || unit == "ps" || unit == "ns"
        || unit == "us" || unit == "ms" || unit == "s";
    if (conversion_error != std::errc { } || end != magnitude_end
        || magnitude == 0 || !valid_unit) {
        diagnostics.error(
            "FSIM-TIME-0001",
            "cannot derive a VCD timescale from '" + std::string(resolution) + "'");
        return std::nullopt;
    }
    const std::uint64_t vcd_magnitude = magnitude % 100 == 0 ? 100 : magnitude % 10 == 0 ? 10
                                                                                         : 1;
    return VcdScale {
        std::to_string(vcd_magnitude) + std::string(unit),
        magnitude / vcd_magnitude
    };
}

struct FstTraceType {
    runtime::TraceTypeKind kind { runtime::TraceTypeKind::Packed };
    runtime::SystemVerilogScalarKind scalar_kind {
        runtime::SystemVerilogScalarKind::None
    };
    std::string canonical_metadata;
};

[[nodiscard]] FstTraceType fst_trace_type(
    const elaboration::SignalInfo& signal,
    const semantic::Language language)
{
    using runtime::SystemVerilogScalarKind;
    if (signal.systemverilog_scalar != SystemVerilogScalarKind::None) {
        FstTraceType result;
        result.kind = runtime::TraceTypeKind::SystemVerilogScalar;
        result.scalar_kind = signal.systemverilog_scalar;
        if (signal.systemverilog_scalar == SystemVerilogScalarKind::ShortReal
            || signal.systemverilog_scalar == SystemVerilogScalarKind::Real
            || signal.systemverilog_scalar
                == SystemVerilogScalarKind::Realtime) {
            result.canonical_metadata = runtime::canonical_fst_systemverilog_real_type(
                signal.systemverilog_scalar);
        }
        return result;
    }

    runtime::FstExtendedTypeMetadata metadata;
    metadata.width = signal.width;
    metadata.nominal_name = signal.nominal_type.empty()
        ? signal.type_name
        : signal.nominal_type;
    if (language == semantic::Language::vhdl
        && signal.source_domain == frontend::ValueDomain::Logic9) {
        metadata.kind = runtime::FstExtendedTypeKind::VhdlLogic9;
        metadata.language = runtime::FstTypeLanguage::Vhdl;
        metadata.enumeration_literals = signal.enumeration_literals;
        if (metadata.enumeration_literals.empty()) {
            metadata.enumeration_literals = {
                "'U'", "'X'", "'0'", "'1'", "'Z'",
                "'W'", "'L'", "'H'", "'-'"
            };
        }
        return {
            runtime::TraceTypeKind::VhdlLogic9,
            SystemVerilogScalarKind::None,
            runtime::canonical_fst_type_metadata(metadata)
        };
    }
    if (!signal.enumeration_literals.empty()
        && signal.source_domain != frontend::ValueDomain::Logic9) {
        metadata.kind = runtime::FstExtendedTypeKind::Enumeration;
        metadata.language = language == semantic::Language::vhdl
            ? runtime::FstTypeLanguage::Vhdl
            : runtime::FstTypeLanguage::SystemVerilog;
        metadata.four_state = language != semantic::Language::vhdl
            && signal.source_domain == frontend::ValueDomain::Logic4;
        metadata.enumeration_literals = signal.enumeration_literals;
        return {
            runtime::TraceTypeKind::Enumeration,
            SystemVerilogScalarKind::None,
            runtime::canonical_fst_type_metadata(metadata)
        };
    }
    if (language == semantic::Language::vhdl && signal.vhdl_physical) {
        metadata.kind = runtime::FstExtendedTypeKind::VhdlPhysical;
        metadata.language = runtime::FstTypeLanguage::Vhdl;
        for (const auto& unit : signal.vhdl_physical->units) {
            if (!unit.scale_factor) {
                throw std::invalid_argument(
                    "FST VHDL physical type has an unresolved unit scale");
            }
            metadata.physical_units.push_back({ unit.name, *unit.scale_factor });
        }
        return {
            runtime::TraceTypeKind::VhdlPhysical,
            SystemVerilogScalarKind::None,
            runtime::canonical_fst_type_metadata(metadata)
        };
    }
    if (language == semantic::Language::vhdl
        && (signal.nominal_type == "@builtin:time"
            || (signal.nominal_type.empty() && signal.type_name == "time"))) {
        metadata.kind = runtime::FstExtendedTypeKind::VhdlTime;
        metadata.language = runtime::FstTypeLanguage::Vhdl;
        return {
            runtime::TraceTypeKind::VhdlTime,
            SystemVerilogScalarKind::None,
            runtime::canonical_fst_type_metadata(metadata)
        };
    }
    return { };
}

[[nodiscard]] runtime::TraceSignalId add_fst_trace_variable(
    runtime::TraceDeclarationBuilder& builder,
    const std::string_view name,
    const elaboration::SignalInfo& signal,
    const semantic::Language language,
    const runtime::TraceSourceMetadata& source)
{
    const auto type = fst_trace_type(signal, language);
    return builder.add_typed_variable(
        name, type.kind, signal.width, type.scalar_kind,
        type.canonical_metadata, source);
}

std::unique_ptr<TraceState> attach_trace(
    Simulation& simulation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    const bool dynamic_selection,
    std::shared_ptr<const TraceControlApplication> configured_application)
{
    if (!config.run.trace_file || !config.run.trace_enabled) {
        return nullptr;
    }
    const auto format = resolve_trace_format(config.run, diagnostics);
    if (!format) {
        return nullptr;
    }
    const auto surface = config.manifest_path == std::filesystem::path { "<command-line>" }
        ? TraceControlSurface::NonProjectSimulate
        : TraceControlSurface::ProjectCli;
    TraceControlResult configured;
    if (configured_application) {
        configured.application = std::move(configured_application);
    } else {
        configured = apply_trace_control(trace_control_request(
            config.run, surface, TraceControlPhase::Simulate));
    }
    if (!configured.ok()) {
        for (const auto& entry : configured.diagnostics) {
            diagnostics.error(entry.code, entry.message);
        }
        return nullptr;
    }
    const auto& filters = configured.application->request().selection;
    const auto scale = vcd_scale(
        simulation.time_resolution(), diagnostics);
    if (!scale) {
        return nullptr;
    }
    const auto fst_exponent = *format == project::TraceFormat::fst
        ? fst_timescale_exponent(scale->timescale, diagnostics)
        : std::optional<std::int8_t> { };
    if (*format == project::TraceFormat::fst && !fst_exponent) {
        return nullptr;
    }
    auto trace = std::make_unique<TraceState>();
    trace->format = *format;
    trace->control = std::move(configured.application);
    if (!prepare_trace_output(
            *trace, trace->control->request().output, diagnostics)) {
        return nullptr;
    }
    try {
        trace->tick_multiplier = scale->tick_multiplier;
        runtime::TraceDeclarationBuilder declaration_builder;
        constexpr std::array<std::string_view, 7> activity_names {
            "sequence", "kind", "action", "root", "value",
            "identity_hash", "detail_hash"
        };
        constexpr std::array<std::size_t, 7> activity_widths {
            64, 4, 4, 64, 64, 64, 64
        };
        std::array<runtime::TraceSignalId, 7> activity_ids { };
        for (std::size_t index = 0; index < activity_names.size(); ++index) {
            activity_ids[index] = declaration_builder.add_variable(
                "__fsim.uvm.activity." + std::string { activity_names[index] },
                activity_widths[index], runtime::SystemVerilogScalarKind::None,
                runtime::TraceSourceKind::Uvm);
        }
        trace->uvm_activity_trace_ids = activity_ids;
        trace->handles.resize(simulation.runtime_adapter().signals().size());
        trace->signal_trace_ids.resize(
            simulation.runtime_adapter().signals().size());
        std::vector<std::vector<runtime::TraceSignalId>> declaration_ids(
            simulation.runtime_adapter().signals().size());
        std::vector<std::string> declaration_owners(
            simulation.runtime_adapter().signals().size());
        std::vector<bool> initially_selected(
            simulation.runtime_adapter().signals().size());
        std::map<std::string, SignalId> trace_signal_by_path;
        std::map<std::uint64_t, const semantic::design::Object*>
            fst_string_by_runtime;
        struct StringTrace {
            runtime::simir::StringObjectId object { };
            runtime::TraceSignalId signal;
            bool selected { };
        };
        std::vector<StringTrace> string_traces;
        trace->scalar_kinds.resize(
            simulation.runtime_adapter().signals().size(),
            runtime::SystemVerilogScalarKind::None);
        for (const auto& signal : simulation.runtime_adapter().signals()) {
            trace->scalar_kinds[signal.id] = signal.systemverilog_scalar;
        }
        if (*format == project::TraceFormat::fst) {
            for (const auto& object : simulation.design_ir().objects()) {
                if (object.kind != semantic::design::ObjectKind::string) {
                    continue;
                }
                if (!fst_string_by_runtime.try_emplace(
                                              object.runtime_index, &object)
                        .second) {
                    throw std::invalid_argument(
                        "FST string trace has duplicate DesignIR ownership");
                }
            }
        }
        for (const auto& object : simulation.design_ir().objects()) {
            if (!design_object_is_signal_bearing(object)
                || object.runtime_index
                    > std::numeric_limits<SignalId>::max()) {
                continue;
            }
            const auto signal_id = static_cast<SignalId>(object.runtime_index);
            const auto& signal = simulation.runtime_adapter().signals().at(signal_id);
            const auto* specialization
                = *format == project::TraceFormat::fst
                ? &simulation.design_ir().specializations().at(
                      object.specialization.value())
                : nullptr;
            const auto fst_object = specialization
                ? std::optional { canonical_fst_trace_object(
                      simulation.design_ir(), object) }
                : std::nullopt;
            const auto trace_path = fst_object
                ? std::string_view { fst_object->path }
                : std::string_view { object.path };
            const auto [existing, inserted]
                = trace_signal_by_path.try_emplace(
                    std::string { trace_path }, signal_id);
            if (!inserted) {
                if (existing->second != signal_id) {
                    throw std::invalid_argument(
                        "trace hierarchy maps distinct signals to '"
                        + std::string { trace_path } + "'");
                }
                continue;
            }
            const auto selected = trace_selected(
                filters, trace_path);
            initially_selected[signal.id]
                = initially_selected[signal.id]
                || (selected && signal.width != 0);
            if (signal.width != 0 && (dynamic_selection || selected)) {
                auto& primary = trace->signal_trace_ids[signal.id];
                runtime::TraceSignalId declaration;
                if (*format == project::TraceFormat::fst) {
                    declaration = primary.value == 0
                        ? add_fst_trace_variable(declaration_builder,
                              fst_object->path, signal,
                              specialization->language, fst_object->source)
                        : declaration_builder.add_alias(
                              fst_object->path, primary, fst_object->source);
                } else {
                    declaration = primary.value == 0
                        ? declaration_builder.add_variable(object.path,
                              signal.width, signal.systemverilog_scalar)
                        : declaration_builder.add_alias(object.path, primary);
                }
                if (primary.value == 0) {
                    primary = declaration;
                    declaration_owners[signal.id] = trace_path;
                }
                declaration_ids[signal.id].push_back(declaration);
            }
        }
        if (*format == project::TraceFormat::fst) {
            for (const auto& object : simulation.runtime_adapter().string_objects()) {
                const auto design_object
                    = fst_string_by_runtime.find(object.id);
                if (design_object == fst_string_by_runtime.end()) {
                    throw std::invalid_argument(
                        "FST string trace lacks DesignIR ownership");
                }
                const auto fst_object = canonical_fst_trace_object(
                    simulation.design_ir(), *design_object->second);
                const auto selected = trace_selected(
                    filters, fst_object.path);
                if (!dynamic_selection && !selected) {
                    continue;
                }
                string_traces.push_back({ object.id,
                    declaration_builder.add_typed_variable(
                        fst_object.path,
                        runtime::TraceTypeKind::SystemVerilogString,
                        0,
                        runtime::SystemVerilogScalarKind::None,
                        runtime::canonical_fst_string_type(),
                        fst_object.source),
                    selected });
            }
        }
        trace->declarations = std::make_unique<runtime::TraceDeclarationModel>(
            std::move(declaration_builder).freeze());
        std::vector<TraceSelectionDeclaration> selection_declarations;
        selection_declarations.reserve(trace->signal_trace_ids.size());
        for (std::size_t signal = 0;
            signal < trace->signal_trace_ids.size(); ++signal) {
            if (trace->signal_trace_ids[signal].value == 0U) {
                continue;
            }
            selection_declarations.push_back(TraceSelectionDeclaration {
                static_cast<SignalId>(signal),
                trace->signal_trace_ids[signal],
                declaration_owners[signal],
                initially_selected[signal] });
        }
        trace->selection = std::make_unique<TraceSelectionControl>(
            *trace->declarations, selection_declarations);
        if (*format == project::TraceFormat::vcd) {
            trace->writer = std::make_unique<runtime::VcdWriter>(
                trace->stream, scale->timescale);
            const auto declared_handles
                = trace->writer->declare_model(*trace->declarations);
            trace->declaration_handles = declared_handles;
            for (std::size_t signal = 0; signal < declaration_ids.size(); ++signal) {
                for (const auto declaration : declaration_ids[signal]) {
                    trace->handles[signal].push_back(
                        declared_handles.at(declaration.value - 1U));
                }
            }
        } else {
            const auto compression =
                trace->control->status().effective_compression
                        == project::TraceCompression::deterministic
                    ? runtime::FstWriterCompression::Deterministic
                    : runtime::FstWriterCompression::None;
            trace->fst_writer = std::make_unique<runtime::FstWriter>(
                trace->stream, *fst_exponent, runtime::FstWriterLimits { },
                compression);
            trace->fst_writer->declare(*trace->declarations);
            for (std::size_t signal = 0; signal < declaration_ids.size(); ++signal) {
                for (const auto declaration : declaration_ids[signal]) {
                    trace->handles[signal].push_back(runtime::VcdSignal {
                        static_cast<std::uint32_t>(declaration.value) });
                }
            }
        }
        trace->observations = std::make_unique<TraceObservationRecorder>(
            *trace->declarations);
        auto* state = trace.get();
        static_cast<void>(trace->observations->add_observer(
            [state](const auto& record) {
                write_trace_observation(*state, record);
            }));
        if (simulation.now()
            > std::numeric_limits<SimulationTick>::max()
                / trace->tick_multiplier) {
            throw std::overflow_error { "trace timestamp scaling overflow" };
        }
        if (*format == project::TraceFormat::vcd) {
            trace->writer->begin(simulation.now() * trace->tick_multiplier);
        } else {
            trace->fst_writer->begin(
                simulation.now() * trace->tick_multiplier);
        }
        if (*format == project::TraceFormat::vcd) {
            const auto verilog_provenance
                = simulation.verilog_scope_provenance();
            const auto verilog_comments
                = simulation.verilog_provenance_comments();
            for (std::size_t index = 0; index < verilog_provenance.size(); ++index) {
                const auto& scope = verilog_provenance[index].path;
                const auto selected_scope = filters.empty()
                    || std::ranges::any_of(
                        simulation.design_ir().objects(), [&](const auto& object) {
                            return trace_selected(
                                       filters, object.path)
                                && (object.path == scope
                                    || (object.path.size() > scope.size()
                                        && object.path.starts_with(scope)
                                        && object.path[scope.size()] == '.'));
                        });
                if (selected_scope) {
                    trace->writer->comment(verilog_comments[index]);
                }
            }
            for (const auto& comment : simulation.vhdl_provenance_comments()) {
                trace->writer->comment(comment);
            }
        }
        for (const auto& signal : simulation.runtime_adapter().signals()) {
            if (trace->selection->selected(signal.id)) {
                if (!trace->handles[signal.id].empty()) {
                    const std::array observed { TraceObservationValue {
                        trace->signal_trace_ids[signal.id],
                        simulation.read_signal(signal.id), signal.id } };
                    static_cast<void>(trace->observations->accept(
                        TraceObservationKind::Signal, simulation.now(),
                        simulation.delta(), runtime::TraceRegion::Snapshot,
                        "signal:" + std::to_string(signal.id), observed));
                }
            }
        }
        if (*format == project::TraceFormat::fst) {
            for (const auto& string_trace : string_traces) {
                if (string_trace.selected) {
                    trace->fst_writer->set_initial_value(
                        string_trace.signal,
                        runtime::encode_fst_string(
                            simulation.read_string_object(
                                string_trace.object)));
                }
            }
        }
        trace->collecting_fst_initial_values = false;
        for (const auto& event : simulation.uvm_activity().events()) {
            write_uvm_activity_trace_event(*trace, event);
        }
        trace->simulation = &simulation;
        trace->uvm_activity_observer = simulation.add_uvm_activity_hook(
            [state](const auto& event) {
                write_uvm_activity_trace_event(*state, event);
            });
        simulation.set_signal_change_hook(
            [state](
                const SignalId signal,
                const PackedLogic4& value,
                const SimulationTick time,
                const std::uint64_t delta) {
                if (signal < state->handles.size()
                    && !state->handles[signal].empty()
                    && state->selection->selected(signal)) {
                    const std::array observed { TraceObservationValue {
                        state->signal_trace_ids[signal], value, signal } };
                    static_cast<void>(state->observations->accept(
                        TraceObservationKind::Signal, time, delta,
                        state->selection->observation_region(
                            time, delta, runtime::TraceRegion::Active),
                        "signal:" + std::to_string(signal), observed));
                }
            });
    } catch (const std::exception& error) {
        fail_trace(*trace, error.what(), &diagnostics);
        return nullptr;
    } catch (...) {
        fail_trace(*trace, "unknown trace attachment failure", &diagnostics);
        return nullptr;
    }
    return trace;
}

void write_trace_signal_value(
    TraceState& state,
    const runtime::simir::SignalId signal,
    const runtime::PackedLogic4& value)
{
    if (signal >= state.handles.size()
        || signal >= state.scalar_kinds.size()) {
        throw std::out_of_range { "trace signal is outside declared storage" };
    }
    const auto kind = state.scalar_kinds[signal];
    if (kind == runtime::SystemVerilogScalarKind::None) {
        for (const auto handle : state.handles[signal]) {
            state.writer->change(handle, value);
        }
        return;
    }
    const auto decoded = runtime::decode_systemverilog_scalar_payload(
        value, kind);
    if (!decoded) {
        const auto expected_width
            = kind == runtime::SystemVerilogScalarKind::ShortReal ? 32U : 64U;
        if (value.width() != expected_width || value.is_logic9()) {
            throw std::invalid_argument {
                "trace signal " + std::to_string(signal)
                + " observed an invalid scalar payload kind "
                + std::to_string(static_cast<unsigned>(kind)) + " width "
                + std::to_string(value.width())
            };
        }
        if (kind == runtime::SystemVerilogScalarKind::Time
            || kind == runtime::SystemVerilogScalarKind::Chandle) {
            for (const auto handle : state.handles[signal]) {
                state.writer->change(handle, value);
            }
        }
        // VCD real variables have no four-state encoding. Omitting an initial or
        // transient unknown payload preserves the implicit unknown interval until
        // a later known scalar change can be written.
        return;
    }
    for (const auto handle : state.handles[signal]) {
        state.writer->change(handle, decoded.value);
    }
}

namespace {

    [[nodiscard]] runtime::FstEncodedValue encode_trace_observation_value(
        const TraceState& state,
        const runtime::TraceSignalId signal,
        const runtime::PackedLogic4& value)
    {
        const auto& variable = state.declarations->variable(signal);
        const auto& type = state.declarations->type(variable.type);
        switch (type.kind) {
        case runtime::TraceTypeKind::Packed:
            return runtime::encode_fst_logic_value(value);
        case runtime::TraceTypeKind::SystemVerilogScalar:
            if (type.scalar_kind
                    == runtime::SystemVerilogScalarKind::ShortReal
                || type.scalar_kind == runtime::SystemVerilogScalarKind::Real
                || type.scalar_kind
                    == runtime::SystemVerilogScalarKind::Realtime) {
                const auto decoded
                    = runtime::decode_systemverilog_scalar_payload(
                        value, type.scalar_kind);
                if (!decoded) {
                    throw std::invalid_argument(
                        "FST real value is not losslessly representable");
                }
                return runtime::encode_fst_systemverilog_real(decoded.value);
            }
            return runtime::encode_fst_systemverilog_scalar(
                value, type.scalar_kind);
        case runtime::TraceTypeKind::Enumeration:
            return runtime::encode_fst_extended_value(
                value, runtime::FstValueProfile::Enumeration,
                type.canonical_metadata);
        case runtime::TraceTypeKind::VhdlPhysical:
            return runtime::encode_fst_extended_value(
                value, runtime::FstValueProfile::VhdlPhysical,
                type.canonical_metadata);
        case runtime::TraceTypeKind::VhdlTime:
            return runtime::encode_fst_extended_value(
                value, runtime::FstValueProfile::VhdlTime,
                type.canonical_metadata);
        case runtime::TraceTypeKind::VhdlLogic9:
            return runtime::encode_fst_extended_value(
                value, runtime::FstValueProfile::VhdlLogic9,
                type.canonical_metadata);
        case runtime::TraceTypeKind::TypedLeaf:
            return runtime::encode_fst_leaf_value(
                value, type.canonical_metadata);
        case runtime::TraceTypeKind::SystemVerilogString:
            break;
        }
        throw std::invalid_argument(
            "FST string declaration used the packed observation path");
    }

    void write_trace_observation(
        TraceState& state,
        const TraceObservationRecord& record)
    {
        for (const auto& observed : record.values) {
            const runtime::TraceEvent event { observed.signal, record.time,
                record.delta, record.region, record.sequence };
            if (state.format == project::TraceFormat::fst) {
                auto scaled_event = event;
                if (scaled_event.time
                    > std::numeric_limits<SimulationTick>::max()
                        / state.tick_multiplier) {
                    throw std::overflow_error(
                        "trace timestamp scaling overflow");
                }
                scaled_event.time *= state.tick_multiplier;
                const auto encoded = encode_trace_observation_value(
                    state, observed.signal, observed.value);
                if (state.collecting_fst_initial_values
                    && record.region == runtime::TraceRegion::Snapshot) {
                    state.fst_writer->set_initial_value(
                        observed.signal, encoded);
                } else {
                    state.fst_writer->change(scaled_event, encoded);
                }
                continue;
            }
            if (record.kind == TraceObservationKind::Signal) {
                state.writer->set_event(event, state.tick_multiplier);
            } else {
                if (record.time
                    > std::numeric_limits<SimulationTick>::max()
                        / state.tick_multiplier) {
                    throw std::overflow_error(
                        "trace timestamp scaling overflow");
                }
                state.writer->set_time(std::max(state.writer->time(),
                    record.time * state.tick_multiplier));
            }
            if (observed.runtime_signal) {
                write_trace_signal_value(
                    state, observed.runtime_signal.value(), observed.value);
            } else {
                state.writer->change(
                    state.declaration_handles.at(observed.signal.value - 1U),
                    observed.value);
            }
        }
    }

    void write_hdl_vcd_value(
        HdlVcdState& state,
        const runtime::simir::SignalId signal,
        const runtime::PackedLogic4& value)
    {
        const auto kind = state.scalar_kinds.at(signal);
        for (const auto handle : state.handles.at(signal)) {
            if (kind == runtime::SystemVerilogScalarKind::None) {
                state.writer->change(handle, value);
                continue;
            }
            const auto decoded = runtime::decode_systemverilog_scalar_payload(
                value, kind);
            if (!decoded) {
                throw std::runtime_error { "invalid HDL VCD scalar payload" };
            }
            state.writer->change(handle, decoded.value);
        }
    }

    void check_hdl_vcd_limit(HdlVcdState& state)
    {
        if (!state.writer || !state.byte_limit || state.limit_reached
            || state.writer->bytes_written() < *state.byte_limit) {
            return;
        }
        state.limit_reached = true;
        state.enabled = false;
        state.writer->comment("dump limit reached");
        state.writer->flush();
        state.stream.flush();
    }

    void hdl_vcd_checkpoint(
        HdlVcdState& state,
        const std::string_view command,
        const bool unknown)
    {
        if (!state.begun || state.limit_reached) {
            return;
        }
        const auto time = state.simulation->now();
        if (time > std::numeric_limits<SimulationTick>::max()
                / state.tick_multiplier) {
            throw std::overflow_error { "trace timestamp scaling overflow" };
        }
        state.writer->set_time(time * state.tick_multiplier);
        state.writer->begin_checkpoint(command);
        for (const auto& signal : state.simulation->runtime_adapter().signals()) {
            if (!state.selected.at(signal.id)) {
                continue;
            }
            if (unknown) {
                for (const auto handle : state.handles.at(signal.id)) {
                    state.writer->change_unknown(handle);
                }
            } else {
                write_hdl_vcd_value(
                    state, signal.id, state.simulation->read_signal(signal.id));
            }
        }
        state.writer->end_checkpoint();
        check_hdl_vcd_limit(state);
    }

    [[nodiscard]] std::filesystem::path hdl_vcd_path(
        const HdlVcdState& state,
        const std::string_view filename,
        const std::string_view default_filename = "dump.vcd")
    {
        auto path = filename.empty()
            ? std::filesystem::path { default_filename }
            : fsim::support::path_from_utf8(filename);
        if (!path.is_absolute()) {
            path = state.file_root / path;
        }
        path = path.lexically_normal();
        const auto root = state.file_root.lexically_normal();
        const auto relative = path.lexically_relative(root);
        if (relative.empty() || relative.is_absolute()
            || std::ranges::any_of(relative, [](const auto& component) {
                   return component == "..";
               })) {
            throw std::runtime_error { "HDL VCD path escapes the project file root" };
        }
        return path;
    }

    void append_extended_vcd(
        HdlVcdState::ExtendedFile& file,
        const std::string_view text)
    {
        file.stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file.stream) {
            throw std::runtime_error { "failed to write extended VCD output" };
        }
        file.bytes_written += text.size();
    }

    void check_extended_vcd_limit(HdlVcdState::ExtendedFile& file)
    {
        if (!file.begun || !file.byte_limit || file.limit_reached
            || file.bytes_written < *file.byte_limit) {
            return;
        }
        file.limit_reached = true;
        file.enabled = false;
        append_extended_vcd(
            file, "$comment dumpports limit reached $end\n");
        file.stream.flush();
    }

    void set_extended_vcd_time(
        HdlVcdState::ExtendedFile& file,
        const SimulationTick time)
    {
        if (time > std::numeric_limits<SimulationTick>::max()
                / file.tick_multiplier) {
            throw std::overflow_error { "extended VCD timestamp scaling overflow" };
        }
        const auto scaled = time * file.tick_multiplier;
        if (file.last_time == scaled) {
            return;
        }
        append_extended_vcd(file, "#" + std::to_string(scaled) + "\n");
        file.last_time = scaled;
    }

    [[nodiscard]] char extended_vcd_state(
        const semantic::design::Direction direction,
        const runtime::Logic4 value)
    {
        using semantic::design::Direction;
        if (direction == Direction::input) {
            return value == runtime::Logic4::zero ? 'D'
                : value == runtime::Logic4::one   ? 'U'
                : value == runtime::Logic4::z     ? 'Z'
                                                  : 'N';
        }
        if (direction == Direction::output
            || direction == Direction::buffer) {
            return value == runtime::Logic4::zero ? 'L'
                : value == runtime::Logic4::one   ? 'H'
                : value == runtime::Logic4::z     ? 'T'
                                                  : 'X';
        }
        return value == runtime::Logic4::zero ? '0'
            : value == runtime::Logic4::one   ? '1'
            : value == runtime::Logic4::z     ? 'F'
                                              : '?';
    }

    void write_extended_vcd_port(
        HdlVcdState& state,
        HdlVcdState::ExtendedFile& file,
        const std::size_t file_port,
        const runtime::PackedLogic4& value,
        const bool synthetic_unknown = false)
    {
        const auto& port = state.extended_ports.at(file.ports.at(file_port));
        std::string states;
        states.reserve(value.width());
        for (std::size_t bit = value.width(); bit-- > 0U;) {
            states.push_back(extended_vcd_state(
                port.direction, value.get(bit)));
        }
        auto strength
            = state.simulation->read_signal_strength(port.signal);
        if (synthetic_unknown) {
            const auto unknown_strength
                = std::max(strength.zero, strength.one);
            strength = { unknown_strength, unknown_strength };
        }
        std::string line;
        line.reserve(states.size() + 32U);
        line.push_back('p');
        line.append(states);
        line.push_back(' ');
        line.push_back(static_cast<char>('0'
            + static_cast<unsigned>(strength.zero)));
        line.push_back(' ');
        line.push_back(static_cast<char>('0'
            + static_cast<unsigned>(strength.one)));
        line.append(" <");
        line.append(std::to_string(file_port));
        line.push_back('\n');
        append_extended_vcd(file, line);
    }

    void extended_vcd_checkpoint(
        HdlVcdState& state,
        HdlVcdState::ExtendedFile& file,
        const std::string_view command,
        const bool unknown)
    {
        if (!file.begun || file.limit_reached) {
            return;
        }
        set_extended_vcd_time(file, state.simulation->now());
        append_extended_vcd(file, "$" + std::string { command } + "\n");
        for (std::size_t index = 0U; index < file.ports.size(); ++index) {
            const auto& port = state.extended_ports.at(file.ports[index]);
            if (unknown) {
                const auto width = state.simulation->read_signal(port.signal).width();
                write_extended_vcd_port(state, file, index,
                    runtime::PackedLogic4 { width, runtime::Logic4::x }, true);
            } else {
                write_extended_vcd_port(state, file, index,
                    state.simulation->read_signal(port.signal));
            }
        }
        append_extended_vcd(file, "$end\n");
        check_extended_vcd_limit(file);
    }

    void begin_extended_vcd_file(
        HdlVcdState& state,
        HdlVcdState::ExtendedFile& file,
        const SimulationTick time)
    {
        if (file.begun) {
            return;
        }
        std::error_code error;
        if (!file.path.parent_path().empty()) {
            std::filesystem::create_directories(file.path.parent_path(), error);
        }
        if (error) {
            throw std::runtime_error {
                "cannot create extended VCD directory: " + error.message()
            };
        }
        file.stream.open(file.path, std::ios::binary | std::ios::trunc);
        if (!file.stream) {
            throw std::runtime_error { "cannot open extended VCD file" };
        }
        diagnostic::Engine diagnostics;
        const auto scale = vcd_scale(
            state.simulation->time_resolution(), diagnostics);
        if (!scale) {
            throw std::runtime_error { "cannot derive extended VCD timescale" };
        }
        file.tick_multiplier = scale->tick_multiplier;
        append_extended_vcd(file, "$version fsim $end\n$timescale ");
        append_extended_vcd(file, scale->timescale);
        append_extended_vcd(file, " $end\n");
        std::string active_scope;
        for (std::size_t index = 0U; index < file.ports.size(); ++index) {
            const auto& port = state.extended_ports.at(file.ports[index]);
            if (port.scope != active_scope) {
                if (!active_scope.empty()) {
                    append_extended_vcd(file, "$upscope $end\n");
                }
                active_scope = port.scope;
                append_extended_vcd(
                    file, "$scope module " + active_scope + " $end\n");
            }
            append_extended_vcd(file, "$var port ");
            if (port.range) {
                append_extended_vcd(file,
                    "[" + std::to_string(port.range->first) + ":"
                        + std::to_string(port.range->second) + "]");
            } else {
                append_extended_vcd(file, "1");
            }
            append_extended_vcd(file,
                " <" + std::to_string(index) + " " + port.reference
                    + " $end\n");
        }
        if (!active_scope.empty()) {
            append_extended_vcd(file, "$upscope $end\n");
        }
        append_extended_vcd(file, "$enddefinitions $end\n");
        file.begun = true;
        set_extended_vcd_time(file, time);
        extended_vcd_checkpoint(state, file, "dumpports", false);
    }

    [[nodiscard]] std::string qualify_vcd_selection(
        const HdlVcdState& state,
        const runtime::simir::VcdControlEvent& event,
        const std::string_view selection)
    {
        const auto exact = [&](const std::string_view path) {
            return std::ranges::any_of(state.objects,
                       [&](const auto& object) { return object.first == path; })
                || std::ranges::find(state.instances, path) != state.instances.end();
        };
        if (exact(selection)) {
            return std::string { selection };
        }
        if (!event.scope.empty()) {
            auto qualified = event.scope + "." + std::string { selection };
            if (exact(qualified)) {
                return qualified;
            }
        }
        return std::string { selection };
    }

    void select_hdl_vcd_objects(
        HdlVcdState& state,
        const runtime::simir::VcdControlEvent& event)
    {
        if (event.selections.empty()) {
            std::fill(state.selected.begin(), state.selected.end(), true);
            return;
        }
        for (const auto& spelling : event.selections) {
            const auto selection = qualify_vcd_selection(state, event, spelling);
            const bool variable = std::ranges::any_of(
                state.objects,
                [&](const auto& object) { return object.first == selection; });
            if (variable) {
                for (const auto& [path, signal] : state.objects) {
                    if (path == selection) {
                        state.selected.at(signal) = true;
                    }
                }
                continue;
            }
            if (std::ranges::find(state.instances, selection)
                == state.instances.end()) {
                throw std::runtime_error {
                    "$dumpvars selection '" + selection + "' was not found"
                };
            }
            for (const auto& [path, signal] : state.objects) {
                if (!path.starts_with(selection)
                    || path.size() <= selection.size()
                    || path[selection.size()] != '.') {
                    continue;
                }
                const auto tail = std::string_view { path }.substr(
                    selection.size() + 1U);
                const auto separators = static_cast<std::uint64_t>(
                    std::ranges::count(tail, '.'));
                if (event.value == 0U || separators < event.value) {
                    state.selected.at(signal) = true;
                }
            }
        }
    }

    [[nodiscard]] std::vector<HdlVcdState::ExtendedFile*>
    selected_extended_vcd_files(
        HdlVcdState& state,
        const runtime::simir::VcdControlEvent& event)
    {
        std::vector<HdlVcdState::ExtendedFile*> result;
        if (event.filename.empty()) {
            for (auto& file : state.extended_files) {
                result.push_back(file.get());
            }
            return result;
        }
        const auto path = hdl_vcd_path(
            state, event.filename, "dumpports.vcd");
        for (auto& file : state.extended_files) {
            if (file->path == path) {
                result.push_back(file.get());
            }
        }
        return result;
    }

    [[nodiscard]] bool handle_extended_vcd_control(
        HdlVcdState& state,
        const runtime::simir::VcdControlEvent& event)
    {
        using runtime::simir::VcdControlKind;
        if (event.kind == VcdControlKind::ports) {
            if (state.dumpports_time && *state.dumpports_time != event.time) {
                throw std::runtime_error {
                    "all $dumpports calls must execute at one simulation time"
                };
            }
            state.dumpports_time = event.time;
            auto scopes = event.selections;
            if (scopes.empty()) {
                if (event.scope.empty()) {
                    throw std::runtime_error {
                        "$dumpports has no current module scope"
                    };
                }
                scopes.push_back(event.scope);
            }
            for (auto& scope : scopes) {
                scope = qualify_vcd_selection(state, event, scope);
                if (std::ranges::find(state.instances, scope)
                    == state.instances.end()) {
                    throw std::runtime_error {
                        "$dumpports scope '" + scope + "' was not found"
                    };
                }
                if (!state.extended_scopes.insert(scope).second) {
                    throw std::runtime_error {
                        "$dumpports scope '" + scope + "' is not unique"
                    };
                }
            }
            auto file = std::make_unique<HdlVcdState::ExtendedFile>();
            file->path = hdl_vcd_path(
                state, event.filename, "dumpports.vcd");
            if (!state.extended_paths.insert(file->path).second) {
                throw std::runtime_error {
                    "$dumpports filename is not unique"
                };
            }
            for (const auto& scope : scopes) {
                for (std::size_t index = 0U;
                    index < state.extended_ports.size(); ++index) {
                    if (state.extended_ports[index].scope == scope) {
                        file->ports.push_back(index);
                    }
                }
            }
            state.extended_files.push_back(std::move(file));
            return true;
        }
        if (event.kind == VcdControlKind::begin_ports) {
            if (!state.dumpports_time || *state.dumpports_time != event.time) {
                return true;
            }
            for (auto& file : state.extended_files) {
                begin_extended_vcd_file(state, *file, event.time);
            }
            return true;
        }
        if (event.kind != VcdControlKind::ports_off
            && event.kind != VcdControlKind::ports_on
            && event.kind != VcdControlKind::ports_all
            && event.kind != VcdControlKind::ports_limit
            && event.kind != VcdControlKind::ports_flush) {
            return false;
        }
        for (auto* file : selected_extended_vcd_files(state, event)) {
            if (event.kind == VcdControlKind::ports_off) {
                if (file->enabled) {
                    extended_vcd_checkpoint(
                        state, *file, "dumpportsoff", true);
                    file->enabled = false;
                }
            } else if (event.kind == VcdControlKind::ports_on) {
                if (!file->enabled) {
                    file->enabled = true;
                    extended_vcd_checkpoint(
                        state, *file, "dumpportson", false);
                }
            } else if (event.kind == VcdControlKind::ports_all) {
                extended_vcd_checkpoint(
                    state, *file, "dumpportsall", false);
            } else if (event.kind == VcdControlKind::ports_limit) {
                file->byte_limit = event.value;
                check_extended_vcd_limit(*file);
            } else if (file->begun) {
                file->stream.flush();
            }
        }
        return true;
    }

} // namespace

void attach_hdl_vcd_control(
    Simulation& simulation,
    HdlVcdState& state,
    const std::filesystem::path& file_root)
{
    state.simulation = &simulation;
    state.file_root = file_root;
    state.handles.resize(simulation.runtime_adapter().signals().size());
    state.selected.resize(simulation.runtime_adapter().signals().size());
    state.scalar_kinds.resize(
        simulation.runtime_adapter().signals().size(),
        runtime::SystemVerilogScalarKind::None);
    for (const auto& signal : simulation.runtime_adapter().signals()) {
        state.scalar_kinds.at(signal.id) = signal.systemverilog_scalar;
    }
    for (const auto& instance : simulation.design_ir().instances()) {
        state.instances.push_back(instance.path);
    }
    for (const auto& object : simulation.design_ir().objects()) {
        if (!design_object_is_signal_bearing(object)
            || object.width == 0U
            || object.runtime_index
                > std::numeric_limits<runtime::simir::SignalId>::max()) {
            continue;
        }
        const auto signal
            = static_cast<runtime::simir::SignalId>(object.runtime_index);
        state.objects.emplace_back(object.path, signal);
    }
    for (const auto& port : simulation.design_ir().ports()) {
        const auto& object = simulation.design_ir().objects().at(
            port.object.value());
        const auto& specialization
            = simulation.design_ir().specializations().at(
                object.specialization.value());
        if ((specialization.language != semantic::Language::verilog
                && specialization.language
                    != semantic::Language::system_verilog)
            || !design_object_is_signal_bearing(object)
            || object.width == 0U
            || object.runtime_index
                > std::numeric_limits<runtime::simir::SignalId>::max()) {
            continue;
        }
        const auto signal
            = static_cast<runtime::simir::SignalId>(object.runtime_index);
        const auto& instance = simulation.design_ir().instances().at(
            port.instance.value());
        const auto& info = simulation.runtime_adapter().signals().at(signal);
        HdlVcdState::ExtendedPort extended;
        extended.scope = instance.path;
        extended.reference = object.name;
        extended.signal = signal;
        extended.direction = port.direction;
        if (info.packed_range) {
            extended.range = std::pair {
                info.packed_range->left, info.packed_range->right
            };
        } else if (info.width > 1U) {
            extended.range = std::pair {
                static_cast<std::int64_t>(info.width - 1U), INT64_C(0)
            };
        }
        state.extended_ports.push_back(std::move(extended));
    }
    simulation.set_vcd_control_hook(
        [&state](const runtime::simir::VcdControlEvent& event) {
            using runtime::simir::VcdControlKind;
            if (handle_extended_vcd_control(state, event)) {
                return;
            }
            if (event.kind == VcdControlKind::file) {
                if (state.begun) {
                    throw std::runtime_error {
                        "$dumpfile cannot change an active VCD file"
                    };
                }
                state.path = hdl_vcd_path(state, event.filename);
                return;
            }
            if (event.kind == VcdControlKind::variables) {
                if (state.dumpvars_time && *state.dumpvars_time != event.time) {
                    throw std::runtime_error {
                        "all $dumpvars calls must execute at one simulation time"
                    };
                }
                state.dumpvars_time = event.time;
                select_hdl_vcd_objects(state, event);
                return;
            }
            if (event.kind == VcdControlKind::begin_variables) {
                if (state.begun || !state.dumpvars_time
                    || *state.dumpvars_time != event.time) {
                    return;
                }
                if (state.path.empty()) {
                    state.path = hdl_vcd_path(state, { });
                }
                std::error_code error;
                if (!state.path.parent_path().empty()) {
                    std::filesystem::create_directories(
                        state.path.parent_path(), error);
                }
                if (error) {
                    throw std::runtime_error {
                        "cannot create HDL VCD directory: " + error.message()
                    };
                }
                state.stream.open(
                    state.path, std::ios::binary | std::ios::trunc);
                if (!state.stream) {
                    throw std::runtime_error { "cannot open HDL VCD file" };
                }
                diagnostic::Engine diagnostics;
                const auto scale = vcd_scale(
                    state.simulation->time_resolution(), diagnostics);
                if (!scale) {
                    throw std::runtime_error {
                        "cannot derive HDL VCD timescale"
                    };
                }
                state.tick_multiplier = scale->tick_multiplier;
                state.writer = std::make_unique<runtime::VcdWriter>(
                    state.stream, scale->timescale);
                for (const auto& [path, signal] : state.objects) {
                    if (!state.selected.at(signal)) {
                        continue;
                    }
                    const auto& info
                        = state.simulation->runtime_adapter().signals().at(signal);
                    state.handles.at(signal).push_back(
                        info.systemverilog_scalar
                                == runtime::SystemVerilogScalarKind::None
                            ? state.writer->declare_signal(path, info.width)
                            : state.writer->declare_systemverilog_scalar(
                                  path, info.systemverilog_scalar));
                }
                state.writer->begin(event.time * state.tick_multiplier);
                state.begun = true;
                hdl_vcd_checkpoint(state, "dumpvars", false);
                return;
            }
            if (event.kind == VcdControlKind::limit) {
                state.byte_limit = event.value;
                check_hdl_vcd_limit(state);
                return;
            }
            if (event.kind == VcdControlKind::off) {
                hdl_vcd_checkpoint(state, "dumpoff", true);
                state.enabled = false;
                return;
            }
            if (event.kind == VcdControlKind::on) {
                if (!state.begun) {
                    return;
                }
                state.enabled = true;
                hdl_vcd_checkpoint(state, "dumpon", false);
                return;
            }
            if (event.kind == VcdControlKind::all) {
                hdl_vcd_checkpoint(state, "dumpall", false);
                return;
            }
            if (event.kind == VcdControlKind::flush) {
                if (state.writer) {
                    state.writer->flush();
                    state.stream.flush();
                }
                return;
            }
            throw std::runtime_error { "unknown HDL VCD control kind" };
        });
    state.observer = simulation.add_signal_change_hook(
        [&state](const runtime::simir::SignalId signal,
            const runtime::PackedLogic4& value,
            const SimulationTick time,
            std::uint64_t) {
            if (state.begun && state.enabled && !state.limit_reached
                && state.selected.at(signal)) {
                state.writer->set_time(time * state.tick_multiplier);
                write_hdl_vcd_value(state, signal, value);
                check_hdl_vcd_limit(state);
            }
            for (auto& file : state.extended_files) {
                if (!file->begun || !file->enabled
                    || file->limit_reached) {
                    continue;
                }
                bool wrote { };
                for (std::size_t index = 0U;
                    index < file->ports.size(); ++index) {
                    const auto& port
                        = state.extended_ports.at(file->ports[index]);
                    if (port.signal != signal) {
                        continue;
                    }
                    if (!wrote) {
                        set_extended_vcd_time(*file, time);
                        wrote = true;
                    }
                    write_extended_vcd_port(
                        state, *file, index, value);
                }
                if (wrote) {
                    check_extended_vcd_limit(*file);
                }
            }
        });
}

std::optional<SimulationTick> configured_duration(
    const project::Config& config,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)
{
    if (!config.run.duration) {
        return std::nullopt;
    }
    std::string error;
    const auto duration = parse_time(
        *config.run.duration, resolution, error);
    if (!duration) {
        diagnostics.error("FSIM-TIME-0002", error);
    }
    return duration;
}

void install_interrupt_hook(Simulation& simulation)
{
    interrupt_requested.store(false, std::memory_order_relaxed);
    simulation.set_safe_point_hook([](runtime::Scheduler& scheduler,
                                       runtime::SchedulerPhase) {
        if (interrupt_requested.exchange(false, std::memory_order_relaxed)) {
            scheduler.request_stop();
        }
    });
}

void report_native_cache_failures(
    const Simulation& simulation,
    diagnostic::Engine& diagnostics)
{
    const auto cache = simulation.native_cache_statistics();
    if (cache.load_failures == 0 && cache.store_failures == 0
        && cache.prune_failures == 0) {
        return;
    }
    diagnostics.warning(
        "FSIM-CACHE-0004",
        "native LLVM object cache reported "
            + std::to_string(cache.load_failures)
            + " load failure(s) and "
            + std::to_string(cache.store_failures)
            + " store failure(s), and "
            + std::to_string(cache.prune_failures)
            + " prune failure(s); simulation remains valid, but cache reuse "
              "or eviction may be incomplete");
}

int handle_check(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto checked = check_project(config, diagnostics);
    if (!checked) {
        return 1;
    }
    output << "checked " << checked->source_count << " source file(s), "
           << checked->parsed.units.size() << " design unit(s)\n";
    return 0;
}

int handle_build(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto built = build_project(config, diagnostics);
    if (!built) {
        return 1;
    }
    const auto roots = built->design_ir.roots();
    const auto signal_count = static_cast<std::size_t>(std::ranges::count_if(
        built->design_ir.objects(), [](const auto& object) {
            return object.kind == semantic::design::ObjectKind::signal
                && !object.parent_object;
        }));
    const auto process_count = built->design_ir.processes().size();
    const auto plugin_count = built->systemc_plugins.size();
    const auto cache_hit = built->cache_hit;
    const auto mapped_libraries = built->mapped_libraries;
    const auto selected_seed = built->seed;
    const auto entropy_seed_selected = built->entropy_seed;
    Simulation prepared(
        std::move(*built),
        config.run.max_deltas,
        SimulationEngine::compiled);
    report_native_cache_failures(prepared, diagnostics);
    const auto native_cache = prepared.native_cache_statistics();
    for (const auto& request : invocation.library_exports) {
        if (!export_library(
                config, request.library, request.path, diagnostics)) {
            return 1;
        }
        output << "exported logical library '" << request.library
               << "' to " << support::path_to_utf8(request.path) << '\n';
    }
    output << "built ";
    for (std::size_t index = 0; index < roots.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << roots[index];
    }
    output << " ("
           << signal_count << " signals, "
           << process_count << " processes";
    if (plugin_count != 0) {
        output << ", " << plugin_count
               << " validated SystemC plug-in artifact(s)";
    }
    if (!mapped_libraries.empty()) {
        output << ", " << mapped_libraries.size()
               << " mapped precompiled librar"
               << (mapped_libraries.size() == 1 ? "y" : "ies") << " [";
        for (std::size_t index = 0; index < mapped_libraries.size(); ++index) {
            if (index != 0) {
                output << ", ";
            }
            output << mapped_libraries[index].library;
        }
        output << ']';
    }
    output << ", " << prepared.compiled_process_count()
           << " LLVM-compiled process(es) in "
           << prepared.compiled_module_count()
           << " specialization module(s)";
    output << "; analysis cache "
           << (cache_hit ? "hit" : "populated");
    if (prepared.compiled_process_count() == 0) {
        output << "; native cache unused";
    } else {
        output << "; native cache "
               << native_cache.hits << " hit(s), "
               << native_cache.misses << " miss(es), "
               << native_cache.stores << " store(s), "
               << native_cache.rejected_entries << " rejected";
    }
    output << ")\n";
    if (entropy_seed_selected) {
        output << "random seed " << selected_seed << '\n';
    }
    return 0;
}

int run_built_project(
    BuiltProject built,
    const SimulationEngine engine,
    const project::Config& config,
    const std::span<const std::string> plusargs,
    diagnostic::Engine& diagnostics,
    std::ostream& output)
{
    const auto duration = configured_duration(
        config, built.time_resolution, diagnostics);
    if (config.run.duration && !duration) {
        return 1;
    }
    if (built.entropy_seed) {
        output << "random seed " << built.seed << '\n';
    }
    project::Config trace_config = config;
    std::shared_ptr<const TraceControlApplication> archived_control;
    if (built.trace_archive) {
        auto consumer_root = config.base_directory;
        if (config.run.trace_file && config.run.trace_enabled
            && built.trace_archive->output_intent.parent_path().empty()) {
            consumer_root = config.run.trace_file->parent_path();
        }
        auto restored = restore_trace_archive_control(
            *built.trace_archive, consumer_root);
        if (!restored.ok()) {
            for (const auto& diagnostic : restored.diagnostics)
                application_detail::import_diagnostic(diagnostics, diagnostic);
            return 1;
        }
        if (config.run.trace_file && config.run.trace_enabled) {
            const auto surface
                = config.manifest_path == std::filesystem::path { "<command-line>" }
                ? TraceControlSurface::NonProjectSimulate
                : TraceControlSurface::ProjectCli;
            auto current = apply_trace_control(trace_control_request(
                config.run, surface, TraceControlPhase::Simulate));
            if (!current.ok()) {
                for (const auto& diagnostic : current.diagnostics)
                    application_detail::import_diagnostic(diagnostics, diagnostic);
                return 1;
            }
            const auto current_snapshot = make_trace_archive_snapshot(
                *current.application, config.base_directory);
            if (!trace_archive_profiles_compatible(
                    current_snapshot, *built.trace_archive)) {
                diagnostics.error("FSIM-TRACE-ARCHIVE-003",
                    "simulation trace request conflicts with the archived design profile");
                return 1;
            }
        }
        archived_control = std::move(restored.application);
        publish_trace_control(*archived_control, trace_config.run);
    }
    Simulation simulation(
        std::move(built),
        config.run.max_deltas,
        engine);
    if (!apply_uvm_command_line(simulation, plusargs, diagnostics)) {
        return 1;
    }
    simulation.set_output_hook(
        [&output](
            const runtime::simir::ProcessId,
            const std::string_view text,
            const bool newline,
            const SimulationTick,
            const std::uint64_t) {
            output << text;
            if (newline) {
                output << '\n';
            }
        });
    simulation.set_report_hook(
        [&output](
            const runtime::simir::ProcessId,
            const std::string_view message,
            const runtime::simir::AssertionSeverity severity,
            const runtime::simir::SourceLocation& source,
            const SimulationTick,
            const std::uint64_t) {
            output << source.path << ':' << source.line << ':'
                   << source.column << ": "
                   << report_severity_name(severity)
                   << "[FSIM-HDL-REPORT]: " << message << '\n';
        });
    report_native_cache_failures(simulation, diagnostics);
    auto trace = attach_trace(simulation, trace_config, diagnostics, false,
        std::move(archived_control));
    if (trace_config.run.trace_file && trace_config.run.trace_enabled && !trace) {
        return 1;
    }
    install_interrupt_hook(simulation);
    const InterruptSignalGuard interrupt_signal;
    try {
        const auto result = simulation.run(duration);
        if (trace && !finish_trace(*trace, diagnostics)) {
            return 1;
        }
        output << "simulation "
               << (result.status == runtime::RunStatus::completed
                          ? "completed"
                          : result.status == runtime::RunStatus::time_limit
                          ? "reached time limit"
                          : "stopped")
               << " at tick " << result.time << ", delta " << result.delta << '\n';
        return 0;
    } catch (const runtime::DeltaCycleLimitError& error) {
        std::ostringstream message;
        message << error.what() << "; active process IDs: ";
        if (error.pending_orders().empty()) {
            message << "none";
        } else {
            for (std::size_t index = 0;
                index < error.pending_orders().size(); ++index) {
                if (index != 0) {
                    message << ',';
                }
                message << error.pending_orders()[index];
            }
        }
        message << "; recently changed signal IDs: ";
        if (error.recent_signals().empty()) {
            message << "none";
        } else {
            for (std::size_t index = 0;
                index < error.recent_signals().size(); ++index) {
                if (index != 0) {
                    message << ',';
                }
                message << error.recent_signals()[index];
            }
        }
        diagnostics.error(
            "FSIM-RUN-DELTA-0001", message.str());
    } catch (const runtime::simir::AssertionError& error) {
        diagnostic::SourceSpan span;
        span.path = error.source().path;
        span.begin.line = error.source().line;
        span.begin.column = error.source().column;
        span.end = span.begin;
        const auto severity = [&] {
            switch (error.severity()) {
            case runtime::simir::AssertionSeverity::note:
                return diagnostic::Severity::note;
            case runtime::simir::AssertionSeverity::warning:
                return diagnostic::Severity::warning;
            case runtime::simir::AssertionSeverity::error:
                return diagnostic::Severity::error;
            case runtime::simir::AssertionSeverity::failure:
                return diagnostic::Severity::fatal;
            }
            return diagnostic::Severity::error;
        }();
        diagnostics.report(diagnostic::Diagnostic {
            severity, "FSIM-RUN-ASSERT-0001", error.what(), std::move(span), { } });
    } catch (const runtime::simir::InterpreterError& error) {
        diagnostics.error("FSIM-RUN-0001", error.what());
    } catch (const std::exception& error) {
        diagnostics.error("FSIM-RUN-0002", error.what());
    }
    return 1;
}

int handle_run(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto built = build_project(config, diagnostics);
    if (!built) {
        return 1;
    }
    return run_built_project(
        std::move(*built), SimulationEngine::compiled,
        config, invocation.plusargs, diagnostics, output);
}

void print_debug_help(std::ostream& output)
{
    output
        << "Commands: continue|run [DURATION], run-until TIME, "
           "step statement|process|phase|delta|time,\n"
        << "          break source [PATH:]LINE, break time TIME, "
           "break signal SIGNAL [==|!= VALUE], break phase IDENTITY|*, "
           "break uvm IDENTITY|*,\n"
        << "          breakpoints,\n"
        << "          delete ID, clear, scope [PATH], scopes [PATH], "
           "signals [PATH],\n"
        << "          show SIGNAL,\n"
        << "          classes, class HANDLE [PROPERTY], chandles, "
           "chandle HANDLE,\n"
        << "          uvm [summary|phases|objections|tlm1|tlm2|all],\n"
        << "          vhdl [summary|scopes|objects|processes|psl|all],\n"
        << "          deposit SIGNAL VALUE, force SIGNAL VALUE, release SIGNAL,\n"
        << "          trace add|remove SIGNAL, trace all|clear|list|status|report|flush|close,\n"
        << "          locals, where, help, quit\n";
}

} // namespace fsim::app::application_detail
