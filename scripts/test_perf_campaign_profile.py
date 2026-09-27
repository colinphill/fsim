#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Checks that profile attribution never hides unknown or malformed samples."""

import ctypes
import os
import signal
import subprocess
import sys
import time
import unittest

from perf_campaign_profile import SamplingError, _stop_group, parse_self_report


class AttributionTests(unittest.TestCase):
    def test_unresolved_samples_remain_in_denominator(self):
        result = parse_self_report(
            '# Overhead;Samples;Shared Object;Symbol\n'
            '50.00%;5;fsim;[.] fsim::run()\n'
            '20.00%;2;perf-42.map;[.] native_process_7\n'
            '20.00%;2;fsim;[.] 0x0000000000001234\n'
            '10.00%;1;[unknown];[.] missing\n'
        )
        self.assertEqual(result['samples'], 10)
        self.assertEqual(result['unresolved_samples'], 3)
        self.assertEqual(result['unresolved_sample_percent'], 30)
        self.assertEqual(len(result['self_samples']), 4)

    def test_missing_samples_are_explicit(self):
        result = parse_self_report('# No samples\n')
        self.assertEqual(result['status'], 'no_samples')
        self.assertIsNone(result['unresolved_sample_percent'])

    def test_perf_ipc_trailer_cannot_conceal_unknown_addresses(self):
        result = parse_self_report(
            '# Total Lost Samples: 3\n'
            '100.00%;4;fsim;[.] 0x0000000000001234;- -\n'
        )
        self.assertEqual(result['unresolved_samples'], 4)
        self.assertEqual(result['reported_lost_samples'], [3])
        self.assertEqual(result['self_samples'][0]['symbol'],
                         '[.] 0x0000000000001234')

    def test_malformed_rows_cannot_disappear(self):
        for row in ('20%;bad;fsim;[.] run', '20%;2;fsim',
                    '20%;-2;fsim;[.] run', '120%;2;fsim;[.] run'):
            with self.subTest(row=row), self.assertRaises(SamplingError):
                parse_self_report(row)


@unittest.skipUnless(sys.platform == 'linux', 'Linux process-group profiling')
class ProcessCleanupTests(unittest.TestCase):
    def test_terminates_descendant_after_launcher_exits(self):
        # Adopt this test's orphaned grandchild so the test also reaps it.
        libc = ctypes.CDLL(None, use_errno=True)
        previous = ctypes.c_int()
        if libc.prctl(37, ctypes.byref(previous), 0, 0, 0) != 0:
            self.skipTest('cannot inspect child subreaper setting')
        if libc.prctl(36, 1, 0, 0, 0) != 0:
            self.skipTest('cannot adopt test descendant')
        child_code = (
            'import os, signal; '
            'signal.signal(signal.SIGTERM, signal.SIG_IGN); '
            'print(os.getpid(), flush=True); signal.pause()'
        )
        launcher_code = (
            'import signal, subprocess, sys; '
            f'child = subprocess.Popen([sys.executable, "-c", {child_code!r}], '
            'stdout=subprocess.PIPE, text=True); '
            'print(child.stdout.readline().strip(), flush=True); signal.pause()'
        )
        process = None
        descendant = None
        reaped = False
        try:
            process = subprocess.Popen(
                [sys.executable, '-c', launcher_code],
                start_new_session=True, stdout=subprocess.PIPE, text=True,
            )
            descendant = int(process.stdout.readline().strip())
            _stop_group(process)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                waited, status = os.waitpid(descendant, os.WNOHANG)
                if waited:
                    reaped = True
                    self.assertTrue(os.WIFSIGNALED(status))
                    self.assertEqual(os.WTERMSIG(status), signal.SIGKILL)
                    break
                time.sleep(0.01)
            self.assertTrue(reaped, 'TERM-ignoring descendant survived cleanup')
        finally:
            if process is not None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                process.stdout.close()
            if descendant is not None and not reaped:
                os.waitpid(descendant, 0)
            libc.prctl(36, previous.value, 0, 0, 0)


if __name__ == '__main__':
    unittest.main()
