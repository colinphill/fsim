#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Generate deterministic reduced or full Reed-Solomon codec fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ALGORITHM_VERSION = "xorshift32-shift13-17-5-v1"
PURPOSES = (
    "payload",
    "corruption_positions",
    "corruption_magnitudes",
    "backpressure",
)
SCENARIOS = (
    {"tag": 0, "name": "clean", "errors": 0, "erasures": 0, "throttle": 0},
    {"tag": 1, "name": "maximum_errors", "errors": 2, "erasures": 0, "throttle": 0},
    {"tag": 2, "name": "maximum_errors_backpressure", "errors": 2, "erasures": 0, "throttle": 1},
    {"tag": 3, "name": "maximum_erasures", "errors": 0, "erasures": 2, "throttle": 0},
    {"tag": 4, "name": "mixed_errors_erasures", "errors": 1, "erasures": 1, "throttle": 0},
    {"tag": 7, "name": "mixed_errors_erasures_backpressure", "errors": 1, "erasures": 1, "throttle": 1},
    {"tag": 5, "name": "shortened_errors", "errors": 2, "erasures": 0, "throttle": 0},
)
KNOWN_ANSWER = (
    "40aec71f",
    "91e00c19",
    "9c0fe128",
    "6570f69d",
    "0fce02cc",
)

INSTANCE_PURPOSES = PURPOSES
FULL_WORKLOAD_PROFILE = "full-11-instance"
REDUCED_WORKLOAD_PROFILE = "reduced-rs15-11-mode1"
DPP_BY_M = {
    2: 0x7,
    3: 0xB,
    4: 0x13,
    5: 0x25,
    6: 0x43,
    7: 0x89,
    8: 0x11D,
    9: 0x211,
    10: 0x409,
    11: 0x805,
    12: 0x1053,
}


class FixtureError(Exception):
    """Raised when an input testbench does not match the expected fixture."""


def xorshift32(state: int) -> int:
    """Return one xorshift32 word with explicit 32-bit truncation."""

    state &= 0xFFFFFFFF
    state = (state ^ ((state << 13) & 0xFFFFFFFF)) & 0xFFFFFFFF
    state = (state ^ (state >> 17)) & 0xFFFFFFFF
    state = (state ^ ((state << 5) & 0xFFFFFFFF)) & 0xFFFFFFFF
    return state


def check_known_answer() -> None:
    state = 0x6D2B79F5
    actual: list[str] = []
    for _ in KNOWN_ANSWER:
        state = xorshift32(state)
        actual.append(f"{state:08x}")
    if tuple(actual) != KNOWN_ANSWER:
        raise FixtureError(f"xorshift32 KAT failed: {actual!r}")


def stream_seeds(base_seed: int) -> dict[int, dict[str, int]]:
    """Derive nonzero domain-separated streams, independent of draw order."""

    result: dict[int, dict[str, int]] = {}
    used: set[int] = set()
    for scenario in SCENARIOS:
        tag = int(scenario["tag"])
        result[tag] = {}
        for purpose in PURPOSES:
            material = (
                f"{ALGORITHM_VERSION}|base={base_seed:08x}|instance=d0|"
                f"scenario={tag}|purpose={purpose}"
            ).encode("ascii")
            word = int.from_bytes(hashlib.sha256(material).digest()[:4], "big")
            while word == 0 or word in used:
                word = (word + 1) & 0xFFFFFFFF
            used.add(word)
            result[tag][purpose] = word
    return result


def _parse_hdl_integer(text: str) -> int:
    value = text.strip().replace("'h", "0x").replace("'H", "0x")
    return int(value, 0)


def _full_instance_configs(source_text: str) -> list[dict[str, int | str | bool]]:
    """Read and validate the original eleven named codec checker instances."""

    root_start = source_text.find("module rs_codec_tb;")
    if root_start < 0:
        raise FixtureError("full workload requires the original rs_codec_tb root")
    root = source_text[root_start:]
    pattern = re.compile(
        r"codec_check\s*#\((.*?)\)\s+d(10|[0-9])\s*\(", re.DOTALL
    )
    required = (
        "N", "K", "FCR", "PRIM_POLY", "PRIM_POW", "IMPL_MODE",
        "ERASURE", "CWS", "PS",
    )
    expected = {
        0: (255, 223, 0, 0, 1, 1, 1, 1, 0),
        1: (255, 223, 0, 0, 1, 0, 1, 1, 0),
        2: (255, 223, 112, 0x187, 11, 1, 0, 1, 0),
        3: (15, 11, 0, 0, 1, 1, 1, 1, 0),
        4: (63, 47, 1, 0, 1, 0, 1, 1, 0),
        5: (255, 223, 0, 0, 1, 1, 1, 1, 1),
        6: (31, 21, 0, 0, 1, 1, 1, 1, 0),
        7: (255, 224, 0, 0, 1, 1, 1, 1, 0),
        8: (255, 224, 0, 0, 1, 0, 1, 1, 0),
        9: (15, 8, 0, 0, 1, 1, 1, 1, 0),
        10: (63, 50, 3, 0, 1, 2, 1, 1, 0),
    }
    configs: list[dict[str, int | str | bool]] = []
    for match in pattern.finditer(root):
        parameters = {
            name: _parse_hdl_integer(value)
            for name, value in re.findall(r"\.(\w+)\(([^()]+)\)", match.group(1))
        }
        if any(name not in parameters for name in required):
            raise FixtureError(f"instance d{match.group(2)} is missing codec parameters")
        instance_id = int(match.group(2))
        actual_parameters = tuple(parameters[name] for name in required)
        if actual_parameters != expected[instance_id]:
            raise FixtureError(
                f"instance d{instance_id} no longer matches the original codec workload: "
                f"{actual_parameters!r}"
            )
        n = parameters["N"]
        k = parameters["K"]
        m = n.bit_length()
        primitive_poly = parameters["PRIM_POLY"] or DPP_BY_M.get(m)
        if primitive_poly is None:
            raise FixtureError(f"no default primitive polynomial is known for M={m}")
        configs.append(
            {
                "instance": f"d{match.group(2)}",
                "instance_id": instance_id,
                "n": n,
                "k": k,
                "fcr": parameters["FCR"],
                "primitive_poly": primitive_poly,
                "primitive_poly_hex": f"0x{primitive_poly:x}",
                "primitive_poly_parameter": parameters["PRIM_POLY"],
                "primitive_power": parameters["PRIM_POW"],
                "implementation_mode": parameters["IMPL_MODE"],
                "erasure_decoding": bool(parameters["ERASURE"]),
                "codeword_shortening": bool(parameters["CWS"]),
                "parity_shortening": bool(parameters["PS"]),
                "m": m,
                "par": n - k,
                "t": (n - k) // 2,
            }
        )
    expected_ids = list(range(11))
    actual_ids = [int(item["instance_id"]) for item in configs]
    if actual_ids != expected_ids:
        raise FixtureError(
            "full workload requires exactly the original d0 through d10 instances; "
            f"found {actual_ids!r}"
        )
    return configs


def _scenario_specs(config: dict[str, int | str | bool]) -> list[dict[str, int | str]]:
    """Return the source-ordered scenarios and their intended transaction sizes."""

    n = int(config["n"])
    k = int(config["k"])
    par = int(config["par"])
    t = int(config["t"])
    specs = [
        {"tag": 0, "name": "clean", "kk": k, "par": par,
         "errors": 0, "erasures": 0, "throttle": 0},
        {"tag": 1, "name": "maximum_errors", "kk": k, "par": par,
         "errors": t, "erasures": 0, "throttle": 0},
        {"tag": 2, "name": "maximum_errors_backpressure", "kk": k, "par": par,
         "errors": t, "erasures": 0, "throttle": 1},
    ]
    if bool(config["erasure_decoding"]):
        specs.extend(
            [
                {"tag": 3, "name": "maximum_erasures", "kk": k, "par": par,
                 "errors": 0, "erasures": t, "throttle": 0},
                {"tag": 4, "name": "mixed_errors_erasures", "kk": k, "par": par,
                 "errors": t // 2, "erasures": t // 2, "throttle": 0},
                {"tag": 7, "name": "mixed_errors_erasures_backpressure", "kk": k,
                 "par": par, "errors": t // 2, "erasures": t // 2, "throttle": 1},
            ]
        )
    if bool(config["codeword_shortening"]):
        specs.append(
            {"tag": 5, "name": "shortened_errors", "kk": k // 2, "par": par,
             "errors": min(t, 8), "erasures": 0, "throttle": 0}
        )
    if bool(config["parity_shortening"]):
        specs.append(
            {"tag": 6, "name": "parity_shortening", "kk": k, "par": t,
             "errors": t // 2, "erasures": 0, "throttle": 0}
        )
    for spec in specs:
        spec["codeword_length"] = int(spec["kk"]) + int(spec["par"])
        spec["payload_count"] = int(spec["kk"])
        spec["corruption_count"] = int(spec["errors"]) + int(spec["erasures"])
        spec["zero_erasure_count"] = int(spec["erasures"]) // 2
        spec["encoder_accept_count"] = int(spec["kk"])
        spec["decoder_accept_count"] = int(spec["codeword_length"])
    if n <= 0:
        raise FixtureError("full workload has an invalid code length")
    return specs


def _full_stream_seeds(
    base_seed: int, configs: list[dict[str, int | str | bool]]
) -> dict[int, dict[int, dict[str, int]]]:
    """Derive unique nonzero seeds for each active instance/tag/purpose tuple."""

    result: dict[int, dict[int, dict[str, int]]] = {}
    used: set[int] = set()
    for config in configs:
        instance_id = int(config["instance_id"])
        result[instance_id] = {}
        for scenario in _scenario_specs(config):
            tag = int(scenario["tag"])
            result[instance_id][tag] = {}
            for purpose in INSTANCE_PURPOSES:
                material = (
                    f"{ALGORITHM_VERSION}|base={base_seed:08x}|"
                    f"instance=d{instance_id}|scenario={tag}|purpose={purpose}"
                ).encode("ascii")
                word = int.from_bytes(hashlib.sha256(material).digest()[:4], "big")
                while word == 0 or word in used:
                    word = (word + 1) & 0xFFFFFFFF
                used.add(word)
                result[instance_id][tag][purpose] = word
    return result


def _full_seed_function(
    seed_table: dict[int, dict[int, dict[str, int]]],
) -> str:
    purpose_case = {purpose: index for index, purpose in enumerate(INSTANCE_PURPOSES)}
    lines = [
        "    function automatic [31:0] perf_seed_for;",
        "        input integer instance_id;",
        "        input integer scenario;",
        "        input integer purpose;",
        "        begin",
        "            perf_seed_for = 32'h00000001;",
        "            case (instance_id)",
    ]
    for instance_id, scenarios in sorted(seed_table.items()):
        lines.extend(
            [f"                {instance_id}: begin", "                    case (scenario)"]
        )
        for tag, purposes in sorted(scenarios.items()):
            lines.extend(
                [
                    f"                        {tag}: begin",
                    "                            case (purpose)",
                ]
            )
            for purpose, purpose_index in purpose_case.items():
                seed = purposes[purpose]
                lines.append(
                    f"                                {purpose_index}: perf_seed_for = "
                    f"32'h{seed:08x};"
                )
            lines.extend(
                [
                    "                                default: perf_seed_for = 32'h00000001;",
                    "                            endcase",
                    "                        end",
                ]
            )
        lines.extend(
            [
                "                        default: perf_seed_for = 32'h00000001;",
                "                    endcase",
                "                end",
            ]
        )
    lines.extend(
        [
            "                default: perf_seed_for = 32'h00000001;",
            "            endcase",
            "        end",
            "    endfunction",
        ]
    )
    return "\n".join(lines)


def _replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise FixtureError(f"expected one {label} anchor, found {count}")
    return source.replace(old, new, 1)


def _replace_region(
    source: str, start_marker: str, end_marker: str, replacement: str, label: str
) -> str:
    start_count = source.count(start_marker)
    if start_count != 1:
        raise FixtureError(f"expected one start marker for {label}, found {start_count}")
    start = source.index(start_marker)
    end = source.find(end_marker, start + len(start_marker))
    if end < 0:
        raise FixtureError(f"missing end marker for {label}")
    return source[:start] + replacement + source[end:]


def _replace_to_eof(source: str, start_marker: str, replacement: str, label: str) -> str:
    count = source.count(start_marker)
    if count != 1:
        raise FixtureError(f"expected one start marker for {label}, found {count}")
    return source[: source.index(start_marker)] + replacement


def _seed_function(seed_table: dict[int, dict[str, int]]) -> str:
    purpose_case = {
        purpose: index for index, purpose in enumerate(PURPOSES)
    }
    lines = [
        "    function automatic [31:0] perf_seed_for;",
        "        input integer scenario;",
        "        input integer purpose;",
        "        begin",
        "            perf_seed_for = 32'h00000001;",
        "            case (scenario)",
    ]
    for tag, purpose_values in seed_table.items():
        lines.append(f"                {tag}: begin")
        lines.append("                    case (purpose)")
        for purpose, purpose_index in purpose_case.items():
            seed = purpose_values[purpose]
            lines.append(
                f"                        {purpose_index}: perf_seed_for = 32'h{seed:08x};"
            )
        lines.append("                        default: perf_seed_for = 32'h00000001;")
        lines.append("                    endcase")
        lines.append("                end")
    lines.extend(
        [
            "                default: perf_seed_for = 32'h00000001;",
            "            endcase",
            "        end",
            "    endfunction",
        ]
    )
    return "\n".join(lines)


def _helpers(seed_table: dict[int, dict[str, int]], base_seed: int) -> str:
    return "\n".join(
        [
            "    // Generated deterministic benchmark stimulus, version "
            + ALGORITHM_VERSION
            + ".",
            f"    localparam [31:0] PERF_BASE_SEED = 32'h{base_seed:08x};",
            "    localparam integer WD_LIMIT = N*64 + 200000;",
            "    integer perf_preflight;",
            "    integer perf_scenarios;",
            "    integer perf_payload_count;",
            "    integer perf_corruption_count;",
            "    integer perf_error_count;",
            "    integer perf_erasure_count;",
            "    integer perf_zero_erasure_count;",
            "    integer perf_ready_cycles;",
            "    integer perf_ready_low_cycles;",
            "    integer perf_output_stall_cycles;",
            "    integer perf_encoder_accept_count;",
            "    integer perf_decoder_accept_count;",
            "    integer perf_xz_count;",
            "    integer perf_event_overflow_count;",
            "    integer perf_encode_timeout;",
            "    integer perf_init_index;",
            "    reg [31:0] perf_payload_fingerprint;",
            "    reg [31:0] perf_corruption_fingerprint;",
            "    reg [31:0] perf_ready_fingerprint;",
            "    reg [31:0] perf_accept_fingerprint;",
            "    reg [31:0] perf_kat_state;",
            "    reg [31:0] perf_kat_words [0:4];",
            "    integer perf_scenario_payload_count [0:7];",
            "    integer perf_scenario_corruption_count [0:7];",
            "    integer perf_scenario_error_count [0:7];",
            "    integer perf_scenario_erasure_count [0:7];",
            "    integer perf_scenario_zero_erasure_count [0:7];",
            "    integer perf_scenario_ready_cycles [0:7];",
            "    integer perf_scenario_ready_low_cycles [0:7];",
            "    integer perf_scenario_output_stall_cycles [0:7];",
            "    integer perf_scenario_encoder_accept_count [0:7];",
            "    integer perf_scenario_decoder_accept_count [0:7];",
            "    integer perf_scenario_xz_count [0:7];",
            "    integer perf_scenario_overflow_count [0:7];",
            "    integer perf_scenario_encode_timeout_count [0:7];",
            "    integer perf_scenario_decode_timeout_count [0:7];",
            "    reg [31:0] perf_scenario_payload_fingerprint [0:7];",
            "    reg [31:0] perf_scenario_corruption_fingerprint [0:7];",
            "    reg [31:0] perf_scenario_ready_fingerprint [0:7];",
            "    reg [31:0] perf_scenario_accept_fingerprint [0:7];",
            "    integer perf_encoder_event_count;",
            "    integer perf_encoder_event_cycle [0:2047];",
            "    integer perf_encoder_event_index [0:2047];",
            "    reg [M-1:0] perf_encoder_event_data [0:2047];",
            "    reg perf_encoder_event_last [0:2047];",
            "    integer perf_decoder_event_count;",
            "    integer perf_decoder_event_cycle [0:2047];",
            "    integer perf_decoder_event_index [0:2047];",
            "    reg [M-1:0] perf_decoder_event_data [0:2047];",
            "    reg perf_decoder_event_last [0:2047];",
            "    reg perf_decoder_event_user [0:2047];",
            "    integer perf_ready_event_count;",
            "    reg perf_ready_event_value [0:4095];",
            "",
            "    function automatic [31:0] perf_xorshift32;",
            "        input [31:0] state_in;",
            "        reg [31:0] state;",
            "        begin",
            "            state = state_in ^ (state_in << 13);",
            "            state = state ^ (state >> 17);",
            "            state = state ^ (state << 5);",
            "            perf_xorshift32 = state;",
            "        end",
            "    endfunction",
            "",
            "    function automatic [31:0] perf_mix;",
            "        input [31:0] hash_in;",
            "        input [31:0] word_in;",
            "        begin",
            "            perf_mix = (hash_in ^ word_in) * 32'h01000193;",
            "        end",
            "    endfunction",
            "",
            _seed_function(seed_table),
            "",
            "    function automatic perf_ready_for_cycle;",
            "        input integer scenario;",
            "        input integer throttle;",
            "        input integer cycle_index;",
            "        reg [31:0] cycle_word;",
            "        begin",
            "            if (throttle == 0)",
            "                perf_ready_for_cycle = 1'b1;",
            "            else begin",
            "                cycle_word = perf_xorshift32(perf_seed_for(scenario, 3) ^ cycle_index);",
            "                perf_ready_for_cycle = ((cycle_index % 7) != 6) &&",
            "                                       ((cycle_word & 32'h00000003) != 0);",
            "            end",
            "        end",
            "    endfunction",
            "",
            "    task automatic perf_note_xz;",
            "        input integer scenario;",
            "        input integer phase;",
            "        input integer cycle_index;",
            "        input [31:0] fields;",
            "        begin",
            "            if ((^fields) === 1'bx) begin",
            "                perf_xz_count = perf_xz_count + 1;",
            "                perf_scenario_xz_count[scenario] = perf_scenario_xz_count[scenario] + 1;",
            "                if (perf_preflight)",
            "                    $display(\"STIM_EVENT instance=0 scenario=%0d kind=invalid seq=%0d phase=%0d fields=%08x\",",
            "                             scenario, cycle_index, phase, fields);",
            "            end",
            "        end",
            "    endtask",
            "",
            "    task automatic perf_print_summary;",
            "        input integer scenario;",
            "        begin",
            "            $display(\"STIM_SUMMARY instance=0 scenario=%0d payload_count=%0d corruption_count=%0d error_count=%0d erasure_count=%0d zero_erasure_count=%0d ready_cycles=%0d ready_low_cycles=%0d output_stall_cycles=%0d encoder_accept_count=%0d decoder_accept_count=%0d encode_timeout_count=%0d decode_timeout_count=%0d xz_count=%0d event_overflow_count=%0d payload_fp=%08x corruption_fp=%08x ready_fp=%08x accepted_fp=%08x\",",
            "                     scenario, perf_scenario_payload_count[scenario],",
            "                     perf_scenario_corruption_count[scenario], perf_scenario_error_count[scenario],",
            "                     perf_scenario_erasure_count[scenario], perf_scenario_zero_erasure_count[scenario],",
            "                     perf_scenario_ready_cycles[scenario], perf_scenario_ready_low_cycles[scenario],",
            "                     perf_scenario_output_stall_cycles[scenario],",
            "                     perf_scenario_encoder_accept_count[scenario],",
            "                     perf_scenario_decoder_accept_count[scenario],",
            "                     perf_scenario_encode_timeout_count[scenario],",
            "                     perf_scenario_decode_timeout_count[scenario],",
            "                     perf_scenario_xz_count[scenario], perf_scenario_overflow_count[scenario],",
            "                     perf_scenario_payload_fingerprint[scenario],",
            "                     perf_scenario_corruption_fingerprint[scenario],",
            "                     perf_scenario_ready_fingerprint[scenario],",
            "                     perf_scenario_accept_fingerprint[scenario]);",
            "        end",
            "    endtask",
            "",
        ]
    )


def _do_encode() -> str:
    return r'''    task automatic do_encode;
        input integer kk;
        input integer par;
        input integer tag;
        integer i, di, len, cycle, accepted_now, watchdog;
        reg [31:0] payload_state;
        begin
            len = kk + par;
            payload_state = perf_seed_for(tag, 0);
            for (i=0;i<kk;i=i+1) begin
                payload_state = perf_xorshift32(payload_state);
                msg[i] = payload_state[M-1:0];
                perf_payload_count = perf_payload_count + 1;
                perf_scenario_payload_count[tag] = perf_scenario_payload_count[tag] + 1;
                perf_payload_fingerprint = perf_mix(perf_payload_fingerprint, tag);
                perf_payload_fingerprint = perf_mix(perf_payload_fingerprint, i);
                perf_payload_fingerprint = perf_mix(perf_payload_fingerprint, msg[i]);
                perf_scenario_payload_fingerprint[tag] = perf_mix(perf_scenario_payload_fingerprint[tag], tag);
                perf_scenario_payload_fingerprint[tag] = perf_mix(perf_scenario_payload_fingerprint[tag], i);
                perf_scenario_payload_fingerprint[tag] = perf_mix(perf_scenario_payload_fingerprint[tag], msg[i]);
                if (perf_preflight)
                    $display("STIM_EVENT instance=0 scenario=%0d kind=payload seq=%0d value=%08x",
                             tag, i, msg[i]);
            end
            perf_encoder_event_count = 0;
            perf_encode_timeout = 0;
            fork
                begin : ENC_DRV
                    di = 0;
                    cycle = 0;
                    accepted_now = 0;
                    @(negedge clk);
                    e_tdata = msg[0];
                    e_tlast = (di == kk-1);
                    e_par = par;
                    e_tvalid = 1'b1;
                    while (di < kk && perf_encode_timeout == 0) begin
                        accepted_now = 0;
                        @(posedge clk);
                        if ((e_tready !== 1'b0) && (e_tready !== 1'b1))
                            perf_note_xz(tag, 0, cycle, {31'b0,e_tready});
                        if ((e_tvalid !== 1'b0) && (e_tvalid !== 1'b1))
                            perf_note_xz(tag, 0, cycle, {31'b0,e_tvalid});
                        if (e_tvalid === 1'b1) begin
                            if ((^{e_tdata,e_tlast}) === 1'bx)
                                perf_note_xz(tag, 0, cycle, {e_tdata,e_tlast});
                            if (e_tready === 1'b1) begin
                                if (perf_encoder_event_count < 2048) begin
                                    perf_encoder_event_cycle[perf_encoder_event_count] = cycle;
                                    perf_encoder_event_index[perf_encoder_event_count] = di;
                                    perf_encoder_event_data[perf_encoder_event_count] = e_tdata;
                                    perf_encoder_event_last[perf_encoder_event_count] = e_tlast;
                                    perf_encoder_event_count = perf_encoder_event_count + 1;
                                end else begin
                                    perf_scenario_overflow_count[tag] =
                                        perf_scenario_overflow_count[tag] + 1;
                                    perf_event_overflow_count = perf_event_overflow_count + 1;
                                end
                                perf_encoder_accept_count = perf_encoder_accept_count + 1;
                                perf_scenario_encoder_accept_count[tag] =
                                    perf_scenario_encoder_accept_count[tag] + 1;
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, 32'd0);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, tag);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, cycle);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, di);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, e_tdata);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, e_tlast);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], 32'd0);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], tag);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], cycle);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], di);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], e_tdata);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], e_tlast);
                                accepted_now = 1;
                                di = di + 1;
                            end
                        end
                        cycle = cycle + 1;
                        if (di < kk) begin
                            @(negedge clk);
                            if (accepted_now) begin
                                e_tdata = msg[di];
                                e_tlast = (di == kk-1);
                            end
                        end
                    end
                    @(negedge clk);
                    e_tvalid = 1'b0;
                    e_tlast = 1'b0;
                end
                begin : ENC_CAP
                    i = 0;
                    watchdog = 0;
                    while (i < len && watchdog < WD_LIMIT) begin
                        @(posedge clk);
                        if ((ec_tvalid !== 1'b0) && (ec_tvalid !== 1'b1))
                            perf_note_xz(tag, 1, watchdog, {31'b0,ec_tvalid});
                        if ((ec_tready !== 1'b0) && (ec_tready !== 1'b1))
                            perf_note_xz(tag, 1, watchdog, {31'b0,ec_tready});
                        if (ec_tvalid === 1'b1) begin
                            if ((^{ec_tdata,ec_tlast}) === 1'bx)
                                perf_note_xz(tag, 1, watchdog, {ec_tdata,ec_tlast});
                            if (ec_tready === 1'b1) begin
                                cw[i] = ec_tdata;
                                i = i + 1;
                            end
                        end
                        watchdog = watchdog + 1;
                    end
                    if (i < len)
                        perf_encode_timeout = 1;
                end
            join
            if (perf_preflight) begin
                for (i=0;i<perf_encoder_event_count;i=i+1)
                    $display("STIM_EVENT instance=0 scenario=%0d kind=accepted_encoder seq=%0d cycle=%0d index=%0d data=%08x last=%0d",
                             tag, perf_encoder_event_index[i], perf_encoder_event_cycle[i], perf_encoder_event_index[i],
                             perf_encoder_event_data[i], perf_encoder_event_last[i]);
            end
        end
    endtask

'''


def _do_decode() -> str:
    return r'''    task automatic do_decode;
        input integer len;
        input integer par;
        input integer throttle;
        input integer tag;
        integer i, di, cycle, input_cycle, watchdog, accepted_now, record_index;
        reg [M-1:0] sampled_data;
        reg sampled_valid, sampled_ready, sampled_last, sampled_user, sampled_unc;
        begin
            dec_to = 1'b0;
            dec_unc = 1'b0;
            perf_decoder_event_count = 0;
            perf_ready_event_count = 0;
            fork
                begin : DEC_DRV
                    di = 0;
                    input_cycle = 0;
                    accepted_now = 0;
                    @(negedge clk);
                    d_tdata = rx[0];
                    d_tuser = er[0];
                    d_tlast = (di == len-1);
                    d_par = par;
                    d_tvalid = 1'b1;
                    while (di < len && !dec_to) begin
                        accepted_now = 0;
                        @(posedge clk);
                        if ((d_tready !== 1'b0) && (d_tready !== 1'b1))
                            perf_note_xz(tag, 2, input_cycle, {31'b0,d_tready});
                        if ((d_tvalid !== 1'b0) && (d_tvalid !== 1'b1))
                            perf_note_xz(tag, 2, input_cycle, {31'b0,d_tvalid});
                        if (d_tvalid === 1'b1) begin
                            if ((^{d_tdata,d_tlast,d_tuser}) === 1'bx)
                                perf_note_xz(tag, 2, input_cycle, {d_tdata,d_tlast,d_tuser});
                            if (d_tready === 1'b1) begin
                                if (perf_decoder_event_count < 2048) begin
                                    perf_decoder_event_cycle[perf_decoder_event_count] = input_cycle;
                                    perf_decoder_event_index[perf_decoder_event_count] = di;
                                    perf_decoder_event_data[perf_decoder_event_count] = d_tdata;
                                    perf_decoder_event_last[perf_decoder_event_count] = d_tlast;
                                perf_decoder_event_user[perf_decoder_event_count] = d_tuser;
                                perf_decoder_event_count = perf_decoder_event_count + 1;
                            end else begin
                                perf_event_overflow_count = perf_event_overflow_count + 1;
                                perf_scenario_overflow_count[tag] =
                                    perf_scenario_overflow_count[tag] + 1;
                            end
                                perf_decoder_accept_count = perf_decoder_accept_count + 1;
                                perf_scenario_decoder_accept_count[tag] =
                                    perf_scenario_decoder_accept_count[tag] + 1;
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, 32'd1);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, tag);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, input_cycle);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, di);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, d_tdata);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, d_tlast);
                                perf_accept_fingerprint = perf_mix(perf_accept_fingerprint, d_tuser);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], 32'd1);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], tag);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], input_cycle);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], di);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], d_tdata);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], d_tlast);
                                perf_scenario_accept_fingerprint[tag] =
                                    perf_mix(perf_scenario_accept_fingerprint[tag], d_tuser);
                                accepted_now = 1;
                                di = di + 1;
                            end
                        end
                        input_cycle = input_cycle + 1;
                        if (di < len && !dec_to) begin
                            @(negedge clk);
                            if (accepted_now) begin
                                d_tdata = rx[di];
                                d_tuser = er[di];
                                d_tlast = (di == len-1);
                            end
                        end
                    end
                    @(negedge clk);
                    d_tvalid = 1'b0;
                    d_tlast = 1'b0;
                    d_tuser = 1'b0;
                end
                begin : DEC_CAP
                    i = 0;
                    cycle = 0;
                    watchdog = 0;
                    @(negedge clk);
                    dc_tready = perf_ready_for_cycle(tag, throttle, cycle);
                    while (i < len && watchdog < WD_LIMIT) begin
                        @(posedge clk);
                        sampled_valid = dc_tvalid;
                        sampled_ready = dc_tready;
                        sampled_data = dc_tdata;
                        sampled_last = dc_tlast;
                        sampled_user = dc_tuser;
                        sampled_unc = unc;
                        if ((dc_tvalid !== 1'b0) && (dc_tvalid !== 1'b1))
                            perf_note_xz(tag, 3, cycle, {31'b0,dc_tvalid});
                        if ((dc_tready !== 1'b0) && (dc_tready !== 1'b1))
                            perf_note_xz(tag, 3, cycle, {31'b0,dc_tready});
                        if (sampled_valid === 1'b1) begin
                            if ((^{sampled_data,sampled_last,sampled_user}) === 1'bx)
                                perf_note_xz(tag, 3, cycle, {sampled_data,sampled_last,sampled_user});
                            if ((sampled_last === 1'b1) &&
                                (sampled_unc !== 1'b0) && (sampled_unc !== 1'b1))
                                perf_note_xz(tag, 3, cycle, {31'b0,unc});
                        end
                        if (perf_ready_event_count < 4096) begin
                            perf_ready_event_value[perf_ready_event_count] = sampled_ready;
                            perf_ready_event_count = perf_ready_event_count + 1;
                        end else begin
                            perf_event_overflow_count = perf_event_overflow_count + 1;
                            perf_scenario_overflow_count[tag] =
                                perf_scenario_overflow_count[tag] + 1;
                        end
                        perf_ready_cycles = perf_ready_cycles + 1;
                        perf_scenario_ready_cycles[tag] = perf_scenario_ready_cycles[tag] + 1;
                        if (sampled_ready === 1'b0)
                            perf_ready_low_cycles = perf_ready_low_cycles + 1;
                        if (sampled_ready === 1'b0)
                            perf_scenario_ready_low_cycles[tag] =
                                perf_scenario_ready_low_cycles[tag] + 1;
                        perf_ready_fingerprint = perf_mix(perf_ready_fingerprint, tag);
                        perf_ready_fingerprint = perf_mix(perf_ready_fingerprint, cycle);
                        perf_ready_fingerprint = perf_mix(perf_ready_fingerprint, sampled_ready);
                        perf_scenario_ready_fingerprint[tag] =
                            perf_mix(perf_scenario_ready_fingerprint[tag], tag);
                        perf_scenario_ready_fingerprint[tag] =
                            perf_mix(perf_scenario_ready_fingerprint[tag], cycle);
                        perf_scenario_ready_fingerprint[tag] =
                            perf_mix(perf_scenario_ready_fingerprint[tag], sampled_ready);
                        if ((sampled_valid === 1'b1) && (sampled_ready === 1'b0))
                            perf_output_stall_cycles = perf_output_stall_cycles + 1;
                        if ((sampled_valid === 1'b1) && (sampled_ready === 1'b0))
                            perf_scenario_output_stall_cycles[tag] =
                                perf_scenario_output_stall_cycles[tag] + 1;
                        if ((sampled_valid === 1'b1) && (sampled_ready === 1'b1)) begin
                            dec_out[i] = sampled_data;
                            if ((sampled_last === 1'b1) && (sampled_unc === 1'b1))
                                dec_unc = 1'b1;
                            i = i + 1;
                        end
                        cycle = cycle + 1;
                        watchdog = watchdog + 1;
                        if (i < len && watchdog < WD_LIMIT) begin
                            @(negedge clk);
                            dc_tready = perf_ready_for_cycle(tag, throttle, cycle);
                        end
                    end
                    if (i < len)
                        dec_to = 1'b1;
                    @(negedge clk);
                    dc_tready = 1'b1;
                end
            join
            if (i < len)
                dec_to = 1'b1;
            if (perf_preflight) begin
                $display("STIM_EVENT instance=0 scenario=%0d kind=ready_window seq=0 start_cycle=0 end_cycle=%0d count=%0d",
                         tag, perf_ready_event_count-1, perf_ready_event_count);
                for (record_index=0;record_index<perf_ready_event_count;record_index=record_index+1)
                    $display("STIM_EVENT instance=0 scenario=%0d kind=backpressure seq=%0d cycle=%0d value=%0d",
                             tag, record_index, record_index, perf_ready_event_value[record_index]);
                for (record_index=0;record_index<perf_decoder_event_count;record_index=record_index+1)
                    $display("STIM_EVENT instance=0 scenario=%0d kind=accepted_decoder seq=%0d cycle=%0d index=%0d data=%08x last=%0d user=%0d",
                             tag, perf_decoder_event_index[record_index], perf_decoder_event_cycle[record_index],
                             perf_decoder_event_index[record_index],
                             perf_decoder_event_data[record_index],
                             perf_decoder_event_last[record_index],
                             perf_decoder_event_user[record_index]);
            end
        end
    endtask

'''


def _corrupt2() -> str:
    return r'''    task automatic corrupt2;
        input integer len;
        input integer ne;
        input integer ns;
        input integer tag;
        integer i, placed, pos, magnitude, ordinal;
        reg [31:0] position_state, magnitude_state;
        reg [M-1:0] delta;
        reg [0:0] used [0:2047];
        begin
            for (i=0;i<len;i=i+1) begin
                rx[i]=cw[i];
                er[i]=1'b0;
                used[i]=1'b0;
            end
            position_state = perf_seed_for(tag, 1);
            magnitude_state = perf_seed_for(tag, 2);
            placed = 0;
            ordinal = 0;
            while (placed < ns) begin
                position_state = perf_xorshift32(position_state);
                pos = position_state % len;
                if (!used[pos]) begin
                    used[pos]=1'b1;
                    er[pos]=1'b1;
                    if (placed[0]) begin
                        magnitude = 0;
                        delta = {M{1'b0}};
                    end else begin
                        magnitude_state = perf_xorshift32(magnitude_state);
                        magnitude = magnitude_state & ((1<<M)-1);
                        if (magnitude == 0)
                            magnitude = 1;
                        delta = magnitude[M-1:0];
                    end
                    rx[pos] = cw[pos] ^ delta;
                    perf_corruption_count = perf_corruption_count + 1;
                    perf_erasure_count = perf_erasure_count + 1;
                    perf_scenario_corruption_count[tag] = perf_scenario_corruption_count[tag] + 1;
                    perf_scenario_erasure_count[tag] = perf_scenario_erasure_count[tag] + 1;
                    if (magnitude == 0)
                        perf_zero_erasure_count = perf_zero_erasure_count + 1;
                    if (magnitude == 0)
                        perf_scenario_zero_erasure_count[tag] =
                            perf_scenario_zero_erasure_count[tag] + 1;
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, tag);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, ordinal);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, pos);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, 32'd1);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, magnitude);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, rx[pos]);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], tag);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], ordinal);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], pos);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], 32'd1);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], magnitude);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], rx[pos]);
                    if (perf_preflight)
                    $display("STIM_EVENT instance=0 scenario=%0d kind=corruption seq=%0d position=%0d erasure=1 magnitude=%08x received=%08x",
                             tag, ordinal, pos, magnitude, rx[pos]);
                    placed = placed + 1;
                    ordinal = ordinal + 1;
                end
            end
            placed = 0;
            while (placed < ne) begin
                position_state = perf_xorshift32(position_state);
                pos = position_state % len;
                if (!used[pos]) begin
                    used[pos]=1'b1;
                    magnitude_state = perf_xorshift32(magnitude_state);
                    magnitude = (magnitude_state & ((1<<M)-2)) + 1;
                    delta = magnitude[M-1:0];
                    rx[pos] = cw[pos] ^ delta;
                    perf_corruption_count = perf_corruption_count + 1;
                    perf_error_count = perf_error_count + 1;
                    perf_scenario_corruption_count[tag] = perf_scenario_corruption_count[tag] + 1;
                    perf_scenario_error_count[tag] = perf_scenario_error_count[tag] + 1;
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, tag);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, ordinal);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, pos);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, 32'd0);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, magnitude);
                    perf_corruption_fingerprint = perf_mix(perf_corruption_fingerprint, rx[pos]);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], tag);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], ordinal);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], pos);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], 32'd0);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], magnitude);
                    perf_scenario_corruption_fingerprint[tag] =
                        perf_mix(perf_scenario_corruption_fingerprint[tag], rx[pos]);
                    if (perf_preflight)
                        $display("STIM_EVENT instance=0 scenario=%0d kind=corruption seq=%0d position=%0d erasure=0 magnitude=%08x received=%08x",
                                 tag, ordinal, pos, magnitude, rx[pos]);
                    placed = placed + 1;
                    ordinal = ordinal + 1;
                end
            end
        end
    endtask

'''


def _root_module() -> str:
    return r'''module rs_codec_tb;
    reg clk = 0;
    always #5 clk = ~clk;
    reg start = 0;
    wire [0:0] dn;

    codec_check #(.N(15),.K(11),.FCR(0),.PRIM_POLY(0),.PRIM_POW(1),
                  .IMPL_MODE(1),.ERASURE(1),.CWS(1),.PS(0)) d0 (clk,start,dn[0]);

    initial begin
        repeat(4) @(negedge clk);
        start = 1'b1;
        wait (dn[0]);
        repeat(5) @(posedge clk);
        $display("ALL_CODEC_DONE");
        $finish;
    end

    initial begin
        #500000000;
        $display("TIMEOUT");
        $finish;
    end
endmodule
'''


def transform(source_text: str, base_seed: int) -> tuple[str, dict[int, dict[str, int]]]:
    seed_table = stream_seeds(base_seed)
    result = source_text
    result = _replace_once(
        result,
        "    localparam integer WD_LIMIT = N*64 + 200000;\n",
        "",
        "original watchdog limit",
    )
    result = _replace_once(
        result,
        "    localparam integer PAR = N - K;\n",
        "    localparam integer PAR = N - K;\n\n" + _helpers(seed_table, base_seed) + "\n",
        "PAR parameter",
    )
    result = _replace_region(
        result,
        "    task automatic do_encode;",
        "    // Independent check:",
        _do_encode(),
        "encoder task",
    )
    result = _replace_once(result, "            do_encode(kk, par);", "            do_encode(kk, par, tag);", "encode call")
    result = _replace_once(
        result,
        "            corrupt2(len, ne, ns);",
        "            corrupt2(len, ne, ns, tag);",
        "corruption call",
    )
    result = _replace_once(
        result,
        "            do_decode(len, par, throttle);",
        "            do_decode(len, par, throttle, tag);",
        "decode call",
    )
    result = _replace_region(
        result,
        "    task automatic do_decode;",
        "    // Run one full encode->corrupt->decode scenario.",
        _do_decode(),
        "decoder task",
    )
    result = _replace_region(
        result,
        "    task automatic corrupt2;",
        "    integer TT;",
        _corrupt2(),
        "corruption task",
    )
    result = _replace_once(
        result,
        "        begin\n            len = kk + par;\n            do_encode(kk, par, tag);\n            ok = (cw_valid(len, par) != 0);",
        "        begin\n            len = kk + par;\n            perf_scenarios = perf_scenarios + 1;\n            do_encode(kk, par, tag);\n            ok = (cw_valid(len, par) != 0);\n            if (perf_encode_timeout) begin\n                ok = 1'b0;\n                perf_scenario_encode_timeout_count[tag] = 1;\n            end",
        "scenario encode checking",
    )
    result = _replace_once(
        result,
        "            perf_scenarios = perf_scenarios + 1;\n            do_encode(kk, par, tag);",
        "            perf_scenarios = perf_scenarios + 1;\n"
        "            if (perf_preflight)\n"
        "                $display(\"STIM_EVENT instance=0 scenario=%0d kind=scenario seq=0 kk=%0d par=%0d errors=%0d erasures=%0d throttle=%0d\",\n"
        "                         tag, kk, par, ne, ns, throttle);\n"
        "            do_encode(kk, par, tag);",
        "scenario event",
    )
    result = _replace_once(
        result,
        "            do_decode(len, par, throttle, tag);\n            nmis = 0; fmis = -1;",
        "            do_decode(len, par, throttle, tag);\n"
        "            if (dec_to) perf_scenario_decode_timeout_count[tag] = 1;\n"
        "            nmis = 0; fmis = -1;",
        "decode timeout accounting",
    )
    result = _replace_once(
        result,
        "        done=1'b0; resetn=1'b0; fails=0; dec_unc=1'b0;",
        "        done=1'b0; resetn=1'b0; fails=0; dec_unc=1'b0;\n"
        "        perf_preflight = $test$plusargs(\"PERF_PREFLIGHT\");\n"
        "        perf_scenarios=0; perf_payload_count=0; perf_corruption_count=0;\n"
        "        perf_error_count=0; perf_erasure_count=0; perf_zero_erasure_count=0;\n"
        "        perf_ready_cycles=0; perf_ready_low_cycles=0; perf_output_stall_cycles=0;\n"
        "        perf_encoder_accept_count=0; perf_decoder_accept_count=0; perf_xz_count=0;\n"
        "        perf_event_overflow_count=0; perf_encode_timeout=0;\n"
        "        perf_payload_fingerprint=32'h811c9dc5;\n"
        "        perf_corruption_fingerprint=32'h811c9dc5;\n"
        "        perf_ready_fingerprint=32'h811c9dc5;\n"
        "        perf_accept_fingerprint=32'h811c9dc5;\n"
        "        perf_encoder_event_count=0; perf_decoder_event_count=0; perf_ready_event_count=0;\n"
        "        for (perf_init_index=0;perf_init_index<8;perf_init_index=perf_init_index+1) begin\n"
        "            perf_scenario_payload_count[perf_init_index]=0;\n"
        "            perf_scenario_corruption_count[perf_init_index]=0;\n"
        "            perf_scenario_error_count[perf_init_index]=0;\n"
        "            perf_scenario_erasure_count[perf_init_index]=0;\n"
        "            perf_scenario_zero_erasure_count[perf_init_index]=0;\n"
        "            perf_scenario_ready_cycles[perf_init_index]=0;\n"
        "            perf_scenario_ready_low_cycles[perf_init_index]=0;\n"
        "            perf_scenario_output_stall_cycles[perf_init_index]=0;\n"
        "            perf_scenario_encoder_accept_count[perf_init_index]=0;\n"
        "            perf_scenario_decoder_accept_count[perf_init_index]=0;\n"
        "            perf_scenario_xz_count[perf_init_index]=0;\n"
        "            perf_scenario_overflow_count[perf_init_index]=0;\n"
        "            perf_scenario_encode_timeout_count[perf_init_index]=0;\n"
        "            perf_scenario_decode_timeout_count[perf_init_index]=0;\n"
        "            perf_scenario_payload_fingerprint[perf_init_index]=32'h811c9dc5;\n"
        "            perf_scenario_corruption_fingerprint[perf_init_index]=32'h811c9dc5;\n"
        "            perf_scenario_ready_fingerprint[perf_init_index]=32'h811c9dc5;\n"
        "            perf_scenario_accept_fingerprint[perf_init_index]=32'h811c9dc5;\n"
        "        end\n"
        "        if (perf_preflight) begin\n"
        "            perf_kat_state=32'h6d2b79f5;\n"
        "            perf_kat_state=perf_xorshift32(perf_kat_state);\n"
        "            perf_kat_words[0]=perf_kat_state;\n"
        "            perf_kat_state=perf_xorshift32(perf_kat_state);\n"
        "            perf_kat_words[1]=perf_kat_state;\n"
        "            perf_kat_state=perf_xorshift32(perf_kat_state);\n"
        "            perf_kat_words[2]=perf_kat_state;\n"
        "            perf_kat_state=perf_xorshift32(perf_kat_state);\n"
        "            perf_kat_words[3]=perf_kat_state;\n"
        "            perf_kat_state=perf_xorshift32(perf_kat_state);\n"
        "            perf_kat_words[4]=perf_kat_state;\n"
        "            if ((perf_kat_words[0] !== 32'h40aec71f) ||\n"
        "                (perf_kat_words[1] !== 32'h91e00c19) ||\n"
        "                (perf_kat_words[2] !== 32'h9c0fe128) ||\n"
        "                (perf_kat_words[3] !== 32'h6570f69d) ||\n"
        "                (perf_kat_words[4] !== 32'h0fce02cc)) begin\n"
        "                perf_xz_count=perf_xz_count+1;\n"
        "                perf_scenario_xz_count[0]=perf_scenario_xz_count[0]+1;\n"
        "                $display(\"STIM_KAT_FAIL\");\n"
        "            end\n"
        "            $display(\"STIM_KAT %08x %08x %08x %08x %08x\",\n"
        "                     perf_kat_words[0], perf_kat_words[1], perf_kat_words[2],\n"
        "                     perf_kat_words[3], perf_kat_words[4]);\n"
        "        end\n",
        "initial setup",
    )
    result = _replace_once(
        result,
        "        resetn=1'b1;",
        "        @(negedge clk);\n        resetn=1'b1;",
        "reset deassertion edge",
    )
    result = _replace_once(
        result,
        "        if (fails==0) $display(\"CODEC_OK [RS%0d_%0d f%0d pw%0d m%0d e%0d cs%0d ps%0d]\",",
        "        if (perf_xz_count != 0 || perf_event_overflow_count != 0)\n"
        "            fails = fails + 1;\n"
        "        perf_print_summary(0);\n"
        "        perf_print_summary(1);\n"
        "        perf_print_summary(2);\n"
        "        perf_print_summary(3);\n"
        "        perf_print_summary(4);\n"
        "        perf_print_summary(7);\n"
        "        perf_print_summary(5);\n"
        "        if (fails==0) $display(\"CODEC_OK [RS%0d_%0d f%0d pw%0d m%0d e%0d cs%0d ps%0d]\",",
        "summary insertion",
    )
    result = _replace_to_eof(result, "module rs_codec_tb;", _root_module(), "benchmark root module")
    if result.count("module rs_codec_tb;") != 1:
        raise FixtureError("generated source must contain exactly one benchmark root")
    if "$urandom" in result or "$random" in result:
        raise FixtureError("generated source still contains simulator RNG calls")
    if result.count("localparam integer WD_LIMIT = N*64 + 200000;") != 1:
        raise FixtureError("generated source must declare WD_LIMIT exactly once before tasks")
    if re.search(r"\)\s+d(?:[1-9]|10)\s*\(", result):
        raise FixtureError("generated root still contains non-reduced codec instances")
    expected_scenarios = (
        "scenario(K, PAR, 0,    0,   0, 0)",
        "scenario(K, PAR, T,    0,   0, 1)",
        "scenario(K, PAR, T,    0,   1, 2)",
        "scenario(K, PAR, 0,    T,   0, 3)",
        "scenario(K, PAR, T/2,  T/2, 0, 4)",
        "scenario(K, PAR, T/2,  T/2, 1, 7)",
        "scenario(K/2, PAR, TT, 0, 0, 5)",
    )
    if any(result.count(scenario) != 1 for scenario in expected_scenarios):
        raise FixtureError("generated checker must retain each of the seven reduced scenarios")
    return result, seed_table


def _full_summary_calls() -> str:
    standard = "\n".join(
        f"                perf_print_summary({tag});" for tag in (0, 1, 2, 3, 4, 7, 5)
    )
    errors_only = "\n".join(
        f"                perf_print_summary({tag});" for tag in (0, 1, 2, 5)
    )
    parity_shortened = "\n".join(
        f"                perf_print_summary({tag});" for tag in (0, 1, 2, 3, 4, 7, 5, 6)
    )
    return (
        "        case (PERF_INSTANCE_ID)\n"
        "            2: begin\n"
        f"{errors_only}\n"
        "            end\n"
        "            5: begin\n"
        f"{parity_shortened}\n"
        "            end\n"
        "            default: begin\n"
        f"{standard}\n"
        "            end\n"
        "        endcase"
    )


def _fullize_event_instances(source_text: str) -> str:
    pattern = re.compile(
        r'(\$display\("STIM_(?:EVENT|SUMMARY) instance=)0([^\"]*)",\s*(.*?)\);',
        re.DOTALL,
    )

    def replace(match: re.Match[str]) -> str:
        arguments = match.group(3).strip()
        return (
            f'{match.group(1)}%0d{match.group(2)}",\n'
            f"                             PERF_INSTANCE_ID, {arguments});"
        )

    result, replacements = pattern.subn(replace, source_text)
    if replacements != 10:
        raise FixtureError(
            f"expected 10 STIM event/summary displays to gain instance IDs, got {replacements}"
        )
    return result


def _insert_root_instance_ids(root: str) -> str:
    pattern = re.compile(r"(codec_check\s*#\()([\s\S]*?)(\)\s+d(10|[0-9])\s*\()")

    def replace(match: re.Match[str]) -> str:
        instance_id = int(match.group(4))
        return (
            f"{match.group(1)}.PERF_INSTANCE_ID({instance_id}),"
            f"{match.group(2)}{match.group(3)}"
        )

    result, replacements = pattern.subn(replace, root)
    if replacements != 11:
        raise FixtureError(f"expected 11 root checker instances, found {replacements}")
    return result


def transform_full(
    source_text: str, base_seed: int
) -> tuple[str, list[dict[str, int | str | bool]], dict[int, dict[int, dict[str, int]]]]:
    """Transform the checker logic while retaining the original 11-instance root."""

    configs = _full_instance_configs(source_text)
    seed_table = _full_stream_seeds(base_seed, configs)
    reduced_source, _ = transform(source_text, base_seed)
    original_root_start = source_text.find("module rs_codec_tb;")
    transformed_root_start = reduced_source.find("module rs_codec_tb;")
    if original_root_start < 0 or transformed_root_start < 0:
        raise FixtureError("could not preserve the original full-workload root")
    transformed = (
        reduced_source[:transformed_root_start]
        + _insert_root_instance_ids(source_text[original_root_start:])
    )

    transformed = _replace_once(
        transformed,
        "    parameter integer T = (N-K)/2\n",
        "    parameter integer T = (N-K)/2,\n"
        "    parameter integer PERF_INSTANCE_ID = 0\n",
        "full-profile instance parameter",
    )
    transformed = _replace_region(
        transformed,
        "    function automatic [31:0] perf_seed_for;",
        "    function automatic perf_ready_for_cycle;",
        _full_seed_function(seed_table) + "\n\n",
        "full-profile seed function",
    )
    for old, new in (
        ("perf_seed_for(tag, 0)", "perf_seed_for(PERF_INSTANCE_ID, tag, 0)"),
        ("perf_seed_for(tag, 1)", "perf_seed_for(PERF_INSTANCE_ID, tag, 1)"),
        ("perf_seed_for(tag, 2)", "perf_seed_for(PERF_INSTANCE_ID, tag, 2)"),
        (
            "perf_seed_for(scenario, 3)",
            "perf_seed_for(PERF_INSTANCE_ID, scenario, 3)",
        ),
    ):
        transformed = _replace_once(transformed, old, new, "full-profile stream lookup")
    transformed = _replace_once(
        transformed,
        "    function automatic perf_ready_for_cycle;\n"
        "        input integer scenario;\n"
        "        input integer throttle;\n"
        "        input integer cycle_index;\n"
        "        reg [31:0] cycle_word;",
        "    function automatic perf_ready_for_cycle;\n"
        "        input integer throttle;\n"
        "        input integer cycle_index;\n"
        "        input [31:0] stream_seed;\n"
        "        reg [31:0] cycle_word;",
        "full-profile cycle-indexed ready function",
    )
    transformed = _replace_once(
        transformed,
        "cycle_word = perf_xorshift32(perf_seed_for(PERF_INSTANCE_ID, scenario, 3) ^ cycle_index);",
        "cycle_word = perf_xorshift32(stream_seed ^ cycle_index);",
        "full-profile ready-seed lookup",
    )
    ready_call = "perf_ready_for_cycle(tag, throttle, cycle)"
    if transformed.count(ready_call) != 2:
        raise FixtureError("expected two decoder ready-schedule call sites")
    transformed = transformed.replace(
        ready_call, "perf_ready_for_cycle(throttle, cycle, backpressure_seed)"
    )
    transformed = _replace_once(
        transformed,
        "        reg [M-1:0] sampled_data;",
        "        reg [M-1:0] sampled_data;\n"
        "        reg [31:0] backpressure_seed;",
        "full-profile cached backpressure seed",
    )
    transformed = _replace_once(
        transformed,
        "            perf_decoder_event_count = 0;\n"
        "            perf_ready_event_count = 0;\n",
        "            perf_decoder_event_count = 0;\n"
        "            perf_ready_event_count = 0;\n"
        "            backpressure_seed = perf_seed_for(PERF_INSTANCE_ID, tag, 3);\n",
        "full-profile scenario backpressure seed",
    )
    transformed = _replace_once(
        transformed,
        "        if (perf_preflight) begin\n"
        "            perf_kat_state=32'h6d2b79f5;",
        "        if (perf_preflight && PERF_INSTANCE_ID == 0) begin\n"
        "            perf_kat_state=32'h6d2b79f5;",
        "full-profile single KAT marker",
    )
    transformed = _replace_once(
        transformed,
        "reg perf_ready_event_value [0:4095];",
        "reg perf_ready_event_value [0:WD_LIMIT-1];",
        "full-profile complete ready-event storage",
    )
    transformed = _replace_once(
        transformed,
        "if (perf_ready_event_count < 4096) begin",
        "if (perf_ready_event_count < WD_LIMIT) begin",
        "full-profile complete ready-event capture",
    )
    transformed = _replace_once(
        transformed,
        "magnitude = magnitude_state & ((1<<M)-1);\n"
        "                        if (magnitude == 0)\n"
        "                            magnitude = 1;",
        "magnitude = (magnitude_state & ((1<<M)-1)) | 1;",
        "full-profile odd nonzero erasure magnitude",
    )
    for old, new, label in (
        (
            "kind=accepted_encoder seq=%0d cycle=%0d index=%0d data=%08x last=%0d",
            "kind=accepted_encoder seq=%0d cycle=%0d index=%0d data=%08x last=%b",
            "full-profile encoder event four-state last value",
        ),
        (
            "kind=accepted_decoder seq=%0d cycle=%0d index=%0d data=%08x last=%0d user=%0d",
            "kind=accepted_decoder seq=%0d cycle=%0d index=%0d data=%08x last=%b user=%b",
            "full-profile decoder event four-state flags",
        ),
        (
            "kind=backpressure seq=%0d cycle=%0d value=%0d",
            "kind=backpressure seq=%0d cycle=%0d value=%b",
            "full-profile ready event four-state value",
        ),
    ):
        transformed = _replace_once(transformed, old, new, label)
    summary_calls = (
        "        perf_print_summary(0);\n"
        "        perf_print_summary(1);\n"
        "        perf_print_summary(2);\n"
        "        perf_print_summary(3);\n"
        "        perf_print_summary(4);\n"
        "        perf_print_summary(7);\n"
        "        perf_print_summary(5);"
    )
    transformed = _replace_once(
        transformed,
        summary_calls,
        _full_summary_calls(),
        "full-profile active summary selection",
    )
    transformed = _fullize_event_instances(transformed)
    if not transformed.endswith("\n"):
        transformed += "\n"
    if transformed.count("module rs_codec_tb;") != 1:
        raise FixtureError("full generated source must contain exactly one root module")
    if re.search(r"\$display\(\"STIM_(?:EVENT|SUMMARY) instance=0", transformed):
        raise FixtureError("full generated source has a hard-coded logical instance ID")
    if transformed.count(".PERF_INSTANCE_ID(") != 11:
        raise FixtureError("full root must label all 11 logical instances")
    if "reg perf_ready_event_value [0:WD_LIMIT-1];" not in transformed:
        raise FixtureError("full transcript ready storage is smaller than its watchdog window")
    if "if (perf_ready_event_count < WD_LIMIT) begin" not in transformed:
        raise FixtureError("full transcript ready capture is still truncated")
    if transformed.count("if (perf_preflight && PERF_INSTANCE_ID == 0) begin") != 1:
        raise FixtureError("full profile must emit exactly one KAT transcript line")
    if "$urandom" in transformed or "$random" in transformed:
        raise FixtureError("full generated source still contains simulator RNG calls")
    return transformed, configs, seed_table


def _parse_seed(text: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("seed must be an integer such as 0x6d2b79f5") from error
    if value <= 0 or value > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("seed must be nonzero and fit in 32 bits")
    return value


def _metadata_scenarios(
    configs: list[dict[str, int | str | bool]],
    seed_table: dict[int, dict[int, dict[str, int]]],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    scenario_rows: list[dict[str, object]] = []
    instance_rows: list[dict[str, object]] = []
    totals = (
        "scenario_count", "payload_count", "corruption_count", "error_count",
        "erasure_count", "zero_erasure_count", "encoder_accept_count",
        "decoder_accept_count",
    )
    for config in configs:
        instance_id = int(config["instance_id"])
        specs = _scenario_specs(config)
        aggregate = {key: 0 for key in totals}
        for spec in specs:
            tag = int(spec["tag"])
            expected_counts = {
                "payload_count": int(spec["payload_count"]),
                "corruption_count": int(spec["corruption_count"]),
                "error_count": int(spec["errors"]),
                "erasure_count": int(spec["erasures"]),
                "zero_erasure_count": int(spec["zero_erasure_count"]),
                "encoder_accept_count": int(spec["encoder_accept_count"]),
                "decoder_accept_count": int(spec["decoder_accept_count"]),
            }
            scenario_rows.append(
                {
                    **spec,
                    "instance": config["instance"],
                    "instance_id": instance_id,
                    "expected_counts": expected_counts,
                    "stream_seeds": {
                        purpose: f"0x{seed_table[instance_id][tag][purpose]:08x}"
                        for purpose in INSTANCE_PURPOSES
                    },
                }
            )
            aggregate["scenario_count"] += 1
            for key in totals[1:]:
                aggregate[key] += expected_counts[key]
        instance_rows.append(
            {
                **config,
                "scenario_tags": [int(spec["tag"]) for spec in specs],
                "expected_summary_count": len(specs),
                "expected_counts": aggregate,
            }
        )
    return instance_rows, scenario_rows


def _algorithm_metadata() -> dict[str, object]:
    return {
        "name": "xorshift32",
        "shifts": [13, 17, 5],
        "right_shift": "logical",
        "truncate_bits_after_each_operation": 32,
        "seed_derivation": "sha256-prefix32-be-domain-separated-by-instance-scenario-purpose",
    }


def generate(
    source_path: Path,
    output_dir: Path,
    base_seed: int,
    case_name: str,
    workload: str = "reduced",
) -> Path:
    check_known_answer()
    source_bytes = source_path.read_bytes()
    try:
        source_text = source_bytes.decode("utf-8")
    except UnicodeDecodeError as error:
        raise FixtureError(f"source is not UTF-8 text: {source_path}") from error
    if workload == "reduced":
        transformed, reduced_seed_table = transform(source_text, base_seed)
        configs: list[dict[str, int | str | bool]] = [
            {
                "instance": "d0",
                "instance_id": 0,
                "n": 15,
                "k": 11,
                "fcr": 0,
                "primitive_poly": 0x13,
                "primitive_poly_hex": "0x13",
                "primitive_poly_parameter": 0,
                "primitive_power": 1,
                "implementation_mode": 1,
                "erasure_decoding": True,
                "codeword_shortening": True,
                "parity_shortening": False,
                "m": 4,
                "par": 4,
                "t": 2,
            }
        ]
        reduced_rows: list[dict[str, object]] = []
        for scenario in SCENARIOS:
            tag = int(scenario["tag"])
            payload_count = 5 if tag == 5 else 11
            codeword_length = payload_count + 4
            expected_counts = {
                "payload_count": payload_count,
                "corruption_count": int(scenario["errors"]) + int(scenario["erasures"]),
                "error_count": int(scenario["errors"]),
                "erasure_count": int(scenario["erasures"]),
                "zero_erasure_count": int(scenario["erasures"]) // 2,
                "encoder_accept_count": payload_count,
                "decoder_accept_count": codeword_length,
            }
            reduced_rows.append(
                {
                    **scenario,
                    "instance": "d0",
                    "instance_id": 0,
                    "kk": payload_count,
                    "par": 4,
                    "codeword_length": codeword_length,
                    "expected_counts": expected_counts,
                    "stream_seeds": {
                        purpose: f"0x{reduced_seed_table[tag][purpose]:08x}"
                        for purpose in PURPOSES
                    },
                }
            )
        instance_rows = [
            {
                **configs[0],
                "scenario_tags": [int(row["tag"]) for row in reduced_rows],
                "expected_summary_count": len(reduced_rows),
                "expected_counts": {
                    "scenario_count": len(reduced_rows),
                    "payload_count": sum(int(row["expected_counts"]["payload_count"])
                                         for row in reduced_rows),
                    "corruption_count": sum(int(row["expected_counts"]["corruption_count"])
                                             for row in reduced_rows),
                    "error_count": sum(int(row["expected_counts"]["error_count"])
                                       for row in reduced_rows),
                    "erasure_count": sum(int(row["expected_counts"]["erasure_count"])
                                         for row in reduced_rows),
                    "zero_erasure_count": sum(int(row["expected_counts"]["zero_erasure_count"])
                                               for row in reduced_rows),
                    "encoder_accept_count": sum(int(row["expected_counts"]["encoder_accept_count"])
                                                 for row in reduced_rows),
                    "decoder_accept_count": sum(int(row["expected_counts"]["decoder_accept_count"])
                                                 for row in reduced_rows),
                },
            }
        ]
        profile = REDUCED_WORKLOAD_PROFILE
    elif workload == "full":
        transformed, configs, full_seed_table = transform_full(source_text, base_seed)
        instance_rows, reduced_rows = _metadata_scenarios(configs, full_seed_table)
        profile = FULL_WORKLOAD_PROFILE
    else:
        raise FixtureError(f"unknown workload profile: {workload}")
    if not transformed.endswith("\n"):
        transformed += "\n"

    output_dir.mkdir(parents=True, exist_ok=True)
    tb_path = output_dir / "rs_codec_tb.v"
    if tb_path.resolve() == source_path.resolve():
        raise FixtureError("output directory would overwrite the external source testbench")
    tb_bytes = transformed.encode("utf-8")
    tb_path.write_bytes(tb_bytes)

    metadata = {
        "schema_version": 2 if workload == "full" else 1,
        "case": case_name,
        "profile": profile,
        "workload_profile": profile,
        "instances": instance_rows,
        "expected_summary_count": sum(
            int(instance["expected_summary_count"]) for instance in instance_rows
        ),
        "expected_summary_identities": [
            {
                "instance": row["instance"],
                "instance_id": row["instance_id"],
                "scenario": row["tag"],
            }
            for row in reduced_rows
        ],
        "scenario_order": [
            {"instance": row["instance"], "tags": row["scenario_tags"]}
            for row in instance_rows
        ],
        "expected_counts": {
            key: sum(int(instance["expected_counts"].get(key, 0)) for instance in instance_rows)
            for key in (
                "scenario_count", "payload_count", "corruption_count", "error_count",
                "erasure_count", "zero_erasure_count", "encoder_accept_count",
                "decoder_accept_count",
            )
        },
        "algorithm_version": ALGORITHM_VERSION,
        "algorithm": _algorithm_metadata(),
        "directed_constraints": {
            "unique_corruption_positions_across_errors_and_erasures": True,
            "zero_magnitude_erasure_ordinals": "odd zero-based placement ordinals",
            "nonzero_erasure_magnitudes": "odd and nonzero",
            "error_magnitudes": "odd and nonzero",
            "backpressure": "dedicated per-instance/scenario stream indexed by cycle",
            "throttle_scenario_tags": [2, 7],
            "shortened_payload_division": "Verilog integer K/2 truncates toward zero",
            "input_drive_edge": "negedge clk",
            "handshake_sample_edge": "posedge clk before nonblocking updates",
            "correctness_checks": [
                "all encoded syndromes are zero",
                "decoded output equals the original encoded codeword",
                "uncorrectable remains deasserted",
                "no encode/decode watchdog timeout",
                "no X/Z in required handshake and data fields",
            ],
        },
        "base_seed": f"0x{base_seed:08x}",
        "kat_seed": "0x6d2b79f5",
        "kat_words": list(KNOWN_ANSWER),
        "expected_kat_line": "STIM_KAT " + " ".join(KNOWN_ANSWER),
        "source": {
            "path": str(source_path),
            "sha256": hashlib.sha256(source_bytes).hexdigest(),
        },
        "generated_testbench": {
            "path": tb_path.name,
            "sha256": hashlib.sha256(tb_bytes).hexdigest(),
        },
        "transcript": {
            "preflight_plusarg": "+PERF_PREFLIGHT",
            "event_prefix": "STIM_EVENT ",
            "summary_prefix": "STIM_SUMMARY ",
            "canonical_sort_keys": ["instance", "scenario", "kind", "seq"],
            "summary_identity_keys": ["instance", "scenario"],
            "ready_window_end_inclusive": True,
        },
        "scenarios": reduced_rows,
    }
    if workload == "reduced":
        metadata["design"] = {
            "n": 15,
            "k": 11,
            "fcr": 0,
            "primitive_poly": "0x13",
            "primitive_power": 1,
            "implementation_mode": 1,
            "erasure_decoding": True,
            "codeword_shortening": True,
            "parity_shortening": False,
        }
    else:
        metadata["expected_stream_seed_count"] = sum(
            len(row["stream_seeds"]) for row in reduced_rows
        )
    manifest_path = output_dir / "fixture_manifest.json"
    manifest_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest_path


def _arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="original or mixed-language rs_codec_tb.v")
    parser.add_argument("--output-dir", required=True, type=Path, help="isolated fixture output directory")
    parser.add_argument("--seed", default="0x6d2b79f5", type=_parse_seed, help="nonzero 32-bit base seed")
    parser.add_argument("--case", default="unspecified", help="manifest case label")
    parser.add_argument(
        "--workload",
        choices=("reduced", "full"),
        default="reduced",
        help="reduced RS(15,11) iteration fixture or original 11-instance codec workload",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _arguments(sys.argv[1:] if argv is None else argv)
    try:
        manifest = generate(
            args.source, args.output_dir, args.seed, args.case, args.workload
        )
    except (FixtureError, OSError) as error:
        print(f"perf_codec_fixture: {error}", file=sys.stderr)
        return 2
    print(manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
