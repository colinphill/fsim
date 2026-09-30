// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/path.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>

/// Filesystem operations that accept paths longer than Windows' MAX_PATH.
///
/// Each function forwards to std::filesystem with the extended-length
/// spelling from path_for_native_io. On other hosts they forward unchanged.
/// Errors and returned paths use the caller's ordinary spelling. Directory
/// iterators yield extended-length entries: pass them back to these functions
/// for I/O, and use path_from_native_io for display, serialization, or
/// comparison with ordinary paths. cmake/CheckNativePathIo.cmake keeps
/// product sources on these seams.
namespace fsim::support::native_fs {

namespace detail {

template <typename Operation>
decltype(auto) with_ordinary_error(
    const char* name, const std::filesystem::path& first,
    Operation&& operation) {
  try {
    return std::forward<Operation>(operation)();
  } catch (const std::filesystem::filesystem_error& error) {
    throw std::filesystem::filesystem_error(
        std::string{"in "} + name, first, error.code());
  }
}

template <typename Operation>
decltype(auto) with_ordinary_error(
    const char* name, const std::filesystem::path& first,
    const std::filesystem::path& second, Operation&& operation) {
  try {
    return std::forward<Operation>(operation)();
  } catch (const std::filesystem::filesystem_error& error) {
    throw std::filesystem::filesystem_error(
        std::string{"in "} + name, first, second, error.code());
  }
}

}  // namespace detail

#define FSIM_NATIVE_FS_UNARY(attributes, name, result)                      \
  attributes inline result name(const std::filesystem::path& path) {        \
    return detail::with_ordinary_error(#name, path, [&] {                   \
      return std::filesystem::name(path_for_native_io(path));               \
    });                                                                     \
  }                                                                         \
  attributes inline result name(                                           \
      const std::filesystem::path& path, std::error_code& error) {          \
    return std::filesystem::name(path_for_native_io(path), error);          \
  }

FSIM_NATIVE_FS_UNARY([[nodiscard]], exists, bool)
FSIM_NATIVE_FS_UNARY([[nodiscard]], is_directory, bool)
FSIM_NATIVE_FS_UNARY([[nodiscard]], is_regular_file, bool)
FSIM_NATIVE_FS_UNARY([[nodiscard]], is_symlink, bool)
FSIM_NATIVE_FS_UNARY([[nodiscard]], is_empty, bool)
FSIM_NATIVE_FS_UNARY([[nodiscard]], status, std::filesystem::file_status)
FSIM_NATIVE_FS_UNARY(
    [[nodiscard]], symlink_status, std::filesystem::file_status)
FSIM_NATIVE_FS_UNARY([[nodiscard]], file_size, std::uintmax_t)
FSIM_NATIVE_FS_UNARY(
    [[nodiscard]], last_write_time, std::filesystem::file_time_type)
// Like std::filesystem, these report success but callers may ignore it.
FSIM_NATIVE_FS_UNARY(, create_directory, bool)
FSIM_NATIVE_FS_UNARY(, create_directories, bool)
FSIM_NATIVE_FS_UNARY(, remove, bool)
FSIM_NATIVE_FS_UNARY(, remove_all, std::uintmax_t)

#undef FSIM_NATIVE_FS_UNARY

inline void last_write_time(
    const std::filesystem::path& path,
    const std::filesystem::file_time_type time) {
  detail::with_ordinary_error("last_write_time", path, [&] {
    std::filesystem::last_write_time(path_for_native_io(path), time);
  });
}
inline void last_write_time(
    const std::filesystem::path& path,
    const std::filesystem::file_time_type time, std::error_code& error) {
  std::filesystem::last_write_time(path_for_native_io(path), time, error);
}

// Status predicates do no I/O; these keep one spelling at every call site.
[[nodiscard]] inline bool exists(
    const std::filesystem::file_status status) noexcept {
  return std::filesystem::exists(status);
}
[[nodiscard]] inline bool is_directory(
    const std::filesystem::file_status status) noexcept {
  return std::filesystem::is_directory(status);
}
[[nodiscard]] inline bool is_regular_file(
    const std::filesystem::file_status status) noexcept {
  return std::filesystem::is_regular_file(status);
}
[[nodiscard]] inline bool is_symlink(
    const std::filesystem::file_status status) noexcept {
  return std::filesystem::is_symlink(status);
}

inline void rename(
    const std::filesystem::path& from, const std::filesystem::path& to) {
  detail::with_ordinary_error("rename", from, to, [&] {
    std::filesystem::rename(path_for_native_io(from), path_for_native_io(to));
  });
}
inline void rename(
    const std::filesystem::path& from, const std::filesystem::path& to,
    std::error_code& error) {
  std::filesystem::rename(
      path_for_native_io(from), path_for_native_io(to), error);
}

[[nodiscard]] inline bool equivalent(
    const std::filesystem::path& first, const std::filesystem::path& second) {
  return detail::with_ordinary_error("equivalent", first, second, [&] {
    return std::filesystem::equivalent(
        path_for_native_io(first), path_for_native_io(second));
  });
}
[[nodiscard]] inline bool equivalent(
    const std::filesystem::path& first, const std::filesystem::path& second,
    std::error_code& error) {
  return std::filesystem::equivalent(
      path_for_native_io(first), path_for_native_io(second), error);
}

inline void permissions(
    const std::filesystem::path& path, const std::filesystem::perms perms,
    const std::filesystem::perm_options options =
        std::filesystem::perm_options::replace) {
  detail::with_ordinary_error("permissions", path, [&] {
    std::filesystem::permissions(path_for_native_io(path), perms, options);
  });
}
inline void permissions(
    const std::filesystem::path& path, const std::filesystem::perms perms,
    std::error_code& error) {
  std::filesystem::permissions(path_for_native_io(path), perms, error);
}
inline void permissions(
    const std::filesystem::path& path, const std::filesystem::perms perms,
    const std::filesystem::perm_options options, std::error_code& error) {
  std::filesystem::permissions(
      path_for_native_io(path), perms, options, error);
}

[[nodiscard]] inline std::filesystem::path canonical(
    const std::filesystem::path& path) {
  return path_from_native_io(
      detail::with_ordinary_error("canonical", path, [&] {
        return std::filesystem::canonical(path_for_native_io(path));
      }));
}
[[nodiscard]] inline std::filesystem::path canonical(
    const std::filesystem::path& path, std::error_code& error) {
  return path_from_native_io(
      std::filesystem::canonical(path_for_native_io(path), error));
}

[[nodiscard]] inline std::filesystem::path weakly_canonical(
    const std::filesystem::path& path) {
  return path_from_native_io(
      detail::with_ordinary_error("weakly_canonical", path, [&] {
        return std::filesystem::weakly_canonical(path_for_native_io(path));
      }));
}
[[nodiscard]] inline std::filesystem::path weakly_canonical(
    const std::filesystem::path& path, std::error_code& error) {
  return path_from_native_io(
      std::filesystem::weakly_canonical(path_for_native_io(path), error));
}

[[nodiscard]] inline std::filesystem::directory_iterator directory_iterator(
    const std::filesystem::path& path) {
  return detail::with_ordinary_error("directory_iterator", path, [&] {
    return std::filesystem::directory_iterator(path_for_native_io(path));
  });
}
[[nodiscard]] inline std::filesystem::directory_iterator directory_iterator(
    const std::filesystem::path& path, std::error_code& error) {
  return std::filesystem::directory_iterator(path_for_native_io(path), error);
}
[[nodiscard]] inline std::filesystem::directory_iterator directory_iterator(
    const std::filesystem::path& path,
    const std::filesystem::directory_options options, std::error_code& error) {
  return std::filesystem::directory_iterator(
      path_for_native_io(path), options, error);
}

[[nodiscard]] inline std::filesystem::recursive_directory_iterator
recursive_directory_iterator(const std::filesystem::path& path) {
  return detail::with_ordinary_error(
      "recursive_directory_iterator", path, [&] {
        return std::filesystem::recursive_directory_iterator(
            path_for_native_io(path));
      });
}
[[nodiscard]] inline std::filesystem::recursive_directory_iterator
recursive_directory_iterator(
    const std::filesystem::path& path, std::error_code& error) {
  return std::filesystem::recursive_directory_iterator(
      path_for_native_io(path), error);
}
[[nodiscard]] inline std::filesystem::recursive_directory_iterator
recursive_directory_iterator(
    const std::filesystem::path& path,
    const std::filesystem::directory_options options, std::error_code& error) {
  return std::filesystem::recursive_directory_iterator(
      path_for_native_io(path), options, error);
}

/// Spell an iterator entry as std::filesystem would for an iteration over
/// root: root joined with the entry's path below the extended-length root.
[[nodiscard]] inline std::filesystem::path ordinary_entry_path(
    const std::filesystem::path& root, const std::filesystem::path& entry) {
#if defined(_WIN32)
  return root / entry.lexically_relative(path_for_native_io(root));
#else
  (void)root;
  return entry;
#endif
}

/// Open file streams; each stream constructor's own mode rules apply.
[[nodiscard]] inline std::ifstream open_ifstream(
    const std::filesystem::path& path,
    const std::ios::openmode mode = std::ios::in) {
  return std::ifstream{path_for_native_io(path), mode};
}
[[nodiscard]] inline std::ofstream open_ofstream(
    const std::filesystem::path& path,
    const std::ios::openmode mode = std::ios::out) {
  return std::ofstream{path_for_native_io(path), mode};
}
[[nodiscard]] inline std::fstream open_fstream(
    const std::filesystem::path& path,
    const std::ios::openmode mode = std::ios::in | std::ios::out) {
  return std::fstream{path_for_native_io(path), mode};
}
template <typename Stream>
void open(
    Stream& stream, const std::filesystem::path& path,
    const std::ios::openmode mode) {
  stream.open(path_for_native_io(path), mode);
}

/// Native UTF-8 spelling for C APIs that open files, such as SQLite.
[[nodiscard]] inline std::string utf8_for_native_io(
    const std::filesystem::path& path) {
  const auto encoded = path_for_native_io(path).u8string();
  return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

}  // namespace fsim::support::native_fs
