// SPDX-License-Identifier: Apache-2.0
#include "tcl_references.hpp"

#if defined(FSIM_HAS_TCL)

#include <array>
#include <atomic>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fsim::app::tcl_detail {

namespace {

    void append_number(std::string& output, const std::uint64_t value)
    {
        std::array<char, 20> buffer { };
        const auto [end, error] = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), value);
        if (error != std::errc { }) {
            throw std::overflow_error { "Tcl reference token formatting failed" };
        }
        output.append(buffer.data(), end);
    }

    bool has_owner(const std::weak_ptr<const void>& weak) noexcept
    {
        const std::weak_ptr<const void> empty;
        return weak.owner_before(empty) || empty.owner_before(weak);
    }

} // namespace

TclReferenceTable::TclReferenceTable()
    : table_instance_ { next_table_instance() }
{
}

std::uint64_t TclReferenceTable::next_table_instance()
{
    static std::atomic<std::uint64_t> next { 1 };
    auto candidate = next.load(std::memory_order_relaxed);
    for (;;) {
        if (candidate == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error { "Tcl reference table IDs exhausted" };
        }
        if (next.compare_exchange_weak(
                candidate,
                candidate + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return candidate;
        }
    }
}

std::string TclReferenceTable::next_token()
{
    if (next_token_id_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "Tcl reference IDs exhausted" };
    }

    std::string token { "@fsim/ref/" };
    append_number(token, table_instance_);
    token.push_back('/');
    append_number(token, next_token_id_++);
    return token;
}

std::uint64_t TclReferenceTable::catalog_generation_unlocked(
    const std::string_view library) const
{
    const auto found = catalog_generations_.find(std::string { library });
    return found == catalog_generations_.end()
        ? workspace_generation_
        : found->second;
}

std::uint64_t TclReferenceTable::runtime_generation_unlocked(
    const std::string_view identity) const
{
    const auto found = runtime_generations_.find(std::string { identity });
    return found == runtime_generations_.end() ? 1U : found->second;
}

void TclReferenceTable::advance_session_generation_unlocked()
{
    if (loaded_session_generation_
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "Tcl loaded-session generations exhausted" };
    }
    ++loaded_session_generation_;
    active_loaded_tokens_.clear();
}

void TclReferenceTable::advance_workspace_generation_unlocked()
{
    if (workspace_generation_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "Tcl workspace generations exhausted" };
    }
    for (const auto& [library, generation] : catalog_generations_) {
        (void)library;
        if (generation == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error { "Tcl catalog generations exhausted" };
        }
    }
    ++workspace_generation_;
    for (auto& [library, generation] : catalog_generations_) {
        (void)library;
        ++generation;
    }
    active_catalog_tokens_.clear();
}

std::string TclReferenceTable::create_catalog_reference(
    const std::string_view library,
    const std::string_view identity)
{
    std::lock_guard<std::mutex> lock { mutex_ };
    const std::string library_name { library };
    const std::string object_identity { identity };

    auto& identities = active_catalog_tokens_[library_name];
    if (const auto found = identities.find(object_identity);
        found != identities.end()) {
        return found->second;
    }

    const auto token = next_token();
    Entry entry;
    entry.kind = TclReferenceKind::catalog_definition;
    entry.identity = object_identity;
    entry.library = library_name;
    entry.workspace_generation = workspace_generation_;
    entry.catalog_generation = catalog_generation_unlocked(library_name);
    entries_.emplace(token, std::move(entry));
    identities.emplace(object_identity, token);
    return token;
}

std::string TclReferenceTable::create_loaded_reference(
    const std::string_view identity,
    std::weak_ptr<const void> runtime_lifetime)
{
    std::lock_guard<std::mutex> lock { mutex_ };
    const std::string object_identity { identity };
    const bool check_lifetime = has_owner(runtime_lifetime);
    const auto runtime_generation
        = runtime_generation_unlocked(object_identity);

    if (!check_lifetime) {
        if (const auto active = active_loaded_tokens_.find(object_identity);
            active != active_loaded_tokens_.end()) {
            const auto entry = entries_.find(active->second);
            if (entry != entries_.end()
                && entry->second.kind == TclReferenceKind::loaded_object
                && !entry->second.has_runtime_lifetime
                && entry->second.session_generation
                    == loaded_session_generation_
                && entry->second.runtime_generation == runtime_generation) {
                return active->second;
            }
            active_loaded_tokens_.erase(active);
        }
    }

    const auto token = next_token();
    Entry entry;
    entry.kind = TclReferenceKind::loaded_object;
    entry.identity = object_identity;
    entry.session_generation = loaded_session_generation_;
    entry.runtime_generation = runtime_generation;
    entry.runtime_lifetime = std::move(runtime_lifetime);
    entry.has_runtime_lifetime = check_lifetime;
    entries_.emplace(token, std::move(entry));
    if (!check_lifetime) {
        active_loaded_tokens_.insert_or_assign(object_identity, token);
    }
    return token;
}

TclReferenceResolution TclReferenceTable::resolve(
    const std::string_view token,
    const TclReferenceKind expected_kind) const
{
    std::lock_guard<std::mutex> lock { mutex_ };
    const auto found = entries_.find(std::string { token });
    if (found == entries_.end()) {
        return { TclReferenceError::invalid, std::nullopt, { } };
    }

    const auto& entry = found->second;
    if (entry.kind != expected_kind) {
        return { TclReferenceError::wrong_kind, std::nullopt, { } };
    }

    std::shared_ptr<const void> lifetime_guard;
    if (entry.kind == TclReferenceKind::catalog_definition) {
        if (entry.workspace_generation != workspace_generation_
            || entry.catalog_generation
                != catalog_generation_unlocked(entry.library)) {
            return { TclReferenceError::stale, std::nullopt, { } };
        }
    } else {
        if (entry.session_generation != loaded_session_generation_
            || entry.runtime_generation
                != runtime_generation_unlocked(entry.identity)) {
            return { TclReferenceError::stale, std::nullopt, { } };
        }
        if (entry.has_runtime_lifetime) {
            lifetime_guard = entry.runtime_lifetime.lock();
            if (!lifetime_guard) {
                return { TclReferenceError::stale, std::nullopt, { } };
            }
        }
    }

    TclReference reference;
    reference.kind = entry.kind;
    reference.identity = entry.identity;
    reference.library = entry.library;
    return { TclReferenceError::none, std::move(reference),
        std::move(lifetime_guard) };
}

void TclReferenceTable::catalog_mutation_completed(
    const std::string_view library,
    const TclCatalogMutation mutation,
    const bool succeeded)
{
    (void)mutation;
    if (!succeeded) {
        return;
    }

    std::lock_guard<std::mutex> lock { mutex_ };
    const std::string library_name { library };
    auto [generation, inserted]
        = catalog_generations_.try_emplace(
            library_name, workspace_generation_);
    (void)inserted;
    if (generation->second == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "Tcl catalog generations exhausted" };
    }
    ++generation->second;
    active_catalog_tokens_.erase(library_name);
}

void TclReferenceTable::snapshot_load_completed(const bool succeeded)
{
    if (!succeeded) {
        return;
    }
    std::lock_guard<std::mutex> lock { mutex_ };
    advance_session_generation_unlocked();
}

void TclReferenceTable::debug_restart_completed(const bool succeeded)
{
    if (!succeeded) {
        return;
    }
    std::lock_guard<std::mutex> lock { mutex_ };
    advance_session_generation_unlocked();
}

void TclReferenceTable::workspace_changed()
{
    std::lock_guard<std::mutex> lock { mutex_ };
    advance_workspace_generation_unlocked();
}

void TclReferenceTable::runtime_object_destroyed(
    const std::string_view identity)
{
    std::lock_guard<std::mutex> lock { mutex_ };
    const std::string object_identity { identity };
    auto [generation, inserted]
        = runtime_generations_.try_emplace(object_identity, 1U);
    (void)inserted;
    if (generation->second == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "Tcl runtime-object generations exhausted" };
    }
    ++generation->second;
    active_loaded_tokens_.erase(object_identity);
}

std::uint64_t TclReferenceTable::catalog_generation(
    const std::string_view library) const
{
    std::lock_guard<std::mutex> lock { mutex_ };
    return catalog_generation_unlocked(library);
}

std::uint64_t TclReferenceTable::workspace_generation() const
{
    std::lock_guard<std::mutex> lock { mutex_ };
    return workspace_generation_;
}

std::uint64_t TclReferenceTable::loaded_session_generation() const
{
    std::lock_guard<std::mutex> lock { mutex_ };
    return loaded_session_generation_;
}

} // namespace fsim::app::tcl_detail

#endif // defined(FSIM_HAS_TCL)
