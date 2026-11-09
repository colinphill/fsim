#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the compiler-owned VHDL subprogram signature tables.

The design retains no declarations for the subprograms of the packages the
compiler serves itself (STD.STANDARD, STD.TEXTIO, STD.ENV, IEEE.STD_LOGIC_1164,
IEEE.NUMERIC_BIT, IEEE.NUMERIC_STD, their unsigned companions, IEEE.MATH_REAL,
IEEE.MATH_COMPLEX, IEEE.STD_LOGIC_TEXTIO). The context-typed resolution of
IEEE 1076-2019 12.5 needs their formal and result types, so this script reads
the subprogram declarations of the package declaration files under
third_party/ieee-1076-2019 and writes a C++ source that defines
fsim::semantic::vhdl_intrinsic_signatures() (include/fsim/semantic/
vhdl_intrinsic_signatures.hpp). The build runs it; the output lives in the
build tree.

Usage: generate_intrinsic_signatures.py --root DIR --revisions FILE
       --output FILE
"""

import argparse
import re
import sys
from pathlib import Path

# (library, package, file relative to the root), in analysis order. Generic
# packages (fixed_generic_pkg, float_generic_pkg) are not served here: their
# formal types depend on the instantiation's generics.
PACKAGES = (
    ("std", "standard", "std/standard.vhdl"),
    ("std", "textio", "std/textio.vhdl"),
    ("std", "env", "std/env.vhdl"),
    ("ieee", "std_logic_1164", "ieee/std_logic_1164.vhdl"),
    ("ieee", "std_logic_textio", "ieee/std_logic_textio.vhdl"),
    ("ieee", "numeric_bit", "ieee/numeric_bit.vhdl"),
    ("ieee", "numeric_bit_unsigned", "ieee/numeric_bit_unsigned.vhdl"),
    ("ieee", "numeric_std", "ieee/numeric_std.vhdl"),
    ("ieee", "numeric_std_unsigned", "ieee/numeric_std_unsigned.vhdl"),
    ("ieee", "math_real", "ieee/math_real.vhdl"),
    ("ieee", "math_complex", "ieee/math_complex.vhdl"),
)

# The language revisions a subprogram's first declaration can name. There is
# no 2002 entry: the 2002 packages equal the 1993 ones.
REVISIONS = ("1987", "1993", "2008", "2019")


def strip_comments(text):
    return re.sub(r"--[^\n]*", "", text)


def package_region(text, name):
    """The declarative part of package NAME (not its body)."""
    start = re.search(r"\bpackage\s+" + re.escape(name) + r"\s+is\b", text,
                      re.I)
    if start is None:
        raise ValueError("package %s is not declared" % name)
    rest = text[start.end():]
    end = re.search(r"\bend\s+(package\s+)?(" + re.escape(name) + r"\s*)?;",
                    rest, re.I)
    if end is None:
        raise ValueError("package %s has no end" % name)
    return rest[:end.start()]


def drop_protected(region):
    """Remove protected type declarations: their methods are not package
    subprograms."""
    return re.sub(r"\btype\s+\w+\s+is\s+protected\b.*?\bend\s+protected\b"
                  r"(\s+\w+)?\s*;", " ", region, flags=re.I | re.S)


def statements(region):
    """Split REGION at the semicolons outside parentheses."""
    depth = 0
    start = 0
    for index, char in enumerate(region):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == ";" and depth == 0:
            yield region[start:index]
            start = index + 1


def match_paren(text, open_index):
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unbalanced parentheses")


def squeeze(text):
    return re.sub(r"\s+", " ", text).strip().lower()


def parse_formals(text):
    formals = []
    for item in statements(text + ";"):
        item = item.strip()
        if not item:
            continue
        found = re.match(
            r"(?P<class>constant|signal|variable|file)?\s*"
            r"(?P<names>[\w\s,]+?)\s*:\s*"
            r"(?P<mode>(?:inout|in|out|buffer|linkage)\b)?\s*"
            r"(?P<type>.*?)\s*(:=\s*(?P<default>.*))?$",
            item, re.I | re.S)
        if found is None:
            raise ValueError("unparsed formal: %r" % item)
        mode = (found.group("mode") or "in").lower()
        klass = (found.group("class") or
                 ("constant" if mode == "in" else "variable")).lower()
        for name in found.group("names").split(","):
            formals.append({
                "name": squeeze(name),
                "type": squeeze(found.group("type")),
                "mode": mode,
                "class": klass,
                "default": found.group("default") is not None,
            })
    return formals


def parse_subprogram(statement):
    text = statement.strip()
    head = re.match(r"(?P<purity>pure\s+|impure\s+)?"
                    r"(?P<kind>function|procedure)\s+"
                    r"(?P<name>\"[^\"]+\"|\w+)\s*", text, re.I)
    if head is None:
        return None
    rest = text[head.end():]
    # Generic subprograms and instantiations are not served.
    if re.match(r"(generic\b|is\b)", rest, re.I):
        return None
    if re.search(r"\bis\s+new\b", rest, re.I):
        return None
    formals = []
    if rest.startswith("(") or re.match(r"parameter\s*\(", rest, re.I):
        open_index = rest.index("(")
        close = match_paren(rest, open_index)
        formals = parse_formals(rest[open_index + 1:close])
        rest = rest[close + 1:]
    result = ""
    if head.group("kind").lower() == "function":
        returned = re.match(r"\s*return\s+(?P<type>.*)$", rest, re.I | re.S)
        if returned is None:
            return None
        result = squeeze(returned.group("type"))
    name = head.group("name").strip('"').lower()
    return {
        "name": name,
        "procedure": head.group("kind").lower() == "procedure",
        "impure": (head.group("purity") or "").strip().lower() == "impure",
        "pure_keyword": bool((head.group("purity") or "").strip()),
        "formals": formals,
        "result": result,
    }


def overload_key(package, subprogram):
    """The key of an overload in the revision map: package, designator and
    the formal subtypes (with the 2008 UNRESOLVED_ prefix of NUMERIC_STD
    and NUMERIC_BIT's UNSIGNED and SIGNED removed)."""
    types = ",".join(re.sub(r"\bunresolved_", "", formal["type"])
                     for formal in subprogram["formals"])
    return "%s|%s|%s" % (package, subprogram["name"], types)


def load_revisions(path):
    """scripts/vhdl_intrinsic_revisions.tsv: package, designator, formal
    types, first revision; '#' lines are comments."""
    revisions = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        package, name, types, first = line.split("\t")
        if first not in REVISIONS:
            raise ValueError("unknown revision %r in %s" % (first, path))
        key = "%s|%s|%s" % (package, name, types)
        if key in revisions and revisions[key] != first:
            raise ValueError("conflicting revisions for %s" % key)
        revisions[key] = first
    return revisions


def cxx_string(text):
    out = []
    for char in text:
        if char in '"\\':
            out.append("\\" + char)
        elif char == "?":
            out.append("\\?")
        else:
            out.append(char)
    return '"' + "".join(out) + '"'


def parse_package(path, package):
    """The subprogram declarations of PACKAGE in the file PATH."""
    text = strip_comments(path.read_bytes().decode("latin-1"))
    region = drop_protected(package_region(text, package))
    entries = []
    for statement in statements(region):
        subprogram = parse_subprogram(statement)
        if subprogram is not None:
            entries.append(subprogram)
    return entries


def collect(root, revisions):
    entries = []
    for library, package, relative in PACKAGES:
        for subprogram in parse_package(root / relative, package):
            subprogram["library"] = library
            subprogram["package"] = package
            key = overload_key(package, subprogram)
            if key not in revisions:
                raise ValueError("no revision for %s (extend "
                                 "scripts/vhdl_intrinsic_revisions.tsv)" % key)
            subprogram["since"] = revisions[key]
            entries.append(subprogram)
    return entries


def render(entries):
    lines = [
        "// Generated by scripts/generate_intrinsic_signatures.py from",
        "// third_party/ieee-1076-2019; do not edit.",
        '#include "fsim/semantic/vhdl_intrinsic_signatures.hpp"',
        "",
        "namespace fsim::semantic {",
        "namespace {",
        "",
        "constexpr VhdlIntrinsicFormal formals[] = {",
    ]
    formal_index = []
    count = 0
    for entry in entries:
        formal_index.append(count)
        for formal in entry["formals"]:
            lines.append("    { %s, %s, VhdlIntrinsicMode::%s, "
                         "VhdlIntrinsicClass::%s, %s }," % (
                             cxx_string(formal["name"]),
                             cxx_string(formal["type"]),
                             formal["mode"], formal["class"],
                             "true" if formal["default"] else "false"))
            count += 1
    if count == 0:
        lines.append("    { \"\", \"\", VhdlIntrinsicMode::in, "
                     "VhdlIntrinsicClass::constant, false },")
    lines += ["};", "", "constexpr VhdlIntrinsicSignature signatures[] = {"]
    for entry, first in zip(entries, formal_index):
        lines.append("    { %s, %s, %s, %s, %d, %d, %s, %s, %s }," % (
            cxx_string(entry["library"]), cxx_string(entry["package"]),
            cxx_string(entry["name"]), cxx_string(entry["result"]), first,
            len(entry["formals"]),
            "true" if entry["procedure"] else "false",
            "true" if entry["impure"] else "false",
            cxx_string(entry["since"])))
    lines += [
        "};",
        "",
        "} // namespace",
        "",
        "std::span<const VhdlIntrinsicSignature> "
        "vhdl_intrinsic_signatures() noexcept",
        "{",
        "    return signatures;",
        "}",
        "",
        "std::span<const VhdlIntrinsicFormal> vhdl_intrinsic_formals(",
        "    const VhdlIntrinsicSignature& signature) noexcept",
        "{",
        "    return std::span<const VhdlIntrinsicFormal> { formals }.subspan(",
        "        signature.first_formal, signature.formal_count);",
        "}",
        "",
        "} // namespace fsim::semantic",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--revisions", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    entries = collect(arguments.root, load_revisions(arguments.revisions))
    if not entries:
        sys.exit("no subprogram declarations were found")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(render(entries), encoding="utf-8")


if __name__ == "__main__":
    main()
