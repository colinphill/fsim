// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_internal.hpp"

#include "fsim/compiler/cache_support.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

namespace fsim::app::workspace {
namespace {

// Each snapshot is one file, `snapshots/<name>.fsimdesign`. An elaboration
// publishes a hidden pending file beside it and commit renames that file
// over the snapshot, so a reader sees the old or the new design, never a
// partial one, and the last commit wins.
std::optional<std::filesystem::path> snapshot_directory(
    const Store& store, const std::string_view name, const bool create,
    std::string& error)
{
    if (!valid_name(name)) {
        error = "invalid snapshot name: " + std::string { name };
        return std::nullopt;
    }
    const auto directory = store.managed_directory() / "snapshots";
    if (create
        && (!detail::ensure_directory(store.managed_directory(), error)
            || !detail::ensure_directory(directory, error)))
        return std::nullopt;
    if (!detail::safe_descendant(store.root(),
            std::filesystem::path { ".fsim/snapshots" } / (std::string { name } + ".fsimdesign"),
            error))
        return std::nullopt;
    std::error_code code;
    if (support::native_fs::is_directory(directory / name, code)) {
        error = "snapshot '" + std::string { name }
            + "' was written by an older fsim; " + detail::legacy_removal_advice(directory / name)
            + ", and elaborate it again";
        return std::nullopt;
    }
    return directory;
}

} // namespace

struct SnapshotTransaction::Impl {
    std::filesystem::path pending;
    std::filesystem::path snapshot;
    bool committed { false };
};

SnapshotTransaction::SnapshotTransaction(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

SnapshotTransaction::~SnapshotTransaction()
{
    if (impl_->committed)
        return;
    std::error_code code;
    support::native_fs::remove(impl_->pending, code);
}

const std::filesystem::path& SnapshotTransaction::artifact_path() const
{
    return impl_->pending;
}

bool SnapshotTransaction::commit(std::string& error)
{
    error.clear();
    if (impl_->committed) {
        error = "snapshot transaction has already committed";
        return false;
    }
    std::error_code code;
    if (!support::native_fs::is_regular_file(impl_->pending, code) || code) {
        error = "snapshot artifact was not published before commit";
        return false;
    }
    if (!compiler::detail::atomic_replace_file(impl_->pending, impl_->snapshot, code)) {
        error = "cannot install snapshot: " + code.message();
        return false;
    }
    impl_->committed = true;
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
    impl->snapshot = *directory / (std::string { name } + ".fsimdesign");
    impl->pending = *directory
        / (".pending-" + detail::new_revision() + "-" + std::string { name } + ".fsimdesign");
    std::error_code code;
    if (support::native_fs::exists(impl->pending, code) || code) {
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
    const auto path = *directory / (std::string { name } + ".fsimdesign");
    std::error_code code;
    if (!support::native_fs::is_regular_file(path, code) || code) {
        error = "snapshot artifact does not exist";
        return std::nullopt;
    }
    return SnapshotRecord { std::string { name }, path };
}

} // namespace fsim::app::workspace
