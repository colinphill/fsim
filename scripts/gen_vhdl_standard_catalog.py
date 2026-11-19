#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate include/fsim/semantic/vhdl_standard_catalog.inc, the declarations of package STD.STANDARD.

IEEE 1076-2019 16.3 lists the declarations of package STANDARD, most of them
as comments because they are implicitly declared (the predefined operations of
each type). The VHPI library model (19.4.5, Figure 4) gives the package
declaration object those declarations, and the compiled HIR retains none of
them, so the VHPI host builds its funcDecl, typeDecl and attrDecl objects from
the table this script writes. The overload resolution of 12.5 reads the same
table (fsim::semantic::vhdl_standard_catalog()): these are the interpretations
of the predefined operators, which no source declares.

Each row is (since, kind, name, result, parameters, element, index):
  since       the first standard whose STANDARD has the declaration (1987, 1993,
              2008, 2019; 1076-2002 added nothing to STANDARD, so it uses the
              1993 rows)
  kind        f pure function, i impure function, t type or subtype,
              r range record type of the named type ('RANGE_RECORD), a attribute
  name        designator, an operator symbol with its quotes
  result      result type, or the attribute's type
  element     for the type row of an array type, its element type
  index       for the type row of an array type, its index subtype
  parameters  NAME/CLASS/TYPE items separated by commas; CLASS c constant,
              s signal; an anonymous formal has an empty NAME

The 2019 rows follow 16.3 in order. The rows for earlier standards are the
2019 rows that existed in those editions; the editions are not checked here
(only the 2019 text is), see docs/foreign-interface-conformance.md.

The same rows also give include/fsim/support/vhdl_standard_declaration.inc,
one X(name, row, since, until) per row, which names the declarations as the
enumeration fsim::support::StandardDeclaration (value row + 1, 0 is none).
`until` is the first standard whose STANDARD no longer has the declaration
because a later row (same kind, designator and parameters) replaces it, or 0.
The name is lower case: the type's name, <type>_range_record, attribute_<name>,
<function>_<parameter types> or op_<operator>_<parameter types>; it takes the
result type as a suffix only where the other parts are not unique (NOW).

  gen_vhdl_standard_catalog.py OUTPUT [DECLARATIONS]
      write OUTPUT and DECLARATIONS (default: support/vhdl_standard_declaration.inc
      beside OUTPUT's directory)
  gen_vhdl_standard_catalog.py --check OUTPUT [DECLARATIONS]
      fail when either file is out of date
  gen_vhdl_standard_catalog.py --counts        print the rows per revision
"""

import os
import sys

REVISIONS = (1987, 1993, 2008, 2019)

rows = []


def add(since, kind, name, result, params=(), element="", index=""):
    items = ",".join("%s/%s/%s" % (n, c, t) for n, c, t in params)
    rows.append((since, kind, name, result, items, element, index))


def anon(*types):
    return [("", "c", t) for t in types]


def relational(t, since):
    for op in ("=", "/=", "<", "<=", ">", ">="):
        add(since, "f", '"%s"' % op, "BOOLEAN", anon(t, t))


def minmax(t, since=2008):
    for op in ("MINIMUM", "MAXIMUM"):
        add(since, "f", op, t, [("L", "c", t), ("R", "c", t)])


def minmax_element(vector, element):
    for op in ("MINIMUM", "MAXIMUM"):
        add(2008, "f", op, element, [("L", "c", vector)])


def range_record(t, since=2019):
    add(since, "r", t, "", ())


def enumeration(name, since=1987, min_max_since=2008):
    add(since, "t", name, "", ())
    relational(name, since)
    minmax(name, max(since, min_max_since))
    range_record(name, max(since, 2019))


def logic_scalar(t):
    for op, since in (("and", 1987), ("or", 1987), ("nand", 1987),
                      ("nor", 1987), ("xor", 1987), ("xnor", 1993)):
        add(since, "f", '"%s"' % op, t, anon(t, t))
    add(1987, "f", '"not"', t, anon(t))


def edges(t):
    for op in ("RISING_EDGE", "FALLING_EDGE"):
        add(2008, "f", op, "BOOLEAN", [("S", "s", t)])


def arithmetic(t, power):
    for op in ("+", "-", "abs"):
        add(1987, "f", '"%s"' % op, t, anon(t))
    for op in ("+", "-", "*", "/"):
        add(1987, "f", '"%s"' % op, t, anon(t, t))
    if t != "REAL" and not t.startswith("universal_real"):
        for op in ("mod", "rem"):
            add(1987, "f", '"%s"' % op, t, anon(t, t))
    if power:
        add(1987, "f", '"**"', t, anon(t, power))


def concatenations(vector, element, since=1987):
    add(since, "f", '"&"', vector, anon(vector, vector))
    add(since, "f", '"&"', vector, anon(vector, element))
    add(since, "f", '"&"', vector, anon(element, vector))
    add(since, "f", '"&"', vector, anon(element, element))


def logic_vector(vector, element, result_match):
    for op, since in (("and", 1987), ("or", 1987), ("nand", 1987),
                      ("nor", 1987), ("xor", 1987), ("xnor", 1993)):
        add(since, "f", '"%s"' % op, vector, anon(vector, vector))
    add(1987, "f", '"not"', vector, anon(vector))
    for op in ("and", "or", "nand", "nor", "xor", "xnor"):
        add(2008, "f", '"%s"' % op, vector, anon(vector, element))
        add(2008, "f", '"%s"' % op, vector, anon(element, vector))
    for op in ("and", "or", "nand", "nor", "xor", "xnor"):
        add(2008, "f", '"%s"' % op, element, anon(vector))
    for op in ("sll", "srl", "sla", "sra", "rol", "ror"):
        add(1993, "f", '"%s"' % op, vector, anon(vector, "INTEGER"))
    relational(vector, 1987)
    for op in ("?=", "?/="):
        add(2008, "f", '"%s"' % op, result_match, anon(vector, vector))
    concatenations(vector, element)
    minmax(vector)
    minmax_element(vector, element)


def build():
    # RANGE_DIRECTION (2019)
    enumeration("RANGE_DIRECTION", since=2019)
    # BOOLEAN
    add(1987, "t", "BOOLEAN", "")
    logic_scalar("BOOLEAN")
    relational("BOOLEAN", 1987)
    minmax("BOOLEAN")
    edges("BOOLEAN")
    range_record("BOOLEAN")
    # BIT
    add(1987, "t", "BIT", "")
    logic_scalar("BIT")
    relational("BIT", 1987)
    for op in ("?=", "?/=", "?<", "?<=", "?>", "?>="):
        add(2008, "f", '"%s"' % op, "BIT", anon("BIT", "BIT"))
    minmax("BIT")
    add(2008, "f", '"??"', "BOOLEAN", anon("BIT"))
    edges("BIT")
    range_record("BIT")
    enumeration("CHARACTER")
    enumeration("SEVERITY_LEVEL")
    # universal_integer and universal_real have no declaration of their own
    relational("universal_integer", 1987)
    for op in ("+", "-", "abs"):
        add(1987, "f", '"%s"' % op, "universal_integer", anon("universal_integer"))
    for op in ("+", "-", "*", "/", "mod", "rem"):
        add(1987, "f", '"%s"' % op, "universal_integer",
            anon("universal_integer", "universal_integer"))
    minmax("universal_integer")
    range_record("universal_integer")
    relational("universal_real", 1987)
    for op in ("+", "-", "abs"):
        add(1987, "f", '"%s"' % op, "universal_real", anon("universal_real"))
    for op in ("+", "-", "*", "/"):
        add(1987, "f", '"%s"' % op, "universal_real",
            anon("universal_real", "universal_real"))
    add(1987, "f", '"*"', "universal_real", anon("universal_real", "universal_integer"))
    add(1987, "f", '"*"', "universal_real", anon("universal_integer", "universal_real"))
    add(1987, "f", '"/"', "universal_real", anon("universal_real", "universal_integer"))
    minmax("universal_real")
    range_record("universal_real")
    # INTEGER
    add(1987, "f", '"**"', "universal_integer", anon("universal_integer", "INTEGER"))
    add(1987, "f", '"**"', "universal_real", anon("universal_real", "INTEGER"))
    add(1987, "t", "INTEGER", "")
    relational("INTEGER", 1987)
    arithmetic("INTEGER", "INTEGER")
    minmax("INTEGER")
    range_record("INTEGER")
    # REAL
    add(1987, "t", "REAL", "")
    relational("REAL", 1987)
    arithmetic("REAL", "INTEGER")
    minmax("REAL")
    range_record("REAL")
    # TIME
    add(1987, "t", "TIME", "")
    relational("TIME", 1987)
    add(1987, "f", '"+"', "TIME", anon("TIME"))
    add(1987, "f", '"-"', "TIME", anon("TIME"))
    add(1987, "f", '"abs"', "TIME", anon("TIME"))
    add(1987, "f", '"+"', "TIME", anon("TIME", "TIME"))
    add(1987, "f", '"-"', "TIME", anon("TIME", "TIME"))
    add(1987, "f", '"*"', "TIME", anon("TIME", "INTEGER"))
    add(1987, "f", '"*"', "TIME", anon("TIME", "REAL"))
    add(1987, "f", '"*"', "TIME", anon("INTEGER", "TIME"))
    add(1987, "f", '"*"', "TIME", anon("REAL", "TIME"))
    add(1987, "f", '"/"', "TIME", anon("TIME", "INTEGER"))
    add(1987, "f", '"/"', "TIME", anon("TIME", "REAL"))
    add(1987, "f", '"/"', "universal_integer", anon("TIME", "TIME"))
    add(2008, "f", '"mod"', "TIME", anon("TIME", "TIME"))
    add(2008, "f", '"rem"', "TIME", anon("TIME", "TIME"))
    minmax("TIME")
    range_record("TIME")
    add(1993, "t", "DELAY_LENGTH", "")
    add(1987, "i", "NOW", "TIME")
    add(1993, "i", "NOW", "DELAY_LENGTH")
    add(1987, "t", "NATURAL", "")
    add(1987, "t", "POSITIVE", "")
    # STRING
    add(1987, "t", "STRING", "", element="CHARACTER", index="POSITIVE")
    relational("STRING", 1987)
    concatenations("STRING", "CHARACTER")
    minmax("STRING")
    minmax_element("STRING", "CHARACTER")
    # BOOLEAN_VECTOR (2008)
    add(2008, "t", "BOOLEAN_VECTOR", "", element="BOOLEAN", index="NATURAL")
    logic_vector("BOOLEAN_VECTOR", "BOOLEAN", "BOOLEAN")
    # BIT_VECTOR
    add(1987, "t", "BIT_VECTOR", "", element="BIT", index="NATURAL")
    logic_vector("BIT_VECTOR", "BIT", "BIT")
    for name in ("TO_STRING", "TO_OSTRING", "TO_HSTRING"):
        add(2008, "f", name, "STRING", [("VALUE", "c", "BIT_VECTOR")])
    # INTEGER_VECTOR, REAL_VECTOR, TIME_VECTOR (2008)
    add(2008, "t", "INTEGER_VECTOR", "", element="INTEGER", index="NATURAL")
    relational("INTEGER_VECTOR", 2008)
    concatenations("INTEGER_VECTOR", "INTEGER", 2008)
    minmax("INTEGER_VECTOR")
    minmax_element("INTEGER_VECTOR", "INTEGER")
    for vector, element in (("REAL_VECTOR", "REAL"), ("TIME_VECTOR", "TIME")):
        add(2008, "t", vector, "", element=element, index="NATURAL")
        for op in ("=", "/="):
            add(2008, "f", '"%s"' % op, "BOOLEAN", anon(vector, vector))
        concatenations(vector, element, 2008)
        minmax_element(vector, element)
    # the file types
    enumeration("FILE_OPEN_KIND", since=1993)
    enumeration("FILE_OPEN_STATUS", since=1993)
    enumeration("FILE_OPEN_STATE", since=2019)
    enumeration("FILE_ORIGIN_KIND", since=2019)
    add(1993, "a", "FOREIGN", "STRING")
    # TO_STRING
    for t in ("BOOLEAN", "BIT", "CHARACTER", "SEVERITY_LEVEL",
              "universal_integer", "universal_real", "INTEGER", "REAL", "TIME",
              "STRING", "BOOLEAN_VECTOR", "INTEGER_VECTOR", "REAL_VECTOR",
              "TIME_VECTOR", "FILE_OPEN_KIND", "FILE_OPEN_STATUS"):
        add(2008, "f", "TO_STRING", "STRING", [("VALUE", "c", t)])
    add(2008, "f", "TO_STRING", "STRING",
        [("VALUE", "c", "REAL"), ("DIGITS", "c", "NATURAL")])
    add(2008, "f", "TO_STRING", "STRING",
        [("VALUE", "c", "REAL"), ("FORMAT", "c", "STRING")])
    add(2008, "f", "TO_STRING", "STRING",
        [("VALUE", "c", "TIME"), ("UNIT", "c", "TIME")])


def build_once():
    if not rows:
        build()


def render():
    build_once()
    # NOW is declared twice above: the 1987 form returns TIME, the 1993 form
    # (an impure function returning DELAY_LENGTH) replaces it from 1993 on.
    out = [
        "// SPDX-License-Identifier: Apache-2.0",
        "// Generated by scripts/gen_vhdl_standard_catalog.py from IEEE 1076-2019 16.3;",
        "// do not edit (ctest fsim.vhdl-standard-catalog regenerates and compares).",
        "// The rows of fsim::semantic::VhdlStandardCatalogEntry",
        "// (include/fsim/semantic/vhdl_standard_catalog.hpp), included inside the",
        "// initializer of the table in src/semantic/vhdl_standard_catalog.cpp.",
    ]
    for since, kind, name, result, params, element, index in rows:
        out.append('{ %d, \'%s\', "%s", "%s", "%s", "%s", "%s" },' % (
            since, kind, name.replace('"', '\\"'), result, params, element,
            index))
    return "\n".join(out) + "\n"


OPERATOR_WORDS = {
    "=": "eq", "/=": "ne", "<": "lt", "<=": "le", ">": "gt", ">=": "ge",
    "+": "add", "-": "sub", "*": "mul", "/": "div", "**": "pow", "&": "cat",
    "?=": "qeq", "?/=": "qne", "?<": "qlt", "?<=": "qle", "?>": "qgt",
    "?>=": "qge", "??": "qq",
}


def parameter_types(params):
    return [item.split("/")[2].lower() for item in params.split(",")] if params else []


def declaration_names():
    """One unique lower-case name per row, and the `until` of each row."""
    build_once()
    bases = []
    for since, kind, name, result, params, element, index in rows:
        if kind == "t":
            base = name.lower()
        elif kind == "r":
            base = name.lower() + "_range_record"
        elif kind == "a":
            base = "attribute_" + name.lower()
        else:
            if name.startswith('"'):
                designator = name[1:-1]
                designator = OPERATOR_WORDS.get(designator, designator)
                base = "op_" + designator
            else:
                base = name.lower()
            base = "_".join([base] + parameter_types(params))
        bases.append(base)
    groups = {}
    for number, base in enumerate(bases):
        groups.setdefault(base, []).append(number)
    names = list(bases)
    until = [0] * len(rows)
    for base, members in groups.items():
        if len(members) == 1:
            continue
        # the same kind, designator and parameters: a later row replaces an
        # earlier one, so the rows differ in since (a repeat is an error)
        sinces = [rows[m][0] for m in members]
        assert len(set(sinces)) == len(sinces), "duplicate declaration " + base
        ordered = sorted(members, key=lambda m: rows[m][0])
        for earlier, later in zip(ordered, ordered[1:]):
            until[earlier] = rows[later][0]
        for m in members:
            names[m] = base + "_" + (rows[m][3].lower() or "none")
    assert len(set(names)) == len(names), "declaration names are not unique"
    return names, until


def render_declarations():
    names, until = declaration_names()
    out = [
        "// SPDX-License-Identifier: Apache-2.0",
        "// Generated by scripts/gen_vhdl_standard_catalog.py from IEEE 1076-2019 16.3;",
        "// do not edit (ctest fsim.vhdl-standard-catalog regenerates and compares).",
        "// X(name, row, since, until): the row of vhdl_standard_catalog.inc, the first",
        "// standard that has the declaration and the first that no longer has it (0:",
        "// none). Included by include/fsim/support/vhdl_standard_declaration.hpp.",
    ]
    for number, row in enumerate(rows):
        out.append("X(%s, %d, %d, %d)" % (names[number], number, row[0], until[number]))
    return "\n".join(out) + "\n"


def counts():
    build()
    for revision in REVISIONS:
        selected = [r for r in rows if r[0] <= revision]
        # a 1987 NOW is replaced by the 1993 one
        if revision >= 1993:
            selected = [r for r in selected
                        if not (r[2] == "NOW" and r[3] == "TIME")]
        by_kind = {}
        for r in selected:
            by_kind[r[1]] = by_kind.get(r[1], 0) + 1
        print(revision, len(selected), sorted(by_kind.items()))


def main(argv):
    if len(argv) == 2 and argv[1] == "--counts":
        counts()
        return 0
    check = len(argv) >= 2 and argv[1] == "--check"
    paths = argv[2:] if check else argv[1:]
    if 1 <= len(paths) <= 2 and not paths[0].startswith("--"):
        if len(paths) == 1:
            paths.append(os.path.join(os.path.dirname(paths[0]), "..", "support",
                                      "vhdl_standard_declaration.inc"))
        outputs = ((paths[0], render()), (paths[1], render_declarations()))
        if not check:
            for path, text in outputs:
                with open(path, "w", encoding="utf-8") as stream:
                    stream.write(text)
            return 0
        status = 0
        for path, text in outputs:
            with open(path, encoding="utf-8") as stream:
                current = stream.read()
            if current != text:
                sys.stderr.write("%s is out of date; run %s %s\n" % (
                    path, argv[0], paths[0]))
                status = 1
        return status
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
