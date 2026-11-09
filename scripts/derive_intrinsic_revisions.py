#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Derive scripts/vhdl_intrinsic_revisions.tsv from package texts.

The map gives, for every subprogram overload of a compiler-served package in
third_party/ieee-1076-2019, the first language revision that declares it. It
is derived once from the package texts of the earlier revisions and checked
in; the build only reads it (generate_intrinsic_signatures.py). The earlier
texts are not in the repository: pass the IEEE-P1076/GHDL library trees that
hold them.

  --old DIR   the VHDL-93 packages (GHDL libraries/ieee): std_logic_1164,
              numeric_bit, numeric_std, math_real, math_complex
  --v08 DIR   the VHDL-2008 IEEE packages (GHDL libraries/ieee2008)
  --std08 DIR the VHDL-2008 STD packages (GHDL libraries/std): textio, env

Rules, applied to each overload of the 2019 text:
  * present in the 93 text: "1987", or "1993" when the 93 text declares it
    `pure`/`impure`, or it is an operator or function that needs a 1993
    reserved word (xnor, sll, srl, rol, ror, sla, sra);
  * else present in the 2008 text: "2008";
  * else "2019".
  * STD.TEXTIO: readline, writeline, and read/write of BIT, BIT_VECTOR,
    BOOLEAN, CHARACTER, INTEGER, REAL, STRING and TIME are "1987" (IEEE
    1076-1987 14.3); the rest follows the 2008 text.
  * STD.STANDARD's NOW is "1987".
2002 equals 1993 for these packages and has no column.

Usage: derive_intrinsic_revisions.py --root DIR --old DIR --v08 DIR
       --std08 DIR --output FILE
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_intrinsic_signatures as generator  # noqa: E402

GATED = {"xnor", "sll", "srl", "rol", "ror", "sla", "sra"}
TEXTIO_1987_TYPES = {"bit", "bit_vector", "boolean", "character", "integer",
                     "real", "string", "time"}


def keys(path, package):
    result = {}
    if path is None or not path.exists():
        return result
    for subprogram in generator.parse_package(path, package):
        result[generator.overload_key(package, subprogram)] = subprogram
    return result


def textio_1987(subprogram):
    name = subprogram["name"]
    formals = subprogram["formals"]
    if name in ("readline", "writeline"):
        return True
    if name in ("read", "write") and len(formals) >= 2:
        if name == "write" and [f["type"] for f in formals][:3] == [
                "line", "real", "string"]:
            return False
        return formals[1]["type"] in TEXTIO_1987_TYPES
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--old", required=True, type=Path)
    parser.add_argument("--v08", required=True, type=Path)
    parser.add_argument("--std08", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    rows = []
    for library, package, relative in generator.PACKAGES:
        current = generator.parse_package(arguments.root / relative, package)
        name = Path(relative).name
        if library == "std":
            old = {}
            newer = keys(arguments.std08 / name, package)
        else:
            old = keys(arguments.old / name, package)
            newer = keys(arguments.v08 / name, package)
        for subprogram in current:
            key = generator.overload_key(package, subprogram)
            if package == "standard":
                first = "1987"
            elif package == "textio" and textio_1987(subprogram):
                first = "1987"
            elif key in old:
                declared = old[key]
                first = "1993" if (declared["pure_keyword"]
                                   or subprogram["name"] in GATED) else "1987"
            elif key in newer:
                first = "2008"
            else:
                first = "2019"
            rows.append((package, subprogram["name"],
                         key.split("|", 2)[2], first))
    seen = {}
    for package, name, types, first in rows:
        key = (package, name, types)
        # The earliest revision wins for textually repeated overloads.
        if key not in seen or generator.REVISIONS.index(first) < \
                generator.REVISIONS.index(seen[key]):
            seen[key] = first
    lines = [
        "# SPDX-License-Identifier: Apache-2.0",
        "#",
        "# First language revision declaring each compiler-served subprogram",
        "# overload. Derived by scripts/derive_intrinsic_revisions.py from the",
        "# package texts of 1076-1993 (GHDL libraries/ieee), 1076-2008 (GHDL",
        "# libraries/ieee2008 and std) and 1076-2019 (third_party). Columns:",
        "# package, designator, formal subtypes (UNRESOLVED_ removed), revision.",
    ]
    for (package, name, types), first in sorted(seen.items()):
        lines.append("%s\t%s\t%s\t%s" % (package, name, types, first))
    arguments.output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
