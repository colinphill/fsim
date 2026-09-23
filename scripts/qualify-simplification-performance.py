#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Preflight and qualify the Batch 188B simplification benchmark corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any


DEFAULT_MANIFEST = Path(__file__).with_name("simplification_benchmarks.json")
TIME_PROGRAM = Path("/usr/bin/time")
COMMAND_TIMEOUT_SECONDS = 7200
VERSION_TIMEOUT_SECONDS = 30
MINIMUM_TRACE_VALUE_CHANGES = 800_000
FST2VCD_PROGRAM = Path("/bin/fst2vcd")
MANDATORY_CASES = {
    "original_codec", "original_throughput", "mixed_codec", "mixed_throughput",
    "codex_reference_mode0_frames1", "codex_reference_mode0_frames2",
    "codex_reference_mode1_frames1", "codex_reference_mode1_frames2",
    "codex_throughput_default", "codex_throughput_direct_syndrome",
}
JIT_CONFIGURATIONS = {"llvm_o0", "llvm_o2"}
REPOSITORY_CONFIGURATIONS = {"interpreter", *JIT_CONFIGURATIONS}
CONFIGURATIONS_BY_ROOT = {
    "original": JIT_CONFIGURATIONS,
    "mixed": JIT_CONFIGURATIONS,
    "codex": JIT_CONFIGURATIONS,
    "repository": REPOSITORY_CONFIGURATIONS,
}
INCLUDE_PATTERN = re.compile(
    r'(?m)^\s*' + chr(96) + r'include\s+"([^"]+)"'
)
LDD_PROGRAM = Path("/usr/bin/ldd")
READELF_PROGRAM = Path("/usr/bin/readelf")
PROFILE_ENVIRONMENT = {
    "FSIM_PROFILE_PHASES": "1",
    "FSIM_PROFILE_JIT": "1",
    "FSIM_PROFILE_JIT_PROCESSES": "1",
    "FSIM_PROFILE_JIT_PROCESSES_ALL": "1",
    "FSIM_PROFILE_JIT_MODULES": "1",
}
NUMBER_PATTERN = r"[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?"
FUNCTIONAL_COVERAGE_DIGEST_PATTERN = re.compile(
    r"(?m)^PASS: simplification functional coverage semantic_sha256="
    r"([^\r\n]*)$"
)
PHASE_PROFILE_PATTERN = re.compile(
    rf"^FSIM-PROFILE setup_ms=({NUMBER_PATTERN}) "
    rf"run_ms=({NUMBER_PATTERN}) native_await_ms=({NUMBER_PATTERN})$"
)
JIT_SUMMARY_PATTERN = re.compile(
    rf"^fsim-profile: jit-process-summary processes=([0-9]+) "
    rf"resumes=([0-9]+) elapsed_ms=({NUMBER_PATTERN})$"
)
JIT_PROCESS_PATTERN = re.compile(
    rf"^fsim-profile: jit-process id=([0-9]+) generated_id=([0-9]+) "
    rf"resumes=([0-9]+) direct_read_slots=([0-9]+) "
    rf"direct_update_slots=([0-9]+) operations=([0-9]+) "
    rf"static_waits=([0-9]+) elapsed_ms=({NUMBER_PATTERN}) name='(.*)'$"
)
JIT_MODULE_PATTERN = re.compile(
    rf"^fsim jit module profile: identity=(.*?) processes=([0-9]+) "
    rf"process_ids=([0-9,]*) operations=([0-9]+) weight=([0-9]+) "
    rf"worker=([0-9]+) startup=([01]) "
    rf"materialization_ms=({NUMBER_PATTERN})$"
)
JIT_UNSUPPORTED_PATTERN = re.compile(
    r"^fsim jit unsupported module: identity=(.*?) reason=(.+)$"
)
JIT_SETUP_PATTERN = re.compile(
    rf"^fsim-profile: jit setup_ms=({NUMBER_PATTERN}) "
    rf"registration_ms=({NUMBER_PATTERN}) "
    rf"materialization_launch_ms=({NUMBER_PATTERN}) modules=([0-9]+) "
    rf"unpacked_modules=([0-9]+) processes=([0-9]+) operations=([0-9]+) "
    rf"lowered_processes=([0-9]+) lowered_operations=([0-9]+) "
    rf"largest_module_operations=([0-9]+) largest_module_identity='(.*)' "
    rf"retained_processes=([0-9]+) retained_operations=([0-9]+) "
    rf"selective_large_design=([01]) compile_all=([01])$"
)


class QualificationError(RuntimeError):
    """The manifest, workload, or acceptance gate is invalid."""


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--baseline-fsim", type=Path)
    parser.add_argument("--candidate-fsim", type=Path)
    parser.add_argument("--baseline-helper", type=Path)
    parser.add_argument("--candidate-helper", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--configuration", action="append", dest="configurations")
    parser.add_argument("--root", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--preflight-only", action="store_true")
    parser.add_argument("--list-cases", action="store_true")
    return parser.parse_args(argv)


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise QualificationError(f"cannot read benchmark manifest {path}: {error}")
    defaults = manifest.get("defaults", {})
    if manifest.get("schema_version") != 1:
        raise QualificationError("manifest schema_version must be 1")
    expected = {"jobs": 8, "seed": 1, "samples": 7, "regression_limit": 1.1}
    if any(defaults.get(key) != value for key, value in expected.items()):
        raise QualificationError("manifest must fix jobs=8, seed=1, samples=7, limit=1.10")
    configuration_ids = [
        configuration.get("id") for configuration in manifest.get("configurations", [])
    ]
    if len(configuration_ids) != len(set(configuration_ids)):
        raise QualificationError("configuration ids must be unique")
    if set(configuration_ids) != set(REPOSITORY_CONFIGURATIONS):
        raise QualificationError(
            "manifest must define interpreter, llvm_o0, and llvm_o2 configurations"
        )
    configurations = {
        configuration["id"]: configuration
        for configuration in manifest["configurations"]
    }
    expected_engines = {
        "interpreter": ("interpreter", "O2"),
        "llvm_o0": ("compiled", "O0"),
        "llvm_o2": ("compiled", "O2"),
    }
    for name, (engine, optimization) in expected_engines.items():
        configuration = configurations[name]
        if (
            configuration.get("engine") != engine
            or configuration.get("optimization") != optimization
        ):
            raise QualificationError(
                f"configuration {name} must use {engine} {optimization}"
            )
    ids = [case.get("id") for case in manifest.get("cases", [])]
    if len(ids) != len(set(ids)):
        raise QualificationError("case ids must be unique")
    active = {
        case["id"] for case in manifest["cases"] if case.get("status") == "active"
    }
    if MANDATORY_CASES - active:
        raise QualificationError(
            "mandatory cases are not active: " + ", ".join(sorted(MANDATORY_CASES - active))
        )
    for case in manifest["cases"]:
        root = case.get("root")
        if root not in CONFIGURATIONS_BY_ROOT:
            raise QualificationError(
                f"case {case.get('id')} has unknown benchmark root {root!r}"
            )
        required = CONFIGURATIONS_BY_ROOT[root]
        declared = case.get("configurations")
        if (
            not isinstance(declared, list)
            or not all(isinstance(name, str) for name in declared)
            or len(declared) != len(set(declared))
        ):
            raise QualificationError(
                f"case {case['id']} must declare unique required configurations"
            )
        if set(declared) != required:
            raise QualificationError(
                f"case {case['id']} must require configurations: "
                + ", ".join(sorted(required))
            )
        if case.get("status") == "pending" and not case.get("reason"):
            raise QualificationError(f"pending case {case['id']} requires a reason")
    return manifest


def parse_root_overrides(values: list[str]) -> dict[str, Path]:
    result = {}
    for value in values:
        name, separator, path = value.partition("=")
        if not separator or not name or not path:
            raise QualificationError(f"invalid --root {value!r}; expected NAME=PATH")
        result[name] = Path(path).expanduser().resolve()
    return result


def resolve_roots(
    manifest: dict[str, Any], manifest_path: Path, overrides: dict[str, Path]
) -> dict[str, Path]:
    unknown = sorted(set(overrides) - set(manifest["roots"]))
    if unknown:
        raise QualificationError("unknown roots: " + ", ".join(unknown))
    repository = manifest_path.resolve().parents[1]
    roots = {}
    for name, entry in manifest["roots"].items():
        default = Path(entry["default"]).expanduser()
        if not default.is_absolute():
            default = repository / default
        roots[name] = overrides.get(name, default).resolve()
    return roots


def selected_entries(
    entries: list[dict[str, Any]], selected: list[str] | None, kind: str
) -> list[dict[str, Any]]:
    by_id = {entry["id"]: entry for entry in entries}
    if selected is None:
        return [entry for entry in entries if entry.get("status", "active") == "active"]
    unknown = [name for name in selected if name not in by_id]
    if unknown:
        raise QualificationError(f"unknown {kind}: " + ", ".join(unknown))
    result = []
    for name in selected:
        entry = by_id[name]
        if entry.get("status", "active") != "active":
            raise QualificationError(
                f"{kind} {name} is {entry.get('status')}: {entry.get('reason')}"
            )
        result.append(entry)
    return result


def configurations_for_case(
    case: dict[str, Any], configurations: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    allowed = case.get("configurations")
    if allowed is None:
        return configurations
    return [configuration for configuration in configurations
            if configuration["id"] in allowed]


def stable_environment() -> tuple[dict[str, str], list[str]]:
    removed = sorted(key for key in os.environ if key.startswith("FSIM_PROFILE_"))
    environment = {
        key: value for key, value in os.environ.items()
        if not key.startswith("FSIM_PROFILE_")
    }
    environment.update({"LANG": "C", "LC_ALL": "C", "TZ": "UTC"})
    return environment, removed


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def command_sha256(command: list[str]) -> str:
    return hashlib.sha256(b"\0".join(item.encode() for item in command)).hexdigest()


def run_bounded(
    command: list[str], *, cwd: Path | None = None,
    environment: dict[str, str] | None = None, stdout: Any = subprocess.PIPE,
    stderr: Any = subprocess.PIPE, timeout: float = COMMAND_TIMEOUT_SECONDS
) -> subprocess.CompletedProcess[Any]:
    process = subprocess.Popen(
        command, cwd=cwd, env=environment, stdout=stdout, stderr=stderr,
        start_new_session=os.name == "posix",
    )
    try:
        captured_stdout, captured_stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        if os.name == "posix":
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        else:
            if process.poll() is None:
                process.kill()
        process.communicate()
        raise QualificationError(
            f"command timed out after {timeout:g} seconds: {shlex.join(command)}"
        ) from error
    return subprocess.CompletedProcess(
        command, process.returncode, captured_stdout, captured_stderr
    )


def source_paths(
    case: dict[str, Any], group: dict[str, Any], manifest: dict[str, Any],
    roots: dict[str, Path], generated: Path
) -> list[Path]:
    names = []
    if "source_set" in group:
        try:
            names.extend(manifest["source_sets"][group["source_set"]])
        except KeyError:
            raise QualificationError(
                f"{case['id']} has unknown source set {group['source_set']}"
            )
    names.extend(group.get("sources", []))
    root = roots[case["root"]]
    return [
        generated / name.removeprefix("$generated/")
        if name.startswith("$generated/") else root / name
        for name in names
    ]


def mixed_long_wrapper(instances: int, cycles: int) -> str:
    if instances <= 0 or cycles < 100000:
        raise QualificationError("mixed long wrapper requires instances>0 and cycles>=100000")
    lines = [
        chr(96) + "timescale 1ns/1ps",
        "module simplification_mixed_long_tb;",
        "  logic clk = 1'b0;",
        "  logic reset = 1'b1;",
    ]
    for index in range(instances):
        lines.extend([
            f"  logic [7:0] counter_q_{index};",
            f"  logic [7:0] child_y_{index};",
        ])
    for index in range(instances):
        lines.extend([
            f"  counter u_counter_{index} (",
            f"    .clk(clk), .reset(reset), .q(counter_q_{index})",
            "  );",
            f"  sv_child u_child_{index} (",
            f"    .value(counter_q_{index}), .inverted(child_y_{index})",
            "  );",
        ])
    lines.extend([
        "  always #1 clk = ~clk;",
        "  integer cycle;",
        "  initial begin",
        "    repeat (2) @(posedge clk);",
        "    @(negedge clk);",
        "    reset = 1'b0;",
        f"    for (cycle = 0; cycle < {cycles}; cycle = cycle + 1) begin",
        "      @(negedge clk);",
    ])
    for index in range(instances):
        lines.extend([
            f"      if (counter_q_{index} !== ((cycle + 1) & 8'hff))",
            f"        $fatal(1, \"counter {index} mismatch cycle=%0d value=%0h\",",
            f"               cycle + 1, counter_q_{index});",
            f"      if (child_y_{index} !== ~counter_q_{index})",
            f"        $fatal(1, \"inversion {index} mismatch cycle=%0d\", cycle + 1);",
        ])
    expected_logical_changes = instances * cycles * 2
    lines.extend([
        "    end",
        f"    $display(\"PASS: simplification mixed long cycles={cycles} "
        f"instances={instances} expected_logical_changes={expected_logical_changes}\");",
        "    $finish;",
        "  end",
        "  initial begin",
        "    #1000000;",
        "    $fatal(1, \"simplification mixed long timeout\");",
        "  end",
        "endmodule",
        "",
    ])
    return "\n".join(lines)


def coverage_wrapper() -> str:
    return "\n".join([
        chr(96) + "timescale 1ns/1ps",
        "module simplification_coverage_tb;",
        "  logic clock = 1'b0;",
        "  logic reset = 1'b1;",
        "  logic [1:0] value;",
        "  real coverage_value;",
        "  counter dut(.*);",
        "  covergroup values @(posedge clock);",
        "    value_point: coverpoint value;",
        "  endgroup",
        "  values observed = new;",
        "  always #1 clock = ~clock;",
        "  initial begin",
        '    $set_coverage_db_name("observations/functional.fsimcov");',
        "    repeat (2) @(posedge clock);",
        "    @(negedge clock);",
        "    reset = 1'b0;",
        "    repeat (6) @(posedge clock);",
        "    @(negedge clock);",
        "    coverage_value = $get_coverage();",
        "    if (value !== 2'b10)",
        '      $fatal(1, "coverage counter mismatch value=%0h", value);',
        "    if (coverage_value < 100.0)",
        '      $fatal(1, "functional coverage incomplete value=%f",',
        "             coverage_value);",
        '    $display("PASS: simplification pure SV coverage value=%0h coverage=%f",',
        "             value, coverage_value);",
        "    $finish;",
        "  end",
        "  initial begin",
        "    #1000;",
        '    $fatal(1, "simplification coverage timeout");',
        "  end",
        "endmodule",
        "",
    ])


def case_wrapper(case: dict[str, Any]) -> str | None:
    if "wrapper" in case:
        return case["wrapper"]
    generator = case.get("wrapper_generator")
    if generator is None:
        return None
    if generator.get("kind") == "coverage":
        return coverage_wrapper()
    if generator.get("kind") != "mixed_long":
        raise QualificationError(
            f"{case['id']} has unknown wrapper generator {generator.get('kind')}"
        )
    return mixed_long_wrapper(generator["instances"], generator["cycles"])


def stable_source_name(path: Path, root_name: str, root: Path) -> str:
    try:
        relative = path.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise QualificationError(
            f"source input escapes declared {root_name} root: {path}"
        ) from error
    return f"{root_name}/{relative.as_posix()}"


def source_record(path: Path, root_name: str, root: Path) -> dict[str, str]:
    if not path.is_file():
        raise QualificationError(f"source input is missing: {path}")
    return {
        "path": stable_source_name(path, root_name, root),
        "sha256": sha256(path),
    }


def include_records(
    sources: list[Path], include_directories: list[Path],
    root_name: str, root: Path
) -> list[dict[str, str]]:
    records: list[dict[str, str]] = []
    visited: set[Path] = set()

    def visit(path: Path) -> None:
        try:
            content = path.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            raise QualificationError(f"cannot read source input {path}: {error}")
        directive = re.compile(r"^\s*" + chr(96) + r"include\b")
        literal = re.compile(
            r'^\s*' + chr(96) + r'include\s+"([^"]+)"\s*(?://.*)?$'
        )
        for line_number, line in enumerate(content.splitlines(), 1):
            if directive.match(line) and not literal.match(line):
                raise QualificationError(
                    f"non-literal include cannot be identity-resolved at "
                    f"{path}:{line_number}"
                )
        for match in INCLUDE_PATTERN.finditer(content):
            include_name = match.group(1)
            candidates = [path.parent / include_name]
            candidates.extend(directory / include_name for directory in include_directories)
            included = next(
                (candidate for candidate in candidates if candidate.is_file()), None
            )
            if included is None:
                raise QualificationError(
                    f"cannot resolve include {include_name!r} from {path}"
                )
            included = included.resolve()
            if included in visited:
                continue
            visited.add(included)
            records.append(source_record(included, root_name, root))
            visit(included)

    for source in sources:
        visit(source)
    return records


def capture_source_identity(
    cases: list[dict[str, Any]], manifest: dict[str, Any],
    roots: dict[str, Path]
) -> dict[str, Any]:
    case_records = []
    generated_root = Path("/generated")
    for case in cases:
        root_name = case["root"]
        root = roots[root_name]
        groups = []
        for index, group in enumerate(case["source_groups"], 1):
            literal_sources = [
                path for path in source_paths(
                    case, group, manifest, roots, generated_root
                )
                if not path.is_relative_to(generated_root)
            ]
            include_directories = [
                root / directory for directory in group.get("include_dirs", [])
            ]
            for directory in include_directories:
                if not directory.is_dir():
                    raise QualificationError(
                        f"include directory is missing: {directory}"
                    )
            groups.append({
                "index": index,
                "language": group["language"],
                "standard": group["standard"],
                "library": "work",
                "compilation_unit": "source-set",
                "defines": list(group.get("defines", [])),
                "include_directories": [
                    stable_source_name(path, root_name, root)
                    for path in include_directories
                ],
                "sources": [
                    source_record(path, root_name, root)
                    for path in literal_sources
                ],
                "include_inputs": include_records(
                    literal_sources, include_directories, root_name, root
                ),
            })
        wrapper = None
        wrapper_text = case_wrapper(case)
        if wrapper_text is not None:
            wrapper = {
                "path": f"{case['id']}/generated/wrapper.sv",
                "sha256": hashlib.sha256(wrapper_text.encode()).hexdigest(),
            }
        case_records.append({
            "case": case["id"], "root": root_name, "top": case["top"],
            "groups": groups,
            "generated_wrapper": wrapper,
            "compile_options": list(case.get("compile_options", [])),
            "elaborate_options": list(case.get("elaborate_options", [])),
            "simulate_options": list(case.get("simulate_options", [])),
            "observation": dict(case.get("observation", {"kind": "none"})),
            "helper_source_inputs": [
                source_record(root / name, root_name, root)
                for name in case.get("helper_source_inputs", [])
            ],
        })
    return {"algorithm": "sha256", "cases": case_records}


def require_identity_unchanged(
    before: dict[str, Any], after: dict[str, Any], description: str
) -> None:
    if before != after:
        raise QualificationError(f"{description} identity changed during qualification")


def materialize_commands(
    case: dict[str, Any], configuration: dict[str, Any], executable: Path,
    manifest: dict[str, Any], roots: dict[str, Path], run_directory: Path,
    write_wrapper: bool = False, helper: Path | None = None
) -> dict[str, Any]:
    allowed_configurations = case.get("configurations")
    if (
        allowed_configurations is not None
        and configuration["id"] not in allowed_configurations
    ):
        raise QualificationError(
            f"{case['id']} does not support configuration {configuration['id']}"
        )
    defaults = manifest["defaults"]
    generated = run_directory / "generated"
    artifacts = run_directory / "artifacts"
    cache = run_directory / "native-cache"
    wrapper_text = case_wrapper(case)
    if write_wrapper and wrapper_text is not None:
        generated.mkdir(parents=True, exist_ok=True)
        (generated / "wrapper.sv").write_text(wrapper_text, encoding="utf-8")
    compile_commands = []
    objects = []
    for index, group in enumerate(case["source_groups"], 1):
        output = artifacts / f"group-{index}.fsimobj"
        objects.append(output)
        command = [
            str(executable), "compile", "--jobs", str(defaults["jobs"]),
            "--lang", group["language"], "--standard", group["standard"],
            "--library", "work", "--compilation-unit", "source-set",
        ]
        for include in group.get("include_dirs", []):
            command.extend(["--include", str(roots[case["root"]] / include)])
        for define in group.get("defines", []):
            command.extend(["--define", define])
        command.extend(case.get("compile_options", []))
        command.extend(["--output", str(output)])
        command.extend(
            str(path) for path in source_paths(
                case, group, manifest, roots, generated
            )
        )
        compile_commands.append(command)
    design = artifacts / "design.fsimdesign"
    elaborate = [
        str(executable), "elaborate", "--jobs", str(defaults["jobs"]),
        "--optimization", configuration["optimization"], "--no-aot",
        "--seed", str(defaults["seed"]),
    ]
    for obj in objects:
        elaborate.extend(["--object", str(obj)])
    elaborate.extend([
        "--top", f"{case['id']}={case['top']}", "--output", str(design)
    ])
    elaborate.extend(case.get("elaborate_options", []))
    if case.get("runner") == "helper":
        if helper is None:
            helper = Path("<simplification-benchmark-helper>")
        simulate = [
            str(helper), "--design", str(design),
            "--mode", case["helper_mode"], "--root", case["id"],
            "--engine", configuration["engine"],
            "--optimization", configuration["optimization"],
            "--seed", str(defaults["seed"]), "--cache", str(cache),
            "--max-deltas", str(defaults["max_deltas"]),
        ]
    else:
        simulate = [
            str(executable), "simulate", "--design", str(design),
            "--engine", configuration["engine"],
            "--optimization", configuration["optimization"],
            "--seed", str(defaults["seed"]), "--cache", str(cache),
            "--max-deltas", str(defaults["max_deltas"]),
        ]
    observation = case.get("observation", {"kind": "none"})
    if case.get("runner") == "helper" and case.get("helper_mode") == "coverage":
        simulate.extend([
            "--file-root", str(run_directory),
            "--coverage-path", observation.get(
                "path", "observations/functional.fsimcov"
            ),
        ])
    elif observation["kind"] == "functional_coverage":
        simulate.extend(["--file-root", str(run_directory)])
    observation_directory = run_directory / "observations"
    if observation["kind"] in ("vcd", "fst"):
        trace = observation_directory / f"simulation.{observation['kind']}"
        simulate.extend([
            "--trace", str(trace), "--trace-format", observation["kind"],
        ])
        for pattern in observation.get("filters", []):
            simulate.extend([
                "--trace-filter", pattern.replace("{root}", case["id"]),
            ])
    simulate.extend(case.get("simulate_options", []))
    return {
        "compile": compile_commands, "elaborate": elaborate, "simulate": simulate,
        "artifacts": artifacts, "cache": cache,
        "observation_directory": observation_directory,
    }


def validate_sources(
    cases: list[dict[str, Any]], manifest: dict[str, Any], roots: dict[str, Path]
) -> None:
    placeholder = Path("/generated")
    for case in cases:
        root = roots[case["root"]]
        if not root.is_dir():
            raise QualificationError(f"source root is not a directory: {root}")
        for group in case["source_groups"]:
            for path in source_paths(case, group, manifest, roots, placeholder):
                if path.is_relative_to(placeholder):
                    continue
                if not path.is_file():
                    raise QualificationError(f"{case['id']} source missing: {path}")


def write_command(path: Path, command: list[str]) -> None:
    path.write_text(shlex.join(command) + "\n", encoding="utf-8")


def run_measured(
    command: list[str], cwd: Path, stem: Path, environment: dict[str, str]
) -> dict[str, Any]:
    metric = stem.with_suffix(".time")
    stdout_path = stem.with_suffix(".stdout")
    stderr_path = stem.with_suffix(".stderr")
    write_command(stem.with_suffix(".command"), command)
    timed = [
        str(TIME_PROGRAM), "--format=%e\t%M", f"--output={metric}", "--", *command
    ]
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        started = time.perf_counter()
        completed = run_bounded(
            timed, cwd=cwd, environment=environment, stdout=stdout, stderr=stderr
        )
        elapsed = time.perf_counter() - started
    if completed.returncode != 0:
        raise QualificationError(
            f"command failed at {stem.name} with status {completed.returncode}; "
            f"see {stderr_path}"
        )
    fields = metric.read_text(encoding="utf-8").strip().split("\t")
    if len(fields) != 2:
        raise QualificationError(f"malformed GNU time output: {metric}")
    return {
        "command": command, "command_sha256": command_sha256(command),
        "wall_seconds": elapsed, "gnu_time_seconds": float(fields[0]),
        "peak_rss_kib": int(fields[1]), "stdout": str(stdout_path),
        "stderr": str(stderr_path), "stdout_sha256": sha256(stdout_path),
        "stderr_sha256": sha256(stderr_path),
    }


def run_unmeasured(
    command: list[str], cwd: Path, stem: Path, environment: dict[str, str]
) -> dict[str, Any]:
    stdout_path = stem.with_suffix(".stdout")
    stderr_path = stem.with_suffix(".stderr")
    write_command(stem.with_suffix(".command"), command)
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        completed = run_bounded(
            command, cwd=cwd, environment=environment,
            stdout=stdout, stderr=stderr,
        )
    if completed.returncode != 0:
        raise QualificationError(
            f"untimed profile command failed at {stem.name} with status "
            f"{completed.returncode}; see {stderr_path}"
        )
    return {
        "command": command,
        "command_sha256": command_sha256(command),
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "stdout_sha256": sha256(stdout_path),
        "stderr_sha256": sha256(stderr_path),
    }


def vcd_observation(
    path: Path, minimum_value_changes: int = MINIMUM_TRACE_VALUE_CHANGES
) -> dict[str, Any]:
    if not path.is_file():
        raise QualificationError(f"VCD observation is missing: {path}")
    event_count = 0
    timestamp_count = 0
    enddefinitions = False
    skipped_metadata = False
    declaration_digest = hashlib.sha256()
    event_digest = hashlib.sha256()
    with path.open("rb") as stream:
        for line in stream:
            stripped = line.lstrip()
            normalized = b" ".join(stripped.split())
            if skipped_metadata:
                if b"$end" in stripped:
                    skipped_metadata = False
                continue
            if stripped.startswith(
                (b"$date", b"$version", b"$comment")
            ):
                if b"$end" not in stripped:
                    skipped_metadata = True
                continue
            if stripped.startswith(b"$enddefinitions $end"):
                enddefinitions = True
            if not enddefinitions:
                declaration_digest.update(normalized + b"\n")
            elif normalized:
                event_digest.update(normalized + b"\n")
                if stripped.startswith(b"#"):
                    timestamp_count += 1
            if (
                stripped[:1] in (b"0", b"1", b"x", b"X", b"z", b"Z", b"b", b"B", b"r", b"R")
                and not stripped.startswith((b"$", b"#"))
            ):
                event_count += 1
    if not enddefinitions:
        raise QualificationError(f"VCD observation lacks enddefinitions: {path}")
    if event_count < minimum_value_changes:
        raise QualificationError(
            f"VCD observation has {event_count} value changes, expected at least "
            f"{minimum_value_changes}"
        )
    digest = sha256(path)
    semantic = {
        "declarations_sha256": declaration_digest.hexdigest(),
        "events_sha256": event_digest.hexdigest(),
        "timestamp_count": timestamp_count,
        "value_change_lines": event_count,
    }
    equivalence = hashlib.sha256(
        json.dumps(semantic, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    return {
        "kind": "vcd", "path": str(path), "size": path.stat().st_size,
        "sha256": digest, "value_change_lines": event_count,
        "timestamp_count": timestamp_count,
        "declarations_sha256": semantic["declarations_sha256"],
        "events_sha256": semantic["events_sha256"],
        "equivalence_sha256": equivalence,
    }


def fst_observation(
    path: Path, cwd: Path, environment: dict[str, str],
    minimum_value_changes: int
) -> dict[str, Any]:
    if not path.is_file():
        raise QualificationError(f"FST observation is missing: {path}")
    size = path.stat().st_size
    if size < 16:
        raise QualificationError(f"FST observation is unexpectedly small: {path}")
    if not FST2VCD_PROGRAM.is_file():
        raise QualificationError(
            f"FST semantic decoder is missing: {FST2VCD_PROGRAM}"
        )
    digest = sha256(path)
    conversion = run_unmeasured(
        [str(FST2VCD_PROGRAM), str(path)],
        cwd,
        cwd / "fst2vcd",
        environment,
    )
    decoded = vcd_observation(
        Path(conversion["stdout"]), minimum_value_changes
    )
    return {
        "kind": "fst", "path": str(path), "size": size,
        "sha256": digest,
        "decoder": {
            "path": str(FST2VCD_PROGRAM),
            "sha256": sha256(FST2VCD_PROGRAM),
            "conversion": conversion,
        },
        "decoded_vcd": decoded,
        "value_change_lines": decoded["value_change_lines"],
        "equivalence_sha256": decoded["equivalence_sha256"],
    }


def functional_coverage_observation(
    path: Path, stdout_path: Path
) -> dict[str, Any]:
    if not path.is_file() or path.stat().st_size == 0:
        raise QualificationError(
            f"functional coverage database is missing or empty: {path}"
        )
    if not stdout_path.is_file():
        raise QualificationError(
            f"functional coverage semantic digest output is missing: {stdout_path}"
        )
    output = stdout_path.read_text(encoding="utf-8", errors="replace")
    digests = FUNCTIONAL_COVERAGE_DIGEST_PATTERN.findall(output)
    if len(digests) != 1:
        raise QualificationError(
            f"functional coverage semantic digest must occur exactly once in "
            f"{stdout_path}; found {len(digests)}"
        )
    semantic_digest = digests[0]
    if re.fullmatch(r"[0-9a-f]{64}", semantic_digest) is None:
        raise QualificationError(
            f"functional coverage semantic digest in {stdout_path} must contain "
            "exactly 64 lowercase hexadecimal digits"
        )
    digest = sha256(path)
    return {
        "kind": "functional_coverage",
        "path": str(path),
        "size": path.stat().st_size,
        "sha256": digest,
        "semantic_sha256": semantic_digest,
        "equivalence_sha256": semantic_digest,
    }


def capture_observation(
    case: dict[str, Any], executable: Path, run_directory: Path,
    environment: dict[str, str], stdout_path: Path | None = None
) -> dict[str, Any] | None:
    observation = case.get("observation", {"kind": "none"})
    kind = observation["kind"]
    if kind == "none":
        return None
    relative = observation.get(
        "path",
        f"observations/simulation.{kind}",
    )
    path = run_directory / relative
    minimum_value_changes = observation.get(
        "minimum_value_changes", MINIMUM_TRACE_VALUE_CHANGES
    )
    if kind == "vcd":
        return vcd_observation(path, minimum_value_changes)
    if kind == "fst":
        return fst_observation(
            path, run_directory, environment, minimum_value_changes
        )
    if kind == "functional_coverage":
        if stdout_path is None:
            raise QualificationError(
                "functional coverage observation requires simulation stdout"
            )
        return functional_coverage_observation(path, stdout_path)
    raise QualificationError(f"{case['id']} has unknown observation kind {kind}")


def require_all_profile_lines_parsed(
    lines: list[str], prefix: str, parsed_count: int
) -> None:
    actual = sum(line.startswith(prefix) for line in lines)
    if actual != parsed_count:
        raise QualificationError(
            f"malformed profiling evidence for {prefix!r}: "
            f"parsed {parsed_count} of {actual} lines"
        )


def parse_phase_profile(stdout: str) -> dict[str, float]:
    lines = stdout.splitlines()
    matches = [
        match for line in lines
        if (match := PHASE_PROFILE_PATTERN.fullmatch(line))
    ]
    require_all_profile_lines_parsed(lines, "FSIM-PROFILE ", len(matches))
    if len(matches) != 1:
        raise QualificationError(
            f"expected exactly one FSIM-PROFILE phase line, found {len(matches)}"
        )
    match = matches[0]
    return {
        "setup_ms": float(match.group(1)),
        "run_ms": float(match.group(2)),
        "native_await_ms": float(match.group(3)),
    }


def parse_native_profile(engine: str, stderr: str) -> dict[str, Any]:
    lines = stderr.splitlines()
    setups = [
        match for line in lines
        if (match := JIT_SETUP_PATTERN.fullmatch(line))
    ]
    summaries = [
        match for line in lines
        if (match := JIT_SUMMARY_PATTERN.fullmatch(line))
    ]
    processes = [
        match for line in lines
        if (match := JIT_PROCESS_PATTERN.fullmatch(line))
    ]
    modules = [
        match for line in lines
        if (match := JIT_MODULE_PATTERN.fullmatch(line))
    ]
    unsupported = [
        match for line in lines
        if (match := JIT_UNSUPPORTED_PATTERN.fullmatch(line))
    ]
    require_all_profile_lines_parsed(
        lines, "fsim-profile: jit setup_ms=", len(setups)
    )
    require_all_profile_lines_parsed(
        lines, "fsim-profile: jit-process-summary", len(summaries)
    )
    require_all_profile_lines_parsed(
        lines, "fsim-profile: jit-process id=", len(processes)
    )
    require_all_profile_lines_parsed(
        lines, "fsim jit module profile:", len(modules)
    )
    require_all_profile_lines_parsed(
        lines, "fsim jit unsupported module:", len(unsupported)
    )
    if engine == "interpreter":
        if setups or processes or modules or unsupported:
            raise QualificationError(
                "interpreter profile unexpectedly reports native execution"
            )
        if summaries:
            if len(summaries) != 1:
                raise QualificationError(
                    "interpreter profile has multiple JIT summaries"
                )
            if int(summaries[0].group(1)) or int(summaries[0].group(2)):
                raise QualificationError(
                    "interpreter profile reports nonzero native execution"
                )
        return {
            "applicable": False,
            "reason": "interpreter configuration has no native execution",
            "summary_observed": bool(summaries),
        }
    if engine != "compiled":
        raise QualificationError(f"unknown profiling engine: {engine}")
    if len(setups) != 1:
        raise QualificationError(
            f"compiled profile expected one JIT setup record, found {len(setups)}"
        )
    setup_match = setups[0]
    setup = {
        "setup_ms": float(setup_match.group(1)),
        "registration_ms": float(setup_match.group(2)),
        "materialization_launch_ms": float(setup_match.group(3)),
        "modules": int(setup_match.group(4)),
        "unpacked_modules": int(setup_match.group(5)),
        "selected_processes": int(setup_match.group(6)),
        "selected_operations": int(setup_match.group(7)),
        "lowered_processes": int(setup_match.group(8)),
        "lowered_operations": int(setup_match.group(9)),
        "largest_module_operations": int(setup_match.group(10)),
        "largest_module_identity": setup_match.group(11),
        "retained_processes": int(setup_match.group(12)),
        "retained_operations": int(setup_match.group(13)),
        "selective_large_design": bool(int(setup_match.group(14))),
        "compile_all": bool(int(setup_match.group(15))),
    }
    no_native_selected = (
        setup["selected_processes"] == 0 or setup["modules"] == 0
    )
    if len(summaries) > 1:
        raise QualificationError(
            f"compiled profile expected at most one JIT summary, found {len(summaries)}"
        )
    if not summaries and not no_native_selected:
        raise QualificationError(
            "compiled profile lacks JIT process evidence despite selected native work"
        )
    if not modules and not no_native_selected:
        raise QualificationError(
            "compiled profile contains no module evidence despite selected native work"
        )
    if no_native_selected and (summaries or processes or modules):
        raise QualificationError(
            "compiled profile reports native execution rows inconsistent with JIT setup"
        )

    process_records = [{
        "id": int(match.group(1)),
        "generated_id": int(match.group(2)),
        "resumes": int(match.group(3)),
        "direct_read_slots": int(match.group(4)),
        "direct_update_slots": int(match.group(5)),
        "operations": int(match.group(6)),
        "static_waits": int(match.group(7)),
        "elapsed_ms": float(match.group(8)),
        "name": match.group(9),
    } for match in processes]
    summary = ({
        "processes": int(summaries[0].group(1)),
        "resumes": int(summaries[0].group(2)),
        "elapsed_ms": float(summaries[0].group(3)),
    } if summaries else {
        "processes": 0,
        "resumes": 0,
        "elapsed_ms": 0.0,
    })
    if summary["processes"] != len(process_records):
        raise QualificationError(
            "JIT process summary count does not match detailed process evidence"
        )
    if summary["resumes"] != sum(item["resumes"] for item in process_records):
        raise QualificationError(
            "JIT process summary resumes do not match detailed evidence"
        )
    if any(item["resumes"] <= 0 for item in process_records):
        raise QualificationError(
            "JIT detailed process evidence contains a non-resumed process"
        )
    unsupported_records = [
        {"identity": match.group(1), "reason": match.group(2)}
        for match in unsupported
    ]
    module_records = []
    native_generated_process_ids = {
        item["generated_id"] for item in process_records
        if item["resumes"] > 0
    }
    for match in modules:
        process_ids = [
            int(value) for value in match.group(3).split(",") if value
        ]
        identity = match.group(1)
        if int(match.group(2)) != len(process_ids):
            raise QualificationError(
                f"JIT module {identity} process count does not match process ids"
            )
        unsupported_matches = [
            item for item in unsupported_records
            if item["identity"][:96] == identity
        ]
        unambiguous = (
            len(unsupported_matches) == 1
            and sum(
                unsupported_matches[0]["identity"][:96]
                == candidate.group(1)
                for candidate in modules
            ) == 1
        )
        module_records.append({
            "identity": identity,
            "processes": int(match.group(2)),
            "process_ids": process_ids,
            "operations": int(match.group(4)),
            "weight": int(match.group(5)),
            "worker": int(match.group(6)),
            "startup": bool(int(match.group(7))),
            "materialization_ms": float(match.group(8)),
            "unsupported_attribution": (
                "matched" if unambiguous
                else "ambiguous" if unsupported_matches
                else "none"
            ),
            "unsupported_reasons": [
                item["reason"] for item in unsupported_matches
            ],
            "native_resume_observed": any(
                process_id in native_generated_process_ids
                for process_id in process_ids
            ),
        })
    unattributed = [
        item for item in unsupported_records
        if sum(item["identity"][:96] == module["identity"]
               for module in module_records) != 1
    ]
    if setup["selected_processes"] == 0 and setup["retained_processes"]:
        native_evidence = "all reported processes retained for interpreter fallback"
    elif setup["selected_processes"] == 0:
        native_evidence = "no native processes selected"
    elif setup["modules"] == 0:
        native_evidence = "no native module registered"
    else:
        native_evidence = "native work selected; observed resumes reported separately"
    return {
        "applicable": True,
        "setup": setup,
        "native_selection_evidence": native_evidence,
        "execution_summary_observed": bool(summaries),
        "summary": summary,
        "native_processes": process_records,
        "modules": module_records,
        "unsupported_modules": unsupported_records,
        "unattributed_unsupported_modules": unattributed,
        "modules_attempted": len(module_records),
        "modules_without_unsupported_report": sum(
            module["unsupported_attribution"] == "none"
            for module in module_records
        ),
        "native_processes_resumed": sum(
            item["resumes"] > 0 for item in process_records
        ),
        "native_resumes": summary["resumes"],
        "static_operation_count_for_profiled_processes": sum(
            item["operations"] for item in process_records
        ),
        "modules_without_native_resumes": [
            module["identity"] for module in module_records
            if not module["native_resume_observed"]
        ],
    }


def functional_profile_output(stdout: str) -> bytes:
    return b"\n".join(
        line.encode()
        for line in stdout.splitlines()
        if not line.startswith("FSIM-PROFILE ")
    ) + b"\n"


def validate_correctness(case: dict[str, Any], stdout_path: Path) -> None:
    output = stdout_path.read_text(encoding="utf-8", errors="replace")
    for requirement in case["correctness"]["required"]:
        count = len(re.findall(requirement["pattern"], output))
        if count != requirement["count"]:
            raise QualificationError(
                f"{case['id']} expected {requirement['count']} matches for "
                f"{requirement['pattern']!r}, found {count}"
            )
    for pattern in case["correctness"]["forbidden"]:
        if re.search(pattern, output):
            raise QualificationError(
                f"{case['id']} matched forbidden output {pattern!r}"
            )


def run_sample(
    case: dict[str, Any], configuration: dict[str, Any], variant: str,
    executable: Path, manifest: dict[str, Any], roots: dict[str, Path],
    run_directory: Path, environment: dict[str, str], helper: Path | None = None
) -> dict[str, Any]:
    run_directory.mkdir(parents=True)
    commands = materialize_commands(
        case, configuration, executable, manifest, roots, run_directory, True,
        helper,
    )
    generated_wrapper = None
    wrapper_text = case_wrapper(case)
    if wrapper_text is not None:
        wrapper_path = run_directory / "generated/wrapper.sv"
        generated_wrapper = {
            "path": f"{case['id']}/generated/wrapper.sv",
            "sha256": sha256(wrapper_path),
        }
        expected = hashlib.sha256(wrapper_text.encode()).hexdigest()
        if generated_wrapper["sha256"] != expected:
            raise QualificationError(
                f"generated wrapper identity mismatch: {wrapper_path}"
            )
    commands["artifacts"].mkdir()
    commands["cache"].mkdir()
    commands["observation_directory"].mkdir()
    compile_metrics = [
        run_measured(command, run_directory, run_directory / f"compile-{index}", environment)
        for index, command in enumerate(commands["compile"], 1)
    ]
    elaborate = run_measured(
        commands["elaborate"], run_directory, run_directory / "elaborate", environment
    )
    simulate = run_measured(
        commands["simulate"], run_directory, run_directory / "simulate", environment
    )
    validate_correctness(case, Path(simulate["stdout"]))
    observation = capture_observation(
        case, executable, run_directory, environment, Path(simulate["stdout"])
    )
    all_metrics = [*compile_metrics, elaborate, simulate]
    return {
        "case": case["id"], "configuration": configuration["id"],
        "variant": variant,
        "phases": {
            "compile": compile_metrics, "elaborate": elaborate, "simulate": simulate
        },
        "e2e_wall_seconds": sum(item["wall_seconds"] for item in all_metrics),
        "e2e_peak_rss_kib": max(item["peak_rss_kib"] for item in all_metrics),
        "simulation_stdout_sha256": simulate["stdout_sha256"],
        "generated_wrapper_identity": generated_wrapper,
        "observation": observation,
    }


def run_profile_pass(
    case: dict[str, Any], configuration: dict[str, Any], variant: str,
    executable: Path, manifest: dict[str, Any], roots: dict[str, Path],
    run_directory: Path, environment: dict[str, str], helper: Path | None = None
) -> dict[str, Any]:
    run_directory.mkdir(parents=True)
    commands = materialize_commands(
        case, configuration, executable, manifest, roots, run_directory, True,
        helper,
    )
    generated_wrapper = None
    wrapper_text = case_wrapper(case)
    if wrapper_text is not None:
        wrapper_path = run_directory / "generated/wrapper.sv"
        generated_wrapper = {
            "path": f"{case['id']}/generated/wrapper.sv",
            "sha256": sha256(wrapper_path),
        }
        expected = hashlib.sha256(wrapper_text.encode()).hexdigest()
        if generated_wrapper["sha256"] != expected:
            raise QualificationError(
                f"generated profile wrapper identity mismatch: {wrapper_path}"
            )
    commands["artifacts"].mkdir()
    commands["cache"].mkdir()
    commands["observation_directory"].mkdir()
    compile_records = [
        run_unmeasured(
            command, run_directory, run_directory / f"compile-{index}",
            environment,
        )
        for index, command in enumerate(commands["compile"], 1)
    ]
    elaborate_record = run_unmeasured(
        commands["elaborate"], run_directory, run_directory / "elaborate",
        environment,
    )
    profile_environment = environment.copy()
    profile_environment.update(PROFILE_ENVIRONMENT)
    simulate_record = run_unmeasured(
        commands["simulate"], run_directory, run_directory / "simulate",
        profile_environment,
    )
    stdout_path = Path(simulate_record["stdout"])
    stderr_path = Path(simulate_record["stderr"])
    validate_correctness(case, stdout_path)
    stdout = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
    observation = capture_observation(
        case, executable, run_directory, environment, stdout_path
    )
    return {
        "case": case["id"],
        "configuration": configuration["id"],
        "variant": variant,
        "timed": False,
        "profile_environment": dict(PROFILE_ENVIRONMENT),
        "commands": {
            "compile": compile_records,
            "elaborate": elaborate_record,
            "simulate": simulate_record,
        },
        "phase_profile": parse_phase_profile(stdout),
        "native_execution": parse_native_profile(
            configuration["engine"], stderr
        ),
        "functional_stdout_sha256": hashlib.sha256(
            functional_profile_output(stdout)
        ).hexdigest(),
        "generated_wrapper_identity": generated_wrapper,
        "observation": observation,
    }


def require_profile_equivalence(
    case: str, configuration: str, pair: dict[str, dict[str, Any]]
) -> None:
    if (
        pair["baseline"]["functional_stdout_sha256"]
        != pair["candidate"]["functional_stdout_sha256"]
    ):
        raise QualificationError(
            f"{case}/{configuration} untimed profile output differs"
        )
    require_observation_equivalence(
        case, configuration, "untimed profile", pair
    )


def require_observation_equivalence(
    case: str, configuration: str, sample: int | str,
    pair: dict[str, dict[str, Any]]
) -> None:
    baseline = pair["baseline"].get("observation")
    candidate = pair["candidate"].get("observation")
    if (baseline is None) != (candidate is None):
        raise QualificationError(
            f"{case}/{configuration} {sample} observation presence differs"
        )
    if baseline is None:
        return
    if baseline["kind"] == "fst" and (
        baseline["decoder"]["sha256"] != candidate["decoder"]["sha256"]
    ):
        raise QualificationError(
            f"{case}/{configuration} {sample} FST decoder identity differs"
        )
    if (
        baseline["kind"] != candidate["kind"]
        or baseline["equivalence_sha256"]
        != candidate["equivalence_sha256"]
    ):
        raise QualificationError(
            f"{case}/{configuration} {sample} observation differs"
        )


def require_equivalence(
    case: str, configuration: str, sample: int, pair: dict[str, dict[str, Any]]
) -> None:
    if (
        pair["baseline"]["simulation_stdout_sha256"]
        != pair["candidate"]["simulation_stdout_sha256"]
    ):
        raise QualificationError(
            f"{case}/{configuration} sample {sample} simulation output differs"
        )
    require_observation_equivalence(case, configuration, sample, pair)


def phase_median(samples: list[dict[str, Any]], phase: str) -> dict[str, float]:
    def values(sample: dict[str, Any]) -> list[dict[str, Any]]:
        value = sample["phases"][phase]
        return value if isinstance(value, list) else [value]
    return {
        "wall_seconds": float(statistics.median(
            sum(item["wall_seconds"] for item in values(sample)) for sample in samples
        )),
        "peak_rss_kib": float(statistics.median(
            max(item["peak_rss_kib"] for item in values(sample)) for sample in samples
        )),
    }


def summarize(
    results: list[dict[str, Any]], cases: list[dict[str, Any]],
    configurations: list[dict[str, Any]], manifest: dict[str, Any]
) -> tuple[dict[str, Any], bool]:
    expected = manifest["defaults"]["samples"]
    limit = manifest["defaults"]["regression_limit"]
    summary = {}
    passed = True
    for case in cases:
        summary[case["id"]] = {}
        for configuration in configurations_for_case(case, configurations):
            entry = {}
            for variant in ("baseline", "candidate"):
                samples = [
                    item for item in results
                    if item["case"] == case["id"]
                    and item["configuration"] == configuration["id"]
                    and item["variant"] == variant
                ]
                if len(samples) != expected:
                    raise QualificationError(
                        f"{case['id']}/{configuration['id']}/{variant} has "
                        f"{len(samples)} samples, expected {expected}"
                    )
                entry[variant] = {
                    "phases": {
                        phase: phase_median(samples, phase)
                        for phase in ("compile", "elaborate", "simulate")
                    },
                    "e2e_wall_seconds": float(statistics.median(
                        item["e2e_wall_seconds"] for item in samples
                    )),
                    "e2e_peak_rss_kib": float(statistics.median(
                        item["e2e_peak_rss_kib"] for item in samples
                    )),
                }
            baseline = entry["baseline"]
            candidate = entry["candidate"]
            if baseline["e2e_wall_seconds"] <= 0 or baseline["e2e_peak_rss_kib"] <= 0:
                raise QualificationError("baseline medians must be positive")
            entry["ratios"] = {
                "e2e_wall": (
                    candidate["e2e_wall_seconds"] / baseline["e2e_wall_seconds"]
                ),
                "e2e_peak_rss": (
                    candidate["e2e_peak_rss_kib"] / baseline["e2e_peak_rss_kib"]
                ),
            }
            entry["gate_passed"] = all(
                value <= limit for value in entry["ratios"].values()
            )
            passed = passed and entry["gate_passed"]
            summary[case["id"]][configuration["id"]] = entry
    return summary, passed


def qualification_blockers(
    manifest: dict[str, Any], cases: list[dict[str, Any]],
    configurations: list[dict[str, Any]]
) -> list[str]:
    blockers = []
    pending = [
        case["id"] for case in manifest["cases"]
        if case.get("status") == "pending"
    ]
    if pending:
        blockers.append("pending cases: " + ", ".join(pending))
    unverified = [
        case["id"] for case in manifest["cases"]
        if case.get("verification_status") == "implementation_unverified"
    ]
    if unverified:
        blockers.append("implementation-unverified cases: " + ", ".join(unverified))
    required_cases = {
        case["id"] for case in manifest["cases"]
        if case.get("status", "active") == "active"
    }
    missing_cases = sorted(required_cases - {case["id"] for case in cases})
    if missing_cases:
        blockers.append("required cases omitted: " + ", ".join(missing_cases))
    required_configurations = {
        entry["id"] for entry in manifest["configurations"]
    }
    missing_configurations = sorted(
        required_configurations
        - {configuration["id"] for configuration in configurations}
    )
    if missing_configurations:
        blockers.append(
            "required configurations omitted: " + ", ".join(missing_configurations)
        )
    required_pairs = {
        (case["id"], configuration_id)
        for case in manifest["cases"]
        if case.get("status", "active") == "active"
        for configuration_id in case["configurations"]
    }
    selected_pairs = {
        (case["id"], configuration["id"])
        for case in cases
        for configuration in configurations_for_case(case, configurations)
    }
    missing_pairs = sorted(required_pairs - selected_pairs)
    if missing_pairs:
        blockers.append(
            "required case/configuration pairs omitted: "
            + ", ".join(f"{case}/{configuration}" for case, configuration in missing_pairs)
        )
    incomplete_foundation = [
        name for name, requirement in manifest["qualification_requirements"].items()
        if requirement.get("status") != "complete"
    ]
    if incomplete_foundation:
        blockers.append(
            "qualification foundation incomplete: "
            + ", ".join(sorted(incomplete_foundation))
        )
    return blockers


def decoded_output(completed: subprocess.CompletedProcess[Any]) -> tuple[str, str]:
    return (
        (completed.stdout or b"").decode("utf-8", errors="replace"),
        (completed.stderr or b"").decode("utf-8", errors="replace"),
    )


def linked_dependency_identity(executable: Path) -> list[dict[str, str]]:
    if not LDD_PROGRAM.is_file():
        raise QualificationError(f"linked dependency tool is missing: {LDD_PROGRAM}")
    completed = run_bounded(
        [str(LDD_PROGRAM), str(executable)], timeout=VERSION_TIMEOUT_SECONDS
    )
    stdout, stderr = decoded_output(completed)
    if completed.returncode:
        raise QualificationError(
            f"cannot resolve linked dependencies for {executable}: {stderr.strip()}"
        )
    dependencies = []
    for raw_line in stdout.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("linux-vdso"):
            continue
        if "=>" in line:
            name, target = (part.strip() for part in line.split("=>", 1))
            path_text = target.split(" (", 1)[0].strip()
            if path_text == "not found":
                raise QualificationError(
                    f"linked dependency {name} is unavailable for {executable}"
                )
        else:
            path_text = line.split(" (", 1)[0].strip()
            name = Path(path_text).name
        path = Path(path_text)
        if not path.is_absolute() or not path.is_file():
            raise QualificationError(
                f"invalid linked dependency for {executable}: {line}"
            )
        dependencies.append({
            "name": name,
            "path": str(path.resolve()),
            "sha256": sha256(path),
        })
    if not dependencies:
        raise QualificationError(f"no linked runtime dependencies found: {executable}")
    return dependencies


def elf_build_identity(executable: Path) -> dict[str, Any]:
    if not READELF_PROGRAM.is_file():
        raise QualificationError(f"ELF identity tool is missing: {READELF_PROGRAM}")
    completed = run_bounded(
        [
            str(READELF_PROGRAM), "--notes", "--string-dump=.comment",
            str(executable),
        ],
        timeout=VERSION_TIMEOUT_SECONDS,
    )
    stdout, stderr = decoded_output(completed)
    if completed.returncode:
        raise QualificationError(
            f"cannot read ELF build identity for {executable}: {stderr.strip()}"
        )
    build_ids = re.findall(r"Build ID:\s*([0-9a-fA-F]+)", stdout)
    compiler_comments = []
    for line in stdout.splitlines():
        match = re.match(r"\s*\[\s*[0-9a-fA-F]+\]\s+(.+)", line)
        if match:
            compiler_comments.append(match.group(1).strip())
    if not build_ids or not compiler_comments:
        raise QualificationError(
            f"ELF build/compiler identity is unavailable: {executable}"
        )
    return {
        "build_ids": build_ids,
        "compiler_comments": sorted(set(compiler_comments)),
    }


def executable_identity(executable: Path) -> dict[str, Any]:
    completed = run_bounded(
        [str(executable), "--version"], timeout=VERSION_TIMEOUT_SECONDS
    )
    if completed.returncode:
        raise QualificationError(f"cannot query executable version: {executable}")
    stdout, stderr = decoded_output(completed)
    dependencies = linked_dependency_identity(executable)
    return {
        "path": str(executable), "sha256": sha256(executable),
        "version_stdout": stdout,
        "version_stderr": stderr,
        "elf": elf_build_identity(executable),
        "linked_dependencies": dependencies,
        "llvm_runtime_dependencies": [
            dependency for dependency in dependencies
            if "llvm" in dependency["name"].lower()
        ],
    }


def dependency_fingerprint(identity: dict[str, Any]) -> list[tuple[str, str]]:
    return sorted(
        (dependency["name"], dependency["sha256"])
        for dependency in identity["linked_dependencies"]
    )


def require_matching_runtime_dependencies(
    baseline: dict[str, Any], candidate: dict[str, Any]
) -> None:
    if dependency_fingerprint(baseline) != dependency_fingerprint(candidate):
        raise QualificationError(
            "baseline and candidate linked runtime dependencies differ"
        )


def tool_identity(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise QualificationError(f"identity tool is missing: {path}")
    completed = run_bounded(
        [str(path), "--version"], timeout=VERSION_TIMEOUT_SECONDS
    )
    stdout, stderr = decoded_output(completed)
    if completed.returncode:
        raise QualificationError(
            f"cannot query tool version for {path}: {stderr.strip()}"
        )
    return {
        "path": str(path), "sha256": sha256(path),
        "version_stdout": stdout, "version_stderr": stderr,
    }


def system_identity() -> dict[str, Any]:
    return {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "gnu_time": tool_identity(TIME_PROGRAM),
        "ldd": tool_identity(LDD_PROGRAM),
        "readelf": tool_identity(READELF_PROGRAM),
    }


def require_executable(path: Path | None, label: str) -> Path:
    if path is None:
        raise QualificationError(f"--{label}-fsim is required")
    result = path.expanduser().resolve()
    if not result.is_file() or not os.access(result, os.X_OK):
        raise QualificationError(f"{label} fsim is not executable: {result}")
    return result


def require_helper(path: Path | None, label: str) -> Path:
    if path is None:
        raise QualificationError(f"--{label}-helper is required for helper cases")
    result = path.expanduser().resolve()
    if not result.is_file() or not os.access(result, os.X_OK):
        raise QualificationError(f"{label} helper is not executable: {result}")
    return result


def preflight(
    cases: list[dict[str, Any]], configurations: list[dict[str, Any]],
    manifest: dict[str, Any], roots: dict[str, Path], arguments: argparse.Namespace
) -> None:
    print("preflight only; no qualification")
    executables = {
        "baseline": arguments.baseline_fsim or Path("<baseline-fsim>"),
        "candidate": arguments.candidate_fsim or Path("<candidate-fsim>"),
    }
    helpers = {
        "baseline": arguments.baseline_helper
            or Path("<baseline-simplification-benchmark-helper>"),
        "candidate": arguments.candidate_helper
            or Path("<candidate-simplification-benchmark-helper>"),
    }
    for case in cases:
        for configuration in configurations_for_case(case, configurations):
            for variant, executable in executables.items():
                run = (
                    Path("<evidence>") / case["id"] / configuration["id"]
                    / "sample-01" / variant
                )
                commands = materialize_commands(
                    case, configuration, executable, manifest, roots, run,
                    helper=helpers[variant],
                )
                print(f"[{case['id']} {configuration['id']} {variant}]")
                wrapper_text = case_wrapper(case)
                if wrapper_text is not None:
                    print(
                        f"wrapper {run / 'generated/wrapper.sv'}: "
                        f"{wrapper_text.strip()}"
                    )
                for command in commands["compile"]:
                    print(shlex.join(command))
                print(shlex.join(commands["elaborate"]))
                print(shlex.join(commands["simulate"]))
    print("preflight only; no qualification")


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    try:
        manifest_path = arguments.manifest.expanduser().resolve()
        manifest = load_manifest(manifest_path)
        roots = resolve_roots(
            manifest, manifest_path, parse_root_overrides(arguments.root)
        )
        cases = selected_entries(manifest["cases"], arguments.cases, "case")
        configurations = selected_entries(
            manifest["configurations"], arguments.configurations, "configuration"
        )
        if arguments.cases is not None and arguments.configurations is not None:
            unsupported_pairs = [
                f"{case['id']}/{configuration['id']}"
                for case in cases
                for configuration in configurations
                if configuration["id"] not in case["configurations"]
            ]
            if unsupported_pairs:
                raise QualificationError(
                    "unsupported case/configuration pairs: "
                    + ", ".join(unsupported_pairs)
                )
        if arguments.list_cases:
            for case in manifest["cases"]:
                suffix = f": {case.get('reason')}" if case.get("reason") else ""
                print(f"{case['id']}\t{case.get('status', 'active')}{suffix}")
            return 0
        validate_sources(cases, manifest, roots)
        source_identity_before = capture_source_identity(cases, manifest, roots)
        if arguments.preflight_only:
            preflight(cases, configurations, manifest, roots, arguments)
            return 0
        if not TIME_PROGRAM.is_file():
            raise QualificationError(f"GNU time is required at {TIME_PROGRAM}")
        baseline = require_executable(arguments.baseline_fsim, "baseline")
        candidate = require_executable(arguments.candidate_fsim, "candidate")
        if baseline == candidate:
            raise QualificationError("baseline and candidate executables must differ")
        helper_cases = any(case.get("runner") == "helper" for case in cases)
        helpers: dict[str, Path] = {}
        if helper_cases:
            helpers = {
                "baseline": require_helper(arguments.baseline_helper, "baseline"),
                "candidate": require_helper(arguments.candidate_helper, "candidate"),
            }
            if helpers["baseline"] == helpers["candidate"]:
                raise QualificationError(
                    "baseline and candidate helper executables must differ"
                )
        if arguments.output is None:
            evidence = Path(tempfile.mkdtemp(prefix="fsim-simplification-perf."))
        else:
            evidence = arguments.output.expanduser().resolve()
            evidence.mkdir(parents=True, exist_ok=True)
            if any(evidence.iterdir()):
                raise QualificationError(f"evidence directory is not empty: {evidence}")
        binary_identities_before = {
            "baseline": executable_identity(baseline),
            "candidate": executable_identity(candidate),
        }
        helper_identities_before = {
            variant: executable_identity(helper)
            for variant, helper in helpers.items()
        }
        system_identity_before = system_identity()
        identity_before = {
            "declared_baseline_revision": manifest["baseline_commit"],
            "baseline_revision_semantics": manifest["baseline_revision_semantics"],
            "source_identity": source_identity_before,
            "binary_identities": binary_identities_before,
            "helper_identities": helper_identities_before,
            "system_identity": system_identity_before,
        }
        (evidence / "identity-before.json").write_text(
            json.dumps(identity_before, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        require_matching_runtime_dependencies(
            binary_identities_before["baseline"],
            binary_identities_before["candidate"],
        )
        if helper_cases:
            require_matching_runtime_dependencies(
                helper_identities_before["baseline"],
                helper_identities_before["candidate"],
            )
        environment, removed_profiles = stable_environment()
        executables = {"baseline": baseline, "candidate": candidate}
        results = []
        profile_results = []
        samples = manifest["defaults"]["samples"]
        print(f"evidence: {evidence}", flush=True)
        for case in cases:
            for configuration in configurations_for_case(case, configurations):
                for sample in range(1, samples + 1):
                    order = (
                        ("baseline", "candidate")
                        if sample % 2 else ("candidate", "baseline")
                    )
                    pair = {}
                    for variant in order:
                        print(
                            f"{case['id']} {configuration['id']} sample "
                            f"{sample}/{samples}: {variant}", flush=True
                        )
                        directory = (
                            evidence / case["id"] / configuration["id"]
                            / f"sample-{sample:02d}" / variant
                        )
                        result = run_sample(
                            case, configuration, variant, executables[variant],
                            manifest, roots, directory, environment,
                            helpers.get(variant),
                        )
                        results.append(result)
                        pair[variant] = result
                    require_equivalence(
                        case["id"], configuration["id"], sample, pair
                    )
                profile_pair = {}
                for variant in ("baseline", "candidate"):
                    print(
                        f"{case['id']} {configuration['id']} untimed profile: "
                        f"{variant}",
                        flush=True,
                    )
                    profile_directory = (
                        evidence / "profiles" / case["id"]
                        / configuration["id"] / variant
                    )
                    profile = run_profile_pass(
                        case, configuration, variant, executables[variant],
                        manifest, roots, profile_directory, environment,
                        helpers.get(variant),
                    )
                    profile_results.append(profile)
                    profile_pair[variant] = profile
                require_profile_equivalence(
                    case["id"], configuration["id"], profile_pair
                )
        source_identity_after = capture_source_identity(cases, manifest, roots)
        require_identity_unchanged(
            source_identity_before, source_identity_after, "source corpus"
        )
        binary_identities_after = {
            "baseline": executable_identity(baseline),
            "candidate": executable_identity(candidate),
        }
        require_identity_unchanged(
            binary_identities_before, binary_identities_after,
            "executable and linked dependency",
        )
        helper_identities_after = {
            variant: executable_identity(helper)
            for variant, helper in helpers.items()
        }
        require_identity_unchanged(
            helper_identities_before, helper_identities_after,
            "helper executable and linked dependency",
        )
        system_identity_after = system_identity()
        require_identity_unchanged(
            system_identity_before, system_identity_after, "system tool"
        )
        summary, performance_gate_passed = summarize(
            results, cases, configurations, manifest
        )
        blockers = qualification_blockers(manifest, cases, configurations)
        passed = performance_gate_passed and not blockers
        report = {
            "declared_baseline_revision": manifest["baseline_commit"],
            "baseline_revision_semantics": (
                manifest["baseline_revision_semantics"]
            ),
            "manifest": str(manifest_path),
            "manifest_sha256": sha256(manifest_path),
            "source_identity": source_identity_before,
            "binary_identities": binary_identities_before,
            "helper_identities": helper_identities_before,
            "system_identity": system_identity_before,
            "post_run_identity_verified": True,
            "environment": {
                "stable_overrides": {
                    key: environment[key] for key in ("LANG", "LC_ALL", "TZ")
                },
                "removed_profile_variables": removed_profiles,
                "profile_capture": (
                    "not run with timed samples; phase timings are recorded separately"
                ),
            },
            "roots": {name: str(path) for name, path in roots.items()},
            "pending_cases": [
                {"id": case["id"], "reason": case["reason"]}
                for case in manifest["cases"] if case.get("status") == "pending"
            ],
            "qualification_requirements": manifest["qualification_requirements"],
            "qualification_blockers": blockers,
            "status": "passed" if passed else "incomplete",
            "performance_gate_passed": performance_gate_passed,
            "results": results,
            "profile_results": profile_results,
            "summary": summary,
            "passed": passed,
        }
        report_path = evidence / "qualification.json"
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(json.dumps(summary, indent=2, sort_keys=True))
        print(f"qualification evidence: {report_path}")
        if not performance_gate_passed:
            raise QualificationError(
                "candidate median end-to-end time or peak RSS exceeded 1.10"
            )
        if blockers:
            raise QualificationError(
                "diagnostic results complete but qualification is incomplete: "
                + "; ".join(blockers)
            )
        return 0
    except QualificationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
