#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Generate deterministic RS(255,223) throughput fixtures.

The generated testbench is a benchmark-only copy. It retains the source
testbench's decoder and encoder logic while limiting each KES-pool variant to
either two codewords for reduced iteration or the original twelve codewords
for full-size transfer, while making all randomized stimulus reproducible.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


DEFAULT_SEED = 0x6D2B79F5
MASK32 = 0xFFFFFFFF
XORSHIFT_VERSION = "xorshift32-shift13-17-5-v1"
XORSHIFT_KAT = (
    0x40AEC71F,
    0x91E00C19,
    0x9C0FE128,
    0x6570F69D,
    0x0FCE02CC,
)
STREAM_CONSTANTS = {
    "instance": 0x9E3779B9,
    "scenario": 0x85EBCA6B,
    "purpose": 0xC2B2AE35,
}
PURPOSES = {
    "payload": 0,
    "corruption_magnitude": 2,
}
VARIANTS = (
    {"instance": "u0", "stream_id": 0, "mode": 0, "num_kes": 1},
    {"instance": "u1", "stream_id": 1, "mode": 0, "num_kes": 2},
    {"instance": "u2", "stream_id": 2, "mode": 0, "num_kes": 4},
    {"instance": "u3", "stream_id": 3, "mode": 0, "num_kes": 8},
)
FULL_VARIANTS = VARIANTS + (
    {"instance": "u4", "stream_id": 4, "mode": 1, "num_kes": 1},
    {"instance": "u5", "stream_id": 5, "mode": 1, "num_kes": 2},
)
WORKLOAD_CODEWORDS = {"reduced": 2, "full": 12}


def xorshift32(state: int) -> int:
    """Return the next unsigned 32-bit xorshift value."""
    value = state & MASK32
    value ^= (value << 13) & MASK32
    value ^= value >> 17
    value ^= (value << 5) & MASK32
    return value & MASK32


def stream_seed(root_seed: int, instance: int, scenario: int, purpose: int) -> int:
    """Derive a nonzero stream seed from its stable stimulus identity."""
    value = root_seed & MASK32
    value ^= ((instance + 1) * STREAM_CONSTANTS["instance"]) & MASK32
    value ^= ((scenario + 1) * STREAM_CONSTANTS["scenario"]) & MASK32
    value ^= ((purpose + 1) * STREAM_CONSTANTS["purpose"]) & MASK32
    return value or 1


def verify_known_answer() -> None:
    state = DEFAULT_SEED
    actual = []
    for _ in XORSHIFT_KAT:
        state = xorshift32(state)
        actual.append(state)
    if tuple(actual) != XORSHIFT_KAT:
        raise RuntimeError(
            "xorshift32 known-answer check failed: "
            + " ".join(f"{word:08x}" for word in actual)
        )


def _replace_once(source: str, pattern: str, replacement: str, description: str) -> str:
    matches = list(re.finditer(pattern, source, flags=re.MULTILINE))
    if len(matches) != 1:
        raise ValueError(
            f"expected one {description} in source testbench; found {len(matches)}"
        )
    updated, count = re.subn(pattern, replacement, source, count=1, flags=re.MULTILINE)
    if count != 1:
        raise ValueError(f"expected one {description} in source testbench; found {count}")
    return updated


def _hdl_helpers(seed: int) -> str:
    return f"""    localparam [31:0] STIM_HASH_INITIAL = 32'h811c9dc5;
    localparam integer PURPOSE_PAYLOAD = 0;
    localparam integer PURPOSE_CORRUPTION_MAGNITUDE = 2;

    // Unsigned xorshift32-shift13-17-5-v1 with 32-bit truncation.
    function automatic [31:0] xorshift32;
        input [31:0] state;
        reg [31:0] value;
        begin
            value = state;
            value = value ^ (value << 13);
            value = value ^ (value >> 17);
            value = value ^ (value << 5);
            xorshift32 = value;
        end
    endfunction

    function automatic [31:0] stream_seed;
        input [31:0] root_seed;
        input integer instance_id;
        input integer scenario_id;
        input integer purpose_id;
        reg [31:0] value;
        begin
            value = root_seed;
            value = value ^ (32'h{STREAM_CONSTANTS['instance']:08x} * (instance_id + 1));
            value = value ^ (32'h{STREAM_CONSTANTS['scenario']:08x} * (scenario_id + 1));
            value = value ^ (32'h{STREAM_CONSTANTS['purpose']:08x} * (purpose_id + 1));
            if (value == 0)
                value = 32'h00000001;
            stream_seed = value;
        end
    endfunction

    function automatic [31:0] fingerprint_word;
        input [31:0] previous;
        input [31:0] word_value;
        reg [31:0] mixed;
        begin
            mixed = previous ^ word_value;
            fingerprint_word = mixed * 32'd16777619;
        end
    endfunction

"""


def _stimulus_declarations() -> str:
    return """    reg [31:0] payload_state;
    reg [31:0] magnitude_state;
    reg [31:0] payload_fp;
    reg [31:0] corruption_fp;
    reg [31:0] ready_fp;
    reg [31:0] accepted_fp;
    reg [31:0] kat_state;
    reg [31:0] kat_words [0:4];
    integer payload_count;
    integer corruption_count;
    integer ready_count;
    integer ready_high_count;
    integer ready_low_count;
    integer accepted_count;
    integer invalid_sample_count;
    integer ready_cycle;
    integer corruption_local;
    integer corruption_magnitude;
    integer preflight_mode;
    integer backpressure_active;
    reg [M-1:0] sampled_input_data;
    reg sampled_input_valid;
    reg sampled_input_ready;
    reg sampled_input_last;
    reg sampled_input_user;
    reg [M-1:0] sampled_output_data;
    reg sampled_output_valid;
    reg sampled_output_ready;
    reg sampled_output_last;
    reg sampled_output_user;
    reg sampled_uncorrectable;
    integer sampled_input_cycle;
    integer sampled_output_cycle;
"""


def _build_burst_task() -> str:
    return """    task automatic build_burst;
        integer c, i, j, p;
        begin
            for (c = 0; c < NCW; c = c + 1) begin
                payload_state = stream_seed(
                    FIXTURE_SEED, STREAM_ID, c, PURPOSE_PAYLOAD);
                magnitude_state = stream_seed(
                    FIXTURE_SEED, STREAM_ID, c, PURPOSE_CORRUPTION_MAGNITUDE);

                for (i = 0; i < K; i = i + 1) begin
                    payload_state = xorshift32(payload_state);
                    msg[i] = payload_state & ((1 << M) - 1);
                    payload_fp = fingerprint_word(payload_fp, STREAM_ID);
                    payload_fp = fingerprint_word(payload_fp, c);
                    payload_fp = fingerprint_word(payload_fp, i);
                    payload_fp = fingerprint_word(payload_fp, msg[i]);
                    payload_count = payload_count + 1;
                    if (preflight_mode)
                        $display("STIM_EVENT instance=%0d scenario=%0d kind=payload seq=%0d index=%0d data=%08x",
                                 STREAM_ID, c, i, i, msg[i]);
                end

                for (i = 0; i < 2*T; i = i + 1)
                    lf[i] = {M{1'b0}};
                for (i = 0; i < K; i = i + 1) begin
                    fb = msg[i] ^ lf[2*T-1];
                    for (j = 2*T-1; j > 0; j = j - 1)
                        lf[j] = lf[j-1] ^ gmul(fb, GP[j]);
                    lf[0] = gmul(fb, GP[0]);
                end

                for (i = 0; i < K; i = i + 1) begin
                    exp[c*N+i] = msg[i];
                    tx[c*N+i] = msg[i];
                end
                for (i = 0; i < 2*T; i = i + 1) begin
                    exp[c*N+K+i] = lf[2*T-1-i];
                    tx[c*N+K+i] = lf[2*T-1-i];
                end

                // Keep the original directed, spread error positions.
                for (i = 0; i < T; i = i + 1) begin
                    corruption_local = (i * (N / T)) % N;
                    p = c*N + corruption_local;
                    magnitude_state = xorshift32(magnitude_state);
                    corruption_magnitude =
                        (magnitude_state & ((1 << M) - 1)) | 1;
                    tx[p] = tx[p] ^ corruption_magnitude;
                    corruption_fp = fingerprint_word(corruption_fp, STREAM_ID);
                    corruption_fp = fingerprint_word(corruption_fp, c);
                    corruption_fp = fingerprint_word(corruption_fp, p);
                    corruption_fp = fingerprint_word(
                        corruption_fp, corruption_magnitude);
                    corruption_count = corruption_count + 1;
                    if (preflight_mode)
                        $display("STIM_EVENT instance=%0d scenario=%0d kind=corruption seq=%0d index=%0d position=%0d magnitude=%08x",
                                 STREAM_ID, c, i, i, p, corruption_magnitude);
                end
            end
        end
    endtask
"""


def _simulation_processes(seed: int) -> str:
    return f"""    integer cyc;
    integer t_first, t_last, errs;
    integer fi, oc;
    integer index_value;
    reg first;

    always @(posedge clk) begin
        if (resetn)
            cyc <= cyc + 1;
    end

    // This source harness has no output backpressure. Preserve its continuous
    // ready policy and fingerprint every cycle from its scenario cycle index.
    always @(negedge clk) begin
        if (backpressure_active) begin
            m_tready = 1'b1;
            ready_fp = fingerprint_word(ready_fp, ready_cycle);
            ready_fp = fingerprint_word(ready_fp, 32'd1);
            ready_count = ready_count + 1;
            ready_high_count = ready_high_count + 1;
            if (preflight_mode)
                $display("STIM_EVENT instance=%0d scenario=0 kind=ready seq=%0d cycle=%0d value=1",
                         STREAM_ID, ready_cycle, ready_cycle);
            ready_cycle = ready_cycle + 1;
        end
    end

    initial begin
        done = 0;
        resetn = 0;
        s_tvalid = 0;
        s_tlast = 0;
        s_tuser = 0;
        s_tdata = 0;
        m_tready = 1;
        cyc = 0;
        t_first = 0;
        t_last = 0;
        errs = 0;
        first = 1;
        fi = 0;
        oc = 0;
        preflight_mode = $test$plusargs("PERF_PREFLIGHT");
        backpressure_active = 0;
        ready_cycle = 0;
        ready_count = 0;
        ready_high_count = 0;
        ready_low_count = 0;
        accepted_count = 0;
        invalid_sample_count = 0;
        payload_count = 0;
        corruption_count = 0;
        payload_fp = STIM_HASH_INITIAL;
        corruption_fp = STIM_HASH_INITIAL;
        ready_fp = STIM_HASH_INITIAL;
        accepted_fp = STIM_HASH_INITIAL;

        kat_state = 32'h{DEFAULT_SEED:08x};
        for (index_value = 0; index_value < 5; index_value = index_value + 1) begin
            kat_state = xorshift32(kat_state);
            kat_words[index_value] = kat_state;
        end
        if (preflight_mode && STREAM_ID == 0)
            $display("STIM_KAT %08x %08x %08x %08x %08x",
                     kat_words[0], kat_words[1], kat_words[2],
                     kat_words[3], kat_words[4]);
        if (kat_words[0] !== 32'h40aec71f ||
            kat_words[1] !== 32'h91e00c19 ||
            kat_words[2] !== 32'h9c0fe128 ||
            kat_words[3] !== 32'h6570f69d ||
            kat_words[4] !== 32'h0fce02cc) begin
            invalid_sample_count = invalid_sample_count + 1;
            $display("STIM_INVALID instance=%0d reason=xorshift32-kat",
                     STREAM_ID);
        end

        build();
        build_burst();
        repeat (20) @(negedge clk);
        resetn = 1;
        repeat (5) @(posedge clk);
        wait (start);
        backpressure_active = 1;

        fork
            begin : feed
                fi = 0;
                @(negedge clk);
                s_tvalid = 1;
                s_tdata = tx[fi];
                s_tlast = ((fi % N) == N-1);
                s_tuser = 0;
                while (fi < NCW*N) begin
                    @(posedge clk);
                    sampled_input_valid = s_tvalid;
                    sampled_input_ready = s_tready;
                    sampled_input_data = s_tdata;
                    sampled_input_last = s_tlast;
                    sampled_input_user = 1'b0;
                    sampled_input_cycle = cyc;
                    #0.001;

                    if ((sampled_input_valid !== 1'b0 &&
                         sampled_input_valid !== 1'b1) ||
                        (sampled_input_ready !== 1'b0 &&
                         sampled_input_ready !== 1'b1) ||
                        (sampled_input_valid === 1'b1 &&
                         ((^sampled_input_data) === 1'bx ||
                          (sampled_input_last !== 1'b0 &&
                           sampled_input_last !== 1'b1) ||
                          (sampled_input_user !== 1'b0 &&
                           sampled_input_user !== 1'b1)))) begin
                        invalid_sample_count = invalid_sample_count + 1;
                        if (preflight_mode)
                            $display("STIM_INVALID instance=%0d cycle=%0d kind=input valid=%b ready=%b data=%h last=%b user=%b",
                                     STREAM_ID, sampled_input_cycle,
                                     sampled_input_valid,
                                     sampled_input_ready, sampled_input_data,
                                     sampled_input_last, sampled_input_user);
                    end

                    if (sampled_input_valid === 1'b1 &&
                        sampled_input_ready === 1'b1) begin
                        if (first) begin
                            t_first = sampled_input_cycle;
                            first = 0;
                        end
                        accepted_fp = fingerprint_word(
                            accepted_fp, sampled_input_cycle);
                        accepted_fp = fingerprint_word(accepted_fp, fi);
                        accepted_fp = fingerprint_word(
                            accepted_fp, sampled_input_data);
                        accepted_fp = fingerprint_word(
                            accepted_fp, sampled_input_last);
                        accepted_fp = fingerprint_word(
                            accepted_fp, sampled_input_user);
                        accepted_count = accepted_count + 1;
                        if (preflight_mode)
                            $display("STIM_EVENT instance=%0d scenario=0 kind=accepted seq=%0d cycle=%0d index=%0d data=%08x last=%0d user=%0d",
                                     STREAM_ID, accepted_count - 1,
                                     sampled_input_cycle, fi,
                                     sampled_input_data, sampled_input_last,
                                     sampled_input_user);
                        fi = fi + 1;
                    end

                    @(negedge clk);
                    if (fi < NCW*N) begin
                        s_tvalid = 1;
                        s_tdata = tx[fi];
                        s_tlast = ((fi % N) == N-1);
                        s_tuser = 0;
                    end else begin
                        s_tvalid = 0;
                        s_tlast = 0;
                    end
                end
            end
            begin : drain
                oc = 0;
                while (oc < NCW*N) begin
                    @(posedge clk);
                    sampled_output_valid = m_tvalid;
                    sampled_output_ready = m_tready;
                    sampled_output_data = m_tdata;
                    sampled_output_last = m_tlast;
                    sampled_output_user = m_tuser;
                    sampled_uncorrectable = unc;
                    sampled_output_cycle = cyc;
                    #0.001;
                    if ((sampled_output_valid !== 1'b0 &&
                         sampled_output_valid !== 1'b1) ||
                        (sampled_output_ready !== 1'b0 &&
                         sampled_output_ready !== 1'b1) ||
                        (sampled_output_valid === 1'b1 &&
                         ((^sampled_output_data) === 1'bx ||
                          (sampled_output_last !== 1'b0 &&
                           sampled_output_last !== 1'b1) ||
                          (sampled_output_user !== 1'b0 &&
                           sampled_output_user !== 1'b1) ||
                          (sampled_uncorrectable !== 1'b0 &&
                           sampled_uncorrectable !== 1'b1)))) begin
                        invalid_sample_count = invalid_sample_count + 1;
                        if (preflight_mode)
                            $display("STIM_INVALID instance=%0d cycle=%0d kind=output valid=%b ready=%b data=%h last=%b user=%b unc=%b",
                                     STREAM_ID, sampled_output_cycle,
                                     sampled_output_valid, sampled_output_ready,
                                     sampled_output_data, sampled_output_last,
                                     sampled_output_user,
                                     sampled_uncorrectable);
                    end
                    if (sampled_output_valid === 1'b1 &&
                        sampled_output_ready === 1'b1) begin
                        if (sampled_output_data !== exp[oc])
                            errs = errs + 1;
                        oc = oc + 1;
                    end
                end
                t_last = sampled_output_cycle;
            end
        join

        backpressure_active = 0;
        if (accepted_count != NCW*N || oc != NCW*N ||
            invalid_sample_count != 0)
            errs = errs + 1;
        $display("THRU [N%0d_m%0d_k%0d] cw=%0d cycles=%0d cyc_per_cw=%0d errs=%0d %s",
                 N, IMPL_MODE, NUM_KES, NCW, (t_last - t_first),
                 (t_last - t_first) / NCW, errs,
                 (errs == 0) ? "PASS" : "FAIL");
        $display("STIM_SUMMARY instance=%0d scenario=0 payload_count=%0d payload_fp=%08x corruption_count=%0d corruption_fp=%08x ready_policy=constant_1 ready_count=%0d ready_ones=%0d ready_zeros=%0d ready_fp=%08x accepted_count=%0d accepted_fp=%08x output_count=%0d invalid_samples=%0d errs=%0d",
                 STREAM_ID, payload_count, payload_fp, corruption_count,
                 corruption_fp, ready_count, ready_high_count,
                 ready_low_count, ready_fp, accepted_count, accepted_fp,
                 oc, invalid_sample_count, errs);
        repeat (10) @(posedge clk);
        done = 1'b1;
    end
"""


def transform_source(source: str, seed: int, workload: str = "reduced") -> str:
    """Apply bounded edits to one of the checked-in external source copies."""
    if workload not in WORKLOAD_CODEWORDS:
        raise ValueError(f"unsupported throughput workload: {workload}")
    codewords = WORKLOAD_CODEWORDS[workload]
    if "$urandom" not in source:
        raise ValueError("source testbench has no expected $urandom stimulus sites")
    if "module rs_thru #(" not in source or "module rs_thru_tb;" not in source:
        raise ValueError("source is not the expected rs_thru_tb testbench")

    updated = _replace_once(
        source,
        r"parameter integer NUM_KES\s*=\s*1,\s*\n\s*parameter integer NCW\s*=\s*12\s*// codewords in the burst",
        "parameter integer NUM_KES   = 1,\n"
        f"    parameter [31:0] FIXTURE_SEED = 32'h{seed:08x},\n"
        "    parameter integer STREAM_ID = 0,\n"
        + (
            "    parameter integer NCW       = 2      // shortened benchmark burst"
            if workload == "reduced"
            else "    parameter integer NCW       = 12     // full benchmark burst"
        ),
        "throughput module burst parameters",
    )
    updated = _replace_once(
        updated,
        r"    localparam integer POLY = \(PRIM_POLY != 0\) \? PRIM_POLY : dflt_poly\(M\);",
        "    localparam integer POLY = (PRIM_POLY != 0) ? PRIM_POLY : dflt_poly(M);\n\n"
        + _hdl_helpers(seed),
        "field-polynomial declaration",
    )
    updated = _replace_once(
        updated,
        r"    reg \[M-1:0\] fb;",
        "    reg [M-1:0] fb;\n\n" + _stimulus_declarations(),
        "encoder feedback declaration",
    )
    updated = _replace_once(
        updated,
        r"    task automatic build_burst;[\s\S]*?    endtask",
        _build_burst_task().rstrip(),
        "burst-generation task",
    )

    process_start = updated.find("    integer cyc;", updated.find("module rs_thru #("))
    if process_start < 0:
        raise ValueError("source is missing the throughput simulation process")
    process_end = updated.find("\nendmodule", process_start)
    if process_end < 0:
        raise ValueError("source is missing the rs_thru module terminator")
    updated = (
        updated[:process_start]
        + _simulation_processes(seed).rstrip()
        + updated[process_end:]
    )

    top_start = updated.find("module rs_thru_tb;")
    top_end = updated.find("\nendmodule", top_start)
    if top_start < 0 or top_end < 0:
        raise ValueError("source is missing the rs_thru_tb top module")
    if workload == "full":
        full_instances = "\n".join(
            f"    rs_thru #(.N(255), .K(223), .IMPL_MODE({variant['mode']}), "
            f".NUM_KES({variant['num_kes']}),\n"
            f"              .FIXTURE_SEED(32'h{seed:08x}), "
            f".STREAM_ID({variant['stream_id']}), .NCW(12))\n"
            f"        {variant['instance']} (.clk(clk), .start(start), "
            f".done(done[{variant['stream_id']}]));"
            for variant in FULL_VARIANTS
        )
        top = f"""module rs_thru_tb;
    localparam integer NM = 6;
    reg clk = 0;
    always #5 clk = ~clk;
    reg start = 0;
    wire [NM-1:0] done;

    // Restore all six original mode and solver-pool configurations at NCW=12.
{full_instances}

    initial begin
        repeat (5) @(posedge clk);
        start = 1;
        wait (&done);
        repeat (20) @(posedge clk);
        $display("THRU TESTS COMPLETE");
        $finish;
    end

    initial begin
        #20000000;
        $display("STIM_INVALID reason=timeout");
        $finish;
    end
endmodule
"""
    else:
        # Preserve this established reduced fixture byte-for-byte.
        top = f"""module rs_thru_tb;
    localparam integer NM = 4;
    reg clk = 0;
    always #5 clk = ~clk;
    reg start = 0;
    wire [NM-1:0] done;

    // Keep the four original mode-0 solver-pool depths, with two codewords.
    rs_thru #(.N(255), .K(223), .IMPL_MODE(0), .NUM_KES(1),
              .FIXTURE_SEED(32'h{seed:08x}), .STREAM_ID(0), .NCW(2))
        u0 (.clk(clk), .start(start), .done(done[0]));
    rs_thru #(.N(255), .K(223), .IMPL_MODE(0), .NUM_KES(2),
              .FIXTURE_SEED(32'h{seed:08x}), .STREAM_ID(1), .NCW(2))
        u1 (.clk(clk), .start(start), .done(done[1]));
    rs_thru #(.N(255), .K(223), .IMPL_MODE(0), .NUM_KES(4),
              .FIXTURE_SEED(32'h{seed:08x}), .STREAM_ID(2), .NCW(2))
        u2 (.clk(clk), .start(start), .done(done[2]));
    rs_thru #(.N(255), .K(223), .IMPL_MODE(0), .NUM_KES(8),
              .FIXTURE_SEED(32'h{seed:08x}), .STREAM_ID(3), .NCW(2))
        u3 (.clk(clk), .start(start), .done(done[3]));

    initial begin
        repeat (5) @(posedge clk);
        start = 1;
        wait (&done);
        repeat (20) @(posedge clk);
        $display("THRU TESTS COMPLETE");
        $finish;
    end

    initial begin
        #20000000;
        $display("STIM_INVALID reason=timeout");
        $finish;
    end
endmodule
"""
    updated = updated[:top_start] + top.rstrip() + updated[top_end + len("\nendmodule") :]

    if "$urandom" in updated or "$random" in updated:
        raise ValueError("generated testbench still contains simulator RNG calls")
    if updated.count("module rs_thru #(") != 1:
        raise ValueError("generated source must contain exactly one rs_thru module")
    if updated.count("STIM_SUMMARY instance=") != 1:
        raise ValueError("generated source must contain one summary display site")
    return updated


def _metadata(
    source_path: Path, generated: str, seed: int, workload: str = "reduced"
) -> dict[str, Any]:
    if workload not in WORKLOAD_CODEWORDS:
        raise ValueError(f"unsupported throughput workload: {workload}")
    codewords = WORKLOAD_CODEWORDS[workload]
    variants = VARIANTS if workload == "reduced" else FULL_VARIANTS
    source_bytes = source_path.read_bytes()
    generated_bytes = generated.encode("utf-8")
    positions = [index * (255 // 16) % 255 for index in range(16)]
    if len(set(positions)) != 16:
        raise ValueError("directed throughput corruption positions are not unique")

    streams = []
    for variant in variants:
        for scenario in range(codewords):
            for purpose_name, purpose_id in PURPOSES.items():
                value = stream_seed(seed, variant["stream_id"], scenario, purpose_id)
                if value == 0:
                    raise ValueError("derived stream seed must be nonzero")
                streams.append(
                    {
                        "instance": variant["instance"],
                        "scenario": scenario,
                        "purpose": purpose_name,
                        "purpose_id": purpose_id,
                        "seed": f"0x{value:08x}",
                    }
                )

    payload_count = 223 * codewords
    corruption_count = 16 * codewords
    transaction_count = 255 * codewords
    expected_summaries = [
        {
            "instance": variant["instance"],
            "stream_id": variant["stream_id"],
            "mode": variant["mode"],
            "num_kes": variant["num_kes"],
            "payload_count": payload_count,
            "corruption_count": corruption_count,
            "ready_policy": "constant_1",
            "accepted_count": transaction_count,
            "output_count": transaction_count,
            "invalid_samples": 0,
            "errs": 0,
        }
        for variant in variants
    ]
    return {
        "schema_version": 1,
        "fixture": (
            "rs255_223_mode0_two_codeword_throughput"
            if workload == "reduced"
            else "rs255_223_full_twelve_codeword_throughput"
        ),
        "workload_profile": (
            "reduced_two_codewords" if workload == "reduced" else "full_original"
        ),
        "reduced": workload == "reduced",
        "source": {
            "path": str(source_path.resolve()),
            "sha256": hashlib.sha256(source_bytes).hexdigest(),
        },
        "generated_testbench": {
            "path": "rs_thru_tb.v",
            "sha256": hashlib.sha256(generated_bytes).hexdigest(),
        },
        "configuration": {
            "n": 255,
            "k": 223,
            "mode": 0 if workload == "reduced" else "per_instance",
            "codewords_per_instance": codewords,
            "instances": [dict(variant) for variant in variants],
            "root_seed": f"0x{seed:08x}",
        },
        "expected": {
            "summary_count": len(variants),
            "thru_pass_count": len(variants),
            "completion_marker_count": 1,
            "kat_line_count": 1,
            "summaries": expected_summaries,
            "totals": {
                "payload_count": payload_count * len(variants),
                "corruption_count": corruption_count * len(variants),
                "accepted_count": transaction_count * len(variants),
                "output_count": transaction_count * len(variants),
                "invalid_samples": 0,
                "errs": 0,
            },
        },
        "random": {
            "algorithm": XORSHIFT_VERSION,
            "shifts": [13, 17, 5],
            "right_shift": "logical",
            "word_width": 32,
            "stream_seed_constants": {
                name: f"0x{value:08x}" for name, value in STREAM_CONSTANTS.items()
            },
            "zero_seed_replacement": "0x00000001",
            "known_answer": {
                "seed": f"0x{DEFAULT_SEED:08x}",
                "words": [f"0x{value:08x}" for value in XORSHIFT_KAT],
                "verified_by_generator": True,
            },
            "stream_count": len(streams),
            "streams": streams,
        },
        "corruption": {
            "count_per_codeword": 16,
            "error_count_per_codeword": 16,
            "erasure_count_per_codeword": 0,
            "location_policy": "directed-spread-i-times-N-div-T-mod-N-v1",
            "local_positions": positions,
            "locations_unique_per_codeword": True,
            "location_random_stream": None,
            "magnitude_policy": "xorshift32-low-M-bits-or-one",
        },
        "ready": {
            "policy": "constant-1-preserved-from-original-throughput-testbench",
            "schedule_fingerprint": "fnv1a-word-v1 over cycle index and ready value",
            "backpressure": False,
            "random_stream": None,
            "reason": "original throughput testbench keeps m_tready asserted",
        },
        "fingerprints": {
            "algorithm": "fnv1a-word-v1",
            "initial": "0x811c9dc5",
            "transcript_event_prefix": "STIM_EVENT",
            "summary_prefix": "STIM_SUMMARY",
            "invalid_prefix": "STIM_INVALID",
        },
    }


def parse_seed(value: str) -> int:
    try:
        seed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("seed must be an integer such as 0x6d2b79f5") from error
    if seed <= 0 or seed > MASK32:
        raise argparse.ArgumentTypeError("seed must be a nonzero unsigned 32-bit integer")
    return seed


def generate(
    source_path: Path,
    output_dir: Path,
    seed: int,
    workload: str = "reduced",
) -> dict[str, Any]:
    verify_known_answer()
    if seed <= 0 or seed > MASK32:
        raise ValueError("seed must be a nonzero unsigned 32-bit integer")
    source_path = source_path.resolve(strict=True)
    source = source_path.read_text(encoding="utf-8")
    generated = transform_source(source, seed, workload)
    metadata = _metadata(source_path, generated, seed, workload)

    output_dir.mkdir(parents=True, exist_ok=True)
    testbench_path = output_dir / "rs_thru_tb.v"
    metadata_path = output_dir / "metadata.json"
    testbench_path.write_text(generated, encoding="utf-8", newline="\n")
    metadata_path.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return {
        "testbench": str(testbench_path.resolve()),
        "metadata": str(metadata_path.resolve()),
        "testbench_sha256": metadata["generated_testbench"]["sha256"],
        "root_seed": metadata["configuration"]["root_seed"],
        "workload_profile": metadata["workload_profile"],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--seed", type=parse_seed, default=DEFAULT_SEED)
    parser.add_argument(
        "--workload",
        choices=tuple(WORKLOAD_CODEWORDS),
        default="reduced",
        help="two-codeword reduced corpus or full twelve-codeword transfer workload",
    )
    arguments = parser.parse_args(argv)
    try:
        result = generate(
            arguments.source,
            arguments.output_dir,
            arguments.seed,
            arguments.workload,
        )
    except (OSError, ValueError, RuntimeError) as error:
        print(f"perf_throughput_fixture: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
