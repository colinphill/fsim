#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Reject C/C++ test string tokens or adjacent groups unsafe for MSVC."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


@dataclass(frozen=True)
class Literal:
    start: int
    end: int
    source_bytes: int


@dataclass(frozen=True)
class Maximum:
    source_bytes: int
    path: Path
    line: int


def skip_quoted(text: str, start: int, quote: str) -> int:
    cursor = start + 1
    while cursor < len(text):
        if text[cursor] == "\\":
            cursor += 2
        elif text[cursor] == quote:
            return cursor + 1
        else:
            cursor += 1
    return len(text)


def literals(text: str) -> list[Literal]:
    result: list[Literal] = []
    cursor = 0
    while cursor < len(text):
        if text.startswith("//", cursor):
            newline = text.find("\n", cursor + 2)
            cursor = len(text) if newline < 0 else newline + 1
            continue
        if text.startswith("/*", cursor):
            close = text.find("*/", cursor + 2)
            cursor = len(text) if close < 0 else close + 2
            continue
        if text[cursor] == "'":
            cursor = skip_quoted(text, cursor, "'")
            continue

        raw_prefix = next(
            (prefix for prefix in ("u8R\"", "uR\"", "UR\"", "LR\"", "R\"")
             if text.startswith(prefix, cursor)),
            None,
        )
        if raw_prefix is not None:
            delimiter_start = cursor + len(raw_prefix)
            open_paren = text.find(
                "(", delimiter_start, min(len(text), delimiter_start + 17)
            )
            if open_paren >= 0:
                delimiter = text[delimiter_start:open_paren]
                close_marker = ")" + delimiter + "\""
                close = text.find(close_marker, open_paren + 1)
                if close >= 0:
                    end = close + len(close_marker)
                    result.append(
                        Literal(cursor, end, len(text[cursor:end].encode("utf-8")))
                    )
                    cursor = end
                    continue

        ordinary_prefix = next(
            (prefix for prefix in ("u8\"", "u\"", "U\"", "L\"", "\"")
             if text.startswith(prefix, cursor)),
            None,
        )
        if ordinary_prefix is not None:
            end = skip_quoted(text, cursor + len(ordinary_prefix) - 1, "\"")
            result.append(
                Literal(cursor, end, len(text[cursor:end].encode("utf-8")))
            )
            cursor = end
            continue
        cursor += 1
    return result


def is_trivia(text: str) -> bool:
    cursor = 0
    while cursor < len(text):
        if text[cursor].isspace():
            cursor += 1
        elif text.startswith("//", cursor):
            newline = text.find("\n", cursor + 2)
            cursor = len(text) if newline < 0 else newline + 1
        elif text.startswith("/*", cursor):
            close = text.find("*/", cursor + 2)
            if close < 0:
                return False
            cursor = close + 2
        else:
            return False
    return True


def location(path: Path, text: str, literal: Literal) -> Maximum:
    return Maximum(literal.source_bytes, path, text.count("\n", 0, literal.start) + 1)


def audit(root: Path, threshold: int) -> int:
    paths = sorted(
        path for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )
    maximum_token = Maximum(0, root, 1)
    maximum_group = Maximum(0, root, 1)
    violations: list[tuple[str, Maximum]] = []

    for path in paths:
        text = path.read_text(encoding="utf-8")
        tokens = literals(text)
        group: Literal | None = None
        previous: Literal | None = None
        for token in tokens:
            token_location = location(path, text, token)
            if token_location.source_bytes > maximum_token.source_bytes:
                maximum_token = token_location
            if previous is not None and is_trivia(text[previous.end:token.start]):
                assert group is not None
                group = Literal(
                    group.start, token.end, group.source_bytes + token.source_bytes
                )
            else:
                group = token
            group_location = location(path, text, group)
            if group_location.source_bytes > maximum_group.source_bytes:
                maximum_group = group_location
            if token.source_bytes >= threshold:
                violations.append(("token", token_location))
            previous = token
    if maximum_group.source_bytes >= threshold:
        violations.append(("adjacent-group", maximum_group))

    print(
        f"MSVC test string audit: {len(paths)} files, "
        f"conservative threshold {threshold} source bytes"
    )
    print(
        f"maximum token {maximum_token.source_bytes} bytes at "
        f"{maximum_token.path}:{maximum_token.line}"
    )
    print(
        f"maximum adjacent group {maximum_group.source_bytes} bytes at "
        f"{maximum_group.path}:{maximum_group.line}"
    )
    for kind, item in sorted(
        violations, key=lambda entry: entry[1].source_bytes, reverse=True
    ):
        print(f"FAIL {kind} {item.source_bytes} bytes at {item.path}:{item.line}")
    return 1 if violations else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("tests"))
    parser.add_argument("--threshold", type=int, default=16_000)
    arguments = parser.parse_args()
    return audit(arguments.root, arguments.threshold)


if __name__ == "__main__":
    raise SystemExit(main())
