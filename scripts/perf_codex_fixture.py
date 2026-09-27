#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Generate an isolated Codex Reed-Solomon reference performance fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


DEFAULT_SOURCE = Path(
    "/home/colin/vprojects/rs_codex/tb/tb_rs_decoder_reference.sv"
)
DEFAULT_SEED = 0x6D2B79F5
DEFAULT_CASE = "codex_reference_mode0_frames1"
REFERENCE_CASES = {
    f"codex_reference_mode{mode}_frames{frames}": (mode, frames)
    for mode in (0, 1) for frames in (1, 2)
}
ALGORITHM_VERSION = "xorshift32-shift13-17-5-v1"
KNOWN_ANSWER = [
    0x40AEC71F,
    0x91E00C19,
    0x9C0FE128,
    0x6570F69D,
    0x0FCE02CC,
]


def xorshift32(value: int) -> int:
    """Return one unsigned xorshift32 word, truncating after every shift step."""
    value &= 0xFFFFFFFF
    value ^= (value << 13) & 0xFFFFFFFF
    value &= 0xFFFFFFFF
    value ^= value >> 17
    value &= 0xFFFFFFFF
    value ^= (value << 5) & 0xFFFFFFFF
    return value & 0xFFFFFFFF


def verify_known_answer() -> None:
    word = 0x6D2B79F5
    observed: list[int] = []
    for _ in KNOWN_ANSWER:
        word = xorshift32(word)
        observed.append(word)
    if observed != KNOWN_ANSWER:
        raise RuntimeError(
            "xorshift32 known-answer check failed: "
            f"expected {[f'{item:08x}' for item in KNOWN_ANSWER]}, "
            f"got {[f'{item:08x}' for item in observed]}"
        )


def xorshift_words(seed: int, count: int = 5) -> list[int]:
    words: list[int] = []
    word = seed
    for _ in range(count):
        word = xorshift32(word)
        words.append(word)
    return words


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise ValueError(f"expected exactly one {label} anchor, found {count}")
    return source.replace(old, new, 1)


def generate_testbench(source: str, seed: int, case_id: str = DEFAULT_CASE) -> str:
    if seed == 0:
        raise ValueError("the stimulus seed must be nonzero")
    if case_id not in REFERENCE_CASES:
        raise ValueError(f"unsupported Codex reference case: {case_id}")
    mode, input_frames = REFERENCE_CASES[case_id]
    seed_words = xorshift_words(seed)
    known_answer_checks = "\n".join(
        "            state = xorshift32(state);\n"
        f"            if (state !== 32'h{word:08x})\n"
        f"                $fatal(1, \"xorshift32 KAT word {index} mismatch: %08x\", state);\n"
        for index, word in enumerate(KNOWN_ANSWER, start=1)
    )
    known_answer_task = (
        "\n\n    task automatic verify_xorshift32_known_answer;\n"
        "        logic [31:0] state;\n"
        "        begin\n"
        "            state = 32'h6d2b79f5;\n"
        + known_answer_checks
        + "\n        end\n"
        "    endtask"
    )
    custom_seed_task = ""
    custom_seed_check = ""
    if seed != DEFAULT_SEED:
        custom_seed_checks = "\n".join(
            "            state = xorshift32(state);\n"
            f"            if (state !== 32'h{word:08x})\n"
            f"                $fatal(1, \"campaign seed check word {index} mismatch: %08x\", state);"
            for index, word in enumerate(seed_words, start=1)
        )
        custom_seed_task = (
            "\n\n    task automatic verify_campaign_seed;\n"
            "        logic [31:0] state;\n"
            "        begin\n"
            "            state = PERF_SEED;\n"
            + custom_seed_checks
            + "\n        end\n"
            "    endtask"
        )
        custom_seed_check = "            verify_campaign_seed();\n"
    kat_marker = (
        f"STIM_KAT algorithm={ALGORITHM_VERSION} seed=0x6d2b79f5 words="
        + ",".join(f"{word:08x}" for word in KNOWN_ANSWER)
    )

    if mode == 0:
        source = replace_once(
            source,
            "    parameter integer MODE = 1,",
            "    parameter integer MODE = 0,",
            "mode parameter",
        )
    source = replace_once(
        source,
        "    parameter integer INPUT_FRAMES = 1\n);",
        "    parameter integer INPUT_FRAMES = 1,\n"
        f"    parameter logic [31:0] PERF_SEED = 32'h{seed:08x}\n);",
        "frame and seed parameters",
    )
    source = replace_once(
        source,
        "    logic [15:0] ready_lfsr;",
        """    logic [15:0] ready_lfsr;
    integer stimulus_cycle;
    integer active_input_frame, active_symbol_index;
    integer payload_count, corruption_count, ready_count, accepted_input_count;
    logic [63:0] payload_fingerprint, corruption_fingerprint;
    logic [63:0] ready_fingerprint, accepted_input_fingerprint;
    bit perf_preflight;

    function automatic logic [31:0] xorshift32(input logic [31:0] value);
        logic [31:0] next_value;
        begin
            next_value = value ^ (value << 13);
            next_value = next_value ^ (next_value >> 17);
            next_value = next_value ^ (next_value << 5);
            xorshift32 = next_value;
        end
    endfunction

    function automatic logic [63:0] hash_byte(
        input logic [63:0] hash_value,
        input logic [7:0] byte_value
    );
        begin
            hash_byte = (hash_value ^ {56'b0, byte_value}) * 64'h00000100000001b3;
        end
    endfunction

    function automatic logic [63:0] hash_u32(
        input logic [63:0] hash_value,
        input logic [31:0] word_value
    );
        logic [63:0] next_hash;
        begin
            next_hash = hash_byte(hash_value, word_value[31:24]);
            next_hash = hash_byte(next_hash, word_value[23:16]);
            next_hash = hash_byte(next_hash, word_value[15:8]);
            hash_u32 = hash_byte(next_hash, word_value[7:0]);
        end
    endfunction

    function automatic logic [63:0] hash_u8(
        input logic [63:0] hash_value,
        input logic [7:0] byte_value
    );
        begin
            hash_u8 = hash_byte(hash_value, byte_value);
        end
    endfunction""" + known_answer_task + custom_seed_task,
        "stimulus instrumentation declaration",
    )

    source = replace_once(
        source,
        "                    value = codeword[symbol_index];\n"
        "                    if ((frame_index == 0) && (symbol_index == 2))\n"
        "                        value = value ^ 8'ha7;\n"
        "                    s_axis_tdata = value;",
        """                    value = codeword[symbol_index];
                    active_input_frame = frame_index;
                    active_symbol_index = symbol_index;
                    if ((frame_index == 0) && (symbol_index == 2))
                        value = value ^ 8'ha7;
                    payload_count = payload_count + 1;
                    payload_fingerprint = hash_u32(payload_fingerprint,
                                                   frame_index);
                    payload_fingerprint = hash_u32(payload_fingerprint,
                                                   symbol_index);
                    payload_fingerprint = hash_u8(payload_fingerprint,
                                                   codeword[symbol_index]);
                    payload_fingerprint = hash_u8(payload_fingerprint, value);
                    if (perf_preflight)
                        $display("STIM_EVENT instance=0 scenario=0 kind=PAYLOAD seq=%0d cycle=%0d frame=%0d symbol=%0d source=%02h data=%02h",
                                 payload_count - 1, stimulus_cycle,
                                 frame_index, symbol_index,
                                 codeword[symbol_index], value);
                    if (value != codeword[symbol_index]) begin
                        corruption_count = corruption_count + 1;
                        corruption_fingerprint = hash_u32(
                            corruption_fingerprint, frame_index);
                        corruption_fingerprint = hash_u32(
                            corruption_fingerprint, symbol_index);
                        corruption_fingerprint = hash_u8(
                            corruption_fingerprint, codeword[symbol_index]);
                        corruption_fingerprint = hash_u8(
                            corruption_fingerprint,
                            value ^ codeword[symbol_index]);
                        corruption_fingerprint = hash_u8(corruption_fingerprint,
                                                         value);
                        if (perf_preflight)
                            $display("STIM_EVENT instance=0 scenario=0 kind=CORRUPTION seq=%0d cycle=%0d frame=%0d symbol=%0d source=%02h mask=%02h data=%02h",
                                     corruption_count - 1, stimulus_cycle,
                                     frame_index, symbol_index,
                                     codeword[symbol_index],
                                     value ^ codeword[symbol_index], value);
                    end
                    s_axis_tdata = value;""",
        "directed payload instrumentation",
    )

    source = replace_once(
        source,
        "        integer symbol_index;\n"
        "        logic [M-1:0] value;",
        "        integer symbol_index;\n"
        "        logic [M-1:0] value;\n"
        "        logic input_ready_sample;",
        "input handshake sample variable",
    )

    source = replace_once(
        source,
        "            do @(posedge clk); while (!s_axis_tready);",
        "            do begin\n"
        "                @(posedge clk);\n"
        "                input_ready_sample = s_axis_tready;\n"
        "            end while (!input_ready_sample);",
        "initial input-ready sample",
    )

    source = replace_once(
        source,
        "                    @(posedge clk);\n"
        "                    if ((MODE == 1) || (NUM_KES > 1)) begin\n"
        "                        if (!s_axis_tready)",
        "                    @(posedge clk);\n"
        "                    input_ready_sample = s_axis_tready;\n"
        "                    if ((MODE == 1) || (NUM_KES > 1)) begin\n"
        "                        if (!input_ready_sample)",
        "input handshake captured before checking",
    )

    source = replace_once(
        source,
        "                    end else begin\n"
        "                        while (!s_axis_tready)\n"
        "                            @(posedge clk);\n"
        "                    end",
        "                    end else begin\n"
        "                        while (!input_ready_sample) begin\n"
        "                            @(posedge clk);\n"
        "                            input_ready_sample = s_axis_tready;\n"
        "                        end\n"
        "                    end",
        "input handshake wait uses captured readiness",
    )

    source = replace_once(
        source,
        "                    s_axis_tdata = value;\n"
        "                    s_axis_tlast = (symbol_index == 8);\n"
        "                    s_axis_tuser = (frame_index == 1) && (symbol_index == 1);\n"
        "                    s_axis_tvalid = 1'b1;",
        """                    s_axis_tdata = value;
                    s_axis_tlast = (symbol_index == 8);
                    s_axis_tuser = (frame_index == 1) && (symbol_index == 1);
                    s_axis_tvalid = 1'b1;
                    if (perf_preflight)
                        $display("STIM_EVENT instance=0 scenario=0 kind=INPUT_DRIVE seq=%0d cycle=%0d frame=%0d symbol=%0d valid=1 data=%02h last=%0d user=%0d",
                                 payload_count - 1, stimulus_cycle,
                                 frame_index, symbol_index,
                                 s_axis_tdata, s_axis_tlast, s_axis_tuser);""",
        "falling-edge input-drive transcript",
    )

    source = replace_once(
        source,
        "            s_axis_tvalid = 1'b0;\n"
        "            s_axis_tlast = 1'b0;\n"
        "            s_axis_tuser = 1'b0;",
        "            s_axis_tvalid = 1'b0;\n"
        "            s_axis_tlast = 1'b0;\n"
        "            s_axis_tuser = 1'b0;\n"
        "            if (perf_preflight)\n"
        "                $display(\"STIM_EVENT instance=0 scenario=0 kind=INPUT_IDLE seq=0 cycle=%0d frame=%0d symbol=9 valid=0 data=%02h last=0 user=0\",\n"
        "                         stimulus_cycle, INPUT_FRAMES, s_axis_tdata);",
        "falling-edge input-idle transcript",
    )

    source = replace_once(
        source,
        "    always @(negedge clk) begin\n"
        "        if (!resetn) begin\n"
        "            ready_lfsr <= 16'h1ace;\n"
        "            m_axis_tready <= 1'b0;\n"
        "        end else begin\n"
        "            ready_lfsr <= {ready_lfsr[14:0],\n"
        "                           ready_lfsr[15]^ready_lfsr[13]^ready_lfsr[12]^ready_lfsr[10]};\n"
        "            m_axis_tready <= ready_lfsr[0] | ready_lfsr[3];\n"
        "        end\n"
        "    end",
        """    always @(negedge clk) begin
        if (!resetn) begin
            ready_lfsr <= 16'h1ace;
            m_axis_tready <= 1'b0;
        end else begin
            ready_lfsr <= {ready_lfsr[14:0],
                           ready_lfsr[15]^ready_lfsr[13]^ready_lfsr[12]^ready_lfsr[10]};
            m_axis_tready <= ready_lfsr[0] | ready_lfsr[3];
        end
    end""",
        "preserved deterministic backpressure generator",
    )

    source = replace_once(
        source,
        "    always @(posedge clk) begin\n"
        "        if (resetn && m_axis_tvalid && m_axis_tready) begin\n"
        "            if (m_axis_tdata !== codeword[output_position])\n"
        "                $fatal(1, \"decoder reference frame=%0d pos=%0d expected=%02x got=%02x\",\n"
        "                       frame_number, output_position, codeword[output_position],\n"
        "                       m_axis_tdata);\n"
        "            if (m_axis_tuser !== ((frame_number == 0 && output_position == 2) ||\n"
        "                                  (frame_number == 1 && output_position == 1)))\n"
        "                $fatal(1, \"decoder reference TUSER mismatch frame=%0d pos=%0d\",\n"
        "                       frame_number, output_position);\n"
        "            if (m_axis_tlast !== (output_position == 8))\n"
        "                $fatal(1, \"decoder reference TLAST mismatch frame=%0d pos=%0d\",\n"
        "                       frame_number, output_position);\n"
        "            if (m_axis_tlast) begin\n"
        "                if ((frame_number == 0) &&\n"
        "                    (input_frames_completed != INPUT_FRAMES))\n"
        "                    $fatal(1,\n"
        "                        \"mode %0d accepted only %0d of %0d slot-capacity frames before first output completed\",\n"
        "                        MODE, input_frames_completed, INPUT_FRAMES);\n"
        "                if (uncorrectable ||\n"
        "                    (corrected_syms != ((frame_number < 2) ? 1 : 0)))\n"
        "                    $fatal(1, \"decoder reference status failure frame=%0d\", frame_number);\n"
        "                if (frame_number == 0) begin\n"
        "                    if ((corrected_bits != 5) || (corrected_0_to_1 != 2) ||\n"
        "                        (corrected_1_to_0 != 3))\n"
        "                        $fatal(1, \"decoder reference bit statistics failure\");\n"
        "                end else if ((corrected_bits != 0) || (corrected_0_to_1 != 0) ||\n"
        "                             (corrected_1_to_0 != 0)) begin\n"
        "                    $fatal(1, \"decoder reference zero-magnitude statistics failure\");\n"
        "                end\n"
        "                frame_number <= frame_number + 1;\n"
        "                output_position <= 0;\n"
        "            end else begin\n"
        "                output_position <= output_position + 1;\n"
        "            end\n"
        "        end\n"
        "    end",
        """    always @(posedge clk) begin : sample_handshakes
        integer cycle_index;
        logic captured_input_valid, captured_input_ready;
        logic [M-1:0] captured_input_data;
        logic captured_input_last, captured_input_user;
        logic captured_output_valid, captured_output_ready;
        logic [M-1:0] captured_output_data;
        logic captured_output_last, captured_output_user;
        logic captured_uncorrectable;
        logic [7:0] captured_corrected_syms;
        logic [10:0] captured_corrected_bits;
        logic [10:0] captured_corrected_0_to_1, captured_corrected_1_to_0;

        captured_input_valid = s_axis_tvalid;
        captured_input_ready = s_axis_tready;
        captured_input_data = s_axis_tdata;
        captured_input_last = s_axis_tlast;
        captured_input_user = s_axis_tuser;
        captured_output_valid = m_axis_tvalid;
        captured_output_ready = m_axis_tready;
        captured_output_data = m_axis_tdata;
        captured_output_last = m_axis_tlast;
        captured_output_user = m_axis_tuser;
        captured_uncorrectable = uncorrectable;
        captured_corrected_syms = corrected_syms;
        captured_corrected_bits = corrected_bits;
        captured_corrected_0_to_1 = corrected_0_to_1;
        captured_corrected_1_to_0 = corrected_1_to_0;

        if (!resetn) begin
            stimulus_cycle = 0;
        end else begin
            cycle_index = stimulus_cycle;
            if ((captured_input_valid !== 1'b0) &&
                (captured_input_valid !== 1'b1))
                $fatal(1, "unknown input valid at cycle %0d", cycle_index);
            if ((captured_input_ready !== 1'b0) &&
                (captured_input_ready !== 1'b1))
                $fatal(1, "unknown input ready at cycle %0d", cycle_index);
            if ((captured_output_valid !== 1'b0) &&
                (captured_output_valid !== 1'b1))
                $fatal(1, "unknown output valid at cycle %0d", cycle_index);
            if ((captured_output_ready !== 1'b0) &&
                (captured_output_ready !== 1'b1))
                $fatal(1, "unknown output ready at cycle %0d", cycle_index);

            ready_count = ready_count + 1;
            ready_fingerprint = hash_u32(ready_fingerprint, cycle_index);
            ready_fingerprint = hash_u8(ready_fingerprint,
                                        {7'b0, captured_output_ready});
            if (perf_preflight)
                $display("STIM_EVENT instance=0 scenario=0 kind=READY seq=%0d cycle=%0d value=%0d",
                         ready_count - 1, cycle_index, captured_output_ready);

            if (captured_input_valid === 1'b1) begin
                if ((^captured_input_data === 1'bx) ||
                    ((captured_input_last !== 1'b0) &&
                     (captured_input_last !== 1'b1)) ||
                    ((captured_input_user !== 1'b0) &&
                     (captured_input_user !== 1'b1)))
                    $fatal(1, "unknown input transaction field at cycle %0d",
                           cycle_index);
            end
            if ((captured_input_valid === 1'b1) &&
                (captured_input_ready === 1'b1)) begin
                accepted_input_count = accepted_input_count + 1;
                accepted_input_fingerprint = hash_u32(
                    accepted_input_fingerprint, cycle_index);
                accepted_input_fingerprint = hash_u32(
                    accepted_input_fingerprint, active_input_frame);
                accepted_input_fingerprint = hash_u32(
                    accepted_input_fingerprint, active_symbol_index);
                accepted_input_fingerprint = hash_u8(
                    accepted_input_fingerprint, captured_input_data);
                accepted_input_fingerprint = hash_u8(
                    accepted_input_fingerprint,
                    {6'b0, captured_input_last, captured_input_user});
                if (perf_preflight)
                    $display("STIM_EVENT instance=0 scenario=0 kind=INPUT_ACCEPT seq=%0d cycle=%0d frame=%0d symbol=%0d valid=1 ready=1 data=%02h last=%0d user=%0d",
                             accepted_input_count - 1, cycle_index,
                             active_input_frame,
                             active_symbol_index, captured_input_data,
                             captured_input_last, captured_input_user);
            end

            if ((captured_output_valid === 1'b1) &&
                (captured_output_ready === 1'b1)) begin
                if ((^captured_output_data === 1'bx) ||
                    ((captured_output_last !== 1'b0) &&
                     (captured_output_last !== 1'b1)) ||
                    ((captured_output_user !== 1'b0) &&
                     (captured_output_user !== 1'b1)) ||
                    (captured_uncorrectable !== 1'b0 &&
                     captured_uncorrectable !== 1'b1) ||
                    (^captured_corrected_syms === 1'bx) ||
                    (^captured_corrected_bits === 1'bx) ||
                    (^captured_corrected_0_to_1 === 1'bx) ||
                    (^captured_corrected_1_to_0 === 1'bx))
                    $fatal(1, "unknown output transaction field at cycle %0d",
                           cycle_index);
            end
            stimulus_cycle = stimulus_cycle + 1;

            if ((captured_output_valid === 1'b1) &&
                (captured_output_ready === 1'b1)) begin
                // Values above were captured at the sampling edge. Check only
                // those snapshots after nonblocking updates have settled.
                #0.001;
                if (captured_output_data !== codeword[output_position])
                    $fatal(1, "decoder reference frame=%0d pos=%0d expected=%02x got=%02x",
                           frame_number, output_position,
                           codeword[output_position], captured_output_data);
                if (captured_output_user !==
                    ((frame_number == 0 && output_position == 2) ||
                     (frame_number == 1 && output_position == 1)))
                    $fatal(1, "decoder reference TUSER mismatch frame=%0d pos=%0d",
                           frame_number, output_position);
                if (captured_output_last !== (output_position == 8))
                    $fatal(1, "decoder reference TLAST mismatch frame=%0d pos=%0d",
                           frame_number, output_position);
                if (captured_output_last) begin
                    if ((frame_number == 0) &&
                        (input_frames_completed != INPUT_FRAMES))
                        $fatal(1,
                            "mode %0d accepted only %0d of %0d slot-capacity frames before first output completed",
                            MODE, input_frames_completed, INPUT_FRAMES);
                    if (captured_uncorrectable ||
                        (captured_corrected_syms !=
                         ((frame_number < 2) ? 1 : 0)))
                        $fatal(1, "decoder reference status failure frame=%0d",
                               frame_number);
                    if (frame_number == 0) begin
                        if ((captured_corrected_bits != 5) ||
                            (captured_corrected_0_to_1 != 2) ||
                            (captured_corrected_1_to_0 != 3))
                            $fatal(1,
                                   "decoder reference bit statistics failure");
                    end else if ((captured_corrected_bits != 0) ||
                                 (captured_corrected_0_to_1 != 0) ||
                                 (captured_corrected_1_to_0 != 0)) begin
                        $fatal(1,
                               "decoder reference zero-magnitude statistics failure");
                    end
                    frame_number = frame_number + 1;
                    output_position = 0;
                end else begin
                    output_position = output_position + 1;
                end
            end
        end
    end""",
        "sample-edge capture and deferred output checker",
    )

    source = replace_once(
        source,
        "        input_frames_completed = 0;\n"
        "        codeword[0]=8'h01;",
        f"""        input_frames_completed = 0;
        stimulus_cycle = 0;
        active_input_frame = 0;
        active_symbol_index = 0;
        payload_count = 0;
        corruption_count = 0;
        ready_count = 0;
        accepted_input_count = 0;
        payload_fingerprint = 64'hcbf29ce484222325;
        corruption_fingerprint = 64'hcbf29ce484222325;
        ready_fingerprint = 64'hcbf29ce484222325;
        accepted_input_fingerprint = 64'hcbf29ce484222325;
        perf_preflight = $test$plusargs("PERF_PREFLIGHT");
        if (perf_preflight) begin
            verify_xorshift32_known_answer();
            $display("{kat_marker}");
{custom_seed_check}        end
        codeword[0]=8'h01;""",
        "stimulus state initialization",
    )

    source = replace_once(
        source,
        "        repeat (4) @(posedge clk);\n"
        "        resetn <= 1'b1;\n"
        "        repeat (3) @(posedge clk);",
        "        repeat (4) @(negedge clk);\n"
        "        #0.001 resetn = 1'b1;\n"
        "        repeat (3) @(posedge clk);",
        "falling-edge reset drive",
    )

    source = replace_once(
        source,
        "        repeat (4) @(posedge clk);\n"
        "        $display(\"PASS: reference decoder mode %0d processed %0d input frame(s)\",\n"
        "                 MODE, INPUT_FRAMES);",
        """        repeat (4) @(posedge clk);
        @(negedge clk);
        #0.001;
        $display("STIM_SUMMARY instance=0 scenario=0 case=codex_mode0_reference mode=%0d input_frames=%0d payload_count=%0d payload_fnv64=%016h corruption_count=%0d corruption_fnv64=%016h ready_cycles=%0d ready_fnv64=%016h accepted_inputs=%0d accepted_fnv64=%016h",
                 MODE, INPUT_FRAMES, payload_count, payload_fingerprint,
                 corruption_count, corruption_fingerprint, ready_count,
                 ready_fingerprint, accepted_input_count,
                 accepted_input_fingerprint);
        $display("PASS: reference decoder mode %0d processed %0d input frame(s)",
                 MODE, INPUT_FRAMES);""",
        "final stimulus summary",
    )

    # A two-frame Codex run includes a zero-magnitude erasure. It keeps the
    # payload byte unchanged, but TUSER marks the transaction as an erasure;
    # represent that directed location in the corruption stream as well.
    if input_frames == 2:
        source = replace_once(
            source,
            "                    if (value != codeword[symbol_index]) begin",
            "                    if ((value != codeword[symbol_index]) ||\n"
            "                        ((frame_index == 1) && (symbol_index == 1))) begin",
            "directed error-or-erasure condition",
        )
        source = replace_once(
            source,
            "                        corruption_fingerprint = hash_u8(corruption_fingerprint,\n"
            "                                                         value);",
            "                        corruption_fingerprint = hash_u8(corruption_fingerprint,\n"
            "                                                         value);\n"
            "                        corruption_fingerprint = hash_u8(\n"
            "                            corruption_fingerprint,\n"
            "                            {7'b0, (frame_index == 1) &&\n"
            "                             (symbol_index == 1)});",
            "erasure identity in corruption fingerprint",
        )
        source = replace_once(
            source,
            "kind=CORRUPTION seq=%0d cycle=%0d frame=%0d symbol=%0d source=%02h mask=%02h data=%02h\",",
            "kind=CORRUPTION seq=%0d cycle=%0d frame=%0d symbol=%0d source=%02h mask=%02h data=%02h user=%0d erasure=%0d\",",
            "erasure event fields",
        )
        source = replace_once(
            source,
            "                                     value ^ codeword[symbol_index], value);",
            "                                     value ^ codeword[symbol_index], value,\n"
            "                                     (frame_index == 1) &&\n"
            "                                         (symbol_index == 1),\n"
            "                                     (frame_index == 1) &&\n"
            "                                         (symbol_index == 1));",
            "erasure event values",
        )

    # Keep the original mode-0/one-frame fixture byte-for-byte stable. Other
    # wrappers use an accurate case label while retaining identical stimulus.
    if case_id != DEFAULT_CASE:
        source = replace_once(
            source,
            "case=codex_mode0_reference",
            f"case={case_id}",
            "case-specific summary identity",
        )

    return source


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_seed(value: str) -> int:
    try:
        seed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("seed must be an integer such as 0x6d2b79f5") from error
    if seed <= 0 or seed > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("seed must be in the range 1..0xffffffff")
    return seed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--seed", type=parse_seed, default=DEFAULT_SEED)
    parser.add_argument("--case", choices=sorted(REFERENCE_CASES), default=DEFAULT_CASE)
    parser.add_argument("--workload", choices=("full",), default=None)
    args = parser.parse_args()

    verify_known_answer()
    if not args.source.is_file():
        parser.error(f"source testbench does not exist: {args.source}")

    original = args.source.read_text(encoding="utf-8")
    mode, input_frames = REFERENCE_CASES[args.case]
    generated = generate_testbench(original, args.seed, args.case)
    known_answer_marker = (
        f"STIM_KAT algorithm={ALGORITHM_VERSION} seed=0x6d2b79f5 words="
        + ",".join(f"{word:08x}" for word in KNOWN_ANSWER)
    )
    campaign_seed_words = xorshift_words(args.seed)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output_path = args.output_dir / "tb_rs_decoder_reference.sv"
    metadata_path = args.output_dir / "metadata.json"
    output_path.write_text(generated, encoding="utf-8", newline="\n")

    expected_symbols = 9 * input_frames
    configuration = {
        "symbol_width": 8,
        "field_poly": "0x11d",
        "fcr": 0,
        "prim": 1,
        "n": 255,
        "k": 223,
        "mode": mode,
        "verify": 1,
        "num_kes": 1,
        "input_frames": input_frames,
        "shorten_codewords": True,
        "shorten_parity": True,
        "erasure_decoding": True,
        "parity_syms": 4,
        "data_symbols_per_frame": 5,
        "symbols_per_frame": 9,
        "expected_input_symbols": expected_symbols,
        "expected_output_symbols": expected_symbols,
    }
    metadata = {
        "schema": "fsim-performance-codex-fixture-v1",
        "case": ("codex_mode0_reference" if args.case == DEFAULT_CASE else args.case),
        "case_id": args.case,
        "workload": "original",
        "source": str(args.source.resolve()),
        "source_sha256": sha256_file(args.source),
        "generated_testbench": output_path.name,
        "generated_testbench_sha256": sha256_text(generated),
        "generator": str(Path(__file__).resolve()),
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "mode": mode,
        "input_frames": input_frames,
        "num_kes": 1,
        "n": 255,
        "k": 223,
        "shortened_input_symbols_per_frame": 9,
        "configuration": configuration,
        "seed": f"0x{args.seed:08x}",
        "seed_purpose": "xorshift32 self-check seed; directed Codex stimulus uses the recorded backpressure LFSR seed",
        "algorithm": ALGORITHM_VERSION,
        "algorithm_purpose": "Generator known-answer and optional campaign-seed self-check",
        "backpressure_algorithm": "lfsr16-taps-15-13-12-10-v1",
        "backpressure_seed": "0x1ace",
        "backpressure": "Preserved reference-testbench LFSR, advances once per active falling edge",
        "stimulus": {
            "instance": "0",
            "scenario": "0",
            "payload": {
                "algorithm": "codex-directed-shortened-codeword-v1",
                "seed": None,
                "description": "Original nine-symbol codeword repeated for each input frame",
                "codeword": ["01", "02", "03", "04", "05", "49", "7a", "63", "51"],
            },
            "corruption_locations": {
                "algorithm": "codex-directed-error-erasure-positions-v1",
                "seed": None,
                "positions": [{"frame": 0, "symbol": 2, "kind": "error"}]
                + ([{"frame": 1, "symbol": 1, "kind": "erasure"}]
                   if input_frames > 1 else []),
            },
            "corruption_magnitudes": {
                "algorithm": "codex-directed-error-mask-v1",
                "seed": None,
                "error_mask": "0xa7",
                "erasure_magnitude": "0x00",
            },
            "backpressure": {
                "algorithm": "lfsr16-taps-15-13-12-10-v1",
                "seed": "0x1ace",
                "cycle_indexed": True,
                "ready_expression": "state[0] | state[3]",
            },
        },
        "expected_summary": {
            "identity": {"instance": "0", "scenario": "0"},
            "summary_count": 1,
            "counts": {
                "payload": expected_symbols,
                "corruption": 2 if input_frames == 2 else 1,
                "accepted": expected_symbols,
            },
            "event_categories": ["payload", "corruption", "ready", "accepted"],
        },
        "correctness": {
            "output_frames": input_frames,
            "output_symbols": expected_symbols,
            "tuser_positions": [[0, 2]]
            + ([[1, 1]] if input_frames > 1 else []),
            "tlast_symbol_index": 8,
            "frame0_corrected_syms": 1,
            "frame0_corrected_bits": 5,
            "frame0_corrected_0_to_1": 2,
            "frame0_corrected_1_to_0": 3,
            "erasure_corrected_syms": 1 if input_frames > 1 else 0,
        },
        "workload_option": args.workload or "legacy-reduced-sentinel",
        "campaign_seed_check_words": [f"0x{word:08x}" for word in campaign_seed_words]
        if args.seed != DEFAULT_SEED
        else [],
        "known_answer_seed": "0x6d2b79f5",
        "known_answer_words": [f"0x{word:08x}" for word in KNOWN_ANSWER],
        "known_answer_marker": known_answer_marker,
        "transcript_plusarg": "+PERF_PREFLIGHT",
        "transcript_prefix": "STIM_EVENT",
        "summary_prefix": "STIM_SUMMARY",
        "fingerprint": "FNV-1a 64-bit, explicit big-endian integer bytes",
    }
    metadata_path.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"generated={output_path}")
    print(f"metadata={metadata_path}")
    print(f"testbench_sha256={metadata['generated_testbench_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
