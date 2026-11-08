#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail when a run-state data member is missing from the classification.

docs/run-state-classification.tsv lists every data member of the classes named
in TARGETS below with its save/restart class (#128, IEEE 1800-2023 D.9, IEEE
1076-2019 20.7-20.9). A member added to one of these classes without a row, or
a row naming a member that no longer exists, fails this check, so the
save/restart image cannot silently drift from the state the engines hold.

The member list is read straight from the class bodies with a small C++ scanner
(comments and literals stripped, depth-0 statements of the class body, function
declarations and static constants skipped); it needs no compiler.

  check_run_state_classification.py SOURCE_DIR          verify
  check_run_state_classification.py SOURCE_DIR --emit   print scope<TAB>member<TAB>declaration

It also checks that every member classified P or P+ids in the engine scopes
(sched.*, interp.*, process.*, kernel.*) has a writer: the save code in
src/runtime/simir_run_state_*.cpp names each member it writes with an
FSIM_RS_IO / FSIM_RS_TABLE / FSIM_RS_SAME / FSIM_RS_CUSTOM macro whose first two
arguments are the scope and the member as the table spells them. A saved member
without a macro, or a macro naming a member the table does not list as saved,
fails the check. Members whose writer is still to come are listed in
NO_WRITER_YET with the reason, and the check fails when such a member gains a
writer (so the list only shrinks). The application and foreign scopes (sim.*,
foreign.*) belong to the app-level sections and are not checked here.

Every class S row of the engine scopes and of the sim.* and foreign.* scopes
(their audits sit in IDLE_FILES) must be covered by the save audit, once:
FSIM_RS_IDLE beside the exact test that refuses the save (FSIM-SAVE-0011) when
the member is not at rest. A member that holds nothing at rest is class W (work
storage or cache, assigned or cleared before every use) or D (derived), with no
marker; there is no bare "transient" marker (#185).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

CLASSES = {
    "P": "plain value, saved as is",
    "P+ids": "saved with ids/handles rebound on restore",
    "C": "closure: saved only through a registered scheduler event kind",
    "D": "derived: rebuilt from saved state or the artifact",
    "S": "scratch: empty at the save point (non-empty refuses the save)",
    "W": "working storage or cache: assigned or cleared before every use",
    "Cfg": "configuration or hook: rebuilt from the same command line",
    "Prof": "profile counter: not saved",
}

# (scope, file, class-header regex). The regex must match the opening of the
# class body; the first match in the file is used.
RT = "src/runtime/"
APP = "src/app/"
TARGETS = [
    ("sched.impl", RT + "scheduler_internal.hpp", r"struct Scheduler::Impl \{"),
    ("sched.current_slot", RT + "scheduler_internal.hpp", r"struct CurrentSlot \{"),
    ("sched.future_slot", RT + "scheduler_internal.hpp", r"struct FutureSlot \{"),
    ("sched.work_queue", RT + "scheduler_internal.hpp", r"struct WorkQueue \{"),
    ("interp.impl", RT + "simir_internal.hpp", r"struct Interpreter::Impl : SchedulerBatchTask \{"),
    ("interp.signal_hot", RT + "simir_signal_storage.hpp", r"struct SignalHot \{"),
    ("interp.signal_cold", RT + "simir_signal_storage.hpp", r"struct SignalCold \{"),
    ("interp.update_slot", RT + "simir_internal.hpp", r"struct SystemVerilogUpdateSlot \{"),
    ("interp.pending_update", RT + "simir_internal.hpp", r"struct PendingUpdate \{"),
    ("interp.pending_inertial", RT + "simir_internal.hpp", r"struct PendingInertialWrite \{"),
    ("interp.projected_driver", RT + "simir_internal.hpp", r"struct ProjectedDriverState \{"),
    ("interp.mailbox", RT + "simir_internal.hpp", r"struct MailboxState \{"),
    ("interp.semaphore", RT + "simir_internal.hpp", r"struct SemaphoreState \{"),
    ("interp.dynamic_event", RT + "simir_internal.hpp", r"struct DynamicEventState \{"),
    ("interp.event_state", RT + "simir_internal.hpp", r"struct EventState \{"),
    ("interp.fork_group", RT + "simir_internal.hpp", r"struct ForkGroup \{"),
    ("interp.cohort", RT + "simir_internal.hpp", r"struct StaticSensitivityCohort \{"),
    ("interp.file", RT + "simir_internal.hpp", r"struct FileState \{"),
    ("process.state", RT + "simir_internal.hpp", r"struct ProcessState \{"),
    ("process.cold", RT + "simir_internal.hpp", r"struct ProcessColdState \{"),
    ("process.dynamic_wait", RT + "simir_internal.hpp", r"struct ProcessDynamicWaitState \{"),
    ("process.callable_frame", RT + "simir_internal.hpp", r"struct ProcessCallableFrameState \{"),
    ("process.fork", RT + "simir_internal.hpp", r"struct ProcessForkState \{"),
    ("process.vital", RT + "simir_internal.hpp", r"struct ProcessVitalState \{"),
    ("kernel.impl", RT + "simir_static_kernel_state.hpp", r"class Interpreter::Impl::StaticKernel \{"),
    ("kernel.member", RT + "simir_static_kernel_state.hpp", r"struct Member \{"),
    ("kernel.partition", RT + "simir_static_kernel_state.hpp", r"struct Partition \{"),
    ("kernel.dissolved", RT + "simir_static_kernel_state.hpp", r"struct DissolvedPartition \{"),
    ("kernel.slot", RT + "simir_static_kernel_state.hpp", r"struct Slot \{"),
    ("kernel.input", RT + "simir_static_kernel_state.hpp", r"struct Input \{"),
    ("kernel.container", RT + "simir_static_kernel_state.hpp", r"struct Container \{"),
    ("kernel.thread", RT + "simir_static_kernel_state.hpp", r"struct Thread \{"),
    ("kernel.fork_group", RT + "simir_static_kernel_state.hpp", r"struct ForkGroup \{"),
    ("kernel.waiter", RT + "simir_static_kernel_state.hpp", r"struct Waiter \{"),
    ("kernel.timer", RT + "simir_static_kernel_state.hpp", r"struct Timer \{"),
    ("kernel.net_delay_site", RT + "simir_static_kernel_state.hpp", r"struct NetDelaySite \{"),
    ("kernel.process_record", RT + "simir_static_kernel_state.hpp", r"struct ProcessRecord \{"),
    ("kernel.sync_wait", RT + "simir_static_kernel_state.hpp", r"struct SynchronizationWait \{"),
    ("kernel.hosted_timer", RT + "simir_static_kernel_state.hpp", r"struct HostedTimer \{"),
    ("kernel.hosted_ready", RT + "simir_static_kernel_state.hpp", r"struct HostedReady \{"),
    ("kernel.hosted_delayed", RT + "simir_static_kernel_state.hpp", r"struct HostedDelayed \{"),
    ("sim.impl", APP + "application_simulation_internal.hpp", r"struct Simulation::Impl \{"),
    ("foreign.vpi_host", APP + "application_vpi_foreign.cpp", r"class Host \{"),
    ("foreign.vpi_live", APP + "application_vpi_foreign.cpp", r"class LiveService final : public Host, public VpiForeignService \{"),
    ("foreign.vhpi_live", APP + "application_vhpi_foreign.cpp", r"class LiveService final : public VhpiForeignService \{"),
    ("foreign.tf_coordinator", APP + "application_tf_scheduler.cpp", r"struct TfSchedulerCoordinator::Impl \{"),
    ("foreign.vpi_callbacks", RT + "vpi_callback.cpp", r"struct SystemVerilogVpiCallbackManager::Impl \{"),
    ("foreign.vpi_control", RT + "vpi_control.cpp", r"struct SystemVerilogVpiControlService::Impl \{"),
    ("foreign.vpi_time", RT + "vpi_time.cpp", r"struct SystemVerilogVpiTimeService::Impl \{"),
]

TSV = "docs/run-state-classification.tsv"

# Scopes the engine sections (S3-S5) write; the others are app-level.
ENGINE_PREFIXES = ("sched.", "interp.", "process.", "kernel.")
WRITER_GLOB = "src/runtime/simir_run_state*.cpp"
WRITER_MACRO = re.compile(
    r'FSIM_RS_(?:IO|TABLE|SAME|CUSTOM)\(\s*"([\w.]+)"\s*,\s*(\w+)')

SCRATCH_MACRO = re.compile(
    r'FSIM_RS_(IDLE)\(\s*"([\w.]+)"\s*,\s*(\w+)')
# Files besides WRITER_GLOB that hold exact rest-state tests.
IDLE_FILES = (
    "src/runtime/scheduler_events.cpp",
    "src/runtime/vpi_callback.cpp",
    "src/app/application_simulation_impl_save.cpp",
    "src/app/application_tf_scheduler.cpp",
    "src/app/application_vpi_foreign.cpp",
    "src/app/application_vhpi_foreign.cpp",
)
# Scopes whose class S rows need an exact test: the engines and, since the
# foreign scopes got their audits (#128), the application and the foreign
# interfaces.
AUDITED_PREFIXES = ENGINE_PREFIXES + ("sim.", "foreign.")

# (scope, member) -> why no section writes it yet. Filled when a member of the
# table cannot be written by the engine sections.
NO_WRITER_YET: dict[tuple[str, str], str] = {}


def strip(src: str) -> str:
    """Remove comments and replace string/char literals with empty ones."""
    out: list[str] = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * src.count("\n", i, j))
            i = j
            continue
        if c == '"':
            if i > 0 and src[i - 1] == "R":
                m = re.match(r'"([^(]*)\(', src[i:])
                delim = m.group(1) if m else ""
                end = src.find(")" + delim + '"', i)
                end = n if end < 0 else end + len(delim) + 2
                out.append('""' + "\n" * src.count("\n", i, end))
                i = end
                continue
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            out.append('""')
            i = j + 1
            continue
        if c == "'" and not (i > 0 and src[i - 1].isalnum()):
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            out.append("''")
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def class_body(text: str, header: str, where: str) -> str:
    m = re.search(header, text)
    if not m:
        raise SystemExit(f"{where}: class header not found: {header}")
    start = text.index("{", m.start())
    depth = 0
    j = start
    while True:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return text[start + 1:j]


def statements(body: str):
    """Yield (text, ends_in_body) for each depth-0 statement of a class body."""
    cur: list[str] = []
    depth = par = 0
    i, n = 0, len(body)
    while i < n:
        c = body[i]
        if c == "{":
            depth += 1
            if depth == 1:
                cur.append("{")
            i += 1
            continue
        if c == "}":
            depth -= 1
            if depth == 0:
                cur.append("}")
                k = i + 1
                while k < n and body[k] in " \t\n":
                    k += 1
                if not (k < n and body[k] in ";,"):
                    yield "".join(cur), True
                    cur = []
            i += 1
            continue
        if depth > 0:
            i += 1
            continue
        if c == "(":
            par += 1
        elif c == ")":
            par -= 1
        if c == ";" and par == 0:
            yield "".join(cur), False
            cur = []
            i += 1
            continue
        if (c == ":" and par == 0 and not body.startswith("::", i)
                and not (i > 0 and body[i - 1] == ":")
                and "".join(cur).strip() in ("public", "private", "protected")):
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1


def strip_angle(text: str) -> str:
    out, depth = [], 0
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">" and depth > 0:
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return "".join(out)


def member_of(statement: str):
    s = " ".join(statement.split())
    if not s:
        return None
    if re.match(r"^(using|typedef|friend|static_assert|template|enum|namespace)\b", s):
        return None
    if re.match(r"^(struct|class|union)\b", s) and not re.search(
            r"\}\s*[A-Za-z_]\w*\s*(\{\s*\})?$", s):
        return None
    if re.match(r"^(static\s+constexpr|constexpr\s+static|static\s+inline\s+constexpr"
                r"|inline\s+static\s+constexpr|static\s+const)\b", s):
        return None
    flat = s
    prev = None
    while prev != flat:
        prev = flat
        flat = re.sub(r"\{[^{}]*\}", "", flat)
    flat = re.sub(r"\s*=\s*.*$", "", flat)
    plain = strip_angle(flat)
    if "(" in plain or "operator" in plain:
        return None
    m = re.search(r"([A-Za-z_]\w*)\s*(\[[^\]]*\])*\s*$", plain.strip())
    if not m or m.group(1) in ("default", "delete", "const", "override", "noexcept"):
        return None
    decl = re.sub(r"\{[^{}]*\}", "{}", s)
    decl = re.sub(r"\s*=.*$", "", decl)
    return m.group(1), decl


def members(root: Path, path: str, header: str):
    text = strip((root / path).read_text(encoding="utf-8"))
    body = class_body(text, header, path)
    result = []
    for stmt, is_function in statements(body):
        if is_function:
            continue
        found = member_of(stmt)
        if found:
            result.append(found)
    return result


def read_tsv(root: Path):
    rows = {}
    for number, line in enumerate(
            (root / TSV).read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#") or line.startswith("scope\t"):
            continue
        fields = line.split("\t")
        if len(fields) != 4:
            raise SystemExit(f"{TSV}:{number}: expected scope, member, class, note")
        scope, member, klass, _note = fields
        if klass not in CLASSES:
            raise SystemExit(f"{TSV}:{number}: unknown class {klass!r}")
        if (scope, member) in rows:
            raise SystemExit(f"{TSV}:{number}: duplicate row {scope} {member}")
        rows[(scope, member)] = klass
    return rows


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    root = Path(argv[1])
    if "--emit" in argv[2:]:
        for scope, path, header in TARGETS:
            for name, decl in members(root, path, header):
                print(f"{scope}\t{name}\t{decl}")
        return 0
    rows = read_tsv(root)
    seen = set()
    problems = []
    for scope, path, header in TARGETS:
        found = members(root, path, header)
        if not found:
            problems.append(f"{scope}: no data members found in {path}")
        for name, _decl in found:
            seen.add((scope, name))
            if (scope, name) not in rows:
                problems.append(
                    f"unclassified run-state member {scope} {name} ({path})")
    scopes = {scope for scope, _p, _h in TARGETS}
    for scope, name in sorted(rows):
        if scope in scopes and (scope, name) not in seen:
            problems.append(f"stale classification row {scope} {name}")
        elif scope not in scopes:
            problems.append(f"classification row for unknown scope {scope}")
    writers = {}
    for path in sorted(root.glob(WRITER_GLOB)):
        text = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))
        for match in WRITER_MACRO.finditer(text):
            writers.setdefault((match.group(1), match.group(2)), path.name)
    for (scope, name), where in sorted(writers.items()):
        klass = rows.get((scope, name))
        if klass is None:
            problems.append(f"{where}: writer for {scope} {name}, which the "
                            f"table does not list")
        elif klass not in ("P", "P+ids"):
            problems.append(f"{where}: writer for {scope} {name}, which the "
                            f"table classifies {klass}")
    for (scope, name), klass in sorted(rows.items()):
        if klass not in ("P", "P+ids") or not scope.startswith(ENGINE_PREFIXES):
            continue
        if (scope, name) in writers:
            if (scope, name) in NO_WRITER_YET:
                problems.append(f"{scope} {name} has a writer now: remove it "
                                f"from NO_WRITER_YET")
        elif (scope, name) not in NO_WRITER_YET:
            problems.append(f"saved member {scope} {name} ({klass}) has no "
                            f"writer in {WRITER_GLOB}")
    # Every class S row of an engine scope is covered by an exact rest-state
    # test (FSIM_RS_IDLE beside it), exactly once.
    scratch = {}
    for path in sorted(root.glob(WRITER_GLOB)) + [root / f for f in IDLE_FILES]:
        text = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))
        for match in SCRATCH_MACRO.finditer(text):
            key = (match.group(2), match.group(3))
            if key in scratch:
                problems.append(f"{path.name}: scratch member {key[0]} {key[1]} "
                                f"is covered twice")
            scratch[key] = match.group(1)
    for (scope, name), klass in sorted(rows.items()):
        if scope.startswith(AUDITED_PREFIXES) and klass == "S" \
                and (scope, name) not in scratch:
            problems.append(f"scratch member {scope} {name} has no exact "
                            f"rest-state test (FSIM_RS_IDLE) in {WRITER_GLOB} or "
                            f"{', '.join(IDLE_FILES)}; classify it W or D if it "
                            f"holds nothing at rest")
    for (scope, name) in sorted(scratch):
        if rows.get((scope, name)) != "S":
            problems.append(f"scratch marker for {scope} {name}, which the "
                            f"table does not classify S")
    if problems:
        print("\n".join(problems[:200]), file=sys.stderr)
        print(f"{len(problems)} run-state classification problem(s); "
              f"classify members in {TSV}", file=sys.stderr)
        return 1
    print(f"run-state classification: {len(rows)} members in "
          f"{len(scopes)} classes classified")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
