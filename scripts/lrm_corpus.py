#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run public HDL language-compliance corpora against fsim.

The corpora live outside the repository (by default under
.local-artifacts/lrm-corpora/sources) and are used unmodified. Each suite
adapter reproduces the suite's own pass/fail convention:

  sv-tests          chipsalliance/sv-tests: exit status vs :should_fail_because:,
                    plus every ':assert:' line printed by simulation tests
  verilator         Verilator test_regress 'simulator' scenario tests: a
                    generated clock shell drives module t; success requires
                    '*-* All Finished *-*'
  ivtest            Icarus ivtest regress lists: PASSED line or gold output;
                    CE/RE types expect compile/runtime errors
  vests             GHDL's VESTs (VHDL-93): Billowitch, Ashenden and Clifton
                    .exp scripts; no error-severity assertion may fire
  nvc               nvc test/regress testlist: normal/fail/gold tests
  vhdl-compliance   VHDL/Compliance-Tests (VUnit testbenches) run against the
                    VUnit-compatible library in scripts/lrm_corpus/

Only simulator-specific hooks are supplied by this runner: top-level selection,
generic overrides, plusargs, the Verilator clock shell, and the VUnit library
stand-in. Results are JSON lines plus a Markdown summary that ranks failure
causes by diagnostic code and LRM clause.
"""

from __future__ import annotations

import argparse
import ast
import collections
import concurrent.futures
import dataclasses
import datetime
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable, Iterable

REPO = Path(__file__).resolve().parent.parent
DEFAULT_ROOT = REPO / ".local-artifacts" / "lrm-corpora"
DEFAULT_FSIM = REPO / "build" / "batch188-release-clang22-final" / "fsim"
SHIM_DIR = Path(__file__).resolve().parent / "lrm_corpus"

SUITES = ("sv-tests", "verilator", "ivtest", "vests", "nvc", "vhdl-compliance")

DIAGNOSTIC_RE = re.compile(r"\b(error|fatal|failure)\[([A-Z0-9-]+)\]: ?(.*)")
HDL_ERROR_RE = re.compile(
    r"\b(error|failure|fatal)\[(FSIM-HDL-REPORT|FSIM-RUN-ASSERT-[0-9]+)\]")
TRAILER_RE = re.compile(r"^simulation (completed|stopped|finished)\b.*\bat tick\b")


@dataclasses.dataclass
class Command:
    """One fsim invocation. `stage` is compile, elaborate or simulate."""

    stage: str
    args: list[str]


@dataclasses.dataclass
class Case:
    suite: str
    id: str
    group: str
    commands: list[Command]
    expect: str = "pass"  # pass | fail
    # Last stage at which an expected failure may occur.
    fail_by: str = "simulate"
    checker: str = "clean"
    checker_data: dict = dataclasses.field(default_factory=dict)
    copies: list[tuple[str, str]] = dataclasses.field(default_factory=list)
    writes: list[tuple[str, str]] = dataclasses.field(default_factory=list)
    env: dict = dataclasses.field(default_factory=dict)
    # Cases sharing a chain run in order in one workspace (VESTs Ashenden).
    chain: str | None = None
    skip: str | None = None
    timeout: float | None = None


STAGE_ORDER = {"compile": 0, "elaborate": 1, "simulate": 2}


# ---------------------------------------------------------------------------
# Source scanning helpers

SV_MODULE_RE = re.compile(
    r"^\s*(?:extern\s+)?(?:module|macromodule|program)\s+(?:automatic\s+|static\s+)?"
    r"([A-Za-z_][A-Za-z0-9_$]*)", re.M)
SV_COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
SV_STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def sv_root_modules(texts: Iterable[str]) -> list[str]:
    """Return modules that no other scanned module instantiates."""
    joined = "\n".join(SV_STRING_RE.sub('""', SV_COMMENT_RE.sub(" ", t))
                       for t in texts)
    declared: list[str] = []
    for name in SV_MODULE_RE.findall(joined):
        if name not in declared:
            declared.append(name)
    roots = []
    for name in declared:
        pattern = re.compile(
            r"(?<![A-Za-z0-9_$.`'])" + re.escape(name)
            + r"\s*(?:#\s*\(|[A-Za-z_\\][A-Za-z0-9_$]*\s*(?:\[[^\]]*\]\s*)*\()")
        instantiated = False
        for match in pattern.finditer(joined):
            prefix = joined[max(0, match.start() - 40):match.start()]
            if re.search(r"(module|macromodule|program|interface|class|"
                         r"function|task|typedef|endmodule)\s*$", prefix):
                continue
            instantiated = True
            break
        if not instantiated:
            roots.append(name)
    if not roots and declared:
        roots = [declared[-1]]
    return roots


VHDL_COMMENT_RE = re.compile(r"--[^\n]*")
VHDL_ENTITY_RE = re.compile(r"^\s*entity\s+(\\[^\\]+\\|\w+)\s+is\b", re.I | re.M)
VHDL_ARCH_RE = re.compile(
    r"^\s*architecture\s+(\w+)\s+of\s+(\w+)\s+is\b", re.I | re.M)
VHDL_CONFIG_RE = re.compile(
    r"^\s*configuration\s+(\w+)\s+of\s+(\w+)\s+is\b", re.I | re.M)


def vhdl_units(text: str):
    clean = VHDL_COMMENT_RE.sub("", text)
    entities = [e.lower() for e in VHDL_ENTITY_RE.findall(clean)]
    architectures = [(a.lower(), e.lower()) for a, e in VHDL_ARCH_RE.findall(clean)]
    configurations = [(c.lower(), e.lower()) for c, e in VHDL_CONFIG_RE.findall(clean)]
    return entities, architectures, configurations


def vhdl_top_spec(entity: str, texts: Iterable[str]) -> str:
    """Name a VHDL top as entity(architecture) using the last-analyzed body."""
    entity = entity.lower()
    architecture = None
    for text in texts:
        for arch, owner in vhdl_units(text)[1]:
            if owner == entity:
                architecture = arch
    if architecture is None:
        return entity
    return f"{entity}({architecture})"


def vhdl_root_entities(texts: list[str]) -> list[str]:
    entities: list[str] = []
    for text in texts:
        for name in vhdl_units(text)[0]:
            if name not in entities:
                entities.append(name)
    joined = "\n".join(VHDL_COMMENT_RE.sub("", t) for t in texts).lower()
    roots = []
    for name in entities:
        instantiated = re.search(
            r":\s*(?:entity\s+(?:\w+\.)?" + re.escape(name) + r"\b|(?:component\s+)?"
            + re.escape(name) + r"\s+(?:generic|port)\s+map\b)", joined)
        if not instantiated:
            roots.append(name)
    return roots or entities[-1:]


# ---------------------------------------------------------------------------
# Suite adapters


def standard_args(language: str, standard: str | None) -> list[str]:
    args = ["--lang", language]
    if standard:
        args += ["--standard", standard]
    return args


def sv_tests_cases(root: Path) -> list[Case]:
    base = root / "sources" / "sv-tests"
    tests_dir = base / "tests"
    files = sorted(tests_dir.glob("chapter-*/*.sv"))
    files += sorted((tests_dir / "generic").glob("**/*.sv"))
    files += sorted((tests_dir / "testbenches").glob("*.sv"))
    generated = root / "generated" / "sv-tests" / "generated"
    files += sorted(generated.glob("**/*.sv"))
    cases = []
    for path in files:
        text = read_text(path)
        params: dict[str, str] = {}
        for line in text.splitlines():
            match = re.match(r"^:([a-zA-Z_-]+):\s*(.+)", line)
            if match:
                params.setdefault(match.group(1).lower(), match.group(2).strip())
        if not params:
            continue
        test_dir = path.parent
        relative = path.relative_to(tests_dir) if path.is_relative_to(tests_dir) \
            else Path("generated") / path.relative_to(generated)
        types = params.get("type", "parsing elaboration").split()
        if "simulation" in types:
            mode = "simulate"
        elif "elaboration" in types or "simulation_without_run" in types:
            mode = "elaborate"
        elif "parsing" in types:
            mode = "compile"
        else:
            mode = "compile"
        tags = params.get("tags", "").split()
        group = tags[0] if tags else "untagged"
        source_files = [
            str((tests_dir / f).resolve()) if not Path(f).is_absolute() else f
            for f in params.get("files", "").split()
        ] or [str(path)]
        # Generated tests name files relative to the sv-tests tests directory.
        source_files = [f if Path(f).exists() else str(path) for f in source_files]
        incdirs = [str((tests_dir / d).resolve()) for d in params.get("incdirs", "").split()]
        incdirs.append(str(test_dir))
        defines = params.get("defines", "").split()
        compile_args = ["compile", "-q"] + standard_args("systemverilog", "2017")
        for incdir in incdirs:
            compile_args += ["-I", incdir]
        for define in defines:
            compile_args += ["-D", define]
        timeout = float(params.get("timeout", "30")) + 30
        if "uvm" in tags or "uvm-1.2" in tags:
            # sv-tests compiles the UVM library sources with each UVM test; use the
            # Accellera release trees governed by docs/uvm-source-provenance.md.
            release = "1.2" if "uvm-1.2" in tags else "2020.3.1"
            uvm_src = root / "uvm" / f"uvm-{release}" / "src"
            compile_args += ["--compilation-unit", "source-set", "--uvm-release", release,
                             "-I", str(uvm_src)]
            source_files = [str(uvm_src / "uvm_pkg.sv")] + source_files
            timeout = max(timeout, 300.0)
        compile_args += source_files
        commands = [Command("compile", compile_args)]
        skip = None
        if params.get("compatible-runners", "all") not in ("all",) and \
                "all" not in params.get("compatible-runners", "").split():
            skip = "test restricted to runners " + params["compatible-runners"]
        top = params.get("top_module", "").strip()
        tops = [top] if top else sv_root_modules(
            read_text(Path(f)) for f in source_files if not f.endswith("uvm_pkg.sv"))
        if not tops:
            # A compilation unit without design units has nothing to elaborate.
            mode = "compile"
        if mode in ("elaborate", "simulate"):
            elab = ["elaborate", "-q", "--no-aot"]
            for top_name in tops:
                elab += ["--top", top_name]
            commands.append(Command("elaborate", elab))
        if mode == "simulate":
            variants = params.get("simvariants", "").split()
            commands.append(Command("simulate", ["simulate", "--engine", "{engine}"]
                                    + (variants[:1] if variants else [])))
        cases.append(Case(
            suite="sv-tests",
            id=str(relative),
            group=group,
            commands=commands,
            expect="fail" if "should_fail_because" in params else "pass",
            fail_by=mode,
            checker="sv_tests",
            skip=skip,
            timeout=timeout,
        ))
    return cases


VERILATOR_SHELL = """module top;
{decls}
    t t (
{ports}
    );
    initial begin
{init0}
        #10;
{init1}
        while ($time < 1100) begin
{body}
        end
    end
endmodule
"""


def verilator_shell(top_text: str) -> str:
    inputs: list[str] = []
    get_sigs = True
    for line in top_text.splitlines():
        if re.match(r"^\s*module\s+t\b", line):
            inputs = []
            get_sigs = True
        if get_sigs:
            match = re.match(r"^\s*input\s*(logic|bit|reg|wire)?\s*([A-Za-z0-9_]+)", line)
            if match and match.group(2) not in inputs:
                inputs.append(match.group(2))
            if re.match(r"^\s*(function|task|endmodule)", line):
                get_sigs = False
    inputs.sort()
    decls = "\n".join(f"    reg {name};" for name in inputs)
    ports = "\n".join(("      " + ("," if i else "") + f".{name} ({name})")
                      for i, name in enumerate(inputs))
    init0 = "".join(f"        {n} = 0;\n" for n in ("fastclk", "clk") if n in inputs)
    init1 = "".join(f"        {n} = 1;\n" for n in ("fastclk", "clk") if n in inputs)
    body = ""
    for step in range(6):
        body += "          #1;\n"
        if "fastclk" in inputs:
            body += "          fastclk = !fastclk;\n"
        if step == 4 and "clk" in inputs:
            body += "          clk = !clk;\n"
    return VERILATOR_SHELL.format(decls=decls, ports=ports, init0=init0.rstrip("\n"),
                                  init1=init1.rstrip("\n"), body=body.rstrip("\n"))


def python_literal_list(source: str, keyword: str) -> list[str] | None:
    match = re.search(keyword + r"\s*=\s*(\[[^\]]*\])", source, re.S)
    if not match:
        return None
    try:
        value = ast.literal_eval(match.group(1))
    except (SyntaxError, ValueError):
        return None
    return [str(v) for v in value]


def verilator_cases(root: Path) -> list[Case]:
    tdir = root / "sources" / "verilator" / "test_regress" / "t"
    cases = []
    for driver in sorted(tdir.glob("t_*.py")):
        source = read_text(driver)
        scenario = re.search(r"test\.scenarios\(([^)]*)\)", source)
        if not scenario or not re.search(r"['\"]simulator(_st)?['\"]", scenario.group(1)):
            continue
        name = driver.stem
        top_match = re.search(r"test\.top_filename\s*=\s*['\"]([^'\"]+)['\"]", source)
        top_file = tdir / f"{name}.v"
        if top_match:
            candidate = top_match.group(1)
            top_file = (tdir.parent / candidate) if candidate.startswith("t/") \
                else tdir / Path(candidate).name
        skip = None
        if not top_file.exists():
            skip = "top file not present"
        if re.search(r"\.(cpp|c|sv)['\"]", source) and "--exe" in source:
            skip = "requires a C++ harness"
        if "test.lint(" in source and "test.execute(" not in source:
            skip = skip or "lint-only test"
        if re.search(r"have_solver|have_sc\b|test\.pli_filename|vpi|dpi|sc_main", source, re.I):
            skip = skip or "requires solver, SystemC, PLI/VPI or DPI harness"
        compile_fails = bool(re.search(r"test\.compile\([^)]*fails\s*=\s*True", source, re.S))
        execute_fails = bool(re.search(r"test\.execute\([^)]*fails\s*=\s*True", source, re.S))
        flags: list[str] = []
        for keyword in ("v_flags2", "v_flags"):
            values = python_literal_list(source, keyword)
            if values:
                flags += values
        # Verilator-only flags still carry the defines and include paths
        # that select the test variant.
        for keyword in ("verilator_flags2", "verilator_flags"):
            for value in python_literal_list(source, keyword) or []:
                flags += [t for t in value.split()
                          if t.startswith(("+define+", "-D", "+incdir+", "-I"))]
        compile_args = ["compile", "-q"] + standard_args("systemverilog", "2017")
        compile_args += ["-I", str(tdir), "-D", "SIMULATOR", "-D", "TEST_OBJ_DIR=obj_dir"]
        plusargs: list[str] = []
        for flag in flags:
            for token in flag.split():
                if token.startswith("+define+"):
                    for definition in token[len("+define+"):].split("+"):
                        if definition:
                            compile_args += ["-D", definition]
                elif token.startswith("-D"):
                    compile_args += ["-D", token[2:]]
                elif token.startswith("+incdir+"):
                    compile_args += ["-I", str(tdir / token[len("+incdir+"):])]
                elif token.startswith("-I"):
                    compile_args += ["-I", str(tdir / token[2:])]
                elif token.startswith("+") and not token.startswith("+libext"):
                    plusargs.append(token)
        all_run_flags = python_literal_list(source, "all_run_flags") or []
        plusargs += [f for f in all_run_flags if f.startswith("+")]
        top_text = read_text(top_file)
        declared = SV_MODULE_RE.findall(SV_COMMENT_RE.sub(" ", top_text))
        # The driver's shell wraps module t; tests with their own top or no
        # module t run as written.
        use_shell = "t" in declared and "top" not in declared \
            and not re.search(r"make_top_shell\s*=\s*False", source)
        compile_args.append(str(top_file))
        if use_shell:
            compile_args.append("verilator_shell.sv")
            tops = ["top"]
        else:
            tops = sv_root_modules([top_text])
        shell = verilator_shell(top_text) if use_shell else ""
        commands = [Command("compile", compile_args)]
        if not compile_fails:
            elab = ["elaborate", "-q", "--no-aot"]
            for top in tops:
                elab += ["--top", top]
            commands.append(Command("elaborate", elab))
            commands.append(Command("simulate", ["simulate", "--engine", "{engine}"] + plusargs))
        cases.append(Case(
            suite="verilator",
            id=name,
            group=verilator_group(name),
            commands=commands,
            expect="fail" if (compile_fails or execute_fails) else "pass",
            fail_by="compile" if compile_fails else "simulate",
            checker="verilator",
            checker_data={"check_finished": not (compile_fails or execute_fails)},
            writes=[("verilator_shell.sv", shell)] if shell else [],
            skip=skip,
        ))
    return cases


def verilator_group(name: str) -> str:
    parts = name.split("_")
    return parts[1] if len(parts) > 1 else name


def ivtest_cases(root: Path) -> list[Case]:
    base = root / "sources" / "iverilog" / "ivtest"
    entries: dict[str, list[str]] = {}
    order: list[str] = []
    for list_name in ("regress-ivl1.list", "regress-vlg.list", "regress-sv.list"):
        content = read_text(base / list_name)
        content = re.sub(r"\\\n\s*", "", content)
        for line in content.splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split()
            if len(fields) < 2 or ":" in fields[0]:
                continue
            if fields[0] in entries:
                continue
            entries[fields[0]] = fields
            order.append(fields[0])
    cases = []
    for name in order:
        fields = entries[name]
        type_field = fields[1].split(",")
        test_type, args = type_field[0], [a for a in type_field[1:] if a]
        directory = fields[2] if len(fields) > 2 else ""
        gold = None
        module = None
        if len(fields) > 3:
            extra = fields[3]
            if extra.startswith("gold="):
                gold = base / "gold" / extra[5:]
            elif extra.startswith("unordered="):
                gold = base / "gold" / extra[10:]
            elif not extra.startswith("diff="):
                module = extra
        source = base / directory / f"{name}.v"
        language, standard = "verilog", "2005"
        compile_args = ["compile", "-q"]
        plusargs = []
        skip = None
        for arg in args:
            if arg in ("-g2005-sv", "-g2009", "-g2012"):
                language = "systemverilog"
                standard = {"-g2005-sv": "2005", "-g2009": "2009", "-g2012": "2012"}[arg]
            elif arg == "-g2001":
                standard = "2001"
            elif arg == "-g1995":
                standard = "1995"
            elif arg.startswith("-D"):
                compile_args += ["-D", arg[2:]]
            elif arg.startswith("-I"):
                compile_args += ["-I", str(base / arg[2:])]
            elif arg.startswith("+"):
                plusargs.append(arg)
            elif arg.startswith("-g") or arg.startswith("-W") or arg.startswith("-p"):
                pass  # iverilog-specific generation or warning flags
            else:
                skip = f"iverilog option {arg}"
        if test_type in ("NI", "EF", "TE", "CN") or test_type.startswith("vlog95"):
            skip = skip or f"ivtest type {test_type}"
        if not source.exists():
            skip = skip or "source not present"
        compile_args += standard_args(language, standard)
        compile_args += ["-I", str(base / directory), str(source)]
        commands = [Command("compile", compile_args)]
        if test_type not in ("CE", "CO"):
            tops = [module] if module else sv_root_modules([read_text(source)])
            elab = ["elaborate", "-q", "--no-aot"]
            for top in tops:
                elab += ["--top", top]
            commands.append(Command("elaborate", elab))
            commands.append(Command("simulate", ["simulate", "--engine", "{engine}"] + plusargs))
        expect = "fail" if test_type in ("CE", "RE") else "pass"
        cases.append(Case(
            suite="ivtest",
            id=name,
            group=directory or "ivltests",
            commands=commands,
            expect=expect,
            fail_by="compile" if test_type == "CE" else "simulate",
            checker="ivtest",
            checker_data={"gold": str(gold) if gold else None, "type": test_type},
            skip=skip,
        ))
    return cases


def parse_exp(path: Path):
    """Yield (command, file, options) from a VESTs .exp script."""
    for raw in read_text(path).splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        command = fields[0]
        rest = fields[1:]
        options = {}
        positional = []
        for field in rest:
            if "=" in field and field.split("=", 1)[0].isupper():
                key, value = field.split("=", 1)
                options.setdefault(key, []).append(value)
            else:
                positional.append(field)
        yield command, positional, options


def vests_cases(root: Path) -> list[Case]:
    base = root / "sources" / "ghdl" / "testsuite" / "vests" / "vhdl-93"
    cases: list[Case] = []
    groups = [
        ("clifton-labs/compliant", "compliant1.exp", "93", False, "clifton"),
        ("ashenden/compliant", "compliant.exp", "93", False, "ashenden"),
        ("ashenden/non_compliant", "non_compliant.exp", "93", False, "ashenden-nc"),
        ("billowitch/compliant", "compliant.exp", "93", True, None),
        ("billowitch/non_compliant/analyzer_failure", "non_compliant.exp", "93", True, None),
        ("billowitch/non_compliant/simulator_failure", "non_compliant.exp", "93", True, None),
    ]
    for directory, exp_name, standard, use_last_entity, chain in groups:
        group_dir = base / directory
        exp = group_dir / exp_name
        libraries: list[str] = []
        for command, positional, options in parse_exp(exp):
            if command == "create_lib" and positional:
                libraries.append(positional[0])
                continue
            if command not in ("build_compliant_test", "run_compliant_test",
                               "run_non_compliant_test", "run_err_non_compliant_test"):
                continue
            if not positional:
                continue
            relative = positional[0]
            source = group_dir / relative
            mode = {
                "build_compliant_test": "compile",
                "run_compliant_test": "run",
                "run_non_compliant_test": "run_err" if "simulator_failure" in directory
                else "ana_err",
                "run_err_non_compliant_test": "run_err",
            }[command]
            library = options.get("LIBRARY", ["work"])[0]
            compile_args = ["compile", "-q"] + standard_args("vhdl", standard)
            compile_args += ["--library", library]
            for lib in libraries:
                if lib != library:
                    compile_args += ["--search-library", lib]
            local_name = source.name
            compile_args.append(local_name)
            copies = [(str(source), local_name)]
            commands = [Command("compile", compile_args)]
            text = read_text(source)
            checker_data: dict = {"outputs": []}
            if mode in ("run", "run_err"):
                entity = options.get("ENTITY", [None])[0]
                if entity is None:
                    if use_last_entity:
                        matches = re.findall(r"^ENTITY\s+(\S+(?:ent|entw|cfg))\s+IS\s*$",
                                             text, re.M | re.I)
                        entity = matches[-1] if matches else None
                    else:
                        roots = vhdl_root_entities([text])
                        entity = roots[-1] if roots else None
                if entity is None:
                    top = None
                else:
                    configs = {c: e for c, e in vhdl_units(text)[2]}
                    top = entity.lower() if entity.lower() in configs \
                        else vhdl_top_spec(entity, [text])
                elab = ["elaborate", "-q", "--no-aot"]
                for lib in libraries:
                    elab += ["--search-library", lib]
                if top:
                    elab += ["--top", top]
                commands.append(Command("elaborate", elab))
                stop = options.get("STOP", [None])[0]
                sim = ["simulate", "--engine", "{engine}", "--file-root", "."]
                if stop:
                    sim += ["--duration", stop]
                commands.append(Command("simulate", sim))
                for item in options.get("INPUT", []):
                    name, _, origin = item.partition(":")
                    copies.append((str(group_dir / origin), name))
                for item in options.get("OUTPUT", []):
                    name, _, expected = item.partition(":")
                    checker_data["outputs"].append((name, str(group_dir / expected)))
            expect = "pass" if mode in ("compile", "run") else "fail"
            cases.append(Case(
                suite="vests",
                id=f"{directory}/{relative}",
                group=vests_group(directory, relative),
                commands=commands,
                expect=expect,
                fail_by="compile" if mode == "ana_err" else "simulate",
                checker="vhdl_clean",
                checker_data=checker_data,
                copies=copies,
                chain=f"vests:{chain}" if chain else None,
            ))
    return cases


def vests_group(directory: str, relative: str) -> str:
    if directory.startswith("billowitch"):
        return directory.split("/")[-1]
    if directory.startswith("clifton"):
        return "clifton/" + relative.split("/")[1] if "/" in relative else "clifton"
    match = re.match(r"(ch_\d+|ap_\w)", relative)
    return "ashenden/" + (match.group(1) if match else "misc")


NVC_SKIP_FLAGS = {
    "vhpi", "wave", "cover", "tcl", "shell", "plugin", "gtkw", "relaxed", "relax",
    "dump-arrays", "export", "define", "H", "exit", "mixed",
}


def nvc_cases(root: Path) -> list[Case]:
    base = root / "sources" / "nvc" / "test" / "regress"
    cases = []
    for raw in read_text(base / "testlist.txt").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        name = fields[0]
        flags = fields[1].split(",") if len(fields) > 1 else []
        standard = "93"
        generics: list[str] = []
        stop_time = None
        skip = None
        verilog = False
        kinds = set()
        for flag in flags:
            key, _, value = flag.partition("=")
            if key in ("2000", "2002", "2008", "2019", "1993"):
                standard = key[-2:] if key != "1993" else "93"
            elif key in ("normal", "fail", "gold"):
                kinds.add(key)
            elif key == "verilog":
                verilog = True
            elif key == "stop-time":
                stop_time = value
            elif key.startswith("g") and value and len(key) > 1:
                generics.append(f"{key[1:]}={value}")
            elif key in ("seed", "slow", "!windows", "O0", "O2", "no-collapse",
                         "shuffle", "per-file", "psl", "work", "stop"):
                if key == "work":
                    skip = skip or "requires named work library"
                if key == "stop":
                    skip = skip or "nvc stop-delta option"
            elif key.startswith("+") or key.startswith("$"):
                skip = skip or f"nvc option {flag}"
            elif key in NVC_SKIP_FLAGS:
                skip = skip or f"nvc-specific test kind {key}"
            elif key.startswith("guut."):
                skip = skip or "hierarchical generic override"
        if verilog:
            source = next((base / f"{name}{ext}" for ext in (".v", ".sv")
                           if (base / f"{name}{ext}").exists()), base / f"{name}.v")
            compile_args = ["compile", "-q"] + standard_args(
                "systemverilog" if source.suffix == ".sv" else "verilog", None)
            top = name
        else:
            source = base / f"{name}.vhd"
            compile_args = ["compile", "-q"] + standard_args(
                "vhdl", {"93": "93", "00": "2000", "02": "2002", "08": "2008",
                         "19": "2019"}[standard])
            top = vhdl_top_spec(name, [read_text(source)])
        if not source.exists():
            skip = skip or "source not present"
        compile_args.append(str(source))
        elab = ["elaborate", "-q", "--no-aot", "--top", top]
        for generic in generics:
            elab += ["--generic", generic]
        sim = ["simulate", "--engine", "{engine}"]
        if stop_time:
            sim += ["--duration", stop_time]
        gold = base / "gold" / f"{name}.txt"
        cases.append(Case(
            suite="nvc",
            id=name,
            group=("verilog" if verilog else f"vhdl-{standard}"),
            commands=[Command("compile", compile_args), Command("elaborate", elab),
                      Command("simulate", sim)],
            expect="fail" if "fail" in kinds else "pass",
            checker="vhdl_clean",
            checker_data={"nvc_gold": str(gold) if "gold" in kinds and gold.exists() else None},
            skip=skip,
        ))
    return cases


VUNIT_RUN_RE = re.compile(r'\brun\s*\(\s*"([^"]+)"\s*\)', re.I)


def vhdl_compliance_cases(root: Path) -> list[Case]:
    base = root / "sources" / "vhdl-compliance-tests"
    shim = SHIM_DIR / "vunit_shim.vhd"
    cases = []
    for standard in ("2008", "2019"):
        for source in sorted((base / f"vhdl_{standard}").glob("*.vhd")):
            text = read_text(source)
            entities = [e for e in vhdl_units(text)[0] if e.startswith("tb_")]
            if not entities:
                continue
            entity = entities[-1]
            tests = VUNIT_RUN_RE.findall(VHDL_COMMENT_RE.sub("", text)) or ["<default>"]
            compile_shim = ["compile", "-q"] + standard_args("vhdl", standard) + [
                "--library", "vunit_lib", str(shim)]
            compile_tb = ["compile", "-q"] + standard_args("vhdl", standard) + [
                "--search-library", "vunit_lib", str(source)]
            elab = ["elaborate", "-q", "--no-aot", "--search-library", "vunit_lib",
                    "--top", vhdl_top_spec(entity, [text]),
                    "--generic", "runner_cfg=enabled_test_cases : __all__,output path : .,"
                    "active python runner : false"]
            sim = ["simulate", "--engine", "{engine}", "--file-root", "."]
            cases.append(Case(
                suite="vhdl-compliance",
                id=f"vhdl_{standard}/{source.name}",
                group=f"vhdl-{standard}",
                commands=[Command("compile", compile_shim), Command("compile", compile_tb),
                          Command("elaborate", elab), Command("simulate", sim)],
                checker="vunit",
                checker_data={"tests": tests},
                env={"VHDL_TEST": "hello world", "TOOL_NAME": "do not use this"},
            ))
    return cases


ADAPTERS: dict[str, Callable[[Path], list[Case]]] = {
    "sv-tests": sv_tests_cases,
    "verilator": verilator_cases,
    "ivtest": ivtest_cases,
    "vests": vests_cases,
    "nvc": nvc_cases,
    "vhdl-compliance": vhdl_compliance_cases,
}


# ---------------------------------------------------------------------------
# Execution


@dataclasses.dataclass
class StepResult:
    stage: str
    returncode: int | None
    seconds: float
    timed_out: bool
    output: str


def remove_tree(path: Path) -> None:
    """Delete a workspace; fsim publishes its artifacts read-only."""
    def make_writable(function, target, _info):
        try:
            os.chmod(target, 0o700)
            parent = os.path.dirname(target)
            os.chmod(parent, 0o700)
            function(target)
        except OSError:
            pass
    if path.exists():
        for directory, _dirs, _files in os.walk(path):
            try:
                os.chmod(directory, 0o700)
            except OSError:
                pass
        shutil.rmtree(path, onerror=make_writable)


def safe_name(identifier: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", identifier)[-150:]


def run_command(fsim: Path, command: Command, workdir: Path, env: dict,
                timeout: float, engine: str) -> StepResult:
    args = [str(fsim)] + [a.replace("{engine}", engine) for a in command.args]
    started = time.monotonic()
    process = subprocess.Popen(
        args, cwd=workdir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=env, start_new_session=True)
    timed_out = False
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        output, _ = process.communicate()
    text = output.decode("utf-8", errors="replace")
    if len(text) > 400_000:
        text = text[:200_000] + "\n...[truncated]...\n" + text[-200_000:]
    return StepResult(command.stage, process.returncode, time.monotonic() - started,
                      timed_out, text)


def first_diagnostics(output: str, limit: int = 3) -> list[dict]:
    found = []
    for line in output.splitlines():
        match = DIAGNOSTIC_RE.search(line)
        if match and match.group(2) not in ("FSIM-HDL-REPORT",):
            found.append({"code": match.group(2), "message": match.group(3)[:200]})
            if len(found) >= limit:
                break
    return found


def simulation_lines(output: str) -> list[str]:
    return [line for line in output.splitlines() if not TRAILER_RE.match(line)]


def evaluate_assert(expression: str) -> bool:
    expression = expression.strip()
    if not re.fullmatch(r"[\sA-Za-z0-9_()=!<>.+\-*/%&|^~'\"]*", expression):
        return False
    try:
        tree = ast.parse(expression, mode="eval")
    except SyntaxError:
        return False
    allowed = (ast.Expression, ast.Compare, ast.BoolOp, ast.UnaryOp, ast.BinOp,
               ast.Constant, ast.Eq, ast.NotEq, ast.Lt, ast.LtE, ast.Gt, ast.GtE,
               ast.And, ast.Or, ast.Not, ast.USub, ast.UAdd, ast.Invert, ast.Add,
               ast.Sub, ast.Mult, ast.Div, ast.FloorDiv, ast.Mod, ast.BitAnd,
               ast.BitOr, ast.BitXor, ast.LShift, ast.RShift, ast.Pow)
    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            # sv-tests evaluates assertions with Python's eval; int() is the
            # only call its generated tests use.
            if not (isinstance(node.func, ast.Name) and node.func.id == "int"
                    and not node.keywords):
                return False
            continue
        if isinstance(node, ast.Name):
            if node.id != "int":
                return False
            continue
        if isinstance(node, ast.Load):
            continue
        if not isinstance(node, allowed):
            return False
    try:
        return bool(eval(compile(tree, "<assert>", "eval"), {"__builtins__": {}},
                         {"int": int}))
    except Exception:  # noqa: BLE001 - mirrors sv-tests' own eval policy
        return False


def check_outcome(case: Case, steps: list[StepResult], workdir: Path) -> tuple[str, str]:
    """Return (status, detail) for a completed or failed run."""
    last = steps[-1]
    crashed = any(s.returncode is not None and (s.returncode < 0 or s.returncode >= 126)
                  for s in steps)
    if any(s.timed_out for s in steps):
        return "timeout", f"{last.stage} timed out"
    if crashed:
        bad = next(s for s in steps if s.returncode < 0 or s.returncode >= 126)
        return "crash", f"{bad.stage} exited with {bad.returncode}"
    failed_step = next((s for s in steps if s.returncode != 0), None)
    sim = next((s for s in steps if s.stage == "simulate"), None)
    sim_output = sim.output if sim else ""
    hdl_errors = [line for line in sim_output.splitlines() if HDL_ERROR_RE.search(line)]

    if case.expect == "fail":
        if failed_step is not None:
            if STAGE_ORDER[failed_step.stage] <= STAGE_ORDER[case.fail_by]:
                return "pass", f"rejected at {failed_step.stage}"
            return "fail", f"rejected late at {failed_step.stage}"
        if case.checker in ("vhdl_clean", "ivtest", "verilator") and hdl_errors:
            return "pass", "runtime error reported"
        if case.checker == "sv_tests" and STAGE_ORDER[case.fail_by] >= 2 and hdl_errors:
            return "pass", "runtime error reported"
        return "fail", "accepted invalid input"

    if failed_step is not None:
        return "fail", f"{failed_step.stage} failed"

    if case.checker == "sv_tests":
        for line in sim_output.splitlines():
            match = re.search(r":([a-z]+):(.*)", line.strip())
            if match and match.group(1) == "assert" and not evaluate_assert(match.group(2)):
                return "fail", "assert failed: " + match.group(2).strip()[:160]
        return "pass", ""
    if case.checker == "verilator":
        if "%Error" in sim_output:
            return "fail", "test reported %Error"
        if case.checker_data.get("check_finished") and "*-* All Finished *-*" not in sim_output:
            return "fail", "missing *-* All Finished *-*"
        return "pass", ""
    if case.checker == "ivtest":
        gold = case.checker_data.get("gold")
        lines = simulation_lines(sim_output)
        if gold:
            expected = [l.rstrip() for l in read_text(Path(gold)).splitlines()
                        if not re.match(r"^(VCD info:|.*\$finish called|.*: warning)", l)]
            actual = [l.rstrip() for l in lines if not l.startswith("objects/")]
            if expected != actual:
                return "fail", "output differs from gold"
            return "pass", ""
        if any(re.fullmatch(r"\s*passed\s*", line, re.I) for line in lines):
            return "pass", ""
        return "fail", "no PASSED line"
    if case.checker == "vhdl_clean":
        if hdl_errors:
            return "fail", "error assertion: " + hdl_errors[0][-160:]
        for name, expected in case.checker_data.get("outputs", []):
            produced = workdir / name
            if not produced.exists():
                return "fail", f"output file {name} missing"
            if read_text(produced).split() != read_text(Path(expected)).split():
                return "fail", f"output file {name} differs"
        return "pass", ""
    if case.checker == "vunit":
        return "pass", ""
    if hdl_errors:
        return "fail", hdl_errors[0][-160:]
    return "pass", ""


def vunit_subresults(case: Case, steps: list[StepResult], status: str) -> list[dict]:
    tests = case.checker_data["tests"]
    sim = next((s for s in steps if s.stage == "simulate"), None)
    if sim is None or status != "pass":
        stage = steps[-1].stage
        return [{"test": t, "status": "fail", "detail": f"{stage} failed"} for t in tests]
    started, failures, done = set(), collections.defaultdict(list), False
    for line in sim.output.splitlines():
        match = re.search(r"VUNIT-SHIM: (start|fail|done) ?(.*)", line)
        if not match:
            continue
        kind, rest = match.groups()
        if kind == "start":
            started.add(rest.strip())
        elif kind == "fail":
            name, _, message = rest.partition(": ")
            failures[name.strip()].append(message)
        else:
            done = True
    errors = [l for l in sim.output.splitlines() if HDL_ERROR_RE.search(l)]
    results = []
    for test in tests:
        if test != "<default>" and test not in started:
            results.append({"test": test, "status": "fail", "detail": "test case never ran"})
        elif failures.get(test):
            results.append({"test": test, "status": "fail", "detail": failures[test][0][:200]})
        elif not done:
            results.append({"test": test, "status": "fail",
                            "detail": "simulation ended before test_runner_cleanup"})
        elif errors and test == "<default>":
            results.append({"test": test, "status": "fail", "detail": errors[0][-160:]})
        else:
            results.append({"test": test, "status": "pass", "detail": ""})
    return results


def run_case(case: Case, fsim: Path, workdir: Path, engine: str, default_timeout: float,
             keep: bool) -> dict:
    record = {"suite": case.suite, "id": case.id, "group": case.group, "expect": case.expect}
    if case.skip:
        record.update(status="skip", detail=case.skip)
        return record
    workdir.mkdir(parents=True, exist_ok=True)
    for source, destination in case.copies:
        target = workdir / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            shutil.copyfile(source, target)
        except OSError as error:
            record.update(status="harness", detail=f"copy failed: {error}")
            return record
    for destination, content in case.writes:
        (workdir / destination).write_text(content, encoding="utf-8")
    env = dict(os.environ)
    env.update(case.env)
    timeout = case.timeout or default_timeout
    steps: list[StepResult] = []
    for command in case.commands:
        step = run_command(fsim, command, workdir, env, timeout, engine)
        steps.append(step)
        (workdir / f"{len(steps)}-{command.stage}.log").write_text(step.output, encoding="utf-8")
        if step.timed_out or step.returncode != 0:
            break
    status, detail = check_outcome(case, steps, workdir)
    failing = next((s for s in steps if s.returncode != 0 or s.timed_out), steps[-1])
    record.update(
        status=status,
        detail=detail,
        stage=failing.stage,
        seconds=round(sum(s.seconds for s in steps), 3),
        diagnostics=first_diagnostics(failing.output) if status != "pass" else [],
    )
    if case.checker == "vunit":
        record["tests"] = vunit_subresults(case, steps, status)
    if status != "pass":
        tail = [l for l in failing.output.splitlines() if l.strip()][-6:]
        record["tail"] = tail
    if status == "pass" and not keep and case.chain is None:
        remove_tree(workdir)
    return record


def run_chain(cases: list[Case], fsim: Path, workdir: Path, engine: str, timeout: float,
              keep: bool) -> list[dict]:
    return [run_case(case, fsim, workdir, engine, timeout, keep) for case in cases]


# ---------------------------------------------------------------------------
# Reporting


def summarize(records: list[dict], title: str) -> str:
    lines = [f"# {title}", ""]
    by_suite: dict[str, list[dict]] = collections.defaultdict(list)
    for record in records:
        by_suite[record["suite"]].append(record)
    lines += ["| Suite | Cases | Pass | Fail | Crash | Timeout | Skip | Pass rate |",
              "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for suite in SUITES:
        group = by_suite.get(suite)
        if not group:
            continue
        counts = collections.Counter(r["status"] for r in group)
        ran = len(group) - counts["skip"]
        rate = f"{100.0 * counts['pass'] / ran:.1f}%" if ran else "-"
        lines.append(f"| {suite} | {len(group)} | {counts['pass']} | {counts['fail']} | "
                     f"{counts['crash']} | {counts['timeout']} | {counts['skip']} | {rate} |")
    vunit = [t for r in records for t in r.get("tests", [])]
    if vunit:
        passed = sum(t["status"] == "pass" for t in vunit)
        lines += ["", f"VUnit test cases: {passed}/{len(vunit)} pass."]

    lines += ["", "## Failure causes by first diagnostic", "",
              "| Diagnostic | Cases | Suites | Example message |", "|---|---:|---|---|"]
    causes: dict[str, list[dict]] = collections.defaultdict(list)
    for record in records:
        if record["status"] in ("pass", "skip"):
            continue
        diagnostics = record.get("diagnostics") or []
        if diagnostics:
            key = diagnostics[0]["code"]
        elif record["status"] in ("crash", "timeout"):
            key = record["status"].upper()
        else:
            key = "(no diagnostic) " + re.sub(r"[0-9]+", "N", record.get("detail", ""))[:60]
        causes[key].append(record)
    for key, items in sorted(causes.items(), key=lambda kv: -len(kv[1]))[:60]:
        suites = ", ".join(sorted({r["suite"] for r in items}))
        example = ""
        for item in items:
            if item.get("diagnostics"):
                example = item["diagnostics"][0]["message"]
                break
            example = item.get("detail", "")
        example = example.replace("|", "\\|")[:110]
        lines.append(f"| {key} | {len(items)} | {suites} | {example} |")

    lines += ["", "## Pass rate by suite group (LRM clause / tag)", ""]
    for suite in SUITES:
        group = by_suite.get(suite)
        if not group:
            continue
        lines += [f"### {suite}", "", "| Group | Pass | Total |", "|---|---:|---:|"]
        per_group: dict[str, list[dict]] = collections.defaultdict(list)
        for record in group:
            if record["status"] != "skip":
                per_group[record["group"]].append(record)
        for name in sorted(per_group, key=natural_key):
            items = per_group[name]
            passed = sum(r["status"] == "pass" for r in items)
            lines.append(f"| {name} | {passed} | {len(items)} |")
        lines.append("")
    crashes = [r for r in records if r["status"] in ("crash", "timeout")]
    if crashes:
        lines += ["## Crashes and timeouts", ""]
        for record in crashes[:200]:
            lines.append(f"- {record['suite']} `{record['id']}`: {record['detail']}")
    return "\n".join(lines) + "\n"


def natural_key(text: str):
    return [int(p) if p.isdigit() else p for p in re.split(r"(\d+)", text)]


# ---------------------------------------------------------------------------
# Command line


def select_cases(args) -> list[Case]:
    suites = args.suite or list(SUITES)
    cases: list[Case] = []
    for suite in suites:
        cases += ADAPTERS[suite](args.root)
    if args.filter:
        pattern = re.compile(args.filter)
        cases = [c for c in cases if pattern.search(f"{c.suite}/{c.id}")]
    return cases


def command_list(args) -> int:
    cases = select_cases(args)
    counts = collections.Counter((c.suite, "skip" if c.skip else c.expect) for c in cases)
    for suite in SUITES:
        row = {k[1]: v for k, v in counts.items() if k[0] == suite}
        if row:
            print(f"{suite:16} pass-expected={row.get('pass', 0):5} "
                  f"fail-expected={row.get('fail', 0):5} skipped={row.get('skip', 0):5}")
    if args.verbose:
        for case in cases:
            print(f"{case.suite}/{case.id} expect={case.expect} skip={case.skip}")
    return 0


def command_run(args) -> int:
    cases = select_cases(args)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = args.out or (args.root / "results" / f"{stamp}-{args.engine}")
    out.mkdir(parents=True, exist_ok=True)
    fsim = args.fsim.resolve()
    units: list[list[Case]] = []
    chains: dict[str, list[Case]] = collections.OrderedDict()
    for case in cases:
        if case.chain:
            chains.setdefault(case.chain, []).append(case)
        else:
            units.append([case])
    units = list(chains.values()) + units
    results_path = out / "results.jsonl"
    records: list[dict] = []
    started = time.monotonic()
    with results_path.open("w", encoding="utf-8") as sink, \
            concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {}
        for unit in units:
            head = unit[0]
            name = head.chain if head.chain else f"{head.suite}/{head.id}"
            workdir = out / "work" / safe_name(name)
            if len(unit) > 1 or head.chain:
                futures[pool.submit(run_chain, unit, fsim, workdir, args.engine,
                                    args.timeout, args.keep)] = unit
            else:
                futures[pool.submit(lambda c=head, w=workdir: [run_case(
                    c, fsim, w, args.engine, args.timeout, args.keep)])] = unit
        done = 0
        for future in concurrent.futures.as_completed(futures):
            for record in future.result():
                records.append(record)
                sink.write(json.dumps(record) + "\n")
                done += 1
            sink.flush()
            if not args.quiet and done % 100 < len(futures[future]):
                elapsed = time.monotonic() - started
                passed = sum(r["status"] == "pass" for r in records)
                print(f"[{done}/{len(cases)}] pass={passed} elapsed={elapsed:.0f}s",
                      flush=True)
    records.sort(key=lambda r: (SUITES.index(r["suite"]), natural_key(r["id"])))
    with results_path.open("w", encoding="utf-8") as sink:
        for record in records:
            sink.write(json.dumps(record) + "\n")
    meta = {
        "fsim": str(fsim),
        "engine": args.engine,
        "started": stamp,
        "seconds": round(time.monotonic() - started, 1),
        "suites": args.suite or list(SUITES),
        "filter": args.filter,
        "sources": source_revisions(args.root),
    }
    (out / "meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    summary = summarize(records, f"LRM corpus results {stamp} ({args.engine})")
    (out / "summary.md").write_text(summary, encoding="utf-8")
    print(f"results: {out}")
    print(summary.split("## Failure causes")[0])
    return 0


def source_revisions(root: Path) -> dict:
    revisions = {}
    sources = root / "sources"
    for child in sorted(sources.iterdir()) if sources.exists() else []:
        try:
            revisions[child.name] = subprocess.run(
                ["git", "-C", str(child), "rev-parse", "HEAD"], capture_output=True,
                text=True, check=True).stdout.strip()
        except (subprocess.CalledProcessError, OSError):
            revisions[child.name] = None
    return revisions


def command_report(args) -> int:
    records = [json.loads(line) for line in args.results.read_text().splitlines() if line]
    print(summarize(records, f"LRM corpus results ({args.results})"), end="")
    return 0


def command_compare(args) -> int:
    def load(path: Path) -> dict:
        return {(r["suite"], r["id"]): r for r in
                (json.loads(l) for l in path.read_text().splitlines() if l)}
    before, after = load(args.before), load(args.after)
    fixed = [k for k in after if after[k]["status"] == "pass"
             and before.get(k, {}).get("status") not in (None, "pass")]
    broken = [k for k in after if after[k]["status"] != "pass"
              and before.get(k, {}).get("status") == "pass"]
    print(f"newly passing: {len(fixed)}  newly failing: {len(broken)}")
    for key in broken:
        print(f"  REGRESSION {key[0]}/{key[1]}: {after[key].get('detail')}")
    if args.verbose:
        for key in fixed:
            print(f"  fixed {key[0]}/{key[1]}")
    return 1 if broken else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    sub = parser.add_subparsers(dest="command", required=True)

    def add_selection(p):
        p.add_argument("--suite", action="append", choices=SUITES)
        p.add_argument("--filter", help="regular expression on suite/id")

    p_list = sub.add_parser("list", help="enumerate cases")
    add_selection(p_list)
    p_list.add_argument("-v", "--verbose", action="store_true")
    p_list.set_defaults(func=command_list)

    p_run = sub.add_parser("run", help="run cases")
    add_selection(p_run)
    p_run.add_argument("--fsim", type=Path, default=DEFAULT_FSIM)
    p_run.add_argument("--engine", default="interpreter",
                       choices=("interpreter", "compiled"))
    p_run.add_argument("-j", "--jobs", type=int, default=8)
    p_run.add_argument("--timeout", type=float, default=60.0)
    p_run.add_argument("--out", type=Path)
    p_run.add_argument("--keep", action="store_true", help="keep passing workspaces")
    p_run.add_argument("-q", "--quiet", action="store_true")
    p_run.set_defaults(func=command_run)

    p_report = sub.add_parser("report", help="summarize a results.jsonl")
    p_report.add_argument("results", type=Path)
    p_report.set_defaults(func=command_report)

    p_compare = sub.add_parser("compare", help="compare two results.jsonl files")
    p_compare.add_argument("before", type=Path)
    p_compare.add_argument("after", type=Path)
    p_compare.add_argument("-v", "--verbose", action="store_true")
    p_compare.set_defaults(func=command_compare)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
