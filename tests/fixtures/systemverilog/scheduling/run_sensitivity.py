#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare marker-delimited event counts with per-case expectations."""

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile


GROUPS = {
    "unrelated_element": {"dynamic": 1},
    "unrelated_bits": {"dynamic": 1, "explicit_element": 1},
    "selected_bits": {
        "dynamic": 1, "explicit_element": 1, "comb": 1,
        "wildcard": 1, "explicit_slice": 1,
    },
    "dynamic_index": {"dynamic": 1},
}
UNKNOWN_INDEX_GROUPS = {
    "known_to_x": {"dynamic": 1, "explicit_dynamic": 1},
    "x_invalid_writes": {"dynamic": 2},
    "x_to_z": {"dynamic": 1},
    "z_invalid_writes": {"dynamic": 2},
    "z_to_known": {"dynamic": 1, "explicit_dynamic": 1},
}
CASES = (
    "fsim_sensitivity_ranges", "fsim_sensitivity_ranges_ascending",
    "fsim_sensitivity_ranges_wide", "fsim_sensitivity_ranges_wide_dynamic",
    "fsim_sensitivity_ranges_unknown_index",
)
CASE_GROUPS = {
    **{case: GROUPS for case in CASES[:-1]},
    "fsim_sensitivity_ranges_unknown_index": UNKNOWN_INDEX_GROUPS,
}
EVENTS = {"dynamic", "explicit_element", "comb", "wildcard",
          "explicit_slice", "explicit_dynamic"}
FOOTER = re.compile(r"simulation (?:completed|stopped) at tick \d+, delta \d+")


def parse_events(transcript, expected_groups):
    groups = {}
    active = None
    finished = False
    for line in transcript.splitlines():
        if FOOTER.fullmatch(line):
            if active is not None or tuple(groups) != tuple(expected_groups) or finished:
                raise ValueError("unexpected simulation footer")
            finished = True
        elif finished:
            raise ValueError("output after simulation footer")
        elif line.startswith("BEGIN "):
            name = line.removeprefix("BEGIN ")
            if active is not None or name not in expected_groups or name in groups:
                raise ValueError(f"unexpected marker: {line}")
            if name != tuple(expected_groups)[len(groups)]:
                raise ValueError(f"out-of-order marker: {line}")
            active = name
            groups[name] = Counter()
        elif line.startswith("END "):
            if active != line.removeprefix("END "):
                raise ValueError(f"unmatched marker: {line}")
            active = None
        elif line.startswith("EVENT "):
            event = line.removeprefix("EVENT ")
            if event not in EVENTS:
                raise ValueError(f"unknown event: {line}")
            if active is not None:
                groups[active][event] += 1
            elif groups:
                raise ValueError(f"event outside stimulus window: {line}")
            # Initial activation precedes the first stimulus window.
        else:
            raise ValueError(f"unexpected transcript line: {line!r}")
    if active is not None or tuple(groups) != tuple(expected_groups) or not finished:
        raise ValueError("incomplete simulation transcript")
    return groups


def run_case(executable, case, directory, engine, optimization):
    fixture = Path(__file__).resolve().parent / f"{case}.sv"
    shutil.copyfile(fixture, directory / fixture.name)
    commands = (
        ("compile", ["compile", "-q", fixture.name]),
        ("elaborate", ["elaborate", "-q", "--top", case, "--no-aot",
                       "--optimization", optimization]),
        ("simulate", ["simulate", "--engine", engine]),
    )
    for step, arguments in commands:
        with (directory / f"{step}.stdout").open("w") as stdout, \
                (directory / f"{step}.stderr").open("w") as stderr:
            subprocess.run([str(executable), *arguments], cwd=directory,
                           stdout=stdout, stderr=stderr, check=True, timeout=120)
    case_groups = CASE_GROUPS[case]
    actual = parse_events((directory / "simulate.stdout").read_text(), case_groups)
    expected = {name: Counter(events) for name, events in case_groups.items()}
    if case.endswith("_wide_dynamic"):
        for name in ("unrelated_bits", "selected_bits", "dynamic_index"):
            expected[name]["explicit_dynamic"] = 1
    (directory / "comparison.json").write_text(json.dumps(
        {"actual": actual, "expected": expected}, indent=2) + "\n")
    if actual != expected:
        reference = "expected sensitivity semantics"
        if case != "fsim_sensitivity_ranges_unknown_index":
            reference = "the verified Vivado oracle"
        raise ValueError(f"{case}: event counts differ from {reference}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fsim", type=Path, required=True)
    parser.add_argument("--engine", choices=("interpreter", "compiled"),
                        required=True)
    parser.add_argument("--optimization", choices=("O0", "O2"), default="O2")
    parser.add_argument("--workspace-parent", type=Path, required=True)
    args = parser.parse_args()
    executable = args.fsim.resolve(strict=True)
    workspace = Path(tempfile.mkdtemp(prefix="fsim-sensitivity-",
                                     dir=args.workspace_parent))
    try:
        for case in CASES:
            directory = workspace / case
            directory.mkdir()
            run_case(executable, case, directory, args.engine, args.optimization)
            print(f"Sensitivity witness passed: {case}")
    except Exception:
        print(f"Failure evidence: {workspace}", flush=True)
        raise
    if os.environ.get("KEEP_WORKSPACE") == "1":
        print(f"Sensitivity witness workspace: {workspace}")
    else:
        # Compiled library snapshots contain read-only directories. Restore
        # write access only inside this runner's temporary workspace so their
        # entries can be removed without modifying the packaged source cache.
        for directory, _, _ in os.walk(workspace):
            path = Path(directory)
            path.chmod(path.stat().st_mode | stat.S_IWUSR | stat.S_IXUSR)
        shutil.rmtree(workspace)


if __name__ == "__main__":
    main()
