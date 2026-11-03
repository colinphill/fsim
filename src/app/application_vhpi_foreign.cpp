// SPDX-License-Identifier: Apache-2.0
#include "application_vhpi_foreign.hpp"

#include "application_foreign_signal.hpp"
#include "application_internal.hpp"
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/vhpi_bridge.h"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/vhdl_identifier.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::app::application_detail {

namespace {

using runtime::Logic4;
using runtime::Logic9;
using runtime::PackedLogic4;
using runtime::simir::SignalId;
namespace vhdl = semantic::vhdl;

constexpr std::size_t maximum_startup_routines = 4096U;
constexpr std::size_t maximum_list_size = 1U << 20U;

[[nodiscard]] std::string upper(std::string_view text)
{
    std::string result { text };
    // An extended identifier keeps its case (IEEE 1076-2019 15.4.3).
    if (!result.empty() && result.front() == '\\') {
        return result;
    }
    for (auto& character : result) {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    }
    return result;
}

[[nodiscard]] std::string lower(std::string_view text)
{
    std::string result { text };
    if (!result.empty() && result.front() == '\\') {
        return result;
    }
    for (auto& character : result) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

[[nodiscard]] bool same_name(std::string_view left, std::string_view right)
{
    // An extended identifier is case sensitive (IEEE 1076-2019 15.4.3); a
    // basic identifier is not (15.4.2).
    if ((!left.empty() && left.front() == '\\')
        || (!right.empty() && right.front() == '\\')) {
        return left == right;
    }
    return left.size() == right.size()
        && std::ranges::equal(left, right, [](const char a, const char b) {
               return std::tolower(static_cast<unsigned char>(a))
                   == std::tolower(static_cast<unsigned char>(b));
           });
}

// --- the type model --------------------------------------------------------

// How a value of a type is laid out in a packed signal value: the leftmost
// array element and the first record element are the most significant.
enum class Storage : std::uint8_t {
    integer,
    enumeration,
    logic9,
    real,
    physical,
    array,
    record,
    other,
};

struct Range {
    std::int64_t left { };
    std::int64_t right { };
    bool descending { };

    friend bool operator==(const Range&, const Range&) = default;

    [[nodiscard]] std::int64_t length() const noexcept
    {
        const auto count = descending ? left - right + 1 : right - left + 1;
        return count < 0 ? 0 : count;
    }
    // The position (0 = leftmost) of an index value, if it is in range.
    [[nodiscard]] std::optional<std::int64_t> position(
        const std::int64_t index) const noexcept
    {
        const auto offset = descending ? left - index : index - left;
        if (offset < 0 || offset >= length()) {
            return std::nullopt;
        }
        return offset;
    }
    [[nodiscard]] std::int64_t index(const std::int64_t position) const noexcept
    {
        return descending ? left - position : left + position;
    }
};

struct Object;

struct Field {
    std::string name;
    std::string case_name;
    Object* type { };
};

struct PhysicalUnit {
    std::string name;
    std::int64_t scale { 1 };
};

// The type-specific part of a type or subtype declaration object.
struct TypeData {
    Storage storage { Storage::other };
    std::size_t width { };
    // Scalar types: the range; enumeration types: the literals.
    std::optional<Range> range;
    std::vector<std::string> literals;
    std::vector<PhysicalUnit> units;
    // Array types: one range per dimension, unconstrained when empty.
    std::vector<Range> dimensions;
    std::vector<Object*> index_types;
    std::size_t dimension_count { };
    bool unconstrained { };
    Object* element { };
    std::vector<Field> fields;
    // The base type, or the type itself.
    Object* base { };
    // The type mark a subtype names.
    Object* parent { };
    // Access and file types: the subtype of their values.
    const vhdl::SubtypeIndication* designated { };
    // The library and package the declaration is in, for full names.
    std::string library;
    std::string package;
};

// --- handles ---------------------------------------------------------------

enum class HandleTag : std::uint8_t {
    object,
    callback,
    iterator,
    foreign,
    // A removed callback's slot until it is reused (IEEE 1076-2019 21.2.4).
    removed,
};

struct Handle {
    explicit Handle(const HandleTag value) noexcept
        : tag { value }
    {
    }
    virtual ~Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HandleTag tag;
};

enum class ListKind : std::uint8_t {
    decls,
    selected_names,
    indexed_names,
    constraints,
    record_elems,
    enum_literals,
    unit_decls,
    internal_regions,
    pack_insts,
    argvs,
    param_decls,
    // Processes and statements (IEEE 1076-2019 19.11).
    statements,
    branches,
    cond_waveforms,
    select_waveforms,
    waveform_elems,
    choices,
    // Connectivity (IEEE 1076-2019 19.12).
    drivers,
    port_assocs,
    // Expressions and collections (IEEE 1076-2019 19.8, 19.15).
    param_assocs,
    elem_assocs,
    members,
};

struct Object final : Handle {
    Object()
        : Handle { HandleTag::object }
    {
    }

    vhpiClassKindT kind { vhpiRootInstK };
    std::string name;
    std::string case_name;
    std::string full_name;
    std::string full_case_name;
    // The region that declares the object, or the prefix of a name.
    Object* parent { };

    // Regions: the design hierarchy path and the declarations in scope.
    std::string path;
    const vhdl::Unit* unit { };
    const vhdl::Unit* entity { };
    std::vector<const vhdl::Declaration*> region_declarations;
    std::vector<Object*> design_units;
    // Instances: the generic values elaboration gave them.
    const semantic::design::Specialization* specialization { };
    // Block and generate statements: the statement, and a for-generate
    // occurrence's parameter value (none for the statement itself).
    const vhdl::GenerateRegion* generate { };
    std::optional<std::int64_t> generate_index;

    // Value objects and the names that select their elements: the signal,
    // the bits of this element (offset of the least significant bit), and
    // the subtype.
    const vhdl::Declaration* declaration { };
    std::optional<SignalId> signal;
    std::size_t offset { };
    Object* type { };
    // A constant or generic with a known value.
    std::optional<PackedLogic4> constant;
    // Indexed names: the index values; selected names: the element
    // declaration (vhpiSuffix).
    std::vector<std::int64_t> indexes;
    Object* suffix { };
    // Enumeration literals and physical units: their position or scale.
    std::int64_t position { };
    // Where a component instantiation statement is (vhpiFileNameP,
    // vhpiLineNoP); other objects take it from their declaration,
    // statement or design unit.
    std::optional<semantic::SourceSpanId> source;
    // A formal of a foreign subprogram call: its actual's position in the
    // running call; the call itself carries a function's result.
    std::optional<std::size_t> call_slot;
    bool call_result { };
    // An alias declaration: the object it is an alternate name for
    // (vhpiAliasedName, IEEE 1076-2019 19.4.3).
    Object* aliased { };
    // A driver: the basic signal it drives (IEEE 1076-2019 19.12.1); its
    // parent is the process or concurrent statement that holds it.
    Object* basic { };
    // A component instantiation: its elaborated occurrence, whose HIR
    // instance holds the port map (IEEE 1076-2019 19.10).
    const semantic::design::InstanceOccurrence* occurrence { };
    // A port association element: the association of the port map.
    const vhdl::Association* port_association { };

    // Processes and statements (IEEE 1076-2019 19.11): the process, or the
    // statement (for a branch, waveform or alternative, the statement it is
    // part of), the case alternative, the statements the object contains
    // (vhpiSeqStmts), the condition of a branch or conditional waveform and
    // the index of a branch or waveform element.
    const vhdl::Process* process { };
    const vhdl::Statement* statement { };
    const vhdl::CaseAlternative* alternative { };
    const std::vector<semantic::StatementId>* body { };
    std::optional<semantic::ExpressionId> condition;
    std::size_t part { };
    // A process or statement (class stmt), and whether it is sequential.
    bool is_stmt { };
    bool sequential { };
    // The equivalent process of a concurrent statement (IEEE 1076-2019
    // 11.4 to 11.7, Figure 38).
    bool equivalent { };
    // Expressions (IEEE 1076-2019 19.8): the expression of a function call
    // or aggregate, the element association of an aggregate, and for an
    // association element its actual and whether it is named.
    const vhdl::Expression* expression { };
    const vhdl::AggregateAssociation* association { };
    std::optional<semantic::ExpressionId> actual;
    bool named { };
    // The explicit label (vhpiLabelNameP); the implicit one is in `name`.
    std::string label_name;
    // A variable of a process (IEEE 1076-2019 22.5.2): the design process
    // that owns it and its debug-local index, found on first use.
    mutable std::optional<std::pair<runtime::simir::ProcessId, std::size_t>> local;
    mutable bool local_resolved { };

    // Type declarations.
    std::unique_ptr<TypeData> type_data;

    // Lazily built one-to-many associations.
    std::map<ListKind, std::vector<Object*>> lists;
    std::map<std::string, Object*, std::less<>> children;
};

// A process or concurrent statement of a region, in textual order.
struct StatementMember {
    const vhdl::Process* process { };
    const vhdl::Statement* statement { };
};

// One branch of an if statement, or one conditional waveform: the node that
// holds the condition (or the assignment of a conditional waveform), the
// condition (none for an else branch) and the statements of the branch.
struct BranchPart {
    const vhdl::Statement* node { };
    std::optional<semantic::ExpressionId> condition;
    const std::vector<semantic::StatementId>* body { };
};

struct Callback final : Handle {
    Callback()
        : Handle { HandleTag::callback }
    {
    }

    vhpiCbDataT data { };
    vhpiTimeT time { };
    vhpiValueT value { };
    bool has_time { };
    bool has_value { };
    vhpiStateT state { vhpiEnable };
    // Repetitive callbacks stay registered after they run.
    bool repetitive { };
    // AfterDelay: the registration on the simulation's timed callbacks.
    std::optional<runtime::SystemVerilogVpiCallbackHandle> managed;
    std::uint64_t delay { };
    // ValueChange and Sensitivity: the bits last seen.
    Object* object { };
    std::optional<PackedLogic4> last;
    // Sensitivity: an event since the cycle's processes last started.
    bool event { };
    // Sensitivity on an anyCollection (IEEE 1076-2019 21.3.3.3): the
    // signals of the sensitivity set, the bits last seen of each, the
    // events since the processes last started as a bitmap (bit k of
    // element k / 32 for member k) and the bitmap the callback receives.
    std::vector<Object*> members;
    std::vector<std::optional<PackedLogic4>> member_last;
    std::vector<vhpiIntT> bitmap;
    std::vector<vhpiIntT> delivered;
    // Statement callbacks (IEEE 1076-2019 21.3.4): the process they watch;
    // for vhpiCbStmt the statement whose execution point triggers it, where
    // it begins, and the object the callback reports.
    Object* process { };
    Object* trigger { };
    Object* reported { };
    std::uint32_t line { };
    std::uint32_t column { };
    // A loop placed on itself: where its later passes report.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> pass;
    std::string file;
    std::uint64_t order { };
    // In `callbacks_`: neither mature nor removed.
    bool registered { };
};

struct Iterator final : Handle {
    Iterator()
        : Handle { HandleTag::iterator }
    {
    }

    std::vector<Object*> items;
    std::size_t next { };
};

// A registered foreign subprogram model (IEEE 1076-2019 23.29
// vhpi_register_foreignf); the names are owned copies.
struct ForeignModel final : Handle {
    ForeignModel()
        : Handle { HandleTag::foreign }
    {
    }

    vhpiForeignDataT data { };
    std::string library;
    std::string model;
};

[[nodiscard]] Handle* handle_of(const vhpiHandleT handle) noexcept
{
    return reinterpret_cast<Handle*>(handle);
}

[[nodiscard]] vhpiHandleT to_handle(Handle* handle) noexcept
{
    return reinterpret_cast<vhpiHandleT>(handle);
}

struct ModuleSpec {
    std::filesystem::path path;
    std::string bootstrap;
};

// `lib[:bootstrap]`; a library named without extension may be a `.vhpi`
// or `.so` file.
[[nodiscard]] ModuleSpec parse_module(std::string_view text)
{
    ModuleSpec spec;
    const auto colon = text.rfind(':');
    if (colon != std::string_view::npos && colon != 0U
        && text.substr(colon + 1U).find('/') == std::string_view::npos) {
        spec.bootstrap = std::string { text.substr(colon + 1U) };
        text = text.substr(0U, colon);
    }
    spec.path = std::filesystem::path { std::string { text } };
    std::error_code error;
    if (!support::native_fs::exists(spec.path, error)) {
        for (const auto* extension : { ".vhpi", ".so" }) {
            auto candidate = spec.path;
            candidate += extension;
            if (support::native_fs::exists(candidate, error)) {
                spec.path = std::move(candidate);
                break;
            }
        }
    }
    if (support::native_fs::exists(spec.path, error)) {
        spec.path = std::filesystem::absolute(spec.path, error);
    }
    return spec;
}

// The femtoseconds in one simulation tick of resolution `text` ("1fs").
[[nodiscard]] std::uint64_t femtoseconds_per_tick(const std::string_view text)
{
    std::uint64_t magnitude { };
    const auto* const end = text.data() + text.size();
    const auto [unit, error] = std::from_chars(text.data(), end, magnitude);
    if (error != std::errc { } || magnitude == 0U) {
        return 1U;
    }
    const auto suffix = std::string_view { unit, end };
    constexpr std::array<std::pair<std::string_view, std::uint64_t>, 6>
        scales { { { "fs", 1U }, { "ps", 1000U }, { "ns", 1000000U },
            { "us", 1000000000U }, { "ms", 1000000000000U },
            { "s", 1000000000000000U } } };
    for (const auto& [name, scale] : scales) {
        if (suffix == name) {
            return magnitude * scale;
        }
    }
    return 1U;
}

[[nodiscard]] vhpiTimeT to_vhpi_time(const std::uint64_t femtoseconds) noexcept
{
    return vhpiTimeT { static_cast<int32_t>(femtoseconds >> 32U),
        static_cast<uint32_t>(femtoseconds) };
}

[[nodiscard]] std::uint64_t from_vhpi_time(const vhpiTimeT& time) noexcept
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(time.high))
               << 32U)
        | time.low;
}

[[nodiscard]] bool repetitive_reason(const int32_t reason) noexcept
{
    switch (reason) {
    case vhpiCbValueChange:
    case vhpiCbForce:
    case vhpiCbRelease:
    case vhpiCbTransaction:
    case vhpiCbRepEndOfProcesses:
    case vhpiCbRepLastKnownDeltaCycle:
    case vhpiCbRepNextTimeStep:
    case vhpiCbRepEndOfTimeStep:
    case vhpiCbRepStartOfNextCycle:
    case vhpiCbRepStartOfProcesses:
    case vhpiCbRepStartOfPostponed:
    case vhpiCbRepAfterDelay:
    case vhpiCbRepTimeOut:
    case vhpiCbQuiescense:
    case vhpiCbPLIError:
    case vhpiCbSigInterrupt:
    case vhpiCbStmt:
    case vhpiCbResume:
    case vhpiCbSuspend:
    case vhpiCbStartOfSubpCall:
    case vhpiCbEndOfSubpCall:
    case vhpiCbSensitivity:
        return true;
    default:
        return false;
    }
}

// The one-shot reason a repetitive one shares its dispatch point with.
[[nodiscard]] int32_t dispatch_reason(const int32_t reason) noexcept
{
    switch (reason) {
    case vhpiCbRepEndOfProcesses:
        return vhpiCbEndOfProcesses;
    case vhpiCbRepLastKnownDeltaCycle:
        return vhpiCbLastKnownDeltaCycle;
    case vhpiCbRepNextTimeStep:
        return vhpiCbNextTimeStep;
    case vhpiCbRepEndOfTimeStep:
        return vhpiCbEndOfTimeStep;
    case vhpiCbRepStartOfNextCycle:
        return vhpiCbStartOfNextCycle;
    case vhpiCbRepStartOfProcesses:
        return vhpiCbStartOfProcesses;
    case vhpiCbRepStartOfPostponed:
        return vhpiCbStartOfPostponed;
    case vhpiCbRepAfterDelay:
        return vhpiCbAfterDelay;
    case vhpiCbRepTimeOut:
        return vhpiCbTimeOut;
    default:
        return reason;
    }
}

class LiveService final : public VhpiForeignService {
public:
    LiveService(Simulation& simulation, PrintSink print,
        std::span<const std::string> arguments)
        : simulation_ { simulation }
        , hir_ { simulation.vhdl_hir() }
        , design_ { simulation.design_ir() }
        , print_ { std::move(print) }
        , femtoseconds_per_tick_ {
            femtoseconds_per_tick(simulation.time_resolution())
        }
    {
        host_.abi_version = FSIM_VHPI_STD_HOST_ABI_VERSION;
        host_.struct_size = sizeof(host_);
        host_.user_data = this;
        install_routines();
        arguments_.emplace_back("fsim");
        for (const auto& argument : arguments) {
            arguments_.push_back(argument);
        }
    }

    ~LiveService() override
    {
        for (auto* const callback : callbacks_) {
            if (callback->managed) {
                (void)simulation_.systemverilog_vpi_callbacks()
                    .remove_callback(*callback->managed);
            }
        }
        for (const auto& registration : lifecycle_) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                registration);
        }
        if (next_time_) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                *next_time_);
        }
        if (safe_point_token_ != 0U) {
            simulation_.remove_safe_point_hook(safe_point_token_);
        }
        if (change_token_ != 0U) {
            simulation_.remove_signal_change_hook(change_token_);
        }
        if (force_token_ != 0U) {
            simulation_.remove_force_statement_hook(force_token_);
        }
        if (execution_token_ != 0U) {
            simulation_.remove_execution_point_hook(execution_token_);
        }
        if (transaction_token_ != 0U) {
            simulation_.remove_signal_transaction_hook(transaction_token_);
        }
        if (driver_token_ != 0U) {
            simulation_.remove_driver_transaction_hook(driver_token_);
        }
    }

    [[nodiscard]] bool load(const std::span<const std::string> modules,
        diagnostic::Engine& diagnostics)
    {
        for (const auto& module : modules) {
            const auto spec = parse_module(module);
            std::string error;
            auto library
                = platform::DynamicLibrary::open(spec.path, error, true);
            if (!library) {
                diagnostics.error("FSIM-VHPI-LOAD-001",
                    "cannot load VHPI module '" + module + "': " + error);
                return false;
            }
            // Registration functions run before the tool starts
            // (IEEE 1076-2019 20.2.1): the named bootstrap function, or the
            // null-terminated vhpi_startup_routines table.
            Scope scope { *this };
            if (!spec.bootstrap.empty()) {
                auto* const symbol = library->symbol(spec.bootstrap, error);
                if (symbol == nullptr) {
                    diagnostics.error("FSIM-VHPI-LOAD-001",
                        "cannot load VHPI module '" + module + "': " + error);
                    return false;
                }
                reinterpret_cast<void (*)()>(symbol)();
            } else {
                auto* const table
                    = library->symbol("vhpi_startup_routines", error);
                if (table == nullptr) {
                    diagnostics.error("FSIM-VHPI-LOAD-001",
                        "cannot load VHPI module '" + module + "': " + error);
                    return false;
                }
                auto* const routines = static_cast<void (**)()>(table);
                for (std::size_t index = 0U;
                     index < maximum_startup_routines
                     && routines[index] != nullptr && !stopping_;
                     ++index) {
                    routines[index]();
                }
            }
            libraries_.push_back(std::move(library));
        }
        // Immediately prior to completion of the registration phase
        // (IEEE 1076-2019 21.3.7.2).
        dispatch(vhpiCbStartOfTool);
        return true;
    }

    // Connect the callback dispatch points to the simulation.
    void attach()
    {
        auto& manager = simulation_.systemverilog_vpi_callbacks();
        const auto lifecycle = [&](const runtime::SystemVerilogVpiCallbackKind
                                       kind,
                                   auto dispatch) {
            runtime::SystemVerilogVpiCallbackRegistration registration;
            registration.kind = kind;
            registration.callback
                = [this, dispatch](const runtime::SystemVerilogVpiCallbackEvent&) {
                      (this->*dispatch)();
                  };
            const auto registered
                = manager.register_callback(std::move(registration));
            if (registered) {
                lifecycle_.push_back(registered.value);
            }
        };
        lifecycle(runtime::SystemVerilogVpiCallbackKind::StartOfSimulation,
            &LiveService::start_of_simulation);
        lifecycle(runtime::SystemVerilogVpiCallbackKind::EndOfSimulation,
            &LiveService::end_of_simulation);
        ensure_change_hook();
        // Calls of foreign subprograms reach their execution functions
        // through the foreign call boundary; other calls pass on.
        simulation_.set_systemverilog_dpi_function_hook(
            [this, next = simulation_.systemverilog_dpi_function_hook()](
                const std::string_view identity,
                std::vector<PackedLogic4>& actuals,
                std::vector<std::string>& strings,
                const std::span<const std::string> names,
                const std::span<const std::uint8_t> directions) {
                if (identity.starts_with(
                        elaboration::vhdl_foreign_call_identity_prefix)) {
                    return foreign_call(identity.substr(
                            elaboration::vhdl_foreign_call_identity_prefix
                                .size()),
                        actuals, names, directions);
                }
                if (!next) {
                    throw std::runtime_error {
                        "SystemVerilog DPI function '"
                        + std::string { identity }
                        + "' has no loaded implementation"
                    };
                }
                return next(identity, actuals, strings, names, directions);
            });
        safe_point_token_ = simulation_.add_safe_point_hook(
            [this](runtime::Scheduler& scheduler,
                const runtime::SchedulerPhase phase) {
                safe_point(scheduler, phase);
            });
    }

    void finish() override
    {
        if (finished_) {
            return;
        }
        finished_ = true;
        if (!simulation_ended_ && !finishing_) {
            end_of_simulation();
        }
        phase_ = vhpiTerminationPhase;
        dispatch(vhpiCbEndOfTool);
    }

    [[nodiscard]] bool failed() const noexcept override { return failed_; }

private:
    // --- call context ----------------------------------------------------------

    class Scope final {
    public:
        explicit Scope(LiveService& service)
            : service_ { service }
        {
            entered_ = fsim_vhpi_std_host_enter_v1(&service.host_) == 0;
        }
        ~Scope()
        {
            if (entered_) {
                (void)fsim_vhpi_std_host_leave_v1(&service_.host_);
            }
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        LiveService& service_;
        bool entered_ { };
    };

    [[nodiscard]] static LiveService& self(void* const user_data)
    {
        return *static_cast<LiveService*>(user_data);
    }

    // Each routine clears the error state and reports through it
    // (IEEE 1076-2019 23.6 vhpi_check_error).
    template <typename Result, typename Function>
    static Result guarded(void* const user_data, const Result fallback,
        Function&& function)
    {
        auto& service = self(user_data);
        service.clear_error();
        auto result = fallback;
        try {
            result = function(service);
        } catch (const std::exception& error) {
            service.error(vhpiError, error.what());
        } catch (...) {
            service.error(vhpiInternal, "internal VHPI error");
        }
        service.pli_error();
        return result;
    }

    void install_routines()
    {
        host_.register_cb = [](void* user, vhpiCbDataT* data, int32_t flags) {
            return guarded<vhpiHandleT>(user, nullptr,
                [&](LiveService& s) { return s.register_cb(data, flags); });
        };
        host_.remove_cb = [](void* user, vhpiHandleT handle) {
            return guarded<int>(user, 1,
                [&](LiveService& s) { return s.remove_cb(handle); });
        };
        host_.disable_cb = [](void* user, vhpiHandleT handle) {
            return guarded<int>(user, 1, [&](LiveService& s) {
                return s.set_cb_state(handle, vhpiDisable);
            });
        };
        host_.enable_cb = [](void* user, vhpiHandleT handle) {
            return guarded<int>(user, 1, [&](LiveService& s) {
                return s.set_cb_state(handle, vhpiEnable);
            });
        };
        host_.get_cb_info = [](void* user, vhpiHandleT handle,
                                vhpiCbDataT* data) {
            return guarded<int>(user, 1,
                [&](LiveService& s) { return s.cb_info(handle, data); });
        };
        host_.handle_by_name = [](void* user, const char* name,
                                   vhpiHandleT scope) {
            return guarded<vhpiHandleT>(user, nullptr,
                [&](LiveService& s) { return s.handle_by_name(name, scope); });
        };
        host_.handle_by_index = [](void* user, vhpiOneToManyT relation,
                                    vhpiHandleT parent, int32_t index) {
            return guarded<vhpiHandleT>(user, nullptr, [&](LiveService& s) {
                return s.handle_by_index(relation, parent, index);
            });
        };
        host_.handle = [](void* user, vhpiOneToOneT relation,
                           vhpiHandleT reference) {
            return guarded<vhpiHandleT>(user, nullptr,
                [&](LiveService& s) { return s.one(relation, reference); });
        };
        host_.iterator = [](void* user, vhpiOneToManyT relation,
                             vhpiHandleT reference) {
            return guarded<vhpiHandleT>(user, nullptr, [&](LiveService& s) {
                return s.iterator(relation, reference);
            });
        };
        host_.scan = [](void* user, vhpiHandleT iterator) {
            return guarded<vhpiHandleT>(user, nullptr,
                [&](LiveService& s) { return s.scan(iterator); });
        };
        host_.get = [](void* user, vhpiIntPropertyT property,
                        vhpiHandleT handle) {
            return guarded<vhpiIntT>(user, vhpiUndefined,
                [&](LiveService& s) { return s.get(property, handle); });
        };
        host_.get_str = [](void* user, vhpiStrPropertyT property,
                            vhpiHandleT handle) {
            return guarded<const vhpiCharT*>(user, nullptr,
                [&](LiveService& s) { return s.get_str(property, handle); });
        };
        host_.get_real = [](void* user, vhpiRealPropertyT property,
                             vhpiHandleT handle) {
            return guarded<vhpiRealT>(user, 0.0,
                [&](LiveService& s) { return s.get_real(property, handle); });
        };
        host_.get_phys = [](void* user, vhpiPhysPropertyT property,
                             vhpiHandleT handle) {
            return guarded<vhpiPhysT>(user, vhpiPhysT { 0, 0 },
                [&](LiveService& s) { return s.get_phys(property, handle); });
        };
        host_.get_value = [](void* user, vhpiHandleT handle,
                              vhpiValueT* value) {
            return guarded<int>(user, -1,
                [&](LiveService& s) { return s.get_value(handle, value); });
        };
        host_.put_value = [](void* user, vhpiHandleT handle, vhpiValueT* value,
                              vhpiPutValueModeT mode) {
            return guarded<int>(user, 1, [&](LiveService& s) {
                return s.put_value(handle, value, mode);
            });
        };
        host_.get_time = [](void* user, vhpiTimeT* time, long* cycles) {
            (void)guarded<int>(user, 0, [&](LiveService& s) {
                s.get_time(time, cycles);
                return 0;
            });
        };
        host_.get_next_time = [](void* user, vhpiTimeT* time) {
            return guarded<int>(user, vhpiNoActivity,
                [&](LiveService& s) { return s.get_next_time(time); });
        };
        host_.control = [](void* user, vhpiSimControlT command, int32_t) {
            return guarded<int>(user, 1,
                [&](LiveService& s) { return s.control(command); });
        };
        host_.print = [](void* user, const char* text, size_t size) {
            auto& service = self(user);
            service.print(std::string_view { text, size });
            return static_cast<int>(size);
        };
        host_.assertion = [](void* user, vhpiSeverityT severity,
                              const char* text, size_t size) {
            auto& service = self(user);
            service.assertion(severity, std::string_view { text, size });
            return 0;
        };
        host_.compare_handles = [](void* user, vhpiHandleT left,
                                    vhpiHandleT right) {
            return guarded<int>(user, 0,
                [&](LiveService& s) { return s.compare(left, right); });
        };
        host_.check_error = [](void* user, vhpiErrorInfoT* info) {
            return self(user).check_error(info);
        };
        host_.release_handle = [](void* user, vhpiHandleT handle) {
            return guarded<int>(user, 1,
                [&](LiveService& s) { return s.release(handle); });
        };
        host_.register_foreignf = [](void* user, vhpiForeignDataT* data) {
            return guarded<vhpiHandleT>(user, nullptr, [&](LiveService& s) {
                return s.register_foreign(data);
            });
        };
        host_.get_foreignf_info = [](void* user, vhpiHandleT handle,
                                      vhpiForeignDataT* data) {
            return guarded<int>(user, 1, [&](LiveService& s) {
                return s.foreign_info(handle, data);
            });
        };
        // A function the tool does not implement raises an error and has no
        // other effect (IEEE 1076-2019 17.3).
        host_.schedule_transaction = [](void* user, vhpiHandleT, vhpiValueT*,
                                         uint32_t, vhpiTimeT*, vhpiDelayModeT,
                                         vhpiTimeT*) {
            return self(user).not_implemented("vhpi_schedule_transaction", 1);
        };
        host_.format_value = [](void* user, const vhpiValueT* in,
                                 vhpiValueT* out) {
            return guarded<int>(user, -1, [&](LiveService& s) {
                return s.format_value(in, out);
            });
        };
        // A foreign process and driver need foreign architectures, which are
        // not implemented; collections are (IEEE 1076-2019 23.6).
        host_.create = [](void* user, vhpiClassKindT kind, vhpiHandleT first,
                           vhpiHandleT second) {
            if (kind == vhpiProcessStmtK || kind == vhpiDriverK) {
                return self(user).not_implemented<vhpiHandleT>(
                    "vhpi_create: creating foreign processes and drivers",
                    nullptr);
            }
            return guarded<vhpiHandleT>(user, nullptr, [&](LiveService& s) {
                return s.create(kind, first, second);
            });
        };
        host_.get_data = [](void* user, int32_t, void*, size_t) {
            return self(user).not_implemented<size_t>("vhpi_get_data", 0U);
        };
        host_.put_data = [](void* user, int32_t, void*, size_t) {
            return self(user).not_implemented<size_t>("vhpi_put_data", 0U);
        };
        host_.protected_call = [](void* user, vhpiHandleT variable,
                                   vhpiUserFctT function, void* data) {
            return guarded<int>(user, 1, [&](LiveService& s) {
                return s.protected_call(variable, function, data);
            });
        };
    }

    template <typename Result>
    [[nodiscard]] Result not_implemented(
        const char* const function, const Result fallback)
    {
        clear_error();
        error(vhpiError, std::string { function } + " is not implemented");
        pli_error();
        return fallback;
    }

    // --- errors and output ---------------------------------------------------------

    void clear_error() noexcept { has_error_ = false; }

    // An error in a VHPI routine runs the vhpiCbPLIError callbacks as the
    // routine returns; they read it with vhpi_check_error, and the caller
    // still sees it afterwards.
    void pli_error()
    {
        if (!has_error_ || in_pli_error_ || !has_callbacks(vhpiCbPLIError)) {
            return;
        }
        in_pli_error_ = true;
        const auto severity = error_severity_;
        const auto message = error_message_;
        try {
            dispatch(vhpiCbPLIError);
        } catch (...) {
            in_pli_error_ = false;
            throw;
        }
        in_pli_error_ = false;
        has_error_ = true;
        error_severity_ = severity;
        error_message_ = message;
    }

    void error(const vhpiSeverityT severity, std::string message)
    {
        has_error_ = true;
        error_severity_ = severity;
        error_message_ = std::move(message);
    }

    [[nodiscard]] int check_error(vhpiErrorInfoT* const info)
    {
        if (!has_error_) {
            return 0;
        }
        if (info != nullptr) {
            info->severity = error_severity_;
            info->message = error_message_.data();
            info->str = nullptr;
            info->file = nullptr;
            info->line = 0;
        }
        return 1;
    }

    void print(const std::string_view text)
    {
        if (!print_) {
            return;
        }
        if (text.empty() || text.back() != '\n') {
            print_(std::string { text } + '\n');
        } else {
            print_(text);
        }
    }

    // vhpi_assert reports like a VHDL assertion of that severity
    // (IEEE 1076-2019 23.2); a failure ends the simulation.
    void assertion(const vhpiSeverityT severity, const std::string_view text)
    {
        const auto* name = "note";
        switch (severity) {
        case vhpiNote:
            name = "note";
            break;
        case vhpiWarning:
            name = "warning";
            break;
        case vhpiError:
            name = "error";
            break;
        case vhpiFailure:
        case vhpiSystem:
        case vhpiInternal:
            name = "failure";
            break;
        }
        print(std::string { name } + "[FSIM-VHPI-ASSERT]: "
            + std::string { text });
        if (severity == vhpiError || severity == vhpiFailure
            || severity == vhpiSystem || severity == vhpiInternal) {
            failed_ = true;
        }
        if (severity == vhpiFailure || severity == vhpiSystem
            || severity == vhpiInternal) {
            stopping_ = true;
            simulation_.request_stop();
        }
    }

    // --- callbacks ---------------------------------------------------------------

    [[nodiscard]] Callback* callback_of(const vhpiHandleT handle)
    {
        auto* const base = handle_of(handle);
        if (base == nullptr || base->tag != HandleTag::callback) {
            error(vhpiError, "handle is not a callback");
            return nullptr;
        }
        return static_cast<Callback*>(base);
    }

    [[nodiscard]] Object* object_of(const vhpiHandleT handle)
    {
        auto* const base = handle_of(handle);
        if (base == nullptr || base->tag != HandleTag::object) {
            error(vhpiError, "invalid object handle");
            return nullptr;
        }
        return static_cast<Object*>(base);
    }

    [[nodiscard]] vhpiHandleT register_cb(
        vhpiCbDataT* const data, const int32_t flags)
    {
        if (data == nullptr || data->cb_rtn == nullptr) {
            error(vhpiError, "callback data has no routine");
            return nullptr;
        }
        auto& callback = allocate_callback();
        // Frees the slot unless registration succeeds.
        struct Pending {
            LiveService& service;
            Callback* callback;
            ~Pending()
            {
                if (callback != nullptr) {
                    service.free_callback(*callback);
                }
            }
        } pending { *this, &callback };
        callback.data = *data;
        callback.state = (flags & vhpiDisableCb) != 0 ? vhpiDisable
                                                       : vhpiEnable;
        callback.repetitive = repetitive_reason(data->reason);
        callback.order = next_order_++;
        if (data->time != nullptr) {
            callback.time = *data->time;
            callback.has_time = true;
        }
        if (data->value != nullptr) {
            callback.value = *data->value;
            callback.has_value = true;
        }
        switch (data->reason) {
        case vhpiCbStartOfSimulation:
        case vhpiCbEndOfSimulation:
        case vhpiCbStartOfInitialization:
        case vhpiCbEndOfInitialization:
        case vhpiCbNextTimeStep:
        case vhpiCbRepNextTimeStep:
        case vhpiCbStartOfNextCycle:
        case vhpiCbRepStartOfNextCycle:
        case vhpiCbEndOfTimeStep:
        case vhpiCbRepEndOfTimeStep:
        case vhpiCbStartOfProcesses:
        case vhpiCbRepStartOfProcesses:
        case vhpiCbEndOfProcesses:
        case vhpiCbRepEndOfProcesses:
        case vhpiCbStartOfPostponed:
        case vhpiCbRepStartOfPostponed:
        case vhpiCbLastKnownDeltaCycle:
        case vhpiCbRepLastKnownDeltaCycle:
        case vhpiCbStartOfAnalysis:
        case vhpiCbEndOfAnalysis:
        case vhpiCbStartOfElaboration:
        case vhpiCbEndOfElaboration:
        case vhpiCbStartOfTool:
        case vhpiCbEndOfTool:
        case vhpiCbQuiescense:
        case vhpiCbPLIError:
        case vhpiCbSigInterrupt:
            callback.data.obj = nullptr;
            callback.data.time = nullptr;
            callback.data.value = nullptr;
            callback.has_time = false;
            callback.has_value = false;
            if (dispatch_reason(data->reason) == vhpiCbNextTimeStep) {
                ensure_next_time();
            }
            // fsim's interrupt event is SIGINT (IEEE 1076-2019
            // 21.3.7.10); it reaches the callbacks at the next safe point
            // instead of stopping the run.
            if (data->reason == vhpiCbSigInterrupt && !interrupt_trap_) {
                interrupt_trap_ = std::make_unique<ForeignSignalTrap>(SIGINT);
                if (!interrupt_trap_->valid()) {
                    interrupt_trap_.reset();
                    error(vhpiError, "cannot trap the interrupt signal");
                    return nullptr;
                }
            }
            break;
        case vhpiCbAfterDelay:
        case vhpiCbRepAfterDelay:
        case vhpiCbTimeOut:
        case vhpiCbRepTimeOut: {
            // An end-of-time-step callback cannot affect the time of the
            // next cycle (IEEE 1076-2019 21.3.6.8).
            if (time_step_ended_) {
                error(vhpiError,
                    "cannot register a timed callback at the end of a time "
                    "step");
                return nullptr;
            }
            if (data->time == nullptr) {
                error(vhpiError, "missing time for a timed callback");
                return nullptr;
            }
            // A timeout ignores obj and value (21.3.3.2).
            if (dispatch_reason(data->reason) == vhpiCbTimeOut) {
                callback.data.obj = nullptr;
                callback.data.value = nullptr;
                callback.has_value = false;
            }
            callback.delay = from_vhpi_time(*data->time);
            if (!schedule_delay(callback)) {
                error(vhpiError, "cannot schedule the after-delay callback");
                return nullptr;
            }
            break;
        }
        case vhpiCbValueChange: {
            auto* const object = object_of(data->obj);
            if (object == nullptr) {
                return nullptr;
            }
            if (!object->signal) {
                error(vhpiError, "value-change callbacks need a signal or a "
                                 "driver");
                return nullptr;
            }
            callback.object = object;
            callback.data.obj = to_handle(object);
            callback.last = element_bits(*object);
            // A driver changes when its process updates it (IEEE 1076-2019
            // 21.3.2.2).
            if (object->kind == vhpiDriverK) {
                ensure_driver_hook();
            } else {
                ensure_change_hook();
            }
            break;
        }
        // An event on the signal triggers it (IEEE 1076-2019 21.3.3.3); a
        // signal's callback ignores the value member.
        case vhpiCbSensitivity: {
            auto* const object = object_of(data->obj);
            if (object == nullptr) {
                return nullptr;
            }
            // A set of signals: the trigger is an event on any member
            // (21.3.3.3); the callback gets the bitmap of the members that
            // had one when it was given a value.
            if (object->kind == vhpiAnyCollectionK) {
                callback.members = object->lists[ListKind::members];
                for (auto* const member : callback.members) {
                    if (!member->signal || member->kind == vhpiDriverK) {
                        error(vhpiError, "a sensitivity set holds signals");
                        return nullptr;
                    }
                    callback.member_last.push_back(element_bits(*member));
                }
                callback.bitmap.assign((callback.members.size() + 31U) / 32U, 0);
                callback.object = object;
                callback.data.obj = to_handle(object);
                ensure_change_hook();
                break;
            }
            if (!object->signal) {
                error(vhpiError, "a sensitivity callback needs a signal or a "
                                 "collection of signals");
                return nullptr;
            }
            callback.object = object;
            callback.data.obj = to_handle(object);
            callback.data.value = nullptr;
            callback.has_value = false;
            callback.last = element_bits(*object);
            ensure_change_hook();
            break;
        }
        // Statement callbacks (IEEE 1076-2019 21.3.4). The kernel reports
        // statements, suspensions and resumptions only when the run keeps
        // its execution probes, which the user asks for.
        case vhpiCbStmt:
        case vhpiCbResume:
        case vhpiCbSuspend:
        case vhpiCbStartOfSubpCall:
        case vhpiCbEndOfSubpCall: {
            if (!simulation_.foreign_probes()) {
                error(vhpiError,
                    std::string { "vhpi_register_cb: " }
                        + statement_reason_name(data->reason)
                        + " needs the execution probes of simulate "
                          "--foreign-probes (configuration key "
                          "simulate.foreign_probes)");
                return nullptr;
            }
            auto* const object = object_of(data->obj);
            if (object == nullptr) {
                return nullptr;
            }
            callback.has_time = false;
            callback.has_value = false;
            callback.data.time = nullptr;
            callback.data.value = nullptr;
            if (data->reason == vhpiCbStartOfSubpCall
                || data->reason == vhpiCbEndOfSubpCall) {
                if (!place_call_callback(callback, *object)) {
                    return nullptr;
                }
            } else if (data->reason != vhpiCbStmt) {
                if (object->kind != vhpiProcessStmtK) {
                    error(vhpiError, "resume and suspend callbacks need a "
                                     "process statement");
                    return nullptr;
                }
                callback.process = object;
            } else if (!place_statement_callback(callback, *object)) {
                return nullptr;
            }
            ensure_execution_hook();
            break;
        }
        // Object callbacks on a signal; a force or release callback with no
        // object watches every signal (IEEE 1076-2019 21.3.2.3-5).
        case vhpiCbForce:
        case vhpiCbRelease:
        case vhpiCbTransaction: {
            Object* object = nullptr;
            if (data->obj != nullptr || data->reason == vhpiCbTransaction) {
                object = object_of(data->obj);
                if (object == nullptr) {
                    return nullptr;
                }
                if (!object->signal
                    || (object->kind == vhpiDriverK
                        && data->reason != vhpiCbTransaction)) {
                    error(vhpiError, "object callbacks need a signal; only "
                                     "transaction callbacks take a driver");
                    return nullptr;
                }
            }
            callback.object = object;
            callback.data.obj = object != nullptr ? to_handle(object) : nullptr;
            if (data->reason == vhpiCbTransaction) {
                if (object->kind == vhpiDriverK) {
                    ensure_driver_hook();
                    const std::array signals { *object->signal };
                    simulation_.watch_signal_changes(signals);
                } else {
                    ensure_transaction_hook(*object->signal);
                }
            } else {
                ensure_force_hook();
            }
            break;
        }
        default:
            error(vhpiError,
                "unsupported callback reason " + std::to_string(data->reason));
            return nullptr;
        }
        // What the callback needs of the v4 kernel (docs/engine-v4-semantics.md
        // S19): a callback within the run sees every kernel round as a
        // scheduler delta, and a value change or sensitivity callback hears
        // every change of its signals.
        if (data->reason != vhpiCbStartOfSimulation
            && data->reason != vhpiCbEndOfSimulation) {
            simulation_.open_external_observation();
        }
        if ((data->reason == vhpiCbValueChange || data->reason == vhpiCbSensitivity)
            && callback.object != nullptr) {
            std::vector<SignalId> watched;
            if (callback.object->signal) {
                watched.push_back(*callback.object->signal);
            }
            for (auto* const member : callback.members) {
                if (member->signal) {
                    watched.push_back(*member->signal);
                }
            }
            simulation_.watch_signal_changes(watched);
        }
        pending.callback = nullptr;
        callback.registered = true;
        callbacks_.push_back(&callback);
        return (flags & vhpiReturnCb) != 0 ? to_handle(&callback) : nullptr;
    }

    [[nodiscard]] bool schedule_delay(Callback& callback)
    {
        const auto profile = simulation_.systemverilog_vpi_time().profile();
        // Delays are in femtoseconds; the timed service counts precision
        // quanta.
        auto quanta = callback.delay;
        for (int exponent = -15; exponent < profile.precision_exponent;
             ++exponent) {
            quanta /= 10U;
        }
        runtime::SystemVerilogVpiCallbackRegistration registration;
        registration.kind = runtime::SystemVerilogVpiCallbackKind::AfterDelay;
        registration.delay = runtime::SystemVerilogVpiTimeValue { };
        registration.delay->high = static_cast<std::uint32_t>(quanta >> 32U);
        registration.delay->low = static_cast<std::uint32_t>(quanta);
        // An after-delay callback runs at the start of its cycle (IEEE
        // 1076-2019 14.7.5.3 b 3); a timeout runs with the processes that
        // resume by a timeout in that cycle, like a wait statement's
        // timeout clause (step f 3, 21.3.3.2).
        registration.at_slot_start
            = dispatch_reason(callback.data.reason) == vhpiCbAfterDelay;
        registration.callback
            = [this, target = &callback, order = callback.order](
                  const runtime::SystemVerilogVpiCallbackEvent&) {
                  timed(target, order);
              };
        const auto registered
            = simulation_.systemverilog_vpi_callbacks().register_callback(
                std::move(registration));
        if (!registered) {
            return false;
        }
        callback.managed = registered.value;
        return true;
    }

    void timed(Callback* const callback, const std::uint64_t order)
    {
        if (!live(callback, order)) {
            return;
        }
        callback->managed.reset();
        if (callback->repetitive) {
            (void)schedule_delay(*callback);
        }
        // Steps b 1) and 2) of the cycle precede its after-delay
        // callbacks (IEEE 1076-2019 14.7.5.3); a timeout runs after the
        // cycle's signal updates, with the processes (step f 3).
        begin_time_step();
        if (dispatch_reason(callback->data.reason) == vhpiCbTimeOut) {
            due_timeouts_.emplace_back(callback, order);
            return;
        }
        run(*callback);
        if (live(callback, order) && !callback->repetitive) {
            retire(callback);
        }
    }

    // Slots are never returned to the allocator, so a callback pointer
    // stays readable after removal; `order` tells a reused slot apart.
    [[nodiscard]] static bool live(
        const Callback* const callback, const std::uint64_t order) noexcept
    {
        return callback->tag == HandleTag::callback && callback->registered
            && callback->order == order;
    }

    [[nodiscard]] Callback& allocate_callback()
    {
        if (free_callbacks_.empty()) {
            return callback_pool_.emplace_back();
        }
        auto* const slot = free_callbacks_.back();
        free_callbacks_.pop_back();
        std::destroy_at(slot);
        return *std::construct_at(slot);
    }

    void free_callback(Callback& callback)
    {
        if (callback.registered) {
            retire(&callback);
        } else if (callback.managed) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                *callback.managed);
            callback.managed.reset();
        }
        callback.tag = HandleTag::removed;
        free_callbacks_.push_back(&callback);
    }

    // A matured callback keeps its handle valid but never runs again
    // (IEEE 1076-2019 21.1).
    void retire(Callback* const callback)
    {
        if (callback->managed) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                *callback->managed);
            callback->managed.reset();
        }
        if (callback->registered) {
            callback->registered = false;
            callback->state = vhpiMature;
            std::erase(callbacks_, callback);
        }
    }

    // A removed callback leaves the information model and its handles
    // become invalid (IEEE 1076-2019 21.2.4).
    [[nodiscard]] int remove_cb(const vhpiHandleT handle)
    {
        auto* const callback = callback_of(handle);
        if (callback == nullptr) {
            return 1;
        }
        free_callback(*callback);
        return 0;
    }

    [[nodiscard]] int set_cb_state(const vhpiHandleT handle,
        const vhpiStateT state)
    {
        auto* const callback = callback_of(handle);
        if (callback == nullptr) {
            return 1;
        }
        const auto required = state == vhpiEnable ? vhpiDisable : vhpiEnable;
        if (callback->state != required) {
            error(vhpiWarning,
                state == vhpiEnable
                    ? "callback must be disabled in order to enable it"
                    : "callback must be enabled in order to disable it");
            return 1;
        }
        if (state == vhpiEnable && time_step_ended_
            && (dispatch_reason(callback->data.reason) == vhpiCbAfterDelay
                || dispatch_reason(callback->data.reason) == vhpiCbTimeOut)) {
            error(vhpiError,
                "cannot enable a timed callback at the end of a time step");
            return 1;
        }
        callback->state = state;
        return 0;
    }

    [[nodiscard]] int cb_info(const vhpiHandleT handle, vhpiCbDataT* const data)
    {
        auto* const callback = callback_of(handle);
        if (callback == nullptr || data == nullptr) {
            return 1;
        }
        *data = callback->data;
        data->time = callback->has_time ? &callback->time : nullptr;
        data->value = callback->has_value ? &callback->value : nullptr;
        return 0;
    }

    // Run a callback's routine with a fresh copy of its data.
    // `trigger`: the object the event occurred on, for a callback
    // registered without one.
    void run(Callback& callback, Object* const trigger = nullptr)
    {
        if (callback.state != vhpiEnable) {
            return;
        }
        auto data = callback.data;
        auto* const object = trigger != nullptr ? trigger : callback.object;
        if (trigger != nullptr) {
            data.obj = to_handle(trigger);
        }
        vhpiTimeT time { };
        if (callback.has_time) {
            time = to_vhpi_time(now_femtoseconds());
            data.time = &time;
        }
        auto value = callback.value;
        const bool bitmap = callback.data.reason == vhpiCbSensitivity
            && object != nullptr && object->kind == vhpiAnyCollectionK;
        if (callback.has_value && bitmap) {
            // The sensitivity-set bitmap (IEEE 1076-2019 21.3.3.3).
            value = { };
            value.format = vhpiIntVecVal;
            value.numElems = static_cast<int32_t>(callback.delivered.size());
            value.bufSize = static_cast<std::size_t>(
                callback.delivered.size() * sizeof(vhpiIntT));
            value.value.intgs = callback.delivered.data();
            data.value = &value;
        } else if (callback.has_value && object != nullptr && !bitmap) {
            (void)read_value(*object, value);
            data.value = &value;
        }
        auto* const previous = dispatching_;
        dispatching_ = data.obj;
        {
            Scope scope { *this };
            data.cb_rtn(&data);
        }
        dispatching_ = previous;
    }

    // Run the callbacks registered for `reason` and its repetitive form,
    // in registration order. A one-shot callback matures even when it is
    // disabled.
    void dispatch(const int32_t reason)
    {
        std::vector<std::pair<Callback*, std::uint64_t>> selected;
        for (auto* const callback : callbacks_) {
            if (dispatch_reason(callback->data.reason) == reason
                && callback->data.reason != vhpiCbValueChange) {
                selected.emplace_back(callback, callback->order);
            }
        }
        for (const auto& [callback, order] : selected) {
            if (!live(callback, order)) {
                continue;
            }
            run(*callback);
            if (live(callback, order) && !callback->repetitive) {
                retire(callback);
            }
        }
    }

    [[nodiscard]] bool has_callbacks(const int32_t reason) const
    {
        return std::ranges::any_of(callbacks_, [&](const auto* callback) {
            return dispatch_reason(callback->data.reason) == reason;
        });
    }

    // The initialization phase begins before any process runs
    // (IEEE 1076-2019 14.7.5.2 a, b); it completes at the first safe point
    // after the processes have run.
    void start_of_simulation()
    {
        phase_ = vhpiInitializationPhase;
        dispatch(vhpiCbStartOfInitialization);
        dispatch(vhpiCbStartOfNextCycle);
        dispatch(vhpiCbStartOfProcesses);
        // The safe point after the processes' first run completes the
        // initialization, also in a design with no process.
        simulation_.schedule_active_task([] { });
    }

    // Steps h) and m) of the initialization phase, then the start of the
    // simulation phase (IEEE 1076-2019 14.7.5.2, 14.7.5.3). Signals still
    // hold their initial values: the first update is in the first cycle.
    void finish_initialization(const runtime::Scheduler* const scheduler)
    {
        initialized_ = true;
        dispatch(vhpiCbEndOfProcesses);
        dispatch(vhpiCbStartOfPostponed);
        dispatch(vhpiCbEndOfInitialization);
        phase_ = vhpiSimulationPhase;
        dispatch(vhpiCbStartOfSimulation);
        // A first simulation cycle at time zero is a delta cycle (Tn = Tc,
        // 14.7.5.1): it starts the next cycle but not a new time step.
        if (scheduler != nullptr && pending_now(*scheduler)) {
            cycle_started_ = true;
            dispatch(vhpiCbStartOfNextCycle);
        }
        if (has_callbacks(vhpiCbNextTimeStep)
            || has_callbacks(vhpiCbStartOfNextCycle)) {
            ensure_next_time();
        }
    }

    void end_of_simulation()
    {
        if (simulation_ended_) {
            return;
        }
        simulation_ended_ = true;
        if (!initialized_) {
            finish_initialization(nullptr);
        }
        flush_changes();
        // The initialization is followed by at least one simulation cycle
        // (IEEE 1076-2019 14.7.5.2), even when no process is active in it.
        if (!cycled_ && !stopping_) {
            cycled_ = true;
            dispatch(vhpiCbEndOfProcesses);
            dispatch(vhpiCbLastKnownDeltaCycle);
            start_of_postponed();
            end_of_time_step_pending_ = true;
        }
        // A time step whose postponed processes have not run yet.
        if (end_of_time_step_pending_) {
            end_of_time_step(nullptr);
        }
        simulation_ending_ = true;
        dispatch(vhpiCbEndOfSimulation);
        simulation_ending_ = false;
    }

    void ensure_next_time()
    {
        if (next_time_) {
            return;
        }
        runtime::SystemVerilogVpiCallbackRegistration registration;
        registration.kind = runtime::SystemVerilogVpiCallbackKind::NextTime;
        registration.callback
            = [this](const runtime::SystemVerilogVpiCallbackEvent&) {
                  next_time_.reset();
                  next_time_step();
              };
        const auto registered
            = simulation_.systemverilog_vpi_callbacks().register_callback(
                std::move(registration));
        if (registered) {
            next_time_ = registered.value;
        }
    }

    // The first cycle of a new time step (IEEE 1076-2019 21.3.6).
    void next_time_step()
    {
        begin_time_step();
        if (has_callbacks(vhpiCbNextTimeStep)) {
            ensure_next_time();
        }
    }

    // Steps b 1) and 2) of the first cycle of a time step, once per time
    // step; a first cycle at time zero is a delta cycle (14.7.5.1).
    void begin_time_step()
    {
        const auto now = now_femtoseconds();
        if (now == 0U || stepped_at_ == now) {
            return;
        }
        stepped_at_ = now;
        cycle_started_ = true;
        awaiting_update_ = true;
        time_step_ended_ = false;
        dispatch(vhpiCbNextTimeStep);
        dispatch(vhpiCbStartOfNextCycle);
    }

    [[nodiscard]] static bool pending_now(const runtime::Scheduler& scheduler)
    {
        if (scheduler.has_pending_activity_now()) {
            return true;
        }
        const auto next = scheduler.next_pending_time();
        return next && *next == scheduler.now();
    }

    void safe_point(runtime::Scheduler& scheduler,
        const runtime::SchedulerPhase phase)
    {
        if (phase == runtime::SchedulerPhase::update) {
            cycle_counted_ = false;
        }
        if (interrupt_trap_ && interrupt_trap_->take()) {
            dispatch(vhpiCbSigInterrupt);
            if (!has_callbacks(vhpiCbSigInterrupt)) {
                interrupt_trap_.reset();
            }
        }
        flush_changes();
        if (phase == runtime::SchedulerPhase::update) {
            flush_forces();
        }
        // The v4 kernel runs its update phase inside its own activation and
        // reports the transactions there; the scheduler's update phase
        // reports the others.
        flush_transactions();
        flush_drivers();
        if (stopping_) {
            return;
        }
        // The signals of the cycle have been updated; its processes run
        // next (IEEE 1076-2019 14.7.5.3 f 1). A cycle whose updates resume
        // no process ends here.
        if (phase == runtime::SchedulerPhase::update && initialized_) {
            awaiting_update_ = false;
            if (pending_now(scheduler) || !due_timeouts_.empty()
                || (cycle_started_ && !end_of_time_step_pending_)) {
                dispatch(vhpiCbStartOfProcesses);
                run_sensitivity();
                run_timeouts();
                flush_changes();
                if (!pending_now(scheduler)) {
                    end_of_cycle(scheduler);
                }
            } else {
                run_sensitivity();
            }
            return;
        }
        if (phase != runtime::SchedulerPhase::active) {
            return;
        }
        if (!initialized_) {
            finish_initialization(&scheduler);
            return;
        }
        // The first active phase of a time step runs its step b); the
        // cycle's processes run after its signal updates.
        if (awaiting_update_) {
            return;
        }
        end_of_cycle(scheduler);
    }

    void run_timeouts()
    {
        auto due = std::move(due_timeouts_);
        due_timeouts_.clear();
        for (const auto& [callback, order] : due) {
            if (!live(callback, order)) {
                continue;
            }
            run(*callback);
            if (live(callback, order) && !callback->repetitive) {
                retire(callback);
            }
        }
    }

    // Steps f 4) to h) of a simulation cycle (IEEE 1076-2019 14.7.5.3).
    void end_of_cycle(runtime::Scheduler& scheduler)
    {
        cycled_ = true;
        cycle_started_ = false;
        // The processes of a simulation cycle have run.
        dispatch(vhpiCbEndOfProcesses);
        if (pending_now(scheduler)) {
            cycle_started_ = true;
            if (has_callbacks(vhpiCbStartOfNextCycle)) {
                dispatch(vhpiCbStartOfNextCycle);
            }
            return;
        }
        dispatch(vhpiCbLastKnownDeltaCycle);
        flush_changes();
        if (pending_now(scheduler)) {
            cycle_started_ = true;
            dispatch(vhpiCbStartOfNextCycle);
            return;
        }
        start_of_postponed();
        // The time step ends after its postponed processes, which run at
        // the end of the time slot (14.7.5.3 h 4-6).
        if (!end_of_time_step_pending_) {
            end_of_time_step_pending_ = true;
            scheduler.schedule_end_of_time_slot(
                std::numeric_limits<runtime::StableOrder>::max(),
                [this](runtime::Scheduler& slot) { end_of_time_step(&slot); });
        }
    }

    // Step h 3) of a simulation cycle: from here to the end of the time
    // step nothing may cause a delta cycle (IEEE 1076-2019 14.7.5.3).
    void start_of_postponed()
    {
        postponed_ = true;
        dispatch(vhpiCbStartOfPostponed);
    }

    // No callback at the end of a time step may schedule another delta
    // cycle (IEEE 1076-2019 21.3.6.8).
    void end_of_time_step(const runtime::Scheduler* const scheduler)
    {
        if (!end_of_time_step_pending_) {
            return;
        }
        end_of_time_step_pending_ = false;
        time_step_ended_ = true;
        dispatch(vhpiCbEndOfTimeStep);
        time_step_ended_ = false;
        postponed_ = false;
        // Nothing is left to simulate (14.7.5.3 h 7); a quiescence
        // callback may cause further cycles (21.3.7.7).
        if (scheduler != nullptr && !pending_now(*scheduler)
            && !scheduler->next_pending_time()) {
            dispatch(vhpiCbQuiescense);
        }
        if (has_callbacks(vhpiCbStartOfNextCycle)) {
            ensure_next_time();
        }
    }

    // After the cycle's start-of-processes callbacks, those of the signals
    // with an event in the cycle (IEEE 1076-2019 14.7.5.3 f 1).
    void run_sensitivity()
    {
        std::vector<std::pair<Callback*, std::uint64_t>> selected;
        for (auto* const callback : callbacks_) {
            if (callback->data.reason == vhpiCbSensitivity
                && std::exchange(callback->event, false)) {
                callback->delivered = callback->bitmap;
                std::ranges::fill(callback->bitmap, 0);
                selected.emplace_back(callback, callback->order);
            }
        }
        for (const auto& [callback, order] : selected) {
            if (live(callback, order)) {
                run(*callback);
            }
        }
    }

    void ensure_change_hook()
    {
        if (change_token_ != 0U) {
            return;
        }
        change_token_ = simulation_.add_signal_change_hook(
            [this](const SignalId signal, const PackedLogic4&,
                const runtime::SimulationTick time, std::uint64_t) {
                if (time != cycle_time_) {
                    cycle_time_ = time;
                    cycles_ = 0U;
                    cycle_counted_ = false;
                }
                if (!cycle_counted_) {
                    ++cycles_;
                    cycle_counted_ = true;
                }
                changed_.insert(signal);
            });
    }

    // Value-change callbacks run after the signal update, once per changed
    // element value (IEEE 1076-2019 21.3.3).
    void flush_changes()
    {
        while (!changed_.empty()) {
            const auto changed = std::exchange(changed_, { });
            std::vector<std::pair<Callback*, std::uint64_t>> selected;
            for (auto* const callback : callbacks_) {
                if (callback->data.reason == vhpiCbSensitivity
                    && callback->object->kind == vhpiAnyCollectionK) {
                    for (std::size_t index = 0U; index < callback->members.size();
                         ++index) {
                        const auto* const member = callback->members[index];
                        if (!changed.contains(*member->signal)) {
                            continue;
                        }
                        auto bits = element_bits(*member);
                        auto& last = callback->member_last[index];
                        if (!last || !bits || *last != *bits) {
                            last = std::move(bits);
                            callback->bitmap[index / 32U]
                                |= static_cast<vhpiIntT>(
                                    std::uint32_t { 1U } << (index % 32U));
                            callback->event = true;
                        }
                    }
                } else if (callback->data.reason == vhpiCbSensitivity
                    && changed.contains(*callback->object->signal)) {
                    auto bits = element_bits(*callback->object);
                    if (!callback->last || !bits || *callback->last != *bits) {
                        callback->last = std::move(bits);
                        callback->event = true;
                    }
                }
                if (callback->data.reason == vhpiCbValueChange
                    && callback->object != nullptr
                    && callback->object->signal
                    && callback->object->kind != vhpiDriverK
                    && changed.contains(*callback->object->signal)) {
                    selected.emplace_back(callback, callback->order);
                }
            }
            for (const auto& [callback, order] : selected) {
                if (!live(callback, order)) {
                    continue;
                }
                auto bits = element_bits(*callback->object);
                if (callback->last && bits && *callback->last == *bits) {
                    continue;
                }
                callback->last = std::move(bits);
                run(*callback);
            }
        }
    }

    // A force or release of a signal slice; `target` is the object a
    // vhpi_put_value named.
    struct ForceEvent {
        SignalId signal { };
        std::size_t offset { };
        std::size_t width { };
        bool release { };
        Object* target { };
    };

    void ensure_force_hook()
    {
        if (force_token_ != 0U) {
            return;
        }
        force_token_ = simulation_.add_force_statement_hook(
            [this](const runtime::simir::ProcessId, const SignalId signal,
                const std::size_t offset, const std::size_t width,
                const bool release) {
                forces_.push_back(
                    ForceEvent { signal, offset, width, release, nullptr });
            });
    }

    [[nodiscard]] static const char* statement_reason_name(
        const int32_t reason) noexcept
    {
        switch (reason) {
        case vhpiCbResume:
            return "vhpiCbResume";
        case vhpiCbSuspend:
            return "vhpiCbSuspend";
        case vhpiCbStartOfSubpCall:
            return "vhpiCbStartOfSubpCall";
        case vhpiCbEndOfSubpCall:
            return "vhpiCbEndOfSubpCall";
        default:
            return "vhpiCbStmt";
        }
    }

    // The process or concurrent statement that runs a statement, expression or
    // call object; none for one in a subprogram body, which any process that
    // calls the subprogram runs (`in_body`).
    [[nodiscard]] static Object* enclosing_owner(
        Object& object, bool& in_body) noexcept
    {
        for (auto* current = &object; current != nullptr;
             current = current->parent) {
            if (current->kind == vhpiProcessStmtK) {
                return current;
            }
            if (current->kind == vhpiSubpBodyK) {
                in_body = true;
                return nullptr;
            }
            if (current->is_stmt && !current->sequential
                && current->statement != nullptr) {
                return current;
            }
        }
        return nullptr;
    }

    // A subprogram call callback's trigger object (IEEE 1076-2019 21.3.4.5,
    // 21.3.4.6): a procedure call statement or a function call, in a
    // process, a concurrent statement or a subprogram body, or for the start
    // of a call also a subprogram declaration, whose every call triggers it.
    [[nodiscard]] bool place_call_callback(Callback& callback, Object& object)
    {
        if (data_reason(callback) == vhpiCbStartOfSubpCall
            && (object.kind == vhpiProcDeclK || object.kind == vhpiFuncDeclK)
            && object.declaration != nullptr) {
            callback.object = &object;
            return true;
        }
        if (object.kind != vhpiSeqProcCallStmtK
            && object.kind != vhpiConcProcCallStmtK
            && object.kind != vhpiFuncCallK) {
            error(vhpiError,
                data_reason(callback) == vhpiCbStartOfSubpCall
                    ? "subprogram call callbacks need a subprogram call or a "
                      "subprogram declaration"
                    : "end of subprogram call callbacks need a subprogram "
                      "call");
            return false;
        }
        bool in_body = false;
        auto* const owner = object.kind == vhpiConcProcCallStmtK
            ? &object
            : enclosing_owner(object, in_body);
        if (owner == nullptr && !in_body) {
            error(vhpiError, "the call is not part of a process");
            return false;
        }
        callback.process = owner;
        callback.trigger = &object;
        callback.reported = &object;
        std::tie(callback.line, callback.column)
            = source_position(object.source);
        callback.file = source_file(object.source);
        return true;
    }

    [[nodiscard]] static int32_t data_reason(const Callback& callback) noexcept
    {
        return callback.data.reason;
    }

    // The procedure a procedure call statement calls, when the design names
    // one.
    [[nodiscard]] const vhdl::Declaration* procedure_callee(
        const vhdl::Statement& statement) const
    {
        if (statement.resolved_procedure) {
            return hir_.find_declaration(*statement.resolved_procedure);
        }
        return statement.procedure.selected
            ? hir_.find_declaration(*statement.procedure.selected)
            : nullptr;
    }

    // The first statement of the body of the procedure a concurrent
    // procedure call statement calls (IEEE 1076-2019 21.3.4.2).
    [[nodiscard]] Object* called_body_statement(Object& call)
    {
        if (call.statement == nullptr) {
            return nullptr;
        }
        const auto* const callee = procedure_callee(*call.statement);
        if (callee == nullptr) {
            return nullptr;
        }
        for (auto* const body : all_bodies()) {
            if (body->declaration == callee) {
                const auto& statements = sequential_statements(*body);
                return statements.empty() ? nullptr : statements.front();
            }
        }
        return nullptr;
    }

    // A vhpiCbStmt trigger object (IEEE 1076-2019 21.3.4.2): a sequential
    // statement triggers before it executes; a process before its first
    // statement, which the callback reports; a concurrent procedure call
    // before the first statement of the procedure body it calls, which the
    // callback reports; a case alternative before its first statement; an if
    // statement's first branch before its condition, which the if statement's
    // own point precedes, and an elsif branch before its condition; an else
    // branch, which has no condition, before its first statement.
    [[nodiscard]] bool place_statement_callback(
        Callback& callback, Object& object)
    {
        Object* trigger = nullptr;
        Object* reported = &object;
        if (object.kind == vhpiProcessStmtK) {
            auto& body = sequential_statements(object);
            if (!body.empty()) {
                trigger = body.front();
                reported = trigger;
            }
        } else if (object.kind == vhpiConcProcCallStmtK) {
            trigger = called_body_statement(object);
            if (trigger == nullptr) {
                error(vhpiError, "the called procedure has no body with "
                                 "statements");
                return false;
            }
            reported = trigger;
        } else if (object.kind == vhpiBranchK && object.alternative != nullptr) {
            auto& body = sequential_statements(object);
            trigger = body.empty() ? nullptr : body.front();
        } else if (object.kind == vhpiBranchK) {
            if (object.parent == nullptr) {
                error(vhpiError, "the branch has no statement");
                return false;
            }
            if (object.part == 0U) {
                trigger = object.parent;
            } else if (object.condition) {
                trigger = &object;
            } else {
                auto& body = sequential_statements(object);
                trigger = body.empty() ? nullptr : body.front();
            }
        } else if (object.is_stmt && object.sequential) {
            trigger = &object;
        } else {
            error(vhpiError, "statement callbacks need a sequential "
                             "statement, a branch or a process");
            return false;
        }
        bool in_body = false;
        auto* const owner = object.kind == vhpiConcProcCallStmtK
            ? &object
            : enclosing_owner(trigger != nullptr ? *trigger : object, in_body);
        if (owner == nullptr && !in_body) {
            error(vhpiError, "the statement is not part of a process");
            return false;
        }
        callback.process = owner;
        callback.trigger = trigger;
        callback.reported = reported;
        if (trigger != nullptr) {
            std::tie(callback.line, callback.column)
                = source_position(trigger->source);
            callback.file = source_file(trigger->source);
            if (trigger == &object && loop_kind(trigger->kind)) {
                callback.pass = source_end(trigger->source);
            }
        }
        return true;
    }

    [[nodiscard]] static bool loop_kind(const vhpiClassKindT kind) noexcept
    {
        return kind == vhpiForLoopK || kind == vhpiWhileLoopK
            || kind == vhpiForeverLoopK;
    }

    // Where a span ends: a loop's later passes report there.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> source_end(
        const std::optional<semantic::SourceSpanId> source) const
    {
        const auto& spans = simulation_.semantics().source_spans();
        if (!source || !source->valid() || source->value() >= spans.size()) {
            return { std::numeric_limits<std::uint32_t>::max(), 0U };
        }
        const auto& end = spans.at(source->value()).end;
        return { end.line, end.column };
    }

    [[nodiscard]] std::string source_file(
        const std::optional<semantic::SourceSpanId> source) const
    {
        const auto& semantics = simulation_.semantics();
        if (!source || !source->valid()
            || source->value() >= semantics.source_spans().size()) {
            return { };
        }
        const auto& span = semantics.source_spans().at(source->value());
        if (!span.logical_name.empty()) {
            return span.logical_name;
        }
        if (span.file.valid()
            && span.file.value() < semantics.source_files().size()) {
            return semantics.source_files().at(span.file.value()).physical_name;
        }
        return { };
    }

    void ensure_execution_hook()
    {
        if (execution_token_ != 0U) {
            return;
        }
        execution_token_ = simulation_.add_execution_point_hook(
            [this](auto&, const runtime::simir::ExecutionPoint& point) {
                deliver_point(point);
            });
    }

    // Whether a process object is the design process running a point: the
    // same HIR process, in the object's region.
    [[nodiscard]] bool runs(
        const Object& process, const runtime::simir::ProcessId running)
    {
        const auto key = std::pair { &process, running };
        if (const auto found = process_runs_.find(key);
            found != process_runs_.end()) {
            return found->second;
        }
        bool result = false;
        const auto& processes = design_.processes();
        const auto occurrence = std::ranges::find(processes, running,
            &semantic::design::ProcessOccurrence::runtime_index);
        if (process.process == nullptr) {
            // A concurrent statement is run by the process made for it.
            if (process.is_stmt && !process.sequential
                && process.statement != nullptr) {
                const auto design = design_process_of(process);
                result = design && *design == running;
            }
        } else if (occurrence != processes.end() && occurrence->source_process
            && process.parent != nullptr
            && occurrence->source_process->value()
                == process.process->id.value()) {
            const auto& region = process.parent->path;
            result = occurrence->name.size() > region.size()
                && occurrence->name.starts_with(region)
                && occurrence->name[region.size()] == '.';
        }
        process_runs_.emplace(key, result);
        return result;
    }

    // The sequential statement of a process that begins at a point's
    // source position, searched through loops and branches.
    [[nodiscard]] Object* statement_in(
        Object& process, const runtime::simir::ExecutionPoint& point)
    {
        const auto key
            = std::tuple { &process, point.source.line, point.source.column };
        if (const auto found = statement_at_.find(key);
            found != statement_at_.end()) {
            return found->second;
        }
        const auto here = std::pair { point.source.line, point.source.column };
        const std::function<Object*(Object&, std::size_t)> search
            = [&](Object& owner, const std::size_t depth) -> Object* {
            if (depth > 256U) {
                return nullptr;
            }
            const auto statements = sequential_statements(owner);
            for (auto* const statement : statements) {
                if (source_position(statement->source) == here
                    || (loop_kind(statement->kind)
                        && source_end(statement->source) == here)) {
                    return statement;
                }
                if (auto* const inner = search(*statement, depth + 1U)) {
                    return inner;
                }
                if (statement->kind == vhpiIfStmtK
                    || statement->kind == vhpiCaseStmtK) {
                    const auto branches
                        = branch_objects(*statement, ListKind::branches);
                    for (auto* const branch : branches) {
                        if (auto* const inner = search(*branch, depth + 1U)) {
                            return inner;
                        }
                    }
                }
            }
            return nullptr;
        };
        auto* const result = search(process, 0U);
        statement_at_.emplace(key, result);
        return result;
    }

    // A point reached by a process: once initialization is over, a
    // process's first statement point since it last suspended resumes it
    // (IEEE 1076-2019 14.7.5.3; no process resumes during initialization,
    // 14.7.5.2 NOTE 3). Then the callbacks of the statement there run
    // (21.3.4.2).
    void deliver_point(const runtime::simir::ExecutionPoint& point)
    {
        using Kind = runtime::simir::ExecutionPointKind;
        if (point.kind == Kind::process_suspend) {
            running_.erase(point.design_process);
            deliver_process(vhpiCbSuspend, point);
            return;
        }
        if (point.kind == Kind::call || point.kind == Kind::call_return) {
            deliver_call(point.kind == Kind::call ? vhpiCbStartOfSubpCall
                                                  : vhpiCbEndOfSubpCall,
                point);
            return;
        }
        if (point.kind != Kind::statement && point.kind != Kind::assertion) {
            return;
        }
        if (running_.insert(point.design_process).second && initialized_) {
            deliver_process(vhpiCbResume, point);
        }
        const auto here = std::pair { point.source.line, point.source.column };
        std::vector<std::pair<Callback*, std::uint64_t>> selected;
        for (auto* const callback : callbacks_) {
            if (callback->data.reason == vhpiCbStmt
                && callback->trigger != nullptr
                && (std::pair { callback->line, callback->column } == here
                    || callback->pass == here)
                && (callback->file.empty() || point.source.path.empty()
                    || callback->file == point.source.path.str())
                && (callback->process == nullptr
                    || runs(*callback->process, point.design_process))) {
                selected.emplace_back(callback, callback->order);
            }
        }
        for (const auto& [callback, order] : selected) {
            if (live(callback, order)) {
                run(*callback, callback->reported);
            }
        }
    }

    // Whether a source file name agrees with a point's.
    [[nodiscard]] static bool same_file(
        const std::string& file, const runtime::simir::ExecutionPoint& point)
    {
        return file.empty() || point.source.path.empty()
            || file == point.source.path.str();
    }

    // The expressions a statement holds directly, not those of the
    // statements nested in it.
    void statement_expressions(const vhdl::Statement& statement,
        std::vector<semantic::ExpressionId>& result) const
    {
        const auto add = [&](const std::optional<semantic::ExpressionId>& id) {
            if (id) {
                result.push_back(*id);
            }
        };
        add(statement.target);
        add(statement.value);
        add(statement.condition);
        add(statement.report);
        add(statement.severity);
        add(statement.loop_initial);
        add(statement.loop_limit);
        add(statement.guard);
        add(delay_expression(statement.delay));
        add(delay_expression(statement.rejection_limit));
        for (const auto& element : statement.waveform) {
            result.push_back(element.value);
            add(delay_expression(element.delay));
        }
        for (const auto& argument : statement.procedure_arguments) {
            result.push_back(argument.actual);
        }
        for (const auto& alternative : statement.alternatives) {
            result.insert(result.end(), alternative.choices.begin(),
                alternative.choices.end());
        }
    }

    // The call expression of an expression tree that begins at a point:
    // a call of a function, or of a user-defined operator. An operand is
    // searched before the call that has it.
    [[nodiscard]] std::optional<semantic::ExpressionId> call_expression_at(
        const semantic::ExpressionId id,
        const runtime::simir::ExecutionPoint& point, const std::size_t depth = 0U)
    {
        const auto* const expression = hir_expression(id);
        if (expression == nullptr || depth > 64U) {
            return std::nullopt;
        }
        for (const auto operand : expression->operands) {
            if (const auto found = call_expression_at(operand, point, depth + 1U)) {
                return found;
            }
        }
        for (const auto& association : expression->associations) {
            if (const auto found
                = call_expression_at(association.value, point, depth + 1U)) {
                return found;
            }
        }
        const bool call = expression->kind == vhdl::ExpressionKind::call
            ? is_function_call(*expression)
            : (expression->kind == vhdl::ExpressionKind::unary
                  || expression->kind == vhdl::ExpressionKind::binary)
                && expression->builtin_operator
                    == vhdl::BuiltinOperatorIdentity::none
                && callee_of(*expression) != nullptr;
        if (call
            && source_position(expression->source)
                == std::pair { point.source.line, point.source.column }
            && same_file(source_file(expression->source), point)) {
            return id;
        }
        return std::nullopt;
    }

    // The subprogram call a call point is at, searched in `owner`: a
    // procedure call statement or a function call among the statements of a
    // process, a subprogram body or a concurrent statement.
    [[nodiscard]] Object* call_in(Object& owner,
        const runtime::simir::ExecutionPoint& point, const std::size_t depth = 0U)
    {
        if (depth > 256U) {
            return nullptr;
        }
        const auto here = std::pair { point.source.line, point.source.column };
        const auto in_statement
            = [&](Object& holder, const vhdl::Statement& hir) -> Object* {
            std::vector<semantic::ExpressionId> expressions;
            statement_expressions(hir, expressions);
            for (const auto id : expressions) {
                if (const auto found = call_expression_at(id, point)) {
                    return expression_object(holder, *found);
                }
            }
            return nullptr;
        };
        if (owner.is_stmt && !owner.sequential && owner.statement != nullptr) {
            if (owner.kind == vhpiConcProcCallStmtK
                && source_position(owner.source) == here
                && same_file(source_file(owner.source), point)) {
                return &owner;
            }
            const std::function<Object*(const vhdl::Statement&, std::size_t)> walk
                = [&](const vhdl::Statement& hir,
                      const std::size_t level) -> Object* {
                if (auto* const found = in_statement(owner, hir)) {
                    return found;
                }
                if (level > 64U) {
                    return nullptr;
                }
                const auto descend
                    = [&](const std::vector<semantic::StatementId>& ids) -> Object* {
                    for (const auto id : ids) {
                        if (const auto* const child = hir_statement(id)) {
                            if (auto* const found = walk(*child, level + 1U)) {
                                return found;
                            }
                        }
                    }
                    return nullptr;
                };
                if (auto* const found = descend(hir.statements)) {
                    return found;
                }
                if (auto* const found = descend(hir.else_statements)) {
                    return found;
                }
                for (const auto& alternative : hir.alternatives) {
                    if (auto* const found = descend(alternative.statements)) {
                        return found;
                    }
                }
                return nullptr;
            };
            return walk(*owner.statement, 0U);
        }
        const auto statements = sequential_statements(owner);
        for (auto* const statement : statements) {
            if (statement->kind == vhpiSeqProcCallStmtK
                && source_position(statement->source) == here
                && same_file(source_file(statement->source), point)) {
                return statement;
            }
            // The conditions of an if statement belong to its branches.
            if (statement->statement != nullptr && statement->kind != vhpiIfStmtK) {
                if (auto* const found = in_statement(*statement, *statement->statement)) {
                    return found;
                }
            }
            if (statement->kind == vhpiIfStmtK || statement->kind == vhpiCaseStmtK) {
                const auto branches = branch_objects(*statement, ListKind::branches);
                for (auto* const branch : branches) {
                    if (statement->kind == vhpiIfStmtK && branch->condition) {
                        if (const auto found
                            = call_expression_at(*branch->condition, point)) {
                            return expression_object(*branch, *found);
                        }
                    }
                    if (auto* const inner = call_in(*branch, point, depth + 1U)) {
                        return inner;
                    }
                }
            }
            if (auto* const inner = call_in(*statement, point, depth + 1U)) {
                return inner;
            }
        }
        return nullptr;
    }

    // The subprogram call a call point is at: in the process that runs, or
    // in a subprogram body.
    [[nodiscard]] Object* call_at(const runtime::simir::ExecutionPoint& point)
    {
        const auto key = std::tuple { point.design_process, point.source.line,
            point.source.column };
        if (const auto found = call_at_.find(key); found != call_at_.end()) {
            return found->second;
        }
        Object* result = nullptr;
        if (auto* const process = process_object(point.design_process)) {
            result = call_in(*process, point);
        }
        for (auto* const body : all_bodies()) {
            if (result != nullptr) {
                break;
            }
            result = call_in(*body, point);
        }
        call_at_.emplace(key, result);
        return result;
    }

    // The subprogram a call object calls, when the design names one.
    [[nodiscard]] const vhdl::Declaration* called_declaration(
        const Object& call) const
    {
        if (call.expression != nullptr) {
            return callee_of(*call.expression);
        }
        return call.statement != nullptr ? procedure_callee(*call.statement)
                                         : nullptr;
    }

    // The start or end of a subprogram call (IEEE 1076-2019 21.3.4.5,
    // 21.3.4.6): the callbacks on the call there, and at a start those on
    // the declaration of the subprogram it calls, which report the call.
    void deliver_call(
        const int32_t reason, const runtime::simir::ExecutionPoint& point)
    {
        const auto here = std::pair { point.source.line, point.source.column };
        Object* call = nullptr;
        bool call_found = false;
        std::vector<std::pair<Callback*, std::uint64_t>> selected;
        for (auto* const callback : callbacks_) {
            if (callback->data.reason != reason) {
                continue;
            }
            if (callback->trigger != nullptr) {
                if (std::pair { callback->line, callback->column } == here
                    && same_file(callback->file, point)
                    && (callback->process == nullptr
                        || runs(*callback->process, point.design_process))) {
                    selected.emplace_back(callback, callback->order);
                }
                continue;
            }
            if (!call_found) {
                call_found = true;
                call = call_at(point);
            }
            if (call != nullptr) {
                const auto* const callee = called_declaration(*call);
                if (callee != nullptr && callback->object != nullptr
                    && callback->object->declaration != nullptr
                    && callback->object->declaration->id == callee->id) {
                    selected.emplace_back(callback, callback->order);
                }
            }
        }
        for (const auto& [callback, order] : selected) {
            if (live(callback, order)) {
                run(*callback,
                    callback->trigger != nullptr ? callback->reported : call);
            }
        }
    }

    // The process statement object of a design process, found among the
    // process statements of the whole hierarchy.
    [[nodiscard]] Object* process_object(const runtime::simir::ProcessId id)
    {
        if (const auto found = process_objects_.find(id);
            found != process_objects_.end()) {
            return found->second;
        }
        if (!all_processes_) {
            all_processes_.emplace();
            std::vector<Object*> pending;
            if (auto* const top = root()) {
                pending.push_back(top);
            }
            std::size_t visited = 0U;
            while (!pending.empty() && visited < 1'000'000U) {
                auto* const region = pending.back();
                pending.pop_back();
                ++visited;
                for (auto* const item : region_statements(*region)) {
                    if (item->kind == vhpiProcessStmtK
                        || (item->is_stmt && !item->sequential
                            && item->statement != nullptr)) {
                        all_processes_->push_back(item);
                    } else if (is_region(*item)) {
                        pending.push_back(item);
                    }
                }
            }
        }
        Object* result = nullptr;
        for (auto* const process : *all_processes_) {
            if (runs(*process, id)) {
                result = process;
                break;
            }
        }
        process_objects_.emplace(id, result);
        return result;
    }

    // A suspension reports the wait statement it executed, a resumption
    // the statement it resumes at; otherwise the process (IEEE 1076-2019
    // 21.3.4.3, 21.3.4.4).
    void deliver_process(
        const int32_t reason, const runtime::simir::ExecutionPoint& point)
    {
        std::vector<std::pair<Callback*, std::uint64_t>> selected;
        for (auto* const callback : callbacks_) {
            if (callback->data.reason == reason && callback->process != nullptr
                && runs(*callback->process, point.design_process)) {
                selected.emplace_back(callback, callback->order);
            }
        }
        for (const auto& [callback, order] : selected) {
            if (!live(callback, order)) {
                continue;
            }
            auto* reported = statement_in(*callback->process, point);
            if (reported == nullptr
                || (reason == vhpiCbSuspend
                    && reported->kind != vhpiWaitStmtK)) {
                reported = callback->process;
            }
            run(*callback, reported);
        }
    }

    // A driver is active when its process updates it, with or without a
    // change of its value (IEEE 1076-2019 14.7.2); the callbacks on it run
    // after the update phase.
    void ensure_driver_hook()
    {
        if (driver_token_ != 0U) {
            return;
        }
        driver_token_ = simulation_.add_driver_transaction_hook(
            [this](const runtime::simir::ProcessId process, const SignalId signal) {
                driver_active_.emplace(process, signal);
            });
    }

    // Driver transaction callbacks run for each driver that was updated, and
    // value-change callbacks when the update changed the driving value
    // (IEEE 1076-2019 21.3.2.2, 21.3.2.5).
    void flush_drivers()
    {
        if (driver_active_.empty()) {
            return;
        }
        const auto active = std::exchange(driver_active_, { });
        std::vector<std::pair<Callback*, std::uint64_t>> selected;
        for (auto* const callback : callbacks_) {
            if ((callback->data.reason != vhpiCbTransaction
                    && callback->data.reason != vhpiCbValueChange)
                || callback->object == nullptr
                || callback->object->kind != vhpiDriverK
                || !callback->object->signal || callback->object->parent == nullptr) {
                continue;
            }
            const auto process = design_process_of(*callback->object->parent);
            if (process
                && active.contains(
                    std::pair { *process, *callback->object->signal })) {
                selected.emplace_back(callback, callback->order);
            }
        }
        for (const auto& [callback, order] : selected) {
            if (!live(callback, order)) {
                continue;
            }
            if (callback->data.reason == vhpiCbValueChange) {
                auto bits = element_bits(*callback->object);
                if (callback->last && bits && *callback->last == *bits) {
                    continue;
                }
                callback->last = std::move(bits);
            }
            run(*callback);
        }
    }

    void ensure_transaction_hook(const SignalId signal)
    {
        // A transaction of a signal the v4 kernel owns reaches the scheduler
        // only while the signal is watched (docs/engine-v4-semantics.md S19).
        if (transaction_watch_.insert(signal).second) {
            const std::array signals { signal };
            simulation_.watch_signal_transactions(signals);
        }
        if (transaction_token_ != 0U) {
            return;
        }
        transaction_token_ = simulation_.add_signal_transaction_hook(
            [this](const SignalId active) {
                if (transaction_watch_.contains(active)) {
                    active_.insert(active);
                }
            });
    }

    // Whether forcing `width` bits of `signal` from `offset` forces the
    // trigger object: the target is the object or contains it
    // (IEEE 1076-2019 21.3.2.3).
    [[nodiscard]] bool forces(const Object& object, const ForceEvent& event)
    {
        if (!object.signal || *object.signal != event.signal) {
            return false;
        }
        const auto bits = object.type != nullptr ? width_of(*object.type) : 0U;
        return object.offset >= event.offset
            && object.offset + bits <= event.offset + event.width;
    }

    // The declared signal a force with no named target object refers to.
    [[nodiscard]] Object* signal_object(const SignalId signal)
    {
        for (const auto& object : objects_) {
            if (object->signal && *object->signal == signal
                && object->declaration != nullptr && object->offset == 0U
                && object->aliased == nullptr) {
                return object.get();
            }
        }
        const auto path = simulation_.signal_path(signal);
        if (!path) {
            return nullptr;
        }
        // The lookup is the tool's own; it leaves the error state as it was.
        const auto had_error = has_error_;
        const auto severity = error_severity_;
        auto message = error_message_;
        auto* const handle = handle_by_name(path->c_str(), nullptr);
        auto* const found = handle != nullptr ? object_of(handle) : nullptr;
        has_error_ = had_error;
        error_severity_ = severity;
        error_message_ = std::move(message);
        return found != nullptr && found->signal && *found->signal == signal
            ? found
            : nullptr;
    }

    // Force and release callbacks run once the update that applies them
    // has taken place, with the resulting value (IEEE 1076-2019 21.3.2.1).
    void flush_forces()
    {
        while (!forces_.empty()) {
            const auto events = std::exchange(forces_, { });
            for (const auto& event : events) {
                deliver_force(event);
            }
        }
    }

    void deliver_force(const ForceEvent& event)
    {
        const auto reason = event.release ? vhpiCbRelease : vhpiCbForce;
        std::vector<std::pair<Callback*, std::uint64_t>> selected;
        for (auto* const callback : callbacks_) {
            if (callback->data.reason == reason
                && (callback->object == nullptr
                    || forces(*callback->object, event))) {
                selected.emplace_back(callback, callback->order);
            }
        }
        Object* target = event.target;
        for (const auto& [callback, order] : selected) {
            if (!live(callback, order)) {
                continue;
            }
            if (callback->object != nullptr) {
                run(*callback);
                continue;
            }
            if (target == nullptr) {
                target = signal_object(event.signal);
                if (target == nullptr) {
                    return;
                }
            }
            run(*callback, target);
        }
    }

    // A signal is active in each cycle with a transaction on it
    // (IEEE 1076-2019 14.7.3.1, 21.3.2.5).
    void flush_transactions()
    {
        if (active_.empty()) {
            return;
        }
        const auto active = std::exchange(active_, { });
        std::vector<std::pair<Callback*, std::uint64_t>> selected;
        for (auto* const callback : callbacks_) {
            if (callback->data.reason == vhpiCbTransaction
                && callback->object != nullptr && callback->object->signal
                && callback->object->kind != vhpiDriverK
                && active.contains(*callback->object->signal)) {
                selected.emplace_back(callback, callback->order);
            }
        }
        for (const auto& [callback, order] : selected) {
            if (live(callback, order)) {
                run(*callback);
            }
        }
    }

    // --- time and control ----------------------------------------------------------

    [[nodiscard]] std::uint64_t now_femtoseconds() const
    {
        return simulation_.now() * femtoseconds_per_tick_;
    }

    void get_time(vhpiTimeT* const time, long* const cycles)
    {
        if (time != nullptr) {
            *time = to_vhpi_time(now_femtoseconds());
        }
        if (cycles != nullptr) {
            *cycles = static_cast<long>(
                simulation_.now() == cycle_time_ ? cycles_ : 0U);
        }
    }

    // Tn, the time of the next simulation cycle: the current time for a
    // pending delta cycle, TIME'HIGH when no activity remains
    // (IEEE 1076-2019 23.14, 14.7.5.1).
    [[nodiscard]] int get_next_time(vhpiTimeT* const time)
    {
        if (phase_ != vhpiSimulationPhase) {
            error(vhpiError,
                "vhpi_get_next_time is available only during simulation");
            return 1;
        }
        const auto next = simulation_.next_pending_time();
        if (time != nullptr) {
            *time = next ? to_vhpi_time(*next * femtoseconds_per_tick_)
                         : vhpiTimeT { std::numeric_limits<int32_t>::max(),
                               std::numeric_limits<uint32_t>::max() };
        }
        return next ? 0 : vhpiNoActivity;
    }

    [[nodiscard]] int control(const vhpiSimControlT command)
    {
        switch (command) {
        case vhpiStop:
            stopping_ = true;
            simulation_.request_stop();
            return 0;
        case vhpiFinish: {
            // The tool enters its termination phase, without
            // vhpiCbEndOfSimulation callbacks (IEEE 1076-2019 23.5).
            runtime::SystemVerilogVpiControlRequest finish;
            finish.operation = runtime::SystemVerilogVpiControlOperation::Finish;
            if (!simulation_.systemverilog_vpi_control().submit(finish)) {
                error(vhpiError, "cannot finish the simulation");
                return 1;
            }
            stopping_ = true;
            finishing_ = true;
            return 0;
        }
        default:
            error(vhpiError, "unsupported simulation control");
            return 1;
        }
    }

    // --- handles -------------------------------------------------------------------

    [[nodiscard]] int compare(const vhpiHandleT left, const vhpiHandleT right)
    {
        if (left == right) {
            return 1;
        }
        auto* const a = handle_of(left);
        auto* const b = handle_of(right);
        if (a == nullptr || b == nullptr || a->tag != HandleTag::object
            || b->tag != HandleTag::object) {
            return 0;
        }
        // A subtype with no constraint of its own denotes its base type.
        return canonical(static_cast<Object*>(a))
                == canonical(static_cast<Object*>(b))
            ? 1
            : 0;
    }

    [[nodiscard]] static Object* canonical(Object* object)
    {
        return object;
    }

    // Objects are shared and live as long as the service; releasing one is
    // a no-op, except for the handle a callback is running with
    // (IEEE 1076-2019 23.29).
    [[nodiscard]] int release(const vhpiHandleT handle)
    {
        auto* const base = handle_of(handle);
        if (base == nullptr) {
            error(vhpiError, "invalid handle");
            return 1;
        }
        if (handle == dispatching_) {
            error(vhpiError,
                "cannot release this handle as it is owned by the system");
            return 1;
        }
        if (base->tag == HandleTag::iterator) {
            const auto found = std::ranges::find_if(iterators_,
                [&](const auto& stored) { return stored.get() == base; });
            if (found != iterators_.end()) {
                iterators_.erase(found);
            }
        }
        return 0;
    }

    [[nodiscard]] Object* make_object(const vhpiClassKindT kind,
        const std::string_view name, Object* const parent)
    {
        auto object = std::make_unique<Object>();
        object->kind = kind;
        // The letter case of the Name property is not specified (IEEE
        // 1076-2019 19.4.3); it keeps the declared case like CaseName.
        object->case_name = std::string { name };
        object->name = std::string { name };
        object->parent = parent;
        objects_.push_back(std::move(object));
        return objects_.back().get();
    }

    // --- the design hierarchy ------------------------------------------------------

    [[nodiscard]] const vhdl::Unit* unit_for(const semantic::UnitId id) const
    {
        const auto found
            = std::ranges::find(hir_.units(), id, &vhdl::Unit::id);
        return found == hir_.units().end() ? nullptr : &*found;
    }

    [[nodiscard]] const vhdl::Unit* entity_of(const vhdl::Unit& unit) const
    {
        if (unit.kind != vhdl::UnitKind::architecture) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(hir_.units(), [&](const auto& c) {
            return c.kind == vhdl::UnitKind::entity
                && same_name(c.library, unit.library)
                && same_name(c.name, unit.primary_name);
        });
        return found == hir_.units().end() ? nullptr : &*found;
    }

    // The declarations of an instance: the entity's generics and ports,
    // then the entity's and the architecture's declarative items.
    void collect_region(Object& region)
    {
        const auto add = [&](const vhdl::Unit& unit) {
            for (const auto id : unit.declarations) {
                if (const auto* const declaration
                    = hir_.find_declaration(id)) {
                    region.region_declarations.push_back(declaration);
                }
            }
        };
        if (region.entity != nullptr) {
            add(*region.entity);
        }
        if (region.unit != nullptr) {
            add(*region.unit);
        }
    }

    [[nodiscard]] Object* instance_object(
        const semantic::design::InstanceOccurrence& instance, Object* parent)
    {
        const auto path = std::string { design_.path(instance.path) };
        if (const auto found = instances_.find(path);
            found != instances_.end()) {
            return found->second;
        }
        const auto& specialization
            = design_.specializations().at(instance.specialization.value());
        const auto* const unit = unit_for(specialization.unit);
        const auto* const entity = unit != nullptr ? entity_of(*unit) : nullptr;
        const auto name = instance.name.empty()
            ? (entity != nullptr ? entity->name : specialization.name)
            : instance.name;
        auto* const object = make_object(
            parent == nullptr ? vhpiRootInstK : vhpiCompInstStmtK,
            parent == nullptr && entity != nullptr ? entity->name : name,
            parent);
        object->path = path;
        object->unit = unit;
        object->entity = entity;
        object->specialization = &specialization;
        object->occurrence = &instance;
        if (parent != nullptr) {
            object->source = instance.source;
        }
        if (parent == nullptr) {
            object->full_name = ":" + object->name;
            object->full_case_name = ":" + object->case_name;
        } else {
            object->full_name = parent->full_name + ":" + object->name;
            object->full_case_name
                = parent->full_case_name + ":" + object->case_name;
        }
        collect_region(*object);
        instances_.emplace(path, object);
        return object;
    }

    [[nodiscard]] Object* root()
    {
        if (root_ != nullptr) {
            return root_;
        }
        // The first VHDL root; in a design with no VHDL root, the first
        // root, whose VHDL descendants are then reachable.
        const semantic::design::InstanceOccurrence* fallback = nullptr;
        for (const auto& instance : design_.instances()) {
            if (instance.parent || !design_unit_instance(instance)) {
                continue;
            }
            const auto& specialization = design_.specializations().at(
                instance.specialization.value());
            if (specialization.language != semantic::Language::vhdl) {
                if (fallback == nullptr) {
                    fallback = &instance;
                }
                continue;
            }
            root_ = instance_object(instance, nullptr);
            break;
        }
        if (root_ == nullptr && fallback != nullptr && has_vhdl_instance()) {
            root_ = instance_object(*fallback, nullptr);
        }
        return root_;
    }

    // A root other than the root instance, by name: the other tops of a
    // design with several.
    [[nodiscard]] Object* other_root(const std::string_view name)
    {
        for (const auto& instance : design_.instances()) {
            if (instance.parent || !design_unit_instance(instance)) {
                continue;
            }
            const auto path = design_.path(instance.path);
            if (same_name(path, name)) {
                auto* const object = instance_object(instance, nullptr);
                return object != root() ? object : nullptr;
            }
        }
        return nullptr;
    }

    // An instance of an entity, architecture, configuration or module, not
    // a package's or another synthetic one.
    [[nodiscard]] bool design_unit_instance(
        const semantic::design::InstanceOccurrence& instance) const
    {
        const auto& specialization
            = design_.specializations().at(instance.specialization.value());
        const auto& units = simulation_.semantics().units();
        if (specialization.unit.value() >= units.size()) {
            return false;
        }
        switch (units.at(specialization.unit.value()).kind) {
        case semantic::UnitKind::vhdl_entity:
        case semantic::UnitKind::vhdl_architecture:
        case semantic::UnitKind::vhdl_configuration:
        case semantic::UnitKind::verilog_module:
        case semantic::UnitKind::systemverilog_interface:
        case semantic::UnitKind::systemverilog_program:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool has_vhdl_instance() const
    {
        return std::ranges::any_of(
            design_.specializations(), [](const auto& specialization) {
                return specialization.language == semantic::Language::vhdl;
            });
    }

    // The instance of a package (IEEE 1076-2019 19.4.2 vhpiPackInstK), a
    // root region named `:package`.
    [[nodiscard]] Object* package_instance(const std::string_view name)
    {
        const auto key = lower(name);
        if (const auto found = packages_.find(key); found != packages_.end()) {
            return found->second;
        }
        const auto unit = std::ranges::find_if(hir_.units(), [&](const auto& u) {
            return u.kind == vhdl::UnitKind::package && same_name(u.name, name);
        });
        if (unit == hir_.units().end()) {
            // STANDARD is part of every design (IEEE 1076-2019 16.3) though
            // its declarations are predefined rather than analyzed.
            if (key != "standard") {
                return nullptr;
            }
            auto* const object = make_object(vhpiPackInstK, "STANDARD", nullptr);
            object->path = "std.standard";
            object->full_name = ":" + object->name;
            object->full_case_name = object->full_name;
            packages_.emplace(key, object);
            return object;
        }
        auto* const object = make_object(vhpiPackInstK, unit->name, nullptr);
        object->unit = &*unit;
        object->path = lower(unit->library) + "." + lower(unit->name);
        object->full_name = ":" + object->name;
        object->full_case_name = ":" + object->case_name;
        collect_region(*object);
        packages_.emplace(key, object);
        return object;
    }

    [[nodiscard]] std::vector<Object*> child_instances(Object& region)
    {
        std::vector<Object*> result;
        if (region.kind == vhpiPackInstK) {
            return result;
        }
        for (const auto& instance : design_.instances()) {
            if (!instance.parent) {
                continue;
            }
            // An instance in a block or generate statement is in that
            // statement's region (IEEE 1076-2019 19.4.2).
            const auto path = design_.path(instance.path);
            const auto dot = path.rfind('.');
            if (dot == std::string_view::npos
                || path.substr(0U, dot) != region.path) {
                continue;
            }
            result.push_back(instance_object(instance, &region));
        }
        return result;
    }

    // Whether elaboration made any object or instance under `path`.
    [[nodiscard]] bool elaborated_path(const std::string_view path)
    {
        const auto under = [&](const std::string_view name) {
            return name.size() > path.size() && name.starts_with(path)
                && name[path.size()] == '.';
        };
        return std::ranges::any_of(simulation_.design().signals(),
                   [&](const auto& info) { return under(info.name); })
            || std::ranges::any_of(design_.instances(),
                [&](const auto& instance) {
                    return under(design_.path(instance.path));
                });
    }

    // A block statement, a generate statement occurrence, or (with no
    // index) a for-generate statement, whose occurrences `indexed`
    // selects (IEEE 1076-2019 19.4.2, 11.8).
    [[nodiscard]] Object* generate_object(Object& region,
        const vhdl::GenerateRegion& generate,
        const std::optional<std::int64_t> index)
    {
        auto name = generate.label;
        auto path = region.path + "." + lower(generate.label);
        if (index) {
            name += "(" + std::to_string(*index) + ")";
            path += "[" + std::to_string(*index) + "]";
        }
        const auto key = "@generate:" + lower(name);
        if (const auto found = region.children.find(key);
            found != region.children.end()) {
            return found->second;
        }
        const auto kind = generate.kind == vhdl::GenerateKind::block
            ? vhpiBlockStmtK
            : generate.kind == vhdl::GenerateKind::iterative
            ? vhpiForGenerateK
            : vhpiIfGenerateK;
        auto* const object = make_object(kind, name, &region);
        object->path = std::move(path);
        object->unit = region.unit;
        object->generate = &generate;
        object->generate_index = index;
        object->full_name = region.full_name + ":" + object->name;
        object->full_case_name
            = region.full_case_name + ":" + object->case_name;
        for (const auto id : generate.declarations) {
            if (const auto* const declaration = hir_.find_declaration(id)) {
                object->region_declarations.push_back(declaration);
            }
        }
        // The generate parameter is a constant of the occurrence (IEEE
        // 1076-2019 11.8, 19.4.2 vhpiParamDecl).
        if (index && !generate.iterator.empty()) {
            auto* const parameter
                = make_object(vhpiConstDeclK, generate.iterator, object);
            parameter->full_name = object->full_name + ":" + parameter->name;
            parameter->full_case_name
                = object->full_case_name + ":" + parameter->case_name;
            parameter->type = standard_type("integer");
            const auto width = parameter->type != nullptr
                    && parameter->type->type_data != nullptr
                    && parameter->type->type_data->width != 0U
                ? parameter->type->type_data->width
                : 32U;
            parameter->constant = PackedLogic4::from_aval_bval(
                width, static_cast<std::uint64_t>(*index), 0U);
            object->children.emplace(lower(generate.iterator), parameter);
            object->children.emplace("@param", parameter);
        }
        region.children.emplace(key, object);
        return object;
    }

    // The statements of `generates` that elaboration made, in order; a
    // for-generate statement's occurrences are the parameter values its
    // elaborated objects are named by.
    [[nodiscard]] std::vector<Object*> generate_occurrences(Object& region,
        const vhdl::GenerateRegion& generate)
    {
        std::vector<Object*> result;
        const auto base = region.path + "." + lower(generate.label);
        if (generate.kind != vhdl::GenerateKind::iterative) {
            if (generate.kind == vhdl::GenerateKind::block
                || elaborated_path(base)) {
                result.push_back(generate_object(region, generate, std::nullopt));
            }
            return result;
        }
        std::vector<std::int64_t> indexes;
        const auto collect = [&](const std::string_view name) {
            if (!name.starts_with(base) || name.size() <= base.size()
                || name[base.size()] != '[') {
                return;
            }
            const auto digits = name.substr(base.size() + 1U);
            std::int64_t index { };
            const auto [last, failure] = std::from_chars(
                digits.data(), digits.data() + digits.size(), index);
            if (failure != std::errc { } || last == digits.data() + digits.size()
                || *last != ']') {
                return;
            }
            if (std::ranges::find(indexes, index) == indexes.end()) {
                indexes.push_back(index);
            }
        };
        for (const auto& info : simulation_.design().signals()) {
            collect(info.name);
        }
        for (const auto& instance : design_.instances()) {
            collect(design_.path(instance.path));
        }
        // A generate that declares no signal is known by its processes.
        for (const auto& process : design_.processes()) {
            collect(process.name);
        }
        for (const auto index : indexes) {
            result.push_back(generate_object(region, generate, index));
        }
        return result;
    }

    // The block and generate statements directly in a region.
    [[nodiscard]] std::vector<Object*> child_statements(Object& region)
    {
        std::vector<Object*> result;
        const std::vector<vhdl::GenerateRegion>* generates = nullptr;
        if (region.generate != nullptr) {
            if (region.kind == vhpiForGenerateK && !region.generate_index) {
                return result;
            }
            generates = &region.generate->nested;
        } else if (region.unit != nullptr && region.kind != vhpiPackInstK) {
            generates = &region.unit->generates;
        }
        if (generates == nullptr) {
            return result;
        }
        for (const auto& generate : *generates) {
            auto occurrences = generate_occurrences(region, generate);
            result.insert(result.end(), occurrences.begin(), occurrences.end());
        }
        return result;
    }

    // --- processes and statements (IEEE 1076-2019 19.11) -----------------------------

    [[nodiscard]] const vhdl::Statement* hir_statement(
        const semantic::StatementId id) const
    {
        return semantic::find_by_id(hir_.statements(), id);
    }

    [[nodiscard]] const vhdl::Process* hir_process(
        const semantic::ProcessId id) const
    {
        return semantic::find_by_id(hir_.processes(), id);
    }

    // Where a statement begins, for the textual order of 19.4.2.3; what has
    // no source sorts last.
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> source_position(
        const std::optional<semantic::SourceSpanId> source) const
    {
        const auto& spans = simulation_.semantics().source_spans();
        if (!source || !source->valid() || source->value() >= spans.size()) {
            return { std::numeric_limits<std::uint32_t>::max(), 0U };
        }
        const auto& begin = spans.at(source->value()).begin;
        return { begin.line, begin.column };
    }

    // The class of a concurrent statement that is equivalent to a process
    // (IEEE 1076-2019 11.4 to 11.7, Figure 38).
    [[nodiscard]] static std::optional<vhpiClassKindT> concurrent_kind(
        const vhdl::Statement& statement)
    {
        switch (statement.kind) {
        case vhdl::StatementKind::signal_assignment:
            return vhpiSimpleSigAssignStmtK;
        case vhdl::StatementKind::conditional:
            if (statement.conditional_assignment) {
                return vhpiCondSigAssignStmtK;
            }
            return std::nullopt;
        case vhdl::StatementKind::selection:
            if (statement.selected_assignment) {
                return vhpiSelectSigAssignStmtK;
            }
            return std::nullopt;
        case vhdl::StatementKind::assertion:
            return vhpiConcAssertStmtK;
        case vhdl::StatementKind::procedure_call:
            return vhpiConcProcCallStmtK;
        default:
            return std::nullopt;
        }
    }

    // The class of a sequential statement (IEEE 1076-2019 Figure 42). A
    // sequential block, force and release statement have no class.
    [[nodiscard]] std::optional<vhpiClassKindT> sequential_kind(
        const vhdl::Statement& statement) const
    {
        switch (statement.kind) {
        case vhdl::StatementKind::signal_assignment:
            return vhpiSeqSigAssignStmtK;
        case vhdl::StatementKind::variable_assignment:
            return vhpiVarAssignStmtK;
        case vhdl::StatementKind::conditional:
            return vhpiIfStmtK;
        case vhdl::StatementKind::selection:
            return vhpiCaseStmtK;
        case vhdl::StatementKind::loop:
            if (!statement.loop_variable.empty()) {
                return vhpiForLoopK;
            }
            return forever_loop(statement) ? vhpiForeverLoopK : vhpiWhileLoopK;
        case vhdl::StatementKind::exit_loop:
            return vhpiExitStmtK;
        case vhdl::StatementKind::next_loop:
            return vhpiNextStmtK;
        case vhdl::StatementKind::return_statement:
            return vhpiReturnStmtK;
        case vhdl::StatementKind::procedure_call:
            return vhpiSeqProcCallStmtK;
        case vhdl::StatementKind::assertion:
            return vhpiSeqAssertStmtK;
        case vhdl::StatementKind::report:
            return vhpiReportStmtK;
        case vhdl::StatementKind::wait_statement:
            return vhpiWaitStmtK;
        case vhdl::StatementKind::null_statement:
            return vhpiNullStmtK;
        default:
            return std::nullopt;
        }
    }

    // A loop statement without an iteration scheme is a plain loop (class
    // foreverLoop, IEEE 1076-2019 10.10); the frontend gives it the
    // condition `true` at the position of the statement's first token, where
    // the condition of `while true loop` has a position of its own.
    [[nodiscard]] bool forever_loop(const vhdl::Statement& statement) const
    {
        if (!statement.condition) {
            return true;
        }
        const auto* const condition = hir_expression(*statement.condition);
        return condition != nullptr
            && condition->kind == vhdl::ExpressionKind::boolean_literal
            && same_name(condition->text, "true")
            && source_position(condition->source)
            == source_position(statement.source);
    }

    // The driver the frontend adds to a guarded block for GUARD.
    [[nodiscard]] bool guard_driver(
        const Object& region, const vhdl::Statement& statement) const
    {
        if (region.generate == nullptr
            || region.generate->kind != vhdl::GenerateKind::block
            || !region.generate->condition
            || statement.kind != vhdl::StatementKind::signal_assignment
            || !statement.target) {
            return false;
        }
        const auto* const target = hir_expression(*statement.target);
        return target != nullptr && same_name(target->text, "guard");
    }

    // The processes and the concurrent statements equivalent to processes
    // that a declarative part holds, in textual order.
    [[nodiscard]] std::vector<StatementMember> unit_members(
        const std::vector<semantic::ProcessId>& processes,
        const std::vector<semantic::StatementId>& statements,
        const Object& region) const
    {
        std::vector<StatementMember> result;
        for (const auto id : processes) {
            if (const auto* const process = hir_process(id)) {
                result.push_back({ process, nullptr });
            }
        }
        for (const auto id : statements) {
            const auto* const statement = hir_statement(id);
            if (statement != nullptr && concurrent_kind(*statement)
                && !guard_driver(region, *statement)) {
                result.push_back({ nullptr, statement });
            }
        }
        std::ranges::stable_sort(
            result, { }, [&](const StatementMember& member) {
                return source_position(member.process != nullptr
                        ? member.process->source
                        : member.statement->source);
            });
        return result;
    }

    // The process and concurrent statement objects of a region: those of
    // the entity declaration, then of the architecture body, a block or a
    // generate statement. An unlabeled one has the implicit label `_P<n>`
    // (IEEE 1076-2019 19.4.2.3), which counts the unlabeled ones only, those
    // of the architecture after those of the entity.
    [[nodiscard]] std::vector<Object*>& concurrent_statements(Object& region)
    {
        auto& result = region.lists[ListKind::statements];
        if (!result.empty() || !is_region(region)
            || region.kind == vhpiPackInstK) {
            return result;
        }
        std::size_t sequence { };
        const auto add = [&](const std::vector<StatementMember>& members) {
            for (const auto& member : members) {
                const auto& declared_label = member.process != nullptr
                    ? member.process->name
                    : member.statement->label;
                const bool implicit = declared_label.empty()
                    || declared_label == "<process>";
                const auto label = implicit
                    ? "_P" + std::to_string(sequence++)
                    : declared_label;
                result.push_back(member_object(region, member, label, implicit));
            }
        };
        if (region.generate != nullptr) {
            if (region.kind != vhpiForGenerateK || region.generate_index) {
                add(unit_members(region.generate->processes,
                    region.generate->concurrent_statements, region));
            }
        } else if (region.unit != nullptr) {
            if (region.entity != nullptr) {
                add(unit_members(region.entity->processes,
                    region.entity->concurrent_statements, region));
            }
            add(unit_members(region.unit->processes,
                region.unit->concurrent_statements, region));
        }
        return result;
    }

    [[nodiscard]] Object* member_object(Object& region,
        const StatementMember& member, const std::string& label,
        const bool implicit)
    {
        const void* const identity = member.process != nullptr
            ? static_cast<const void*>(member.process)
            : static_cast<const void*>(member.statement);
        const auto key
            = "@stmt:" + std::to_string(reinterpret_cast<std::uintptr_t>(identity));
        if (const auto found = region.children.find(key);
            found != region.children.end()) {
            return found->second;
        }
        const auto kind = member.process != nullptr
            ? vhpiProcessStmtK
            : *concurrent_kind(*member.statement);
        auto* const object = make_object(kind, label, &region);
        object->process = member.process;
        object->statement = member.statement;
        object->is_stmt = true;
        object->label_name = implicit ? std::string { } : label;
        object->source = member.process != nullptr ? member.process->source
                                                   : member.statement->source;
        object->path = region.path + "." + lower(label);
        object->full_name = region.full_name + ":" + object->name;
        object->full_case_name = region.full_case_name + ":" + object->case_name;
        if (member.process != nullptr) {
            object->body = &member.process->statements;
            for (const auto id : member.process->declarations) {
                if (const auto* const declaration = hir_.find_declaration(id)) {
                    object->region_declarations.push_back(declaration);
                }
            }
            std::size_t next_loop { };
            number_loops(member.process->statements, next_loop);
        }
        region.children.emplace(key, object);
        return object;
    }

    // The sequence numbers of the loop statements of a process, in textual
    // order, labeled ones included (IEEE 1076-2019 19.4.2.2).
    void number_loops(const std::vector<semantic::StatementId>& statements,
        std::size_t& next)
    {
        for (const auto id : statements) {
            const auto* const statement = hir_statement(id);
            if (statement == nullptr) {
                continue;
            }
            if (statement->kind == vhdl::StatementKind::loop) {
                loop_numbers_.emplace(statement, next++);
            }
            number_loops(statement->statements, next);
            number_loops(statement->else_statements, next);
            for (const auto& alternative : statement->alternatives) {
                number_loops(alternative.statements, next);
            }
        }
    }

    // The statements of a region (vhpiStmts): its processes and concurrent
    // statements, then its component instantiations, block and generate
    // statements.
    [[nodiscard]] std::vector<Object*> region_statements(Object& region)
    {
        auto result = concurrent_statements(region);
        const auto instances = child_instances(region);
        result.insert(result.end(), instances.begin(), instances.end());
        const auto statements = child_statements(region);
        result.insert(result.end(), statements.begin(), statements.end());
        return result;
    }

    [[nodiscard]] static Object* enclosing_process(Object& object) noexcept
    {
        for (auto* current = &object; current != nullptr;
             current = current->parent) {
            if (current->kind == vhpiProcessStmtK) {
                return current;
            }
        }
        return nullptr;
    }

    // The sequential statements of a process, loop statement or branch
    // (vhpiSeqStmts).
    [[nodiscard]] std::vector<Object*>& sequential_statements(Object& owner)
    {
        auto& result = owner.lists[ListKind::statements];
        if (!result.empty() || owner.body == nullptr) {
            return result;
        }
        for (const auto id : *owner.body) {
            const auto* const statement = hir_statement(id);
            if (statement != nullptr && sequential_kind(*statement)) {
                result.push_back(sequential_object(owner, *statement));
            }
        }
        return result;
    }

    [[nodiscard]] Object* sequential_object(
        Object& owner, const vhdl::Statement& statement)
    {
        const auto key = "@seq:"
            + std::to_string(reinterpret_cast<std::uintptr_t>(&statement));
        if (const auto found = owner.children.find(key);
            found != owner.children.end()) {
            return found->second;
        }
        const bool loop = statement.kind == vhdl::StatementKind::loop;
        // A loop statement is named by its label, explicit or implicit
        // (IEEE 1076-2019 19.4.3); other statements by none.
        const auto& declared_label = loop && !statement.loop_label.empty()
            ? statement.loop_label
            : statement.label;
        auto label = declared_label;
        if (loop && label.empty()) {
            if (const auto found = loop_numbers_.find(&statement);
                found != loop_numbers_.end()) {
                label = "_L" + std::to_string(found->second);
            }
        }
        auto* const object = make_object(*sequential_kind(statement), label, &owner);
        object->statement = &statement;
        object->is_stmt = true;
        object->sequential = true;
        object->label_name = declared_label;
        object->source = statement.source;
        // A procedure call is named by the procedure (IEEE 1076-2019 19.4.3).
        if (statement.kind == vhdl::StatementKind::procedure_call) {
            const auto name = simple_name(!statement.procedure.spelling.empty()
                    ? std::string_view { statement.procedure.spelling }
                    : statement.procedure.text());
            object->name = name;
            object->case_name = name;
        }
        if (const auto* const process = enclosing_process(owner)) {
            if (loop && !label.empty()) {
                object->full_name = process->full_name + ":" + object->name;
                object->full_case_name
                    = process->full_case_name + ":" + object->case_name;
                object->path = process->path + "." + lower(label);
            }
        }
        if (loop) {
            object->body = &statement.statements;
            for (const auto id : statement.declarations) {
                if (const auto* const declaration = hir_.find_declaration(id)) {
                    object->region_declarations.push_back(declaration);
                }
            }
            // The loop parameter of a for loop (IEEE 1076-2019 19.11
            // Figure 40).
            if (object->kind == vhpiForLoopK
                && !object->region_declarations.empty()) {
                if (auto* const parameter
                    = declared(*object, *object->region_declarations.front())) {
                    object->children.emplace("@param", parameter);
                }
            }
        }
        owner.children.emplace(key, object);
        return object;
    }

    // The branches of an if statement: the if and every elsif, then the else.
    // An else part that holds one if statement is an elsif only when the
    // parser made it one; `else if` is an else branch with an if statement.
    [[nodiscard]] std::vector<BranchPart> if_branches(
        const vhdl::Statement& statement) const
    {
        std::vector<BranchPart> result;
        const auto* node = &statement;
        while (node != nullptr) {
            result.push_back({ node, node->condition, &node->statements });
            const vhdl::Statement* next = nullptr;
            if (node->else_statements.size() == 1U) {
                const auto* const child = hir_statement(node->else_statements.front());
                if (child != nullptr
                    && child->kind == vhdl::StatementKind::conditional
                    && child->elsif_branch && !child->conditional_assignment) {
                    next = child;
                }
            }
            if (next == nullptr && !node->else_statements.empty()) {
                result.push_back({ nullptr, std::nullopt, &node->else_statements });
            }
            node = next;
        }
        return result;
    }

    // The conditional waveforms of a concurrent conditional signal
    // assignment (IEEE 1076-2019 11.6): the assignment of each part, with
    // its condition, none for the last part.
    [[nodiscard]] std::vector<BranchPart> conditional_waveform_parts(
        const vhdl::Statement& statement) const
    {
        std::vector<BranchPart> result;
        const auto* node = &statement;
        while (node != nullptr && node->kind == vhdl::StatementKind::conditional
            && node->conditional_assignment) {
            if (!node->statements.empty()) {
                if (const auto* const leaf = hir_statement(node->statements.front())) {
                    result.push_back({ leaf, node->condition, nullptr });
                }
            }
            const vhdl::Statement* next = nullptr;
            if (!node->else_statements.empty()) {
                const auto* const child = hir_statement(node->else_statements.front());
                if (child != nullptr
                    && child->kind == vhdl::StatementKind::conditional
                    && child->conditional_assignment) {
                    next = child;
                } else if (child != nullptr) {
                    result.push_back({ child, std::nullopt, nullptr });
                }
            }
            node = next;
        }
        return result;
    }

    // The signal assignment a concurrent conditional or selected signal
    // assignment holds first, which has the target and the delay mechanism.
    [[nodiscard]] const vhdl::Statement& assignment_of(
        const vhdl::Statement& statement) const
    {
        const vhdl::Statement* leaf = nullptr;
        if (statement.kind == vhdl::StatementKind::conditional
            && statement.conditional_assignment && !statement.statements.empty()) {
            leaf = hir_statement(statement.statements.front());
        } else if (statement.kind == vhdl::StatementKind::selection
            && statement.selected_assignment && !statement.alternatives.empty()
            && !statement.alternatives.front().statements.empty()) {
            leaf = hir_statement(statement.alternatives.front().statements.front());
        }
        return leaf != nullptr ? *leaf : statement;
    }

    // The branches of an if or case statement (vhpiBranchs), the
    // conditional waveforms (vhpiCondWaveforms) or the selected waveforms
    // (vhpiSelectWaveforms) of a concurrent signal assignment.
    [[nodiscard]] std::vector<Object*>& branch_objects(
        Object& owner, const ListKind kind)
    {
        auto& result = owner.lists[kind];
        if (!result.empty() || owner.statement == nullptr) {
            return result;
        }
        const auto& statement = *owner.statement;
        const auto add = [&](const vhpiClassKindT object_kind) -> Object& {
            auto* const object = make_object(object_kind, "", &owner);
            object->part = result.size();
            result.push_back(object);
            return *object;
        };
        if (kind == ListKind::branches && owner.kind == vhpiIfStmtK) {
            for (const auto& part : if_branches(statement)) {
                auto& object = add(vhpiBranchK);
                object.statement = part.node != nullptr ? part.node : &statement;
                object.condition = part.condition;
                object.body = part.body;
                if (part.node != nullptr) {
                    object.source = part.node->source;
                }
                object.sequential = true;
            }
        } else if (kind == ListKind::branches) {
            for (const auto& alternative : statement.alternatives) {
                auto& object = add(vhpiBranchK);
                object.statement = &statement;
                object.alternative = &alternative;
                object.body = &alternative.statements;
                object.source = alternative.source;
                object.sequential = true;
            }
        } else if (kind == ListKind::cond_waveforms) {
            for (const auto& part : conditional_waveform_parts(statement)) {
                auto& object = add(vhpiCondWaveformK);
                object.statement = part.node;
                object.condition = part.condition;
                // The frontend's assignment of the first part carries no
                // source of its own; its waveform does.
                object.source = part.node->waveform.empty()
                    ? part.node->source
                    : part.node->waveform.front().source;
            }
        } else {
            for (const auto& alternative : statement.alternatives) {
                auto& object = add(vhpiSelectWaveformK);
                object.alternative = &alternative;
                object.statement = alternative.statements.empty()
                    ? nullptr
                    : hir_statement(alternative.statements.front());
                object.source = alternative.source;
            }
        }
        return result;
    }

    // The waveform elements of a signal assignment (vhpiWaveformElems); a
    // conditional or selected waveform holds the assignment of its part.
    [[nodiscard]] std::vector<Object*>& waveform_elements(Object& owner)
    {
        auto& result = owner.lists[ListKind::waveform_elems];
        const auto* const assignment = owner.statement;
        if (!result.empty() || assignment == nullptr) {
            return result;
        }
        const auto count = assignment->waveform.empty()
            ? (assignment->unaffected ? std::size_t { 1U } : std::size_t { })
            : assignment->waveform.size();
        for (std::size_t index = 0U; index < count; ++index) {
            auto* const object = make_object(vhpiWaveformElemK, "", &owner);
            object->statement = assignment;
            object->part = index;
            object->source = index < assignment->waveform.size()
                ? assignment->waveform[index].source
                : assignment->source;
            result.push_back(object);
        }
        return result;
    }

    // The choices of a case branch or selected waveform (vhpiChoices); the
    // choices that are ranges have no object.
    [[nodiscard]] std::vector<Object*>& choice_objects(Object& owner)
    {
        auto& result = owner.lists[ListKind::choices];
        if (!result.empty() || owner.alternative == nullptr) {
            return result;
        }
        for (const auto choice : owner.alternative->choices) {
            if (auto* const object = expression_object(owner, choice)) {
                result.push_back(object);
            }
        }
        if (owner.alternative->is_default) {
            auto* const others = make_object(vhpiOthersK, "others", &owner);
            others->source = owner.alternative->source;
            result.push_back(others);
        }
        return result;
    }

    // The signals a process or concurrent statement is sensitive to, or a
    // wait statement waits on (vhpiSensitivitys, vhpiSigNames). The
    // sensitivity of a concurrent statement is implicit and not listed.
    [[nodiscard]] std::vector<Object*> sensitivity_objects(Object& owner)
    {
        std::vector<Object*> result;
        const std::vector<vhdl::Sensitivity>* sensitivities = nullptr;
        if (owner.process != nullptr) {
            sensitivities = &owner.process->sensitivities;
        } else if (owner.statement != nullptr && owner.is_stmt) {
            sensitivities = &owner.statement->sensitivities;
        }
        if (sensitivities == nullptr) {
            return result;
        }
        for (const auto& sensitivity : *sensitivities) {
            Object* object = nullptr;
            if (sensitivity.expression) {
                object = expression_object(owner, *sensitivity.expression);
            }
            if (object == nullptr) {
                object = lookup_name(owner, sensitivity.signal);
            }
            if (object != nullptr) {
                result.push_back(object);
            }
        }
        return result;
    }

    // --- connectivity (IEEE 1076-2019 19.12) ---------------------------------------

    // Whether an object denotes a signal or a part of one.
    [[nodiscard]] static bool is_signal_object(const Object& object) noexcept
    {
        return object.signal && object.kind != vhpiDriverK
            && object.type != nullptr && object.type->type_data != nullptr;
    }

    // The scalar subelements of a signal object in left-to-right order;
    // each is a basic signal (19.12.2.1).
    void basic_leaves(Object& object, std::vector<Object*>& result,
        const std::size_t depth)
    {
        if (depth > 64U || result.size() >= maximum_list_size
            || !is_signal_object(object)) {
            return;
        }
        switch (object.type->type_data->storage) {
        case Storage::array:
            for (auto* const element : indexed_names(&object)) {
                basic_leaves(*element, result, depth + 1U);
            }
            break;
        case Storage::record:
            for (auto* const element : selected_names(&object)) {
                basic_leaves(*element, result, depth + 1U);
            }
            break;
        default:
            result.push_back(&object);
            break;
        }
    }

    // The basic signals of a signal (vhpiBasicSignals).
    [[nodiscard]] std::vector<Object*> basic_signals(Object& object)
    {
        std::vector<Object*> result;
        basic_leaves(object, result, 0U);
        return result;
    }

    // The identifier at the start of a name's text.
    [[nodiscard]] static std::string_view leading_identifier(
        std::string_view text)
    {
        while (!text.empty()
            && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
            text.remove_prefix(1U);
        }
        if (!text.empty() && text.front() == '\\') {
            const auto end = text.find('\\', 1U);
            return end == std::string_view::npos ? text
                                                 : text.substr(0U, end + 1U);
        }
        std::size_t length { };
        while (length < text.size()
            && (std::isalnum(static_cast<unsigned char>(text[length])) != 0
                || text[length] == '_')) {
            ++length;
        }
        return text.substr(0U, length);
    }

    // The signal part a target or actual designates, down to its longest
    // static prefix (IEEE 1076-2019 14.7.2): literal indexes and bounds select
    // an element or slice, any other index or bound leaves the prefix.
    [[nodiscard]] Object* target_object(Object& scope,
        const semantic::ExpressionId id, const std::size_t depth)
    {
        const auto* const expression = hir_expression(id);
        if (expression == nullptr || depth > 16U) {
            return nullptr;
        }
        switch (expression->kind) {
        case vhdl::ExpressionKind::index: {
            if (expression->operands.size() < 2U) {
                return nullptr;
            }
            auto* const prefix
                = target_object(scope, expression->operands[0], depth + 1U);
            if (prefix == nullptr) {
                return nullptr;
            }
            std::vector<std::int64_t> indexes;
            for (std::size_t operand = 1U; operand < expression->operands.size();
                 ++operand) {
                const auto index = integer_literal(expression->operands[operand]);
                if (!index) {
                    return prefix;
                }
                indexes.push_back(*index);
            }
            auto* const element = indexed(*prefix, indexes);
            return element != nullptr ? element : prefix;
        }
        case vhdl::ExpressionKind::slice: {
            if (expression->operands.size() != 3U) {
                return nullptr;
            }
            auto* const prefix
                = target_object(scope, expression->operands[0], depth + 1U);
            if (prefix == nullptr) {
                return nullptr;
            }
            const auto left = integer_literal(expression->operands[1]);
            const auto right = integer_literal(expression->operands[2]);
            if (!left || !right) {
                return prefix;
            }
            auto* const part = sliced(*prefix,
                Range { *left, *right, same_name(expression->text, "downto") });
            return part != nullptr ? part : prefix;
        }
        default:
            return expression_object(scope, id);
        }
    }

    // The formal parameter a call's argument is associated with: by name,
    // or by position (IEEE 1076-2019 4.2.2.3, 9.3.7).
    [[nodiscard]] const vhdl::Declaration* call_formal(
        const vhdl::Statement& call, const std::size_t index) const
    {
        if (index >= call.procedure_arguments.size()) {
            return nullptr;
        }
        const auto* const callee = procedure_callee(call);
        if (callee == nullptr || !callee->callable) {
            return nullptr;
        }
        const auto& formals = callee->callable->formals;
        const auto& argument = call.procedure_arguments[index];
        if (argument.formal && !argument.formal->spelling.empty()) {
            const auto wanted = leading_identifier(argument.formal->spelling);
            for (const auto id : formals) {
                const auto* const candidate = hir_.find_declaration(id);
                if (candidate != nullptr && same_name(candidate->name, wanted)) {
                    return candidate;
                }
            }
            return nullptr;
        }
        return index < formals.size() ? hir_.find_declaration(formals[index])
                                      : nullptr;
    }

    // Whether a formal is a signal parameter of mode out.
    [[nodiscard]] static bool out_signal_parameter(
        const vhdl::Declaration* const formal) noexcept
    {
        return formal != nullptr
            && formal->object_class == vhdl::ObjectClass::signal
            && formal->direction == vhdl::Direction::output;
    }

    // The signals that the out and inout signal parameters of a procedure
    // call are associated with; the process holds a driver for each
    // (IEEE 1076-2019 4.2.2.3).
    void collect_call_targets(Object& scope, const vhdl::Statement& call,
        std::vector<Object*>& result)
    {
        for (std::size_t index = 0U; index < call.procedure_arguments.size();
             ++index) {
            const auto* const formal = call_formal(call, index);
            if (formal == nullptr
                || formal->object_class != vhdl::ObjectClass::signal
                || (formal->direction != vhdl::Direction::output
                    && formal->direction != vhdl::Direction::inout)) {
                continue;
            }
            if (auto* const object = target_object(
                    scope, call.procedure_arguments[index].actual, 0U);
                object != nullptr && object->signal) {
                basic_leaves(*object, result, 0U);
            }
        }
    }

    // The signals a target names, as basic signals: names, indexed names,
    // slices and aggregates of those, to the longest static prefix.
    void collect_targets(Object& scope, const semantic::ExpressionId id,
        std::vector<Object*>& result, const std::size_t depth)
    {
        const auto* const expression = hir_expression(id);
        if (expression == nullptr || depth > 16U) {
            return;
        }
        if (expression->kind == vhdl::ExpressionKind::aggregate) {
            for (const auto& association : expression->associations) {
                collect_targets(scope, association.value, result, depth + 1U);
            }
            for (const auto operand : expression->operands) {
                collect_targets(scope, operand, result, depth + 1U);
            }
            return;
        }
        if (auto* const object = target_object(scope, id, 0U);
            object != nullptr && object->signal) {
            basic_leaves(*object, result, 0U);
        }
    }

    // The signals the signal assignments of a statement and of the
    // statements inside it target (14.7.2), and those its procedure calls
    // associate with out and inout signal parameters. The bodies of called
    // procedures are not searched.
    void collect_statement_targets(Object& owner, const vhdl::Statement& statement,
        std::vector<Object*>& result, const std::size_t depth)
    {
        if (depth > 256U) {
            return;
        }
        if (statement.kind == vhdl::StatementKind::signal_assignment
            && statement.target) {
            collect_targets(owner, *statement.target, result, 0U);
        }
        if (statement.kind == vhdl::StatementKind::procedure_call) {
            collect_call_targets(owner, statement, result);
        }
        collect_assigned(owner, statement.statements, result, depth + 1U);
        collect_assigned(owner, statement.else_statements, result, depth + 1U);
        for (const auto& alternative : statement.alternatives) {
            collect_assigned(owner, alternative.statements, result, depth + 1U);
        }
    }

    void collect_assigned(Object& owner,
        const std::vector<semantic::StatementId>& statements,
        std::vector<Object*>& result, const std::size_t depth)
    {
        for (const auto id : statements) {
            if (const auto* const statement = hir_statement(id)) {
                collect_statement_targets(owner, *statement, result, depth);
            }
        }
    }

    // The basic signal of a declared signal that a scalar subelement name
    // stands for: an element of a slice is the element of the signal.
    [[nodiscard]] Object* canonical_basic(Object& leaf)
    {
        auto* root = &leaf;
        while (root->parent != nullptr
            && (root->kind == vhpiIndexedNameK || root->kind == vhpiSelectedNameK
                || root->kind == vhpiSliceNameK)) {
            root = root->parent;
        }
        if (root == &leaf) {
            return &leaf;
        }
        for (auto* const candidate : basic_signals(*root)) {
            if (candidate->signal == leaf.signal && candidate->offset == leaf.offset) {
                return candidate;
            }
        }
        return &leaf;
    }

    // The drivers of a process or of a concurrent statement equivalent to
    // one: a driver for each scalar subelement of each signal it assigns
    // (IEEE 1076-2019 14.7.2, 19.12.1).
    [[nodiscard]] std::vector<Object*>& process_drivers(Object& owner)
    {
        auto& result = owner.lists[ListKind::drivers];
        if (!result.empty() || !owner.is_stmt || owner.sequential
            || (owner.process == nullptr && owner.statement == nullptr)) {
            return result;
        }
        std::vector<Object*> targets;
        if (owner.process != nullptr) {
            collect_assigned(owner, owner.process->statements, targets, 0U);
        } else {
            collect_statement_targets(owner, *owner.statement, targets, 0U);
        }
        std::set<std::pair<SignalId, std::size_t>> seen;
        for (auto* const target : targets) {
            if (!seen.emplace(*target->signal, target->offset).second) {
                continue;
            }
            auto* const driver = make_object(vhpiDriverK, "", &owner);
            driver->basic = canonical_basic(*target);
            driver->signal = target->signal;
            driver->offset = target->offset;
            driver->type = target->type;
            result.push_back(driver);
        }
        return result;
    }

    // The innermost region that declares the signal an object denotes.
    [[nodiscard]] static Object* declaring_region(const Object& object) noexcept
    {
        auto* region = object.parent;
        while (region != nullptr && !is_region(*region)) {
            region = region->parent;
        }
        return region;
    }

    // Whether a basic signal lies within the bits of another signal object.
    [[nodiscard]] static bool covers(const Object& whole, const Object& part)
    {
        if (!whole.signal || !part.signal || *whole.signal != *part.signal
            || whole.type == nullptr || part.type == nullptr) {
            return false;
        }
        return whole.offset <= part.offset
            && part.offset + width_of(*part.type)
            <= whole.offset + width_of(*whole.type);
    }

    // The processes and concurrent statements of a region and of the
    // blocks and generate statements inside it; the places a signal declared
    // in the region can be assigned or read directly (IEEE 1076-2019 14.7.2).
    void region_members(Object& region, std::vector<Object*>& result,
        const std::size_t depth)
    {
        if (depth > 64U) {
            return;
        }
        for (auto* const member : concurrent_statements(region)) {
            if (member->process != nullptr || member->statement != nullptr) {
                result.push_back(member);
            }
        }
        for (auto* const inner : child_statements(region)) {
            region_members(*inner, result, depth + 1U);
        }
    }

    // The drivers of a basic signal (vhpiDrivers, vhpiLocalContributors).
    [[nodiscard]] std::vector<Object*> signal_drivers(Object& basic)
    {
        std::vector<Object*> result;
        auto* const region = declaring_region(basic);
        if (region == nullptr || !basic.signal) {
            return result;
        }
        std::vector<Object*> members;
        region_members(*region, members, 0U);
        for (auto* const member : members) {
            for (auto* const driver : process_drivers(*member)) {
                if (driver->signal == basic.signal && driver->offset == basic.offset) {
                    result.push_back(driver);
                }
            }
        }
        return result;
    }

    // The drivers of a signal: those of its basic signals.
    [[nodiscard]] std::vector<Object*> drivers_of(Object& object)
    {
        std::vector<Object*> result;
        if (object.kind == vhpiProcessStmtK || (object.is_stmt && !object.sequential)) {
            return process_drivers(object);
        }
        for (auto* const basic : basic_signals(object)) {
            const auto drivers = signal_drivers(*basic);
            result.insert(result.end(), drivers.begin(), drivers.end());
        }
        return result;
    }

    // --- port associations (IEEE 1076-2019 19.10, 19.12.2) ---------------------------

    // What a port association element connects: the port, or the part of one,
    // that the formal part designates and the signal, part of a signal or
    // expression that the actual part designates, with the conversions of
    // either part.
    struct PortConnection {
        Object* formal { };
        Object* actual { };
        Object* formal_conversion { };
        Object* actual_conversion { };
        bool actual_signal { };
        bool open { };
    };

    // The port map of an instance or block statement.
    [[nodiscard]] const std::vector<vhdl::Association>* port_map_of(
        const Object& region) const
    {
        if (region.generate != nullptr) {
            return region.generate->kind == vhdl::GenerateKind::block
                ? &region.generate->port_map
                : nullptr;
        }
        if (region.kind != vhpiCompInstStmtK || region.occurrence == nullptr
            || !region.occurrence->source_instance || region.unit == nullptr) {
            return nullptr;
        }
        const auto* const instance = semantic::find_by_id(
            hir_.instances(), *region.occurrence->source_instance);
        return instance != nullptr ? &instance->port_map : nullptr;
    }

    // The association elements of a port map, in order (vhpiPortAssocs).
    [[nodiscard]] std::vector<Object*>& port_associations(Object& region)
    {
        auto& result = region.lists[ListKind::port_assocs];
        if (!result.empty()) {
            return result;
        }
        const auto* const map = port_map_of(region);
        if (map == nullptr) {
            return result;
        }
        for (std::size_t index = 0U; index < map->size(); ++index) {
            const auto& association = (*map)[index];
            auto* const object = make_object(vhpiAssocElemK, "", &region);
            object->position = static_cast<std::int64_t>(index);
            object->port_association = &association;
            object->actual = association.expression;
            object->named
                = association.formal && !association.formal->spelling.empty();
            object->source = association.source;
            result.push_back(object);
        }
        return result;
    }

    // The part of a port that `rest` (what follows the port name in a formal
    // part) selects: nothing, a literal index or a literal slice.
    [[nodiscard]] Object* designated_part(Object& base, std::string_view rest)
    {
        while (!rest.empty() && rest.front() == ' ') {
            rest.remove_prefix(1U);
        }
        while (!rest.empty() && rest.back() == ' ') {
            rest.remove_suffix(1U);
        }
        if (rest.empty()) {
            return &base;
        }
        if (!rest.starts_with('(') || !rest.ends_with(')')) {
            return nullptr;
        }
        rest = rest.substr(1U, rest.size() - 2U);
        if (const auto range = parse_slice(rest)) {
            return sliced(base, *range);
        }
        std::vector<std::int64_t> indexes;
        while (!rest.empty()) {
            while (!rest.empty() && rest.front() == ' ') {
                rest.remove_prefix(1U);
            }
            std::int64_t value { };
            const auto parsed
                = std::from_chars(rest.data(), rest.data() + rest.size(), value);
            if (parsed.ec != std::errc { }) {
                return nullptr;
            }
            indexes.push_back(value);
            rest.remove_prefix(static_cast<std::size_t>(parsed.ptr - rest.data()));
            while (!rest.empty() && rest.front() == ' ') {
                rest.remove_prefix(1U);
            }
            if (!rest.empty()) {
                if (rest.front() != ',') {
                    return nullptr;
                }
                rest.remove_prefix(1U);
            }
        }
        return indexes.empty() ? nullptr : indexed(base, indexes);
    }

    // A conversion in a port association (IEEE 1076-2019 19.12.2.2): a type
    // conversion (class typeConv) or a conversion function call (convFunc).
    [[nodiscard]] Object* conversion_object(Object& scope,
        const std::string_view name, const bool function, const std::string& key)
    {
        if (const auto found = scope.children.find(key);
            found != scope.children.end()) {
            return found->second;
        }
        auto* const object
            = make_object(function ? vhpiConvFuncK : vhpiTypeConvK, name, &scope);
        object->full_name = object->name;
        object->full_case_name = object->case_name;
        scope.children.emplace(key, object);
        return object;
    }

    // The port part a formal part designates; a conversion function or type
    // mark around the port name is recorded in `conversion`.
    [[nodiscard]] Object* named_formal(Object& region, std::string_view text,
        Object*& conversion, const std::size_t depth)
    {
        const auto name = leading_identifier(text);
        if (name.empty() || depth > 4U) {
            return nullptr;
        }
        auto rest = text.substr(
            static_cast<std::size_t>(name.data() - text.data()) + name.size());
        for (const auto* const declaration : region.region_declarations) {
            if (declaration->form == vhdl::DeclarationForm::port
                && same_name(declaration->name, name)) {
                auto* const port = declared(region, *declaration);
                return port != nullptr ? designated_part(*port, rest) : nullptr;
            }
        }
        // `conversion(port part)`.
        while (!rest.empty() && rest.front() == ' ') {
            rest.remove_prefix(1U);
        }
        while (!rest.empty() && rest.back() == ' ') {
            rest.remove_suffix(1U);
        }
        if (!rest.starts_with('(') || !rest.ends_with(')')) {
            return nullptr;
        }
        auto* const part = named_formal(
            region, rest.substr(1U, rest.size() - 2U), conversion, depth + 1U);
        if (part != nullptr && conversion == nullptr) {
            const auto* const type = lookup_name(region, name);
            const bool function = standard_type(std::string { name }) == nullptr
                && (type == nullptr || !is_type(*type));
            conversion = conversion_object(region, name, function,
                "@formal-conversion:" + lower(name));
        }
        return part;
    }

    // The actual part of an association: a signal, a part of one, an
    // expression, or a conversion of a signal (scope: the region the
    // instantiation is in).
    void resolve_actual(Object& scope, const semantic::ExpressionId id,
        PortConnection& connection)
    {
        const auto* const expression = hir_expression(id);
        if (expression == nullptr) {
            return;
        }
        if (expression->kind == vhdl::ExpressionKind::call
            && expression->operands.size() == 1U
            && !expression->text.empty() && expression->text.front() != '@'
            && expression->text.find('\'') == std::string::npos) {
            auto* const inner
                = target_object(scope, expression->operands.front(), 0U);
            if (inner != nullptr && inner->signal && is_signal_object(*inner)) {
                connection.actual = inner;
                connection.actual_signal = true;
                connection.actual_conversion = conversion_object(scope,
                    simple_name(expression->referenced_name
                            && !expression->referenced_name->spelling.empty()
                        ? std::string_view { expression->referenced_name->spelling }
                        : std::string_view { expression->text }),
                    is_function_call(*expression),
                    "@conversion:" + std::to_string(id.value()));
                return;
            }
        }
        auto* const object = target_object(scope, id, 0U);
        connection.actual = object;
        connection.actual_signal
            = object != nullptr && object->signal && is_signal_object(*object);
    }

    [[nodiscard]] const PortConnection& port_connection(Object& association)
    {
        if (const auto found = connections_.find(&association);
            found != connections_.end()) {
            return found->second;
        }
        PortConnection connection;
        auto* const region = association.parent;
        const auto* const source = association.port_association;
        if (region != nullptr && source != nullptr) {
            connection.open = source->kind == vhdl::AssociationKind::open
                || source->kind == vhdl::AssociationKind::default_box
                || !source->expression;
            if (association.named) {
                connection.formal = named_formal(*region,
                    source->formal->spelling, connection.formal_conversion, 0U);
            } else {
                std::size_t seen { };
                for (const auto* const declaration : region->region_declarations) {
                    if (declaration->form == vhdl::DeclarationForm::port
                        && seen++ == static_cast<std::size_t>(association.position)) {
                        connection.formal = declared(*region, *declaration);
                        break;
                    }
                }
            }
            if (!connection.open && region->parent != nullptr) {
                resolve_actual(
                    *region->parent, *source->expression, connection);
            }
        }
        return connections_.emplace(&association, connection).first->second;
    }

    // A port seen as an inPort or an outPort (IEEE 1076-2019 19.12.1,
    // Figure 46): the same signal, as an object of the port class.
    [[nodiscard]] Object* port_view(Object& port, const vhpiClassKindT kind)
    {
        const std::string key = kind == vhpiInPortK ? "@inport" : "@outport";
        if (const auto found = port.children.find(key);
            found != port.children.end()) {
            return found->second;
        }
        auto* const object = make_object(kind, port.name, port.parent);
        object->case_name = port.case_name;
        object->path = port.path;
        object->full_name = port.full_name;
        object->full_case_name = port.full_case_name;
        object->declaration = port.declaration;
        object->signal = port.signal;
        object->offset = port.offset;
        object->type = port.type;
        object->indexes = port.indexes;
        object->suffix = port.suffix;
        object->position = port.position;
        object->source = port.source;
        port.children.emplace(key, object);
        return object;
    }

    // The aspects of a port: the part read from inside (an inPort) and the
    // part driven from inside (an outPort), IEEE 1076-2019 6.5.6.3.
    [[nodiscard]] static bool port_reads(const vhdl::Declaration* const port) noexcept
    {
        return port != nullptr && port->form == vhdl::DeclarationForm::port
            && !port->linkage
            && (port->direction == vhdl::Direction::input
                || port->direction == vhdl::Direction::inout
                || port->direction == vhdl::Direction::buffer);
    }

    [[nodiscard]] static bool port_drives(const vhdl::Declaration* const port) noexcept
    {
        return port != nullptr && port->form == vhdl::DeclarationForm::port
            && !port->linkage
            && (port->direction == vhdl::Direction::output
                || port->direction == vhdl::Direction::inout
                || port->direction == vhdl::Direction::buffer);
    }

    // The instances and the blocks with port maps in a region and in the
    // blocks and generate statements inside it, not inside instances: the
    // places that name a signal of the region as an actual.
    [[nodiscard]] const std::vector<Object*>& connection_owners(Object& region)
    {
        if (const auto found = owners_.find(&region); found != owners_.end()) {
            return found->second;
        }
        std::vector<Object*> result;
        std::vector<Object*> pending { &region };
        std::size_t visited { };
        while (!pending.empty() && visited++ < 100'000U) {
            auto* const current = pending.back();
            pending.pop_back();
            for (auto* const instance : child_instances(*current)) {
                result.push_back(instance);
            }
            for (auto* const inner : child_statements(*current)) {
                if (const auto* const map = port_map_of(*inner);
                    map != nullptr && !map->empty()) {
                    result.push_back(inner);
                }
                pending.push_back(inner);
            }
        }
        return owners_.emplace(&region, std::move(result)).first->second;
    }

    // The scalar subelement of `to` that sits where `basic` sits in `from`;
    // both are the two sides of a connection of the same type.
    [[nodiscard]] Object* counterpart(
        Object& from, Object& to, const Object& basic)
    {
        const auto from_leaves = basic_signals(from);
        const auto to_leaves = basic_signals(to);
        if (from_leaves.size() != to_leaves.size()) {
            return nullptr;
        }
        for (std::size_t index = 0U; index < from_leaves.size(); ++index) {
            if (from_leaves[index]->signal == basic.signal
                && from_leaves[index]->offset == basic.offset) {
                return to_leaves[index];
            }
        }
        return nullptr;
    }

    // The contributors (IEEE 1076-2019 19.12.2.2) or loads (19.12.2.3) of a
    // basic signal that are not drivers or processes: the ports and
    // conversions of the associations that name it, the association of the
    // port it is, and default expressions.
    [[nodiscard]] std::vector<Object*> connection_objects(
        Object& basic, const bool contributors)
    {
        std::vector<Object*> result;
        auto* const region = declaring_region(basic);
        if (region == nullptr || !basic.signal) {
            return result;
        }
        const auto add = [&](Object* const object) {
            if (object != nullptr
                && std::ranges::find(result, object) == result.end()) {
                result.push_back(object);
            }
        };
        // The signal as an actual: the ports it is associated with.
        for (auto* const owner : connection_owners(*region)) {
            for (auto* const association : port_associations(*owner)) {
                const auto& connection = port_connection(*association);
                if (connection.formal == nullptr || connection.actual == nullptr
                    || !connection.actual_signal
                    || !covers(*connection.actual, basic)) {
                    continue;
                }
                const auto* const port = connection.formal->declaration;
                if (contributors) {
                    // A port driving the signal, or the conversion of its
                    // formal part.
                    if (!port_drives(port)) {
                        continue;
                    }
                    if (connection.formal_conversion != nullptr) {
                        add(connection.formal_conversion);
                    } else if (auto* const leaf = counterpart(
                                   *connection.actual, *connection.formal, basic)) {
                        add(port_view(*leaf, vhpiOutPortK));
                    }
                } else if (port_reads(port)) {
                    // A port reading the signal, or the conversion of its
                    // actual part.
                    if (connection.actual_conversion != nullptr) {
                        add(connection.actual_conversion);
                    } else if (auto* const leaf = counterpart(
                                   *connection.actual, *connection.formal, basic)) {
                        add(port_view(*leaf, vhpiInPortK));
                    }
                }
            }
        }
        // The signal as a port: the actual it is associated with in the map
        // of its instance or block.
        const auto* const declaration = basic.declaration;
        if (declaration != nullptr
            && declaration->form == vhdl::DeclarationForm::port
            && port_map_of(*region) != nullptr) {
            const bool in_side = declaration->direction == vhdl::Direction::input
                || declaration->direction == vhdl::Direction::inout;
            bool associated { };
            for (auto* const association : port_associations(*region)) {
                const auto& connection = port_connection(*association);
                if (connection.formal == nullptr || connection.open
                    || !covers(*connection.formal, basic)) {
                    continue;
                }
                associated = true;
                if (contributors ? !in_side : !port_drives(declaration)) {
                    continue;
                }
                if (contributors && connection.actual_conversion != nullptr) {
                    add(connection.actual_conversion);
                } else if (!contributors && connection.formal_conversion != nullptr) {
                    add(connection.formal_conversion);
                } else if (connection.actual_signal) {
                    add(counterpart(*connection.formal, *connection.actual, basic));
                } else {
                    add(connection.actual);
                }
            }
            // An unassociated port takes its default expression.
            if (contributors && in_side && !associated
                && declaration->initializer) {
                add(expression_object(*region, *declaration->initializer));
            }
        }
        return result;
    }

    // The signals an expression reads. A name with a non-literal index reads
    // the whole prefix.
    void collect_reads(Object& scope, const semantic::ExpressionId id,
        std::vector<Object*>& result, const std::size_t depth)
    {
        const auto* const expression = hir_expression(id);
        if (expression == nullptr || depth > 64U) {
            return;
        }
        if (expression->kind == vhdl::ExpressionKind::name
            || expression->kind == vhdl::ExpressionKind::index) {
            if (auto* const object = expression_object(scope, id);
                object != nullptr && object->signal) {
                result.push_back(object);
                return;
            }
        }
        for (const auto operand : expression->operands) {
            collect_reads(scope, operand, result, depth + 1U);
        }
        for (const auto& association : expression->associations) {
            collect_reads(scope, association.value, result, depth + 1U);
        }
    }

    void collect_sensitivity_reads(Object& owner,
        const std::vector<vhdl::Sensitivity>& sensitivities,
        std::vector<Object*>& result)
    {
        for (const auto& sensitivity : sensitivities) {
            Object* object = nullptr;
            if (sensitivity.expression) {
                object = expression_object(owner, *sensitivity.expression);
            }
            if (object == nullptr) {
                object = lookup_name(owner, sensitivity.signal);
            }
            if (object != nullptr && object->signal) {
                result.push_back(object);
            }
        }
    }

    void collect_statement_reads(Object& owner, const vhdl::Statement& statement,
        std::vector<Object*>& result, const std::size_t depth)
    {
        if (depth > 256U) {
            return;
        }
        const auto read = [&](const std::optional<semantic::ExpressionId>& id) {
            if (id) {
                collect_reads(owner, *id, result, 0U);
            }
        };
        read(statement.value);
        read(statement.condition);
        read(statement.loop_initial);
        read(statement.loop_limit);
        read(statement.report);
        read(statement.severity);
        read(statement.guard);
        read(delay_expression(statement.delay));
        read(delay_expression(statement.rejection_limit));
        for (const auto& element : statement.waveform) {
            collect_reads(owner, element.value, result, 0U);
            read(delay_expression(element.delay));
        }
        for (std::size_t index = 0U; index < statement.procedure_arguments.size();
             ++index) {
            // An actual of a signal parameter of mode out is not read.
            if (out_signal_parameter(call_formal(statement, index))) {
                continue;
            }
            collect_reads(
                owner, statement.procedure_arguments[index].actual, result, 0U);
        }
        collect_sensitivity_reads(owner, statement.sensitivities, result);
        const auto inner = [&](const std::vector<semantic::StatementId>& ids) {
            for (const auto id : ids) {
                if (const auto* const nested = hir_statement(id)) {
                    collect_statement_reads(owner, *nested, result, depth + 1U);
                }
            }
        };
        inner(statement.statements);
        inner(statement.else_statements);
        for (const auto& alternative : statement.alternatives) {
            inner(alternative.statements);
        }
    }

    // The signals a process or concurrent statement reads, its implicit or
    // explicit sensitivity included.
    [[nodiscard]] const std::vector<Object*>& member_reads(Object& member)
    {
        if (const auto found = reads_cache_.find(&member);
            found != reads_cache_.end()) {
            return found->second;
        }
        std::vector<Object*> result;
        if (member.process != nullptr) {
            collect_sensitivity_reads(member, member.process->sensitivities, result);
            for (const auto id : member.process->statements) {
                if (const auto* const statement = hir_statement(id)) {
                    collect_statement_reads(member, *statement, result, 0U);
                }
            }
        } else if (member.statement != nullptr) {
            collect_statement_reads(member, *member.statement, result, 0U);
        }
        return reads_cache_.emplace(&member, std::move(result)).first->second;
    }

    // The processes and concurrent statements that read a basic signal
    // (vhpiLocalLoads): those of the declaring region and the blocks and
    // generates inside it. Port associations and conversions are not loads
    // here.
    [[nodiscard]] std::vector<Object*> signal_loads(Object& basic)
    {
        std::vector<Object*> result;
        auto* const region = declaring_region(basic);
        if (region == nullptr || !basic.signal) {
            return result;
        }
        std::vector<Object*> members;
        region_members(*region, members, 0U);
        for (auto* const member : members) {
            const auto& reads = member_reads(*member);
            if (std::ranges::any_of(reads, [&](const Object* const read) {
                    return covers(*read, basic);
                })) {
                result.push_back(member);
            }
        }
        return result;
    }

    // The design process that runs a process or concurrent statement object.
    // A concurrent statement's equivalent process (IEEE 1076-2019 11.1) is
    // named by its label, or for an unlabeled one `concurrent_<n>` with n
    // increasing in textual order; it has no source process.
    [[nodiscard]] std::optional<runtime::simir::ProcessId> design_process_of(
        const Object& owner) const
    {
        if (owner.parent == nullptr) {
            return std::nullopt;
        }
        const auto prefix = owner.parent->path + ".";
        const auto direct_child = [&](const std::string& name) {
            return name.starts_with(prefix)
                && name.find('.', prefix.size()) == std::string::npos;
        };
        if (owner.process != nullptr) {
            for (const auto& occurrence : design_.processes()) {
                if (occurrence.source_process
                    && occurrence.source_process->value()
                        == owner.process->id.value()
                    && direct_child(occurrence.name)) {
                    return static_cast<runtime::simir::ProcessId>(
                        occurrence.runtime_index);
                }
            }
            return std::nullopt;
        }
        if (owner.statement == nullptr) {
            return std::nullopt;
        }
        if (!owner.statement->label.empty()) {
            const auto name = prefix + lower(owner.statement->label);
            for (const auto& occurrence : design_.processes()) {
                if (!occurrence.source_process && occurrence.name == name) {
                    return static_cast<runtime::simir::ProcessId>(
                        occurrence.runtime_index);
                }
            }
            return std::nullopt;
        }
        // The rank of this statement among the unlabeled concurrent
        // statements of its region, and the unlabeled occurrences there in
        // the order of their sequence numbers.
        const auto found = owner.parent->lists.find(ListKind::statements);
        if (found == owner.parent->lists.end()) {
            return std::nullopt;
        }
        std::size_t rank { };
        bool seen { };
        for (const auto* const member : found->second) {
            if (member == &owner) {
                seen = true;
                break;
            }
            if (member->process == nullptr && member->statement != nullptr
                && member->statement->label.empty()) {
                ++rank;
            }
        }
        if (!seen) {
            return std::nullopt;
        }
        const auto unlabeled_prefix = prefix + "concurrent_";
        std::vector<std::pair<std::size_t, std::uint32_t>> unlabeled;
        for (const auto& occurrence : design_.processes()) {
            if (occurrence.source_process
                || !occurrence.name.starts_with(unlabeled_prefix)
                || !direct_child(occurrence.name)) {
                continue;
            }
            const auto digits = std::string_view { occurrence.name }.substr(
                unlabeled_prefix.size());
            std::size_t order { };
            const auto [end, error] = std::from_chars(
                digits.data(), digits.data() + digits.size(), order);
            if (error == std::errc { } && end == digits.data() + digits.size()) {
                unlabeled.emplace_back(order, occurrence.runtime_index);
            }
        }
        std::ranges::sort(unlabeled);
        return rank < unlabeled.size()
            ? std::optional { static_cast<runtime::simir::ProcessId>(
                  unlabeled[rank].second) }
            : std::nullopt;
    }

    // The value a driver contributes now (IEEE 1076-2019 14.7.2), as the
    // bits of its basic signal.
    [[nodiscard]] std::optional<PackedLogic4> driver_bits(const Object& driver)
    {
        if (driver.parent == nullptr || !driver.signal) {
            return std::nullopt;
        }
        const auto process = design_process_of(*driver.parent);
        if (!process) {
            return std::nullopt;
        }
        try {
            return simulation_.read_driver(*process, *driver.signal);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    // The object a name denotes, searched from `scope` outward through the
    // processes, loops and regions that enclose it.
    [[nodiscard]] Object* lookup_name(Object& scope, const std::string_view text)
    {
        if (text.empty()) {
            return nullptr;
        }
        const std::string name { text };
        for (auto* current = &scope; current != nullptr;
             current = current->parent) {
            if (!is_declarative(*current)) {
                continue;
            }
            if (const auto found = handle_by_name(name.c_str(), to_handle(current))) {
                return object_of(found);
            }
        }
        return nullptr;
    }

    // The object an expression stands for: a name that denotes a declared
    // object or a part of one with literal indexes, or an integer literal.
    // Other expressions have no object.
    [[nodiscard]] Object* expression_object(
        Object& scope, const semantic::ExpressionId id)
    {
        const auto* const expression = hir_expression(id);
        if (expression == nullptr) {
            return nullptr;
        }
        switch (expression->kind) {
        case vhdl::ExpressionKind::name: {
            const std::string_view text = expression->referenced_name
                    && !expression->referenced_name->spelling.empty()
                ? std::string_view { expression->referenced_name->spelling }
                : std::string_view { expression->text };
            return lookup_name(scope, text);
        }
        case vhdl::ExpressionKind::index: {
            if (expression->operands.size() < 2U) {
                return nullptr;
            }
            auto* const prefix = expression_object(scope, expression->operands[0]);
            if (prefix == nullptr) {
                return nullptr;
            }
            std::vector<std::int64_t> indexes;
            for (std::size_t operand = 1U; operand < expression->operands.size();
                 ++operand) {
                const auto index = integer_literal(expression->operands[operand]);
                if (!index) {
                    // An index that is no literal: a name without a value.
                    return dynamic_name(scope, id, *expression, *prefix);
                }
                indexes.push_back(*index);
            }
            return indexed(*prefix, indexes);
        }
        case vhdl::ExpressionKind::slice: {
            if (expression->operands.size() < 3U) {
                return nullptr;
            }
            auto* const prefix = expression_object(scope, expression->operands[0]);
            if (prefix == nullptr) {
                return nullptr;
            }
            // A slice with literal bounds is the sliceName of the model.
            const auto left = integer_literal(expression->operands[1]);
            const auto right = integer_literal(expression->operands[2]);
            if (left && right) {
                return sliced(*prefix,
                    Range { *left, *right, expression->text == "downto" });
            }
            return dynamic_name(scope, id, *expression, *prefix);
        }
        case vhdl::ExpressionKind::integer_literal:
            return literal_object(id);
        case vhdl::ExpressionKind::unary:
            if (integer_literal(id)) {
                return literal_object(id);
            }
            return expression_class_object(scope, id, *expression);
        default:
            return expression_class_object(scope, id, *expression);
        }
    }

    [[nodiscard]] Object* literal_object(const semantic::ExpressionId id)
    {
        if (const auto found = literal_objects_.find(id.value());
            found != literal_objects_.end()) {
            return found->second;
        }
        const auto value = integer_literal(id);
        if (!value) {
            return nullptr;
        }
        auto* const object
            = make_object(vhpiIntLiteralK, std::to_string(*value), nullptr);
        object->full_name = object->name;
        object->full_case_name = object->case_name;
        object->type = standard_type("integer");
        const auto width = object->type != nullptr
                && object->type->type_data != nullptr
                && object->type->type_data->width != 0U
            ? object->type->type_data->width
            : 32U;
        object->constant = PackedLogic4::from_aval_bval(
            width, static_cast<std::uint64_t>(*value), 0U);
        literal_objects_.emplace(id.value(), object);
        return object;
    }

    // --- expression objects (IEEE 1076-2019 19.8, Figures 23 to 26) ----------------

    [[nodiscard]] static std::string simple_name(std::string_view text)
    {
        if (const auto dot = text.rfind('.'); dot != std::string_view::npos) {
            text.remove_prefix(dot + 1U);
        }
        return std::string { text };
    }

    [[nodiscard]] static std::string without_underscores(
        const std::string_view text)
    {
        std::string result;
        for (const auto character : text) {
            if (character != '_') {
                result.push_back(character);
            }
        }
        return result;
    }

    // A call of a type mark is a type conversion, and an attribute call an
    // attribute name; neither is a function call (Figure 25).
    [[nodiscard]] bool is_function_call(const vhdl::Expression& expression)
    {
        if (expression.text.empty() || expression.text.front() == '@'
            || expression.text.find('\'') != std::string::npos) {
            return false;
        }
        return !is_type_conversion(expression);
    }

    // A call whose name is a type mark (IEEE 1076-2019 9.3.6).
    [[nodiscard]] bool is_type_conversion(const vhdl::Expression& expression)
    {
        if (expression.text.empty() || expression.text.front() == '@'
            || expression.text.find('\'') != std::string::npos) {
            return false;
        }
        if (expression.referenced_name && expression.referenced_name->selected) {
            const auto* const declaration
                = hir_.find_declaration(*expression.referenced_name->selected);
            if (declaration != nullptr
                && (declaration->form == vhdl::DeclarationForm::type
                    || declaration->form == vhdl::DeclarationForm::subtype)) {
                return true;
            }
        }
        return standard_type(expression.text) != nullptr;
    }

    // The object of an expression that is not a name or an integer literal:
    // an operator or function call (class funcCall; the operator class of
    // earlier revisions is deprecated), an aggregate, a type conversion, a
    // qualified expression, an allocator, an attribute name or another
    // literal. Concatenations have no object.
    [[nodiscard]] Object* expression_class_object(Object& scope,
        const semantic::ExpressionId id, const vhdl::Expression& expression)
    {
        switch (expression.kind) {
        case vhdl::ExpressionKind::real_literal:
        case vhdl::ExpressionKind::string_literal:
        case vhdl::ExpressionKind::boolean_literal:
        case vhdl::ExpressionKind::logic_literal:
            return value_literal_object(id, expression);
        case vhdl::ExpressionKind::unary:
        case vhdl::ExpressionKind::binary:
            return call_object(scope, id, expression);
        case vhdl::ExpressionKind::call:
            if (expression.text.starts_with("@vhdl-physical:")
                && expression.operands.size() == 1U) {
                const auto* const magnitude
                    = hir_expression(expression.operands.front());
                if (magnitude == nullptr) {
                    return nullptr;
                }
                return physical_literal(without_underscores(magnitude->text),
                    expression.text.substr(std::strlen("@vhdl-physical:")));
            }
            if (expression.text.starts_with("@vhdl-qualified:")) {
                return typed_expression(scope, id, expression, vhpiQualifiedExprK,
                    expression.text.substr(std::strlen("@vhdl-qualified:")));
            }
            if (expression.text == "@vhdl-new"
                || expression.text == "@vhdl-new-qualified") {
                return typed_expression(
                    scope, id, expression, vhpiAllocatorK, "new");
            }
            if (expression.text.starts_with('\'')) {
                return attribute_name(scope, id, expression);
            }
            if (is_type_conversion(expression)) {
                return typed_expression(
                    scope, id, expression, vhpiTypeConvK, expression.text);
            }
            return is_function_call(expression)
                ? call_object(scope, id, expression)
                : nullptr;
        case vhdl::ExpressionKind::aggregate: {
            const auto key = "@expr:" + std::to_string(id.value());
            if (const auto found = scope.children.find(key);
                found != scope.children.end()) {
                return found->second;
            }
            auto* const object = make_object(vhpiAggregateK, "", &scope);
            object->expression = &expression;
            object->source = expression.source;
            scope.children.emplace(key, object);
            return object;
        }
        default:
            return nullptr;
        }
    }

    // A funcCall: its Name is the function's, or for an operator its symbol
    // (IEEE 1076-2019 19.4.3); IsOperator tells the two apart.
    [[nodiscard]] Object* call_object(Object& scope,
        const semantic::ExpressionId id, const vhdl::Expression& expression)
    {
        const auto key = "@expr:" + std::to_string(id.value());
        if (const auto found = scope.children.find(key);
            found != scope.children.end()) {
            return found->second;
        }
        const bool symbol = expression.kind != vhdl::ExpressionKind::call;
        std::string name;
        if (symbol) {
            name = "\"" + lower(expression.text) + "\"";
        } else {
            name = simple_name(expression.referenced_name
                        && !expression.referenced_name->spelling.empty()
                    ? std::string_view { expression.referenced_name->spelling }
                    : std::string_view { expression.text });
            if (expression.operator_symbol_call && !name.starts_with('"')) {
                name = "\"" + lower(name) + "\"";
            }
        }
        auto* const object = make_object(vhpiFuncCallK, name, &scope);
        object->expression = &expression;
        object->source = expression.source;
        object->full_name = object->name;
        object->full_case_name = object->case_name;
        scope.children.emplace(key, object);
        return object;
    }

    // An expression object with an operand list and a type (Figures 24, 25
    // and 29): a type conversion, a qualified expression, an allocator;
    // shared by expression.
    [[nodiscard]] Object* typed_expression(Object& scope,
        const semantic::ExpressionId id, const vhdl::Expression& expression,
        const vhpiClassKindT kind, const std::string_view name)
    {
        const auto key = "@expr:" + std::to_string(id.value());
        if (const auto found = scope.children.find(key);
            found != scope.children.end()) {
            return found->second;
        }
        auto* const object = make_object(kind, name, &scope);
        object->expression = &expression;
        object->source = expression.source;
        object->full_name = object->name;
        object->full_case_name = object->case_name;
        scope.children.emplace(key, object);
        return object;
    }

    // The attributes that take no parameter, which have class simpAttrName
    // when predefined (Figure 24); one with parameters is a paramAttrName,
    // any other a userAttrName.
    [[nodiscard]] static vhpiClassKindT attribute_kind(
        const vhdl::Expression& expression)
    {
        static constexpr std::array<std::string_view, 24> simple {
            "event", "active", "last_event", "last_active", "last_value",
            "driving", "driving_value", "left", "right", "high", "low",
            "ascending", "length", "range", "reverse_range", "stable",
            "quiet", "transaction", "base", "subtype", "element", "path_name",
            "instance_name", "simple_name"
        };
        if (expression.operands.size() > 1U) {
            return vhpiParamAttrNameK;
        }
        const std::string_view designator
            = std::string_view { expression.text }.substr(1U);
        return std::ranges::find(simple, designator) != simple.end()
            ? vhpiSimpAttrNameK
            : vhpiUserAttrNameK;
    }

    // An attribute name `prefix'designator`; its Name is the prefix's with
    // the attribute's (IEEE 1076-2019 19.4.3).
    [[nodiscard]] Object* attribute_name(Object& scope,
        const semantic::ExpressionId id, const vhdl::Expression& expression)
    {
        const auto key = "@expr:" + std::to_string(id.value());
        if (const auto found = scope.children.find(key);
            found != scope.children.end()) {
            return found->second;
        }
        std::string prefix;
        if (!expression.operands.empty()) {
            if (const auto* const object
                = expression_object(scope, expression.operands.front())) {
                prefix = object->name;
            } else {
                prefix = source_text(expression.operands.front());
            }
        }
        auto* const object = make_object(
            attribute_kind(expression), prefix + expression.text, &scope);
        object->expression = &expression;
        object->source = expression.source;
        object->full_name = object->name;
        object->full_case_name = object->case_name;
        scope.children.emplace(key, object);
        return object;
    }

    // A text for an expression that has no object of its own, written as
    // the source does for the common forms.
    [[nodiscard]] std::string source_text(
        const semantic::ExpressionId id, const std::size_t depth = 0U) const
    {
        const auto* const expression = hir_expression(id);
        if (expression == nullptr || depth > 16U) {
            return { };
        }
        const auto operand = [&](const std::size_t index) {
            return index < expression->operands.size()
                ? source_text(expression->operands[index], depth + 1U)
                : std::string { };
        };
        switch (expression->kind) {
        case vhdl::ExpressionKind::binary:
            return operand(0U) + " " + expression->text + " " + operand(1U);
        case vhdl::ExpressionKind::unary:
            return expression->text + " " + operand(0U);
        default:
            return expression->text;
        }
    }

    // An indexed or slice name whose index or bounds are no literals: it
    // denotes an element or slice only while the design runs, so it has no
    // value object (IEEE 1076-2019 22.5.1); the index expressions are
    // vhpiIndexExprs.
    [[nodiscard]] Object* dynamic_name(Object& scope,
        const semantic::ExpressionId id, const vhdl::Expression& expression,
        Object& prefix)
    {
        const auto key = "@expr:" + std::to_string(id.value());
        if (const auto found = scope.children.find(key);
            found != scope.children.end()) {
            return found->second;
        }
        const bool slice = expression.kind == vhdl::ExpressionKind::slice;
        std::string text = prefix.name + "(";
        for (std::size_t index = 1U; index < expression.operands.size();
             ++index) {
            if (index > 1U) {
                text += slice ? " " + expression.text + " " : ",";
            }
            text += source_text(expression.operands[index]);
        }
        text += ")";
        auto* const object = make_object(
            slice ? vhpiSliceNameK : vhpiIndexedNameK, text, &prefix);
        object->expression = &expression;
        object->source = expression.source;
        object->full_name = text;
        object->full_case_name = text;
        if (prefix.type != nullptr && prefix.type->type_data != nullptr) {
            const auto& data = *prefix.type->type_data;
            object->type = slice ? prefix.type : data.element;
        }
        scope.children.emplace(key, object);
        return object;
    }

    // The type a type mark names.
    [[nodiscard]] Object* type_by_mark(
        const vhdl::Expression& expression, const std::string_view mark)
    {
        if (expression.referenced_name && expression.referenced_name->selected) {
            const auto* const declaration
                = hir_.find_declaration(*expression.referenced_name->selected);
            if (declaration != nullptr
                && (declaration->form == vhdl::DeclarationForm::type
                    || declaration->form == vhdl::DeclarationForm::subtype)) {
                return type_of_declaration(*declaration);
            }
        }
        const auto name = simple_name(mark);
        if (auto* const type = standard_type(name)) {
            return type;
        }
        return package_type(name);
    }

    // The subprogram a call expression calls, when the design's resolution
    // pass found it.
    [[nodiscard]] const vhdl::Declaration* callee_of(
        const vhdl::Expression& expression) const
    {
        if (expression.resolved_declaration) {
            return hir_.find_declaration(*expression.resolved_declaration);
        }
        if (expression.referenced_name && expression.referenced_name->selected) {
            return hir_.find_declaration(*expression.referenced_name->selected);
        }
        return nullptr;
    }

    // The type of an expression object whose class has no type of its own
    // (vhpiType, IEEE 1076-2019 19.8): the result type of the function a call
    // calls or of a predefined operator, an aggregate's type when the design's
    // resolution pass gave it a predefined one, the type a conversion,
    // qualification or allocator names.
    [[nodiscard]] Object* expression_type(Object& object)
    {
        const auto* const expression = object.expression;
        if (expression == nullptr) {
            return nullptr;
        }
        switch (object.kind) {
        case vhpiTypeConvK:
            return type_by_mark(*expression, expression->text);
        case vhpiQualifiedExprK:
            return type_by_mark(*expression,
                std::string_view { expression->text }.substr(
                    std::strlen("@vhdl-qualified:")));
        case vhpiAllocatorK: {
            if (expression->operands.empty()) {
                return nullptr;
            }
            const auto* const subtype = hir_expression(expression->operands.front());
            return subtype == nullptr
                ? nullptr
                : type_by_mark(*subtype,
                      std::string_view { subtype->text }.substr(
                          std::min(subtype->text.size(),
                              std::strlen("@vhdl-subtype:"))));
        }
        case vhpiFuncCallK: {
            if (const auto* const callee = callee_of(*expression);
                callee != nullptr && callee->callable
                && callee->callable->return_type) {
                if (auto* const type
                    = subtype_object(*callee->callable->return_type, nullptr)) {
                    return type;
                }
            }
            break;
        }
        default:
            break;
        }
        // A predefined relational operator yields BOOLEAN.
        if (object.kind == vhpiFuncCallK
            && expression->kind != vhdl::ExpressionKind::call) {
            static constexpr std::array<std::string_view, 6> relational {
                "=", "/=", "<", "<=", ">", ">="
            };
            if (std::ranges::find(relational, std::string_view { expression->text })
                != relational.end()) {
                return standard_type("boolean");
            }
        }
        if (!expression->resolved_builtin_type.empty()) {
            if (auto* const type = standard_type(expression->resolved_builtin_type)) {
                return type;
            }
        }
        if (!expression->nominal_type.empty()) {
            if (auto* const type = type_by_mark(*expression, expression->nominal_type)) {
                return type;
            }
        }
        // Another predefined operator has the type of its operand.
        if (object.kind == vhpiFuncCallK
            && expression->kind != vhdl::ExpressionKind::call
            && !expression->operands.empty()) {
            if (auto* const operand
                = expression_object(object, expression->operands.front())) {
                return operand->type;
            }
        }
        return nullptr;
    }

    // The staticness of an expression object (IEEE 1076-2019 9.4, 19.8): a
    // literal is locally static; a call, aggregate, conversion or
    // qualification has the weakest staticness of its operands, and a call
    // of a function that is no predefined operator is at best globally
    // static, and only when it is pure.
    [[nodiscard]] std::optional<vhpiIntT> expression_staticness(
        Object& object, const std::size_t depth = 0U)
    {
        switch (object.kind) {
        case vhpiIntLiteralK:
        case vhpiRealLiteralK:
        case vhpiStringLiteralK:
        case vhpiBitStringLiteralK:
        case vhpiPhysLiteralK:
        case vhpiNullLiteralK:
            return vhpiLocallyStatic;
        case vhpiAggregateK:
        case vhpiFuncCallK:
        case vhpiTypeConvK:
        case vhpiQualifiedExprK:
            break;
        default:
            return staticness(object);
        }
        if (object.expression == nullptr || depth > 32U) {
            return std::nullopt;
        }
        const auto& expression = *object.expression;
        vhpiIntT result = vhpiLocallyStatic;
        const auto fold = [&](const semantic::ExpressionId id) {
            auto* const operand = expression_object(object, id);
            std::optional<vhpiIntT> value;
            if (operand != nullptr) {
                value = expression_staticness(*operand, depth + 1U);
                if (!value) {
                    value = primary_staticness(*operand);
                }
            }
            result = std::max(result, value.value_or(vhpiDynamic));
        };
        if (object.kind == vhpiAggregateK) {
            for (const auto& association : expression.associations) {
                fold(association.value);
                for (const auto choice : association.choices) {
                    fold(choice);
                }
            }
        } else {
            for (const auto operand : expression.operands) {
                fold(operand);
            }
        }
        if (object.kind == vhpiFuncCallK
            && !(expression.kind != vhdl::ExpressionKind::call
                && expression.builtin_operator
                    != vhdl::BuiltinOperatorIdentity::none)) {
            const auto* const callee = callee_of(expression);
            const bool pure = callee != nullptr && callee->callable
                && callee->callable->pure;
            result = pure ? std::max<vhpiIntT>(result, vhpiGloballyStatic)
                          : vhpiDynamic;
        }
        return result;
    }

    // The literal classes other than integer (Figure 26): real, string and
    // enumeration literals, a character literal being an enumeration literal.
    [[nodiscard]] Object* value_literal_object(
        const semantic::ExpressionId id, const vhdl::Expression& expression)
    {
        if (const auto found = literal_objects_.find(id.value());
            found != literal_objects_.end()) {
            return found->second;
        }
        Object* object = nullptr;
        switch (expression.kind) {
        case vhdl::ExpressionKind::real_literal: {
            const auto digits = without_underscores(expression.text);
            object = make_object(vhpiRealLiteralK, digits, nullptr);
            object->type = standard_type("real");
            double value { };
            const auto* const end = digits.data() + digits.size();
            if (const auto [last, failure]
                = std::from_chars(digits.data(), end, value);
                failure == std::errc { } && last == end) {
                object->constant = PackedLogic4::from_aval_bval(
                    64U, std::bit_cast<std::uint64_t>(value), 0U);
            }
            break;
        }
        case vhdl::ExpressionKind::string_literal: {
            const std::string text = expression.decoded_string
                ? *expression.decoded_string
                : expression.text;
            object = make_object(vhpiStringLiteralK, text, nullptr);
            if (auto value = string_constant(standard_type("string"), text)) {
                object->type = value->first;
                object->constant = std::move(value->second);
            }
            break;
        }
        case vhdl::ExpressionKind::boolean_literal: {
            const bool value = same_name(expression.text, "true");
            object = make_object(
                vhpiEnumLiteralK, value ? "TRUE" : "FALSE", nullptr);
            object->position = value ? 1 : 0;
            object->type = standard_type("boolean");
            if (object->type != nullptr && object->type->type_data != nullptr) {
                object->constant = PackedLogic4::from_aval_bval(
                    width_of(*object->type), value ? 1U : 0U, 0U);
            }
            break;
        }
        default: {
            // A character literal keeps its quotes; its type is the one the
            // design's resolution gave it, when that is a predefined one.
            auto text = expression.text;
            if (text.size() == 1U) {
                text = "'" + text + "'";
            }
            object = make_object(vhpiEnumLiteralK, text, nullptr);
            auto* const type = expression.resolved_builtin_type.empty()
                ? nullptr
                : standard_type(expression.resolved_builtin_type);
            if (type != nullptr && type->type_data != nullptr) {
                const auto& literals = type->type_data->literals;
                if (const auto found = std::ranges::find(literals, text);
                    found != literals.end()) {
                    object->type = type;
                    object->position = found - literals.begin();
                }
            }
            break;
        }
        }
        object->full_name = object->name;
        object->full_case_name = object->case_name;
        literal_objects_.emplace(id.value(), object);
        return object;
    }

    // A physical literal such as `5 ns` (class physLiteral), shared by value.
    [[nodiscard]] Object* physical_literal(
        const std::string_view digits, const std::string_view unit)
    {
        const auto name = std::string { digits } + " " + lower(unit);
        if (const auto found = physical_literals_.find(name);
            found != physical_literals_.end()) {
            return found->second;
        }
        auto* const object = make_object(vhpiPhysLiteralK, name, nullptr);
        object->full_name = object->name;
        object->full_case_name = object->case_name;
        object->type = standard_type("time");
        std::int64_t magnitude { };
        const auto* const end = digits.data() + digits.size();
        const auto parsed = std::from_chars(digits.data(), end, magnitude);
        if (object->type != nullptr && object->type->type_data != nullptr
            && parsed.ec == std::errc { } && parsed.ptr == end) {
            for (const auto& candidate : object->type->type_data->units) {
                if (same_name(candidate.name, unit)) {
                    object->position = candidate.scale;
                    object->constant = PackedLogic4::from_aval_bval(64U,
                        static_cast<std::uint64_t>(magnitude * candidate.scale),
                        0U);
                    break;
                }
            }
        }
        physical_literals_.emplace(name, object);
        return object;
    }

    // The time a delay holds, when the frontend kept only its parts and no
    // expression.
    [[nodiscard]] Object* delay_literal(const vhdl::Delay& delay)
    {
        const auto& value = delay.primary;
        if (value.divisor != 1U) {
            return nullptr;
        }
        if (!value.unit.empty()) {
            return physical_literal(std::to_string(value.magnitude), value.unit);
        }
        // Elaboration normalized the delay to resolution ticks; the literal
        // is written in the largest unit of TIME that holds it exactly.
        if (value.magnitude != 0U
            && value.magnitude
                > std::numeric_limits<std::uint64_t>::max() / femtoseconds_per_tick_) {
            return nullptr;
        }
        const auto femtoseconds = value.magnitude * femtoseconds_per_tick_;
        static constexpr std::array<std::pair<std::uint64_t, std::string_view>, 8>
            units { { { 3'600'000'000'000'000'000ULL, "hr" },
                { 60'000'000'000'000'000ULL, "min" },
                { 1'000'000'000'000'000ULL, "sec" },
                { 1'000'000'000'000ULL, "ms" }, { 1'000'000'000ULL, "us" },
                { 1'000'000ULL, "ns" }, { 1'000ULL, "ps" }, { 1ULL, "fs" } } };
        for (const auto& [scale, unit] : units) {
            if (femtoseconds == 0U ? scale == 1U : femtoseconds % scale == 0U) {
                return physical_literal(
                    std::to_string(femtoseconds / scale), unit);
            }
        }
        return nullptr;
    }

    // The delay a time relation of a statement object leads to.
    [[nodiscard]] const vhdl::Delay* statement_delay(
        const Object& object, const vhpiOneToOneT relation) const
    {
        const auto* const statement = object.statement;
        if (statement == nullptr) {
            return nullptr;
        }
        switch (relation) {
        case vhpiTimeOutExpr:
            return object.kind == vhpiWaitStmtK && statement->delay
                ? &*statement->delay
                : nullptr;
        case vhpiRejectTime:
            if (is_signal_assignment(object)) {
                const auto& assignment = assignment_of(*statement);
                return assignment.rejection_limit ? &*assignment.rejection_limit
                                                  : nullptr;
            }
            return nullptr;
        case vhpiTimeExpr:
            if (object.kind == vhpiWaveformElemK
                && object.part < statement->waveform.size()
                && statement->waveform[object.part].delay) {
                return &*statement->waveform[object.part].delay;
            }
            return nullptr;
        default:
            return nullptr;
        }
    }

    // The association elements of a function or procedure call
    // (vhpiParamAssocs, Figure 35), in the order of the actuals.
    [[nodiscard]] std::vector<Object*>& parameter_associations(Object& call)
    {
        auto& result = call.lists[ListKind::param_assocs];
        if (!result.empty()) {
            return result;
        }
        const auto add = [&](const std::optional<semantic::ExpressionId> actual,
                             const bool named) {
            auto* const object = make_object(vhpiAssocElemK, "", &call);
            object->position = static_cast<std::int64_t>(result.size());
            object->actual = actual;
            object->named = named;
            result.push_back(object);
        };
        if (call.expression != nullptr) {
            const auto& names = call.expression->argument_names;
            for (std::size_t index = 0U; index < call.expression->operands.size();
                 ++index) {
                add(call.expression->operands[index],
                    index < names.size() && !names[index].empty());
            }
        } else if (call.statement != nullptr) {
            for (const auto& argument : call.statement->procedure_arguments) {
                add(argument.actual,
                    argument.formal && !argument.formal->spelling.empty());
            }
        }
        return result;
    }

    // The index expressions of an indexed name (vhpiIndexExprs, Figure 25):
    // those of the expression, or for a name with literal indexes the
    // literals.
    [[nodiscard]] std::vector<Object*> index_expressions(Object& name)
    {
        std::vector<Object*> result;
        if (name.expression != nullptr) {
            const auto& operands = name.expression->operands;
            for (std::size_t index = 1U; index < operands.size(); ++index) {
                if (auto* const operand = expression_object(name, operands[index])) {
                    result.push_back(operand);
                }
            }
            return result;
        }
        for (const auto index : name.indexes) {
            result.push_back(integer_value_object(index));
        }
        return result;
    }

    // The integer literal object of a value.
    [[nodiscard]] Object* integer_value_object(const std::int64_t value)
    {
        if (const auto found = integer_objects_.find(value);
            found != integer_objects_.end()) {
            return found->second;
        }
        auto* const object = make_object(vhpiIntLiteralK, std::to_string(value), nullptr);
        object->full_name = object->name;
        object->full_case_name = object->case_name;
        object->type = standard_type("integer");
        const auto width = object->type != nullptr
                && object->type->type_data != nullptr
                && object->type->type_data->width != 0U
            ? object->type->type_data->width
            : 32U;
        object->constant = PackedLogic4::from_aval_bval(
            width, static_cast<std::uint64_t>(value), 0U);
        integer_objects_.emplace(value, object);
        return object;
    }

    // The range of a for loop (vhpiConstraint, Figure 40); only a range
    // whose bounds are literals has an object.
    [[nodiscard]] Object* loop_range(Object& loop)
    {
        if (const auto found = loop.children.find("@range");
            found != loop.children.end()) {
            return found->second;
        }
        if (loop.statement == nullptr || !loop.statement->loop_initial
            || !loop.statement->loop_limit) {
            return nullptr;
        }
        const auto left = integer_literal(*loop.statement->loop_initial);
        const auto right = integer_literal(*loop.statement->loop_limit);
        if (!left || !right) {
            return nullptr;
        }
        auto* const range = range_object(
            Range { *left, *right, loop.statement->loop_descending });
        loop.children.emplace("@range", range);
        return range;
    }

    // The formal parameter of the called subprogram that an association
    // element associates (vhpiFormal, Figure 35): the one named when the
    // association is named, else the one at its position.
    [[nodiscard]] Object* formal_of(Object& association)
    {
        if (association.kind != vhpiAssocElemK || association.parent == nullptr) {
            return nullptr;
        }
        const auto& call = *association.parent;
        const auto index = static_cast<std::size_t>(association.position);
        const vhdl::Declaration* callee = nullptr;
        std::string formal_name;
        if (call.expression != nullptr) {
            callee = callee_of(*call.expression);
            const auto& names = call.expression->argument_names;
            if (association.named && index < names.size()) {
                formal_name = names[index];
            }
        } else if (call.statement != nullptr) {
            const auto& statement = *call.statement;
            callee = procedure_callee(statement);
            if (association.named && index < statement.procedure_arguments.size()
                && statement.procedure_arguments[index].formal) {
                formal_name = statement.procedure_arguments[index].formal->spelling;
            }
        }
        if (callee == nullptr || !callee->callable) {
            return nullptr;
        }
        const auto& formals = callee->callable->formals;
        const vhdl::Declaration* formal = nullptr;
        if (!formal_name.empty()) {
            for (const auto id : formals) {
                const auto* const candidate = hir_.find_declaration(id);
                if (candidate != nullptr && same_name(candidate->name, formal_name)) {
                    formal = candidate;
                    break;
                }
            }
        } else if (index < formals.size()) {
            formal = hir_.find_declaration(formals[index]);
        }
        return formal == nullptr ? nullptr : param_object(*formal, nullptr);
    }

    // The element associations of an aggregate (vhpiElemAssocs, Figure 23).
    [[nodiscard]] std::vector<Object*>& element_associations(Object& aggregate)
    {
        auto& result = aggregate.lists[ListKind::elem_assocs];
        if (!result.empty() || aggregate.expression == nullptr) {
            return result;
        }
        for (const auto& association : aggregate.expression->associations) {
            auto* const object = make_object(vhpiElemAssocK, "", &aggregate);
            object->association = &association;
            object->named = !association.positional();
            object->source = association.source;
            result.push_back(object);
        }
        return result;
    }

    // The choices of an element association (vhpiChoices); a choice that is
    // a record element name has no object.
    [[nodiscard]] std::vector<Object*>& association_choices(Object& owner)
    {
        auto& result = owner.lists[ListKind::choices];
        if (!result.empty() || owner.association == nullptr) {
            return result;
        }
        for (const auto choice : owner.association->choices) {
            if (auto* const object = expression_object(owner, choice)) {
                result.push_back(object);
            }
        }
        if (same_name(owner.association->choice_spelling, "others")) {
            auto* const others = make_object(vhpiOthersK, "others", &owner);
            others->source = owner.association->source;
            result.push_back(others);
        }
        return result;
    }

    // The process equivalent to a concurrent assertion, procedure call or
    // signal assignment (vhpiEqProcessStmt, IEEE 1076-2019 11.4 to 11.7,
    // Figure 38). It shares the statement's label, position and
    // sensitivities and has no sequential statements of its own.
    [[nodiscard]] Object* equivalent_process(Object& statement)
    {
        if (!statement.is_stmt || statement.sequential || statement.equivalent
            || statement.process != nullptr || statement.statement == nullptr
            || !concurrent_kind(*statement.statement)) {
            return nullptr;
        }
        if (const auto found = statement.children.find("@eq");
            found != statement.children.end()) {
            return found->second;
        }
        auto* const object
            = make_object(vhpiProcessStmtK, statement.name, statement.parent);
        object->case_name = statement.case_name;
        object->full_name = statement.full_name;
        object->full_case_name = statement.full_case_name;
        object->path = statement.path;
        object->statement = statement.statement;
        object->is_stmt = true;
        object->equivalent = true;
        object->label_name = statement.label_name;
        object->source = statement.source;
        statement.children.emplace("@eq", object);
        return object;
    }

    [[nodiscard]] std::optional<vhpiIntT> span_line(
        const std::optional<semantic::SourceSpanId> source, const bool end) const
    {
        const auto& spans = simulation_.semantics().source_spans();
        if (!source || !source->valid() || source->value() >= spans.size()) {
            return std::nullopt;
        }
        const auto& span = spans.at(source->value());
        return static_cast<vhpiIntT>(end ? span.end.line : span.begin.line);
    }

    // --- collections (IEEE 1076-2019 19.15, 23.6) ------------------------------------

    [[nodiscard]] static bool is_collection(const Object& object) noexcept
    {
        return object.kind == vhpiAnyCollectionK
            || object.kind == vhpiDriverCollectionK;
    }

    // vhpi_create of a collection: a new one holding the object, or the
    // members of the collection, handle2 names, or `handle1` extended by
    // them. Foreign processes and drivers are not created (see install).
    [[nodiscard]] vhpiHandleT create(const vhpiClassKindT kind,
        const vhpiHandleT first, const vhpiHandleT second)
    {
        if (kind != vhpiAnyCollectionK && kind != vhpiDriverCollectionK) {
            error(vhpiError, "vhpi_create does not create objects of class "
                    + kind_name(kind));
            return nullptr;
        }
        const bool drivers = kind == vhpiDriverCollectionK;
        Object* collection = nullptr;
        if (first != nullptr) {
            collection = object_of(first);
            if (collection == nullptr) {
                return nullptr;
            }
            if (collection->kind != kind) {
                error(vhpiError, "vhpi_create: handle1 is not an object of class "
                        + kind_name(kind));
                return nullptr;
            }
        }
        if (second == nullptr) {
            error(vhpiError, "vhpi_create: handle2 is missing");
            return nullptr;
        }
        auto* const member = object_of(second);
        if (member == nullptr) {
            return nullptr;
        }
        // A collection contributes its members, in order; copied first, as
        // it may be the one being extended.
        std::vector<Object*> added;
        if (is_collection(*member)) {
            if (drivers && member->kind != vhpiDriverCollectionK) {
                error(vhpiError, "vhpi_create: handle2 is neither a driver nor "
                                 "a driverCollection");
                return nullptr;
            }
            added = member->lists[ListKind::members];
        } else {
            added.push_back(member);
        }
        // The members of a driverCollection are drivers; whether they are
        // all drivers of subelements of one signal is not checked, as the
        // driver objects carry no signal association yet.
        if (drivers
            && std::ranges::any_of(added, [](const Object* const candidate) {
                   return candidate->kind != vhpiDriverK;
               })) {
            error(vhpiError, "vhpi_create: handle2 is neither a driver nor "
                             "a driverCollection");
            return nullptr;
        }
        if (collection == nullptr) {
            collection = make_object(kind, "", nullptr);
        }
        auto& members = collection->lists[ListKind::members];
        members.insert(members.end(), added.begin(), added.end());
        return to_handle(collection);
    }

    // --- protected types (IEEE 1076-2019 23.26) --------------------------------------

    // Whether a variable declaration is of a protected type (IsProtectedType).
    [[nodiscard]] bool protected_variable(const Object& object) const
    {
        if (object.kind != vhpiVarDeclK || object.declaration == nullptr
            || !object.declaration->subtype) {
            return false;
        }
        const auto& mark = object.declaration->subtype->type_mark;
        if (!mark.target.valid()) {
            return false;
        }
        const auto* const definition = hir_.find_type(mark.target);
        return definition != nullptr
            && definition->form == vhdl::TypeForm::protected_type;
    }

    // vhpi_protected_call. A VHPI routine runs on the thread of the callback
    // or foreign subprogram that called it, at a point where the simulation
    // does not advance (callbacks run from the scheduler's safe points), so
    // no VHDL process can hold the variable meanwhile and exclusive access
    // is given. A nested call for a variable already held would wait forever
    // for itself in VHDL (4.3), so it is an error.
    [[nodiscard]] int protected_call(const vhpiHandleT variable,
        const vhpiUserFctT function, void* const data)
    {
        auto* const object = object_of(variable);
        if (object == nullptr) {
            return 1;
        }
        if (function == nullptr) {
            error(vhpiError, "vhpi_protected_call has no function");
            return 1;
        }
        if (object->kind != vhpiVarDeclK || object->declaration == nullptr
            || !object->declaration->shared || !protected_variable(*object)) {
            error(vhpiError, "vhpi_protected_call needs a shared variable of a "
                             "protected type");
            return 1;
        }
        if (!protected_held_.insert(object).second) {
            error(vhpiError, object->full_name
                    + " is held by an enclosing vhpi_protected_call");
            return 1;
        }
        struct Release {
            std::set<const Object*>& held;
            const Object* object;
            ~Release() { held.erase(object); }
        } const release { protected_held_, object };
        // The function takes the handle and the data (23.26); the header
        // declares it without parameters.
        using Function = int (*)(vhpiHandleT, void*);
        const auto callable = reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(function));
        const auto result = callable(variable, data);
        // The routine itself succeeded, whatever the function's own calls
        // reported.
        clear_error();
        return result;
    }

    [[nodiscard]] static std::optional<semantic::ExpressionId> delay_expression(
        const std::optional<vhdl::Delay>& delay)
    {
        return delay ? delay->primary.expression : std::nullopt;
    }

    // The HIR expression a relation of a statement object leads to.
    [[nodiscard]] std::optional<semantic::ExpressionId> statement_expression(
        const Object& object, const vhpiOneToOneT relation) const
    {
        const auto* const statement = object.statement;
        if (statement == nullptr) {
            return std::nullopt;
        }
        switch (relation) {
        case vhpiCondExpr:
            switch (object.kind) {
            case vhpiBranchK:
            case vhpiCondWaveformK:
                return object.condition;
            case vhpiWhileLoopK:
            case vhpiExitStmtK:
            case vhpiNextStmtK:
            case vhpiWaitStmtK:
            case vhpiSeqAssertStmtK:
            case vhpiConcAssertStmtK:
                return statement->condition;
            default:
                return std::nullopt;
            }
        case vhpiLhsExpr:
            switch (object.kind) {
            case vhpiSimpleSigAssignStmtK:
            case vhpiCondSigAssignStmtK:
            case vhpiSelectSigAssignStmtK:
            case vhpiSeqSigAssignStmtK:
            case vhpiVarAssignStmtK:
                return assignment_of(*statement).target;
            default:
                return std::nullopt;
            }
        case vhpiRhsExpr:
            return object.kind == vhpiVarAssignStmtK ? statement->value
                                                     : std::nullopt;
        case vhpiCaseExpr:
            return object.kind == vhpiCaseStmtK ? statement->condition
                                                : std::nullopt;
        case vhpiSelectExpr:
            return object.kind == vhpiSelectSigAssignStmtK ? statement->condition
                                                           : std::nullopt;
        case vhpiReturnExpr:
            return object.kind == vhpiReturnStmtK ? statement->value
                                                  : std::nullopt;
        case vhpiTimeOutExpr:
            return object.kind == vhpiWaitStmtK
                ? delay_expression(statement->delay)
                : std::nullopt;
        case vhpiReportExpr:
            return object.kind == vhpiReportStmtK
                    || object.kind == vhpiSeqAssertStmtK
                    || object.kind == vhpiConcAssertStmtK
                ? statement->report
                : std::nullopt;
        case vhpiSeverityExpr:
            return object.kind == vhpiReportStmtK
                    || object.kind == vhpiSeqAssertStmtK
                    || object.kind == vhpiConcAssertStmtK
                ? statement->severity
                : std::nullopt;
        case vhpiRejectTime:
            switch (object.kind) {
            case vhpiSimpleSigAssignStmtK:
            case vhpiCondSigAssignStmtK:
            case vhpiSelectSigAssignStmtK:
            case vhpiSeqSigAssignStmtK:
                return delay_expression(
                    assignment_of(*statement).rejection_limit);
            default:
                return std::nullopt;
            }
        case vhpiTimeExpr:
            if (object.kind == vhpiWaveformElemK
                && object.part < statement->waveform.size()) {
                return delay_expression(statement->waveform[object.part].delay);
            }
            return std::nullopt;
        case vhpiValExpr:
            if (object.kind == vhpiWaveformElemK && !statement->unaffected
                && object.part < statement->waveform.size()) {
                return statement->waveform[object.part].value;
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static bool is_signal_assignment(const Object& object) noexcept
    {
        switch (object.kind) {
        case vhpiSimpleSigAssignStmtK:
        case vhpiCondSigAssignStmtK:
        case vhpiSelectSigAssignStmtK:
        case vhpiSeqSigAssignStmtK:
            return object.statement != nullptr;
        default:
            return false;
        }
    }

    // Whether a process, or the statements below, assign a signal or call
    // a procedure; a procedure may assign signals, so a call is taken to.
    void scan_activity(const std::vector<semantic::StatementId>& statements,
        bool& active) const
    {
        for (const auto id : statements) {
            const auto* const statement = hir_statement(id);
            if (statement == nullptr) {
                continue;
            }
            switch (statement->kind) {
            case vhdl::StatementKind::signal_assignment:
            case vhdl::StatementKind::force:
            case vhdl::StatementKind::release:
            case vhdl::StatementKind::procedure_call:
                active = true;
                break;
            default:
                break;
            }
            scan_activity(statement->statements, active);
            scan_activity(statement->else_statements, active);
            for (const auto& alternative : statement->alternatives) {
                scan_activity(alternative.statements, active);
            }
        }
    }

    // The IsPassive property of a process or concurrent statement.
    [[nodiscard]] vhpiIntT passive(const Object& object) const
    {
        if (object.process != nullptr) {
            bool active { };
            scan_activity(object.process->statements, active);
            return active ? 0 : 1;
        }
        if (object.equivalent && object.statement != nullptr) {
            return object.statement->kind == vhdl::StatementKind::assertion ? 1
                                                                            : 0;
        }
        return object.kind == vhpiConcAssertStmtK ? 1 : 0;
    }

    // The label of the loop an exit or next statement leaves (vhpiLoopLabelNameP).
    [[nodiscard]] std::string loop_label_of(const Object& object) const
    {
        if (object.statement != nullptr
            && !object.statement->loop_control_label.empty()) {
            return object.statement->loop_control_label;
        }
        for (const auto* current = object.parent; current != nullptr;
             current = current->parent) {
            if (current->kind == vhpiForLoopK || current->kind == vhpiWhileLoopK
                || current->kind == vhpiForeverLoopK) {
                return current->name;
            }
        }
        return { };
    }

    // A declared object of a region, created on first use.
    [[nodiscard]] Object* declared(Object& region,
        const vhdl::Declaration& declaration)
    {
        if (const auto kind = item_kind(declaration.form)) {
            return declared_item(region, declaration, *kind);
        }
        // The formals of a subprogram are its parameters, shared with the
        // subprogram declaration (IEEE 1076-2019 19.10).
        if (region.kind == vhpiSubpBodyK
            && declaration.form == vhdl::DeclarationForm::port) {
            Object* const subprogram
                = region.parent != nullptr && region.declaration != nullptr
                ? declared(*region.parent, *region.declaration)
                : nullptr;
            return param_object(
                declaration, subprogram != nullptr ? subprogram : &region);
        }
        const auto key = lower(declaration.name);
        if (const auto found = region.children.find(key);
            found != region.children.end()) {
            return found->second;
        }
        if (declaration.form == vhdl::DeclarationForm::alias) {
            return declared_alias(region, declaration, key);
        }
        auto kind = vhpiConstDeclK;
        switch (declaration.form) {
        case vhdl::DeclarationForm::port:
            kind = vhpiPortDeclK;
            break;
        case vhdl::DeclarationForm::signal:
            kind = vhpiSigDeclK;
            break;
        case vhdl::DeclarationForm::generic_constant:
            kind = vhpiGenericDeclK;
            break;
        case vhdl::DeclarationForm::variable:
            kind = vhpiVarDeclK;
            break;
        case vhdl::DeclarationForm::constant:
            kind = vhpiConstDeclK;
            break;
        case vhdl::DeclarationForm::type:
        case vhdl::DeclarationForm::subtype: {
            auto* const type = type_of_declaration(declaration);
            if (type != nullptr) {
                region.children.emplace(key, type);
            }
            return type;
        }
        default:
            return nullptr;
        }
        auto* const object = make_object(kind, declaration.name, &region);
        object->declaration = &declaration;
        object->full_name = region.full_name + ":" + object->name;
        object->full_case_name = region.full_case_name + ":" + object->case_name;
        if (declaration.subtype) {
            object->type = subtype_object(*declaration.subtype, nullptr);
        }
        if (!region.path.empty()) {
            object->signal = simulation_.find_signal(
                region.path + "." + lower(declaration.name));
        }
        if (!object->signal && declaration.initializer
            && (declaration.form == vhdl::DeclarationForm::constant
                || declaration.form
                    == vhdl::DeclarationForm::generic_constant)) {
            literal_constant(*object, *declaration.initializer);
        }
        if (!object->signal && !object->constant
            && declaration.form == vhdl::DeclarationForm::generic_constant) {
            elaborated_generic(region, *object);
        }
        if (object->signal) {
            if (const auto* const info = signal_info(*object->signal)) {
                object->type = refine(object->type, *info);
                // A scalar type's storage width is the simulator's layout of
                // every object of that type.
                if (auto* const type = object->type; type != nullptr
                    && type->type_data != nullptr && info->width != 0U
                    && (type->type_data->storage == Storage::integer
                        || type->type_data->storage == Storage::physical
                        || type->type_data->storage == Storage::enumeration)) {
                    type->type_data->width = info->width;
                }
            }
        }
        region.children.emplace(key, object);
        return object;
    }

    // The class of a file, subprogram, component, attribute or group
    // declaration, or of an attribute specification (IEEE 1076-2019 19.4,
    // Figure 14).
    [[nodiscard]] static std::optional<vhpiClassKindT> item_kind(
        const vhdl::DeclarationForm form)
    {
        switch (form) {
        case vhdl::DeclarationForm::file:
            return vhpiFileDeclK;
        case vhdl::DeclarationForm::function:
            return vhpiFuncDeclK;
        case vhdl::DeclarationForm::procedure:
            return vhpiProcDeclK;
        case vhdl::DeclarationForm::component:
            return vhpiCompDeclK;
        case vhdl::DeclarationForm::attribute_declaration:
            return vhpiAttrDeclK;
        case vhdl::DeclarationForm::attribute_specification:
            return vhpiAttrSpecK;
        case vhdl::DeclarationForm::group_template:
            return vhpiGroupTempDeclK;
        case vhdl::DeclarationForm::group_instance:
            return vhpiGroupDeclK;
        default:
            return std::nullopt;
        }
    }

    // One object per such declaration: subprograms overload and attribute
    // specifications repeat names, so these are keyed by declaration.
    [[nodiscard]] Object* declared_item(Object& region,
        const vhdl::Declaration& declaration, const vhpiClassKindT kind)
    {
        const auto key = "@item:"
            + std::to_string(reinterpret_cast<std::uintptr_t>(&declaration));
        if (const auto found = region.children.find(key);
            found != region.children.end()) {
            return found->second;
        }
        auto* const object = make_object(kind, declaration.name, &region);
        object->declaration = &declaration;
        object->full_name = region.full_name + ":" + object->name;
        object->full_case_name = region.full_case_name + ":" + object->case_name;
        if (kind == vhpiFileDeclK && declaration.subtype) {
            object->type = subtype_object(*declaration.subtype, nullptr);
        }
        // The generics and ports of a component (IEEE 1076-2019 19.4,
        // Figure 14).
        if (kind == vhpiCompDeclK && declaration.component) {
            for (const auto* const ids : { &declaration.component->generics,
                     &declaration.component->ports }) {
                for (const auto id : *ids) {
                    if (const auto* const item = hir_.find_declaration(id)) {
                        object->region_declarations.push_back(item);
                    }
                }
            }
        }
        region.children.emplace(key, object);
        return object;
    }

    // --- subprograms (IEEE 1076-2019 19.10) -----------------------------------------

    // The class of a formal parameter (Figure 34).
    [[nodiscard]] static vhpiClassKindT param_kind(
        const vhdl::Declaration& formal) noexcept
    {
        switch (formal.object_class) {
        case vhdl::ObjectClass::signal:
            return vhpiSigParamDeclK;
        case vhdl::ObjectClass::variable:
            return vhpiVarParamDeclK;
        case vhdl::ObjectClass::file:
            return vhpiFileParamDeclK;
        case vhdl::ObjectClass::constant:
            break;
        }
        return vhpiConstParamDeclK;
    }

    // The object of a formal parameter; the subprogram declaration, its body
    // and the association elements of its calls share it.
    [[nodiscard]] Object* param_object(
        const vhdl::Declaration& formal, Object* const parent)
    {
        if (const auto found = params_.find(&formal); found != params_.end()) {
            return found->second;
        }
        auto* const object = make_object(param_kind(formal), formal.name, parent);
        object->declaration = &formal;
        if (parent != nullptr) {
            object->full_name = parent->full_name + ":" + object->name;
            object->full_case_name = parent->full_case_name + ":" + object->case_name;
        }
        if (formal.subtype) {
            object->type = subtype_object(*formal.subtype, nullptr);
        }
        params_.emplace(&formal, object);
        return object;
    }

    // The formal parameters of a subprogram declaration (vhpiParamDecls).
    [[nodiscard]] std::vector<Object*>& parameter_declarations(Object& subprogram)
    {
        auto& result = subprogram.lists[ListKind::param_decls];
        if (!result.empty() || subprogram.declaration == nullptr
            || !subprogram.declaration->callable) {
            return result;
        }
        for (const auto id : subprogram.declaration->callable->formals) {
            if (const auto* const formal = hir_.find_declaration(id)) {
                result.push_back(param_object(*formal, &subprogram));
            }
        }
        return result;
    }

    // The body of a subprogram declaration (class subpBody, Figure 34):
    // its declarations and statements. A declaration that holds no body has
    // none (a specification whose body is declared apart has no object).
    [[nodiscard]] Object* body_object(
        Object& owner, const vhdl::Declaration& declaration)
    {
        if (!declaration.callable || !declaration.callable->defined) {
            return nullptr;
        }
        const auto key = "@body:"
            + std::to_string(reinterpret_cast<std::uintptr_t>(&declaration));
        if (const auto found = owner.children.find(key);
            found != owner.children.end()) {
            return found->second;
        }
        auto* const object = make_object(vhpiSubpBodyK, declaration.name, &owner);
        object->declaration = &declaration;
        object->body = &declaration.statements;
        object->source = declaration.source;
        object->full_name = owner.full_name + ":" + object->name;
        object->full_case_name = owner.full_case_name + ":" + object->case_name;
        const auto& callable = *declaration.callable;
        for (const auto id : declaration.children) {
            if (callable.return_identifier
                && callable.return_identifier->value() == id.value()) {
                continue;
            }
            if (const auto* const child = hir_.find_declaration(id)) {
                object->region_declarations.push_back(child);
            }
        }
        std::size_t next_loop { };
        number_loops(declaration.statements, next_loop);
        owner.children.emplace(key, object);
        return object;
    }

    // The subprogram bodies of the design: those declared in a region, a
    // process or another body, found once.
    [[nodiscard]] const std::vector<Object*>& all_bodies()
    {
        if (all_bodies_) {
            return *all_bodies_;
        }
        all_bodies_.emplace();
        std::vector<Object*> owners;
        if (auto* const top = root()) {
            owners.push_back(top);
        }
        for (auto* const package : package_instances()) {
            owners.push_back(package);
        }
        std::set<const Object*> seen;
        std::size_t visited { };
        while (!owners.empty() && visited < 1'000'000U) {
            auto* const owner = owners.back();
            owners.pop_back();
            ++visited;
            if (!seen.insert(owner).second) {
                continue;
            }
            for (const auto* const declaration : owner->region_declarations) {
                if (declaration->form != vhdl::DeclarationForm::function
                    && declaration->form != vhdl::DeclarationForm::procedure) {
                    continue;
                }
                if (auto* const body = body_object(*owner, *declaration)) {
                    all_bodies_->push_back(body);
                    owners.push_back(body);
                }
            }
            if (is_region(*owner)) {
                for (auto* const item : region_statements(*owner)) {
                    if (item->kind == vhpiProcessStmtK || is_region(*item)) {
                        owners.push_back(item);
                    }
                }
            }
        }
        return *all_bodies_;
    }

    // Where an object is: its own statement, its declaration, its block or
    // generate statement, or its design unit (IEEE 1076-2019 Figure 14).
    [[nodiscard]] std::optional<std::pair<std::string, std::uint32_t>>
    source_location(const Object& object) const
    {
        auto source = object.source;
        if (!source && object.declaration != nullptr) {
            source = object.declaration->source;
        }
        if (!source && object.generate != nullptr) {
            source = object.generate->source;
        }
        if (!source && object.unit != nullptr
            && (is_design_unit(object) || object.kind == vhpiRootInstK
                || object.kind == vhpiPackInstK)) {
            source = object.unit->source;
        }
        const auto& semantics = simulation_.semantics();
        if (!source || !source->valid()
            || source->value() >= semantics.source_spans().size()) {
            return std::nullopt;
        }
        const auto& span = semantics.source_spans().at(source->value());
        if (!span.logical_name.empty()) {
            return std::pair { span.logical_name, span.begin.line };
        }
        if (span.file.valid()
            && span.file.value() < semantics.source_files().size()) {
            return std::pair {
                semantics.source_files().at(span.file.value()).physical_name,
                span.begin.line
            };
        }
        return std::nullopt;
    }

    // An alias of an object, which reads and indexes as the object does and
    // has the alias's own subtype when that has the same extent (IEEE
    // 1076-2019 6.6.2, 19.4.3 vhpiAliasedName). An alias of anything but
    // an object has no VHPI object.
    [[nodiscard]] Object* declared_alias(Object& region,
        const vhdl::Declaration& declaration, const std::string& key)
    {
        if (!declaration.alias_target
            || !resolving_aliases_.insert(&declaration).second) {
            return nullptr;
        }
        const auto handle = handle_by_name(
            std::string { declaration.alias_target->spelling }.c_str(),
            to_handle(&region));
        resolving_aliases_.erase(&declaration);
        if (handle == nullptr) {
            return nullptr;
        }
        auto* target = object_of(handle);
        // An alias of an element or a slice names that part of the object
        // (IEEE 1076-2019 6.6.2); its expression keeps the selection.
        if (target != nullptr && declaration.initializer) {
            target = aliased_part(*target, *declaration.initializer, 0U);
        }
        if (target == nullptr || is_type(*target) || is_region(*target)
            || target->type == nullptr || target->type->type_data == nullptr) {
            return nullptr;
        }
        auto* const object
            = make_object(vhpiAliasDeclK, declaration.name, &region);
        object->declaration = &declaration;
        object->aliased = target;
        object->full_name = region.full_name + ":" + object->name;
        object->full_case_name = region.full_case_name + ":" + object->case_name;
        object->signal = target->signal;
        object->constant = target->constant;
        object->offset = target->offset;
        object->call_slot = target->call_slot;
        object->call_result = target->call_result;
        object->type = target->type;
        if (declaration.subtype) {
            auto* const subtype = subtype_object(*declaration.subtype, nullptr);
            if (subtype != nullptr && subtype->type_data != nullptr
                && width_of(*subtype) == width_of(*target->type)) {
                object->type = subtype;
            }
        }
        region.children.emplace(key, object);
        return object;
    }

    // The part of an alias's named object that its index or slice name
    // selects, with literal indexes and bounds.
    [[nodiscard]] Object* aliased_part(Object& named,
        const semantic::ExpressionId id, const std::size_t depth)
    {
        const auto* const expression = hir_expression(id);
        if (expression == nullptr || depth > 16U) {
            return nullptr;
        }
        switch (expression->kind) {
        case vhdl::ExpressionKind::name:
            return &named;
        case vhdl::ExpressionKind::slice: {
            if (expression->operands.size() != 3U) {
                return nullptr;
            }
            auto* const prefix
                = aliased_part(named, expression->operands[0], depth + 1U);
            const auto left = integer_literal(expression->operands[1]);
            const auto right = integer_literal(expression->operands[2]);
            if (prefix == nullptr || !left || !right) {
                return nullptr;
            }
            return sliced(*prefix,
                Range { *left, *right, expression->text == "downto" });
        }
        case vhdl::ExpressionKind::index: {
            if (expression->operands.size() < 2U) {
                return nullptr;
            }
            auto* const prefix
                = aliased_part(named, expression->operands[0], depth + 1U);
            std::vector<std::int64_t> indexes;
            for (std::size_t operand = 1U;
                operand < expression->operands.size(); ++operand) {
                const auto index
                    = integer_literal(expression->operands[operand]);
                if (!index) {
                    return nullptr;
                }
                indexes.push_back(*index);
            }
            return prefix == nullptr ? nullptr : indexed(*prefix, indexes);
        }
        default:
            return nullptr;
        }
    }

    [[nodiscard]] const vhdl::Expression* hir_expression(
        const semantic::ExpressionId id) const
    {
        const auto& expressions = hir_.expressions();
        const auto index = static_cast<std::size_t>(id.value());
        if (index < expressions.size() && expressions[index].id == id) {
            return &expressions[index];
        }
        // Expression ids are shared across languages, so a VHDL
        // expression's position need not be its id.
        const auto found
            = std::ranges::find(expressions, id, &vhdl::Expression::id);
        return found == expressions.end() ? nullptr : &*found;
    }

    // An integer literal, optionally negated.
    [[nodiscard]] std::optional<std::int64_t> integer_literal(
        const semantic::ExpressionId id) const
    {
        const auto* expression = hir_expression(id);
        bool negative = false;
        if (expression != nullptr
            && expression->kind == vhdl::ExpressionKind::unary
            && expression->text == "-" && expression->operands.size() == 1U) {
            negative = true;
            expression = hir_expression(expression->operands.front());
        }
        if (expression == nullptr
            || expression->kind != vhdl::ExpressionKind::integer_literal) {
            return std::nullopt;
        }
        std::string digits;
        for (const auto character : expression->text) {
            if (character != '_') {
                digits.push_back(character);
            }
        }
        std::int64_t value { };
        const auto* const end = digits.data() + digits.size();
        if (const auto [last, failure]
            = std::from_chars(digits.data(), end, value);
            failure != std::errc { } || last != end) {
            return std::nullopt;
        }
        return negative ? -value : value;
    }

    // The value elaboration gave a generic of an instance (IEEE 1076-2019
    // 14.3.2): an integer, or a string shown as its literal.
    void elaborated_generic(const Object& region, Object& object)
    {
        if (region.specialization == nullptr || object.type == nullptr
            || object.type->type_data == nullptr) {
            return;
        }
        const auto& parameters = region.specialization->parameters;
        const auto found = std::ranges::find_if(parameters,
            [&](const auto& parameter) {
                return same_name(parameter.name, object.case_name);
            });
        if (found == parameters.end()) {
            return;
        }
        const std::string_view display = found->value;
        const auto& data = *object.type->type_data;
        if (display.size() >= 2U && display.front() == '"'
            && display.back() == '"') {
            std::string text;
            for (std::size_t position = 1U; position + 1U < display.size();
                 ++position) {
                text.push_back(display[position]);
                if (display[position] == '"') {
                    ++position;
                }
            }
            if (auto value = string_constant(object.type, text)) {
                object.type = value->first;
                object.constant = std::move(value->second);
            }
            return;
        }
        if (data.storage != Storage::integer) {
            return;
        }
        std::int64_t value { };
        const auto* const end = display.data() + display.size();
        if (const auto [last, failure]
            = std::from_chars(display.data(), end, value);
            failure != std::errc { } || last != end) {
            return;
        }
        object.constant = PackedLogic4::from_aval_bval(
            data.width != 0U ? data.width : 32U,
            static_cast<std::uint64_t>(value), 0U);
    }

    // The value of a constant whose initializer is a literal (IEEE 1076-2019
    // 22.5): an integer, a negated integer, a string, which also constrains
    // an unconstrained string type by its length (5.3.2.2), or a record
    // aggregate of those.
    void literal_constant(Object& object, const semantic::ExpressionId id)
    {
        if (auto value = literal_value(object.type, id, 0U)) {
            object.type = value->first;
            object.constant = std::move(value->second);
        }
    }

    [[nodiscard]] std::optional<std::pair<Object*, PackedLogic4>>
    literal_value(Object* const type, const semantic::ExpressionId id,
        const std::size_t depth)
    {
        const auto find = [&](const semantic::ExpressionId expression) {
            return hir_expression(expression);
        };
        const auto* expression = find(id);
        if (expression == nullptr || type == nullptr
            || type->type_data == nullptr || depth > 16U) {
            return std::nullopt;
        }
        const auto& data = *type->type_data;
        // A type no signal lays out has the 32-bit INTEGER representation.
        const auto integer_width = data.width != 0U ? data.width : 32U;
        if (data.storage == Storage::integer && integer_width <= 64U) {
            bool negative = false;
            if (expression->kind == vhdl::ExpressionKind::unary
                && expression->text == "-" && expression->operands.size() == 1U) {
                negative = true;
                expression = find(expression->operands.front());
            }
            if (expression == nullptr
                || expression->kind != vhdl::ExpressionKind::integer_literal) {
                return std::nullopt;
            }
            std::string digits;
            for (const auto character : expression->text) {
                if (character != '_') {
                    digits.push_back(character);
                }
            }
            std::int64_t value { };
            const auto* const end = digits.data() + digits.size();
            if (const auto [last, failure]
                = std::from_chars(digits.data(), end, value);
                failure != std::errc { } || last != end) {
                return std::nullopt;
            }
            return std::pair { type, PackedLogic4::from_aval_bval(
                integer_width,
                static_cast<std::uint64_t>(negative ? -value : value), 0U) };
        }
        if (data.storage == Storage::record
            && expression->kind == vhdl::ExpressionKind::aggregate
            && expression->associations.size() == data.fields.size()) {
            // Each element association names one element, positionally or
            // by its simple name (IEEE 1076-2019 9.3.3.2).
            auto fields = data.fields;
            std::vector<std::optional<PackedLogic4>> values(fields.size());
            for (std::size_t index = 0U;
                 index < expression->associations.size(); ++index) {
                const auto& association = expression->associations[index];
                auto position = index;
                if (!association.positional()) {
                    const auto named = std::ranges::find_if(fields,
                        [&](const Field& field) {
                            return same_name(field.name,
                                association.choice_spelling);
                        });
                    if (named == fields.end()) {
                        return std::nullopt;
                    }
                    position = static_cast<std::size_t>(
                        named - fields.begin());
                }
                if (values[position]) {
                    return std::nullopt;
                }
                auto element = literal_value(
                    fields[position].type, association.value, depth + 1U);
                if (!element) {
                    return std::nullopt;
                }
                fields[position].type = element->first;
                values[position] = std::move(element->second);
            }
            // The first element is the most significant (as selected names
            // read it).
            std::size_t width { };
            for (const auto& value : values) {
                width += value->width();
            }
            PackedLogic4 bits(width, Logic4::zero);
            auto lsb = width;
            for (const auto& value : values) {
                lsb -= value->width();
                for (std::size_t bit = 0U; bit < value->width(); ++bit) {
                    bits.set(lsb + bit, value->get(bit));
                }
            }
            bool refined = false;
            for (std::size_t index = 0U; index < fields.size(); ++index) {
                refined = refined
                    || fields[index].type != data.fields[index].type;
            }
            auto* value_type = type;
            if (refined) {
                value_type = constrained(*type);
                value_type->type_data->fields = std::move(fields);
            }
            return std::pair { value_type, std::move(bits) };
        }
        if (data.storage == Storage::real
            && expression->kind == vhdl::ExpressionKind::real_literal) {
            std::string digits;
            for (const auto character : expression->text) {
                if (character != '_') {
                    digits.push_back(character);
                }
            }
            double value { };
            const auto* const end = digits.data() + digits.size();
            if (const auto [last, failure]
                = std::from_chars(digits.data(), end, value);
                failure != std::errc { } || last != end) {
                return std::nullopt;
            }
            return std::pair { type, PackedLogic4::from_aval_bval(64U,
                std::bit_cast<std::uint64_t>(value), 0U) };
        }
        if (expression->kind != vhdl::ExpressionKind::string_literal
            || !expression->decoded_string) {
            return std::nullopt;
        }
        return string_constant(type, *expression->decoded_string);
    }

    // The value of a string constant, which also constrains an unconstrained
    // string type by its length (IEEE 1076-2019 5.3.2.2).
    [[nodiscard]] std::optional<std::pair<Object*, PackedLogic4>>
    string_constant(Object* const type, const std::string_view text)
    {
        if (type == nullptr || type->type_data == nullptr) {
            return std::nullopt;
        }
        const auto& data = *type->type_data;
        if (data.storage != Storage::array || data.element == nullptr
            || data.element->type_data == nullptr
            || data.element->type_data->literals.size() != 256U
            || text.empty()) {
            return std::nullopt;
        }
        auto* value_type = type;
        if (data.unconstrained || data.dimensions.empty()) {
            const auto* const index = data.index_types.empty()
                ? nullptr
                : data.index_types.front();
            const auto left = index != nullptr && index->type_data != nullptr
                    && index->type_data->range
                ? index->type_data->range->left
                : 1;
            value_type = constrained(*type);
            value_type->type_data->dimensions = { Range { left,
                left + static_cast<std::int64_t>(text.size()) - 1, false } };
            value_type->type_data->unconstrained = false;
        }
        if (value_type->type_data->dimensions.size() != 1U
            || static_cast<std::size_t>(
                   value_type->type_data->dimensions.front().length())
                != text.size()) {
            return std::nullopt;
        }
        PackedLogic4 bits(text.size() * 8U, Logic4::zero);
        for (std::size_t position = 0U; position < text.size(); ++position) {
            const auto code = static_cast<unsigned char>(text[position]);
            const auto base = (text.size() - 1U - position) * 8U;
            for (std::size_t bit = 0U; bit < 8U; ++bit) {
                bits.set(base + bit,
                    ((code >> bit) & 1U) != 0U ? Logic4::one : Logic4::zero);
            }
        }
        return std::pair { value_type, std::move(bits) };
    }

    // The declaration a name denotes in a region; a type name denotes the
    // full declaration of an incomplete type (IEEE 1076-2019 5.4.2).
    [[nodiscard]] Object* find_declared(Object& region, std::string_view name)
    {
        const vhdl::Declaration* match = nullptr;
        for (const auto* const declaration : region.region_declarations) {
            if (!same_name(declaration->name, name)) {
                continue;
            }
            if (match == nullptr) {
                match = declaration;
            } else if (declaration->form == vhdl::DeclarationForm::type
                && match->form == vhdl::DeclarationForm::type) {
                match = declaration;
            }
        }
        if (match != nullptr) {
            return declared(region, *match);
        }
        if (const auto parameter = region.children.find("@param");
            parameter != region.children.end()
            && same_name(parameter->second->case_name, name)) {
            return parameter->second;
        }
        for (auto* const child : child_instances(region)) {
            if (same_name(child->case_name, name)) {
                return child;
            }
        }
        const auto* const generates = region.generate != nullptr
            ? &region.generate->nested
            : region.unit != nullptr && region.kind != vhpiPackInstK
            ? &region.unit->generates
            : nullptr;
        if (generates != nullptr) {
            for (const auto& generate : *generates) {
                if (!same_name(generate.label, name)) {
                    continue;
                }
                if (generate.kind == vhdl::GenerateKind::iterative) {
                    return generate_object(region, generate, std::nullopt);
                }
                const auto occurrences = generate_occurrences(region, generate);
                return occurrences.empty() ? nullptr : occurrences.front();
            }
        }
        // A process or a concurrent statement, by its label.
        if (is_region(region)) {
            for (auto* const statement : concurrent_statements(region)) {
                if (same_name(statement->case_name, name)) {
                    return statement;
                }
            }
        }
        return nullptr;
    }

    // --- elaborated subtypes ---------------------------------------------------------

    [[nodiscard]] const elaboration::SignalInfo* signal_info(
        const SignalId signal)
    {
        if (signal_infos_.empty()) {
            for (const auto& info : simulation_.design().signals()) {
                signal_infos_.emplace(info.id, &info);
            }
        }
        const auto found = signal_infos_.find(signal);
        return found == signal_infos_.end() ? nullptr : found->second;
    }

    // A subtype of `type` with the constraints elaboration resolved for an
    // object (IEEE 1076-2019 14.4.2.2); `type` itself when it already has
    // them.
    template <typename Metadata>
    [[nodiscard]] Object* refine(Object* const type, const Metadata& metadata)
    {
        if (type == nullptr || type->type_data == nullptr) {
            return type;
        }
        const auto& data = *type->type_data;
        if (data.storage == Storage::array && metadata.vhdl_array) {
            const auto& array = *metadata.vhdl_array;
            std::vector<Range> dimensions;
            for (const auto& dimension : array.dimensions) {
                if (!dimension.range) {
                    return type;
                }
                dimensions.push_back(Range { dimension.range->left,
                    dimension.range->right, dimension.range->descending });
            }
            auto* element = data.element;
            if (element != nullptr && !array.element_types.empty()) {
                element = refine(element, array.element_types.front());
            }
            if (dimensions.empty()
                || (dimensions == data.dimensions && element == data.element)) {
                return type;
            }
            auto* const result = constrained(*type);
            result->type_data->dimensions = std::move(dimensions);
            result->type_data->element = element;
            result->type_data->unconstrained = false;
            return result;
        }
        if (data.storage == Storage::record && !metadata.packed_members.empty()) {
            auto fields = data.fields;
            bool changed = false;
            for (auto& field : fields) {
                const auto member = std::ranges::find_if(
                    metadata.packed_members, [&](const auto& candidate) {
                        return same_name(candidate.name, field.name);
                    });
                if (member == metadata.packed_members.end()
                    || member->nested_types.empty()) {
                    continue;
                }
                auto* const refined
                    = refine(field.type, member->nested_types.front());
                changed = changed || refined != field.type;
                field.type = refined;
            }
            if (!changed) {
                return type;
            }
            auto* const result = constrained(*type);
            result->type_data->fields = std::move(fields);
            return result;
        }
        return type;
    }

    // An anonymous subtype of `type` that adds constraints.
    [[nodiscard]] Object* constrained(Object& type)
    {
        auto* const result = make_object(vhpiSubtypeDeclK, "", nullptr);
        result->type_data = std::make_unique<TypeData>(*type.type_data);
        result->type_data->parent = &type;
        result->name = type.name;
        result->case_name = type.case_name;
        result->full_name = type.full_name;
        result->full_case_name = type.full_case_name;
        return result;
    }

    // The element declarations of a record type, shared by its subtypes.
    [[nodiscard]] std::vector<Object*>& element_declarations(Object& type)
    {
        auto* const base = type.type_data != nullptr
                && type.type_data->base != nullptr
            ? type.type_data->base
            : &type;
        auto& list = base->lists[ListKind::record_elems];
        if (list.empty() && base->type_data != nullptr) {
            const auto& fields = base->type_data->fields;
            for (std::size_t index = 0U; index < fields.size(); ++index) {
                auto* const element
                    = make_object(vhpiElemDeclK, fields[index].case_name, base);
                // Element names keep their declared spelling.
                element->name = fields[index].case_name;
                element->type = fields[index].type;
                element->position = static_cast<std::int64_t>(index);
                element->full_name = base->full_name + "." + element->name;
                element->full_case_name
                    = base->full_case_name + "." + element->case_name;
                list.push_back(element);
            }
        }
        return list;
    }

    // --- names -----------------------------------------------------------------------

    // The element of a composite object a selected or indexed name denotes.
    [[nodiscard]] Object* selected(Object& prefix, std::string_view element)
    {
        auto* const type = prefix.type;
        if (type == nullptr || type->type_data == nullptr
            || type->type_data->storage != Storage::record) {
            return nullptr;
        }
        const auto key = "." + lower(element);
        if (const auto found = prefix.children.find(key);
            found != prefix.children.end()) {
            return found->second;
        }
        const auto& fields = type->type_data->fields;
        auto& declarations = element_declarations(*type);
        std::size_t lsb = prefix.offset + width_of(*type);
        for (std::size_t index = 0U; index < fields.size(); ++index) {
            const auto& field = fields[index];
            lsb -= width_of(*field.type);
            if (!same_name(field.name, element)) {
                continue;
            }
            auto* const object
                = make_object(vhpiSelectedNameK, field.case_name, &prefix);
            object->name = prefix.name + "." + field.case_name;
            object->case_name = prefix.case_name + "." + field.case_name;
            object->full_name = prefix.full_name + "." + field.case_name;
            object->full_case_name
                = prefix.full_case_name + "." + field.case_name;
            object->signal = prefix.signal;
            object->constant = prefix.constant;
            object->offset = lsb;
            object->type = field.type;
            object->declaration = prefix.declaration;
            if (index < declarations.size()) {
                object->suffix = declarations[index];
            }
            prefix.children.emplace(key, object);
            return object;
        }
        return nullptr;
    }

    // `left downto right` or `left to right` with literal bounds.
    [[nodiscard]] static std::optional<Range> parse_slice(
        std::string_view text)
    {
        const auto skip = [&] {
            while (!text.empty() && text.front() == ' ') {
                text.remove_prefix(1U);
            }
        };
        const auto number = [&]() -> std::optional<std::int64_t> {
            skip();
            std::int64_t value { };
            const auto parsed
                = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc { }) {
                return std::nullopt;
            }
            text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
            return value;
        };
        const auto left = number();
        if (!left) {
            return std::nullopt;
        }
        skip();
        bool descending = false;
        if (text.size() >= 6U && same_name(text.substr(0U, 6U), "downto")) {
            descending = true;
            text.remove_prefix(6U);
        } else if (text.size() >= 2U && same_name(text.substr(0U, 2U), "to")) {
            text.remove_prefix(2U);
        } else {
            return std::nullopt;
        }
        const auto right = number();
        skip();
        if (!right || !text.empty()) {
            return std::nullopt;
        }
        return Range { *left, *right, descending };
    }

    // The slice of a one-dimensional array object whose index range lies
    // within the object's and runs in the same direction (IEEE 1076-2019
    // 8.5, 19.4.3 sliceName); its type is an anonymous subtype of the
    // prefix's type constrained to the slice.
    [[nodiscard]] Object* sliced(Object& prefix, const Range& slice)
    {
        auto* const type = prefix.type;
        if (type == nullptr || type->type_data == nullptr
            || type->type_data->storage != Storage::array
            || type->type_data->dimensions.size() != 1U
            || type->type_data->element == nullptr) {
            return nullptr;
        }
        const auto& data = *type->type_data;
        const auto& whole = data.dimensions.front();
        if (slice.descending != whole.descending) {
            return nullptr;
        }
        const auto left = whole.position(slice.left);
        const auto right = whole.position(slice.right);
        if (!left || !right || *left > *right) {
            return nullptr;
        }
        const auto suffix = "(" + std::to_string(slice.left)
            + (slice.descending ? " downto " : " to ")
            + std::to_string(slice.right) + ")";
        if (const auto found = prefix.children.find(suffix);
            found != prefix.children.end()) {
            return found->second;
        }
        const auto count = static_cast<std::size_t>(whole.length());
        auto* const object = make_object(vhpiSliceNameK, "", &prefix);
        object->name = prefix.name + suffix;
        object->case_name = prefix.case_name + suffix;
        object->full_name = prefix.full_name + suffix;
        object->full_case_name = prefix.full_case_name + suffix;
        object->signal = prefix.signal;
        object->constant = prefix.constant;
        object->declaration = prefix.declaration;
        object->offset = prefix.offset
            + (count - 1U - static_cast<std::size_t>(*right))
                * width_of(*data.element);
        object->position = *left;
        auto* const subtype = constrained(*type);
        subtype->type_data->dimensions = { slice };
        subtype->type_data->unconstrained = false;
        object->type = subtype;
        prefix.children.emplace(suffix, object);
        return object;
    }

    // The element of an array object at one index value per dimension.
    [[nodiscard]] Object* indexed(Object& prefix,
        const std::span<const std::int64_t> indexes)
    {
        if (prefix.kind == vhpiForGenerateK && !prefix.generate_index
            && prefix.generate != nullptr && prefix.parent != nullptr) {
            if (indexes.size() != 1U) {
                return nullptr;
            }
            for (auto* const occurrence :
                generate_occurrences(*prefix.parent, *prefix.generate)) {
                if (occurrence->generate_index == indexes.front()) {
                    return occurrence;
                }
            }
            return nullptr;
        }
        auto* const type = prefix.type;
        if (type == nullptr || type->type_data == nullptr
            || type->type_data->dimensions.size() != indexes.size()
            || type->type_data->element == nullptr) {
            return nullptr;
        }
        const auto& data = *type->type_data;
        std::size_t position = 0U;
        std::size_t count = 1U;
        std::string suffix = "(";
        for (std::size_t dimension = 0U; dimension < indexes.size();
             ++dimension) {
            const auto& range = data.dimensions[dimension];
            const auto offset = range.position(indexes[dimension]);
            if (!offset) {
                return nullptr;
            }
            const auto length = static_cast<std::size_t>(range.length());
            position = position * length + static_cast<std::size_t>(*offset);
            count *= length;
            if (dimension != 0U) {
                suffix += ',';
            }
            suffix += std::to_string(indexes[dimension]);
        }
        suffix += ')';
        if (const auto found = prefix.children.find(suffix);
            found != prefix.children.end()) {
            return found->second;
        }
        const auto element_width = width_of(*data.element);
        auto* const object = make_object(vhpiIndexedNameK, "", &prefix);
        object->name = prefix.name + suffix;
        object->case_name = prefix.case_name + suffix;
        object->full_name = prefix.full_name + suffix;
        object->full_case_name = prefix.full_case_name + suffix;
        object->signal = prefix.signal;
        object->constant = prefix.constant;
        object->offset = prefix.offset + (count - 1U - position) * element_width;
        object->type = data.element;
        object->declaration = prefix.declaration;
        object->indexes.assign(indexes.begin(), indexes.end());
        object->position = static_cast<std::int64_t>(position);
        prefix.children.emplace(suffix, object);
        return object;
    }

    // Resolve `name` relative to `scope`, or as a path from the root
    // instance when there is no scope (IEEE 1076-2019 23.21).
    [[nodiscard]] vhpiHandleT handle_by_name(const char* const text,
        const vhpiHandleT scope_handle)
    {
        if (text == nullptr) {
            error(vhpiError, "missing name");
            return nullptr;
        }
        std::string_view name { text };
        Object* scope = nullptr;
        if (scope_handle != nullptr) {
            scope = object_of(scope_handle);
            if (scope == nullptr) {
                return nullptr;
            }
        } else {
            scope = root();
            if (scope == nullptr) {
                return nullptr;
            }
            const bool absolute = name.starts_with(':');
            if (absolute) {
                name.remove_prefix(1U);
            }
            const auto split = name.find_first_of(".:(");
            const auto first = name.substr(0U, split);
            Object* top = nullptr;
            if (same_name(first, scope->case_name)) {
                top = scope;
            } else if (auto* const package = package_instance(first)) {
                top = package;
            } else if (auto* const other = other_root(first)) {
                top = other;
            }
            if (top != nullptr) {
                scope = top;
                if (split == std::string_view::npos) {
                    return to_handle(scope);
                }
                name.remove_prefix(split + 1U);
            } else if (absolute) {
                return nullptr;
            }
        }
        auto* current = scope;
        while (!name.empty() && current != nullptr) {
            if (name.front() == '(') {
                const auto close = name.find(')');
                if (close == std::string_view::npos) {
                    return nullptr;
                }
                std::vector<std::int64_t> indexes;
                auto inner = name.substr(1U, close - 1U);
                // A slice name `(left downto right)` (IEEE 1076-2019 8.5).
                if (const auto slice = parse_slice(inner)) {
                    current = sliced(*current, *slice);
                    name.remove_prefix(close + 1U);
                    if (!name.empty()
                        && (name.front() == '.' || name.front() == ':')) {
                        name.remove_prefix(1U);
                    }
                    continue;
                }
                while (true) {
                    while (!inner.empty() && inner.front() == ' ') {
                        inner.remove_prefix(1U);
                    }
                    std::int64_t index { };
                    const auto parsed = std::from_chars(
                        inner.data(), inner.data() + inner.size(), index);
                    if (parsed.ec != std::errc { }) {
                        return nullptr;
                    }
                    indexes.push_back(index);
                    inner.remove_prefix(
                        static_cast<std::size_t>(parsed.ptr - inner.data()));
                    while (!inner.empty() && inner.front() == ' ') {
                        inner.remove_prefix(1U);
                    }
                    if (inner.empty()) {
                        break;
                    }
                    if (inner.front() != ',') {
                        return nullptr;
                    }
                    inner.remove_prefix(1U);
                }
                current = indexed(*current, indexes);
                name.remove_prefix(close + 1U);
                if (!name.empty() && (name.front() == '.' || name.front() == ':')) {
                    name.remove_prefix(1U);
                }
                continue;
            }
            const auto split = name.find_first_of(".:(");
            auto part = name.substr(0U, split);
            while (!part.empty() && part.back() == ' ') {
                part.remove_suffix(1U);
            }
            if (is_declarative(*current)) {
                current = find_declared(*current, part);
            } else {
                current = selected(*current, part);
            }
            if (split == std::string_view::npos) {
                name = { };
            } else {
                name.remove_prefix(split);
                if (name.front() != '(') {
                    name.remove_prefix(1U);
                }
            }
        }
        return current == nullptr ? nullptr : to_handle(current);
    }

    [[nodiscard]] static bool is_region(const Object& object) noexcept
    {
        switch (object.kind) {
        case vhpiRootInstK:
        case vhpiCompInstStmtK:
        case vhpiBlockStmtK:
        case vhpiForGenerateK:
        case vhpiIfGenerateK:
        case vhpiPackInstK:
            return true;
        default:
            return false;
        }
    }

    // A region, a process, a for loop or a subprogram body, and a component
    // declaration for its interface: the objects whose declarative part
    // holds declarations that names can denote.
    [[nodiscard]] static bool is_declarative(const Object& object) noexcept
    {
        return is_region(object) || object.kind == vhpiProcessStmtK
            || object.kind == vhpiForLoopK || object.kind == vhpiSubpBodyK
            || object.kind == vhpiCompDeclK;
    }

    [[nodiscard]] static bool is_type(const Object& object) noexcept
    {
        return object.type_data != nullptr;
    }

    // --- relations -------------------------------------------------------------------

    [[nodiscard]] vhpiHandleT one(const vhpiOneToOneT relation,
        const vhpiHandleT reference)
    {
        if (relation == vhpiRootInst) {
            if (phase_ == vhpiRegistrationPhase) {
                return nullptr;
            }
            return to_handle(root());
        }
        if (relation == vhpiTool) {
            return to_handle(tool());
        }
        auto* const object = object_of(reference);
        if (object == nullptr) {
            return nullptr;
        }
        switch (relation) {
        case DEPRECATED_vhpiValSubtype:
        case vhpiValType:
            // An access or file type's values are of its designated type
            // (IEEE 1076-2019 5.4.1, 5.5.1).
            if (is_type(*object) && object->type_data->designated != nullptr) {
                if (object->type == nullptr) {
                    object->type = subtype_object(
                        *object->type_data->designated, nullptr);
                }
                return object->type == nullptr ? nullptr
                                               : to_handle(object->type);
            }
            [[fallthrough]];
        case DEPRECATED_vhpiSubtype:
        case vhpiType:
            if (object->type == nullptr && object->expression != nullptr) {
                object->type = expression_type(*object);
            }
            return object->type == nullptr ? nullptr : to_handle(object->type);
        case vhpiBaseType: {
            auto* const type = is_type(*object) ? object : object->type;
            if (type == nullptr || type->type_data == nullptr) {
                return nullptr;
            }
            return to_handle(type->type_data->base);
        }
        case DEPRECATED_vhpiElemSubtype:
        case vhpiElemType: {
            auto* type = is_type(*object) ? object : object->type;
            if (type == nullptr || type->type_data == nullptr) {
                return nullptr;
            }
            if (type->type_data->element == nullptr
                && type->type_data->base != nullptr) {
                type = type->type_data->base;
            }
            return type->type_data->element == nullptr
                ? nullptr
                : to_handle(type->type_data->element);
        }
        case vhpiPrefix:
            if (object->kind == vhpiSelectedNameK
                || object->kind == vhpiIndexedNameK
                || object->kind == vhpiSliceNameK) {
                return to_handle(object->parent);
            }
            if ((object->kind == vhpiSimpAttrNameK
                    || object->kind == vhpiParamAttrNameK
                    || object->kind == vhpiUserAttrNameK)
                && object->expression != nullptr
                && !object->expression->operands.empty()) {
                auto* const prefix = expression_object(
                    *object, object->expression->operands.front());
                return prefix == nullptr ? nullptr : to_handle(prefix);
            }
            return nullptr;
        case vhpiSubpBody: {
            // The body of a subprogram declaration (Figure 34).
            auto* const body
                = (object->kind == vhpiFuncDeclK || object->kind == vhpiProcDeclK)
                    && object->parent != nullptr && object->declaration != nullptr
                ? body_object(*object->parent, *object->declaration)
                : nullptr;
            return body == nullptr ? nullptr : to_handle(body);
        }
        case vhpiReturnType: {
            // The result type of a function declaration.
            if (object->kind != vhpiFuncDeclK || object->declaration == nullptr
                || !object->declaration->callable
                || !object->declaration->callable->return_type) {
                return nullptr;
            }
            auto* const type = subtype_object(
                *object->declaration->callable->return_type, nullptr);
            return type == nullptr ? nullptr : to_handle(type);
        }
        case vhpiSuffix:
            return object->suffix == nullptr ? nullptr
                                             : to_handle(object->suffix);
        case vhpiDesignUnit: {
            auto* const unit = design_unit_object(*object);
            return unit == nullptr ? nullptr : to_handle(unit);
        }
        case vhpiPrimaryUnit: {
            // The entity declaration an architecture body is of (IEEE
            // 1076-2019 19.4.5).
            auto* const entity = primary_unit_object(*object);
            return entity == nullptr ? nullptr : to_handle(entity);
        }
        case vhpiEntityDecl: {
            // The entity declaration of an architecture body or of an
            // instance's design unit (IEEE 1076-2019 19.4.2).
            auto* const unit
                = is_region(*object) ? design_unit_object(*object) : object;
            auto* const entity
                = unit == nullptr ? nullptr : primary_unit_object(*unit);
            return entity == nullptr ? nullptr : to_handle(entity);
        }
        case vhpiAliasedName:
            return object->aliased == nullptr ? nullptr
                                              : to_handle(object->aliased);
        case DEPRECATED_vhpiBasicSignal:
            // The basic signal a driver drives (IEEE 1076-2019 19.12.1).
            return object->kind == vhpiDriverK && object->basic != nullptr
                ? to_handle(object->basic)
                : nullptr;
        case vhpiEqProcessStmt: {
            // The process, or the concurrent statement equivalent to one,
            // that holds a driver; a concurrent statement's equivalent
            // process.
            if (object->kind == vhpiDriverK) {
                return object->parent != nullptr ? to_handle(object->parent)
                                                 : nullptr;
            }
            auto* const process = equivalent_process(*object);
            return process == nullptr ? nullptr : to_handle(process);
        }
        case vhpiSimNet:
            // No object is selected to represent a simulated net, so none
            // is the target of a SimNet association (19.12.2.4).
            return nullptr;
        case vhpiConstraint: {
            if (object->kind == vhpiForLoopK) {
                auto* const range = loop_range(*object);
                return range == nullptr ? nullptr : to_handle(range);
            }
            // The first constraint of a type (IEEE 1076-2019 19.4.3).
            const auto constraints = type_list(vhpiConstraints, object);
            return constraints.empty() ? nullptr : to_handle(constraints.front());
        }
        case DEPRECATED_vhpiDecl: {
            // The declared object a name denotes a part of.
            auto* declaration = object;
            while ((declaration->kind == vhpiIndexedNameK
                       || declaration->kind == vhpiSelectedNameK
                       || declaration->kind == vhpiSliceNameK)
                && declaration->parent != nullptr) {
                declaration = declaration->parent;
            }
            return to_handle(declaration);
        }
        case vhpiCurRegion:
        case vhpiImmRegion: {
            // The innermost region that contains the object (IEEE 1076-2019
            // 19.4.2).
            for (auto* region = object->parent; region != nullptr;
                 region = region->parent) {
                if (is_region(*region)) {
                    return to_handle(region);
                }
            }
            return nullptr;
        }
        case vhpiParamDecl: {
            const auto found = object->children.find("@param");
            return found == object->children.end() ? nullptr
                                                   : to_handle(found->second);
        }
        case vhpiCondExpr:
        case vhpiLhsExpr:
        case vhpiRhsExpr:
        case vhpiCaseExpr:
        case vhpiSelectExpr:
        case vhpiReturnExpr:
        case vhpiTimeOutExpr:
        case vhpiReportExpr:
        case vhpiSeverityExpr:
        case vhpiRejectTime:
        case vhpiTimeExpr:
        case vhpiValExpr: {
            // The expression of a statement, branch or waveform (IEEE
            // 1076-2019 19.11); one with no object leads to none.
            const auto id = statement_expression(*object, relation);
            auto* expression = id ? expression_object(*object, *id) : nullptr;
            if (!id) {
                if (const auto* const delay = statement_delay(*object, relation)) {
                    expression = delay_literal(*delay);
                }
            }
            return expression == nullptr ? nullptr : to_handle(expression);
        }
        case vhpiInPort:
        case vhpiOutPort: {
            // The aspects of a port that are read and driven from inside
            // (IEEE 1076-2019 19.12.1, Figure 46).
            const auto* const port = object->declaration;
            if (!object->signal || object->kind == vhpiDriverK
                || !(relation == vhpiInPort ? port_reads(port)
                                            : port_drives(port))) {
                return nullptr;
            }
            return to_handle(port_view(
                *object, relation == vhpiInPort ? vhpiInPortK : vhpiOutPortK));
        }
        case vhpiActual: {
            // The actual of an association element (Figure 35); a conversion
            // of a port association's actual part stands for the part.
            if (object->kind == vhpiAssocElemK
                && object->port_association != nullptr) {
                const auto& connection = port_connection(*object);
                auto* const part = connection.actual_conversion != nullptr
                    ? connection.actual_conversion
                    : connection.actual;
                return part == nullptr ? nullptr : to_handle(part);
            }
            auto* const actual = object->kind == vhpiAssocElemK && object->actual
                ? expression_object(*object, *object->actual)
                : nullptr;
            return actual == nullptr ? nullptr : to_handle(actual);
        }
        case vhpiFormal: {
            // The port a port association's formal part designates, or the
            // formal parameter a call's association element is for
            // (Figure 35).
            if (object->kind == vhpiAssocElemK
                && object->port_association != nullptr) {
                const auto& connection = port_connection(*object);
                return connection.formal == nullptr
                    ? nullptr
                    : to_handle(connection.formal);
            }
            auto* const formal = formal_of(*object);
            return formal == nullptr ? nullptr : to_handle(formal);
        }
        case vhpiExpr: {
            // The value of an element association (Figure 23), the operand
            // of a type conversion or qualified expression (Figure 25).
            Object* value = nullptr;
            if (object->kind == vhpiElemAssocK && object->association != nullptr) {
                value = expression_object(*object, object->association->value);
            } else if ((object->kind == vhpiTypeConvK
                           || object->kind == vhpiQualifiedExprK)
                && object->expression != nullptr
                && !object->expression->operands.empty()) {
                value = expression_object(
                    *object, object->expression->operands.front());
            }
            return value == nullptr ? nullptr : to_handle(value);
        }
        case vhpiInitExpr: {
            // The initial value of an allocator (Figure 29).
            auto* const value = object->kind == vhpiAllocatorK
                    && object->expression != nullptr
                    && object->expression->operands.size() > 1U
                ? expression_object(*object, object->expression->operands[1])
                : nullptr;
            return value == nullptr ? nullptr : to_handle(value);
        }
        case vhpiUpperRegion:
            // The region of a sequential statement is the region of its
            // process, not the enclosing statement.
            if (object->sequential) {
                for (auto* region = object->parent; region != nullptr;
                     region = region->parent) {
                    if (is_region(*region)) {
                        return to_handle(region);
                    }
                }
                return nullptr;
            }
            [[fallthrough]];
        case vhpiParent:
            return object->parent == nullptr ? nullptr
                                             : to_handle(object->parent);
        default:
            error(vhpiError,
                "unsupported relation " + std::to_string(relation));
            return nullptr;
        }
    }

    // The design unit object of a region (IEEE 1076-2019 19.4.2).
    [[nodiscard]] Object* design_unit_object(Object& region)
    {
        if (!is_region(region) || region.unit == nullptr) {
            return nullptr;
        }
        const auto found = region.children.find("@unit");
        if (found != region.children.end()) {
            return found->second;
        }
        const auto kind = region.unit->kind == vhdl::UnitKind::package
            ? vhpiPackDeclK
            : vhpiArchBodyK;
        auto* const unit = make_object(kind, region.unit->name, nullptr);
        unit->full_name = upper(region.unit->library) + "." + unit->name;
        unit->full_case_name = region.unit->library + "." + unit->case_name;
        unit->unit = region.unit;
        unit->entity = region.entity;
        region.children.emplace("@unit", unit);
        return unit;
    }

    // The entity declaration an architecture body object is of.
    [[nodiscard]] Object* primary_unit_object(Object& architecture)
    {
        if (architecture.kind != vhpiArchBodyK
            || architecture.entity == nullptr) {
            return nullptr;
        }
        const auto found = architecture.children.find("@primary");
        if (found != architecture.children.end()) {
            return found->second;
        }
        auto* const entity = make_object(
            vhpiEntityDeclK, architecture.entity->name, nullptr);
        entity->full_name
            = upper(architecture.entity->library) + "." + entity->name;
        entity->full_case_name
            = architecture.entity->library + "." + entity->case_name;
        entity->unit = architecture.entity;
        architecture.children.emplace("@primary", entity);
        return entity;
    }

    [[nodiscard]] Object* tool()
    {
        if (tool_ == nullptr) {
            tool_ = make_object(vhpiToolK, "fsim", nullptr);
            tool_->full_name = tool_->name;
            tool_->full_case_name = tool_->case_name;
        }
        return tool_;
    }

    [[nodiscard]] std::vector<Object*> list(const vhpiOneToManyT relation,
        Object* const object)
    {
        std::vector<Object*> result;
        const auto declarations = [&](auto&& accept) {
            if (object == nullptr || !is_declarative(*object)) {
                return;
            }
            for (const auto* const declaration : object->region_declarations) {
                if (!accept(*declaration)) {
                    continue;
                }
                if (auto* const declared_object = declared(*object, *declaration)) {
                    result.push_back(declared_object);
                }
            }
        };
        using Form = vhdl::DeclarationForm;
        switch (relation) {
        case vhpiDecls:
            declarations([&](const vhdl::Declaration& d) {
                // The formals of a subprogram are its declaration's
                // parameters, not declarations of its body.
                if (object->kind == vhpiSubpBodyK && d.form == Form::port) {
                    return false;
                }
                return d.form == Form::port || d.form == Form::signal
                    || d.form == Form::generic_constant
                    || d.form == Form::constant || d.form == Form::variable
                    || d.form == Form::type || d.form == Form::subtype
                    || d.form == Form::alias || item_kind(d.form);
            });
            break;
        case vhpiAliasDecls:
            declarations([](const vhdl::Declaration& d) {
                return d.form == Form::alias;
            });
            break;
        case vhpiSigDecls:
            declarations([](const vhdl::Declaration& d) {
                return d.form == Form::signal;
            });
            break;
        case vhpiPortDecls:
            declarations([](const vhdl::Declaration& d) {
                return d.form == Form::port;
            });
            break;
        case vhpiGenericDecls:
            declarations([](const vhdl::Declaration& d) {
                return d.form == Form::generic_constant;
            });
            break;
        case vhpiConstDecls:
            declarations([](const vhdl::Declaration& d) {
                return d.form == Form::constant;
            });
            break;
        case vhpiVarDecls:
            declarations([](const vhdl::Declaration& d) {
                return d.form == Form::variable;
            });
            break;
        case vhpiCompInstStmts:
            if (object != nullptr && is_region(*object)) {
                result = child_instances(*object);
            }
            break;
        case vhpiInternalRegions:
            if (object != nullptr && is_region(*object)) {
                result = child_instances(*object);
                const auto statements = child_statements(*object);
                result.insert(result.end(), statements.begin(),
                    statements.end());
            }
            break;
        case vhpiBlockStmts:
        case vhpiGenerateStmts:
            if (object != nullptr && is_region(*object)) {
                for (auto* const statement : child_statements(*object)) {
                    if ((statement->kind == vhpiBlockStmtK)
                        == (relation == vhpiBlockStmts)) {
                        result.push_back(statement);
                    }
                }
            }
            break;
        case vhpiStmts:
            if (object != nullptr && is_region(*object)) {
                result = region_statements(*object);
            }
            break;
        case vhpiSeqStmts:
            if (object != nullptr && object->body != nullptr) {
                result = sequential_statements(*object);
            }
            break;
        case vhpiBranchs:
            if (object != nullptr
                && (object->kind == vhpiIfStmtK || object->kind == vhpiCaseStmtK)) {
                result = branch_objects(*object, ListKind::branches);
            }
            break;
        case vhpiCondWaveforms:
            if (object != nullptr && object->kind == vhpiCondSigAssignStmtK) {
                result = branch_objects(*object, ListKind::cond_waveforms);
            }
            break;
        case vhpiSelectWaveforms:
            if (object != nullptr && object->kind == vhpiSelectSigAssignStmtK) {
                result = branch_objects(*object, ListKind::select_waveforms);
            }
            break;
        case vhpiWaveformElems:
            if (object != nullptr
                && (object->kind == vhpiSimpleSigAssignStmtK
                    || object->kind == vhpiSeqSigAssignStmtK
                    || object->kind == vhpiCondWaveformK
                    || object->kind == vhpiSelectWaveformK)) {
                result = waveform_elements(*object);
            }
            break;
        case vhpiChoices:
            if (object != nullptr
                && (object->kind == vhpiBranchK
                    || object->kind == vhpiSelectWaveformK)) {
                result = choice_objects(*object);
            } else if (object != nullptr && object->kind == vhpiElemAssocK) {
                result = association_choices(*object);
            }
            break;
        case vhpiParamAssocs:
            if (object != nullptr
                && (object->kind == vhpiFuncCallK
                    || object->kind == vhpiSeqProcCallStmtK
                    || object->kind == vhpiConcProcCallStmtK)) {
                result = parameter_associations(*object);
            }
            break;
        case vhpiElemAssocs:
            if (object != nullptr && object->kind == vhpiAggregateK) {
                result = element_associations(*object);
            }
            break;
        case vhpiMembers:
            if (object != nullptr && is_collection(*object)) {
                result = object->lists[ListKind::members];
            }
            break;
        case vhpiEqProcessStmts:
            if (object != nullptr) {
                if (auto* const process = equivalent_process(*object)) {
                    result.push_back(process);
                }
            }
            break;
        case DEPRECATED_vhpiSensitivitys:
        case vhpiSigNames:
            if (object != nullptr) {
                result = sensitivity_objects(*object);
            }
            break;
        case vhpiIndexedNames:
            result = indexed_names(object);
            break;
        case vhpiSelectedNames:
            result = selected_names(object);
            break;
        case vhpiRecordElems:
        case vhpiEnumLiterals:
        case vhpiConstraints:
        case vhpiUnitDecls:
            result = type_list(relation, object);
            break;
        case vhpiPackInsts:
            result = package_instances();
            break;
        case vhpiParamDecls:
            if (object != nullptr
                && (object->kind == vhpiFuncDeclK || object->kind == vhpiProcDeclK)) {
                result = parameter_declarations(*object);
            } else if (object != nullptr) {
                result = object->lists[ListKind::param_decls];
            }
            break;
        case vhpiIndexExprs:
            if (object != nullptr && object->kind == vhpiIndexedNameK) {
                result = index_expressions(*object);
            }
            break;
        case vhpiParamExprs:
            if (object != nullptr && object->kind == vhpiParamAttrNameK
                && object->expression != nullptr) {
                const auto& operands = object->expression->operands;
                for (std::size_t index = 1U; index < operands.size(); ++index) {
                    if (auto* const operand
                        = expression_object(*object, operands[index])) {
                        result.push_back(operand);
                    }
                }
            }
            break;
        case vhpiDrivers:
            if (object != nullptr
                && (object->kind == vhpiProcessStmtK || is_signal_object(*object)
                    || (object->is_stmt && !object->sequential))) {
                result = drivers_of(*object);
            }
            break;
        case vhpiBasicSignals:
            if (object != nullptr && is_signal_object(*object)) {
                result = basic_signals(*object);
            }
            break;
        case vhpiPortAssocs:
            if (object != nullptr && is_region(*object)) {
                result = port_associations(*object);
            }
            break;
        case vhpiLocalContributors:
            // The drivers, the ports and conversions associated with the
            // signal, and a default expression when it has no source
            // (IEEE 1076-2019 19.12.2.2).
            if (object != nullptr && is_signal_object(*object)) {
                for (auto* const basic : basic_signals(*object)) {
                    const auto first = result.size();
                    const auto drivers = signal_drivers(*basic);
                    result.insert(result.end(), drivers.begin(), drivers.end());
                    for (auto* const other : connection_objects(*basic, true)) {
                        if (std::ranges::find(result, other) == result.end()) {
                            result.push_back(other);
                        }
                    }
                    if (result.size() == first && basic->declaration != nullptr
                        && basic->declaration->form
                            == vhdl::DeclarationForm::signal
                        && basic->declaration->initializer) {
                        if (auto* const region = declaring_region(*basic)) {
                            if (auto* const initial = expression_object(
                                    *region, *basic->declaration->initializer)) {
                                result.push_back(initial);
                            }
                        }
                    }
                }
            }
            break;
        case vhpiLocalLoads:
            // The processes that read the signal, the ports and conversions
            // it is associated with (19.12.2.3).
            if (object != nullptr && is_signal_object(*object)) {
                for (auto* const basic : basic_signals(*object)) {
                    for (auto* const load : signal_loads(*basic)) {
                        if (std::ranges::find(result, load) == result.end()) {
                            result.push_back(load);
                        }
                    }
                    for (auto* const load : connection_objects(*basic, false)) {
                        if (std::ranges::find(result, load) == result.end()) {
                            result.push_back(load);
                        }
                    }
                }
            }
            break;
        case vhpiOptimizedContributors:
        case vhpiOptimizedLoads:
            // No contributor or load is optimized away (19.12.2.1).
            break;
        case vhpiArgvs:
            result = argv_objects();
            break;
        default:
            error(vhpiError, "unsupported iteration " + std::to_string(relation));
            break;
        }
        return result;
    }

    // The instances of the design's packages, STANDARD first (IEEE
    // 1076-2019 19.4.2).
    [[nodiscard]] std::vector<Object*> package_instances()
    {
        std::vector<Object*> result;
        if (auto* const standard = package_instance("standard")) {
            result.push_back(standard);
        }
        for (const auto& unit : hir_.units()) {
            if (unit.kind != vhdl::UnitKind::package
                || same_name(unit.name, "standard")) {
                continue;
            }
            if (auto* const package = package_instance(unit.name);
                package != nullptr
                && std::ranges::find(result, package) == result.end()) {
                result.push_back(package);
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<Object*> argv_objects()
    {
        if (argvs_.empty()) {
            for (const auto& argument : arguments_) {
                auto* const object = make_object(vhpiArgvK, argument, tool());
                object->name = argument;
                object->full_name = argument;
                argvs_.push_back(object);
            }
        }
        return argvs_;
    }

    // Every element name of a composite object, in left-to-right order
    // (IEEE 1076-2019 19.4.3).
    [[nodiscard]] std::vector<Object*> selected_names(Object* const object)
    {
        std::vector<Object*> result;
        if (object == nullptr || object->type == nullptr
            || object->type->type_data == nullptr
            || object->type->type_data->storage != Storage::record) {
            return result;
        }
        for (const auto& field : object->type->type_data->fields) {
            if (auto* const element = selected(*object, field.name)) {
                result.push_back(element);
            }
        }
        return result;
    }

    // Every element of an array object, the rightmost index varying
    // fastest (IEEE 1076-2019 19.4.3).
    [[nodiscard]] std::vector<Object*> indexed_names(Object* const object)
    {
        std::vector<Object*> result;
        if (object == nullptr || object->type == nullptr
            || object->type->type_data == nullptr
            || object->type->type_data->dimensions.empty()) {
            return result;
        }
        const auto dimensions = object->type->type_data->dimensions;
        std::size_t count = 1U;
        for (const auto& range : dimensions) {
            count *= static_cast<std::size_t>(range.length());
        }
        std::vector<std::int64_t> indexes(dimensions.size());
        for (std::size_t position = 0U;
             position < count && result.size() < maximum_list_size;
             ++position) {
            auto rest = position;
            for (std::size_t dimension = dimensions.size(); dimension-- > 0U;) {
                const auto length
                    = static_cast<std::size_t>(dimensions[dimension].length());
                indexes[dimension] = dimensions[dimension].index(
                    static_cast<std::int64_t>(rest % length));
                rest /= length;
            }
            if (auto* const element = indexed(*object, indexes)) {
                result.push_back(element);
            }
        }
        return result;
    }

    [[nodiscard]] vhpiHandleT iterator(const vhpiOneToManyT relation,
        const vhpiHandleT reference)
    {
        Object* object = nullptr;
        if (reference != nullptr) {
            object = object_of(reference);
            if (object == nullptr) {
                return nullptr;
            }
        } else if (relation != vhpiArgvs) {
            object = root();
        }
        auto items = list(relation, object);
        // An association with no target objects, like an unsupported one,
        // has no iterator (IEEE 1076-2019 23.24).
        if (has_error_ || items.empty()) {
            return nullptr;
        }
        auto stored = std::make_unique<Iterator>();
        stored->items = std::move(items);
        iterators_.push_back(std::move(stored));
        return to_handle(iterators_.back().get());
    }

    [[nodiscard]] vhpiHandleT scan(const vhpiHandleT handle)
    {
        auto* const base = handle_of(handle);
        if (base == nullptr || base->tag != HandleTag::iterator) {
            error(vhpiError, "handle is not an iterator");
            return nullptr;
        }
        auto& iterator = static_cast<Iterator&>(*base);
        if (iterator.next >= iterator.items.size()) {
            return nullptr;
        }
        return to_handle(iterator.items[iterator.next++]);
    }

    [[nodiscard]] vhpiHandleT handle_by_index(const vhpiOneToManyT relation,
        const vhpiHandleT parent, const int32_t index)
    {
        // Package instances and tool arguments have no reference object.
        auto* const object = parent == nullptr
                && (relation == vhpiPackInsts || relation == vhpiArgvs)
            ? nullptr
            : object_of(parent);
        if (object == nullptr && parent != nullptr) {
            return nullptr;
        }
        if (object == nullptr && relation != vhpiPackInsts
            && relation != vhpiArgvs) {
            return nullptr;
        }
        const auto items = list(relation, object);
        if (index < 0 || static_cast<std::size_t>(index) >= items.size()) {
            return nullptr;
        }
        return to_handle(items[static_cast<std::size_t>(index)]);
    }

    // --- types ---------------------------------------------------------------------

    [[nodiscard]] static std::size_t width_of(const Object& type)
    {
        if (type.type_data == nullptr) {
            return 0U;
        }
        const auto& data = *type.type_data;
        switch (data.storage) {
        case Storage::array: {
            if (data.element == nullptr || data.dimensions.empty()) {
                return 0U;
            }
            std::size_t count = 1U;
            for (const auto& range : data.dimensions) {
                count *= static_cast<std::size_t>(range.length());
            }
            return count * width_of(*data.element);
        }
        case Storage::record: {
            std::size_t total = 0U;
            for (const auto& field : data.fields) {
                total += width_of(*field.type);
            }
            return total;
        }
        // INTEGER and TIME representations where no object lays the type
        // out (a generic or constant).
        case Storage::integer:
            return data.width != 0U ? data.width : 32U;
        case Storage::physical:
            return data.width != 0U ? data.width : 64U;
        default:
            return data.width;
        }
    }

    [[nodiscard]] static std::size_t enumeration_width(const std::size_t count)
    {
        std::size_t width = 1U;
        while ((std::size_t { 1 } << width) < count) {
            ++width;
        }
        return width;
    }

    [[nodiscard]] Object* make_type(const vhpiClassKindT kind,
        const std::string_view name, const std::string_view library,
        const std::string_view package)
    {
        auto* const object = make_object(kind, name, nullptr);
        object->type_data = std::make_unique<TypeData>();
        object->type_data->library = upper(library);
        object->type_data->package = upper(package);
        object->type_data->base = object;
        object->full_name = "@" + upper(library) + ":" + upper(package) + ":"
            + object->name;
        object->full_case_name = "@" + lower(library) + ":" + lower(package)
            + ":" + object->case_name;
        return object;
    }

    // The predefined types of STD.STANDARD and IEEE.STD_LOGIC_1164
    // (IEEE 1076-2019 16.3), which compiled HIR does not retain.
    [[nodiscard]] Object* standard_type(const std::string_view spelling)
    {
        const auto key = lower(spelling);
        if (const auto found = standard_types_.find(key);
            found != standard_types_.end()) {
            return found->second;
        }
        Object* type = nullptr;
        const auto enumeration = [&](const std::string_view name,
                                     std::vector<std::string> literals,
                                     const Storage storage,
                                     const std::string_view library,
                                     const std::string_view package) {
            auto* const object
                = make_type(vhpiEnumTypeDeclK, name, library, package);
            auto& data = *object->type_data;
            data.storage = storage;
            data.width = storage == Storage::logic9
                ? 1U
                : enumeration_width(literals.size());
            data.range = Range { 0,
                static_cast<std::int64_t>(literals.size()) - 1, false };
            data.literals = std::move(literals);
            return object;
        };
        const auto integer = [&](const std::string_view name,
                                 const std::int64_t left,
                                 const std::int64_t right) {
            auto* const object
                = make_type(vhpiIntTypeDeclK, name, "std", "standard");
            auto& data = *object->type_data;
            data.storage = Storage::integer;
            data.width = 32U;
            data.range = Range { left, right, false };
            return object;
        };
        const auto array = [&](const std::string_view name, Object* element,
                               const std::string_view library,
                               const std::string_view package,
                               Object* index) {
            auto* const object
                = make_type(vhpiArrayTypeDeclK, name, library, package);
            auto& data = *object->type_data;
            data.storage = Storage::array;
            data.element = element;
            data.unconstrained = true;
            data.dimension_count = 1U;
            data.index_types = { index };
            return object;
        };
        if (key == "integer") {
            type = integer("INTEGER", std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max());
        } else if (key == "natural" || key == "positive") {
            auto* const base = standard_type("integer");
            type = make_type(vhpiSubtypeDeclK, key, "std", "standard");
            auto& data = *type->type_data;
            data.storage = Storage::integer;
            data.width = 32U;
            data.range = Range { key == "natural" ? 0 : 1,
                std::numeric_limits<std::int32_t>::max(), false };
            data.base = base;
            data.parent = base;
        } else if (key == "bit") {
            type = enumeration("BIT", { "'0'", "'1'" }, Storage::enumeration,
                "std", "standard");
        } else if (key == "boolean") {
            type = enumeration("BOOLEAN", { "FALSE", "TRUE" },
                Storage::enumeration, "std", "standard");
        } else if (key == "severity_level") {
            type = enumeration("SEVERITY_LEVEL",
                { "NOTE", "WARNING", "ERROR", "FAILURE" },
                Storage::enumeration, "std", "standard");
        } else if (key == "character") {
            std::vector<std::string> literals;
            for (int code = 0; code < 256; ++code) {
                literals.push_back(std::string { '\'' }
                    + static_cast<char>(code) + '\'');
            }
            type = enumeration("CHARACTER", std::move(literals),
                Storage::enumeration, "std", "standard");
        } else if (key == "std_ulogic" || key == "std_logic") {
            type = enumeration("STD_ULOGIC",
                { "'U'", "'X'", "'0'", "'1'", "'Z'", "'W'", "'L'", "'H'",
                    "'-'" },
                Storage::logic9, "ieee", "std_logic_1164");
            if (key == "std_logic") {
                auto* const base = type;
                type = make_type(vhpiSubtypeDeclK, "std_logic", "ieee",
                    "std_logic_1164");
                *type->type_data = *base->type_data;
                type->type_data->base = base;
                type->type_data->parent = base;
                type->type_data->library = "IEEE";
                type->type_data->package = "STD_LOGIC_1164";
            }
        } else if (key == "real") {
            type = make_type(vhpiFloatTypeDeclK, "REAL", "std", "standard");
            type->type_data->storage = Storage::real;
            type->type_data->width = 64U;
        } else if (key == "time" || key == "delay_length") {
            type = make_type(vhpiPhysTypeDeclK, "TIME", "std", "standard");
            auto& data = *type->type_data;
            data.storage = Storage::physical;
            data.width = 64U;
            data.range = Range { std::numeric_limits<std::int64_t>::min(),
                std::numeric_limits<std::int64_t>::max(), false };
            data.units = { { "FS", 1 }, { "PS", 1000 }, { "NS", 1000000 },
                { "US", 1000000000LL }, { "MS", 1000000000000LL },
                { "SEC", 1000000000000000LL }, { "MIN", 60000000000000000LL },
                { "HR", 3600000000000000000LL } };
            if (key == "delay_length") {
                auto* const base = type;
                type = make_type(vhpiSubtypeDeclK, "delay_length", "std",
                    "standard");
                *type->type_data = *base->type_data;
                type->type_data->range->left = 0;
                type->type_data->base = base;
                type->type_data->parent = base;
            }
        } else if (key == "string") {
            type = array("STRING", standard_type("character"), "std",
                "standard", standard_type("positive"));
        } else if (key == "bit_vector") {
            type = array("BIT_VECTOR", standard_type("bit"), "std", "standard",
                standard_type("natural"));
        } else if (key == "boolean_vector") {
            type = array("BOOLEAN_VECTOR", standard_type("boolean"), "std",
                "standard", standard_type("natural"));
        } else if (key == "integer_vector") {
            type = array("INTEGER_VECTOR", standard_type("integer"), "std",
                "standard", standard_type("natural"));
        } else if (key == "real_vector") {
            type = array("REAL_VECTOR", standard_type("real"), "std",
                "standard", standard_type("natural"));
        } else if (key == "time_vector") {
            type = array("TIME_VECTOR", standard_type("time"), "std",
                "standard", standard_type("natural"));
        } else if (key == "std_ulogic_vector" || key == "std_logic_vector") {
            type = array(upper(key), standard_type(
                    key == "std_ulogic_vector" ? "std_ulogic" : "std_logic"),
                "ieee", "std_logic_1164", standard_type("natural"));
        }
        standard_types_.emplace(key, type);
        return type;
    }

    // Evaluate a range bound from the HIR.
    [[nodiscard]] std::optional<Range> range_of(
        const vhdl::RangeConstraint& constraint) const
    {
        if (!constraint.left || !constraint.right) {
            return std::nullopt;
        }
        return Range { *constraint.left, *constraint.right,
            constraint.descending };
    }

    // The object of a declared type or subtype.
    [[nodiscard]] Object* type_of_declaration(
        const vhdl::Declaration& declaration)
    {
        if (const auto found = declared_types_.find(declaration.id.value());
            found != declared_types_.end()) {
            return found->second;
        }
        Object* result = nullptr;
        if (declaration.form == vhdl::DeclarationForm::subtype
            && declaration.subtype) {
            auto* const parent = subtype_object(*declaration.subtype, nullptr);
            if (parent != nullptr) {
                result = make_type(vhpiSubtypeDeclK, declaration.name, "work",
                    "");
                *result->type_data = *parent->type_data;
                result->type_data->parent = parent;
                result->type_data->base = parent->type_data->base;
            }
        } else if (declaration.declared_type) {
            if (const auto* const definition
                = hir_.find_type(*declaration.declared_type)) {
                result = type_of_definition(*definition, declaration.name);
            }
        }
        if (result != nullptr && result->declaration == nullptr) {
            result->declaration = &declaration;
            name_type(*result, declaration);
        }
        declared_types_.emplace(declaration.id.value(), result);
        return result;
    }

    [[nodiscard]] const vhdl::Unit* owning_unit(
        const vhdl::Declaration& declaration)
    {
        if (declaration_units_.empty()) {
            for (const auto& unit : hir_.units()) {
                for (const auto id : unit.declarations) {
                    declaration_units_.emplace(id.value(), &unit);
                }
            }
        }
        const auto found = declaration_units_.find(declaration.id.value());
        return found == declaration_units_.end() ? nullptr : found->second;
    }

    // A type declared in a package is named from its library and package;
    // one declared in a design entity from that region (IEEE 1076-2019
    // 19.4.3 vhpiFullNameP).
    void name_type(Object& type, const vhdl::Declaration& declaration)
    {
        const auto* const unit = owning_unit(declaration);
        if (unit == nullptr) {
            return;
        }
        if (unit->kind == vhdl::UnitKind::package) {
            type.type_data->library = upper(unit->library);
            type.type_data->package = upper(unit->name);
            type.full_name = "@" + type.type_data->library + ":"
                + type.type_data->package + ":" + type.name;
            type.full_case_name = "@" + lower(unit->library) + ":"
                + lower(unit->name) + ":" + type.case_name;
            return;
        }
        Object* region = nullptr;
        if (auto* const top = root();
            top != nullptr && (top->unit == unit || top->entity == unit)) {
            region = top;
        }
        for (const auto& [path, instance] : instances_) {
            if (region == nullptr
                && (instance->unit == unit || instance->entity == unit)) {
                region = instance;
            }
        }
        if (region != nullptr) {
            type.full_name = region->full_name + ":" + type.name;
            type.full_case_name = region->full_case_name + ":" + type.case_name;
        }
    }

    [[nodiscard]] Object* type_of_definition(
        const vhdl::TypeDefinition& definition, const std::string_view name)
    {
        if (const auto found = defined_types_.find(definition.id.value());
            found != defined_types_.end()) {
            return found->second;
        }
        if (auto* const standard = standard_type(definition.name);
            standard != nullptr && definition.form != vhdl::TypeForm::record) {
            const auto* const declaration
                = hir_.find_declaration(definition.declaration);
            if (declaration == nullptr || is_standard_declaration(*declaration)) {
                defined_types_.emplace(definition.id.value(), standard);
                return standard;
            }
        }
        Object* result = nullptr;
        switch (definition.form) {
        case vhdl::TypeForm::enumeration: {
            result = make_type(vhpiEnumTypeDeclK, name, "work", "");
            auto& data = *result->type_data;
            for (const auto& literal : definition.enumeration_literals) {
                data.literals.push_back(upper(literal.spelling));
            }
            data.storage = Storage::enumeration;
            data.width = enumeration_width(data.literals.size());
            data.range = Range { 0,
                static_cast<std::int64_t>(data.literals.size()) - 1, false };
            break;
        }
        case vhdl::TypeForm::scalar: {
            result = make_type(vhpiIntTypeDeclK, name, "work", "");
            auto& data = *result->type_data;
            data.storage = Storage::integer;
            data.width = 32U;
            if (definition.scalar_range) {
                data.range = range_of(*definition.scalar_range);
            }
            break;
        }
        case vhdl::TypeForm::physical: {
            result = make_type(vhpiPhysTypeDeclK, name, "work", "");
            auto& data = *result->type_data;
            data.storage = Storage::physical;
            data.width = 64U;
            if (definition.scalar_range) {
                data.range = range_of(*definition.scalar_range);
            }
            for (const auto& unit : definition.physical_units) {
                data.units.push_back({ upper(unit.name),
                    unit_scale(unit, data.units) });
            }
            break;
        }
        case vhdl::TypeForm::array: {
            result = make_type(vhpiArrayTypeDeclK, name, "work", "");
            auto& data = *result->type_data;
            data.storage = Storage::array;
            data.dimension_count = definition.array_dimensions.size();
            bool constrained = true;
            for (const auto& dimension : definition.array_dimensions) {
                data.index_types.push_back(
                    standard_type(dimension.index_subtype.spelling));
                if (dimension.unconstrained || !dimension.constraint) {
                    constrained = false;
                    continue;
                }
                if (const auto range = range_of(*dimension.constraint)) {
                    data.dimensions.push_back(*range);
                } else {
                    constrained = false;
                }
            }
            data.unconstrained = !constrained;
            if (!constrained) {
                data.dimensions.clear();
            }
            if (definition.element_subtype) {
                data.element
                    = subtype_object(*definition.element_subtype, nullptr);
            }
            // A constrained array type declaration declares an anonymous
            // array type and a named subtype of it (IEEE 1076-2019 5.3.2.1).
            if (constrained) {
                auto* const base
                    = make_type(vhpiArrayTypeDeclK, name, "work", "");
                *base->type_data = data;
                base->type_data->base = base;
                base->type_data->unconstrained = true;
                base->type_data->dimensions.clear();
                result->kind = vhpiSubtypeDeclK;
                data.parent = base;
                data.base = base;
            }
            break;
        }
        case vhdl::TypeForm::record: {
            result = make_type(vhpiRecordTypeDeclK, name, "work", "");
            auto& data = *result->type_data;
            data.storage = Storage::record;
            for (const auto& element : definition.record_elements) {
                data.fields.push_back(Field { upper(element.name),
                    element.name, subtype_object(element.subtype, nullptr) });
            }
            break;
        }
        case vhdl::TypeForm::access:
            result = make_type(vhpiAccessTypeDeclK, name, "work", "");
            if (definition.designated_subtype) {
                result->type_data->designated = &*definition.designated_subtype;
            }
            break;
        case vhdl::TypeForm::file:
            result = make_type(vhpiFileTypeDeclK, name, "work", "");
            if (definition.element_subtype) {
                result->type_data->designated = &*definition.element_subtype;
            }
            break;
        default:
            break;
        }
        defined_types_.emplace(definition.id.value(), result);
        return result;
    }

    // A type or subtype a package declares, by its (possibly selected)
    // name, when the type mark carries no resolved reference.
    [[nodiscard]] Object* package_type(std::string_view spelling)
    {
        std::string_view package;
        if (const auto dot = spelling.rfind('.'); dot != std::string_view::npos) {
            package = spelling.substr(0U, dot);
            spelling.remove_prefix(dot + 1U);
            if (const auto inner = package.rfind('.');
                inner != std::string_view::npos) {
                package.remove_prefix(inner + 1U);
            }
        }
        for (const auto& unit : hir_.units()) {
            if (unit.kind != vhdl::UnitKind::package
                || (!package.empty() && !same_name(unit.name, package))) {
                continue;
            }
            for (const auto id : unit.declarations) {
                const auto* const declaration = hir_.find_declaration(id);
                if (declaration != nullptr
                    && (declaration->form == vhdl::DeclarationForm::type
                        || declaration->form == vhdl::DeclarationForm::subtype)
                    && same_name(declaration->name, spelling)) {
                    return type_of_declaration(*declaration);
                }
            }
        }
        return nullptr;
    }

    // A secondary unit's position in primary units: its declaration
    // `name = N unit` scales a unit declared before it (IEEE 1076-2019
    // 5.2.4.1).
    [[nodiscard]] std::int64_t unit_scale(const vhdl::PhysicalUnit& unit,
        const std::vector<PhysicalUnit>& earlier) const
    {
        // Analysis folds a static scale to primary units.
        if (unit.scale_factor || !unit.scale) {
            return unit.scale_factor.value_or(1);
        }
        const auto expression = std::ranges::find(
            hir_.expressions(), *unit.scale, &vhdl::Expression::id);
        if (expression == hir_.expressions().end()) {
            return unit.scale_factor.value_or(1);
        }
        // A physical literal `N unit` is the unit name with the abstract
        // literal N as its operand.
        constexpr std::string_view physical_prefix = "@vhdl-physical:";
        if (!expression->text.starts_with(physical_prefix)) {
            return 1;
        }
        const auto base_name
            = std::string_view { expression->text }.substr(physical_prefix.size());
        std::int64_t count { 1 };
        if (!expression->operands.empty()) {
            const auto operand = std::ranges::find(hir_.expressions(),
                expression->operands.front(), &vhdl::Expression::id);
            if (operand != hir_.expressions().end()) {
                std::string digits;
                for (const auto character : operand->text) {
                    if (character != '_') {
                        digits += character;
                    }
                }
                std::int64_t parsed_count { };
                const auto parsed = std::from_chars(
                    digits.data(), digits.data() + digits.size(), parsed_count);
                if (parsed.ec == std::errc { }) {
                    count = parsed_count;
                }
            }
        }
        const auto base = std::ranges::find_if(earlier, [&](const auto& candidate) {
            return same_name(candidate.name, base_name);
        });
        return base == earlier.end() ? count : count * base->scale;
    }

    [[nodiscard]] bool is_standard_declaration(
        const vhdl::Declaration& declaration) const
    {
        for (const auto& unit : hir_.units()) {
            if (unit.kind != vhdl::UnitKind::package) {
                continue;
            }
            if (std::ranges::find(unit.declarations, declaration.id)
                == unit.declarations.end()) {
                continue;
            }
            return (same_name(unit.library, "std")
                       && same_name(unit.name, "standard"))
                || (same_name(unit.library, "ieee")
                    && same_name(unit.name, "std_logic_1164"));
        }
        return false;
    }

    // The type object of a subtype indication: its type mark, or an
    // anonymous subtype when it adds a constraint.
    [[nodiscard]] Object* subtype_object(
        const vhdl::SubtypeIndication& subtype, Object* const)
    {
        Object* mark = nullptr;
        if (subtype.type_mark.target.valid()) {
            if (const auto* const definition
                = hir_.find_type(subtype.type_mark.target)) {
                const auto* const declaration
                    = hir_.find_declaration(definition->declaration);
                mark = declaration != nullptr
                    ? type_of_declaration(*declaration)
                    : type_of_definition(*definition,
                          subtype.type_mark.spelling);
            }
        }
        if (mark == nullptr) {
            // A predefined type may be bound without its declaration.
            auto spelling
                = vhdl::without_builtin_prefix(subtype.type_mark.spelling);
            if (const auto dot = spelling.rfind('.');
                dot != std::string_view::npos) {
                spelling.remove_prefix(dot + 1U);
            }
            mark = standard_type(spelling);
            if (mark == nullptr) {
                mark = package_type(subtype.type_mark.spelling);
            }
            if (mark == nullptr && subtype.builtin_type
                    != vhdl::BuiltinTypeIdentity::none) {
                mark = standard_type(vhdl::builtin_type_name(subtype.builtin_type));
            }
        }
        if (mark == nullptr || subtype.constraints.empty()
            || mark->type_data == nullptr) {
            return mark;
        }
        // A constraint that repeats the type mark's adds nothing.
        if (subtype.constraints.size() == 1U
            && mark->type_data->storage != Storage::array) {
            const auto range = range_of(subtype.constraints.front());
            if (!range || range == mark->type_data->range) {
                return mark;
            }
        }
        // An anonymous constrained subtype.
        auto* const result = make_object(vhpiSubtypeDeclK, "", nullptr);
        result->type_data = std::make_unique<TypeData>(*mark->type_data);
        auto& data = *result->type_data;
        data.parent = mark;
        data.base = mark->type_data->base;
        result->name = mark->name;
        result->case_name = mark->case_name;
        result->full_name = mark->full_name;
        result->full_case_name = mark->full_case_name;
        if (data.storage == Storage::array) {
            data.dimensions.clear();
            for (const auto& constraint : subtype.constraints) {
                if (const auto range = range_of(constraint)) {
                    data.dimensions.push_back(*range);
                }
            }
            data.unconstrained
                = data.dimensions.size() != std::max<std::size_t>(
                      data.dimension_count, 1U);
        } else if (const auto range = range_of(subtype.constraints.front())) {
            data.range = range;
        }
        return result;
    }

    [[nodiscard]] std::vector<Object*> type_list(
        const vhpiOneToManyT relation, Object* const object)
    {
        std::vector<Object*> result;
        if (object == nullptr) {
            return result;
        }
        auto* const type = is_type(*object) ? object : object->type;
        if (type == nullptr || type->type_data == nullptr) {
            return result;
        }
        auto& data = *type->type_data;
        // The same list for every iteration, so handles compare equal.
        const auto kind = relation == vhpiRecordElems ? ListKind::record_elems
            : relation == vhpiEnumLiterals            ? ListKind::enum_literals
            : relation == vhpiConstraints             ? ListKind::constraints
                                                      : ListKind::unit_decls;
        if (kind == ListKind::record_elems) {
            return data.storage == Storage::record
                ? element_declarations(*type)
                : result;
        }
        auto& list = type->lists[kind];
        if (!list.empty()) {
            return list;
        }
        switch (relation) {
        case vhpiEnumLiterals:
            for (std::size_t position = 0U; position < data.literals.size();
                 ++position) {
                auto* const literal = make_object(
                    vhpiEnumLiteralK, data.literals[position], type);
                literal->position = static_cast<std::int64_t>(position);
                literal->type = type;
                list.push_back(literal);
            }
            break;
        case vhpiConstraints:
            for (std::size_t dimension = 0U; dimension < data.dimensions.size();
                 ++dimension) {
                list.push_back(range_object(data.dimensions[dimension],
                    dimension < data.index_types.size()
                        ? data.index_types[dimension]
                        : nullptr));
            }
            if (data.dimensions.empty() && data.range
                && data.storage != Storage::array) {
                list.push_back(range_object(*data.range, type));
            }
            break;
        case vhpiUnitDecls:
            for (const auto& unit : data.units) {
                auto* const declaration
                    = make_object(vhpiUnitDeclK, unit.name, type);
                declaration->position = unit.scale;
                declaration->type = type->type_data->base != nullptr
                    ? type->type_data->base
                    : type;
                list.push_back(declaration);
            }
            break;
        default:
            break;
        }
        return list;
    }

    // A range object; its class follows the type of the range's values
    // (IEEE 1076-2019 19.4.3).
    [[nodiscard]] Object* range_object(
        const Range& range, const Object* const index_type = nullptr)
    {
        auto kind = vhpiIntRangeK;
        if (index_type != nullptr && index_type->type_data != nullptr) {
            switch (index_type->type_data->storage) {
            case Storage::enumeration:
            case Storage::logic9:
                kind = vhpiEnumRangeK;
                break;
            case Storage::physical:
                kind = vhpiPhysRangeK;
                break;
            case Storage::real:
                kind = vhpiFloatRangeK;
                break;
            default:
                break;
            }
        }
        auto* const object = make_object(kind, "", nullptr);
        object->type_data = std::make_unique<TypeData>();
        object->type_data->range = range;
        return object;
    }

    // --- properties ------------------------------------------------------------------

    [[nodiscard]] vhpiIntT get(const vhpiIntPropertyT property,
        const vhpiHandleT handle)
    {
        if (property == vhpiPhaseP) {
            return phase_;
        }
        auto* const base = handle_of(handle);
        if (base == nullptr) {
            error(vhpiError, "missing handle");
            return vhpiUndefined;
        }
        if (base->tag == HandleTag::callback) {
            const auto& callback = static_cast<Callback&>(*base);
            if (property == vhpiStateP) {
                return callback.state;
            }
            if (property == vhpiKindP) {
                return vhpiCallbackK;
            }
            if (property == vhpiReasonP) {
                return callback.data.reason;
            }
            error(vhpiError, "unsupported callback property");
            return vhpiUndefined;
        }
        if (base->tag == HandleTag::iterator) {
            if (property == vhpiKindP) {
                return vhpiIteratorK;
            }
            error(vhpiError, "unsupported iterator property");
            return vhpiUndefined;
        }
        if (base->tag != HandleTag::object) {
            error(vhpiError, "invalid handle");
            return vhpiUndefined;
        }
        auto& object = static_cast<Object&>(*base);
        auto* const type = is_type(object) ? &object : object.type;
        switch (property) {
        case vhpiKindP:
            return object.kind;
        case vhpiCapabilitiesP:
            // Only the capability sets that are fully provided (17.3).
            return vhpiProvidesHierarchy | vhpiProvidesForeignModel;
        case vhpiArgcP:
            return static_cast<vhpiIntT>(arguments_.size());
        case vhpiLineNoP:
            if (const auto location = source_location(object)) {
                return static_cast<vhpiIntT>(location->second);
            }
            return vhpiUndefined;
        case vhpiIsUpP:
        case vhpiLeftBoundP:
        case vhpiRightBoundP:
            // The range of a constraint, of a scalar type, or of the first
            // index of a constrained array (IEEE 1076-2019 19.4.3); the
            // other dimensions are the ranges of vhpiConstraints.
            if (const auto* const range = range_for(object)) {
                if (property == vhpiIsUpP) {
                    return range->descending ? 0 : 1;
                }
                return static_cast<vhpiIntT>(
                    property == vhpiLeftBoundP ? range->left : range->right);
            }
            break;
        case vhpiStaticnessP:
            if (const auto value = expression_staticness(object)) {
                return *value;
            }
            break;
        case vhpiNumLiteralsP:
            if (type != nullptr && type->type_data
                && (type->type_data->storage == Storage::enumeration
                    || type->type_data->storage == Storage::logic9)) {
                return static_cast<vhpiIntT>(type->type_data->literals.size());
            }
            break;
        case vhpiSigKindP: {
            // A guarded signal is a bus or a register (IEEE 1076-2019 6.4.2.3).
            const auto* const declaration = object.aliased != nullptr
                ? object.aliased->declaration
                : object.declaration;
            if (declaration != nullptr
                && (declaration->form == vhdl::DeclarationForm::signal
                    || declaration->form == vhdl::DeclarationForm::port)) {
                return declaration->guarded_kind == 1U ? vhpiBus
                    : declaration->guarded_kind == 2U  ? vhpiRegister
                                                       : vhpiNormal;
            }
            break;
        }
        case vhpiAccessP:
            // An ordinary process driver can be read and navigated, not
            // written: only vhpi_create'd drivers take vhpiWrite (IEEE
            // 1076-2019 22.6).
            if (object.kind == vhpiDriverK) {
                return vhpiRead | vhpiConnectivity;
            }
            break;
        case vhpiIsBasicP:
            if (is_signal_object(object)) {
                const auto storage = type->type_data->storage;
                return storage != Storage::array && storage != Storage::record
                    ? 1
                    : 0;
            }
            break;
        case vhpiIsForcedP:
            if (object.signal && object.kind != vhpiDriverK) {
                return simulation_.signal_is_forced(*object.signal) ? 1 : 0;
            }
            break;
        case vhpiSizeP:
            if (type != nullptr && type->type_data) {
                return static_cast<vhpiIntT>(element_count(*type));
            }
            if (is_range(object) && object.type_data->range) {
                return static_cast<vhpiIntT>(object.type_data->range->length());
            }
            break;
        case vhpiIsScalarP:
            if (type != nullptr && type->type_data) {
                const auto storage = type->type_data->storage;
                return storage != Storage::array && storage != Storage::record
                        && storage != Storage::other
                    ? 1
                    : 0;
            }
            break;
        case vhpiIsCompositeP:
            if (type != nullptr && type->type_data) {
                const auto storage = type->type_data->storage;
                return storage == Storage::array || storage == Storage::record
                    ? 1
                    : 0;
            }
            break;
        case vhpiIsUnconstrainedP:
            if (type != nullptr && type->type_data) {
                return type->type_data->unconstrained ? 1 : 0;
            }
            break;
        case vhpiNumDimensionsP:
            if (type != nullptr && type->type_data) {
                return static_cast<vhpiIntT>(std::max(
                    type->type_data->dimension_count,
                    type->type_data->dimensions.size()));
            }
            break;
        case vhpiNumFieldsP:
            if (type != nullptr && type->type_data) {
                const auto* const record = type->type_data->base;
                return static_cast<vhpiIntT>(record != nullptr
                        ? record->type_data->fields.size()
                        : type->type_data->fields.size());
            }
            break;
        case vhpiPositionP:
            return static_cast<vhpiIntT>(object.position);
        case vhpiBaseIndexP:
            // The element's offset from the left of its prefix, the
            // indexes taken in row-major order (IEEE 1076-2019 19.5.3).
            if (object.kind == vhpiIndexedNameK) {
                return static_cast<vhpiIntT>(object.position);
            }
            break;
        case vhpiModeP:
            if (object.declaration != nullptr) {
                switch (object.declaration->direction) {
                case vhdl::Direction::input:
                    return vhpiInMode;
                case vhdl::Direction::output:
                    return vhpiOutMode;
                case vhdl::Direction::inout:
                    return vhpiInoutMode;
                case vhdl::Direction::buffer:
                    return vhpiBufferMode;
                default:
                    break;
                }
            }
            break;
        case vhpiIsLocalP:
            return 0;
        case vhpiIsSeqStmtP:
            if (object.is_stmt) {
                return object.sequential ? 1 : 0;
            }
            if (is_region(object) && object.kind != vhpiRootInstK
                && object.kind != vhpiPackInstK) {
                return 0;
            }
            break;
        case vhpiIsPostponedP:
            // A process or a concurrent statement (IEEE 1076-2019 11.3).
            if (object.process != nullptr) {
                return object.process->postponed ? 1 : 0;
            }
            if (object.is_stmt && !object.sequential
                && object.statement != nullptr) {
                return object.statement->postponed ? 1 : 0;
            }
            break;
        case vhpiIsPassiveP:
            if (object.is_stmt && !object.sequential) {
                return passive(object);
            }
            break;
        case vhpiIsForeignP:
            // A subprogram body of the design is not foreign: the FOREIGN
            // attribute is not retained, and foreign subprograms register
            // through vhpi_register_foreignf (IEEE 1076-2019 20.4).
            if (object.process != nullptr || object.kind == vhpiDriverK
                || object.equivalent || object.kind == vhpiSubpBodyK) {
                return 0;
            }
            break;
        case vhpiIsPureP:
            if (object.kind == vhpiFuncDeclK && object.declaration != nullptr
                && object.declaration->callable) {
                return object.declaration->callable->pure ? 1 : 0;
            }
            break;
        case vhpiIsBuiltInP:
            // A predefined operator, not a function a package or the design
            // declares (IEEE 1076-2019 19.10, Figure 35).
            if (object.kind == vhpiFuncCallK && object.expression != nullptr) {
                const auto kind = object.expression->kind;
                return (kind == vhdl::ExpressionKind::unary
                           || kind == vhdl::ExpressionKind::binary)
                        && callee_of(*object.expression) == nullptr
                    ? 1
                    : 0;
            }
            if (object.kind == vhpiSeqProcCallStmtK
                || object.kind == vhpiConcProcCallStmtK) {
                return 0;
            }
            break;
        case vhpiIsTransportP:
            if (is_signal_assignment(object)) {
                const auto& assignment = assignment_of(*object.statement);
                return assignment.delay_mechanism
                        && *assignment.delay_mechanism
                            == vhdl::DelayMechanism::transport
                    ? 1
                    : 0;
            }
            break;
        case vhpiIsUnaffectedP:
            if (object.kind == vhpiWaveformElemK && object.statement != nullptr) {
                return object.statement->unaffected ? 1 : 0;
            }
            break;
        case vhpiIsGuardedP:
            if (is_signal_assignment(object)) {
                const auto& assignment = assignment_of(*object.statement);
                return object.statement->guarded_assignment
                        || assignment.guarded_assignment
                    ? 1
                    : 0;
            }
            // A guarded block declares the implicit signal GUARD (IEEE
            // 1076-2019 11.2).
            if (object.kind == vhpiBlockStmtK) {
                return simulation_.find_signal(object.path + ".guard") ? 1 : 0;
            }
            break;
        case vhpiGenerateIndexP:
            if (object.generate_index) {
                return static_cast<vhpiIntT>(*object.generate_index);
            }
            break;
        case vhpiIsOperatorP:
            if (object.kind == vhpiFuncCallK && object.expression != nullptr) {
                return object.expression->kind != vhdl::ExpressionKind::call
                        || object.expression->operator_symbol_call
                    ? 1
                    : 0;
            }
            break;
        case vhpiNumParamsP:
            if (object.kind == vhpiFuncDeclK || object.kind == vhpiProcDeclK) {
                return static_cast<vhpiIntT>(parameter_declarations(object).size());
            }
            if (object.kind == vhpiFuncCallK || object.kind == vhpiSeqProcCallStmtK
                || object.kind == vhpiConcProcCallStmtK) {
                // A call of a foreign subprogram lists its formals.
                const auto count = object.expression == nullptr
                        && object.statement == nullptr
                    ? object.lists[ListKind::param_decls].size()
                    : parameter_associations(object).size();
                return static_cast<vhpiIntT>(count);
            }
            break;
        case vhpiIsNamedP:
            if (object.kind == vhpiAssocElemK || object.kind == vhpiElemAssocK) {
                return object.named ? 1 : 0;
            }
            break;
        case vhpiIsOpenP:
            if (object.kind == vhpiAssocElemK) {
                return object.port_association != nullptr
                        && port_connection(object).open
                    ? 1
                    : 0;
            }
            break;
        case vhpiNumMembersP:
            if (is_collection(object)) {
                return static_cast<vhpiIntT>(
                    object.lists[ListKind::members].size());
            }
            break;
        case vhpiBeginLineNoP:
        case vhpiEndLineNoP:
            // A process, and the process equivalent to a concurrent
            // statement (Figure 38), and a subprogram body (Figure 34).
            if (object.kind == vhpiProcessStmtK || object.kind == vhpiSubpBodyK) {
                if (const auto line
                    = span_line(object.source, property == vhpiEndLineNoP)) {
                    return *line;
                }
                return vhpiUndefined;
            }
            break;
        case vhpiIsSharedP:
            if (object.kind == vhpiVarDeclK && object.declaration != nullptr) {
                return object.declaration->shared ? 1 : 0;
            }
            break;
        case vhpiIsProtectedTypeP:
            if (object.kind == vhpiVarDeclK) {
                return protected_variable(object) ? 1 : 0;
            }
            break;
        case vhpiIntValP:
            if (object.kind == vhpiIntLiteralK) {
                vhpiValueT value { };
                value.format = vhpiIntVal;
                return read_value(object, value) == 0 ? value.value.intg
                                                       : vhpiUndefined;
            }
            break;
        case vhpiIsNullP:
            if (is_range(object) && object.type_data
                && object.type_data->range) {
                return object.type_data->range->length() == 0 ? 1 : 0;
            }
            break;
        case vhpiIsDiscreteP:
            if (object.kind == vhpiIntRangeK || object.kind == vhpiEnumRangeK) {
                return 1;
            }
            if (type != nullptr && type->type_data) {
                const auto storage = type->type_data->storage;
                return storage == Storage::integer
                        || storage == Storage::enumeration
                        || storage == Storage::logic9
                    ? 1
                    : 0;
            }
            break;
        default:
            break;
        }
        error(vhpiError, "unsupported property " + std::to_string(property)
                + " of " + object.full_name);
        return vhpiUndefined;
    }

    [[nodiscard]] static bool is_range(const Object& object) noexcept
    {
        return object.kind == vhpiIntRangeK || object.kind == vhpiEnumRangeK
            || object.kind == vhpiPhysRangeK || object.kind == vhpiFloatRangeK;
    }

    // The range a bound property reads: a range object's, a scalar type's,
    // or the first index range of an array object's type.
    [[nodiscard]] static const Range* range_for(const Object& object) noexcept
    {
        if (object.type_data != nullptr && object.type_data->range) {
            return &*object.type_data->range;
        }
        const auto* const type = object.type_data != nullptr
            ? &object
            : object.type;
        if (type != nullptr && type->type_data != nullptr
            && !type->type_data->dimensions.empty()) {
            return &type->type_data->dimensions.front();
        }
        return nullptr;
    }

    // The staticness of an expression or range (IEEE 1076-2019 9.4; the
    // property belongs to the expr and range classes, 19.13, 19.16): a
    // range or literal of the elaborated design is locally static, and a
    // name has the staticness of the object it denotes as a primary: a
    // constant of a locally static value is locally static, another
    // constant or a generic globally static, a signal or variable dynamic.
    // A declaration is not an expression and has no staticness.
    [[nodiscard]] static std::optional<vhpiIntT> staticness(
        const Object& object)
    {
        if (is_range(object) || object.kind == vhpiEnumLiteralK) {
            return vhpiLocallyStatic;
        }
        switch (object.kind) {
        case vhpiIndexedNameK:
        case vhpiSelectedNameK:
        case vhpiSliceNameK:
            return object.parent != nullptr ? primary_staticness(*object.parent)
                                            : std::nullopt;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::optional<vhpiIntT> primary_staticness(
        const Object& object)
    {
        switch (object.kind) {
        case vhpiConstDeclK:
            return object.constant ? vhpiLocallyStatic : vhpiGloballyStatic;
        case vhpiGenericDeclK:
            return vhpiGloballyStatic;
        case vhpiSigDeclK:
        case vhpiPortDeclK:
        case vhpiVarDeclK:
            return vhpiDynamic;
        case vhpiIndexedNameK:
        case vhpiSelectedNameK:
        case vhpiSliceNameK:
            return object.parent != nullptr ? primary_staticness(*object.parent)
                                            : std::nullopt;
        case vhpiAliasDeclK:
            return object.aliased != nullptr
                ? primary_staticness(*object.aliased)
                : std::nullopt;
        default:
            return std::nullopt;
        }
    }

    // The number of scalar subelements: one for a scalar, the product of
    // the index ranges for an array (IEEE 1076-2019 19.4.3 vhpiSizeP).
    [[nodiscard]] static std::int64_t element_count(const Object& type)
    {
        const auto& data = *type.type_data;
        switch (data.storage) {
        case Storage::array: {
            std::int64_t count = data.dimensions.empty() ? 0 : 1;
            for (const auto& range : data.dimensions) {
                count *= range.length();
            }
            if (data.element != nullptr && data.element->type_data) {
                count *= element_count(*data.element);
            }
            return count;
        }
        case Storage::record: {
            std::int64_t count = 0;
            for (const auto& field : data.fields) {
                if (field.type != nullptr && field.type->type_data) {
                    count += element_count(*field.type);
                }
            }
            return count;
        }
        case Storage::other:
            return data.range ? data.range->length() : 1;
        default:
            return 1;
        }
    }

    [[nodiscard]] const vhpiCharT* get_str(const vhpiStrPropertyT property,
        const vhpiHandleT handle)
    {
        auto* const base = handle_of(handle);
        if (base == nullptr || base->tag != HandleTag::object) {
            error(vhpiError, "missing object handle");
            return nullptr;
        }
        auto& object = static_cast<Object&>(*base);
        const std::string* text = nullptr;
        std::string scratch;
        switch (property) {
        case vhpiNameP:
            text = &object.name;
            break;
        case vhpiCaseNameP:
            text = &object.case_name;
            break;
        case vhpiFullNameP:
            text = &object.full_name;
            break;
        case vhpiFullCaseNameP:
            text = &object.full_case_name;
            break;
        case vhpiLabelNameP:
            if (object.kind == vhpiCompInstStmtK
                || object.kind == vhpiBlockStmtK
                || object.kind == vhpiForGenerateK
                || object.kind == vhpiIfGenerateK) {
                if (object.generate != nullptr) {
                    scratch = object.generate->label;
                } else {
                    text = &object.case_name;
                }
            } else if (object.is_stmt) {
                // The explicit label; an unlabeled statement has none.
                text = &object.label_name;
            }
            break;
        case vhpiLoopLabelNameP:
            if (object.kind == vhpiExitStmtK || object.kind == vhpiNextStmtK) {
                scratch = loop_label_of(object);
                break;
            }
            error(vhpiError, "vhpiLoopLabelNameP needs an exit or next statement");
            return nullptr;
        case vhpiKindStrP:
            scratch = kind_name(object.kind);
            break;
        case vhpiFileNameP:
            if (const auto location = source_location(object)) {
                scratch = location->first;
                break;
            }
            return nullptr;
        case vhpiToolVersionP:
            scratch = std::string { fsim::version };
            break;
        case vhpiStrValP:
            scratch = object.case_name;
            break;
        case vhpiLibLogicalNameP:
            // A design unit's library (IEEE 1076-2019 19.4.5).
            if (object.unit != nullptr && is_design_unit(object)) {
                scratch = upper(object.unit->library);
            } else {
                scratch = object.type_data ? object.type_data->library : "WORK";
            }
            break;
        case vhpiUnitNameP:
            // `library.unit[:body]` (IEEE 1076-2019 19.4.5).
            if (object.unit == nullptr || !is_design_unit(object)) {
                error(vhpiError, "vhpiUnitNameP needs a design unit");
                return nullptr;
            }
            scratch = unit_name(*object.unit);
            break;
        case vhpiDefNameP:
            // `@unit{.scope}[.name]` (IEEE 1076-2019 19.4.6).
            if (const auto name = def_name(object)) {
                scratch = *name;
                break;
            }
            error(vhpiError, "no definition name for " + object.full_name);
            return nullptr;
        default:
            error(vhpiError, "unsupported string property "
                    + std::to_string(property));
            return nullptr;
        }
        if (text == nullptr) {
            strings_.push_back(std::move(scratch));
            text = &strings_.back();
        }
        return reinterpret_cast<const vhpiCharT*>(text->c_str());
    }

    [[nodiscard]] static bool is_design_unit(const Object& object) noexcept
    {
        return object.kind == vhpiArchBodyK || object.kind == vhpiEntityDeclK
            || object.kind == vhpiPackDeclK;
    }

    // The UnitName of a design unit: `library.unit`, and for an
    // architecture body `library.entity:architecture` (IEEE 1076-2019
    // 19.4.5).
    [[nodiscard]] static std::string unit_name(const vhdl::Unit& unit)
    {
        const auto library = upper(unit.library);
        if (unit.kind == vhdl::UnitKind::architecture) {
            return library + "." + unit.primary_name + ":" + unit.name;
        }
        return library + "." + unit.name;
    }

    // The DefName of a region, declaration or predefined type: the unit
    // name of the declaring design unit, the labels of the enclosing
    // generate and block statements, then the object's own name (IEEE
    // 1076-2019 19.4.6).
    [[nodiscard]] static std::optional<std::string> def_name(
        const Object& object)
    {
        const auto label = [](const Object& scope) {
            return scope.generate != nullptr ? scope.generate->label
                                             : scope.case_name;
        };
        if (is_type(object)) {
            const auto& data = *object.type_data;
            if (data.package.empty()) {
                return std::nullopt;
            }
            return "@" + data.library + "." + data.package + "." + object.name;
        }
        if (object.kind == vhpiRootInstK) {
            return object.entity != nullptr
                ? std::optional<std::string> { "@" + unit_name(*object.entity) }
                : std::nullopt;
        }
        if (object.kind == vhpiPackInstK) {
            return object.unit != nullptr
                ? std::optional<std::string> { "@" + unit_name(*object.unit) }
                : std::nullopt;
        }
        // The labels from the enclosing instance down to the object.
        std::vector<std::string> scopes;
        const Object* region = nullptr;
        switch (object.kind) {
        case vhpiCompInstStmtK:
        case vhpiBlockStmtK:
        case vhpiForGenerateK:
        case vhpiIfGenerateK:
            scopes.push_back(label(object));
            region = object.parent;
            break;
        case vhpiSigDeclK:
        case vhpiPortDeclK:
        case vhpiGenericDeclK:
        case vhpiConstDeclK:
        case vhpiVarDeclK:
        case vhpiAliasDeclK:
            scopes.push_back(object.name);
            region = object.parent;
            break;
        default:
            return std::nullopt;
        }
        while (region != nullptr && region->generate != nullptr) {
            scopes.push_back(label(*region));
            region = region->parent;
        }
        if (region == nullptr || region->unit == nullptr) {
            return std::nullopt;
        }
        const vhdl::Unit* unit = region->unit;
        // The generics and ports are the entity's declarations.
        if (object.declaration != nullptr && region->entity != nullptr
            && std::ranges::any_of(region->entity->declarations,
                [&](const auto id) {
                    return id.value() == object.declaration->id.value();
                })) {
            unit = region->entity;
        }
        auto result = "@" + unit_name(*unit);
        for (auto name = scopes.rbegin(); name != scopes.rend(); ++name) {
            result += "." + *name;
        }
        return result;
    }

    [[nodiscard]] static std::string kind_name(const vhpiClassKindT kind)
    {
        switch (kind) {
        case vhpiAccessTypeDeclK:
            return "vhpiAccessTypeDeclK";
        case vhpiAggregateK:
            return "vhpiAggregateK";
        case vhpiAliasDeclK:
            return "vhpiAliasDeclK";
        case vhpiAllK:
            return "vhpiAllK";
        case vhpiAllocatorK:
            return "vhpiAllocatorK";
        case vhpiAnyCollectionK:
            return "vhpiAnyCollectionK";
        case vhpiArchBodyK:
            return "vhpiArchBodyK";
        case vhpiArgvK:
            return "vhpiArgvK";
        case vhpiArrayTypeDeclK:
            return "vhpiArrayTypeDeclK";
        case vhpiAssocElemK:
            return "vhpiAssocElemK";
        case vhpiAttrDeclK:
            return "vhpiAttrDeclK";
        case vhpiAttrSpecK:
            return "vhpiAttrSpecK";
        case vhpiBitStringLiteralK:
            return "vhpiBitStringLiteralK";
        case vhpiBlockConfigK:
            return "vhpiBlockConfigK";
        case vhpiBlockStmtK:
            return "vhpiBlockStmtK";
        case vhpiBranchK:
            return "vhpiBranchK";
        case vhpiCallbackK:
            return "vhpiCallbackK";
        case vhpiCaseStmtK:
            return "vhpiCaseStmtK";
        case vhpiCharLiteralK:
            return "vhpiCharLiteralK";
        case vhpiCompConfigK:
            return "vhpiCompConfigK";
        case vhpiCompDeclK:
            return "vhpiCompDeclK";
        case vhpiCompInstStmtK:
            return "vhpiCompInstStmtK";
        case vhpiCondSigAssignStmtK:
            return "vhpiCondSigAssignStmtK";
        case vhpiCondWaveformK:
            return "vhpiCondWaveformK";
        case vhpiConfigDeclK:
            return "vhpiConfigDeclK";
        case vhpiConstDeclK:
            return "vhpiConstDeclK";
        case vhpiConstParamDeclK:
            return "vhpiConstParamDeclK";
        case vhpiConvFuncK:
            return "vhpiConvFuncK";
        case vhpiDerefObjK:
            return "vhpiDerefObjK";
        case vhpiDisconnectSpecK:
            return "vhpiDisconnectSpecK";
        case vhpiDriverK:
            return "vhpiDriverK";
        case vhpiDriverCollectionK:
            return "vhpiDriverCollectionK";
        case vhpiElemAssocK:
            return "vhpiElemAssocK";
        case vhpiElemDeclK:
            return "vhpiElemDeclK";
        case vhpiEntityClassEntryK:
            return "vhpiEntityClassEntryK";
        case vhpiEntityDeclK:
            return "vhpiEntityDeclK";
        case vhpiEnumLiteralK:
            return "vhpiEnumLiteralK";
        case vhpiEnumRangeK:
            return "vhpiEnumRangeK";
        case vhpiEnumTypeDeclK:
            return "vhpiEnumTypeDeclK";
        case vhpiExitStmtK:
            return "vhpiExitStmtK";
        case vhpiFileDeclK:
            return "vhpiFileDeclK";
        case vhpiFileParamDeclK:
            return "vhpiFileParamDeclK";
        case vhpiFileTypeDeclK:
            return "vhpiFileTypeDeclK";
        case vhpiFloatRangeK:
            return "vhpiFloatRangeK";
        case vhpiFloatTypeDeclK:
            return "vhpiFloatTypeDeclK";
        case vhpiForGenerateK:
            return "vhpiForGenerateK";
        case vhpiForLoopK:
            return "vhpiForLoopK";
        case vhpiForeignfK:
            return "vhpiForeignfK";
        case vhpiFuncCallK:
            return "vhpiFuncCallK";
        case vhpiFuncDeclK:
            return "vhpiFuncDeclK";
        case vhpiGenericDeclK:
            return "vhpiGenericDeclK";
        case vhpiGroupDeclK:
            return "vhpiGroupDeclK";
        case vhpiGroupTempDeclK:
            return "vhpiGroupTempDeclK";
        case vhpiIfGenerateK:
            return "vhpiIfGenerateK";
        case vhpiIfStmtK:
            return "vhpiIfStmtK";
        case vhpiInPortK:
            return "vhpiInPortK";
        case vhpiIndexedNameK:
            return "vhpiIndexedNameK";
        case vhpiIntLiteralK:
            return "vhpiIntLiteralK";
        case vhpiIntRangeK:
            return "vhpiIntRangeK";
        case vhpiIntTypeDeclK:
            return "vhpiIntTypeDeclK";
        case vhpiIteratorK:
            return "vhpiIteratorK";
        case vhpiLibraryDeclK:
            return "vhpiLibraryDeclK";
        case vhpiNextStmtK:
            return "vhpiNextStmtK";
        case vhpiNullLiteralK:
            return "vhpiNullLiteralK";
        case vhpiNullStmtK:
            return "vhpiNullStmtK";
        case vhpiOthersK:
            return "vhpiOthersK";
        case vhpiOutPortK:
            return "vhpiOutPortK";
        case vhpiPackBodyK:
            return "vhpiPackBodyK";
        case vhpiPackDeclK:
            return "vhpiPackDeclK";
        case vhpiPackInstK:
            return "vhpiPackInstK";
        case vhpiParamAttrNameK:
            return "vhpiParamAttrNameK";
        case vhpiPhysLiteralK:
            return "vhpiPhysLiteralK";
        case vhpiPhysRangeK:
            return "vhpiPhysRangeK";
        case vhpiPhysTypeDeclK:
            return "vhpiPhysTypeDeclK";
        case vhpiPortDeclK:
            return "vhpiPortDeclK";
        case vhpiProcDeclK:
            return "vhpiProcDeclK";
        case vhpiProcessStmtK:
            return "vhpiProcessStmtK";
        case vhpiProtectedTypeBodyK:
            return "vhpiProtectedTypeBodyK";
        case vhpiProtectedTypeDeclK:
            return "vhpiProtectedTypeDeclK";
        case vhpiRealLiteralK:
            return "vhpiRealLiteralK";
        case vhpiRecordTypeDeclK:
            return "vhpiRecordTypeDeclK";
        case vhpiReportStmtK:
            return "vhpiReportStmtK";
        case vhpiReturnStmtK:
            return "vhpiReturnStmtK";
        case vhpiRootInstK:
            return "vhpiRootInstK";
        case vhpiSelectSigAssignStmtK:
            return "vhpiSelectSigAssignStmtK";
        case vhpiSelectWaveformK:
            return "vhpiSelectWaveformK";
        case vhpiSelectedNameK:
            return "vhpiSelectedNameK";
        case vhpiSigDeclK:
            return "vhpiSigDeclK";
        case vhpiSigParamDeclK:
            return "vhpiSigParamDeclK";
        case vhpiSimpAttrNameK:
            return "vhpiSimpAttrNameK";
        case vhpiSimpleSigAssignStmtK:
            return "vhpiSimpleSigAssignStmtK";
        case vhpiSliceNameK:
            return "vhpiSliceNameK";
        case vhpiStringLiteralK:
            return "vhpiStringLiteralK";
        case vhpiSubpBodyK:
            return "vhpiSubpBodyK";
        case vhpiSubtypeDeclK:
            return "vhpiSubtypeDeclK";
        case vhpiToolK:
            return "vhpiToolK";
        case vhpiTransactionK:
            return "vhpiTransactionK";
        case vhpiTypeConvK:
            return "vhpiTypeConvK";
        case vhpiUnitDeclK:
            return "vhpiUnitDeclK";
        case vhpiUserAttrNameK:
            return "vhpiUserAttrNameK";
        case vhpiVarAssignStmtK:
            return "vhpiVarAssignStmtK";
        case vhpiVarDeclK:
            return "vhpiVarDeclK";
        case vhpiVarParamDeclK:
            return "vhpiVarParamDeclK";
        case vhpiWaitStmtK:
            return "vhpiWaitStmtK";
        case vhpiWaveformElemK:
            return "vhpiWaveformElemK";
        case vhpiWhileLoopK:
            return "vhpiWhileLoopK";
        case vhpiQualifiedExprK:
            return "vhpiQualifiedExprK";
        case vhpiUseClauseK:
            return "vhpiUseClauseK";
        case vhpiConcAssertStmtK:
            return "vhpiConcAssertStmtK";
        case vhpiConcProcCallStmtK:
            return "vhpiConcProcCallStmtK";
        case vhpiForeverLoopK:
            return "vhpiForeverLoopK";
        case vhpiSeqAssertStmtK:
            return "vhpiSeqAssertStmtK";
        case vhpiSeqProcCallStmtK:
            return "vhpiSeqProcCallStmtK";
        case vhpiSeqSigAssignStmtK:
            return "vhpiSeqSigAssignStmtK";
        case vhpiProtectedTypeInstK:
            return "vhpiProtectedTypeInstK";
        default:
            return "vhpiUndefined";
        }
    }

    [[nodiscard]] vhpiRealT get_real(const vhpiRealPropertyT property,
        const vhpiHandleT handle)
    {
        auto* const object = object_of(handle);
        if (object == nullptr) {
            return 0.0;
        }
        switch (property) {
        case vhpiFloatLeftBoundP:
        case vhpiFloatRightBoundP: {
            // The bounds of a real type or subtype, or of a real range
            // (IEEE 1076-2019 19.4.3); the predefined REAL spans the
            // representable doubles.
            const auto* const type = is_type(*object) ? object : object->type;
            if (type == nullptr || type->type_data == nullptr
                || type->type_data->storage != Storage::real) {
                break;
            }
            const auto& data = *type->type_data;
            const auto largest = std::numeric_limits<double>::max();
            if (data.range) {
                return static_cast<double>(
                    property == vhpiFloatLeftBoundP ? data.range->left
                                                    : data.range->right);
            }
            return property == vhpiFloatLeftBoundP ? -largest : largest;
        }
        case vhpiRealValP: {
            // The value of a real object (IEEE 1076-2019 19.4.3).
            vhpiValueT value { };
            value.format = vhpiRealVal;
            if (read_value(*object, value) == 0) {
                return value.value.real;
            }
            return 0.0;
        }
        default:
            break;
        }
        error(vhpiError, "unsupported real property "
                + std::to_string(property) + " of " + object->full_name);
        return 0.0;
    }

    [[nodiscard]] vhpiPhysT get_phys(const vhpiPhysPropertyT property,
        const vhpiHandleT handle)
    {
        const auto physical = [](const std::int64_t value) {
            return vhpiPhysT { static_cast<int32_t>(value >> 32U),
                static_cast<uint32_t>(value) };
        };
        if (property == vhpiResolutionLimitP) {
            return physical(static_cast<std::int64_t>(femtoseconds_per_tick_));
        }
        auto* const object = object_of(handle);
        if (object == nullptr) {
            return vhpiPhysT { 0, 0 };
        }
        auto* const type = is_type(*object) ? object : object->type;
        switch (property) {
        case vhpiPhysValP: {
            vhpiValueT value { };
            value.format = vhpiPhysVal;
            if (read_value(*object, value) == 0) {
                return value.value.phys;
            }
            return vhpiPhysT { 0, 0 };
        }
        case vhpiPhysPositionP:
            return physical(object->position);
        case vhpiPhysLeftBoundP:
        case vhpiPhysRightBoundP:
            if (type != nullptr && type->type_data && type->type_data->range) {
                const auto& range = *type->type_data->range;
                return physical(
                    property == vhpiPhysLeftBoundP ? range.left : range.right);
            }
            break;
        default:
            break;
        }
        error(vhpiError, "unsupported physical property "
                + std::to_string(property));
        return vhpiPhysT { 0, 0 };
    }

    // --- values ----------------------------------------------------------------------

    // The debug local that holds a variable declared in a process (IEEE
    // 1076-2019 22.5.2): the design process that owns it and the index of
    // its debug local, found by name. The names of the locals are prefixed
    // with the scopes of their statements; a variable of the process itself
    // has none or the process's.
    [[nodiscard]] std::optional<std::pair<runtime::simir::ProcessId, std::size_t>>
    variable_local(const Object& object) const
    {
        if (object.local_resolved) {
            return object.local;
        }
        object.local_resolved = true;
        if (object.kind != vhpiVarDeclK || object.declaration == nullptr
            || object.declaration->shared || object.parent == nullptr
            || object.parent->kind != vhpiProcessStmtK) {
            return std::nullopt;
        }
        const auto design = design_process_of(*object.parent);
        if (!design) {
            return std::nullopt;
        }
        try {
            const auto& locals = simulation_.process_program(*design).debug_locals;
            const auto name = lower(object.declaration->name);
            std::optional<std::size_t> suffix;
            for (std::size_t index = 0U; index < locals.size(); ++index) {
                const auto local_name = lower(locals[index].name);
                if (local_name == name) {
                    object.local = std::pair { *design, index };
                    return object.local;
                }
                if (!suffix && local_name.size() > name.size()
                    && local_name.ends_with(name)
                    && local_name[local_name.size() - name.size() - 1U] == '.') {
                    suffix = index;
                }
            }
            if (suffix) {
                object.local = std::pair { *design, *suffix };
            }
        } catch (const std::exception&) {
            object.local.reset();
        }
        return object.local;
    }

    [[nodiscard]] std::optional<PackedLogic4> read_local(
        const std::pair<runtime::simir::ProcessId, std::size_t>& local) const
    {
        try {
            return simulation_.read_process_local(local.first, local.second);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    // The bits of an object's value: the element of its signal, or its
    // constant value.
    [[nodiscard]] std::optional<PackedLogic4> element_bits(const Object& object)
    {
        std::optional<PackedLogic4> whole;
        if (object.kind == vhpiDriverK) {
            whole = driver_bits(object);
        } else if (object.signal) {
            whole = simulation_.read_signal_snapshot(*object.signal);
        } else if (object.constant) {
            whole = *object.constant;
        } else if (object.call_slot || object.call_result) {
            whole = call_value(object);
        } else if (const auto local = variable_local(object)) {
            whole = read_local(*local);
        }
        if (!whole || object.type == nullptr) {
            return whole;
        }
        const auto width = width_of(*object.type);
        if (width == 0U || object.offset + width > whole->width()) {
            return whole;
        }
        if (object.offset == 0U && width == whole->width()) {
            return whole;
        }
        PackedLogic4 result(width, Logic4::zero);
        if (whole->is_logic9()) {
            for (std::size_t bit = 0U; bit < width; ++bit) {
                result.set_logic9(bit, whole->get_logic9(object.offset + bit));
            }
        } else {
            for (std::size_t bit = 0U; bit < width; ++bit) {
                result.set(bit, whole->get(object.offset + bit));
            }
        }
        return result;
    }

    [[nodiscard]] static std::uint64_t unsigned_bits(const PackedLogic4& value,
        const std::size_t offset, const std::size_t width)
    {
        std::uint64_t result { };
        for (std::size_t bit = 0U; bit < width && bit < 64U; ++bit) {
            if (value.get(offset + bit) == Logic4::one) {
                result |= std::uint64_t { 1 } << bit;
            }
        }
        return result;
    }

    [[nodiscard]] static std::int64_t signed_bits(const PackedLogic4& value,
        const std::size_t offset, const std::size_t width)
    {
        auto result = unsigned_bits(value, offset, width);
        if (width < 64U && width > 0U
            && (result & (std::uint64_t { 1 } << (width - 1U))) != 0U) {
            result |= ~((std::uint64_t { 1 } << width) - 1U);
        }
        return static_cast<std::int64_t>(result);
    }

    [[nodiscard]] static Logic9 logic9_at(const PackedLogic4& value,
        const std::size_t offset)
    {
        if (value.is_logic9()) {
            return value.get_logic9(offset);
        }
        switch (value.get(offset)) {
        case Logic4::zero:
            return Logic9::zero;
        case Logic4::one:
            return Logic9::one;
        case Logic4::z:
            return Logic9::z;
        case Logic4::x:
            return Logic9::x;
        }
        return Logic9::x;
    }

    // The scalar format of a type's values (IEEE 1076-2019 22.4).
    [[nodiscard]] static vhpiFormatT scalar_format(const TypeData& data)
    {
        switch (data.storage) {
        case Storage::integer:
            return vhpiIntVal;
        case Storage::logic9:
            return vhpiLogicVal;
        case Storage::enumeration:
            if (data.literals.size() == 256U) {
                return vhpiCharVal;
            }
            return data.literals.size() <= 256U ? vhpiSmallEnumVal
                                                : vhpiEnumVal;
        case Storage::real:
            return vhpiRealVal;
        case Storage::physical:
            return vhpiPhysVal;
        default:
            return vhpiRawDataVal;
        }
    }

    [[nodiscard]] static vhpiFormatT object_format(const Object& type)
    {
        const auto& data = *type.type_data;
        if (data.storage != Storage::array) {
            return scalar_format(data);
        }
        if (data.element == nullptr || data.element->type_data == nullptr) {
            return vhpiRawDataVal;
        }
        const auto& element = *data.element->type_data;
        switch (element.storage) {
        case Storage::integer:
            return vhpiIntVecVal;
        case Storage::logic9:
            return vhpiLogicVecVal;
        case Storage::enumeration:
            if (element.literals.size() == 256U) {
                return vhpiStrVal;
            }
            return element.literals.size() <= 256U ? vhpiSmallEnumVecVal
                                                   : vhpiEnumVecVal;
        case Storage::real:
            return vhpiRealVecVal;
        case Storage::physical:
            return vhpiPhysVecVal;
        default:
            return vhpiRawDataVal;
        }
    }

    // The scalar value at `offset` as an integer of the type's position.
    [[nodiscard]] static std::int64_t scalar_at(const TypeData& data,
        const PackedLogic4& value, const std::size_t offset)
    {
        switch (data.storage) {
        case Storage::integer:
        case Storage::physical:
            return signed_bits(value, offset, data.width);
        case Storage::logic9:
            return static_cast<std::int64_t>(logic9_at(value, offset));
        default:
            return static_cast<std::int64_t>(
                unsigned_bits(value, offset, data.width));
        }
    }

    // A scalar in a string format: its position number in that radix
    // (IEEE 1076-2019 22.2.8). A STD_ULOGIC value in vhpiBinStrVal reads as
    // its character, as it does for logic arrays; the format is optional
    // (22.4), and VHPI programs compare it against "0" and "1".
    [[nodiscard]] std::optional<std::string> scalar_text(const TypeData& data,
        const PackedLogic4& bits, const vhpiFormatT format) const
    {
        unsigned radix = 0U;
        switch (format) {
        case vhpiBinStrVal:
            radix = 2U;
            break;
        case vhpiOctStrVal:
            radix = 8U;
            break;
        case vhpiDecStrVal:
            radix = 10U;
            break;
        case vhpiHexStrVal:
            radix = 16U;
            break;
        default:
            return std::nullopt;
        }
        if (data.storage == Storage::real || data.storage == Storage::other) {
            return std::nullopt;
        }
        const auto position = scalar_at(data, bits, 0U);
        if (format == vhpiBinStrVal && data.storage == Storage::logic9) {
            return std::string(
                1U, runtime::to_char(static_cast<Logic9>(position)));
        }
        std::array<char, 72> digits { };
        const auto result = std::to_chars(digits.data(),
            digits.data() + digits.size(), position, static_cast<int>(radix));
        auto text = std::string { digits.data(), result.ptr };
        if (radix == 16U) {
            std::ranges::transform(text, text.begin(), [](const char c) {
                return static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
            });
        }
        return text;
    }

    // The value of a vector of BIT or STD_ULOGIC elements, leftmost
    // first, as binary digit characters (IEEE 1076-2019 22.2.8).
    [[nodiscard]] static std::optional<std::string> logic_chars(
        const TypeData& element, const PackedLogic4& bits,
        const std::size_t count, const std::size_t element_width)
    {
        if (element_width != 1U
            || !(element.storage == Storage::logic9
                || (element.storage == Storage::enumeration
                    && element.literals.size() == 2U))) {
            return std::nullopt;
        }
        std::string chars;
        for (std::size_t position = 0U; position < count; ++position) {
            const auto offset = count - 1U - position;
            if (element.storage == Storage::logic9) {
                chars.push_back(runtime::to_char(logic9_at(bits, offset)));
            } else {
                chars.push_back(
                    scalar_at(element, bits, offset) != 0 ? '1' : '0');
            }
        }
        return chars;
    }

    // The value of a vector in the octal, decimal or hexadecimal string
    // format. A digit whose bits are not all 0 or 1 reads as the common
    // character of its bits, or X when they differ; a decimal string needs
    // known bits and at most 64 of them.
    [[nodiscard]] static std::optional<std::string> vector_text(
        const TypeData& element, const PackedLogic4& bits,
        const std::size_t count, const std::size_t element_width,
        const vhpiFormatT format)
    {
        const auto chars = logic_chars(element, bits, count, element_width);
        if (!chars) {
            return std::nullopt;
        }
        return radix_text(*chars, format);
    }

    // `chars`, binary digit characters, in an octal, decimal or hexadecimal
    // string.
    [[nodiscard]] static std::optional<std::string> radix_text(
        const std::string_view chars, const vhpiFormatT format)
    {
        const auto known = [](const char c) {
            return c == '0' || c == '1' || c == 'L' || c == 'H';
        };
        const auto bit = [](const char c) {
            return c == '1' || c == 'H' ? 1U : 0U;
        };
        if (format == vhpiDecStrVal) {
            if (chars.size() > 64U || !std::ranges::all_of(chars, known)) {
                return std::nullopt;
            }
            std::uint64_t number { };
            for (const auto c : chars) {
                number = (number << 1U) | bit(c);
            }
            std::array<char, 24> digits { };
            const auto result = std::to_chars(
                digits.data(), digits.data() + digits.size(), number);
            return std::string { digits.data(), result.ptr };
        }
        const std::size_t group = format == vhpiOctStrVal ? 3U : 4U;
        const std::size_t groups = (chars.size() + group - 1U) / group;
        std::string text;
        for (std::size_t index = 0U; index < groups; ++index) {
            // The digit's bits, the first group padded with zero bits.
            std::string digit;
            const auto first = index * group;
            const auto pad = groups * group - chars.size();
            for (std::size_t offset = 0U; offset < group; ++offset) {
                const auto place = first + offset;
                digit.push_back(place < pad ? '0' : chars[place - pad]);
            }
            if (std::ranges::all_of(digit, known)) {
                unsigned number { };
                for (const auto c : digit) {
                    number = (number << 1U) | bit(c);
                }
                text.push_back("0123456789ABCDEF"[number]);
            } else if (std::ranges::all_of(digit, [&](const char c) {
                           return c == digit.front();
                       })) {
                text.push_back(digit.front());
            } else {
                text.push_back('X');
            }
        }
        return text;
    }

    [[nodiscard]] int read_value(const Object& object, vhpiValueT& value)
    {
        if (object.type == nullptr || object.type->type_data == nullptr) {
            error(vhpiError, "object has no value");
            return -1;
        }
        const auto bits = element_bits(object);
        if (!bits) {
            error(vhpiError, object.full_name + " has no value");
            return -1;
        }
        const auto& type = *object.type;
        const auto& data = *type.type_data;
        if (value.format == vhpiObjTypeVal) {
            value.format = object_format(type);
        }
        // The implementation-defined internal representation (IEEE
        // 1076-2019 22.2.8): one byte per scalar bit, the least
        // significant first, holding the bit's logic code.
        if (value.format == vhpiRawDataVal) {
            const auto size = bits->width();
            value.numElems = static_cast<int32_t>(size);
            if (value.bufSize < size || value.value.ptr == nullptr) {
                return static_cast<int>(size);
            }
            auto* const bytes = static_cast<std::uint8_t*>(value.value.ptr);
            for (std::size_t bit = 0U; bit < size; ++bit) {
                bytes[bit] = bits->is_logic9()
                    ? static_cast<std::uint8_t>(bits->get_logic9(bit))
                    : static_cast<std::uint8_t>(
                          bits->get(bit) == Logic4::one ? 1U : 0U);
            }
            return 0;
        }
        const auto store_scalar = [&](const TypeData& scalar,
                                      const std::size_t offset,
                                      const vhpiFormatT format) -> bool {
            const auto position = scalar_at(scalar, *bits, offset);
            switch (format) {
            case vhpiIntVal:
                value.value.intg = static_cast<vhpiIntT>(position);
                return true;
            case vhpiLongIntVal:
                value.value.longintg = position;
                return true;
            case vhpiLogicVal:
            case vhpiEnumVal:
                value.value.enumv = static_cast<vhpiEnumT>(position);
                return true;
            case vhpiSmallEnumVal:
                value.value.smallenumv = static_cast<vhpiSmallEnumT>(position);
                return true;
            case vhpiCharVal:
                value.value.ch = static_cast<vhpiCharT>(position);
                return true;
            case vhpiRealVal:
                value.value.real = std::bit_cast<double>(
                    unsigned_bits(*bits, offset, 64U));
                return true;
            case vhpiPhysVal:
            case vhpiTimeVal:
                value.value.phys = vhpiPhysT {
                    static_cast<int32_t>(position >> 32U),
                    static_cast<uint32_t>(position) };
                return true;
            default:
                return false;
            }
        };
        if (data.storage != Storage::array) {
            if (const auto text = scalar_text(data, *bits, value.format)) {
                // The size required, terminator included, when the buffer is
                // too small (IEEE 1076-2019 23.19).
                const auto size = text->size() + 1U;
                value.numElems = 1;
                if (value.bufSize < size || value.value.str == nullptr) {
                    return static_cast<int>(size);
                }
                std::memcpy(value.value.str, text->c_str(), size);
                return 0;
            }
            if (!store_scalar(data, 0U, value.format)) {
                error(vhpiError, "unsupported value format "
                        + std::to_string(value.format));
                return -1;
            }
            value.numElems = 1;
            return 0;
        }
        if (data.element == nullptr || data.element->type_data == nullptr) {
            error(vhpiError, "array element type is unknown");
            return -1;
        }
        const auto& element = *data.element->type_data;
        const auto element_width = width_of(*data.element);
        const auto count = element_width == 0U
            ? 0U
            : bits->width() / element_width;
        const auto offset_of = [&](const std::size_t position) {
            return (count - 1U - position) * element_width;
        };
        const auto need = [&](const std::size_t size) -> bool {
            value.numElems = static_cast<int32_t>(count);
            if (value.bufSize < size) {
                return false;
            }
            return true;
        };
        switch (value.format) {
        case vhpiStrVal:
        case vhpiBinStrVal: {
            const auto size = count + 1U;
            if (!need(size)) {
                return static_cast<int>(size);
            }
            for (std::size_t position = 0U; position < count; ++position) {
                const auto scalar = scalar_at(element, *bits, offset_of(position));
                char character = static_cast<char>(scalar);
                if (value.format == vhpiBinStrVal
                    || element.storage == Storage::logic9) {
                    if (element.storage == Storage::logic9) {
                        character = runtime::to_char(
                            static_cast<Logic9>(scalar));
                    } else {
                        character = scalar != 0 ? '1' : '0';
                    }
                }
                value.value.str[position] = static_cast<vhpiCharT>(character);
            }
            value.value.str[count] = 0;
            return 0;
        }
        case vhpiOctStrVal:
        case vhpiDecStrVal:
        case vhpiHexStrVal: {
            // A vector of BIT or STD_ULOGIC read as one number (IEEE
            // 1076-2019 22.2.8).
            const auto text = vector_text(element, *bits, count,
                element_width, value.format);
            if (!text) {
                error(vhpiError, "unsupported value format "
                        + std::to_string(value.format));
                return -1;
            }
            const auto size = text->size() + 1U;
            value.numElems = static_cast<int32_t>(text->size());
            if (value.bufSize < size || value.value.str == nullptr) {
                return static_cast<int>(size);
            }
            std::memcpy(value.value.str, text->c_str(), size);
            return 0;
        }
        case vhpiPhysVecVal:
        case vhpiTimeVecVal: {
            const auto size = count * sizeof(vhpiPhysT);
            if (!need(size)) {
                return static_cast<int>(size);
            }
            for (std::size_t position = 0U; position < count; ++position) {
                const auto scalar
                    = scalar_at(element, *bits, offset_of(position));
                const vhpiPhysT physical { static_cast<int32_t>(scalar >> 32U),
                    static_cast<uint32_t>(scalar) };
                if (value.format == vhpiPhysVecVal) {
                    value.value.physs[position] = physical;
                } else {
                    value.value.times[position]
                        = vhpiTimeT { physical.high, physical.low };
                }
            }
            return 0;
        }
        case vhpiLogicVecVal:
        case vhpiEnumVecVal: {
            const auto size = count * sizeof(vhpiEnumT);
            if (!need(size)) {
                return static_cast<int>(size);
            }
            for (std::size_t position = 0U; position < count; ++position) {
                value.value.enumvs[position] = static_cast<vhpiEnumT>(
                    scalar_at(element, *bits, offset_of(position)));
            }
            return 0;
        }
        case vhpiSmallEnumVecVal: {
            const auto size = count * sizeof(vhpiSmallEnumT);
            if (!need(size)) {
                return static_cast<int>(size);
            }
            for (std::size_t position = 0U; position < count; ++position) {
                value.value.smallenumvs[position]
                    = static_cast<vhpiSmallEnumT>(
                        scalar_at(element, *bits, offset_of(position)));
            }
            return 0;
        }
        case vhpiIntVecVal: {
            const auto size = count * sizeof(vhpiIntT);
            if (!need(size)) {
                return static_cast<int>(size);
            }
            for (std::size_t position = 0U; position < count; ++position) {
                value.value.intgs[position] = static_cast<vhpiIntT>(
                    scalar_at(element, *bits, offset_of(position)));
            }
            return 0;
        }
        case vhpiRealVecVal: {
            const auto size = count * sizeof(vhpiRealT);
            if (!need(size)) {
                return static_cast<int>(size);
            }
            for (std::size_t position = 0U; position < count; ++position) {
                value.value.reals[position] = std::bit_cast<double>(
                    unsigned_bits(*bits, offset_of(position), 64U));
            }
            return 0;
        }
        default:
            // A scalar format reads the leftmost element.
            if (count != 0U && store_scalar(element, offset_of(0U), value.format)) {
                value.numElems = 1;
                return 0;
            }
            error(vhpiError, "unsupported value format "
                    + std::to_string(value.format));
            return -1;
        }
    }

    // --- vhpi_format_value (IEEE 1076-2019 23.9) ---------------------------------

    // A value in an intermediate form: a position number, and/or binary
    // digit characters, leftmost first.
    struct Canonical {
        std::optional<std::int64_t> number;
        std::string chars;
    };

    [[nodiscard]] static std::optional<std::int64_t> unit_position(
        const vhpiPhysT& unit) noexcept
    {
        const auto position = (static_cast<std::int64_t>(unit.high) << 32)
            | static_cast<std::int64_t>(unit.low);
        return position > 0 ? std::optional<std::int64_t> { position }
                            : std::nullopt;
    }

    [[nodiscard]] static std::optional<int> logic_code(const char character)
    {
        for (int code = 0; code < 9; ++code) {
            if (runtime::to_char(static_cast<Logic9>(code)) == character) {
                return code;
            }
        }
        return std::nullopt;
    }

    // The unsigned value of binary digit characters.
    [[nodiscard]] static std::optional<std::int64_t> binary_value(
        const std::string_view chars)
    {
        if (chars.empty() || chars.size() > 62U
            || !std::ranges::all_of(chars,
                [](const char c) { return c == '0' || c == '1'; })) {
            return std::nullopt;
        }
        std::int64_t number { };
        for (const auto c : chars) {
            number = (number << 1) | (c == '1' ? 1 : 0);
        }
        return number;
    }

    [[nodiscard]] static std::optional<std::string> binary_chars(
        const std::int64_t number)
    {
        if (number < 0) {
            return std::nullopt;
        }
        std::array<char, 72> digits { };
        const auto result = std::to_chars(
            digits.data(), digits.data() + digits.size(), number, 2);
        return std::string { digits.data(), result.ptr };
    }

    // The input value structure of vhpi_format_value as a Canonical.
    [[nodiscard]] static std::optional<Canonical> canonical_value(
        const vhpiValueT& in)
    {
        Canonical result;
        const auto count = in.numElems > 0
            ? static_cast<std::size_t>(in.numElems)
            : std::size_t { 0 };
        switch (in.format) {
        case vhpiIntVal:
        case vhpiLongIntVal:
        case vhpiEnumVal:
        case vhpiSmallEnumVal:
        case vhpiCharVal:
            result.number = scalar_of(in);
            return result;
        case vhpiLogicVal: {
            const auto code = static_cast<int>(in.value.enumv);
            if (code < 0 || code > 8) {
                return std::nullopt;
            }
            result.number = code;
            result.chars = std::string(
                1U, runtime::to_char(static_cast<Logic9>(code)));
            return result;
        }
        case vhpiPhysVal:
        case vhpiTimeVal:
            result.number = scalar_of(in);
            if (const auto scale = unit_position(in.unit)) {
                *result.number *= *scale;
            }
            return result;
        case vhpiBinStrVal:
        case vhpiOctStrVal:
        case vhpiDecStrVal:
        case vhpiHexStrVal: {
            if (in.value.str == nullptr) {
                return std::nullopt;
            }
            const std::string_view text {
                reinterpret_cast<const char*>(in.value.str) };
            if (text.empty()) {
                return std::nullopt;
            }
            if (in.format == vhpiBinStrVal) {
                result.chars = std::string { text };
                return result;
            }
            const int base = in.format == vhpiOctStrVal ? 8
                : in.format == vhpiHexStrVal            ? 16
                                                        : 10;
            std::int64_t number { };
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), number, base);
            if (parsed.ec != std::errc { }
                || parsed.ptr != text.data() + text.size()) {
                return std::nullopt;
            }
            result.number = number;
            return result;
        }
        case vhpiLogicVecVal:
        case vhpiEnumVecVal:
        case vhpiSmallEnumVecVal: {
            if (in.value.enumvs == nullptr || count == 0U) {
                return std::nullopt;
            }
            for (std::size_t index = 0U; index < count; ++index) {
                const auto code = in.format == vhpiSmallEnumVecVal
                    ? static_cast<std::uint32_t>(in.value.smallenumvs[index])
                    : in.value.enumvs[index];
                if (in.format == vhpiLogicVecVal) {
                    if (code > 8U) {
                        return std::nullopt;
                    }
                    result.chars.push_back(runtime::to_char(
                        static_cast<Logic9>(code)));
                } else if (code > 1U) {
                    return std::nullopt;
                } else {
                    result.chars.push_back(code == 1U ? '1' : '0');
                }
            }
            return result;
        }
        default:
            return std::nullopt;
        }
    }

    // Write `value` in the format of `out`; the return value follows 23.9.
    [[nodiscard]] int store_formatted(const Canonical& value, vhpiValueT& out)
    {
        const auto number = [&]() -> std::optional<std::int64_t> {
            return value.number ? value.number : binary_value(value.chars);
        };
        const auto fail = [&](const char* const reason) {
            error(vhpiError, std::string { "vhpi_format_value: " } + reason);
            return -1;
        };
        // A string or vector result needs a buffer of `size` bytes.
        const auto fits = [&](const std::size_t size, const std::size_t elems,
                              const void* const buffer) {
            out.numElems = static_cast<int32_t>(elems);
            return buffer != nullptr && out.bufSize >= size;
        };
        switch (out.format) {
        case vhpiIntVal:
        case vhpiLongIntVal:
        case vhpiEnumVal:
        case vhpiSmallEnumVal:
        case vhpiCharVal:
        case vhpiLogicVal:
        case vhpiPhysVal:
        case vhpiTimeVal: {
            std::optional<std::int64_t> position = number();
            if (out.format == vhpiLogicVal && !value.number
                && value.chars.size() == 1U) {
                if (const auto code = logic_code(value.chars.front())) {
                    position = *code;
                }
            }
            if (!position) {
                return fail("the value has no numeric form");
            }
            if (out.format == vhpiPhysVal || out.format == vhpiTimeVal) {
                if (const auto scale = unit_position(out.unit)) {
                    position = *position / *scale;
                }
                out.value.phys = vhpiPhysT {
                    static_cast<int32_t>(*position >> 32U),
                    static_cast<uint32_t>(*position) };
                return 0;
            }
            const auto low = out.format == vhpiIntVal
                ? std::int64_t { std::numeric_limits<std::int32_t>::min() }
                : out.format == vhpiLongIntVal
                ? std::numeric_limits<std::int64_t>::min()
                : std::int64_t { 0 };
            const auto high = out.format == vhpiIntVal
                ? std::int64_t { std::numeric_limits<std::int32_t>::max() }
                : out.format == vhpiLongIntVal
                ? std::numeric_limits<std::int64_t>::max()
                : out.format == vhpiSmallEnumVal || out.format == vhpiCharVal
                ? std::int64_t { 255 }
                : out.format == vhpiLogicVal
                ? std::int64_t { 8 }
                : std::int64_t { std::numeric_limits<std::int32_t>::max() };
            if (*position < low || *position > high) {
                return fail("the value is outside the range of the format");
            }
            switch (out.format) {
            case vhpiIntVal:
                out.value.intg = static_cast<vhpiIntT>(*position);
                break;
            case vhpiLongIntVal:
                out.value.longintg = *position;
                break;
            case vhpiSmallEnumVal:
                out.value.smallenumv = static_cast<vhpiSmallEnumT>(*position);
                break;
            case vhpiCharVal:
                out.value.ch = static_cast<vhpiCharT>(*position);
                break;
            default:
                out.value.enumv = static_cast<vhpiEnumT>(*position);
                break;
            }
            return 0;
        }
        case vhpiBinStrVal:
        case vhpiOctStrVal:
        case vhpiDecStrVal:
        case vhpiHexStrVal: {
            std::optional<std::string> text;
            if (out.format == vhpiDecStrVal && value.number) {
                text = std::to_string(*value.number);
            } else {
                auto chars = value.chars;
                if (chars.empty() && value.number) {
                    if (const auto binary = binary_chars(*value.number)) {
                        chars = *binary;
                    }
                }
                if (chars.empty()) {
                    return fail("the value has no digit form");
                }
                text = out.format == vhpiBinStrVal
                    ? std::optional<std::string> { chars }
                    : radix_text(chars, out.format);
            }
            if (!text) {
                return fail("the value cannot be shown in that radix");
            }
            const auto size = text->size() + 1U;
            if (!fits(size, text->size(), out.value.str)) {
                return static_cast<int>(size);
            }
            std::memcpy(out.value.str, text->c_str(), size);
            return 0;
        }
        case vhpiLogicVecVal:
        case vhpiEnumVecVal:
        case vhpiSmallEnumVecVal: {
            auto chars = value.chars;
            if (chars.empty() && value.number) {
                if (const auto binary = binary_chars(*value.number)) {
                    chars = *binary;
                }
            }
            if (chars.empty()) {
                return fail("the value has no vector form");
            }
            const auto small = out.format == vhpiSmallEnumVecVal;
            const auto size
                = chars.size() * (small ? sizeof(vhpiSmallEnumT) : sizeof(vhpiEnumT));
            if (!fits(size, chars.size(), out.value.enumvs)) {
                return static_cast<int>(size);
            }
            for (std::size_t index = 0U; index < chars.size(); ++index) {
                int code = 0;
                if (out.format == vhpiLogicVecVal) {
                    const auto found = logic_code(chars[index]);
                    if (!found) {
                        return fail("not a logic character");
                    }
                    code = *found;
                } else if (chars[index] == '0' || chars[index] == '1') {
                    code = chars[index] == '1' ? 1 : 0;
                } else {
                    return fail("not a bit");
                }
                if (small) {
                    out.value.smallenumvs[index]
                        = static_cast<vhpiSmallEnumT>(code);
                } else {
                    out.value.enumvs[index] = static_cast<vhpiEnumT>(code);
                }
            }
            return 0;
        }
        default:
            return fail("unsupported output format");
        }
    }

    // Convert the value of `in` to the format of `out` (IEEE 1076-2019
    // 23.9); the conversions are among the scalar, string and logic vector
    // formats.
    [[nodiscard]] int format_value(const vhpiValueT* const in,
        vhpiValueT* const out)
    {
        if (in == nullptr || out == nullptr) {
            error(vhpiError, "vhpi_format_value: missing value structure");
            return -1;
        }
        const auto value = canonical_value(*in);
        if (!value) {
            error(vhpiError,
                "vhpi_format_value: unsupported input format "
                    + std::to_string(in->format));
            return -1;
        }
        return store_formatted(*value, *out);
    }

    [[nodiscard]] int get_value(const vhpiHandleT handle, vhpiValueT* const value)
    {
        auto* const object = object_of(handle);
        if (object == nullptr || value == nullptr) {
            return -1;
        }
        return read_value(*object, *value);
    }

    static void store_unsigned(PackedLogic4& target, const std::size_t offset,
        const std::size_t width, const std::uint64_t value)
    {
        for (std::size_t bit = 0U; bit < width; ++bit) {
            const auto set = bit < 64U
                && ((value >> bit) & std::uint64_t { 1 }) != 0U;
            if (target.is_logic9()) {
                target.set_logic9(offset + bit, set ? Logic9::one : Logic9::zero);
            } else {
                target.set(offset + bit, set ? Logic4::one : Logic4::zero);
            }
        }
    }

    static void store_scalar(PackedLogic4& target, const TypeData& data,
        const std::size_t offset, const std::int64_t position)
    {
        if (data.storage == Storage::logic9) {
            target.set_logic9(offset, static_cast<Logic9>(position));
            return;
        }
        store_unsigned(target, offset, data.width,
            static_cast<std::uint64_t>(position));
    }

    // The position of `value`'s scalar as the element type stores it, or
    // nothing when the format does not denote one scalar.
    [[nodiscard]] static std::optional<std::int64_t> scalar_of(
        const vhpiValueT& value)
    {
        switch (value.format) {
        case vhpiIntVal:
            return value.value.intg;
        case vhpiLongIntVal:
            return value.value.longintg;
        case vhpiLogicVal:
        case vhpiEnumVal:
            return static_cast<std::int64_t>(value.value.enumv);
        case vhpiSmallEnumVal:
            return value.value.smallenumv;
        case vhpiCharVal:
            return static_cast<unsigned char>(value.value.ch);
        case vhpiRealVal:
            return static_cast<std::int64_t>(
                std::bit_cast<std::uint64_t>(value.value.real));
        case vhpiPhysVal:
        case vhpiTimeVal:
            return (static_cast<std::int64_t>(value.value.phys.high) << 32)
                | value.value.phys.low;
        default:
            return std::nullopt;
        }
    }

    // Write `value` into the element of the signal `object` denotes
    // (IEEE 1076-2019 23.26 vhpi_put_value). The buffer size gives the
    // number of array elements written, from the leftmost.
    [[nodiscard]] int put_value(const vhpiHandleT handle, vhpiValueT* const value,
        const vhpiPutValueModeT mode)
    {
        auto* const object = object_of(handle);
        if (object == nullptr || value == nullptr) {
            return 1;
        }
        // A variable of a process is updated at once, without activity on
        // signals, so the phases that refuse that allow it (22.5.2).
        const auto local = variable_local(*object);
        if (!local && (time_step_ended_ || postponed_)) {
            error(vhpiError,
                "cannot create delta cycle during current simulation phase");
            return 1;
        }
        // No update at the end of simulation (IEEE 1076-2019 21.3.7.6).
        if (simulation_ending_) {
            error(vhpiError,
                "cannot update a value in an end-of-simulation callback");
            return 1;
        }
        if (object->kind == vhpiDriverK) {
            error(vhpiError, "an ordinary driver has no vhpiWrite access");
            return 1;
        }
        const bool call = object->call_slot || object->call_result;
        if ((!object->signal && !call && !local) || object->type == nullptr
            || object->type->type_data == nullptr) {
            error(vhpiError, object->full_name + " cannot be updated");
            return 1;
        }
        // A variable takes the deposit modes, which are the same for it
        // (22.5.1, 22.5.2); forcing a variable is not supported.
        if (local && mode != vhpiDeposit && mode != vhpiDepositPropagate) {
            error(vhpiError, object->full_name
                    + ": forcing and releasing a variable are not supported");
            return 1;
        }
        // A foreign subprogram writes its formals of mode out or inout and
        // its function result (IEEE 1076-2019 20.4.2, 22.5.5).
        if (call
            && (active_call_ == nullptr
                || (object->call_slot
                    && (*object->call_slot >= active_call_->directions.size()
                        || (active_call_->directions[*object->call_slot]
                                != static_cast<std::uint8_t>(
                                    frontend::PortDirection::Output)
                            && active_call_->directions[*object->call_slot]
                                != static_cast<std::uint8_t>(
                                    frontend::PortDirection::Inout))))
                || mode == vhpiRelease)) {
            error(vhpiError, object->full_name + " cannot be updated");
            return 1;
        }
        if (mode == vhpiRelease) {
            simulation_.release_signal(*object->signal);
            put_force_event(*object, true, false);
            return 0;
        }
        // Writes to elements of a signal in one cycle merge into the value
        // the next update phase applies.
        const bool propagate
            = mode == vhpiDepositPropagate || mode == vhpiForcePropagate;
        const auto pending = !call && !local && propagate
            ? pending_updates_.find(*object->signal)
            : pending_updates_.end();
        PackedLogic4 whole;
        if (local) {
            auto current = read_local(*local);
            if (!current) {
                error(vhpiError, object->full_name + " has no value yet");
                return 1;
            }
            whole = std::move(*current);
        } else {
            whole = call
                ? call_value(*object).value_or(PackedLogic4 {})
                : pending != pending_updates_.end()
                ? pending->second.value
                : simulation_.read_signal_snapshot(*object->signal);
        }
        const auto& data = *object->type->type_data;
        const auto width = width_of(*object->type);
        if (object->offset + width > whole.width()) {
            error(vhpiError, object->full_name + " has an unexpected layout");
            return 1;
        }
        if (data.storage != Storage::array) {
            const auto position = scalar_of(*value);
            if (!position) {
                error(vhpiError, "unsupported value format "
                        + std::to_string(value->format));
                return 1;
            }
            if (data.range && data.storage == Storage::integer
                && (*position < std::min(data.range->left, data.range->right)
                    || *position
                        > std::max(data.range->left, data.range->right))) {
                error(vhpiError, "value out of range for " + object->full_name);
                return 1;
            }
            store_scalar(whole, data, object->offset, *position);
        } else {
            if (data.element == nullptr || data.element->type_data == nullptr) {
                error(vhpiError, "array element type is unknown");
                return 1;
            }
            const auto& element = *data.element->type_data;
            const auto element_width = width_of(*data.element);
            const auto count = element_width == 0U ? 0U : width / element_width;
            const auto at = [&](const std::size_t position) {
                return object->offset + (count - 1U - position) * element_width;
            };
            const auto buffer = static_cast<std::size_t>(value->bufSize);
            std::size_t elements = 1U;
            switch (value->format) {
            case vhpiLogicVecVal:
            case vhpiEnumVecVal:
                elements = buffer / sizeof(vhpiEnumT);
                break;
            case vhpiSmallEnumVecVal:
                elements = buffer / sizeof(vhpiSmallEnumT);
                break;
            case vhpiIntVecVal:
                elements = buffer / sizeof(vhpiIntT);
                break;
            case vhpiRealVecVal:
                elements = buffer / sizeof(vhpiRealT);
                break;
            case vhpiStrVal:
            case vhpiBinStrVal:
                elements = buffer == 0U ? 0U : buffer - 1U;
                break;
            default:
                if (!scalar_of(*value)) {
                    error(vhpiError, "unsupported value format "
                            + std::to_string(value->format));
                    return 1;
                }
                break;
            }
            if (elements > count) {
                error(vhpiError, "too many values ("
                        + std::to_string(elements) + ") for "
                        + object->full_name + " with "
                        + std::to_string(count) + " elements");
                return 1;
            }
            for (std::size_t position = 0U; position < elements; ++position) {
                std::int64_t scalar { };
                switch (value->format) {
                case vhpiLogicVecVal:
                case vhpiEnumVecVal:
                    scalar = value->value.enumvs[position];
                    break;
                case vhpiSmallEnumVecVal:
                    scalar = value->value.smallenumvs[position];
                    break;
                case vhpiIntVecVal:
                    scalar = value->value.intgs[position];
                    break;
                case vhpiRealVecVal:
                    scalar = static_cast<std::int64_t>(
                        std::bit_cast<std::uint64_t>(
                            value->value.reals[position]));
                    break;
                case vhpiStrVal:
                case vhpiBinStrVal: {
                    const auto character = static_cast<char>(
                        value->value.str[position]);
                    if (element.storage == Storage::logic9) {
                        const auto parsed = runtime::parse_logic9(character);
                        if (!parsed) {
                            error(vhpiError, std::string { "invalid logic value '" }
                                    + character + "'");
                            return 1;
                        }
                        scalar = static_cast<std::int64_t>(*parsed);
                    } else if (value->format == vhpiBinStrVal) {
                        scalar = character == '1' ? 1 : 0;
                    } else {
                        scalar = static_cast<unsigned char>(character);
                    }
                    break;
                }
                default:
                    scalar = *scalar_of(*value);
                    break;
                }
                store_scalar(whole, element, at(position), scalar);
            }
        }
        if (call) {
            if (object->call_slot) {
                (*active_call_->actuals)[*object->call_slot] = std::move(whole);
            } else {
                active_call_->result = std::move(whole);
                active_call_->has_result = true;
            }
            return 0;
        }
        if (local) {
            try {
                simulation_.write_process_local(
                    local->first, local->second, whole);
            } catch (const std::exception& failure) {
                error(vhpiError, object->full_name + " cannot be updated: "
                        + failure.what());
                return 1;
            }
            return 0;
        }
        // The propagating modes take effect in the next signal update phase
        // (IEEE 1076-2019 22.5.3).
        switch (mode) {
        case vhpiForce:
            simulation_.force_signal(*object->signal, std::move(whole));
            absorb_change(*object->signal);
            put_force_event(*object, false, false);
            break;
        case vhpiForcePropagate:
        case vhpiDepositPropagate:
            schedule_update(*object->signal, std::move(whole),
                mode == vhpiForcePropagate);
            if (mode == vhpiForcePropagate) {
                put_force_event(*object, false, true);
            }
            break;
        default:
            simulation_.deposit_signal(*object->signal, std::move(whole));
            absorb_change(*object->signal);
            break;
        }
        return 0;
    }

    // A force or release by vhpi_put_value triggers the callbacks on its
    // object (IEEE 1076-2019 21.3.2.3-4): at once, or after the update
    // that applies a propagating force.
    void put_force_event(
        Object& object, const bool release, const bool propagate)
    {
        if (force_token_ == 0U) {
            return;
        }
        const auto whole = simulation_.read_signal_snapshot(*object.signal);
        const auto width = object.type != nullptr ? width_of(*object.type) : 0U;
        ForceEvent event { *object.signal, object.offset,
            width != 0U ? width : whole.width(), release, &object };
        if (propagate) {
            forces_.push_back(event);
        } else {
            deliver_force(event);
        }
    }

    // A deposit or force without propagation is not a value-change trigger
    // (IEEE 1076-2019 21.3.2.2 NOTE 1): the watchers take the new value as
    // already seen.
    void absorb_change(const SignalId signal)
    {
        for (auto* const callback : callbacks_) {
            if (callback->data.reason == vhpiCbValueChange
                && callback->object != nullptr && callback->object->signal
                && *callback->object->signal == signal) {
                callback->last = element_bits(*callback->object);
            }
        }
    }

    // A force takes precedence over a deposit scheduled for the same update
    // (IEEE 1076-2019 22.5.3 NOTE).
    void schedule_update(
        const SignalId signal, PackedLogic4 value, const bool force)
    {
        auto [found, created] = pending_updates_.try_emplace(signal);
        found->second.value = std::move(value);
        found->second.force = found->second.force || force;
        if (!created) {
            return;
        }
        simulation_.schedule_update_task([this, signal] {
            const auto update = pending_updates_.find(signal);
            if (update == pending_updates_.end()) {
                return;
            }
            auto value = std::move(update->second.value);
            const auto force = update->second.force;
            pending_updates_.erase(update);
            if (force) {
                simulation_.force_signal(signal, std::move(value));
            } else {
                simulation_.deposit_signal(signal, std::move(value));
            }
        });
    }

    // --- foreign subprograms -------------------------------------------------------

    [[nodiscard]] vhpiHandleT register_foreign(vhpiForeignDataT* const data)
    {
        if (data == nullptr || data->libraryName == nullptr
            || data->modelName == nullptr) {
            error(vhpiError, "foreign model data is incomplete");
            return nullptr;
        }
        if ((data->kind == vhpiProcF || data->kind == vhpiFuncF)
            && data->execf == nullptr) {
            error(vhpiError, "a foreign subprogram needs an execution function");
            return nullptr;
        }
        const std::string_view library { data->libraryName };
        const std::string_view model { data->modelName };
        // A library and model name a single registration (20.2.4.2).
        if (std::ranges::any_of(foreign_models_, [&](const auto& stored) {
                return stored->data.kind == data->kind
                    && stored->library == library && stored->model == model;
            })) {
            error(vhpiError, "foreign model '" + std::string { library } + " "
                    + std::string { model } + "' is already registered");
            return nullptr;
        }
        auto stored = std::make_unique<ForeignModel>();
        stored->library = std::string { library };
        stored->model = std::string { model };
        stored->data = *data;
        stored->data.libraryName = stored->library.data();
        stored->data.modelName = stored->model.data();
        foreign_models_.push_back(std::move(stored));
        return to_handle(foreign_models_.back().get());
    }

    [[nodiscard]] int foreign_info(const vhpiHandleT handle,
        vhpiForeignDataT* const data)
    {
        auto* const base = handle_of(handle);
        if (base == nullptr || base->tag != HandleTag::foreign
            || data == nullptr) {
            error(vhpiError, "handle is not a foreign model");
            return 1;
        }
        *data = static_cast<ForeignModel&>(*base).data;
        return 0;
    }

    // The value of a formal or the result of the running foreign call.
    [[nodiscard]] std::optional<PackedLogic4> call_value(const Object& object)
    {
        if (active_call_ == nullptr) {
            return std::nullopt;
        }
        if (object.call_slot) {
            return *object.call_slot < active_call_->actuals->size()
                ? std::optional { (*active_call_->actuals)[*object.call_slot] }
                : std::nullopt;
        }
        return active_call_->result;
    }

    // The subtype of a formal: an unconstrained array formal takes its
    // actual's length, indexed from its index subtype's left bound
    // (IEEE 1076-2019 5.3.2.2).
    [[nodiscard]] Object* formal_type(
        const vhdl::Declaration& formal, const std::size_t width)
    {
        auto* const type = formal.subtype
            ? subtype_object(*formal.subtype, nullptr)
            : nullptr;
        if (type == nullptr || type->type_data == nullptr) {
            return type;
        }
        const auto& data = *type->type_data;
        if (data.storage != Storage::array || data.element == nullptr
            || (!data.unconstrained && !data.dimensions.empty())
            || data.dimension_count > 1U) {
            return type;
        }
        const auto element = width_of(*data.element);
        if (element == 0U || width % element != 0U) {
            return type;
        }
        const auto* const index = data.index_types.empty()
            ? nullptr
            : data.index_types.front();
        const auto left = index != nullptr && index->type_data != nullptr
                && index->type_data->range
            ? index->type_data->range->left
            : 0;
        auto* const result = constrained(*type);
        result->type_data->dimensions = { Range { left,
            left + static_cast<std::int64_t>(width / element) - 1, false } };
        result->type_data->unconstrained = false;
        return result;
    }

    // Run the execution function of a foreign subprogram for one call
    // (IEEE 1076-2019 20.4.2). `identity` is
    // `<library>\t<model>\t<name>\t<declaration>\t<scope>`; each name is
    // `<formal>\t<declaration>`.
    [[nodiscard]] PackedLogic4 foreign_call(const std::string_view identity,
        std::vector<PackedLogic4>& actuals,
        const std::span<const std::string> names,
        const std::span<const std::uint8_t> directions)
    {
        std::vector<std::string_view> fields;
        for (auto rest = identity;;) {
            const auto tab = rest.find('\t');
            fields.push_back(rest.substr(0U, tab));
            if (tab == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(tab + 1U);
        }
        if (fields.size() < 4U) {
            throw std::runtime_error { "malformed foreign subprogram call" };
        }
        const auto found = std::ranges::find_if(foreign_models_,
            [&](const auto& stored) {
                return (stored->data.kind == vhpiProcF
                           || stored->data.kind == vhpiFuncF)
                    && stored->library == fields[0]
                    && stored->model == fields[1];
            });
        if (found == foreign_models_.end()) {
            throw std::runtime_error { "foreign subprogram '"
                + std::string { fields[2] } + "' (VHPI "
                + std::string { fields[0] } + " " + std::string { fields[1] }
                + ") has no registered execution function" };
        }
        const auto declaration = [&](const std::string_view text)
            -> const vhdl::Declaration* {
            std::uint32_t value { };
            const auto [last, failure] = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (failure != std::errc { } || last != text.data() + text.size()) {
                return nullptr;
            }
            return hir_.find_declaration(
                semantic::DeclarationId::from_index(value));
        };
        const auto* const callable = declaration(fields[3]);
        const bool function = callable != nullptr
            ? callable->form == vhdl::DeclarationForm::function
            : found->get()->data.kind == vhpiFuncF;
        auto* const call = make_object(
            function ? vhpiFuncCallK : vhpiSeqProcCallStmtK,
            upper(fields[2]), nullptr);
        auto* const region = root();
        call->full_name = (region != nullptr ? region->full_name : std::string {})
            + ":" + call->name;
        call->full_case_name = call->full_name;
        call->call_result = function;
        if (function && callable != nullptr && callable->callable
            && callable->callable->return_type) {
            call->type = subtype_object(*callable->callable->return_type, nullptr);
        }
        auto& parameters = call->lists[ListKind::param_decls];
        for (std::size_t index = 0U;
             index < names.size() && index < actuals.size(); ++index) {
            const auto name = std::string_view { names[index] };
            const auto tab = name.find('\t');
            const auto* const formal = tab == std::string_view::npos
                ? nullptr
                : declaration(name.substr(tab + 1U));
            const bool writable = index < directions.size()
                && (directions[index]
                        == static_cast<std::uint8_t>(
                            frontend::PortDirection::Output)
                    || directions[index]
                        == static_cast<std::uint8_t>(
                            frontend::PortDirection::Inout));
            auto* const parameter = make_object(
                writable ? vhpiVarParamDeclK : vhpiConstParamDeclK,
                upper(name.substr(0U, tab)), call);
            parameter->full_name = call->full_name + ":" + parameter->name;
            parameter->full_case_name = parameter->full_name;
            parameter->declaration = formal;
            parameter->call_slot = index;
            parameter->position = static_cast<std::int64_t>(index);
            if (formal != nullptr) {
                parameter->type = formal_type(*formal, actuals[index].width());
            }
            parameters.push_back(parameter);
        }
        if (function && (call->type == nullptr || width_of(*call->type) == 0U)) {
            throw std::runtime_error { "foreign function '"
                + std::string { fields[2] } + "' has no result subtype" };
        }
        ActiveCall running { &actuals, directions,
            PackedLogic4 { function ? width_of(*call->type) : 1U,
                Logic4::zero },
            false };
        auto* const previous = active_call_;
        active_call_ = &running;
        vhpiCbDataT data { };
        data.reason = vhpiCbStartOfSubpCall;
        data.cb_rtn = found->get()->data.execf;
        data.obj = to_handle(call);
        auto* const dispatching = dispatching_;
        dispatching_ = data.obj;
        try {
            Scope scope { *this };
            data.cb_rtn(&data);
        } catch (...) {
            dispatching_ = dispatching;
            active_call_ = previous;
            throw;
        }
        dispatching_ = dispatching;
        active_call_ = previous;
        if (function) {
            // A foreign function provides a result (20.4.2).
            if (!running.has_result) {
                throw std::runtime_error { "foreign function '"
                    + std::string { fields[2] } + "' provided no result" };
            }
            return std::move(running.result);
        }
        return PackedLogic4 { 1U, Logic4::zero };
    }

    struct ActiveCall {
        std::vector<PackedLogic4>* actuals { };
        std::span<const std::uint8_t> directions;
        PackedLogic4 result;
        bool has_result { };
    };

    Simulation& simulation_;
    const semantic::vhdl::Hir& hir_;
    const semantic::design::DesignIr& design_;
    PrintSink print_;
    std::uint64_t femtoseconds_per_tick_ { 1U };
    fsim_vhpi_std_host_v1 host_ { };
    std::vector<std::string> arguments_;
    std::vector<std::unique_ptr<platform::DynamicLibrary>> libraries_;
    std::vector<std::unique_ptr<Object>> objects_;
    // The sequence numbers of the loop statements of the processes, and
    // the objects of integer literals, by expression.
    std::map<const vhdl::Statement*, std::size_t> loop_numbers_;
    std::map<std::uint32_t, Object*> literal_objects_;
    std::map<std::int64_t, Object*> integer_objects_;
    // The shared variables held by a running vhpi_protected_call.
    std::set<const Object*> protected_held_;
    std::map<std::string, Object*, std::less<>> physical_literals_;
    // Callback slots: addresses stay stable and are reused after removal.
    std::deque<Callback> callback_pool_;
    std::vector<Callback*> free_callbacks_;
    // Registered callbacks in registration order.
    std::vector<Callback*> callbacks_;
    struct PendingUpdate {
        PackedLogic4 value;
        bool force { };
    };
    std::unordered_map<SignalId, PendingUpdate> pending_updates_;
    std::vector<std::unique_ptr<Iterator>> iterators_;
    std::deque<std::string> strings_;
    std::map<std::string, Object*, std::less<>> instances_;
    std::map<std::string, Object*, std::less<>> standard_types_;
    std::unordered_map<std::uint32_t, Object*> declared_types_;
    std::unordered_map<std::uint32_t, Object*> defined_types_;
    std::vector<Object*> argvs_;
    std::set<const vhdl::Declaration*> resolving_aliases_;
    std::map<std::string, Object*, std::less<>> packages_;
    std::unordered_map<std::uint32_t, const vhdl::Unit*> declaration_units_;
    std::unordered_map<std::uint32_t, const elaboration::SignalInfo*>
        signal_infos_;
    std::vector<runtime::SystemVerilogVpiCallbackHandle> lifecycle_;
    std::optional<runtime::SystemVerilogVpiCallbackHandle> next_time_;
    std::set<SignalId> changed_;
    std::vector<ForceEvent> forces_;
    std::set<SignalId> transaction_watch_;
    std::set<SignalId> active_;
    // The drivers (process, signal) updated since the last update phase.
    std::set<std::pair<runtime::simir::ProcessId, SignalId>> driver_active_;
    std::uint64_t driver_token_ { };
    std::uint64_t force_token_ { };
    std::uint64_t execution_token_ { };
    // The design processes that reached a statement since they last
    // suspended.
    std::set<runtime::simir::ProcessId> running_;
    // Whether a process object is a running design process, and the
    // statement of a process object at a source position.
    std::map<std::pair<const Object*, runtime::simir::ProcessId>, bool>
        process_runs_;
    // The signals each process or concurrent statement reads.
    std::map<const Object*, std::vector<Object*>> reads_cache_;
    // The connections of the port association elements, and the instances and
    // blocks whose port maps can name the signals of a region.
    std::map<const Object*, PortConnection> connections_;
    std::map<const Object*, std::vector<Object*>> owners_;
    std::map<std::tuple<const Object*, std::uint32_t, std::uint32_t>, Object*>
        statement_at_;
    // Every process statement object, and the one of a design process.
    std::optional<std::vector<Object*>> all_processes_;
    std::optional<std::vector<Object*>> all_bodies_;
    // The subprogram call at a call point.
    std::map<std::tuple<runtime::simir::ProcessId, std::uint32_t, std::uint32_t>,
        Object*>
        call_at_;
    std::unordered_map<const vhdl::Declaration*, Object*> params_;
    std::map<runtime::simir::ProcessId, Object*> process_objects_;
    std::uint64_t transaction_token_ { };
    Object* root_ { };
    Object* tool_ { };
    vhpiHandleT dispatching_ { };
    std::vector<std::unique_ptr<ForeignModel>> foreign_models_;
    ActiveCall* active_call_ { };
    std::uint64_t safe_point_token_ { };
    std::uint64_t change_token_ { };
    std::uint64_t next_order_ { };
    vhpiPhaseT phase_ { vhpiRegistrationPhase };
    bool has_error_ { };
    vhpiSeverityT error_severity_ { vhpiNote };
    std::string error_message_;
    bool stopping_ { };
    // vhpi_control(vhpiFinish) was called.
    bool finishing_ { };
    bool failed_ { };
    bool finished_ { };
    bool simulation_ended_ { };
    // Inside the end-of-simulation callbacks.
    bool simulation_ending_ { };
    bool time_step_ended_ { };
    // Between steps h 3) and h 6) of a cycle (IEEE 1076-2019 14.7.5.3).
    bool postponed_ { };
    bool end_of_time_step_pending_ { };
    bool in_pli_error_ { };
    // Traps SIGINT while vhpiCbSigInterrupt callbacks exist.
    std::unique_ptr<ForeignSignalTrap> interrupt_trap_;
    // A cycle has started that has not reached its step f 4).
    bool cycle_started_ { };
    // The first cycle of a time step has not updated its signals yet.
    bool awaiting_update_ { };
    // Timeouts that expired in the current cycle (14.7.5.3 f 3).
    std::vector<std::pair<Callback*, std::uint64_t>> due_timeouts_;
    // The time, in femtoseconds, whose first cycle has run steps b 1-2).
    std::optional<std::uint64_t> stepped_at_;
    // The initialization phase has completed.
    bool initialized_ { };
    bool cycled_ { };
    // The simulation cycles of the current time step that updated a signal
    // (IEEE 1076-2019 14.7.5.3): the cycle count vhpi_get_time reports.
    runtime::SimulationTick cycle_time_ { };
    std::uint64_t cycles_ { };
    bool cycle_counted_ { };
};

} // namespace

std::unique_ptr<VhpiForeignService> attach_vhpi_modules(
    Simulation& simulation,
    const std::span<const std::string> modules,
    VhpiForeignService::PrintSink print,
    const std::span<const std::string> arguments,
    diagnostic::Engine& diagnostics)
{
    auto service = std::make_unique<LiveService>(
        simulation, std::move(print), arguments);
    service->attach();
    if (!service->load(modules, diagnostics)) {
        return nullptr;
    }
    return service;
}

} // namespace fsim::app::application_detail
