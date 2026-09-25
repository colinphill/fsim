// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(FSIM_HAS_TCL)

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fsim::app::tcl_detail {

enum class TclReferenceKind : std::uint8_t {
    catalog_definition,
    loaded_object,
};

enum class TclReferenceError : std::uint8_t {
    none,
    invalid,
    wrong_kind,
    stale,
};

enum class TclCatalogMutation : std::uint8_t {
    replaced,
    object_deleted,
    library_deleted,
    remapped,
};

struct TclReference {
    TclReferenceKind kind { TclReferenceKind::loaded_object };
    std::string identity;
    std::string library;
};

struct TclReferenceResolution {
    TclReferenceError error { TclReferenceError::invalid };
    std::optional<TclReference> reference;
    std::shared_ptr<const void> lifetime_guard;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == TclReferenceError::none && reference.has_value();
    }
};

/// Issues opaque Tcl-facing handles and checks their owning lifetime.
///
/// The table is scoped to one Tcl runtime. Catalog generations are isolated by
/// library and by workspace; loaded-object generations are isolated from
/// catalog generations. The caller reports the result of workspace/session
/// operations so failed operations leave every prior handle usable.
class TclReferenceTable final {
public:
    TclReferenceTable();
    TclReferenceTable(const TclReferenceTable&) = delete;
    TclReferenceTable& operator=(const TclReferenceTable&) = delete;
    TclReferenceTable(TclReferenceTable&&) = delete;
    TclReferenceTable& operator=(TclReferenceTable&&) = delete;

    [[nodiscard]] std::string create_catalog_reference(
        std::string_view library,
        std::string_view identity);

    [[nodiscard]] std::string create_loaded_reference(
        std::string_view identity,
        std::weak_ptr<const void> runtime_lifetime = { });

    [[nodiscard]] TclReferenceResolution resolve(
        std::string_view token,
        TclReferenceKind expected_kind) const;

    void catalog_mutation_completed(
        std::string_view library,
        TclCatalogMutation mutation,
        bool succeeded);
    void snapshot_load_completed(bool succeeded);
    void debug_restart_completed(bool succeeded);
    void workspace_changed();
    void runtime_object_destroyed(std::string_view identity);

    [[nodiscard]] std::uint64_t workspace_generation() const;
    [[nodiscard]] std::uint64_t catalog_generation(
        std::string_view library) const;
    [[nodiscard]] std::uint64_t loaded_session_generation() const;

private:
    struct Entry {
        TclReferenceKind kind { TclReferenceKind::loaded_object };
        std::string identity;
        std::string library;
        std::uint64_t workspace_generation { };
        std::uint64_t catalog_generation { };
        std::uint64_t session_generation { };
        std::uint64_t runtime_generation { };
        std::weak_ptr<const void> runtime_lifetime;
        bool has_runtime_lifetime { };
    };

    [[nodiscard]] static std::uint64_t next_table_instance();
    [[nodiscard]] std::string next_token();
    void advance_workspace_generation_unlocked();
    [[nodiscard]] std::uint64_t catalog_generation_unlocked(
        std::string_view library) const;
    [[nodiscard]] std::uint64_t runtime_generation_unlocked(
        std::string_view identity) const;
    void advance_session_generation_unlocked();

    mutable std::mutex mutex_;
    std::uint64_t table_instance_ { };
    std::uint64_t next_token_id_ { 1 };
    std::uint64_t workspace_generation_ { 1 };
    std::uint64_t loaded_session_generation_ { 1 };
    std::unordered_map<std::string, std::uint64_t> catalog_generations_;
    std::unordered_map<std::string, std::uint64_t> runtime_generations_;
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
        active_catalog_tokens_;
    std::unordered_map<std::string, std::string> active_loaded_tokens_;
};

} // namespace fsim::app::tcl_detail

#endif // defined(FSIM_HAS_TCL)
