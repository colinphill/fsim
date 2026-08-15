// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/trace_api.hpp"
#include "fsim/app/trace_archive.hpp"
#include "fsim/runtime/fst_reader.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string { message });
    }
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input { path, std::ios::binary };
    require(static_cast<bool>(input), "cannot open owned FST corpus manifest");
    return { std::istreambuf_iterator<char> { input }, { } };
}

void verify_manifest(const std::filesystem::path& directory)
{
    const auto source_text = read_file(directory / "complete_trace.tsv");
    std::string text;
    text.reserve(source_text.size());
    for (std::size_t index = 0; index < source_text.size(); ++index) {
        if (source_text[index] == '\r'
            && index + 1U < source_text.size()
            && source_text[index + 1U] == '\n') {
            continue;
        }
        text.push_back(source_text[index]);
    }
    require(text.starts_with("# SPDX-License-Identifier: Apache-2.0\n"),
        "FST corpus manifest must retain Apache-2.0 provenance");
    require(text.find('\r') == std::string::npos,
        "FST corpus manifest must not contain a bare carriage return");
    require(static_cast<std::size_t>(std::ranges::count(text, '\n')) == 30U,
        "FST corpus manifest must retain 28 exact obligations");
    constexpr std::array tokens {
        "packed-bit", "packed-logic", "real", "shortreal", "realtime",
        "time", "chandle", "string", "enumeration", "vhdl-physical",
        "vhdl-time", "vhdl-logic9", "resolved-state", "strength-zero",
        "strength-one", "packed-aggregate", "unpacked-aggregate",
        "dynamic-class", "container", "coverage", "assertion", "alias",
        "mixed-roots", "selection", "callbacks", "phases", "artifacts",
        "negative PASS time advancement and clean close"
    };
    for (const auto token : tokens) {
        require(text.find(token) != std::string::npos,
            "FST corpus manifest lost an obligation");
    }
}

struct CorpusValue {
    fsim::runtime::TraceSignalId signal;
    fsim::runtime::FstEncodedValue initial;
};

struct CompleteCorpus {
    fsim::runtime::TraceDeclarationModel model;
    std::vector<CorpusValue> values;
    fsim::runtime::TraceSignalId bits;
    fsim::runtime::TraceSignalId logic;
};

[[nodiscard]] fsim::runtime::TraceSourceMetadata source(
    const fsim::runtime::TraceLanguage language,
    const std::string_view root, const std::string_view library,
    const std::string_view owner,
    const fsim::runtime::TraceSourceKind kind
        = fsim::runtime::TraceSourceKind::Hdl)
{
    fsim::runtime::TraceSourceMetadata result;
    result.kind = kind;
    result.language = language;
    result.root_identity = root;
    result.library = library;
    result.owner_identity = owner;
    return result;
}

[[nodiscard]] CompleteCorpus make_complete_corpus()
{
    using namespace fsim::runtime;
    const auto verilog = source(TraceLanguage::Verilog, "producer", "work",
        "work.producer@producer");
    const auto systemverilog = source(TraceLanguage::SystemVerilog, "monitor",
        "verification", "verification.monitor@monitor");
    const auto vhdl = source(TraceLanguage::Vhdl, "consumer", "rtl",
        "rtl.consumer(rtl)@consumer");
    const auto systemc = source(TraceLanguage::SystemC, "bridge", "native",
        "native.bridge@bridge", TraceSourceKind::SystemC);

    TraceDeclarationBuilder builder;
    std::vector<CorpusValue> values;
    const auto add = [&](const std::string_view path,
                         const TraceTypeKind type_kind,
                         const SystemVerilogScalarKind scalar_kind,
                         const FstEncodedValue& value,
                         const TraceSourceMetadata& provenance) {
        const auto signal = builder.add_typed_variable(path, type_kind,
            value.width(), scalar_kind, std::string { value.canonical_type() },
            provenance);
        values.push_back({ signal, value });
        return signal;
    };

    const auto bit_value = encode_fst_bit_value(
        PackedBit2::from_msb_string("10100101"), FstValueProfile::BitVector);
    const auto bits = add("producer.bits", TraceTypeKind::Packed,
        SystemVerilogScalarKind::None, bit_value, verilog);
    const auto logic_value = encode_fst_logic_value(
        PackedLogic4::from_msb_string("10XZ0101"));
    const auto logic = add("producer.logic", TraceTypeKind::Packed,
        SystemVerilogScalarKind::None, logic_value, verilog);

    const auto real = encode_fst_systemverilog_real(
        { SystemVerilogScalarKind::Real, UINT64_C(0x8000000000000000) });
    static_cast<void>(add("monitor.real", TraceTypeKind::SystemVerilogScalar,
        SystemVerilogScalarKind::Real, real, systemverilog));
    const auto shortreal = encode_fst_systemverilog_real(
        { SystemVerilogScalarKind::ShortReal, UINT32_C(0x3fc00000) });
    static_cast<void>(add("monitor.shortreal",
        TraceTypeKind::SystemVerilogScalar,
        SystemVerilogScalarKind::ShortReal, shortreal, systemverilog));
    const auto realtime = encode_fst_systemverilog_real(
        { SystemVerilogScalarKind::Realtime,
            UINT64_C(0x4000000000000000) });
    static_cast<void>(add("monitor.realtime",
        TraceTypeKind::SystemVerilogScalar,
        SystemVerilogScalarKind::Realtime, realtime, systemverilog));
    const auto time = encode_fst_systemverilog_scalar(
        PackedLogic4::from_aval_bval(64U, 17U, 0U),
        SystemVerilogScalarKind::Time);
    static_cast<void>(add("monitor.time", TraceTypeKind::SystemVerilogScalar,
        SystemVerilogScalarKind::Time, time, systemverilog));
    const auto chandle = encode_fst_systemverilog_scalar(
        PackedLogic4::from_aval_bval(64U, 0x1234U, 0U),
        SystemVerilogScalarKind::Chandle);
    static_cast<void>(add("monitor.chandle",
        TraceTypeKind::SystemVerilogScalar,
        SystemVerilogScalarKind::Chandle, chandle, systemverilog));
    const auto string
        = encode_fst_string(std::string_view { "A\0B", 3U });
    static_cast<void>(add("monitor.text", TraceTypeKind::SystemVerilogString,
        SystemVerilogScalarKind::None, string, systemverilog));

    FstExtendedTypeMetadata enum_metadata;
    enum_metadata.kind = FstExtendedTypeKind::Enumeration;
    enum_metadata.language = FstTypeLanguage::SystemVerilog;
    enum_metadata.nominal_name = "state_t";
    enum_metadata.width = 3U;
    enum_metadata.four_state = true;
    enum_metadata.enumeration_literals = { "IDLE", "BUSY", "ERROR" };
    const auto enumeration = encode_fst_extended_value(
        PackedLogic4::from_msb_string("1X0"),
        FstValueProfile::Enumeration, enum_metadata);
    static_cast<void>(add("monitor.state", TraceTypeKind::Enumeration,
        SystemVerilogScalarKind::None, enumeration, systemverilog));

    FstExtendedTypeMetadata physical_metadata;
    physical_metadata.kind = FstExtendedTypeKind::VhdlPhysical;
    physical_metadata.language = FstTypeLanguage::Vhdl;
    physical_metadata.nominal_name = "duration";
    physical_metadata.width = 32U;
    physical_metadata.physical_units = { { "fs", 1 }, { "ps", 1'000 } };
    const auto physical = encode_fst_extended_value(
        PackedLogic4::from_aval_bval(32U, 123U, 0U),
        FstValueProfile::VhdlPhysical, physical_metadata);
    static_cast<void>(add("consumer.physical", TraceTypeKind::VhdlPhysical,
        SystemVerilogScalarKind::None, physical, vhdl));
    FstExtendedTypeMetadata vhdl_time_metadata;
    vhdl_time_metadata.kind = FstExtendedTypeKind::VhdlTime;
    vhdl_time_metadata.language = FstTypeLanguage::Vhdl;
    vhdl_time_metadata.nominal_name = "@builtin:time";
    vhdl_time_metadata.width = 64U;
    const auto vhdl_time = encode_fst_extended_value(
        PackedLogic4::from_aval_bval(64U, 25U, 0U),
        FstValueProfile::VhdlTime, vhdl_time_metadata);
    static_cast<void>(add("consumer.time", TraceTypeKind::VhdlTime,
        SystemVerilogScalarKind::None, vhdl_time, vhdl));
    FstExtendedTypeMetadata logic9_metadata;
    logic9_metadata.kind = FstExtendedTypeKind::VhdlLogic9;
    logic9_metadata.language = FstTypeLanguage::Vhdl;
    logic9_metadata.nominal_name = "std_logic_vector";
    logic9_metadata.width = 9U;
    logic9_metadata.enumeration_literals = {
        "'U'", "'X'", "'0'", "'1'", "'Z'", "'W'", "'L'", "'H'", "'-'"
    };
    const auto logic9 = encode_fst_extended_value(
        PackedLogic4::from_logic9_msb_string("UX01ZWLH-"),
        FstValueProfile::VhdlLogic9, logic9_metadata);
    static_cast<void>(add("consumer.logic9", TraceTypeKind::VhdlLogic9,
        SystemVerilogScalarKind::None, logic9, vhdl));

    constexpr std::array leaf_kinds {
        FstLeafKind::ResolvedState, FstLeafKind::ResolvedStrengthZero,
        FstLeafKind::ResolvedStrengthOne, FstLeafKind::PackedAggregate,
        FstLeafKind::UnpackedAggregate, FstLeafKind::DynamicClass,
        FstLeafKind::Container, FstLeafKind::Coverage,
        FstLeafKind::Assertion
    };
    for (std::size_t index = 0U; index < leaf_kinds.size(); ++index) {
        FstLeafTypeMetadata metadata;
        metadata.kind = leaf_kinds[index];
        metadata.owner_identity = "bridge-owner-" + std::to_string(index);
        metadata.leaf_path = "payload.leaf-" + std::to_string(index);
        if (metadata.kind == FstLeafKind::ResolvedState) {
            metadata.width = 1U;
            metadata.four_state = true;
        } else if (metadata.kind == FstLeafKind::ResolvedStrengthZero
            || metadata.kind == FstLeafKind::ResolvedStrengthOne) {
            metadata.width = 3U;
        } else {
            metadata.width = 8U;
            metadata.four_state = true;
        }
        if (metadata.kind == FstLeafKind::PackedAggregate) {
            metadata.dimensions = { { 7, 0, true } };
        } else if (metadata.kind == FstLeafKind::UnpackedAggregate
            || metadata.kind == FstLeafKind::Container) {
            metadata.dimensions = { { 0, 7, false } };
        }
        const auto symbols = metadata.width == 1U ? "z"
            : metadata.width == 3U                 ? "101"
                                                   : "10XZ0101";
        const auto value = encode_fst_leaf_value(
            PackedLogic4::from_msb_string(symbols), metadata);
        const auto path = "bridge.leaf_" + std::to_string(index);
        static_cast<void>(add(path, TraceTypeKind::TypedLeaf,
            SystemVerilogScalarKind::None, value, systemc));
    }
    static_cast<void>(builder.add_alias(
        "consumer.producer_logic", logic, vhdl));
    return { std::move(builder).freeze(), std::move(values), bits, logic };
}

[[nodiscard]] std::string write_fst(const CompleteCorpus& corpus,
    const fsim::runtime::FstWriterCompression compression)
{
    using namespace fsim::runtime;
    std::ostringstream output;
    FstWriter writer { output, -9, { }, compression };
    writer.declare(corpus.model);
    writer.begin(5U);
    for (const auto& value : corpus.values) {
        writer.set_initial_value(value.signal, value.initial);
    }
    writer.change({ corpus.logic, 6U, 0U, TraceRegion::Active, 1U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("00000001")));
    writer.change({ corpus.bits, 7U, 0U, TraceRegion::Observed, 2U },
        encode_fst_bit_value(PackedBit2::from_msb_string("01010101"),
            FstValueProfile::BitVector));
    writer.change({ corpus.logic, 7U, 1U, TraceRegion::Reactive, 3U },
        encode_fst_logic_value(PackedLogic4::from_msb_string("00000010")));
    writer.close(7U);
    return output.str();
}

void verify_complete_fst(const CompleteCorpus& corpus)
{
    using namespace fsim::runtime;
    const auto deterministic
        = write_fst(corpus, FstWriterCompression::Deterministic);
    const auto repeated
        = write_fst(corpus, FstWriterCompression::Deterministic);
    const auto stored = write_fst(corpus, FstWriterCompression::None);
    require(deterministic == repeated,
        "deterministic complete FST corpus bytes must repeat exactly");
    require(deterministic != stored,
        "stored and deterministic complete FST bytes must remain distinct");
    const auto decoded = read_fst(deterministic);
    const auto decoded_stored = read_fst(stored);
    require(decoded.ok() && decoded_stored.ok(),
        "both complete FST compression profiles must decode");
    require(decoded.trace->semantic_digest
            == decoded_stored.trace->semantic_digest,
        "both complete FST profiles must retain identical semantics");
    require(decoded.trace->initial_time == 5U
            && decoded.trace->final_time == 7U,
        "complete FST corpus must advance time and close exactly");
    require(decoded.trace->timestamps
            == std::vector<SimulationTick>({ 5U, 6U, 7U }),
        "complete FST corpus must retain its ordered timestamps");
    require(decoded.trace->scopes.size() == 4U,
        "complete FST corpus must retain four mixed-language roots");
    require(decoded.trace->declarations.size() == corpus.values.size() + 1U,
        "complete FST corpus must retain every variable and alias");
    require(decoded.trace->values.size() == corpus.values.size() + 3U,
        "complete FST corpus must retain every initial and later value");
    auto truncated = deterministic;
    truncated.pop_back();
    const auto rejected = read_fst(truncated);
    require(!rejected.ok() && rejected.diagnostics.size() == 1U,
        "complete FST corpus negative must reject transactionally");
}

void verify_vcd_surface()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto bits = builder.add_variable("producer.bits", 8U);
    const auto logic = builder.add_variable("producer.logic", 8U);
    static_cast<void>(builder.add_alias("consumer.producer_logic", logic));
    const auto model = std::move(builder).freeze();
    std::ostringstream output;
    VcdWriter writer { output, "1ns", 64U };
    const auto handles = writer.declare_model(model);
    require(handles.size() == 3U, "VCD corpus must retain its alias handle");
    writer.begin(5U);
    writer.set_event({ bits, 5U, 0U, TraceRegion::Snapshot, 1U });
    writer.change(handles[0], PackedLogic4::from_msb_string("10100101"));
    writer.set_event({ logic, 5U, 0U, TraceRegion::Snapshot, 2U });
    writer.change(handles[1], PackedLogic4::from_msb_string("10XZ0101"));
    writer.change(handles[2], PackedLogic4::from_msb_string("10XZ0101"));
    writer.set_event({ logic, 6U, 0U, TraceRegion::Active, 3U });
    writer.change(handles[1], PackedLogic4::from_msb_string("00000001"));
    writer.change(handles[2], PackedLogic4::from_msb_string("00000001"));
    writer.set_event({ bits, 7U, 0U, TraceRegion::Observed, 4U });
    writer.change(handles[0], PackedLogic4::from_msb_string("01010101"));
    writer.flush();
    const auto text = output.str();
    require(text.find("$enddefinitions $end") != std::string::npos
            && text.find("#5") != std::string::npos
            && text.find("#6") != std::string::npos
            && text.find("#7") != std::string::npos,
        "VCD corpus must retain declarations and time advancement");
}

void verify_phase_artifact_and_relocation_closure()
{
    using namespace fsim;
    constexpr std::array surfaces {
        app::TraceControlSurface::ProjectCli,
        app::TraceControlSurface::NonProjectCompile,
        app::TraceControlSurface::NonProjectElaborate,
        app::TraceControlSurface::NonProjectSimulate
    };
    constexpr std::array phases { app::TraceControlPhase::Simulate,
        app::TraceControlPhase::Compile, app::TraceControlPhase::Elaborate,
        app::TraceControlPhase::Simulate };
    constexpr std::array kinds { app::TraceArchiveKind::Object,
        app::TraceArchiveKind::Design, app::TraceArchiveKind::Library,
        app::TraceArchiveKind::NativeCache,
        app::TraceArchiveKind::Checkpoint };
    const auto relocation_root
        = std::filesystem::temp_directory_path() / "fsim-fst-corpus-relocation";
    const auto producer = relocation_root / "producer" / "build";
    const auto consumer = relocation_root / "consumer" / "replay";
    for (std::size_t index = 0U; index < surfaces.size(); ++index) {
        app::TraceControlRequest request;
        request.surface = surfaces[index];
        request.phase = phases[index];
        request.output = producer / "traces"
            / ("complete-" + std::to_string(index) + ".fst");
        request.format = project::TraceFormat::fst;
        request.compression = project::TraceCompression::deterministic;
        request.selection = { "producer.logic", "consumer.logic9" };
        request.lifecycle = app::TraceLifecycle::Configured;
        request.generation = index + 1U;
        const auto applied = app::apply_trace_control(std::move(request));
        require(applied.ok(), "every project/non-project phase must apply");
        const auto snapshot
            = app::make_trace_archive_snapshot(*applied.application, producer);
        require(!snapshot.output_intent.is_absolute(),
            "trace archive corpus must hide producer paths");
        for (const auto kind : kinds) {
            const auto encoded = app::encode_trace_archive(snapshot, kind);
            require(encoded.ok(), "every trace artifact kind must encode");
            const auto decoded = app::decode_trace_archive(encoded.archive,
                kind, project::TraceFormat::fst, snapshot.profile_identity);
            require(decoded.ok() && decoded.snapshot == snapshot,
                "every trace artifact kind must round-trip exactly");
        }
        const auto restored
            = app::restore_trace_archive_control(snapshot, consumer);
        require(restored.ok()
                && restored.application->request().output.string().find(
                       consumer.string())
                    == 0U,
            "every trace phase must restore beneath the consumer root");
    }
}

} // namespace

int main(const int argc, const char* const* argv)
{
    try {
        require(argc == 2, "owned FST corpus directory is required");
        verify_manifest(argv[1]);
        const auto corpus = make_complete_corpus();
        verify_complete_fst(corpus);
        verify_vcd_surface();
        verify_phase_artifact_and_relocation_closure();
        std::cout
            << "FSIM-FST-CORPUS-PASS "
               "formats=vcd,fst profiles=stored,deterministic "
               "values=bit,logic,real,shortreal,realtime,time,chandle,string,enum,physical,vhdl-time,logic9,typed-leaves "
               "roots=verilog,systemverilog,vhdl,systemc aliases=yes "
               "selection=selective,late callbacks=retained "
               "phases=project,compile,elaborate,simulate "
               "artifacts=object,design,library,native-cache,checkpoint "
               "relocation=yes negatives=yes time-advanced=5,6,7 "
               "pass=yes clean-close=yes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FST corpus test failure: " << error.what() << '\n';
        return 1;
    }
}
