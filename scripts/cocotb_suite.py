#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run cocotb's own regression suite against fsim.

cocotb's checkouts and virtual environments live outside the repository, by
default under .local-artifacts/cocotb (src-2.1 with venv-2.1, src-master with
venv-master). The suite is used unmodified: each test environment gets the
in-repo src/cocotb/Makefile.fsim as cocotb_tools/makefiles/simulators/
Makefile.fsim and a .pth file that imports fsim_cocotb, so SIM=fsim and
get_runner("fsim") work.

Configurations:
  verilog   TOPLEVEL_LANG=verilog (VPI)
  vhdl      TOPLEVEL_LANG=vhdl, VHDL_GPI_INTERFACE=vhpi (VHPI)

As in cocotb's own CI there is no separate mixed configuration: the suites
built on mixed-language designs (the uart2bus tests, examples/mixed_language)
run with both interfaces in whichever configuration selects them.

Each run copies cocotb's tests/ and examples/ into the run directory and
runs every Makefile suite (tests/test_cases/*, the examples run_tests.py
names) and the pytest tests marked simulator_required. Results are one TSV
row per test case (suite, test, status), compared against a baseline like
the corpus sweeps; excluded cases (scripts/cocotb_exclusions.tsv) need an
LRM-based reason.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import fnmatch
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_ROOT = REPO / ".local-artifacts" / "cocotb"
EXCLUSIONS = REPO / "scripts" / "cocotb_exclusions.tsv"
VERSIONS = ("2.1", "master")
CONFIGURATIONS = {
    "verilog": {"TOPLEVEL_LANG": "verilog"},
    "vhdl": {"TOPLEVEL_LANG": "vhdl", "VHDL_GPI_INTERFACE": "vhpi"},
}
EXAMPLES = (
    "adder/tests",
    "simple_dff",
    "matrix_multiplier/tests",
    "mixed_language/tests",
)


def venv_python(root: Path, version: str) -> Path:
    return root / f"venv-{version}" / "bin" / "python"


def site_packages(python: Path) -> Path:
    output = subprocess.run(
        [str(python), "-c", "import sysconfig; print(sysconfig.get_paths()['purelib'])"],
        check=True, text=True, capture_output=True).stdout
    return Path(output.strip())


# Registers the fsim runner when cocotb_tools.runner is first imported, so
# that interpreters that never use the runners (python -m cocotb_tools.config)
# import nothing early.
REGISTRATION_HOOK = """\
import importlib.abc
import importlib.util
import sys


class _FsimRunnerRegistration(importlib.abc.MetaPathFinder):
    def find_spec(self, name, path, target=None):
        if name != "cocotb_tools.runner":
            return None
        sys.meta_path.remove(self)
        spec = importlib.util.find_spec(name)
        if spec is None or spec.loader is None:
            return spec
        execute = spec.loader.exec_module

        def execute_and_register(module):
            execute(module)
            import fsim_cocotb  # noqa: F401

        spec.loader.exec_module = execute_and_register
        return spec


sys.meta_path.insert(0, _FsimRunnerRegistration())
"""


def install(root: Path, version: str) -> None:
    """Puts Makefile.fsim and the fsim runner registration into a test venv."""
    packages = site_packages(venv_python(root, version))
    simulators = packages / "cocotb_tools" / "makefiles" / "simulators"
    shutil.copyfile(REPO / "src" / "cocotb" / "Makefile.fsim",
                    simulators / "Makefile.fsim")
    (packages / "fsim_cocotb_registration.py").write_text(REGISTRATION_HOOK)
    (packages / "fsim_cocotb.pth").write_text(
        f"{REPO / 'src' / 'cocotb'}\nimport fsim_cocotb_registration\n")


def suites(tree: Path, configuration: str) -> list[Path]:
    found = sorted(makefile.parent
                   for makefile in (tree / "tests" / "test_cases").glob("*/Makefile"))
    found += [tree / "examples" / example for example in EXAMPLES]
    if configuration == "verilog" and (tree / "examples" / "first_steps").is_dir():
        found.append(tree / "examples" / "first_steps")
    return [suite for suite in found if (suite / "Makefile").is_file()]


def read_results(files: list[Path]) -> list[tuple[str, str]]:
    """The (test, status) pairs of JUnit results files."""
    rows: list[tuple[str, str]] = []
    for results in files:
        try:
            tree = ElementTree.parse(results)
        except ElementTree.ParseError:
            rows.append((results.name, "results_error"))
            continue
        for case in tree.iter("testcase"):
            name = f"{case.get('classname', '')}.{case.get('name', '')}".strip(".")
            if case.find("failure") is not None or case.find("error") is not None:
                status = "fail"
            elif case.find("skipped") is not None:
                status = "skip"
            else:
                status = "pass"
            rows.append((name, status))
    return rows


def run_suite(suite: Path, tree: Path, environment: dict[str, str],
              timeout: int) -> list[tuple[str, str, str]]:
    name = suite.relative_to(tree).as_posix()
    command = ["make", "-k"]
    if (tree / "examples") in suite.parents:
        command += [f"SIM={environment['SIM']}",
                    f"TOPLEVEL_LANG={environment['TOPLEVEL_LANG']}"]
    log = suite / "fsim-suite.log"
    with log.open("w") as stream:
        try:
            completed = subprocess.run(
                command, cwd=suite, env={**environment, "PWD": str(suite)},
                stdout=stream, stderr=subprocess.STDOUT, timeout=timeout,
                check=False)
            code = completed.returncode
        except subprocess.TimeoutExpired:
            code = None
    # A suite that checks its own expected failures removes results.xml when
    # the check passes, so only results.xml files count and make's status
    # decides the rest (combined_results.xml is the check's input).
    rows = [(name, test, status)
            for test, status in read_results(sorted(suite.rglob("results.xml")))]
    if not rows:
        status = "timeout" if code is None else ("no_results" if code else "pass")
        rows = [(name, "-", status)]
    elif code is None:
        rows.append((name, "-", "timeout"))
    elif code and all(status != "fail" for _, _, status in rows):
        rows.append((name, "-", "make_error"))
    return rows


def run_pytest(tree: Path, environment: dict[str, str], python: Path,
               output: Path, timeout: int) -> list[tuple[str, str, str]]:
    report = output / "pytest-results.xml"
    log = output / "pytest.log"
    with log.open("w") as stream:
        try:
            subprocess.run(
                # test_ipython_support runs no simulator
                [str(python), "-m", "pytest", "-v", "-m", "simulator_required",
                 "--ignore=tests/pytest/test_ipython_support.py",
                 f"--junitxml={report}", "tests/pytest"],
                cwd=tree, env=environment, stdout=stream,
                stderr=subprocess.STDOUT, timeout=timeout, check=False)
        except subprocess.TimeoutExpired:
            return [("tests/pytest", "-", "timeout")]
    if not report.is_file():
        return [("tests/pytest", "-", "no_results")]
    return [("tests/pytest", test, status)
            for test, status in read_results([report])]


def run(arguments: argparse.Namespace, version: str, configuration: str) -> Path:
    root = arguments.root
    source = root / f"src-{version}"
    output = root / "runs" / arguments.label / f"{version}-{configuration}"
    tree = output / "tree"
    if tree.exists():
        raise SystemExit(f"{tree} exists; use a new label")
    output.mkdir(parents=True)
    for part in ("tests", "examples", "scripts"):
        if (source / part).is_dir():
            shutil.copytree(source / part, tree / part, symlinks=True)
    # pytest's markers and test paths
    shutil.copy2(source / "pyproject.toml", tree / "pyproject.toml")
    install(root, version)
    python = venv_python(root, version)
    environment = os.environ.copy()
    environment["PATH"] = os.pathsep.join(
        [str(arguments.fsim.resolve().parent), str(python.parent),
         environment.get("PATH", "")])
    environment["VIRTUAL_ENV"] = str(python.parent.parent)
    environment["SIM"] = "fsim"
    environment.update(CONFIGURATIONS[configuration])
    # As cocotb's noxfile configure_test_env (its debug logging left out).
    environment["PYTHONWARNINGS"] = (
        "error,ignore::DeprecationWarning:site,ignore:coroutine :RuntimeWarning")
    if version == "master":
        environment["COCOTB_PREVIEW"] = "1"
    rows: list[tuple[str, str, str]] = []
    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as pool:
        futures = [pool.submit(run_suite, suite, tree, environment,
                               arguments.timeout)
                   for suite in suites(tree, configuration)
                   if selected(suite.relative_to(tree).as_posix(), arguments.suite)]
        for future in futures:
            rows += future.result()
    if arguments.suite:
        # A subset run: compare checks only the suites it ran.
        (output / "subset").write_text("\n".join(arguments.suite) + "\n")
    elif not arguments.no_pytest:
        rows += run_pytest(tree, environment, python, output, arguments.timeout * 4)
    with (output / "results.tsv").open("w", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(("suite", "test", "status"))
        writer.writerows(sorted(rows))
    counts: dict[str, int] = {}
    for _, _, status in rows:
        counts[status] = counts.get(status, 0) + 1
    print(f"{version}-{configuration}: "
          + ", ".join(f"{status} {count}" for status, count in sorted(counts.items())))
    return output / "results.tsv"


def selected(suite: str, patterns: list[str] | None) -> bool:
    """Whether a suite (tests/test_cases/test_assign, say) matches --suite."""
    return not patterns or any(fnmatch.fnmatch(suite, pattern)
                               for pattern in patterns)


def load(path: Path) -> dict[tuple[str, str], str]:
    with path.open(newline="") as stream:
        return {(row["suite"], row["test"]): row["status"]
                for row in csv.DictReader(stream, delimiter="\t")}


def exclusions() -> dict[tuple[str, str], str]:
    if not EXCLUSIONS.is_file():
        return {}
    excluded: dict[tuple[str, str], str] = {}
    with EXCLUSIONS.open(newline="") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if row and not row[0].startswith("#") and len(row) >= 3:
                excluded[(row[0], row[1])] = row[2]
    return excluded


def compare(arguments: argparse.Namespace) -> int:
    runs = arguments.root / "runs"
    losses = 0
    for current in sorted((runs / arguments.label).glob("*/results.tsv")):
        baseline = runs / arguments.baseline / current.parent.name / "results.tsv"
        now = load(current)
        excluded = exclusions()
        in_scope = {key: status for key, status in now.items() if key not in excluded}
        passed = sum(status in ("pass", "skip") for status in in_scope.values())
        line = (f"{current.parent.name}: {passed}/{len(in_scope)} passed, "
                f"{len(now) - len(in_scope)} excluded")
        if baseline.is_file():
            before = load(baseline)
            if (current.parent / "subset").is_file():
                ran = {suite for suite, _ in now}
                before = {key: status for key, status in before.items()
                          if key[0] in ran}
            lost = sorted(key for key, status in before.items()
                          if status in ("pass", "skip") and key not in excluded
                          and now.get(key) not in ("pass", "skip"))
            gained = sorted(key for key, status in now.items()
                            if status in ("pass", "skip") and key not in excluded
                            and before.get(key) not in ("pass", "skip"))
            line += f"; vs {arguments.baseline}: +{len(gained)} -{len(lost)}"
            for suite, test in lost:
                line += f"\n  lost {suite} {test} ({now.get((suite, test), 'missing')})"
            losses += len(lost)
        print(line)
    return 1 if losses else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("run", "compare"))
    parser.add_argument("label")
    parser.add_argument("--fsim", type=Path, help="fsim binary (run)")
    parser.add_argument("--baseline", help="baseline label (compare)")
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--version", action="append", choices=VERSIONS)
    parser.add_argument("--configuration", action="append",
                        choices=tuple(CONFIGURATIONS))
    parser.add_argument("--jobs", "-j", type=int, default=6)
    parser.add_argument("--timeout", type=int, default=600,
                        help="seconds per Makefile suite")
    parser.add_argument("--no-pytest", action="store_true")
    parser.add_argument("--suite", action="append",
                        help="run only the suites matching this glob, e.g. "
                             "'tests/test_cases/test_assign*' (repeatable; "
                             "skips the pytest tests)")
    arguments = parser.parse_args()
    if arguments.command == "compare":
        if not arguments.baseline:
            parser.error("compare needs --baseline")
        return compare(arguments)
    if arguments.fsim is None:
        parser.error("run needs --fsim")
    for version in arguments.version or VERSIONS:
        for configuration in arguments.configuration or tuple(CONFIGURATIONS):
            run(arguments, version, configuration)
    return 0


if __name__ == "__main__":
    sys.exit(main())
