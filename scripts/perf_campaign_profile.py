#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Separate Linux CPU sampling for the cross-simulator campaign.

Sampling changes execution costs and asynchronous JIT scheduling. Nothing
returned here is an uninstrumented performance measurement.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time
from typing import Any


class SamplingError(RuntimeError):
    """Sampling or attribution evidence could not be collected."""


def parse_self_report(report: str) -> dict[str, Any]:
    """Parse unfiltered exclusive samples, retaining unresolved attribution."""
    rows: list[dict[str, Any]] = []
    for line in report.splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        # Some perf versions append an IPC column even when --fields requests
        # only these four fields. Keep it separate from the symbol; otherwise
        # a trailing IPC value could conceal an unresolved hexadecimal address.
        fields = [field.strip() for field in line.split(';')]
        if len(fields) < 4:
            raise SamplingError(f"unexpected perf histogram row: {line}")
        try:
            percentage = float(fields[0].removesuffix('%'))
            samples = int(fields[1])
        except ValueError as error:
            raise SamplingError(f"invalid perf sample counts: {line}") from error
        if samples < 0 or not 0 <= percentage <= 100:
            raise SamplingError(f"invalid perf histogram values: {line}")
        dso, symbol = fields[2:4]
        bare_symbol = re.sub(r'^\[.\]\s*', '', symbol)
        unresolved = (
            '[unknown]' in dso or '[unknown]' in symbol
            or re.fullmatch(r'(?:0x)?[0-9a-fA-F]{8,}', bare_symbol) is not None
        )
        rows.append({
            'overhead_percent': percentage,
            'samples': samples,
            'dso': dso,
            'symbol': symbol,
            'unresolved': unresolved,
            'additional_fields': fields[4:],
        })
    total = sum(row['samples'] for row in rows)
    unresolved = sum(row['samples'] for row in rows if row['unresolved'])
    return {
        'samples': total,
        'unresolved_samples': unresolved,
        'unresolved_sample_percent': 100 * unresolved / total if total else None,
        'status': 'sampled' if total else 'no_samples',
        'self_samples': rows,
        'reported_lost_samples': [int(value) for value in re.findall(
            r'^#\s*Total Lost Samples:\s*(\d+)\s*$', report, re.MULTILINE)],
    }


def _hash_file(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def _stop_group(process: subprocess.Popen[bytes]) -> None:
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        process.wait()
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    finally:
        # The launcher may exit on TERM while a descendant ignores it. The
        # process group remains ours until every member has stopped.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def _run(command: list[str], cwd: Path, environment: dict[str, str],
         cpu: int, stdout: Path, stderr: Path, timeout: float) -> tuple[int, float]:
    started = time.perf_counter()
    with stdout.open('wb') as output, stderr.open('wb') as errors:
        process = subprocess.Popen(
            command, cwd=cwd, env=environment, stdout=output, stderr=errors,
            start_new_session=True,
            preexec_fn=lambda: os.sched_setaffinity(0, {cpu}),
        )
        try:
            return_code = process.wait(timeout=timeout)
        except BaseException:
            _stop_group(process)
            raise
    return return_code, time.perf_counter() - started


def sample_command(command: list[str], cwd: Path, environment: dict[str, str],
                   cpu: int, stem: Path, timeout: float = 7200,
                   frequency: int = 99) -> dict[str, Any]:
    """Run a fresh-workspace phase with sampling and retained JIT attribution.

    The caller creates the fresh workspace, checks HDL results against its
    preflight, and keeps this entire invocation separate from timing samples.
    The record has the usual phase fields plus a diagnostic-only sampling
    payload. Profile RSS includes the sampler process and is not baseline RSS.
    """
    perf_name = shutil.which('perf', path=environment.get('PATH'))
    if perf_name is None:
        raise SamplingError('perf is required for CPU sampling')
    if frequency <= 0 or timeout <= 0:
        raise SamplingError('sampling frequency and timeout must be positive')
    if cpu not in os.sched_getaffinity(0):
        raise SamplingError(f'CPU {cpu} is outside the permitted affinity')
    perf = Path(perf_name).resolve()
    cwd = Path(cwd).resolve()
    stem = Path(stem).resolve()
    stem.parent.mkdir(parents=True, exist_ok=True)
    data_path = stem.with_suffix('.perf.data')
    if data_path.exists():
        raise SamplingError(f'refusing to replace existing profile: {data_path}')
    stdout_path = stem.with_suffix('.stdout')
    stderr_path = stem.with_suffix('.stderr')
    rss_path = stem.with_suffix('.profile-rss')
    profile_environment = environment.copy()
    profile_environment.update({
        'FSIM_PERF_MAP': '1', 'LC_ALL': 'C', 'DEBUGINFOD_URLS': '',
    })
    record_command = [
        '/usr/bin/time', '-f', 'PROFILE_RSS_KIB=%M', '-o', str(rss_path),
        '--', str(perf), 'record', '--no-buildid-cache',
        '-e', 'cpu-clock:u', '-F', str(frequency),
        '--call-graph', 'dwarf,8192', '-o', str(data_path), '--', *command,
    ]
    try:
        return_code, elapsed = _run(
            record_command, cwd, profile_environment, cpu,
            stdout_path, stderr_path, timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise SamplingError(f'CPU sampling failed; inspect {stderr_path}: {error}') from error
    if return_code:
        raise SamplingError(
            f'sampled command exited {return_code}; inspect {stdout_path} and {stderr_path}'
        )
    rss_match = re.search(r'^PROFILE_RSS_KIB=(\d+)$', rss_path.read_text(), re.MULTILINE)
    if rss_match is None:
        raise SamplingError(f'missing profile RSS record: {rss_path}')

    commands = [record_command]

    def analyze(arguments: list[str], suffix: str) -> Path:
        destination = stem.with_suffix(suffix)
        errors = Path(str(destination) + '.stderr')
        analysis_command = [str(perf), *arguments, '-i', str(data_path)]
        commands.append(analysis_command)
        try:
            status, _ = _run(
                analysis_command, cwd, profile_environment, cpu,
                destination, errors, timeout,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise SamplingError(f'perf analysis failed; inspect {errors}: {error}') from error
        if status:
            raise SamplingError(f'perf analysis exited {status}; inspect {errors}')
        return destination

    # Restrict map retention to processes actually sampled by this invocation.
    # The original /tmp maps are read-only here; never replace another process's
    # map when retaining evidence or decoding a later run.
    pids_path = analyze(['script', '-G', '-F', 'pid'], '.sample-pids.txt')
    pids: set[int] = set()
    for line in pids_path.read_text().splitlines():
        if line.strip():
            try:
                pids.add(int(line.strip()))
            except ValueError as error:
                raise SamplingError(f'invalid sampled PID in {pids_path}: {line}') from error
    maps = []
    map_directory = stem.with_suffix('.jit-maps')
    for pid in sorted(pids):
        source = Path(f'/tmp/perf-{pid}.map')
        if not source.is_file():
            continue
        map_directory.mkdir(exist_ok=True)
        destination = map_directory / source.name
        shutil.copyfile(source, destination)
        maps.append({
            'pid': pid, 'path': str(destination), 'original_path': str(source),
            'sha256': _hash_file(destination),
            'symbols': len(destination.read_text().splitlines()),
        })

    report_path = analyze([
        'report', '--stdio', '--stdio-color', 'never', '--no-children',
        '--percent-limit', '0', '--call-graph', 'none',
        '--field-separator', ';', '--sort', 'dso,symbol',
        '--fields', 'overhead,sample,dso,symbol',
    ], '.self-report.txt')
    attribution = parse_self_report(report_path.read_text())
    chain_path = analyze([
        'report', '--stdio', '--stdio-color', 'never', '--children',
        '--percent-limit', '1', '--call-graph', 'graph,1,caller',
    ], '.call-paths.txt')
    loss_path = analyze([
        'script', '-G', '--show-lost-events', '-F', 'pid',
    ], '.loss-report.txt')
    loss_records = [line for line in loss_path.read_text().splitlines()
                    if 'LOST' in line.upper()]
    return {
        'command': command, 'profile_command': record_command, 'cwd': str(cwd),
        'returncode': return_code, 'elapsed_seconds': elapsed,
        'peak_rss_kib': int(rss_match.group(1)),
        'stdout': str(stdout_path), 'stderr': str(stderr_path),
        'instrumented': True,
        'sampling': {
            **attribution,
            'event': 'cpu-clock:u', 'frequency_hz': frequency,
            'call_graph': 'dwarf,8192', 'cpu_affinity': [cpu],
            'perf': str(perf), 'perf_sha256': _hash_file(perf),
            'perf_data': str(data_path), 'self_report': str(report_path),
            'call_paths': str(chain_path), 'loss_report': str(loss_path),
            'reported_loss_records': loss_records,
            'sampled_pids': sorted(pids), 'jit_maps': maps,
            'commands': commands,
            'limitations': [
                'Instrumented wall time and RSS cannot establish a speedup.',
                'RSS includes the sampler; JIT scheduling can change during sampling.',
                'User CPU samples exclude kernel CPU and off-CPU waits or I/O latency.',
                'Unresolved counts describe sampled instruction pointers; call stacks may be incomplete.',
                'DWARF stack capture is limited to 8192 bytes; JIT map names do not provide unwind metadata.',
                'Self overhead percentages are weighted by sampled periods, not sample counts.',
            ],
        },
    }
