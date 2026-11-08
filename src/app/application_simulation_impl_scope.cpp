// SPDX-License-Identifier: Apache-2.0
// The interactive scope system tasks $scope, $showscopes and $showvars
// (IEEE 1800-2023 D.11, D.12, D.13; IEEE 1364-2005 C.10-C.12). The compiled
// kernel and the reference interpreter both reach run_scope_task through the
// same host-call text, so the two engines print the same lines.
#include "application_simulation_internal.hpp"

#include "fsim/runtime/systemverilog_scalar.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fsim::app {
namespace {

    using ObjectKind = runtime::SystemVerilogVpiObjectKind;
    using ObjectInfo = runtime::SystemVerilogVpiObjectInfo;

    // The scopes $scope may select and $showscopes lists: module (interface
    // and program) instances, tasks, functions and named blocks (D.11,
    // D.12). A generate block is not one, nor are the root and packages.
    [[nodiscard]] std::optional<std::string_view> scope_label(
        const ObjectKind kind) noexcept
    {
        switch (kind) {
        // A top-level module instance is published as the root kind
        // (vpi_instance_kind with a configured root); it is a module
        // instance for $scope(top) (D.11).
        case ObjectKind::Root:
        case ObjectKind::Module:
            return "module";
        case ObjectKind::Interface:
            return "interface";
        case ObjectKind::Program:
            return "program";
        case ObjectKind::Task:
            return "task";
        case ObjectKind::Function:
            return "function";
        case ObjectKind::Begin:
        case ObjectKind::Fork:
            return "named block";
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] std::string_view net_name(
        const runtime::SystemVerilogVpiNetKind kind) noexcept
    {
        using Kind = runtime::SystemVerilogVpiNetKind;
        switch (kind) {
        case Kind::Tri:
            return "tri";
        case Kind::Wand:
            return "wand";
        case Kind::Wor:
            return "wor";
        case Kind::Tri0:
            return "tri0";
        case Kind::Tri1:
            return "tri1";
        case Kind::Supply0:
            return "supply0";
        case Kind::Supply1:
            return "supply1";
        case Kind::Uwire:
            return "uwire";
        case Kind::TriReg:
            return "trireg";
        case Kind::TriAnd:
            return "triand";
        case Kind::TriOr:
            return "trior";
        case Kind::Wire:
        case Kind::None:
            return "wire";
        }
        return "wire";
    }

    [[nodiscard]] std::string_view strength_name(
        const runtime::simir::StrengthRank rank) noexcept
    {
        using Rank = runtime::simir::StrengthRank;
        switch (rank) {
        case Rank::highz:
            return "highz";
        case Rank::small:
            return "small";
        case Rank::medium:
            return "medium";
        case Rank::weak:
            return "weak";
        case Rank::large:
            return "large";
        case Rank::pull:
            return "pull";
        case Rank::strong:
            return "strong";
        case Rank::supply:
            return "supply";
        }
        return "strong";
    }

    // The declared kind of a variable as a Verilog or SystemVerilog user
    // wrote it, so far as the published type tells it.
    [[nodiscard]] std::string variable_label(const ObjectInfo& object)
    {
        using Category = runtime::SystemVerilogVpiValueCategory;
        if (!object.type) {
            return "variable";
        }
        switch (object.type->category) {
        case Category::Bit2:
            return "bit";
        case Category::Logic4:
        case Category::Logic9:
            // D.13 calls a variable of four-state values a reg variable
            // whichever keyword (reg or logic) declared it.
            return "reg";
        case Category::Integer2:
            return "int";
        case Category::Integer4:
            return "integer";
        case Category::Real:
            return "real";
        case Category::ShortReal:
            return "shortreal";
        case Category::Time:
            return "time";
        case Category::String:
            return "string";
        case Category::Event:
            return "event";
        case Category::None:
            break;
        }
        return "variable";
    }

    // The declared range of a vector, `[left:right]`, empty for a scalar.
    [[nodiscard]] std::string range_text(const ObjectInfo& object)
    {
        if (!object.type) {
            return { };
        }
        using Category = runtime::SystemVerilogVpiValueCategory;
        if (object.type->category == Category::Real
            || object.type->category == Category::ShortReal
            || object.type->category == Category::Time
            || object.type->category == Category::String
            || object.type->category == Category::Event) {
            return { };
        }
        const auto& descriptor = object.type->descriptor;
        if (descriptor && !descriptor->ranges.empty()) {
            const auto& range = descriptor->ranges.front();
            return "[" + std::to_string(range.left) + ":"
                + std::to_string(range.right) + "]";
        }
        if (object.type->width > 1U) {
            return "[" + std::to_string(object.type->width - 1U) + ":0]";
        }
        return { };
    }

    [[nodiscard]] std::string lowered(std::string text)
    {
        for (auto& character : text) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        return text;
    }

    [[nodiscard]] std::string_view parent_path(
        const std::string_view path) noexcept
    {
        const auto separator = path.rfind('.');
        return separator == std::string_view::npos
            ? std::string_view { }
            : path.substr(0U, separator);
    }

} // namespace

std::string Simulation::Impl::current_interactive_scope() const
{
    // Before any change the interactive scope is the first top-level module
    // (IEEE 1800-2023 D.11, 38.4).
    return interactive_scope.empty() ? std::string { built.design_ir.top() }
                                     : interactive_scope;
}

Simulation::Impl::ScopeCatalog Simulation::Impl::scope_catalog()
{
    if (vpi_runtime_updates_enabled) {
        return { vpi_registry.get(), &vpi_handle_signals, &vpi_scalar_kinds,
            &vpi_string_handles };
    }
    if (!scope_design) {
        // A run without a foreign interface publishes no VPI design; the
        // scope tasks build it once, on first use, so a design without them
        // pays nothing.
        scope_design.emplace(make_systemverilog_vpi_design(built, *interpreter));
        scope_design->registry->set_child_materializer(
            [this](const fsim_vpi_handle_v1 parent) {
                const auto deferred = scope_design->deferred.find(parent);
                if (deferred == scope_design->deferred.end()) {
                    return;
                }
                const auto children = std::move(deferred->second);
                scope_design->deferred.erase(deferred);
                (void)materialize_systemverilog_vpi_children(
                    *scope_design->registry, built, *interpreter, parent,
                    children);
            });
    }
    return { scope_design->registry.get(), &scope_design->handles,
        &scope_design->scalar_kinds, &scope_design->string_handles };
}

void Simulation::Impl::run_scope_task(
    const runtime::simir::ProcessId process,
    const std::string_view payload,
    const SimulationTick time,
    const std::uint64_t delta)
{
    const auto separator = payload.find('|');
    const auto task = payload.substr(0U, separator);
    const auto second = separator == std::string_view::npos
        ? std::string_view::npos
        : payload.find('|', separator + 1U);
    const auto lexical = second == std::string_view::npos
        ? std::string_view { }
        : payload.substr(separator + 1U, second - separator - 1U);
    const auto argument = second == std::string_view::npos
        ? std::string_view { }
        : payload.substr(second + 1U);
    const auto emit = [&](const std::string& line) {
        if (output_hook) {
            output_hook(process, line, true, time, delta);
        }
    };
    const auto fail = [&](const std::string& message) {
        throw std::runtime_error {
            "$" + std::string { task } + ": " + message
        };
    };

    // The instance the calling process is in; hierarchical names resolve
    // from there outward (IEEE 1800-2023 23.8).
    const auto runtime_process = interpreter->design_process(process);
    const auto occurrence = std::ranges::find(built.design_ir.processes(),
        runtime_process, &semantic::design::ProcessOccurrence::runtime_index);
    if (occurrence == built.design_ir.processes().end()) {
        fail("the calling process has no DesignIR occurrence");
    }
    const auto& specialization = built.design_ir.specializations().at(
        occurrence->specialization.value());
    const auto calling_path = std::string { built.design_ir.path(
        built.design_ir.instances().at(specialization.instance.value()).path) };

    const bool inside_instance = lexical == calling_path
        || (lexical.size() > calling_path.size()
            && lexical.starts_with(calling_path)
            && lexical[calling_path.size()] == '.');
    const auto lexical_path
        = inside_instance ? std::string { lexical } : calling_path;

    auto catalog = scope_catalog();
    auto& registry = *catalog.registry;

    // The object a hierarchical name denotes: found from the calling scope
    // outward, then as an absolute name from a top-level module, and for
    // `fallback_scope` last, relative to the interactive scope.
    const auto resolve = [&](std::string_view name,
                             const std::string_view fallback_scope)
        -> std::optional<ObjectInfo> {
        constexpr std::string_view root_prefix { "$root." };
        const bool rooted = name.starts_with(root_prefix);
        if (rooted) {
            name.remove_prefix(root_prefix.size());
        }
        const auto find = [&](const std::string& path)
            -> std::optional<ObjectInfo> {
            const auto found = registry.find(path);
            return found ? found.value : std::nullopt;
        };
        if (!rooted) {
            // From the calling task or named block outward (23.8).
            auto prefix = lexical_path;
            while (!prefix.empty()) {
                if (auto found = find(prefix + "." + std::string { name })) {
                    return found;
                }
                prefix = std::string { parent_path(prefix) };
            }
        }
        if (auto found = find(std::string { name })) {
            return found;
        }
        if (!fallback_scope.empty()) {
            return find(std::string { fallback_scope } + "."
                + std::string { name });
        }
        return std::nullopt;
    };

    const auto children_of = [&](const fsim_vpi_handle_v1 parent) {
        std::vector<ObjectInfo> children;
        const auto iterator = registry.iterate_children(parent);
        if (!iterator) {
            return children;
        }
        for (;;) {
            const auto next = registry.scan(iterator.value);
            if (!next) {
                break;
            }
            if (const auto info = registry.lookup(next.value)) {
                children.push_back(*info.value);
            }
        }
        (void)registry.release_iterator(iterator.value);
        return children;
    };

    if (task == "scope") {
        // $scope(hierarchical_name) (D.11) names a module instance, task,
        // function or named block; the name is resolved here, at run time,
        // because it may reach into other instances.
        const auto target = resolve(argument, { });
        if (!target) {
            fail("'" + std::string { argument }
                + "' does not name a scope of the design");
        }
        if (!scope_label(target->kind)) {
            fail("'" + std::string { argument }
                + "' is not a module instance, task, function or named "
                  "block");
        }
        // The same announcement as vpi_control(vpiSetInteractiveScope): the
        // VPI scope-change callbacks, the PLI misctf reason_scope, the VHPI
        // and the debugger all hear of it, and the hook above records it.
        for (std::size_t index = 0U; index < lifecycle_hooks.size(); ++index) {
            lifecycle_hooks[index](Simulation::LifecycleEvent::scope_change,
                target->full_name);
        }
        return;
    }

    const auto scope_name = current_interactive_scope();

    if (task == "showscopes") {
        // $showscopes (D.12): the scopes defined at the current level, and
        // with a nonzero argument everything in or below it. Sorted by name
        // so the listing does not depend on declaration order.
        const bool all = argument == "1";
        const auto current = registry.find(scope_name);
        if (!current) {
            fail("the interactive scope '" + scope_name
                + "' is not in the design");
        }
        std::vector<ObjectInfo> listed;
        std::vector<fsim_vpi_handle_v1> pending { current.value->handle };
        while (!pending.empty()) {
            const auto parent = pending.back();
            pending.pop_back();
            for (auto& child : children_of(parent)) {
                // Below the current level the listing goes through generate
                // blocks, which are not scopes themselves, to what they hold.
                if (all
                    && (scope_label(child.kind)
                        || child.kind == ObjectKind::GenerateScope)) {
                    pending.push_back(child.handle);
                }
                if (!scope_label(child.kind)) {
                    continue;
                }
                listed.push_back(std::move(child));
            }
        }
        std::ranges::sort(listed, {}, &ObjectInfo::full_name);
        emit(std::string { all ? "$showscopes: scopes in or below "
                               : "$showscopes: scopes at " }
            + scope_name);
        if (listed.empty()) {
            emit("  (none)");
        }
        for (const auto& scope : listed) {
            emit("  " + std::string { *scope_label(scope.kind) } + " "
                + scope.full_name);
        }
        return;
    }

    // $showvars [ (list_of_variables) ] (D.13): the status of the variables
    // and nets of the current scope, or of those listed. A bit-select or
    // part-select names its whole vector (the lowering keeps the name).
    std::vector<ObjectInfo> shown;
    const bool listed_names = !argument.empty();
    if (listed_names) {
        std::size_t begin = 0U;
        while (begin <= argument.size()) {
            auto end = argument.find('\x1e', begin);
            if (end == std::string_view::npos) {
                end = argument.size();
            }
            const auto name = argument.substr(begin, end - begin);
            const auto found = resolve(name, scope_name);
            if (!found) {
                fail("'" + std::string { name }
                    + "' does not name a variable or net of the design");
            }
            shown.push_back(*found);
            begin = end + 1U;
        }
    } else {
        const auto current = registry.find(scope_name);
        if (!current) {
            fail("the interactive scope '" + scope_name
                + "' is not in the design");
        }
        for (auto& child : children_of(current.value->handle)) {
            // "@static:N" is the storage of a callable's static locals,
            // not a variable the user declared in this scope.
            if (child.name.starts_with('@')) {
                continue;
            }
            if (child.kind == ObjectKind::Net
                || child.kind == ObjectKind::Variable
                || child.kind == ObjectKind::Memory
                || child.kind == ObjectKind::Array
                || child.kind == ObjectKind::NamedEvent) {
                shown.push_back(std::move(child));
            }
        }
        std::ranges::sort(shown, {}, &ObjectInfo::full_name);
    }
    emit(listed_names ? std::string { "$showvars: listed variables" }
                      : "$showvars: variables in " + scope_name);
    if (shown.empty()) {
        emit("  (none)");
    }
    for (const auto& object : shown) {
        std::string line = "  ";
        switch (object.kind) {
        case ObjectKind::Net:
            line += object.type ? net_name(object.type->net_kind) : "wire";
            break;
        case ObjectKind::Variable:
            line += variable_label(object);
            break;
        case ObjectKind::Memory:
            line += "memory";
            break;
        case ObjectKind::Array:
            line += "array";
            break;
        case ObjectKind::NamedEvent:
            line += "event";
            break;
        default:
            fail("'" + object.full_name + "' is not a variable or net");
        }
        if (const auto range = range_text(object); !range.empty()) {
            line += " " + range;
        }
        line += " " + object.full_name;
        const auto signal = catalog.signals->find(object.handle);
        if (signal != catalog.signals->end()) {
            const auto value = interpreter->signal_value_snapshot(signal->second);
            const auto scalar = catalog.scalar_kinds->find(signal->second);
            const auto kind = scalar != catalog.scalar_kinds->end()
                ? scalar->second
                : runtime::SystemVerilogScalarKind::None;
            line += " = ";
            if (kind != runtime::SystemVerilogScalarKind::None
                && kind != runtime::SystemVerilogScalarKind::Chandle) {
                const auto decoded
                    = runtime::decode_systemverilog_scalar_payload(value, kind);
                std::ostringstream text;
                if (decoded && (kind == runtime::SystemVerilogScalarKind::Real
                        || kind == runtime::SystemVerilogScalarKind::Realtime)) {
                    text << *decoded.value.as_real();
                } else if (decoded
                    && kind == runtime::SystemVerilogScalarKind::ShortReal) {
                    text << *decoded.value.as_shortreal();
                } else if (decoded
                    && kind == runtime::SystemVerilogScalarKind::Time) {
                    text << *decoded.value.as_time();
                } else {
                    text << value.width() << "'b"
                         << lowered(value.to_msb_string());
                }
                line += text.str();
            } else {
                line += std::to_string(value.width()) + "'b"
                    + lowered(value.to_msb_string());
            }
            if (object.kind == ObjectKind::Net) {
                // The strongest active zero and one contributions.
                const auto strength
                    = interpreter->signal_strength(signal->second);
                line += " strength0=" + std::string { strength_name(strength.zero) }
                    + " strength1=" + std::string { strength_name(strength.one) };
            }
        } else if (const auto text = catalog.strings->find(object.handle);
                   text != catalog.strings->end()) {
            line += " = \"" + interpreter->string_object_value(text->second)
                + "\"";
        }
        emit(line);
    }
}

} // namespace fsim::app
