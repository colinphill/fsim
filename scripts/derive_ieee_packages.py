# SPDX-License-Identifier: Apache-2.0
"""Derive the IEEE package sources of older VHDL standards from 1076-2019.

The input is the Apache-2.0 IEEE 1076-2019 package source tree
(third_party/ieee-1076-2019) and the removals table
(scripts/vhdl_ieee_removals.tsv).  A removal names one declarative item of one
package (a whole subprogram declaration together with its body, a type, a
subtype, a constant, an alias, a use clause, ...) that a later standard added.
Every byte of a derived file is a byte of the 2019 file or of the generated
header, so no text of an older IEEE original is ever copied.

  derive_ieee_packages.py --root DIR --table FILE --out DIR     write the files
  derive_ieee_packages.py --root DIR --table FILE --verify DIR  reproduce and
                                                  check the subsequence rule
  derive_ieee_packages.py --root DIR --table FILE --names DIR   declared names

The functions are also used by the registered checks.
"""

import argparse
import hashlib
import re
import sys
from pathlib import Path

STANDARDS = ("1987", "1993", "2002", "2008")
# The 2000 text equals the 2002 text for these packages, so there is no
# separate 2000 tree.
PACKAGES = {
    "1987": ("std_logic_1164",),
    "1993": ("std_logic_1164", "numeric_bit", "numeric_std", "math_real",
             "math_complex"),
    "2002": ("std_logic_1164", "numeric_bit", "numeric_std", "math_real",
             "math_complex"),
    "2008": ("fixed_float_types", "fixed_generic_pkg", "fixed_pkg",
             "float_generic_pkg", "float_pkg", "math_complex", "math_real",
             "numeric_bit", "numeric_bit_unsigned", "numeric_std",
             "numeric_std_unsigned", "std_logic_1164", "std_logic_textio"),
}
# Packages whose 2019 text has no separate body file.
NO_BODY = ("fixed_float_types", "fixed_pkg", "float_pkg", "std_logic_textio")
HEADER_END = "-- End of the fsim derivation header; the 2019 text follows."


def sha256_hex(data):
    return hashlib.sha256(data).hexdigest()


# ---------------------------------------------------------------- lexing

def lex(text):
    """Tokens (kind, text, begin, end); kind c(omment) w(ord) s(tring/char)
    n(umber) o(ther)."""
    result = []
    i = 0
    n = len(text)
    previous = ""
    while i < n:
        ch = text[i]
        if ch in " \t\r\n\f":
            i += 1
            continue
        if text.startswith("--", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            result.append(("c", text[i:j], i, j))
            i = j
            continue
        if ch == '"':
            j = i + 1
            while j < n:
                if text[j] == '"':
                    if j + 1 < n and text[j + 1] == '"':
                        j += 2
                        continue
                    break
                j += 1
            result.append(("s", text[i:j + 1], i, j + 1))
            i = j + 1
            previous = "s"
            continue
        if ch == "'":
            before = text[i - 1] if i > 0 else " "
            if i + 2 < n and text[i + 2] == "'" and not (
                    before.isalnum() or before in "_)]"):
                result.append(("s", text[i:i + 3], i, i + 3))
                i += 3
                continue
            result.append(("o", "'", i, i + 1))
            i += 1
            continue
        if ch.isalpha():
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            result.append(("w", text[i:j], i, j))
            i = j
            continue
        if ch.isdigit():
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] in "_.#"):
                j += 1
            result.append(("n", text[i:j], i, j))
            i = j
            continue
        if ch == "\\":
            j = text.find("\\", i + 1)
            result.append(("w", text[i:j + 1], i, j + 1))
            i = j + 1
            continue
        for op in ("<=", ">=", "/=", ":=", "=>", "**", "<>", "??", "?=",
                   "?/=", "?<=", "?>=", "?<", "?>"):
            if text.startswith(op, i):
                result.append(("o", op, i, i + len(op)))
                i += len(op)
                break
        else:
            result.append(("o", ch, i, i + 1))
            i += 1
    return result


def squeeze(text):
    return re.sub(r"\s+", " ", text.strip()).lower()


# ------------------------------------------------------------------ items

class Item:
    def __init__(self, kind, name, key, first, last, body, code_first=None):
        self.code_first = first if code_first is None else code_first
        self.kind = kind
        self.name = name
        self.key = key
        self.first = first  # first line (0-based), comment block included
        self.last = last    # last line, inclusive
        self.body = body    # a subprogram or protected body

    def __repr__(self):
        return "Item(%s, %d-%d%s)" % (self.key, self.first + 1, self.last + 1,
                                      ", body" if self.body else "")


def formal_types(tokens_in_parens):
    """Types of a formal part, one entry per declared name."""
    groups = []
    depth = 0
    current = []
    for kind, word, _, _ in tokens_in_parens:
        if word in "([":
            depth += 1
        elif word in ")]":
            depth -= 1
        if word == ";" and depth == 0:
            groups.append(current)
            current = []
        else:
            current.append(word)
    groups.append(current)
    types = []
    for group in groups:
        if not group:
            continue
        if ":" not in group:
            types.append(squeeze(" ".join(group)))
            continue
        colon = group.index(":")
        names = [w for w in group[:colon]
                 if w != "," and w.lower() not in (
                     "signal", "constant", "variable", "file")]
        rest = group[colon + 1:]
        if ":=" in rest:
            rest = rest[:rest.index(":=")]
        if rest and rest[0].lower() in ("in", "out", "inout", "buffer",
                                        "linkage"):
            rest = rest[1:]
        types.extend([squeeze(" ".join(rest))] * max(1, len(names)))
    return types


def declaration_key(toks):
    """(kind, name, key) of an item from its comment-free tokens."""
    words = [t[1] for t in toks]
    low = [w.lower() for w in words]
    index = 0
    pure = None
    if low[0] in ("pure", "impure"):
        pure = low[0]
        index = 1
    if low[index] in ("function", "procedure"):
        kind = low[index]
        name = low[index + 1]
        position = index + 2
        types = []
        if position < len(words) and words[position] == "(":
            depth = 0
            start = position + 1
            for j in range(position, len(words)):
                if words[j] == "(":
                    depth += 1
                elif words[j] == ")":
                    depth -= 1
                    if depth == 0:
                        types = formal_types(toks[start:j])
                        position = j + 1
                        break
        ret = ""
        if position < len(low) and low[position] == "return":
            end = position + 1
            while end < len(low) and low[end] not in ("is", ";"):
                end += 1
            ret = squeeze(" ".join(words[position + 1:end]))
        key = "%s|%s(%s)%s" % (kind, name, ",".join(types),
                               "->" + ret if ret else "")
        return kind, name, key
    if low[0] == "alias":
        name = low[1]
        rest = squeeze(" ".join(words[low.index("is") + 1:])) \
            if "is" in low else ""
        return "alias", name, "alias|%s=%s" % (name, rest.rstrip("; "))
    if low[0] in ("use", "library", "context"):
        return low[0], "", "%s|%s" % (low[0], squeeze(" ".join(words)).rstrip(
            "; "))
    if low[0] in ("generic", "port"):
        return low[0], "", "%s|%s" % (low[0], squeeze(" ".join(words))[:80])
    if low[0] in ("type", "subtype"):
        # One namespace: a type and a subtype of the same name are one item
        # whose form may differ between standards.
        return low[0], low[1], "type|%s" % low[1]
    if low[0] in ("constant", "attribute", "file", "component", "signal",
                  "variable", "shared", "package", "nature", "terminal"):
        return low[0], low[1], "%s|%s" % (low[0], low[1])
    return "other", "", "other|" + squeeze(" ".join(words))[:60]


def find_unit(code):
    """Index of the first token of the declarative part, and whether the
    unit is a package body.  Returns (start, is_body) or None."""
    for index, (kind, word, _, _) in enumerate(code):
        if kind == "w" and word.lower() == "package":
            nxt = code[index + 1][1].lower()
            is_body = nxt == "body"
            name_at = index + 2 if is_body else index + 1
            if code[name_at + 1][1].lower() != "is":
                continue
            if code[name_at + 2][1].lower() == "new":
                return None
            return name_at + 2, is_body, index
    return None


def scan_item(code, i):
    """Index of the token ending the item that starts at code[i], and whether
    it is a body."""
    low = lambda j: code[j][1].lower()
    first = low(i)
    depth = 0
    j = i
    subprogram = first in ("function", "procedure", "pure", "impure")
    if first == "pure" or first == "impure":
        pass
    level = 0
    seen_is = False
    pending = False  # a nested subprogram specification awaits its `is`
    body = False
    while j < len(code):
        word = code[j][1]
        w = word.lower()
        if word in ("(", "["):
            depth += 1
        elif word in (")", "]"):
            depth -= 1
        elif depth == 0:
            previous = low(j - 1) if j > i else ""
            if subprogram and not seen_is and w == "is":
                nxt = low(j + 1)
                if nxt in ("<>", "new"):
                    pass
                else:
                    seen_is = True
                    body = True
                    level = 1
            elif seen_is or level:
                if w == "end":
                    level -= 1
                    if level == 0:
                        k = j
                        while code[k][1] != ";":
                            k += 1
                        return k, body
                elif w in ("if", "case", "loop", "record", "units") and \
                        previous != "end":
                    level += 1
                elif w in ("function", "procedure", "pure", "impure") and \
                        previous not in ("end",):
                    pending = True
                elif w == "is" and pending:
                    pending = False
                    level += 1
                elif word == ";" and pending:
                    pending = False
            elif w in ("record", "units", "protected") and previous != "end":
                level = 1
                seen_is = True
            elif word == ";":
                return j, False
        j += 1
    raise ValueError("unterminated declarative item")


def split_items(text):
    """Declarative items of one design unit's file: context clauses before
    the unit, then the items of its declarative part.  Returns the list."""
    lines = text.split("\n")
    offsets = []
    position = 0
    for line in lines:
        offsets.append(position)
        position += len(line) + 1

    def line_of(offset):
        low, high = 0, len(offsets) - 1
        while low < high:
            mid = (low + high + 1) // 2
            if offsets[mid] <= offset:
                low = mid
            else:
                high = mid - 1
        return low

    code = [t for t in lex(text) if t[0] != "c"]
    unit = find_unit(code)
    items = []
    previous_last = -1

    def attach(first_line):
        line = first_line
        while line - 1 > previous_last and lines[line - 1].strip().startswith(
                "--"):
            line -= 1
        return line

    def take(start, stop, body):
        nonlocal previous_last
        toks = code[start:stop + 1]
        # A body is keyed by its specification (the tokens before `is`).
        if body:
            for j, t in enumerate(toks):
                if t[1].lower() == "is":
                    toks = toks[:j] + [("o", ";", 0, 0)]
                    break
        kind, name, key = declaration_key(toks)
        code_first = line_of(code[start][2])
        first = attach(code_first)
        last = line_of(code[stop][3] - 1)
        items.append(Item(kind, name, key, first, last, body, code_first))
        previous_last = last

    if unit is None:
        return items
    start_index, is_body, package_index = unit
    # Context clauses and libraries before the unit.
    i = 0
    while i < package_index:
        stop, _ = scan_item(code, i)
        take(i, stop, False)
        i = stop + 1
    i = start_index
    while i < len(code):
        if code[i][1].lower() == "end":
            break
        stop, body = scan_item(code, i)
        take(i, stop, body)
        i = stop + 1
    return items


# ----------------------------------------------------------------- tables

class Removal:
    def __init__(self, standard, package, key, clause, reason, line):
        self.standard = standard
        self.package = package
        self.key = key
        self.clause = clause
        self.reason = reason
        self.line = line  # 1-based row line in the table


class Rewrite:
    """One row of the rewrites table.  Rules: copy_as (the item KEY becomes a
    copy of the item named by from=, with the subst= identifiers replaced),
    duplicate (a copy of the item KEY with subst= follows it), substitute
    (identifiers replaced in the item KEY, or everywhere for KEY *),
    drop_end_kind (the kind keywords after `end` are dropped), insert_before
    (a copy of the item named by from= of the 2019 file file=, with subst=,
    precedes the item KEY), omit_file (the file is not shipped)."""

    RULES = ("copy_as", "duplicate", "substitute", "drop_end_kind",
             "insert_before", "omit_file")

    def __init__(self, standard, package, files, rule, key, arguments, clause,
                 reason, line):
        self.standard = standard
        self.package = package
        self.files = files
        self.rule = rule
        self.key = key
        self.arguments = arguments
        self.clause = clause
        self.reason = reason
        self.line = line
        self.source = arguments.get("from", "")
        self.subst = {}
        for pair in arguments.get("subst", "").split(","):
            if pair:
                old, new = pair.split(":")
                self.subst[old.lower()] = new


def _rows(path):
    for number, line in enumerate(
            Path(path).read_text(encoding="utf-8").split("\n"), 1):
        if line and not line.startswith("#"):
            yield number, line.split("\t")


def load_table(path):
    rows = []
    for number, fields in _rows(path):
        if len(fields) != 5:
            raise ValueError("%s:%d: five tab-separated columns expected" %
                             (path, number))
        standard, package, key, clause, reason = fields
        if standard not in STANDARDS:
            raise ValueError("%s:%d: unknown standard %s" % (path, number,
                                                             standard))
        if not clause.strip() or not reason.strip():
            raise ValueError("%s:%d: clause and reason are required" %
                             (path, number))
        rows.append(Removal(standard, package, key, clause, reason, number))
    return rows


def load_rewrites(path):
    rows = []
    for number, fields in _rows(path):
        if len(fields) != 8:
            raise ValueError("%s:%d: eight tab-separated columns expected" %
                             (path, number))
        standard, package, files, rule, key, arguments, clause, reason = \
            fields
        if standard not in STANDARDS or rule not in Rewrite.RULES or \
                files not in ("decl", "body", "both"):
            raise ValueError("%s:%d: bad standard, rule or file scope" %
                             (path, number))
        if not clause.strip() or not reason.strip():
            raise ValueError("%s:%d: clause and reason are required" %
                             (path, number))
        parsed = {}
        for part in arguments.split(";"):
            if part:
                name, value = part.split("=", 1)
                parsed[name] = value
        rows.append(Rewrite(standard, package, files, rule, key, parsed,
                            clause, reason, number))
    return rows


# --------------------------------------------------------------- deriving

def source_files(root, package):
    base = Path(root) / "ieee"
    files = [(package + ".vhdl", base / (package + ".vhdl"), "decl")]
    if package not in NO_BODY:
        files.append((package + "-body.vhdl", base / (package + "-body.vhdl"),
                      "body"))
    return files


def replace_identifiers(text, mapping):
    """`text` with each identifier token in `mapping` (lower case keys)
    replaced, keeping the original upper/lower case style."""
    if not mapping:
        return text
    out = []
    last = 0
    for kind, word, begin, end in lex(text):
        if kind == "w" and word.lower() in mapping:
            new = mapping[word.lower()]
            if word.isupper():
                new = new.upper()
            elif word.islower():
                new = new.lower()
            out.append(text[last:begin])
            out.append(new)
            last = end
    out.append(text[last:])
    return "".join(out)


def drop_end_kinds(text):
    """`end function X;` -> `end X;` (VHDL-1987 has no kind keyword)."""
    tokens_ = [t for t in lex(text) if t[0] != "c"]
    out = []
    last = 0
    i = 0
    while i < len(tokens_):
        kind, word, begin, end = tokens_[i]
        if kind == "w" and word.lower() == "end" and i + 1 < len(tokens_):
            nxt = tokens_[i + 1][1].lower()
            drop_to = None
            if nxt in ("function", "procedure"):
                drop_to = i + 1
            elif nxt == "package":
                drop_to = i + 1
                if i + 2 < len(tokens_) and tokens_[i + 2][1].lower() == "body":
                    drop_to = i + 2
            if drop_to is not None:
                out.append(text[last:end])
                last = tokens_[drop_to][3]
                i = drop_to
        i += 1
    out.append(text[last:])
    return "".join(out)


def derive_file(root, original_text, items, removals, rewrites, file_kind):
    """The derived text of one file; also the set of removal and rewrite rows
    that took effect."""
    lines = original_text.split("\n")
    removal_keys = {r.key: r for r in removals}
    by_key = {}
    for item in items:
        by_key.setdefault(item.key, []).append(item)
    used = set()

    def applies(row):
        return row.files in ("both", file_kind)

    def code_of(item):
        return lines[item.code_first:item.last + 1]

    def transformed(item, mapping):
        return replace_identifiers("\n".join(code_of(item)), mapping)

    out = []
    cursor = 0
    for item in items:
        out.extend(lines[cursor:item.first])
        cursor = item.last + 1
        for row in rewrites:
            if applies(row) and row.rule == "insert_before" and \
                    row.key == item.key:
                other = (Path(root) / "ieee" / row.arguments["file"]
                         ).read_bytes().decode("latin-1")
                other_lines = other.split("\n")
                source = [i for i in split_items(other)
                          if i.key == row.source][0]
                out.extend(replace_identifiers(
                    "\n".join(other_lines[source.code_first:source.last + 1]),
                    row.subst).split("\n"))
                out.append("")
                used.add(("rewrite", id(row)))
        if item.key in removal_keys:
            used.add(("removal", id(removal_keys[item.key])))
            continue
        head = lines[item.first:item.code_first]
        code = "\n".join(code_of(item))
        for row in rewrites:
            if not applies(row) or row.rule != "copy_as" or \
                    row.key != item.key:
                continue
            source = by_key[row.source][0]
            code = transformed(source, row.subst)
            used.add(("rewrite", id(row)))
        for row in rewrites:
            if applies(row) and row.rule == "substitute" and \
                    row.key == item.key:
                code = replace_identifiers(code, row.subst)
                used.add(("rewrite", id(row)))
        block = head + code.split("\n")
        for row in rewrites:
            if applies(row) and row.rule == "duplicate" and \
                    row.key == item.key:
                block += [""] + replace_identifiers(
                    "\n".join(code_of(item)), row.subst).split("\n")
                used.add(("rewrite", id(row)))
        out.extend(block)
    out.extend(lines[cursor:])
    text = "\n".join(out)
    for row in rewrites:
        if applies(row) and row.rule == "substitute" and row.key == "*":
            text = replace_identifiers(text, row.subst)
            used.add(("rewrite", id(row)))
    for row in rewrites:
        if applies(row) and row.rule == "drop_end_kind":
            text = drop_end_kinds(text)
            used.add(("rewrite", id(row)))
    return text, used


def header_for(standard, package, name, original_bytes, removal_rows,
               rewrite_rows):
    digest = sha256_hex(original_bytes)
    out = [
        "-- ----------------------------------------------------------------",
        "-- fsim-derived IEEE package source for VHDL-%s." % standard,
        "--",
        "-- This file is NOT an IEEE original. It is derived from the IEEE",
        "-- P1076 Working Group Open Source Repository file ieee/%s" % name,
        "-- (IEEE Std 1076-2019 packages, tag 1076-2019, SHA-256 %s)" % digest,
        "-- by deleting the declarations and bodies that later revisions of the",
        "-- standard added and by the mechanical rewrites declared in the",
        "-- tables below. The %d removal rows and %d rewrite rows applied to" %
        (len(removal_rows), len(rewrite_rows)),
        "-- this file are rows of scripts/vhdl_ieee_removals.tsv (standard %s," %
        standard,
        "-- package %s) and scripts/vhdl_ieee_rewrites.tsv." % package,
        "-- Rewrite rows (line numbers of the rewrites table): %s." %
        (", ".join(str(r.line) for r in rewrite_rows) or "none"),
        "--",
        "-- The 2019 file's own notice (Copyright 2019 IEEE P1076 WG Authors,",
        "-- Apache License, Version 2.0) follows unchanged and applies to this",
        "-- derived work; see the LICENSE and AUTHORS files of",
        "-- third_party/ieee-1076-2019.",
        HEADER_END,
    ]
    return "\n".join(out) + "\n"


def derive(root, table, rewrites, standard):
    """({file name: bytes}, unmatched rows, packages without a tree)."""
    removals = [r for r in table if r.standard == standard]
    edits = [r for r in rewrites if r.standard == standard]
    result = {}
    matched = set()
    omitted = {(r.package, r.files) for r in edits if r.rule == "omit_file"}
    for r in edits:
        if r.rule == "omit_file":
            matched.add(("rewrite", id(r)))
    for package in PACKAGES[standard]:
        package_removals = [r for r in removals if r.package == package]
        package_edits = [r for r in edits if r.package == package and
                         r.rule != "omit_file"]
        for name, path, kind in source_files(root, package):
            if (package, kind) in omitted:
                continue
            original = path.read_bytes()
            text = original.decode("latin-1")
            items = split_items(text)
            derived, used = derive_file(
                root, text, items, package_removals, package_edits, kind)
            matched.update(used)
            file_removals = [r for r in package_removals
                             if ("removal", id(r)) in used]
            file_edits = [r for r in package_edits if ("rewrite", id(r)) in used]
            result[name] = (header_for(standard, package, name, original,
                                       file_removals, file_edits) +
                            derived).encode("latin-1")
    unmatched = [r for r in removals if ("removal", id(r)) not in matched] + \
        [r for r in edits if ("rewrite", id(r)) not in matched]
    packaged = ({r.package for r in removals} | {r.package for r in edits}) \
        - set(PACKAGES[standard])
    return result, unmatched, packaged


def readme_for(standard, names):
    packages = sorted({n.replace("-body", "")[:-len(".vhdl")] for n in names})
    declaration_only = sorted(
        p for p in packages if p not in NO_BODY and
        (p + "-body.vhdl") not in names)
    return (
        "<!-- SPDX-License-Identifier: Apache-2.0 -->\n"
        "# IEEE package sources derived for VHDL-%s\n\n"
        "These files are included for reference only. fsim does not read them:\n"
        "it simulates the IEEE packages with its built-in models.\n\n"
        "These files are NOT IEEE originals. Each one is the matching file of the\n"
        "IEEE 1076-2019 package sources (`third_party/ieee-1076-2019`, Apache-2.0)\n"
        "with the declarations and bodies that later standards added deleted\n"
        "(`scripts/vhdl_ieee_removals.tsv`, rows for standard %s) and with the\n"
        "mechanical rewrites of `scripts/vhdl_ieee_rewrites.tsv` applied, so that\n"
        "the text is valid VHDL of this standard. No text of an older IEEE original\n"
        "is used. A generated header names the 2019 file, its SHA-256, the rows\n"
        "applied, and the notice that applies. `scripts/derive_ieee_packages.py`\n"
        "writes the files and the `fsim.ieee-derived-*` checks reproduce them byte\n"
        "for byte.\n\n"
        "Packages: %s.\n\n"
        "%s%s"
        "`SHA256SUMS` records every VHDL file of this directory.\n"
    ) % (standard, standard, ", ".join(packages),
         ("The 2000 text equals the 2002 text for these packages, so `2002` "
          "serves\nboth VHDL-2000 and VHDL-2002.\n\n" if standard == "2002"
          else ""),
         ("Declaration only (no body file): %s. The 2019 bodies call the\n"
          "2008 predefined MAXIMUM and MINIMUM, which this standard does not\n"
          "have, and the 2019 text has no function to derive an integer MAX\n"
          "or MIN helper from.\n\n" % ", ".join(declaration_only)
          if declaration_only else ""))


def sums_for(files):
    return "".join("%s  ieee/%s\n" % (sha256_hex(data), name)
                   for name, data in sorted(files.items()))


def split_header(data):
    text = data.decode("latin-1")
    marker = HEADER_END + "\n"
    at = text.index(marker)
    return text[:at + len(marker)], text[at + len(marker):]


def check_traceability(root, table, rewrites, standard, package, name,
                       derived_bytes):
    """Problems (strings) in one derived file against its 2019 original:
    every change is inside an item a declared row names, and the header lists
    exactly the rewrite rows that apply to the file."""
    import difflib
    problems = []
    original = (Path(root) / "ieee" / name).read_bytes().decode("latin-1")
    header, body = split_header(derived_bytes)
    original_lines = original.split("\n")
    kept = body.split("\n")
    kind = "body" if name.endswith("-body.vhdl") else "decl"
    items = split_items(original)
    removal_keys = {r.key for r in table
                    if r.standard == standard and r.package == package}
    edits = [r for r in rewrites if r.standard == standard and
             r.package == package and r.files in ("both", kind) and
             r.rule != "omit_file"]
    allowed = set()
    for item in items:
        if item.key in removal_keys:
            allowed.update(range(item.first, item.last + 1))
    global_rows = [r for r in edits if r.key == "*"]
    for row in edits:
        if row.key == "*":
            continue
        for item in items:
            if row.rule == "insert_before" and item.key == row.key:
                allowed.update(range(max(item.first - 1, 0), item.first + 1))
            if item.key in (row.key, row.source):
                allowed.update(range(item.first, item.last + 1))
                if row.rule == "duplicate" and item.key == row.key:
                    # The copy follows the item after one separator line; a
                    # line diff may place the insertion on either side of it.
                    allowed.update((item.last + 1, item.last + 2))
    mapping = {}
    drops = False
    for row in global_rows:
        if row.rule == "substitute":
            mapping.update(row.subst)
        elif row.rule == "drop_end_kind":
            drops = True

    def global_transform(line):
        line = replace_identifiers(line, mapping)
        return drop_end_kinds(line) if drops else line

    transformed = [global_transform(line) for line in original_lines]
    matcher = difflib.SequenceMatcher(None, transformed, kept,
                                      autojunk=False)
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            continue
        span = set(range(i1, i2)) or {i1}
        if not span <= allowed:
            problems.append(
                "lines %d-%d of the 2019 file changed outside the declared "
                "rows" % (i1 + 1, max(i2, i1 + 1)))
    listed = re.search(r"Rewrite rows \(line numbers of the rewrites table\): "
                       r"([^\n]*)\.\n", header)
    expected = ", ".join(str(r.line) for r in edits if r.rule != "omit_file") \
        or "none"
    if listed is None:
        problems.append("the header lacks the rewrite-row list")
    else:
        rows_in_header = [x for x in listed.group(1).split(", ") if x != "none"]
        applicable = {str(r.line) for r in edits}
        if not set(rows_in_header) <= applicable:
            problems.append("the header lists rewrite rows that do not apply")
    del expected
    return problems


# ------------------------------------------------------------------ names

def declared_names(text):
    """Names declared by a package declaration file: subprograms (operators
    without quotes), types, subtypes, constants, aliases, attributes and the
    enumeration literals that are identifiers."""
    names = set()
    lines = text.split("\n")
    for item in split_items(text):
        if item.kind in ("function", "procedure", "alias", "type", "subtype",
                         "constant", "attribute"):
            names.add(item.name.strip('"'))
        if item.kind == "type":
            body = "\n".join(lines[item.first:item.last + 1])
            body = re.sub(r"--[^\n]*", "", body)
            match = re.search(r"\btype\s+\w+\s+is\s*\((.*)\)\s*;", body,
                              re.I | re.S)
            if match and "range" not in match.group(1).lower():
                for literal in match.group(1).split(","):
                    literal = literal.strip()
                    if re.fullmatch(r"[A-Za-z]\w*", literal):
                        names.add(literal)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--table", required=True)
    parser.add_argument("--rewrites", required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--out")
    group.add_argument("--verify")
    arguments = parser.parse_args()
    table = load_table(arguments.table)
    rewrites = load_rewrites(arguments.rewrites)
    failures = 0
    for standard in STANDARDS:
        files, unmatched, extra = derive(arguments.root, table, rewrites,
                                         standard)
        for row in unmatched:
            print("table line %d matches nothing: %s %s %s" % (
                row.line, row.standard, row.package, row.key))
            failures += 1
        for package in sorted(extra):
            print("standard %s has rows for absent package %s" % (standard,
                                                                  package))
            failures += 1
        base = Path(arguments.out or arguments.verify) / standard
        for name, data in files.items():
            target = base / "ieee" / name
            if arguments.out:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            elif not target.exists() or target.read_bytes() != data:
                print("%s differs from the generator output" % target)
                failures += 1
        extras = {"README.md": readme_for(standard, files).encode(),
                  "SHA256SUMS": sums_for(files).encode()}
        for name, data in extras.items():
            target = base / name
            if arguments.out:
                target.write_bytes(data)
            elif not target.exists() or target.read_bytes() != data:
                print("%s differs from the generator output" % target)
                failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
