// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"

#include "../elaboration/hierarchy_sv_constant_evaluator.hpp"

#include "fsim/runtime/class_randomize_graph.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/support/native_filesystem.hpp"

#include <charconv>

namespace fsim::app {

[[nodiscard]] std::filesystem::path Simulation::Impl::coverage_database_file(
    const std::string_view filename) const
{
    std::filesystem::path relative;
    try {
        relative = support::path_from_utf8(filename).lexically_normal();
    } catch (const std::exception&) {
        throw std::runtime_error {
            "coverage database filename is not a valid UTF-8 path"
        };
    }
    if (relative.empty() || relative.is_absolute()
        || relative.has_root_name()
        || std::ranges::any_of(relative, [](const auto& component) {
               return component == "..";
           })) {
        throw std::runtime_error {
            "coverage database filename must remain beneath the project file root"
        };
    }
    return (built.file_root / relative).lexically_normal();
}

void Simulation::Impl::load_coverage_database(const std::filesystem::path& path)
{
    std::error_code error;
    // A database not yet written holds no cumulative coverage to merge
    // (IEEE 1800-2017 20.14.4).
    if (!support::native_fs::exists(path, error) && !error) {
        return;
    }
    error.clear();
    const auto bytes = support::native_fs::file_size(path, error);
    if (error) {
        throw std::runtime_error {
            "cannot inspect coverage database: " + error.message()
        };
    }
    if (bytes > runtime::simir::maximum_memory_file_bytes) {
        throw std::runtime_error {
            "coverage database exceeds the governed file-size budget"
        };
    }
    auto input = support::native_fs::open_ifstream(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error { "cannot open coverage database" };
    }
    std::string contents(
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { });
    if (!input.good() && !input.eof()) {
        throw std::runtime_error { "cannot read coverage database" };
    }
    diagnostic::Engine diagnostics;
    auto loaded = deserialize_systemverilog_coverage_state(
        contents, support::path_to_utf8(path), built.systemverilog_hir,
        built.semantics, diagnostics);
    if (!loaded) {
        const auto& entries = diagnostics.diagnostics();
        throw std::runtime_error {
            entries.empty()
                ? "coverage database is malformed"
                : entries.front().message
        };
    }
    std::string merge_error;
    if (!application_detail::merge_systemverilog_coverage_state(
            built.systemverilog_coverage, *loaded,
            built.systemverilog_hir, built.semantics, merge_error)) {
        throw std::runtime_error { std::move(merge_error) };
    }
}

void Simulation::Impl::save_coverage_database()
{
    if (!coverage_database_path) {
        return;
    }
    diagnostic::Engine diagnostics;
    auto contents = serialize_systemverilog_coverage_state(
        built.systemverilog_coverage, built.systemverilog_hir,
        built.semantics, diagnostics);
    if (!contents) {
        const auto& entries = diagnostics.diagnostics();
        throw std::runtime_error {
            entries.empty()
                ? "cannot serialize coverage database"
                : entries.front().message
        };
    }
    std::error_code error;
    support::native_fs::create_directories(
        coverage_database_path->parent_path(), error);
    if (error) {
        throw std::runtime_error {
            "cannot create coverage database directory: "
            + error.message()
        };
    }
    auto temporary = *coverage_database_path;
    temporary += ".fsim-tmp";
    {
        auto output = support::native_fs::open_ofstream(
            temporary, std::ios::binary | std::ios::trunc);
        output.write(contents->data(),
            static_cast<std::streamsize>(contents->size()));
        if (!output) {
            support::native_fs::remove(temporary, error);
            throw std::runtime_error {
                "cannot write coverage database"
            };
        }
    }
    auto backup = *coverage_database_path;
    backup += ".fsim-old";
    support::native_fs::remove(backup, error);
    if (error) {
        support::native_fs::remove(temporary, error);
        throw std::runtime_error {
            "cannot prepare coverage database replacement: "
            + error.message()
        };
    }
    const auto had_existing = support::native_fs::exists(
        *coverage_database_path, error);
    if (error) {
        support::native_fs::remove(temporary, error);
        throw std::runtime_error {
            "cannot inspect coverage database destination: "
            + error.message()
        };
    }
    if (had_existing) {
        support::native_fs::rename(
            *coverage_database_path, backup, error);
        if (error) {
            support::native_fs::remove(temporary, error);
            throw std::runtime_error {
                "cannot preserve the previous coverage database: "
                + error.message()
            };
        }
    }
    support::native_fs::rename(
        temporary, *coverage_database_path, error);
    if (error) {
        const auto publish_error = error.message();
        if (had_existing) {
            std::error_code restore_error;
            support::native_fs::rename(
                backup, *coverage_database_path, restore_error);
        }
        support::native_fs::remove(temporary, error);
        throw std::runtime_error {
            "cannot publish coverage database: " + publish_error
        };
    }
    if (had_existing) {
        support::native_fs::remove(backup, error);
    }
}

void Simulation::Impl::control_coverage_database(
    const runtime::simir::CoverageDatabaseControlEvent& event)
{
    const auto path = coverage_database_file(event.filename);
    if (event.kind
        == runtime::simir::CoverageDatabaseControlKind::set_name) {
        coverage_database_path = path;
        return;
    }
    if (event.kind
        == runtime::simir::CoverageDatabaseControlKind::load) {
        load_coverage_database(path);
        return;
    }
    throw std::runtime_error {
        "unknown coverage database control kind"
    };
}

std::vector<runtime::SystemVerilogUvmCommandReportApplication>
Simulation::Impl::apply_uvm_report_settings(
    const std::string_view phase,
    const SimulationTick time)
{
    return uvm_command_line.apply_report_settings(
        uvm_components, uvm_reports, phase, time);
}

void Simulation::Impl::schedule_uvm_report_settings()
{
    auto& scheduler = interpreter->scheduler();
    for (const auto& handle : uvm_report_setting_tasks) {
        (void)scheduler.cancel(handle);
    }
    uvm_report_setting_tasks.clear();
    const auto now = scheduler.now();
    (void)apply_uvm_report_settings("time", now);
    std::set<SimulationTick> offsets;
    for (const auto& setting : uvm_command_line.settings().verbosity_settings) {
        if (setting.phase == "time" && setting.time_offset
            && *setting.time_offset > now) {
            offsets.insert(*setting.time_offset);
        }
    }
    runtime::StableOrder order { };
    for (const auto offset : offsets) {
        uvm_report_setting_tasks.push_back(
            scheduler.schedule_after_cancelable(
                offset - now, runtime::SchedulerPhase::active, order++,
                [this, offset](runtime::Scheduler&) {
                    (void)apply_uvm_report_settings("time", offset);
                }));
    }
}

void Simulation::Impl::validate_external_value(
    const SignalId signal,
    const PackedLogic4& value,
    const std::string_view operation) const
{
    const auto& info = built.design.signals().at(signal);
    if (info.width != value.width()) {
        throw std::invalid_argument(
            std::string { operation } + " width does not match signal '"
            + info.name + "'");
    }
    if (info.source_domain != frontend::ValueDomain::Bit2
        && info.source_domain != frontend::ValueDomain::Boolean) {
        return;
    }
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        const auto state = value.get(bit);
        if (state != runtime::Logic4::zero
            && state != runtime::Logic4::one) {
            throw std::invalid_argument(
                std::string { operation }
                + " would place an X/Z value into two-state signal '"
                + info.name + "'");
        }
    }
}

[[nodiscard]] const semantic::sv::ClassSpecialization&
Simulation::Impl::class_specialization(const std::string_view identity) const
{
    auto found = std::ranges::find(
        built.compiled_systemverilog_class_specializations, identity,
        &semantic::sv::ClassSpecialization::specialization_identity);
    if (found == built.compiled_systemverilog_class_specializations.end()) {
        const auto matches = std::ranges::count(
            built.compiled_systemverilog_class_specializations, identity,
            &semantic::sv::ClassSpecialization::declaration_identity);
        if (matches == 1) {
            found = std::ranges::find(
                built.compiled_systemverilog_class_specializations, identity,
                &semantic::sv::ClassSpecialization::declaration_identity);
        } else if (matches > 1) {
            // A bare class name denotes its default specialization.
            found = std::ranges::find_if(
                built.compiled_systemverilog_class_specializations,
                [&](const semantic::sv::ClassSpecialization& candidate) {
                    return candidate.declaration_identity == identity
                        && candidate.default_specialization;
                });
        }
    }
    if (found == built.compiled_systemverilog_class_specializations.end()) {
        throw std::out_of_range {
            "SystemVerilog class specialization '" + std::string { identity }
            + "' is not available in this simulation"
        };
    }
    return *found;
}

[[nodiscard]] runtime::SystemVerilogUvmRootHandle Simulation::Impl::component_root(
    const std::string_view allocation_scope)
{
    return class_execution.component_root(allocation_scope);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_source_randomize(
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const std::string> names,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const runtime::SystemVerilogConstraintTemplate>
        source_constraints)
{
    constexpr std::string_view inline_value_prefix = "@inline-value:";
    std::vector<std::string> selected_names;
    std::vector<runtime::PackedLogic4> inline_values;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (!names[index].starts_with(inline_value_prefix)) {
            selected_names.push_back(names[index]);
            continue;
        }
        if (index >= actuals.size()) {
            throw std::invalid_argument {
                "randomize inline value has no actual"
            };
        }
        inline_values.push_back(actuals[index]);
    }
    // Fill each calling-scope value placeholder (IEEE 1800-2017 18.7).
    std::vector<runtime::SystemVerilogConstraintTemplate> inline_constraints {
        source_constraints.begin(), source_constraints.end()
    };
    const auto fill = [&](auto&& self,
                          runtime::SystemVerilogConstraintTemplate& node)
        -> void {
        if (node.kind == runtime::SystemVerilogConstraintTemplateKind::Constant
            && node.text.starts_with(inline_value_prefix)) {
            const auto slot = std::stoull(
                node.text.substr(inline_value_prefix.size()));
            if (slot >= inline_values.size()
                || inline_values[slot].width() != node.constant.width()) {
                throw std::invalid_argument {
                    "randomize inline value does not match its constraint"
                };
            }
            node.constant = inline_values[slot];
            return;
        }
        for (auto& operand : node.operands) {
            self(self, operand);
        }
    };
    for (auto& constraint : inline_constraints) {
        fill(fill, constraint);
    }

    auto& object = class_heap.object(handle);
    const auto prior_properties = object.properties;
    const auto callback_on = [&](const runtime::SystemVerilogClassHandle target,
                                 const std::string_view name) {
        const auto* method = class_hir_execution.method_named(target, name);
        if (method == nullptr)
            return;
        const auto returns_void = !method->return_type
            || method->return_type->target.spelling == "void";
        if (method->kind != semantic::sv::ClassMethodKind::function
            || method->static_method || !method->formals.empty()
            || !returns_void) {
            throw std::invalid_argument {
                "randomize callback requires a nonstatic zero-argument void "
                "function"
            };
        }
        std::vector<runtime::PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        (void)class_hir_execution.invoke_function(
            *method, target, actuals, string_actuals, { }, { });
    };
    const auto callback = [&](const std::string_view name) {
        callback_on(handle, name);
    };
    try {
        callback("pre_randomize");
    } catch (...) {
        object.properties = prior_properties;
        return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
    }
    // Objects reached through class-handle properties join the solve; rand
    // handles extend randomization to theirs, whose pre_randomize runs after
    // the parent's (IEEE 1800-2017 18.5.9, 18.6.1).
    auto members = runtime::systemverilog_class_randomize_members(
        class_heap, handle);
    std::vector<std::pair<runtime::SystemVerilogClassHandle,
        std::vector<runtime::SystemVerilogClassPropertyValue>>>
        prior_members;
    try {
        for (const auto& member : members) {
            prior_members.emplace_back(member.handle,
                class_heap.object(member.handle).properties);
            if (member.randomized) {
                callback_on(member.handle, "pre_randomize");
            }
        }
    } catch (...) {
        object.properties = prior_properties;
        for (auto& [member_handle, properties] : prior_members) {
            class_heap.object(member_handle).properties = std::move(properties);
        }
        return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
    }
    runtime::SystemVerilogClassRandomizeRequest request;
    // Bound the enumerated domains, as scope randomize does, so a wide
    // random property fails cleanly instead of exhausting memory.
    request.limits.maximum_domain_values = std::size_t { 1U } << 22U;
    application_detail::configure_systemverilog_randomize_selection(
        request, selected_names);
    request.call_identity = object.specialization_identity
        + "::randomize@source";
    // A random enumerated property takes only its type's values (IEEE
    // 1800-2017 18.4).
    const auto enumeration_domains = [&](const std::string& specialization) {
        std::map<std::string, std::vector<runtime::PackedLogic4>, std::less<>>
            domains;
        for (const auto& property :
            class_specialization(specialization).properties) {
            if (property.enumeration_domain.empty() || property.static_storage
                || property.bit_width == 0U || property.bit_width > 64U) {
                continue;
            }
            auto& domain = domains[property.canonical_identity];
            domain.clear();
            for (const auto value : property.enumeration_domain) {
                const auto masked = property.bit_width == 64U
                    ? value
                    : value
                        & ((std::uint64_t { 1 } << property.bit_width) - 1U);
                const auto encoded = runtime::PackedLogic4::from_aval_bval(
                    property.bit_width, masked, 0U);
                if (std::ranges::find(domain, encoded) == domain.end()) {
                    domain.push_back(encoded);
                }
            }
        }
        return domains;
    };
    const auto class_constraints_of = [this](
        const runtime::SystemVerilogClassHandle target,
        std::string specialization, std::string path) {
        return runtime::SystemVerilogClassConstraintConfigurator {
            [this, target, specialization = std::move(specialization),
                path = std::move(path)](
                auto& solver, const auto& variables) {
                application_detail::configure_systemverilog_class_constraints(
                    solver, variables, built.systemverilog_hir,
                    class_specialization(specialization),
                    [this, target](const auto identity) {
                        if (const auto shared
                            = static_constraint_modes.find(identity);
                            shared != static_constraint_modes.end()) {
                            return shared->second;
                        }
                        return class_heap.constraint_mode(target, identity);
                    },
                    path);
            }
        };
    };
    request.property_domains = enumeration_domains(
        object.specialization_identity);
    request.class_constraints = class_constraints_of(
        handle, object.specialization_identity, std::string { });
    for (auto& member : members) {
        const auto& specialization
            = class_heap.object(member.handle).specialization_identity;
        member.property_domains = enumeration_domains(specialization);
        // Only an object reached through rand handles adds its constraints;
        // others are state the solve may read (IEEE 1800-2017 18.5.9).
        if (member.randomized) {
            member.class_constraints = class_constraints_of(
                member.handle, specialization, member.path);
        }
    }
    if (!inline_constraints.empty()) {
        request.inline_constraints
            = [inline_constraints,
                  identity = object.specialization_identity](
                  auto& solver, const auto& variables) {
                  runtime::configure_systemverilog_inline_constraints(
                      solver, variables, inline_constraints,
                      identity + "::randomize@inline");
              };
    }
    // Unpacked arrays, dynamic arrays and queues of packed values join the
    // solve element by element (IEEE 1800-2017 18.4).
    std::vector<runtime::SystemVerilogClassRandomizeContainer> containers;
    const auto node_handle = [&](const std::size_t node) {
        return node == 0U ? handle : members[node - 1U].handle;
    };
    if (!class_constraint_property_types) {
        auto& types = class_constraint_property_types.emplace();
        const auto collect = [&](auto&& self,
                                 const semantic::sv::ConstraintExpression&
                                     expression) -> void {
            for (const auto& binding : expression.bindings) {
                if (binding.kind
                        == semantic::sv::ConstraintReferenceKind::property
                    && binding.canonical_identity.find('.')
                        == std::string::npos) {
                    types.try_emplace(binding.canonical_identity,
                        binding.type);
                }
            }
            for (const auto& operand : expression.operands) {
                self(self, operand);
            }
        };
        for (const auto& declaration : built.systemverilog_hir.classes()) {
            for (const auto& constraint : declaration.constraints) {
                for (const auto& expression : constraint.expressions) {
                    collect(collect, expression);
                }
            }
        }
    }
    using runtime::simir::ContainerElementKind;
    // The containers of a property and its packed elements, in pre-order,
    // each with its path from the property: a nested container by
    // element, a multi-dimensional fixed-size array by dimension (IEEE
    // 1800-2017 7.4.2, 7.4.5). False when an element is neither.
    const auto extent = [](const std::int64_t left, const std::int64_t right) {
        return static_cast<std::size_t>(
                   left >= right ? left - right : right - left)
            + 1U;
    };
    const auto fixed_shape = [](const std::int64_t left,
                                 const std::int64_t right) {
        runtime::SystemVerilogConstraintContainerShape shape;
        shape.fixed = true;
        shape.left = left;
        shape.descending = left > right;
        return shape;
    };
    const auto visit_container = [&](auto&& self,
                                     runtime::simir::ContainerValue& value,
                                     const std::string& path,
                                     const auto& leaf,
                                     const auto& node,
                                     const auto& aggregate) -> bool {
        const auto& type = value.type;
        if (type.fixed && type.dimensions.size() > 1U) {
            if (type.element_kind != ContainerElementKind::Packed) {
                return false;
            }
            const auto rank = type.dimensions.size();
            std::vector<std::size_t> strides(rank, 1U);
            for (std::size_t k = rank - 1U; k-- > 0U;) {
                strides[k] = strides[k + 1U]
                    * extent(type.dimensions[k + 1U].first,
                        type.dimensions[k + 1U].second);
            }
            if (strides.front()
                    * extent(type.dimensions.front().first,
                        type.dimensions.front().second)
                != value.elements.size()) {
                return false;
            }
            // Elements are stored with the rightmost dimension varying
            // fastest.
            const auto dimension = [&](auto&& next, const std::size_t k,
                                       const std::size_t offset,
                                       const std::string& prefix) -> void {
                const auto [left, right] = type.dimensions[k];
                const auto count = extent(left, right);
                node(prefix, count, fixed_shape(left, right));
                for (std::size_t position = 0; position < count;
                     ++position) {
                    const auto child
                        = prefix + "[" + std::to_string(position) + "]";
                    const auto at = offset + position * strides[k];
                    if (k + 1U == rank) {
                        leaf(child, value.elements[at]);
                    } else {
                        next(next, k + 1U, at, child);
                    }
                }
            };
            dimension(dimension, 0U, 0U, path);
            return true;
        }
        runtime::SystemVerilogConstraintContainerShape shape;
        if (type.fixed) shape = fixed_shape(type.index_left, type.index_right);
        if (type.associative) {
            shape.associative = true;
            shape.string_keys = type.string_indices;
            shape.keys = value.keys;
            shape.string_key_values = value.string_keys;
        }
        if (type.element_kind == ContainerElementKind::Packed) {
            node(path, value.elements.size(), shape);
            for (std::size_t index = 0; index < value.elements.size();
                 ++index) {
                leaf(path + "[" + std::to_string(index) + "]",
                    value.elements[index]);
            }
            return true;
        }
        // An array of unpacked structures: each element's members.
        if (type.element_kind == ContainerElementKind::Aggregate
            && !type.aggregate_value) {
            node(path, value.nested_elements.size(), shape);
            for (std::size_t index = 0; index < value.nested_elements.size();
                 ++index) {
                if (!aggregate(path + "[" + std::to_string(index) + "]",
                        value.nested_elements[index])) {
                    return false;
                }
            }
            return true;
        }
        if (type.element_kind != ContainerElementKind::Container) {
            return false;
        }
        node(path, value.nested_elements.size(), shape);
        for (std::size_t index = 0; index < value.nested_elements.size();
             ++index) {
            if (!self(self, value.nested_elements[index],
                    path + "[" + std::to_string(index) + "]", leaf, node,
                    aggregate)) {
                return false;
            }
        }
        return true;
    };
    const auto no_aggregate = [](const std::string&,
                                 runtime::simir::ContainerValue&) {
        return false;
    };
    // A nested, multi-dimensional or associative property is listed with
    // its whole shape and written back from this value.
    std::vector<std::optional<runtime::simir::ContainerValue>> shaped_values;
    // A structure property is listed member by member; each member is
    // written back to these bits (offset, width) of the property.
    std::vector<std::vector<std::pair<std::size_t, std::size_t>>>
        structure_bits;
    const auto definition_of = [&](const semantic::sv::TypeReference& type)
        -> const semantic::sv::TypeDefinition* {
        // A type of another unit (a `$unit` or package typedef) keeps only
        // its spelling; it resolves when the spelling names one typedef.
        const auto named = [&](const semantic::sv::TypeReference& reference)
            -> const semantic::sv::TypeDefinition* {
            if (reference.target.target.valid()) {
                return built.systemverilog_hir.find_type(
                    reference.target.target);
            }
            auto spelling = std::string_view { reference.target.spelling };
            if (const auto cut = spelling.rfind("::");
                cut != std::string_view::npos) {
                spelling.remove_prefix(cut + 2U);
            }
            if (spelling.empty()) return nullptr;
            const semantic::sv::TypeDefinition* found = nullptr;
            for (const auto& candidate : built.systemverilog_hir.types()) {
                if (candidate.name != spelling) continue;
                if (found != nullptr) return nullptr;
                found = &candidate;
            }
            return found;
        };
        const auto* definition = named(type);
        for (std::size_t depth = 0; depth < 16U && definition != nullptr
             && definition->members.empty()
             && (definition->base.target.target.valid()
                 || !definition->base.target.spelling.empty());
             ++depth) {
            definition = named(definition->base);
        }
        return definition;
    };
    const auto structure_of = [&](const semantic::sv::TypeReference& type)
        -> const semantic::sv::TypeDefinition* {
        if (type.container_form) return nullptr;
        const auto* definition = definition_of(type);
        return definition != nullptr && !definition->members.empty()
                && (definition->form
                        == semantic::sv::TypeForm::packed_structure
                    || definition->form
                        == semantic::sv::TypeForm::unpacked_structure)
            ? definition
            : nullptr;
    };
    // The bits a value of `type` takes in a structure's image.
    const auto image_width = [&](auto&& self,
                                 const semantic::sv::TypeReference& type)
        -> std::optional<std::size_t> {
        if (type.container_form) return std::nullopt;
        if (const auto* definition = structure_of(type)) {
            std::size_t total { };
            for (const auto& member : definition->members) {
                const auto width = self(self, member.type);
                if (!width) return std::nullopt;
                total += *width;
            }
            return total;
        }
        if (type.executable_width) {
            return static_cast<std::size_t>(*type.executable_width);
        }
        if (type.packed_range && type.packed_range->left
            && type.packed_range->right) {
            return static_cast<std::size_t>(support::index_distance(
                       *type.packed_range->left, *type.packed_range->right))
                + 1U;
        }
        if (const auto* definition = definition_of(type);
            definition != nullptr && definition->members.empty()
            && definition->base.executable_width) {
            return static_cast<std::size_t>(
                *definition->base.executable_width);
        }
        return std::nullopt;
    };
    struct StructureLeaf {
        std::string path;
        std::size_t offset { };
        std::size_t width { };
        const semantic::sv::TypeReference* type { };
        bool random { };
    };
    // The members of a structure, first member most significant; an
    // unpacked structure's member is random when declared rand, a packed
    // structure's members with the structure (IEEE 1800-2017 7.2, 18.4).
    const auto structure_leaves = [&](auto&& self,
                                      const semantic::sv::TypeDefinition&
                                          definition,
                                      const std::string& path,
                                      std::size_t top, const bool random,
                                      std::vector<StructureLeaf>& leaves)
        -> bool {
        const bool packed
            = definition.form == semantic::sv::TypeForm::packed_structure;
        for (const auto& member : definition.members) {
            const auto width = image_width(image_width, member.type);
            if (!width || *width == 0U || *width > top) return false;
            top -= *width;
            const bool member_random = random && (packed || member.random);
            if (const auto* nested = structure_of(member.type)) {
                if (!self(self, *nested, path + "." + member.name,
                        top + *width, member_random, leaves)) {
                    return false;
                }
                continue;
            }
            if (*width > 4096U) return false;
            leaves.push_back({ path + "." + member.name, top, *width,
                &member.type, member_random });
        }
        return true;
    };
    // The members of an unpacked structure held as a value (an element of
    // an array of structures), in declaration order: `visit` receives each
    // packed member's path, value, type and whether it is random.
    const auto box_leaves = [&](auto&& self,
                                runtime::simir::ContainerValue& box,
                                const semantic::sv::TypeDefinition& definition,
                                const std::string& path, const bool random,
                                const auto& visit) -> bool {
        if (box.nested_elements.size() != definition.members.size()) {
            return false;
        }
        for (std::size_t index = 0; index < definition.members.size();
             ++index) {
            const auto& member = definition.members[index];
            auto& held = box.nested_elements[index];
            const bool member_random = random && member.random;
            if (const auto* nested = structure_of(member.type)) {
                if (!self(self, held, *nested, path + "." + member.name,
                        member_random, visit)) {
                    return false;
                }
                continue;
            }
            if (held.elements.size() != 1U
                || (held.type.element_kind != ContainerElementKind::Packed
                    && held.type.element_kind
                        != ContainerElementKind::Scalar)) {
                return false;
            }
            // A real member is not a random variable (IEEE 1800-2017 18.4).
            visit(path + "." + member.name, held.elements.front(), member.type,
                member_random
                    && held.type.element_kind == ContainerElementKind::Packed);
        }
        return true;
    };
    // The default value of a fixed-size array of unpacked structures, in
    // the layout the elaborated design gives it: aggregate boxes of
    // one-element packed members (IEEE 1800-2017 7.2, 7.4.1).
    const auto box_type = [&](auto&& self,
                              const semantic::sv::TypeDefinition& definition)
        -> std::optional<runtime::simir::ContainerType> {
        runtime::simir::ContainerType box;
        box.element_kind = ContainerElementKind::Aggregate;
        box.element_width = 0U;
        box.aggregate_value = true;
        for (const auto& member : definition.members) {
            if (member.type.container_form
                || !member.type.unpacked_dimensions.empty()) {
                return std::nullopt;
            }
            const auto* nested = structure_of(member.type);
            if (nested != nullptr
                && nested->form == semantic::sv::TypeForm::unpacked_structure) {
                auto inner = self(self, *nested);
                if (!inner) return std::nullopt;
                box.member_names.push_back(member.name);
                box.element_types.push_back(std::move(*inner));
                continue;
            }
            const auto width = image_width(image_width, member.type);
            if (!width || *width == 0U || *width > 4096U
                || (nested != nullptr
                    && nested->form
                        != semantic::sv::TypeForm::packed_structure)) {
                return std::nullopt;
            }
            runtime::simir::ContainerType leaf;
            leaf.element_kind = ContainerElementKind::Packed;
            leaf.element_width = static_cast<std::uint32_t>(*width);
            leaf.two_state = !member.type.four_state;
            leaf.signed_elements = member.type.signed_value;
            leaf.fixed = true;
            leaf.dimensions.emplace_back(0, 0);
            box.member_names.push_back(member.name);
            box.element_types.push_back(std::move(leaf));
        }
        return box;
    };
    const auto default_structure_array
        = [&](const semantic::sv::TypeDefinition& definition,
              const semantic::sv::TypeReference& type)
        -> std::optional<runtime::simir::ContainerValue> {
        if (type.unpacked_dimensions.size() != 1U) return std::nullopt;
        const auto& range = type.unpacked_dimensions.front();
        if (!range.left || !range.right
            || extent(*range.left, *range.right) > (1U << 20)) {
            return std::nullopt;
        }
        auto element = box_type(box_type, definition);
        if (!element) return std::nullopt;
        auto array = std::move(*element);
        array.aggregate_value = false;
        array.fixed = true;
        array.index_left = static_cast<std::int32_t>(*range.left);
        array.index_right = static_cast<std::int32_t>(*range.right);
        array.dimensions.emplace_back(array.index_left, array.index_right);
        return runtime::simir::default_container_value(array);
    };
    const auto leaf_profile = [](const semantic::sv::TypeReference& type,
                                 const runtime::PackedLogic4& value,
                                 const bool random) {
        runtime::SystemVerilogClassPropertyValue element;
        element.kind = type.four_state
            ? runtime::SystemVerilogClassPropertyKind::Logic4
            : runtime::SystemVerilogClassPropertyKind::Bit2;
        element.packed = value;
        element.random_state = runtime::SystemVerilogClassRandomState {
            random ? runtime::SystemVerilogClassRandomKind::Rand
                   : runtime::SystemVerilogClassRandomKind::None,
            value.width(),
            type.signed_value,
            type.target.spelling.empty()
                ? std::string { "$constraint-value" }
                : type.target.spelling,
            true, 0, 0, 0, 0, { } };
        return element;
    };
    // The structure of each array of structures' elements, for write-back.
    std::vector<const semantic::sv::TypeDefinition*> element_structures;
    for (std::size_t node = 0; node <= members.size(); ++node) {
        const auto target = node_handle(node);
        const auto& target_object = class_heap.object(target);
        for (const auto& property :
            class_specialization(target_object.specialization_identity)
                .properties) {
            if (!property.static_storage && !property.type.container_form) {
                const auto* structure = structure_of(property.type);
                if (structure == nullptr
                    || std::ranges::find(target_object.property_names,
                           property.canonical_identity)
                        == target_object.property_names.end()) {
                    continue;
                }
                const auto& held = class_heap.property(
                    target, property.canonical_identity);
                const auto width = structure == nullptr ? std::nullopt
                    : image_width(image_width, property.type);
                std::vector<StructureLeaf> leaves;
                if (structure == nullptr || !width
                    || held.packed.width() != *width
                    || !structure_leaves(structure_leaves, *structure,
                        std::string { }, *width, true, leaves)) {
                    continue;
                }
                runtime::SystemVerilogClassRandomizeContainer container;
                container.node = node;
                container.name = property.canonical_identity;
                std::vector<std::pair<std::size_t, std::size_t>> bits;
                for (const auto& leaf : leaves) {
                    runtime::PackedLogic4 value(
                        leaf.width, runtime::Logic4::zero);
                    for (std::size_t bit = 0; bit < leaf.width; ++bit) {
                        value.set(bit, held.packed.get(leaf.offset + bit));
                    }
                    auto element = leaf_profile(*leaf.type, value, leaf.random);
                    container.element_paths.push_back(leaf.path);
                    container.values.push_back(element.packed);
                    container.element_profiles.push_back(std::move(element));
                    bits.emplace_back(leaf.offset, leaf.width);
                }
                container.element = container.element_profiles.front();
                shaped_values.resize(containers.size());
                shaped_values.emplace_back();
                structure_bits.resize(containers.size());
                structure_bits.push_back(std::move(bits));
                containers.push_back(std::move(container));
                continue;
            }
            if (property.static_storage || !property.type.container_form
                || property.type.container_element_types.size() != 1U) {
                continue;
            }
            const auto* leaf_type = &property.type;
            while (leaf_type->container_form
                && leaf_type->container_element_types.size() == 1U) {
                leaf_type = &leaf_type->container_element_types.front();
            }
            if (leaf_type->container_form) continue;
            const bool nested = property.type.container_element_types.front()
                                    .container_form.has_value();
            const auto form = *property.type.container_form;
            const bool resizable
                = form == semantic::sv::TypeForm::dynamic_array
                || form == semantic::sv::TypeForm::queue;
            if (!resizable && form != semantic::sv::TypeForm::static_array
                && form != semantic::sv::TypeForm::associative_array) {
                continue;
            }
            // An array of unpacked structures joins the solve member by
            // member of each element (`a[i].m`, IEEE 1800-2017 7.2, 18.4);
            // it keeps its size. A fixed-size one never written holds its
            // default elements.
            const auto* element_structure = structure_of(*leaf_type);
            const bool written = std::ranges::find(
                                     target_object.property_names,
                                     property.canonical_identity)
                != target_object.property_names.end();
            auto structure_array = element_structure != nullptr && written
                    && element_structure->form
                        == semantic::sv::TypeForm::unpacked_structure
                ? class_heap.property(target, property.canonical_identity)
                      .container
                : std::nullopt;
            if (!structure_array && element_structure != nullptr && written
                && element_structure->form
                    == semantic::sv::TypeForm::unpacked_structure
                && form == semantic::sv::TypeForm::static_array && !nested) {
                structure_array = default_structure_array(
                    *element_structure, property.type);
            }
            if (structure_array) {
                runtime::SystemVerilogClassRandomizeContainer container;
                container.node = node;
                container.name = property.canonical_identity;
                auto shaped = *structure_array;
                bool valid = true;
                const auto leaf = [&](const std::string&,
                                      const runtime::PackedLogic4&) {
                    valid = false;
                };
                const auto shape_node = [&](const std::string& path,
                                            const std::size_t size,
                                            runtime::SystemVerilogConstraintContainerShape
                                                shape) {
                    container.shapes.push_back(
                        { path, size, std::move(shape) });
                };
                const auto aggregate = [&](const std::string& path,
                                           runtime::simir::ContainerValue& box) {
                    return box_leaves(box_leaves, box, *element_structure, path,
                        true,
                        [&](const std::string& member_path,
                            const runtime::PackedLogic4& value,
                            const semantic::sv::TypeReference& type,
                            const bool random) {
                            container.element_paths.push_back(member_path);
                            container.values.push_back(value);
                            container.element_profiles.push_back(
                                leaf_profile(type, value, random));
                        });
                };
                if (!visit_container(visit_container, shaped, std::string { },
                        leaf, shape_node, aggregate)
                    || !valid) {
                    continue;
                }
                container.element = container.element_profiles.empty()
                    ? leaf_profile(*leaf_type,
                          runtime::PackedLogic4(1U, runtime::Logic4::zero),
                          false)
                    : container.element_profiles.front();
                shaped_values.resize(containers.size());
                shaped_values.push_back(std::move(shaped));
                element_structures.resize(containers.size());
                element_structures.push_back(element_structure);
                containers.push_back(std::move(container));
                continue;
            }
            // The constraints' profile of the elements, where they name
            // the array, matches the variables their clauses read.
            const auto named = class_constraint_property_types->find(
                property.canonical_identity);
            // Otherwise the element type, through its typedefs.
            auto declared = nested ? *leaf_type
                : named != class_constraint_property_types->end()
                ? named->second : property.type;
            for (std::size_t depth = 0; depth < 16U
                 && !declared.executable_width && !declared.packed_range;
                 ++depth) {
                const auto& element
                    = declared.container_element_types.empty()
                    ? declared : declared.container_element_types.front();
                const auto* definition = element.target.target.valid()
                    ? built.systemverilog_hir.find_type(element.target.target)
                    : nullptr;
                // A typedef from another scope keeps only its spelling; it
                // resolves when the spelling names one typedef.
                if (definition == nullptr && !element.target.spelling.empty()) {
                    for (const auto& candidate :
                        built.systemverilog_hir.types()) {
                        if (candidate.name != element.target.spelling) {
                            continue;
                        }
                        if (definition != nullptr) {
                            definition = nullptr;
                            break;
                        }
                        definition = &candidate;
                    }
                }
                if (definition == nullptr || !definition->members.empty()) {
                    break;
                }
                const auto spelling = declared.target.spelling;
                declared = definition->base;
                declared.target.spelling = spelling;
            }
            if (!declared.executable_width && declared.packed_range
                && declared.packed_range->left
                && declared.packed_range->right) {
                declared.executable_width = static_cast<std::uint64_t>(
                    support::index_distance(*declared.packed_range->left,
                        *declared.packed_range->right) + 1U);
            }
            const auto width = declared.executable_width.value_or(0U);
            if (width == 0U || width > 4096U
                || leaf_type->value_form == semantic::sv::TypeForm::string
                || leaf_type->value_form
                    == semantic::sv::TypeForm::class_handle
                || std::ranges::find(target_object.property_names,
                       property.canonical_identity)
                    == target_object.property_names.end()) {
                continue;
            }
            runtime::SystemVerilogClassRandomizeContainer container;
            container.node = node;
            container.name = property.canonical_identity;
            container.resizable = resizable;
            container.element.kind = declared.four_state
                ? runtime::SystemVerilogClassPropertyKind::Logic4
                : runtime::SystemVerilogClassPropertyKind::Bit2;
            const auto initial = declared.four_state
                ? runtime::Logic4::x : runtime::Logic4::zero;
            container.element.packed = runtime::PackedLogic4(width, initial);
            container.element.random_state
                = runtime::SystemVerilogClassRandomState {
                      runtime::SystemVerilogClassRandomKind::Rand,
                      static_cast<std::size_t>(width),
                      declared.signed_value,
                      declared.target.spelling.empty()
                          ? std::string { "$constraint-value" }
                          : declared.target.spelling,
                      true, 0, 0, 0, 0, { } };
            // An array of an enumeration takes only its values.
            if (!property.enumeration_domain.empty() && width <= 64U) {
                auto& domains = node == 0U
                    ? request.property_domains
                    : members[node - 1U].property_domains;
                auto& domain = domains[property.canonical_identity];
                domain.clear();
                for (const auto value : property.enumeration_domain) {
                    const auto masked = width == 64U ? value
                        : value & ((std::uint64_t { 1 } << width) - 1U);
                    const auto encoded = runtime::PackedLogic4::from_aval_bval(
                        width, masked, 0U);
                    if (std::ranges::find(domain, encoded) == domain.end()) {
                        domain.push_back(encoded);
                    }
                }
            }
            std::optional<runtime::simir::ContainerValue> shaped;
            if (const auto& stored = class_heap.property(
                    target, property.canonical_identity).container) {
                if (nested || stored->type.associative
                    || stored->type.dimensions.size() > 1U) {
                    shaped = *stored;
                } else if (stored->type.element_kind
                        != ContainerElementKind::Packed
                    || std::ranges::any_of(stored->elements,
                        [&](const auto& element) {
                            return element.width() != width;
                        })) {
                    continue;
                } else {
                    container.values = stored->elements;
                    if (stored->type.fixed) {
                        container.shapes.push_back({ std::string { },
                            stored->elements.size(),
                            fixed_shape(stored->type.index_left,
                                stored->type.index_right) });
                    }
                }
            } else if (!resizable && !nested
                && form == semantic::sv::TypeForm::static_array) {
                // An array never written holds its default elements.
                std::vector<runtime::simir::ContainerDimension> dimensions;
                for (const auto& range : property.type.unpacked_dimensions) {
                    if (!range.left || !range.right
                        || extent(*range.left, *range.right) > (1U << 20)) {
                        dimensions.clear();
                        break;
                    }
                    dimensions.emplace_back(
                        static_cast<std::int32_t>(*range.left),
                        static_cast<std::int32_t>(*range.right));
                }
                if (dimensions.empty()) continue;
                runtime::simir::ContainerType type;
                type.element_width = static_cast<std::uint32_t>(width);
                type.two_state = !declared.four_state;
                type.signed_elements = declared.signed_value;
                type.fixed = true;
                type.index_left = dimensions.front().first;
                type.index_right = dimensions.front().second;
                type.dimensions = dimensions;
                if (dimensions.size() > 1U) {
                    shaped = runtime::simir::default_container_value(type);
                } else {
                    container.values.assign(
                        extent(type.index_left, type.index_right),
                        container.element.packed);
                    container.shapes.push_back({ std::string { },
                        container.values.size(),
                        fixed_shape(type.index_left, type.index_right) });
                }
            }
            if (shaped) {
                // A nested or associative property keeps its sizes.
                container.resizable = false;
                container.shapes.clear();
                bool packed = true;
                const auto leaf = [&](const std::string& path,
                                      const runtime::PackedLogic4& value) {
                    packed = packed && value.width() == width;
                    container.element_paths.push_back(path);
                    container.values.push_back(value);
                };
                const auto shape_node = [&](const std::string& path,
                                            const std::size_t size,
                                            runtime::SystemVerilogConstraintContainerShape
                                                shape) {
                    container.shapes.push_back(
                        { path, size, std::move(shape) });
                };
                if (!visit_container(visit_container, *shaped, std::string { },
                        leaf, shape_node, no_aggregate)
                    || !packed) {
                    continue;
                }
            }
            shaped_values.resize(containers.size());
            shaped_values.push_back(std::move(shaped));
            containers.push_back(std::move(container));
        }
    }
    shaped_values.resize(containers.size());
    structure_bits.resize(containers.size());
    element_structures.resize(containers.size());
    const auto result = runtime::randomize_systemverilog_class_graph(
        class_heap, handle, request, members, containers);
    for (std::size_t c = 0; c < containers.size(); ++c) {
        const auto& container = containers[c];
        if (!container.committed) continue;
        if (!structure_bits[c].empty()) {
            auto& packed = class_heap.property(
                node_handle(container.node), container.name).packed;
            for (std::size_t leaf = 0; leaf < structure_bits[c].size();
                 ++leaf) {
                const auto [offset, width] = structure_bits[c][leaf];
                for (std::size_t bit = 0; bit < width; ++bit) {
                    packed.set(offset + bit, container.values[leaf].get(bit));
                }
            }
            continue;
        }
        auto& stored = class_heap.property(
            node_handle(container.node), container.name).container;
        if (auto& shaped = shaped_values[c]) {
            std::size_t next { };
            const auto leaf = [&](const std::string&,
                                  runtime::PackedLogic4& value) {
                value = container.values.at(next++);
            };
            const auto shape_node = [](const std::string&, std::size_t,
                                       const runtime::SystemVerilogConstraintContainerShape&) {
            };
            const auto aggregate = [&](const std::string& path,
                                       runtime::simir::ContainerValue& box) {
                return element_structures[c] != nullptr
                    && box_leaves(box_leaves, box, *element_structures[c], path,
                        true,
                        [&](const std::string&, runtime::PackedLogic4& value,
                            const semantic::sv::TypeReference&, bool) {
                            value = container.values.at(next++);
                        });
            };
            (void)visit_container(visit_container, *shaped, std::string { },
                leaf, shape_node, aggregate);
            stored = std::move(*shaped);
            continue;
        }
        if (!stored) {
            runtime::simir::ContainerValue created;
            created.type.element_width = static_cast<std::uint32_t>(
                container.element.packed.width());
            created.type.two_state = container.element.kind
                == runtime::SystemVerilogClassPropertyKind::Bit2;
            created.type.signed_elements
                = container.element.random_state->signed_value;
            created.type.queue = false;
            created.type.fixed = !container.resizable;
            if (!container.resizable) {
                const auto last = static_cast<std::int32_t>(
                    container.values.size()) - 1;
                const auto left = container.shapes.empty()
                    ? 0 : static_cast<std::int32_t>(
                          container.shapes.front().shape.left);
                const auto right = container.shapes.empty()
                    || !container.shapes.front().shape.descending
                    ? left + last : left - last;
                created.type.index_left = left;
                created.type.index_right = right;
                created.type.dimensions.emplace_back(left, right);
            }
            stored = std::move(created);
        }
        stored->elements = container.values;
    }
    if (result.language_result() != 0) {
        try {
            callback("post_randomize");
            for (const auto& member : members) {
                if (member.randomized) {
                    callback_on(member.handle, "post_randomize");
                }
            }
        } catch (...) {
            object.properties = prior_properties;
            for (auto& [member_handle, properties] : prior_members) {
                class_heap.object(member_handle).properties
                    = std::move(properties);
            }
            return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
        }
    }
    return runtime::PackedLogic4::from_aval_bval(
        32, result.language_result(), 0);
}

[[nodiscard]] bool Simulation::Impl::static_constraint(
    const std::string_view identity)
{
    if (!static_constraint_identities) {
        auto& identities = static_constraint_identities.emplace();
        for (const auto& declaration : built.systemverilog_hir.classes()) {
            for (const auto& constraint : declaration.constraints) {
                if (constraint.static_constraint) {
                    identities.insert(constraint.canonical_identity);
                }
            }
        }
    }
    return static_constraint_identities->contains(identity);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_source_randomization_mode(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view method,
    const std::span<const runtime::PackedLogic4> actuals)
{
    // A static constraint's mode belongs to its class (IEEE 1800-2017
    // 18.9): every object reads and updates the same mode.
    constexpr std::string_view constraint_prefix { "@builtin-constraint-mode:" };
    if (method.starts_with(constraint_prefix)
        && static_constraint(method.substr(constraint_prefix.size()))) {
        const auto identity = method.substr(constraint_prefix.size());
        if (actuals.size() > 1U) {
            throw std::invalid_argument {
                "randomization mode method accepts zero or one argument"
            };
        }
        if (!actuals.empty()) {
            const auto value = actuals.front().low_word();
            if (value.bval != 0 || (value.aval != 0 && value.aval != 1)) {
                throw std::invalid_argument {
                    "randomization mode update requires a known zero or one"
                };
            }
            static_constraint_modes.insert_or_assign(
                std::string { identity }, value.aval != 0);
        }
        const auto mode = static_constraint_modes.find(identity);
        const bool enabled = mode == static_constraint_modes.end()
            || mode->second;
        return runtime::PackedLogic4::from_aval_bval(
            32, enabled ? 1U : 0U, 0);
    }
    return class_execution.invoke_source_randomization_mode(
        handle, method, actuals);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::resize_packed(
    const runtime::PackedLogic4& value,
    const std::size_t width)
{
    return application_detail::SystemVerilogClassExecution::resize_packed(
        value, width);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::packed_property_value(
    const runtime::SystemVerilogClassPropertyValue& value)
{
    return application_detail::SystemVerilogClassExecution::packed_property_value(
        value);
}

void Simulation::Impl::assign_property_value(
    runtime::SystemVerilogClassPropertyValue& destination,
    const std::string_view canonical_identity,
    const runtime::PackedLogic4& value,
    const std::string_view object_specialization)
{
    if (destination.kind
        == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
        const auto word = value.low_word();
        if (word.bval != 0) {
            throw std::invalid_argument {
                "class-handle property assignment contains X or Z"
            };
        }
        const auto [named_owner, name] = static_property_parts(
            canonical_identity);
        // A static property of a specialization is named through the
        // specialization's identity.
        const semantic::sv::ClassSpecialization* specialization = nullptr;
        std::string_view owner = named_owner;
        if (const auto exact = std::ranges::find(
                built.compiled_systemverilog_class_specializations,
                named_owner,
                &semantic::sv::ClassSpecialization::specialization_identity);
            exact != built.compiled_systemverilog_class_specializations.end()
            && exact->declaration_identity != named_owner) {
            owner = exact->declaration_identity;
            specialization = &*exact;
        }
        // The property's type comes from the specialization of the object
        // that holds it, or of the base it inherits the property from: a
        // type parameter's actual can be a class (IEEE 1800-2017 8.25).
        for (auto current = specialization == nullptr
                 ? object_specialization
                 : std::string_view { };
             !current.empty();) {
            const auto& candidate = class_specialization(current);
            if (std::ranges::any_of(candidate.properties,
                    [&](const auto& property) {
                        return property.owner_identity == owner
                            && property.name == name;
                    })) {
                specialization = &candidate;
                break;
            }
            if (!candidate.base) {
                break;
            }
            current = candidate.base->specialization_identity;
        }
        if (specialization == nullptr) {
            specialization = &class_specialization(owner);
        }
        const auto property = std::ranges::find_if(
            specialization->properties, [&](const auto& candidate) {
                return candidate.owner_identity == owner
                    && candidate.name == name;
            });
        if (property == specialization->properties.end()) {
            throw std::out_of_range {
                "SystemVerilog class property profile '"
                + std::string { canonical_identity } + "' is not available"
            };
        }
        if (std::getenv("FSIM_SV_CLASS_SPEC_TRACE") != nullptr) {
            std::cerr << "[fsim-sv-class-spec] assign " << canonical_identity
                      << " object=" << object_specialization
                      << " selected=" << specialization->specialization_identity
                      << " type=" << property->type.class_identity << '\n';
        }
        if (word.aval != 0) {
            (void)class_heap.checked_cast(
                word.aval, property->type.class_identity);
        }
        destination.handle = word.aval;
        return;
    }
    destination.packed = resize_packed(value, destination.packed.width());
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_class_container(
    const runtime::SystemVerilogClassHandle receiver,
    const std::string_view operation,
    const std::span<const runtime::PackedLogic4> actuals)
{
    return class_execution.invoke_class_container(
        receiver, operation, actuals);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_checked_class_cast(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view operation) const
{
    return class_execution.invoke_checked_class_cast(handle, operation);
}

[[nodiscard]] std::pair<std::string_view, std::string_view>
Simulation::Impl::static_property_parts(const std::string_view identity)
{
    return application_detail::SystemVerilogClassExecution::static_property_parts(
        identity);
}

std::string Simulation::Impl::static_property_specialization(
    const std::string_view owner,
    const runtime::SystemVerilogClassHandle handle) const
{
    const auto registered = [&] {
        try {
            static_cast<void>(class_static_store.initialized(owner));
            return true;
        } catch (const std::out_of_range&) {
            return false;
        }
    }();
    if (handle == 0U || registered) {
        return std::string { owner };
    }
    try {
        for (std::string current {
                 class_heap.object(handle).specialization_identity };
             !current.empty();) {
            const auto& candidate = class_specialization(current);
            if (candidate.declaration_identity == owner) {
                return current;
            }
            if (!candidate.base) {
                break;
            }
            current = candidate.base->specialization_identity;
        }
    } catch (const std::exception&) {
    }
    return std::string { owner };
}

[[nodiscard]] runtime::SystemVerilogClassHandle Simulation::Impl::allocate_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type,
    const std::string_view allocation_scope)
{
    const auto& specialization = class_specialization(
        specialization_identity);
    runtime::SystemVerilogClassDescriptor descriptor;
    descriptor.dynamic_type = specialization.declaration_identity;
    descriptor.declared_type = declared_type.empty()
        ? descriptor.dynamic_type
        : std::string { declared_type };
    descriptor.specialization_identity = specialization.specialization_identity;
    const auto separator = allocation_scope.find('.');
    descriptor.random_root_identity = std::string {
        allocation_scope.substr(0, separator)
    };
    const auto* current = &specialization;
    std::vector<const semantic::sv::SpecializedClassRelation*> interfaces;
    while (current != nullptr) {
        descriptor.assignable_declared_types.push_back(
            current->declaration_identity);
        for (const auto& implemented : current->interfaces) {
            interfaces.push_back(&implemented);
        }
        if (!current->base)
            break;
        current = &class_specialization(
            current->base->specialization_identity);
    }
    // A handle of a class converts to each interface class it implements,
    // and to the interface classes those extend (IEEE 1800-2017 8.26).
    for (std::size_t index { }; index < interfaces.size(); ++index) {
        const auto& implemented = *interfaces[index];
        if (std::ranges::find(descriptor.assignable_declared_types,
                implemented.declaration_identity)
            != descriptor.assignable_declared_types.end()) {
            continue;
        }
        descriptor.assignable_declared_types.push_back(
            implemented.declaration_identity);
        const auto interface_class = std::ranges::find(
            built.compiled_systemverilog_class_specializations,
            implemented.specialization_identity,
            &semantic::sv::ClassSpecialization::specialization_identity);
        if (interface_class
            == built.compiled_systemverilog_class_specializations.end()) {
            continue;
        }
        if (interface_class->base) {
            interfaces.push_back(&*interface_class->base);
        }
        for (const auto& extended : interface_class->interfaces) {
            interfaces.push_back(&extended);
        }
    }
    for (const auto& property : specialization.properties) {
        if (!property.static_storage) {
            descriptor.properties.push_back(
                class_property_descriptor(
                    property, built.systemverilog_hir, true));
        }
    }
    const auto declaration = std::ranges::find(
        built.systemverilog_hir.classes(),
        specialization.declaration_identity,
        &semantic::sv::ClassDeclaration::canonical_identity);
    if (declaration != built.systemverilog_hir.classes().end()) {
        for (const auto& constraint : declaration->composed_constraints) {
            if (constraint.override_legal) {
                descriptor.constraint_modes.emplace_back(
                    constraint.selected_identity,
                    constraint.mode_enabled);
            }
        }
    } else {
        for (const auto& constraint : specialization.constraints) {
            descriptor.constraint_modes.emplace_back(
                constraint.selected_identity, constraint.mode_enabled);
        }
    }
    return class_heap.allocate(descriptor);
}

[[nodiscard]] runtime::SystemVerilogClassHandle Simulation::Impl::construct_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals,
    const std::span<const std::string> actual_names,
    const std::string_view allocation_scope,
    const runtime::SystemVerilogUvmRootHandle requested_root,
    const std::optional<runtime::simir::ProcessId> owner_design_process,
    const runtime::SystemVerilogClassHandle owner_context_handle,
    const std::optional<runtime::simir::ProcessId> report_process)
{
    // `@no-constructor:` allocates the object for a constructor the
    // elaborated process runs itself.
    constexpr std::string_view lowered_constructor { "@no-constructor:" };
    const bool run_constructor
        = !specialization_identity.starts_with(lowered_constructor);
    const auto identity = run_constructor
        ? specialization_identity
        : specialization_identity.substr(lowered_constructor.size());
    const auto& specialization = class_specialization(identity);
    ClassOwnerParameterValues owner_parameters;
    std::optional<runtime::simir::ProcessId> owner_process_context;
    bool owner_context_available { };
    if (owner_context_handle != 0U) {
        const auto inherited = class_owner_parameter_values.find(
            owner_context_handle);
        if (inherited != class_owner_parameter_values.end()) {
            owner_parameters = inherited->second.parameters;
            owner_process_context = inherited->second.owner_design_process;
            owner_context_available = true;
        }
    }
    if (owner_design_process) {
        owner_process_context = owner_design_process;
        const semantic::design::ProcessOccurrence* owner_occurrence = nullptr;
        for (const auto& process : built.design_ir.processes()) {
            if (process.runtime_index != *owner_design_process) {
                continue;
            }
            if (owner_occurrence != nullptr) {
                throw std::logic_error {
                    "class allocation owner process is ambiguous"
                };
            }
            owner_occurrence = &process;
        }
        if (owner_occurrence == nullptr) {
            throw std::logic_error {
                "class allocation owner process has no design occurrence"
            };
        }
        const auto* exact = exact_specialization(
            built.design_ir, owner_occurrence->specialization);
        if (exact == nullptr) {
            throw std::logic_error {
                "class allocation owner has no exact specialization"
            };
        }
        const auto& owner_specialization = *exact;
        if (owner_specialization.language
                != semantic::Language::system_verilog
            || !owner_specialization.unit.valid()
            || !owner_specialization.scope.valid()) {
            throw std::logic_error {
                "class allocation owner specialization is not a resolved "
                "SystemVerilog unit"
            };
        }
        const semantic::sv::Unit* owner_unit = nullptr;
        for (const auto& unit : built.systemverilog_hir.units()) {
            if (unit.id != owner_specialization.unit) {
                continue;
            }
            if (owner_unit != nullptr) {
                throw std::logic_error {
                    "class allocation owner has ambiguous HIR unit identity"
                };
            }
            owner_unit = &unit;
        }
        if (owner_unit == nullptr
            || owner_unit->scope != owner_specialization.scope) {
            throw std::logic_error {
                "class allocation owner has no exact HIR unit scope"
            };
        }
        const auto owner_constant_declaration = [&](
            const semantic::design::ParameterValue& parameter)
            -> const semantic::sv::Declaration* {
            const auto is_owner_declaration = [&](
                const semantic::DeclarationId declaration_id) {
                return std::ranges::find(
                           owner_unit->declarations, declaration_id)
                    != owner_unit->declarations.end();
            };
            if (parameter.declaration) {
                if (!is_owner_declaration(*parameter.declaration)) {
                    return nullptr;
                }
                const auto* declaration
                    = built.systemverilog_hir.find_declaration(
                        *parameter.declaration);
                if (declaration == nullptr
                    || declaration->name != parameter.name) {
                    throw std::logic_error {
                        "class allocation owner parameter identity is stale"
                    };
                }
                if (declaration->form
                        != semantic::sv::DeclarationForm::parameter
                    && declaration->form
                        != semantic::sv::DeclarationForm::local_parameter
                    && declaration->form
                        != semantic::sv::DeclarationForm::type_parameter) {
                    return nullptr;
                }
                return &*declaration;
            }
            const semantic::sv::Declaration* selected = nullptr;
            for (const auto declaration_id : owner_unit->declarations) {
                const auto* declaration
                    = built.systemverilog_hir.find_declaration(declaration_id);
                if (declaration == nullptr
                    || declaration->name != parameter.name
                    || (declaration->form
                            != semantic::sv::DeclarationForm::parameter
                        && declaration->form
                            != semantic::sv::DeclarationForm::local_parameter
                        && declaration->form
                            != semantic::sv::DeclarationForm::type_parameter)) {
                    continue;
                }
                if (selected != nullptr) {
                    throw std::logic_error {
                        "class allocation owner parameter name is ambiguous"
                    };
                }
                selected = &*declaration;
            }
            return selected;
        };
        const auto packed_parameter_value = [&](
            const semantic::design::ParameterValue& parameter,
            const semantic::sv::Declaration& declaration)
            -> std::optional<runtime::PackedLogic4> {
            if (const auto decoded
                = elaboration::decode_hir_systemverilog_constant(
                    parameter.identity);
                decoded && decoded->packed.width() > 0U
                && decoded->packed.width() <= 64U) {
                return decoded->packed;
            }
            if (!declaration.type
                || !declaration.type->executable_width
                || *declaration.type->executable_width == 0U
                || *declaration.type->executable_width > 64U
                || declaration.type->container_form
                || !declaration.type->value_form
                || (*declaration.type->value_form
                        != semantic::sv::TypeForm::packed_integral
                    && *declaration.type->value_form
                        != semantic::sv::TypeForm::enumeration)) {
                return std::nullopt;
            }
            std::int64_t value { };
            const auto text = std::string_view { parameter.value };
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc { }
                || parsed.ptr != text.data() + text.size()) {
                return std::nullopt;
            }
            return runtime::PackedLogic4::from_aval_bval(
                static_cast<std::size_t>(
                    *declaration.type->executable_width),
                static_cast<std::uint64_t>(value), 0U);
        };
        owner_parameters.clear();
        for (const auto& parameter : owner_specialization.parameters) {
            const auto* declaration = owner_constant_declaration(parameter);
            if (declaration == nullptr) {
                // Specialization records also retain imported package values
                // and other identity metadata. They are not bindings in the
                // exact owning module's lexical scope.
                continue;
            }
            const bool module_parameter = declaration->form
                == semantic::sv::DeclarationForm::parameter;
            const auto value = packed_parameter_value(parameter, *declaration);
            if (!module_parameter && !value) {
                // A localparam can be evaluated from its retained HIR
                // initializer using this owner's exact parameter bindings.
                // Leave it absent here so that path is available.
                continue;
            }
            const bool inserted = owner_parameters.emplace(
                declaration->id, value).second;
            if (!inserted) {
                throw std::logic_error {
                    "class allocation owner has duplicate parameter "
                    "declaration identities"
                };
            }
        }
        owner_context_available = true;
    }
    const auto handle = allocate_class(
        specialization.specialization_identity,
        declared_type,
        allocation_scope);
    try {
        if (owner_context_available) {
            const auto [owner, inserted] = class_owner_parameter_values.emplace(
                handle, ClassOwnerParameterContext { std::move(owner_parameters), owner_process_context });
            (void)owner;
            if (!inserted) {
                throw std::logic_error {
                    "class allocation reused a live owner context handle"
                };
            }
        }
    } catch (...) {
        (void)class_heap.release(handle);
        throw;
    }
    const auto before = packed_class_snapshot();
    const auto component_specialization = is_systemverilog_uvm_type(
        built.compiled_systemverilog_class_specializations,
        specialization,
        "uvm_component");
    bool object_initialized { };
    bool automatic_root_created { };
    runtime::SystemVerilogUvmRootHandle automatic_root { };
    std::string automatic_root_identity;
    try {
        if (is_systemverilog_uvm_type(
                built.compiled_systemverilog_class_specializations,
                specialization,
                "uvm_object")) {
            uvm_objects.initialize(handle);
            object_initialized = true;
            std::size_t name_index { };
            if (!actual_names.empty()) {
                const auto found = std::ranges::find(actual_names, "name");
                if (found != actual_names.end()) {
                    name_index = static_cast<std::size_t>(
                        std::distance(actual_names.begin(), found));
                }
            }
            if (name_index < string_actuals.size()
                && !string_actuals[name_index].empty()) {
                uvm_objects.set_name(handle, string_actuals[name_index]);
            }
        }
        if (component_specialization) {
            std::size_t name_index { };
            std::size_t parent_index { 1U };
            if (!actual_names.empty()) {
                const auto named_index = [&](const std::string_view name,
                                             const std::size_t fallback) {
                    const auto found = std::ranges::find(actual_names, name);
                    return found == actual_names.end()
                        ? fallback
                        : static_cast<std::size_t>(
                              std::distance(actual_names.begin(), found));
                };
                name_index = named_index("name", 0U);
                parent_index = named_index("parent", 1U);
            }
            std::string name = name_index < string_actuals.size()
                ? string_actuals[name_index]
                : std::string { };
            if (name.empty()) {
                name = "COMP_" + std::to_string(uvm_objects.instance_id(handle));
            }
            const auto parent = parent_index < actuals.size()
                ? actuals[parent_index].low_word().aval
                : runtime::SystemVerilogClassHandle { };
            auto root = requested_root;
            if (parent == 0 && root == 0) {
                automatic_root_identity = allocation_scope.empty()
                    ? "$simulation"
                    : std::string { allocation_scope };
                automatic_root_created = !uvm_roots_by_scope.contains(automatic_root_identity);
                root = component_root(allocation_scope);
                automatic_root = root;
            }
            uvm_components.initialize(handle, std::move(name), parent, root);
        }
        // The UVM base state exists once `super.new` has run, so a
        // user-derived constructor may already report through it; the
        // simulation's UVM services stand in for the library constructors.
        if (run_constructor) {
            invoke_source_constructor(
                specialization, handle, actuals, string_actuals, actual_names,
                report_process);
        }
    } catch (...) {
        class_owner_parameter_values.erase(handle);
        if (component_specialization) {
            if (uvm_components.contains(handle)) {
                try {
                    uvm_components.release(handle);
                } catch (...) {
                }
            } else {
                if (object_initialized)
                    uvm_objects.erase(handle);
                (void)class_heap.release(handle);
            }
            if (automatic_root_created
                && uvm_components.contains_root(automatic_root)) {
                try {
                    uvm_phases.unparticipate_standard_root(automatic_root);
                } catch (...) {
                }
                try {
                    uvm_components.destroy_root(automatic_root);
                } catch (...) {
                }
                uvm_roots_by_scope.erase(automatic_root_identity);
            }
        }
        if (component_specialization && class_heap.contains(handle)) {
            (void)class_heap.release(handle);
        }
        // HIR writes are not rolled back on this path. At the outermost
        // construction boundary, publish the current live-state diff; an
        // enclosing source call owns the snapshot when construction was nested.
        if (source_function_dispatch_depth == 0U) {
            try {
                notify_class_changes(before);
            } catch (...) {
            }
        }
        throw;
    }
    if (source_function_dispatch_depth == 0U)
        notify_class_changes(before);
    return handle;
}

[[nodiscard]] Simulation::Impl::PackedSnapshot
Simulation::Impl::packed_class_snapshot() const
{
    PackedSnapshot result;
    for (const auto handle : class_heap.live_handles()) {
        const auto& object = class_heap.object(handle);
        for (std::size_t index = 0; index < object.properties.size(); ++index) {
            const auto& property = object.properties[index];
            if (property.kind
                == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
                // A handle property changes when it refers to another
                // object (IEEE 1800-2017 9.4.2); the change hook sees only
                // packed values.
                result.emplace(
                    std::pair { handle, object.property_names[index] },
                    runtime::PackedLogic4::from_aval_bval(
                        64U, property.handle, 0U));
            } else if (property.packed.width() != 0) {
                result.emplace(
                    std::pair { handle, object.property_names[index] },
                    property.packed);
            }
        }
    }
    return result;
}

void Simulation::Impl::toggle_class_heap_change()
{
    if (!class_heap_change_signal) {
        return;
    }
    const auto& current = interpreter->signal_value(*class_heap_change_signal);
    const bool high = current.width() != 0U
        && current.get(0U) == runtime::Logic4::one;
    interpreter->deposit_signal(*class_heap_change_signal,
        runtime::PackedLogic4 { 1U,
            high ? runtime::Logic4::zero : runtime::Logic4::one });
}

void Simulation::Impl::notify_class_changes(const PackedSnapshot& before)
{
    if ((!class_property_change_hook && !class_heap_change_signal)
        || interpreter->package_static_initialization_active())
        return;
    const auto after = packed_class_snapshot();
    bool changed = false;
    for (const auto& [identity, value] : after) {
        const auto prior = before.find(identity);
        if (prior == before.end() || prior->second != value) {
            changed = true;
            if (class_property_change_hook
                && class_heap.property(identity.first, identity.second).kind
                    != runtime::SystemVerilogClassPropertyKind::ClassHandle) {
                class_property_change_hook(
                    identity.first, identity.second, value,
                    interpreter->scheduler().now(),
                    interpreter->scheduler().delta());
            }
        }
    }
    if (changed) {
        toggle_class_heap_change();
    }
}

[[nodiscard]] Simulation::Impl::StaticPackedSnapshot
Simulation::Impl::packed_static_snapshot() const
{
    StaticPackedSnapshot result;
    for (const auto& state : class_static_store.snapshots()) {
        for (std::size_t index = 0; index < state.properties.size(); ++index) {
            if (state.properties[index].packed.width() != 0) {
                result.emplace(
                    std::pair { state.specialization_identity,
                        state.property_names[index] },
                    state.properties[index].packed);
            }
        }
    }
    return result;
}

void Simulation::Impl::notify_static_changes(const StaticPackedSnapshot& before)
{
    if ((!class_static_property_change_hook && !class_heap_change_signal)
        || interpreter->package_static_initialization_active())
        return;
    bool changed = false;
    for (const auto& [identity, value] : packed_static_snapshot()) {
        const auto prior = before.find(identity);
        if (prior == before.end() || prior->second != value) {
            changed = true;
            if (class_static_property_change_hook) {
                class_static_property_change_hook(
                    identity.first, identity.second, value,
                    interpreter->scheduler().now(),
                    interpreter->scheduler().delta());
            }
        }
    }
    if (changed) {
        toggle_class_heap_change();
    }
}

[[nodiscard]] runtime::SystemVerilogClassInvocationResult
Simulation::Impl::invoke_class_method(
    const std::string_view canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
    const std::optional<std::uint32_t> virtual_slot)
{
    const auto before = packed_class_snapshot();
    const auto static_before = packed_static_snapshot();
    auto result = virtual_slot
        ? class_methods.invoke_virtual(*virtual_slot, this_handle, actuals)
        : class_methods.invoke(canonical_method, this_handle, actuals);
    notify_class_changes(before);
    notify_static_changes(static_before);
    return result;
}

[[nodiscard]] std::optional<runtime::simir::SignalId>
Simulation::Impl::backdoor_signal(const std::string_view path) const noexcept
{
    const auto found = std::ranges::find_if(
        built.design_ir.objects(), [&](const auto& object) {
            return design_object_is_signal_bearing(object)
                && object.path.valid()
                && object.path.value() < built.design_ir.hierarchy_paths().size()
                && built.design_ir.path(object.path) == path
                && object.runtime_index
                    <= std::numeric_limits<runtime::simir::SignalId>::max();
        });
    return found == built.design_ir.objects().end()
        ? std::nullopt
        : std::optional<runtime::simir::SignalId> { static_cast<
              runtime::simir::SignalId>(found->runtime_index) };
}

#if defined(FSIM_HAS_LLVM)
void Simulation::Impl::request_background_jit_compilation(
    const bool force_adaptive)
{
    {
        std::scoped_lock lock { jit_background_mutex };
        jit_background_requested = true;
        jit_background_forced |= force_adaptive;
    }
    jit_background_condition.notify_all();
}

void Simulation::Impl::cancel_background_jit_compilation() noexcept
{
    {
        std::scoped_lock lock { jit_background_mutex };
        jit_background_cancelled = true;
    }
    jit_background_condition.notify_all();
}

#endif

Simulation::Impl::~Impl()
{
#if defined(FSIM_HAS_LLVM)
    // Materialization jobs retain pointers to process programs owned by
    // the interpreter. Normal simulation may deliberately finish before
    // background promotion, so join while both the interpreter and JIT
    // are still alive rather than relying on reverse member destruction.
    cancel_background_jit_compilation();
    if (jit_materialization.valid()) {
        jit_materialization.wait();
    }
#endif
    if (coverage_database_path && lifecycle != Lifecycle::finished) {
        try {
            save_coverage_database();
        } catch (...) {
        }
    }
    if (vpi_started && !vpi_ended) {
        try {
            end_vpi();
        } catch (...) {
        }
    }
    if (systemc_start_attempted && !systemc_ended) {
        try {
            end_systemc();
        } catch (...) {
        }
    }
    if (vpi_registry && vpi_value_state_observer) {
        (void)vpi_registry->remove_value_state_observer(
            *vpi_value_state_observer);
    }
}

[[nodiscard]] runtime::SystemVerilogVpiStoredValue Simulation::Impl::vpi_value(
    const SignalId signal,
    PackedLogic4 value) const
{
    return systemverilog_vpi_signal_value(
        std::move(value), vpi_scalar_kinds.at(signal),
        vpi_categories.at(signal));
}

[[nodiscard]] runtime::SystemVerilogVpiStoredValue Simulation::Impl::vpi_driver_value(
    const SignalId signal,
    const SystemVerilogVpiDriverBinding& binding) const
{
    auto result = vpi_value(signal,
        interpreter->driver_value(binding.process, signal));
    result.strength = binding.strength;
    return result;
}

void Simulation::Impl::require_vpi_value(
    const runtime::SystemVerilogVpiValueError error,
    const std::string_view operation)
{
    if (error == runtime::SystemVerilogVpiValueError::None) {
        return;
    }
    throw std::logic_error { "live VPI " + std::string { operation }
        + " failed with error "
        + std::to_string(static_cast<unsigned>(error)) };
}

[[nodiscard]] runtime::SystemVerilogVpiValueError Simulation::Impl::apply_vpi_value_state(
    const runtime::SystemVerilogVpiValueStateUpdate& update)
{
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    try {
        if (vpi_read_only_container_words.contains(update.object)) {
            return runtime::SystemVerilogVpiValueError::ReadOnly;
        }
        const auto word = vpi_word_handles.find(update.object);
        if (word != vpi_word_handles.end()) {
            if (update.forced_value) {
                return runtime::SystemVerilogVpiValueError::ReadOnly;
            }
            auto value = interpreter->container_object_value_snapshot(
                word->second.first);
            auto& element = value.elements.at(word->second.second);
            const auto replacement = systemverilog_vpi_packed_value(
                update.value,
                vpi_container_scalar_kinds.at(word->second.first),
                vpi_container_categories.at(word->second.first));
            if (element != replacement) {
                element = replacement;
                interpreter->deposit_container_object(
                    word->second.first, std::move(value));
            }
            return runtime::SystemVerilogVpiValueError::None;
        }
        const auto signal = vpi_handle_signals.find(update.object);
        if (signal == vpi_handle_signals.end()) {
            return runtime::SystemVerilogVpiValueError::None;
        }
        const auto stored = systemverilog_vpi_packed_value(
            update.value, vpi_scalar_kinds.at(signal->second),
            vpi_categories.at(signal->second));
        const bool was_forced
            = vpi_forced_signals.contains(signal->second);
        for (const auto alias : vpi_signal_handles.at(signal->second)) {
            if (alias == update.object) {
                continue;
            }
            require_vpi_value(
                vpi_registry->update_bound_value(alias, update.value),
                "alias stored-value publication");
            if (update.forced_value) {
                require_vpi_value(
                    vpi_registry->update_forced_value(
                        alias, *update.forced_value),
                    "alias forced-value publication");
            } else if (was_forced) {
                const auto released
                    = vpi_registry->release_bound_force(alias);
                if (released != runtime::SystemVerilogVpiValueError::None
                    && released
                        != runtime::SystemVerilogVpiValueError::NotForced) {
                    require_vpi_value(
                        released, "alias force release publication");
                }
            }
        }
        if (interpreter->stored_signal_value_snapshot(signal->second)
            != stored) {
            interpreter->deposit_signal(signal->second, stored);
        }
        if (update.forced_value) {
            const auto forced = systemverilog_vpi_packed_value(
                *update.forced_value,
                vpi_scalar_kinds.at(signal->second),
                vpi_categories.at(signal->second));
            if (!interpreter->signal_is_forced(signal->second)
                || interpreter->signal_value_snapshot(signal->second)
                    != forced) {
                interpreter->force_signal(signal->second, forced);
            }
            vpi_forced_signals.insert(signal->second);
        } else {
            if (interpreter->signal_is_forced(signal->second)) {
                interpreter->release_signal(signal->second);
            }
            vpi_forced_signals.erase(signal->second);
        }
        return runtime::SystemVerilogVpiValueError::None;
    } catch (...) {
        return runtime::SystemVerilogVpiValueError::ResourceLimit;
    }
}

void Simulation::Impl::publish_vpi_stored_signal(const SignalId signal)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    if (vpi_event_handles.contains(signal)) {
        publish_vpi_event(signal);
    }
    const auto objects = vpi_signal_handles.find(signal);
    if (objects == vpi_signal_handles.end()) {
        return;
    }
    for (const auto object : objects->second) {
        require_vpi_value(
            vpi_registry->update_bound_value(
                object,
                vpi_value(signal,
                    interpreter->stored_signal_value_snapshot(signal))),
            "stored-value publication");
    }
    const auto drivers = vpi_driver_bindings.find(signal);
    if (drivers != vpi_driver_bindings.end()) {
        for (const auto& driver : drivers->second) {
            require_vpi_value(
                vpi_registry->update_bound_value(
                    driver.handle,
                    vpi_driver_value(signal, driver)),
                "driver publication");
        }
    }
}

void Simulation::Impl::publish_vpi_package_startup_values()
{
    if (package_static_startup_values_published) {
        return;
    }
    if (!vpi_runtime_updates_enabled || !vpi_registry) {
        return;
    }
    for (const auto signal : package_static_startup_signal_ids) {
        const auto handles = vpi_signal_handles.find(signal);
        if (handles == vpi_signal_handles.end()) {
            continue;
        }
        const auto value = vpi_value(
            signal, interpreter->stored_signal_value_snapshot(signal));
        for (const auto handle : handles->second) {
            require_vpi_value(
                vpi_registry->initialize_bound_value(handle, value),
                "package startup baseline publication");
        }
    }
    package_static_startup_values_published = true;
}

void Simulation::Impl::start_interpreter()
{
    interpreter->start();
    publish_vpi_package_startup_values();
    refresh_observation_hooks();
}

void Simulation::Impl::publish_vpi_driver(
    const runtime::simir::ProcessId process,
    const SignalId signal)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto drivers = vpi_driver_bindings.find(signal);
    if (drivers == vpi_driver_bindings.end()) {
        return;
    }
    for (const auto& driver : drivers->second) {
        if (driver.process != process) {
            continue;
        }
        require_vpi_value(
            vpi_registry->update_bound_value(
                driver.handle,
                vpi_driver_value(signal, driver)),
            "driver publication");
    }
}

void Simulation::Impl::publish_vpi_container(
    const runtime::simir::ContainerObjectId object)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto words = vpi_container_words.find(object);
    if (words == vpi_container_words.end()) {
        return;
    }
    const auto value
        = interpreter->container_object_value_snapshot(object);
    for (const auto& [handle, ordinal] : words->second) {
        require_vpi_value(
            vpi_registry->update_bound_value(handle,
                systemverilog_vpi_signal_value(
                    value.elements.at(ordinal),
                    vpi_container_scalar_kinds.at(object),
                    vpi_container_categories.at(object))),
            "memory-word publication");
    }
}

void Simulation::Impl::publish_vpi_event(const SignalId event)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto objects = vpi_event_handles.find(event);
    if (objects == vpi_event_handles.end()) {
        return;
    }
    for (const auto object : objects->second) {
        const auto dispatched = vpi_callbacks->dispatch_named_event(object);
        if (dispatched
            != runtime::SystemVerilogVpiCallbackError::None) {
            throw std::logic_error {
                "live VPI named-event callback dispatch failed with error "
                + std::to_string(static_cast<unsigned>(dispatched))
            };
        }
    }
}

void Simulation::Impl::publish_vpi_signal(
    const SignalId signal,
    const PackedLogic4& value)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto objects = vpi_signal_handles.find(signal);
    if (objects == vpi_signal_handles.end()) {
        return;
    }
    if (interpreter->signal_is_forced(signal)) {
        for (const auto object : objects->second) {
            require_vpi_value(
                vpi_registry->update_forced_value(
                    object, vpi_value(signal, value)),
                "forced-value publication");
        }
        vpi_forced_signals.insert(signal);
    } else {
        for (const auto object : objects->second) {
            require_vpi_value(
                vpi_registry->update_bound_value(
                    object,
                    vpi_value(signal,
                        interpreter->stored_signal_value_snapshot(signal))),
                "effective-value publication");
        }
        if (vpi_forced_signals.erase(signal) != 0U) {
            for (const auto object : objects->second) {
                const auto released
                    = vpi_registry->release_bound_force(object);
                if (released != runtime::SystemVerilogVpiValueError::None
                    && released
                        != runtime::SystemVerilogVpiValueError::NotForced) {
                    require_vpi_value(
                        released, "force release publication");
                }
            }
        }
    }
}

void Simulation::Impl::publish_vpi_assertion(
    const ConcurrentAssertionEvent& event)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    const auto object = vpi_assertion_handles.find(event.process);
    if (object == vpi_assertion_handles.end() || !vpi_assertions) {
        return;
    }
    runtime::SystemVerilogVpiAssertionEvent published;
    published.kind
        = event.kind == ConcurrentAssertionCoverageKind::assumption
        ? runtime::SystemVerilogVpiAssertionKind::Assumption
        : event.kind == ConcurrentAssertionCoverageKind::cover
        ? runtime::SystemVerilogVpiAssertionKind::Cover
        : event.kind == ConcurrentAssertionCoverageKind::restriction
        ? runtime::SystemVerilogVpiAssertionKind::Restriction
        : runtime::SystemVerilogVpiAssertionKind::Assertion;
    published.outcome
        = event.outcome == ConcurrentAssertionOutcome::pass
        ? runtime::SystemVerilogVpiAssertionOutcome::Success
        : event.outcome == ConcurrentAssertionOutcome::failure
        ? runtime::SystemVerilogVpiAssertionOutcome::Failure
        : event.outcome == ConcurrentAssertionOutcome::disabled
        ? runtime::SystemVerilogVpiAssertionOutcome::Disabled
        : event.outcome == ConcurrentAssertionOutcome::vacuous
        ? runtime::SystemVerilogVpiAssertionOutcome::Vacuous
        : runtime::SystemVerilogVpiAssertionOutcome::Aborted;
    published.name = event.name;
    published.process = event.process;
    published.instance_identity = event.instance_identity;
    published.slot = event.slot;
    published.source_span = event.source_span;
    published.action_suppressed = event.action_suppressed;
    const auto error = vpi_assertions->observe(
        object->second, std::move(published));
    if (error != runtime::SystemVerilogVpiAssertionApiError::None) {
        throw std::logic_error {
            "live VPI assertion callback dispatch failed with error "
            + std::to_string(static_cast<unsigned>(error))
        };
    }
}

void Simulation::Impl::start_vpi()
{
    if (vpi_started) {
        return;
    }
    const auto sealed = vpi_systems->seal_registrations();
    if (sealed != runtime::SystemVerilogVpiSystemError::None) {
        throw std::logic_error { "failed to seal VPI system registrations" };
    }
    const auto callback = vpi_callbacks->dispatch_lifecycle_now(
        runtime::SystemVerilogVpiCallbackKind::StartOfSimulation);
    if (callback != runtime::SystemVerilogVpiCallbackError::None) {
        throw std::logic_error { "failed to dispatch VPI start callbacks" };
    }
    vpi_started = true;
}

void Simulation::Impl::end_vpi()
{
    if (!vpi_started || vpi_ended) {
        return;
    }
    const auto callback = vpi_callbacks->dispatch_lifecycle_now(
        runtime::SystemVerilogVpiCallbackKind::EndOfSimulation);
    if (callback != runtime::SystemVerilogVpiCallbackError::None) {
        throw std::logic_error { "failed to dispatch VPI end callbacks" };
    }
    vpi_control->mark_finished();
    vpi_ended = true;
}

void Simulation::Impl::start_systemc()
{
    if (systemc_start_attempted) {
        return;
    }
    systemc_start_attempted = true;
    for_each_systemc_registry([&](auto& registry, const auto& roots) {
        registry.start_simulation(roots);
    });
}

void Simulation::Impl::end_systemc(
    const std::optional<runtime::SimulationTick> current_time)
{
    if (!systemc_start_attempted || systemc_ended) {
        return;
    }
    for_each_systemc_registry([&](auto& registry, const auto& roots) {
        registry.end_simulation(
            roots,
            current_time.value_or(interpreter->scheduler().now()));
    });
    systemc_ended = true;
}
} // namespace fsim::app
