#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the table of natively implemented VHDL package declarations.

IEEE 1076-2019 16 packages STD.TEXTIO, STD.ENV, IEEE.STD_LOGIC_1164,
IEEE.NUMERIC_BIT, IEEE.NUMERIC_STD, IEEE.NUMERIC_BIT_UNSIGNED,
IEEE.NUMERIC_STD_UNSIGNED and IEEE.MATH_REAL are implemented natively by the
compiler. The standard library artifact of each edition analyzes the package
declarations (real declarations, no bodies); the stamp
(src/semantic/vhdl_native_intrinsic_stamp.cpp) gives every declaration an
intrinsic id from the table written here, by position in the package's
declarative part.

For each edition and each covered package this script enumerates the
declarative items of the package declaration in source order, with the item
parser and item keys of scripts/derive_ieee_packages.py. The items that stay
ordinary VHDL (the subtypes X01, X01Z, UX01 and UX01Z of IEEE.STD_LOGIC_1164
and the constant CopyRightNotice) have no table entry and id 0 in the
package's item list, so the stamp follows the list alone. Equal (library,
package, key) of several editions are one table entry whose `editions` mask
has bit int(VhdlStandard) set per edition. The output defines the functions
of include/fsim/semantic/vhdl_native_intrinsics.hpp other than the stamp.

Trees: 1987, 1993, 2002, 2008 are third_party/ieee-derived/<edition>; 2000
uses the 2002 tree; 2019 is third_party/ieee-1076-2019. A package file a tree
does not hold is skipped.

Every entry also carries generated enumerators (written to --header as
include/fsim/semantic/vhdl_native_names.hpp): its package
(VhdlNativePackage), its kind (VhdlNativeKind) and its designator
(VhdlNativeName, operators spelled out: "+" is op_plus). Compiler code
identifies a native declaration by these, never by its spelling.

Usage: generate_vhdl_native_intrinsics.py --root-2019 DIR --derived DIR
       --output FILE --header FILE
"""

import argparse
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import derive_ieee_packages as derive  # noqa: E402

# (edition name, int(frontend::VhdlStandard), tree: derived subdirectory or
# None for the 2019 originals).
EDITIONS = (
    ("1987", 0, "1987"),
    ("1993", 1, "1993"),
    ("2000", 2, "2002"),
    ("2002", 3, "2002"),
    ("2008", 4, "2008"),
    ("2019", 5, None),
)
# (library, package) in table order.
PACKAGES = (
    ("ieee", "math_real"),
    ("ieee", "numeric_bit"),
    ("ieee", "numeric_bit_unsigned"),
    ("ieee", "numeric_std"),
    ("ieee", "numeric_std_unsigned"),
    ("ieee", "std_logic_1164"),
    ("std", "env"),
    ("std", "textio"),
)
# Items that stay ordinary VHDL (design report D2 section 1).
KEPT_1164 = ("type|x01", "type|x01z", "type|ux01", "type|ux01z")
KEPT_ANY = ("constant|copyrightnotice",)
# Items of the context before the unit and use clauses are not declarations
# of the package.
NOT_DECLARATIONS = ("use", "library", "context")


def package_items(path):
    """Keys of the package declaration items of the file at PATH, in source
    order, unique within the package."""
    text = path.read_bytes().decode("latin-1")
    code = [t for t in derive.lex(text) if t[0] != "c"]
    unit = derive.find_unit(code)
    if unit is None or unit[1]:
        raise ValueError("%s: no package declaration" % path)
    package_line = text.count("\n", 0, code[unit[2]][2])
    keys = []
    seen = {}
    for item in derive.split_items(text):
        if item.last <= package_line:
            continue  # the context clauses before the unit
        if item.kind in NOT_DECLARATIONS:
            continue
        if item.kind == "other":
            raise ValueError("%s: unrecognized item %s" % (path, item.key))
        keys.append(item.key)
    unique = []
    for key in keys:
        seen[key] = seen.get(key, 0) + 1
        unique.append(key if seen[key] == 1 else "%s#%d" % (key, seen[key]))
    return unique


def kept(package, key):
    """Whether the item stays ordinary VHDL (no native id).

    Besides X01..UX01Z and CopyRightNotice: an alias denotes its target
    (IEEE 1076-2019 6.6), which carries the id; the MATH_REAL constants are
    folded from their initializers and the STD.ENV types are the ordinary
    types the package text declares (owner decisions 11-19)."""
    if key in KEPT_ANY or key.startswith("alias|"):
        return True
    if package == ("ieee", "math_real") and key.startswith("constant|"):
        return True
    if package == ("std", "env") and key.startswith(("type|", "subtype|")):
        return True
    return package == ("ieee", "std_logic_1164") and key in KEPT_1164


# Operator designators (IEEE 1076-2019 9.2) as identifiers.
OPERATOR_NAMES = {
    "**": "op_power", "*": "op_times", "+": "op_plus", "-": "op_minus",
    "/=": "op_ne", "/": "op_divide", "<=": "op_le", "<": "op_lt",
    "=": "op_eq", ">=": "op_ge", ">": "op_gt", "?/=": "op_match_ne",
    "?<=": "op_match_le", "?<": "op_match_lt", "?=": "op_match_eq",
    "?>=": "op_match_ge", "?>": "op_match_gt", "??": "op_condition",
    "abs": "op_abs", "and": "op_and", "mod": "op_mod", "nand": "op_nand",
    "nor": "op_nor", "not": "op_not", "or": "op_or", "rem": "op_rem",
    "rol": "op_rol", "ror": "op_ror", "sla": "op_sla", "sll": "op_sll",
    "sra": "op_sra", "srl": "op_srl", "xnor": "op_xnor", "xor": "op_xor",
}
KINDS = ("alias", "constant", "file", "function", "procedure", "type")
# C++ keywords and alternative tokens a designator may spell: the enumerator
# takes a trailing underscore (IEEE.NUMERIC_STD.SIGNED is `signed_`).
CPP_RESERVED = frozenset("""
    alignas alignof and and_eq asm auto bitand bitor bool break case catch
    char char8_t char16_t char32_t class compl concept const consteval
    constexpr constinit const_cast continue co_await co_return co_yield
    decltype default delete do double dynamic_cast else enum explicit export
    extern false float for friend goto if inline int long mutable namespace
    new noexcept not not_eq nullptr operator or or_eq private protected
    public register reinterpret_cast requires return short signed sizeof
    static static_assert static_cast struct switch template this
    thread_local throw true try typedef typeid typename union unsigned using
    virtual void volatile wchar_t while xor xor_eq""".split())


def key_kind(key):
    kind = key.split("|", 1)[0]
    if kind not in KINDS:
        raise ValueError("unknown item kind in key %r" % key)
    return kind


def key_name(key):
    """The enumerator of KEY's designator."""
    rest = key.split("|", 1)[1]
    if rest.startswith('"'):
        close = rest.find('"', 1)
        operator = rest[1:close] if close != -1 else None
        if operator not in OPERATOR_NAMES:
            raise ValueError("unknown operator designator in key %r" % key)
        return OPERATOR_NAMES[operator]
    end = len(rest)
    for stop in "(#=":
        at = rest.find(stop)
        if at != -1:
            end = min(end, at)
    arrow = rest.find("->")
    if arrow != -1:
        end = min(end, arrow)
    designator = rest[:end]
    if designator.startswith('"'):
        if not designator.endswith('"') or designator[1:-1] not in OPERATOR_NAMES:
            raise ValueError("unknown operator designator in key %r" % key)
        return OPERATOR_NAMES[designator[1:-1]]
    if not designator or not all(c.isalnum() or c == "_" for c in designator):
        raise ValueError("designator of key %r is not an identifier" % key)
    return designator + "_" if designator in CPP_RESERVED else designator


def cpp_string(text):
    out = ['"']
    for ch in text:
        if ch in '"\\?':
            out.append("\\" + ch)
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        else:
            raise ValueError("non-ASCII key %r" % text)
    out.append('"')
    return "".join(out)


def build(root_2019, derived):
    entries = {}  # package -> {key: [order, mask]}
    order = {package: [] for package in PACKAGES}
    lists = {}  # (standard, package) -> keys in declaration order
    for _, standard, tree in EDITIONS:
        for package in PACKAGES:
            base = Path(root_2019) if tree is None else Path(derived) / tree
            path = base / package[0] / (package[1] + ".vhdl")
            if not path.is_file():
                continue
            keys = package_items(path)
            lists[(standard, package)] = [
                k if not kept(package, k) else None for k in keys]
            table = entries.setdefault(package, {})
            for key in keys:
                if kept(package, key):
                    continue
                if key not in table:
                    table[key] = 0
                    order[package].append(key)
                table[key] |= 1 << standard
    rows = []
    identity = {}
    for package in PACKAGES:
        for key in order[package]:
            rows.append((package[0], package[1], key, entries[package][key]))
            identity[(package, key)] = len(rows)  # id = index + 1
    return rows, identity, lists


def key_identifier(key):
    """The identifier designator of KEY (lowercase, as the item keys spell
    it); None for an operator designator."""
    rest = key.split("|", 1)[1]
    end = len(rest)
    for stop in "(#=[ ":
        at = rest.find(stop)
        if at != -1:
            end = min(end, at)
    arrow = rest.find("->")
    if arrow != -1:
        end = min(end, arrow)
    designator = rest[:end]
    if not designator or not all(c.isalnum() or c == "_" for c in designator):
        return None
    return designator


def designator_editions(root_2019, derived):
    """(package, identifier) -> mask of the editions whose package declares
    an item (aliases included) with that identifier designator."""
    index = {}
    for _, standard, tree in EDITIONS:
        for package in PACKAGES:
            base = Path(root_2019) if tree is None else Path(derived) / tree
            path = base / package[0] / (package[1] + ".vhdl")
            if not path.is_file():
                continue
            for key in package_items(path):
                name = key_identifier(key)
                if name is not None:
                    index[(package, name)] = (
                        index.get((package, name), 0) | 1 << standard)
    return sorted(index.items())


def item_id(identity, package, key):
    # 0 for an item that stays ordinary VHDL (no table entry).
    return 0 if key is None else identity[(package, key)]


def canonical(rows, lists, identity):
    lines = []
    for library, package, key, mask in rows:
        lines.append("T|%s|%s|%s|%d" % (library, package, key, mask))
    for (standard, package), keys in sorted(lists.items()):
        lines.append("L|%d|%s|%s|%s" % (standard, package[0], package[1],
                                        ",".join(str(item_id(identity, package, k))
                                                 for k in keys)))
    return "\n".join(lines) + "\n"


def emit(rows, lists, identity, digest, editions):
    out = []
    out.append("// SPDX-License-Identifier: Apache-2.0")
    out.append("// Generated by scripts/generate_vhdl_native_intrinsics.py;"
               " do not edit.")
    out.append('#include "fsim/semantic/vhdl_native_intrinsics.hpp"')
    names = sorted({key_name(key) for _, _, key, _ in rows})
    out.append("")
    out.append("#include <array>")
    out.append("")
    out.append("namespace fsim::semantic {")
    out.append("namespace {")
    out.append("")
    out.append("constexpr std::array<VhdlNativeIntrinsic, %d> kTable {{"
               % len(rows))
    for library, package, key, mask in rows:
        out.append("    { %s, %s, %s, %du, VhdlNativePackage::%s_%s,"
                   " VhdlNativeKind::%s, VhdlNativeName::%s }," % (
            cpp_string(library), cpp_string(package), cpp_string(key), mask,
            library, package, key_kind(key), key_name(key)))
    out.append("}};")
    out.append("")
    names = []
    for (standard, package), keys in sorted(lists.items()):
        name = "kItems%d_%s_%s" % (standard, package[0], package[1])
        names.append((standard, package, name, len(keys)))
        if keys:
            out.append("constexpr std::array<std::uint32_t, %d> %s {"
                       % (len(keys), name))
            for start in range(0, len(keys), 12):
                out.append("    " + " ".join(
                    "%du," % item_id(identity, package, k)
                    for k in keys[start:start + 12]))
            out.append("};")
    out.append("")
    out.append("struct PackageItems {")
    out.append("    int standard;")
    out.append("    std::string_view library;")
    out.append("    std::string_view package;")
    out.append("    std::span<const std::uint32_t> items;")
    out.append("};")
    out.append("")
    out.append("constexpr std::array<PackageItems, %d> kPackageItems {{"
               % len(names))
    for standard, package, name, count in names:
        out.append("    { %d, %s, %s, %s }," % (
            standard, cpp_string(package[0]), cpp_string(package[1]),
            name if count else "std::span<const std::uint32_t> { }"))
    out.append("}};")
    out.append("")
    out.append("struct DesignatorEditions {")
    out.append("    VhdlNativePackage package;")
    out.append("    std::string_view designator;")
    out.append("    std::uint32_t editions;")
    out.append("};")
    out.append("")
    out.append("// Sorted by (package, designator).")
    out.append("constexpr std::array<DesignatorEditions, %d> kDesignators {{"
               % len(editions))
    for (package, name), mask in editions:
        out.append("    { VhdlNativePackage::%s_%s, %s, %du }," % (
            package[0], package[1], cpp_string(name), mask))
    out.append("}};")
    out.append("")
    out.append("constexpr std::string_view kDigest = %s;"
               % cpp_string(digest))
    out.append("")
    out.append("} // namespace")
    out.append("")
    out.append("std::span<const VhdlNativeIntrinsic>"
               " vhdl_native_intrinsics() noexcept")
    out.append("{")
    out.append("    return kTable;")
    out.append("}")
    out.append("")
    out.append("std::span<const std::uint32_t> vhdl_native_package_items(")
    out.append("    const frontend::VhdlStandard standard,")
    out.append("    const std::string_view library,")
    out.append("    const std::string_view package) noexcept")
    out.append("{")
    out.append("    for (const auto& entry : kPackageItems) {")
    out.append("        if (entry.standard == static_cast<int>(standard)")
    out.append("            && entry.library == library")
    out.append("            && entry.package == package) {")
    out.append("            return entry.items;")
    out.append("        }")
    out.append("    }")
    out.append("    return { };")
    out.append("}")
    out.append("")
    out.append("const VhdlNativeIntrinsic* vhdl_native_intrinsic("
               "const std::uint32_t id) noexcept")
    out.append("{")
    out.append("    return id == 0U || id > kTable.size() ? nullptr"
               " : &kTable[id - 1U];")
    out.append("}")
    out.append("")
    out.append("std::uint32_t vhdl_native_designator_editions(")
    out.append("    const VhdlNativePackage package,")
    out.append("    const std::string_view designator) noexcept")
    out.append("{")
    out.append("    for (const auto& entry : kDesignators) {")
    out.append("        if (entry.package == package"
               " && entry.designator == designator) {")
    out.append("            return entry.editions;")
    out.append("        }")
    out.append("    }")
    out.append("    return 0U;")
    out.append("}")
    out.append("")
    out.append("std::string_view vhdl_native_intrinsics_digest() noexcept")
    out.append("{")
    out.append("    return kDigest;")
    out.append("}")
    out.append("")
    out.append("} // namespace fsim::semantic")
    return "\n".join(out) + "\n"


def emit_header(rows):
    names = sorted({key_name(key) for _, _, key, _ in rows})
    out = []
    out.append("// SPDX-License-Identifier: Apache-2.0")
    out.append("// Generated by scripts/generate_vhdl_native_intrinsics.py;"
               " do not edit.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace fsim::semantic {")
    out.append("")
    out.append("// The packages with native declarations.")
    out.append("enum class VhdlNativePackage : std::uint8_t {")
    for library, package in PACKAGES:
        out.append("    %s_%s," % (library, package))
    out.append("};")
    out.append("")
    out.append("// The kind of a native declaration (a subtype is a type item).")
    out.append("enum class VhdlNativeKind : std::uint8_t {")
    for kind in KINDS:
        out.append("    %s," % kind)
    out.append("};")
    out.append("")
    out.append("// The designators of the native declarations (operators"
               " spelled out).")
    out.append("enum class VhdlNativeName : std::uint16_t {")
    for name in names:
        out.append("    %s," % name)
    out.append("};")
    out.append("")
    out.append("} // namespace fsim::semantic")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root-2019", required=True)
    parser.add_argument("--derived", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--header", required=True)
    args = parser.parse_args()
    rows, identity, lists = build(args.root_2019, args.derived)
    digest = hashlib.sha256(
        canonical(rows, lists, identity).encode("ascii")).hexdigest()
    text = emit(rows, lists, identity, digest,
                designator_editions(args.root_2019, args.derived))
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_bytes(text.encode("ascii"))
    header = emit_header(rows)
    Path(args.header).parent.mkdir(parents=True, exist_ok=True)
    if not Path(args.header).is_file() or Path(args.header).read_bytes() != header.encode("ascii"):
        Path(args.header).write_bytes(header.encode("ascii"))
    for (standard, package), keys in sorted(lists.items()):
        print("%d %s.%s %d" % (standard, package[0], package[1], len(keys)),
              file=sys.stderr)
    print("table entries: %d digest %s" % (len(rows), digest),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
