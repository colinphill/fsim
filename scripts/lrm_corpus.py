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
    # Cases sharing a chain run in order in one workspace (VESTs Ashenden and
    # selected Billowitch file round trips).
    chain: str | None = None
    skip: str | None = None
    # Why a failure of this case is outside the IEEE LRMs (another
    # simulator's extension); a failing run is recorded as a skip.
    out_of_scope: str | None = None
    timeout: float | None = None
    # Governed UVM release whose precompiled library the case maps (sv-tests).
    uvm_release: str | None = None
    # Require this specific diagnostic from a failed simulation after compile
    # and elaboration succeeded. Tuple is (diagnostic code, message substring,
    # normal process exit status).
    expected_runtime_error: tuple[str, str, int] | None = None


STAGE_ORDER = {"compile": 0, "elaborate": 1, "simulate": 2}


# ---------------------------------------------------------------------------
# Source scanning helpers

SV_MODULE_RE = re.compile(
    r"^\s*(?:\(\*.*?\*\)\s*)*(?:extern\s+)?(?:module|macromodule|program)\s+(?:automatic\s+|static\s+)?"
    r"([A-Za-z_][A-Za-z0-9_$]*|\\\S+)", re.M)
SV_COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
SV_SCAN_TRIVIA_RE = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\\[^\s]+', re.S)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def sv_root_modules(texts: Iterable[str]) -> list[str]:
    """Return modules that no other scanned module instantiates."""
    def hide_trivia(match: re.Match[str]) -> str:
        token = match.group(0)
        # Quotes and comment delimiters inside an escaped identifier are part
        # of its name. Preserve it before recognizing surrounding strings.
        if token.startswith("\\"):
            return token
        return "".join("\n" if char == "\n" else " " for char in token)

    joined = "\n".join(SV_SCAN_TRIVIA_RE.sub(hide_trivia, text)
                       for text in texts)
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
        uvm_release = None
        if "uvm" in tags or "uvm-1.2" in tags:
            # sv-tests compiles the UVM library sources with each UVM test. The
            # runner compiles each Accellera release tree (governed by
            # docs/uvm-source-provenance.md) once per run into a library `uvm`
            # that every UVM case maps and searches (see prepare_uvm_libraries).
            uvm_release = "1.2" if "uvm-1.2" in tags else "2020.3.1"
            uvm_src = root / "uvm" / f"uvm-{uvm_release}" / "src"
            compile_args += ["--search-library", "uvm", "-I", str(uvm_src)]
            timeout = max(timeout, 300.0)
        # A preprocessing test checks only the preprocessor's output, as the
        # sv-tests harness runs it (IEEE 1800-2017 22).
        if types == ["preprocessing"]:
            compile_args.append("--preprocess-only")
        compile_args += source_files
        commands = [Command("compile", compile_args)]
        if uvm_release:
            commands.insert(0, Command("compile", [
                "library", "map", "uvm", "{uvm_library:" + uvm_release + "}"]))
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
            if uvm_release:
                elab += ["--search-library", "uvm"]
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
            uvm_release=uvm_release,
        ))
    return cases


VERILATOR_SHELL = """module {shell};
{decls}
    {module} {module} (
{ports}
    );
    initial begin
{init0}
        #10;
        while ($time < {sim_time}) begin
{body}
        end
    end
endmodule
"""


def verilator_shell(top_text: str, module: str = "t", shell: str = "top",
                    sim_time: int = 1100) -> str:
    inputs: list[str] = []
    get_sigs = True
    for line in top_text.splitlines():
        if re.match(r"^\s*module\s+" + re.escape(module) + r"\b", line):
            inputs = []
            get_sigs = True
        if get_sigs:
            match = re.match(r"^\s*input\s*(logic|bit|reg|wire)?\s*([A-Za-z0-9_]+)", line)
            if match and match.group(2) not in inputs:
                inputs.append(match.group(2))
            if re.match(r"^\s*(function|task|endmodule)", line):
                get_sigs = False
    # The C++ main only drives clk and fastclk; other inputs keep their reset
    # value, so the shell leaves them unconnected.
    inputs = sorted(name for name in inputs if name in ("clk", "fastclk"))
    decls = "\n".join(f"    reg {name};" for name in inputs)
    ports = "\n".join(("      " + ("," if i else "") + f".{name} ({name})")
                      for i, name in enumerate(inputs))
    init0 = "".join(f"        {n} = 0;\n" for n in ("fastclk", "clk") if n in inputs)
    # Mirrors the C++ main that driver.py generates for Verilator runs (the
    # tests' cycle counts are calibrated against it): after time 10, each loop
    # iteration advances five units, toggling fastclk every unit and clk on the
    # first, so clk rises at 10, 20, ..., 1090.
    body = ""
    for step in range(5):
        if "fastclk" in inputs:
            body += "          fastclk = !fastclk;\n"
        if step == 0 and "clk" in inputs:
            body += "          clk = !clk;\n"
        body += "          #1;\n"
    return VERILATOR_SHELL.format(decls=decls, ports=ports, init0=init0.rstrip("\n"),
                                  body=body.rstrip("\n"), module=module, shell=shell,
                                  sim_time=sim_time)


def python_driver_names(source: str) -> dict[str, object]:
    """Top-level literal assignments of a test driver (`name = "x"`,
    `test.cycles = (A if test.benchmark else B)`), taking the non-benchmark
    branch of a conditional."""
    names: dict[str, object] = {}
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return names

    def value_of(node: ast.AST):
        if isinstance(node, ast.IfExp):
            return value_of(node.orelse)
        return ast.literal_eval(node)

    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        key = ast.unparse(target)
        try:
            names[key] = value_of(statement.value)
        except (ValueError, SyntaxError, TypeError):
            continue
    return names


def python_literal_list(source: str, keyword: str) -> list[str] | None:
    """A driver's flag list: literals, f-strings over top-level literal
    names, string `+` and `str()` of such names."""
    match = re.search(keyword + r"\s*=\s*(\[[^\]]*\])", source, re.S)
    if not match:
        return None
    try:
        tree = ast.parse(match.group(1), mode="eval")
    except SyntaxError:
        return None
    names = python_driver_names(source)

    def evaluate(node: ast.AST) -> str:
        if isinstance(node, ast.Constant):
            return str(node.value)
        if isinstance(node, (ast.Name, ast.Attribute)):
            key = ast.unparse(node)
            if key not in names:
                raise ValueError(key)
            return str(names[key])
        if isinstance(node, ast.JoinedStr):
            return "".join(evaluate(part) for part in node.values)
        if isinstance(node, ast.FormattedValue):
            return evaluate(node.value)
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            return evaluate(node.left) + evaluate(node.right)
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                and node.func.id == "str" and len(node.args) == 1):
            return evaluate(node.args[0])
        raise ValueError(ast.dump(node))

    if not isinstance(tree.body, ast.List):
        return None
    try:
        return [evaluate(element) for element in tree.body.elts]
    except ValueError:
        return None


def verilator_library_files(top_file: Path, tdir: Path) -> list[Path]:
    """Files driver.py's `-y t/` would load: a module or interface used but
    not declared is read from t/<name>.v."""
    unit_re = re.compile(r"^\s*(?:module|macromodule|interface|program)\s+"
                         r"(?:automatic\s+|static\s+)?([A-Za-z_]\w*)", re.M)
    loaded: list[Path] = []
    pending = [top_file]
    declared: set[str] = set()
    texts: list[str] = []
    while pending:
        path = pending.pop()
        text = SV_COMMENT_RE.sub(" ", read_text(path))
        texts.append(text)
        declared.update(unit_re.findall(text))
        for name in sorted(set(re.findall(r"\b([A-Za-z_]\w*)\b", text))):
            if name in declared or len(name) > 100:
                continue
            for extension in (".v", ".sv"):
                candidate = tdir / f"{name}{extension}"
                if candidate == top_file or candidate in loaded \
                        or not candidate.is_file():
                    continue
                units = unit_re.findall(SV_COMMENT_RE.sub(" ", read_text(candidate)))
                if name in units and "t" not in units:
                    loaded.append(candidate)
                    pending.append(candidate)
                    declared.update(units)
                break
    return loaded


# Icarus system tasks that are not in IEEE 1364/1800.
ICARUS_EXTENSION_TASKS = re.compile(
    r"\$(abstime|is_signed|countdrivers|simparam|fopenr|sizeof|deposit|finish_and_return|ivlh_\w+)\b")
# Icarus syntax extensions: the `bool` variable type, the binary `~&`
# and `~|` operators (the IEEE LRMs define them only as unary reductions),
# and real-valued built-in nets (`wire real`; IEEE 1800-2017 6.7.1 allows
# only four-state integral net types).
ICARUS_EXTENSION_SYNTAX = re.compile(
    r"\breg\s+bool\b|[\w)\]]\s*~[&|]\s*[\w(]"
    r"|\b(?:wire|tri|tri0|tri1|wand|wor|triand|trior|uwire)\s+"
    r"(?:real|realtime|shortreal)\b")
# ivtest cases whose expected output depends on Icarus's own choices:
# the order of processes in one time step (IEEE 1800-2017 4.7 leaves it
# undefined), implementation-defined formatting or widths, or Icarus-only
# semantics and options.
_ICARUS_RACE = "depends on Icarus's process order within a time step"
ICARUS_NONSTANDARD = {
    # IEEE 1497-2001 5.2: an SDF header starts with the required
    # SDFVERSION entry; these files omit it.
    **{name: "SDF file without the required SDFVERSION header (IEEE 1497-2001 5.2)"
       for name in ("br960a", "br960b", "br960c", "br960d", "pr2972866")},
    **{name: _ICARUS_RACE for name in (
        "br_ml20190806b", "pr3064375", "pr1662508", "pr1861212b",
        "pr1862744a", "sdw_dsbl", "fork3.19A", "fork3.19B", "pr812",
        "br1019", "sv_ps_array_cassign", "real_wire_force_rel",
        "nb_ec_multi_ev", "nb_ec_real", "pv_wr_vec2_nb_ec",
        "pv_wr_vec2a_nb_ec", "pv_wr_vec4_nb_ec", "pv_wr_vec4a_nb_ec",
        # Time-zero order of initial blocks, declaration initializers and
        # continuous assignments (unspecified in IEEE 1364-2005 9.9).
        "array_lval_select6", "br1008", "br988", "pr1719055")},
    "pr243": "expects $monitor output in the time step of $finish",
    "big_int": "expects Icarus's width for unsized literals over 32 bits",
    "br1029a": "formats a real with %d/%x/%b",
    "br1029b": "formats a real with %d/%x/%b",
    "pr1985582": "expects Icarus's default real formatting",
    "pr2486350": "expects %h of a 64-bit value without leading zeros",
    "implicit_cast13": "checks only under __ICARUS__",
    "pr2815398a_std": "needs __ICARUS__",
    "pr1771903": "needs Icarus -gallow-real-arg-to-bits",
    "cast_int": "expects infinity and NaN to convert to x",
    "sv_cast_darray": (
        "casts a REAL dynamic array to packed bits; REAL is not a bit-stream "
        "element type (IEEE 1800-2005 4.16, IEEE 1800-2017 6.24.3)"
    ),
    "concat3": "expects side effects from a zero replication",
    "cfunc_assign_op_mixed": "expects Icarus's compound assignment result",
    "sv_string6": "expects Icarus's hextoa of a negative value",
    "scanf4": "expects zero instead of EOF (-1) when whitespace-only %s input "
        "ends before the first conversion (IEEE 1364-2005 17.2.4.1, 17.2.4.3)",
    # Typedef declarator dimensions precede the nested type's dimensions
    # (IEEE 1800-2017 7.4.5, 20.7). This test expects the reverse order.
    "sv_typedef_nested_array": (
        "expects nested typedef array dimensions in Icarus order instead of "
        "outermost-first order (IEEE 1800-2017 7.4.5, 20.7)"
    ),
    "module_port_shortreal": "compares a shortreal with a double literal",
    "signed_a": "expects an out-of-range index to be stripped",
    "br_gh156": "expects Icarus's VCD layout",
    "br_gh889": "expects output that ignores the SDF file",
    # IEEE 1800-2012 11.5.1 permits, but does not require, a compile-time
    # error for an out-of-range packed select. This CE expectation is optional.
    "br_gh840b": "requires an optional compile-time diagnostic for an "
        "out-of-range packed select (IEEE 1800-2012 7.4.6, 11.5.1 Note 2)",
    # This CE row is a graceful-failure test for Icarus's missing feature;
    # unpacked structures are standard SystemVerilog (IEEE 1800-2009 7.2).
    "br_ml20150315b": "expects rejection of a legal unpacked struct declaration "
        "because Icarus does not implement it (IEEE 1800-2009 7.2)",
    "sv_packed_port1": "expects z from an undriven variable bit",
    "sv_packed_port2": "expects z from an undriven variable bit",
    # SystemVerilog preprocessor features in Icarus's default Verilog-2005
    # mode (IEEE 1364-2005 has neither `" nor `__LINE__).
    **{name: "uses `\" or `__LINE__ in Verilog-2005 mode" for name in (
        "attrib_expr", "br_gh484", "module_output_port_list_def",
        "nb_ec_concat", "parameter_1bit", "real_array_multi_dim",
        "string13", "string14")},
    # Icarus's ``NAME macro-text stringification is not in either standard.
    **{name: "uses Icarus ``NAME macro stringification" for name in (
        "macro_with_args", "pr622", "pr639", "pr1741212", "pr1912112",
        "pr1925360")},
    "pr1716276": "omits a positional parameter override (`#( , 4)`)",
    **{name: "calls $urandom_range in Verilog-2005 mode" for name in (
        "urand_r", "urand_r2", "urand_r3")},
    # IEEE 1364-2005 / IEEE 1800-2017 Annex A.3.1 permits exactly
    # one output terminal per pull_gate_instance, unlike buf/not.
    **{name: "uses multiple output terminals in one pullup/pulldown instance "
        "(non-standard; Annex A.3.1)" for name in (
        "pr1787423", "pr1787423b", "pr2834340")},
    # IEEE 1800-2017 7.5.1: the new[] initializer is an array.
    "sv_darray_args3": "initializes new[] with a scalar",
    "sv_darray_args4": "initializes new[] with a scalar",
    # IEEE 1800-2017 Table 11-20: z and z under an unknown condition is x.
    "pr2172606b": "expects z from ?: of two z operands under an unknown "
        "condition",
    # SystemVerilog constructs in Icarus's default Verilog-2005 mode (Batch
    # 65 triage, triage/b65-c/INDEX.md).
    **{name: "uses SystemVerilog constructs in Verilog-2005 mode" for name in (
        "automatic_error4", "br_gh531", "br_gh788", "br_gh1178c", "pr872",
        "random", "string12")},
    # Non-standard input or implementation-defined output.
    "param_string_compare": "applies ==? to string operands (IEEE 1800-2017 "
        "11.4.6 is integral only)",
    "pr2994193": "calls $dumpvars at two simulation times (IEEE 1800-2017 "
        "21.7.1.2)",
    "delay_var": "expects Icarus's default real display format",
    **{name: "declares a 2-state or enum net (IEEE 1800-2017 6.7.1)"
        for name in ("br_gh99e", "sv-2val-nets", "pull371")},
    "display_bug": "uses a size-only packed dimension",
    "br_gh553": "instantiates a module array without a port list",
    "pr2834340b": "uses multiple output terminals in one pullup instance",
    **{name: "uses several sources in a parallel module path (IEEE 1800-2017 "
        "30.4.4.1)" for name in ("pr1877743", "specify_01")},
    **{name: "drives a gate primitive with a real value" for name in (
        "br_gh1223a", "br_gh1223b", "br_gh1223c")},
    "pr1528093": "applies % to real operands (IEEE 1800-2017 11.3.1)",
    "urand": "calls $urandom in Verilog-2005 mode",
    "itor_rtoi": "expects results the test notes are not defined by the "
        "standard",
    "sv_class9": "expects dynamic-array assignment to alias (IEEE 1800-2017 "
        "7.6 copies)",
    **{name: "depends on Icarus @* and replication side-effect behavior"
        for name in ("br1000", "concat4")},
    **{name: "expects Icarus's assertion message format (implementation-"
        "defined, IEEE 1800-2017 20.10)" for name in (
        "sv_immediate_assert", "sv_immediate_assume")},
    "pr1699519": "expects Icarus's %G/%E output",
    "br_gh315": "uses the non-standard %-d flag",
    "pr2476430": "uses non-standard %+08d and %04c flags",
    **{name: "uses Icarus preprocessor behavior" for name in (
        "pr478", "pr1467825", "br979")},
    "pr1701855b": "calls $printtimescale on selects and arrays",
    "queue_stat": "expects $q_* statistics behavior",
    "always_latch_no_sens": "expects an Icarus-only error",
    "enum_compatibility_fail1": "expects an error for instances that share "
        "one enum declaration",
    "real_delay": "uses variable-delay UDPs Icarus does not support",
    **{name: "compares array-word VCD output with Icarus" for name in (
        "array_dump", "array_word_check", "dump_memword", "pr2859628",
        "vcd-dup")},
    "br_gh732": "expects Icarus's initial x behavior",
    "fread": "expects a non-fatal invalid $fread",
    "br_gh1298": "expects an unspecified x-condition string result",
    "pr985": "depends on the execution order of two time-zero initial "
        "blocks (IEEE 1800-2017 9.2.1)",
    **{name: "connects UDP terminals by name (udp_instance is positional, "
        "IEEE 1800-2017 29.7)" for name in ("udp_dff", "pr938b")},
    **{name: "gives a module instance an unparenthesized parameter value "
        "(IEEE 1364-2005 A.4.1.1)" for name in ("z1", "z2")},
    **{name: "declares ports in a module without a port list" for name in (
        "module_output_port_var2", "module_output_port_sv_var2")},
}
# Verilator's C++ escape functions ($c, $c8..$c64, $cpure).
VERILATOR_C_TASKS = re.compile(r"\$c(pure|\d+)?\s*\(")
# Verilator cases that rely on behaviour Verilator itself calls
# non-standard or that other simulators reject.
VERILATOR_NONSTANDARD = {
    # IEEE 1800-2017 9.2.2.2.2: always_comb runs once at time zero; an
    # always @* procedure waits for its first event.
    "t_enum_func": "expects an always @* procedure to run at time zero, "
        "which IEEE 1800-2017 9.2.2.2.2 gives only always_comb",
    # IEEE 1800-2017 20.2: $finish exits at once; the test counts a
    # nonblocking update scheduled in the same time step as the $finish.
    "t_scheduling_many_clocks": "counts a nonblocking update of the time "
        "step that calls $finish, which IEEE 1800-2017 20.2 does not run",
    # IEEE 1800-2017 20.4.2: $timeformat takes all four arguments or none.
    "t_display_time": "calls $timeformat with one to three or empty "
        "arguments; IEEE 1800-2017 20.4.2 requires all four or none",
    # IEEE 1800-2017 6.12: shortreal is single precision, so a shortreal
    # holding 1.414 differs from the real 1.414 (11.3.1 compares them as
    # real); the test expects Verilator's double-precision shortreal.
    "t_math_shortreal": "compares a shortreal with a real literal as "
        "equal, which needs shortreal stored in double precision "
        "(IEEE 1800-2017 6.12 makes it single precision)",
    # A dist is a constraint item (IEEE 1800-2017 A.1.10 expression_or_dist),
    # not an operator, so it cannot appear inside parentheses.
    "t_constraint_dist_foreach_if": "writes a dist inside parentheses, "
        "which IEEE 1800-2017 A.1.10 does not allow (dist is a constraint "
        "item, not an expression)",
    "t_constraint_dist_if_foreach": "writes a dist inside parentheses, "
        "which IEEE 1800-2017 A.1.10 does not allow (dist is a constraint "
        "item, not an expression)",
    # IEEE 1800-2017 21.3.5 positions files with $rewind; $frewind is a
    # Verilator spelling.
    "t_sys_file_basic": "calls $frewind, which is not an IEEE system task "
        "(IEEE 1800-2017 21.3.5 defines $rewind)",
    "t_sys_file_autoflush": "calls $frewind, which is not an IEEE system "
        "task (IEEE 1800-2017 21.3.5 defines $rewind)",
    "t_sys_file_basic_cover_expr": "calls $frewind, which is not an IEEE "
        "system task (IEEE 1800-2017 21.3.5 defines $rewind)",
    # factorial1 recursively invokes a static task, so its input x is
    # shared by every activation and is 1 when recursion unwinds. The
    # fixture instead expects the factorial from automatic arguments.
    "t_static_task_args": "expects automatic argument storage in recursive "
        "static tasks (IEEE 1800-2017 13.3.2)",
    "t_enum_type_methods": "prints an enumeration with %s as its name "
        "(non-standard)",
    "t_package_export": "reads names a package imports but does not export "
        "(IEEE 1800-2017 26.6)",
    # Verilator rejects timing controls unless --timing or --no-timing is
    # given; the sources are legal.
    "t_timing_unset1": "expects the Verilator --timing flag error",
    "t_timing_unset2": "expects the Verilator --timing flag error",
    "t_timing_unset3": "expects the Verilator --timing flag error",
    # Two-state assumptions: `clk = !clk` from an x `logic clk` never
    # toggles (IEEE 1800-2017 6.8), and an `always @*` reading only a
    # variable it writes waits for a change that never comes (9.4.2.2).
    "t_virtual_interface_method": "toggles a clock that starts at x",
    "t_wait_iface_vif": "expects an always @* block to run without an event",
    # A constant function may call only constant system functions, which
    # do not include $sformatf (IEEE 1800-2017 13.4.3, 11.2.1).
    "t_const_string_func": "calls $sformatf in a constant function",
    "t_display_string": "calls $sformatf in a constant function",
    "t_display_string_noopt": "calls $sformatf in a constant function",
    "t_display_enum_format": "calls $sformatf in a constant expression",
    "t_unroll_nested_param": "calls $sformatf and writes module variables "
        "in a constant function",
    # Verilator command-line flags the case depends on.
    "t_assert_disabled": "depends on Verilator --no-assert",
    "t_flag_main_top_name": "depends on a Verilator command-line flag",
    "t_flag_parameter_hier": "depends on a Verilator command-line flag",
    "t_flag_parameter_pkg": "depends on a Verilator command-line flag",
    "t_fourstate_no_fourstate": "depends on a Verilator command-line flag",
    "t_vlt_timing": "depends on a Verilator configuration file",
    "t_net_delay_timing_sc": "requires a SystemC harness",
    # Two-state initial values (IEEE 1800-2017 6.8 gives x).
    "t_assert_goto_rep": "assumes two-state initial values",
    "t_assert_future": "assumes two-state initial values",
    "t_emit_constw": "assumes two-state initial values",
    "t_split_var_4": "assumes two-state initial values",
    "t_split_var_5": "assumes two-state initial values",
    "t_tri_pull01": "expects a select past the MSB to read 0, not x",
    "t_sequence_sexpr_throughout": "expects Verilator's count; the case "
        "notes other simulators differ",
    "t_sys_system": "expects an implementation-defined $system result",
    "t_sys_readmem_eof": "reads a file the Verilator driver generates",
    "t_strength_strongest_non_tristate": "needs clocks the Verilator "
        "harness drives",
    # IEEE 1800-2017 11.8.1: an unsigned operand makes the power's
    # context-determined left operand unsigned; the case guards these checks
    # from other simulators.
    "t_math_pow3": "keeps a power operand signed in an unsigned comparison",
    # Verilator lint or implementation limits, not LRM errors (Batch 65
    # triage, triage/b65-c/INDEX.md).
    "t_module_reserved_keyword": "expects a Verilator reserved-word lint error",
    **{name: "expects a Verilator UNOPTFLAT lint error" for name in (
        "t_unoptflat_simple_bad", "t_unoptflat_simple_2_bad",
        "t_unoptflat_simple_3_bad")},
    **{name: "expects Verilator's convergence-limit abort" for name in (
        "t_unopt_converge_run_bad", "t_unopt_converge_print_bad",
        "t_unopt_converge_ndbg_bad", "t_unopt_converge_initial_run_bad")},
    "t_constraint_array_limit": "depends on Verilator --constraint-array-limit",
    "t_constraint_dist_randmode_unsup": "expects a Verilator unsupported-"
        "feature error for legal code",
    "t_assert_unique_case_bad": "expects a fatal stop; IEEE 1800-2017 12.5.3 "
        "requires only a violation report",
    # Verilator language extensions and directives.
    **{name: "uses Verilog-AMS keywords (not an IEEE 1800 begin_keywords "
        "version, 22.14)" for name in (
        "t_vams_basic", "t_vams_wreal", "t_split_var_3_wreal")},
    "t_verilated_header": "includes verilated.v",
    "t_hier_block_cmake": "uses Verilator directives",
    "t_cover_fsm_sel_assign": "uses a Verilator metacomment",
    "t_timing_off": "uses Verilator timing_off metacomments",
    **{name: "calls the non-standard array method map()" for name in (
        "t_queue_method", "t_array_method", "t_assoc_method",
        "t_assoc_wildcard_method")},
    "t_randomize_soft_foreach": "uses soft foreach (soft applies only to "
        "expression_or_dist, IEEE 1800-2017 18.5.14)",
    "t_display_pattern_format": "uses the \\r escape (not in IEEE 1800-2017 "
        "Table 5-1)",
    # Input the LRM rejects.
    "t_preproc_str_undef": "uses an undefined macro (IEEE 1800-2017 22.5.1)",
    **{name: "gives always_latch an event control (IEEE 1800-2017 9.2.2.3)"
        for name in ("t_opt_dedupe_clk_gate", "t_opt_dedupe_clk_gate_off")},
    **{name: "declares timeunit after other module items (IEEE 1800-2017 "
        "3.14.2.3)" for name in ("t_timescale_unit", "t_timing_osc")},
    "t_var_rsvd": "uses the reserved word global as a name",
    **{name: "uses ref formals of a static subroutine (IEEE 1800-2017 "
        "13.5.2)" for name in (
        "t_var_ref", "t_var_ref_noinline", "t_func_ref", "t_func_ref_arg",
        "t_func_ref_noinline")},
    **{name: "replicates inside an assignment pattern with other items "
        "(IEEE 1800-2017 10.9.1)" for name in (
        "t_struct_init", "t_struct_init_trace")},
    "t_stream_unpacked_struct": "streams into a continuous assignment "
        "target (net_lvalue, IEEE 1800-2017 10.3)",
    **{name: "declares a local with its function's name (IEEE 1800-2017 "
        "13.4)" for name in ("t_func_under", "t_class_format")},
    **{name: "gives an n-input gate three delays (IEEE 1800-2017 28.3)"
        for name in ("t_gate_basic", "t_gate_basic_timing")},
    "t_opt_simulate_array": "uses an unnamed module-level begin-end block",
    # Zero-delay loops, scheduling races and Verilator-specific evaluation.
    **{name: "needs a zero-delay always loop to terminate" for name in (
        "t_event_control_prev_name_collision", "t_opt_slice",
        "t_opt_slice_no", "t_split_var_types", "t_timing_write_expr")},
    **{name: "expects a process to wake on its own writes (not IEEE "
        "1800-2017 9.4.2)" for name in (
        "t_timing_sched", "t_scheduling_5", "t_finish_stops_nonfinal2")},
    **{name: "depends on the order of same-time-step processes" for name in (
        "t_force_assign", "t_assign_dff", "t_multidriven_simple",
        "t_interface_input_port_assign", "t_interface_input_port_assign_noinl",
        "t_math_eq", "t_tri_select_eqcase", "t_property_if_else",
        "t_unpack_array_no_expand", "t_unpack_array_direct_assignment")},
    **{name: "expects transport behavior from net and assign delays "
        "(IEEE 1800-2017 28.16 is inertial)" for name in (
        "t_net_delay_timing", "t_timing_nested_assignment_on_lhs")},
    "t_opt_split_no": "assumes two-state initial values",
    "t_select_plus": "expects a partly out-of-range part-select write to be "
        "ignored whole (IEEE 1800-2017 11.5.1 writes the in-range bits)",
    "t_clocked_release_combo": "expects release to restore a variable "
        "(IEEE 1800-2017 10.6.2 keeps the forced value)",
    **{name: "expects Verilator's resolution of variable or interface "
        "contention" for name in ("t_tri_assigndly_nba",
        "t_tri_iface_semantics")},
    "t_union_unpacked": "reads an unpacked-union member other than the last "
        "written (implementation-defined)",
    "t_sys_fread": "reads a file the Verilator driver generates",
    "t_assert_nonconsec_rep": "expects Verilator's count; the case notes "
        "other simulators differ",
    "t_assert_pre": "counts actions of a time-zero clock edge race",
    **{name: "procedurally assigns an output port declared with an "
        "implicit data type, which is a net (IEEE 1800-2017 23.2.2.3)"
        for name in ("t_langext_2", "t_langext_4", "t_langext_2012ext",
            "t_langext_2017ext", "t_langext_2023ext")},
    "t_math_pow6": "initializes output net ports with nonconstant "
        "expressions (IEEE 1800-2017 A.2.1.2 list_of_port_identifiers)",
}

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
        # Cases Verilator gates on its constraint solver run: class and
        # scope randomization bound wide variables instead of enumerating
        # them (2026-10-12).
        if re.search(r"have_sc\b|test\.pli_filename|vpi|dpi|sc_main", source, re.I):
            skip = skip or "requires SystemC, PLI/VPI or DPI harness"
        # Verilator extensions and behaviour it marks non-standard are outside
        # the IEEE LRMs (owner decision, 2026-10-12).
        out_of_scope = None
        if top_file.exists():
            verilog = read_text(top_file)
            if VERILATOR_C_TASKS.search(verilog):
                out_of_scope = "out of scope: Verilator $c extension"
            elif name in VERILATOR_NONSTANDARD:
                out_of_scope = "out of scope: " + VERILATOR_NONSTANDARD[name]
        # fails=test.vlt_all marks a Verilator-only diagnostic; other
        # simulators run the model, which may end in an unconditional $stop.
        verilator_only_failure = bool(re.search(
            r"test\.compile\([^)]*fails\s*=\s*test\.vlt", source, re.S))
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
        # `+1364-2005ext+v` selects the Verilog-2005 keywords for the test.
        language = ("systemverilog", "2017")
        for flag in flags:
            for token in flag.split():
                if token.startswith("+1364-2005ext+"):
                    language = ("verilog", "2005")
                elif token.startswith("+1364-2001ext+"):
                    language = ("verilog", "2001")
        compile_args = ["compile", "-q"] + standard_args(*language)
        # `--timing` makes Verilator define VERILATOR_TIMING.
        if any("--timing" in value.split()
               for keyword in ("verilator_flags2", "verilator_flags", "v_flags2")
               for value in python_literal_list(source, keyword) or []):
            compile_args += ["-D", "VERILATOR_TIMING"]
        # driver.py also names the trace file a test may open with
        # $dumpfile(`STRINGIFY(`TEST_DUMPFILE)).
        compile_args += ["-I", str(tdir), "-D", "SIMULATOR", "-D", "TEST_OBJ_DIR=obj_dir",
                         "-D", "TEST_DUMPFILE=obj_dir/simx.vcd"]
        plusargs: list[str] = []
        for flag in flags:
            for token in flag.split():
                if token.startswith("+define+"):
                    # Verilator splits on `+`; a piece that is not a macro
                    # name (`std::process`) defines nothing usable.
                    for definition in token[len("+define+"):].split("+"):
                        if re.fullmatch(r"[A-Za-z_]\w*(=.*)?", definition):
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
        # Modules declared in `include files (t_mailbox_class includes the
        # module of t_mailbox.v) are part of the top file's text.
        included_text = []
        pending = [top_text]
        seen_includes: set[str] = set()
        while pending:
            text = pending.pop()
            for include in re.findall(r'`include\s+"([^"]+)"', text):
                path = tdir / include
                if include in seen_includes or not path.is_file():
                    continue
                seen_includes.add(include)
                body = read_text(path)
                included_text.append(body)
                pending.append(body)
        top_text = "\n".join([top_text, *included_text])
        declared = SV_MODULE_RE.findall(SV_COMMENT_RE.sub(" ", top_text))
        # The driver's shell wraps module t; tests with their own top or no
        # module t run as written.
        use_shell = "t" in declared and "top" not in declared \
            and not re.search(r"make_top_shell\s*=\s*False", source)
        # `--top-module X` (or `--top X`, which may name a configuration)
        # selects the root the driver elaborates.
        # driver.py's main runs the clock until `test.sim_time` (1100 by
        # default).
        sim_time_match = re.search(r"^test\.sim_time\s*=\s*(\d+)\s*$", source, re.M)
        sim_time = int(sim_time_match.group(1)) if sim_time_match else 1100
        explicit_top = None
        for value in python_literal_list(source, "verilator_flags2") or []:
            match = re.search(r"--top(?:-module)?[ =]([A-Za-z_]\w*)", value)
            if match:
                explicit_top = match.group(1)
        # `--work LIB FILE...` compiles the files that follow into library
        # LIB (IEEE 1800-2017 33.2); configurations then select cells from
        # those libraries.
        library_args = list(compile_args)
        library_files: dict[str, list[str]] = {}
        current_library = None
        flag_tokens = [token for value in
                       python_literal_list(source, "verilator_flags2") or []
                       for token in value.split()]
        for index, token in enumerate(flag_tokens):
            if token == "--work" and index + 1 < len(flag_tokens):
                current_library = flag_tokens[index + 1]
            elif current_library and token.startswith("t/") \
                    and token.endswith((".v", ".sv", ".vg")) \
                    and (tdir.parent / token).is_file():
                library_files.setdefault(current_library, []).append(
                    str(tdir.parent / token))
        compile_args.append(str(top_file))
        compile_args += [str(f) for f in verilator_library_files(top_file, tdir)]
        shell = ""
        if explicit_top and explicit_top != "t":
            if explicit_top in declared and re.search(
                    r"\bmodule\s+" + re.escape(explicit_top) + r"\s*\(\s*input\s+clk\b",
                    top_text):
                compile_args.append("verilator_shell.sv")
                tops = ["fsim_verilator_top"]
                shell = verilator_shell(top_text, explicit_top, "fsim_verilator_top",
                                        sim_time)
            else:
                tops = [explicit_top]
                # A configuration with several design cells (33.4.1.1)
                # elaborates each of them as a root.
                config = re.search(r"\bconfig\s+" + re.escape(explicit_top)
                                   + r"\s*;(.*?)\bendconfig\b", top_text, re.S)
                cells = re.search(r"\bdesign\s+([^;]+);", config.group(1)) if config else None
                if cells and len(cells.group(1).split()) > 1:
                    tops = [cell.split(".")[-1] for cell in cells.group(1).split()]
        elif use_shell:
            compile_args.append("verilator_shell.sv")
            tops = ["top"]
            shell = verilator_shell(top_text, sim_time=sim_time)
        else:
            tops = sv_root_modules([top_text])
        commands = [Command("compile", library_args + ["--library", library] + files)
                    for library, files in library_files.items()]
        commands.append(Command("compile", compile_args))
        # driver.py simulates only when the test calls test.execute(); a
        # test that only compiles passes once the model builds.
        executes = "test.execute(" in source
        # Verilator's compile also elaborates, so an expected compile
        # failure may come from fsim's elaborate step.
        elab = ["elaborate", "-q", "--no-aot"]
        for library in library_files:
            elab += ["--search-library", library]
        for top in tops:
            elab += ["--top", top]
        # A compile-only test declaring no module (only classes or
        # packages) has no design to elaborate.
        if tops or executes or compile_fails:
            commands.append(Command("elaborate", elab))
        if not compile_fails:
            if executes:
                # driver.py runs from test_regress: data files are named
                # relative to it (`t/x.mem`) and outputs go to obj_dir/.
                commands.append(Command("simulate", ["simulate", "--engine", "{engine}",
                                                     "--file-root", "."] + plusargs))
        copies: list[tuple[str, str]] = []
        for literal in sorted(set(re.findall(r'"(t/[\w./-]+)"', top_text))):
            candidate = tdir.parent / literal
            if candidate.is_file():
                copies.append((str(candidate), literal))
        cases.append(Case(
            suite="verilator",
            id=name,
            copies=copies,
            group=verilator_group(name),
            commands=commands,
            expect="fail" if (compile_fails or execute_fails) else "pass",
            fail_by="elaborate" if compile_fails else "simulate" if executes else "elaborate",
            checker="verilator",
            # driver.py's execute() checks for "*-* All Finished *-*" only
            # with check_finished=True; otherwise a run passes unless it
            # stops ($stop) or reports %Error.
            checker_data={"allow_stop": verilator_only_failure,
                          "check_finished": executes
                          and not (compile_fails or execute_fails)
                          and bool(re.search(
                              r"test\.execute\([^)]*check_finished\s*=\s*True",
                              source, re.S))},
            writes=([("verilator_shell.sv", shell)] if shell else [])
            + [("obj_dir/.keep", "")],
            skip=skip,
            out_of_scope=out_of_scope,
        ))
    return cases


def verilator_group(name: str) -> str:
    parts = name.split("_")
    return parts[1] if len(parts) > 1 else name


# The delta-cycle cap is an implementation guard, not a language limit:
# these loop millions of `#0` iterations at time 0.
IVTEST_SIMULATE_OPTIONS = {
    "pow_ca_signed": ["--max-deltas", "100000000"],
    "pow_ca_unsigned": ["--max-deltas", "100000000"],
}


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
        # Icarus marks this legal Verilog-2005 case CE because it does not
        # support event arrays. Run its original wake-count checks instead.
        if name == "event_array" and test_type == "CE":
            test_type = "normal"
        directory = fields[2] if len(fields) > 2 else ""
        gold = None
        module = None
        diff = None
        if len(fields) > 3:
            extra = fields[3]
            if extra.startswith("gold="):
                gold = base / "gold" / extra[5:]
            elif extra.startswith("unordered="):
                gold = base / "gold" / extra[10:]
            elif extra.startswith("diff="):
                # diff=work/out:gold/expected[:skip] compares a file the test
                # writes with a gold file, ignoring the first `skip` lines.
                parts = extra[5:].split(":")
                if len(parts) >= 2:
                    diff = [parts[0], str(base / parts[1]),
                            int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 0]
            else:
                module = extra
        source = base / directory / f"{name}.v"
        language, standard = "verilog", "2005"
        compile_args = ["compile", "-q"]
        plusargs = []
        delay_mode: list[str] = []
        skip = None
        for arg in args:
            if arg in ("-Tmin", "-Ttyp", "-Tmax"):
                # iverilog's min/typ/max delay selection.
                delay_mode = ["--delay-mode", arg[2:]]
            elif arg in ("-g2005-sv", "-g2009", "-g2012"):
                language = "systemverilog"
                standard = {"-g2005-sv": "2005", "-g2009": "2009", "-g2012": "2012"}[arg]
            elif arg == "-g2001":
                standard = "2001"
            elif arg in ("-g1995", "-g1"):
                # iverilog's -g1 selects IEEE 1364-1995.
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
        out_of_scope = None
        if source.exists():
            # Icarus extensions are outside the IEEE LRMs (owner decision,
            # 2026-10-12): its own system tasks, and gold output holding
            # Icarus SORRY or WARNING messages.
            text = read_text(source)
            gold_text = read_text(gold) if gold is not None and gold.is_file() else ""
            if name in ICARUS_NONSTANDARD:
                out_of_scope = "out of scope: " + ICARUS_NONSTANDARD[name]
            elif ICARUS_EXTENSION_TASKS.search(text):
                out_of_scope = "out of scope: Icarus system task extension"
            elif test_type != "CE" and ICARUS_EXTENSION_SYNTAX.search(
                    re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', "",
                           text, flags=re.S)):
                out_of_scope = "out of scope: Icarus syntax extension"
            elif re.search(r"^\s*(SORRY|WARNING)\b|^warning: "
                           r"|: (sorry|warning|Warning): ",
                           gold_text, re.M):
                out_of_scope = "out of scope: Icarus diagnostic in gold output"
        compile_args += standard_args(language, standard)
        # iverilog runs from the ivtest root, so an include named
        # "ivltests/x.v" resolves against it as well (IEEE 1800-2017 22.4).
        compile_args += ["-I", str(base / directory), "-I", str(base), str(source)]
        commands = [Command("compile", compile_args)]
        if source.exists():
            # A module may come from an included file.
            texts = [read_text(source)]
            pending = [texts[0]]
            for _ in range(4):
                found = []
                for text in pending:
                    for included in re.findall(r'`include\s+"([^"]+)"', text):
                        for root in (base / directory, base):
                            if (root / included).is_file():
                                found.append(read_text(root / included))
                                break
                texts += found
                pending = found
            tops = [module] if module else sv_root_modules(texts)
            elab = ["elaborate", "-q", "--no-aot"] + delay_mode
            for top in tops:
                elab += ["--top", top]
            # iverilog's compile step includes elaboration, so a CE test
            # may be rejected by fsim's elaborate as well, and a CO
            # (compile-only) test passes once it elaborates.
            commands.append(Command("elaborate", elab))
            if test_type not in ("CE", "CO"):
                # ivtest runs from its root: relative file names resolve
                # against the work directory.
                commands.append(Command("simulate", ["simulate", "--engine", "{engine}",
                                                     "--file-root", "."] + plusargs
                                        + IVTEST_SIMULATE_OPTIONS.get(name, [])))
        expect = "fail" if test_type in ("CE", "RE") else "pass"
        # ivtest runs from its root, so data files named by a relative path
        # ("ivltests/mem1.dat") are copied into the case's work directory.
        copies: list[tuple[str, str]] = []
        if source.exists():
            for literal in set(re.findall(r'"([\w./-]+\.\w+)"', read_text(source))):
                candidate = base / literal
                if not literal.startswith(("/", "..")) and candidate.is_file() \
                        and candidate != source:
                    copies.append((str(candidate), literal))
        cases.append(Case(
            suite="ivtest",
            id=name,
            copies=copies,
            # iverilog's harness provides a work/ directory for outputs.
            writes=[("work/.keep", "")],
            group=directory or "ivltests",
            commands=commands,
            expect=expect,
            fail_by="elaborate" if test_type == "CE" else "simulate",
            checker="ivtest",
            checker_data={"gold": str(gold) if gold else None, "type": test_type,
                          "diff": diff},
            skip=skip,
            out_of_scope=out_of_scope,
            timeout=300.0 if name in IVTEST_SIMULATE_OPTIONS else None,
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


# Billowitch's VESTs suite runs these file writers before their corresponding
# readers in one workspace. Keep each pair together so readers consume the
# file produced by this implementation, not a serialized fixture from GHDL.
VESTS_BILLOWITCH_FILE_ROUNDTRIPS = (
    ("tc624.vhd", "tc627.vhd", "iofile.36"),
    ("tc632.vhd", "tc633.vhd", "iofile.38"),
    ("tc635.vhd", "tc636.vhd", "iofile.39"),
    ("tc634.vhd", "tc639.vhd", "iofile.43"),
    ("tc642.vhd", "tc644.vhd", "iofile.44"),
    ("tc648.vhd", "tc651.vhd", "iofile.01"),
    ("tc664.vhd", "tc665.vhd", "iofile.11"),
    ("tc666.vhd", "tc667.vhd", "iofile.13"),
    ("tc668.vhd", "tc669.vhd", "iofile.15"),
    ("tc671.vhd", "tc673.vhd", "iofile.51"),
    ("tc690.vhd", "tc691.vhd", "iofile.53"),
    ("tc708.vhd", "tc709.vhd", "iofile.59"),
    ("tc713w.vhd", "tc713.vhd", "iofile.02"),
    ("tc3190.vhd", "tc3193.vhd", "iofile.08"),
    ("tc3187.vhd", "tc3191.vhd", "iofile.04"),
    ("tc3194.vhd", "tc3195.vhd", "iofile.09"),
    ("tc3198.vhd", "tc3199.vhd", "iofile.12"),
    ("tc3200.vhd", "tc3201.vhd", "iofile.14"),
    ("tc3203.vhd", "tc3202.vhd", "iofile.61"),
    ("tc3205.vhd", "tc3206.vhd", "iofile.64"),
)


def vests_file_chains(
    group: str, roundtrips: tuple[tuple[str, str, str], ...]
) -> tuple[str, dict[str, str], dict[str, str]]:
    """The chain prefix of a VESTs group's writer/reader file pairs, the
    chain each case joins, and the file each chain's writer leaves."""
    by_case = {
        case_name: f"{group}:file:{Path(writer).stem}-{Path(reader).stem}"
        for writer, reader, _filename in roundtrips
        for case_name in (writer, reader)
    }
    outputs = {
        f"vests:{by_case[writer]}": filename
        for writer, _reader, filename in roundtrips
    }
    return f"vests:{group}:file:", by_case, outputs


(
    VESTS_BILLOWITCH_FILE_CHAIN_PREFIX,
    VESTS_BILLOWITCH_FILE_CHAIN_BY_CASE,
    VESTS_BILLOWITCH_FILE_CHAIN_OUTPUTS,
) = vests_file_chains("billowitch", VESTS_BILLOWITCH_FILE_ROUNDTRIPS)
VESTS_CLIFTON_FILE_ROUNDTRIPS = (
    (
        "functional/textio/write/character_array_write.vhd",
        "functional/textio/read/character_array_read.vhdl",
        "character_array.file",
    ),
)
(
    VESTS_CLIFTON_FILE_CHAIN_PREFIX,
    VESTS_CLIFTON_FILE_CHAIN_BY_CASE,
    VESTS_CLIFTON_FILE_CHAIN_OUTPUTS,
) = vests_file_chains("clifton", VESTS_CLIFTON_FILE_ROUNDTRIPS)
VESTS_FILE_CHAIN_OUTPUTS = {
    **VESTS_BILLOWITCH_FILE_CHAIN_OUTPUTS,
    **VESTS_CLIFTON_FILE_CHAIN_OUTPUTS,
}
VESTS_FILE_CHAIN_PREFIXES = (
    VESTS_BILLOWITCH_FILE_CHAIN_PREFIX,
    VESTS_CLIFTON_FILE_CHAIN_PREFIX,
)
# VESTs cases outside the LRM, by (group directory, case path).
VESTS_OUT_OF_SCOPE = {
    ("clifton-labs/compliant",
     "functional/attributes/type/range/simple-range-attribute.vhdl"): (
        "out of scope: applies scalar bound attributes to A'RANGE; "
        "RANGE denotes a range, not a scalar type or subtype "
        "(IEEE 1076-1993 14.1)"
    ),
    ("billowitch/non_compliant/analyzer_failure", "tc300.vhd"): (
        "out of scope: expects compile-time detection of REAL range "
        "overflow; REAL bounds are host-dependent and implementations "
        "need not detect predefined floating-point overflow "
        "(IEEE 1076-1993 3.1.4/3.1.4.1)"
    ),
}


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
        # Libraries a chained group has compiled into, and those its script
        # deletes before the next case (`delete_lib`): a later chapter may
        # reuse an earlier chapter's unit names.
        compiled_libraries: set[str] = set()
        pending_deletes: list[str] = []
        for command, positional, options in parse_exp(exp):
            if command == "create_lib" and positional:
                libraries.append(positional[0])
                continue
            if command == "delete_lib" and positional and chain:
                if positional[0] in compiled_libraries:
                    compiled_libraries.discard(positional[0])
                    pending_deletes.append(positional[0])
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
            commands = [Command("compile", ["library", "delete", lib])
                        for lib in pending_deletes]
            pending_deletes = []
            commands.append(Command("compile", compile_args))
            compiled_libraries.add(library)
            text = read_text(source)
            checker_data: dict = {"outputs": []}
            case_chain = chain
            skip = VESTS_OUT_OF_SCOPE.get((directory, relative))
            if directory == "billowitch/compliant":
                case_chain = VESTS_BILLOWITCH_FILE_CHAIN_BY_CASE.get(relative, chain)
            elif directory == "clifton-labs/compliant":
                case_chain = VESTS_CLIFTON_FILE_CHAIN_BY_CASE.get(relative, chain)
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
                    # A configuration of the chosen entity is the intended
                    # top: it supplies the component bindings.
                    owned = [c for c, e in configs.items() if e == entity.lower()]
                    # The billowitch harness elaborates the last `XXXent`
                    # entity itself (`ghdl --elab-run`), never a
                    # configuration declared for it.
                    if use_last_entity:
                        owned = []
                    top = entity.lower() if entity.lower() in configs \
                        else owned[-1] if owned \
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
                chain=f"vests:{case_chain}" if case_chain else None,
                skip=skip,
            ))
    return cases


def vests_group(directory: str, relative: str) -> str:
    if directory.startswith("billowitch"):
        return directory.split("/")[-1]
    if directory.startswith("clifton"):
        return "clifton/" + relative.split("/")[1] if "/" in relative else "clifton"
    match = re.match(r"(ch_\d+|ap_\w)", relative)
    return "ashenden/" + (match.group(1) if match else "misc")


# nvc cases that need extensions or expect behavior contrary to the LRM.
NVC_NONSTANDARD = {
    "agg7": (
        "expects a descending array-valued range association in an "
        "ascending constrained target (IEEE 1076-2008 9.3.3.3)"
    ),
    "attr16": (
        "applies scalar bound attributes to A'RANGE; RANGE denotes a range, "
        "not a scalar type or subtype (IEEE 1076-2008 16.2.2/16.2.3)"
    ),
    "integer2": (
        "expects INTEGER'HIGH + 1 to wrap to signed-64 INTEGER'LEFT; "
        "out-of-range INTEGER arithmetic is erroneous "
        "(IEEE 1076-2019 5.2.3.1)"
    ),
    # `a ~& b` and `a ~| b` are not binary operators in IEEE 1364 or 1800.
    "binary8": "uses ~& and ~| as binary operators",
    "wide7": "uses ~& and ~| as binary operators",
}

NVC_SKIP_FLAGS = {
    "vhpi", "wave", "cover", "tcl", "shell", "plugin", "gtkw", "relaxed", "relax",
    "dump-arrays", "export", "define", "H", "exit", "mixed",
}

# This source is legal VHDL-93, but its function returns a record aggregate
# whose `f` value is outside `integer range 1 to n`. IEEE 1076 requires the
# record-element subtype check during aggregate evaluation, so this one NVC
# regression case is expected to report the runtime constraint failure.
NVC_EXPECTED_RUNTIME_ERRORS = {
    "issue502": ("FSIM-RUN-0001", "VHDL integer subtype range check failed", 1),
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
            elif key.startswith("guut."):
                skip = skip or "hierarchical generic override"
            elif key.startswith("g") and value and len(key) > 1:
                generics.append(f"{key[1:]}={value}")
            elif key in ("seed", "slow", "!windows", "O0", "O2", "no-collapse",
                         "shuffle", "per-file", "psl", "work", "stop"):
                if key == "work":
                    skip = skip or "requires named work library"
                if key == "stop":
                    skip = skip or "nvc stop-delta option"
                if key == "shuffle":
                    # Process execution order within a cycle is unspecified
                    # (IEEE 1076-2008 14.7.5); the test asserts nvc's
                    # --shuffle reordering.
                    skip = skip or "nvc --shuffle process order"
            elif key.startswith("+") or key.startswith("$"):
                skip = skip or f"nvc option {flag}"
            elif key in NVC_SKIP_FLAGS:
                skip = skip or f"nvc-specific test kind {key}"
        if name in NVC_NONSTANDARD:
            skip = skip or "out of scope: " + NVC_NONSTANDARD[name]
        if verilog:
            source = next((base / f"{name}{ext}" for ext in (".v", ".sv")
                           if (base / f"{name}{ext}").exists()), base / f"{name}.v")
            # nvc's Verilog tests use SystemVerilog operators (`++`, `+=`) in
            # .v files; like the Verilator suite, they compile as
            # SystemVerilog.
            compile_args = ["compile", "-q"] + standard_args(
                "systemverilog", "2017")
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
            expect="fail" if (
                "fail" in kinds or name in NVC_EXPECTED_RUNTIME_ERRORS
            ) else "pass",
            expected_runtime_error=NVC_EXPECTED_RUNTIME_ERRORS.get(name),
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


# Run-wide placeholder values such as "{uvm_library:2020.3.1}", set by
# command_run before any case starts.
PLACEHOLDERS: dict[str, str] = {}


def substitute(argument: str, engine: str) -> str:
    argument = argument.replace("{engine}", engine)
    for placeholder, value in PLACEHOLDERS.items():
        argument = argument.replace(placeholder, value)
    return argument


def prepare_uvm_libraries(cases: list[Case], fsim: Path, root: Path, out: Path,
                          quiet: bool) -> None:
    """Compile each UVM release that the selected cases use once, into a
    library `uvm` that the cases map, instead of compiling uvm_pkg.sv with
    every test."""
    releases = sorted({c.uvm_release for c in cases if c.uvm_release and not c.skip})
    for release in releases:
        # Absolute: each case maps the library from its own working directory.
        workspace = (out / "shared" / f"uvm-{release}").resolve()
        remove_tree(workspace)
        workspace.mkdir(parents=True, exist_ok=True)
        uvm_src = root / "uvm" / f"uvm-{release}" / "src"
        args = [str(fsim), "compile", "-q", "--standard", "2017", "--library", "uvm",
                "--uvm-release", release, "-I", str(uvm_src), str(uvm_src / "uvm_pkg.sv")]
        started = time.monotonic()
        completed = subprocess.run(args, cwd=workspace, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        (workspace / "compile.log").write_bytes(completed.stdout)
        if not quiet or completed.returncode != 0:
            print(f"UVM {release} library: exit {completed.returncode} in "
                  f"{time.monotonic() - started:.0f}s", flush=True)
        PLACEHOLDERS["{uvm_library:" + release + "}"] = str(
            workspace / ".fsim" / "libraries" / "uvm")


def run_command(fsim: Path, command: Command, workdir: Path, env: dict,
                timeout: float, engine: str) -> StepResult:
    args = [str(fsim)] + [substitute(a, engine) for a in command.args]
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
    # Gold comparisons need the whole output; only very large logs are cut.
    if len(text) > 16_000_000:
        text = text[:8_000_000] + "\n...[truncated]...\n" + text[-8_000_000:]
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



def ivtest_source_location_lines(case_id: str, lines: list[str]) -> list[str]:
    """Map only known __FILE__ transport paths in the two location probes."""
    # IEEE 1800-2017 22.13 permits implementation-dependent path formatting.
    # These gold files use Icarus's ./ivltests paths; fsim opens archived
    # sources. Keep every filename, line number, label and other output exact.
    files = {
        "br_gh782a": {"br_gh782a.v": "sources/roots/root",
                       "br_gh782a.vi": "includes/roots"},
        "br_gh782b": {"br_gh782b.v": "sources/roots/root"},
        "fileline": {"fileline.v": "sources/roots/root"},
    }.get(case_id, {})
    result = []
    for line in lines:
        bare = re.fullmatch(
            r"objects/[0-9a-f]{64}/(sources/roots/root)/[0-9]{8}/([^/ ]+)",
            line)
        if bare and files.get(bare[2]) == bare[1]:
            result.append(f"./ivltests/{bare[2]}")
            continue
        match = re.fullmatch(
            r"(File|Included file) objects/[0-9a-f]{64}/"
            r"(sources/roots/root|includes/roots)/[0-9]{8}/"
            r"([^/ ]+) line ([0-9]+)", line)
        if match and files.get(match[3]) == match[2]:
            line = f"{match[1]} ./ivltests/{match[3]} line {match[4]}"
        result.append(line)
    return result


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
               ast.In, ast.NotIn,
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
        bad = next(s for s in steps if s.returncode is not None
                   and (s.returncode < 0 or s.returncode >= 126))
        return "crash", f"{bad.stage} exited with {bad.returncode}"
    failed_step = next((s for s in steps if s.returncode != 0), None)
    sim = next((s for s in steps if s.stage == "simulate"), None)
    sim_output = sim.output if sim else ""
    hdl_errors = [line for line in sim_output.splitlines() if HDL_ERROR_RE.search(line)]

    expected_runtime_error = getattr(case, "expected_runtime_error", None)
    if expected_runtime_error is not None:
        if case.expect != "fail":
            return "fail", "expected runtime diagnostic configured for a passing case"
        by_stage = {step.stage: step for step in steps}
        for stage in ("compile", "elaborate"):
            prerequisite = by_stage.get(stage)
            if prerequisite is None or prerequisite.returncode != 0:
                return "fail", f"{stage} did not pass before expected runtime error"
        if sim is None:
            return "fail", "simulation did not run for expected runtime error"
        if sim.returncode is None or sim.returncode == 0:
            return "fail", "simulation did not fail with the expected runtime error"
        expected_code, expected_message, expected_exit_code = expected_runtime_error
        if sim.returncode != expected_exit_code:
            return "fail", (f"simulation exited with {sim.returncode}, expected "
                             f"normal runtime-failure status {expected_exit_code}")
        for line in sim_output.splitlines():
            match = DIAGNOSTIC_RE.search(line)
            if (match and match.group(2) == expected_code
                    and expected_message in match.group(3)):
                return "pass", f"expected runtime diagnostic {expected_code} reported"
        return "fail", (f"simulation failed without expected runtime diagnostic "
                         f"{expected_code}: {expected_message}")

    if case.expect == "fail":
        if failed_step is not None:
            if STAGE_ORDER[failed_step.stage] <= STAGE_ORDER[case.fail_by]:
                return "pass", f"rejected at {failed_step.stage}"
            return "fail", f"rejected late at {failed_step.stage}"
        if case.checker in ("vhdl_clean", "ivtest", "verilator") and hdl_errors:
            return "pass", "runtime error reported"
        # The Verilator harness counts $stop as a failing run, as it does for
        # expected-pass cases below.
        if case.checker == "verilator" \
                and re.search(r"^\$stop paused the simulation", sim_output, re.M):
            return "pass", "stopped"
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
        if re.search(r"^\$stop paused the simulation", sim_output, re.M) \
                and not case.checker_data.get("allow_stop"):
            return "fail", "test stopped ($stop)"
        if case.checker_data.get("check_finished") and "*-* All Finished *-*" not in sim_output:
            return "fail", "missing *-* All Finished *-*"
        return "pass", ""
    if case.checker == "ivtest":
        gold = case.checker_data.get("gold")
        lines = simulation_lines(sim_output)
        diff = case.checker_data.get("diff")
        if diff:
            written, expected_file, skip = diff
            produced = workdir / written
            if not produced.is_file() or not Path(expected_file).is_file():
                return "fail", "compared output file missing"
            ours = read_text(produced).splitlines()[skip:]
            theirs = read_text(Path(expected_file)).splitlines()[skip:]
            if [l.rstrip() for l in ours] != [l.rstrip() for l in theirs]:
                return "fail", "output file differs from gold"
            return "pass", ""
        if gold:
            expected = [l.rstrip() for l in read_text(Path(gold)).splitlines()
                        if not re.match(r"^(VCD info:|.*\$finish called|.*: warning|.*:\d+\s+warning)", l)]
            actual = ivtest_source_location_lines(
                case.id, [l.rstrip() for l in lines])
            actual = [l for l in actual if not l.startswith("objects/")]
            if expected != actual:
                return "fail", "output differs from gold"
            return "pass", ""
        if any(re.fullmatch(r"\s*passed\s*", line, re.I) for line in lines):
            return "pass", ""
        if case.checker_data.get("type") == "CO":
            return "pass", "compiled"
        return "fail", "no PASSED line"
    if case.checker == "vhdl_clean":
        if hdl_errors:
            return "fail", "error assertion: " + hdl_errors[0][-160:]
        # GHDL's VESTs harness (testsuite.sh) records OUTPUT= files but never
        # compares them: the iofiles are inputs for the read tests, and a
        # typed file's representation is implementation defined.
        return "pass", ""
    if case.checker == "vunit":
        failed_test = next((test for test in vunit_subresults(case, steps)
                            if test["status"] != "pass"), None)
        if failed_test is not None:
            return "fail", (f"VUnit test {failed_test['test']} failed: "
                             f"{failed_test['detail']}")
        return "pass", ""
    if hdl_errors:
        return "fail", hdl_errors[0][-160:]
    return "pass", ""


def vunit_subresults(case: Case, steps: list[StepResult]) -> list[dict]:
    """Classify expected VUnit tests and reject incomplete runner transcripts."""
    tests = case.checker_data["tests"]
    sim = next((s for s in steps if s.stage == "simulate"), None)
    if sim is None:
        stage = steps[-1].stage if steps else "run"
        detail = f"{stage} failed before simulation"
        return [{"test": test, "status": "fail", "detail": detail}
                for test in tests] or [
                    {"test": "<default>", "status": "fail",
                     "detail": "no VUnit tests configured"}]
    if not tests:
        return [{"test": "<default>", "status": "fail",
                 "detail": "no VUnit tests configured"}]

    test_names = {test for test in tests if test != "<default>"}
    start_counts = collections.Counter()
    failures = collections.defaultdict(list)
    done_count = 0
    done_failures = None
    emitted_failures = 0
    after_done = False
    current = "<default>"
    for line in sim.output.splitlines():
        match = re.search(r"VUNIT-SHIM: (start|fail|done):? ?(.*)", line)
        if match:
            kind, rest = match.groups()
            if after_done:
                failures["<default>"].append(
                    "VUnit marker appeared after test_runner_cleanup")
            if kind == "start":
                name = rest.strip()
                if name not in test_names:
                    failures["<default>"].append(
                        f"unexpected VUnit test start: {name}")
                start_counts[name] += 1
                if start_counts[name] > 1:
                    failures[name].append("test case started more than once")
                current = name
            elif kind == "fail":
                emitted_failures += 1
                # Failures before the first named test remain suite-level.
                failures[current].append(rest.strip())
            else:
                done_count += 1
                after_done = True
                if done_count > 1:
                    failures["<default>"].append(
                        "test_runner_cleanup emitted more than one done marker")
                count = re.search(r"\bfailures=(\d+)\b", rest)
                if count:
                    done_failures = int(count.group(1))
                else:
                    failures["<default>"].append(
                        "test_runner_cleanup omitted its failure count")
        elif HDL_ERROR_RE.search(line):
            # Runtime errors belong to the currently active test, or to the
            # suite if no expected named test has started yet.
            failures[current].append(line[-160:])
            if after_done:
                failures["<default>"].append(
                    "HDL error appeared after test_runner_cleanup")

    if done_count != 1:
        failures["<default>"].append(
            "simulation ended without exactly one test_runner_cleanup marker")
    if done_failures is not None and done_failures != emitted_failures:
        failures["<default>"].append(
            f"test_runner_cleanup reported {done_failures} failure(s), "
            f"but {emitted_failures} fail marker(s) were emitted")

    results = []
    for test in tests:
        if test == "<default>":
            if failures.get(test):
                results.append({"test": test, "status": "fail",
                                "detail": failures[test][0][:200]})
            elif done_count != 1:
                results.append({"test": test, "status": "fail",
                                "detail": "invalid test_runner_cleanup transcript"})
            else:
                results.append({"test": test, "status": "pass", "detail": ""})
        elif start_counts[test] == 0:
            results.append({"test": test, "status": "fail",
                            "detail": "test case never ran"})
        elif failures.get(test):
            results.append({"test": test, "status": "fail",
                            "detail": failures[test][0][:200]})
        elif done_count != 1:
            results.append({"test": test, "status": "fail",
                            "detail": "invalid test_runner_cleanup transcript"})
        else:
            results.append({"test": test, "status": "pass", "detail": ""})

    # Preserve suite-level failures when the expected list contains named tests.
    if failures.get("<default>") and "<default>" not in tests:
        results.append({"test": "<default>", "status": "fail",
                        "detail": failures["<default>"][0][:200]})
    return results


def run_case(case: Case, fsim: Path, workdir: Path, engine: str, default_timeout: float,
             keep: bool, chain_outputs: set[str] | None = None) -> dict:
    record = run_case_once(case, fsim, workdir, engine, default_timeout, keep,
                           chain_outputs)
    if case.out_of_scope and record.get("status") not in ("pass", "skip"):
        record["detail"] = f"{case.out_of_scope} ({record.get('detail', '')})"
        record["status"] = "skip"
    return record


def run_case_once(case: Case, fsim: Path, workdir: Path, engine: str,
                  default_timeout: float, keep: bool,
                  chain_outputs: set[str] | None = None) -> dict:
    record = {"suite": case.suite, "id": case.id, "group": case.group, "expect": case.expect}
    if case.skip:
        record.update(status="skip", detail=case.skip)
        return record
    workdir.mkdir(parents=True, exist_ok=True)
    for source, destination in case.copies:
        if chain_outputs and destination in chain_outputs:
            continue
        target = workdir / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            shutil.copyfile(source, target)
        except OSError as error:
            record.update(status="harness", detail=f"copy failed: {error}")
            return record
    for destination, content in case.writes:
        (workdir / destination).parent.mkdir(parents=True, exist_ok=True)
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
        record["tests"] = vunit_subresults(case, steps)
    if status != "pass":
        tail = [l for l in failing.output.splitlines() if l.strip()][-6:]
        record["tail"] = tail
    if status == "pass" and not keep and case.chain is None:
        remove_tree(workdir)
    return record


def run_chain(cases: list[Case], fsim: Path, workdir: Path, engine: str, timeout: float,
              keep: bool) -> list[dict]:
    chain_outputs = {
        VESTS_FILE_CHAIN_OUTPUTS[case.chain]
        for case in cases
        if case.chain in VESTS_FILE_CHAIN_OUTPUTS
    }
    workdir.mkdir(parents=True, exist_ok=True)
    workdir_root = workdir.resolve()
    for filename in chain_outputs:
        target = workdir / filename
        try:
            target.resolve().relative_to(workdir_root)
        except (OSError, ValueError):
            continue
        if target.is_file() or target.is_symlink():
            target.unlink()
    return [run_case(case, fsim, workdir, engine, timeout, keep, chain_outputs)
            for case in cases]


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
    all_cases: list[Case] = []
    for suite in suites:
        all_cases += ADAPTERS[suite](args.root)
    cases = all_cases
    if args.filter:
        pattern = re.compile(args.filter)
        cases = [c for c in cases if pattern.search(f"{c.suite}/{c.id}")]
    if getattr(args, "rerun_from", None):
        # Re-run only cases that did not pass in an earlier run, optionally
        # restricted to those whose first diagnostic has the given code.
        wanted = set()
        for line in args.rerun_from.read_text().splitlines():
            record = json.loads(line)
            if record["status"] in ("pass", "skip"):
                continue
            first = (record.get("diagnostics") or [{}])[0].get("code")
            if args.rerun_code and first != args.rerun_code:
                continue
            wanted.add((record["suite"], record["id"]))
        cases = [c for c in cases if (c.suite, c.id) in wanted]
    file_chains = {
        case.chain for case in cases
        if case.chain and any(case.chain.startswith(prefix)
                              for prefix in VESTS_FILE_CHAIN_PREFIXES)
    }
    if file_chains:
        selected_ids = {(case.suite, case.id) for case in cases}
        cases = [
            case for case in all_cases
            if (case.suite, case.id) in selected_ids or case.chain in file_chains
        ]
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
    prepare_uvm_libraries(cases, fsim, args.root, out, args.quiet)
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
    p_run.add_argument("--rerun-from", type=Path,
                       help="results.jsonl whose non-passing cases to re-run")
    p_run.add_argument("--rerun-code",
                       help="with --rerun-from, only cases failing first with this code")
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
