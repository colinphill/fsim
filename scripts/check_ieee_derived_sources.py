# SPDX-License-Identifier: Apache-2.0
"""Checks of the derived per-standard IEEE package sources.

  sources  ROOT TABLE REWRITES DERIVED
                                  byte reproduction, that every change is
                                  inside a declared removal or rewrite row,
                                  the checksums, no names of later standards
                                  in the 87/93/02 texts, and that the checker
                                  catches added, changed and unlisted-removed
                                  lines
  analysis FSIM FINDINGS DERIVED WORK
                                  every derived file is analyzed by fsim under
                                  its own standard; the pinned findings are the
                                  only failures
  names    TOOL_OUTPUT DERIVED DIFFERENCES REVISIONS
                                  the names each derived package declares are
                                  the names fsim's projection gives that
                                  standard, except the pinned differences
"""

import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import derive_ieee_packages as derive  # noqa: E402


def read(path):
    return Path(path).read_bytes().decode("latin-1")


# Predefined names and types that first appear in IEEE Std 1076-2008 (5.2.6,
# 5.3.2.2, 5.7): a derived file of an older standard must not use them. The
# analysis of fsim does not resolve names inside subprogram bodies, so this
# token check is the strict test of the derived texts for these names.
LATER_NAMES = ("maximum", "minimum", "to_string", "to_hstring", "to_ostring",
               "boolean_vector", "integer_vector", "time_vector")


def later_name_problems(text):
    found = set()
    for kind, word, _, _ in derive.lex(text):
        if kind == "w":
            low = word.lower()
            if low in LATER_NAMES or low.startswith("unresolved_"):
                found.add(low)
    return sorted(found)


def check_sources(root, table_path, rewrites_path, derived):
    table = derive.load_table(table_path)
    rewrites = derive.load_rewrites(rewrites_path)
    problems = []
    used_rows = set()
    for standard in derive.STANDARDS:
        files, unmatched, extra = derive.derive(root, table, rewrites,
                                                standard)
        for row in unmatched:
            problems.append("table line %d matches nothing (%s %s)" %
                            (row.line, row.standard, row.package))
        for package in sorted(extra):
            problems.append("rows for %s package %s, which that standard "
                            "does not have" % (standard, package))
        tree = Path(derived) / standard / "ieee"
        present = sorted(p.name for p in tree.glob("*.vhdl"))
        if present != sorted(files):
            problems.append("%s: files %s differ from the generator's %s" %
                            (standard, present, sorted(files)))
        for name, data in files.items():
            on_disk = (tree / name).read_bytes() if (tree / name).exists() \
                else None
            if on_disk != data:
                problems.append("%s/%s is not the generator output" %
                                (standard, name))
                continue
            package = name.replace("-body", "")[:-len(".vhdl")]
            for problem in derive.check_traceability(
                    root, table, rewrites, standard, package, name, data):
                problems.append("%s/%s: %s" % (standard, name, problem))
            if standard != "2008":
                later = later_name_problems(derive.split_header(data)[1])
                if later:
                    problems.append("%s/%s uses names of later standards: %s" %
                                    (standard, name, later))
        sums = Path(derived) / standard / "SHA256SUMS"
        if sums.read_bytes() != derive.sums_for(files).encode():
            problems.append("%s/SHA256SUMS is stale" % standard)
        readme = Path(derived) / standard / "README.md"
        if readme.read_bytes() != derive.readme_for(standard, files).encode():
            problems.append("%s/README.md is stale" % standard)
    # The checker must catch each kind of tampering.
    files, _, _ = derive.derive(root, table, rewrites, "1993")
    name = "std_logic_1164.vhdl"
    original = files[name]
    header, body = derive.split_header(original)
    lines = body.split("\n")
    tampered = {
        "an added line": "\n".join(lines[:10] + ["  -- added"] + lines[10:]),
        "a changed line": "\n".join(
            lines[:10] + [lines[10] + " "] + lines[11:]),
        "an unlisted removal": "\n".join(lines[:10] + lines[11:]),
    }
    for what, text in tampered.items():
        found = derive.check_traceability(
            root, table, rewrites, "1993", "std_logic_1164", name,
            (header + text).encode("latin-1"))
        if not found:
            problems.append("the traceability check missed " + what)
    if derive.check_traceability(root, table, rewrites, "1993",
                                 "std_logic_1164", name, original):
        problems.append("the traceability check rejects the generator output")
    if later_name_problems("x := MAXIMUM(1, 2);") != ["maximum"]:
        problems.append("the later-standard name check does not work")
    return problems


FINDING = re.compile(r"error\[([A-Z0-9-]+)\]")
DEPENDENCIES = {"math_complex": ["math_real"]}


def check_analysis(fsim, findings_path, derived, work):
    expected = {}
    for line in Path(findings_path).read_text().split("\n"):
        if line and not line.startswith("#"):
            standard, package, code, note = line.split("\t")
            expected[(standard, package)] = code
    problems = []
    seen = set()
    for standard in derive.STANDARDS:
        tree = Path(derived) / standard / "ieee"
        packages = sorted({p.name.replace("-body", "")[:-len(".vhdl")]
                           for p in tree.glob("*.vhdl")})
        for package in packages:
            files = []
            for name in DEPENDENCIES.get(package, []) + [package]:
                files.append(tree / (name + ".vhdl"))
                if (tree / (name + "-body.vhdl")).exists():
                    files.append(tree / (name + "-body.vhdl"))
            workdir = Path(work)
            workdir.mkdir(parents=True, exist_ok=True)
            result = subprocess.run(
                [fsim, "check", "--standard", standard] + [str(f) for f in files],
                capture_output=True, text=True, cwd=workdir, timeout=600)
            output = result.stdout + result.stderr
            codes = FINDING.findall(output)
            key = (standard, package)
            if result.returncode == 0:
                if key in expected:
                    problems.append("%s %s now analyzes; drop its finding" %
                                    key)
            else:
                seen.add(key)
                if key not in expected:
                    problems.append("%s %s fails analysis: %s" % (
                        key + (output.strip().split("\n")[0],)))
                elif not codes or codes[0] != expected[key]:
                    problems.append("%s %s fails with %s, pinned %s" % (
                        key + (codes[:1], expected[key])))
    for key in expected:
        if key not in seen and key not in [
                (s, p) for s in derive.STANDARDS for p in derive.PACKAGES[s]]:
            problems.append("pinned finding for unknown file %s" % (key,))
    return problems


def normalize_name(name):
    return name.strip('"').lower()


LEVELS = {"1987": 0, "1993": 1, "2002": 1, "2008": 2, "2019": 3}
REVISION_LEVELS = {"1987": 0, "1993": 1, "2008": 2, "2019": 3}


def overload_of_key(key):
    match = re.match(r'(?:function|procedure)\|("[^"]+"|[^(]+)\((.*?)\)(?:->.*)?$',
                     key)
    if not match:
        return None
    name = match.group(1).strip('"')
    types = match.group(2).replace("unresolved_", "")
    return "%s(%s)" % (name, types.replace(" ", ""))


def revision_differences(derived, revisions_path):
    """Per standard and package, the subprogram overloads that the derived
    declaration file has and the first-revision table
    (scripts/vhdl_intrinsic_revisions.tsv) does not allow, and the reverse."""
    table = {}
    for line in Path(revisions_path).read_text().split("\n"):
        if line and not line.startswith("#"):
            package, name, types, revision = line.split("\t")
            table.setdefault(package, []).append(
                ("%s(%s)" % (name, types.replace(" ", "")),
                 REVISION_LEVELS[revision]))
    result = {}
    for standard in ("1987", "1993", "2002", "2008"):
        for package, entries in table.items():
            path = Path(derived) / standard / "ieee" / (package + ".vhdl")
            if not path.exists():
                continue
            ours = set()
            for item in derive.split_items(read(path)):
                overload = overload_of_key(item.key)
                if overload is not None:
                    ours.add(overload.replace("std_ulogic", "std_logic"))
            theirs = {name.replace("std_ulogic", "std_logic")
                      for name, level in entries if level <= LEVELS[standard]}
            if ours - theirs:
                result[(standard, package, "derived-only-vs-revisions")] = \
                    ours - theirs
            if theirs - ours:
                result[(standard, package, "revisions-only")] = theirs - ours
    return result


def name_differences(tool_output, derived, revisions_path):
    """({(standard, package, side): names}, problems)."""
    fsim_names = {}
    for line in Path(tool_output).read_text().split("\n"):
        if not line:
            continue
        fields = line.split("\t")
        if fields[0] == "UNAVAILABLE":
            fsim_names.setdefault((fields[1], fields[2]), None)
        else:
            current = fsim_names.get((fields[1], fields[2])) or set()
            fsim_names[(fields[1], fields[2])] = current | set(fields[3:])
    packages = ["std_logic_1164", "std_logic_textio", "numeric_bit",
                "numeric_std", "math_real", "math_complex",
                "numeric_bit_unsigned", "numeric_std_unsigned",
                "fixed_float_types", "fixed_generic_pkg", "fixed_pkg",
                "float_generic_pkg", "float_pkg"]
    problems = []
    found = {}
    for standard in ("1987", "1993", "2002", "2008", "2019"):
        for package in packages:
            if standard == "2019":
                path = Path(derived).parent / "ieee-1076-2019" / "ieee" / (
                    package + ".vhdl")
            else:
                path = Path(derived) / standard / "ieee" / (package + ".vhdl")
            theirs = fsim_names.get((standard, package), "missing")
            if theirs == "missing":
                problems.append("the tool printed nothing for %s %s" %
                                (standard, package))
                continue
            has_file = path.exists()
            if not has_file and theirs is None:
                continue
            ours = {normalize_name(n) for n in derive.declared_names(
                read(path))} if has_file else set()
            ours.discard("")
            if theirs is None:
                found[(standard, package, "derived-only")] = ours
                continue
            if package in ("fixed_pkg", "float_pkg"):
                # Instantiations of the generic packages: the declarations
                # are those of the generic package (IEEE 1076-2019 4.7).
                generic = path.with_name(package.replace("_pkg", "_generic_pkg")
                                         + ".vhdl")
                ours = {normalize_name(n) for n in derive.declared_names(
                    read(generic))}
                ours.discard("")
            only_ours = ours - theirs
            only_theirs = theirs - ours
            if only_ours:
                found[(standard, package, "derived-only")] = only_ours
            if only_theirs:
                found[(standard, package, "fsim-only")] = only_theirs
    found.update(revision_differences(derived, revisions_path))
    return found, problems


def check_names(tool_output, derived, differences_path, revisions_path):
    found, problems = name_differences(tool_output, derived, revisions_path)
    pinned = {}
    for line in Path(differences_path).read_text().split("\n"):
        if line and not line.startswith("#"):
            standard, package, side, names, klass, note = line.split("\t")
            if klass not in ("fsim projection bug", "derivation limitation") \
                    or not note.strip():
                problems.append("bad class or empty finding: %s" % line[:60])
            pinned[(standard, package, side)] = set(names.split(";;"))
    for key, names in found.items():
        if pinned.get(key) != names:
            problems.append("%s: differences %s, pinned %s" % (
                key, sorted(names)[:8], sorted(pinned.get(key, []))[:8]))
    for key in pinned:
        if key not in found:
            problems.append("pinned difference %s no longer exists" % (key,))
    return problems


def main():
    mode = sys.argv[1]
    if mode == "sources":
        problems = check_sources(*sys.argv[2:6])
    elif mode == "analysis":
        problems = check_analysis(*sys.argv[2:6])
    elif mode == "names":
        problems = check_names(*sys.argv[2:6])
    else:
        problems = ["unknown mode " + mode]
    for problem in problems:
        print("FAIL:", problem)
    if not problems:
        print("ieee derived %s check: ok" % mode)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
