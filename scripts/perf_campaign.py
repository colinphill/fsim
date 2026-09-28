#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Run a deterministic, paired fsim and Vivado performance campaign.

This runner deliberately owns one fresh workspace per simulator invocation.
All selected cases first run an untimed stimulus transcript comparison. Timing
starts only after every selected case passes that comparison.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import re
import shlex
import shutil
import signal
import stat
import statistics
import subprocess
import sys
import time
from typing import Any


REPOSITORY = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = Path(__file__).with_name("simplification_benchmarks.json")
DEFAULT_CASES = [
    "original_codec", "mixed_codec", "original_throughput",
    "mixed_throughput", "codex_reference_mode0_frames1",
]
DEFAULT_SEED = 0x6D2B79F5
XORSHIFT32_VERSION = "xorshift32-shift13-17-5-v1"
XORSHIFT32_KAT = (
    "40aec71f", "91e00c19", "9c0fe128", "6570f69d", "0fce02cc",
)
BASELINE_COMMIT = "d11004e41c929dcbd7ad7934921af722754bbdfa"
TIME_PROGRAM = Path("/usr/bin/time")
COMMAND_TIMEOUT_SECONDS = 7200
INCLUDE_PATTERN = re.compile(r'(?m)^\s*`include\s+"([^"]+)"')
STIM_LINE = re.compile(r"^\s*(STIM_EVENT|STIM_SUMMARY|STIM_KAT)\b(.*)$")
ASSIGNMENT = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
ERROR_PATTERN = re.compile(r"\b(?:ERROR|FATAL):", re.IGNORECASE)
EXPECTED_CODEC_SCENARIOS = [0, 1, 2, 3, 4, 7, 5]
REFERENCE_CASES = [
    "original_codec", "original_throughput", "mixed_codec", "mixed_throughput",
    "codex_reference_mode0_frames1", "codex_reference_mode0_frames2",
    "codex_reference_mode1_frames1", "codex_reference_mode1_frames2",
    "codex_throughput_default", "codex_throughput_direct_syndrome",
]


class CampaignError(RuntimeError):
    """The selected corpus, tools, correctness, or parity check is invalid."""


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--fsim", type=Path,
                        help="frozen fsim executable to benchmark")
    parser.add_argument("--vivado-bin", type=Path,
                        default=Path("/opt/eda/AMD/2025.2.1/Vivado/bin"))
    parser.add_argument("--output", type=Path,
                        help="new or empty directory for campaign evidence")
    parser.add_argument("--case", action="append", dest="cases",
                        help="manifest case ID; repeat for a corpus subset")
    parser.add_argument("--configuration", action="append", dest="configurations",
                        choices=("llvm_o2", "llvm_o0", "llvm_o2_aot"),
                        help="fsim mode; default is llvm_o2")
    parser.add_argument("--root", action="append", default=[], metavar="NAME=PATH",
                        help="override a manifest source root")
    parser.add_argument("--testbench-overlay", action="append", default=[],
                        metavar="CASE=PATH",
                        help="benchmark testbench copy for the named case")
    parser.add_argument("--prepare-reduced", action="store_true",
                        help="generate the selected reduced fixture copies")
    parser.add_argument("--prepare-full", action="store_true",
                        help="generate fixture copies for original reference workloads")
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--seed", type=lambda value: int(value, 0),
                        default=DEFAULT_SEED)
    parser.add_argument("--cpu", type=int,
                        help="single CPU for every benchmark subprocess")
    parser.add_argument("--vivado-settle-seconds", type=float, default=0.0,
                        help="required Vivado settle delay included in each total")
    parser.add_argument("--retain-workspaces", action="store_true",
                        help="keep successful per-sample artifact and native-cache trees")
    parser.add_argument("--preflight-only", action="store_true",
                        help="run parity and correctness preflight without timing")
    parser.add_argument("--reuse-preflight", type=Path,
                        help="reuse matching verified preflight evidence from a prior campaign")
    parser.add_argument("--profile", action="store_true",
                        help="run separate CPU-sampling and JIT-map diagnostics after preflight")
    parser.add_argument("--profile-llvm-modules", action="store_true",
                        help="include LLVM module summaries in the separate profile pass")
    parser.add_argument("--candidate-provenance",
                        help="explicit revision/build identity when --fsim is not the frozen baseline")
    parser.add_argument("--fsim-vhdl-compatibility", action="append", default=[],
                        metavar="CASE=PROFILE",
                        help="explicit fsim VHDL compatibility profile for a selected case")
    parser.add_argument("--list-cases", action="store_true")
    return parser.parse_args(argv)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CampaignError(f"cannot read manifest {path}: {error}") from error
    if result.get("schema_version") != 1:
        raise CampaignError("benchmark manifest schema_version must be 1")
    for field in ("roots", "source_sets", "cases", "configurations", "defaults"):
        if field not in result:
            raise CampaignError(f"benchmark manifest is missing {field}")
    return result


def parse_named_paths(values: list[str], label: str) -> dict[str, Path]:
    paths: dict[str, Path] = {}
    for value in values:
        name, equals, raw_path = value.partition("=")
        if not equals or not name or not raw_path:
            raise CampaignError(f"invalid {label} {value!r}; expected NAME=PATH")
        if name in paths:
            raise CampaignError(f"duplicate {label} for {name}")
        paths[name] = Path(raw_path).expanduser().resolve()
    return paths


def resolve_roots(manifest: dict[str, Any], manifest_path: Path,
                  overrides: dict[str, Path]) -> dict[str, Path]:
    unknown = sorted(set(overrides) - set(manifest["roots"]))
    if unknown:
        raise CampaignError("unknown root name(s): " + ", ".join(unknown))
    repository = manifest_path.resolve().parents[1]
    roots: dict[str, Path] = {}
    for name, entry in manifest["roots"].items():
        default = Path(entry["default"]).expanduser()
        if not default.is_absolute():
            default = repository / default
        roots[name] = overrides.get(name, default).resolve()
    return roots


def select_cases(manifest: dict[str, Any], requested: list[str] | None) -> list[dict[str, Any]]:
    by_id = {case["id"]: case for case in manifest["cases"]}
    names = requested if requested is not None else DEFAULT_CASES
    unknown = sorted(set(names) - set(by_id))
    if unknown:
        raise CampaignError("unknown case ID(s): " + ", ".join(unknown))
    result = [by_id[name] for name in names]
    inactive = [case["id"] for case in result if case.get("status", "active") != "active"]
    if inactive:
        raise CampaignError("selected cases are not active: " + ", ".join(inactive))
    return result


def select_configurations(manifest: dict[str, Any], requested: list[str] | None
                          ) -> list[dict[str, Any]]:
    standard = {item["id"]: item for item in manifest["configurations"]}
    configs = {
        "llvm_o2": {"id": "llvm_o2", "engine": "compiled", "optimization": "O2",
                    "aot": False},
        "llvm_o0": {"id": "llvm_o0", "engine": "compiled", "optimization": "O0",
                    "aot": False},
        "llvm_o2_aot": {"id": "llvm_o2_aot", "engine": "compiled",
                        "optimization": "O2", "aot": True},
    }
    for name in ("llvm_o2", "llvm_o0"):
        declared = standard.get(name)
        if declared is None or declared.get("engine") != "compiled":
            raise CampaignError(f"manifest does not declare compiled configuration {name}")
        if declared.get("optimization") != configs[name]["optimization"]:
            raise CampaignError(f"manifest configuration {name} has an unexpected optimization")
    names = requested or ["llvm_o2"]
    return [configs[name] for name in names]


def prepare_output(requested: Path | None) -> Path:
    if requested is None:
        stamp = time.strftime("%Y%m%dT%H%M%S", time.gmtime())
        directory = REPOSITORY / ".local-artifacts" / "perf-campaign" / f"run-{stamp}-{os.getpid()}"
    else:
        directory = requested.expanduser().resolve()
    if directory.exists() and any(directory.iterdir()):
        raise CampaignError(f"campaign output directory must be empty: {directory}")
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def stable_environment() -> tuple[dict[str, str], dict[str, str]]:
    removed = {key: value for key, value in os.environ.items()
               if key.startswith("FSIM_PROFILE_") or key == "FSIM_PERF_MAP"}
    environment = {key: value for key, value in os.environ.items()
                   if key not in removed}
    environment.update({"LANG": "C", "LC_ALL": "C", "TZ": "UTC"})
    return environment, removed


def cpu_available() -> set[int]:
    if not hasattr(os, "sched_getaffinity"):
        raise CampaignError("Linux CPU affinity support is required")
    return set(os.sched_getaffinity(0))


def require_cpu(cpu: int | None) -> int:
    available = cpu_available()
    chosen = min(available) if cpu is None else cpu
    if chosen not in available:
        raise CampaignError(f"CPU {chosen} is outside the current affinity {sorted(available)}")
    return chosen


def affinity_preexec(cpu: int):
    return lambda: os.sched_setaffinity(0, {cpu})


def fsim_native_cache_path(workspace: Path) -> Path:
    return workspace / ".fsim" / "cache" / "llvm-native"


def fsim_profile_environment(environment: dict[str, str],
                             profile_llvm_modules: bool) -> dict[str, str]:
    profile_environment = environment.copy()
    profile_environment.update({"FSIM_PROFILE_PHASES": "1", "FSIM_PROFILE_JIT": "1"})
    profile_environment.pop("FSIM_PROFILE_LLVM_MODULES", None)
    if profile_llvm_modules:
        profile_environment["FSIM_PROFILE_LLVM_MODULES"] = "1"
    return profile_environment


def native_cache_snapshot(cache_directory: Path) -> dict[str, Any]:
    # Include interrupted writes so a partial cache cannot pass as cold.
    object_directory = cache_directory / "llvm" / "objects"
    native_objects = sorted(
        path for path in object_directory.rglob("*.fobj*")
        if path.is_file()
    ) if object_directory.is_dir() else []
    object_manifest = hashlib.sha256()
    object_bytes = 0
    relative_objects = []
    for path in native_objects:
        relative_path = path.relative_to(cache_directory).as_posix()
        size = path.stat().st_size
        relative_objects.append(relative_path)
        object_bytes += size
        object_manifest.update(relative_path.encode("utf-8"))
        object_manifest.update(b"\0")
        object_manifest.update(str(size).encode("ascii"))
        object_manifest.update(b"\n")
    receipt_directory = cache_directory / "aot-receipts"
    receipts = sorted(
        path for path in receipt_directory.rglob("*")
        if path.is_file()
    ) if receipt_directory.is_dir() else []
    return {
        "path": str(cache_directory.resolve()),
        "exists": cache_directory.exists(),
        "native_object_count": len(native_objects),
        "native_object_bytes": object_bytes,
        "native_object_manifest_sha256": object_manifest.hexdigest(),
        "native_object_manifest_hash_basis": (
            "relative paths and byte sizes; object contents are not hashed"
        ),
        "native_object_examples": relative_objects[:8],
        "aot_receipt_count": len(receipts),
    }


def require_native_cache_empty(snapshot: dict[str, Any], boundary: str,
                               case_id: str) -> None:
    object_count = snapshot["native_object_count"]
    receipt_count = snapshot["aot_receipt_count"]
    if object_count or receipt_count:
        examples = ", ".join(snapshot["native_object_examples"])
        raise CampaignError(
            f"fresh fsim workspace for {case_id} has native objects or AOT receipts "
            f"{boundary}: {object_count} object(s), {receipt_count} receipt(s) "
            f"under {snapshot['path']}"
            + (f" ({examples})" if examples else "")
        )


def native_cache_run_record(cache_directory: Path, aot: bool,
                            before_elaboration: dict[str, Any],
                            before_simulation: dict[str, Any],
                            after_simulation: dict[str, Any],
                            elaborate_seconds: float) -> dict[str, Any]:
    return {
        "policy": "aot_precompiled" if aot else "jit_cold",
        "cache_directory": str(cache_directory.resolve()),
        "before_elaboration": before_elaboration,
        "before_simulation": before_simulation,
        "after_simulation": after_simulation,
        "jit_cold_before_simulation": (
            not aot
            and before_simulation["native_object_count"] == 0
            and before_simulation["aot_receipt_count"] == 0
        ),
        "aot_precompilation": {
            "enabled": aot,
            "elapsed_phase": "elaborate" if aot else None,
            "included_in_total": aot,
            "separately_attributed_seconds": None,
            "elaborate_phase_seconds": elaborate_seconds if aot else None,
            "native_object_count_before_simulation": (
                before_simulation["native_object_count"] if aot else None
            ),
            "receipt_count_before_simulation": (
                before_simulation["aot_receipt_count"] if aot else None
            ),
        },
    }


def run_capture(command: list[str], cwd: Path, environment: dict[str, str], cpu: int,
                timeout: float = COMMAND_TIMEOUT_SECONDS) -> subprocess.CompletedProcess[str]:
    try:
        process = subprocess.Popen(
            command, cwd=cwd, env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            start_new_session=True, preexec_fn=affinity_preexec(cpu))
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.communicate()
            raise
        return subprocess.CompletedProcess(command, process.returncode, output, None)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise CampaignError(f"cannot run {shlex.join(command)}: {error}") from error


def tool_identity(path: Path, environment: dict[str, str], cpu: int,
                  probe_dir: Path, version_args: list[str]) -> dict[str, Any]:
    resolved = path.resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise CampaignError(f"required executable is missing or not executable: {path}")
    probe_dir.mkdir(parents=True, exist_ok=True)
    version = run_capture([str(path), *version_args], probe_dir, environment, cpu,
                          timeout=120)
    if version.returncode != 0:
        raise CampaignError(f"version probe failed for {path}: {version.stdout[-2000:]}")
    dependencies: list[dict[str, str]] = []
    ldd_outputs: dict[str, str] = {}
    ldd = shutil.which("ldd")
    def add_runtime_dependencies(binary: Path, label: str) -> None:
        if not ldd or not binary.is_file():
            return
        dependency_environment = environment.copy()
        if "/Vivado/" in str(binary):
            vivado_root = Path(str(binary).split("/Vivado/", 1)[0] + "/Vivado")
            library_dirs = [vivado_root / "lib" / "lnx64.o",
                            vivado_root / "bin" / "unwrapped" / "lnx64.o"]
            existing = dependency_environment.get("LD_LIBRARY_PATH", "")
            dependency_environment["LD_LIBRARY_PATH"] = ":".join(
                [*(str(path) for path in library_dirs if path.is_dir()), existing]
            ).rstrip(":")
        dependency_result = subprocess.run([ldd, str(binary)], text=True,
                                           stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT,
                                           check=False, env=dependency_environment,
                                           preexec_fn=affinity_preexec(cpu))
        ldd_outputs[label] = dependency_result.stdout.strip()
        for line in dependency_result.stdout.splitlines():
            match = re.search(r"(\S+)\s+=>\s+(\S+)", line)
            if match:
                name, dependency_name = match.group(1), match.group(2)
            else:
                # ldd prints the ELF interpreter as an absolute path without
                # an arrow, but it is part of the frozen executable identity.
                loader = re.match(r"\s*(/\S+)\s+\(", line)
                if loader is None:
                    continue
                dependency_name = loader.group(1)
                name = Path(dependency_name).name
            if Path(dependency_name).is_file():
                dependency = Path(dependency_name).resolve()
                dependencies.append({"name": name, "path": str(dependency),
                                     "sha256": sha256(dependency)})
    add_runtime_dependencies(resolved, "launcher")
    first_line = resolved.read_bytes()[:256]
    interpreter = None
    if first_line.startswith(b"#!"):
        interpreter = first_line.splitlines()[0][2:].decode(
            "utf-8", "replace").strip().split()[0]
        interpreter_path = Path(interpreter)
        if interpreter_path.is_file():
            interpreter_path = interpreter_path.resolve()
            dependencies.append({"name": "script-interpreter", "path": str(interpreter_path),
                                 "sha256": sha256(interpreter_path)})
    wrapper_files: list[Path] = []
    if resolved.parent.name == "bin" and interpreter is not None:
        wrapper_files = [resolved.parent / "setupEnv.sh", resolved.parent / "loader"]
        unwrapped = resolved.parent / "unwrapped" / "lnx64.o" / resolved.name
        if unwrapped.is_file():
            wrapper_files.append(unwrapped)
            add_runtime_dependencies(unwrapped.resolve(), "unwrapped_binary")
    wrapper_identities = []
    for wrapper_file in wrapper_files:
        if wrapper_file.is_file():
            wrapper_identities.append({"path": str(wrapper_file.resolve()),
                                       "sha256": sha256(wrapper_file)})
    return {
        "path": str(path.resolve()), "sha256": sha256(resolved),
        "version": version.stdout.strip(), "dependencies": dependencies,
        "ldd_output": ldd_outputs,
        "shebang_interpreter": interpreter,
        "wrapper_files": wrapper_identities,
    }


def vivado_tools(bin_dir: Path) -> dict[str, Path]:
    result = {name: (bin_dir / name).resolve()
              for name in ("xvlog", "xvhdl", "xelab", "xsim")}
    missing = [str(path) for path in result.values() if not path.is_file()]
    if missing:
        raise CampaignError("Vivado simulator tools missing: " + ", ".join(missing))
    return result


def configuration_identity(manifest: dict[str, Any], manifest_path: Path,
                           cases: list[dict[str, Any]],
                           configs: list[dict[str, Any]], roots: dict[str, Path],
                           fsim: Path, vivado: dict[str, Path], output: Path,
                           environment: dict[str, str], cpu: int, seed: int,
                           baseline_commit: str) -> dict[str, Any]:
    version_root = output / "tool-probes"
    binaries = {
        "fsim": tool_identity(fsim, environment, cpu, version_root / "fsim", ["--version"]),
    }
    for name, tool in vivado.items():
        binaries[name] = tool_identity(tool, environment, cpu, version_root / name,
                                       ["-version"])
    if not TIME_PROGRAM.is_file():
        raise CampaignError(f"GNU time is required at {TIME_PROGRAM}")
    time_identity = tool_identity(TIME_PROGRAM, environment, cpu,
                                  version_root / "gnu-time", ["--version"])
    auxiliary_tools = {"gnu_time": time_identity}
    perf_program = shutil.which("perf")
    if perf_program:
        perf_path = Path(perf_program).resolve()
        auxiliary_tools["perf"] = {"path": str(perf_path), "sha256": sha256(perf_path)}
    git = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPOSITORY,
                         text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         check=False)
    status = subprocess.run(["git", "status", "--short"], cwd=REPOSITORY,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                            check=False)
    cpu_model = "unknown"
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                cpu_model = line.partition(":")[2].strip()
                break
    except OSError:
        pass
    return {
        "baseline_commit": baseline_commit,
        "repository_head_at_run": git.stdout.strip(),
        "working_tree_status": status.stdout.splitlines(),
        "manifest_path": str(manifest_path.resolve()),
        "manifest_sha256": sha256(manifest_path),
        "manifest_declared_baseline_commit": manifest.get("baseline_commit"),
        "baseline_executable": str(fsim.resolve()),
        "baseline_binary": binaries["fsim"],
        "simulator_binaries": binaries,
        "auxiliary_tools": auxiliary_tools,
        "source_roots": {name: str(path) for name, path in roots.items()},
        "cases": [case["id"] for case in cases],
        "configurations": configs,
        "system": {
            "platform": platform.platform(), "machine": platform.machine(),
            "cpu_model": cpu_model, "cpu_affinity": [cpu],
            "python": sys.version.split()[0],
            "execution_context": execution_context_identity(),
        },
        "benchmark_configuration": {
            "cpu_count": 1, "cpu": cpu, "waveforms": False,
            "interactive_debugging": False, "fsim_jobs": 1,
            "vivado_xelab_multithreading": "off", "environment": {
                key: value for key, value in environment.items()
                if key in ("LANG", "LC_ALL", "TZ", "LD_LIBRARY_PATH")
                or key.startswith("FSIM_")
            },
            "vivado_environment": {
                key: value for key, value in environment.items()
                if key in ("LANG", "LC_ALL", "TZ", "LD_LIBRARY_PATH")
                or key.startswith("FSIM_")
            },
        },
        "frozen_build_identity": frozen_build_identity(fsim, binaries["fsim"]),
        "stimulus": {
            "algorithm": XORSHIFT32_VERSION,
            "root_seed": f"0x{seed:08x}",
            "seed": f"0x{seed:08x}",
        },
    }


def frozen_build_identity(fsim: Path,
                          fsim_identity: dict[str, Any] | None = None) -> dict[str, Any]:
    identity_path = fsim.parent / "identity.json"
    if not identity_path.is_file():
        return {"verified": False, "reason": "no adjacent identity.json"}
    try:
        identity = json.loads(identity_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CampaignError(f"invalid frozen fsim identity {identity_path}: {error}") from error
    executable = identity.get("executable", {})
    if not executable.get("path") or not executable.get("sha256"):
        raise CampaignError("frozen fsim identity has no executable path and SHA-256")
    if Path(executable["path"]).resolve() != fsim.resolve():
        raise CampaignError("frozen identity executable path does not match --fsim")
    if executable["sha256"] != sha256(fsim):
        raise CampaignError("frozen identity executable hash does not match --fsim")
    dependency_field = "linked_dependencies"
    dependencies = identity.get(dependency_field)
    if dependency_field not in identity:
        dependency_field = "executable.dependencies"
        dependencies = executable.get("dependencies")
    if not isinstance(dependencies, list) or not dependencies:
        raise CampaignError("frozen fsim identity requires a nonempty dependency list")
    expected_dependencies = {}
    for item in dependencies:
        if (not isinstance(item, dict)
                or not isinstance(item.get("path"), str) or not item["path"]
                or not isinstance(item.get("sha256"), str)
                or re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) is None):
            raise CampaignError("frozen fsim dependency has no valid path and SHA-256")
        path = str(Path(item["path"]).resolve())
        if path in expected_dependencies:
            raise CampaignError("frozen fsim dependency manifest contains a duplicate path")
        expected_dependencies[path] = item["sha256"]
    if fsim_identity is None:
        raise CampaignError("frozen fsim verification requires observed dependency identities")
    actual_dependencies = {
        str(Path(item["path"]).resolve()): item["sha256"]
        for item in fsim_identity.get("dependencies", [])
    }
    if expected_dependencies != actual_dependencies:
        raise CampaignError("frozen fsim dependency identities do not match its manifest")
    return {
        "verified": True, "path": str(identity_path.resolve()),
        "sha256": sha256(identity_path), "baseline_commit": identity.get("baseline_commit"),
        "identity": identity, "dependencies_verified": True,
        "dependency_count": len(expected_dependencies), "dependency_field": dependency_field,
    }


def case_source_groups(case: dict[str, Any], manifest: dict[str, Any], roots: dict[str, Path],
                       overlay: Path | None, input_directory: Path) -> list[dict[str, Any]]:
    root = roots[case["root"]]
    groups = []
    overlay_replaced = False
    for group_index, group in enumerate(case["source_groups"], 1):
        names: list[str] = []
        source_set = group.get("source_set")
        if source_set:
            if source_set not in manifest["source_sets"]:
                raise CampaignError(f"{case['id']} uses unknown source set {source_set}")
            names.extend(manifest["source_sets"][source_set])
        names.extend(group.get("sources", []))
        sources: list[Path] = []
        original_tb_dirs: list[Path] = []
        for name in names:
            if name.startswith("$generated/"):
                generated = input_directory / name.removeprefix("$generated/")
                if name == "$generated/wrapper.sv" and case.get("wrapper"):
                    generated.parent.mkdir(parents=True, exist_ok=True)
                    generated.write_text(case["wrapper"], encoding="utf-8")
                source = generated.resolve()
            else:
                source = (root / name).resolve()
                if name.startswith("tb/") and overlay is not None:
                    if overlay.name != Path(name).name:
                        raise CampaignError(
                            f"overlay {overlay} must retain source basename {Path(name).name}"
                        )
                    source = overlay
                    original_tb_dirs.append((root / "tb").resolve())
                    overlay_replaced = True
            if not source.is_file():
                raise CampaignError(f"missing source input for {case['id']}: {source}")
            sources.append(source)
        include_dirs = [(root / name).resolve() for name in group.get("include_dirs", [])]
        include_dirs.extend(original_tb_dirs)
        groups.append({
            "index": group_index, "language": group["language"],
            "standard": group.get("standard"), "sources": sources,
            "includes": list(dict.fromkeys(include_dirs)),
            "defines": list(group.get("defines", [])),
        })
    if overlay is not None and not overlay_replaced:
        raise CampaignError(f"overlay for {case['id']} did not replace a manifest tb/ source")
    return groups


def find_testbench_source(case: dict[str, Any], manifest: dict[str, Any], root: Path) -> Path:
    for group in case["source_groups"]:
        names: list[str] = []
        if "source_set" in group:
            names.extend(manifest["source_sets"][group["source_set"]])
        names.extend(group.get("sources", []))
        for name in names:
            if name.startswith("tb/"):
                path = (root / name).resolve()
                if path.is_file():
                    return path
    raise CampaignError(f"case {case['id']} has no resolvable tb/ source")


def prepare_fixtures(cases: list[dict[str, Any]], manifest: dict[str, Any],
                     roots: dict[str, Path], output: Path, seed: int,
                     environment: dict[str, str], cpu: int,
                     workload: str) -> tuple[dict[str, Path], dict[str, Any]]:
    scripts = {
        "original_codec": "perf_codec_fixture.py",
        "mixed_codec": "perf_codec_fixture.py",
        "original_throughput": "perf_throughput_fixture.py",
        "mixed_throughput": "perf_throughput_fixture.py",
        "codex_reference_mode0_frames1": "perf_codex_fixture.py",
    }
    if workload == "full":
        scripts.update({
            "codex_reference_mode0_frames2": "perf_codex_fixture.py",
            "codex_reference_mode1_frames1": "perf_codex_fixture.py",
            "codex_reference_mode1_frames2": "perf_codex_fixture.py",
            "codex_throughput_default": "perf_codex_throughput_fixture.py",
            "codex_throughput_direct_syndrome": "perf_codex_throughput_fixture.py",
        })
    elif workload != "reduced":
        raise CampaignError(f"unknown fixture workload {workload}")
    overlays: dict[str, Path] = {}
    metadata_records: dict[str, Any] = {}
    for case in cases:
        name = case["id"]
        if name not in scripts:
            raise CampaignError(f"--prepare-{workload} has no generator for {name}")
        source = find_testbench_source(case, manifest, roots[case["root"]])
        directory = output / "fixtures" / name
        directory.mkdir(parents=True, exist_ok=False)
        generator = Path(__file__).with_name(scripts[name]).resolve()
        if not generator.is_file():
            raise CampaignError(f"reduced fixture generator is missing: {generator}")
        command = [sys.executable, str(generator), "--source", str(source),
                   "--output-dir", str(directory), "--seed", f"0x{seed:08x}"]
        if name in {"original_codec", "mixed_codec"} or (workload == "full" and
                name.startswith("codex_")):
            command.extend(["--case", name])
        if workload == "full":
            command.extend(["--workload", "full"])
        completed = run_capture(command, directory, environment, cpu, timeout=300)
        (directory / "generator.log").write_text(completed.stdout, encoding="utf-8")
        if completed.returncode != 0:
            raise CampaignError(f"fixture generator failed for {name}: {completed.stdout[-3000:]}")
        testbench = directory / source.name
        if not testbench.is_file():
            candidates = [path for path in directory.iterdir()
                          if path.is_file() and path.suffix.lower() in {".v", ".sv", ".vh"}]
            if len(candidates) != 1:
                raise CampaignError(f"generator did not emit expected testbench {source.name}")
            testbench = candidates[0]
            if testbench.name != source.name:
                renamed = directory / source.name
                testbench.rename(renamed)
                testbench = renamed
        json_candidates = [path for path in directory.glob("*.json")]
        metadata_path = next((path for path in json_candidates
                              if path.name in {"metadata.json", "fixture_manifest.json",
                                               f"{source.name}.json", f"{source.stem}.json"}), None)
        if metadata_path is None:
            raise CampaignError(f"generator did not emit fixture metadata for {name}")
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise CampaignError(f"invalid fixture metadata {metadata_path}: {error}") from error
        overlays[name] = testbench.resolve()
        metadata_records[name] = {
            "generator": str(generator), "generator_sha256": sha256(generator),
            "source": str(source), "source_sha256": sha256(source),
            "testbench": str(testbench.resolve()), "testbench_sha256": sha256(testbench),
            "metadata_path": str(metadata_path.resolve()) if metadata_path else None,
            "metadata_sha256": sha256(metadata_path) if metadata_path else None,
            "metadata": metadata,
            "seed": f"0x{seed:08x}",
            "algorithm": metadata.get("random", {}).get("algorithm",
                          metadata.get("algorithm_version", XORSHIFT32_VERSION)),
        }
    return overlays, metadata_records


def resolve_includes(source: Path, include_dirs: list[Path]) -> list[Path]:
    result: list[Path] = []
    visited = {source.resolve()}
    pending = [source.resolve()]
    while pending:
        current = pending.pop()
        try:
            content = current.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            raise CampaignError(f"cannot read source input {current}: {error}") from error
        for include in INCLUDE_PATTERN.findall(content):
            candidates = [current.parent / include,
                          *(directory / include for directory in include_dirs)]
            resolved = next((candidate.resolve() for candidate in candidates
                             if candidate.is_file()), None)
            if resolved is None:
                raise CampaignError(f"cannot resolve `include {include!r} from {current}")
            if resolved not in visited:
                visited.add(resolved)
                result.append(resolved)
                pending.append(resolved)
    return result


def collect_case_inputs(cases: list[dict[str, Any]], manifest: dict[str, Any],
                        roots: dict[str, Path], overlays: dict[str, Path],
                        output: Path) -> dict[str, Any]:
    collected: dict[str, Any] = {}
    for case in cases:
        groups = case_source_groups(case, manifest, roots, overlays.get(case["id"]),
                                   output / "inputs" / case["id"])
        identities: dict[str, dict[str, str]] = {}
        group_inputs = []
        for group in groups:
            includes: list[Path] = []
            for source in group["sources"]:
                for include in resolve_includes(source, group["includes"]):
                    if include not in includes:
                        includes.append(include)
            for source in [*group["sources"], *includes]:
                identities[str(source)] = {"path": str(source), "sha256": sha256(source)}
            group_inputs.append({
                "language": group["language"], "standard": group["standard"],
                "sources": [str(path) for path in group["sources"]],
                "source_hashes": [sha256(path) for path in group["sources"]],
                "include_dirs": [str(path) for path in group["includes"]],
                "include_inputs": [{"path": str(path), "sha256": sha256(path)}
                                   for path in includes],
            })
        collected[case["id"]] = {"groups": group_inputs,
                                 "all_inputs": list(identities.values())}
    return collected


def invoke_timed(command: list[str], cwd: Path, environment: dict[str, str], cpu: int,
                 stem: Path, timeout: float = COMMAND_TIMEOUT_SECONDS) -> dict[str, Any]:
    stem.parent.mkdir(parents=True, exist_ok=True)
    stdout_path = stem.with_suffix(".stdout")
    stderr_path = stem.with_suffix(".stderr")
    time_path = stem.with_suffix(".time")
    timed_command = [str(TIME_PROGRAM), "-f",
                     "FSIM_CAMPAIGN_RSS_KIB=%M "
                     "USER_SECONDS=%U SYSTEM_SECONDS=%S ELAPSED_SECONDS=%e "
                     "VOLUNTARY_CONTEXT_SWITCHES=%w "
                     "INVOLUNTARY_CONTEXT_SWITCHES=%c",
                     "-o", str(time_path), "--", *command]
    started = time.perf_counter()
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        process = subprocess.Popen(timed_command, cwd=cwd, env=environment,
                                   stdout=stdout, stderr=stderr, start_new_session=True,
                                   preexec_fn=affinity_preexec(cpu))
        try:
            return_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise CampaignError(f"command timed out after {timeout:g}s: {shlex.join(command)}") from error
    elapsed = time.perf_counter() - started
    try:
        rss_text = time_path.read_text(encoding="utf-8").strip()
        rss_match = re.search(r"FSIM_CAMPAIGN_RSS_KIB=(\d+)", rss_text)
        if rss_match is None:
            raise CampaignError(f"GNU time produced no peak RSS value: {time_path}")
        peak_rss_kib = int(rss_match.group(1))
    except OSError as error:
        raise CampaignError(f"GNU time did not write peak RSS at {time_path}: {error}") from error
    if return_code != 0:
        output = (stdout_path.read_text(encoding="utf-8", errors="replace")
                  + stderr_path.read_text(encoding="utf-8", errors="replace"))
        raise CampaignError(
            f"command failed ({return_code}): {shlex.join(command)}\n{output[-4000:]}"
        )
    return {
        "command": command, "timed_command": timed_command, "cwd": str(cwd),
        "returncode": return_code, "elapsed_seconds": elapsed,
        "peak_rss_kib": peak_rss_kib,
        "stdout": str(stdout_path), "stderr": str(stderr_path),
    }


def fsim_phase_commands(fsim: Path, case: dict[str, Any], config: dict[str, Any],
                        groups: list[dict[str, Any]],
                        seed: int, max_deltas: int,
                        plusarg: str | None = None) -> tuple[list[list[str]], list[str], list[str]]:
    compile_commands: list[list[str]] = []
    for group in groups:
        command = [str(fsim), "compile", "--jobs", "1", "--lang", group["language"]]
        if group["standard"]:
            command.extend(["--standard", str(group["standard"])])
        if group["language"].lower() == "vhdl" and case.get("fsim_vhdl_compatibility"):
            command.extend(["--vhdl-compatibility", case["fsim_vhdl_compatibility"]])
        command.extend(["--library", "work", "--compilation-unit", "source-set"])
        for include in group["includes"]:
            command.extend(["--include", str(include)])
        for define in group["defines"]:
            command.extend(["--define", define])
        command.extend(str(source) for source in group["sources"])
        compile_commands.append(command)
    elaborate = [str(fsim), "elaborate", "--jobs", "1", "--optimization",
                 config["optimization"], "--seed", str(seed)]
    if config["aot"]:
        elaborate.extend(["--aot", "--aot-scope", "all"])
    else:
        elaborate.append("--no-aot")
    elaborate.append(case["top"])
    simulate = [str(fsim), "simulate", "--engine", "compiled",
                "--optimization", config["optimization"], "--seed", str(seed),
                "--max-deltas", str(max_deltas)]
    if config["aot"]:
        simulate.extend(["--compiled-processes", "all"])
    if plusarg is not None:
        simulate.append("+" + plusarg.lstrip("+"))
    return compile_commands, elaborate, simulate


def vivado_phase_commands(vivado: dict[str, Path], case: dict[str, Any],
                          groups: list[dict[str, Any]], snapshot: str,
                          plusarg: str | None = None) -> tuple[list[list[str]], list[str], list[str]]:
    compile_commands: list[list[str]] = []
    for group in groups:
        language = group["language"].lower()
        if language == "vhdl":
            standard = str(group["standard"] or "2008")
            command = [str(vivado["xvhdl"]), f"-{standard}", "-work", "work"]
        else:
            command = [str(vivado["xvlog"])]
            if language in {"systemverilog", "system_verilog"}:
                command.append("--sv")
            for include in group["includes"]:
                command.extend(["-i", str(include)])
            for define in group["defines"]:
                command.extend(["-d", define])
            command.extend(["-work", "work"])
        if language == "vhdl":
            for include in group["includes"]:
                command.extend(["-i", str(include)])
        command.extend(["-log", f"compile-{group['index']}.log"])
        command.extend(str(source) for source in group["sources"])
        compile_commands.append(command)
    top = case["top"].split(":", 1)[-1]
    elaborate = [str(vivado["xelab"]), "-mt", "off", "-debug", "off",
                 top, "-s", snapshot, "-log", "elaborate.log"]
    simulate = [str(vivado["xsim"]), snapshot, "-R", "-log", "simulate.log"]
    if plusarg is not None:
        simulate.extend(["-testplusarg", plusarg.lstrip("+")])
    return compile_commands, elaborate, simulate


def run_engine(engine: str, fsim: Path, vivado: dict[str, Path], case: dict[str, Any],
               config: dict[str, Any], manifest: dict[str, Any], roots: dict[str, Path],
               overlay: Path | None, input_directory: Path, directory: Path,
               environment: dict[str, str], cpu: int, seed: int,
               preflight: bool = False, settle_seconds: float = 0.0) -> dict[str, Any]:
    directory.mkdir(parents=True, exist_ok=False)
    workspace = directory / "workspace"
    workspace.mkdir()
    cache = fsim_native_cache_path(workspace) if engine == "fsim" else None
    groups = case_source_groups(case, manifest, roots, overlay, input_directory)
    if engine == "fsim":
        compile_commands, elaborate_command, simulate_command = fsim_phase_commands(
            fsim, case, config, groups, seed,
            int(manifest["defaults"].get("max_deltas", 100_000_000)),
            "PERF_PREFLIGHT" if preflight else None,
        )
    elif engine == "vivado":
        compile_commands, elaborate_command, simulate_command = vivado_phase_commands(
            vivado, case, groups, case["id"],
            "PERF_PREFLIGHT" if preflight else None,
        )
    else:
        raise CampaignError(f"unknown simulator {engine}")

    wall_started = time.perf_counter()
    phase_records: dict[str, Any] = {"compile": []}
    for index, command in enumerate(compile_commands, 1):
        phase_records["compile"].append(invoke_timed(
            command, workspace, environment, cpu,
            directory / "phases" / f"compile-{index:02d}"))
    cache_before_elaboration = None
    if engine == "fsim":
        cache_before_elaboration = native_cache_snapshot(cache)
        require_native_cache_empty(
            cache_before_elaboration, "before elaboration", case["id"])
    phase_records["elaborate"] = invoke_timed(
        elaborate_command, workspace, environment, cpu,
        directory / "phases" / "elaborate")
    cache_before_simulation = None
    if engine == "fsim":
        cache_before_simulation = native_cache_snapshot(cache)
        if not config["aot"]:
            require_native_cache_empty(
                cache_before_simulation, "before JIT simulation", case["id"])
    launch_delay = 0.0
    if engine == "vivado" and not preflight and settle_seconds > 0:
        delay_started = time.perf_counter()
        time.sleep(settle_seconds)
        launch_delay = time.perf_counter() - delay_started
    simulation_record = invoke_timed(
        simulate_command, workspace, environment, cpu,
        directory / "phases" / "simulate")
    elapsed_total_seconds = time.perf_counter() - wall_started
    cache_after_simulation = (
        native_cache_snapshot(cache) if engine == "fsim" else None
    )
    phase_records["simulate"] = simulation_record
    stdout = "\n".join(
        Path(record["stdout"]).read_text(encoding="utf-8", errors="replace")
        for record in [*phase_records["compile"], phase_records["elaborate"], simulation_record]
    )
    stderr = "\n".join(
        Path(record["stderr"]).read_text(encoding="utf-8", errors="replace")
        for record in [*phase_records["compile"], phase_records["elaborate"], simulation_record]
    )
    if ERROR_PATTERN.search(stdout + "\n" + stderr):
        raise CampaignError(f"{engine} emitted ERROR/FATAL for {case['id']}; inspect {directory}")
    compile_seconds = sum(item["elapsed_seconds"] for item in phase_records["compile"])
    elaborate_seconds = phase_records["elaborate"]["elapsed_seconds"]
    simulate_seconds = simulation_record["elapsed_seconds"]
    peak_rss = [item["peak_rss_kib"] for item in phase_records["compile"]
                if item.get("peak_rss_kib") is not None]
    peak_rss.extend(item["peak_rss_kib"] for item in
                    (phase_records["elaborate"], simulation_record)
                    if item.get("peak_rss_kib") is not None)
    result = {
        "engine": engine, "case": case["id"], "configuration": config["id"],
        "preflight": preflight,
        "workspace": str(workspace),
        "native_cache": str(cache) if cache is not None else None,
        "native_cache_state": (
            native_cache_run_record(
                cache, config["aot"], cache_before_elaboration,
                cache_before_simulation, cache_after_simulation,
                phase_records["elaborate"]["elapsed_seconds"])
            if engine == "fsim" else None
        ),
        "phases": phase_records,
        "phase_seconds": {
            "compile": compile_seconds, "elaborate": elaborate_seconds,
            "native_setup_and_simulation": simulate_seconds,
            "vivado_settle_delay": launch_delay,
            "phase_sum": compile_seconds + elaborate_seconds + launch_delay + simulate_seconds,
        },
        "elapsed_seconds": elapsed_total_seconds,
        "peak_rss_kib": max(peak_rss) if peak_rss else None,
        "stdout": str(Path(simulation_record["stdout"]).resolve()),
        "stderr": str(Path(simulation_record["stderr"]).resolve()),
        "combined_output": stdout + "\n" + stderr,
    }
    return result


def parse_fields(line: str) -> dict[str, str]:
    return {key.lower(): value.strip('"') for key, value in ASSIGNMENT.findall(line)}


def first_field(fields: dict[str, str], names: tuple[str, ...], default: str | None = None
                ) -> str | None:
    for name in names:
        if name in fields:
            return fields[name]
    return default


def normalize_kind(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", value.lower()).strip("_")


def event_category(kind: str) -> str:
    if kind in {"backpressure", "ready_window"}:
        return "ready"
    if kind in {"accepted", "accepted_encoder", "accepted_decoder",
                "input_accept", "accepted_input", "input_accepted", "accept"}:
        return "accepted"
    return kind


def has_unknown_logic(fields: dict[str, str]) -> bool:
    value_keys = {
        "data", "source", "mask", "value", "valid", "ready", "last", "user",
        "magnitude", "position", "payload", "corruption", "fields",
    }
    for key, value in fields.items():
        if key in value_keys and re.search(r"[xz]", value, re.IGNORECASE):
            return True
    return False


def event_identity(line: str, index: int) -> tuple[str, str, str, str, dict[str, str]]:
    match = STIM_LINE.fullmatch(line)
    if match is None or match.group(1) != "STIM_EVENT":
        raise CampaignError(f"internal transcript parser error for: {line}")
    remainder = match.group(2).strip()
    tokens = shlex.split(remainder)
    fields = parse_fields(remainder)
    positional = next((token for token in tokens if "=" not in token), "")
    raw_kind = first_field(fields, ("kind", "purpose", "stream", "event"), positional)
    if not raw_kind:
        raise CampaignError(f"STIM_EVENT lacks event kind: {line}")
    kind = normalize_kind(raw_kind)
    instance = first_field(fields, ("instance", "logical_instance", "inst", "instance_id"), "0")
    scenario = first_field(fields, ("scenario", "tag", "case"), None)
    if scenario is None:
        scenario = first_field(fields, ("frame", "frame_index"), "default")
    sequence = first_field(fields, ("sequence", "seq", "index", "cycle", "symbol",
                                    "symbol_index", "ordinal", "position"), None)
    if sequence is None:
        raise CampaignError(f"STIM_EVENT lacks a sequence, index, cycle, or symbol: {line}")
    if has_unknown_logic(fields):
        raise CampaignError(f"unknown X/Z stimulus value in transcript: {line}")
    stream = (str(instance), str(scenario), kind)
    return stream[0], stream[1], stream[2], str(sequence), fields


def canonical_transcript(output: str, preflight: bool,
                         required_categories: set[str] | None = None) -> dict[str, Any]:
    if "STIM_INVALID" in output or "STIM_KAT_FAIL" in output:
        raise CampaignError("stimulus transcript contains an invalid-sample or KAT failure marker")
    raw = [line.strip() for line in output.splitlines()
           if (match := STIM_LINE.fullmatch(line)) is not None]
    events: dict[tuple[str, str, str], list[tuple[int, str]]] = {}
    summaries: list[tuple[tuple[str, ...], str]] = []
    kats: list[str] = []
    category_set: set[str] = set()
    codec_scenarios: set[str] = set()
    for index, line in enumerate(raw):
        kind_match = STIM_LINE.fullmatch(line)
        assert kind_match is not None
        record_kind, remainder = kind_match.group(1), kind_match.group(2).strip()
        fields = parse_fields(remainder)
        if record_kind == "STIM_EVENT":
            instance, scenario, purpose, raw_sequence, fields = event_identity(line, index)
            if purpose == "invalid" or fields.get("invalid") == "1":
                raise CampaignError(f"STIM_EVENT contains invalid stimulus data: {line}")
            try:
                sequence = int(raw_sequence, 0)
            except ValueError:
                try:
                    sequence = int(raw_sequence, 10)
                except ValueError as error:
                    raise CampaignError(f"non-numeric STIM_EVENT sequence in {line}") from error
            stream_key = (instance, scenario, purpose)
            events.setdefault(stream_key, []).append((sequence, line))
            category_set.add(event_category(purpose))
            codec_scenarios.add(scenario)
        elif record_kind == "STIM_SUMMARY":
            instance = first_field(fields, ("instance", "logical_instance", "inst"))
            scenario = first_field(fields, ("scenario", "tag"))
            if instance is None or scenario is None:
                raise CampaignError(f"STIM_SUMMARY lacks instance or scenario identity: {line}")
            summary_key = (instance, scenario, first_field(fields, ("case",), "") or "")
            summaries.append((summary_key, line))
        else:
            kats.append(line)
    if preflight:
        if not events:
            raise CampaignError("preflight output has no STIM_EVENT records")
        if not summaries:
            raise CampaignError("preflight output has no STIM_SUMMARY records")
        if len(kats) != 1:
            raise CampaignError(f"preflight must contain exactly one STIM_KAT record, found {len(kats)}")
        kat_remainder = STIM_LINE.fullmatch(kats[0]).group(2).strip()
        kat_fields = parse_fields(kat_remainder)
        if "seed" in kat_fields and kat_fields["seed"].lower() != "0x6d2b79f5":
            raise CampaignError("STIM_KAT seed does not match 0x6d2b79f5")
        if "algorithm" in kat_fields and kat_fields["algorithm"] != XORSHIFT32_VERSION:
            raise CampaignError("STIM_KAT algorithm version is unexpected")
        if "words" in kat_fields:
            kat_words = [word.lower().removeprefix("0x")
                         for word in kat_fields["words"].split(",")]
        else:
            kat_words = re.findall(r"(?<![0-9a-f])[0-9a-f]{8}(?![0-9a-f])",
                                   kat_remainder.lower())
        if tuple(kat_words) != XORSHIFT32_KAT:
            raise CampaignError("STIM_KAT does not exactly match the required xorshift32 vector")
        expected_categories = (required_categories if required_categories is not None else
                               {"payload", "corruption", "ready", "accepted"})
        if not expected_categories.issubset(category_set):
            raise CampaignError("preflight lacks stimulus event categories: "
                                + ", ".join(sorted(expected_categories - category_set)))
    canonical_events: list[dict[str, Any]] = []
    for stream_key in sorted(events):
        records = events[stream_key]
        sequences = [sequence for sequence, _ in records]
        if any(right <= left for left, right in zip(sequences, sequences[1:])):
            raise CampaignError(f"STIM_EVENT stream sequence is not strictly increasing: {stream_key}")
        canonical_events.append({
            "instance": stream_key[0], "scenario": stream_key[1],
            "purpose": stream_key[2], "records": [line for _, line in records],
        })
    summary_identities = [key for key, _ in summaries]
    if len(set(summary_identities)) != len(summary_identities):
        raise CampaignError("STIM_SUMMARY contains duplicate instance/scenario identities")
    canonical_summaries = [line for _, line in sorted(summaries, key=lambda item: item[0])]
    return {
        "raw_records": raw,
        "events": canonical_events,
        "summaries": canonical_summaries,
        "summary_identities": [list(key) for key in sorted(summary_identities)],
        "known_answer": kats,
        "known_answer_words": list(XORSHIFT32_KAT) if preflight else [],
        "event_categories": sorted(category_set),
        "scenario_ids": sorted(codec_scenarios),
    }


def required_correctness(case: dict[str, Any], has_overlay: bool,
                         fixture_metadata: dict[str, Any] | None = None) -> list[dict[str, Any]]:
    metadata = (fixture_metadata or {}).get("metadata", {})
    if has_overlay and case["id"] in {"original_codec", "mixed_codec"}:
        scenarios = metadata.get("scenarios", [])
        instances = metadata.get("instances", [])
        return [
            {"pattern": r"(?m)^ALL_CODEC_DONE\s*$", "count": 1},
            {"pattern": r"(?m)^CODEC_OK \[", "count": len(instances) or 1},
            {"pattern": r"(?m)^PASS \[", "count": len(scenarios)},
        ]
    if has_overlay and case["id"] in {"original_throughput", "mixed_throughput"}:
        instances = metadata.get("configuration", {}).get("instances", [])
        return [
            {"pattern": r"(?m)^THRU TESTS COMPLETE\s*$", "count": 1},
            {"pattern": r"(?m)^THRU \[.* PASS\s*$", "count": len(instances)},
        ]
    return case.get("correctness", {}).get("required", [])


def expected_summary_count(case: dict[str, Any],
                           fixture_metadata: dict[str, Any] | None = None) -> int:
    metadata = (fixture_metadata or {}).get("metadata", {})
    if case["id"] in {"original_codec", "mixed_codec"}:
        return len(metadata.get("scenarios", []))
    if case["id"] in {"original_throughput", "mixed_throughput"}:
        return len(metadata.get("configuration", {}).get("instances", []))
    return 1


def validate_summary(case: dict[str, Any], summary: str,
                     fixture_metadata: dict[str, Any] | None) -> None:
    fields = parse_fields(summary)
    required_fingerprints = {
        "payload": ("payload_count", ("payload_fp", "payload_fnv64", "payload_fingerprint")),
        "corruption": ("corruption_count", ("corruption_fp", "corruption_fnv64",
                                               "corruption_fingerprint")),
        "ready": (("ready_count", "ready_cycles"),
                  ("ready_fp", "ready_fnv64", "ready_fingerprint")),
        "accepted": (("accepted_count", "accepted_inputs", "accepted_input_count",
                       "encoder_accept_count", "decoder_accept_count"),
                     ("accepted_fp", "accepted_fnv64", "accepted_fingerprint")),
    }
    for category, (count_names, fingerprint_names) in required_fingerprints.items():
        counts = (count_names,) if isinstance(count_names, str) else count_names
        count_value = first_field(fields, counts)
        fingerprint_value = first_field(fields, fingerprint_names)
        if count_value is None or fingerprint_value is None:
            raise CampaignError(f"STIM_SUMMARY lacks {category} count/fingerprint: {summary}")
        if not re.fullmatch(r"(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{16})", fingerprint_value):
            raise CampaignError(f"STIM_SUMMARY has invalid {category} fingerprint: {summary}")
        try:
            if int(count_value, 0) < 0:
                raise ValueError("negative count")
        except ValueError as error:
            raise CampaignError(f"STIM_SUMMARY has invalid {category} count: {summary}") from error

    metadata = (fixture_metadata or {}).get("metadata", {})
    if case["id"].startswith("codex_"):
        expected_summary = metadata.get("expected_summary", {})
        expected_counts = expected_summary.get("counts", {})
        for category, field_name in (("payload", "payload_count"),
                                     ("corruption", "corruption_count"),
                                     ("accepted", "accepted_inputs")):
            expected = expected_counts.get(category)
            if expected is not None and (field_name not in fields
                                         or int(fields[field_name], 0) != int(expected)):
                raise CampaignError(
                    f"Codex summary expected {field_name}={expected}"
                )
    if case["id"].startswith("codex_reference_mode"):
        match = re.search(r"mode(\d+)_frames(\d+)$", case["id"])
        if match is None:
            raise CampaignError(f"unknown Codex reference workload: {case['id']}")
        for key, expected in (("mode", int(match.group(1))),
                              ("input_frames", int(match.group(2)))):
            if key not in fields or key not in metadata or int(fields[key], 0) != expected \
                    or int(metadata[key]) != expected:
                raise CampaignError(f"Codex summary expected {key}={expected}")
        return
    if case["id"] in {"codex_throughput_default",
                      "codex_throughput_direct_syndrome"}:
        configuration = metadata.get("configuration", {})
        correctness = metadata.get("correctness", {})
        for field_name, expected in (
            ("mode", configuration.get("mode")),
            ("frames", configuration.get("frames")),
            ("n", configuration.get("n")),
            ("k", configuration.get("k")),
            ("verify", configuration.get("verify")),
            ("erasures", configuration.get("erasures")),
            ("output_count", correctness.get("output_symbols")),
            ("output_frames", correctness.get("output_frames")),
            ("input_stalls", correctness.get("input_stalls")),
            ("output_bubbles", correctness.get("output_bubbles")),
            ("ready_zeros", 0),
        ):
            if expected is None or field_name not in fields \
                    or int(fields[field_name], 0) != int(expected):
                raise CampaignError(
                    f"Codex throughput summary expected {field_name}={expected}"
                )
        if ("ready_ones" not in fields or "ready_cycles" not in fields
                or int(fields["ready_ones"], 0) != int(fields["ready_cycles"], 0)):
            raise CampaignError("Codex throughput ready schedule is not constantly ready")
        return
    if case["id"] in {"original_throughput", "mixed_throughput"}:
        config = metadata.get("configuration", {})
        corruption = metadata.get("corruption", {})
        codewords = config.get("codewords_per_instance")
        data_symbols = config.get("k")
        encoded_symbols = config.get("n")
        if any(value is None for value in (codewords, data_symbols, encoded_symbols)):
            raise CampaignError("throughput fixture metadata lacks N/K/codeword configuration")
        expected = {
            "payload_count": int(data_symbols) * int(codewords),
            "corruption_count": int(corruption.get("count_per_codeword", 0)) * int(codewords),
            "accepted_count": int(encoded_symbols) * int(codewords),
            "output_count": int(encoded_symbols) * int(codewords),
            "invalid_samples": 0,
            "errs": 0,
            "ready_zeros": 0,
        }
        for key, value in expected.items():
            if key not in fields or int(fields[key], 0) != value:
                raise CampaignError(
                    f"throughput summary expected {key}={value}, got {fields.get(key)}"
                )
        if ("ready_ones" in fields and "ready_count" in fields
                and int(fields["ready_ones"], 0) != int(fields["ready_count"], 0)):
            raise CampaignError("throughput summary ready schedule is not constantly ready")
        return

    if case["id"] not in {"original_codec", "mixed_codec"}:
        return
    scenario_value = first_field(fields, ("scenario", "tag"))
    if scenario_value is None:
        raise CampaignError(f"codec STIM_SUMMARY lacks scenario tag: {summary}")
    scenario = int(scenario_value, 0)
    scenario_rows = metadata.get("scenarios", [])
    instance_value = first_field(fields, ("instance", "logical_instance", "inst"))
    row = next((candidate for candidate in scenario_rows
                if int(candidate.get("tag", -1)) == scenario
                and str(candidate.get("instance_id", 0)) == instance_value), None)
    if row is None:
        raise CampaignError(f"codec summary scenario {scenario} is absent from fixture metadata")
    expected_counts = row.get("expected_counts", {})
    for key, expected in expected_counts.items():
        if key not in fields or int(fields[key], 0) != int(expected):
            raise CampaignError(
                f"codec summary tag {scenario} expected {key}={expected}, got {fields.get(key)}"
            )
    for key in ("encode_timeout_count", "decode_timeout_count", "xz_count",
                "event_overflow_count"):
        if key not in fields or int(fields[key], 0) != 0:
            raise CampaignError(f"codec summary tag {scenario} has nonzero or missing {key}")


def verify_correctness(case: dict[str, Any], result: dict[str, Any], has_overlay: bool,
                       preflight: bool, fixture_metadata: dict[str, Any] | None = None,
                       expected_timed_summaries: list[str] | None = None,
                       expected_timed_correctness_lines: list[str] | None = None
                       ) -> dict[str, Any]:
    output = result["combined_output"]
    forbidden = case.get("correctness", {}).get("forbidden", [])
    correctness_output = "\n".join(
        line for line in output.splitlines() if STIM_LINE.fullmatch(line) is None
    )
    violations = [pattern for pattern in forbidden if re.search(pattern, correctness_output)]
    if violations:
        raise CampaignError(f"{result['engine']} {case['id']} matched forbidden correctness text {violations}")
    matches = []
    requirements = required_correctness(case, has_overlay, fixture_metadata)
    for requirement in requirements:
        found = re.findall(requirement["pattern"], output)
        expected = int(requirement["count"])
        if len(found) != expected:
            raise CampaignError(
                f"{result['engine']} {case['id']} expected {expected} matches for "
                f"{requirement['pattern']!r}, found {len(found)}"
            )
        matches.append({"pattern": requirement["pattern"], "count": len(found),
                        "matches": [str(item) for item in found]})
    required_categories = {"payload", "ready", "accepted"}
    if case["id"] not in {"codex_throughput_default",
                          "codex_throughput_direct_syndrome"}:
        required_categories.add("corruption")
    transcript = canonical_transcript(output, preflight, required_categories)
    correctness_lines = sorted({line.strip() for line in output.splitlines()
                                if any(re.search(requirement["pattern"], line)
                                       for requirement in requirements)})
    if preflight:
        if case["id"] in {"original_codec", "mixed_codec"}:
            metadata = (fixture_metadata or {}).get("metadata", {})
            scenarios = metadata.get("scenarios", [])
            expected_ids = {(str(row.get("instance_id", 0)), str(row["tag"]))
                            for row in scenarios}
            if len(expected_ids) != len(scenarios) or len(scenarios) not in {7, 75}:
                raise CampaignError("codec fixture metadata has incomplete scenario identities")
            summary_ids = {tuple(identity[:2])
                           for identity in transcript["summary_identities"]}
            if summary_ids != expected_ids:
                raise CampaignError("codec STIM_SUMMARY identities differ from fixture scenarios")
            if len(scenarios) == 7:
                tags = [int(value, 0) for value in re.findall(
                    r"(?m)^STIM_SUMMARY\b[^\n]*\bscenario=(\d+)", output)]
                if tags != EXPECTED_CODEC_SCENARIOS:
                    raise CampaignError("reduced codec scenario order differs from source")
                transcript["codec_tags"] = tags
            expected_tags = {str(row["tag"]) for row in scenarios}
            if set(transcript["scenario_ids"]) != expected_tags:
                raise CampaignError("codec preflight scenario tags differ from fixture")
        elif case["id"] in {"original_throughput", "mixed_throughput"}:
            metadata = (fixture_metadata or {}).get("metadata", {})
            configuration = metadata.get("configuration", {})
            instances = configuration.get("instances", [])
            expected_instances = {str(item["stream_id"]): (item["instance"],
                                                              int(item["num_kes"]))
                                  for item in instances
                                  if "stream_id" in item and "instance" in item
                                  and "num_kes" in item}
            full = len(expected_instances) == 6
            reduced_instances = {"0": ("u0", 1), "1": ("u1", 2),
                                 "2": ("u2", 4), "3": ("u3", 8)}
            full_instances = {**reduced_instances, "4": ("u4", 1), "5": ("u5", 2)}
            if expected_instances != (full_instances if full else reduced_instances):
                raise CampaignError("throughput metadata has unexpected instance identities")
            summary_ids = {tuple(identity[:2])
                           for identity in transcript["summary_identities"]}
            if summary_ids != {(instance, "0") for instance in expected_instances}:
                raise CampaignError("throughput summaries do not cover all expected instances")
            n = int(configuration.get("n", 0))
            thru_rows = re.findall(r"(?m)^THRU \[N(\d+)_m(\d+)_k(\d+)\].*\bPASS\s*$",
                                   output)
            if ({(int(row[0]), int(row[1]), int(row[2])) for row in thru_rows}
                    != {(n, int(item.get("mode", configuration.get("mode", -1))),
                         int(item["num_kes"]))
                        for item in instances}):
                raise CampaignError("throughput correctness results do not cover expected NUM_KES variants")
        else:
            identities = transcript["summary_identities"]
            if len(identities) != 1 or identities[0][:2] != ["0", "0"]:
                raise CampaignError("Codex preflight must contain exactly its one instance summary")
        if len(transcript["summaries"]) != expected_summary_count(case, fixture_metadata):
            raise CampaignError(
                f"{result['engine']} {case['id']} expected "
                f"{expected_summary_count(case, fixture_metadata)} summaries, "
                f"found {len(transcript['summaries'])}"
            )
        for summary in transcript["summaries"]:
            validate_summary(case, summary, fixture_metadata)
        if case["id"].startswith("codex_"):
            summary_fields = parse_fields(transcript["summaries"][0])
            event_counts: dict[str, int] = {}
            for stream in transcript["events"]:
                category = event_category(stream["purpose"])
                event_counts[category] = event_counts.get(category, 0) + len(stream["records"])
            for category, count_keys in {
                "payload": ("payload_count",),
                "corruption": ("corruption_count",),
                "ready": ("ready_cycles", "ready_count"),
                "accepted": ("accepted_inputs", "accepted_count"),
            }.items():
                value = first_field(summary_fields, count_keys)
                if value is None or int(value, 0) != event_counts.get(category, 0):
                    raise CampaignError(
                        f"Codex {category} summary count differs from stimulus events")
    else:
        if any((match := STIM_LINE.fullmatch(line)) is not None
               and match.group(1) == "STIM_EVENT" for line in output.splitlines()):
            raise CampaignError("timed output unexpectedly contains a full STIM_EVENT transcript")
        if any(line.startswith("STIM_KAT") for line in output.splitlines()):
            raise CampaignError("timed output unexpectedly contains STIM_KAT records")
        if len(transcript["summaries"]) != expected_summary_count(case, fixture_metadata):
            raise CampaignError(f"timed {result['engine']} {case['id']} has wrong summary count")
        for summary in transcript["summaries"]:
            validate_summary(case, summary, fixture_metadata)
        if expected_timed_summaries is not None and transcript["summaries"] != expected_timed_summaries:
            raise CampaignError(
                f"timed {result['engine']} summary fingerprints differ from validated preflight"
            )
        if (expected_timed_correctness_lines is not None
                and correctness_lines != expected_timed_correctness_lines):
            raise CampaignError(
                f"timed {result['engine']} correctness result lines differ from validated preflight"
            )
    result["correctness"] = matches
    result["stimulus"] = transcript
    result["correctness_lines"] = correctness_lines
    return transcript


def persist_transcript(directory: Path, result: dict[str, Any], transcript: dict[str, Any]) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "transcript.raw.txt").write_text(
        "\n".join(transcript["raw_records"]) + "\n", encoding="utf-8")
    serializable = {key: value for key, value in transcript.items() if key != "raw_records"}
    (directory / "transcript.canonical.json").write_text(
        json.dumps(serializable, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    result["transcript_raw"] = str((directory / "transcript.raw.txt").resolve())
    result["transcript_canonical"] = str((directory / "transcript.canonical.json").resolve())


def persist_engine_result(directory: Path, result: dict[str, Any],
                          validation_error: str | None = None) -> Path:
    stimulus = result.get("stimulus", {})
    record = {key: value for key, value in result.items()
              if key not in {"combined_output", "stimulus"}}
    record["stimulus"] = {
        "event_count": sum(len(stream["records"]) for stream in stimulus.get("events", [])),
        "summary_count": len(stimulus.get("summaries", [])),
        "summary_identities": stimulus.get("summary_identities", []),
        "summaries": stimulus.get("summaries", []),
        "known_answer": stimulus.get("known_answer", []),
        "event_categories": stimulus.get("event_categories", []),
    }
    if validation_error is not None:
        record["validation_error"] = validation_error
    path = directory / "engine_result.json"
    path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path.resolve()


def persist_pair_verdict(directory: Path, pair: dict[str, dict[str, Any]],
                         passed: bool, error: str | None = None) -> None:
    record = {
        "passed": passed,
        "error": error,
        "engine_results": {
            engine: result.get("engine_result_path", str(
                (Path(result["workspace"]).parent / "engine_result.json").resolve()))
            for engine, result in pair.items()
        },
        "stimulus": {
            engine: {
                "events": sum(len(stream["records"])
                               for stream in result.get("stimulus", {}).get("events", [])),
                "summaries": len(result.get("stimulus", {}).get("summaries", [])),
                "known_answer": result.get("stimulus", {}).get("known_answer", []),
            } for engine, result in pair.items()
        },
        "correctness_lines": {
            engine: result.get("correctness_lines", [])
            for engine, result in pair.items()
        },
    }
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "pair_verdict.json").write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def discard_successful_workspaces(results: list[dict[str, Any]], retain: bool) -> None:
    if retain:
        return
    for result in results:
        for key in ("workspace", "native_cache"):
            raw_path = result.get(key)
            if not raw_path:
                continue
            path = Path(raw_path)
            if path.exists():
                for root, directories, files in os.walk(path, topdown=False):
                    for name in files:
                        item = Path(root) / name
                        if not item.is_symlink():
                            item.chmod(item.stat().st_mode | stat.S_IWUSR)
                    for name in directories:
                        item = Path(root) / name
                        if not item.is_symlink():
                            item.chmod(item.stat().st_mode | stat.S_IWUSR | stat.S_IXUSR)
                    current = Path(root)
                    current.chmod(current.stat().st_mode | stat.S_IWUSR | stat.S_IXUSR)
                shutil.rmtree(path)
        result["workspaces_removed_after_success"] = True


def parity_payload(transcript: dict[str, Any]) -> dict[str, Any]:
    result = {
        "events": transcript["events"],
        "summaries": transcript["summaries"],
        "known_answer": transcript["known_answer"],
    }
    if "codec_tags" in transcript:
        result["codec_tags"] = transcript["codec_tags"]
    return result


def ensure_pair_parity(pair: dict[str, dict[str, Any]], case_id: str,
                       stage: str) -> None:
    if parity_payload(pair["fsim"]["stimulus"]) != parity_payload(pair["vivado"]["stimulus"]):
        raise CampaignError(f"{stage} stimulus parity mismatch for {case_id}")
    if pair["fsim"]["correctness_lines"] != pair["vivado"]["correctness_lines"]:
        raise CampaignError(f"{stage} correctness result lines differ for {case_id}")


def execution_context_identity() -> dict[str, Any]:
    """Keep cached preflights in the same host and Linux launch context."""
    context: dict[str, Any] = {
        "uid": os.getuid(), "gid": os.getgid(),
        "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text(encoding="utf-8").strip(),
    }
    context["namespaces"] = {
        name: os.readlink(Path("/proc/self/ns") / name)
        for name in ("ipc", "mnt", "net", "pid", "user", "uts")
    }
    status = Path("/proc/self/status").read_text(encoding="utf-8")
    context["launch_restrictions"] = {
        name: line.partition(":")[2].strip()
        for line in status.splitlines()
        if (name := line.partition(":")[0]) in ("NoNewPrivs", "Seccomp", "Seccomp_filters")
    }
    return context


def preflight_identity(engine: str, case: dict[str, Any], config: dict[str, Any],
                       identities: dict[str, Any], output: Path,
                       environment: dict[str, str]) -> dict[str, Any]:
    """Use ordered input roles; normalize only paths inside this campaign."""
    output = output.resolve()

    def logical_paths(value: Any) -> Any:
        if isinstance(value, dict):
            return {key: logical_paths(item) for key, item in value.items()}
        if isinstance(value, list):
            return [logical_paths(item) for item in value]
        if isinstance(value, str) and Path(value).is_absolute():
            try:
                relative = Path(value).relative_to(output)
                return "<campaign>/" + relative.as_posix()
            except ValueError:
                pass
        return value

    tool_names = ("fsim",) if engine == "fsim" else ("xvlog", "xvhdl", "xelab", "xsim")
    binaries = {
        name: {key: identities["simulator_binaries"][name].get(key)
               for key in ("path", "sha256", "dependencies", "wrapper_files",
                           "shebang_interpreter")}
        for name in tool_names
    }
    fixture = dict(identities["fixture_metadata"][case["id"]])
    # The metadata bytes embed the generated testbench's absolute path.
    # Compare their complete parsed contents below, after logical normalization.
    fixture.pop("metadata_sha256", None)
    environment_sha256 = hashlib.sha256(json.dumps(
        environment, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return logical_paths({
        "schema": "fsim-preflight-identity-v1", "engine": engine,
        "case": case, "configuration": config,
        "manifest_sha256": identities["manifest_sha256"],
        "binaries": binaries, "system": identities["system"],
        "benchmark_configuration": identities["benchmark_configuration"],
        "environment_sha256": environment_sha256,
        "inputs": identities["source_identity"][case["id"]],
        "fixture": fixture, "stimulus": identities["stimulus"],
        "campaign_code": identities["campaign_code"],
    })


def reuse_preflight_result(prior: Path, engine: str, case: dict[str, Any],
                           config: dict[str, Any], identity: dict[str, Any],
                           has_overlay: bool, fixture: dict[str, Any] | None
                           ) -> tuple[dict[str, Any] | None, str]:
    directory = prior / "preflight" / case["id"] / engine
    receipt_path = directory / "reuse-proof.json"
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        if receipt.get("schema") != "fsim-preflight-proof-v1":
            return None, "missing or unsupported proof version"
        if receipt.get("identity") != identity:
            return None, "preflight identity differs"
        for record in receipt["files"]:
            if sha256(Path(record["path"])) != record["sha256"]:
                return None, "saved evidence hash differs"
        report_path = prior / "campaign_report.json"
        if sha256(report_path) != receipt["campaign_report_sha256"]:
            return None, "prior campaign report changed"
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if (not report.get("passed_preflight")
                or not report["identities"]["post_run_identity_check"]["passed"]):
            return None, "prior campaign did not finish identity and parity checks"
        result = json.loads((directory / "engine_result.json").read_text(encoding="utf-8"))
        if (result.get("validation_error") or not result.get("preflight")
                or result.get("engine") != engine or result.get("case") != case["id"]
                or result.get("configuration") != config["id"]):
            return None, "saved engine result is not a matching successful preflight"
        phases = result["phases"]
        records = [*phases["compile"], phases["elaborate"], phases["simulate"]]
        if not phases["compile"] or any(item["returncode"] != 0 for item in records):
            return None, "saved execution has an incomplete or failed phase"
        required_paths = {str((directory / "engine_result.json").resolve()),
                          result["transcript_raw"], result["transcript_canonical"]}
        required_paths.update(item[stream] for item in records for stream in ("stdout", "stderr"))
        if required_paths != {item["path"] for item in receipt["files"]}:
            return None, "saved proof does not cover every execution and transcript file"
        stdout = "\n".join(Path(item["stdout"]).read_text(encoding="utf-8", errors="replace")
                           for item in records)
        stderr = "\n".join(Path(item["stderr"]).read_text(encoding="utf-8", errors="replace")
                           for item in records)
        result["combined_output"] = stdout + "\n" + stderr
        if ERROR_PATTERN.search(result["combined_output"]):
            return None, "saved execution contains ERROR/FATAL"
        transcript = verify_correctness(case, result, has_overlay, True, fixture)
        saved = json.loads(Path(result["transcript_canonical"]).read_text(encoding="utf-8"))
        if saved != {key: value for key, value in transcript.items() if key != "raw_records"}:
            return None, "reparsed canonical transcript differs"
        if Path(result["transcript_raw"]).read_text(encoding="utf-8") != "\n".join(transcript["raw_records"]) + "\n":
            return None, "reparsed raw transcript differs"
        result["preflight_reuse"] = {
            "source_proof": str(receipt_path.resolve()), "source_proof_sha256": sha256(receipt_path),
            "historical_elapsed_seconds": result.get("elapsed_seconds"),
            "historical_phase_seconds": result.get("phase_seconds"),
            "historical_peak_rss_kib": result.get("peak_rss_kib"),
            "phase_records_are_historical": True,
            "native_cache_state_is_historical": True,
        }
        result["elapsed_seconds"] = None
        result["phase_seconds"] = {}
        result["peak_rss_kib"] = None
        return result, "identity, evidence integrity, correctness and canonical transcript verified"
    except (OSError, ValueError, KeyError, TypeError, AttributeError, CampaignError) as error:
        return None, f"fresh preflight required: {type(error).__name__}: {error}"


def write_preflight_proofs(output: Path, report: dict[str, Any]) -> None:
    """Publish proofs only after final source/tool checks and the campaign report."""
    report_hash = sha256(output / "campaign_report.json")
    for result in report["preflight_results"]:
        directory = output / "preflight" / result["case"] / result["engine"]
        phases = result["phases"]
        records = [*phases["compile"], phases["elaborate"], phases["simulate"]]
        paths = {directory / "engine_result.json", Path(result["transcript_raw"]),
                 Path(result["transcript_canonical"])}
        paths.update(Path(item[stream]) for item in records for stream in ("stdout", "stderr"))
        proof = {
            "schema": "fsim-preflight-proof-v1", "identity": result["preflight_identity"],
            "campaign_report_sha256": report_hash,
            "files": [{"path": str(path.resolve()), "sha256": sha256(path)}
                      for path in sorted(paths)],
        }
        (directory / "reuse-proof.json").write_text(
            json.dumps(proof, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_preflight(cases: list[dict[str, Any]], config: dict[str, Any], fsim: Path,
                  vivado: dict[str, Path], manifest: dict[str, Any], roots: dict[str, Path],
                  overlays: dict[str, Path], output: Path,
                  environment: dict[str, str], cpu: int, seed: int,
                  fixture_metadata: dict[str, Any], retain_workspaces: bool,
                  identities: dict[str, Any] | None = None, reuse: Path | None = None
                  ) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    results = []
    validated: dict[str, dict[str, Any]] = {}
    for case in cases:
        pair: dict[str, dict[str, Any]] = {}
        for engine in ("fsim", "vivado"):
            print(f"preflight {case['id']}: {engine}", flush=True)
            directory = output / "preflight" / case["id"] / engine
            identity = (preflight_identity(engine, case, config, identities, output, environment)
                        if identities else None)
            result = None
            reason = "reuse not requested"
            if reuse is not None and identity is not None:
                result, reason = reuse_preflight_result(
                    reuse, engine, case, config, identity, case["id"] in overlays,
                    fixture_metadata.get(case["id"]))
                print(f"preflight {case['id']}: {engine} reuse {'PASS' if result else 'MISS'}: {reason}", flush=True)
            if result is None:
                result = run_engine(
                    engine, fsim, vivado, case, config, manifest, roots,
                    overlays.get(case["id"]), output / "inputs" / case["id"],
                    directory, environment, cpu, seed, preflight=True,
                )
            if identity is not None:
                result["preflight_identity"] = identity
            result["preflight_reuse_decision"] = reason
            try:
                transcript = verify_correctness(case, result,
                                                case["id"] in overlays, preflight=True,
                                                fixture_metadata=fixture_metadata.get(case["id"]))
            except CampaignError as error:
                persist_engine_result(directory, result, str(error))
                raise
            persist_transcript(directory, result, transcript)
            result["engine_result_path"] = str((directory / "engine_result.json").resolve())
            persist_engine_result(directory, result)
            pair[engine] = result
            results.append({key: value for key, value in result.items()
                            if key != "combined_output"})
        pair_directory = output / "preflight" / case["id"]
        try:
            ensure_pair_parity(pair, case["id"], "preflight")
        except CampaignError as error:
            persist_pair_verdict(pair_directory, pair, False, str(error))
            raise
        persist_pair_verdict(pair_directory, pair, True)
        validated[case["id"]] = {
            engine: {
                "summaries": result["stimulus"]["summaries"],
                "correctness_lines": result["correctness_lines"],
            } for engine, result in pair.items()
        }
        discard_successful_workspaces(
            [result for result in pair.values() if "preflight_reuse" not in result],
            retain_workspaces)
        print(f"preflight {case['id']}: stimulus parity PASS", flush=True)
    return results, validated


def run_samples(cases: list[dict[str, Any]], configs: list[dict[str, Any]], fsim: Path,
                vivado: dict[str, Path], manifest: dict[str, Any], roots: dict[str, Path],
                overlays: dict[str, Path], output: Path, environment: dict[str, str],
                cpu: int, seed: int, samples: int, settle_seconds: float,
                fixture_metadata: dict[str, Any],
                expected_preflight: dict[str, dict[str, Any]],
                retain_workspaces: bool) -> list[dict[str, Any]]:
    results = []
    for case in cases:
        for config in configs:
            for sample in range(1, samples + 1):
                order = ("fsim", "vivado") if sample % 2 else ("vivado", "fsim")
                paired: dict[str, dict[str, Any]] = {}
                for engine in order:
                    print(f"timing {case['id']} {config['id']} sample {sample}/{samples}: {engine}",
                          flush=True)
                    directory = (output / "timings" / case["id"] / config["id"]
                                 / f"sample-{sample:02d}" / engine)
                    result = run_engine(
                        engine, fsim, vivado, case, config, manifest, roots,
                        overlays.get(case["id"]), output / "inputs" / case["id"],
                        directory, environment, cpu, seed,
                        settle_seconds=settle_seconds,
                    )
                    try:
                        transcript = verify_correctness(
                            case, result, case["id"] in overlays, preflight=False,
                            fixture_metadata=fixture_metadata.get(case["id"]),
                            expected_timed_summaries=(
                                expected_preflight[case["id"]][engine]["summaries"]),
                            expected_timed_correctness_lines=(
                                expected_preflight[case["id"]][engine]["correctness_lines"]))
                    except CampaignError as error:
                        persist_engine_result(directory, result, str(error))
                        raise
                    persist_transcript(directory, result, transcript)
                    persist_engine_result(directory, result)
                    paired[engine] = result
                pair_directory = (output / "timings" / case["id"] / config["id"]
                                  / f"sample-{sample:02d}")
                try:
                    ensure_pair_parity(
                        paired, f"{case['id']} {config['id']} sample {sample}", "timed")
                except CampaignError as error:
                    persist_pair_verdict(pair_directory, paired, False, str(error))
                    raise
                persist_pair_verdict(pair_directory, paired, True)
                for engine, result in paired.items():
                    results.append({
                        "case": case["id"], "configuration": config["id"],
                        "sample": sample, "engine": engine,
                        "elapsed_seconds": result["elapsed_seconds"],
                        "phase_seconds": result["phase_seconds"],
                        "peak_rss_kib": result["peak_rss_kib"],
                        "correctness": result["correctness"],
                        "stimulus": result["stimulus"],
                        "workspace": result["workspace"],
                        "native_cache": result["native_cache"],
                        "native_cache_state": result["native_cache_state"],
                        "phases": result["phases"],
                    })
                discard_successful_workspaces(list(paired.values()), retain_workspaces)
    return results


def run_fsim_profile(case: dict[str, Any], config: dict[str, Any], fsim: Path,
                     manifest: dict[str, Any], roots: dict[str, Path],
                     overlay: Path, input_directory: Path, directory: Path,
                     environment: dict[str, str], cpu: int, seed: int,
                     fixture_metadata: dict[str, Any],
                     expected_preflight: dict[str, Any], retain_workspaces: bool,
                     profile_llvm_modules: bool = False
                     ) -> dict[str, Any]:
    try:
        from perf_campaign_profile import SamplingError, sample_command
    except ImportError as error:
        raise CampaignError(f"cannot import CPU sampling helper: {error}") from error

    directory.mkdir(parents=True, exist_ok=False)
    workspace = directory / "workspace"
    workspace.mkdir()
    cache = fsim_native_cache_path(workspace)
    groups = case_source_groups(case, manifest, roots, overlay, input_directory)
    compile_commands, elaborate_command, simulate_command = fsim_phase_commands(
        fsim, case, config, groups, seed,
        int(manifest["defaults"].get("max_deltas", 100_000_000)))
    profile_environment = fsim_profile_environment(environment, profile_llvm_modules)

    def sample_phase(command: list[str], stem: Path) -> dict[str, Any]:
        try:
            record = sample_command(command, workspace, profile_environment, cpu, stem)
        except SamplingError as error:
            raise CampaignError(f"fsim profile failed for {case['id']}: {error}") from error
        stdout = Path(record["stdout"]).read_text(encoding="utf-8", errors="replace")
        stderr = Path(record["stderr"]).read_text(encoding="utf-8", errors="replace")
        if ERROR_PATTERN.search(stdout + "\n" + stderr):
            raise CampaignError(f"profiled fsim setup phase emitted ERROR/FATAL for {case['id']}")
        phase_record_path = stem.with_suffix(".result.json")
        phase_record_path.write_text(
            json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        record["record_path"] = str(phase_record_path.resolve())
        return record

    phases: dict[str, Any] = {"compile": []}
    for index, command in enumerate(compile_commands, 1):
        phases["compile"].append(sample_phase(
            command, directory / "profiles" / f"compile-{index:02d}"))
    cache_before_elaboration = native_cache_snapshot(cache)
    require_native_cache_empty(
        cache_before_elaboration, "before elaboration", case["id"])
    phases["elaborate"] = sample_phase(
        elaborate_command, directory / "profiles" / "elaborate")
    cache_before_simulation = native_cache_snapshot(cache)
    require_native_cache_empty(
        cache_before_simulation, "before JIT simulation", case["id"])
    sampled = sample_phase(simulate_command, directory / "profiles" / "simulate")
    cache_after_simulation = native_cache_snapshot(cache)
    stdout = Path(sampled["stdout"]).read_text(encoding="utf-8", errors="replace")
    stderr = Path(sampled["stderr"]).read_text(encoding="utf-8", errors="replace")
    if ERROR_PATTERN.search(stdout + "\n" + stderr):
        raise CampaignError(f"profiled fsim simulation emitted ERROR/FATAL for {case['id']}")
    result = {
        "engine": "fsim", "case": case["id"], "configuration": config["id"],
        "combined_output": stdout + "\n" + stderr,
    }
    transcript = verify_correctness(
        case, result, True, False, fixture_metadata=fixture_metadata,
        expected_timed_summaries=expected_preflight["fsim"]["summaries"],
        expected_timed_correctness_lines=expected_preflight["fsim"]["correctness_lines"])
    persist_transcript(directory / "profile", result, transcript)
    phase_line = re.search(
        r"(?m)^FSIM-PROFILE setup_ms=([0-9.eE+-]+) run_ms=([0-9.eE+-]+) "
        r"native_await_ms=([0-9.eE+-]+)$", stdout)
    record = {
        "case": case["id"], "configuration": config["id"],
        "instrumented": True,
        "performance_evidence": False,
        "llvm_module_profile_enabled": profile_llvm_modules,
        "workspace": str(workspace),
        "native_cache": str(cache.resolve()),
        "native_cache_state": native_cache_run_record(
            cache, False, cache_before_elaboration, cache_before_simulation,
            cache_after_simulation, phases["elaborate"]["elapsed_seconds"]),
        "compile_and_elaborate_phases": phases,
        "sampled_simulation": sampled,
        "internal_profile_phase_ms": ({
            "setup": float(phase_line.group(1)), "run": float(phase_line.group(2)),
            "native_await": float(phase_line.group(3)),
        } if phase_line else None),
        "transcript_canonical": result["transcript_canonical"],
        "interpretation": (
            "diagnostic only; native materialization may overlap run time, and profile "
            "instrumentation changes scheduling and wall time"
        ),
    }
    profile_record_path = directory / "profile_result.json"
    profile_record_path.write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    record["record_path"] = str(profile_record_path.resolve())
    discard_successful_workspaces([{
        "workspace": str(workspace), "native_cache": str(cache),
    }], retain_workspaces)
    return record


def run_profiles(cases: list[dict[str, Any]], fsim: Path,
                 manifest: dict[str, Any], roots: dict[str, Path],
                 overlays: dict[str, Path], output: Path,
                 environment: dict[str, str], cpu: int, seed: int,
                 fixture_metadata: dict[str, Any],
                 expected_preflight: dict[str, dict[str, Any]],
                 retain_workspaces: bool,
                 profile_llvm_modules: bool = False) -> list[dict[str, Any]]:
    config = {"id": "llvm_o2", "engine": "compiled", "optimization": "O2",
              "aot": False}
    results = []
    for case in cases:
        print(f"profile {case['id']}: fsim llvm_o2", flush=True)
        results.append(run_fsim_profile(
            case, config, fsim, manifest, roots, overlays[case["id"]],
            output / "inputs" / case["id"], output / "profiles" / case["id"] / "fsim",
            environment, cpu, seed, fixture_metadata[case["id"]],
            expected_preflight[case["id"]], retain_workspaces, profile_llvm_modules))
    return results


def summarize_samples(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    keys = sorted({(row["case"], row["configuration"]) for row in results})
    summaries = []
    for case, config in keys:
        by_engine = {
            engine: [row for row in results if row["case"] == case
                     and row["configuration"] == config and row["engine"] == engine]
            for engine in ("fsim", "vivado")
        }
        engine_summary = {}
        for engine, rows in by_engine.items():
            times = [row["elapsed_seconds"] for row in rows]
            rss = [row["peak_rss_kib"] for row in rows if row["peak_rss_kib"] is not None]
            engine_summary[engine] = {
                "samples": len(rows), "median_seconds": statistics.median(times),
                "min_seconds": min(times), "max_seconds": max(times),
                "median_peak_rss_kib": statistics.median(rss) if rss else None,
            }
        fsim_median = engine_summary["fsim"]["median_seconds"]
        vivado_median = engine_summary["vivado"]["median_seconds"]
        paired_times: dict[str, dict[int, float]] = {"fsim": {}, "vivado": {}}
        for row in results:
            if row["case"] == case and row["configuration"] == config:
                paired_times[row["engine"]][row["sample"]] = row["elapsed_seconds"]
        ratios = [paired_times["vivado"][sample] / paired_times["fsim"][sample]
                  for sample in sorted(set(paired_times["fsim"])
                                       & set(paired_times["vivado"]))]
        # Deterministic paired bootstrap; the interval is a noise check, not a
        # substitute for the required seven interleaved observations.
        random_seed = int(hashlib.sha256(f"{case}|{config}".encode()).hexdigest()[:16], 16)
        randomizer = random.Random(random_seed)
        bootstrap_medians = []
        if len(ratios) >= 2:
            for _ in range(10_000):
                bootstrap_medians.append(statistics.median(
                    randomizer.choice(ratios) for _ in range(len(ratios))))
            bootstrap_medians.sort()
            low = bootstrap_medians[int(0.025 * (len(bootstrap_medians) - 1))]
            high = bootstrap_medians[int(0.975 * (len(bootstrap_medians) - 1))]
            confidence_interval = [low, high]
        else:
            confidence_interval = None
        summaries.append({
            "case": case, "configuration": config,
            "engines": engine_summary,
            "fsim_speedup_vs_vivado": vivado_median / fsim_median,
            "fsim_median_below_vivado": fsim_median < vivado_median,
            "paired_speedup_95pct_bootstrap_ci": confidence_interval,
            "noise_clear": confidence_interval is not None and low > 1.0,
        })
    return summaries


def completion_scope(complete_reference_matrix: bool,
                    complete_original_workloads: bool) -> str:
    if complete_reference_matrix and complete_original_workloads:
        return "all_ten_reference_cases_original_workloads"
    if complete_reference_matrix:
        return "ten_case_diagnostic_reduced_inputs"
    return "diagnostic_subset"


def extract_seed_records(value: Any, path: str = "$",
                        stream_seed_context: bool = False) -> list[dict[str, str]]:
    records: list[dict[str, str]] = []
    if isinstance(value, dict):
        for key, item in value.items():
            child_path = f"{path}.{key}"
            is_stream_seed_map = "stream_seed" in key.lower()
            if (("seed" in key.lower() or stream_seed_context) and
                    isinstance(item, (str, int, float))):
                records.append({"path": child_path, "value": str(item)})
            records.extend(extract_seed_records(
                item, child_path, stream_seed_context or is_stream_seed_map))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            records.extend(extract_seed_records(
                item, f"{path}[{index}]", stream_seed_context))
    return records


def load_overlay_metadata(cases: list[dict[str, Any]], overlays: dict[str, Path],
                          prepared: dict[str, Any]) -> dict[str, Any]:
    metadata = dict(prepared)
    for case in cases:
        case_id = case["id"]
        if case_id not in overlays or case_id in metadata:
            continue
        directory = overlays[case_id].parent
        metadata_path = next((directory / name for name in
                              ("fixture_manifest.json", "metadata.json")
                              if (directory / name).is_file()), None)
        if metadata_path is not None:
            try:
                document = json.loads(metadata_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as error:
                raise CampaignError(f"invalid overlay metadata {metadata_path}: {error}") from error
            metadata[case_id] = {
                "metadata_path": str(metadata_path.resolve()),
                "metadata_sha256": sha256(metadata_path),
                "metadata": document,
                "source_sha256": (document.get("source", {}).get("sha256")
                                  if isinstance(document.get("source"), dict)
                                  else document.get("source_sha256")),
                "source_path": (document.get("source", {}).get("path")
                                if isinstance(document.get("source"), dict)
                                else document.get("source")),
                "testbench": str(overlays[case_id]),
                "testbench_sha256": sha256(overlays[case_id]),
                "seed": document.get("base_seed", document.get("seed")),
                "algorithm": document.get("algorithm_version", document.get("algorithm")),
            }
        else:
            raise CampaignError(f"benchmark overlay requires adjacent fixture metadata: {overlays[case_id]}")
    return metadata


def fixture_generator_path(case_id: str) -> Path:
    if case_id in {"original_codec", "mixed_codec"}:
        script = "perf_codec_fixture.py"
    elif case_id in {"original_throughput", "mixed_throughput"}:
        script = "perf_throughput_fixture.py"
    elif case_id in {"codex_throughput_default",
                     "codex_throughput_direct_syndrome"}:
        script = "perf_codex_throughput_fixture.py"
    elif case_id.startswith("codex_"):
        script = "perf_codex_fixture.py"
    else:
        raise CampaignError(f"no deterministic fixture generator is registered for {case_id}")
    path = Path(__file__).with_name(script).resolve()
    if not path.is_file():
        raise CampaignError(f"fixture generator is missing: {path}")
    return path


def metadata_source_info(metadata: dict[str, Any]) -> tuple[str | None, str | None]:
    source = metadata.get("source")
    if isinstance(source, dict):
        return source.get("path"), source.get("sha256")
    return source if isinstance(source, str) else None, metadata.get("source_sha256")


def metadata_root_seed(metadata: dict[str, Any]) -> Any:
    configuration = metadata.get("configuration", {})
    return metadata.get("base_seed", configuration.get("root_seed", metadata.get("seed")))


def metadata_algorithm(metadata: dict[str, Any]) -> Any:
    return metadata.get("algorithm_version",
                        metadata.get("random", {}).get("algorithm", metadata.get("algorithm")))


def validate_fixture_metadata(cases: list[dict[str, Any]], overlays: dict[str, Path],
                              fixture_metadata: dict[str, Any], manifest: dict[str, Any],
                              roots: dict[str, Path], seed: int) -> None:
    for case in cases:
        case_id = case["id"]
        overlay = overlays.get(case_id)
        record = fixture_metadata.get(case_id)
        if overlay is None or record is None:
            raise CampaignError(f"case {case_id} requires a benchmark testbench overlay and metadata")
        metadata = record.get("metadata", {})
        metadata_path = Path(record["metadata_path"])
        if not metadata_path.is_file():
            raise CampaignError(f"fixture metadata is missing: {metadata_path}")
        if sha256(metadata_path) != record.get("metadata_sha256"):
            raise CampaignError(f"fixture metadata hash does not match for {case_id}")
        if sha256(overlay) != record.get("testbench_sha256"):
            raise CampaignError(f"fixture testbench hash does not match metadata for {case_id}")
        generated = metadata.get("generated_testbench", {})
        expected_tb_hash = (generated.get("sha256") if isinstance(generated, dict)
                            else metadata.get("generated_testbench_sha256"))
        if expected_tb_hash is None:
            expected_tb_hash = metadata.get("generated_testbench_sha256")
        if expected_tb_hash and expected_tb_hash != sha256(overlay):
            raise CampaignError(f"fixture metadata does not identify the supplied testbench for {case_id}")
        source_path_text, source_hash = metadata_source_info(metadata)
        original_source = find_testbench_source(case, manifest, roots[case["root"]])
        if source_hash is None or source_hash != sha256(original_source):
            raise CampaignError(f"fixture metadata source hash does not match {case_id} manifest source")
        if source_path_text:
            source_path = Path(source_path_text).expanduser().resolve()
            if not source_path.is_file() or sha256(source_path) != source_hash:
                raise CampaignError(f"fixture source identity is unavailable or changed for {case_id}")
        seed_value = metadata_root_seed(metadata)
        try:
            metadata_seed = int(seed_value, 0) if isinstance(seed_value, str) else int(seed_value)
        except (TypeError, ValueError) as error:
            raise CampaignError(f"fixture metadata has no usable root seed for {case_id}") from error
        if metadata_seed != seed:
            raise CampaignError(f"fixture metadata seed differs from campaign seed for {case_id}")
        if metadata_algorithm(metadata) != XORSHIFT32_VERSION:
            raise CampaignError(f"fixture metadata algorithm differs from campaign version for {case_id}")
        generator_path_text = record.get("generator") or metadata.get("generator_path")
        generator_hash = record.get("generator_sha256") or metadata.get("generator_sha256")
        if generator_path_text is None:
            generator = fixture_generator_path(case_id)
            generator_path_text = str(generator)
            generator_hash = sha256(generator)
            record["generator"] = generator_path_text
            record["generator_sha256"] = generator_hash
        generator_path = Path(generator_path_text).expanduser().resolve()
        if not generator_path.is_file() or generator_hash != sha256(generator_path):
            raise CampaignError(f"fixture generator identity is unavailable or changed for {case_id}")
        record["source_path"] = str(original_source.resolve())
        record["source_sha256"] = source_hash
        record["generator"] = str(generator_path)
        record["generator_sha256"] = generator_hash


def original_workload_identity(case: dict[str, Any], fixture_record: dict[str, Any],
                               original_source: Path) -> dict[str, Any]:
    metadata = fixture_record.get("metadata", {})
    marker = " ".join(str(metadata.get(key, "")) for key in
                      ("profile", "fixture", "workload_profile")).lower()
    reasons: list[str] = []
    if metadata.get("reduced") is True or "reduced" in marker or "two_codeword" in marker:
        reasons.append("fixture metadata identifies a reduced workload")
    source_text = original_source.read_text(encoding="utf-8", errors="replace")
    overlay_path = Path(fixture_record.get("testbench", ""))
    overlay_text = (overlay_path.read_text(encoding="utf-8", errors="replace")
                    if overlay_path.is_file() else "")

    def parameter_value(text: str, name: str) -> int | None:
        match = re.search(
            rf"(?im)\b(?:localparam|parameter)\s+(?:integer|int|bit)\s+"
            rf"{re.escape(name)}\s*=\s*([01]'b[01]|\d+)", text)
        if match is None:
            return None
        value = match.group(1)
        return int(value[-1]) if "'b" in value else int(value)

    def required_shape(name: str, metadata_value: Any,
                       expected_value: int | None = None) -> None:
        source_value = parameter_value(source_text, name)
        overlay_value = parameter_value(overlay_text, name)
        target = source_value if expected_value is None else expected_value
        if target is None or overlay_value is None or metadata_value is None:
            reasons.append(f"missing {name} workload-shape evidence")
            return
        try:
            declared = int(metadata_value)
        except (TypeError, ValueError):
            reasons.append(f"invalid {name} workload-shape metadata")
            return
        allowed_overlay_values = ({source_value} if expected_value is None
                                  else {source_value, target})
        if overlay_value not in allowed_overlay_values or declared != target:
            reasons.append(f"{name} differs from the original workload")

    if case["id"] in {"original_codec", "mixed_codec"}:
        design = metadata.get("design") or next(iter(metadata.get("instances", [])), {})
        for name, key in (("N", "n"), ("K", "k")):
            required_shape(name, design.get(key))
        if metadata.get("workload_profile") == "full-11-instance":
            if (parameter_value(source_text, "IMPL_MODE") is None
                    or parameter_value(overlay_text, "IMPL_MODE")
                    != parameter_value(source_text, "IMPL_MODE")):
                reasons.append("codec module default mode differs from source")
            instance_rows = metadata.get("instances", [])
            scenario_rows = metadata.get("scenarios", [])
            expected_ids = {str(index) for index in range(11)}
            checker_pattern = re.compile(
                r"codec_check\s*#\(([^;]*?)\)\s+d(\d+)\s*\(", re.DOTALL)
            parameter_pattern = re.compile(r"\.(\w+)\(\s*([^)]*?)\s*\)")
            parameter_names = {
                "N": "n", "K": "k", "FCR": "fcr",
                "PRIM_POLY": "primitive_poly_parameter",
                "PRIM_POW": "primitive_power",
                "IMPL_MODE": "implementation_mode",
                "ERASURE": "erasure_decoding",
                "CWS": "codeword_shortening",
                "PS": "parity_shortening",
            }

            def checker_shapes(text: str) -> list[tuple[str, dict[str, int]]]:
                shapes = []
                for match in checker_pattern.finditer(text):
                    values = {}
                    for name, spelling in parameter_pattern.findall(match.group(1)):
                        if name not in parameter_names:
                            continue
                        normalized = spelling.strip().lower().replace("_", "")
                        try:
                            values[name] = (int(normalized.split("'h", 1)[1], 16)
                                            if "'h" in normalized else int(normalized, 10))
                        except ValueError:
                            reasons.append(f"codec checker d{match.group(2)} has nonliteral {name}")
                    shapes.append((match.group(2), values))
                return shapes

            source_shapes = checker_shapes(source_text)
            overlay_shapes = checker_shapes(overlay_text)
            source_ids = [identity for identity, _ in source_shapes]
            declared_ids = {str(row.get("instance_id")) for row in instance_rows}
            if (len(source_ids) != 11 or source_shapes != overlay_shapes
                    or set(source_ids) != expected_ids or declared_ids != expected_ids):
                reasons.append("codec checker configurations differ from original")
            declared_shapes = {
                str(row.get("instance_id")): row for row in instance_rows
            }
            for identity, shape in source_shapes:
                row = declared_shapes.get(identity)
                if row is None or any(
                    name not in shape or shape[name] != int(row.get(key, -1))
                    for name, key in parameter_names.items()
                ):
                    reasons.append(f"codec checker d{identity} metadata differs from source")
            expected_tags = {
                index: ({0, 1, 2, 5} if index == 2 else
                        {0, 1, 2, 3, 4, 5, 6, 7} if index == 5 else
                        {0, 1, 2, 3, 4, 5, 7})
                for index in range(11)
            }
            declared_tags = {
                index: {int(row["tag"]) for row in scenario_rows
                        if int(row.get("instance_id", -1)) == index}
                for index in range(11)
            }
            if (len(scenario_rows) != 75 or declared_tags != expected_tags):
                reasons.append("codec scenario set differs from original 75 scenarios")
            totals = {
                key: sum(int(row.get("expected_counts", {}).get(key, 0))
                         for row in scenario_rows)
                for key in ("payload_count", "decoder_accept_count",
                            "corruption_count", "encoder_accept_count")
            }
            if totals != {"payload_count": 9151, "decoder_accept_count": 10751,
                          "corruption_count": 614, "encoder_accept_count": 9151}:
                reasons.append("codec expected transaction totals differ from original")
        else:
            required_shape("IMPL_MODE", design.get("implementation_mode"))
    elif case["id"] in {"original_throughput", "mixed_throughput"}:
        configuration = metadata.get("configuration", {})
        for name, key in (("N", "n"), ("K", "k"),
                          ("NCW", "codewords_per_instance")):
            required_shape(name, configuration.get(key))
        if configuration.get("mode") == "per_instance":
            pattern = re.compile(
                r"rs_thru\s*#\(\s*\.N\((\d+)\),\s*\.K\((\d+)\),"
                r"\s*\.IMPL_MODE\((\d+)\),\s*\.NUM_KES\((\d+)\)"
                r"[^;]*?\)\s*(u\d+)\s*\(", re.DOTALL)
            source_instances = [tuple(match.groups()) for match in pattern.finditer(source_text)]
            overlay_instances = [tuple(match.groups()) for match in pattern.finditer(overlay_text)]
            declared_instances = [
                (str(configuration.get("n")), str(configuration.get("k")),
                 str(item.get("mode")), str(item.get("num_kes")), str(item.get("instance")))
                for item in configuration.get("instances", [])
            ]
            if (len(source_instances) != 6 or overlay_instances != source_instances
                    or declared_instances != source_instances):
                reasons.append("throughput instance mode/pool configuration differs from source")
            required_shape("NM", len(declared_instances))
        else:
            required_shape("IMPL_MODE", configuration.get("mode"))
    elif case["id"].startswith("codex_reference_mode"):
        match = re.search(r"mode(\d+)_frames(\d+)$", case["id"])
        if match is None:
            reasons.append("Codex reference case identifier is unrecognized")
        else:
            required_shape("MODE", metadata.get("mode"), int(match.group(1)))
            required_shape("INPUT_FRAMES", metadata.get("input_frames"), int(match.group(2)))
        for name, key in (("N", "n"), ("K", "k"), ("NUM_KES", "num_kes")):
            required_shape(name, metadata.get(key))
    elif case["id"] in {"codex_throughput_default",
                        "codex_throughput_direct_syndrome"}:
        configuration = metadata.get("configuration", metadata)
        for name, key in (("N", "n"), ("K", "k"), ("FRAMES", "frames"),
                          ("MODE", "mode"), ("NUM_KES", "num_kes")):
            required_shape(name, configuration.get(key))
        for name, key in (("VERIFY", "verify"), ("ERASURES", "erasures"),
                          ("FIXED_FEATURES", "fixed_features"),
                          ("INJECT_ERRORS", "inject_errors")):
            override = re.search(rf"\.{name}\(\s*(\d+)\s*\)", case.get("wrapper", ""))
            expected = int(override.group(1)) if override else parameter_value(source_text, name)
            required_shape(name, configuration.get(key), expected)
    else:
        reasons.append("no original-workload shape validator for case")
    return {"verified": not reasons, "reasons": reasons,
            "source_path": str(original_source.resolve()),
            "source_sha256": sha256(original_source)}


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    evidence: Path | None = None
    try:
        manifest_path = arguments.manifest.expanduser().resolve()
        manifest = load_manifest(manifest_path)
        if arguments.list_cases:
            for case in manifest["cases"]:
                print(f"{case['id']}\t{case.get('status', 'active')}")
            return 0
        if arguments.profile_llvm_modules and not arguments.profile:
            raise CampaignError("--profile-llvm-modules requires --profile")
        if arguments.fsim is None:
            raise CampaignError("--fsim is required unless --list-cases is used")
        cases = select_cases(manifest, arguments.cases)
        compatibility_profiles: dict[str, str] = {}
        for value in arguments.fsim_vhdl_compatibility:
            case_id, separator, profile = value.partition("=")
            if separator != "=" or not case_id or not profile or case_id in compatibility_profiles:
                raise CampaignError("--fsim-vhdl-compatibility requires unique CASE=PROFILE entries")
            compatibility_profiles[case_id] = profile
        unknown_compatibility_cases = sorted(set(compatibility_profiles)
                                             - {case["id"] for case in cases})
        if unknown_compatibility_cases:
            raise CampaignError("VHDL compatibility supplied for unselected cases: "
                                + ", ".join(unknown_compatibility_cases))
        for case in cases:
            profile = compatibility_profiles.get(case["id"])
            if profile is None:
                continue
            if profile != "legacy-unprotected-shared-variable":
                raise CampaignError(f"unknown VHDL compatibility profile {profile}")
            if not any(group["language"].lower() == "vhdl"
                       for group in case["source_groups"]):
                raise CampaignError(f"case {case['id']} has no VHDL source group")
            case["fsim_vhdl_compatibility"] = profile
        configurations = select_configurations(manifest, arguments.configurations)
        case_configs = {case["id"]: set(case.get("configurations", [])) for case in cases}
        unsupported = [f"{case['id']}/{config['id']}" for case in cases
                       for config in configurations
                       if config["id"] not in case_configs[case["id"]]
                       and not (config["id"] == "llvm_o2_aot"
                                and "llvm_o2" in case_configs[case["id"]])]
        if unsupported:
            raise CampaignError("unsupported case/configuration pairs: " + ", ".join(unsupported))
        if arguments.samples <= 0:
            raise CampaignError("--samples must be positive")
        if arguments.seed <= 0 or arguments.seed > 0xFFFFFFFF:
            raise CampaignError("--seed must be a nonzero unsigned 32-bit value")
        if arguments.vivado_settle_seconds < 0:
            raise CampaignError("--vivado-settle-seconds must not be negative")
        cpu = require_cpu(arguments.cpu)
        evidence = prepare_output(arguments.output)
        environment, removed_profile_variables = stable_environment()
        roots = resolve_roots(manifest, manifest_path,
                              parse_named_paths(arguments.root, "--root"))
        fsim = arguments.fsim.expanduser().resolve()
        if not fsim.is_file() or not os.access(fsim, os.X_OK):
            raise CampaignError(f"fsim executable is missing or not executable: {fsim}")
        vivado = vivado_tools(arguments.vivado_bin.expanduser().resolve())
        overlays = parse_named_paths(arguments.testbench_overlay, "--testbench-overlay")
        if arguments.prepare_reduced and arguments.prepare_full:
            raise CampaignError("--prepare-reduced and --prepare-full are mutually exclusive")
        if arguments.prepare_reduced or arguments.prepare_full:
            if overlays:
                raise CampaignError("use either fixture preparation or --testbench-overlay, not both")
            overlays, fixture_metadata = prepare_fixtures(
                cases, manifest, roots, evidence, arguments.seed, environment, cpu,
                "full" if arguments.prepare_full else "reduced")
        else:
            fixture_metadata = {}
        unknown_overlay_cases = sorted(set(overlays) - {case["id"] for case in cases})
        if unknown_overlay_cases:
            raise CampaignError("overlays supplied for unselected cases: "
                                + ", ".join(unknown_overlay_cases))
        for case in cases:
            if case["id"] in overlays and not overlays[case["id"]].is_file():
                raise CampaignError(f"testbench overlay is missing: {overlays[case['id']]}")
        fixture_metadata = load_overlay_metadata(cases, overlays, fixture_metadata)
        validate_fixture_metadata(cases, overlays, fixture_metadata, manifest, roots,
                                  arguments.seed)
        for left_id, right_id in (("original_codec", "mixed_codec"),
                                  ("original_throughput", "mixed_throughput")):
            left = fixture_metadata.get(left_id, {})
            right = fixture_metadata.get(right_id, {})
            if (left.get("source_sha256") and left.get("source_sha256") == right.get("source_sha256")
                    and left.get("testbench_sha256") != right.get("testbench_sha256")):
                raise CampaignError(
                    f"identical {left_id}/{right_id} source testbenches generated different overlays"
                )
        inputs = collect_case_inputs(cases, manifest, roots, overlays, evidence)
        identities = configuration_identity(
            manifest, manifest_path, cases, configurations, roots,
            fsim, vivado, evidence, environment, cpu, arguments.seed, BASELINE_COMMIT)
        frozen = identities["frozen_build_identity"]
        if frozen.get("verified") and frozen.get("baseline_commit") == BASELINE_COMMIT:
            identities["executable_provenance"] = {
                "kind": "frozen_baseline", "revision": BASELINE_COMMIT,
                "identity_path": frozen["path"], "identity_sha256": frozen["sha256"],
            }
        elif arguments.candidate_provenance:
            identities["executable_provenance"] = {
                "kind": "candidate", "provenance": arguments.candidate_provenance,
                "frozen_identity_revision": frozen.get("baseline_commit"),
            }
        else:
            raise CampaignError(
                "--fsim must match a verified d11004e4 frozen identity or provide "
                "--candidate-provenance"
            )
        identities["source_identity"] = inputs
        identities["fixture_metadata"] = fixture_metadata
        seed_records = []
        for case_metadata in fixture_metadata.values():
            seed_records.extend(extract_seed_records(case_metadata.get("metadata", {})))
        identities["stimulus"]["derived_stream_seeds"] = seed_records
        identities["stimulus"]["algorithm"] = XORSHIFT32_VERSION
        identities["stimulus"]["known_answer_seed"] = "0x6d2b79f5"
        identities["stimulus"]["known_answer_words"] = list(XORSHIFT32_KAT)
        identities["stimulus"]["preflight_transcript_required"] = True
        code_paths = {
            "runner": Path(__file__).resolve(),
            "profile_helper": Path(__file__).with_name("perf_campaign_profile.py").resolve(),
        }
        identities["campaign_code"] = {
            name: {"path": str(path), "sha256": sha256(path)}
            for name, path in code_paths.items() if path.is_file()
        }
        identities["workload_identity"] = {
            case["id"]: original_workload_identity(
                case, fixture_metadata[case["id"]],
                find_testbench_source(case, manifest, roots[case["root"]]))
            for case in cases
        }
        identities["run_configuration"] = {
            "samples": arguments.samples,
            "vivado_settle_seconds": arguments.vivado_settle_seconds,
            "preflight_only": arguments.preflight_only,
            "profile_pass": arguments.profile,
            "profile_llvm_modules": arguments.profile_llvm_modules,
            "waveforms": False,
            "interactive_debugging": False,
            "launch_delay_included_in_total": True,
            "removed_profile_environment_names": sorted(removed_profile_variables),
            "fsim_vhdl_compatibility_by_case": compatibility_profiles,
            "workspace_policy": "fresh per engine, case, config, and sample",
            "fsim_native_cache_policy": {
                "path": "<workspace>/.fsim/cache/llvm-native",
                "jit": "require no native objects or AOT receipts before simulate",
                "aot": (
                    "require no native objects or AOT receipts before elaborate; record pre-sim "
                    "inventory; AOT cost remains in elaborate and total"
                ),
            },
            "successful_workspace_retention": arguments.retain_workspaces,
            "reuse_preflight_from": str(arguments.reuse_preflight.resolve())
                if arguments.reuse_preflight else None,
        }
        campaign_manifest_path = evidence / "campaign_manifest.json"
        campaign_manifest_path.write_text(json.dumps(identities, indent=2, sort_keys=True) + "\n",
                                          encoding="utf-8")
        (evidence / "manifest.sha256").write_text(sha256(manifest_path) + "\n",
                                                  encoding="utf-8")

        config_for_preflight = next((item for item in configurations
                                     if item["id"] == "llvm_o2"), configurations[0])
        preflight_results, expected_preflight = run_preflight(
            cases, config_for_preflight, fsim, vivado, manifest, roots, overlays,
            evidence, environment, cpu, arguments.seed, fixture_metadata,
            arguments.retain_workspaces, identities,
            arguments.reuse_preflight.resolve() if arguments.reuse_preflight else None)
        report: dict[str, Any] = {
            "campaign_manifest": str(campaign_manifest_path.resolve()),
            "campaign_manifest_sha256": sha256(campaign_manifest_path),
            "identities": identities,
            "preflight_results": preflight_results,
            "timing_results": [], "summaries": [],
            "passed_preflight": True, "passed_timing": None,
            "overall_qualified": False,
        }
        if arguments.preflight_only:
            report["status"] = "diagnostic_preflight_passed"
        else:
            timing_results = run_samples(
                cases, configurations, fsim, vivado, manifest, roots, overlays,
                evidence, environment, cpu, arguments.seed, arguments.samples,
                arguments.vivado_settle_seconds, fixture_metadata, expected_preflight,
                arguments.retain_workspaces)
            summaries = summarize_samples(timing_results)
            report["timing_results"] = timing_results
            report["summaries"] = summaries
            o2_summaries = [item for item in summaries if item["configuration"] == "llvm_o2"]
            report["passed_timing"] = bool(o2_summaries) and all(
                item["fsim_median_below_vivado"] for item in o2_summaries)
            complete_reference_matrix = (len(cases) == len(REFERENCE_CASES)
                                         and set(case["id"] for case in cases)
                                         == set(REFERENCE_CASES))
            complete_original_workloads = all(
                identities["workload_identity"].get(case_id, {}).get("verified", False)
                for case_id in REFERENCE_CASES)
            enough_samples = (len(o2_summaries) == len(REFERENCE_CASES)
                              and all(item["engines"]["fsim"]["samples"] >= 7
                                      and item["engines"]["vivado"]["samples"] >= 7
                                      for item in o2_summaries))
            report["overall_qualified"] = (
                complete_reference_matrix and complete_original_workloads and enough_samples
                and all(item["fsim_median_below_vivado"] and item["noise_clear"]
                        for item in o2_summaries)
            )
            report["completion_scope"] = completion_scope(
                complete_reference_matrix, complete_original_workloads)
            report["original_workloads_verified"] = complete_original_workloads
            report["workload_identity"] = identities["workload_identity"]
            report["requires_more_samples"] = any(
                item["fsim_median_below_vivado"] and not item["noise_clear"]
                for item in o2_summaries)
            if report["overall_qualified"]:
                report["status"] = "qualified"
            elif o2_summaries and not report["passed_timing"]:
                report["status"] = "performance_gate_failed"
            else:
                report["status"] = "diagnostic_partial"

        if arguments.profile:
            profile_results = run_profiles(
                cases, fsim, manifest, roots, overlays, evidence, environment, cpu,
                arguments.seed, fixture_metadata, expected_preflight,
                arguments.retain_workspaces, arguments.profile_llvm_modules)
            report["profile_results"] = profile_results
            report["profile_interpretation"] = (
                "compile, elaborate, and simulation are CPU-sampled in separate runs; "
                "instrumented time/RSS do not establish speedup and overlapping JIT phase "
                "timers are not additive"
            )

        # Verify every source and tool identity again after the executions.
        post_inputs = collect_case_inputs(cases, manifest, roots, overlays, evidence)
        if post_inputs != inputs:
            raise CampaignError("source or testbench inputs changed during the campaign")
        validate_fixture_metadata(cases, overlays, fixture_metadata, manifest, roots,
                                  arguments.seed)
        if sha256(manifest_path) != identities["manifest_sha256"]:
            raise CampaignError("benchmark manifest changed during the campaign")
        post_identity = configuration_identity(
            manifest, manifest_path, cases, configurations, roots,
            fsim, vivado, evidence, environment, cpu, arguments.seed, BASELINE_COMMIT)
        def binary_identity(document: dict[str, Any]) -> dict[str, Any]:
            return {name: {
                "path": item.get("path"), "sha256": item.get("sha256"),
                "dependencies": item.get("dependencies"),
                "wrapper_files": item.get("wrapper_files"),
            } for name, item in document.items()}
        if binary_identity(identities["simulator_binaries"]) != binary_identity(
                post_identity["simulator_binaries"]):
            raise CampaignError("simulator executable or runtime dependency identity changed")
        if binary_identity(identities["auxiliary_tools"]) != binary_identity(
                post_identity["auxiliary_tools"]):
            raise CampaignError("measurement tool identity changed during the campaign")
        if identities["frozen_build_identity"] != post_identity["frozen_build_identity"]:
            raise CampaignError("frozen fsim identity changed during the campaign")
        for name, record in identities["campaign_code"].items():
            if sha256(Path(record["path"])) != record["sha256"]:
                raise CampaignError(f"campaign code changed during the campaign: {name}")
        identities["post_run_identity_check"] = {
            "passed": True, "source_identity_unchanged": True,
            "simulator_binary_and_dependencies_unchanged": True,
            "measurement_tools_unchanged": True,
        }
        campaign_manifest_path.write_text(json.dumps(identities, indent=2, sort_keys=True) + "\n",
                                          encoding="utf-8")
        report["campaign_manifest_sha256"] = sha256(campaign_manifest_path)
        report_path = evidence / "campaign_report.json"
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                               encoding="utf-8")
        write_preflight_proofs(evidence, report)
        print(f"campaign report: {report_path}")
        if report["status"] == "performance_gate_failed":
            return 2
        return 0
    except CampaignError as error:
        print(f"error: {error}", file=sys.stderr)
        if evidence is not None:
            failure = {"status": "failed", "error": str(error)}
            (evidence / "campaign_failure.json").write_text(
                json.dumps(failure, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"partial evidence: {evidence}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
