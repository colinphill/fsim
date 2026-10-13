#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Focused tests for VESTs writer/reader chain planning in lrm_corpus.py."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from scripts import lrm_corpus


def write_source(path: Path) -> None:
    entity = f"{path.stem}ent"
    path.write_text(
        f"entity {entity} is\nend entity;\n"
        f"architecture test_arch of {entity} is\nbegin\nend architecture;\n",
        encoding="utf-8",
    )


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
        self.cases = lrm_corpus.vests_cases(self.root)
        self.by_id = {case.id: case for case in self.cases}

    def test_file_roundtrip_pairs_share_unique_chains(self) -> None:
        pairs = lrm_corpus.VESTS_BILLOWITCH_FILE_ROUNDTRIPS
        self.assertEqual(len(pairs), 14)
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
        selected = lrm_corpus.select_cases(SimpleNamespace(
            suite=["vests"],
            root=self.root,
            filter=r"vests/billowitch/compliant/tc627\.vhd$",
            rerun_from=None,
            rerun_code=None,
        ))
        self.assertEqual(
            [case.id for case in selected],
            ["billowitch/compliant/tc624.vhd", "billowitch/compliant/tc627.vhd"],
        )

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


if __name__ == "__main__":
    unittest.main()
