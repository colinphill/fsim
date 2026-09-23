#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for qualify-simplification-performance.py."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).with_name("qualify-simplification-performance.py")
SPEC = importlib.util.spec_from_file_location("simplification_performance", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
MANIFEST_PATH = Path(__file__).with_name("simplification_benchmarks.json")


def jit_setup(
    *, modules: int = 1, processes: int = 1, operations: int = 7,
    retained_processes: int = 0, retained_operations: int = 0
) -> str:
    return (
        "fsim-profile: jit setup_ms=1 registration_ms=0.5 "
        f"materialization_launch_ms=0.25 modules={modules} "
        f"unpacked_modules={modules} processes={processes} "
        f"operations={operations} lowered_processes={processes} "
        f"lowered_operations={operations} largest_module_operations={operations} "
        "largest_module_identity='fixture' "
        f"retained_processes={retained_processes} "
        f"retained_operations={retained_operations} "
        "selective_large_design=0 compile_all=0"
    )


class SimplificationPerformanceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = MODULE.load_manifest(MANIFEST_PATH)
        self.roots = MODULE.resolve_roots(self.manifest, MANIFEST_PATH, {})
        self.cases = {case["id"]: case for case in self.manifest["cases"]}
        self.configurations = {
            entry["id"]: entry for entry in self.manifest["configurations"]
        }

    def test_mandatory_cases_are_active(self) -> None:
        active = {
            case["id"] for case in self.manifest["cases"]
            if case["status"] == "active"
        }
        self.assertTrue(MODULE.MANDATORY_CASES <= active)
        for name in (
            "repository_pure_sv_coverage",
            "repository_mixed_long_bare",
            "repository_mixed_long_vcd",
            "repository_mixed_long_fst",
            "repository_mixed_long_observer",
            "repository_mixed_long_history",
        ):
            self.assertEqual(self.cases[name]["status"], "active")
            self.assertEqual(
                self.cases[name]["verification_status"],
                "implementation_verified",
            )

    def test_real_designs_require_jit_and_repository_cases_keep_all_engines(self) -> None:
        jit_configurations = {"llvm_o0", "llvm_o2"}
        all_configurations = {"interpreter", *jit_configurations}
        real_cases = [
            case for case in self.manifest["cases"]
            if case["root"] != "repository"
        ]
        self.assertEqual(len(real_cases), 10)
        for name in MODULE.MANDATORY_CASES:
            self.assertNotEqual(self.cases[name]["root"], "repository")
            self.assertEqual(
                set(self.cases[name]["configurations"]), jit_configurations
            )
        repository_cases = [
            case for case in self.manifest["cases"]
            if case["root"] == "repository"
        ]
        self.assertEqual(len(repository_cases), 6)
        for case in repository_cases:
            self.assertEqual(set(case["configurations"]), all_configurations)

    def test_preflight_uses_only_configurations_declared_for_each_case(self) -> None:
        cases = [
            self.cases["original_codec"],
            self.cases["repository_pure_sv_coverage"],
        ]
        arguments = MODULE.argparse.Namespace(
            baseline_fsim=None, candidate_fsim=None,
            baseline_helper=None, candidate_helper=None,
        )
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            MODULE.preflight(
                cases, list(self.configurations.values()), self.manifest,
                self.roots, arguments,
            )
        report = output.getvalue()
        self.assertNotIn("[original_codec interpreter ", report)
        self.assertIn("[original_codec llvm_o0 baseline]", report)
        self.assertIn("[original_codec llvm_o2 baseline]", report)
        self.assertIn("[repository_pure_sv_coverage interpreter baseline]", report)

    def test_real_design_cannot_materialize_an_interpreter_command(self) -> None:
        with self.assertRaisesRegex(
            MODULE.QualificationError, "does not support configuration interpreter"
        ):
            MODULE.materialize_commands(
                self.cases["original_codec"], self.configurations["interpreter"],
                Path("/fsim"), self.manifest, self.roots, Path("/evidence/run"),
            )

    def test_repository_coverage_case_enables_and_checks_coverage(self) -> None:
        case = self.cases["repository_pure_sv_coverage"]
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            commands = MODULE.materialize_commands(
                case, self.configurations["llvm_o2"], Path("/fsim"),
                self.manifest, self.roots, run, True,
            )
            wrapper = (run / "generated/wrapper.sv").read_text(encoding="utf-8")
        self.assertIn("$set_coverage_db_name", wrapper)
        self.assertIn("$get_coverage()", wrapper)
        self.assertNotIn("$coverage_save", wrapper)
        self.assertNotIn("SV_COV_NOCOV", wrapper)
        self.assertIn('observations/functional.fsimcov', wrapper)
        self.assertIn("PASS: simplification pure SV coverage", wrapper)
        self.assertEqual(case["runner"], "helper")
        self.assertEqual(case["helper_mode"], "coverage")
        self.assertNotIn("--code-coverage", commands["compile"][0])
        self.assertNotIn("--code-coverage", commands["elaborate"])
        self.assertNotIn("--code-coverage", commands["simulate"])
        self.assertIn("--file-root", commands["simulate"])
        self.assertEqual(
            commands["simulate"][commands["simulate"].index("--file-root") + 1],
            str(run),
        )
        self.assertEqual(
            commands["simulate"][commands["simulate"].index("--coverage-path") + 1],
            "observations/functional.fsimcov",
        )
        self.assertEqual(case["observation"]["kind"], "functional_coverage")

    def test_functional_coverage_observation_has_equivalence_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "functional.fsimcov"
            path.write_bytes(b"FSIMSVCV-functional-state")
            stdout = Path(directory) / "simulate.stdout"
            stdout.write_text(
                "PASS: simplification functional coverage semantic_sha256="
                + "0123456789abcdef" * 4 + "\n",
                encoding="utf-8",
            )
            observation = MODULE.functional_coverage_observation(path, stdout)
        self.assertEqual(observation["kind"], "functional_coverage")
        self.assertEqual(observation["size"], len(b"FSIMSVCV-functional-state"))
        self.assertEqual(
            observation["semantic_sha256"], observation["equivalence_sha256"]
        )
        self.assertNotEqual(observation["sha256"], observation["equivalence_sha256"])

    def test_functional_coverage_digest_requires_one_strict_marker(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "functional.fsimcov"
            path.write_bytes(b"FSIMSVCV-functional-state")
            stdout = root / "simulate.stdout"
            prefix = "PASS: simplification functional coverage semantic_sha256="
            semantic_digest = "0123456789abcdef" * 4
            marker = prefix + semantic_digest + "\n"
            for output, expected in (
                ("", "found 0"),
                (marker + marker, "found 2"),
                (marker.replace("0", "G", 1), "lowercase hexadecimal"),
                (prefix + semantic_digest[:-1] + "\n", "lowercase hexadecimal"),
                (prefix + semantic_digest + "0\n", "lowercase hexadecimal"),
                (prefix + " " + semantic_digest + "\n", "lowercase hexadecimal"),
                (prefix + semantic_digest + " \n", "lowercase hexadecimal"),
            ):
                stdout.write_text(output, encoding="utf-8")
                with self.assertRaisesRegex(MODULE.QualificationError, expected):
                    MODULE.functional_coverage_observation(path, stdout)

    def test_vcd_semantic_identity_ignores_embedded_metadata(self) -> None:
        first = """$date first $end
$version one $end
$timescale 1ns $end
$scope module top $end
$var wire 1 ! value $end
$upscope $end
$enddefinitions $end
$comment before values $end
#0
0!
$comment
different embedded metadata
$end
#1
1!
"""
        second = first.replace("first", "second").replace(
            "$version one $end", "$version two $end"
        ).replace("different embedded metadata", "another comment")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first_path = root / "first.vcd"
            second_path = root / "second.vcd"
            first_path.write_text(first, encoding="utf-8")
            second_path.write_text(second, encoding="utf-8")
            first_observation = MODULE.vcd_observation(first_path, 2)
            second_observation = MODULE.vcd_observation(second_path, 2)
        self.assertEqual(
            first_observation["equivalence_sha256"],
            second_observation["equivalence_sha256"],
        )
        self.assertEqual(first_observation["value_change_lines"], 2)

    def test_repository_mixed_variants_share_oracle_and_trace_flags(self) -> None:
        wrappers = []
        commands = {}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for variant in ("bare", "vcd", "fst"):
                case = self.cases[f"repository_mixed_long_{variant}"]
                run = root / variant
                commands[variant] = MODULE.materialize_commands(
                    case, self.configurations["interpreter"], Path("/fsim"),
                    self.manifest, self.roots, run, True,
                )
                wrappers.append(
                    (run / "generated/wrapper.sv").read_text(encoding="utf-8")
                )
        self.assertEqual(wrappers[0], wrappers[1])
        self.assertEqual(wrappers[1], wrappers[2])
        self.assertEqual(wrappers[0].count("counter u_counter_"), 16)
        self.assertIn("cycle < 100000", wrappers[0])
        self.assertEqual(wrappers[0].count("if (counter_q_"), 16)
        self.assertEqual(wrappers[0].count("if (child_y_"), 16)
        self.assertIn("PASS: simplification mixed long", wrappers[0])
        self.assertNotIn("--trace", commands["bare"]["simulate"])
        for variant in ("vcd", "fst"):
            simulate = commands[variant]["simulate"]
            self.assertEqual(simulate[simulate.index("--trace-format") + 1], variant)
            self.assertTrue(
                simulate[simulate.index("--trace") + 1].endswith(
                    f"simulation.{variant}"
                )
            )
            case = self.cases[f"repository_mixed_long_{variant}"]
            filters = case["observation"]["filters"]
            self.assertEqual(len(filters), 8)
            self.assertEqual(simulate.count("--trace-filter"), 8)
            self.assertEqual(
                [
                    simulate[index + 1]
                    for index, value in enumerate(simulate)
                    if value == "--trace-filter"
                ],
                [
                    pattern.replace("{root}", case["id"])
                    for pattern in filters
                ],
            )
            self.assertTrue(all(pattern.startswith("{root}.") for pattern in filters))
            self.assertEqual(
                case["observation"]["minimum_value_changes"], 800000
            )

    def test_original_command_uses_explicit_systemverilog_fixture(self) -> None:
        commands = MODULE.materialize_commands(
            self.cases["original_codec"], self.configurations["llvm_o0"],
            Path("/fsim"), self.manifest, self.roots, Path("/evidence/run")
        )
        self.assertEqual(len(commands["compile"]), 1)
        compile_command = commands["compile"][0]
        self.assertIn("systemverilog", compile_command)
        self.assertIn("2017", compile_command)
        self.assertIn("SIMULATION", compile_command)
        self.assertEqual(compile_command.count("--jobs"), 1)
        self.assertIn("8", compile_command)

    def test_mixed_command_preserves_vhdl_order_and_separate_tb(self) -> None:
        commands = MODULE.materialize_commands(
            self.cases["mixed_codec"], self.configurations["llvm_o0"],
            Path("/fsim"), self.manifest, self.roots, Path("/evidence/run")
        )
        self.assertEqual(len(commands["compile"]), 2)
        vhdl = commands["compile"][0]
        self.assertLess(
            vhdl.index(str(self.roots["mixed"] / "rtl_vhdl/rs_pkg.vhd")),
            vhdl.index(str(self.roots["mixed"] / "rtl_vhdl/gf_mult.vhd")),
        )
        self.assertEqual(commands["compile"][1][-1], str(
            self.roots["mixed"] / "tb/rs_codec_tb.v"
        ))
        self.assertIn("O0", commands["elaborate"])
        self.assertIn("compiled", commands["simulate"])

    def test_codex_wrapper_replaces_unavailable_parameter_cli(self) -> None:
        case = self.cases["codex_reference_mode1_frames2"]
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            commands = MODULE.materialize_commands(
                case, self.configurations["llvm_o2"], Path("/fsim"),
                self.manifest, self.roots, run, True
            )
            wrapper = run / "generated/wrapper.sv"
            self.assertIn(".MODE(1)", wrapper.read_text(encoding="utf-8"))
            self.assertIn(".INPUT_FRAMES(2)", wrapper.read_text(encoding="utf-8"))
            self.assertEqual(commands["compile"][0][-28].rsplit("/", 1)[-1],
                             "rs_kes_euclid_pkg.sv")
            self.assertEqual(commands["compile"][1][-1], str(wrapper))

    def test_helper_cases_materialize_explicit_helper_commands(self) -> None:
        case = self.cases["repository_mixed_long_history"]
        commands = MODULE.materialize_commands(
            case, self.configurations["llvm_o0"], Path("/fsim"),
            self.manifest, self.roots, Path("/evidence/history"),
            helper=Path("/helper"),
        )
        self.assertEqual(commands["simulate"][0], "/helper")
        self.assertEqual(
            commands["simulate"][commands["simulate"].index("--mode") + 1],
            "history",
        )
        self.assertEqual(
            commands["simulate"][commands["simulate"].index("--root") + 1],
            case["id"],
        )

    def test_subset_and_unverified_foundation_cannot_qualify(self) -> None:
        unverified_case = dict(self.cases["repository_pure_sv_coverage"])
        unverified_case["verification_status"] = "implementation_unverified"
        manifest = dict(self.manifest)
        manifest["cases"] = [
            unverified_case
            if case["id"] == unverified_case["id"] else case
            for case in self.manifest["cases"]
        ]
        self.assertEqual(
            self.cases["repository_pure_sv_coverage"]["verification_status"],
            "implementation_verified",
        )
        blockers = MODULE.qualification_blockers(
            manifest,
            [self.cases["original_codec"]],
            [self.configurations["interpreter"]],
        )
        self.assertTrue(
            any("implementation-unverified cases:" in blocker
                for blocker in blockers)
        )
        self.assertTrue(
            any("required cases omitted:" in blocker for blocker in blockers)
        )
        self.assertTrue(
            any("required configurations omitted:" in blocker for blocker in blockers)
        )
        self.assertTrue(
            any("required case/configuration pairs omitted:" in blocker
                for blocker in blockers)
        )
        self.assertFalse(
            any("qualification foundation incomplete:" in blocker
                for blocker in blockers)
        )

    def test_profile_environment_is_removed_from_timed_environment(self) -> None:
        with mock.patch.dict(
            os.environ,
            {"FSIM_PROFILE_PHASES": "1", "FSIM_PROFILE_JIT": "1", "SAFE_VALUE": "x"},
            clear=True,
        ):
            environment, removed = MODULE.stable_environment()
        self.assertEqual(removed, ["FSIM_PROFILE_JIT", "FSIM_PROFILE_PHASES"])
        self.assertNotIn("FSIM_PROFILE_PHASES", environment)
        self.assertEqual(environment["SAFE_VALUE"], "x")
        self.assertEqual(environment["LC_ALL"], "C")

    def test_source_and_include_identity_drift_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "top.sv"
            header = root / "defs.svh"
            source.write_text(
                chr(96) + 'include "defs.svh"\nmodule top; endmodule\n',
                encoding="utf-8",
            )
            header.write_text("localparam int VALUE = 1;\n", encoding="utf-8")
            case = {
                "id": "identity", "root": "fixture", "top": "sv:work.top",
                "source_groups": [{
                    "language": "systemverilog", "standard": "2017",
                    "sources": ["top.sv"], "include_dirs": ["."],
                    "defines": ["IDENTITY_TEST"],
                }],
            }
            before = MODULE.capture_source_identity(
                [case], {"source_sets": {}}, {"fixture": root}
            )
            group = before["cases"][0]["groups"][0]
            self.assertEqual(group["sources"][0]["path"], "fixture/top.sv")
            self.assertEqual(
                group["include_inputs"][0]["path"], "fixture/defs.svh"
            )
            self.assertEqual(group["defines"], ["IDENTITY_TEST"])
            header.write_text("localparam int VALUE = 2;\n", encoding="utf-8")
            after = MODULE.capture_source_identity(
                [case], {"source_sets": {}}, {"fixture": root}
            )
            with self.assertRaisesRegex(
                MODULE.QualificationError, "source corpus identity changed"
            ):
                MODULE.require_identity_unchanged(
                    before, after, "source corpus"
                )

    def test_dependency_drift_fails_closed(self) -> None:
        baseline = {
            "linked_dependencies": [
                {"name": "libLLVM.so", "path": "/lib/libLLVM.so", "sha256": "a"}
            ]
        }
        candidate = {
            "linked_dependencies": [
                {"name": "libLLVM.so", "path": "/lib/libLLVM.so", "sha256": "b"}
            ]
        }
        with self.assertRaisesRegex(
            MODULE.QualificationError, "runtime dependencies differ"
        ):
            MODULE.require_matching_runtime_dependencies(baseline, candidate)
        with self.assertRaisesRegex(
            MODULE.QualificationError,
            "executable and linked dependency identity changed",
        ):
            MODULE.require_identity_unchanged(
                baseline, candidate, "executable and linked dependency"
            )

    def test_correctness_requires_exact_counts_and_rejects_failure(self) -> None:
        case = {
            "id": "probe",
            "correctness": {
                "required": [{"pattern": "(?m)^PASS$", "count": 2}],
                "forbidden": ["(?i)FAIL"],
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "stdout"
            output.write_text("PASS\nPASS\n", encoding="utf-8")
            MODULE.validate_correctness(case, output)
            output.write_text("PASS\nFAIL\n", encoding="utf-8")
            with self.assertRaises(MODULE.QualificationError):
                MODULE.validate_correctness(case, output)

    def test_compiled_profile_reports_native_execution_and_fallback(self) -> None:
        stderr = "\n".join([
            jit_setup(modules=2),
            "fsim-profile: jit-process-summary processes=1 resumes=3 elapsed_ms=1.5",
            "fsim-profile: jit-process id=4 generated_id=9 resumes=3 "
            "direct_read_slots=1 direct_update_slots=2 operations=7 "
            "static_waits=1 elapsed_ms=1.5 name='dut.process'",
            "fsim jit module profile: identity=native processes=1 process_ids=9 "
            "operations=7 weight=8 worker=0 startup=1 materialization_ms=2.5",
            "fsim jit unsupported module: identity=fallback reason=unsupported op",
            "fsim jit module profile: identity=fallback processes=1 process_ids=5 "
            "operations=6 weight=1 worker=0 startup=0 materialization_ms=0.5",
        ])
        profile = MODULE.parse_native_profile("compiled", stderr)
        self.assertTrue(profile["applicable"])
        self.assertEqual(profile["summary"]["resumes"], 3)
        self.assertEqual(
            profile["static_operation_count_for_profiled_processes"], 7
        )
        self.assertEqual(
            profile["unsupported_modules"],
            [{"identity": "fallback", "reason": "unsupported op"}],
        )
        self.assertEqual(
            profile["modules_without_native_resumes"], ["fallback"]
        )
        self.assertEqual(profile["setup"]["selected_processes"], 1)

    def test_compiled_profile_all_fallback_uses_setup_evidence(self) -> None:
        profile = MODULE.parse_native_profile(
            "compiled",
            jit_setup(
                modules=0, processes=0, operations=0,
                retained_processes=4, retained_operations=20,
            ),
        )
        self.assertTrue(profile["applicable"])
        self.assertFalse(profile["execution_summary_observed"])
        self.assertEqual(profile["native_processes_resumed"], 0)
        self.assertIn("retained", profile["native_selection_evidence"])

    def test_missing_or_malformed_profile_evidence_fails_closed(self) -> None:
        with self.assertRaisesRegex(
            MODULE.QualificationError, "exactly one FSIM-PROFILE"
        ):
            MODULE.parse_phase_profile("simulation completed\n")
        malformed = "\n".join([
            jit_setup(),
            "fsim-profile: jit-process-summary processes=1 resumes=1 elapsed_ms=1",
            "fsim-profile: jit-process id=broken",
            "fsim jit module profile: identity=one processes=1 process_ids=1 "
            "operations=1 weight=1 worker=0 startup=1 materialization_ms=1",
        ])
        with self.assertRaisesRegex(
            MODULE.QualificationError, "malformed profiling evidence"
        ):
            MODULE.parse_native_profile("compiled", malformed)
        with self.assertRaisesRegex(
            MODULE.QualificationError, "one JIT setup record"
        ):
            MODULE.parse_native_profile(
                "compiled",
                "fsim jit module profile: identity=one processes=1 "
                "process_ids=1 operations=1 weight=1 worker=0 startup=1 "
                "materialization_ms=1",
            )

    def test_truncated_unsupported_identity_collision_is_preserved(self) -> None:
        identity = "x" * 96
        stderr = "\n".join([
            jit_setup(modules=2, processes=2, operations=2),
            "fsim-profile: jit-process-summary processes=0 resumes=0 elapsed_ms=0",
            f"fsim jit unsupported module: identity={identity}one reason=first",
            f"fsim jit module profile: identity={identity} processes=1 "
            "process_ids=1 operations=1 weight=1 worker=0 startup=1 "
            "materialization_ms=1",
            f"fsim jit module profile: identity={identity} processes=1 "
            "process_ids=2 operations=1 weight=1 worker=0 startup=1 "
            "materialization_ms=1",
        ])
        profile = MODULE.parse_native_profile("compiled", stderr)
        self.assertEqual(
            [module["unsupported_attribution"] for module in profile["modules"]],
            ["ambiguous", "ambiguous"],
        )
        self.assertEqual(
            profile["unattributed_unsupported_modules"],
            [{"identity": identity + "one", "reason": "first"}],
        )

    def test_interpreter_native_profile_is_explicitly_not_applicable(self) -> None:
        profile = MODULE.parse_native_profile("interpreter", "")
        self.assertFalse(profile["applicable"])
        self.assertIn("interpreter", profile["reason"])
        with self.assertRaisesRegex(
            MODULE.QualificationError, "unexpectedly reports native"
        ):
            MODULE.parse_native_profile(
                "interpreter",
                "fsim jit module profile: identity=one processes=1 "
                "process_ids=1 operations=1 weight=1 worker=0 startup=1 "
                "materialization_ms=1",
            )

    def test_summary_gates_each_case_configuration(self) -> None:
        case = {"id": "case"}
        configuration = {"id": "config"}
        results = []
        for variant, wall, rss in (
            ("baseline", 10.0, 100),
            ("candidate", 10.9, 109),
        ):
            for _ in range(7):
                metric = {"wall_seconds": wall / 3, "peak_rss_kib": rss}
                results.append({
                    "case": "case", "configuration": "config",
                    "variant": variant,
                    "phases": {
                        "compile": [metric], "elaborate": metric, "simulate": metric
                    },
                    "e2e_wall_seconds": wall, "e2e_peak_rss_kib": rss,
                })
        summary, passed = MODULE.summarize(
            results, [case], [configuration], self.manifest
        )
        self.assertTrue(passed)
        self.assertAlmostEqual(
            summary["case"]["config"]["ratios"]["e2e_wall"], 1.09
        )
        self.assertTrue(summary["case"]["config"]["gate_passed"])

    def test_summary_skips_configurations_outside_case_requirements(self) -> None:
        case = {"id": "real", "configurations": ["llvm_o0", "llvm_o2"]}
        configurations = [
            {"id": name} for name in ("interpreter", "llvm_o0", "llvm_o2")
        ]
        results = []
        metric = {"wall_seconds": 1.0, "peak_rss_kib": 100}
        for configuration in ("llvm_o0", "llvm_o2"):
            for variant in ("baseline", "candidate"):
                for _ in range(7):
                    results.append({
                        "case": "real", "configuration": configuration,
                        "variant": variant,
                        "phases": {
                            "compile": [metric], "elaborate": metric,
                            "simulate": metric,
                        },
                        "e2e_wall_seconds": 3.0,
                        "e2e_peak_rss_kib": 100,
                    })
        summary, passed = MODULE.summarize(
            results, [case], configurations, self.manifest
        )
        self.assertTrue(passed)
        self.assertEqual(set(summary["real"]), {"llvm_o0", "llvm_o2"})

    def test_summary_rejects_regression_above_limit(self) -> None:
        results = []
        metric = {"wall_seconds": 1.0, "peak_rss_kib": 100}
        for variant, wall in (("baseline", 10.0), ("candidate", 11.01)):
            for _ in range(7):
                results.append({
                    "case": "case", "configuration": "config",
                    "variant": variant,
                    "phases": {
                        "compile": [metric], "elaborate": metric, "simulate": metric
                    },
                    "e2e_wall_seconds": wall, "e2e_peak_rss_kib": 100,
                })
        _, passed = MODULE.summarize(
            results, [{"id": "case"}], [{"id": "config"}], self.manifest
        )
        self.assertFalse(passed)

    def test_summary_rejects_missing_samples(self) -> None:
        with self.assertRaisesRegex(MODULE.QualificationError, "expected 7"):
            MODULE.summarize(
                [], [{"id": "case"}], [{"id": "config"}], self.manifest
            )

    def test_bounded_process_times_out(self) -> None:
        with self.assertRaisesRegex(MODULE.QualificationError, "timed out"):
            MODULE.run_bounded(
                [sys.executable, "-c", "import time; time.sleep(1)"],
                timeout=0.01,
            )

    def test_manifest_json_is_stable_and_baseline_is_exact(self) -> None:
        raw = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(
            raw["baseline_commit"],
            "ed5ca693704edd277ec3f055ed7d9ed0e3048f2d",
        )
        self.assertIn("Declared comparison revision", raw["baseline_revision_semantics"])
        self.assertEqual(
            raw["qualification_requirements"]["source_identity_hashes"]["status"],
            "complete",
        )
        self.assertEqual(
            raw["qualification_requirements"]["dependency_identity_hashes"]["status"],
            "complete",
        )
        self.assertEqual(
            raw["qualification_requirements"]["phase_profiling"]["status"],
            "complete",
        )
        self.assertEqual(
            raw["qualification_requirements"]["native_coverage_acceptance"]["status"],
            "complete",
        )
        self.assertEqual(
            [entry["id"] for entry in raw["configurations"]],
            ["interpreter", "llvm_o0", "llvm_o2"],
        )


if __name__ == "__main__":
    unittest.main()
