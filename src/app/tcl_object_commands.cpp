// SPDX-License-Identifier: Apache-2.0
#include "tcl_command_catalog.hpp"
#include "tcl_references.hpp"

#include "application_workspace_store.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::app::tcl_detail {

#if defined(FSIM_HAS_TCL)

namespace {

    using runtime::SystemVerilogClassHandle;
    using runtime::SystemVerilogClassPropertyKind;
    using runtime::SystemVerilogClassPropertyValue;
    using runtime::simir::ContainerElementKind;
    using runtime::simir::ContainerValue;
    using semantic::design::DesignIr;
    using semantic::design::InstanceOccurrence;
    using semantic::design::Object;
    using semantic::design::ObjectKind;
    using semantic::design::Specialization;

    constexpr std::string_view invalid_reference_code {
        "FSIM-TCL-OBJECT-0001"
    };
    constexpr std::string_view stale_reference_code {
        "FSIM-TCL-OBJECT-0002"
    };
    constexpr std::string_view not_found_code {
        "FSIM-TCL-OBJECT-0003"
    };
    constexpr std::string_view unsupported_code {
        "FSIM-TCL-OBJECT-0004"
    };
    constexpr std::string_view mutation_code {
        "FSIM-TCL-OBJECT-0005"
    };
    constexpr std::string_view value_code {
        "FSIM-TCL-OBJECT-0006"
    };

    Tcl_Obj* string_object(const std::string_view value)
    {
        const char* bytes = value.empty() ? "" : value.data();
        return Tcl_NewStringObj(bytes, tcl_size(value.size()));
    }

    std::string_view tcl_string(Tcl_Obj* value)
    {
        Tcl_Size length { };
        const char* bytes = Tcl_GetStringFromObj(value, &length);
        return { bytes, static_cast<std::size_t>(length) };
    }

    Tcl_Obj* unsigned_object(const std::uint64_t value)
    {
        if (value <= static_cast<std::uint64_t>(
                std::numeric_limits<Tcl_WideInt>::max())) {
            return Tcl_NewWideIntObj(static_cast<Tcl_WideInt>(value));
        }
        return string_object(std::to_string(value));
    }

    void list_append(Tcl_Interp* interpreter, Tcl_Obj* list, Tcl_Obj* value)
    {
        if (Tcl_ListObjAppendElement(interpreter, list, value) != TCL_OK) {
            throw std::runtime_error { "failed to construct a Tcl list" };
        }
    }

    void dict_put(
        Tcl_Interp* interpreter,
        Tcl_Obj* dictionary,
        const std::string_view key,
        Tcl_Obj* value)
    {
        if (Tcl_DictObjPut(
                interpreter, dictionary, string_object(key), value)
            != TCL_OK) {
            throw std::runtime_error { "failed to construct a Tcl dictionary" };
        }
    }

    int set_result(Tcl_Interp* interpreter, Tcl_Obj* result)
    {
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }

    int object_error(
        TclContext& context,
        Tcl_Interp* interpreter,
        const std::string_view code,
        const std::string_view message)
    {
        context.diagnostics.error(std::string { code }, std::string { message });
        Tcl_SetObjResult(interpreter, string_object(message));
        Tcl_Obj* error_code = Tcl_NewListObj(0, nullptr);
        list_append(interpreter, error_code, string_object("FSIM"));
        list_append(interpreter, error_code, string_object(code));
        Tcl_SetObjErrorCode(interpreter, error_code);
        return TCL_ERROR;
    }

    std::string_view value_error_code(const std::string_view message)
    {
        if (message.find("unsupported") != std::string_view::npos
            || message.find("requires an initialized simulation")
                != std::string_view::npos
            || message.find("have no value") != std::string_view::npos) {
            return unsupported_code;
        }
        return value_code;
    }

    const semantic::Model* current_semantics(const TclContext& context)
    {
        if (context.simulation) {
            return &context.simulation->semantics();
        }
        if (context.built) {
            return &context.built->semantics;
        }
        return nullptr;
    }

    const elaboration::ElaboratedDesign* current_adapter(
        const TclContext& context)
    {
        if (context.simulation) {
            return &context.simulation->runtime_adapter();
        }
        if (context.built) {
            return &context.built->design;
        }
        return nullptr;
    }

    std::span<const semantic::sv::ClassSpecialization> class_specializations(
        const TclContext& context)
    {
        if (context.simulation) {
            return context.simulation->class_specializations();
        }
        if (context.built) {
            return context.built->compiled_systemverilog_class_specializations;
        }
        return { };
    }

    std::string_view language_name(const semantic::Language language)
    {
        switch (language) {
        case semantic::Language::vhdl:
            return "vhdl";
        case semantic::Language::verilog:
            return "verilog";
        case semantic::Language::system_verilog:
            return "systemverilog";
        case semantic::Language::systemc:
            return "systemc";
        }
        return "unknown";
    }

    std::string_view object_kind_name(const ObjectKind kind)
    {
        switch (kind) {
        case ObjectKind::signal:
            return "signal";
        case ObjectKind::string:
            return "string";
        case ObjectKind::container:
            return "container";
        case ObjectKind::protected_object:
            return "protected";
        case ObjectKind::protected_member:
            return "protected_member";
        case ObjectKind::systemc_module:
            return "systemc_module";
        case ObjectKind::systemc_port:
            return "systemc_port";
        case ObjectKind::systemc_event:
            return "systemc_event";
        case ObjectKind::systemc_channel:
            return "systemc_channel";
        case ObjectKind::systemc_signal:
            return "systemc_signal";
        case ObjectKind::systemc_export:
            return "systemc_export";
        }
        return "unknown";
    }

    std::string object_identity(const Object& object)
    {
        return "object:" + std::to_string(object.id.value());
    }

    std::string instance_identity(const std::string_view path)
    {
        return "instance:" + std::string { path };
    }

    std::optional<std::uint64_t> parse_unsigned(const std::string_view text)
    {
        std::uint64_t value { };
        const auto converted = std::from_chars(
            text.data(), text.data() + text.size(), value, 10);
        if (converted.ec != std::errc { }
            || converted.ptr != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    std::optional<std::pair<SystemVerilogClassHandle, std::size_t>>
    class_property_identity(const std::string_view identity)
    {
        constexpr std::string_view prefix { "class-property:" };
        if (!identity.starts_with(prefix)) {
            return std::nullopt;
        }
        const auto payload = identity.substr(prefix.size());
        const auto separator = payload.find(':');
        if (separator == std::string_view::npos) {
            return std::nullopt;
        }
        const auto handle = parse_unsigned(payload.substr(0, separator));
        const auto property = parse_unsigned(payload.substr(separator + 1));
        if (!handle || !property
            || *property > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return std::pair {
            static_cast<SystemVerilogClassHandle>(*handle),
            static_cast<std::size_t>(*property)
        };
    }

    std::optional<SystemVerilogClassHandle> class_object_identity(
        const std::string_view identity)
    {
        constexpr std::string_view prefix { "class-object:" };
        if (!identity.starts_with(prefix)) {
            return std::nullopt;
        }
        const auto handle = parse_unsigned(identity.substr(prefix.size()));
        return handle
            ? std::optional<SystemVerilogClassHandle> {
                  static_cast<SystemVerilogClassHandle>(*handle)
              }
            : std::nullopt;
    }

    std::string_view short_member_name(const std::string_view identity)
    {
        const auto separator = identity.rfind("::");
        return separator == std::string_view::npos
            ? identity
            : identity.substr(separator + 2);
    }

    std::string_view type_name(
        const TclContext& context,
        const semantic::TypeReference& reference,
        const std::string_view fallback = { })
    {
        if (!reference.spelling.empty()) {
            return reference.spelling;
        }
        if (reference.target.valid()) {
            if (const auto* model = current_semantics(context)) {
                const auto& types = model->types();
                if (reference.target.value() < types.size()) {
                    return types[reference.target.value()].name;
                }
            }
        }
        return fallback;
    }

    std::string_view type_name(
        const semantic::Model& model,
        const semantic::TypeReference& reference,
        const std::string_view fallback = { })
    {
        if (!reference.spelling.empty()) {
            return reference.spelling;
        }
        if (reference.target.valid()) {
            const auto& types = model.types();
            if (reference.target.value() < types.size()) {
                return types[reference.target.value()].name;
            }
        }
        return fallback;
    }

    std::string_view type_name(
        const TclContext& context,
        const semantic::sv::TypeReference& reference,
        const std::string_view fallback = { })
    {
        if (!reference.class_identity.empty()) {
            return reference.class_identity;
        }
        return type_name(context, reference.target, fallback);
    }

    std::string_view type_name(
        const semantic::Model& model,
        const semantic::sv::TypeReference& reference,
        const std::string_view fallback = { })
    {
        if (!reference.class_identity.empty()) {
            return reference.class_identity;
        }
        return type_name(model, reference.target, fallback);
    }

    std::string_view visibility_name(semantic::sv::ClassVisibility visibility);

    const semantic::sv::SpecializedClassProperty* find_class_property(
        const TclContext& context,
        std::string_view dynamic_type,
        std::string_view name);

    const Specialization* object_specialization(
        const DesignIr& ir,
        const Object& object)
    {
        const auto& specializations = ir.specializations();
        if (!object.specialization.valid()
            || object.specialization.value() >= specializations.size()) {
            return nullptr;
        }
        return &specializations[object.specialization.value()];
    }

    const InstanceOccurrence* find_instance(
        const DesignIr& ir,
        const std::string_view path)
    {
        const auto& instances = ir.instances();
        const auto found = std::ranges::find_if(
            instances, [&](const InstanceOccurrence& candidate) {
                return ir.path(candidate.path) == path;
            });
        return found == instances.end() ? nullptr : &*found;
    }

    const Object* find_object(const DesignIr& ir, const std::uint32_t id)
    {
        const auto& objects = ir.objects();
        const auto found = std::ranges::find_if(
            objects, [&](const Object& candidate) {
                return candidate.id.value() == id;
            });
        return found == objects.end() ? nullptr : &*found;
    }

    const Object* find_object_path(
        const DesignIr& ir,
        const std::string_view path)
    {
        const auto& objects = ir.objects();
        const auto found = std::ranges::find_if(
            objects, [&](const Object& candidate) {
                return ir.path(candidate.path) == path;
            });
        return found == objects.end() ? nullptr : &*found;
    }

    struct LoadedObjectView {
        enum class Kind { instance,
            design_object,
            class_object,
            class_property };
        Kind kind { Kind::design_object };
        const InstanceOccurrence* instance { };
        const Object* object { };
        SystemVerilogClassHandle class_handle { };
        std::size_t property_index { };
        bool synthetic_root { };
        std::string root_path;
    };

    std::optional<LoadedObjectView> find_loaded_object(
        TclContext& context,
        const std::string_view identity)
    {
        const auto* ir = current_design_ir(context);
        if (!ir) {
            return std::nullopt;
        }
        constexpr std::string_view object_prefix { "object:" };
        constexpr std::string_view instance_prefix { "instance:" };
        if (identity.starts_with(object_prefix)) {
            const auto id = parse_unsigned(identity.substr(object_prefix.size()));
            if (!id || *id > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            const auto* object = find_object(*ir, static_cast<std::uint32_t>(*id));
            if (!object) {
                return std::nullopt;
            }
            LoadedObjectView view;
            view.kind = LoadedObjectView::Kind::design_object;
            view.object = object;
            return view;
        }
        if (identity.starts_with(instance_prefix)) {
            const auto path = identity.substr(instance_prefix.size());
            if (const auto* instance = find_instance(*ir, path)) {
                LoadedObjectView view;
                view.kind = LoadedObjectView::Kind::instance;
                view.instance = instance;
                return view;
            }
            const auto root = std::ranges::find_if(
                ir->roots(), [&](const semantic::HierarchyPathId id) {
                    return ir->path(id) == path;
                });
            if (root != ir->roots().end()) {
                LoadedObjectView view;
                view.kind = LoadedObjectView::Kind::instance;
                view.synthetic_root = true;
                view.root_path = std::string { path };
                return view;
            }
            return std::nullopt;
        }
        if (const auto handle = class_object_identity(identity)) {
            if (context.simulation && context.simulation->class_heap().contains(*handle)) {
                LoadedObjectView view;
                view.kind = LoadedObjectView::Kind::class_object;
                view.class_handle = *handle;
                return view;
            }
            return std::nullopt;
        }
        if (const auto property = class_property_identity(identity)) {
            if (!context.simulation
                || !context.simulation->class_heap().contains(property->first)) {
                return std::nullopt;
            }
            const auto& object = context.simulation->class_heap().object(property->first);
            if (property->second >= object.properties.size()
                || property->second >= object.property_names.size()) {
                return std::nullopt;
            }
            LoadedObjectView view;
            view.kind = LoadedObjectView::Kind::class_property;
            view.class_handle = property->first;
            view.property_index = property->second;
            return view;
        }
        return std::nullopt;
    }

    int reference_error(
        TclContext& context,
        Tcl_Interp* interpreter,
        const TclReferenceError error)
    {
        if (error == TclReferenceError::stale) {
            return object_error(
                context, interpreter, stale_reference_code,
                "fsim object reference is stale");
        }
        return object_error(
            context, interpreter, invalid_reference_code,
            "invalid fsim object reference");
    }

    struct AnyReference {
        TclReference reference;
        TclReferenceResolution resolution;
    };

    bool runtime_object_reference_is_live(
        TclContext& context,
        const std::string_view identity)
    {
        if (const auto handle = class_object_identity(identity)) {
            return context.simulation
                && context.simulation->class_heap().contains(*handle);
        }
        if (const auto property = class_property_identity(identity)) {
            if (!context.simulation
                || !context.simulation->class_heap().contains(property->first)) {
                return false;
            }
            const auto& object = context.simulation->class_heap().object(property->first);
            return property->second < object.properties.size()
                && property->second < object.property_names.size();
        }
        return true;
    }

    std::optional<AnyReference> resolve_reference(
        TclContext& context,
        Tcl_Interp* interpreter,
        const std::string_view token)
    {
        if (!context.references) {
            (void)object_error(
                context, interpreter, unsupported_code,
                "fsim object references are unavailable in this Tcl session");
            return std::nullopt;
        }
        auto loaded = context.references->resolve(
            token, TclReferenceKind::loaded_object);
        if (loaded) {
            if (!runtime_object_reference_is_live(
                    context, loaded.reference->identity)) {
                context.references->runtime_object_destroyed(
                    loaded.reference->identity);
                (void)reference_error(context, interpreter, TclReferenceError::stale);
                return std::nullopt;
            }
            return AnyReference { *loaded.reference, std::move(loaded) };
        }
        auto catalog = context.references->resolve(
            token, TclReferenceKind::catalog_definition);
        if (catalog) {
            return AnyReference { *catalog.reference, std::move(catalog) };
        }
        const auto error = loaded.error == TclReferenceError::stale
                || catalog.error == TclReferenceError::stale
            ? TclReferenceError::stale
            : TclReferenceError::invalid;
        (void)reference_error(context, interpreter, error);
        return std::nullopt;
    }

    std::string reference_for_loaded(TclContext& context, std::string identity)
    {
        if (!context.references) {
            throw std::runtime_error { "fsim object reference table is unavailable" };
        }
        return context.references->create_loaded_reference(identity);
    }

    std::string reference_for_catalog(
        TclContext& context,
        const std::string_view library,
        const std::string_view identity)
    {
        if (!context.references) {
            throw std::runtime_error { "fsim object reference table is unavailable" };
        }
        return context.references->create_catalog_reference(library, identity);
    }

    void add_dimension(
        Tcl_Interp* interpreter,
        Tcl_Obj* dimensions,
        const std::string_view kind,
        const std::int64_t left,
        const std::int64_t right)
    {
        Tcl_Obj* dimension = Tcl_NewDictObj();
        dict_put(interpreter, dimension, "kind", string_object(kind));
        dict_put(interpreter, dimension, "left", Tcl_NewWideIntObj(left));
        dict_put(interpreter, dimension, "right", Tcl_NewWideIntObj(right));
        const auto left_unsigned = static_cast<std::uint64_t>(left);
        const auto right_unsigned = static_cast<std::uint64_t>(right);
        const auto difference = left >= right
            ? left_unsigned - right_unsigned
            : right_unsigned - left_unsigned;
        const auto size = difference < std::numeric_limits<std::uint64_t>::max()
            ? difference + 1
            : 0;
        dict_put(interpreter, dimension, "size", unsigned_object(size));
        list_append(interpreter, dimensions, dimension);
    }

    Tcl_Obj* object_dimensions(
        Tcl_Interp* interpreter,
        const TclContext& context,
        const Object& object)
    {
        Tcl_Obj* dimensions = Tcl_NewListObj(0, nullptr);
        const auto* adapter = current_adapter(context);
        if (!adapter) {
            return dimensions;
        }
        if ((object.kind == ObjectKind::signal
                || object.kind == ObjectKind::systemc_signal)
            && object.runtime_index < adapter->signals().size()) {
            const auto& signal = adapter->signals()[object.runtime_index];
            if (signal.packed_range) {
                add_dimension(
                    interpreter, dimensions, "packed",
                    signal.packed_range->left, signal.packed_range->right);
            }
            if (signal.vhdl_array) {
                for (const auto& dimension : signal.vhdl_array->dimensions) {
                    if (dimension.range) {
                        add_dimension(
                            interpreter, dimensions, "unpacked",
                            dimension.range->left, dimension.range->right);
                    }
                }
            }
        }
        if (object.kind == ObjectKind::container
            && object.runtime_index < adapter->container_objects().size()) {
            const auto& container = adapter->container_objects()[object.runtime_index];
            for (const auto& [left, right] : container.type.dimensions) {
                add_dimension(interpreter, dimensions, "unpacked", left, right);
            }
        }
        return dimensions;
    }

    std::string object_parent_identity(
        const DesignIr& ir,
        const Object& object)
    {
        if (object.parent_object) {
            const auto* parent = find_object(ir, object.parent_object->value());
            if (parent) {
                return object_identity(*parent);
            }
        }
        const auto* specialization = object_specialization(ir, object);
        if (!specialization || !specialization->instance.valid()
            || specialization->instance.value() >= ir.instances().size()) {
            return { };
        }
        const auto& instance = ir.instances()[specialization->instance.value()];
        return instance_identity(ir.path(instance.path));
    }

    Tcl_Obj* object_info_dictionary(
        TclContext& context,
        Tcl_Interp* interpreter,
        const std::string_view token,
        const LoadedObjectView& view)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "reference", string_object(token));
        const auto* ir = current_design_ir(context);
        if (view.kind == LoadedObjectView::Kind::instance) {
            std::string path;
            std::string name;
            std::string target;
            std::string parent;
            const Specialization* specialization = nullptr;
            if (view.instance) {
                path = std::string { ir->path(view.instance->path) };
                name = view.instance->name;
                target = view.instance->target;
                if (view.instance->parent) {
                    const auto& parent_instance = ir->instances().at(
                        view.instance->parent->value());
                    parent = instance_identity(ir->path(parent_instance.path));
                }
                if (view.instance->specialization.valid()
                    && view.instance->specialization.value()
                        < ir->specializations().size()) {
                    specialization = &ir->specializations()[view.instance->specialization.value()];
                }
            } else {
                const auto identity = std::string_view { token };
                constexpr std::string_view prefix { "instance:" };
                path = identity.starts_with(prefix)
                    ? std::string { identity.substr(prefix.size()) }
                    : std::string { };
                const auto separator = path.find_last_of("./:");
                name = separator == std::string::npos
                    ? path
                    : path.substr(separator + 1);
            }
            dict_put(interpreter, result, "identity", string_object(path));
            dict_put(interpreter, result, "kind", string_object("instance"));
            dict_put(interpreter, result, "name", string_object(name));
            dict_put(interpreter, result, "path", string_object(path));
            dict_put(interpreter, result, "parent", string_object(parent));
            dict_put(interpreter, result, "type", string_object(specialization && !specialization->name.empty() ? specialization->name : target));
            dict_put(interpreter, result, "width", Tcl_NewWideIntObj(0));
            dict_put(interpreter, result, "dimensions", Tcl_NewListObj(0, nullptr));
            if (specialization) {
                dict_put(
                    interpreter, result, "language",
                    string_object(language_name(specialization->language)));
                dict_put(
                    interpreter, result, "library",
                    string_object(specialization->library));
            }
            return result;
        }
        if (view.kind == LoadedObjectView::Kind::design_object) {
            const auto& object = *view.object;
            const auto path = ir->path(object.path);
            const auto* specialization = object_specialization(*ir, object);
            std::string_view declared_type = object.external_type;
            if (declared_type.empty()) {
                declared_type = type_name(context, object.type);
            }
            const auto parent_identity = object_parent_identity(*ir, object);
            std::string parent;
            if (!parent_identity.empty()) {
                parent = reference_for_loaded(context, parent_identity);
            }
            dict_put(interpreter, result, "identity", string_object(path));
            dict_put(
                interpreter, result, "kind",
                string_object(object_kind_name(object.kind)));
            dict_put(interpreter, result, "name", string_object(object.name));
            dict_put(interpreter, result, "path", string_object(path));
            dict_put(interpreter, result, "parent", string_object(parent));
            dict_put(interpreter, result, "type", string_object(declared_type));
            dict_put(interpreter, result, "width", unsigned_object(object.width));
            dict_put(interpreter, result, "dimensions",
                object_dimensions(interpreter, context, object));
            dict_put(interpreter, result, "signed", Tcl_NewBooleanObj(object.signed_value));
            if (specialization) {
                dict_put(
                    interpreter, result, "language",
                    string_object(language_name(specialization->language)));
                dict_put(
                    interpreter, result, "library",
                    string_object(specialization->library));
            }
            if (object.source && object.source->valid()) {
                if (const auto* model = current_semantics(context)) {
                    const auto& spans = model->source_spans();
                    if (object.source->value() < spans.size()) {
                        const auto& span = spans[object.source->value()];
                        Tcl_Obj* provenance = Tcl_NewDictObj();
                        dict_put(interpreter, provenance, "path",
                            string_object(span.logical_name));
                        dict_put(interpreter, provenance, "line",
                            Tcl_NewWideIntObj(span.begin.line));
                        dict_put(interpreter, provenance, "column",
                            Tcl_NewWideIntObj(span.begin.column));
                        dict_put(interpreter, result, "provenance", provenance);
                    }
                }
            }
            return result;
        }
        if (!context.simulation) {
            throw std::runtime_error {
                "class object reflection requires an initialized simulation"
            };
        }
        const auto& heap = context.simulation->class_heap();
        const auto& class_object = heap.object(view.class_handle);
        if (view.kind == LoadedObjectView::Kind::class_object) {
            dict_put(interpreter, result, "identity",
                string_object("class:" + class_object.dynamic_type));
            dict_put(interpreter, result, "kind", string_object("class"));
            dict_put(interpreter, result, "name", string_object(class_object.dynamic_type));
            dict_put(interpreter, result, "path",
                string_object("$class." + class_object.dynamic_type));
            dict_put(interpreter, result, "parent", string_object(""));
            dict_put(interpreter, result, "type", string_object(class_object.dynamic_type));
            dict_put(interpreter, result, "width", Tcl_NewWideIntObj(0));
            dict_put(interpreter, result, "dimensions", Tcl_NewListObj(0, nullptr));
            dict_put(interpreter, result, "declared_type",
                string_object(class_object.declared_type));
            dict_put(interpreter, result, "dynamic_type",
                string_object(class_object.dynamic_type));
            return result;
        }
        const auto index = view.property_index;
        const auto& value = class_object.properties.at(index);
        const auto& identity = class_object.property_names.at(index);
        const auto name = short_member_name(identity);
        const auto* declaration = find_class_property(
            context, class_object.dynamic_type, identity);
        const std::string property_path = "$class." + class_object.dynamic_type
            + "." + identity;
        dict_put(interpreter, result, "identity", string_object(property_path));
        dict_put(interpreter, result, "kind", string_object("class_property"));
        dict_put(interpreter, result, "name", string_object(name));
        dict_put(interpreter, result, "path", string_object(property_path));
        dict_put(interpreter, result, "parent",
            string_object(reference_for_loaded(
                context, "class-object:" + std::to_string(view.class_handle))));
        std::string_view property_type = declaration
            ? type_name(context, declaration->type)
            : std::string_view { };
        if (property_type.empty()) {
            property_type = value.kind == SystemVerilogClassPropertyKind::String
                ? "string"
                : value.kind == SystemVerilogClassPropertyKind::ClassHandle
                ? "class_handle"
                : value.kind == SystemVerilogClassPropertyKind::Container
                ? "container"
                : "logic";
        }
        dict_put(interpreter, result, "type", string_object(property_type));
        dict_put(interpreter, result, "width",
            unsigned_object(value.packed.width()));
        dict_put(interpreter, result, "dimensions",
            Tcl_NewListObj(0, nullptr));
        if (declaration) {
            dict_put(interpreter, result, "visibility",
                string_object(visibility_name(declaration->visibility)));
            dict_put(interpreter, result, "static",
                Tcl_NewBooleanObj(declaration->static_storage));
            dict_put(interpreter, result, "constant",
                Tcl_NewBooleanObj(declaration->constant));
        }
        return result;
    }

    std::string_view visibility_name(const semantic::sv::ClassVisibility visibility)
    {
        switch (visibility) {
        case semantic::sv::ClassVisibility::public_access:
            return "public";
        case semantic::sv::ClassVisibility::protected_access:
            return "protected";
        case semantic::sv::ClassVisibility::local_access:
            return "local";
        }
        return "unknown";
    }

    const semantic::sv::SpecializedClassProperty* find_class_property(
        const TclContext& context,
        const std::string_view dynamic_type,
        const std::string_view name)
    {
        const auto specializations = class_specializations(context);
        for (const auto& specialization : specializations) {
            if (specialization.specialization_identity != dynamic_type
                && specialization.declaration_identity != dynamic_type) {
                continue;
            }
            const auto property = std::ranges::find_if(
                specialization.properties, [&](const auto& candidate) {
                    return candidate.name == name
                        || candidate.canonical_identity == name;
                });
            if (property != specialization.properties.end()) {
                return &*property;
            }
        }
        return nullptr;
    }

    bool is_class_type(
        const TclContext& context,
        const std::string_view type)
    {
        if (type.empty()) {
            return false;
        }
        const auto specializations = class_specializations(context);
        return std::ranges::any_of(
            specializations, [&](const auto& candidate) {
                return candidate.declaration_identity == type
                    || candidate.specialization_identity == type
                    || (type.size() < candidate.declaration_identity.size()
                        && candidate.declaration_identity.ends_with(
                            "::" + std::string { type }));
            });
    }

    std::optional<SystemVerilogClassHandle> class_handle_value(
        TclContext& context,
        const Object& object)
    {
        if (!context.simulation || !is_class_type(context, !object.external_type.empty() ? std::string_view { object.external_type } : type_name(context, object.type))) {
            return std::nullopt;
        }
        if (object.kind != ObjectKind::signal
            && object.kind != ObjectKind::systemc_signal) {
            return std::nullopt;
        }
        const auto value = context.simulation->read_signal(
            runtime::simir::SignalId {
                static_cast<std::uint32_t>(object.runtime_index) });
        if (value.width() < sizeof(SystemVerilogClassHandle) * 8U) {
            return std::nullopt;
        }
        const auto word = value.low_word();
        if (word.bval != 0) {
            return std::nullopt;
        }
        return word.aval;
    }

    Tcl_Obj* class_property_value(
        TclContext& context,
        Tcl_Interp* interpreter,
        const SystemVerilogClassPropertyValue& value)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "kind", string_object("class_property"));
        switch (value.kind) {
        case SystemVerilogClassPropertyKind::Bit2:
        case SystemVerilogClassPropertyKind::Logic4:
        case SystemVerilogClassPropertyKind::Logic9:
        case SystemVerilogClassPropertyKind::Integer:
            dict_put(interpreter, result, "shape", string_object("packed"));
            dict_put(interpreter, result, "width", unsigned_object(value.packed.width()));
            dict_put(interpreter, result, "value",
                string_object(value.packed.to_msb_string()));
            break;
        case SystemVerilogClassPropertyKind::String:
            dict_put(interpreter, result, "shape", string_object("scalar"));
            dict_put(interpreter, result, "value", string_object(value.string));
            break;
        case SystemVerilogClassPropertyKind::ClassHandle: {
            dict_put(interpreter, result, "shape", string_object("reference"));
            if (value.handle == 0) {
                dict_put(interpreter, result, "value", string_object(""));
                dict_put(interpreter, result, "null", Tcl_NewBooleanObj(1));
            } else if (context.simulation->class_heap().contains(value.handle)) {
                dict_put(interpreter, result, "value", string_object(reference_for_loaded(context, "class-object:" + std::to_string(value.handle))));
                dict_put(interpreter, result, "null", Tcl_NewBooleanObj(0));
            } else {
                dict_put(interpreter, result, "value", string_object(""));
                dict_put(interpreter, result, "stale", Tcl_NewBooleanObj(1));
            }
            break;
        }
        case SystemVerilogClassPropertyKind::Container: {
            dict_put(interpreter, result, "shape", string_object("composite"));
            Tcl_Obj* elements = Tcl_NewListObj(0, nullptr);
            if (value.handle_container) {
                for (const auto handle : value.handle_container->sequential_values()) {
                    if (handle != 0 && context.simulation->class_heap().contains(handle)) {
                        list_append(interpreter, elements, string_object(reference_for_loaded(context, "class-object:" + std::to_string(handle))));
                    } else {
                        list_append(interpreter, elements, string_object(""));
                    }
                }
                for (const auto& [key, handle] : value.handle_container->keyed_values()) {
                    Tcl_Obj* entry = Tcl_NewDictObj();
                    dict_put(interpreter, entry, "key", string_object(key));
                    dict_put(interpreter, entry, "value",
                        handle != 0 && context.simulation->class_heap().contains(handle)
                            ? string_object(reference_for_loaded(
                                  context,
                                  "class-object:" + std::to_string(handle)))
                            : string_object(""));
                    list_append(interpreter, elements, entry);
                }
            } else {
                for (const auto handle : value.handles) {
                    if (handle != 0 && context.simulation->class_heap().contains(handle)) {
                        list_append(interpreter, elements, string_object(reference_for_loaded(context, "class-object:" + std::to_string(handle))));
                    } else {
                        list_append(interpreter, elements, string_object(""));
                    }
                }
            }
            dict_put(interpreter, result, "value", elements);
            break;
        }
        }
        return result;
    }

    Tcl_Obj* container_value_object(
        TclContext& context,
        Tcl_Interp* interpreter,
        const ContainerValue& value,
        const std::size_t depth = 0)
    {
        if (depth > 32) {
            throw std::length_error { "container value nesting exceeds 32 levels" };
        }
        if (value.type.element_kind == ContainerElementKind::Aggregate
            && value.type.aggregate_value) {
            Tcl_Obj* result = Tcl_NewDictObj();
            for (std::size_t index = 0;
                index < value.nested_elements.size(); ++index) {
                const auto name = index < value.type.member_names.size()
                    ? value.type.member_names[index]
                    : std::to_string(index);
                dict_put(interpreter, result, name,
                    container_value_object(
                        context, interpreter, value.nested_elements[index], depth + 1));
            }
            return result;
        }
        Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
        const auto size = runtime::simir::container_value_size(value);
        for (std::size_t index = 0; index < size; ++index) {
            Tcl_Obj* element = nullptr;
            switch (value.type.element_kind) {
            case ContainerElementKind::Packed:
                element = string_object(value.elements.at(index).to_msb_string());
                break;
            case ContainerElementKind::Scalar: {
                const auto scalar = runtime::decode_systemverilog_scalar_payload(
                    value.elements.at(index), value.type.scalar_kind);
                if (!scalar) {
                    throw std::runtime_error { "container contains an invalid scalar value" };
                }
                const auto text = runtime::format_systemverilog_scalar(scalar.value);
                if (!text) {
                    throw std::runtime_error { "container scalar value cannot be formatted" };
                }
                element = string_object(text.text);
                break;
            }
            case ContainerElementKind::String:
                element = string_object(value.string_elements.at(index));
                break;
            case ContainerElementKind::Container:
            case ContainerElementKind::Aggregate:
                element = container_value_object(
                    context, interpreter, value.nested_elements.at(index), depth + 1);
                break;
            }
            if (value.type.associative) {
                Tcl_Obj* pair = Tcl_NewListObj(0, nullptr);
                list_append(
                    interpreter, pair,
                    value.type.string_indices
                        ? string_object(value.string_keys.at(index))
                        : string_object(value.keys.at(index).to_msb_string()));
                list_append(interpreter, pair, element);
                list_append(interpreter, result, pair);
            } else {
                list_append(interpreter, result, element);
            }
        }
        return result;
    }

    struct DefinitionMember {
        std::string name;
        std::string kind;
        std::string identity;
        std::string type;
        std::uint64_t width { };
        std::string visibility;
        bool static_storage { };
        bool constant { };
    };

    struct DefinitionRecord {
        std::string kind;
        std::string library;
        std::string name;
        std::string identity;
        std::string type;
        std::string parent_identity;
        std::uint64_t width { };
        std::vector<DefinitionMember> members;
    };

    bool owns_unit(
        const std::span<const library::UnitIndexEntry> owned_units,
        const library::UnitIndexEntry& candidate)
    {
        const auto identity = workspace::unit_identity(candidate);
        return std::ranges::any_of(owned_units, [&](const auto& owned) {
            return workspace::unit_identity(owned) == identity;
        });
    }

    bool owns_class(
        const std::span<const library::UnitIndexEntry> owned_units,
        const std::string_view identity)
    {
        return std::ranges::any_of(owned_units, [&](const auto& owned) {
            return owned.kind == "class" && owned.name == identity;
        });
    }

    std::vector<DefinitionRecord> definition_catalog(
        const semantic::CompiledDesign& compiled,
        const std::span<const semantic::sv::ClassSpecialization> specializations,
        const std::string_view source_library,
        const std::string_view logical_library,
        const std::string_view artifact_id,
        const std::span<const library::UnitIndexEntry> owned_units)
    {
        std::vector<DefinitionRecord> result;
        const auto& model = compiled.semantics;
        const auto& units = model.units();
        const auto& scopes = model.scopes();
        const auto compiled_units = application_detail::compiled_unit_metadata_entries(
            compiled, source_library);
        const auto package_entry = [&](const semantic::Unit& unit)
            -> const library::UnitIndexEntry* {
            const auto found = std::ranges::find_if(
                compiled_units, [&](const auto& entry) {
                    return entry.kind == "package" && entry.name == unit.name
                        && entry.language == language_name(unit.language);
                });
            return found == compiled_units.end() ? nullptr : &*found;
        };
        const auto scope_belongs_to_package = [&](
                                                  const semantic::ScopeId scope, const semantic::UnitId unit) {
            return scope.valid() && scope.value() < scopes.size()
                && scopes[scope.value()].unit == unit;
        };

        for (const auto& unit : units) {
            if (unit.kind != semantic::UnitKind::systemverilog_package
                && unit.kind != semantic::UnitKind::vhdl_package) {
                continue;
            }
            const auto* indexed_unit = package_entry(unit);
            if (indexed_unit == nullptr || !owns_unit(owned_units, *indexed_unit)) {
                continue;
            }
            DefinitionRecord package;
            package.kind = "package";
            package.library = logical_library;
            package.name = unit.name;
            package.identity = "package:" + std::string { artifact_id } + ":"
                + std::to_string(unit.id.value());
            package.type = language_name(unit.language);
            for (const auto& value : model.values()) {
                if (!scope_belongs_to_package(value.scope, unit.id)) {
                    continue;
                }
                package.members.push_back({ value.name,
                    "value",
                    "package-member:" + std::string { artifact_id } + ":"
                        + std::to_string(unit.id.value()) + ":value:"
                        + std::to_string(value.id.value()),
                    std::string { type_name(model, value.type) },
                    0,
                    { },
                    false,
                    false });
            }
            for (const auto& type : model.types()) {
                if (!scope_belongs_to_package(type.scope, unit.id)) {
                    continue;
                }
                package.members.push_back({ type.name,
                    "type",
                    "package-member:" + std::string { artifact_id } + ":"
                        + std::to_string(unit.id.value()) + ":type:"
                        + std::to_string(type.id.value()),
                    std::string { type_name(model, type.base) },
                    0,
                    { },
                    false,
                    false });
            }
            for (const auto& declaration : model.declarations()) {
                if (!scope_belongs_to_package(declaration.scope, unit.id)) {
                    continue;
                }
                const bool already_listed = std::ranges::any_of(
                    package.members, [&](const DefinitionMember& member) {
                        return member.name == declaration.name;
                    });
                if (!already_listed) {
                    package.members.push_back({ declaration.name,
                        "declaration",
                        "package-member:" + std::string { artifact_id } + ":"
                            + std::to_string(unit.id.value()) + ":declaration:"
                            + std::to_string(declaration.id.value()),
                        { },
                        0,
                        { },
                        false,
                        false });
                }
            }
            for (const auto& specialization : specializations) {
                if (!owns_class(owned_units, specialization.declaration_identity)
                    || !specialization.declaration_scope.valid()
                    || specialization.declaration_scope.value() >= scopes.size()
                    || scopes[specialization.declaration_scope.value()].unit != unit.id) {
                    continue;
                }
                const auto separator = specialization.declaration_identity.rfind("::");
                const auto name = separator == std::string::npos
                    ? specialization.declaration_identity
                    : specialization.declaration_identity.substr(separator + 2);
                package.members.push_back({ name,
                    "class",
                    "class:" + specialization.specialization_identity,
                    specialization.specialization_identity,
                    0,
                    { },
                    false,
                    false });
            }
            result.push_back(std::move(package));
        }

        for (const auto& specialization : specializations) {
            if (!owns_class(owned_units, specialization.declaration_identity)) {
                continue;
            }
            std::string source_unit_library;
            if (specialization.declaration_scope.valid()
                && specialization.declaration_scope.value() < scopes.size()) {
                const auto unit_id = scopes[specialization.declaration_scope.value()].unit;
                if (unit_id.valid() && unit_id.value() < units.size()) {
                    source_unit_library = units[unit_id.value()].library;
                }
            }
            if (source_unit_library.empty()) {
                source_unit_library = "work";
            }
            if (source_unit_library != source_library) {
                continue;
            }
            DefinitionRecord definition;
            definition.kind = "class";
            definition.library = logical_library;
            definition.name = specialization.declaration_identity;
            definition.identity = "class:" + specialization.specialization_identity;
            definition.type = specialization.specialization_identity;
            for (const auto& parameter : specialization.parameters) {
                definition.members.push_back({ parameter.name,
                    parameter.type_parameter ? "type_parameter" : "parameter",
                    "class-member:" + specialization.specialization_identity
                        + ":parameter:" + parameter.canonical_identity,
                    parameter.type
                        ? std::string { type_name(model, *parameter.type) }
                        : std::string { },
                    0,
                    { },
                    false,
                    true });
            }
            for (const auto& property : specialization.properties) {
                definition.members.push_back({ property.name,
                    "property",
                    "class-member:" + specialization.specialization_identity
                        + ":property:" + property.canonical_identity,
                    std::string { type_name(model, property.type) },
                    static_cast<std::uint64_t>(property.bit_width),
                    std::string { visibility_name(property.visibility) },
                    property.static_storage,
                    property.constant });
            }
            for (const auto& method : specialization.methods) {
                definition.members.push_back({ method.name,
                    method.kind == semantic::sv::ClassMethodKind::task
                        ? "task"
                        : "method",
                    "class-member:" + specialization.specialization_identity
                        + ":method:" + method.canonical_identity,
                    method.return_type
                        ? std::string { type_name(model, *method.return_type) }
                        : std::string { },
                    0,
                    std::string { visibility_name(method.visibility) },
                    method.static_method,
                    false });
            }
            for (const auto& constraint : specialization.constraints) {
                definition.members.push_back({ constraint.name,
                    "constraint",
                    "class-member:" + specialization.specialization_identity
                        + ":constraint:" + constraint.selected_identity,
                    { },
                    0,
                    { },
                    false,
                    false });
            }
            result.push_back(std::move(definition));
        }

        std::ranges::sort(result, [](const auto& left, const auto& right) {
            return std::tie(left.library, left.name, left.kind, left.identity)
                < std::tie(right.library, right.name, right.kind, right.identity);
        });
        for (auto& definition : result) {
            std::ranges::sort(definition.members, [](const auto& left, const auto& right) {
                return std::tie(left.name, left.kind, left.identity)
                    < std::tie(right.name, right.kind, right.identity);
            });
        }
        return result;
    }

    std::optional<std::vector<DefinitionRecord>> library_definition_catalog(
        TclContext& context,
        const std::optional<std::string_view> requested_library,
        std::string& error)
    {
        workspace::Store store(context.config.base_directory);
        std::vector<workspace::LibraryCatalog> catalogs;
        if (requested_library) {
            auto catalog = store.read_library(*requested_library, error);
            if (!catalog) {
                return std::nullopt;
            }
            catalogs.push_back(std::move(*catalog));
        } else {
            const auto locations = store.library_locations(error);
            if (!locations) {
                return std::nullopt;
            }
            for (const auto& location : *locations) {
                auto catalog = store.read_library(location.name, error);
                if (!catalog) {
                    return std::nullopt;
                }
                catalogs.push_back(std::move(*catalog));
            }
        }

        std::vector<DefinitionRecord> result;
        for (const auto& catalog : catalogs) {
            for (const auto& artifact_record : catalog.artifacts) {
                if (artifact_record.kind != workspace::ArtifactKind::Hdl
                    || !std::ranges::any_of(
                        artifact_record.units, [](const auto& owned) {
                            return owned.unit.kind == "package"
                                || owned.unit.kind == "class";
                        })) {
                    continue;
                }
                const auto artifact_path = store.artifact_path(
                    catalog.location, artifact_record, error);
                if (!artifact_path) {
                    return std::nullopt;
                }
                diagnostic::Engine archive_diagnostics;
                const auto metadata = artifact::load_object_metadata(
                    *artifact_path, archive_diagnostics);
                if (!metadata) {
                    const auto& failures = archive_diagnostics.diagnostics();
                    error = failures.empty()
                        ? "cannot read compiled library object metadata"
                        : failures.back().message;
                    for (const auto& diagnostic : failures) {
                        context.diagnostics.report(diagnostic);
                    }
                    return std::nullopt;
                }
                for (const auto& owned : artifact_record.units) {
                    const auto indexed = std::ranges::find_if(
                        metadata->units, [&](const auto& candidate) {
                            return workspace::unit_identity(candidate)
                                == workspace::unit_identity(owned.unit);
                        });
                    if (indexed == metadata->units.end()) {
                        error = "workspace catalog owns a definition absent from its "
                                "compiled object archive";
                        return std::nullopt;
                    }
                }
                const auto bundle_path = *artifact_path
                    / metadata->compiled_hir_artifact;
                auto payload = application_detail::read_binary_payload(
                    bundle_path, kCompiledHirDecodeBudgetBytes);
                if (!payload.bytes) {
                    error = payload.budget_exceeded
                        ? "compiled definition archive exceeds the decode byte budget"
                        : "cannot read compiled definition archive '"
                            + support::path_to_utf8(bundle_path) + "'";
                    return std::nullopt;
                }
                if (support::Sha256::hex(
                        support::Sha256::digest(*payload.bytes))
                    != metadata->compiled_hir_checksum) {
                    error = "compiled definition archive checksum does not match "
                            "its object metadata";
                    return std::nullopt;
                }
                auto compiled = deserialize_compiled_hir_bundle(
                    *payload.bytes, support::path_to_utf8(bundle_path),
                    archive_diagnostics);
                if (!compiled) {
                    const auto& failures = archive_diagnostics.diagnostics();
                    error = failures.empty()
                        ? "cannot decode compiled definition archive"
                        : failures.back().message;
                    for (const auto& diagnostic : failures) {
                        context.diagnostics.report(diagnostic);
                    }
                    return std::nullopt;
                }
                auto classes = semantic::sv::specialize_classes(*compiled);
                const auto source_library = metadata->library.empty()
                    ? std::string_view { catalog.location.name }
                    : std::string_view { metadata->library };
                auto definitions = definition_catalog(
                    *compiled, classes.specializations, source_library,
                    catalog.location.name, artifact_record.id,
                    [&] {
                        std::vector<library::UnitIndexEntry> owned;
                        owned.reserve(artifact_record.units.size());
                        for (const auto& unit : artifact_record.units) {
                            owned.push_back(unit.unit);
                        }
                        return owned;
                    }());
                result.insert(result.end(),
                    std::make_move_iterator(definitions.begin()),
                    std::make_move_iterator(definitions.end()));
            }
        }
        std::ranges::sort(result, [](const auto& left, const auto& right) {
            return std::tie(left.library, left.name, left.kind, left.identity)
                < std::tie(right.library, right.name, right.kind, right.identity);
        });
        return result;
    }

    std::optional<DefinitionRecord> find_definition(
        TclContext& context,
        const std::string_view library_name,
        const std::string_view identity,
        std::string& error)
    {
        auto definitions = library_definition_catalog(
            context, library_name, error);
        if (!definitions) {
            return std::nullopt;
        }
        const auto found = std::ranges::find_if(
            *definitions, [&](const DefinitionRecord& definition) {
                return definition.library == library_name
                    && definition.identity == identity;
            });
        if (found != definitions->end()) {
            return *found;
        }
        for (const auto& definition : *definitions) {
            const auto member = std::ranges::find_if(
                definition.members, [&](const DefinitionMember& candidate) {
                    return candidate.identity == identity;
                });
            if (member != definition.members.end()) {
                DefinitionRecord record;
                record.kind = member->kind;
                record.library = definition.library;
                record.name = member->name;
                record.identity = member->identity;
                record.type = member->type;
                record.parent_identity = definition.identity;
                record.width = member->width;
                return record;
            }
        }
        return std::nullopt;
    }

    Tcl_Obj* definition_member_dictionary(
        TclContext& context,
        Tcl_Interp* interpreter,
        const DefinitionRecord& owner,
        const DefinitionMember& member)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "name", string_object(member.name));
        dict_put(interpreter, result, "kind", string_object(member.kind));
        dict_put(interpreter, result, "type", string_object(member.type));
        dict_put(interpreter, result, "width", unsigned_object(member.width));
        dict_put(interpreter, result, "identity", string_object(member.identity));
        dict_put(interpreter, result, "reference", string_object(reference_for_catalog(context, owner.library, member.identity)));
        if (!member.visibility.empty()) {
            dict_put(interpreter, result, "visibility", string_object(member.visibility));
        }
        dict_put(interpreter, result, "static", Tcl_NewBooleanObj(member.static_storage));
        dict_put(interpreter, result, "constant", Tcl_NewBooleanObj(member.constant));
        return result;
    }

    Tcl_Obj* definition_dictionary(
        TclContext& context,
        Tcl_Interp* interpreter,
        const DefinitionRecord& definition)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "kind", string_object(definition.kind));
        dict_put(interpreter, result, "library", string_object(definition.library));
        dict_put(interpreter, result, "name", string_object(definition.name));
        dict_put(interpreter, result, "identity", string_object(definition.identity));
        dict_put(interpreter, result, "type", string_object(definition.type));
        dict_put(interpreter, result, "reference", string_object(reference_for_catalog(context, definition.library, definition.identity)));
        Tcl_Obj* members = Tcl_NewListObj(0, nullptr);
        for (const auto& member : definition.members) {
            list_append(interpreter, members,
                definition_member_dictionary(context, interpreter, definition, member));
        }
        dict_put(interpreter, result, "members", members);
        return result;
    }

    Tcl_Obj* catalog_info_dictionary(
        TclContext& context,
        Tcl_Interp* interpreter,
        const std::string_view token,
        const TclReference& reference,
        const DefinitionRecord& definition)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "reference", string_object(token));
        dict_put(interpreter, result, "identity", string_object(definition.identity));
        dict_put(interpreter, result, "kind", string_object(definition.kind));
        dict_put(interpreter, result, "name", string_object(definition.name));
        dict_put(interpreter, result, "path", string_object(definition.name));
        dict_put(interpreter, result, "parent",
            string_object(definition.parent_identity.empty()
                    ? std::string { }
                    : reference_for_catalog(
                          context, definition.library, definition.parent_identity)));
        dict_put(interpreter, result, "type", string_object(definition.type));
        dict_put(interpreter, result, "width", unsigned_object(definition.width));
        dict_put(interpreter, result, "dimensions", Tcl_NewListObj(0, nullptr));
        dict_put(interpreter, result, "library", string_object(reference.library));
        Tcl_Obj* members = Tcl_NewListObj(0, nullptr);
        for (const auto& member : definition.members) {
            list_append(interpreter, members,
                definition_member_dictionary(context, interpreter, definition, member));
        }
        dict_put(interpreter, result, "members", members);
        return result;
    }

    std::optional<std::string_view> short_class_name(
        const std::string_view identity)
    {
        const auto separator = identity.rfind("::");
        if (separator == std::string_view::npos) {
            return identity;
        }
        return identity.substr(separator + 2);
    }

    std::vector<std::string> object_children_identities(
        TclContext& context,
        const LoadedObjectView& view)
    {
        const auto* ir = current_design_ir(context);
        if (!ir) {
            return { };
        }
        std::vector<std::string> result;
        if (view.kind == LoadedObjectView::Kind::instance) {
            std::optional<semantic::design::InstanceOccurrenceId> instance_id;
            if (view.instance) {
                instance_id = view.instance->id;
                for (const auto& child : ir->instances()) {
                    if (child.parent == instance_id) {
                        result.push_back(instance_identity(ir->path(child.path)));
                    }
                }
            }
            for (const auto& object : ir->objects()) {
                if (object.parent_object) {
                    continue;
                }
                const auto* specialization = object_specialization(*ir, object);
                if (!specialization) {
                    continue;
                }
                const bool belongs = instance_id
                    ? specialization->instance == *instance_id
                    : view.synthetic_root
                        && specialization->instance.valid()
                        && specialization->instance.value() < ir->instances().size()
                        && ir->path(ir->instances()[specialization->instance.value()].path)
                            == view.root_path;
                if (belongs) {
                    result.push_back(object_identity(object));
                }
            }
        } else if (view.kind == LoadedObjectView::Kind::design_object) {
            for (const auto& child : ir->objects()) {
                if (child.parent_object == view.object->id) {
                    result.push_back(object_identity(child));
                }
            }
            if (const auto handle = class_handle_value(context, *view.object)) {
                if (*handle != 0 && context.simulation->class_heap().contains(*handle)) {
                    result.push_back(
                        "class-object:" + std::to_string(*handle));
                }
            }
        } else if (view.kind == LoadedObjectView::Kind::class_object) {
            const auto& object = context.simulation->class_heap().object(view.class_handle);
            for (std::size_t index = 0; index < object.properties.size(); ++index) {
                result.push_back(
                    "class-property:" + std::to_string(view.class_handle)
                    + ":" + std::to_string(index));
            }
        } else {
            const auto& value = context.simulation->class_heap()
                                    .object(view.class_handle)
                                    .properties.at(view.property_index);
            if (value.kind == SystemVerilogClassPropertyKind::ClassHandle
                && value.handle != 0
                && context.simulation->class_heap().contains(value.handle)) {
                result.push_back("class-object:" + std::to_string(value.handle));
            } else if (value.kind == SystemVerilogClassPropertyKind::Container) {
                if (value.handle_container) {
                    for (const auto handle : value.handle_container->sequential_values()) {
                        if (handle != 0 && context.simulation->class_heap().contains(handle)) {
                            result.push_back("class-object:" + std::to_string(handle));
                        }
                    }
                    for (const auto& [key, handle] : value.handle_container->keyed_values()) {
                        (void)key;
                        if (handle != 0 && context.simulation->class_heap().contains(handle)) {
                            result.push_back("class-object:" + std::to_string(handle));
                        }
                    }
                }
                for (const auto handle : value.handles) {
                    if (handle != 0 && context.simulation->class_heap().contains(handle)) {
                        result.push_back("class-object:" + std::to_string(handle));
                    }
                }
            }
        }
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    std::optional<runtime::PackedLogic4> read_design_signal(
        TclContext& context,
        const Object& object)
    {
        const auto id = static_cast<std::uint32_t>(object.runtime_index);
        if (context.simulation) {
            return context.simulation->read_signal(runtime::simir::SignalId { id });
        }
        if (context.built) {
            const auto state = context.built->design.state();
            if (id < state.signals.size()) {
                return state.signals[id].initial_value;
            }
        }
        return std::nullopt;
    }

    std::optional<runtime::SystemVerilogScalarKind> signal_scalar_kind(
        const TclContext& context,
        const Object& object)
    {
        const auto* adapter = current_adapter(context);
        if (!adapter || object.runtime_index >= adapter->signals().size()) {
            return std::nullopt;
        }
        const auto kind = adapter->signals()[object.runtime_index].systemverilog_scalar;
        if (kind == runtime::SystemVerilogScalarKind::None) {
            return std::nullopt;
        }
        return kind;
    }

    std::optional<std::string> read_design_string(
        TclContext& context,
        const Object& object)
    {
        if (context.simulation) {
            return context.simulation->read_string_object(
                static_cast<runtime::simir::StringObjectId>(object.runtime_index));
        }
        if (context.built) {
            const auto state = context.built->design.state();
            if (object.runtime_index < state.string_objects.size()) {
                return state.string_objects[object.runtime_index].initial_value;
            }
        }
        return std::nullopt;
    }

    std::optional<ContainerValue> read_design_container(
        TclContext& context,
        const Object& object)
    {
        if (context.simulation) {
            return context.simulation->read_container_object(
                static_cast<runtime::simir::ContainerObjectId>(object.runtime_index));
        }
        if (context.built) {
            const auto state = context.built->design.state();
            if (object.runtime_index < state.container_objects.size()) {
                return state.container_objects[object.runtime_index].initial_value;
            }
        }
        return std::nullopt;
    }

    Tcl_Obj* class_object_value(
        TclContext& context,
        Tcl_Interp* interpreter,
        const SystemVerilogClassHandle handle)
    {
        const auto& object = context.simulation->class_heap().object(handle);
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "kind", string_object("class"));
        dict_put(interpreter, result, "shape", string_object("object"));
        dict_put(interpreter, result, "declared_type", string_object(object.declared_type));
        dict_put(interpreter, result, "dynamic_type", string_object(object.dynamic_type));
        dict_put(interpreter, result, "reference", string_object(reference_for_loaded(context, "class-object:" + std::to_string(handle))));
        Tcl_Obj* properties = Tcl_NewDictObj();
        for (std::size_t index = 0; index < object.property_names.size(); ++index) {
            dict_put(interpreter, properties, object.property_names[index],
                class_property_value(context, interpreter, object.properties.at(index)));
        }
        dict_put(interpreter, result, "value", properties);
        return result;
    }

    Tcl_Obj* design_object_value(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Object& object)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "type",
            string_object(!object.external_type.empty()
                    ? std::string_view { object.external_type }
                    : type_name(context, object.type)));
        dict_put(interpreter, result, "width", unsigned_object(object.width));
        dict_put(interpreter, result, "dimensions",
            object_dimensions(interpreter, context, object));
        const auto class_type = !object.external_type.empty()
            ? std::string_view { object.external_type }
            : type_name(context, object.type);
        if (is_class_type(context, class_type)
            && object.kind == ObjectKind::signal) {
            if (!context.simulation) {
                throw std::runtime_error {
                    "class handle values require an initialized simulation"
                };
            }
            const auto packed = read_design_signal(context, object);
            if (!packed || packed->width() < 64) {
                throw std::runtime_error {
                    "class handle signal does not have a readable 64-bit value"
                };
            }
            const auto word = packed->low_word();
            if (word.bval != 0) {
                throw std::runtime_error {
                    "class handle signal contains an unknown value"
                };
            }
            if (word.aval == 0) {
                dict_put(interpreter, result, "kind", string_object("class"));
                dict_put(interpreter, result, "shape", string_object("reference"));
                dict_put(interpreter, result, "null", Tcl_NewBooleanObj(1));
                dict_put(interpreter, result, "value", string_object(""));
            } else if (!context.simulation->class_heap().contains(word.aval)) {
                throw std::runtime_error { "class handle signal contains a stale handle" };
            } else {
                dict_put(interpreter, result, "kind", string_object("class"));
                dict_put(interpreter, result, "shape", string_object("reference"));
                dict_put(interpreter, result, "null", Tcl_NewBooleanObj(0));
                dict_put(interpreter, result, "value", string_object(reference_for_loaded(context, "class-object:" + std::to_string(word.aval))));
                dict_put(interpreter, result, "dynamic_type",
                    string_object(context.simulation->class_heap()
                            .object(word.aval)
                            .dynamic_type));
            }
            return result;
        }
        if (object.kind == ObjectKind::signal
            || object.kind == ObjectKind::systemc_signal) {
            const auto packed = read_design_signal(context, object);
            if (!packed) {
                throw std::runtime_error { "signal value is unavailable" };
            }
            if (const auto scalar_kind = signal_scalar_kind(context, object)) {
                runtime::SystemVerilogScalarValue scalar;
                if (context.simulation) {
                    scalar = context.simulation->read_scalar_signal(
                        runtime::simir::SignalId {
                            static_cast<std::uint32_t>(object.runtime_index) });
                } else {
                    const auto decoded = runtime::decode_systemverilog_scalar_payload(
                        *packed, *scalar_kind);
                    if (!decoded) {
                        throw std::runtime_error {
                            "scalar signal value cannot be decoded"
                        };
                    }
                    scalar = decoded.value;
                }
                const auto formatted = runtime::format_systemverilog_scalar(scalar);
                if (!formatted) {
                    throw std::runtime_error { "scalar signal value cannot be formatted" };
                }
                dict_put(interpreter, result, "kind", string_object("scalar"));
                dict_put(interpreter, result, "shape", string_object("scalar"));
                dict_put(interpreter, result, "value", string_object(formatted.text));
            } else {
                dict_put(interpreter, result, "kind", string_object("logic"));
                dict_put(interpreter, result, "shape",
                    string_object(packed->width() == 1 ? "scalar" : "packed"));
                dict_put(interpreter, result, "value",
                    string_object(packed->to_msb_string()));
            }
            return result;
        }
        if (object.kind == ObjectKind::string) {
            const auto value = read_design_string(context, object);
            if (!value) {
                throw std::runtime_error { "string value is unavailable" };
            }
            dict_put(interpreter, result, "kind", string_object("string"));
            dict_put(interpreter, result, "shape", string_object("scalar"));
            dict_put(interpreter, result, "value", string_object(*value));
            return result;
        }
        if (object.kind == ObjectKind::container) {
            const auto value = read_design_container(context, object);
            if (!value) {
                throw std::runtime_error { "container value is unavailable" };
            }
            dict_put(interpreter, result, "kind", string_object("composite"));
            dict_put(interpreter, result, "shape", string_object("container"));
            dict_put(interpreter, result, "value",
                container_value_object(context, interpreter, *value));
            return result;
        }
        throw std::runtime_error {
            "value reads are unsupported for object kind '"
            + std::string { object_kind_name(object.kind) } + "'"
        };
    }

    Tcl_Obj* object_value(
        TclContext& context,
        Tcl_Interp* interpreter,
        const LoadedObjectView& view)
    {
        if (view.kind == LoadedObjectView::Kind::design_object) {
            return design_object_value(context, interpreter, *view.object);
        }
        if (!context.simulation) {
            throw std::runtime_error {
                "runtime object values require an initialized simulation"
            };
        }
        if (view.kind == LoadedObjectView::Kind::class_object) {
            return class_object_value(context, interpreter, view.class_handle);
        }
        if (view.kind == LoadedObjectView::Kind::class_property) {
            const auto& object = context.simulation->class_heap().object(view.class_handle);
            Tcl_Obj* result = class_property_value(
                context, interpreter, object.properties.at(view.property_index));
            dict_put(interpreter, result, "name",
                string_object(short_member_name(
                    object.property_names.at(view.property_index))));
            return result;
        }
        throw std::runtime_error { "module and instance references have no value" };
    }

    bool is_decimal_integer(const std::string_view text)
    {
        std::size_t first_digit { };
        if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
            first_digit = 1;
        }
        return first_digit < text.size()
            && std::ranges::all_of(text.substr(first_digit), [](const char value) { return value >= '0' && value <= '9'; });
    }

    std::optional<runtime::PackedLogic4> parse_container_integer_element(
        const std::string_view text,
        const runtime::simir::ContainerType& type,
        std::string& error)
    {
        const auto width = static_cast<std::size_t>(type.element_width);
        if (width == 0 || width > 64 || !is_decimal_integer(text)) {
            error = "decimal integer is unsupported for this container element width";
            return std::nullopt;
        }

        std::uint64_t bits { };
        if (type.signed_elements) {
            auto numeric_text = text;
            if (numeric_text.starts_with('+')) {
                numeric_text.remove_prefix(1);
            }
            std::int64_t value { };
            const auto converted = std::from_chars(
                numeric_text.data(), numeric_text.data() + numeric_text.size(),
                value, 10);
            if (converted.ec != std::errc { }
                || converted.ptr != numeric_text.data() + numeric_text.size()) {
                error = "decimal integer is invalid or out of range for its declared container element type";
                return std::nullopt;
            }
            if (width < 64) {
                const auto sign_bit = std::int64_t { 1 } << (width - 1U);
                if (value < -sign_bit || value > sign_bit - 1) {
                    error = "decimal integer is out of range for its declared container element width";
                    return std::nullopt;
                }
            }
            bits = static_cast<std::uint64_t>(value);
            if (width < 64) {
                bits &= (std::uint64_t { 1 } << width) - 1U;
            }
        } else {
            auto numeric_text = text;
            if (numeric_text.starts_with('+')) {
                numeric_text.remove_prefix(1);
            }
            std::uint64_t value { };
            const auto converted = std::from_chars(
                numeric_text.data(), numeric_text.data() + numeric_text.size(),
                value, 10);
            if (converted.ec != std::errc { }
                || converted.ptr != numeric_text.data() + numeric_text.size()) {
                error = "decimal integer is invalid or out of range for its declared container element type";
                return std::nullopt;
            }
            if (width < 64 && value >= (std::uint64_t { 1 } << width)) {
                error = "decimal integer is out of range for its declared container element width";
                return std::nullopt;
            }
            bits = value;
        }
        return runtime::PackedLogic4::from_aval_bval(width, bits, 0);
    }

    std::optional<ContainerValue> parse_container_value(
        Tcl_Interp* interpreter,
        const ContainerValue& original,
        Tcl_Obj* input,
        std::string& error)
    {
        if (original.type.associative) {
            error = "associative container mutation is unsupported";
            return std::nullopt;
        }
        if (original.type.element_kind == ContainerElementKind::Container
            || original.type.element_kind == ContainerElementKind::Aggregate) {
            error = "nested and aggregate container mutation is unsupported";
            return std::nullopt;
        }
        Tcl_Obj** values { };
        Tcl_Size count { };
        if (Tcl_ListObjGetElements(interpreter, input, &count, &values) != TCL_OK) {
            error = "container value must be a Tcl list";
            return std::nullopt;
        }
        if (original.type.fixed
            && static_cast<std::size_t>(count)
                != runtime::simir::container_value_size(original)) {
            error = "fixed container value has the wrong number of elements";
            return std::nullopt;
        }
        ContainerValue result = original;
        result.elements.clear();
        result.string_elements.clear();
        result.nested_elements.clear();
        result.keys.clear();
        result.string_keys.clear();
        for (Tcl_Size index = 0; index < count; ++index) {
            const std::string text { tcl_string(values[index]) };
            if (original.type.element_kind == ContainerElementKind::String) {
                result.string_elements.push_back(text);
                continue;
            }
            if (original.type.element_kind == ContainerElementKind::Packed) {
                std::string packed_error;
                auto packed = fsim::app::parse_value(
                    text, original.type.element_width, packed_error);
                if (!packed && is_decimal_integer(text)) {
                    packed = parse_container_integer_element(
                        text, original.type, error);
                } else if (!packed) {
                    error = std::move(packed_error);
                }
                if (!packed) {
                    return std::nullopt;
                }
                result.elements.push_back(std::move(*packed));
                continue;
            }
            const auto scalar = runtime::scan_systemverilog_scalar(
                text, original.type.scalar_kind);
            if (!scalar) {
                error = "container scalar value is invalid for its declared type";
                return std::nullopt;
            }
            auto encoded = runtime::encode_systemverilog_scalar_payload(scalar.value);
            if (!encoded) {
                error = "container scalar value cannot be encoded";
                return std::nullopt;
            }
            result.elements.push_back(std::move(encoded.value));
        }
        if (!original.type.fixed) {
            try {
                runtime::simir::resize_container_value(result,
                    static_cast<std::size_t>(count));
            } catch (const std::exception& exception) {
                error = exception.what();
                return std::nullopt;
            }
        }
        return result;
    }

    int object_roots(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count != 2) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "roots");
            return TCL_ERROR;
        }
        const auto* ir = current_design_ir(context);
        if (!ir) {
            return object_error(
                context, interpreter, not_found_code,
                "no loaded design is available for object roots");
        }
        Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
        for (const auto root : ir->roots()) {
            list_append(interpreter, result,
                string_object(reference_for_loaded(
                    context, instance_identity(ir->path(root)))));
        }
        return set_result(interpreter, result);
    }

    int object_resolve(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count != 3) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "resolve PATH");
            return TCL_ERROR;
        }
        const auto* ir = current_design_ir(context);
        if (!ir) {
            return object_error(
                context, interpreter, not_found_code,
                "no loaded design is available for object resolution");
        }
        const std::string path { tcl_string(arguments[2]) };
        if (std::ranges::any_of(ir->roots(), [&](const auto root) {
                return ir->path(root) == path;
            })
            || find_instance(*ir, path)) {
            return set_result(interpreter, string_object(reference_for_loaded(context, instance_identity(path))));
        }
        if (const auto* object = find_object_path(*ir, path)) {
            return set_result(interpreter, string_object(reference_for_loaded(context, object_identity(*object))));
        }
        return object_error(
            context, interpreter, not_found_code,
            "no loaded design object has path '" + path + "'");
    }

    int object_children(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count != 3) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "children REFERENCE");
            return TCL_ERROR;
        }
        const std::string token { tcl_string(arguments[2]) };
        const auto any = resolve_reference(context, interpreter, token);
        if (!any) {
            return TCL_ERROR;
        }
        if (any->reference.kind == TclReferenceKind::catalog_definition) {
            std::string error;
            const auto definition = find_definition(
                context, any->reference.library, any->reference.identity, error);
            if (!definition) {
                if (!error.empty()) {
                    return object_error(context, interpreter, value_code, error);
                }
                return object_error(
                    context, interpreter, stale_reference_code,
                    "compiled definition reference no longer resolves");
            }
            Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
            for (const auto& member : definition->members) {
                list_append(interpreter, result, string_object(reference_for_catalog(context, definition->library, member.identity)));
            }
            return set_result(interpreter, result);
        }
        auto view = find_loaded_object(context, any->reference.identity);
        if (!view) {
            return object_error(
                context, interpreter, stale_reference_code,
                "fsim object reference no longer names a live design object");
        }
        Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
        for (const auto& identity : object_children_identities(context, *view)) {
            list_append(interpreter, result,
                string_object(reference_for_loaded(context, identity)));
        }
        return set_result(interpreter, result);
    }

    int object_info(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count != 3) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "info REFERENCE");
            return TCL_ERROR;
        }
        const std::string token { tcl_string(arguments[2]) };
        const auto any = resolve_reference(context, interpreter, token);
        if (!any) {
            return TCL_ERROR;
        }
        if (any->reference.kind == TclReferenceKind::catalog_definition) {
            std::string error;
            const auto definition = find_definition(
                context, any->reference.library, any->reference.identity, error);
            if (!definition) {
                if (!error.empty()) {
                    return object_error(context, interpreter, value_code, error);
                }
                return object_error(
                    context, interpreter, stale_reference_code,
                    "compiled definition reference no longer resolves");
            }
            return set_result(interpreter, catalog_info_dictionary(context, interpreter, token, any->reference, *definition));
        }
        const auto view = find_loaded_object(context, any->reference.identity);
        if (!view) {
            return object_error(
                context, interpreter, stale_reference_code,
                "fsim object reference no longer names a live design object");
        }
        try {
            return set_result(interpreter,
                object_info_dictionary(context, interpreter, token, *view));
        } catch (const std::exception& exception) {
            return object_error(
                context, interpreter, unsupported_code, exception.what());
        }
    }

    int object_value_command(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count != 3) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "value REFERENCE");
            return TCL_ERROR;
        }
        const std::string token { tcl_string(arguments[2]) };
        const auto any = resolve_reference(context, interpreter, token);
        if (!any) {
            return TCL_ERROR;
        }
        if (any->reference.kind == TclReferenceKind::catalog_definition) {
            return object_error(
                context, interpreter, unsupported_code,
                "compiled definitions have metadata but no runtime value");
        }
        const auto view = find_loaded_object(context, any->reference.identity);
        if (!view) {
            return object_error(
                context, interpreter, stale_reference_code,
                "fsim object reference no longer names a live design object");
        }
        try {
            return set_result(interpreter, object_value(context, interpreter, *view));
        } catch (const std::exception& exception) {
            return object_error(
                context, interpreter, value_error_code(exception.what()),
                exception.what());
        }
    }

    int set_design_object(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Object& object,
        Tcl_Obj* input)
    {
        const std::string text { tcl_string(input) };
        const auto declared_type = !object.external_type.empty()
            ? std::string_view { object.external_type }
            : type_name(context, object.type);
        if (is_class_type(context, declared_type)) {
            return object_error(
                context, interpreter, unsupported_code,
                "class handle signals cannot be assigned through an opaque object value");
        }
        if (object.kind == ObjectKind::signal) {
            const auto scalar_kind = signal_scalar_kind(context, object);
            if (scalar_kind) {
                const auto scalar = runtime::scan_systemverilog_scalar(text, *scalar_kind);
                if (!scalar) {
                    return object_error(
                        context, interpreter, value_code,
                        "value is invalid for the signal's declared scalar type");
                }
                context.simulation->deposit_scalar_signal(
                    runtime::simir::SignalId {
                        static_cast<std::uint32_t>(object.runtime_index) },
                    scalar.value);
            } else {
                std::string parse_error;
                auto value = fsim::app::parse_value(text, object.width, parse_error);
                if (!value) {
                    return object_error(
                        context, interpreter, value_code, parse_error);
                }
                context.simulation->deposit_signal(
                    runtime::simir::SignalId {
                        static_cast<std::uint32_t>(object.runtime_index) },
                    std::move(*value));
            }
            return TCL_OK;
        }
        if (object.kind == ObjectKind::string) {
            context.simulation->deposit_string_object(
                static_cast<runtime::simir::StringObjectId>(object.runtime_index), text);
            return TCL_OK;
        }
        if (object.kind == ObjectKind::container) {
            const auto current = context.simulation->read_container_object(
                static_cast<runtime::simir::ContainerObjectId>(object.runtime_index));
            std::string parse_error;
            auto value = parse_container_value(
                interpreter, current, input, parse_error);
            if (!value) {
                return object_error(
                    context, interpreter, value_code, parse_error);
            }
            context.simulation->deposit_container_object(
                static_cast<runtime::simir::ContainerObjectId>(object.runtime_index),
                std::move(*value));
            return TCL_OK;
        }
        return object_error(
            context, interpreter, unsupported_code,
            "mutation is unsupported for object kind '"
                + std::string { object_kind_name(object.kind) } + "'");
    }

    int set_class_property(
        TclContext& context,
        Tcl_Interp* interpreter,
        const LoadedObjectView& view,
        Tcl_Obj* input)
    {
        const auto& object = context.simulation->class_heap().object(view.class_handle);
        const auto& name = object.property_names.at(view.property_index);
        const auto& value = object.properties.at(view.property_index);
        const auto* declaration = find_class_property(
            context, object.dynamic_type, name);
        if (!declaration || declaration->static_storage) {
            return object_error(
                context, interpreter, unsupported_code,
                "static or unknown class property mutation is unsupported");
        }
        if (declaration->constant || declaration->parameter) {
            return object_error(
                context, interpreter, mutation_code,
                "constant class properties cannot be mutated");
        }
        if (value.kind != SystemVerilogClassPropertyKind::Bit2
            && value.kind != SystemVerilogClassPropertyKind::Logic4
            && value.kind != SystemVerilogClassPropertyKind::Logic9
            && value.kind != SystemVerilogClassPropertyKind::Integer) {
            return object_error(
                context, interpreter, unsupported_code,
                "only packed scalar class properties support mutation");
        }
        std::string parse_error;
        auto packed = fsim::app::parse_value(
            tcl_string(input), value.packed.width(), parse_error);
        if (!packed) {
            return object_error(
                context, interpreter, value_code, parse_error);
        }
        try {
            context.simulation->deposit_class_property(
                view.class_handle, name, std::move(*packed));
        } catch (const std::exception& exception) {
            return object_error(
                context, interpreter, value_code, exception.what());
        }
        return TCL_OK;
    }

    int object_set(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count != 4) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "set REFERENCE VALUE");
            return TCL_ERROR;
        }
        if (context.callback_depth != 0) {
            return object_error(
                context, interpreter, mutation_code,
                "object mutation is not allowed from a Tcl callback");
        }
        const std::string token { tcl_string(arguments[2]) };
        const auto any = resolve_reference(context, interpreter, token);
        if (!any) {
            return TCL_ERROR;
        }
        if (any->reference.kind == TclReferenceKind::catalog_definition) {
            return object_error(
                context, interpreter, mutation_code,
                "compiled definition references are read-only");
        }
        if (!context.simulation && !ensure_simulation(context, interpreter)) {
            return TCL_ERROR;
        }
        auto view = find_loaded_object(context, any->reference.identity);
        if (!view) {
            return object_error(
                context, interpreter, stale_reference_code,
                "fsim object reference no longer names a live design object");
        }
        try {
            int result = TCL_OK;
            if (view->kind == LoadedObjectView::Kind::design_object) {
                result = set_design_object(
                    context, interpreter, *view->object, arguments[3]);
            } else if (view->kind == LoadedObjectView::Kind::class_property) {
                result = set_class_property(context, interpreter, *view, arguments[3]);
            } else {
                return object_error(
                    context, interpreter, unsupported_code,
                    "instance and class object references are not directly mutable");
            }
            if (result != TCL_OK) {
                return result;
            }
            return set_result(interpreter,
                object_value(context, interpreter, *view));
        } catch (const std::exception& exception) {
            return object_error(context, interpreter, value_code, exception.what());
        }
    }

    int object_definitions(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count < 2 || argument_count > 3) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "definitions ?LIBRARY?");
            return TCL_ERROR;
        }
        const std::string library = argument_count == 3
            ? std::string { tcl_string(arguments[2]) }
            : std::string { };
        std::string error;
        const auto definitions = library_definition_catalog(
            context,
            argument_count == 3
                ? std::optional<std::string_view> { library }
                : std::nullopt,
            error);
        if (!definitions) {
            return object_error(context, interpreter, value_code, error);
        }
        Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
        for (const auto& definition : *definitions) {
            if (!library.empty() && definition.library != library) {
                continue;
            }
            list_append(interpreter, result,
                definition_dictionary(context, interpreter, definition));
        }
        return set_result(interpreter, result);
    }

    int object_definition(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count != 4) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "definition LIBRARY NAME");
            return TCL_ERROR;
        }
        const std::string library { tcl_string(arguments[2]) };
        const std::string name { tcl_string(arguments[3]) };
        std::string error;
        const auto catalog = library_definition_catalog(
            context, std::optional<std::string_view> { library }, error);
        if (!catalog) {
            return object_error(context, interpreter, value_code, error);
        }
        std::vector<const DefinitionRecord*> candidates;
        for (const auto& definition : *catalog) {
            if (definition.library != library) {
                continue;
            }
            const auto short_name = short_class_name(definition.name);
            if (definition.name == name
                || definition.identity == name
                || (short_name && *short_name == name)) {
                candidates.push_back(&definition);
            }
        }
        if (candidates.empty()) {
            return object_error(
                context, interpreter, not_found_code,
                "compiled definition '" + name + "' was not found in library '"
                    + library + "'");
        }
        if (candidates.size() != 1) {
            return object_error(
                context, interpreter, not_found_code,
                "compiled definition name '" + name
                    + "' is ambiguous; use its qualified identity");
        }
        return set_result(interpreter, string_object(reference_for_catalog(context, library, candidates.front()->identity)));
    }

    int object_command(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[]) noexcept
    {
        try {
            if (argument_count < 2) {
                Tcl_WrongNumArgs(
                    interpreter, 1, arguments,
                    "roots|resolve|children|info|value|set|definitions|definition ?ARG ...?");
                return TCL_ERROR;
            }
            const std::string_view subcommand { tcl_string(arguments[1]) };
            if (subcommand == "roots") {
                return object_roots(context, interpreter, argument_count, arguments);
            }
            if (subcommand == "resolve") {
                return object_resolve(context, interpreter, argument_count, arguments);
            }
            if (subcommand == "children") {
                return object_children(context, interpreter, argument_count, arguments);
            }
            if (subcommand == "info") {
                return object_info(context, interpreter, argument_count, arguments);
            }
            if (subcommand == "value") {
                return object_value_command(context, interpreter, argument_count, arguments);
            }
            if (subcommand == "set") {
                return object_set(context, interpreter, argument_count, arguments);
            }
            if (subcommand == "definitions") {
                return object_definitions(context, interpreter, argument_count, arguments);
            }
            if (subcommand == "definition") {
                return object_definition(context, interpreter, argument_count, arguments);
            }
            return object_error(
                context, interpreter, not_found_code,
                "unknown fsim object subcommand '" + std::string { subcommand } + "'");
        } catch (const std::exception& exception) {
            return object_error(
                context, interpreter, value_code, exception.what());
        } catch (...) {
            return object_error(
                context, interpreter, value_code,
                "unknown fsim object command failure");
        }
    }

    constexpr std::array object_arguments {
        TclCommandArgument { "subcommand", TclCompletionDomain::literal },
        TclCommandArgument { "path_or_reference", TclCompletionDomain::design_object, true },
        TclCommandArgument { "value_or_library", TclCompletionDomain::literal, true },
    };

    constexpr std::array<TclCommandArgument, 0> no_object_arguments { };
    constexpr std::array resolve_arguments {
        TclCommandArgument { "path", TclCompletionDomain::design_object },
    };
    constexpr std::array object_reference_arguments {
        TclCommandArgument { "reference", TclCompletionDomain::design_object },
    };
    constexpr std::array set_arguments {
        TclCommandArgument { "reference", TclCompletionDomain::design_object },
        TclCommandArgument { "value", TclCompletionDomain::literal },
    };
    constexpr std::array definitions_arguments {
        TclCommandArgument { "library", TclCompletionDomain::library, true },
    };
    constexpr std::array definition_arguments {
        TclCommandArgument { "library", TclCompletionDomain::library },
        TclCommandArgument { "name", TclCompletionDomain::compiled_definition },
    };

    constexpr std::array object_subcommands {
        TclSubcommandSpec {
            "roots", "roots", "List loaded design roots.", no_object_arguments,
            TclCommandCapability::loaded_design },
        TclSubcommandSpec {
            "resolve", "resolve PATH", "Resolve a loaded hierarchy path.", resolve_arguments,
            TclCommandCapability::loaded_design },
        TclSubcommandSpec {
            "children", "children REFERENCE", "List an object's children.", object_reference_arguments,
            TclCommandCapability::loaded_design },
        TclSubcommandSpec {
            "info", "info REFERENCE", "Return structured object metadata.", object_reference_arguments,
            TclCommandCapability::loaded_design },
        TclSubcommandSpec {
            "value", "value REFERENCE", "Read a supported runtime or initial value.", object_reference_arguments,
            TclCommandCapability::loaded_design },
        TclSubcommandSpec {
            "set", "set REFERENCE VALUE", "Validate and apply a supported value mutation.", set_arguments,
            TclCommandCapability::loaded_design },
        TclSubcommandSpec {
            "definitions", "definitions ?LIBRARY?", "List compiled packages and classes.", definitions_arguments },
        TclSubcommandSpec {
            "definition", "definition LIBRARY NAME", "Resolve a compiled package or class.", definition_arguments },
    };

    constexpr std::array<TclCommandSpec, 1> object_specs { {
        {
            "fsim::object",
            "roots | resolve PATH | children REFERENCE | info REFERENCE | value REFERENCE | set REFERENCE VALUE | definitions ?LIBRARY? | definition LIBRARY NAME",
            "Inspect loaded design objects and compiled package or class definitions.",
            TclCommandCapability::workspace,
            true,
            object_arguments,
            &object_command,
            TclResultShape::variant,
            "FSIM-TCL-OBJECT",
            object_subcommands,
        },
    } };

} // namespace

std::span<const TclCommandSpec> object_command_specs()
{
    return object_specs;
}

#endif // defined(FSIM_HAS_TCL)

} // namespace fsim::app::tcl_detail
