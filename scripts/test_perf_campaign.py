#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Focused fail-closed tests for the performance campaign transcript gate."""

from __future__ import annotations

import importlib.util
import contextlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("perf_campaign.py")
SPEC = importlib.util.spec_from_file_location("perf_campaign_under_test", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
campaign = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(campaign)

MANIFEST = campaign.load_manifest(Path(__file__).with_name("simplification_benchmarks.json"))
CASES = {case["id"]: case for case in MANIFEST["cases"]}
KAT = "STIM_KAT " + " ".join(campaign.XORSHIFT32_KAT)


def codex_summary(payload_fp: str = "0000000000001111") -> str:
    return (
        "STIM_SUMMARY instance=0 scenario=0 case=codex_mode0_reference mode=0 "
        "input_frames=1 payload_count=1 payload_fnv64=" + payload_fp +
        " corruption_count=1 corruption_fnv64=0000000000002222 "
        "ready_cycles=1 ready_fnv64=0000000000003333 "
        "accepted_inputs=1 accepted_fnv64=0000000000004444"
    )


def codex_output(summary: str | None = None, event_sequence: tuple[int, ...] = (0,)) -> str:
    lines = [
        "STIM_EVENT instance=0 scenario=0 kind=payload seq=0 data=01",
        "STIM_EVENT instance=0 scenario=0 kind=corruption seq=0 position=0 magnitude=01",
        "STIM_EVENT instance=0 scenario=0 kind=ready seq=0 cycle=0 value=1",
        "STIM_EVENT instance=0 scenario=0 kind=accepted seq=0 cycle=0 index=0 data=01",
    ]
    if event_sequence != (0,):
        lines = [
            f"STIM_EVENT instance=0 scenario=0 kind=payload seq={seq} data=01"
            for seq in event_sequence
        ] + lines[1:]
    lines.extend([KAT, summary or codex_summary(),
                  "PASS: reference decoder mode 0 processed 1 input frame(s)"])
    return "\n".join(lines)


def verify_codex(output: str, *, preflight: bool = True,
                 expected_summaries: list[str] | None = None,
                 expected_correctness: list[str] | None = None) -> dict:
    result = {"engine": "fsim", "case": "codex_reference_mode0_frames1",
              "combined_output": output}
    campaign.verify_correctness(
        CASES[result["case"]], result, True, preflight,
        fixture_metadata={"metadata": {"mode": 0, "input_frames": 1}},
        expected_timed_summaries=expected_summaries,
        expected_timed_correctness_lines=expected_correctness,
    )
    return result


def timed_codex_output(summary: str | None = None) -> str:
    return "\n".join([summary or codex_summary(),
                      "PASS: reference decoder mode 0 processed 1 input frame(s)"])


def codec_fixture_metadata() -> dict:
    rows = []
    scenario_counts = {
        0: (11, 0, 0, 0, 0, 11, 15),
        1: (11, 2, 2, 0, 0, 11, 15),
        2: (11, 2, 2, 0, 0, 11, 15),
        3: (11, 2, 0, 2, 1, 11, 15),
        4: (11, 2, 1, 1, 0, 11, 15),
        7: (11, 2, 1, 1, 0, 11, 15),
        5: (5, 2, 2, 0, 0, 5, 9),
    }
    for tag, counts in scenario_counts.items():
        rows.append({"tag": tag, "expected_counts": dict(zip(
            ("payload_count", "corruption_count", "error_count", "erasure_count",
             "zero_erasure_count", "encoder_accept_count", "decoder_accept_count"),
            counts))})
    return {"metadata": {"scenarios": rows}}


def codec_output() -> str:
    lines = []
    for tag in campaign.EXPECTED_CODEC_SCENARIOS:
        lines.extend([
            f"STIM_EVENT instance=0 scenario={tag} kind=payload seq=0 data=01",
            f"STIM_EVENT instance=0 scenario={tag} kind=corruption seq=0 position=0 magnitude=01",
            f"STIM_EVENT instance=0 scenario={tag} kind=backpressure seq=0 cycle=0 value=1",
            f"STIM_EVENT instance=0 scenario={tag} kind=accepted_encoder seq=0 cycle=0 data=01",
        ])
    for tag, expected in zip(campaign.EXPECTED_CODEC_SCENARIOS,
                             codec_fixture_metadata()["metadata"]["scenarios"]):
        counts = expected["expected_counts"]
        lines.append(
            f"STIM_SUMMARY instance=0 scenario={tag} payload_count={counts['payload_count']} "
            f"corruption_count={counts['corruption_count']} error_count={counts['error_count']} "
            f"erasure_count={counts['erasure_count']} zero_erasure_count={counts['zero_erasure_count']} "
            f"ready_cycles=1 ready_low_cycles=0 output_stall_cycles=0 "
            f"encoder_accept_count={counts['encoder_accept_count']} "
            f"decoder_accept_count={counts['decoder_accept_count']} "
            "encode_timeout_count=0 decode_timeout_count=0 xz_count=0 event_overflow_count=0 "
            "payload_fp=0000000000000001 corruption_fp=0000000000000002 "
            "ready_fp=0000000000000003 accepted_fp=0000000000000004"
        )
    lines.extend([KAT, *(f"PASS [scenario={tag}]" for tag in campaign.EXPECTED_CODEC_SCENARIOS),
                  "CODEC_OK [RS15_11]", "ALL_CODEC_DONE"])
    return "\n".join(lines)


def throughput_fixture_metadata() -> dict:
    return {"metadata": {
        "configuration": {
            "n": 255, "k": 223, "mode": 0, "codewords_per_instance": 2,
            "instances": [
                {"instance": f"u{index}", "stream_id": index, "num_kes": kes}
                for index, kes in enumerate((1, 2, 4, 8))
            ],
        },
        "corruption": {"count_per_codeword": 16},
    }}


def throughput_output(duplicate_summary: bool = False) -> str:
    lines = []
    for instance, kes in enumerate((1, 2, 4, 8)):
        lines.extend([
            f"STIM_EVENT instance={instance} scenario=0 kind=payload seq=0 data=01",
            f"STIM_EVENT instance={instance} scenario=0 kind=corruption seq=0 position=0 magnitude=01",
            f"STIM_EVENT instance={instance} scenario=0 kind=ready seq=0 cycle=0 value=1",
            f"STIM_EVENT instance={instance} scenario=0 kind=accepted seq=0 cycle=0 index=0 data=01",
        ])
    for instance in range(4):
        lines.append(
            f"STIM_SUMMARY instance={instance} scenario=0 payload_count=446 "
            "payload_fp=0000000000000001 corruption_count=32 "
            "corruption_fp=0000000000000002 ready_policy=constant_1 "
            "ready_count=10 ready_ones=10 ready_zeros=0 ready_fp=0000000000000003 "
            "accepted_count=510 accepted_fp=0000000000000004 output_count=510 "
            "invalid_samples=0 errs=0"
        )
    if duplicate_summary:
        lines.append(lines[-1])
    lines.extend([KAT, "THRU TESTS COMPLETE"])
    lines.extend(f"THRU [N255_m0_k{kes}] cw=2 cycles=20 errs=0 PASS"
                 for kes in (1, 2, 4, 8))
    return "\n".join(lines)


class ProfileEnvironmentTests(unittest.TestCase):
    def test_llvm_module_option_is_isolated_from_preflight_and_timing(self) -> None:
        with patch.dict(os.environ, {"FSIM_PROFILE_LLVM_MODULES": "1"}):
            campaign_environment, removed = campaign.stable_environment()

        self.assertIn("FSIM_PROFILE_LLVM_MODULES", removed)
        self.assertNotIn("FSIM_PROFILE_LLVM_MODULES", campaign_environment)
        timed_environment = campaign_environment.copy()
        preflight_environment = campaign_environment.copy()

        profile_environment = campaign.fsim_profile_environment(
            campaign_environment, profile_llvm_modules=True)

        self.assertNotIn("FSIM_PROFILE_LLVM_MODULES", timed_environment)
        self.assertNotIn("FSIM_PROFILE_LLVM_MODULES", preflight_environment)
        self.assertEqual(profile_environment["FSIM_PROFILE_LLVM_MODULES"], "1")
        self.assertEqual(profile_environment["FSIM_PROFILE_PHASES"], "1")
        self.assertEqual(profile_environment["FSIM_PROFILE_JIT"], "1")
        self.assertNotIn("FSIM_PROFILE_LLVM_MODULES", campaign_environment)

    def test_llvm_module_flag_requires_profile_mode(self) -> None:
        error_output = io.StringIO()
        with contextlib.redirect_stderr(error_output):
            result = campaign.main(["--manifest", str(campaign.DEFAULT_MANIFEST),
                                    "--profile-llvm-modules"])
        self.assertEqual(result, 1)
        self.assertIn("--profile-llvm-modules requires --profile", error_output.getvalue())


class TranscriptGateTests(unittest.TestCase):
    def test_codex_summary_counts_and_fingerprints_are_checked(self) -> None:
        with self.assertRaisesRegex(campaign.CampaignError, "count differs from stimulus events"):
            verify_codex(codex_output().replace("payload_count=1", "payload_count=999"))
        with self.assertRaisesRegex(campaign.CampaignError, "invalid payload fingerprint"):
            verify_codex(codex_output().replace("payload_fnv64=0000000000001111",
                                                 "payload_fnv64=zzzz"))

    def test_missing_shape_metadata_cannot_qualify_original_workload(self) -> None:
        case = CASES["codex_throughput_default"]
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "tb.sv"
            source.write_text(
                "localparam integer N=255; localparam integer K=223; "
                "localparam integer FRAMES=12; parameter integer MODE=1; "
                "parameter integer NUM_KES=1; parameter bit VERIFY=1'b1; "
                "parameter bit ERASURES=1'b1;\n", encoding="utf-8")
            identity = campaign.original_workload_identity(
                case, {"metadata": {"frames": 1}, "testbench": str(source)}, source)
        self.assertFalse(identity["verified"])
        self.assertTrue(any("FRAMES" in reason for reason in identity["reasons"]))

    def test_reduced_workloads_never_receive_full_matrix_scope(self) -> None:
        self.assertEqual(
            campaign.completion_scope(True, False),
            "ten_case_diagnostic_reduced_inputs",
        )
        self.assertEqual(
            campaign.completion_scope(True, True),
            "all_ten_reference_cases_original_workloads",
        )

    def test_missing_and_duplicate_required_summary_fail(self) -> None:
        output = codex_output()
        summary = codex_summary()
        with self.assertRaises(campaign.CampaignError):
            verify_codex(output.replace(summary + "\n", ""))
        with self.assertRaises(campaign.CampaignError):
            verify_codex(output.replace(summary, summary + "\n" + summary))

    def test_duplicate_summary_identity_fails_even_with_expected_total_count(self) -> None:
        case = CASES["original_throughput"]
        result = {"engine": "fsim", "case": case["id"],
                  "combined_output": throughput_output(duplicate_summary=True)}
        with self.assertRaisesRegex(campaign.CampaignError, "duplicate instance/scenario"):
            campaign.verify_correctness(case, result, True, True,
                                        fixture_metadata=throughput_fixture_metadata())

    def test_same_stream_sequence_reorder_fails(self) -> None:
        with self.assertRaisesRegex(campaign.CampaignError, "strictly increasing"):
            verify_codex(codex_output(event_sequence=(1, 0)))

    def test_unknown_xz_and_invalid_markers_fail(self) -> None:
        with self.assertRaisesRegex(campaign.CampaignError, "unknown X/Z"):
            verify_codex(codex_output().replace("data=01", "data=0x", 1))
        with self.assertRaisesRegex(campaign.CampaignError, "invalid-sample"):
            verify_codex(codex_output() + "\nSTIM_INVALID instance=0 count=1")
        with self.assertRaisesRegex(campaign.CampaignError, "invalid stimulus data"):
            verify_codex(codex_output().replace("kind=payload", "kind=invalid", 1))

    def test_duplicate_or_extra_known_answer_record_fails(self) -> None:
        with self.assertRaisesRegex(campaign.CampaignError, "exactly one STIM_KAT"):
            verify_codex(codex_output() + "\n" + KAT)
        with self.assertRaisesRegex(campaign.CampaignError, "exactly match"):
            verify_codex(codex_output().replace(KAT, KAT + " deadbeef"))

    def test_timed_summary_must_match_preflight(self) -> None:
        timed = timed_codex_output()
        with self.assertRaisesRegex(campaign.CampaignError, "summary fingerprints differ"):
            verify_codex(timed, preflight=False,
                         expected_summaries=[codex_summary(payload_fp="000000000000ffff")],
                         expected_correctness=[
                             "PASS: reference decoder mode 0 processed 1 input frame(s)"])

    def test_timed_correctness_must_match_preflight(self) -> None:
        timed = timed_codex_output()
        with self.assertRaisesRegex(campaign.CampaignError, "correctness result lines differ"):
            verify_codex(timed, preflight=False, expected_summaries=[codex_summary()],
                         expected_correctness=["different correctness output"])

    def test_pair_gate_compares_full_correctness_lines(self) -> None:
        transcript = campaign.canonical_transcript(codex_output(), True)
        pair = {
            "fsim": {"stimulus": transcript, "correctness_lines": ["PASS fsim"]},
            "vivado": {"stimulus": transcript, "correctness_lines": ["PASS vivado"]},
        }
        with self.assertRaisesRegex(campaign.CampaignError, "correctness result lines differ"):
            campaign.ensure_pair_parity(pair, "codex", "timed")

    def test_codec_zero_timeout_counters_are_not_failure_markers(self) -> None:
        case = CASES["original_codec"]
        result = {"engine": "vivado", "case": case["id"],
                  "combined_output": codec_output()}
        transcript = campaign.verify_correctness(
            case, result, True, True, fixture_metadata=codec_fixture_metadata())
        self.assertEqual(transcript["codec_tags"], campaign.EXPECTED_CODEC_SCENARIOS)
        self.assertEqual(len(transcript["summaries"]), 7)

    def test_throughput_summaries_match_all_expected_instances_and_counts(self) -> None:
        case = CASES["original_throughput"]
        result = {"engine": "vivado", "case": case["id"],
                  "combined_output": throughput_output()}
        transcript = campaign.verify_correctness(
            case, result, True, True, fixture_metadata=throughput_fixture_metadata())
        self.assertEqual(len(transcript["summaries"]), 4)
        self.assertEqual({identity[0] for identity in transcript["summary_identities"]},
                         {"0", "1", "2", "3"})

    def test_one_pair_has_no_confidence_interval(self) -> None:
        rows = [
            {"case": "original_codec", "configuration": "llvm_o2", "engine": engine,
             "sample": 1, "elapsed_seconds": elapsed, "peak_rss_kib": 100}
            for engine, elapsed in (("fsim", 3.0), ("vivado", 7.0))
        ]
        summary = campaign.summarize_samples(rows)[0]
        self.assertTrue(summary["fsim_median_below_vivado"])
        self.assertIsNone(summary["paired_speedup_95pct_bootstrap_ci"])
        self.assertFalse(summary["noise_clear"])

    def test_full_throughput_identity_checks_each_instance_mode(self) -> None:
        instances = [("u0", 0, 1), ("u1", 0, 2), ("u2", 0, 4),
                     ("u3", 0, 8), ("u4", 1, 1), ("u5", 1, 2)]
        header = ("parameter integer N = 255;\nparameter integer K = 223;\n"
                  "localparam integer NCW = 12;\nlocalparam integer NM = 6;\n")
        body = "".join(
            f"rs_thru #(.N(255), .K(223), .IMPL_MODE({mode}), "
            f".NUM_KES({kes})) {name} (.clk(clk));\n"
            for name, mode, kes in instances
        )
        metadata = {"configuration": {
            "n": 255, "k": 223, "codewords_per_instance": 12,
            "mode": "per_instance", "instances": [
                {"instance": name, "mode": mode, "num_kes": kes}
                for name, mode, kes in instances
            ],
        }}
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.v"
            overlay = Path(directory) / "overlay.v"
            source.write_text(header + body, encoding="utf-8")
            overlay.write_text(header + body, encoding="utf-8")
            record = {"metadata": metadata, "testbench": str(overlay)}
            self.assertTrue(campaign.original_workload_identity(
                CASES["original_throughput"], record, source)["verified"])
            overlay.write_text(header + body.replace(".IMPL_MODE(1), .NUM_KES(2)",
                                                     ".IMPL_MODE(0), .NUM_KES(2)"),
                               encoding="utf-8")
            self.assertFalse(campaign.original_workload_identity(
                CASES["original_throughput"], record, source)["verified"])


class NativeCacheGateTests(unittest.TestCase):
    def test_native_cache_path_is_workspace_owned(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "sample" / "workspace"
            self.assertEqual(
                campaign.fsim_native_cache_path(workspace),
                workspace / ".fsim" / "cache" / "llvm-native",
            )

    def test_fresh_jit_cache_is_empty_before_simulation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory) / "workspace"
            cache = campaign.fsim_native_cache_path(workspace)
            self.assertFalse(cache.exists())
            before_elaboration = campaign.native_cache_snapshot(cache)
            campaign.require_native_cache_empty(
                before_elaboration, "before elaboration", "fixture")
            before_simulation = campaign.native_cache_snapshot(cache)
            campaign.require_native_cache_empty(
                before_simulation, "before JIT simulation", "fixture")
            record = campaign.native_cache_run_record(
                cache, False, before_elaboration, before_simulation,
                before_simulation, 1.25)
            self.assertTrue(record["jit_cold_before_simulation"])
            self.assertEqual(record["policy"], "jit_cold")

            object_path = cache / "llvm" / "objects" / "ab" / "abcd.fobj"
            object_path.parent.mkdir(parents=True)
            object_path.write_bytes(b"native cache object")
            populated = campaign.native_cache_snapshot(cache)
            with self.assertRaisesRegex(
                    campaign.CampaignError, "before JIT simulation"):
                campaign.require_native_cache_empty(
                    populated, "before JIT simulation", "fixture")

    def test_interrupted_native_object_write_is_not_treated_as_cold(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            cache = campaign.fsim_native_cache_path(Path(directory) / "workspace")
            temporary_object = cache / "llvm" / "objects" / "ab" / "abcd.fobj.tmp.1"
            temporary_object.parent.mkdir(parents=True)
            temporary_object.write_bytes(b"partial")
            snapshot = campaign.native_cache_snapshot(cache)
            self.assertEqual(snapshot["native_object_count"], 1)
            with self.assertRaises(campaign.CampaignError):
                campaign.require_native_cache_empty(
                    snapshot, "before JIT simulation", "fixture")

    def test_receipt_without_native_object_breaks_jit_cold_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            cache = campaign.fsim_native_cache_path(Path(directory) / "workspace")
            before_elaboration = campaign.native_cache_snapshot(cache)
            receipt = cache / "aot-receipts" / "stale.receipt"
            receipt.parent.mkdir(parents=True)
            receipt.write_text("stale", encoding="utf-8")
            before_simulation = campaign.native_cache_snapshot(cache)
            self.assertEqual(before_simulation["native_object_count"], 0)
            self.assertEqual(before_simulation["aot_receipt_count"], 1)
            record = campaign.native_cache_run_record(
                cache, False, before_elaboration, before_simulation,
                before_simulation, 1.25)
            self.assertFalse(record["jit_cold_before_simulation"])
            with self.assertRaisesRegex(
                    campaign.CampaignError, "native objects or AOT receipts"):
                campaign.require_native_cache_empty(
                    before_simulation, "before JIT simulation", "fixture")

    def test_aot_objects_are_recorded_as_elaboration_cost(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            cache = campaign.fsim_native_cache_path(Path(directory) / "workspace")
            before_elaboration = campaign.native_cache_snapshot(cache)
            campaign.require_native_cache_empty(
                before_elaboration, "before AOT elaboration", "fixture")
            object_path = cache / "llvm" / "objects" / "ab" / "abcd.fobj"
            object_path.parent.mkdir(parents=True)
            object_path.write_bytes(b"AOT native object")
            receipt = cache / "aot-receipts" / "fixture.receipt"
            receipt.parent.mkdir(parents=True)
            receipt.write_text("receipt", encoding="utf-8")
            before_simulation = campaign.native_cache_snapshot(cache)
            record = campaign.native_cache_run_record(
                cache, True, before_elaboration, before_simulation,
                before_simulation, 12.5)
            self.assertEqual(record["policy"], "aot_precompiled")
            self.assertFalse(record["jit_cold_before_simulation"])
            self.assertEqual(
                record["aot_precompilation"]["elapsed_phase"], "elaborate")
            self.assertTrue(record["aot_precompilation"]["included_in_total"])
            self.assertEqual(
                record["aot_precompilation"]["elaborate_phase_seconds"], 12.5)
            self.assertEqual(
                record["aot_precompilation"]["native_object_count_before_simulation"],
                1)
            self.assertEqual(
                record["aot_precompilation"]["receipt_count_before_simulation"], 1)

    def test_workspace_commands_do_not_pass_ignored_cache_override(self) -> None:
        case = {"top": "fixture"}
        group = {"language": "verilog", "standard": None, "includes": [],
                 "defines": [], "sources": ["fixture.v"]}
        base = campaign.fsim_phase_commands(
            Path("fsim"), case, {"optimization": "O2", "aot": False},
            [group], 123, 1000)
        aot = campaign.fsim_phase_commands(
            Path("fsim"), case, {"optimization": "O2", "aot": True},
            [group], 123, 1000)
        for commands in (base, aot):
            self.assertNotIn("--cache", commands[1])
            self.assertNotIn("--cache", commands[2])
        self.assertIn("--no-aot", base[1])
        self.assertIn("--aot", aot[1])


class FrozenDependencyIdentityTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.fsim = self.root / "fsim"
        self.fsim.write_bytes(b"frozen simulator fixture")
        self.executable = {"path": str(self.fsim), "sha256": campaign.sha256(self.fsim)}
        self.dependency = {"path": str(self.root / "libfixture.so"), "sha256": "a" * 64}
        self.observed = {"dependencies": [self.dependency.copy()]}
        self.identity = {"executable": self.executable,
                         "linked_dependencies": [self.dependency.copy()]}

    def write_identity(self) -> None:
        (self.root / "identity.json").write_text(json.dumps(self.identity), encoding="utf-8")

    def test_complete_manifest_verifies_observed_dependencies(self) -> None:
        self.write_identity()
        result = campaign.frozen_build_identity(self.fsim, self.observed)
        self.assertTrue(result["verified"])
        self.assertTrue(result["dependencies_verified"])
        self.assertEqual(result["dependency_count"], 1)
        self.assertEqual(result["dependency_field"], "linked_dependencies")

    def test_missing_dependencies_fail_before_a_campaign(self) -> None:
        del self.identity["linked_dependencies"]
        self.write_identity()
        with self.assertRaisesRegex(campaign.CampaignError, "requires a nonempty dependency"):
            campaign.frozen_build_identity(self.fsim, self.observed)

    def test_empty_or_malformed_dependencies_fail(self) -> None:
        for dependencies in ([], {}, [None], [{"path": "/library"}],
                             [{"path": "/library", "sha256": "invalid"}]):
            with self.subTest(dependencies=dependencies):
                self.identity["linked_dependencies"] = dependencies
                self.write_identity()
                with self.assertRaises(campaign.CampaignError):
                    campaign.frozen_build_identity(self.fsim, self.observed)

    def test_duplicate_dependency_paths_fail(self) -> None:
        self.identity["linked_dependencies"].append(self.dependency.copy())
        self.write_identity()
        with self.assertRaisesRegex(campaign.CampaignError, "duplicate path"):
            campaign.frozen_build_identity(self.fsim, self.observed)

    def test_changed_or_missing_observed_dependency_fails(self) -> None:
        self.write_identity()
        changed = {**self.dependency, "sha256": "b" * 64}
        for observed in ({"dependencies": [changed]}, {"dependencies": []}):
            with self.subTest(observed=observed):
                with self.assertRaisesRegex(campaign.CampaignError, "do not match"):
                    campaign.frozen_build_identity(self.fsim, observed)
        with self.assertRaisesRegex(campaign.CampaignError, "requires observed"):
            campaign.frozen_build_identity(self.fsim)

    def test_legacy_nested_dependencies_are_verified_without_rewriting_identity(self) -> None:
        del self.identity["linked_dependencies"]
        self.executable["dependencies"] = [self.dependency.copy()]
        self.write_identity()
        original = (self.root / "identity.json").read_bytes()
        result = campaign.frozen_build_identity(self.fsim, self.observed)
        self.assertTrue(result["dependencies_verified"])
        self.assertEqual(result["dependency_field"], "executable.dependencies")
        self.assertEqual((self.root / "identity.json").read_bytes(), original)
        self.observed["dependencies"][0]["sha256"] = "b" * 64
        with self.assertRaisesRegex(campaign.CampaignError, "do not match"):
            campaign.frozen_build_identity(self.fsim, self.observed)

    def test_invalid_top_level_dependencies_do_not_fall_back_to_legacy_field(self) -> None:
        self.identity["linked_dependencies"] = []
        self.executable["dependencies"] = [self.dependency.copy()]
        self.write_identity()
        with self.assertRaisesRegex(campaign.CampaignError, "requires a nonempty dependency"):
            campaign.frozen_build_identity(self.fsim, self.observed)


class PreflightReuseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prior = self.root / "prior"
        self.current = self.root / "current"
        self.case = CASES["codex_reference_mode0_frames1"]
        self.config = {"id": "llvm_o2", "engine": "compiled", "optimization": "O2", "aot": False}
        self.fixture = {"metadata": {"mode": 0, "input_frames": 1}}
        self.environment = {"LANG": "C", "TZ": "UTC"}

    def identities(self, root: Path) -> dict:
        tools = {name: {"path": "/tools/" + name, "sha256": name,
                        "dependencies": [{"path": "/lib/a.so", "sha256": "library"}],
                        "wrapper_files": []}
                 for name in ("fsim", "xvlog", "xvhdl", "xelab", "xsim")}
        return {
            "simulator_binaries": tools, "manifest_sha256": "manifest",
            "system": {"cpu_affinity": [0], "cpu_model": "test"},
            "benchmark_configuration": {"cpu": 0, "waveforms": False},
            "source_identity": {self.case["id"]: {
                "groups": [{"sources": ["/rtl/a/input.sv", str(root / "fixtures" / "tb.sv")],
                            "source_hashes": ["rtl", "testbench"],
                            "include_dirs": ["/rtl/include"], "include_inputs": []}],
                "all_inputs": [{"path": "/rtl/a/input.sv", "sha256": "rtl"},
                               {"path": str(root / "fixtures" / "tb.sv"), "sha256": "testbench"}],
            }},
            "fixture_metadata": {self.case["id"]: {
                **self.fixture, "testbench": str(root / "fixtures" / "tb.sv"),
                "testbench_sha256": "testbench", "metadata_sha256": str(root),
            }},
            "stimulus": {"seed": "0x6d2b79f5", "algorithm": campaign.XORSHIFT32_VERSION},
            "campaign_code": {"runner": {"path": "/scripts/perf_campaign.py", "sha256": "runner"}},
        }

    def key(self, engine: str, root: Path, document: dict | None = None) -> dict:
        return campaign.preflight_identity(
            engine, self.case, self.config, document or self.identities(root), root,
            self.environment)

    def create_proof(self, engine: str, identity: dict | None = None) -> dict:
        directory = self.prior / "preflight" / self.case["id"] / engine
        directory.mkdir(parents=True, exist_ok=True)
        phases = {}
        for phase in ("compile", "elaborate", "simulate"):
            stdout = directory / (phase + ".stdout")
            stderr = directory / (phase + ".stderr")
            stdout.write_text(codex_output() if phase == "simulate" else "")
            stderr.write_text("")
            phases[phase] = {"returncode": 0, "stdout": str(stdout), "stderr": str(stderr)}
        phases["compile"] = [phases["compile"]]
        result = {
            "case": self.case["id"], "engine": engine, "configuration": self.config["id"],
            "preflight": True, "phases": phases, "combined_output": codex_output(),
            "elapsed_seconds": 123.0, "peak_rss_kib": 100, "phase_seconds": {"compile": 1.0},
            "workspace": str(directory / "workspace"),
            "preflight_identity": identity or self.key(engine, self.prior),
        }
        transcript = campaign.verify_correctness(self.case, result, True, True, self.fixture)
        campaign.persist_transcript(directory, result, transcript)
        campaign.persist_engine_result(directory, result)
        report = {"passed_preflight": True,
                  "identities": {"post_run_identity_check": {"passed": True}},
                  "preflight_results": [result]}
        # Avoid serializing the full test-only combined output into the receipt's report.
        (self.prior / "campaign_report.json").write_text(json.dumps({
            key: value for key, value in report.items() if key != "preflight_results"}))
        campaign.write_preflight_proofs(self.prior, report)
        return result

    def reuse(self, engine: str = "fsim", identity: dict | None = None):
        return campaign.reuse_preflight_result(
            self.prior, engine, self.case, self.config,
            identity or self.key(engine, self.current), True, self.fixture)

    def test_logical_fixture_relocation_preserves_identity_but_external_roles_do_not(self) -> None:
        self.assertEqual(self.key("fsim", self.prior), self.key("fsim", self.current))
        changed = self.identities(self.current)
        changed["source_identity"][self.case["id"]]["groups"][0]["sources"][0] = "/rtl/b/input.sv"
        self.assertNotEqual(self.key("fsim", self.prior), self.key("fsim", self.current, changed))

    def test_engine_dependency_source_seed_and_environment_changes_invalidate(self) -> None:
        for change in ("binary", "dependency", "source", "seed", "environment", "cpu", "context", "config"):
            with self.subTest(change=change):
                document = self.identities(self.current)
                config = dict(self.config)
                env = dict(self.environment)
                if change == "binary":
                    document["simulator_binaries"]["fsim"]["sha256"] = "new"
                elif change == "dependency":
                    document["simulator_binaries"]["fsim"]["dependencies"][0]["sha256"] = "new"
                elif change == "source":
                    document["source_identity"][self.case["id"]]["groups"][0]["source_hashes"][0] = "new"
                elif change == "seed":
                    document["stimulus"]["seed"] = "0x1"
                elif change == "environment":
                    env["FSIM_OPTION"] = "new"
                elif change == "cpu":
                    document["system"]["cpu_affinity"] = [1]
                elif change == "context":
                    document["system"]["execution_context"] = {"namespaces": {"pid": "new"}}
                else:
                    config["optimization"] = "O0"
                key = campaign.preflight_identity("fsim", self.case, config, document, self.current, env)
                self.assertNotEqual(self.key("fsim", self.prior), key)
        document = self.identities(self.current)
        document["simulator_binaries"]["fsim"]["sha256"] = "new"
        self.assertEqual(self.key("vivado", self.prior), self.key("vivado", self.current, document))

    def test_reuse_reparses_evidence_and_keeps_historical_time_out_of_current_time(self) -> None:
        self.create_proof("fsim")
        result, reason = self.reuse()
        self.assertIsNotNone(result, reason)
        self.assertIsNone(result["elapsed_seconds"])
        self.assertEqual(result["phase_seconds"], {})
        self.assertEqual(result["preflight_reuse"]["historical_elapsed_seconds"], 123.0)
        self.assertEqual(result["stimulus"]["known_answer_words"], list(campaign.XORSHIFT32_KAT))

    def test_legacy_missing_changed_and_corrupt_proofs_fall_back(self) -> None:
        result, reason = self.reuse()
        self.assertIsNone(result)
        self.create_proof("fsim")
        changed = self.key("fsim", self.current)
        changed["stimulus"]["seed"] = "0x1"
        self.assertIsNone(self.reuse(identity=changed)[0])
        result_path = self.prior / "preflight" / self.case["id"] / "fsim" / "simulate.stdout"
        result_path.write_text(result_path.read_text() + "corrupted\n")
        self.assertIsNone(self.reuse()[0])

    def test_reparse_rejects_bad_transcript_even_if_its_hash_is_refreshed(self) -> None:
        result = self.create_proof("fsim")
        Path(result["transcript_canonical"]).write_text("{}")
        campaign.write_preflight_proofs(self.prior, {"preflight_results": [result]})
        reused, reason = self.reuse()
        self.assertIsNone(reused)
        self.assertIn("reparsed canonical", reason)

    def test_invalid_proof_shape_and_missing_phase_file_fall_back(self) -> None:
        self.create_proof("fsim")
        directory = self.prior / "preflight" / self.case["id"] / "fsim"
        receipt = directory / "reuse-proof.json"
        original = receipt.read_text()
        receipt.write_text("[]")
        self.assertIsNone(self.reuse()[0])
        receipt.write_text(original)
        (directory / "compile.stderr").unlink()
        self.assertIsNone(self.reuse()[0])

    def test_reuse_still_compares_complete_pair_and_preserves_source_workspaces(self) -> None:
        self.create_proof("fsim")
        self.create_proof("vivado")
        with patch.object(campaign, "run_engine", side_effect=AssertionError("unexpected simulator")), \
                patch.object(campaign, "discard_successful_workspaces") as discard, \
                contextlib.redirect_stdout(io.StringIO()):
            results, expected = campaign.run_preflight(
                [self.case], self.config, Path("/fsim"), {}, {}, {},
                {self.case["id"]: Path("/tb")}, self.current, self.environment, 0, 1,
                {self.case["id"]: self.fixture}, False, self.identities(self.current), self.prior)
        self.assertEqual(len(results), 2)
        self.assertEqual(expected[self.case["id"]]["fsim"], expected[self.case["id"]]["vivado"])
        discard.assert_called_once_with([], False)
        verdict = json.loads((self.current / "preflight" / self.case["id"] / "pair_verdict.json").read_text())
        self.assertTrue(verdict["passed"])
        for engine in ("fsim", "vivado"):
            self.assertEqual(Path(verdict["engine_results"][engine]),
                             self.current / "preflight" / self.case["id"] / engine / "engine_result.json")

    def test_changed_fsim_runs_fresh_while_vivado_reuses_and_pair_parity_still_gates(self) -> None:
        self.create_proof("fsim")
        self.create_proof("vivado")
        document = self.identities(self.current)
        document["simulator_binaries"]["fsim"]["sha256"] = "new-candidate"

        def fresh_engine(engine, fsim, vivado, case, config, manifest, roots,
                         overlay, inputs, directory, environment, cpu, seed, **keywords):
            self.assertEqual(engine, "fsim")
            self.assertEqual(directory, self.current / "preflight" / self.case["id"] / "fsim")
            self.assertTrue(keywords["preflight"])
            directory.mkdir(parents=True)
            return {"engine": engine, "case": case["id"], "configuration": config["id"],
                    "workspace": str(directory / "workspace"), "combined_output": codex_output(
                        codex_summary("000000000000ffff"))}

        with patch.object(campaign, "run_engine", side_effect=fresh_engine) as fresh, \
                contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(campaign.CampaignError):
                campaign.run_preflight(
                    [self.case], self.config, Path("/fsim"), {}, {}, {},
                    {self.case["id"]: Path("/tb")}, self.current, self.environment, 0, 1,
                    {self.case["id"]: self.fixture}, False, document, self.prior)
        self.assertEqual(fresh.call_count, 1)
        verdict = json.loads((self.current / "preflight" / self.case["id"] / "pair_verdict.json").read_text())
        self.assertFalse(verdict["passed"])


if __name__ == "__main__":
    unittest.main()
