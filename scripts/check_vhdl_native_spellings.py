#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Ratchet for spelling-based identification of the 8 native VHDL packages.

STD.TEXTIO, STD.ENV, IEEE.STD_LOGIC_1164, NUMERIC_BIT, NUMERIC_STD,
NUMERIC_BIT_UNSIGNED, NUMERIC_STD_UNSIGNED and MATH_REAL are real precompiled
declarations with native identities (include/fsim/semantic/vhdl_native_identity.hpp).
Code under src/elaboration, src/semantic and src/app must identify their types,
functions and packages through that identity, not by spelling.

The script counts banned spellings per file (comments stripped) and compares
the counts with the CEILINGS table.  A file over its ceiling, or a file with no
ceiling that has any hit, fails the check (exit 1).  Files under their ceiling
pass (lower the ceiling with --update).  `--update` prints the current table in
a form that can be pasted over CEILINGS; `--sites` lists every hit.

Usage: check_vhdl_native_spellings.py [--root DIR] [--update] [--sites]
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from pathlib import Path

SCAN_DIRS = ("src/elaboration", "src/semantic", "src/app")
SCAN_SUFFIXES = (".cpp", ".hpp")

# Artifact assembly selects units by unit name (13.1); Tcl/VPI UI text and the
# SystemVerilog hierarchy files only share spellings such as "signed"/"width".
EXEMPT_GLOBS = (
    "src/app/application_standard_library*.cpp",
    "src/app/tcl*.cpp",
    "src/app/application_trace_api.cpp",
    "src/app/application_time.cpp",
    "src/app/application_vpi_*.cpp",
    "src/elaboration/hierarchy_sv_*.cpp",
    "src/app/application_debugger.cpp",
    "src/app/application_sdf_condition_timing.cpp",
    # IEEE fixed_pkg/float_pkg stay projected stubs (their to_integer/resize/sqrt are not the 8 packages).
    "src/elaboration/lowerer_hir_vhdl_fixed.cpp",
    "src/elaboration/lowerer_hir_vhdl_float.cpp",
)

# Type names of the 8 packages (plus the VITAL-style subtype spellings X01...).
TYPE_NAMES = (
    "std_ulogic", "std_logic", "std_ulogic_vector", "std_logic_vector",
    "unsigned", "signed", "unresolved_unsigned", "unresolved_signed",
    "u_unsigned", "u_signed", "x01", "x01z", "ux01", "ux01z",
)
# Package names (library names "ieee"/"std" alone are reserved identities, not banned).
PACKAGE_NAMES = (
    "std_logic_1164", "numeric_std", "numeric_bit", "textio", "env",
    "math_real", "numeric_std_unsigned", "numeric_bit_unsigned",
)
# Function/procedure names that are distinctive enough to count anywhere as a literal.
FUNCTION_NAMES = (
    "to_integer", "to_unsigned", "to_signed", "resize", "rising_edge",
    "falling_edge", "to_x01", "to_x01z", "to_ux01", "to_ux01z", "to_bit",
    "to_bitvector", "to_stdulogic", "to_stdlogicvector", "to_stdulogicvector",
    "is_x", "std_match", "resolved", "readline", "writeline", "hread", "hwrite",
    "oread", "owrite", "sread", "swrite", "tee", "endline", "shift_left",
    "shift_right", "rotate_left", "rotate_right", "to_hstring", "to_ostring",
    "ceil", "floor", "sqrt", "cbrt", "exp", "log2", "log10", "sin", "cos", "tan",
    "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "arcsinh", "arccosh",
    "arctanh", "realmax", "realmin", "math_pi", "math_e", "dir_separator",
    "vhdl_getenv", "getenv", "uniform", "call_path", "call_path_element",
    "call_path_vector", "current_call_path", "get_call_path", "time_record",
)
# Generic words: counted only as the operand of a comparison / name helper.
GENERIC_NAMES = (
    "line", "text", "side", "width", "read", "write", "flush", "input",
    "output", "stop", "finish", "log", "round", "trunc", "sign",
)

CALL_NAMES = (
    "vhdl_builtin_package_member_imported",
    "vhdl_denotes_provided_package_member",
    "builtin_standard_package_exports",
)
# Package predicates; a hit whose argument window names a projected stub
# package or STD.STANDARD is a keeper and is not counted.
PREDICATE_NAMES = ("vhdl_scope_in_package", "is_package")
KEEPER_WORDS = (
    '"standard"', "fixed_pkg", "float_pkg", "fixed_generic_pkg", "float_generic_pkg",
    "fixed_float_types", "vital", "std_logic_arith", "std_logic_signed",
    "std_logic_unsigned", "std_logic_misc", "std_logic_textio",
)

LITERAL = r'"(?:{})"'
RE_LITERAL = re.compile(
    LITERAL.format("|".join(TYPE_NAMES + PACKAGE_NAMES + FUNCTION_NAMES)), re.IGNORECASE)
_g = "|".join(GENERIC_NAMES)
RE_GENERIC = re.compile(
    r'(?:(?:==|!=)\s*"(?:{g})"|"(?:{g})"\s*(?:==|!=)'
    r'|(?:equal_ignoring_ascii_case|same_hir_identifier|compiled_vhdl_name_equal)'
    r'\s*\([^;]*?"(?:{g})")'.format(g=_g),
    re.IGNORECASE)
RE_CALL = re.compile(r"\b(?:{})\b".format("|".join(CALL_NAMES)))
RE_PRED = re.compile(r"\b(?:{})\s*\(".format("|".join(PREDICATE_NAMES)))

# Today's counts (generated with --update).
CEILINGS: dict[str, int] = {
    "src/app/application_analysis.cpp": 1,
    "src/app/application_vhdl_analysis.cpp": 1,
    "src/app/application_vhdl_hir_projection.cpp": 2,
    "src/app/application_workspace_compile.cpp": 4,
    "src/elaboration/hierarchy_packages.cpp": 33,
    "src/elaboration/hierarchy_resolution.cpp": 5,
    "src/elaboration/lowerer_hir_callable.cpp": 46,
    "src/elaboration/lowerer_hir_capability.cpp": 54,
    "src/elaboration/lowerer_hir_expression.cpp": 94,
    "src/elaboration/lowerer_hir_file.cpp": 46,
    "src/elaboration/lowerer_hir_math.cpp": 28,
    "src/elaboration/lowerer_hir_process.cpp": 3,
    "src/elaboration/lowerer_hir_statement.cpp": 53,
    "src/elaboration/lowerer_hir_vhdl_shaped.cpp": 1,
    "src/elaboration/lowerer_hir_vhdl_special.cpp": 4,
    "src/elaboration/lowerer_hir_vhdl_vital.cpp": 2,
    "src/elaboration/lowerer_hir_vhdl_vital_state.cpp": 1,
    "src/elaboration/lowerer_vhdl_vital_names.hpp": 8,
    "src/elaboration/vhdl_hir_type_validation.cpp": 65,
    "src/semantic/compiled_design.cpp": 2,
    "src/semantic/compiled_design_resolver.cpp": 29,
    "src/semantic/compiled_design_specialization.cpp": 101,
    "src/semantic/compiled_design_vhdl_resolution.cpp": 12,
}


def strip_comments(text: str) -> list[str]:
    """Return the file lines with comments blanked (string literals kept)."""
    out: list[str] = []
    in_block = False
    for line in text.split("\n"):
        res = []
        i = 0
        in_str = False
        quote = ""
        while i < len(line):
            ch = line[i]
            two = line[i:i + 2]
            if in_block:
                if two == "*/":
                    in_block = False
                    i += 2
                else:
                    i += 1
                continue
            if in_str:
                res.append(ch)
                if ch == "\\" and i + 1 < len(line):
                    res.append(line[i + 1])
                    i += 2
                    continue
                if ch == quote:
                    in_str = False
                i += 1
                continue
            if two == "//":
                break
            if two == "/*":
                in_block = True
                i += 2
                continue
            if ch in "\"'":
                in_str = True
                quote = ch
            res.append(ch)
            i += 1
        out.append("".join(res))
    return out


def scan_file(path: Path) -> list[tuple[int, str]]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    lines = strip_comments(text)
    hits: list[tuple[int, str]] = []
    for n, line in enumerate(lines, 1):
        window = " ".join(lines[n - 1:n + 3])
        count = len(RE_LITERAL.findall(line))
        # a generic word counts only when the comparison is on this line
        count += len(RE_GENERIC.findall(line))
        count += len(RE_CALL.findall(line))
        for m in RE_PRED.finditer(line):
            tail = window[m.start():]
            end = tail.find(";")
            tail = tail if end < 0 else tail[:end]
            if not any(w in tail.lower() for w in KEEPER_WORDS):
                count += 1
        if count:
            hits.append((n, line.strip()))
            hits.extend([(n, "")] * (count - 1))
    return hits


def candidate_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for rel in SCAN_DIRS:
        base = root / rel
        if not base.is_dir():
            continue
        for p in sorted(base.iterdir()):
            if p.is_file() and p.suffix in SCAN_SUFFIXES:
                relpath = p.relative_to(root).as_posix()
                if any(fnmatch.fnmatch(relpath, g) for g in EXEMPT_GLOBS):
                    continue
                files.append(p)
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument("--update", action="store_true", help="print the current ceiling table")
    ap.add_argument("--sites", action="store_true", help="list every hit")
    args = ap.parse_args()
    root = Path(args.root)

    counts: dict[str, int] = {}
    for path in candidate_files(root):
        hits = scan_file(path)
        rel = path.relative_to(root).as_posix()
        if hits:
            counts[rel] = len(hits)
        if args.sites:
            for n, text in hits:
                if text:
                    print(f"{rel}:{n}: {text[:160]}")

    if args.update:
        print("CEILINGS: dict[str, int] = {")
        for rel in sorted(counts):
            print(f'    "{rel}": {counts[rel]},')
        print("}")
        return 0
    if args.sites:
        return 0

    failures = []
    for rel in sorted(counts):
        ceiling = CEILINGS.get(rel, 0)
        if counts[rel] > ceiling:
            failures.append(f"{rel}: {counts[rel]} spelling sites, ceiling {ceiling}")
    for line in failures:
        print(f"check_vhdl_native_spellings: {line}", file=sys.stderr)
    if failures:
        print("Identify native VHDL declarations through vhdl_native_of()/vhdl_native_type_of(), "
              "not by spelling.", file=sys.stderr)
        return 1
    print(f"check_vhdl_native_spellings: OK ({sum(counts.values())} sites in {len(counts)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
