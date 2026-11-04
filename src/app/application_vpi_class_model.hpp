// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_bridge.h"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_class_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "sv_vpi_user.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <unordered_map>
#include <vector>

namespace fsim::app::application_detail {

/// The class definitions and class typespecs of a design as VPI objects
/// (IEEE 1800-2023 37.31, 37.32), built on demand from the HIR and the class
/// specializations. A class defn is the registry's class object; the model
/// answers for it by its declaration identity. Everything else (class
/// typespecs, extends objects, methods, class parameters and typedefs) is
/// owned by the model and identified by a serial.
class VpiClassModel final {
public:
    struct Hooks {
        // The handle of object `serial`.
        std::function<fsim_vpi_handle_v1(std::uint64_t)> handle;
        // The registry's class defn object of a declaration identity; 0
        // when none is published.
        std::function<fsim_vpi_handle_v1(const std::string&)> defn;
        // The registry objects of a class defn that iterate as `type`:
        // its properties, body parameters, events and nested classes.
        std::function<std::vector<fsim_vpi_handle_v1>(
            const std::string&, PLI_INT32)>
            members;
        // The full name of a registry object.
        std::function<std::string(fsim_vpi_handle_v1)> full_name;
    };

    VpiClassModel(const semantic::sv::Hir& hir,
        const semantic::Model& semantics,
        std::span<const semantic::sv::ClassSpecialization> specializations,
        Hooks hooks);

    /// Whether `serial` names an object of the model.
    [[nodiscard]] bool owns(std::uint64_t serial) const noexcept;
    [[nodiscard]] PLI_INT32 type(std::uint64_t serial) const;
    [[nodiscard]] std::optional<std::int64_t> property(
        std::uint64_t serial, PLI_INT32 property) const;
    [[nodiscard]] std::optional<std::string> string_property(
        std::uint64_t serial, PLI_INT32 property) const;
    /// vpi_handle(type, object): the defn of a typespec, the base class
    /// typespec of an extends object, an object's scope.
    [[nodiscard]] fsim_vpi_handle_v1 relation(
        std::uint64_t serial, PLI_INT32 type);
    /// vpi_iterate(type, object) of a class typespec; none when `type` is
    /// no relation of the object.
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> iterate(
        std::uint64_t serial, PLI_INT32 type);

    /// The class typespec a source spelling
    /// (`Box`, `pkg::Box#(4)`) or a class identity names; 0 when no class
    /// does, as for the built-in classes (37.32 detail 13).
    [[nodiscard]] fsim_vpi_handle_v1 typespec_for_name(
        std::string_view spelling);
    /// The vpiClassType of a class named in the source (37.32): a built-in
    /// mailbox, semaphore or process, or a user-defined class.
    [[nodiscard]] static std::int64_t class_type_of(std::string_view name);

    /// The class a class typespec names: its declaration identity and its
    /// specialization identity (empty for a lexical typespec); none for any
    /// other object of the model.
    [[nodiscard]] std::optional<std::pair<std::string, std::string>>
    typespec_class(std::uint64_t serial) const;
    /// The canonical identity and the name of a constraint of the model;
    /// none for any other object.
    [[nodiscard]] std::optional<std::pair<std::string, std::string>>
    constraint_identity(std::uint64_t serial) const;

    /// The class defn of declaration identity `identity` (37.31).
    [[nodiscard]] std::optional<std::int64_t> defn_property(
        const std::string& identity, PLI_INT32 property) const;
    [[nodiscard]] fsim_vpi_handle_v1 defn_relation(
        const std::string& identity, PLI_INT32 type);
    [[nodiscard]] std::optional<std::vector<fsim_vpi_handle_v1>> defn_iterate(
        const std::string& identity, PLI_INT32 type);

    /// A property of the class `owner`: vpiVisibility and vpiRandType
    /// (37.17); none for the others.
    [[nodiscard]] std::optional<std::int64_t> member_property(
        const std::string& owner, const std::string& name, PLI_INT32 property);

private:
    enum class Kind : std::uint8_t {
        typespec,
        extends,
        method,
        parameter,
        class_typedef,
        // A constraint (37.34), and an item of one: a constraint
        // expression, ordering, distribution or leaf expression (37.34,
        // 37.38) whose `member` is its index in `nodes_`.
        constraint,
        constraint_item,
    };
    static constexpr std::size_t none = static_cast<std::size_t>(-1);
    // A lexical class has `specialization` none.
    struct Entry {
        Kind kind { Kind::typespec };
        std::size_t declaration { };
        std::size_t specialization { none };
        std::size_t member { };
    };

    // One item of a constraint's expression tree. The HIR keeps constraints
    // as expression trees, so an item is a pointer into the declaration.
    struct Node {
        const semantic::sv::ConstraintExpression* expression { };
        std::size_t declaration { };
        std::size_t specialization { none };
        std::size_t constraint { };
        // The enclosing item, none for an item of the constraint itself.
        std::size_t parent { none };
        // The item was written `soft` (37.38).
        bool soft { };
    };

    [[nodiscard]] fsim_vpi_handle_v1 item_of(const Entry& owner,
        std::size_t constraint, std::size_t parent,
        const semantic::sv::ConstraintExpression& expression, bool soft);
    // The items a constraint set stands for: a block's items, the item
    // itself otherwise; `soft` marks the item it wraps.
    void collect_items(const Entry& owner, std::size_t constraint,
        std::size_t parent, const semantic::sv::ConstraintExpression& set,
        bool soft, std::vector<fsim_vpi_handle_v1>& out);
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> constraint_iterate(
        const Entry& entry, PLI_INT32 type);
    [[nodiscard]] fsim_vpi_handle_v1 constraint_relation(
        const Entry& entry, PLI_INT32 type);
    [[nodiscard]] PLI_INT32 item_type(const Node& node) const;
    [[nodiscard]] std::optional<std::int64_t> item_property(
        const Node& node, PLI_INT32 property) const;

    [[nodiscard]] const semantic::sv::ClassDeclaration& class_at(
        std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> find_declaration(
        const std::string& identity) const;
    [[nodiscard]] std::string identity(std::size_t index) const;
    [[nodiscard]] fsim_vpi_handle_v1 make(Kind kind, std::size_t declaration,
        std::size_t specialization, std::size_t member);
    [[nodiscard]] fsim_vpi_handle_v1 typespec_of(
        std::size_t declaration, std::size_t specialization);
    [[nodiscard]] std::size_t default_specialization(
        std::size_t declaration) const;
    [[nodiscard]] fsim_vpi_handle_v1 extends_of(
        std::size_t declaration, std::size_t specialization);
    [[nodiscard]] fsim_vpi_handle_v1 defn_of(std::size_t declaration) const;
    [[nodiscard]] std::string typespec_name(
        std::size_t declaration, std::size_t specialization) const;
    [[nodiscard]] std::string defn_full_name(std::size_t declaration) const;
    [[nodiscard]] std::vector<fsim_vpi_handle_v1> members_of(
        std::size_t declaration, std::size_t specialization, PLI_INT32 type);
    [[nodiscard]] PLI_INT32 typedef_type(const Entry& entry) const;
    [[nodiscard]] fsim_vpi_handle_v1 typedef_class(const Entry& entry);

    const semantic::sv::Hir& hir_;
    const semantic::Model& semantics_;
    std::span<const semantic::sv::ClassSpecialization> specializations_;
    Hooks hooks_;
    std::unordered_map<std::string, std::size_t> identities_;
    std::unordered_map<std::string, std::vector<std::size_t>> by_name_;
    // The specializations of each declaration, and each by its identity.
    std::unordered_map<std::string, std::vector<std::size_t>> specialized_;
    std::unordered_map<std::string, std::size_t> specialization_index_;
    std::vector<Entry> entries_;
    std::vector<Node> nodes_;
    std::map<std::pair<const semantic::sv::ConstraintExpression*, std::size_t>,
        std::size_t>
        node_index_;
    std::map<std::tuple<std::uint8_t, std::size_t, std::size_t, std::size_t>,
        std::size_t>
        index_;
};

} // namespace fsim::app::application_detail
