#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Focused tests for control/candidate throughput pairing invariants."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).with_name("perf_pair_campaign.py")
SPEC = importlib.util.spec_from_file_location("perf_pair_campaign_under_test", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
pairing = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pairing)


def timing_pair(case: str, pair_id: str, round_number: int,
                control: float, candidate: float) -> dict:
    def leg(label: str, seconds: float) -> dict:
        return {
            "leg": label, "pair_id": pair_id, "round": round_number,
            "case": case, "configuration": pairing.CONFIGURATION_ID,
            "elapsed_seconds": seconds,
            "phase_seconds": {
                "compile": seconds * 0.1, "elaborate": seconds * 0.2,
                "native_setup_and_simulation": seconds * 0.7,
            },
            "peak_rss_kib": 1000 + round_number,
        }
    return {
        "case": case, "pair_id": pair_id, "round": round_number,
        "legs": {"control": leg("control", control),
                 "candidate": leg("candidate", candidate)},
    }


def preflight_identity(label: str) -> dict:
    throughput_metadata = {
        "schema_version": 1,
        "fixture": "full_original_throughput",
        "source": {"path": "/src/tb/rs_thru_tb.v", "sha256": "source-hash"},
        "generated_testbench": {"path": "rs_thru_tb.v", "sha256": "fixture-hash"},
        "configuration": {
            "root_seed": "0x0000002a",
            "instances": [{"instance": "u0", "stream_id": 0, "num_kes": 1}],
        },
        "random": {"algorithm": "xorshift32-shift13-17-5-v1"},
    }
    fixture = {
        "generator": "/src/perf_throughput_fixture.py",
        "generator_sha256": "generator-hash",
        "source": "/src/tb/rs_thru_tb.v",
        "source_sha": "source-hash",
        "testbench": "/evidence/fixtures/rs_thru_tb.v",
        "testbench_sha256": "fixture-hash",
        "metadata_path": "/evidence/fixtures/metadata.json",
        "metadata_sha256": "metadata-hash",
        "metadata": throughput_metadata,
        "seed": "0x0000002a",
        "algorithm": "xorshift32-shift13-17-5-v1",
    }
    return {
        "manifest_sha256": "manifest-hash",
        "cases": list(pairing.CASE_IDS),
        "configurations": [{"id": pairing.CONFIGURATION_ID, "engine": "compiled",
                             "optimization": "O2", "aot": False}],
        "source_roots": {"original": "/src/original", "mixed": "/src/mixed"},
        "source_identity": {
            case: {"groups": [{"language": "systemverilog", "standard": "2017",
                                "sources": [{"path": f"/src/{case}.sv",
                                             "sha256": f"{case}-source-hash"}],
                                "includes": [], "defines": ["SIMULATION"]}]}
            for case in pairing.CASE_IDS
        },
        "fixture_metadata": {case: copy.deepcopy(fixture) for case in pairing.CASE_IDS},
        "stimulus": {
            "algorithm": "xorshift32-shift13-17-5-v1",
            "root_seed": "0x0000002a", "seed": "0x0000002a",
            "derived_stream_seeds": [], "known_answer_seed": "0x6d2b79f5",
            "known_answer_words": ["40aec71f", "91e00c19", "9c0fe128",
                                   "6570f69d", "0fce02cc"],
            "preflight_transcript_required": True,
        },
        "workload_identity": {
            case: {"verified": True, "reasons": [], "source_path": f"/src/{case}.sv",
                   "source_sha256": f"{case}-source-hash"}
            for case in pairing.CASE_IDS
        },
        "campaign_code": {
            "runner": {"path": "/src/perf_campaign.py", "sha256": "runner"},
            "profile_helper": {"path": "/src/perf_campaign_profile.py",
                               "sha256": "profile"},
        },
        "system": {
            "platform": "test-host", "machine": "x86_64", "cpu_model": "test CPU",
            "cpu_affinity": [3], "python": "3.x",
            "execution_context": {"uid": 1000, "gid": 1000,
                                  "boot_id": "test-boot",
                                  "namespaces": {"pid": "test-pid"},
                                  "launch_restrictions": {"NoNewPrivs": "0"}},
        },
        "benchmark_configuration": {
            "cpu_count": 1, "cpu": 3, "waveforms": False,
            "interactive_debugging": False, "fsim_jobs": 1,
            "vivado_xelab_multithreading": "off",
            "environment": {"LANG": "C", "LC_ALL": "C", "TZ": "UTC"},
            "vivado_environment": {"LANG": "C", "LC_ALL": "C", "TZ": "UTC"},
        },
        "run_configuration": {
            "samples": 1, "vivado_settle_seconds": 0.0,
            "preflight_only": True, "profile_pass": False,
            "profile_llvm_modules": False, "waveforms": False,
            "interactive_debugging": False, "launch_delay_included_in_total": True,
            "removed_profile_environment_names": [],
            "fsim_vhdl_compatibility_by_case": dict(pairing.FSIM_VHDL_COMPATIBILITY),
            "workspace_policy": "fresh per engine, case, config, and sample",
            "fsim_native_cache_policy": {
                "path": "<workspace>/.fsim/cache/llvm-native",
                "jit": "require no native objects or AOT receipts before simulate",
                "aot": "require no native objects or AOT receipts before elaborate",
            },
            "successful_workspace_retention": False,
            "reuse_preflight_from": None,
        },
        "auxiliary_tools": {"gnu_time": {"path": "/usr/bin/time", "sha256": "time"}},
        "simulator_binaries": {
            "fsim": {"path": f"/bin/{label}", "sha256": f"{label}-hash"},
            "xvlog": {"path": "/vivado/xvlog", "sha256": "xvlog"},
            "xvhdl": {"path": "/vivado/xvhdl", "sha256": "xvhdl"},
            "xelab": {"path": "/vivado/xelab", "sha256": "xelab"},
            "xsim": {"path": "/vivado/xsim", "sha256": "xsim"},
        },
        "executable_provenance": {"id": label},
    }


def preflight_bundle(label: str, transcript: dict | None = None) -> dict:
    identity = preflight_identity(label)
    canonical = transcript or {"events": [{"seq": 1}], "summaries": ["fp=123"],
                               "known_answer": ["kat"]}
    results = {}
    for case in pairing.CASE_IDS:
        for engine in ("fsim", "vivado"):
            results[(case, engine)] = {
                "canonical_transcript": copy.deepcopy(canonical),
                "correctness_lines": ["PASS"],
            }
    return {"identity": identity, "results": results}


def candidate_overlay_fixture(control_fixture: dict) -> dict:
    candidate = copy.deepcopy(control_fixture)
    candidate.pop("generator", None)
    candidate.pop("generator_sha256", None)
    candidate["source_path"] = candidate.pop("source")
    candidate["source_sha256"] = candidate.pop("source_sha")
    # load_overlay_metadata derives these values from different metadata keys;
    # the complete parsed metadata below is the stable identity source.
    candidate["seed"] = None
    candidate["algorithm"] = None
    return candidate


class PairScheduleTests(unittest.TestCase):
    def test_rounds_randomize_case_and_executable_order_deterministically(self) -> None:
        first = pairing.randomized_schedule(7, 0x6D2B79F5)
        second = pairing.randomized_schedule(7, 0x6D2B79F5)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 7)
        case_orders = [tuple(row["case_order"]) for row in first]
        self.assertEqual(set(case_orders), {pairing.CASE_IDS, pairing.CASE_IDS[::-1]})
        self.assertIn(abs(case_orders.count(pairing.CASE_IDS)
                          - case_orders.count(pairing.CASE_IDS[::-1])), (0, 1))
        for case in pairing.CASE_IDS:
            orders = [tuple(row["leg_order_by_case"][case]) for row in first]
            self.assertEqual(set(orders), {("control", "candidate"),
                                           ("candidate", "control")})
            self.assertIn(abs(orders.count(("control", "candidate"))
                              - orders.count(("candidate", "control"))), (0, 1))
        for row in first:
            self.assertEqual(set(row["case_order"]), set(pairing.CASE_IDS))

    def test_cpu_is_required_and_cannot_select_cpu_zero(self) -> None:
        with self.assertRaises(SystemExit):
            pairing.parse_arguments([
                "--control", "/bin/control", "--candidate", "/bin/candidate",
                "--control-provenance", "control", "--candidate-provenance", "candidate",
                "--output", "/campaign",
            ])
        with self.assertRaisesRegex(pairing.campaign.CampaignError, "nonzero CPU"):
            pairing._require_nonzero_cpu(0)

    def test_compatibility_profile_reaches_preflight_and_timed_case(self) -> None:
        profile = "legacy-unprotected-shared-variable"
        configured = pairing._configure_cases([{
            "id": "mixed_throughput",
            "source_groups": [{"language": "vhdl", "standard": "2008"}],
        }])[0]
        self.assertEqual(configured["fsim_vhdl_compatibility"], profile)
        control_command = pairing._preflight_command(
            Path("/control/fsim"), "control", Path("/campaign/control"),
            Path("/manifest.json"), Path("/vivado/bin"), [], 42, 3)
        candidate_command = pairing._preflight_command(
            Path("/candidate/fsim"), "candidate", Path("/campaign/candidate"),
            Path("/manifest.json"), Path("/vivado/bin"), [], 42, 3,
            {"original_throughput": Path("/fixtures/original.v"),
             "mixed_throughput": Path("/fixtures/mixed.v")})
        expected = ["--fsim-vhdl-compatibility", f"mixed_throughput={profile}"]
        for command in (control_command, candidate_command):
            self.assertEqual(command.count(expected[0]), 1)
            index = command.index(expected[0])
            self.assertEqual(command[index:index + 2], expected)

    def test_each_leg_has_a_distinct_workspace_location(self) -> None:
        root = Path("/campaign")
        control = pairing._timing_directory(root, "original_throughput", 1, "control")
        candidate = pairing._timing_directory(root, "original_throughput", 1, "candidate")
        next_round = pairing._timing_directory(root, "original_throughput", 2, "control")
        self.assertNotEqual(control, candidate)
        self.assertNotEqual(control, next_round)
        self.assertNotEqual(
            pairing._native_cache_directory(root, "original_throughput", 1, "control"),
            pairing._native_cache_directory(root, "original_throughput", 1, "candidate"),
        )


class PreflightIdentityTests(unittest.TestCase):
    def test_cross_binary_preflight_requires_shared_fixture_and_oracle_transcripts(self) -> None:
        control = preflight_bundle("control")
        candidate = preflight_bundle("candidate")
        candidate["identity"]["fixture_metadata"] = {
            case: candidate_overlay_fixture(fixture)
            for case, fixture in control["identity"]["fixture_metadata"].items()
        }
        verified = pairing._compare_preflight_bundles(control, candidate)
        self.assertEqual(set(verified), set(pairing.CASE_IDS))
        for case in pairing.CASE_IDS:
            self.assertEqual(set(verified[case]), {"control", "candidate"})

    def test_control_candidate_vivado_transcript_mismatch_is_rejected(self) -> None:
        control = preflight_bundle("control")
        candidate = preflight_bundle("candidate")
        candidate["results"][("mixed_throughput", "fsim")]["canonical_transcript"][
            "summaries"] = ["different-final-fingerprint"]
        with self.assertRaisesRegex(pairing.campaign.CampaignError,
                                    "canonical preflight transcripts differ"):
            pairing._compare_preflight_bundles(control, candidate)

    def test_different_fixture_seed_or_bytes_is_rejected_before_timing(self) -> None:
        control = preflight_bundle("control")
        candidate = preflight_bundle("candidate")
        candidate["identity"]["fixture_metadata"]["original_throughput"][
            "testbench_sha256"] = "other-fixture-hash"
        with self.assertRaisesRegex(pairing.campaign.CampaignError,
                                    "source, fixture, stimulus"):
            pairing._compare_preflight_bundles(control, candidate)

    def test_nested_seed_difference_is_rejected_even_if_overlay_path_is_same(self) -> None:
        control = preflight_bundle("control")
        candidate = preflight_bundle("candidate")
        candidate["identity"]["fixture_metadata"] = {
            case: candidate_overlay_fixture(fixture)
            for case, fixture in control["identity"]["fixture_metadata"].items()
        }
        candidate["identity"]["fixture_metadata"]["mixed_throughput"][
            "metadata"]["configuration"]["root_seed"] = "0x0000002b"
        with self.assertRaisesRegex(pairing.campaign.CampaignError,
                                    "source, fixture, stimulus"):
            pairing._compare_preflight_bundles(control, candidate)

    def test_mixed_compatibility_profile_is_part_of_shared_preflight_identity(self) -> None:
        control = preflight_bundle("control")
        candidate = preflight_bundle("candidate")
        candidate["identity"]["run_configuration"][
            "fsim_vhdl_compatibility_by_case"] = {}
        with self.assertRaisesRegex(pairing.campaign.CampaignError,
                                    "source, fixture, stimulus"):
            pairing._compare_preflight_bundles(control, candidate)

    def test_projection_normalizes_nested_seed_and_generator_only_control_fields(self) -> None:
        control = preflight_identity("control")["fixture_metadata"]["original_throughput"]
        candidate = candidate_overlay_fixture(control)
        self.assertEqual(pairing._fixture_projection(control),
                         pairing._fixture_projection(candidate))

    def test_distinct_executable_provenance_and_binary_identity_are_required(self) -> None:
        control = preflight_bundle("control")
        candidate = preflight_bundle("candidate")
        candidate["identity"]["simulator_binaries"]["fsim"]["sha256"] = \
            control["identity"]["simulator_binaries"]["fsim"]["sha256"]
        with self.assertRaisesRegex(pairing.campaign.CampaignError,
                                    "distinct fsim executables"):
            pairing._compare_preflight_bundles(control, candidate)

    def test_missing_preflight_engine_leg_is_rejected_before_timing(self) -> None:
        control = preflight_bundle("control")
        candidate = preflight_bundle("candidate")
        del candidate["results"][("original_throughput", "vivado")]
        with self.assertRaisesRegex(pairing.campaign.CampaignError,
                                    "missing or extra case/engine results"):
            pairing._compare_preflight_bundles(control, candidate)


class PairedStatisticsTests(unittest.TestCase):
    def test_statistics_pair_by_explicit_pair_id_not_input_order(self) -> None:
        rows = [
            timing_pair("original_throughput", "round-03", 3, 100.0, 90.0),
            timing_pair("original_throughput", "round-01", 1, 10.0, 20.0),
            timing_pair("original_throughput", "round-02", 2, 1000.0, 500.0),
        ]
        summary = pairing.summarize_case_pairs(rows, 3, 17)
        reversed_summary = pairing.summarize_case_pairs(list(reversed(rows)), 3, 17)
        self.assertEqual(summary, reversed_summary)
        self.assertEqual(summary["paired_ids"], ["round-01", "round-02", "round-03"])
        self.assertEqual(summary["candidate_over_control"]["median_paired_ratio"], 0.9)
        self.assertEqual(summary["legs"]["control"]["wall_seconds"]["median"], 100.0)
        self.assertEqual(summary["legs"]["candidate"]["wall_seconds"]["median"], 90.0)
        self.assertIn("native_setup_and_simulation",
                      summary["legs"]["candidate"]["phase_seconds"])
        self.assertIsNotNone(summary["legs"]["candidate"]["peak_rss_kib"])

    def test_incomplete_or_misidentified_leg_cannot_be_summarized(self) -> None:
        rows = [timing_pair("original_throughput", "round-01", 1, 10.0, 9.0)]
        with self.assertRaisesRegex(pairing.campaign.CampaignError, "completed pairs"):
            pairing.summarize_case_pairs(rows, 2, 7)
        duplicate = rows + [copy.deepcopy(rows[0])]
        with self.assertRaisesRegex(pairing.campaign.CampaignError, "duplicate identity"):
            pairing.summarize_case_pairs(duplicate, 2, 7)
        wrong_leg = copy.deepcopy(rows)
        wrong_leg[0]["legs"]["candidate"]["pair_id"] = "another-pair"
        with self.assertRaisesRegex(pairing.campaign.CampaignError, "invalid candidate timing row"):
            pairing.summarize_case_pairs(wrong_leg, 1, 7)

    def test_five_pairs_decide_seven_pairs_qualify_only_with_both_gates(self) -> None:
        def summaries(pair_count: int) -> dict[str, dict]:
            result = {}
            for case, candidate_time, control_time in (
                ("original_throughput", 10.5, 13.125),
                ("mixed_throughput", 10.0, 12.5),
            ):
                rows = [timing_pair(case, f"round-{index:02d}", index,
                                    control_time, candidate_time)
                        for index in range(1, pair_count + 1)]
                result[case] = pairing.summarize_case_pairs(rows, pair_count, 42)
            return result

        five = pairing.assess_campaign(summaries(5), 5, True, True)
        self.assertTrue(five["decision_pairs_met"])
        self.assertFalse(five["final_qualification_pairs_met"])
        self.assertFalse(five["overall_qualified"])

        seven = pairing.assess_campaign(summaries(7), 7, True, True)
        self.assertTrue(seven["absolute_performance_targets_passed"])
        self.assertEqual(seven["control_candidate_improvement_status"],
                         "supported_improvement")
        self.assertTrue(seven["overall_qualified"])

        six = pairing.assess_campaign(summaries(6), 6, True, True)
        self.assertFalse(six["overall_qualified"])

    def test_mixed_case_regression_blocks_overall_improvement(self) -> None:
        summaries = {}
        for case, control, candidate in (
            ("original_throughput", 20.0, 10.0),
            ("mixed_throughput", 10.0, 10.5),
        ):
            rows = [timing_pair(case, f"round-{index}", index, control, candidate)
                    for index in range(1, 8)]
            summaries[case] = pairing.summarize_case_pairs(rows, 7, 19)
        decision = pairing.assess_campaign(summaries, 7, True, True)
        self.assertEqual(decision["case_improvement_status"]["original_throughput"],
                         "supported_improvement")
        self.assertEqual(decision["case_improvement_status"]["mixed_throughput"],
                         "supported_regression")
        self.assertEqual(decision["control_candidate_improvement_status"],
                         "supported_case_regression")
        self.assertTrue(decision["overall_qualified"])

    def test_overlapping_paired_interval_stays_inconclusive(self) -> None:
        ratios = (0.9, 1.1, 0.95, 1.05, 1.0, 0.8, 1.2)
        summaries = {}
        for case in pairing.CASE_IDS:
            rows = [timing_pair(case, f"round-{index}", index, 10.0, 10.0 * ratio)
                    for index, ratio in enumerate(ratios, 1)]
            summaries[case] = pairing.summarize_case_pairs(rows, 7, 5)
        decision = pairing.assess_campaign(summaries, 7, True, True)
        self.assertEqual(decision["control_candidate_improvement_status"], "inconclusive")
        self.assertTrue(decision["overall_qualified"])

    def test_absolute_limits_are_inclusive_and_checked_per_case(self) -> None:
        summaries = {}
        for case, candidate in (("original_throughput", 10.5),
                                ("mixed_throughput", 10.0)):
            rows = [timing_pair(case, f"round-{index}", index, 20.0, candidate)
                    for index in range(1, 8)]
            summaries[case] = pairing.summarize_case_pairs(rows, 7, 7)
        decision = pairing.assess_campaign(summaries, 7, True, True)
        self.assertTrue(decision["absolute_performance_targets_passed"])
        self.assertTrue(decision["anti_slowdown_guard_passed"])

        summaries["original_throughput"]["legs"]["candidate"][
            "wall_seconds"]["median"] = 15.01
        decision = pairing.assess_campaign(summaries, 7, True, True)
        self.assertFalse(decision["absolute_performance_targets_passed"])
        self.assertFalse(decision["overall_qualified"])

    def test_median_slowdown_with_overlapping_interval_is_inconclusive(self) -> None:
        ratios = (0.7, 0.9, 1.02, 1.05, 1.1, 1.2, 1.3)
        rows = [timing_pair("mixed_throughput", f"round-{index}", index,
                            10.0, 10.0 * ratio)
                for index, ratio in enumerate(ratios, 1)]
        summary = pairing.summarize_case_pairs(rows, 7, 31)
        self.assertEqual(summary["candidate_over_control"]["point_estimate_status"],
                         "candidate_slower")
        self.assertEqual(summary["candidate_over_control"]["inference_status"],
                         "inconclusive")

    def test_candidate_original_gap_cannot_close_by_slowing_mixed(self) -> None:
        summaries = {}
        for case, control, candidate in (
            ("original_throughput", 20.0, 11.0),
            ("mixed_throughput", 10.0, 12.0),
        ):
            rows = [timing_pair(case, f"round-{index}", index, control, candidate)
                    for index in range(1, 8)]
            summaries[case] = pairing.summarize_case_pairs(rows, 7, 9)
        decision = pairing.assess_campaign(summaries, 7, True, True)
        self.assertTrue(decision["absolute_performance_targets_passed"])
        self.assertFalse(decision["anti_slowdown_guard_passed"])
        self.assertFalse(decision["overall_qualified"])


if __name__ == "__main__":
    unittest.main()
