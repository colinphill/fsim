#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Qualify Batch 188A compile and elaboration performance.

The caller supplies independently built baseline and candidate fsim binaries.
This script deliberately does not create checkouts or build either binary.
It runs seven cold artifact-phase samples for one pure-SystemVerilog example
and one mixed VHDL/SystemVerilog example, alternating variant order to reduce
thermal and filesystem-cache bias.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any


BASELINE_COMMIT = "a867847c"
JOBS = 8
SAMPLES = 7
REGRESSION_LIMIT = 1.05
MIXED_INSTANCE_COUNT = 128
TIME_PROGRAM = Path("/usr/bin/time")
VCD_OBJECT_SOURCE = re.compile(
    rb"(source_path=objects/)[0-9a-f]{64}(/sources/)"
)
VCD_SOURCE_ID = re.compile(rb" source=[0-9]+ ")


class QualificationError(RuntimeError):
    """A benchmark command or acceptance check failed."""


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--baseline-fsim",
        type=Path,
        required=True,
        help=f"fsim executable built from {BASELINE_COMMIT}",
    )
    parser.add_argument(
        "--current-fsim",
        type=Path,
        required=True,
        help="fsim executable built from the Batch 188A worktree",
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="fsim source root containing the representative examples",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="new or empty evidence directory (default: /tmp/fsim-batch188a-perf.*)",
    )
    return parser.parse_args()


def run_capture(command: list[str], cwd: Path) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def require_success(
    completed: subprocess.CompletedProcess[bytes], description: str
) -> None:
    if completed.returncode == 0:
        return
    stderr = completed.stderr.decode("utf-8", errors="replace")
    raise QualificationError(
        f"{description} failed with status {completed.returncode}: {stderr.strip()}"
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def comparable_vcd(path: Path) -> bytes:
    """Remove schema-dependent identities from VCD scope comments."""
    content = VCD_OBJECT_SOURCE.sub(
        lambda match: match.group(1) + b"<object-digest>" + match.group(2),
        path.read_bytes(),
    )
    lines = []
    for line in content.splitlines(keepends=True):
        if line.startswith(b"$comment fsim-") and b"-scope " in line:
            line = VCD_SOURCE_ID.sub(b" source=<source-id> ", line)
        lines.append(line)
    return b"".join(lines)


def prepare_paths(arguments: argparse.Namespace) -> tuple[Path, Path, Path, Path]:
    baseline = arguments.baseline_fsim.expanduser().resolve()
    current = arguments.current_fsim.expanduser().resolve()
    source_root = arguments.source_root.expanduser().resolve()

    for label, executable in (("baseline", baseline), ("current", current)):
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise QualificationError(
                f"{label} fsim is not an executable file: {executable}"
            )
    if baseline == current:
        raise QualificationError("baseline and current fsim paths must differ")
    if not TIME_PROGRAM.is_file():
        raise QualificationError(f"GNU time is required at {TIME_PROGRAM}")

    required_sources = (
        "examples/v3_coverage/counter.sv",
        "examples/v3_coverage/coverage_tb.sv",
        "examples/vertical_slice/counter.vhd",
        "examples/vertical_slice/tb.sv",
        "examples/vertical_slice/sv_child.sv",
    )
    missing = [source for source in required_sources if not (source_root / source).is_file()]
    if missing:
        raise QualificationError(
            "source root is missing representative inputs: " + ", ".join(missing)
        )

    verify_ref = run_capture(
        ["git", "rev-parse", "--verify", f"{BASELINE_COMMIT}^{{commit}}"],
        source_root,
    )
    require_success(verify_ref, f"resolve baseline commit {BASELINE_COMMIT}")
    examples_unchanged = run_capture(
        [
            "git",
            "diff",
            "--quiet",
            BASELINE_COMMIT,
            "--",
            "examples/v3_coverage",
            "examples/vertical_slice",
        ],
        source_root,
    )
    if examples_unchanged.returncode != 0:
        raise QualificationError(
            "representative example sources differ from the baseline commit"
        )

    if arguments.output is None:
        evidence = Path(tempfile.mkdtemp(prefix="fsim-batch188a-perf."))
    else:
        evidence = arguments.output.expanduser().resolve()
        evidence.mkdir(parents=True, exist_ok=True)
        if any(evidence.iterdir()):
            raise QualificationError(f"evidence directory is not empty: {evidence}")
    return baseline, current, source_root, evidence


def stable_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment.update(
        {
            "LANG": "C",
            "LC_ALL": "C",
            "TZ": "UTC",
        }
    )
    return environment


def write_command(path: Path, command: list[str]) -> None:
    path.write_text(shlex.join(command) + "\n", encoding="utf-8")


def write_mixed_benchmark(path: Path) -> None:
    lines = [
        "// SPDX-License-Identifier: Apache-2.0",
        "module batch188a_mixed_benchmark;",
        "  logic clk;",
        "  logic reset;",
    ]
    for index in range(MIXED_INSTANCE_COUNT):
        lines.append(f"  logic [7:0] counter_q_{index};")
        lines.append(f"  logic [7:0] child_y_{index};")
    for index in range(MIXED_INSTANCE_COUNT):
        lines.extend(
            (
                f"  counter u_counter_{index} (",
                f"    .clk(clk), .reset(reset), .q(counter_q_{index})",
                "  );",
                f"  sv_child u_child_{index} (",
                f"    .value(counter_q_{index}), .inverted(child_y_{index})",
                "  );",
            )
        )
    lines.extend(
        (
            "  initial begin",
            "    clk = 1'b0;",
            "    reset = 1'b1;",
            "    #1 clk = 1'b1;",
            "    #1 clk = 1'b0;",
            "    #1 reset = 1'b0;",
            "    #1 clk = 1'b1;",
            "    #1 clk = 1'b0;",
            "    #1 $finish;",
            "  end",
            "endmodule",
            "",
        )
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def run_measured(
    command: list[str],
    cwd: Path,
    evidence_stem: Path,
    environment: dict[str, str],
) -> dict[str, Any]:
    metric_path = evidence_stem.with_suffix(".time")
    stdout_path = evidence_stem.with_suffix(".stdout")
    stderr_path = evidence_stem.with_suffix(".stderr")
    command_path = evidence_stem.with_suffix(".command")
    write_command(command_path, command)

    timed_command = [
        str(TIME_PROGRAM),
        "--format=%e\t%M",
        f"--output={metric_path}",
        "--",
        *command,
    ]
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        started = time.perf_counter()
        completed = subprocess.run(
            timed_command,
            cwd=cwd,
            env=environment,
            stdout=stdout,
            stderr=stderr,
            check=False,
        )
        elapsed_seconds = time.perf_counter() - started
    if completed.returncode != 0:
        raise QualificationError(
            f"command failed with status {completed.returncode}; "
            f"see {command_path}, {stdout_path}, and {stderr_path}"
        )

    fields = metric_path.read_text(encoding="utf-8").strip().split("\t")
    if len(fields) != 2:
        raise QualificationError(f"malformed GNU time output in {metric_path}")
    return {
        "command": command,
        "elapsed_seconds": elapsed_seconds,
        "peak_rss_kib": int(fields[1]),
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
    }


def case_commands(
    case: str, fsim: Path, source_root: Path, run_directory: Path
) -> tuple[list[list[str]], list[str], list[str], Path]:
    design = run_directory / "design.fsimdesign"
    trace = run_directory / "simulation.vcd"
    common_compile = [str(fsim), "compile", "--jobs", str(JOBS)]
    common_elaborate = [
        str(fsim),
        "elaborate",
        "--jobs",
        str(JOBS),
        "--optimization",
        "O2",
        "--no-aot",
        "--seed",
        "1",
    ]

    if case == "pure_sv":
        sv_object = run_directory / "coverage.fsimobj"
        compile_commands = [
            [
                *common_compile,
                "--lang",
                "systemverilog",
                "--standard",
                "2023",
                "--library",
                "work",
                "--compilation-unit",
                "source-set",
                "--code-coverage",
                "--output",
                str(sv_object),
                str(source_root / "examples/v3_coverage/counter.sv"),
                str(source_root / "examples/v3_coverage/coverage_tb.sv"),
            ]
        ]
        elaborate = [
            *common_elaborate,
            "--object",
            str(sv_object),
            "--top",
            "coverage=sv:work.coverage_tb",
            "--code-coverage",
            "--output",
            str(design),
        ]
        simulate = [
            str(fsim),
            "simulate",
            "--design",
            str(design),
            "--engine",
            "interpreter",
            "--seed",
            "1",
            "--max-deltas",
            "1000",
            "--code-coverage",
            "--trace",
            str(trace),
            "--trace-format",
            "vcd",
            "--trace-filter",
            "coverage.*",
        ]
        return compile_commands, elaborate, simulate, trace

    if case == "mixed":
        vhdl_object = run_directory / "counter.fsimobj"
        sv_object = run_directory / "testbench.fsimobj"
        benchmark_source = run_directory / "mixed_benchmark.sv"
        write_mixed_benchmark(benchmark_source)
        compile_commands = [
            [
                *common_compile,
                "--lang",
                "vhdl",
                "--standard",
                "2008",
                "--library",
                "work",
                "--output",
                str(vhdl_object),
                str(source_root / "examples/vertical_slice/counter.vhd"),
            ],
            [
                *common_compile,
                "--lang",
                "systemverilog",
                "--standard",
                "2017",
                "--library",
                "work",
                "--compilation-unit",
                "source-set",
                "--output",
                str(sv_object),
                str(benchmark_source),
                str(source_root / "examples/vertical_slice/sv_child.sv"),
            ],
        ]
        elaborate = [
            *common_elaborate,
            "--object",
            str(vhdl_object),
            "--object",
            str(sv_object),
            "--top",
            "mixed=sv:work.batch188a_mixed_benchmark",
            "--delay-mode",
            "typ",
            "--output",
            str(design),
        ]
        simulate = [
            str(fsim),
            "simulate",
            "--design",
            str(design),
            "--engine",
            "interpreter",
            "--seed",
            "1",
            "--duration",
            "10ns",
            "--max-deltas",
            "100000",
            "--delay-mode",
            "typ",
            "--trace",
            str(trace),
            "--trace-format",
            "vcd",
            "--trace-filter",
            "mixed.*",
        ]
        return compile_commands, elaborate, simulate, trace

    raise QualificationError(f"unknown qualification case: {case}")


def run_simulation(
    command: list[str],
    cwd: Path,
    trace: Path,
    environment: dict[str, str],
) -> dict[str, Any]:
    stdout_path = cwd / "simulate.stdout"
    stderr_path = cwd / "simulate.stderr"
    write_command(cwd / "simulate.command", command)
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        completed = subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            stdout=stdout,
            stderr=stderr,
            check=False,
        )
    if completed.returncode != 0:
        raise QualificationError(
            f"simulation failed with status {completed.returncode}; "
            f"see {stdout_path} and {stderr_path}"
        )
    if not trace.is_file():
        raise QualificationError(f"simulation did not publish VCD: {trace}")
    return {
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "vcd": str(trace),
        "stdout_sha256": sha256(stdout_path),
        "stderr_sha256": sha256(stderr_path),
        "vcd_sha256": sha256(trace),
    }


def run_sample(
    case: str,
    variant: str,
    fsim: Path,
    source_root: Path,
    run_directory: Path,
    environment: dict[str, str],
) -> dict[str, Any]:
    run_directory.mkdir(parents=True)
    compile_commands, elaborate, simulate, trace = case_commands(
        case, fsim, source_root, run_directory
    )
    compile_metrics = []
    for index, command in enumerate(compile_commands, start=1):
        compile_metrics.append(
            run_measured(
                command,
                run_directory,
                run_directory / f"compile-{index}",
                environment,
            )
        )
    elaboration_metric = run_measured(
        elaborate, run_directory, run_directory / "elaborate", environment
    )
    simulation = run_simulation(simulate, run_directory, trace, environment)

    compile_wall = sum(metric["elapsed_seconds"] for metric in compile_metrics)
    compile_rss = max(metric["peak_rss_kib"] for metric in compile_metrics)
    elaboration_wall = elaboration_metric["elapsed_seconds"]
    elaboration_rss = elaboration_metric["peak_rss_kib"]
    return {
        "case": case,
        "variant": variant,
        "compile_commands": compile_metrics,
        "compile_wall_seconds": compile_wall,
        "compile_peak_rss_kib": compile_rss,
        "elaborate_command": elaboration_metric,
        "elaborate_wall_seconds": elaboration_wall,
        "elaborate_peak_rss_kib": elaboration_rss,
        "total_wall_seconds": compile_wall + elaboration_wall,
        "total_peak_rss_kib": max(compile_rss, elaboration_rss),
        "simulation": simulation,
    }


def require_equivalence(case: str, sample: int, pair: dict[str, dict[str, Any]]) -> None:
    for artifact in ("stdout", "stderr", "vcd"):
        baseline = Path(pair["baseline"]["simulation"][artifact])
        current = Path(pair["current"]["simulation"][artifact])
        baseline_bytes = (
            comparable_vcd(baseline) if artifact == "vcd" else baseline.read_bytes()
        )
        current_bytes = (
            comparable_vcd(current) if artifact == "vcd" else current.read_bytes()
        )
        if baseline_bytes != current_bytes:
            raise QualificationError(
                f"{case} sample {sample} has non-equivalent simulation {artifact}: "
                f"{baseline} != {current}"
            )


def median_summary(samples: list[dict[str, Any]]) -> dict[str, float]:
    metrics = (
        "compile_wall_seconds",
        "compile_peak_rss_kib",
        "elaborate_wall_seconds",
        "elaborate_peak_rss_kib",
        "total_wall_seconds",
        "total_peak_rss_kib",
    )
    return {
        metric: float(statistics.median(sample[metric] for sample in samples))
        for metric in metrics
    }


def summarize(results: list[dict[str, Any]]) -> tuple[dict[str, Any], bool]:
    summary: dict[str, Any] = {}
    passed = True
    for case in ("pure_sv", "mixed"):
        case_summary: dict[str, Any] = {}
        variants = {
            variant: [
                sample
                for sample in results
                if sample["case"] == case and sample["variant"] == variant
            ]
            for variant in ("baseline", "current")
        }
        for variant, samples in variants.items():
            if len(samples) != SAMPLES:
                raise QualificationError(
                    f"{case} {variant} has {len(samples)} samples, expected {SAMPLES}"
                )
            case_summary[variant] = median_summary(samples)

        baseline = case_summary["baseline"]
        current = case_summary["current"]
        wall_ratio = current["total_wall_seconds"] / baseline["total_wall_seconds"]
        rss_ratio = current["total_peak_rss_kib"] / baseline["total_peak_rss_kib"]
        case_passed = wall_ratio <= REGRESSION_LIMIT and rss_ratio <= REGRESSION_LIMIT
        case_summary["ratios"] = {
            "total_wall": wall_ratio,
            "total_peak_rss": rss_ratio,
        }
        case_summary["gate_passed"] = case_passed
        summary[case] = case_summary
        passed = passed and case_passed
    return summary, passed


def write_summary(path: Path, summary: dict[str, Any]) -> None:
    lines = [
        "case\tmetric\tbaseline_median\tcurrent_median\tratio\tlimit\tstatus"
    ]
    for case in ("pure_sv", "mixed"):
        baseline = summary[case]["baseline"]
        current = summary[case]["current"]
        for metric in (
            "compile_wall_seconds",
            "compile_peak_rss_kib",
            "elaborate_wall_seconds",
            "elaborate_peak_rss_kib",
            "total_wall_seconds",
            "total_peak_rss_kib",
        ):
            ratio = current[metric] / baseline[metric]
            gated = metric in ("total_wall_seconds", "total_peak_rss_kib")
            status = "PASS" if not gated or ratio <= REGRESSION_LIMIT else "FAIL"
            limit = f"{REGRESSION_LIMIT:.2f}" if gated else "report-only"
            lines.append(
                f"{case}\t{metric}\t{baseline[metric]:.6f}\t"
                f"{current[metric]:.6f}\t{ratio:.6f}\t{limit}\t{status}"
            )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def binary_identity(executable: Path, source_root: Path) -> dict[str, str]:
    version = run_capture([str(executable), "--version"], source_root)
    require_success(version, f"query version for {executable}")
    return {
        "path": str(executable),
        "sha256": sha256(executable),
        "version_stdout": version.stdout.decode("utf-8", errors="replace"),
        "version_stderr": version.stderr.decode("utf-8", errors="replace"),
    }


def main() -> int:
    arguments = parse_arguments()
    try:
        baseline, current, source_root, evidence = prepare_paths(arguments)
        environment = stable_environment()
        identities = {
            "baseline": binary_identity(baseline, source_root),
            "current": binary_identity(current, source_root),
        }
        executables = {"baseline": baseline, "current": current}
        results: list[dict[str, Any]] = []

        print(f"evidence: {evidence}", flush=True)
        for case in ("pure_sv", "mixed"):
            for sample in range(1, SAMPLES + 1):
                order = (
                    ("baseline", "current")
                    if sample % 2 == 1
                    else ("current", "baseline")
                )
                pair: dict[str, dict[str, Any]] = {}
                for variant in order:
                    print(
                        f"{case} sample {sample}/{SAMPLES}: {variant}", flush=True
                    )
                    run_directory = (
                        evidence / case / f"sample-{sample:02d}" / variant
                    )
                    result = run_sample(
                        case,
                        variant,
                        executables[variant],
                        source_root,
                        run_directory,
                        environment,
                    )
                    results.append(result)
                    pair[variant] = result
                require_equivalence(case, sample, pair)

        summary, passed = summarize(results)
        report = {
            "baseline_commit": BASELINE_COMMIT,
            "jobs": JOBS,
            "samples_per_variant_per_case": SAMPLES,
            "regression_limit": REGRESSION_LIMIT,
            "source_root": str(source_root),
            "binary_identities": identities,
            "results": results,
            "summary": summary,
            "passed": passed,
        }
        report_path = evidence / "qualification.json"
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        write_summary(evidence / "summary.tsv", summary)
        print((evidence / "summary.tsv").read_text(encoding="utf-8"), end="")
        print(f"qualification evidence: {report_path}")
        if not passed:
            raise QualificationError(
                "compile-plus-elaborate wall time or peak RSS exceeded the 5% gate"
            )
        return 0
    except QualificationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
