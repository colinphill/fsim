// SPDX-License-Identifier: Apache-2.0
#include "tcl_completion_adapter.hpp"

#include "tcl_command_catalog.hpp"
#include "tcl_internal.hpp"

#include "application_workspace_store.hpp"
#include "fsim/app/application.hpp"
#include "fsim/support/path.hpp"

#include <tcl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::app::tcl_detail {
namespace {

    using namespace tcl_completion;
    namespace fs = std::filesystem;
    namespace workspace = fsim::app::workspace;

    constexpr std::size_t kMaximumSymbols = 4096U;
    constexpr std::size_t kMaximumNamespaces = 256U;
    constexpr std::size_t kMaximumPathItems = 1024U;
    constexpr std::size_t kMaximumPathDirectories = 96U;
    constexpr std::size_t kMaximumDirectoryEntries = 128U;
    constexpr std::size_t kMaximumLibraries = 128U;
    constexpr std::size_t kMaximumSnapshots = 128U;
    constexpr std::size_t kMaximumCatalogUnits = 8192U;
    constexpr std::size_t kMaximumDebuggerEntities = 2048U;
    constexpr std::size_t kMaximumNamespaceDepth = 16U;

    class StableHash {
    public:
        void add(const std::string_view text)
        {
            for (const auto value : text) {
                m_value ^= static_cast<unsigned char>(value);
                m_value *= 1099511628211ULL;
            }
            m_value ^= 0xFFU;
            m_value *= 1099511628211ULL;
        }

        void add(const std::uint64_t value)
        {
            for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
                m_value ^= static_cast<unsigned char>((value >> shift) & 0xFFU);
                m_value *= 1099511628211ULL;
            }
            m_value ^= 0xFEU;
            m_value *= 1099511628211ULL;
        }

        [[nodiscard]] std::uint64_t value() const noexcept { return m_value; }

    private:
        std::uint64_t m_value { 14695981039346656037ULL };
    };

    struct OwnedItem {
        std::string text;
        std::string help;
        CandidateKind kind { CandidateKind::design_object };
        CapabilityMask required_capabilities { };
    };

    struct ArgumentStorage {
        std::string name;
        ArgumentDomain domain { ArgumentDomain::literal };
        bool optional { false };
        bool repeatable { false };
        std::vector<CompletionChoice> choices;

        [[nodiscard]] CompletionArgument view() const
        {
            return CompletionArgument {
                domain, optional, repeatable, name, choices
            };
        }
    };

    struct OptionStorage {
        std::string text;
        std::string help;
        std::optional<ArgumentDomain> value_domain;
        std::vector<CompletionChoice> value_choices;

        [[nodiscard]] CompletionOption view() const
        {
            return CompletionOption { text, help, value_domain, value_choices };
        }
    };

    struct SchemaStorage {
        std::vector<ArgumentStorage> argument_storage;
        std::vector<OptionStorage> option_storage;
        std::vector<CompletionArgument> arguments;
        std::vector<CompletionOption> options;

        void finish()
        {
            arguments.clear();
            arguments.reserve(argument_storage.size());
            for (const auto& argument : argument_storage) {
                arguments.push_back(argument.view());
            }
            options.clear();
            options.reserve(option_storage.size());
            for (const auto& option : option_storage) {
                options.push_back(option.view());
            }
        }
    };

    struct SubcommandStorage {
        std::string name;
        std::string usage;
        std::string help;
        CapabilityMask required_capabilities { };
        SchemaStorage schema;

        [[nodiscard]] CompletionSubcommand view() const
        {
            return CompletionSubcommand {
                name, usage, help, schema.arguments, schema.options,
                required_capabilities
            };
        }
    };

    struct CommandStorage {
        std::string name;
        std::string usage;
        std::string help;
        CapabilityMask required_capabilities { };
        bool callback_safe { false };
        SchemaStorage schema;
        std::vector<SubcommandStorage> subcommand_storage;
        std::vector<CompletionSubcommand> subcommands;

        void finish()
        {
            schema.finish();
            for (auto& subcommand : subcommand_storage) {
                subcommand.schema.finish();
            }
            subcommands.clear();
            subcommands.reserve(subcommand_storage.size());
            for (const auto& subcommand : subcommand_storage) {
                subcommands.push_back(subcommand.view());
            }
        }

        [[nodiscard]] CompletionCommand view() const
        {
            return CompletionCommand {
                name, usage, help, required_capabilities, callback_safe,
                schema.arguments, schema.options, subcommands
            };
        }
    };

    struct CollectedSnapshot {
        std::vector<OwnedItem> owned_namespaces;
        std::vector<OwnedItem> owned_procedures;
        std::vector<OwnedItem> owned_variables;
        std::vector<OwnedItem> owned_paths;
        std::vector<OwnedItem> owned_libraries;
        std::vector<OwnedItem> owned_snapshots;
        std::vector<OwnedItem> owned_definitions;
        std::vector<OwnedItem> owned_objects;
        std::vector<OwnedItem> owned_packages;
        std::vector<OwnedItem> owned_types;
        std::vector<OwnedItem> owned_debugger_entities;
        std::vector<CompletionItem> namespaces;
        std::vector<CompletionItem> procedures;
        std::vector<CompletionItem> variables;
        std::vector<CompletionItem> paths;
        std::vector<CompletionItem> libraries;
        std::vector<CompletionItem> snapshots;
        std::vector<CompletionItem> definitions;
        std::vector<CompletionItem> objects;
        std::vector<CompletionItem> packages;
        std::vector<CompletionItem> types;
        std::vector<CompletionItem> debugger_entities;
        std::vector<CommandStorage> command_storage;
        std::vector<CompletionCommand> commands;
        Generations generations;
        bool truncated { false };

        void finish_items(
            std::vector<OwnedItem>& owned,
            std::vector<CompletionItem>& views)
        {
            std::sort(owned.begin(), owned.end(), [](const OwnedItem& left, const OwnedItem& right) {
                return std::tie(left.text, left.kind, left.help)
                    < std::tie(right.text, right.kind, right.help);
            });
            owned.erase(std::unique(owned.begin(), owned.end(), [](const OwnedItem& left, const OwnedItem& right) {
                return left.text == right.text && left.kind == right.kind;
            }),
                owned.end());
            views.clear();
            views.reserve(owned.size());
            for (const auto& item : owned) {
                views.push_back(CompletionItem {
                    item.text, item.help, item.kind, item.required_capabilities });
            }
        }

        void finish()
        {
            finish_items(owned_namespaces, namespaces);
            finish_items(owned_procedures, procedures);
            finish_items(owned_variables, variables);
            finish_items(owned_paths, paths);
            finish_items(owned_libraries, libraries);
            finish_items(owned_snapshots, snapshots);
            finish_items(owned_definitions, definitions);
            finish_items(owned_objects, objects);
            finish_items(owned_packages, packages);
            finish_items(owned_types, types);
            finish_items(owned_debugger_entities, debugger_entities);

            for (auto& command : command_storage) {
                command.finish();
            }
            commands.clear();
            commands.reserve(command_storage.size());
            for (const auto& command : command_storage) {
                commands.push_back(command.view());
            }
        }

        [[nodiscard]] CompletionSnapshot view() const
        {
            CompletionSnapshot result;
            result.generations = generations;
            result.commands = commands;
            result.namespaces = namespaces;
            result.procedures = procedures;
            result.variables = variables;
            result.paths = paths;
            result.libraries = libraries;
            result.snapshots = snapshots;
            result.compiled_definitions = definitions;
            result.design_objects = objects;
            result.packages = packages;
            result.types = types;
            result.debugger_entities = debugger_entities;
            return result;
        }
    };

    ArgumentDomain completion_domain(const TclCompletionDomain domain)
    {
        switch (domain) {
        case TclCompletionDomain::source_path:
            return ArgumentDomain::source_path;
        case TclCompletionDomain::directory_path:
            return ArgumentDomain::directory_path;
        case TclCompletionDomain::library:
            return ArgumentDomain::library;
        case TclCompletionDomain::snapshot:
            return ArgumentDomain::snapshot;
        case TclCompletionDomain::compiled_definition:
            return ArgumentDomain::compiled_definition;
        case TclCompletionDomain::design_object:
            return ArgumentDomain::design_object;
        case TclCompletionDomain::debugger_entity:
            return ArgumentDomain::debugger_entity;
        case TclCompletionDomain::literal:
            return ArgumentDomain::literal;
        }
        return ArgumentDomain::literal;
    }

    CapabilityMask command_capability(const TclCommandCapability capability)
    {
        switch (capability) {
        case TclCommandCapability::workspace:
            return capability_mask(Capability::workspace);
        case TclCommandCapability::loaded_design:
            return capability_mask(Capability::loaded_design);
        case TclCommandCapability::simulation:
            return capability_mask(Capability::simulation);
        case TclCommandCapability::debugger:
            return capability_mask(Capability::debugger);
        }
        return 0U;
    }

    void map_arguments(
        SchemaStorage& destination,
        const std::span<const TclCommandArgument> arguments)
    {
        destination.argument_storage.reserve(arguments.size());
        destination.option_storage.reserve(arguments.size());
        for (const auto& argument : arguments) {
            if (!argument.name.empty() && argument.name.front() == '-') {
                OptionStorage option;
                option.text = argument.name;
                option.help = argument.name;
                option.help.append(" value");
                option.value_domain = completion_domain(argument.completion);
                option.value_choices.reserve(argument.choices.size());
                for (const auto choice : argument.choices) {
                    option.value_choices.push_back(CompletionChoice { choice, { } });
                }
                destination.option_storage.push_back(std::move(option));
                continue;
            }
            ArgumentStorage mapped;
            mapped.name = argument.name;
            mapped.domain = completion_domain(argument.completion);
            mapped.optional = argument.optional;
            mapped.repeatable = argument.repeatable;
            mapped.choices.reserve(argument.choices.size());
            for (const auto choice : argument.choices) {
                mapped.choices.push_back(CompletionChoice { choice, { } });
            }
            destination.argument_storage.push_back(std::move(mapped));
        }
    }

    std::vector<CommandStorage> command_descriptors()
    {
        const auto specs = command_specs();
        std::vector<CommandStorage> result;
        result.reserve(specs.size());
        for (const auto& spec : specs) {
            CommandStorage command;
            command.name = spec.name;
            command.usage = spec.usage;
            command.help = spec.help;
            command.required_capabilities = command_capability(spec.capability);
            command.callback_safe = spec.callback_safe;
            map_arguments(command.schema, spec.arguments);
            command.subcommand_storage.reserve(spec.subcommands.size());
            for (const auto& subcommand_spec : spec.subcommands) {
                SubcommandStorage subcommand;
                subcommand.name = subcommand_spec.name;
                subcommand.usage = subcommand_spec.usage;
                subcommand.help = subcommand_spec.help;
                subcommand.required_capabilities = command_capability(subcommand_spec.capability);
                map_arguments(subcommand.schema, subcommand_spec.arguments);
                command.subcommand_storage.push_back(std::move(subcommand));
            }
            result.push_back(std::move(command));
        }
        return result;
    }

    void append_item(
        CollectedSnapshot& result,
        std::vector<OwnedItem>& items,
        std::string text,
        std::string help,
        const CandidateKind kind,
        const CapabilityMask required = 0U,
        const std::size_t limit = kMaximumSymbols)
    {
        if (text.empty()) {
            return;
        }
        if (items.size() >= limit) {
            result.truncated = true;
            return;
        }
        items.push_back(OwnedItem {
            std::move(text), std::move(help), kind, required });
    }

    class TclObjectRef {
    public:
        explicit TclObjectRef(Tcl_Obj* object)
            : m_object(object)
        {
            if (m_object != nullptr) {
                Tcl_IncrRefCount(m_object);
            }
        }

        ~TclObjectRef()
        {
            if (m_object != nullptr) {
                Tcl_DecrRefCount(m_object);
            }
        }

        TclObjectRef(const TclObjectRef&) = delete;
        TclObjectRef& operator=(const TclObjectRef&) = delete;

        TclObjectRef(TclObjectRef&& other) noexcept
            : m_object(std::exchange(other.m_object, nullptr))
        {
        }

        TclObjectRef& operator=(TclObjectRef&& other) noexcept
        {
            if (this != &other) {
                if (m_object != nullptr) {
                    Tcl_DecrRefCount(m_object);
                }
                m_object = std::exchange(other.m_object, nullptr);
            }
            return *this;
        }

        [[nodiscard]] Tcl_Obj* get() const noexcept { return m_object; }

    private:
        Tcl_Obj* m_object { };
    };

    class InterpStateRestore {
    public:
        explicit InterpStateRestore(Tcl_Interp* interpreter)
            : m_interpreter(interpreter)
            , m_state(Tcl_SaveInterpState(interpreter, TCL_OK))
        {
        }

        ~InterpStateRestore()
        {
            if (m_state != nullptr) {
                static_cast<void>(Tcl_RestoreInterpState(m_interpreter, m_state));
            }
        }

        InterpStateRestore(const InterpStateRestore&) = delete;
        InterpStateRestore& operator=(const InterpStateRestore&) = delete;

    private:
        Tcl_Interp* m_interpreter { };
        Tcl_InterpState m_state { };
    };

    bool invoke_core_introspection(
        Tcl_Interp* interpreter,
        const char* command_name,
        const std::span<const std::string> arguments,
        std::vector<std::string>& result)
    {
        // This intentionally calls only Tcl's native core `info` and `namespace`
        // object procedures with fixed arguments. It never dispatches the edited
        // buffer. Embedders must keep these standard core commands native.
        if (interpreter == nullptr) {
            return false;
        }
        Tcl_CmdInfo command_info { };
        if (Tcl_GetCommandInfo(interpreter, command_name, &command_info) == 0
            || command_info.isNativeObjectProc == 0
            || (command_info.objProc == nullptr && command_info.objProc2 == nullptr)) {
            return false;
        }

        const auto argument_count = arguments.size() + 1U;
        if (argument_count > 8U) {
            return false;
        }
        std::vector<TclObjectRef> objects;
        objects.reserve(argument_count);
        objects.emplace_back(Tcl_NewStringObj(command_name, -1));
        for (const auto& argument : arguments) {
            if (argument.size() > static_cast<std::size_t>(TCL_SIZE_MAX)) {
                return false;
            }
            objects.emplace_back(Tcl_NewStringObj(
                argument.data(), static_cast<Tcl_Size>(argument.size())));
        }
        if (std::ranges::any_of(objects, [](const TclObjectRef& object) {
                return object.get() == nullptr;
            })) {
            return false;
        }
        std::vector<Tcl_Obj*> object_vector;
        object_vector.reserve(objects.size());
        for (const auto& object : objects) {
            object_vector.push_back(object.get());
        }

        InterpStateRestore restore(interpreter);
        int code = TCL_ERROR;
        if (command_info.isNativeObjectProc == 2 && command_info.objProc2 != nullptr) {
            code = command_info.objProc2(
                command_info.objClientData2,
                interpreter,
                static_cast<Tcl_Size>(object_vector.size()),
                object_vector.data());
        } else if (command_info.objProc != nullptr) {
            if (object_vector.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                return false;
            }
            code = command_info.objProc(
                command_info.objClientData,
                interpreter,
                static_cast<int>(object_vector.size()),
                object_vector.data());
        }
        if (code != TCL_OK) {
            return false;
        }

        Tcl_Size element_count = 0;
        Tcl_Obj** elements = nullptr;
        if (Tcl_ListObjGetElements(
                interpreter, Tcl_GetObjResult(interpreter), &element_count, &elements)
                != TCL_OK
            || element_count < 0) {
            return false;
        }
        const auto count = std::min<std::size_t>(
            static_cast<std::size_t>(element_count), kMaximumSymbols);
        result.clear();
        result.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            Tcl_Size byte_count = 0;
            const auto* value = Tcl_GetStringFromObj(
                elements[index], &byte_count);
            if (value != nullptr && byte_count >= 0) {
                result.emplace_back(value, static_cast<std::size_t>(byte_count));
            }
        }
        return true;
    }

    std::vector<std::string> core_list(
        Tcl_Interp* interpreter,
        const char* command_name,
        std::initializer_list<std::string> arguments)
    {
        std::vector<std::string> values;
        const std::vector<std::string> owned_arguments(arguments);
        static_cast<void>(invoke_core_introspection(
            interpreter, command_name, owned_arguments, values));
        return values;
    }

    void collect_tcl_symbols(CollectedSnapshot& result, Tcl_Interp* interpreter)
    {
        std::vector<std::string> pending { "::" };
        std::set<std::string, std::less<>> visited;
        while (!pending.empty() && visited.size() < kMaximumNamespaces) {
            auto name = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(name).second) {
                continue;
            }
            if (name != "::") {
                append_item(
                    result, result.owned_namespaces, name, "Tcl namespace",
                    CandidateKind::namespace_name);
            }

            const auto pattern = name == "::" ? std::string { "::*" } : name + "::*";
            for (auto& procedure : core_list(interpreter, "::info", { "procs", pattern })) {
                append_item(
                    result, result.owned_procedures, std::move(procedure),
                    "Tcl procedure", CandidateKind::procedure);
            }
            for (auto& variable : core_list(interpreter, "::info", { "vars", pattern })) {
                append_item(
                    result, result.owned_variables, std::move(variable),
                    "Tcl variable", CandidateKind::variable);
            }

            if (visited.size() >= kMaximumNamespaces) {
                result.truncated = true;
                break;
            }
            std::size_t namespace_depth = 0U;
            for (std::size_t index = 2U; index < name.size(); ++index) {
                if (name[index] == ':' && index + 1U < name.size()
                    && name[index + 1U] == ':') {
                    ++namespace_depth;
                    ++index;
                }
            }
            if (namespace_depth >= kMaximumNamespaceDepth) {
                result.truncated = true;
                continue;
            }
            auto children = core_list(interpreter, "::namespace", { "children", name });
            std::sort(children.begin(), children.end());
            for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
                if (visited.size() + pending.size() >= kMaximumNamespaces) {
                    result.truncated = true;
                    break;
                }
                pending.push_back(std::move(*iterator));
            }
        }
    }

    fs::path workspace_root(const TclContext& context)
    {
        if (!context.config.base_directory.empty()) {
            return context.config.base_directory;
        }
        if (!context.invocation.manifest.empty()) {
            return context.invocation.manifest.parent_path();
        }
        std::error_code error;
        const auto current = fs::current_path(error);
        return error ? fs::path { "." } : current;
    }

    std::string user_path(const fs::path& path, const fs::path& root)
    {
        auto normalized = path.lexically_normal();
        if (normalized.is_absolute()) {
            const auto relative = normalized.lexically_relative(root.lexically_normal());
            if (!relative.empty() && !relative.is_absolute()) {
                bool escapes_root = false;
                for (const auto& component : relative) {
                    if (component == "..") {
                        escapes_root = true;
                        break;
                    }
                }
                if (!escapes_root) {
                    normalized = relative;
                }
            }
        }
        return fsim::support::path_to_utf8(normalized);
    }

    bool should_skip_directory(const std::string_view name)
    {
        return name == ".git" || name == ".fsim" || name == "build"
            || name == "out" || name == "target";
    }

    void add_path(
        CollectedSnapshot& result,
        const fs::path& path,
        const fs::path& root,
        const bool directory)
    {
        if (result.owned_paths.size() >= kMaximumPathItems) {
            result.truncated = true;
            return;
        }
        const auto text = user_path(path, root);
        append_item(
            result, result.owned_paths, text,
            directory ? "Filesystem directory" : "Filesystem path",
            directory ? CandidateKind::directory : CandidateKind::path,
            capability_mask(Capability::workspace), kMaximumPathItems);
    }

    void scan_path_directory(
        CollectedSnapshot& result,
        const fs::path& directory,
        const fs::path& root,
        std::set<fs::path>& visited,
        std::vector<fs::path>& next_directories)
    {
        if (visited.size() >= kMaximumPathDirectories
            || result.owned_paths.size() >= kMaximumPathItems
            || !visited.insert(directory.lexically_normal()).second) {
            return;
        }
        std::error_code error;
        const fs::directory_iterator end;
        fs::directory_iterator iterator {
            directory, fs::directory_options::skip_permission_denied, error
        };
        std::size_t entry_count = 0U;
        while (!error && iterator != end && entry_count < kMaximumDirectoryEntries
            && result.owned_paths.size() < kMaximumPathItems) {
            const auto entry = *iterator;
            const auto name = fsim::support::path_to_utf8(entry.path().filename());
            const auto status = entry.symlink_status(error);
            if (!error && !fs::is_symlink(status)) {
                if (fs::is_directory(status)) {
                    if (!should_skip_directory(name)) {
                        add_path(result, entry.path(), root, true);
                        if (visited.size() + next_directories.size() < kMaximumPathDirectories) {
                            next_directories.push_back(entry.path());
                        }
                    }
                } else if (fs::is_regular_file(status)) {
                    add_path(result, entry.path(), root, false);
                }
            }
            if (!error) {
                iterator.increment(error);
            }
            ++entry_count;
        }
        if (error || iterator != end) {
            result.truncated = true;
        }
    }

    void collect_paths(CollectedSnapshot& result, const TclContext& context, const fs::path& root)
    {
        std::vector<fs::path> directories { root };
        for (const auto& source_set : context.config.source_sets) {
            for (const auto& file : source_set.files) {
                add_path(result, file, root, false);
                directories.push_back(file.parent_path().empty() ? root : file.parent_path());
            }
            for (const auto& directory : source_set.include_directories) {
                add_path(result, directory, root, true);
                directories.push_back(directory);
            }
        }
        for (const auto& file : context.invocation.files) {
            add_path(result, file, root, false);
            directories.push_back(file.parent_path().empty() ? root : file.parent_path());
        }
        std::sort(directories.begin(), directories.end());
        directories.erase(std::unique(directories.begin(), directories.end()), directories.end());
        std::set<fs::path> visited;
        std::vector<fs::path> next_directories;
        for (const auto& directory : directories) {
            scan_path_directory(result, directory, root, visited, next_directories);
        }
        while (!next_directories.empty() && visited.size() < kMaximumPathDirectories
            && result.owned_paths.size() < kMaximumPathItems) {
            auto next = std::move(next_directories);
            next_directories.clear();
            for (const auto& directory : next) {
                scan_path_directory(result, directory, root, visited, next_directories);
                if (visited.size() >= kMaximumPathDirectories
                    || result.owned_paths.size() >= kMaximumPathItems) {
                    break;
                }
            }
        }
    }

    std::string object_kind_name(const semantic::design::ObjectKind kind)
    {
        using semantic::design::ObjectKind;
        switch (kind) {
        case ObjectKind::signal:
            return "signal";
        case ObjectKind::string:
            return "string";
        case ObjectKind::container:
            return "container";
        case ObjectKind::protected_object:
            return "protected object";
        case ObjectKind::protected_member:
            return "protected member";
        case ObjectKind::systemc_module:
            return "SystemC module";
        case ObjectKind::systemc_port:
            return "SystemC port";
        case ObjectKind::systemc_event:
            return "SystemC event";
        case ObjectKind::systemc_channel:
            return "SystemC channel";
        case ObjectKind::systemc_signal:
            return "SystemC signal";
        case ObjectKind::systemc_export:
            return "SystemC export";
        }
        return "design object";
    }

    bool is_package(const semantic::UnitKind kind)
    {
        return kind == semantic::UnitKind::systemverilog_package
            || kind == semantic::UnitKind::vhdl_package;
    }

    void collect_loaded_design(CollectedSnapshot& result, const TclContext& context)
    {
        const auto* design_ir = current_design_ir(context);
        if (design_ir != nullptr) {
            for (const auto& object : design_ir->objects()) {
                const auto path = std::string { design_ir->path(object.path) };
                const auto kind = object_kind_name(object.kind);
                auto help = "Loaded " + kind;
                if (!object.external_type.empty()) {
                    help.append(": ");
                    help.append(object.external_type);
                }
                append_item(
                    result, result.owned_objects, path, help,
                    CandidateKind::design_object,
                    capability_mask(Capability::loaded_design));
                append_item(
                    result, result.owned_debugger_entities, path, help,
                    CandidateKind::debugger_entity,
                    capability_mask(Capability::debugger), kMaximumDebuggerEntities);
            }
        }

        const semantic::Model* model = nullptr;
        if (context.simulation) {
            model = &context.simulation->semantics();
        } else if (context.built) {
            model = &context.built->semantics;
        }
        if (model == nullptr) {
            return;
        }
        for (const auto& unit : model->units()) {
            std::string help = "Loaded compiled design unit";
            if (unit.kind == semantic::UnitKind::systemverilog_package) {
                help = "Loaded SystemVerilog package";
            } else if (unit.kind == semantic::UnitKind::vhdl_package) {
                help = "Loaded VHDL package";
            }
            append_item(
                result, result.owned_definitions, unit.name,
                std::move(help),
                CandidateKind::compiled_definition,
                capability_mask(Capability::loaded_design));
            if (is_package(unit.kind)) {
                append_item(
                    result, result.owned_packages, unit.name,
                    "Loaded HDL package", CandidateKind::package,
                    capability_mask(Capability::loaded_design));
            }
        }
        for (const auto& type : model->types()) {
            append_item(
                result, result.owned_types, type.name,
                "Loaded HDL type", CandidateKind::type,
                capability_mask(Capability::loaded_design));
        }
    }

    void collect_workspace_store(
        CollectedSnapshot& result,
        const TclContext& context,
        const fs::path& root,
        StableHash& catalog_hash)
    {
        workspace::Store store { root };
        std::string error;
        const auto add_configured_library = [&result, &context, &catalog_hash](
                                                const std::string_view name) {
            if (name.empty()) {
                return;
            }
            append_item(
                result, result.owned_libraries, std::string { name },
                "Configured workspace library", CandidateKind::library,
                capability_mask(Capability::workspace));
            catalog_hash.add(name);
            if (context.references) {
                catalog_hash.add(context.references->catalog_generation(name));
            }
        };
        add_configured_library(context.invocation.library);
        for (const auto& source_set : context.config.source_sets) {
            add_configured_library(source_set.library);
        }
        for (const auto& mapping : context.config.library_mappings) {
            add_configured_library(mapping.library);
        }
        const auto locations = store.library_locations(error);
        if (locations) {
            const auto limit = std::min(locations->size(), kMaximumLibraries);
            if (locations->size() > limit) {
                result.truncated = true;
            }
            for (std::size_t index = 0U; index < limit; ++index) {
                const auto& location = (*locations)[index];
                append_item(
                    result, result.owned_libraries, location.name,
                    location.mapped ? "Mapped workspace library" : "Workspace library",
                    CandidateKind::library, capability_mask(Capability::workspace));
                catalog_hash.add(location.name);
                if (context.references) {
                    catalog_hash.add(context.references->catalog_generation(location.name));
                }
                const auto catalog = store.read_library(location.name, error);
                if (!catalog) {
                    continue;
                }
                std::size_t unit_count = 0U;
                for (const auto& artifact : catalog->artifacts) {
                    catalog_hash.add(artifact.id);
                    catalog_hash.add(artifact.fingerprint);
                    for (const auto& owned : artifact.units) {
                        if (unit_count >= kMaximumCatalogUnits) {
                            result.truncated = true;
                            break;
                        }
                        ++unit_count;
                        catalog_hash.add(owned.unit.name);
                        catalog_hash.add(owned.unit.kind);
                        auto help = "Compiled " + owned.unit.kind + " in " + location.name;
                        append_item(
                            result, result.owned_definitions, owned.unit.name, help,
                            CandidateKind::compiled_definition,
                            capability_mask(Capability::workspace));
                        if (owned.unit.kind.find("package") != std::string::npos) {
                            append_item(
                                result, result.owned_packages, owned.unit.name, help,
                                CandidateKind::package,
                                capability_mask(Capability::workspace));
                        }
                    }
                    if (unit_count >= kMaximumCatalogUnits) {
                        break;
                    }
                }
            }
        }

        const auto snapshot_directory = store.managed_directory() / "snapshots";
        std::error_code code;
        if (!fs::is_directory(snapshot_directory, code) || code) {
            return;
        }
        fs::directory_iterator iterator {
            snapshot_directory, fs::directory_options::skip_permission_denied, code
        };
        const fs::directory_iterator end;
        std::size_t count = 0U;
        while (!code && iterator != end && count < kMaximumSnapshots) {
            const auto name = fsim::support::path_to_utf8(iterator->path().filename());
            const auto status = iterator->symlink_status(code);
            if (!code && fs::is_directory(status) && !fs::is_symlink(status)) {
                const auto snapshot = store.read_snapshot(name, error);
                if (snapshot) {
                    append_item(
                        result, result.owned_snapshots, name,
                        "Workspace snapshot", CandidateKind::snapshot,
                        capability_mask(Capability::workspace));
                }
            }
            if (!code) {
                iterator.increment(code);
            }
            ++count;
        }
        if (count >= kMaximumSnapshots || code) {
            result.truncated = true;
        }
    }

    void collect_debugger(CollectedSnapshot& result, const TclContext& context, StableHash& debugger_hash)
    {
        if (!context.debugger) {
            return;
        }
        const auto status = context.debugger->status();
        debugger_hash.add(static_cast<std::uint64_t>(status.time));
        debugger_hash.add(status.delta);
        debugger_hash.add(status.scope);
        debugger_hash.add(status.stop_reason);
        debugger_hash.add(status.finished ? 1U : 0U);
        debugger_hash.add(status.poisoned ? 1U : 0U);
        if (status.breakpoint_id) {
            debugger_hash.add(*status.breakpoint_id);
        }
        for (const auto& breakpoint : context.debugger->breakpoints()) {
            const auto help = "Breakpoint " + std::to_string(breakpoint.id)
                + " (" + breakpoint.kind + ")";
            append_item(
                result, result.owned_debugger_entities, breakpoint.path, help,
                CandidateKind::debugger_entity,
                capability_mask(Capability::debugger), kMaximumDebuggerEntities);
            debugger_hash.add(breakpoint.path);
            debugger_hash.add(breakpoint.kind);
            debugger_hash.add(breakpoint.id);
        }
        for (const auto& watch : context.debugger->watches()) {
            const auto help = "Watch " + std::to_string(watch.id);
            append_item(
                result, result.owned_debugger_entities, watch.path, help,
                CandidateKind::debugger_entity,
                capability_mask(Capability::debugger), kMaximumDebuggerEntities);
            debugger_hash.add(watch.path);
            debugger_hash.add(watch.kind);
            debugger_hash.add(watch.id);
        }
        for (const auto& frame : context.debugger->frames()) {
            append_item(
                result, result.owned_debugger_entities, frame.process,
                "Debugger process frame", CandidateKind::debugger_entity,
                capability_mask(Capability::debugger), kMaximumDebuggerEntities);
            append_item(
                result, result.owned_debugger_entities, frame.scope,
                "Debugger frame scope", CandidateKind::debugger_entity,
                capability_mask(Capability::debugger), kMaximumDebuggerEntities);
            debugger_hash.add(frame.process);
            debugger_hash.add(frame.scope);
            debugger_hash.add(static_cast<std::uint64_t>(frame.index));
            debugger_hash.add(frame.process_id);
            for (const auto& local : frame.locals) {
                append_item(
                    result, result.owned_debugger_entities, local.name,
                    "Debugger local " + local.type, CandidateKind::debugger_entity,
                    capability_mask(Capability::debugger), kMaximumDebuggerEntities);
                debugger_hash.add(local.name);
                debugger_hash.add(local.type);
            }
        }
    }

    void hash_items(
        StableHash& hash,
        const std::vector<OwnedItem>& items,
        const bool include_help = false)
    {
        std::vector<const OwnedItem*> ordered;
        ordered.reserve(items.size());
        for (const auto& item : items) {
            ordered.push_back(&item);
        }
        std::sort(ordered.begin(), ordered.end(), [](const OwnedItem* left, const OwnedItem* right) {
            return std::tie(left->text, left->help, left->kind)
                < std::tie(right->text, right->help, right->kind);
        });
        for (const auto* item : ordered) {
            hash.add(item->text);
            hash.add(static_cast<std::uint64_t>(item->kind));
            if (include_help) {
                hash.add(item->help);
            }
        }
    }

    CollectedSnapshot collect_snapshot(const TclContext& context)
    {
        CollectedSnapshot result;
        StableHash catalog_hash;
        StableHash workspace_hash;
        StableHash session_hash;
        StableHash debugger_hash;
        result.command_storage = command_descriptors();

        collect_tcl_symbols(result, context.interpreter);
        const auto root = workspace_root(context);
        collect_paths(result, context, root);
        collect_workspace_store(result, context, root, catalog_hash);
        collect_loaded_design(result, context);
        collect_debugger(result, context, debugger_hash);

        hash_items(workspace_hash, result.owned_namespaces, true);
        hash_items(workspace_hash, result.owned_procedures, true);
        hash_items(workspace_hash, result.owned_variables, true);
        hash_items(workspace_hash, result.owned_paths, true);
        hash_items(workspace_hash, result.owned_libraries, true);
        hash_items(workspace_hash, result.owned_snapshots, true);
        if (context.references) {
            workspace_hash.add(context.references->workspace_generation());
        }
        hash_items(session_hash, result.owned_objects, true);
        hash_items(session_hash, result.owned_definitions, true);
        hash_items(session_hash, result.owned_packages);
        hash_items(session_hash, result.owned_types);
        if (context.references) {
            session_hash.add(context.references->loaded_session_generation());
        }
        result.generations.catalog = catalog_hash.value();
        result.generations.workspace = workspace_hash.value();
        result.generations.session = session_hash.value();
        result.generations.debugger = debugger_hash.value();
        result.finish();
        return result;
    }

    CapabilityMask available_capabilities(const TclContext& context)
    {
        auto result = capability_mask(Capability::workspace);
        if (current_design_ir(context) != nullptr) {
            result = result | Capability::loaded_design;
        }
        if (context.simulation) {
            result = result | Capability::simulation;
        }
        if (context.debugger) {
            result = result | Capability::debugger;
        }
        return result;
    }

} // namespace

tcl_completion::CompletionResult complete_tcl(
    TclContext& context,
    const std::string_view buffer,
    const std::size_t cursor_byte,
    const tcl_completion::RequestContext request_context)
{
    if (!synchronize_workspace(context, context.interpreter)) {
        tcl_completion::CompletionResult failed;
        failed.status = tcl_completion::CompletionStatus::stale_snapshot;
        return failed;
    }
    auto collected = collect_snapshot(context);
    auto snapshot = collected.view();
    CompletionRequest request {
        buffer,
        cursor_byte,
        request_context,
        available_capabilities(context),
        collected.generations,
        64U
    };
    auto result = complete(request, snapshot);
    result.candidates_truncated = result.candidates_truncated || collected.truncated;
    return result;
}

tcl_completion::Generations tcl_completion_generations(TclContext& context)
{
    if (!synchronize_workspace(context, context.interpreter)) {
        return { };
    }
    return collect_snapshot(context).generations;
}

} // namespace fsim::app::tcl_detail
