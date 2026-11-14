// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_class_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/runtime/vpi_abi.h"

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
#include <vpi_user.h>

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
        // The name of a registry object.
        std::function<std::string(fsim_vpi_handle_v1)> name;
        // The expression object of HIR expression `id`, a child of the
        // object `parent` (an extends object's constructor arguments).
        std::function<fsim_vpi_handle_v1(
            semantic::ExpressionId, fsim_vpi_handle_v1)>
            expression;
        // The typespec of a class parameter as the elaborator recorded it
        // (class declaration identity, specialization identity, empty for
        // the declaration's own, and name): the declared type of a value
        // parameter, the type a type parameter names; 0 when none is.
        std::function<fsim_vpi_handle_v1(
            const std::string&, const std::string&, const std::string&)>
            parameter_typespec;
        // The typespec of a typedef a class declares as the elaborator
        // recorded it (class declaration identity, specialization identity,
        // empty for the declaration's own, and name); 0 when none is.
        std::function<fsim_vpi_handle_v1(
            const std::string&, const std::string&, const std::string&)>
            typedef_typespec;
        // A property of a class typespec that represents specialization
        // `specialization` (its identity) of class `owner` (37.32 detail
        // 5), from the defn's registry property `declared`: the handle of a
        // static one, which holds the specialization's value (detail 6); 0
        // for an automatic one, which the model represents.
        std::function<fsim_vpi_handle_v1(fsim_vpi_handle_v1,
            const std::string&, const std::string&, fsim_vpi_handle_v1)>
            specialization_member;
        // The vpiType, an integer property and the typespec of the
        // automatic property `declared` (the defn's registry property) in
        // specialization `specialization` (its identity) of class `owner`.
        std::function<PLI_INT32(
            fsim_vpi_handle_v1, const std::string&, const std::string&)>
            variable_type;
        std::function<std::optional<std::int64_t>(fsim_vpi_handle_v1,
            const std::string&, const std::string&, PLI_INT32)>
            variable_property;
        std::function<fsim_vpi_handle_v1(
            fsim_vpi_handle_v1, const std::string&, const std::string&)>
            variable_typespec;
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

    /// The class typespec of a class identity: a specialization's, or the
    /// declaration's (its default specialization); 0 for any other text, as
    /// for the built-in classes, which have no identity (37.32 detail 13).
    [[nodiscard]] fsim_vpi_handle_v1 typespec_for_identity(
        const std::string& identity);
    /// The vpiClassType of a class named in the source (37.32): a built-in
    /// mailbox, semaphore or process, or a user-defined class.
    [[nodiscard]] static std::int64_t class_type_of(std::string_view name);

    /// The class a class typespec names: its declaration identity and its
    /// specialization identity (empty for a lexical typespec); none for any
    /// other object of the model.
    [[nodiscard]] std::optional<std::pair<std::string, std::string>>
    typespec_class(std::uint64_t serial) const;
    /// The expression of a constraint leaf object (37.34); null for any other
    /// object. vpi_get_value on a constant leaf reads it.
    [[nodiscard]] const semantic::sv::ConstraintExpression* leaf_expression(
        std::uint64_t serial) const;
    /// The class property a name leaf of a constraint refers to, by its
    /// canonical identity (the binding the frontend recorded); none for any
    /// other object.
    [[nodiscard]] std::optional<std::string> leaf_property(
        std::uint64_t serial) const;
    /// A class parameter of the model as the elaborator recorded it: the
    /// class declaration identity, the specialization identity (empty for
    /// the declaration's own), the name and whether it is a type
    /// parameter; none for any other object.
    struct ParameterKey {
        std::string declaration;
        std::string specialization;
        std::string name;
        bool type_parameter { };
    };
    [[nodiscard]] std::optional<ParameterKey> parameter_key(
        std::uint64_t serial) const;
    /// The declaration that holds the body of a method object (37.31): the
    /// method's own, or for an extern prototype its out-of-block
    /// definition's (8.24); none for any other object.
    [[nodiscard]] std::optional<semantic::DeclarationId> method_body(
        std::uint64_t serial) const;
    /// Whether a method object is a constructor, a function or a task
    /// (8.7, 37.31); none for any other object.
    [[nodiscard]] std::optional<semantic::sv::ClassMethodKind> method_kind(
        std::uint64_t serial) const;
    /// The method object (the one the class lists, 37.31 detail 4) whose
    /// declaration or out-of-block definition is `declaration`; 0 when no
    /// class method has it.
    [[nodiscard]] fsim_vpi_handle_v1 method_object(
        semantic::DeclarationId declaration);
    /// The body declaration of class method declaration `declaration`
    /// (an extern prototype's definition), or `declaration` itself.
    [[nodiscard]] semantic::DeclarationId method_body_declaration(
        semantic::DeclarationId declaration) const;
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

    /// The class typespec a property `name` of class defn `owner` is
    /// declared with: a lexical construct whose parameter assignments are
    /// the actuals written there, else the defaults (37.32 details 1 and
    /// 3); 0 when its type names no class.
    [[nodiscard]] fsim_vpi_handle_v1 item_typespec(
        const std::string& owner, const std::string& name);

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
        // The assignment of a specialization's actual to a class parameter
        // (37.32 vpiParamAssign): `member` is the parameter's index.
        param_assign,
        // A constraint (37.34), and an item of one: a constraint
        // expression, ordering, distribution or leaf expression (37.34,
        // 37.38) whose `member` is its index in `nodes_`.
        constraint,
        constraint_item,
        // A parameter of the class body of a specialization (a local
        // parameter, 37.31 detail 7) with the value it has there: `member`
        // is the property's index. The defn's are registry objects.
        body_parameter,
        // An automatic property of a class typespec that represents a
        // specialization (37.32 detail 5), which holds no value (detail 6):
        // `member` is the defn's registry property handle.
        variable,
    };
    static constexpr std::size_t none = static_cast<std::size_t>(-1);
    // A lexical param assign packs its use above the parameter's index.
    static constexpr std::size_t param_assign_stride = 4096U;
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
    // The VPI type of a leaf expression of a constraint (37.34, 37.38).
    [[nodiscard]] PLI_INT32 leaf_type(
        const semantic::sv::ConstraintExpression& expression) const;
    // What an index applied to a leaf selects (37.19): the type of the
    // elements its unpacked dimensions hold and how many unpacked
    // dimensions are left to select; none when the leaf has no declared
    // type the model knows.
    struct SelectShape {
        const semantic::sv::TypeReference* element { };
        std::size_t unpacked { };
    };
    [[nodiscard]] SelectShape select_shape(
        const semantic::sv::TypeReference& type) const;
    [[nodiscard]] std::optional<SelectShape> leaf_shape(
        const semantic::sv::ConstraintExpression& expression) const;
    [[nodiscard]] std::optional<std::int64_t> item_property(
        const Node& node, PLI_INT32 property) const;

    [[nodiscard]] const semantic::sv::ClassDeclaration& class_at(
        std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> find_declaration(
        const std::string& identity) const;
    [[nodiscard]] std::string identity(std::size_t index) const;
    [[nodiscard]] ParameterKey parameter_key_of(
        std::size_t declaration, std::size_t specialization,
        std::size_t index) const;
    [[nodiscard]] fsim_vpi_handle_v1 make(Kind kind, std::size_t declaration,
        std::size_t specialization, std::size_t member);
    [[nodiscard]] fsim_vpi_handle_v1 typespec_of(
        std::size_t declaration, std::size_t specialization);
    // The expression a lexical use of a class gives parameter `index` of
    // `base`: the explicit actual the use wrote (an `extends`, a class
    // item's declared type or a typedef), else the parameter's default
    // (37.32 detail 3).
    [[nodiscard]] std::optional<semantic::ExpressionId> lexical_actual(
        std::size_t use, std::size_t base, std::size_t index) const;
    // The number of a lexical use, by the actuals it wrote (plus one, so a
    // typespec without a use keeps member 0).
    [[nodiscard]] std::size_t use_of(
        const std::vector<semantic::sv::ActualAssociation>& actuals);
    // The declared type a class typedef names.
    [[nodiscard]] const semantic::sv::TypeReference* typedef_reference(
        const Entry& entry) const;
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
    // The specializations of each declaration, and each by its identity.
    std::unordered_map<std::string, std::vector<std::size_t>> specialized_;
    std::unordered_map<std::string, std::size_t> specialization_index_;
    std::vector<Entry> entries_;
    std::vector<const std::vector<semantic::sv::ActualAssociation>*> uses_;
    std::map<const std::vector<semantic::sv::ActualAssociation>*, std::size_t>
        use_index_;
    std::vector<Node> nodes_;
    std::map<std::pair<const semantic::sv::ConstraintExpression*, std::size_t>,
        std::size_t>
        node_index_;
    std::map<std::tuple<std::uint8_t, std::size_t, std::size_t, std::size_t>,
        std::size_t>
        index_;
};

} // namespace fsim::app::application_detail
