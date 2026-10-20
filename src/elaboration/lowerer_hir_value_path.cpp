// SPDX-License-Identifier: Apache-2.0
//
// Structured value paths over SystemVerilog unpacked values (IEEE 1800-2017
// 7.2, 7.4): a root container object, then index and member steps through
// arrays and unpacked aggregates. Reads and writes walk the path with the
// element and member container operations, so any nesting of arrays and
// structures uses one mechanism.
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
namespace {

    [[nodiscard]] bool aggregate_value_type(const ContainerType& type)
    {
        return type.element_kind == ContainerElementKind::Aggregate
            && type.aggregate_value;
    }

    // An aggregate member profile with packed or scalar elements is that
    // packed or scalar member value (the aggregate member read and write
    // operations select its single element).
    [[nodiscard]] bool packed_leaf_type(const ContainerType& type)
    {
        return (type.element_kind == ContainerElementKind::Packed
                   || type.element_kind == ContainerElementKind::Scalar)
            && type.element_width != 0U;
    }

    // An aggregate member holding one value: its profile has no
    // dimensions, or the single-element fixed dimension members use.
    [[nodiscard]] bool singleton_member_type(const ContainerType& type)
    {
        return !type.queue && !type.associative
            && (type.dimensions.empty()
                || (type.dimensions.size() == 1U
                    && type.dimensions.front().first
                        == type.dimensions.front().second));
    }

    [[nodiscard]] std::vector<std::string_view> dotted_names(
        std::string_view text)
    {
        std::vector<std::string_view> names;
        while (!text.empty()) {
            const auto separator = text.find('.');
            names.push_back(text.substr(0U, separator));
            if (separator == std::string_view::npos) {
                break;
            }
            text.remove_prefix(separator + 1U);
        }
        return names;
    }

} // namespace

std::optional<Lowerer::HirSvValuePath> Lowerer::hir_class_subarray_element(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    auto current = expression_id;
    for (std::size_t depth { }; depth < 16U; ++depth) {
        const auto expression = specialized_hir_unit_->find_expression(
            current);
        if (!expression || expression->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& source = *expression->systemverilog;
        if (source.kind == semantic::sv::ExpressionKind::index
            && source.text == "index" && source.operands.size() == 2U) {
            current = source.operands.front();
            continue;
        }
        if (depth == 0U || source.kind != semantic::sv::ExpressionKind::call
            || !source.text.starts_with("@sv-container-index:")) {
            return std::nullopt;
        }
        auto path = hir_sv_value_path(expression_id);
        if (!path || !path->leaf || path->string_leaf) {
            return std::nullopt;
        }
        return path;
    }
    return std::nullopt;
}

std::optional<Lowerer::HirSvValuePath> Lowerer::hir_sv_value_path(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    // Collect the syntactic steps from the expression, innermost last.
    struct PendingStep {
        std::optional<std::string_view> member;
        std::vector<semantic::ExpressionId> indices;
    };
    std::vector<PendingStep> pending;
    std::optional<HirContainerObjectBinding> root;
    std::optional<HirSvValuePath::ClassRoot> class_root;
    std::shared_ptr<const ContainerType> class_root_type;
    auto current = expression_id;
    for (std::size_t depth { }; depth < 64U; ++depth) {
        const auto expression = specialized_hir_unit_->find_expression(
            current);
        if (!expression || expression->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& source = *expression->systemverilog;
        constexpr auto select_prefix = std::string_view { "@sv-select:" };
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with(select_prefix)
            && source.operands.size() == 1U) {
            pending.push_back(PendingStep {
                std::string_view { source.text }.substr(
                    select_prefix.size()),
                { } });
            current = source.operands.front();
            continue;
        }
        if (source.kind == semantic::sv::ExpressionKind::index
            && (source.text == "index" || source.text.starts_with("index."))
            && source.operands.size() >= 2U) {
            // `a[i].x.y` keeps its member suffix in the index text.
            if (source.text.size() > 6U) {
                auto members = dotted_names(
                    std::string_view { source.text }.substr(6U));
                for (auto member = members.rbegin();
                    member != members.rend(); ++member) {
                    pending.push_back(PendingStep { *member, { } });
                }
            }
            pending.push_back(PendingStep {
                std::nullopt,
                { source.operands.begin() + 1, source.operands.end() } });
            current = source.operands.front();
            continue;
        }
        // A value-container property of an object (`obj.v`, `obj.v[i]`)
        // or of a class (`C::v`) roots the path (IEEE 1800-2017 8.4, 8.9).
        constexpr auto property_prefix
            = std::string_view { "@sv-container-property:" };
        constexpr auto element_prefix
            = std::string_view { "@sv-container-index:" };
        if (source.kind == semantic::sv::ExpressionKind::call
            && ((source.text.starts_with(property_prefix)
                    && source.operands.size() == 1U)
                || (source.text.starts_with(element_prefix)
                    && source.operands.size() >= 2U))) {
            const bool element = source.text.starts_with(element_prefix);
            auto identity = source.text.substr(element
                    ? element_prefix.size()
                    : property_prefix.size());
            auto type = hir_class_value_container_type(identity);
            if (!type) {
                return std::nullopt;
            }
            if (element) {
                pending.push_back(PendingStep { std::nullopt,
                    { source.operands.begin() + 1, source.operands.end() } });
            }
            const auto* property = hir_resolver()
                .resolve_systemverilog_class_property(identity, false)
                .unique();
            if (property == nullptr) {
                return std::nullopt;
            }
            class_root = HirSvValuePath::ClassRoot {
                source.operands.front(), std::move(identity) };
            class_root_type = std::make_shared<const ContainerType>(
                std::move(*type));
            root = HirContainerObjectBinding { property->declaration,
                class_root->identity, { }, std::nullopt,
                class_root_type.get(), false };
            break;
        }
        if (source.kind == semantic::sv::ExpressionKind::call
            && source.text.starts_with("@sv-static-container-property:")
            && source.operands.empty()) {
            root = hir_class_static_container_binding(current);
            break;
        }
        if (source.kind == semantic::sv::ExpressionKind::name) {
            root = hir_container_object_binding(current);
            // A container property of `this` named by its simple name in a
            // method (IEEE 1800-2017 8.11).
            if (!root && hir_class_receiver_register_
                && source.text.find('.') == std::string::npos) {
                if (const auto declaration
                    = hir_referenced_declaration(current)) {
                    for (const auto& candidate : specialized_hir_unit_
                             ->design().systemverilog_hir.classes()) {
                        for (const auto& property : candidate.properties) {
                            if (property.declaration != *declaration
                                || property.static_storage) {
                                continue;
                            }
                            auto type = hir_class_value_container_type(
                                property.canonical_identity);
                            if (!type) {
                                break;
                            }
                            class_root = HirSvValuePath::ClassRoot {
                                std::nullopt, property.canonical_identity };
                            class_root_type
                                = std::make_shared<const ContainerType>(
                                    std::move(*type));
                            root = HirContainerObjectBinding {
                                property.declaration,
                                property.canonical_identity, { },
                                std::nullopt, class_root_type.get(), false };
                        }
                    }
                }
            }
            if (!root) {
                // `s.a.b` names an unpacked structure and its members.
                const auto names = dotted_names(source.text);
                if (names.size() < 2U) {
                    return std::nullopt;
                }
                const semantic::CompiledDeclarationPredicate object
                    = [](const semantic::CompiledDeclarationView& candidate) {
                          return candidate.systemverilog != nullptr
                              && candidate.systemverilog->type.has_value()
                              && (candidate.systemverilog->form
                                      == semantic::sv::DeclarationForm::
                                          variable
                                  || candidate.systemverilog->form
                                      == semantic::sv::DeclarationForm::port);
                      };
                const auto declaration
                    = hir_resolver()
                          .resolve_systemverilog(names.front(), source.scope,
                              object, false)
                          .unique();
                if (!declaration) {
                    return std::nullopt;
                }
                root = hir_container_object_binding_for_declaration(
                    *declaration);
                for (auto member = names.rbegin();
                    member + 1 != names.rend(); ++member) {
                    pending.push_back(PendingStep { *member, { } });
                }
            }
            break;
        }
        return std::nullopt;
    }
    if (!root || root->type == nullptr || pending.empty()) {
        return std::nullopt;
    }
    std::ranges::reverse(pending);
    // `obj.v[i][j]` of a multidimensional array arrives as nested selects:
    // one step takes an index per fixed dimension.
    while (pending.size() >= 2U && root->type->fixed
        && !pending[0].member && !pending[1].member
        && pending[0].indices.size()
            < std::max<std::size_t>(root->type->dimensions.size(), 1U)) {
        auto& indices = pending[0].indices;
        indices.insert(indices.end(), pending[1].indices.begin(),
            pending[1].indices.end());
        pending.erase(pending.begin() + 1);
    }

    HirSvValuePath path;
    path.root = *root;
    path.class_root = std::move(class_root);
    path.class_root_type = std::move(class_root_type);
    path.type = *root->type;
    for (std::size_t ordinal { }; ordinal < pending.size(); ++ordinal) {
        const auto& step = pending[ordinal];
        if (path.leaf) {
            // Members of a packed structure leaf select its bits (7.2.1).
            if (path.string_leaf || path.packed_tail || !step.member
                || path.type.element_nominal_type.empty()) {
                return std::nullopt;
            }
            std::string members;
            for (auto rest = ordinal; rest < pending.size(); ++rest) {
                if (!pending[rest].member) {
                    return std::nullopt;
                }
                if (!members.empty()) {
                    members += '.';
                }
                members += *pending[rest].member;
            }
            const auto nominal = semantic::sv::nominal_identity_type(
                path.type.element_nominal_type);
            if (!nominal) {
                return std::nullopt;
            }
            semantic::sv::TypeReference reference;
            reference.target.target = *nominal;
            const auto root_expression
                = specialized_hir_unit_->find_expression(expression_id);
            const auto selected = hir_systemverilog_type_member_path(
                reference, root_expression->systemverilog->scope,
                path.root.declaration, members);
            if (!selected || selected->width == 0U
                || selected->offset + selected->width
                    > path.type.element_width) {
                return std::nullopt;
            }
            path.packed_tail = HirSvValuePath::PackedTail {
                selected->offset, selected->width, selected->signed_value };
            break;
        }
        if (step.member) {
            if (!aggregate_value_type(path.type)
                || path.type.member_names.size()
                    != path.type.element_types.size()) {
                return std::nullopt;
            }
            const auto found = std::ranges::find(
                path.type.member_names, *step.member);
            if (found == path.type.member_names.end()) {
                return std::nullopt;
            }
            const auto member = static_cast<std::uint32_t>(
                std::distance(path.type.member_names.begin(), found));
            HirSvValuePath::Step selected;
            selected.member = member;
            path.steps.push_back(std::move(selected));
            auto member_type = path.type.element_types[member];
            // A string member is a string value, not a container.
            path.string_leaf = member_type.element_kind
                    == ContainerElementKind::String
                && singleton_member_type(member_type);
            // A queue or associative-array member is a container (7.10).
            path.leaf = (packed_leaf_type(member_type) && !member_type.queue
                            && !member_type.associative)
                || path.string_leaf;
            path.type = std::move(member_type);
            continue;
        }
        // An index selects one element of an unpacked array, queue or
        // associative array: one index per fixed dimension.
        // A structure value is not indexed.
        if (aggregate_value_type(path.type)) {
            return std::nullopt;
        }
        const auto expected = path.type.fixed
            ? std::max<std::size_t>(path.type.dimensions.size(), 1U)
            : std::size_t { 1U };
        if (step.indices.size() != expected) {
            return std::nullopt;
        }
        HirSvValuePath::Step selected;
        selected.indices = step.indices;
        selected.container = path.type;
        path.steps.push_back(std::move(selected));
        if (path.type.element_kind == ContainerElementKind::Packed
            || path.type.element_kind == ContainerElementKind::Scalar) {
            path.leaf = true;
            auto leaf = path.type;
            leaf.fixed = false;
            leaf.queue = false;
            leaf.associative = false;
            leaf.dimensions.clear();
            leaf.maximum_elements.reset();
            path.type = std::move(leaf);
            continue;
        }
        const auto element = nested_element_container_type(path.type);
        if (!element) {
            return std::nullopt;
        }
        path.type = *element;
    }
    return path;
}

std::optional<RegisterId> Lowerer::lower_hir_sv_value_path_index(
    const HirSvValuePath& path,
    const HirSvValuePath::Step& step,
    const std::optional<ContainerRegisterId> container)
{
    HirContainerElementBinding level;
    level.declaration = path.root.declaration;
    level.name = path.root.name;
    level.object = path.root.object;
    level.local = container ? container : path.root.local;
    level.type = &step.container;
    level.selected_type = &step.container;
    level.indices = step.indices;
    level.width = step.container.element_width;
    level.read_only = path.root.read_only;
    return lower_hir_container_element_index(level);
}

std::optional<Lowerer::HirSvValuePathCursor>
Lowerer::lower_hir_sv_value_path_descend(const HirSvValuePath& path)
{
    HirSvValuePathCursor cursor;
    if (path.class_root && !path.class_root->receiver) {
        if (!hir_class_receiver_register_) {
            return std::nullopt;
        }
        cursor.root = allocate_container_register(*path.root.type);
        process_.operations.emplace_back(ClassPropertyContainerRead {
            cursor.root, *hir_class_receiver_register_,
            path.class_root->identity });
        cursor.class_receiver = *hir_class_receiver_register_;
    } else if (path.class_root) {
        const auto held = lower_hir_class_value_container(
            *path.class_root->receiver, path.class_root->identity);
        if (!held || held->container.type != *path.root.type) {
            return std::nullopt;
        }
        cursor.root = held->container.value;
        cursor.class_receiver = held->receiver;
    } else if (path.root.local) {
        cursor.root = *path.root.local;
    } else {
        cursor.root = allocate_container_register(*path.root.type);
        process_.operations.emplace_back(ReadContainerObject {
            cursor.root, path.root.object });
        record_container_object_dependency(path.root.object);
    }
    cursor.container = cursor.root;
    // Every step but the last produces an intermediate container value.
    for (std::size_t index { }; index + 1U < path.steps.size(); ++index) {
        const auto& step = path.steps[index];
        HirSvValuePathCursor::Level level;
        level.container = cursor.container;
        if (step.member) {
            level.index = load_constant(
                unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
            level.members = { *step.member };
        } else {
            // Nested element access has no linearized form.
            if (step.indices.size() != 1U) {
                return std::nullopt;
            }
            const auto lowered = lower_hir_sv_value_path_index(
                path, step, cursor.container);
            if (!lowered) {
                return std::nullopt;
            }
            level.index = *lowered;
            level.signed_index = step.container.signed_indices;
        }
        // The value type after this step.
        const auto type = [&]() -> std::optional<ContainerType> {
            if (step.member) {
                const auto& owner = index == 0U
                    ? *path.root.type
                    : cursor.levels.back().type;
                return *step.member < owner.element_types.size()
                    ? std::optional { owner.element_types[*step.member] }
                    : std::nullopt;
            }
            return nested_element_container_type(step.container);
        }();
        if (!type) {
            return std::nullopt;
        }
        level.type = *type;
        const auto child = allocate_container_register(*type);
        process_.operations.emplace_back(ContainerElementRead {
            child, level.container, level.index, level.signed_index,
            level.members });
        cursor.container = child;
        cursor.levels.push_back(std::move(level));
    }
    return cursor;
}

void Lowerer::publish_hir_sv_value_path(
    const HirSvValuePath& path,
    const HirSvValuePathCursor& cursor)
{
    auto child = cursor.container;
    for (auto level = cursor.levels.rbegin(); level != cursor.levels.rend();
        ++level) {
        process_.operations.emplace_back(ContainerElementWrite {
            level->container, level->index, child, level->signed_index,
            level->members });
        child = level->container;
    }
    if (path.class_root) {
        if (cursor.class_receiver) {
            process_.operations.emplace_back(ClassPropertyContainerWrite {
                *cursor.class_receiver, cursor.root,
                path.class_root->identity });
        }
    } else if (!path.root.local) {
        process_.operations.emplace_back(WriteContainerObject {
            path.root.object, cursor.root, std::nullopt });
    }
}

std::optional<RegisterId> Lowerer::lower_hir_sv_value_path_read(
    const HirSvValuePath& path)
{
    if (!path.leaf || path.string_leaf || path.steps.empty()) {
        return std::nullopt;
    }
    const auto cursor = lower_hir_sv_value_path_descend(path);
    if (!cursor) {
        return std::nullopt;
    }
    const auto& step = path.steps.back();
    const auto destination = allocate_register(path.type.element_width,
        path.type.two_state ? frontend::ValueDomain::Bit2
                            : frontend::ValueDomain::Logic4);
    if (step.member) {
        const auto zero = load_constant(
            unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ContainerAggregateRead {
            destination, cursor->container, zero, { *step.member }, true,
            false });
        return destination;
    }
    const auto index = lower_hir_sv_value_path_index(
        path, step, cursor->container);
    if (!index) {
        return std::nullopt;
    }
    process_.operations.emplace_back(ContainerRead {
        destination, cursor->container, *index,
        step.indices.size() > 1U || step.container.signed_indices,
        step.indices.size() > 1U, step.container.string_indices });
    return destination;
}

std::optional<RegisterId> Lowerer::lower_hir_sv_value_path_value(
    const HirSvValuePath& path)
{
    const auto leaf = lower_hir_sv_value_path_read(path);
    if (!leaf || !path.packed_tail) {
        return leaf;
    }
    const auto selected = allocate_register(
        path.packed_tail->width, register_domain(*leaf));
    process_.operations.emplace_back(Extract {
        selected, *leaf, static_cast<std::uint32_t>(path.packed_tail->offset),
        static_cast<std::uint32_t>(path.packed_tail->width) });
    return selected;
}

std::optional<Lowerer::HirStaticContainerValue>
Lowerer::lower_hir_sv_value_path_container_read(const HirSvValuePath& path)
{
    if (path.leaf || path.steps.empty()) {
        return std::nullopt;
    }
    const auto cursor = lower_hir_sv_value_path_descend(path);
    if (!cursor) {
        return std::nullopt;
    }
    const auto& step = path.steps.back();
    std::optional<RegisterId> index;
    if (step.member) {
        index = load_constant(
            unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
    } else {
        if (step.indices.size() != 1U) {
            return std::nullopt;
        }
        index = lower_hir_sv_value_path_index(
            path, step, cursor->container);
    }
    if (!index) {
        return std::nullopt;
    }
    const auto destination = allocate_container_register(path.type);
    process_.operations.emplace_back(ContainerElementRead {
        destination, cursor->container, *index,
        step.member ? true : step.container.signed_indices,
        step.member ? std::vector<std::uint32_t> { *step.member }
                    : std::vector<std::uint32_t> { } });
    return HirStaticContainerValue { destination, path.type };
}

bool Lowerer::lower_hir_sv_value_path_write(
    const HirSvValuePath& path,
    const std::optional<RegisterId> packed_value,
    const std::optional<ContainerRegisterId> container_value)
{
    if (path.steps.empty() || path.root.read_only || path.string_leaf
        || (path.leaf != packed_value.has_value())
        || (!path.leaf && !container_value)) {
        return false;
    }
    const auto cursor = lower_hir_sv_value_path_descend(path);
    if (!cursor) {
        return false;
    }
    const auto& step = path.steps.back();
    std::optional<RegisterId> index;
    if (step.member) {
        index = load_constant(
            unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
    } else {
        index = lower_hir_sv_value_path_index(
            path, step, cursor->container);
    }
    if (!index) {
        return false;
    }
    if (path.leaf) {
        auto value = *packed_value;
        if (path.packed_tail) {
            // Replace the selected bits of the current leaf value.
            const auto current = lower_hir_sv_value_path_read(path);
            if (!current) {
                return false;
            }
            if (register_width(value) != path.packed_tail->width) {
                value = resize_register(
                    value, path.packed_tail->width, false);
            }
            const auto updated = allocate_register(
                register_width(*current), register_domain(*current));
            process_.operations.emplace_back(CopyRegister {
                updated, *current });
            process_.operations.emplace_back(Insert {
                updated, updated, value,
                static_cast<std::uint32_t>(path.packed_tail->offset) });
            value = updated;
        }
        if (register_width(value) != path.type.element_width) {
            value = resize_register(value, path.type.element_width, false);
        }
        if (step.member) {
            process_.operations.emplace_back(ContainerAggregateWrite {
                cursor->container, *index, value, { *step.member }, true,
                false });
        } else {
            process_.operations.emplace_back(ContainerWrite {
                cursor->container, *index, value,
                step.indices.size() > 1U || step.container.signed_indices,
                step.indices.size() > 1U, step.container.string_indices });
        }
    } else {
        if (!step.member && step.indices.size() != 1U) {
            return false;
        }
        process_.operations.emplace_back(ContainerElementWrite {
            cursor->container, *index, *container_value,
            step.member ? true : step.container.signed_indices,
            step.member ? std::vector<std::uint32_t> { *step.member }
                        : std::vector<std::uint32_t> { } });
    }
    publish_hir_sv_value_path(path, *cursor);
    return true;
}

} // namespace fsim::elaboration

namespace fsim::elaboration {

// The packed and scalar leaves of an unpacked structure value in declaration
// order, or nothing when a member is an array, string or other container.
std::optional<std::vector<Lowerer::HirAggregateLeaf>>
Lowerer::hir_aggregate_packed_leaves(const ContainerType& type) const
{
    if (type.element_kind != ContainerElementKind::Aggregate
        || !type.aggregate_value || type.element_types.empty()) {
        return std::nullopt;
    }
    std::vector<HirAggregateLeaf> leaves;
    const auto visit = [&](const auto& self, const ContainerType& value,
                           std::vector<std::uint32_t> members) -> bool {
        for (std::size_t index { }; index < value.element_types.size();
            ++index) {
            const auto& member = value.element_types[index];
            auto path = members;
            path.push_back(static_cast<std::uint32_t>(index));
            if (member.element_kind == ContainerElementKind::Aggregate
                && member.aggregate_value && !value.union_aggregate) {
                if (!self(self, member, std::move(path))) {
                    return false;
                }
                continue;
            }
            if ((member.element_kind != ContainerElementKind::Packed
                    && member.element_kind != ContainerElementKind::Scalar)
                || member.element_width == 0U || member.queue
                || member.associative
                || (member.fixed && member.dimensions.size() == 1U
                    && member.dimensions.front().first
                        != member.dimensions.front().second)
                || member.dimensions.size() > 1U || value.union_aggregate) {
                return false;
            }
            leaves.push_back(HirAggregateLeaf {
                std::move(path), member.element_width, member.two_state });
        }
        return true;
    };
    if (!visit(visit, type, { })) {
        return std::nullopt;
    }
    return leaves;
}

// Concatenate the leaves of an unpacked structure, first member most
// significant, as a class property stores it.
std::optional<RegisterId> Lowerer::pack_hir_aggregate_value(
    const ContainerRegisterId value, const ContainerType& type)
{
    const auto leaves = hir_aggregate_packed_leaves(type);
    if (!leaves) {
        return std::nullopt;
    }
    std::vector<RegisterId> parts;
    std::size_t width { };
    bool two_state = true;
    const auto zero = load_constant(
        unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
    for (const auto& leaf : *leaves) {
        const auto part = allocate_register(leaf.width,
            leaf.two_state ? frontend::ValueDomain::Bit2
                           : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ContainerAggregateRead {
            part, value, zero, leaf.members, true, false });
        parts.push_back(part);
        width += leaf.width;
        two_state = two_state && leaf.two_state;
    }
    if (width == 0U || width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto destination = allocate_register(width,
        two_state ? frontend::ValueDomain::Bit2
                  : frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(Concatenate {
        destination, std::move(parts), static_cast<std::uint32_t>(width) });
    return destination;
}

// Split a packed image into the leaves of an unpacked structure value.
std::optional<ContainerRegisterId> Lowerer::unpack_hir_aggregate_value(
    const RegisterId packed, const ContainerType& type)
{
    const auto leaves = hir_aggregate_packed_leaves(type);
    if (!leaves) {
        return std::nullopt;
    }
    std::size_t width { };
    for (const auto& leaf : *leaves) {
        width += leaf.width;
    }
    if (register_width(packed) != width) {
        return std::nullopt;
    }
    const auto destination = allocate_container_register(type);
    const auto zero = load_constant(
        unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
    auto remaining = width;
    for (const auto& leaf : *leaves) {
        remaining -= leaf.width;
        const auto part = allocate_register(leaf.width,
            leaf.two_state ? frontend::ValueDomain::Bit2
                           : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Extract {
            part, packed, static_cast<std::uint32_t>(remaining),
            static_cast<std::uint32_t>(leaf.width) });
        process_.operations.emplace_back(ContainerAggregateWrite {
            destination, zero, part, leaf.members, true, false });
    }
    return destination;
}

} // namespace fsim::elaboration

namespace fsim::elaboration {

// A nonblocking write of a packed member of an unpacked structure object,
// or of an array of them (IEEE 1800-2017 10.4.2): the member is published
// in the NBA region.
bool Lowerer::lower_hir_sv_value_path_nonblocking_write(
    const HirSvValuePath& path, const RegisterId value)
{
    if (!path.leaf || path.string_leaf || path.root.local || path.class_root
        || path.root.read_only || path.steps.empty()) {
        return false;
    }
    std::size_t first_member { };
    std::optional<RegisterId> index;
    bool signed_index = true;
    if (!path.steps.front().member) {
        const auto& step = path.steps.front();
        if (step.indices.size() != 1U) {
            return false;
        }
        index = lower_hir_sv_value_path_index(path, step, std::nullopt);
        signed_index = step.container.signed_indices;
        first_member = 1U;
    } else {
        index = load_constant(
            unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
    }
    if (!index || first_member >= path.steps.size()) {
        return false;
    }
    std::vector<std::uint32_t> members;
    for (auto step = path.steps.begin()
            + static_cast<std::ptrdiff_t>(first_member);
        step != path.steps.end(); ++step) {
        if (!step->member) {
            return false;
        }
        members.push_back(*step->member);
    }
    auto source = value;
    if (path.packed_tail) {
        // The rest of the packed member keeps its current value.
        const auto current = lower_hir_sv_value_path_read(path);
        if (!current) {
            return false;
        }
        if (register_width(source) != path.packed_tail->width) {
            source = resize_register(source, path.packed_tail->width, false);
        }
        const auto updated = allocate_register(
            register_width(*current), register_domain(*current));
        process_.operations.emplace_back(CopyRegister { updated, *current });
        process_.operations.emplace_back(Insert {
            updated, updated, source,
            static_cast<std::uint32_t>(path.packed_tail->offset) });
        source = updated;
    }
    if (register_width(source) != path.type.element_width) {
        source = resize_register(source, path.type.element_width, false);
    }
    WriteContainerObjectElement write;
    write.object = path.root.object;
    write.index = *index;
    write.source = source;
    write.signed_index = signed_index;
    write.nonblocking = true;
    write.members = std::move(members);
    process_.operations.emplace_back(std::move(write));
    return true;
}

} // namespace fsim::elaboration

namespace fsim::elaboration {

// Whether a streaming concatenation has an unpacked array, queue or
// structure operand (IEEE 1800-2017 11.4.14).
bool Lowerer::hir_sv_container_stream(
    const semantic::ExpressionId expression_id) const
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return false;
    }
    const auto& source = *expression->systemverilog;
    if (source.kind != semantic::sv::ExpressionKind::call
        || (source.text != "@stream-left" && source.text != "@stream-right")
        || source.operands.size() < 2U) {
        return false;
    }
    return std::any_of(source.operands.begin() + 1, source.operands.end(),
        [&](const semantic::ExpressionId operand) {
            return hir_static_container_expression_type(operand)
                .has_value();
        });
}

// The bit stream of a streaming concatenation as a queue of single bits:
// operands left to right, each most significant bit first, then the
// slice reordering of `<<` (IEEE 1800-2017 11.4.14.1, 11.4.14.2).
std::optional<ContainerRegisterId> Lowerer::lower_hir_sv_bitstream(
    const semantic::ExpressionId expression_id)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    if (source.kind != semantic::sv::ExpressionKind::call
        || (source.text != "@stream-left" && source.text != "@stream-right")
        || source.operands.size() < 2U) {
        return std::nullopt;
    }
    const auto slice = hir_constant_integer(source.operands.front());
    if (!slice || *slice <= 0
        || *slice > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    ContainerType bit_queue;
    bit_queue.element_kind = ContainerElementKind::Packed;
    bit_queue.element_width = 1U;
    bit_queue.queue = true;
    const auto bits = allocate_container_register(bit_queue);
    ContainerBitstream clear;
    clear.operation = ContainerBitstreamOperation::clear;
    clear.bits = bits;
    process_.operations.emplace_back(clear);
    for (auto operand = source.operands.begin() + 1;
        operand != source.operands.end(); ++operand) {
        ContainerBitstream append;
        append.bits = bits;
        if (hir_static_container_expression_type(*operand)) {
            const auto value = lower_hir_static_container_value(*operand);
            if (!value) {
                return std::nullopt;
            }
            append.operation = ContainerBitstreamOperation::append_container;
            append.container = value->value;
        } else {
            const auto width = hir_expression_width(
                *operand, hir_process_scope_);
            if (!width || *width == 0U) {
                return std::nullopt;
            }
            const auto value = lower_hir_expression(*operand, *width);
            if (!value) {
                return std::nullopt;
            }
            append.operation = ContainerBitstreamOperation::append_value;
            append.value = *value;
        }
        process_.operations.emplace_back(append);
    }
    if (source.text == "@stream-left") {
        ContainerBitstream reverse;
        reverse.operation = ContainerBitstreamOperation::reverse;
        reverse.bits = bits;
        reverse.slice = static_cast<std::uint32_t>(*slice);
        process_.operations.emplace_back(reverse);
    }
    return bits;
}

// A container stream in a packed context of `width` bits: left-justified
// and zero-filled (IEEE 1800-2017 11.4.14).
std::optional<RegisterId> Lowerer::lower_hir_sv_bitstream_value(
    const semantic::ExpressionId expression_id,
    const std::size_t width,
    const frontend::ValueDomain domain)
{
    if (width == 0U || width > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto bits = lower_hir_sv_bitstream(expression_id);
    if (!bits) {
        return std::nullopt;
    }
    const auto destination = allocate_register(width, domain);
    ContainerBitstream extract;
    extract.operation = ContainerBitstreamOperation::to_value;
    extract.bits = *bits;
    extract.value = destination;
    extract.slice = static_cast<std::uint32_t>(width);
    process_.operations.emplace_back(extract);
    return destination;
}

// A stream as the value of a container of `type` (6.24.3, 11.4.14.4).
std::optional<ContainerRegisterId> Lowerer::lower_hir_sv_bitstream_container(
    const semantic::ExpressionId expression_id,
    const ContainerType& type)
{
    std::optional<ContainerRegisterId> bits;
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression_id)
        : std::nullopt;
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& source = *expression->systemverilog;
    const bool stream = source.kind == semantic::sv::ExpressionKind::call
        && (source.text == "@stream-left" || source.text == "@stream-right");
    if (stream) {
        bits = lower_hir_sv_bitstream(expression_id);
    } else {
        // A packed value cast to an array type (6.24.3).
        const auto width = hir_expression_width(
            expression_id, hir_process_scope_);
        const auto value = width && *width != 0U
            ? lower_hir_expression(expression_id, *width)
            : std::nullopt;
        if (!value) {
            return std::nullopt;
        }
        ContainerType bit_queue;
        bit_queue.element_kind = ContainerElementKind::Packed;
        bit_queue.element_width = 1U;
        bit_queue.queue = true;
        bits = allocate_container_register(bit_queue);
        ContainerBitstream clear;
        clear.operation = ContainerBitstreamOperation::clear;
        clear.bits = *bits;
        process_.operations.emplace_back(clear);
        ContainerBitstream append;
        append.operation = ContainerBitstreamOperation::append_value;
        append.bits = *bits;
        append.value = *value;
        process_.operations.emplace_back(append);
    }
    if (!bits) {
        return std::nullopt;
    }
    const auto destination = allocate_container_register(type);
    ContainerBitstream unpack;
    unpack.operation = ContainerBitstreamOperation::to_container;
    unpack.bits = *bits;
    unpack.container = destination;
    process_.operations.emplace_back(unpack);
    return destination;
}

} // namespace fsim::elaboration
