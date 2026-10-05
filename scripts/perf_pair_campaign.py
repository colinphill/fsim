#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Run paired control/candidate Wall measurements for the two throughput cases.

The existing perf_campaign.py remains the authority for fixture generation,
preflight correctness, and one-fsim-versus-Vivado comparisons. This outer
runner uses that CLI twice for independent control and candidate preflights,
then reuses its phase runner for matched control/candidate Wall pairs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import statistics
import sys
from typing import Any

import perf_campaign as campaign


CASE_IDS = ("original_throughput", "mixed_throughput")
CONFIGURATION_ID = "llvm_o2"
FSIM_VHDL_COMPATIBILITY = {
    "mixed_throughput": "legacy-unprotected-shared-variable",
}
MIN_DECISION_PAIRS = 5
MIN_QUALIFICATION_PAIRS = 7
MAX_CANDIDATE_MEDIAN_SECONDS = 15.0
MAX_ORIGINAL_TO_MIXED_RATIO = 1.05
BOOTSTRAP_ITERATIONS = 10_000


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control", type=Path, required=True,
                        help="frozen control fsim executable")
    parser.add_argument("--candidate", type=Path, required=True,
                        help="frozen candidate fsim executable")
    parser.add_argument("--control-provenance", required=True,
                        help="control revision/build identity recorded with evidence")
    parser.add_argument("--candidate-provenance", required=True,
                        help="candidate revision/build identity recorded with evidence")
    parser.add_argument("--manifest", type=Path, default=campaign.DEFAULT_MANIFEST)
    parser.add_argument("--vivado-bin", type=Path,
                        default=Path("/opt/eda/AMD/2025.2.1/Vivado/bin"))
    parser.add_argument("--output", type=Path, required=True,
                        help="new or empty directory for complete pairing evidence")
    parser.add_argument("--root", action="append", default=[], metavar="NAME=PATH",
                        help="override a manifest source root; shared by all legs")
    parser.add_argument("--pairs", type=int, default=MIN_DECISION_PAIRS,
                        help="matched Wall pairs per throughput case (minimum 5; 7 to qualify)")
    parser.add_argument("--seed", type=lambda value: int(value, 0),
                        default=campaign.DEFAULT_SEED)
    parser.add_argument("--cpu", type=int, required=True,
                        help="one currently available CPU shared by every subprocess")
    parser.add_argument("--retain-workspaces", action="store_true",
                        help="retain successful per-leg workspace and native-cache trees")
    return parser.parse_args(argv)


def _read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise campaign.CampaignError(f"cannot read {label} {path}: {error}") from error
    if not isinstance(result, dict):
        raise campaign.CampaignError(f"{label} is not a JSON object: {path}")
    return result


def _require_nonzero_cpu(cpu: int) -> int:
    if cpu <= 0:
        raise campaign.CampaignError("--cpu must select a nonzero CPU")
    return campaign.require_cpu(cpu)


def _fixture_projection(record: dict[str, Any]) -> dict[str, Any]:
    source_path = record.get("source_path", record.get("source"))
    source_hash = record.get("source_sha256", record.get("source_sha"))
    metadata = record.get("metadata", {})
    return {
        "source_path": source_path,
        "source_sha256": source_hash,
        "testbench": record.get("testbench"),
        "testbench_sha256": record.get("testbench_sha256"),
        "metadata_path": record.get("metadata_path"),
        "metadata_sha256": record.get("metadata_sha256"),
        "metadata": record.get("metadata"),
        "seed": campaign.metadata_root_seed(metadata),
        "algorithm": campaign.metadata_algorithm(metadata),
    }


def _shared_identity_projection(identity: dict[str, Any]) -> dict[str, Any]:
    simulator_binaries = identity.get("simulator_binaries", {})
    return {
        "manifest_sha256": identity.get("manifest_sha256"),
        "cases": identity.get("cases"),
        "configurations": identity.get("configurations"),
        "source_roots": identity.get("source_roots"),
        "source_identity": identity.get("source_identity"),
        "fixture_metadata": {
            case_id: _fixture_projection(identity.get("fixture_metadata", {}).get(case_id, {}))
            for case_id in CASE_IDS
        },
        "stimulus": identity.get("stimulus"),
        "workload_identity": identity.get("workload_identity"),
        "campaign_code": identity.get("campaign_code"),
        "system": identity.get("system"),
        "benchmark_configuration": identity.get("benchmark_configuration"),
        "run_configuration": identity.get("run_configuration"),
        "auxiliary_tools": identity.get("auxiliary_tools"),
        "vivado_binaries": {name: record for name, record in simulator_binaries.items()
                             if name != "fsim"},
    }


def _load_preflight_bundle(directory: Path, expected_executable: Path
                           ) -> dict[str, Any]:
    report_path = directory / "campaign_report.json"
    report = _read_json(report_path, "preflight campaign report")
    if report.get("status") != "diagnostic_preflight_passed" or not report.get("passed_preflight"):
        raise campaign.CampaignError(f"preflight did not pass: {report_path}")
    manifest_path = Path(report.get("campaign_manifest", ""))
    if not manifest_path.is_file() or campaign.sha256(manifest_path) != report.get(
            "campaign_manifest_sha256"):
        raise campaign.CampaignError(f"preflight campaign manifest is missing or changed: {manifest_path}")
    identity = _read_json(manifest_path, "preflight campaign manifest")
    if identity.get("post_run_identity_check", {}).get("passed") is not True:
        raise campaign.CampaignError(f"preflight did not finish its identity checks: {manifest_path}")
    fsim_identity = identity.get("simulator_binaries", {}).get("fsim", {})
    resolved_executable = expected_executable.resolve()
    if (Path(fsim_identity.get("path", "")).resolve() != resolved_executable
            or fsim_identity.get("sha256") != campaign.sha256(resolved_executable)):
        raise campaign.CampaignError(f"preflight executable identity differs: {resolved_executable}")
    campaign.require_wall_measurement_binary(str(fsim_identity.get("version", "")), False)

    expected = {(case_id, engine) for case_id in CASE_IDS for engine in ("fsim", "vivado")}
    results: dict[tuple[str, str], dict[str, Any]] = {}
    preflight_results = report.get("preflight_results")
    if not isinstance(preflight_results, list):
        raise campaign.CampaignError(
            f"preflight report has no result list: {report_path}"
        )
    for result in preflight_results:
        if not isinstance(result, dict):
            raise campaign.CampaignError(
                f"preflight report contains a malformed result row: {report_path}"
            )
        key = (result.get("case"), result.get("engine"))
        if key not in expected or key in results:
            raise campaign.CampaignError(f"unexpected or duplicate preflight result {key}")
        if result.get("validation_error"):
            raise campaign.CampaignError(f"preflight result failed for {key}: {result['validation_error']}")
        canonical_path = Path(result.get("transcript_canonical", ""))
        if not canonical_path.is_file():
            raise campaign.CampaignError(f"canonical preflight transcript is missing for {key}")
        result["canonical_transcript"] = _read_json(
            canonical_path, f"canonical preflight transcript for {key}")
        result["canonical_transcript_sha256"] = campaign.sha256(canonical_path)
        results[key] = result
    if set(results) != expected:
        missing = sorted(expected - set(results))
        raise campaign.CampaignError(f"preflight is missing required case/engine rows: {missing}")
    return {"directory": directory.resolve(), "report": report,
            "identity": identity, "results": results}


def _compare_preflight_bundles(control: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    expected_results = {(case_id, engine) for case_id in CASE_IDS
                        for engine in ("fsim", "vivado")}
    for label, bundle in (("control", control), ("candidate", candidate)):
        if set(bundle.get("results", {})) != expected_results:
            raise campaign.CampaignError(
                f"{label} preflight has missing or extra case/engine results"
            )
    control_projection = _shared_identity_projection(control["identity"])
    candidate_projection = _shared_identity_projection(candidate["identity"])
    if control_projection != candidate_projection:
        raise campaign.CampaignError(
            "control and candidate preflight source, fixture, stimulus, host, or tool inputs differ"
        )
    control_provenance = control["identity"].get("executable_provenance")
    candidate_provenance = candidate["identity"].get("executable_provenance")
    if (not control_provenance or not candidate_provenance
            or control_provenance == candidate_provenance):
        raise campaign.CampaignError("control and candidate executable provenance must be distinct")
    control_fsim = control["identity"]["simulator_binaries"]["fsim"]
    candidate_fsim = candidate["identity"]["simulator_binaries"]["fsim"]
    if (control_fsim.get("sha256") == candidate_fsim.get("sha256")
            or Path(control_fsim.get("path", "")).resolve()
            == Path(candidate_fsim.get("path", "")).resolve()):
        raise campaign.CampaignError("control and candidate must be distinct fsim executables")
    for case_id in CASE_IDS:
        documents = [control["results"][(case_id, engine)]["canonical_transcript"]
                     for engine in ("fsim", "vivado")]
        documents.extend(candidate["results"][(case_id, engine)]["canonical_transcript"]
                          for engine in ("fsim", "vivado"))
        if any(document != documents[0] for document in documents[1:]):
            raise campaign.CampaignError(
                f"control/candidate/Vivado canonical preflight transcripts differ for {case_id}"
            )
        correctness = [control["results"][(case_id, engine)]["correctness_lines"]
                       for engine in ("fsim", "vivado")]
        correctness.extend(candidate["results"][(case_id, engine)]["correctness_lines"]
                           for engine in ("fsim", "vivado"))
        if any(lines != correctness[0] for lines in correctness[1:]):
            raise campaign.CampaignError(
                f"control/candidate/Vivado correctness records differ for {case_id}"
            )
        for label, bundle in (("control", control), ("candidate", candidate)):
            if bundle["identity"].get("workload_identity", {}).get(case_id, {}).get(
                    "verified") is not True:
                raise campaign.CampaignError(
                    f"{label} preflight does not verify the full original workload for {case_id}"
                )
    return {
        case_id: {
            label: bundle["results"][(case_id, "fsim")]
            for label, bundle in (("control", control), ("candidate", candidate))
        }
        for case_id in CASE_IDS
    }


def _preflight_command(binary: Path, provenance: str, output: Path,
                       manifest: Path, vivado_bin: Path,
                       root_overrides: list[str], seed: int, cpu: int,
                       overlays: dict[str, Path] | None = None) -> list[str]:
    command = [
        sys.executable, str(Path(campaign.__file__).resolve()),
        "--manifest", str(manifest), "--fsim", str(binary),
        "--vivado-bin", str(vivado_bin), "--output", str(output),
        "--configuration", CONFIGURATION_ID, "--samples", "1",
        "--seed", str(seed), "--cpu", str(cpu), "--preflight-only",
        "--candidate-provenance", provenance,
    ]
    for case_id in CASE_IDS:
        command.extend(["--case", case_id])
    for case_id, profile in sorted(FSIM_VHDL_COMPATIBILITY.items()):
        command.extend(["--fsim-vhdl-compatibility", f"{case_id}={profile}"])
    for override in root_overrides:
        command.extend(["--root", override])
    if overlays is None:
        command.append("--prepare-full")
    else:
        for case_id in CASE_IDS:
            command.extend(["--testbench-overlay", f"{case_id}={overlays[case_id]}"])
    return command


def _run_preflight(binary: Path, provenance: str, directory: Path,
                   manifest: Path, vivado_bin: Path, root_overrides: list[str],
                   seed: int, cpu: int, environment: dict[str, str],
                   overlays: dict[str, Path] | None) -> dict[str, Any]:
    directory.parent.mkdir(parents=True, exist_ok=True)
    command = _preflight_command(binary, provenance, directory, manifest, vivado_bin,
                                 root_overrides, seed, cpu, overlays)
    result = campaign.run_capture(command, campaign.REPOSITORY, environment, cpu)
    log_path = directory.parent / f"{directory.name}-driver.log"
    log_path.write_text(result.stdout, encoding="utf-8")
    (directory.parent / f"{directory.name}-driver-command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8")
    if result.returncode != 0:
        raise campaign.CampaignError(
            f"{directory.name} preflight failed with exit {result.returncode}; inspect {log_path}"
        )
    return _load_preflight_bundle(directory, binary)


def _configure_cases(cases: list[dict[str, Any]]) -> list[dict[str, Any]]:
    configured = []
    for case in cases:
        configured_case = dict(case)
        profile = FSIM_VHDL_COMPATIBILITY.get(case["id"])
        if profile is not None:
            configured_case["fsim_vhdl_compatibility"] = profile
        configured.append(configured_case)
    return configured


def _verify_fixture_generators(bundle: dict[str, Any],
                               cases: list[dict[str, Any]]) -> dict[str, dict[str, str]]:
    records: dict[str, dict[str, str]] = {}
    fixture_metadata = bundle["identity"].get("fixture_metadata", {})
    for case in cases:
        case_id = case["id"]
        fixture = fixture_metadata.get(case_id, {})
        generator = Path(fixture.get("generator", "")).resolve()
        expected = campaign.fixture_generator_path(case_id).resolve()
        digest = fixture.get("generator_sha256")
        if generator != expected or not generator.is_file() or digest != campaign.sha256(generator):
            raise campaign.CampaignError(
                f"control preflight fixture generator identity is invalid for {case_id}"
            )
        records[case_id] = {"path": str(generator), "sha256": digest}
    return records


def randomized_schedule(pairs: int, seed: int) -> list[dict[str, Any]]:
    """Interleave cases by round and independently balance each C/C leg order."""
    case_orders = campaign.randomized_pair_orders(
        CASE_IDS, pairs, seed, "throughput-case-round-order|llvm_o2")
    leg_orders = {
        case_id: campaign.randomized_pair_orders(
            ("control", "candidate"), pairs, seed,
            f"{case_id}|llvm_o2|control-candidate")
        for case_id in CASE_IDS
    }
    return [{
        "round": round_index + 1,
        "case_order": list(case_order),
        "leg_order_by_case": {
            case_id: list(leg_orders[case_id][round_index]) for case_id in CASE_IDS
        },
    } for round_index, case_order in enumerate(case_orders)]


def _timing_directory(output: Path, case_id: str, round_index: int,
                      leg: str) -> Path:
    return output / "timings" / case_id / f"round-{round_index:02d}" / leg


def _native_cache_directory(output: Path, case_id: str, round_index: int,
                            leg: str) -> Path:
    workspace = _timing_directory(output, case_id, round_index, leg) / "workspace"
    return campaign.fsim_native_cache_path(workspace)


def _timed_leg(binary: Path, leg: str, case: dict[str, Any], config: dict[str, Any],
               manifest: dict[str, Any], roots: dict[str, Path], overlay: Path,
               fixture_record: dict[str, Any], expected_preflight: dict[str, Any],
               output: Path, round_index: int, pair_id: str,
               leg_order: list[str], environment: dict[str, str],
               cpu: int, seed: int, retain_workspaces: bool) -> dict[str, Any]:
    directory = _timing_directory(output, case["id"], round_index, leg)
    input_directory = output / "inputs" / case["id"] / f"round-{round_index:02d}" / leg
    input_directory.mkdir(parents=True, exist_ok=True)
    result = campaign.run_engine(
        "fsim", binary, {}, case, config, manifest, roots, overlay,
        input_directory, directory, environment, cpu, seed,
    )
    expected_cache = _native_cache_directory(output, case["id"], round_index, leg).resolve()
    if (Path(result["native_cache"]).resolve() != expected_cache
            or not result["native_cache_state"]["jit_cold_before_simulation"]):
        raise campaign.CampaignError(
            f"{leg} did not receive a fresh empty native cache for {pair_id}"
        )
    try:
        transcript = campaign.verify_correctness(
            case, result, True, False, fixture_metadata=fixture_record,
            expected_timed_summaries=expected_preflight["stimulus"]["summaries"],
            expected_timed_correctness_lines=expected_preflight["correctness_lines"],
        )
    except campaign.CampaignError as error:
        campaign.persist_engine_result(directory, result, str(error))
        raise
    campaign.persist_transcript(directory, result, transcript)
    result["pair_id"] = pair_id
    result["round"] = round_index
    result["leg"] = leg
    result["leg_order"] = list(leg_order)
    result["engine_result_path"] = str(campaign.persist_engine_result(directory, result))
    return result


def _quartiles(values: list[float]) -> dict[str, float | None]:
    if not values:
        raise campaign.CampaignError("cannot summarize an empty measurement series")
    quartiles = statistics.quantiles(values, n=4, method="inclusive") if len(values) >= 2 else None
    return {
        "samples": len(values),
        "median": statistics.median(values),
        "min": min(values),
        "max": max(values),
        "iqr": quartiles[2] - quartiles[0] if quartiles else None,
    }


def _bootstrap_interval(values: list[float], seed: int) -> list[float] | None:
    if len(values) < 2:
        return None
    randomizer = random.Random(seed)
    medians = [statistics.median(randomizer.choice(values) for _ in values)
               for _ in range(BOOTSTRAP_ITERATIONS)]
    medians.sort()
    return [medians[int(0.025 * (len(medians) - 1))],
            medians[int(0.975 * (len(medians) - 1))]]


def summarize_case_pairs(pair_rows: list[dict[str, Any]], expected_pairs: int,
                         seed: int) -> dict[str, Any]:
    if len(pair_rows) != expected_pairs:
        raise campaign.CampaignError(
            f"case has {len(pair_rows)} completed pairs; expected {expected_pairs}"
        )
    by_pair_id: dict[str, dict[str, Any]] = {}
    expected_case = pair_rows[0].get("case") if pair_rows else None
    for pair in pair_rows:
        pair_id = pair.get("pair_id")
        legs = pair.get("legs")
        if (pair.get("case") != expected_case
                or not isinstance(pair_id, str) or not pair_id or pair_id in by_pair_id
                or not isinstance(legs, dict) or set(legs) != {"control", "candidate"}):
            raise campaign.CampaignError("timing pair has a missing/duplicate identity or leg")
        for leg in ("control", "candidate"):
            row = legs[leg]
            elapsed = row.get("elapsed_seconds")
            if (row.get("pair_id") != pair_id or row.get("leg") != leg
                    or row.get("case") != expected_case
                    or row.get("round") != pair.get("round")
                    or not isinstance(elapsed, (int, float)) or not math.isfinite(elapsed)
                    or elapsed <= 0):
                raise campaign.CampaignError(f"invalid {leg} timing row for {pair_id}")
        by_pair_id[pair_id] = pair

    ordered = sorted(by_pair_id.values(), key=lambda pair: pair["round"])
    rounds = [pair["round"] for pair in ordered]
    if len(set(rounds)) != len(rounds):
        raise campaign.CampaignError("timing pairs repeat a case/round identity")
    times = {
        leg: [float(pair["legs"][leg]["elapsed_seconds"]) for pair in ordered]
        for leg in ("control", "candidate")
    }
    ratios = [pair["legs"]["candidate"]["elapsed_seconds"]
              / pair["legs"]["control"]["elapsed_seconds"] for pair in ordered]
    deltas = [pair["legs"]["candidate"]["elapsed_seconds"]
              - pair["legs"]["control"]["elapsed_seconds"] for pair in ordered]
    bootstrap_seed = int(hashlib.sha256(
        f"paired-throughput-v1|{seed}|{ordered[0]['case']}|{CONFIGURATION_ID}".encode()
    ).hexdigest()[:16], 16)

    def phase_summary(leg: str) -> dict[str, dict[str, float | None]]:
        phase_names = sorted({name for pair in ordered
                              for name in pair["legs"][leg].get("phase_seconds", {})})
        return {
            name: _quartiles([
                float(pair["legs"][leg]["phase_seconds"][name])
                for pair in ordered
                if name in pair["legs"][leg].get("phase_seconds", {})
            ]) for name in phase_names
        }

    leg_summary = {
        leg: {
            "wall_seconds": _quartiles(times[leg]),
            "phase_seconds": phase_summary(leg),
            "peak_rss_kib": _quartiles([
                float(pair["legs"][leg]["peak_rss_kib"])
                for pair in ordered
                if pair["legs"][leg].get("peak_rss_kib") is not None
            ]) if any(pair["legs"][leg].get("peak_rss_kib") is not None
                     for pair in ordered) else None,
        } for leg in ("control", "candidate")
    }
    ratio_median = statistics.median(ratios)
    ratio_interval = _bootstrap_interval(ratios, bootstrap_seed)
    delta_interval = _bootstrap_interval(deltas, bootstrap_seed ^ 0x9E3779B97F4A7C15)
    if ratio_interval is not None and ratio_interval[1] < 1.0:
        improvement = "supported_improvement"
    elif ratio_interval is not None and ratio_interval[0] > 1.0:
        improvement = "supported_regression"
    else:
        improvement = "inconclusive"
    if ratio_median < 1.0:
        point_estimate = "candidate_faster"
    elif ratio_median > 1.0:
        point_estimate = "candidate_slower"
    else:
        point_estimate = "equal_median"
    return {
        "case": ordered[0]["case"],
        "configuration": CONFIGURATION_ID,
        "pair_count": len(ordered),
        "paired_ids": [pair["pair_id"] for pair in ordered],
        "legs": leg_summary,
        "candidate_over_control": {
            "median_paired_ratio": ratio_median,
            "median_paired_delta_seconds": statistics.median(deltas),
            "paired_ratio_95pct_bootstrap_ci": ratio_interval,
            "paired_delta_95pct_bootstrap_ci_seconds": delta_interval,
            "point_estimate_status": point_estimate,
            "inference_status": improvement,
        },
    }


def assess_campaign(summaries: dict[str, dict[str, Any]], pair_count: int,
                    preflight_passed: bool, workload_verified: bool) -> dict[str, Any]:
    complete = all(case_id in summaries and summaries[case_id]["pair_count"] == pair_count
                   for case_id in CASE_IDS)
    decision_ready = complete and pair_count >= MIN_DECISION_PAIRS
    final_pairs_ready = complete and pair_count >= MIN_QUALIFICATION_PAIRS
    original = summaries.get("original_throughput", {})
    mixed = summaries.get("mixed_throughput", {})
    original_median = original.get("legs", {}).get("candidate", {}).get(
        "wall_seconds", {}).get("median")
    mixed_median = mixed.get("legs", {}).get("candidate", {}).get(
        "wall_seconds", {}).get("median")
    control_mixed_median = mixed.get("legs", {}).get("control", {}).get(
        "wall_seconds", {}).get("median")
    if not final_pairs_ready or original_median is None or mixed_median is None:
        absolute_status = "insufficient_final_pairs"
    elif (original_median <= MAX_CANDIDATE_MEDIAN_SECONDS
          and mixed_median <= MAX_CANDIDATE_MEDIAN_SECONDS
          and original_median <= MAX_ORIGINAL_TO_MIXED_RATIO * mixed_median):
        absolute_status = "pass"
    else:
        absolute_status = "fail"
    absolute_passed = absolute_status == "pass"
    if (not final_pairs_ready or original_median is None or mixed_median is None
            or control_mixed_median is None):
        anti_slowdown_status = "insufficient_final_pairs"
        anti_slowdown_limit = None
    else:
        anti_slowdown_limit = MAX_ORIGINAL_TO_MIXED_RATIO * min(
            control_mixed_median, mixed_median)
        anti_slowdown_status = (
            "pass" if original_median <= anti_slowdown_limit else "fail")
    anti_slowdown_passed = anti_slowdown_status == "pass"
    case_improvements = {
        case_id: summaries.get(case_id, {}).get("candidate_over_control", {}).get(
            "inference_status", "insufficient_pairs")
        for case_id in CASE_IDS
    }
    if not decision_ready:
        improvement_status = "insufficient_pairs"
    elif any(status == "supported_regression" for status in case_improvements.values()):
        improvement_status = "supported_case_regression"
    elif all(status == "supported_improvement" for status in case_improvements.values()):
        improvement_status = "supported_improvement"
    else:
        improvement_status = "inconclusive"
    overall_qualified = bool(
        preflight_passed and workload_verified and final_pairs_ready
        and absolute_passed and anti_slowdown_passed
    )
    if overall_qualified:
        status = "qualified_absolute_targets"
    elif not decision_ready:
        status = "insufficient_pairs"
    elif not preflight_passed or not workload_verified:
        status = "invalid_preflight_or_workload_identity"
    elif not final_pairs_ready:
        status = "decision_only_more_final_pairs_required"
    elif not absolute_passed:
        status = "absolute_target_failed"
    elif not anti_slowdown_passed:
        status = "anti_slowdown_guard_failed"
    else:
        status = "not_qualified"
    return {
        "status": status,
        "decision_pairs_met": decision_ready,
        "final_qualification_pairs_met": final_pairs_ready,
        "absolute_performance_targets_status": absolute_status,
        "absolute_performance_targets_passed": absolute_passed,
        "anti_slowdown_guard_status": anti_slowdown_status,
        "anti_slowdown_guard_limit_seconds": anti_slowdown_limit,
        "anti_slowdown_guard_passed": anti_slowdown_passed,
        "absolute_targets": {
            "candidate_original_median_max_seconds": MAX_CANDIDATE_MEDIAN_SECONDS,
            "candidate_mixed_median_max_seconds": MAX_CANDIDATE_MEDIAN_SECONDS,
            "candidate_original_over_mixed_max_ratio": MAX_ORIGINAL_TO_MIXED_RATIO,
        },
        "case_improvement_status": case_improvements,
        "control_candidate_improvement_status": improvement_status,
        "overall_qualified": overall_qualified,
        "overall_improvement_supported": improvement_status == "supported_improvement",
    }


def _verify_file_record(record: dict[str, Any], label: str) -> None:
    path_text = record.get("path")
    expected_hash = record.get("sha256")
    if not isinstance(path_text, str) or not isinstance(expected_hash, str):
        raise campaign.CampaignError(f"invalid frozen {label} identity record")
    path = Path(path_text)
    if not path.is_file() or campaign.sha256(path) != expected_hash:
        raise campaign.CampaignError(f"frozen {label} changed during the pairing campaign: {path}")


def _verify_bundle_files(bundle: dict[str, Any]) -> None:
    identity = bundle["identity"]
    for record in identity.get("simulator_binaries", {}).values():
        _verify_file_record(record, "simulator executable")
        for nested in record.get("dependencies", []) + record.get("wrapper_files", []):
            _verify_file_record(nested, "simulator dependency")
    for record in identity.get("auxiliary_tools", {}).values():
        _verify_file_record(record, "measurement tool")
        for nested in record.get("dependencies", []) + record.get("wrapper_files", []):
            _verify_file_record(nested, "measurement dependency")


def _persist_failure(output: Path, error: str) -> None:
    if output.exists():
        (output / "campaign_failure.json").write_text(json.dumps({
            "status": "failed", "overall_qualified": False,
            "error": error,
        }, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    output: Path | None = None
    try:
        if args.pairs < MIN_DECISION_PAIRS:
            raise campaign.CampaignError(f"--pairs must be at least {MIN_DECISION_PAIRS}")
        if args.seed <= 0 or args.seed > 0xFFFFFFFF:
            raise campaign.CampaignError("--seed must be a nonzero unsigned 32-bit value")
        if not args.control_provenance.strip() or not args.candidate_provenance.strip():
            raise campaign.CampaignError("control and candidate provenance must be nonempty")
        if args.control_provenance.strip() == args.candidate_provenance.strip():
            raise campaign.CampaignError("control and candidate provenance must be distinct")
        manifest_path = args.manifest.expanduser().resolve()
        manifest = campaign.load_manifest(manifest_path)
        cases = _configure_cases(campaign.select_cases(manifest, list(CASE_IDS)))
        config = campaign.select_configurations(manifest, [CONFIGURATION_ID])[0]
        if any(CONFIGURATION_ID not in case.get("configurations", []) for case in cases):
            raise campaign.CampaignError("both throughput cases must support llvm_o2")
        root_overrides = campaign.parse_named_paths(args.root, "--root")
        roots = campaign.resolve_roots(manifest, manifest_path, root_overrides)
        cpu = _require_nonzero_cpu(args.cpu)
        control_binary = args.control.expanduser().resolve()
        candidate_binary = args.candidate.expanduser().resolve()
        for binary in (control_binary, candidate_binary):
            if not binary.is_file() or not os.access(binary, os.X_OK):
                raise campaign.CampaignError(f"fsim executable is missing or not executable: {binary}")
        if (control_binary == candidate_binary
                or campaign.sha256(control_binary) == campaign.sha256(candidate_binary)):
            raise campaign.CampaignError("control and candidate must be distinct fsim executables")
        vivado_bin = args.vivado_bin.expanduser().resolve()
        environment, removed_environment = campaign.stable_environment()
        output = campaign.prepare_output(args.output)
        (output / "driver_logs").mkdir()
        manifest_hash = campaign.sha256(manifest_path)
        runner_hash = campaign.sha256(Path(__file__).resolve())
        source_runner_hash = campaign.sha256(Path(campaign.__file__).resolve())

        control_bundle = _run_preflight(
            control_binary, args.control_provenance, output / "preflight" / "control",
            manifest_path, vivado_bin, args.root, args.seed, cpu, environment, None)
        fixture_generators = _verify_fixture_generators(control_bundle, cases)
        overlays = {
            case_id: Path(control_bundle["identity"]["fixture_metadata"][case_id]["testbench"])
            for case_id in CASE_IDS
        }
        for case_id, overlay in overlays.items():
            expected = control_bundle["identity"]["fixture_metadata"][case_id][
                "testbench_sha256"]
            if not overlay.is_file() or campaign.sha256(overlay) != expected:
                raise campaign.CampaignError(f"full control fixture changed before candidate preflight: {case_id}")
        candidate_bundle = _run_preflight(
            candidate_binary, args.candidate_provenance,
            output / "preflight" / "candidate", manifest_path, vivado_bin,
            args.root, args.seed, cpu, environment, overlays)
        expected_preflight = _compare_preflight_bundles(control_bundle, candidate_bundle)
        _verify_bundle_files(control_bundle)
        _verify_bundle_files(candidate_bundle)

        identities = {
            "schema": "fsim-throughput-control-candidate-v1",
            "cases": list(CASE_IDS), "configuration": config,
            "manifest": {"path": str(manifest_path), "sha256": manifest_hash},
            "runner": {"path": str(Path(__file__).resolve()), "sha256": runner_hash},
            "campaign_runner": {"path": str(Path(campaign.__file__).resolve()),
                                "sha256": source_runner_hash},
            "control_preflight": {
                "directory": str(control_bundle["directory"]),
                "campaign_manifest_sha256": control_bundle["report"]["campaign_manifest_sha256"],
                "fsim_sha256": control_bundle["identity"]["simulator_binaries"]["fsim"]["sha256"],
            },
            "candidate_preflight": {
                "directory": str(candidate_bundle["directory"]),
                "campaign_manifest_sha256": candidate_bundle["report"]["campaign_manifest_sha256"],
                "fsim_sha256": candidate_bundle["identity"]["simulator_binaries"]["fsim"]["sha256"],
            },
            "shared_preflight_identity": _shared_identity_projection(control_bundle["identity"]),
            "fixture_generators": fixture_generators,
            "fsim_vhdl_compatibility_by_case": FSIM_VHDL_COMPATIBILITY,
            "host": control_bundle["identity"]["system"],
            "cpu": cpu,
            "removed_environment_variables": sorted(removed_environment),
            "cache_policy": {
                "fsim": "fresh isolated workspace/native cache for every executable leg",
                "os_page_cache": "left warm; never dropped between preflight or timing legs",
                "profile_pass": False,
            },
            "pair_count": args.pairs,
            "seed": args.seed,
            "schedule": randomized_schedule(args.pairs, args.seed),
        }
        manifest_output = output / "pair_campaign_manifest.json"
        manifest_output.write_text(json.dumps(identities, indent=2, sort_keys=True) + "\n",
                                   encoding="utf-8")

        pair_rows = {case_id: [] for case_id in CASE_IDS}
        for schedule_row in identities["schedule"]:
            round_index = int(schedule_row["round"])
            for case_id in schedule_row["case_order"]:
                pair_id = f"round-{round_index:02d}:{case_id}"
                leg_order = schedule_row["leg_order_by_case"][case_id]
                case = next(item for item in cases if item["id"] == case_id)
                legs: dict[str, dict[str, Any]] = {}
                for leg in leg_order:
                    bundle = control_bundle if leg == "control" else candidate_bundle
                    binary = control_binary if leg == "control" else candidate_binary
                    preflight = expected_preflight[case_id][leg]
                    fixture = bundle["identity"]["fixture_metadata"][case_id]
                    result = _timed_leg(
                        binary, leg, case, config, manifest, roots, overlays[case_id],
                        fixture, preflight, output, round_index, pair_id, leg_order,
                        environment, cpu, args.seed, args.retain_workspaces)
                    legs[leg] = result
                control_result = legs["control"]
                candidate_result = legs["candidate"]
                if (campaign.parity_payload(control_result["stimulus"])
                        != campaign.parity_payload(candidate_result["stimulus"])
                        or control_result["correctness_lines"]
                        != candidate_result["correctness_lines"]):
                    raise campaign.CampaignError(
                        f"timed control/candidate transcript mismatch for {pair_id}"
                    )
                pair_record = {
                    "case": case_id, "configuration": CONFIGURATION_ID,
                    "pair_id": pair_id, "round": round_index,
                    "case_order": schedule_row["case_order"],
                    "leg_order": leg_order, "legs": legs,
                    "parity_passed": True,
                }
                pair_directory = output / "timings" / case_id / f"round-{round_index:02d}"
                pair_directory.mkdir(parents=True, exist_ok=True)
                (pair_directory / "pair_result.json").write_text(
                    json.dumps(pair_record, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")
                campaign.discard_successful_workspaces(list(legs.values()), args.retain_workspaces)
                pair_rows[case_id].append(pair_record)

        if campaign.sha256(manifest_path) != manifest_hash:
            raise campaign.CampaignError("benchmark manifest changed during the pairing campaign")
        if campaign.sha256(Path(__file__).resolve()) != runner_hash:
            raise campaign.CampaignError("pairing runner changed during the campaign")
        if campaign.sha256(Path(campaign.__file__).resolve()) != source_runner_hash:
            raise campaign.CampaignError("campaign helper changed during the pairing campaign")
        for bundle in (control_bundle, candidate_bundle):
            _verify_bundle_files(bundle)
            campaign.validate_fixture_metadata(
                cases, overlays, bundle["identity"]["fixture_metadata"],
                manifest, roots, args.seed)
        for record in fixture_generators.values():
            _verify_file_record(record, "throughput fixture generator")
        post_inputs = campaign.collect_case_inputs(
            cases, manifest, roots, overlays, output / "post-run-inputs")
        if post_inputs != control_bundle["identity"].get("source_identity"):
            raise campaign.CampaignError("source or fixture inputs changed during timing")

        summaries = {
            case_id: summarize_case_pairs(pair_rows[case_id], args.pairs, args.seed)
            for case_id in CASE_IDS
        }
        decision = assess_campaign(
            summaries, args.pairs, preflight_passed=True,
            workload_verified=all(
                bundle["identity"].get("workload_identity", {}).get(case_id, {}).get(
                    "verified") is True
                for bundle in (control_bundle, candidate_bundle)
                for case_id in CASE_IDS
            ),
        )
        report = {
            "status": decision["status"], "identities": identities,
            "preflight_passed": True, "timing_kind": "unprofiled_wall",
            "profile_pass": False,
            "pair_results": pair_rows, "summaries": summaries,
            **decision,
        }
        report_path = output / "pair_campaign_report.json"
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                               encoding="utf-8")
        print(f"pair campaign report: {report_path}")
        return 0 if decision["overall_qualified"] else 2
    except campaign.CampaignError as error:
        print(f"error: {error}", file=sys.stderr)
        if output is not None:
            _persist_failure(output, str(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
