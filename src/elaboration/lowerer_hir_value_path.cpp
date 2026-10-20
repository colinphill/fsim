// SPDX-License-Identifier: Apache-2.0
//
// Structured value paths over SystemVerilog unpacked values (IEEE 1800-2017
// 7.2, 7.4): a root container object, then index and member steps through
// arrays and unpacked aggregates. Reads and writes walk the path with the
// element and member container operations, so any nesting of arrays and
// structures uses one mechanism.
#include "lowerer_internal.hpp"

#include <cstdlib>
#include <iostream>

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
    // FSIM_SV_VALUE_PATH_TRACE names the line that rejected a path.
    const auto rejected = [&](const int line)
        -> std::optional<HirSvValuePath> {
        if (std::getenv("FSIM_SV_VALUE_PATH_TRACE") != nullptr) {
            std::cerr << "[fsim-sv-value-path] expression="
                      << expression_id.value() << " rejected line=" << line
                      << '\n';
        }
        return std::nullopt;
    };
    if (specialized_hir_unit_ == nullptr) {
        return rejected(__LINE__);
    }
    // Collect the syntactic steps from the expression, innermost last.
    struct PendingStep {
        std::optional<std::string_view> member;
        std::vector<semantic::ExpressionId> indices;
        // A part-select of a packed leaf (`m[i][3:0]`).
        std::optional<semantic::ExpressionId> select { };
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
            return rejected(__LINE__);
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
        if (source.kind == semantic::sv::ExpressionKind::slice
            && source.operands.size() == 3U) {
            pending.push_back(PendingStep { std::nullopt, { }, current });
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
                return rejected(__LINE__);
            }
            if (element) {
                pending.push_back(PendingStep { std::nullopt,
                    { source.operands.begin() + 1, source.operands.end() } });
            }
            const auto* property = hir_resolver()
                .resolve_systemverilog_class_property(identity, false)
                .unique();
            if (property == nullptr) {
                return rejected(__LINE__);
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
                    return rejected(__LINE__);
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
                    return rejected(__LINE__);
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
        if (std::getenv("FSIM_SV_VALUE_PATH_TRACE") != nullptr) {
            std::cerr << "[fsim-sv-value-path] unknown step kind="
                      << static_cast<int>(source.kind) << " text="
                      << source.text << " operands="
                      << source.operands.size() << '\n';
        }
        return rejected(__LINE__);
    }
    if (!root || root->type == nullptr || pending.empty()) {
        return rejected(__LINE__);
    }
    std::ranges::reverse(pending);
    // An index of an unpacked slice (`m[1][2:4][2]`) selects the array's
    // own element: a slice keeps the indices of the array it selects from
    // (IEEE 1800-2017 7.4.5, 7.4.6).
    for (std::size_t ordinal { }; ordinal + 1U < pending.size();) {
        auto& slice_step = pending[ordinal];
        const auto& index_step = pending[ordinal + 1U];
        if (!slice_step.select || index_step.member || index_step.select
            || index_step.indices.size() != 1U) {
            ++ordinal;
            continue;
        }
        const auto selection
            = specialized_hir_unit_->find_expression(*slice_step.select);
        const auto& slice = *selection->systemverilog;
        const auto first = hir_constant_integer(slice.operands[1]);
        const auto second = hir_constant_integer(slice.operands[2]);
        const auto index = hir_constant_integer(index_step.indices.front());
        if (first && second && index) {
            const auto low = slice.text == "+:" ? *first
                : slice.text == "-:"            ? *first - (*second - 1)
                                                : std::min(*first, *second);
            const auto high = slice.text == "+:" ? *first + (*second - 1)
                : slice.text == "-:"             ? *first
                                                 : std::max(*first, *second);
            if (*index < low || *index > high) {
                return rejected(__LINE__);
            }
        }
        slice_step = PendingStep { std::nullopt, index_step.indices };
        pending.erase(pending.begin()
            + static_cast<std::ptrdiff_t>(ordinal + 1U));
    }
    // `obj.v[i][j]` of a multidimensional array arrives as nested selects:
    // one step takes an index per fixed dimension.
    while (pending.size() >= 2U && root->type->fixed
        && !pending[0].member && !pending[1].member
        && !pending[0].select && !pending[1].select
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
    const auto root_scope = [&]() {
        const auto root_expression
            = specialized_hir_unit_->find_expression(expression_id);
        return root_expression->systemverilog->scope;
    };
    // The packed dimensions a select of the leaf has not yet consumed.
    std::optional<std::vector<HirPackedRange>> leaf_dimensions;
    bool part_selected { };
    for (std::size_t ordinal { }; ordinal < pending.size(); ++ordinal) {
        const auto& step = pending[ordinal];
        if (step.select && !path.leaf) {
            // A constant slice of a one-dimensional fixed array ends the
            // path (IEEE 1800-2017 7.4.6). A slice of a declared array of
            // packed elements is that array's packed slice instead.
            if (ordinal + 1U != pending.size() || !path.type.fixed
                || path.type.dimensions.size() != 1U
                || aggregate_value_type(path.type)
                || (path.steps.empty()
                    && (path.type.element_kind == ContainerElementKind::Packed
                        || path.type.element_kind
                            == ContainerElementKind::Scalar))) {
                return rejected(__LINE__);
            }
            const auto selection
                = specialized_hir_unit_->find_expression(*step.select);
            const auto& slice = *selection->systemverilog;
            const auto first = hir_constant_integer(slice.operands[1]);
            const auto second = hir_constant_integer(slice.operands[2]);
            if (!first || !second) {
                return rejected(__LINE__);
            }
            const auto declared_left = static_cast<std::int64_t>(
                path.type.dimensions.front().first);
            const auto declared_right = static_cast<std::int64_t>(
                path.type.dimensions.front().second);
            const bool descending = declared_left >= declared_right;
            std::int64_t left { *first };
            std::int64_t right { *second };
            if (slice.text == "+:" || slice.text == "-:") {
                if (*second <= 0) {
                    return rejected(__LINE__);
                }
                const auto low = slice.text == "+:"
                    ? *first : *first - (*second - 1);
                const auto high = low + (*second - 1);
                left = descending ? high : low;
                right = descending ? low : high;
            } else if (left != right && (left > right) != descending) {
                return rejected(__LINE__);
            }
            if (std::min(left, right) < std::min(declared_left, declared_right)
                || std::max(left, right)
                    > std::max(declared_left, declared_right)) {
                return rejected(__LINE__);
            }
            HirSvValuePath::Slice selected { left, right, path.type };
            path.type.dimensions.front() = {
                static_cast<std::int32_t>(left),
                static_cast<std::int32_t>(right) };
            path.type.index_left = static_cast<std::int32_t>(left);
            path.type.index_right = static_cast<std::int32_t>(right);
            path.slice = std::move(selected);
            continue;
        }
        if (path.leaf && !step.member) {
            // A bit-, element- or part-select of the packed leaf (IEEE
            // 1800-2017 7.4.5, 11.5.1).
            if (path.string_leaf || part_selected
                || (path.packed_tail && !leaf_dimensions)
                || (!step.select && step.indices.size() != 1U)) {
                return rejected(__LINE__);
            }
            if (!leaf_dimensions) {
                leaf_dimensions
                    = hir_sv_value_path_leaf_shape(path, root_scope());
                if (leaf_dimensions->empty()) {
                    return rejected(__LINE__);
                }
                path.packed_tail = HirSvValuePath::PackedTail {
                    0U, path.type.element_width, false, { } };
            }
            if (leaf_dimensions->empty()) {
                return rejected(__LINE__);
            }
            auto& tail = *path.packed_tail;
            const auto dimension = leaf_dimensions->front();
            leaf_dimensions->erase(leaf_dimensions->begin());
            const auto elements
                = index_distance(dimension.left, dimension.right) + 1U;
            if (elements == 0U || tail.width % elements != 0U) {
                return rejected(__LINE__);
            }
            const auto stride = tail.width / elements;
            // The element's distance from the dimension's right bound.
            const auto ordinal_of = [&](const std::int64_t index)
                -> std::optional<std::size_t> {
                if (index < std::min(dimension.left, dimension.right)
                    || index > std::max(dimension.left, dimension.right)) {
                    return std::nullopt;
                }
                return static_cast<std::size_t>(dimension.descending
                        ? index - dimension.right
                        : dimension.right - index);
            };
            if (!step.select) {
                if (const auto constant
                    = hir_constant_integer(step.indices.front())) {
                    const auto position = ordinal_of(*constant);
                    if (!position) {
                        return rejected(__LINE__);
                    }
                    tail.offset += *position * stride;
                } else {
                    tail.selects.push_back(HirSvValuePath::PackedSelect {
                        HirSvValuePath::PackedSelect::Kind::index,
                        step.indices.front(), dimension, stride, 1U });
                }
                tail.width = stride;
                continue;
            }
            const auto selection
                = specialized_hir_unit_->find_expression(*step.select);
            const auto& slice = *selection->systemverilog;
            part_selected = true;
            leaf_dimensions->clear();
            const auto first = hir_constant_integer(slice.operands[1]);
            const auto second = hir_constant_integer(slice.operands[2]);
            if (slice.text != "+:" && slice.text != "-:") {
                // `[a:b]` has constant bounds (IEEE 1800-2017 11.5.1).
                const auto left = first ? ordinal_of(*first) : std::nullopt;
                const auto right = second ? ordinal_of(*second)
                                          : std::nullopt;
                if (!left || !right
                    || (dimension.left != dimension.right
                        && (*first > *second) != dimension.descending)) {
                    return rejected(__LINE__);
                }
                tail.offset += std::min(*left, *right) * stride;
                tail.width = (std::max(*left, *right)
                                 - std::min(*left, *right) + 1U)
                    * stride;
                continue;
            }
            if (!second || *second <= 0
                || static_cast<std::uint64_t>(*second) > elements) {
                return rejected(__LINE__);
            }
            const auto count = static_cast<std::size_t>(*second);
            // `b +: n` spans b upward and `b -: n` downward; the lowest
            // bit is at the element nearest the right bound.
            const bool up = slice.text == "+:";
            if (first) {
                const auto low = up == dimension.descending
                    ? *first
                    : up ? *first + (*second - 1) : *first - (*second - 1);
                const auto high = up ? *first + (*second - 1)
                                     : *first - (*second - 1);
                const auto low_position = ordinal_of(low);
                if (!low_position || !ordinal_of(high)) {
                    return rejected(__LINE__);
                }
                tail.offset += *low_position * stride;
            } else {
                tail.selects.push_back(HirSvValuePath::PackedSelect {
                    up ? HirSvValuePath::PackedSelect::Kind::up
                       : HirSvValuePath::PackedSelect::Kind::down,
                    slice.operands[1], dimension, stride, count });
            }
            tail.width = count * stride;
            continue;
        }
        if (path.leaf) {
            // Members of a packed structure leaf select its bits (7.2.1).
            if (path.string_leaf || path.packed_tail || !step.member
                || path.type.element_nominal_type.empty()) {
                return rejected(__LINE__);
            }
            std::string members;
            for (auto rest = ordinal; rest < pending.size(); ++rest) {
                if (!pending[rest].member) {
                    return rejected(__LINE__);
                }
                if (!members.empty()) {
                    members += '.';
                }
                members += *pending[rest].member;
            }
            const auto nominal = semantic::sv::nominal_identity_type(
                path.type.element_nominal_type);
            if (!nominal) {
                return rejected(__LINE__);
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
                return rejected(__LINE__);
            }
            path.packed_tail = HirSvValuePath::PackedTail {
                selected->offset, selected->width, selected->signed_value,
                { } };
            break;
        }
        if (step.member) {
            if (!aggregate_value_type(path.type)
                || path.type.member_names.size()
                    != path.type.element_types.size()) {
                return rejected(__LINE__);
            }
            const auto found = std::ranges::find(
                path.type.member_names, *step.member);
            if (found == path.type.member_names.end()) {
                return rejected(__LINE__);
            }
            const auto member = static_cast<std::uint32_t>(
                std::distance(path.type.member_names.begin(), found));
            HirSvValuePath::Step selected;
            selected.member = member;
            selected.member_name = std::string { *step.member };
            path.steps.push_back(std::move(selected));
            auto member_type = path.type.element_types[member];
            // A string member is a string value, not a container.
            path.string_leaf = member_type.element_kind
                    == ContainerElementKind::String
                && singleton_member_type(member_type);
            // An array, queue or associative-array member is a container
            // (7.4, 7.10); a packed or scalar member holds one value.
            path.leaf = (packed_leaf_type(member_type) && member_type.fixed
                            && singleton_member_type(member_type))
                || path.string_leaf;
            path.type = std::move(member_type);
            continue;
        }
        // An index selects one element of an unpacked array, queue or
        // associative array: one index per fixed dimension.
        // A structure value is not indexed.
        if (aggregate_value_type(path.type)) {
            return rejected(__LINE__);
        }
        const auto expected = path.type.fixed
            ? std::max<std::size_t>(path.type.dimensions.size(), 1U)
            : std::size_t { 1U };
        // `s.m[i][j]` of a multidimensional member arrives as nested
        // selects: the step takes an index per fixed dimension.
        auto indices = step.indices;
        while (indices.size() < expected && ordinal + 1U < pending.size()
            && !pending[ordinal + 1U].member
            && !pending[ordinal + 1U].select) {
            ++ordinal;
            indices.insert(indices.end(), pending[ordinal].indices.begin(),
                pending[ordinal].indices.end());
        }
        if (indices.size() != expected) {
            return rejected(__LINE__);
        }
        HirSvValuePath::Step selected;
        selected.indices = std::move(indices);
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
            return rejected(__LINE__);
        }
        path.type = *element;
    }
    return path;
}

std::optional<Lowerer::HirPlaceSignalImage> Lowerer::hir_place_signal_image(
    const semantic::ExpressionId expression) const
{
    const auto path = hir_sv_value_path(expression);
    if (!path || path->class_root || path->string_leaf || path->slice
        || path->root.type == nullptr || path->root.local
        || path->steps.empty()
        || (path->packed_tail && !path->packed_tail->selects.empty())) {
        return std::nullopt;
    }
    const runtime::simir::ContainerSignalAlias* alias { };
    for (const auto& candidate : design_.container_signal_aliases_) {
        if (candidate.object != path->root.object) {
            continue;
        }
        if (alias != nullptr) {
            return std::nullopt;
        }
        alias = &candidate;
    }
    const auto total = container_signal_bridge_width(*path->root.type);
    if (alias == nullptr || !total
        || static_cast<std::size_t>(alias->signal)
            >= design_.signal_info_.size()
        || design_.signal_info_[static_cast<std::size_t>(alias->signal)].width
            != *total) {
        return std::nullopt;
    }
    // Walk the steps, counting bits from the most significant end.
    std::size_t above { };
    std::size_t width = *total;
    ContainerType current = *path->root.type;
    for (const auto& step : path->steps) {
        if (step.member) {
            if (*step.member >= current.element_types.size()) {
                return std::nullopt;
            }
            for (std::uint32_t member { }; member < *step.member; ++member) {
                const auto preceding = container_signal_bridge_width(
                    current.element_types[member]);
                if (!preceding) {
                    return std::nullopt;
                }
                above += *preceding;
            }
            auto selected = current.element_types[*step.member];
            current = std::move(selected);
            const auto member_width = container_signal_bridge_width(current);
            if (!member_width) {
                return std::nullopt;
            }
            width = *member_width;
            continue;
        }
        const auto& container = step.container;
        const auto element_width = container_packed_element_width(container);
        if (!container.fixed || !element_width
            || step.indices.size() != container.dimensions.size()) {
            return std::nullopt;
        }
        std::size_t position { };
        for (std::size_t dimension { }; dimension < step.indices.size();
            ++dimension) {
            const auto left = static_cast<std::int64_t>(
                container.dimensions[dimension].first);
            const auto right = static_cast<std::int64_t>(
                container.dimensions[dimension].second);
            const auto chosen = hir_constant_integer(step.indices[dimension]);
            if (!chosen || *chosen < std::min(left, right)
                || *chosen > std::max(left, right)) {
                return std::nullopt;
            }
            const auto extent = static_cast<std::size_t>(
                std::max(left, right) - std::min(left, right)) + 1U;
            position = position * extent
                + static_cast<std::size_t>(
                    left <= right ? *chosen - left : left - *chosen);
        }
        above += position * *element_width;
        width = *element_width;
        if (container.element_kind == ContainerElementKind::Packed
            || container.element_kind == ContainerElementKind::Scalar) {
            current = ContainerType { };
            continue;
        }
        const auto element = nested_element_container_type(container);
        if (!element) {
            return std::nullopt;
        }
        current = *element;
    }
    if (above + width > *total) {
        return std::nullopt;
    }
    HirConstantSelection selection {
        *total - above - width,
        width,
    };
    if (path->packed_tail) {
        if (path->packed_tail->offset + path->packed_tail->width > width) {
            return std::nullopt;
        }
        selection.offset += path->packed_tail->offset;
        selection.width = path->packed_tail->width;
    }
    return HirPlaceSignalImage {
        alias->signal, selection, path->root.declaration };
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
            // An element of a multidimensional fixed array is selected by
            // its linear position.
            if (step.indices.size() != 1U && !step.container.fixed) {
                return std::nullopt;
            }
            const auto lowered = lower_hir_sv_value_path_index(
                path, step, cursor.container);
            if (!lowered) {
                return std::nullopt;
            }
            level.index = *lowered;
            level.linear_index = step.indices.size() > 1U;
            level.signed_index = level.linear_index
                || step.container.signed_indices;
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
        ContainerElementRead read {
            child, level.container, level.index, level.signed_index,
            level.members };
        read.linear_index = level.linear_index;
        process_.operations.emplace_back(std::move(read));
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
        ContainerElementWrite write {
            level->container, level->index, child, level->signed_index,
            level->members };
        write.linear_index = level->linear_index;
        process_.operations.emplace_back(std::move(write));
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

std::vector<Lowerer::HirPackedRange> Lowerer::hir_sv_value_path_leaf_shape(
    const HirSvValuePath& path, const semantic::ScopeId scope) const
{
    std::vector<HirPackedRange> shape;
    std::string members;
    for (const auto& step : path.steps) {
        if (step.member) {
            if (!members.empty()) {
                members += '.';
            }
            members += step.member_name;
        }
    }
    // The recorded shape of an elaborated object's elements: its bounds
    // can name parameters of the object's own scope (IEEE 1800-2017 23.6).
    const auto recorded = members.empty() && !path.root.local
            && !path.class_root
        ? design_.container_element_packed_shape_.find(path.root.object)
        : design_.container_element_packed_shape_.end();
    const auto declaration = path.root.declaration.valid()
        ? specialized_hir_unit_->find_declaration(path.root.declaration)
        : std::nullopt;
    if (recorded != design_.container_element_packed_shape_.end()) {
        for (const auto& [left, right] : recorded->second) {
            shape.push_back(HirPackedRange { left, right, left >= right });
        }
    } else if (declaration && declaration->systemverilog != nullptr
        && declaration->systemverilog->type) {
        // The leaf's type is the declared element type, or a member of it.
        auto element = *declaration->systemverilog->type;
        element.unpacked_dimensions.clear();
        element.container_form.reset();
        if (!members.empty()) {
            const auto member = hir_systemverilog_type_member_path(
                element, scope, path.root.declaration, members, true);
            if (member && !member->shape.empty()) {
                shape = member->shape;
            } else if (member && member->range) {
                shape.push_back(*member->range);
            }
        } else if (const auto resolved
                   = hir_resolver().underlying_systemverilog_type(
                       element, scope)) {
            const auto append = [&](const semantic::sv::PackedRange& range) {
                if (range.left && range.right) {
                    shape.push_back(HirPackedRange { *range.left,
                        *range.right, *range.left >= *range.right });
                } else {
                    shape.clear();
                }
            };
            if (!resolved->packed_dimensions.empty()) {
                for (const auto& range : resolved->packed_dimensions) {
                    append(range);
                    if (shape.empty()) {
                        break;
                    }
                }
            } else if (resolved->packed_range) {
                append(*resolved->packed_range);
            }
        }
    }
    std::uint64_t width = 1U;
    for (const auto& dimension : shape) {
        width *= index_distance(dimension.left, dimension.right) + 1U;
    }
    if (std::getenv("FSIM_SV_VALUE_PATH_TRACE") != nullptr) {
        std::cerr << "[fsim-sv-value-path] leaf-shape members=" << members
                  << " object=" << path.root.object << " recorded="
                  << (recorded != design_.container_element_packed_shape_.end())
                  << " declaration=" << declaration.has_value()
                  << " dimensions=" << shape.size() << " width=" << width
                  << " element_width=" << path.type.element_width << '\n';
    }
    // The innermost elements can be wider than one bit (`packet_t [1:0]`
    // of a packed structure type, IEEE 1800-2017 7.4.1).
    if (shape.empty() || path.type.element_width % width != 0U) {
        return { };
    }
    return shape;
}

std::optional<RegisterId> Lowerer::lower_hir_sv_value_path_tail_offset(
    const HirSvValuePath& path)
{
    if (!path.packed_tail) {
        return std::nullopt;
    }
    const auto domain = frontend::ValueDomain::Logic4;
    auto offset = load_integer(
        static_cast<std::int64_t>(path.packed_tail->offset), 32U, domain);
    for (const auto& select : path.packed_tail->selects) {
        auto position = lower_hir_systemverilog_element_offset(
            select.index, select.dimension, select.stride);
        if (!position) {
            return std::nullopt;
        }
        // An indexed part-select's lowest bit is at its element nearest
        // the right bound (IEEE 1800-2017 11.5.1).
        using Kind = HirSvValuePath::PackedSelect::Kind;
        if (select.kind != Kind::index
            && (select.kind == Kind::up) != select.dimension.descending) {
            const auto adjusted = allocate_register(32U, domain);
            process_.operations.emplace_back(Binary {
                BinaryOperator::subtract_signed, adjusted, *position,
                load_integer(static_cast<std::int64_t>(
                                 (select.count - 1U) * select.stride),
                    32U, domain) });
            position = adjusted;
        }
        const auto sum = allocate_register(32U, domain);
        process_.operations.emplace_back(Binary {
            BinaryOperator::add_signed, sum, offset, *position });
        offset = sum;
    }
    return offset;
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
    if (!path.packed_tail->selects.empty()) {
        // Out-of-range bits read as X, or 0 for a two-state leaf (IEEE
        // 1800-2017 11.5.1).
        const auto offset = lower_hir_sv_value_path_tail_offset(path);
        if (!offset) {
            return std::nullopt;
        }
        process_.operations.emplace_back(DynamicPartSelect {
            selected, *leaf, *offset,
            static_cast<std::int64_t>(register_width(*leaf)) - 1, 0,
            static_cast<std::uint32_t>(path.packed_tail->width), true, true,
            path.type.two_state, 0U });
        return selected;
    }
    process_.operations.emplace_back(Extract {
        selected, *leaf, static_cast<std::uint32_t>(path.packed_tail->offset),
        static_cast<std::uint32_t>(path.packed_tail->width) });
    return selected;
}

std::optional<Lowerer::HirStaticContainerValue>
Lowerer::lower_hir_sv_value_path_container_read(const HirSvValuePath& path)
{
    if (path.slice) {
        // The whole array, then the slice's elements (7.4.6).
        auto base = path;
        base.slice.reset();
        base.type = path.slice->base;
        std::optional<ContainerRegisterId> whole;
        if (base.steps.empty()) {
            if (const auto cursor = lower_hir_sv_value_path_descend(base)) {
                whole = cursor->container;
            }
        } else if (const auto read
            = lower_hir_sv_value_path_container_read(base)) {
            whole = read->value;
        }
        const auto destination = allocate_container_register(path.type);
        if (!whole
            || !lower_hir_container_element_copy(*whole, destination,
                path.type, path.slice->left, path.slice->right)) {
            return std::nullopt;
        }
        return HirStaticContainerValue { destination, path.type };
    }
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
        if (step.indices.size() != 1U && !step.container.fixed) {
            return std::nullopt;
        }
        index = lower_hir_sv_value_path_index(
            path, step, cursor->container);
    }
    if (!index) {
        return std::nullopt;
    }
    const bool linear = !step.member && step.indices.size() > 1U;
    const auto destination = allocate_container_register(path.type);
    ContainerElementRead read {
        destination, cursor->container, *index,
        step.member || linear ? true : step.container.signed_indices,
        step.member ? std::vector<std::uint32_t> { *step.member }
                    : std::vector<std::uint32_t> { } };
    read.linear_index = linear;
    process_.operations.emplace_back(std::move(read));
    return HirStaticContainerValue { destination, path.type };
}

bool Lowerer::lower_hir_container_element_copy(
    const ContainerRegisterId source,
    const ContainerRegisterId target,
    const ContainerType& type,
    const std::int64_t left,
    const std::int64_t right,
    const bool source_positional)
{
    constexpr std::int64_t maximum_elements { 4096 };
    if (!type.fixed || type.dimensions.size() != 1U
        || std::max(left, right) - std::min(left, right) >= maximum_elements) {
        return false;
    }
    const auto element = nested_element_container_type(type);
    const auto count = std::max(left, right) - std::min(left, right) + 1;
    for (std::int64_t position { }; position < count; ++position) {
        // Elements in order from the left bound (IEEE 1800-2017 7.6).
        const auto at = left >= right ? left - position : left + position;
        const auto index = load_integer(at, 32U, frontend::ValueDomain::Integer);
        const auto from = source_positional
            ? load_integer(position, 32U, frontend::ValueDomain::Integer)
            : index;
        switch (type.element_kind) {
        case ContainerElementKind::Packed:
        case ContainerElementKind::Scalar: {
            const auto value = allocate_register(type.element_width,
                type.two_state ? frontend::ValueDomain::Bit2
                               : frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(ContainerRead {
                value, source, from, true, source_positional, false });
            process_.operations.emplace_back(ContainerWrite {
                target, index, value, true, false, false });
            break;
        }
        case ContainerElementKind::String: {
            const auto value = allocate_string_register();
            process_.operations.emplace_back(ContainerStringRead {
                value, source, from, true, source_positional, false, { } });
            process_.operations.emplace_back(ContainerStringWrite {
                target, index, value, true, false, false, { } });
            break;
        }
        case ContainerElementKind::Aggregate:
        case ContainerElementKind::Container: {
            if (!element) {
                return false;
            }
            const auto value = allocate_container_register(*element);
            ContainerElementRead read { value, source, from, true, { } };
            read.linear_index = source_positional;
            process_.operations.emplace_back(std::move(read));
            process_.operations.emplace_back(ContainerElementWrite {
                target, index, value, true, { } });
            break;
        }
        }
    }
    return true;
}

bool Lowerer::lower_hir_sv_value_path_write(
    const HirSvValuePath& path,
    const std::optional<RegisterId> packed_value,
    const std::optional<ContainerRegisterId> container_value)
{
    const auto write_rejected = [](const int line) {
        if (std::getenv("FSIM_SV_VALUE_PATH_TRACE") != nullptr) {
            std::cerr << "[fsim-sv-value-path] write rejected line=" << line
                      << '\n';
        }
        return false;
    };
    if (path.slice) {
        // Replace the slice's elements of the whole array (7.4.6, 7.6).
        if (path.leaf || !container_value || path.root.read_only) {
            return write_rejected(__LINE__);
        }
        auto base = path;
        base.slice.reset();
        base.type = path.slice->base;
        if (base.steps.empty()) {
            const auto cursor = lower_hir_sv_value_path_descend(base);
            if (!cursor
                || !lower_hir_container_element_copy(*container_value,
                    cursor->container, path.type, path.slice->left,
                    path.slice->right, true)) {
                return write_rejected(__LINE__);
            }
            publish_hir_sv_value_path(base, *cursor);
            return true;
        }
        const auto whole = lower_hir_sv_value_path_container_read(base);
        if (!whole
            || !lower_hir_container_element_copy(*container_value,
                whole->value, path.type, path.slice->left,
                path.slice->right, true)) {
            return write_rejected(__LINE__);
        }
        return lower_hir_sv_value_path_write(base, std::nullopt, whole->value);
    }
    if (path.steps.empty() || path.root.read_only || path.string_leaf
        || (path.leaf != packed_value.has_value())
        || (!path.leaf && !container_value)) {
        return write_rejected(__LINE__);
    }
    const auto cursor = lower_hir_sv_value_path_descend(path);
    if (!cursor) {
        return write_rejected(__LINE__);
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
        return write_rejected(__LINE__);
    }
    if (path.leaf) {
        auto value = *packed_value;
        if (path.packed_tail) {
            // Replace the selected bits of the current leaf value.
            const auto current = lower_hir_sv_value_path_read(path);
            if (!current) {
                return write_rejected(__LINE__);
            }
            if (register_width(value) != path.packed_tail->width) {
                value = resize_register(
                    value, path.packed_tail->width, false);
            }
            if (register_domain(value) != register_domain(*current)) {
                const auto converted = allocate_register(
                    register_width(value), register_domain(*current));
                process_.operations.emplace_back(CopyRegister {
                    converted, value });
                value = converted;
            }
            const auto updated = allocate_register(
                register_width(*current), register_domain(*current));
            if (!path.packed_tail->selects.empty()) {
                // A wholly out-of-range select writes nothing (IEEE
                // 1800-2017 11.5.1).
                const auto offset = lower_hir_sv_value_path_tail_offset(path);
                if (!offset) {
                    return write_rejected(__LINE__);
                }
                process_.operations.emplace_back(DynamicPartInsert {
                    updated, *current, value,
                    DynamicPartIndex { *offset,
                        static_cast<std::int64_t>(register_width(*current))
                            - 1,
                        0, 0U,
                        static_cast<std::uint32_t>(path.packed_tail->width),
                        true, true } });
            } else {
                process_.operations.emplace_back(CopyRegister {
                    updated, *current });
                process_.operations.emplace_back(Insert {
                    updated, updated, value,
                    static_cast<std::uint32_t>(path.packed_tail->offset) });
            }
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
        if (!step.member && step.indices.size() != 1U
            && !step.container.fixed) {
            return write_rejected(__LINE__);
        }
        const bool linear = !step.member && step.indices.size() > 1U;
        ContainerElementWrite write {
            cursor->container, *index, *container_value,
            step.member || linear ? true : step.container.signed_indices,
            step.member ? std::vector<std::uint32_t> { *step.member }
                        : std::vector<std::uint32_t> { } };
        write.linear_index = linear;
        process_.operations.emplace_back(std::move(write));
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
    const HirSvValuePath& path, const RegisterId value,
    const std::function<bool()>& before_write)
{
    const auto write_rejected = [](const int line) {
        if (std::getenv("FSIM_SV_VALUE_PATH_TRACE") != nullptr) {
            std::cerr << "[fsim-sv-value-path] nonblocking-write rejected line=" << line
                      << '\n';
        }
        return false;
    };
    if (!path.leaf || path.string_leaf || path.root.local || path.class_root
        || path.root.read_only || path.steps.empty()) {
        return write_rejected(__LINE__);
    }
    std::optional<RegisterId> index;
    bool signed_index = true;
    bool linear_index { };
    std::vector<std::uint32_t> members;
    std::vector<ContainerPathLevel> levels;
    const auto lower_leaf_index = [&](const HirSvValuePath::Step& step,
                                      const std::optional<ContainerRegisterId>
                                          container) {
        if (step.indices.empty() || step.container.string_indices) {
            return write_rejected(__LINE__);
        }
        index = lower_hir_sv_value_path_index(path, step, container);
        linear_index = step.indices.size() > 1U;
        signed_index = linear_index || step.container.signed_indices;
        return index.has_value();
    };
    if (std::any_of(path.steps.begin() + 1, path.steps.end(),
            [](const auto& step) { return !step.member; })) {
        // An element of a nested container (`m[i][j]` of a queue of
        // queues, `a[i].q[j]`): the indices of every level are evaluated
        // now and the object is rebuilt when the write is published.
        const auto cursor = lower_hir_sv_value_path_descend(path);
        if (!cursor) {
            return write_rejected(__LINE__);
        }
        for (const auto& level : cursor->levels) {
            levels.push_back(ContainerPathLevel {
                level.index, level.signed_index, false, level.members });
        }
        const auto& leaf = path.steps.back();
        if (leaf.member) {
            index = load_constant(
                unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
            members.push_back(*leaf.member);
        } else if (!lower_leaf_index(leaf, cursor->container)) {
            return write_rejected(__LINE__);
        }
    } else {
        std::size_t first_member { };
        if (!path.steps.front().member) {
            if (!lower_leaf_index(path.steps.front(), std::nullopt)) {
                return write_rejected(__LINE__);
            }
            first_member = 1U;
        } else {
            index = load_constant(
                unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
        }
        if (first_member >= path.steps.size() && !path.packed_tail) {
            return write_rejected(__LINE__);
        }
        for (auto step = path.steps.begin()
                + static_cast<std::ptrdiff_t>(first_member);
            step != path.steps.end(); ++step) {
            members.push_back(*step->member);
        }
    }
    auto source = value;
    std::optional<DynamicPartIndex> part;
    if (path.packed_tail) {
        if (register_width(source) != path.packed_tail->width) {
            source = resize_register(source, path.packed_tail->width, false);
        }
        const auto offset = path.packed_tail->selects.empty()
            ? load_integer(
                  static_cast<std::int64_t>(path.packed_tail->offset), 32U,
                  frontend::ValueDomain::Logic4)
            : lower_hir_sv_value_path_tail_offset(path);
        if (!offset) {
            return write_rejected(__LINE__);
        }
        const DynamicPartIndex selection { *offset,
            static_cast<std::int64_t>(path.type.element_width) - 1, 0, 0U,
            static_cast<std::uint32_t>(path.packed_tail->width), true, true };
        if (members.empty()) {
            // The selected bits are replaced when the update is published,
            // so other updates of the element in the slot keep theirs.
            part = selection;
        } else {
            // The rest of the packed member keeps its current value.
            const auto current = lower_hir_sv_value_path_read(path);
            if (!current) {
                return write_rejected(__LINE__);
            }
            const auto updated = allocate_register(
                register_width(*current), register_domain(*current));
            process_.operations.emplace_back(DynamicPartInsert {
                updated, *current, source, selection });
            source = updated;
        }
    }
    const auto element_width = part ? path.packed_tail->width
                                    : path.type.element_width;
    if (register_width(source) != element_width) {
        source = resize_register(source, element_width, false);
    }
    const auto domain = path.type.two_state ? frontend::ValueDomain::Bit2
                                            : frontend::ValueDomain::Logic4;
    if (register_domain(source) != domain) {
        const auto converted = allocate_register(element_width, domain);
        process_.operations.emplace_back(CopyRegister { converted, source });
        source = converted;
    }
    if (before_write && !before_write()) {
        return write_rejected(__LINE__);
    }
    WriteContainerObjectElement write;
    write.object = path.root.object;
    write.index = *index;
    write.source = source;
    write.signed_index = signed_index;
    write.linear_index = linear_index;
    write.nonblocking = true;
    write.dynamic_part = part;
    write.members = std::move(members);
    write.levels = std::move(levels);
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
