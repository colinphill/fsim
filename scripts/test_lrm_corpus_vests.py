#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Focused tests for corpus source scanning and VESTs writer/reader planning."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from scripts import lrm_corpus


def write_source(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    entity = f"{path.stem}ent"
    path.write_text(
        f"entity {entity} is\nend entity;\n"
        f"architecture test_arch of {entity} is\nbegin\nend architecture;\n",
        encoding="utf-8",
    )


class SystemVerilogRootScanningTests(unittest.TestCase):
    def test_quotes_inside_escaped_macro_names(self) -> None:
        source = r'''`define \name"with"quotes  "macro value"
module top;
  initial $display("module phantom; // /*");
endmodule
'''
        self.assertEqual(lrm_corpus.sv_root_modules([source]), ["top"])

    def test_escaped_module_names_and_comments(self) -> None:
        source = r'''/* module phantom; */
module \top"name ;
endmodule
'''
        self.assertEqual(lrm_corpus.sv_root_modules([source]), [r'\top"name'])



class IvtestSourceLocationTests(unittest.TestCase):
    def test_known_source_and_include_paths_keep_line_numbers(self):
        root = "objects/" + "a" * 64
        actual = [
            f"File {root}/sources/roots/root/00000000/br_gh782a.v line 4",
            f"Included file {root}/includes/roots/00000001/br_gh782a.vi line 1",
            f"File {root}/sources/roots/root/00000000/br_gh782a.v line 6",
        ]
        expected = ["File ./ivltests/br_gh782a.v line 4",
                    "Included file ./ivltests/br_gh782a.vi line 1",
                    "File ./ivltests/br_gh782a.v line 6"]
        normalize = lrm_corpus.ivtest_source_location_lines
        self.assertEqual(normalize("br_gh782a", actual), expected)
        for broken in [actual[0].replace("line 4", "line 5"),
                       actual[0].replace("br_gh782a.v", "wrong.v"),
                       actual[0].replace("sources/roots/root", "includes/roots"),
                       actual[0].replace("File ", "Included file ")]:
            with self.subTest(broken=broken):
                self.assertNotEqual(normalize("br_gh782a", [broken]), expected[:1])

    def test_timescale_and_unrelated_output_remain_exact(self):
        root = "objects/" + "b" * 64
        actual = [f"File {root}/sources/roots/root/00000000/br_gh782b.v line 10",
                  "Time scale of (t1) is 1s / 100ms"]
        normalize = lrm_corpus.ivtest_source_location_lines
        self.assertEqual(normalize("br_gh782b", actual),
                         ["File ./ivltests/br_gh782b.v line 10", actual[1]])
        self.assertEqual(normalize("unrelated", actual), actual)
        wrong_scale = actual.copy()
        wrong_scale[1] = "Time scale of (t1) is 1s / 10ms"
        self.assertNotEqual(normalize("br_gh782b", wrong_scale),
                            normalize("br_gh782b", actual))


class VestsFileRoundtripTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        base = self.root / "sources/ghdl/testsuite/vests/vhdl-93/billowitch"
        compliant = base / "compliant"
        iofiles = compliant / "iofiles"
        iofiles.mkdir(parents=True)

        commands = []
        for writer, reader, filename in lrm_corpus.VESTS_BILLOWITCH_FILE_ROUNDTRIPS:
            commands.append(
                f"run_compliant_test {writer} OUTPUT={filename}:iofiles/{filename}\n"
            )
            commands.append(
                f"run_compliant_test {reader} INPUT={filename}:iofiles/{filename}\n"
            )
            write_source(compliant / writer)
            write_source(compliant / reader)
            (iofiles / filename).write_bytes(b"static fixture bytes")
        (compliant / "compliant.exp").write_text("".join(commands), encoding="utf-8")

        analyzer_failure = base / "non_compliant/analyzer_failure"
        analyzer_failure.mkdir(parents=True)
        (analyzer_failure / "tc300.vhd").write_text(
            "entity tc300ent is end entity;\n"
            "architecture test_arch of tc300ent is begin end architecture;\n",
            encoding="utf-8",
        )
        (analyzer_failure / "non_compliant.exp").write_text(
            "run_non_compliant_test tc300.vhd\n", encoding="utf-8"
        )

        clifton = self.root / (
            "sources/ghdl/testsuite/vests/vhdl-93/clifton-labs/compliant"
        )
        writer = "functional/textio/write/character_array_write.vhd"
        reader = "functional/textio/read/character_array_read.vhdl"
        write_source(clifton / writer)
        write_source(clifton / reader)
        (clifton / "compliant1.exp").write_text(
            f"run_compliant_test {writer}\nrun_compliant_test {reader}\n",
            encoding="utf-8",
        )
        self.cases = lrm_corpus.vests_cases(self.root)
        self.by_id = {case.id: case for case in self.cases}

    def test_file_roundtrip_pairs_share_unique_chains(self) -> None:
        pairs = lrm_corpus.VESTS_BILLOWITCH_FILE_ROUNDTRIPS
        self.assertEqual(len(pairs), 20)
        chains = set()
        for writer, reader, filename in pairs:
            producer = self.by_id[f"billowitch/compliant/{writer}"]
            consumer = self.by_id[f"billowitch/compliant/{reader}"]
            self.assertIsNotNone(producer.chain)
            self.assertEqual(producer.chain, consumer.chain)
            self.assertTrue(producer.chain.startswith(
                lrm_corpus.VESTS_BILLOWITCH_FILE_CHAIN_PREFIX
            ))
            self.assertIn(producer.chain, lrm_corpus.VESTS_BILLOWITCH_FILE_CHAIN_OUTPUTS)
            self.assertIn(filename, {destination for _source, destination in consumer.copies})
            self.assertLess(self.cases.index(producer), self.cases.index(consumer))
            chains.add(producer.chain)
        self.assertEqual(len(chains), len(pairs))

    def test_filtering_reader_selects_its_producer(self) -> None:
        for writer, reader, _filename in lrm_corpus.VESTS_BILLOWITCH_FILE_ROUNDTRIPS:
            with self.subTest(reader=reader):
                selected = lrm_corpus.select_cases(SimpleNamespace(
                    suite=["vests"],
                    root=self.root,
                    filter=(
                        rf"vests/billowitch/compliant/{reader.replace('.', '[.]')}$"
                    ),
                    rerun_from=None,
                    rerun_code=None,
                ))
                self.assertEqual(
                    [case.id for case in selected],
                    [f"billowitch/compliant/{writer}",
                     f"billowitch/compliant/{reader}"],
                )

    def test_clifton_character_array_reader_selects_its_writer(self) -> None:
        writer = "clifton-labs/compliant/functional/textio/write/character_array_write.vhd"
        reader = "clifton-labs/compliant/functional/textio/read/character_array_read.vhdl"
        producer = self.by_id[writer]
        consumer = self.by_id[reader]
        self.assertEqual(producer.chain, consumer.chain)
        self.assertTrue(producer.chain.startswith(
            lrm_corpus.VESTS_CLIFTON_FILE_CHAIN_PREFIX
        ))
        self.assertIn(producer.chain, lrm_corpus.VESTS_FILE_CHAIN_OUTPUTS)

        selected = lrm_corpus.select_cases(SimpleNamespace(
            suite=["vests"],
            root=self.root,
            filter=r"character_array_read\.vhdl$",
            rerun_from=None,
            rerun_code=None,
        ))
        self.assertEqual([case.id for case in selected], [writer, reader])

    def test_clifton_reader_uses_fresh_writer_output(self) -> None:
        writer = self.by_id[
            "clifton-labs/compliant/functional/textio/write/character_array_write.vhd"
        ]
        reader = self.by_id[
            "clifton-labs/compliant/functional/textio/read/character_array_read.vhdl"
        ]
        workdir = self.root / "clifton-work"
        workdir.mkdir()
        output = workdir / "character_array.file"
        output.write_bytes(b"stale file from an earlier invocation")
        current_source = {"name": ""}
        writer_started_with_output = []
        observed = []

        def fake_run_command(_fsim, command, command_workdir, _env, _timeout, _engine):
            if command.stage == "compile":
                current_source["name"] = command.args[-1]
            elif command.stage == "simulate":
                file_path = command_workdir / "character_array.file"
                if current_source["name"] == "character_array_write.vhd":
                    writer_started_with_output.append(file_path.exists())
                    file_path.write_bytes(b"current character array output")
                elif current_source["name"] == "character_array_read.vhdl":
                    observed.append(file_path.read_bytes())
            return lrm_corpus.StepResult(command.stage, 0, 0.0, False, "")

        with patch.object(lrm_corpus, "run_command", side_effect=fake_run_command):
            records = lrm_corpus.run_chain(
                [writer, reader], Path("unused-fsim"), workdir, "interpreter", 1.0, True
            )

        self.assertEqual([record["status"] for record in records], ["pass", "pass"])
        self.assertEqual(writer_started_with_output, [False])
        self.assertEqual(observed, [b"current character array output"])

    def test_reader_keeps_current_chain_output_over_static_seed(self) -> None:
        writer = self.by_id["billowitch/compliant/tc624.vhd"]
        reader = self.by_id["billowitch/compliant/tc627.vhd"]
        workdir = self.root / "work"
        workdir.mkdir()
        (workdir / "iofile.36").write_bytes(b"stale output from an earlier invocation")
        current_source = {"name": ""}
        producer_started_with_output = []
        observed = []

        def fake_run_command(_fsim, command, command_workdir, _env, _timeout, _engine):
            if command.stage == "compile":
                current_source["name"] = command.args[-1]
            elif command.stage == "simulate":
                if current_source["name"] == "tc624.vhd":
                    producer_started_with_output.append(
                        (command_workdir / "iofile.36").exists()
                    )
                    (command_workdir / "iofile.36").write_bytes(b"current simulator output")
                elif current_source["name"] == "tc627.vhd":
                    observed.append((command_workdir / "iofile.36").read_bytes())
            return lrm_corpus.StepResult(command.stage, 0, 0.0, False, "")

        with patch.object(lrm_corpus, "run_command", side_effect=fake_run_command):
            records = lrm_corpus.run_chain(
                [writer, reader], Path("unused-fsim"), workdir, "interpreter", 1.0, True
            )

        self.assertEqual([record["status"] for record in records], ["pass", "pass"])
        self.assertEqual(producer_started_with_output, [False])
        self.assertEqual(observed, [b"current simulator output"])

    def test_missing_producer_output_does_not_fall_back_to_static_seed(self) -> None:
        writer = self.by_id["billowitch/compliant/tc624.vhd"]
        reader = self.by_id["billowitch/compliant/tc627.vhd"]
        workdir = self.root / "work"
        workdir.mkdir()
        (workdir / "iofile.36").write_bytes(b"stale output from an earlier invocation")
        current_source = {"name": ""}
        producer_started_with_output = []
        reader_inputs = []

        def fake_run_command(_fsim, command, command_workdir, _env, _timeout, _engine):
            returncode = 0
            if command.stage == "compile":
                current_source["name"] = command.args[-1]
            elif command.stage == "simulate":
                output = command_workdir / "iofile.36"
                if current_source["name"] == "tc624.vhd":
                    producer_started_with_output.append(output.exists())
                elif current_source["name"] == "tc627.vhd":
                    reader_inputs.append(output.read_bytes() if output.exists() else None)
                    if not output.exists():
                        returncode = 1
            return lrm_corpus.StepResult(command.stage, returncode, 0.0, False, "")

        with patch.object(lrm_corpus, "run_command", side_effect=fake_run_command):
            records = lrm_corpus.run_chain(
                [writer, reader], Path("unused-fsim"), workdir, "interpreter", 1.0, True
            )

        self.assertEqual(producer_started_with_output, [False])
        self.assertEqual(reader_inputs, [None])
        self.assertEqual([record["status"] for record in records], ["pass", "fail"])
        self.assertEqual(records[1]["stage"], "simulate")
        self.assertEqual(records[1]["detail"], "simulate failed")

    def test_tc300_is_skipped_for_nonportable_overflow_diagnostic(self) -> None:
        case = self.by_id["billowitch/non_compliant/analyzer_failure/tc300.vhd"]
        self.assertIn("out of scope", case.skip)
        self.assertIn("REAL bounds are host-dependent", case.skip)
        self.assertIn("IEEE 1076-1993 3.1.4/3.1.4.1", case.skip)


class VunitParentStatusTests(unittest.TestCase):
    def evaluate(self, transcript: str, *, tests=("alpha",), returncode=0):
        from pathlib import Path

        case = SimpleNamespace(
            expect="pass",
            checker="vunit",
            checker_data={"tests": list(tests)},
        )
        step = lrm_corpus.StepResult("simulate", returncode, 0.01, False, transcript)
        status, detail = lrm_corpus.check_outcome(case, [step], Path("."))
        children = lrm_corpus.vunit_subresults(case, [step])
        return status, detail, children

    def test_failed_check_fails_parent_and_preserves_child(self):
        status, detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\n"
            "VUNIT-SHIM: fail: check_equal got 1 expected 0\n"
            "VUNIT-SHIM: done failures=1\n"
        )
        self.assertEqual(status, "fail")
        self.assertIn("alpha", detail)
        self.assertEqual(children[0]["status"], "fail")
        self.assertIn("check_equal", children[0]["detail"])

    def test_missing_cleanup_fails_parent(self):
        status, _detail, children = self.evaluate("VUNIT-SHIM: start alpha\n")
        self.assertEqual(status, "fail")
        self.assertIn("cleanup", children[0]["detail"])

    def test_missing_expected_test_fails_parent(self):
        status, _detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\nVUNIT-SHIM: done failures=0\n",
            tests=("alpha", "beta"),
        )
        self.assertEqual(status, "fail")
        beta = next(child for child in children if child["test"] == "beta")
        self.assertEqual(beta["status"], "fail")
        self.assertEqual(beta["detail"], "test case never ran")

    def test_clean_all_pass_transcript_passes(self):
        status, detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\n"
            "VUNIT-SHIM: start beta\n"
            "VUNIT-SHIM: done failures=0\n",
            tests=("alpha", "beta"),
        )
        self.assertEqual((status, detail), ("pass", ""))
        self.assertEqual([child["status"] for child in children], ["pass", "pass"])

    def test_hdl_error_fails_active_child_and_parent(self):
        status, detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\n"
            "error[FSIM-HDL-REPORT]: assertion failed\n"
            "VUNIT-SHIM: done failures=0\n"
        )
        self.assertEqual(status, "fail")
        self.assertIn("FSIM-HDL-REPORT", detail)
        self.assertEqual(children[0]["test"], "alpha")
        self.assertEqual(children[0]["status"], "fail")

    def test_global_failure_before_named_test_fails_parent(self):
        status, _detail, children = self.evaluate(
            "VUNIT-SHIM: fail: global setup failed\n"
            "VUNIT-SHIM: start alpha\n"
            "VUNIT-SHIM: done failures=1\n"
        )
        self.assertEqual(status, "fail")
        self.assertEqual(children[0]["status"], "pass")
        self.assertEqual(children[-1]["test"], "<default>")
        self.assertEqual(children[-1]["status"], "fail")

    def test_nonzero_cleanup_failure_count_fails_parent(self):
        status, detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\nVUNIT-SHIM: done failures=1\n"
        )
        self.assertEqual(status, "fail")
        self.assertIn("reported 1 failure", detail)
        self.assertEqual(children[-1]["test"], "<default>")
        self.assertEqual(children[-1]["status"], "fail")

    def test_cleanup_failure_count_must_match_failure_markers(self):
        status, _detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\n"
            "VUNIT-SHIM: fail: check failed\n"
            "VUNIT-SHIM: done failures=0\n"
        )
        self.assertEqual(status, "fail")
        self.assertTrue(any(child["status"] == "fail" for child in children))

    def test_duplicate_cleanup_marker_fails_parent(self):
        status, _detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\n"
            "VUNIT-SHIM: done failures=0\n"
            "VUNIT-SHIM: done failures=0\n"
        )
        self.assertEqual(status, "fail")
        self.assertTrue(any(child["status"] == "fail" for child in children))

    def test_nonzero_simulation_exit_fails_even_if_cleanup_is_clean(self):
        status, detail, _children = self.evaluate(
            "VUNIT-SHIM: start alpha\nVUNIT-SHIM: done failures=0\n",
            returncode=1,
        )
        self.assertEqual(status, "fail")
        self.assertEqual(detail, "simulate failed")


    def test_clean_default_test_passes(self):
        status, detail, children = self.evaluate(
            "VUNIT-SHIM: done failures=0\n", tests=("<default>",)
        )
        self.assertEqual((status, detail), ("pass", ""))
        self.assertEqual(children, [{"test": "<default>", "status": "pass", "detail": ""}])

    def test_unexpected_start_fails_parent(self):
        status, _detail, children = self.evaluate(
            "VUNIT-SHIM: start unexpected\nVUNIT-SHIM: done failures=0\n"
        )
        self.assertEqual(status, "fail")
        self.assertTrue(any(child["status"] == "fail" for child in children))

    def test_duplicate_start_fails_child_and_parent(self):
        status, _detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\n"
            "VUNIT-SHIM: start alpha\n"
            "VUNIT-SHIM: done failures=0\n"
        )
        self.assertEqual(status, "fail")
        self.assertEqual(children[0]["status"], "fail")

    def test_failure_after_cleanup_fails_parent(self):
        status, _detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\n"
            "VUNIT-SHIM: done failures=0\n"
            "VUNIT-SHIM: fail: late failure\n"
        )
        self.assertEqual(status, "fail")
        self.assertTrue(any(child["status"] == "fail" for child in children))

    def test_cleanup_without_failure_count_fails_parent(self):
        status, _detail, children = self.evaluate(
            "VUNIT-SHIM: start alpha\nVUNIT-SHIM: done\n"
        )
        self.assertEqual(status, "fail")
        self.assertTrue(any(child["status"] == "fail" for child in children))


class NvcExpectedRuntimeErrorTests(unittest.TestCase):
    expected = ("FSIM-RUN-0001", "VHDL integer subtype range check failed", 1)

    def make_case(self) -> lrm_corpus.Case:
        return lrm_corpus.Case(
            suite="nvc", id="issue502", group="vhdl-93", commands=[],
            expect="fail", checker="vhdl_clean", expected_runtime_error=self.expected,
        )

    @staticmethod
    def step(stage: str, returncode: int | None, output: str = "",
             timed_out: bool = False) -> lrm_corpus.StepResult:
        return lrm_corpus.StepResult(stage, returncode, 0.0, timed_out, output)

    def test_only_issue502_gets_typed_runtime_failure_expectation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            regress = root / "sources/nvc/test/regress"
            regress.mkdir(parents=True)
            (regress / "testlist.txt").write_text(
                "issue502 normal\nissue503 normal\n", encoding="utf-8")
            for name in ("issue502", "issue503"):
                (regress / f"{name}.vhd").write_text(
                    f"entity {name} is end entity;\n"
                    f"architecture test of {name} is begin end architecture;\n",
                    encoding="utf-8",
                )

            cases = {case.id: case for case in lrm_corpus.nvc_cases(root)}

        self.assertEqual(cases["issue502"].group, "vhdl-93")
        self.assertEqual(cases["issue502"].expect, "fail")
        self.assertEqual(cases["issue502"].expected_runtime_error, self.expected)
        self.assertEqual(cases["issue503"].expect, "pass")
        self.assertIsNone(cases["issue503"].expected_runtime_error)

    def test_expected_integer_subtype_error_requires_successful_frontend(self) -> None:
        status, detail = lrm_corpus.check_outcome(
            self.make_case(),
            [self.step("compile", 0), self.step("elaborate", 0),
             self.step("simulate", 1,
                       "error[FSIM-RUN-0001]: SimIR process 0, instruction 29: "
                       "VHDL integer subtype range check failed; "
                       "process 'issue502.g[1].u.p1'")],
            Path("."),
        )
        self.assertEqual(status, "pass")
        self.assertIn("expected runtime diagnostic FSIM-RUN-0001", detail)

        for failed_stage, failed_steps in (
            ("compile", [self.step(
                "compile", 1,
                "error[FSIM-RUN-0001]: VHDL integer subtype range check failed")]),
            ("elaborate", [self.step("compile", 0), self.step(
                "elaborate", 1,
                "error[FSIM-RUN-0001]: VHDL integer subtype range check failed")]),
        ):
            with self.subTest(failed_stage=failed_stage):
                status, detail = lrm_corpus.check_outcome(
                    self.make_case(), failed_steps, Path("."))
                self.assertEqual(status, "fail")
                self.assertIn(f"{failed_stage} did not pass", detail)

    def test_unrelated_runtime_failure_or_success_does_not_satisfy_expectation(self) -> None:
        frontend = [self.step("compile", 0), self.step("elaborate", 0)]
        for output, returncode in (
            ("error[FSIM-RUN-0002]: unsupported operation", 1),
            ("error[FSIM-RUN-0001]: VHDL array index check failed", 1),
            ("error[FSIM-RUN-0001]: VHDL integer subtype range check failed", 0),
        ):
            with self.subTest(output=output, returncode=returncode):
                status, _ = lrm_corpus.check_outcome(
                    self.make_case(),
                    frontend + [self.step("simulate", returncode, output)],
                    Path("."),
                )
                self.assertEqual(status, "fail")

    def test_diagnostic_does_not_mask_timeout_crash_or_unexpected_exit(self) -> None:
        frontend = [self.step("compile", 0), self.step("elaborate", 0)]
        diagnostic = (
            "error[FSIM-RUN-0001]: SimIR process 0, instruction 29: "
            "VHDL integer subtype range check failed; "
            "process 'issue502.g[1].u.p1'"
        )
        cases = (
            (self.step("simulate", 1, diagnostic, timed_out=True), "timeout"),
            (self.step("simulate", -11, diagnostic), "crash"),
            (self.step("simulate", 127, diagnostic), "crash"),
            (self.step("simulate", 2, diagnostic), "fail"),
        )
        for simulate, expected_status in cases:
            with self.subTest(returncode=simulate.returncode,
                              timed_out=simulate.timed_out):
                status, detail = lrm_corpus.check_outcome(
                    self.make_case(), frontend + [simulate], Path("."))
                self.assertEqual(status, expected_status)
                if simulate.returncode == 2:
                    self.assertIn("expected normal runtime-failure status 1", detail)


if __name__ == "__main__":
    unittest.main()
