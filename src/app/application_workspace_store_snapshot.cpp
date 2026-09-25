// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_internal.hpp"

#include "fsim/support/path.hpp"

namespace fsim::app::workspace {
namespace {

constexpr std::string_view kSnapshotHeader = "FSIM-SNAPSHOT 1\n";

bool snapshot_revision(const std::string_view name)
{
    if (name.size() != 44U || name.front() != 'r' || !name.ends_with(".fsimdesign"))
        return false;
    for (const char character : name.substr(1U, 32U)) {
        if (!(character >= '0' && character <= '9')
            && !(character >= 'a' && character <= 'f'))
            return false;
    }
    return true;
}

std::optional<std::filesystem::path> snapshot_directory(
    const Store& store, const std::string_view name, const bool create,
    std::string& error)
{
    if (!valid_name(name)) {
        error = "invalid snapshot name: " + std::string { name };
        return std::nullopt;
    }
    const auto directory = store.managed_directory() / "snapshots" / name;
    if (create
        && (!detail::ensure_directory(store.managed_directory(), error)
            || !detail::ensure_directory(store.managed_directory() / "snapshots", error)
            || !detail::ensure_directory(directory, error)
            || !detail::ensure_directory(directory / "revisions", error)))
        return std::nullopt;
    if (!detail::safe_descendant(store.root(),
            std::filesystem::path { ".fsim/snapshots" } / name, error))
        return std::nullopt;
    return directory;
}

} // namespace

struct SnapshotTransaction::Impl {
    std::filesystem::path directory;
    std::filesystem::path artifact;
    std::unique_ptr<compiler::detail::CacheDirectoryLock> lock;
    bool committed { false };
};

SnapshotTransaction::SnapshotTransaction(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

SnapshotTransaction::~SnapshotTransaction()
{
    if (!impl_->committed)
        detail::remove_revision(impl_->artifact);
}

const std::filesystem::path& SnapshotTransaction::artifact_path() const
{
    return impl_->artifact;
}

bool SnapshotTransaction::commit(std::string& error)
{
    error.clear();
    if (impl_->committed) {
        error = "snapshot transaction has already committed";
        return false;
    }
    const auto relative = std::filesystem::path { "revisions" } / impl_->artifact.filename();
    std::error_code code;
    if (!detail::safe_descendant(impl_->directory, relative, error)
        || !std::filesystem::is_directory(impl_->artifact, code) || code) {
        if (error.empty())
            error = "snapshot artifact was not published before commit";
        return false;
    }
    const auto revision = support::path_to_utf8(impl_->artifact.filename());
    if (!detail::safe_descendant(impl_->directory, "snapshot.index", error)
        || !detail::replace_text(impl_->directory / "snapshot.index",
            std::string { kSnapshotHeader } + revision + '\n', error))
        return false;
    impl_->committed = true;
    detail::collect_revisions(impl_->directory / "revisions", { revision });
    return true;
}

std::unique_ptr<SnapshotTransaction> Store::begin_snapshot(
    const std::string_view name, std::string& error) const
{
    error.clear();
    const auto directory = snapshot_directory(*this, name, true, error);
    if (!directory)
        return nullptr;
    auto impl = std::make_unique<SnapshotTransaction::Impl>();
    impl->directory = *directory;
    impl->lock = detail::lock_directory(*directory / ".writer-lock", error);
    if (!impl->lock)
        return nullptr;
    impl->artifact = *directory / "revisions" / (detail::new_revision() + ".fsimdesign");
    std::error_code code;
    if (std::filesystem::exists(impl->artifact, code) || code) {
        error = "generated snapshot artifact path is unavailable";
        return nullptr;
    }
    return std::unique_ptr<SnapshotTransaction> { new SnapshotTransaction { std::move(impl) } };
}

std::optional<SnapshotRecord> Store::read_snapshot(
    const std::string_view name, std::string& error) const
{
    error.clear();
    const auto directory = snapshot_directory(*this, name, false, error);
    if (!directory)
        return std::nullopt;
    if (!detail::safe_descendant(*directory, "snapshot.index", error))
        return std::nullopt;
    const auto text = detail::read_text(*directory / "snapshot.index", 256U, error);
    if (!text)
        return std::nullopt;
    if (!text->starts_with(kSnapshotHeader) || text->back() != '\n') {
        error = "snapshot index has an unsupported format";
        return std::nullopt;
    }
    const auto revision = std::string_view { *text }.substr(
        kSnapshotHeader.size(), text->size() - kSnapshotHeader.size() - 1U);
    const auto relative = std::filesystem::path { "revisions" }
        / support::path_from_utf8(revision);
    if (!snapshot_revision(revision) || !detail::safe_descendant(*directory, relative, error)) {
        if (error.empty())
            error = "snapshot index contains an unsafe artifact path";
        return std::nullopt;
    }
    std::error_code code;
    if (!std::filesystem::is_directory(*directory / relative, code) || code) {
        error = "snapshot artifact does not exist";
        return std::nullopt;
    }
    return SnapshotRecord { std::string { name }, *directory / relative };
}

} // namespace fsim::app::workspace
