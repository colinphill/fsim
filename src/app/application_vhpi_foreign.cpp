// SPDX-License-Identifier: Apache-2.0
#include "application_vhpi_foreign.hpp"

#include "application_internal.hpp"
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/vhpi_bridge.h"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
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
    // A formal of a foreign subprogram call: its actual's position in the
    // running call; the call itself carries a function's result.
    std::optional<std::size_t> call_slot;
    bool call_result { };

    // Type declarations.
    std::unique_ptr<TypeData> type_data;

    // Lazily built one-to-many associations.
    std::map<ListKind, std::vector<Object*>> lists;
    std::map<std::string, Object*, std::less<>> children;
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
    // ValueChange: the bits last seen.
    Object* object { };
    std::optional<PackedLogic4> last;
    std::uint64_t order { };
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
    case vhpiCbRepEndOfProcesses:
    case vhpiCbRepLastKnownDeltaCycle:
    case vhpiCbRepNextTimeStep:
    case vhpiCbRepEndOfTimeStep:
    case vhpiCbRepStartOfNextCycle:
    case vhpiCbRepAfterDelay:
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
    case vhpiCbRepAfterDelay:
        return vhpiCbAfterDelay;
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
        for (const auto& callback : callbacks_) {
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
        if (!simulation_ended_) {
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
        try {
            return function(service);
        } catch (const std::exception& error) {
            service.error(vhpiError, error.what());
        } catch (...) {
            service.error(vhpiInternal, "internal VHPI error");
        }
        return fallback;
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
    }

    // --- errors and output ---------------------------------------------------------

    void clear_error() noexcept { has_error_ = false; }

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
        auto stored = std::make_unique<Callback>();
        auto& callback = *stored;
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
        case vhpiCbEndOfProcesses:
        case vhpiCbRepEndOfProcesses:
        case vhpiCbLastKnownDeltaCycle:
        case vhpiCbRepLastKnownDeltaCycle:
        case vhpiCbStartOfAnalysis:
        case vhpiCbEndOfAnalysis:
        case vhpiCbStartOfElaboration:
        case vhpiCbEndOfElaboration:
        case vhpiCbStartOfTool:
        case vhpiCbEndOfTool:
            callback.data.obj = nullptr;
            callback.data.time = nullptr;
            callback.data.value = nullptr;
            callback.has_time = false;
            callback.has_value = false;
            if (dispatch_reason(data->reason) == vhpiCbNextTimeStep) {
                ensure_next_time();
            }
            break;
        case vhpiCbAfterDelay:
        case vhpiCbRepAfterDelay: {
            if (data->time == nullptr) {
                error(vhpiError, "missing time for an after-delay callback");
                return nullptr;
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
                error(vhpiError, "value-change callbacks need a signal");
                return nullptr;
            }
            callback.object = object;
            callback.data.obj = to_handle(object);
            callback.last = element_bits(*object);
            ensure_change_hook();
            break;
        }
        default:
            error(vhpiError,
                "unsupported callback reason " + std::to_string(data->reason));
            return nullptr;
        }
        callbacks_.push_back(std::move(stored));
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
        registration.at_slot_start = true;
        registration.callback
            = [this, target = &callback](
                  const runtime::SystemVerilogVpiCallbackEvent&) {
                  timed(target);
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

    void timed(Callback* const callback)
    {
        if (!live(callback)) {
            return;
        }
        callback->managed.reset();
        if (callback->repetitive) {
            (void)schedule_delay(*callback);
        }
        run(*callback);
        if (!callback->repetitive && live(callback)) {
            retire(callback);
        }
    }

    [[nodiscard]] bool live(const Callback* const callback) const
    {
        return std::ranges::any_of(callbacks_,
            [&](const auto& stored) { return stored.get() == callback; });
    }

    // A matured or removed callback keeps its handle valid but never runs
    // again.
    void retire(Callback* const callback)
    {
        if (callback->managed) {
            (void)simulation_.systemverilog_vpi_callbacks().remove_callback(
                *callback->managed);
            callback->managed.reset();
        }
        const auto found = std::ranges::find_if(callbacks_,
            [&](const auto& stored) { return stored.get() == callback; });
        if (found != callbacks_.end()) {
            callback->state = vhpiMature;
            retired_.push_back(std::move(*found));
            callbacks_.erase(found);
        }
    }

    [[nodiscard]] int remove_cb(const vhpiHandleT handle)
    {
        auto* const callback = callback_of(handle);
        if (callback == nullptr) {
            return 1;
        }
        retire(callback);
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
    void run(Callback& callback)
    {
        if (callback.state != vhpiEnable) {
            return;
        }
        auto data = callback.data;
        vhpiTimeT time { };
        if (callback.has_time) {
            time = to_vhpi_time(now_femtoseconds());
            data.time = &time;
        }
        auto value = callback.value;
        if (callback.has_value && callback.object != nullptr) {
            (void)read_value(*callback.object, value);
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
        std::vector<Callback*> selected;
        for (const auto& callback : callbacks_) {
            if (dispatch_reason(callback->data.reason) == reason
                && callback->data.reason != vhpiCbValueChange) {
                selected.push_back(callback.get());
            }
        }
        for (auto* const callback : selected) {
            if (!live(callback)) {
                continue;
            }
            run(*callback);
            if (!callback->repetitive && live(callback)) {
                retire(callback);
            }
        }
    }

    [[nodiscard]] bool has_callbacks(const int32_t reason) const
    {
        return std::ranges::any_of(callbacks_, [&](const auto& callback) {
            return dispatch_reason(callback->data.reason) == reason;
        });
    }

    // nvc fires end of initialization before the start of simulation
    // (IEEE 1076-2019 21.3.4.2, 21.3.6); signals hold their initial values.
    void start_of_simulation()
    {
        phase_ = vhpiSimulationPhase;
        dispatch(vhpiCbEndOfInitialization);
        dispatch(vhpiCbStartOfSimulation);
        // Time zero is the first time step (IEEE 1076-2019 14.7.5.2).
        dispatch(vhpiCbNextTimeStep);
    }

    void end_of_simulation()
    {
        if (simulation_ended_) {
            return;
        }
        simulation_ended_ = true;
        flush_changes();
        // The initialization is followed by at least one simulation cycle
        // (IEEE 1076-2019 14.7.5.2), even when no process is active in it.
        if (!cycled_ && !stopping_) {
            cycled_ = true;
            dispatch(vhpiCbEndOfProcesses);
            dispatch(vhpiCbLastKnownDeltaCycle);
            time_step_ended_ = true;
            dispatch(vhpiCbEndOfTimeStep);
            time_step_ended_ = false;
        }
        dispatch(vhpiCbEndOfSimulation);
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
        time_step_ended_ = false;
        dispatch(vhpiCbNextTimeStep);
        dispatch(vhpiCbStartOfNextCycle);
        if (has_callbacks(vhpiCbNextTimeStep)) {
            ensure_next_time();
        }
    }

    [[nodiscard]] static bool pending_now(const runtime::Scheduler& scheduler)
    {
        const auto next = scheduler.next_pending_time();
        return next && *next == scheduler.now();
    }

    void safe_point(runtime::Scheduler& scheduler,
        const runtime::SchedulerPhase phase)
    {
        if (phase == runtime::SchedulerPhase::update) {
            cycle_counted_ = false;
        }
        flush_changes();
        if (phase != runtime::SchedulerPhase::active || stopping_) {
            return;
        }
        cycled_ = true;
        // The processes of a simulation cycle have run.
        dispatch(vhpiCbEndOfProcesses);
        if (pending_now(scheduler)) {
            if (has_callbacks(vhpiCbStartOfNextCycle)) {
                dispatch(vhpiCbStartOfNextCycle);
            }
            return;
        }
        dispatch(vhpiCbLastKnownDeltaCycle);
        flush_changes();
        if (pending_now(scheduler)) {
            dispatch(vhpiCbStartOfNextCycle);
            return;
        }
        // No callback at the end of a time step may schedule another delta
        // cycle (IEEE 1076-2019 21.3.6).
        time_step_ended_ = true;
        dispatch(vhpiCbEndOfTimeStep);
        time_step_ended_ = false;
        if (has_callbacks(vhpiCbStartOfNextCycle)) {
            ensure_next_time();
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
            std::vector<Callback*> selected;
            for (const auto& callback : callbacks_) {
                if (callback->data.reason == vhpiCbValueChange
                    && callback->object != nullptr
                    && callback->object->signal
                    && changed.contains(*callback->object->signal)) {
                    selected.push_back(callback.get());
                }
            }
            for (auto* const callback : selected) {
                if (!live(callback)) {
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

    [[nodiscard]] int get_next_time(vhpiTimeT* const time)
    {
        if (time != nullptr) {
            *time = to_vhpi_time(now_femtoseconds());
        }
        return simulation_.has_pending() ? 0 : vhpiNoActivity;
    }

    [[nodiscard]] int control(const vhpiSimControlT command)
    {
        switch (command) {
        case vhpiStop:
        case vhpiFinish:
            stopping_ = true;
            simulation_.request_stop();
            return 0;
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
        for (const auto& instance : design_.instances()) {
            if (instance.parent) {
                continue;
            }
            const auto& specialization = design_.specializations().at(
                instance.specialization.value());
            if (specialization.language != semantic::Language::vhdl) {
                continue;
            }
            root_ = instance_object(instance, nullptr);
            break;
        }
        return root_;
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

    // A declared object of a region, created on first use.
    [[nodiscard]] Object* declared(Object& region,
        const vhdl::Declaration& declaration)
    {
        const auto key = lower(declaration.name);
        if (const auto found = region.children.find(key);
            found != region.children.end()) {
            return found->second;
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
        object->signal = simulation_.find_signal(
            region.path + "." + lower(declaration.name));
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
        const auto& expressions = hir_.expressions();
        const auto find = [&](const semantic::ExpressionId expression)
            -> const vhdl::Expression* {
            const auto index = static_cast<std::size_t>(expression.value());
            if (index < expressions.size()
                && expressions[index].id == expression) {
                return &expressions[index];
            }
            // Expression ids are shared across languages, so a VHDL
            // expression's position need not be its id.
            const auto found = std::ranges::find(
                expressions, expression, &vhdl::Expression::id);
            return found == expressions.end() ? nullptr : &*found;
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
            const auto part = name.substr(0U, split);
            if (is_region(*current)) {
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
        case vhpiType:
            return object->type == nullptr ? nullptr : to_handle(object->type);
        case vhpiBaseType: {
            auto* const type = is_type(*object) ? object : object->type;
            if (type == nullptr || type->type_data == nullptr) {
                return nullptr;
            }
            return to_handle(type->type_data->base);
        }
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
                || object->kind == vhpiIndexedNameK) {
                return to_handle(object->parent);
            }
            return nullptr;
        case vhpiSuffix:
            return object->suffix == nullptr ? nullptr
                                             : to_handle(object->suffix);
        case vhpiDesignUnit: {
            if (!is_region(*object) || object->unit == nullptr) {
                return nullptr;
            }
            const auto found = object->children.find("@unit");
            if (found != object->children.end()) {
                return to_handle(found->second);
            }
            const auto kind = object->unit->kind == vhdl::UnitKind::package
                ? vhpiPackDeclK
                : vhpiArchBodyK;
            auto* const unit = make_object(kind, object->unit->name, nullptr);
            unit->full_name = upper(object->unit->library) + "." + unit->name;
            unit->full_case_name = object->unit->library + "." + unit->case_name;
            unit->unit = object->unit;
            unit->entity = object->entity;
            object->children.emplace("@unit", unit);
            return to_handle(unit);
        }
        case vhpiPrimaryUnit: {
            // The entity declaration an architecture body is of (IEEE
            // 1076-2019 19.4.5).
            if (object->kind != vhpiArchBodyK || object->entity == nullptr) {
                return nullptr;
            }
            const auto found = object->children.find("@primary");
            if (found != object->children.end()) {
                return to_handle(found->second);
            }
            auto* const entity
                = make_object(vhpiEntityDeclK, object->entity->name, nullptr);
            entity->full_name
                = upper(object->entity->library) + "." + entity->name;
            entity->full_case_name
                = object->entity->library + "." + entity->case_name;
            entity->unit = object->entity;
            object->children.emplace("@primary", entity);
            return to_handle(entity);
        }
        case vhpiParamDecl: {
            const auto found = object->children.find("@param");
            return found == object->children.end() ? nullptr
                                                   : to_handle(found->second);
        }
        case vhpiParent:
        case vhpiUpperRegion:
            return object->parent == nullptr ? nullptr
                                             : to_handle(object->parent);
        default:
            error(vhpiError,
                "unsupported relation " + std::to_string(relation));
            return nullptr;
        }
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
            if (object == nullptr || !is_region(*object)) {
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
            declarations([](const vhdl::Declaration& d) {
                return d.form == Form::port || d.form == Form::signal
                    || d.form == Form::generic_constant
                    || d.form == Form::constant || d.form == Form::variable
                    || d.form == Form::type || d.form == Form::subtype;
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
            if (object != nullptr) {
                result = object->lists[ListKind::param_decls];
            }
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
            for (const auto& range : data.dimensions) {
                list.push_back(range_object(range));
            }
            if (data.dimensions.empty() && data.range
                && data.storage != Storage::array) {
                list.push_back(range_object(*data.range));
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

    [[nodiscard]] Object* range_object(const Range& range)
    {
        auto* const object = make_object(vhpiIntRangeK, "", nullptr);
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
        auto& object = static_cast<Object&>(*base);
        auto* const type = is_type(object) ? &object : object.type;
        switch (property) {
        case vhpiKindP:
            return object.kind;
        case vhpiCapabilitiesP:
            return vhpiProvidesHierarchy | vhpiProvidesStaticAccess
                | vhpiProvidesConnectivity | vhpiProvidesPostAnalysis
                | vhpiProvidesForeignModel;
        case vhpiArgcP:
            return static_cast<vhpiIntT>(arguments_.size());
        case vhpiIsUpP:
            if (object.kind == vhpiIntRangeK && object.type_data) {
                return object.type_data->range->descending ? 0 : 1;
            }
            break;
        case vhpiLeftBoundP:
        case vhpiRightBoundP:
            if (object.type_data && object.type_data->range) {
                const auto& range = *object.type_data->range;
                return static_cast<vhpiIntT>(
                    property == vhpiLeftBoundP ? range.left : range.right);
            }
            if (type != nullptr && type->type_data
                && !type->type_data->dimensions.empty()) {
                const auto& range = type->type_data->dimensions.front();
                return static_cast<vhpiIntT>(
                    property == vhpiLeftBoundP ? range.left : range.right);
            }
            break;
        case vhpiSizeP:
            if (type != nullptr && type->type_data) {
                return static_cast<vhpiIntT>(element_count(*type));
            }
            if (object.kind == vhpiIntRangeK && object.type_data) {
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
            if (is_region(object) && object.kind != vhpiRootInstK
                && object.kind != vhpiPackInstK) {
                return 0;
            }
            break;
        case vhpiIsGuardedP:
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
        case vhpiIsNullP:
            if (object.kind == vhpiIntRangeK && object.type_data
                && object.type_data->range) {
                return object.type_data->range->length() == 0 ? 1 : 0;
            }
            break;
        case vhpiIsDiscreteP:
            if (object.kind == vhpiIntRangeK) {
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
            }
            break;
        case vhpiKindStrP:
            scratch = kind_name(object.kind);
            break;
        case vhpiToolVersionP:
            scratch = "3";
            break;
        case vhpiStrValP:
            scratch = object.case_name;
            break;
        case vhpiLibLogicalNameP:
            scratch = object.type_data ? object.type_data->library : "WORK";
            break;
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

    [[nodiscard]] static std::string kind_name(const vhpiClassKindT kind)
    {
        switch (kind) {
        case vhpiRootInstK:
            return "vhpiRootInstK";
        case vhpiCompInstStmtK:
            return "vhpiCompInstStmtK";
        case vhpiSigDeclK:
            return "vhpiSigDeclK";
        case vhpiPortDeclK:
            return "vhpiPortDeclK";
        case vhpiGenericDeclK:
            return "vhpiGenericDeclK";
        case vhpiConstDeclK:
            return "vhpiConstDeclK";
        case vhpiVarDeclK:
            return "vhpiVarDeclK";
        case vhpiIndexedNameK:
            return "vhpiIndexedNameK";
        case vhpiSelectedNameK:
            return "vhpiSelectedNameK";
        case vhpiIntTypeDeclK:
            return "vhpiIntTypeDeclK";
        case vhpiEnumTypeDeclK:
            return "vhpiEnumTypeDeclK";
        case vhpiArrayTypeDeclK:
            return "vhpiArrayTypeDeclK";
        case vhpiRecordTypeDeclK:
            return "vhpiRecordTypeDeclK";
        case vhpiSubtypeDeclK:
            return "vhpiSubtypeDeclK";
        case vhpiPhysTypeDeclK:
            return "vhpiPhysTypeDeclK";
        case vhpiFloatTypeDeclK:
            return "vhpiFloatTypeDeclK";
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

    // The bits of an object's value: the element of its signal, or its
    // constant value.
    [[nodiscard]] std::optional<PackedLogic4> element_bits(const Object& object)
    {
        std::optional<PackedLogic4> whole;
        if (object.signal) {
            whole = simulation_.read_signal_snapshot(*object.signal);
        } else if (object.constant) {
            whole = *object.constant;
        } else if (object.call_slot || object.call_result) {
            whole = call_value(object);
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
        if (time_step_ended_) {
            error(vhpiError,
                "cannot create delta cycle during current simulation phase");
            return 1;
        }
        const bool call = object->call_slot || object->call_result;
        if ((!object->signal && !call) || object->type == nullptr
            || object->type->type_data == nullptr) {
            error(vhpiError, object->full_name + " cannot be updated");
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
            return 0;
        }
        auto whole = call
            ? call_value(*object).value_or(PackedLogic4 {})
            : simulation_.read_signal_snapshot(*object->signal);
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
        switch (mode) {
        case vhpiForce:
        case vhpiForcePropagate:
            simulation_.force_signal(*object->signal, std::move(whole));
            break;
        default:
            simulation_.deposit_signal(*object->signal, std::move(whole));
            break;
        }
        return 0;
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
    std::vector<std::unique_ptr<Callback>> callbacks_;
    std::vector<std::unique_ptr<Callback>> retired_;
    std::vector<std::unique_ptr<Iterator>> iterators_;
    std::deque<std::string> strings_;
    std::map<std::string, Object*, std::less<>> instances_;
    std::map<std::string, Object*, std::less<>> standard_types_;
    std::unordered_map<std::uint32_t, Object*> declared_types_;
    std::unordered_map<std::uint32_t, Object*> defined_types_;
    std::vector<Object*> argvs_;
    std::map<std::string, Object*, std::less<>> packages_;
    std::unordered_map<std::uint32_t, const vhdl::Unit*> declaration_units_;
    std::unordered_map<std::uint32_t, const elaboration::SignalInfo*>
        signal_infos_;
    std::vector<runtime::SystemVerilogVpiCallbackHandle> lifecycle_;
    std::optional<runtime::SystemVerilogVpiCallbackHandle> next_time_;
    std::set<SignalId> changed_;
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
    bool failed_ { };
    bool finished_ { };
    bool simulation_ended_ { };
    bool time_step_ended_ { };
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
