#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Create fsim release ZIPs without host or wall-clock metadata."""

from __future__ import annotations

import argparse
import datetime as dt
import os
from pathlib import Path, PurePosixPath
import stat
import zipfile


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--mtime", required=True)
    return parser.parse_args()


def checked_paths(manifest: Path) -> list[str]:
    paths = manifest.read_text(encoding="utf-8").splitlines()
    if not paths:
        raise ValueError("archive manifest is empty")
    if paths != sorted(paths):
        raise ValueError("archive manifest is not bytewise ordered")
    if len(paths) != len(set(paths)):
        raise ValueError("archive manifest contains a duplicate path")
    for spelling in paths:
        path = PurePosixPath(spelling)
        if (
            not spelling
            or path.is_absolute()
            or "\\" in spelling
            or "//" in spelling
            or any(part in {"", ".", ".."} for part in path.parts)
        ):
            raise ValueError(f"unsafe archive path: {spelling}")
    return paths


def zip_timestamp(spelling: str) -> tuple[int, int, int, int, int, int]:
    parsed = dt.datetime.strptime(spelling, "%Y-%m-%dT%H:%M:%SZ")
    if parsed.year < 1980:
        raise ValueError("ZIP timestamps must be 1980 or later")
    return (parsed.year, parsed.month, parsed.day, parsed.hour, parsed.minute, parsed.second)


def archive_mode(path: Path, spelling: str) -> tuple[int, bytes]:
    if path.is_symlink():
        target = os.readlink(path).encode("utf-8")
        return stat.S_IFLNK | 0o777, target
    if not path.is_file():
        raise ValueError(f"archive input is not a regular file: {spelling}")
    parts = PurePosixPath(spelling).parts
    relative_parts = parts[1:]
    executable = (
        bool(relative_parts)
        and relative_parts[0] == "bin"
        or len(relative_parts) >= 2
        and relative_parts[0] == "scripts"
        and PurePosixPath(spelling).suffix in {".py", ".sh"}
    )
    mode = 0o755 if executable else 0o644
    return stat.S_IFREG | mode, path.read_bytes()


def main() -> int:
    args = parse_args()
    paths = checked_paths(args.manifest)
    timestamp = zip_timestamp(args.mtime)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(f".{args.output.name}.tmp")
    temporary.unlink(missing_ok=True)
    try:
        with zipfile.ZipFile(
            temporary,
            mode="w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
            strict_timestamps=True,
        ) as archive:
            archive.comment = b""
            for spelling in paths:
                path = Path(spelling)
                mode, data = archive_mode(path, spelling)
                info = zipfile.ZipInfo(spelling, date_time=timestamp)
                info.create_system = 3
                info.create_version = 20
                info.extract_version = 20
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = mode << 16
                info.flag_bits = 0x800
                info.extra = b""
                info.comment = b""
                archive.writestr(
                    info,
                    data,
                    compress_type=zipfile.ZIP_DEFLATED,
                    compresslevel=9,
                )
        os.replace(temporary, args.output)
    finally:
        temporary.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
