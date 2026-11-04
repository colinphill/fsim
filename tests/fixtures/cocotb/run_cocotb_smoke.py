#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run one fsim cocotb smoke case through Makefile.fsim or get_runner("fsim").

The case's files are copied into the work directory, its workspace (a rerun
recompiles and re-elaborates into the same .fsim); the test passes when every
cocotb test case in the new results file passed.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

FIXTURES = Path(__file__).resolve().parent

# case: (toplevel language, toplevel, Verilog sources, VHDL sources,
#        parameters, test module)
CASES = {
    "dff": ("verilog", "dff", ["dff.sv"], [], {}, "test_dff"),
    "counter": ("vhdl", "counter", [], ["counter.vhd"],
                {"WIDTH": 6, "ENABLE_HIGH": False},
                "test_counter"),
    "mixed": ("verilog", "mixed_top", ["mixed_top.sv"], ["mixed_adder.vhd"],
              {}, "test_mixed"),
}


def environment(fsim: Path, python: Path) -> dict[str, str]:
    result = os.environ.copy()
    # fsim and cocotb-config (the Python environment's scripts) first.
    result["PATH"] = os.pathsep.join(
        [str(fsim.parent), str(python.parent), result.get("PATH", "")])
    for name in ("COCOTB_TRUST_INERTIAL_WRITES", "SIM", "TOPLEVEL_LANG",
                 "GPI_EXTRA", "GPI_IMPL", "FSIM_NO_CONFIG"):
        result.pop(name, None)
    result["FSIM_NO_CONFIG"] = "1"
    return result


def run_makefile(work: Path, env: dict[str, str]) -> Path:
    subprocess.run(["make"], cwd=work, env={**env, "PWD": str(work)},
                   check=False)
    return work / "results.xml"


RUNNER = """\
import json
import sys
from pathlib import Path

import fsim_cocotb  # noqa: F401
from cocotb_tools.runner import get_runner

work = Path(sys.argv[1])
language, toplevel, verilog, vhdl, parameters, module = json.loads(sys.argv[2])
runner = get_runner("fsim")
runner.build(hdl_toplevel=toplevel, build_dir=work, timescale=("1ns", "1ps"),
             verilog_sources=[work / name for name in verilog],
             vhdl_sources=[work / name for name in vhdl])
results = runner.test(hdl_toplevel=toplevel, hdl_toplevel_lang=language,
                      test_module=module, test_dir=work, build_dir=work,
                      parameters=parameters)
print("results:", results)
"""


def run_runner(work: Path, env: dict[str, str], python: Path, case: str,
               pythonpath: str) -> Path:
    env = {**env, "PYTHONPATH": os.pathsep.join([pythonpath, str(work)])}
    subprocess.run([str(python), "-c", RUNNER, str(work), json.dumps(CASES[case])],
                   cwd=work, env=env, check=False)
    return work / "results.xml"


def passed(results: Path) -> bool:
    if not results.is_file():
        print(f"no results file {results}", file=sys.stderr)
        return False
    cases = list(ElementTree.parse(results).iter("testcase"))
    failed = [case.get("name") for case in cases
              if case.find("failure") is not None
              or case.find("error") is not None]
    print(f"{len(cases)} cocotb test cases, {len(failed)} failed: {failed}")
    return bool(cases) and not failed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fsim", type=Path, required=True)
    parser.add_argument("--python", type=Path, required=True)
    parser.add_argument("--case", choices=tuple(CASES), required=True)
    parser.add_argument("--flow", choices=("makefile", "runner"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    arguments = parser.parse_args()
    fsim = arguments.fsim.resolve()
    work = arguments.work.resolve()
    # Not resolved: a virtual environment's python is a symbolic link.
    python = arguments.python.absolute()
    shutil.copytree(FIXTURES / arguments.case, work, dirs_exist_ok=True)
    (work / "results.xml").unlink(missing_ok=True)
    env = environment(fsim, python)
    if arguments.flow == "makefile":
        results = run_makefile(work, env)
    else:
        pythonpath = subprocess.run(
            [str(fsim), "cocotb", "pythonpath"], env=env, check=True,
            capture_output=True, text=True).stdout.strip()
        results = run_runner(work, env, python, arguments.case,
                             pythonpath)
    return 0 if passed(results) else 1


if __name__ == "__main__":
    sys.exit(main())
