// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/tcl_references.hpp"

#if defined(FSIM_HAS_TCL)

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

using fsim::app::tcl_detail::TclCatalogMutation;
using fsim::app::tcl_detail::TclReferenceError;
using fsim::app::tcl_detail::TclReferenceKind;
using fsim::app::tcl_detail::TclReferenceTable;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "tcl_references_test: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void expect_live(
    const TclReferenceTable& references,
    const std::string_view token,
    const TclReferenceKind kind,
    const std::string_view identity)
{
    const auto result = references.resolve(token, kind);
    check(result.error == TclReferenceError::none,
        "expected a live reference");
    check(result.reference.has_value(), "live reference has no record");
    check(result.reference->identity == identity,
        "resolved reference has a different identity");
}

void expect_error(
    const TclReferenceTable& references,
    const std::string_view token,
    const TclReferenceKind kind,
    const TclReferenceError expected)
{
    const auto result = references.resolve(token, kind);
    check(result.error == expected, "reference returned the wrong error");
    check(!result.reference.has_value(), "failed reference returned a record");
}

void successful_catalog_change_invalidates_only_its_library(
    const TclCatalogMutation mutation)
{
    TclReferenceTable references;
    const auto affected = references.create_catalog_reference(
        "work", "unit:old-definition");
    const auto same_library = references.create_catalog_reference(
        "work", "unit:other-definition");
    const auto other_library = references.create_catalog_reference(
        "vendor", "unit:vendor-definition");
    const auto loaded = references.create_loaded_reference("object:17");

    check(references.catalog_generation("work") == 1U,
        "catalog generation did not start at one");
    references.catalog_mutation_completed("work", mutation, true);
    check(references.catalog_generation("work") == 2U,
        "successful catalog change did not advance its generation");

    expect_error(references, affected,
        TclReferenceKind::catalog_definition, TclReferenceError::stale);
    expect_error(references, same_library,
        TclReferenceKind::catalog_definition, TclReferenceError::stale);
    expect_live(references, other_library,
        TclReferenceKind::catalog_definition, "unit:vendor-definition");
    expect_live(references, loaded, TclReferenceKind::loaded_object,
        "object:17");

    const auto replacement = references.create_catalog_reference(
        "work", "unit:old-definition");
    check(replacement != affected,
        "catalog replacement reused the previous opaque token");
    expect_live(references, replacement,
        TclReferenceKind::catalog_definition, "unit:old-definition");
}

void catalog_failures_preserve_references()
{
    TclReferenceTable references;
    const auto catalog = references.create_catalog_reference(
        "work", "unit:definition");
    const auto loaded = references.create_loaded_reference("object:3");
    references.catalog_mutation_completed(
        "work", TclCatalogMutation::replaced, false);
    expect_live(references, catalog,
        TclReferenceKind::catalog_definition, "unit:definition");
    expect_live(references, loaded, TclReferenceKind::loaded_object,
        "object:3");
    check(references.catalog_generation("work") == 1U,
        "failed catalog mutation advanced its generation");
}

void failed_load_and_restart_preserve_references()
{
    TclReferenceTable references;
    const auto catalog = references.create_catalog_reference(
        "work", "unit:definition");
    const auto loaded = references.create_loaded_reference("object:9");
    const auto initial_session = references.loaded_session_generation();

    references.snapshot_load_completed(false);
    references.debug_restart_completed(false);
    check(references.loaded_session_generation() == initial_session,
        "failed load or restart advanced the loaded session");
    expect_live(references, catalog,
        TclReferenceKind::catalog_definition, "unit:definition");
    expect_live(references, loaded, TclReferenceKind::loaded_object,
        "object:9");
}

void successful_load_and_restart_invalidate_loaded_references()
{
    TclReferenceTable references;
    const auto catalog = references.create_catalog_reference(
        "work", "unit:definition");
    const auto loaded_before_load
        = references.create_loaded_reference("object:9");
    const auto initial_session = references.loaded_session_generation();

    references.snapshot_load_completed(true);
    check(references.loaded_session_generation() == initial_session + 1U,
        "successful snapshot load did not advance the loaded session");
    expect_error(references, loaded_before_load,
        TclReferenceKind::loaded_object, TclReferenceError::stale);
    expect_live(references, catalog,
        TclReferenceKind::catalog_definition, "unit:definition");

    const auto loaded_before_restart
        = references.create_loaded_reference("object:9");
    references.debug_restart_completed(true);
    expect_error(references, loaded_before_restart,
        TclReferenceKind::loaded_object, TclReferenceError::stale);
    expect_live(references, catalog,
        TclReferenceKind::catalog_definition, "unit:definition");
}

void workspace_change_invalidates_only_catalog_references()
{
    TclReferenceTable references;
    references.catalog_mutation_completed(
        "work", TclCatalogMutation::replaced, true);
    references.catalog_mutation_completed(
        "vendor", TclCatalogMutation::remapped, true);
    const auto initial_work_catalog = references.catalog_generation("work");
    const auto initial_vendor_catalog = references.catalog_generation("vendor");
    const auto initial_fresh_catalog = references.catalog_generation("fresh");
    const auto work = references.create_catalog_reference(
        "work", "unit:definition");
    const auto vendor = references.create_catalog_reference(
        "vendor", "unit:vendor-definition");
    const auto loaded = references.create_loaded_reference("object:9");
    const auto initial_workspace = references.workspace_generation();
    const auto initial_session = references.loaded_session_generation();

    references.workspace_changed();

    check(references.workspace_generation() == initial_workspace + 1U,
        "workspace change did not advance its generation");
    check(references.catalog_generation("work") > initial_work_catalog,
        "workspace change did not advance the work catalog generation");
    check(references.catalog_generation("vendor") > initial_vendor_catalog,
        "workspace change did not advance the vendor catalog generation");
    check(references.catalog_generation("fresh") > initial_fresh_catalog,
        "workspace change did not advance an unseen catalog generation");
    check(references.loaded_session_generation() == initial_session,
        "workspace change advanced the loaded session");
    expect_error(references, work,
        TclReferenceKind::catalog_definition, TclReferenceError::stale);
    expect_error(references, vendor,
        TclReferenceKind::catalog_definition, TclReferenceError::stale);
    expect_live(references, loaded, TclReferenceKind::loaded_object,
        "object:9");

    const auto replacement = references.create_catalog_reference(
        "work", "unit:definition");
    check(replacement != work,
        "workspace change reused a catalog reference token");
    expect_live(references, replacement,
        TclReferenceKind::catalog_definition, "unit:definition");
}

void runtime_destruction_invalidates_without_aliasing_reused_identity()
{
    TclReferenceTable references;
    const auto old = references.create_loaded_reference("object:21");
    references.runtime_object_destroyed("object:21");
    expect_error(references, old, TclReferenceKind::loaded_object,
        TclReferenceError::stale);

    const auto replacement = references.create_loaded_reference("object:21");
    check(replacement != old,
        "runtime object recreation reused its prior reference token");
    expect_live(references, replacement, TclReferenceKind::loaded_object,
        "object:21");
}

void weak_runtime_lifetimes_are_validated_and_guarded()
{
    TclReferenceTable references;
    auto owner = std::make_shared<int>(42);
    std::weak_ptr<const void> lifetime { owner };
    const auto token
        = references.create_loaded_reference("object:weak", lifetime);

    {
        auto held = references.resolve(token, TclReferenceKind::loaded_object);
        check(held.error == TclReferenceError::none,
            "live weak-lifetime reference was rejected");
        check(static_cast<bool>(held.lifetime_guard),
            "resolution did not retain a runtime lifetime guard");
        owner.reset();
        expect_live(references, token, TclReferenceKind::loaded_object,
            "object:weak");
    }

    expect_error(references, token, TclReferenceKind::loaded_object,
        TclReferenceError::stale);
}

void wrong_kind_unknown_and_other_table_tokens_are_rejected()
{
    TclReferenceTable first;
    const auto catalog
        = first.create_catalog_reference("work", "unit:definition");
    const auto loaded = first.create_loaded_reference("object:5");
    expect_error(first, catalog, TclReferenceKind::loaded_object,
        TclReferenceError::wrong_kind);
    expect_error(first, loaded, TclReferenceKind::catalog_definition,
        TclReferenceError::wrong_kind);
    expect_error(first, "@fsim/ref/unknown", TclReferenceKind::loaded_object,
        TclReferenceError::invalid);

    TclReferenceTable second;
    const auto second_catalog
        = second.create_catalog_reference("work", "unit:definition");
    const auto second_loaded = second.create_loaded_reference("object:5");
    check(second_catalog != catalog,
        "separate tables issued an aliased catalog token");
    check(second_loaded != loaded,
        "separate tables issued an aliased loaded-object token");
    expect_error(second, catalog, TclReferenceKind::catalog_definition,
        TclReferenceError::invalid);
}

} // namespace

int main()
{
    successful_catalog_change_invalidates_only_its_library(
        TclCatalogMutation::replaced);
    successful_catalog_change_invalidates_only_its_library(
        TclCatalogMutation::object_deleted);
    successful_catalog_change_invalidates_only_its_library(
        TclCatalogMutation::library_deleted);
    successful_catalog_change_invalidates_only_its_library(
        TclCatalogMutation::remapped);
    catalog_failures_preserve_references();
    failed_load_and_restart_preserve_references();
    successful_load_and_restart_invalidate_loaded_references();
    workspace_change_invalidates_only_catalog_references();
    runtime_destruction_invalidates_without_aliasing_reused_identity();
    weak_runtime_lifetimes_are_validated_and_guarded();
    wrong_kind_unknown_and_other_table_tokens_are_rejected();
    return EXIT_SUCCESS;
}

#else

int main()
{
    return 0;
}

#endif // defined(FSIM_HAS_TCL)
