// SPDX-License-Identifier: Apache-2.0
//
// Run-state sections (#128; IEEE 1800-2023 D.9, IEEE 1076-2019 20.7-20.9): the
// archive that every section is written and read through, the section
// registry the application adds its sections to, and the reference
// interpreter's host and process sections. Expected values are derived in the
// comments; lines marked "unverified" depend on scheduler code the test does
// not read.
#include "runtime_test_support.hpp"

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/class_static.hpp"
#include "../../src/runtime/run_state_archive.hpp"
#include "fsim/runtime/run_state_image.hpp"
#include "fsim/runtime/run_state_sections.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/scheduler_events.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_coverage.hpp"
#include "fsim/runtime/simir_interpreter.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_phase.hpp"
#include "../../src/runtime/uvm_run_state.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include "../../src/runtime/run_state_value_codecs.hpp"

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

struct Point {
    std::uint32_t x { };
    std::int64_t y { };
    std::string name;
    std::optional<std::uint8_t> tag;
    friend bool operator==(const Point&, const Point&) = default;
};

struct Sample {
    bool flag { };
    std::vector<Point> points;
    std::map<std::uint32_t, std::string> ordered;
    std::unordered_map<std::uint32_t, Point> hashed;
    std::unordered_set<std::uint64_t> seen;
    std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
    std::vector<bool> bits;
    std::unique_ptr<PackedLogic4> value;
    PackedLogic4 wide;
};

[[nodiscard]] std::string encode(Sample& sample)
{
    std::string bytes;
    auto archive = RunStateArchive::writer(bytes);
    archive.io(sample);
    require(archive.ok(), "a sample is written");
    return bytes;
}

// The archive reads back what it writes, and the bytes of an unordered
// container do not depend on the order it was filled in (so that saving twice
// from one point gives the same bytes).
void test_archive_round_trip_and_determinism()
{
    Sample first;
    first.flag = true;
    first.points = { { 7U, -3, "a", std::nullopt }, { 9U, 4, "bc", 5U } };
    first.ordered = { { 2U, "two" }, { 1U, "one" } };
    for (std::uint32_t key = 0U; key < 50U; ++key) {
        first.hashed.emplace(key, Point { key, -static_cast<std::int64_t>(key),
            std::to_string(key), std::nullopt });
    }
    first.pairs = { { 3U, 1U }, { 1U, 2U } };
    first.bits = { true, false, true, true };
    first.value = std::make_unique<PackedLogic4>(
        PackedLogic4::from_msb_string("10XZ"));
    first.wide = PackedLogic4::from_logic9_msb_string("UXLH01WZ-");

    Sample second;
    second.flag = true;
    second.points = first.points;
    second.ordered = first.ordered;
    // The same entries in the opposite insertion order.
    for (std::uint32_t key = 50U; key-- > 0U;) {
        second.hashed.emplace(key, first.hashed.at(key));
    }
    for (std::uint32_t bit = 0U; bit < 60U; ++bit) {
        first.seen.insert(UINT64_C(1) << bit);
    }
    for (std::uint32_t bit = 60U; bit-- > 0U;) {
        second.seen.insert(UINT64_C(1) << bit);
    }
    second.pairs = first.pairs;
    second.bits = first.bits;
    second.value = std::make_unique<PackedLogic4>(*first.value);
    second.wide = first.wide;
    const auto bytes = encode(first);
    require(bytes == encode(second),
        "equal states with different hash orders give equal bytes");

    Sample loaded;
    auto reader = RunStateArchive::reader(bytes);
    reader.io(loaded);
    require(reader.finished(), "the whole sample is consumed");
    require(loaded.flag && loaded.points == first.points
            && loaded.ordered == first.ordered && loaded.hashed == first.hashed
            && loaded.seen == first.seen && loaded.pairs == first.pairs
            && loaded.bits == first.bits,
        "containers round-trip");
    require(loaded.value && loaded.value->to_msb_string() == "10XZ"
            && loaded.wide.is_logic9() && loaded.wide.width() == 9U,
        "packed values round-trip, four-state and nine-state");
    require(encode(loaded) == bytes, "a loaded state saves the same bytes");

    // A section that ends early, or claims more elements than it holds, fails
    // without allocating.
    Sample broken;
    auto truncated = RunStateArchive::reader(
        std::string_view { bytes }.substr(0U, bytes.size() / 2U));
    truncated.io(broken);
    require(!truncated.ok(), "a truncated section is refused");
    std::string huge;
    auto writer = RunStateArchive::writer(huge);
    std::uint64_t claimed = UINT64_C(1) << 40U;
    writer.io(claimed);
    std::vector<std::uint64_t> sink;
    auto lying = RunStateArchive::reader(huge);
    lying.io(sink);
    require(!lying.ok(), "a count past the bytes left is refused");
}

// The registry writes every owner's section, reads them back in order, and
// refuses an image with a missing section, another version or trailing bytes
// with the FSIM-SAVE codes of docs/diagnostics.md.
void test_section_registry()
{
    std::uint64_t counter = 41U;
    std::string label = "abc";
    RunStateSectionRegistry registry;
    registry.add({ "test.counter", 3U,
        [&](RunStateArchive& archive) { archive.io(counter); },
        [&](RunStateArchive& archive) { archive.io(counter); }, true });
    registry.add({ "test.label", 1U,
        [&](RunStateArchive& archive) { archive.io(label); },
        [&](RunStateArchive& archive) { archive.io(label); }, false });
    bool duplicate_refused { };
    try {
        registry.add({ "test.label", 1U, { }, { }, true });
    } catch (const std::logic_error&) {
        duplicate_refused = true;
    }
    require(duplicate_refused, "a section name has one owner");

    RunStateImage image { "test-identity" };
    std::vector<std::string> errors;
    require(registry.save(image, errors) && errors.empty(), "owners save");
    require(image.find("test.counter") != nullptr
            && image.find("test.counter")->version == 3U,
        "the section carries its owner's version");

    counter = 0U;
    label.clear();
    require(registry.restore(image, errors) && counter == 41U && label == "abc",
        "owners restore their state");

    RunStateImage lacking { "test-identity" };
    lacking.put("test.label", 1U, image.find("test.label")->bytes);
    require(!registry.restore(lacking, errors) && !errors.empty()
            && errors.back().starts_with("FSIM-SAVE-0008"),
        "a missing required section is FSIM-SAVE-0008");

    RunStateImage stale { "test-identity" };
    stale.put("test.counter", 2U, image.find("test.counter")->bytes);
    errors.clear();
    require(!registry.restore(stale, errors)
            && errors.back().starts_with("FSIM-SAVE-0009"),
        "another version is FSIM-SAVE-0009");

    RunStateImage trailing { "test-identity" };
    trailing.put("test.counter", 3U, image.find("test.counter")->bytes + "x");
    errors.clear();
    require(!registry.restore(trailing, errors)
            && errors.back().starts_with("FSIM-SAVE-0010"),
        "trailing bytes are FSIM-SAVE-0010");

    RunStateImage extra { "test-identity" };
    extra.put("other.section", 1U, "z");
    require(registry.unclaimed(extra).size() == 1U,
        "an unregistered section is reported as unclaimed");
}

// Optional owners: one with `present` is not written while absent, is cleared
// when the image lacks it, and one with `refuse` stops the save with
// FSIM-SAVE-0021 (a live chandle cannot be written, IEEE 1800-2023 35.5.4).
void test_optional_section_owners()
{
    std::uint64_t stored = 7U;
    int cleared = 0;
    bool in_use = true;
    RunStateSectionRegistry registry;
    registry.add({ .name = "app.number", .version = 3U,
        .save = [&](RunStateArchive& archive) { archive.io(stored); },
        .restore = [&](RunStateArchive& archive) { archive.io(stored); } });
    registry.add({ .name = "app.maybe", .version = 1U,
        .save = [&](RunStateArchive& archive) { archive.io(stored); },
        .restore = [&](RunStateArchive& archive) { archive.io(stored); },
        .present = [&] { return in_use; },
        .clear = [&] { ++cleared; } });

    RunStateImage image { "design" };
    std::vector<std::string> errors;
    require(registry.save(image, errors) && image.find("app.maybe") != nullptr,
        "a present optional section is written");
    in_use = false;
    RunStateImage without { "design" };
    require(registry.save(without, errors) && without.find("app.number") != nullptr
            && without.find("app.maybe") == nullptr,
        "an absent optional section is not written");

    stored = 0U;
    require(registry.restore(without, errors) && cleared == 1 && stored == 7U,
        "an image without the optional section clears it and is accepted");
    require(registry.restore(image, errors) && cleared == 1,
        "an image with the section reads it instead of clearing");

    // The image bytes of a section are the same each time it is saved.
    RunStateImage again { "design" };
    in_use = true;
    require(registry.save(again, errors) && registry.save(image, errors)
            && again.state_hash() == image.state_hash(),
        "saving twice from one state gives one hash");

    RunStateSectionRegistry refusing;
    refusing.add({ .name = "app.chandle", .version = 1U,
        .present = [] { return true; },
        .refuse = [] { return std::string { "a live chandle" }; } });
    RunStateImage refused { "design" };
    errors.clear();
    require(!refusing.save(refused, errors) && errors.size() == 1U
            && errors.front().starts_with("FSIM-SAVE-0021: app.chandle"),
        "a refusing section refuses the save with FSIM-SAVE-0021");
    // Nothing refuses while the subsystem is absent.
    RunStateSectionRegistry idle;
    idle.add({ .name = "app.chandle", .version = 1U,
        .present = [] { return false; },
        .refuse = [] { return std::string { "never asked" }; } });
    errors.clear();
    require(idle.save(refused, errors) && errors.empty(),
        "an absent section is not asked to refuse");
}

[[nodiscard]] SystemVerilogClassDescriptor class_descriptor()
{
    SystemVerilogClassDescriptor result;
    result.declared_type = "work::Base";
    result.dynamic_type = "work::Derived";
    result.specialization_identity = "work::Derived<WIDTH=8>";
    result.assignable_declared_types = { "work::Derived", "work::Base" };
    result.properties = {
        { "four_state", SystemVerilogClassPropertyKind::Logic4, 70 },
        { "label", SystemVerilogClassPropertyKind::String, 0 },
        { "next", SystemVerilogClassPropertyKind::ClassHandle, 64 } };
    return result;
}

[[nodiscard]] std::string heap_bytes(SystemVerilogClassHeap& heap)
{
    std::string bytes;
    auto archive = RunStateArchive::writer(bytes);
    heap.run_state_io(archive);
    require(archive.ok(), "a class heap is written");
    return bytes;
}

// The class heap keeps slot and generation: a handle taken before the save
// names the same object after the restore, a released handle stays stale, the
// next allocation reuses the freed slot with a new generation, and a heap of
// another simulation seed refuses the section (IEEE 1800-2023 D.9).
void test_class_heap_run_state()
{
    SystemVerilogClassHeap heap;
    const auto first = heap.allocate(class_descriptor());
    const auto second = heap.allocate(class_descriptor());
    heap.property(first, "four_state").packed = PackedLogic4::from_msb_string(
        "1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x0z1x");
    heap.property(first, "label").string = "first";
    heap.property(first, "next").handle = second;
    heap.property(second, "label").string = "second";
    require(heap.release(second), "the second object is released");
    const auto third = heap.allocate(class_descriptor());
    require(third != second, "a released handle does not come back as itself");

    const auto bytes = heap_bytes(heap);
    require(bytes == heap_bytes(heap), "saving the class heap twice gives equal bytes");

    SystemVerilogClassHeap restored;
    auto reader = RunStateArchive::reader(bytes);
    restored.run_state_io(reader);
    require(reader.finished(), "the class heap section restores");
    require(restored.contains(first) && restored.contains(third)
            && !restored.contains(second),
        "the restored heap holds the saved handles");
    require(restored.property(first, "label").string == "first"
            && restored.property(first, "next").handle == second
            && restored.property(first, "four_state").packed
                == heap.property(first, "four_state").packed
            && restored.live_objects() == heap.live_objects()
            && restored.storage_bytes() == heap.storage_bytes(),
        "a restored object equals the saved one");
    require(heap_bytes(restored) == bytes, "the restored heap saves the same bytes");
    // The freed slot is reused by both heaps with the same new generation.
    require(restored.allocate(class_descriptor()) == heap.allocate(class_descriptor()),
        "the restored heap allocates as the saved one would");

    SystemVerilogClassHeap other_seed { { }, 99U };
    auto other_reader = RunStateArchive::reader(bytes);
    other_seed.run_state_io(other_reader);
    require(!other_reader.ok(), "a class heap refuses the state of another seed");
}

// The design of CounterRun, added to any interpreter: the reset source builds
// the twin with it.
void populate_counter(Interpreter& interpreter, SignalId& counter, SignalId& mirror)
{
    // Two processes. `ticker` adds one to `counter` every 5 ticks.
    // `follower` copies `counter` to `mirror` whenever it changes.
    counter = interpreter.add_signal(
        { "top.counter", PackedLogic4::from_aval_bval(32, 0, 0) });
    mirror = interpreter.add_signal(
        { "top.mirror", PackedLogic4::from_aval_bval(32, 0, 0) });
    Process ticker;
    ticker.id = 0;
    ticker.name = "ticker";
    ticker.register_count = 3;
    ticker.operations = {
        LoadConstant { 1, PackedLogic4::from_aval_bval(32, 1, 0) },
        WaitFor { 5 },
        ReadSignal { 0, counter },
        Binary { BinaryOperator::add_unsigned, 2, 0, 1 },
        WriteBlocking { counter, 2 },
        Jump { 1 },
    };
    (void)interpreter.add_process(std::move(ticker));
    Process follower;
    follower.id = 1;
    follower.name = "follower";
    follower.register_count = 1;
    follower.static_sensitivity = { { counter, EdgeKind::any } };
    follower.operations = {
        WaitSensitivity { },
        ReadSignal { 0, counter },
        WriteBlocking { mirror, 0 },
        Jump { 0 },
    };
    (void)interpreter.add_process(std::move(follower));
}

[[nodiscard]] std::unique_ptr<Interpreter> make_counter_interpreter()
{
    auto interpreter = std::make_unique<Interpreter>();
    SignalId counter { };
    SignalId mirror { };
    populate_counter(*interpreter, counter, mirror);
    return interpreter;
}

struct CounterRun {
    Interpreter interpreter;
    SignalId counter { };
    SignalId mirror { };

    CounterRun() { populate_counter(interpreter, counter, mirror); }

    [[nodiscard]] std::uint64_t value(const SignalId signal) const
    {
        const auto known = interpreter.signal_value(signal).known_unsigned_value();
        require(known.has_value(), "a counter value is known");
        return *known;
    }
};

// Save the reference interpreter in the middle of a run, restore into a
// freshly built and started one, and check the hash, the bytes of a second
// save, and that both continue to the same state.
void test_interpreter_round_trip()
{
    CounterRun source;
    // The ticker adds one at 5 and at 10 (and the next at 15), so after the
    // run to 12 the counter holds 2 (unverified: that run(12) stops at 12 with
    // the scheduler quiescent, which record_run_state reports if it is not).
    static_cast<void>(source.interpreter.run(12));
    require(source.value(source.counter) == 2U && source.value(source.mirror) == 2U,
        "two ticks passed by time 12");

    RunStateImage first { "run-state-test" };
    RunStateReport report;
    const bool saved = source.interpreter.record_run_state(first, report);
    if (!saved) {
        std::string why;
        for (const auto& message : report.messages) {
            why += message + "\n";
        }
        throw std::runtime_error { "the run did not save:\n" + why };
    }
    require(first.find("sched.state") != nullptr && first.find("host.run") != nullptr
            && first.find("host.signals") != nullptr
            && first.find("host.processes.shared") != nullptr
            && first.find("host.processes.000000") != nullptr,
        "the engine sections are in the image");
    RunStateImage again { "run-state-test" };
    require(source.interpreter.record_run_state(again, report),
        "a second save works");
    require(first.serialize() == again.serialize(),
        "saving twice from one point gives identical bytes");
    const auto hash = source.interpreter.run_state_hash();
    require(hash != 0U, "the state hash is available");

    // Through the file form.
    RunStateImageError error { };
    const auto parsed = RunStateImage::parse(first.serialize(), error);
    require(parsed.has_value() && parsed->state_hash() == first.state_hash(),
        "the image round-trips through its byte form");

    CounterRun restored;
    restored.interpreter.start();
    RunStateReport restore_report;
    if (!restored.interpreter.restore_run_state(*parsed, restore_report)) {
        std::string why;
        for (const auto& message : restore_report.messages) {
            why += message + "\n";
        }
        throw std::runtime_error { "the image did not restore:\n" + why };
    }
    require(restored.interpreter.run_state_hash() == hash,
        "the restored run has the saved state's hash");
    require(restored.value(restored.counter) == 2U
            && restored.value(restored.mirror) == 2U,
        "the restored signals hold the saved values");

    // Both runs reach the same state: the ticks at 15, 20, 25 and 30 follow
    // (unverified: inclusive end of run(30)).
    static_cast<void>(source.interpreter.run(30));
    static_cast<void>(restored.interpreter.run(30));
    require(source.value(source.counter) == restored.value(restored.counter)
            && source.value(source.mirror) == restored.value(restored.mirror)
            && source.value(source.counter) >= 5U
            && source.value(source.counter) <= 6U,
        "the restored run continues as the original does");
    require(restored.interpreter.run_state_hash()
            == source.interpreter.run_state_hash(),
        "the two runs still have equal state hashes");

    // An image of another section version is refused.
    RunStateImage old_version { "run-state-test" };
    for (const auto& section : first.sections()) {
        old_version.put(section.name,
            section.name == "host.run" ? section.version + 1U : section.version,
            section.bytes);
    }
    CounterRun stranger;
    stranger.interpreter.start();
    RunStateReport refusal;
    require(!stranger.interpreter.restore_run_state(old_version, refusal)
            && !refusal.messages.empty()
            && refusal.messages.front().starts_with("FSIM-SAVE-0009"),
        "a section of another version is refused with FSIM-SAVE-0009");

    // An image saved before the attempt block's layout 4 (host version 2 of
    // the host sections) is refused up front by the version, not at the
    // assertion block.
    RunStateImage layout_three { "run-state-test" };
    for (const auto& section : first.sections()) {
        layout_three.put(section.name,
            section.name.starts_with("host.") ? 2U : section.version, section.bytes);
    }
    CounterRun older;
    older.interpreter.start();
    RunStateReport older_refusal;
    require(!older.interpreter.restore_run_state(layout_three, older_refusal)
            && !older_refusal.messages.empty()
            && older_refusal.messages.front().starts_with("FSIM-SAVE-0009"),
        "an image of the previous host section version is refused with FSIM-SAVE-0009");
}


// A reset puts the run back at its time-zero state through the restart path
// (IEEE 1800-2023 D.8): the counters read zero, the scheduler is at time zero,
// $reset_count and $reset_value survive, and the run then goes the way a fresh
// start goes. Without a reset source the run cannot reset.
void test_reset()
{
    CounterRun plain;
    plain.interpreter.start();
    require(!plain.interpreter.can_reset_run_state()
            && !plain.interpreter.reset_run_state(),
        "a run without a reset source cannot reset");

    CounterRun run;
    run.interpreter.set_reset_source([] { return make_counter_interpreter(); });
    require(run.interpreter.can_reset_run_state(),
        "a reset source makes the run resettable");
    run.interpreter.start();
    static_cast<void>(run.interpreter.run(12));
    require(run.value(run.counter) == 2U, "two ticks passed by time 12");
    run.interpreter.set_reset_values(3, 42);
    require(run.interpreter.reset_run_state(), "the run resets");
    require(run.value(run.counter) == 0U && run.value(run.mirror) == 0U,
        "the signals are back at their initial values");
    require(run.interpreter.scheduler().now() == 0U, "time is back at zero");

    // The reset run is the state of a fresh start that carries the same
    // $reset_count and $reset_value (unverified: the hash covers every
    // section, so it also needs equal scheduler sequence numbers).
    CounterRun fresh;
    fresh.interpreter.start();
    fresh.interpreter.set_reset_values(3, 42);
    require(run.interpreter.run_state_hash() == fresh.interpreter.run_state_hash(),
        "a reset run equals a fresh start");
    static_cast<void>(run.interpreter.run(12));
    static_cast<void>(fresh.interpreter.run(12));
    require(run.value(run.counter) == fresh.value(fresh.counter)
            && run.value(run.counter) == 2U,
        "the reset run repeats the first life");
}

// The overflow and enable flags of the code coverage counters are part of the
// image: a counter that saturated stays flagged, one switched off stays off.
void test_coverage_flags()
{
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    CodeCoverageCounters source;
    source.reset({ maximum, 0U, 5U });
    require(source.record({ 0U }) == CodeCoverageCounterUpdate::FirstOverflow,
        "the saturated counter overflows");
    const ::fsim::runtime::CodeCoverageCounterId off[] = { { 2U } };
    require(source.set_enabled(off, false), "a counter switches off");

    CodeCoverageCounters target;
    target.reset({ maximum, 0U, 5U });
    require(target.restore_flags(source.overflow_flags(), source.enabled_flags()),
        "the flags restore");
    require(target.overflowed({ 0U }) && !target.overflowed({ 1U })
            && target.overflow_count() == 1U && !target.enabled(),
        "overflow and enable flags come back");
    const std::uint8_t short_table[] = { 0U };
    require(!target.restore_flags(short_table, short_table),
        "flags of another table size are refused");
}

// An executor that keeps its own state refuses the save with FSIM-SAVE-0007
// and a clause; a state-free one does not (IEEE 1800-2023 D.9).
class StatefulExecutor final : public ProcessExecutor {
public:
    explicit StatefulExecutor(const bool state_free)
        : state_free_ { state_free }
    {
    }
    [[nodiscard]] bool run_state_free() const noexcept override
    {
        return state_free_;
    }
    ProcessResumeResult resume(ProcessExecutionContext&, InstructionIndex) override
    {
        return ProcessResumeResult { 0U, 1U };
    }

private:
    bool state_free_ { };
};

void test_executor_refusal()
{
    CounterRun stateful;
    stateful.interpreter.set_process_executor(
        0U, std::make_unique<StatefulExecutor>(false));
    stateful.interpreter.start();
    RunStateImage image { "executor" };
    RunStateReport report;
    require(!stateful.interpreter.record_run_state(image, report),
        "a process with its own executor state refuses the save");
    bool found = false;
    for (const auto& message : report.messages) {
        found = found
            || (message.starts_with("FSIM-SAVE-0007")
                && message.find("IEEE 1800-2023 D.9") != std::string::npos);
    }
    require(found, "the refusal is FSIM-SAVE-0007 and cites D.9");
}


// ---------------------------------------------------------------------------
// The rest state of the scratch members (#185). A save is taken while every
// kind of table is filled: a delayed nonblocking update, fork children, a
// process waiting on a signal, a VHDL driver with a projected waveform and a
// timer. Each save is at a time-slot end, where the audit finds the step
// scratch at rest (FSIM-SAVE-0011 otherwise), and the restored twin continues
// to the same state as the original.

[[nodiscard]] PackedLogic4 word32(const std::uint64_t value)
{
    return PackedLogic4::from_aval_bval(32, value, 0);
}

struct ActivityRun {
    Interpreter interpreter;
    SignalId delayed { };
    SignalId joined { };
    SignalId waited { };
    SignalId projected { };

    ActivityRun()
    {
        delayed = interpreter.add_signal({ "top.delayed", word32(0) });
        joined = interpreter.add_signal({ "top.joined", word32(0) });
        waited = interpreter.add_signal({ "top.waited", word32(0) });
        projected = interpreter.add_signal({ "top.projected", word32(0) });
        // A nonblocking update 25 ticks ahead: the pending event of the
        // scheduler's NBA region at the save point (time 12).
        Process nba;
        nba.id = 0;
        nba.name = "nba";
        nba.register_count = 1;
        // An update tagged for the NBA region comes from a SystemVerilog
        // process (IEEE 1800-2023 4.4.2.6).
        nba.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        nba.operations = {
            LoadConstant { 0, word32(5) },
            WriteAfter { delayed, 0, 25, SignalUpdateDomain::systemverilog_nba },
            Halt { },
        };
        (void)interpreter.add_process(std::move(nba));
        // Two fork children, finishing at 10 and at 20: at 12 one child is
        // live and the parent waits for the join (join all).
        Process fork;
        fork.id = 1;
        fork.name = "fork";
        fork.register_count = 1;
        fork.operations = {
            LoadConstant { 0, word32(9) },
            Fork { { 4, 7 }, ForkJoinKind::all },
            WriteBlocking { joined, 0 },
            Halt { },
            WaitFor { 10 },
            LoadConstant { 0, word32(9) },
            ForkEnd { },
            WaitFor { 20 },
            LoadConstant { 0, word32(9) },
            ForkEnd { },
        };
        (void)interpreter.add_process(std::move(fork));
        // A process waiting on `delayed`; it copies the value to `waited`
        // when the nonblocking update lands at 25.
        Process waiter;
        waiter.id = 2;
        waiter.name = "waiter";
        waiter.register_count = 1;
        waiter.static_sensitivity = { { delayed, EdgeKind::any } };
        waiter.operations = {
            WaitSensitivity { },
            ReadSignal { 0, delayed },
            WriteBlocking { waited, 0 },
            Jump { 0 },
        };
        (void)interpreter.add_process(std::move(waiter));
        // A VHDL driver whose projected waveform holds two transactions, at
        // 15 and at 30 (IEEE 1076-2019 10.5.2.2).
        Process driver;
        driver.id = 3;
        driver.name = "driver";
        driver.register_count = 2;
        driver.operations = {
            LoadConstant { 0, word32(3) },
            LoadConstant { 1, word32(6) },
            WriteProjectedWaveform { projected,
                { ProjectedWaveformElement { 0, 15, std::nullopt, 0U },
                    ProjectedWaveformElement { 1, 30, std::nullopt, 0U } },
                0, ProjectedDelayMode::transport },
            Halt { },
        };
        (void)interpreter.add_process(std::move(driver));
    }

    [[nodiscard]] std::uint64_t value(const SignalId signal) const
    {
        const auto known = interpreter.signal_value(signal).known_unsigned_value();
        require(known.has_value(), "an activity value is known");
        return *known;
    }
};

void test_activity_tables_round_trip()
{
    ActivityRun source;
    source.interpreter.start();
    // Unverified: that run(12) stops with the scheduler at a save point and
    // that every kind of pending event above is a registered data kind (a
    // refusal here names the unregistered one in FSIM-SAVE-0004).
    static_cast<void>(source.interpreter.run(12));
    RunStateImage image { "activity" };
    RunStateReport report;
    if (!source.interpreter.record_run_state(image, report)) {
        std::string why;
        for (const auto& message : report.messages) {
            why += message + "\n";
        }
        throw std::runtime_error { "the busy run did not save:\n" + why };
    }
    for (const auto& message : report.messages) {
        require(!message.starts_with("FSIM-SAVE-0011"),
            "no scratch member is left busy at the save point");
    }

    ActivityRun restored;
    restored.interpreter.start();
    RunStateReport restore_report;
    if (!restored.interpreter.restore_run_state(image, restore_report)) {
        std::string why;
        for (const auto& message : restore_report.messages) {
            why += message + "\n";
        }
        throw std::runtime_error { "the busy image did not restore:\n" + why };
    }
    require(restored.interpreter.run_state_hash()
            == source.interpreter.run_state_hash(),
        "the restored busy run has the saved state's hash");

    static_cast<void>(source.interpreter.run(40));
    static_cast<void>(restored.interpreter.run(40));
    // The nonblocking update lands at 25, the join at 20, the projected
    // waveform ends at 30 (unverified: that each fires by 40).
    require(source.value(source.delayed) == 5U
            && source.value(source.joined) == 9U
            && source.value(source.waited) == 5U
            && source.value(source.projected) == 6U,
        "the original run completes the pending work");
    require(restored.value(restored.delayed) == source.value(source.delayed)
            && restored.value(restored.joined) == source.value(source.joined)
            && restored.value(restored.waited) == source.value(source.waited)
            && restored.value(restored.projected) == source.value(source.projected),
        "the restored run completes the pending work the same way");
    require(restored.interpreter.run_state_hash()
            == source.interpreter.run_state_hash(),
        "the two runs end in equal states");
}

// The scheduler's own rest state: a save requested for the end of a time slot
// is taken in a callback of the run (in_run and in_callback), finds no batch,
// reservation, ticket or position left over (FSIM-SAVE-0011 otherwise), and
// an idle scheduler saves too. A save from inside an event is not at a save
// point (FSIM-SAVE-0001).
struct SchedulerSaveProbe {
    bool ran { };
    bool ok { };
    std::vector<std::string> refusals;
};

void probe_save(void* context, Scheduler& scheduler)
{
    auto& probe = *static_cast<SchedulerSaveProbe*>(context);
    const auto saved = scheduler.save_future(SchedulerEventOwners { });
    probe.ran = true;
    probe.ok = saved.ok;
    probe.refusals = saved.refusals;
}

void test_scheduler_rest_state()
{
    Scheduler idle;
    require(idle.save_future(SchedulerEventOwners { }).ok,
        "an idle scheduler saves");

    Scheduler scheduler;
    int ran = 0;
    SchedulerSaveProbe probe;
    scheduler.schedule_at(5U, SchedulerPhase::active, 0U,
        Scheduler::Task { [&ran](Scheduler& self) {
            ++ran;
            // From inside an event the scheduler is mid-phase.
            const auto inside = self.save_future(SchedulerEventOwners { });
            require(!inside.ok && !inside.refusals.empty()
                    && inside.refusals.front().starts_with("FSIM-SAVE-0001"),
                "a save from inside an event is refused with FSIM-SAVE-0001");
        } });
    scheduler.request_quiet_point(
        SchedulerQuietPoint::slot_end, &probe, probe_save);
    static_cast<void>(scheduler.run());
    require(ran == 1 && probe.ran, "the event and the slot-end request ran");
    // Unverified: that no registered event is pending after the closure ran
    // (the closure is consumed), so the slot-end save finds nothing to refuse.
    std::string why;
    for (const auto& refusal : probe.refusals) {
        why += refusal + "\n";
    }
    require(probe.ok, why.c_str());
}


struct UvmServices {
    SystemVerilogClassHeap heap { { 64, 1'024 } };
    SystemVerilogUvmObjectService objects { heap,
        [](const std::string_view, const std::string_view,
            const std::string_view) { return SystemVerilogClassHandle { }; } };
    SystemVerilogUvmComponentService components { heap, objects };
    SystemVerilogUvmPhaseService phases { components };
};

// The archive reads and writes a variant by its index, and the UVM services
// (#188) write their state with handles that a restore binds to the services
// of the restoring run: a second set of services that took the same setup
// holds the saved roots, domains and phases, saves the same bytes, and a
// reset returns it to the standard schedule and no root.
void test_uvm_service_run_state()
{
    using Value = std::variant<std::string, double, std::uint64_t>;
    std::vector<Value> values { std::string { "text" }, 2.5, std::uint64_t { 9 } };
    std::string value_bytes;
    auto value_writer = RunStateArchive::writer(value_bytes);
    value_writer.io(values);
    require(value_writer.ok(), "a variant is written");
    std::vector<Value> loaded_values;
    auto value_reader = RunStateArchive::reader(value_bytes);
    value_reader.io(loaded_values);
    require(value_reader.finished() && loaded_values == values,
        "a variant round-trips");
    std::string bad_bytes;
    auto bad_writer = RunStateArchive::writer(bad_bytes);
    std::uint64_t one = 1U;
    std::uint32_t bad_index = 7U;
    bad_writer.io(one);
    bad_writer.io(bad_index);
    std::vector<Value> bad_values;
    auto bad_reader = RunStateArchive::reader(bad_bytes);
    bad_reader.io(bad_values);
    require(!bad_reader.ok(), "a variant index past the alternatives is refused");

    UvmServices saved;
    const auto root = saved.components.create_root("top");
    static_cast<void>(saved.phases.create_standard_schedule(
        std::array<SystemVerilogUvmRootHandle, 1U> { root }));
    const auto extra = saved.phases.create_domain(
        "extra", SystemVerilogUvmDomainKind::Custom);
    static_cast<void>(saved.phases.create_custom_phase(
        extra, "custom", SystemVerilogUvmPhaseExecutionKind::Task));
    const auto write = [](UvmServices& services) {
        std::string bytes;
        auto writer = RunStateArchive::writer(bytes);
        const SystemVerilogUvmRunStateOwners owners {
            services.phases.run_state_owner(), nullptr
        };
        writer.set_context(&owners);
        writer.io(services.components);
        writer.io(services.phases);
        require(writer.ok(), "the UVM services are written");
        return bytes;
    };
    const auto bytes = write(saved);

    // The setup every run makes before a restore replaces its state.
    UvmServices restored;
    static_cast<void>(restored.phases.create_standard_schedule());
    {
        auto reader = RunStateArchive::reader(bytes);
        const SystemVerilogUvmRunStateOwners owners {
            restored.phases.run_state_owner(), nullptr
        };
        reader.set_context(&owners);
        reader.io(restored.components);
        reader.io(restored.phases);
        require(reader.finished(), "the UVM services are read back whole");
    }
    require(restored.components.roots() == saved.components.roots()
            && restored.components.root_identity(root) == "top",
        "the root comes back");
    require(restored.phases.standard_schedule().has_value(),
        "the standard schedule comes back");
    const auto run = restored.phases.standard_schedule()->phase(
        SystemVerilogUvmPhaseKind::Run);
    require(restored.phases.contains(run)
            && restored.phases.snapshot(run).identity
                == systemverilog_uvm_phase_identity(SystemVerilogUvmPhaseKind::Run),
        "a restored phase handle is bound to the restoring service");
    require(!saved.phases.contains(run),
        "a handle of the saving run is not a handle of the restored one");
    bool found_custom = false;
    for (const auto& domain : restored.phases.domains()) {
        const auto snapshot = restored.phases.snapshot(domain);
        if (snapshot.identity == "extra") {
            found_custom = snapshot.phases.size() == 1U
                && restored.phases.snapshot(snapshot.phases.front()).identity
                    == "custom";
        }
    }
    require(found_custom, "a custom domain and its phase come back");
    require(write(restored) == bytes, "a restored state saves the same bytes");

    restored.phases.reset_run_state();
    restored.components.reset_run_state();
    require(restored.components.roots().empty()
            && restored.phases.domains().size() == 2U
            && restored.phases.standard_schedule().has_value(),
        "a reset returns to the standard schedule and no root");
}

}  // namespace

void test_run_state()
{
    test_archive_round_trip_and_determinism();
    test_section_registry();
    test_optional_section_owners();
    test_class_heap_run_state();
    test_interpreter_round_trip();
    test_reset();
    test_coverage_flags();
    test_executor_refusal();
    test_activity_tables_round_trip();
    test_scheduler_rest_state();
    test_uvm_service_run_state();
}

}  // namespace fsim::tests::runtime
