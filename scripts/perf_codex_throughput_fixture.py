#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Generate an isolated Codex full-length decoder-throughput fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


DEFAULT_SOURCE = Path(
    "/home/colin/vprojects/rs_codex/tb/tb_rs_decoder_throughput.sv"
)
DEFAULT_SEED = 0x6D2B79F5
ALGORITHM_VERSION = "xorshift32-shift13-17-5-v1"
KNOWN_ANSWER = (
    0x40AEC71F,
    0x91E00C19,
    0x9C0FE128,
    0x6570F69D,
    0x0FCE02CC,
)
THROUGHPUT_CASES = {
    "codex_throughput_default": {
        "mode": 1,
        "verify": 1,
        "erasures": 1,
        "wrapper_parameters": {},
    },
    "codex_throughput_direct_syndrome": {
        "mode": 1,
        "verify": 0,
        "erasures": 0,
        "wrapper_parameters": {"VERIFY": 0, "ERASURES": 0},
    },
}


def xorshift32(value: int) -> int:
    value &= 0xFFFFFFFF
    value ^= (value << 13) & 0xFFFFFFFF
    value &= 0xFFFFFFFF
    value ^= value >> 17
    value &= 0xFFFFFFFF
    value ^= (value << 5) & 0xFFFFFFFF
    return value & 0xFFFFFFFF


def xorshift_words(seed: int, count: int = 5) -> list[int]:
    words: list[int] = []
    word = seed
    for _ in range(count):
        word = xorshift32(word)
        words.append(word)
    return words


def verify_known_answer() -> None:
    if xorshift_words(0x6D2B79F5) != list(KNOWN_ANSWER):
        raise RuntimeError("xorshift32 known-answer check failed")


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise ValueError(f"expected exactly one {label} anchor, found {count}")
    return source.replace(old, new, 1)


def parse_seed(value: str) -> int:
    try:
        seed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "seed must be an integer such as 0x6d2b79f5"
        ) from error
    if seed <= 0 or seed > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("seed must be in the range 1..0xffffffff")
    return seed


def testbench_kat(seed: int) -> tuple[str, str]:
    checks = "\n".join(
        "            state = xorshift32(state);\n"
        f"            if (state !== 32'h{word:08x})\n"
        f"                $fatal(1, \"xorshift32 KAT word {index} mismatch: %08x\", state);"
        for index, word in enumerate(KNOWN_ANSWER, 1)
    )
    task = (
        "\n\n    task automatic verify_xorshift32_known_answer;\n"
        "        logic [31:0] state;\n"
        "        begin\n"
        "            state = 32'h6d2b79f5;\n"
        + checks
        + "\n        end\n"
        "    endtask"
    )
    custom_check = ""
    if seed != DEFAULT_SEED:
        campaign_checks = "\n".join(
            "            state = xorshift32(state);\n"
            f"            if (state !== 32'h{word:08x})\n"
            f"                $fatal(1, \"campaign seed check word {index} mismatch: %08x\", state);"
            for index, word in enumerate(xorshift_words(seed), 1)
        )
        task += (
            "\n\n    task automatic verify_campaign_seed;\n"
            "        logic [31:0] state;\n"
            "        begin\n"
            "            state = PERF_SEED;\n"
            + campaign_checks
            + "\n        end\n"
            "    endtask"
        )
        custom_check = "            verify_campaign_seed();\n"
    marker = (
        f"STIM_KAT algorithm={ALGORITHM_VERSION} seed=0x6d2b79f5 words="
        + ",".join(f"{word:08x}" for word in KNOWN_ANSWER)
    )
    return task, custom_check + f'            $display("{marker}");\n'


def monitor_block() -> str:
    return r'''    always @(posedge clk) begin : sample_handshakes
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
        sampled_input_ready = captured_input_ready;

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
            if (captured_output_ready === 1'b1)
                ready_ones = ready_ones + 1;
            else
                ready_zeros = ready_zeros + 1;
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
                (captured_input_ready === 1'b0)) begin
                input_stalls = input_stalls + 1;
                $display("THROUGHPUT INPUT STALL beat=%0d frame=%0d position=%0d time=%0t",
                         active_input_beat, active_input_beat/N,
                         active_input_beat%N, $time);
                if ((MODE == 0) && (NUM_KES > 1) &&
                    (active_input_beat < (MODE0_FRAME_SLOTS*N)))
                    $fatal(1,
                        "mode 0 throttled before accepting every padded resident frame slot");
            end
            if ((captured_input_valid === 1'b1) &&
                (captured_input_ready === 1'b1)) begin
                accepted_input_count = accepted_input_count + 1;
                accepted_input_fingerprint = hash_u32(
                    accepted_input_fingerprint, cycle_index);
                accepted_input_fingerprint = hash_u32(
                    accepted_input_fingerprint, active_input_beat);
                accepted_input_fingerprint = hash_u32(
                    accepted_input_fingerprint, active_input_beat/N);
                accepted_input_fingerprint = hash_u32(
                    accepted_input_fingerprint, active_input_beat%N);
                accepted_input_fingerprint = hash_u8(
                    accepted_input_fingerprint, captured_input_data);
                accepted_input_fingerprint = hash_u8(
                    accepted_input_fingerprint,
                    {6'b0, captured_input_last, captured_input_user});
                if (perf_preflight)
                    $display("STIM_EVENT instance=0 scenario=0 kind=INPUT_ACCEPT seq=%0d cycle=%0d frame=%0d symbol=%0d valid=1 ready=1 data=%02h last=%0d user=%0d",
                             accepted_input_count - 1, cycle_index,
                             active_input_beat/N, active_input_beat%N,
                             captured_input_data, captured_input_last,
                             captured_input_user);
            end

            if (output_started && (output_frames < FRAMES) &&
                (captured_output_valid === 1'b0))
                output_bubbles = output_bubbles + 1;

            if ((captured_output_valid === 1'b1) &&
                (captured_output_ready === 1'b1)) begin
                if ((^captured_output_data === 1'bx) ||
                    ((captured_output_last !== 1'b0) &&
                     (captured_output_last !== 1'b1)) ||
                    ((captured_output_user !== 1'b0) &&
                     (captured_output_user !== 1'b1)) ||
                    ((captured_uncorrectable !== 1'b0) &&
                     (captured_uncorrectable !== 1'b1)) ||
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
                // These snapshots describe the values sampled before NBA.
                #0.001;
                output_count = output_count + 1;
                output_started = 1'b1;
                if (captured_output_data !== '0 || captured_uncorrectable)
                    $fatal(1, "invalid corrected zero-codeword output");
                if (captured_output_user !== (INJECT_ERRORS &&
                    ((output_position == 3) || (output_position == 100))))
                    $fatal(1, "throughput TUSER mismatch frame=%0d position=%0d",
                           output_frames, output_position);
                if (captured_output_last !== (output_position == N-1))
                    $fatal(1, "throughput TLAST mismatch frame=%0d position=%0d",
                           output_frames, output_position);
                if (captured_output_last) begin
                    if ((MODE == 0) && (output_frames == 0) &&
                        (input_beat < ((NUM_KES+1)*N)))
                        $fatal(1,
                            "mode 0 did not admit NUM_KES+1 frames before first output completed");
                    if (captured_corrected_syms != (INJECT_ERRORS ? 2 : 0) ||
                        captured_corrected_bits != (INJECT_ERRORS ? 2 : 0) ||
                        captured_corrected_0_to_1 != 0 ||
                        captured_corrected_1_to_0 != (INJECT_ERRORS ? 2 : 0))
                        $fatal(1, "throughput correction status mismatch frame=%0d",
                               output_frames);
                    output_frames = output_frames + 1;
                    output_position = 0;
                end else begin
                    output_position = output_position + 1;
                end
            end
        end
    end'''


def generate_testbench(source: str, seed: int, case_id: str) -> str:
    if seed == 0:
        raise ValueError("the stimulus seed must be nonzero")
    if case_id not in THROUGHPUT_CASES:
        raise ValueError(f"unsupported Codex throughput case: {case_id}")
    kat_task, kat_display = testbench_kat(seed)

    declarations = r'''    integer stimulus_cycle;
    integer active_input_beat, last_payload_beat;
    integer payload_count, corruption_count, ready_count;
    integer accepted_input_count, output_count, ready_ones, ready_zeros;
    logic sampled_input_ready;
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
    endfunction'''
    source = replace_once(
        source,
        "    logic output_started;",
        "    logic output_started;\n" + declarations + kat_task,
        "throughput stimulus declarations",
    )
    source = replace_once(
        source,
        "    parameter bit INJECT_ERRORS = 1'b0\n);",
        "    parameter bit INJECT_ERRORS = 1'b0,\n"
        f"    parameter logic [31:0] PERF_SEED = 32'h{seed:08x}\n);",
        "campaign seed parameter",
    )

    start = source.find(
        "    always @(posedge clk) begin\n"
        "        if (resetn) begin\n"
        "            if (s_axis_tvalid && !s_axis_tready) begin"
    )
    if start < 0:
        raise ValueError("expected throughput handshake checker anchor")
    end = source.find("\n    initial begin", start)
    if end < 0:
        raise ValueError("expected checker end before the stimulus initial block")
    source = source[:start] + monitor_block() + source[end:]

    source = replace_once(
        source,
        "        s_axis_tdata = '0;\n"
        "        s_axis_tvalid = 1'b0;\n"
        "        s_axis_tlast = 1'b0;\n"
        "        s_axis_tuser = 1'b0;\n"
        "        parity_syms = 6'd32;\n"
        "        m_axis_tready = 1'b1;\n"
        "        input_beat = 0;\n"
        "        input_stalls = 0;\n"
        "        output_position = 0;\n"
        "        output_frames = 0;\n"
        "        output_bubbles = 0;\n"
        "        output_started = 1'b0;",
        f"""        s_axis_tdata = '0;
        s_axis_tvalid = 1'b0;
        s_axis_tlast = 1'b0;
        s_axis_tuser = 1'b0;
        parity_syms = 6'd32;
        m_axis_tready = 1'b1;
        input_beat = 0;
        input_stalls = 0;
        output_position = 0;
        output_frames = 0;
        output_bubbles = 0;
        output_started = 1'b0;
        stimulus_cycle = 0;
        active_input_beat = 0;
        last_payload_beat = -1;
        payload_count = 0;
        corruption_count = 0;
        ready_count = 0;
        accepted_input_count = 0;
        output_count = 0;
        ready_ones = 0;
        ready_zeros = 0;
        sampled_input_ready = 1'b0;
        payload_fingerprint = 64'hcbf29ce484222325;
        corruption_fingerprint = 64'hcbf29ce484222325;
        ready_fingerprint = 64'hcbf29ce484222325;
        accepted_input_fingerprint = 64'hcbf29ce484222325;
        perf_preflight = $test$plusargs("PERF_PREFLIGHT");
        if (perf_preflight) begin
            verify_xorshift32_known_answer();
{kat_display}        end""",
        "throughput stimulus initialization",
    )

    source = replace_once(
        source,
        "        repeat (4) @(posedge clk);\n"
        "        resetn <= 1'b1;\n"
        "        do @(posedge clk); while (!s_axis_tready);",
        "        repeat (4) @(negedge clk);\n"
        "        #0.001 resetn = 1'b1;\n"
        "        do begin\n"
        "            @(posedge clk);\n"
        "            #0.002;\n"
        "        end while (!sampled_input_ready);",
        "falling-edge reset and sampled input-ready wait",
    )

    source = replace_once(
        source,
        "            s_axis_tvalid = 1'b1;\n"
        "            s_axis_tdata = INJECT_ERRORS ? error_mask(input_beat % N) : '0;\n"
        "            s_axis_tuser = 1'b0;\n"
        "            s_axis_tlast = ((input_beat % N) == N-1);\n"
        "            @(posedge clk);\n"
        "            if (s_axis_tready)\n"
        "                input_beat = input_beat + 1;",
        """            active_input_beat = input_beat;
            s_axis_tvalid = 1'b1;
            s_axis_tdata = INJECT_ERRORS ? error_mask(input_beat % N) : '0;
            s_axis_tuser = 1'b0;
            s_axis_tlast = ((input_beat % N) == N-1);
            if (input_beat != last_payload_beat) begin
                last_payload_beat = input_beat;
                payload_count = payload_count + 1;
                payload_fingerprint = hash_u32(payload_fingerprint, input_beat);
                payload_fingerprint = hash_u32(payload_fingerprint, input_beat/N);
                payload_fingerprint = hash_u32(payload_fingerprint, input_beat%N);
                payload_fingerprint = hash_u8(payload_fingerprint, s_axis_tdata);
                if (perf_preflight)
                    $display("STIM_EVENT instance=0 scenario=0 kind=PAYLOAD seq=%0d cycle=%0d frame=%0d symbol=%0d source=%02h data=%02h last=%0d user=%0d",
                             payload_count - 1, stimulus_cycle,
                             input_beat/N, input_beat%N,
                             s_axis_tdata, s_axis_tdata,
                             s_axis_tlast, s_axis_tuser);
            end
            @(posedge clk);
            #0.002;
            if (sampled_input_ready)
                input_beat = input_beat + 1;""",
        "falling-edge payload drive and sampled input acceptance",
    )

    summary = f'''        wait (output_frames == FRAMES);
        repeat (3) @(posedge clk);
        @(negedge clk);
        #0.001;
        $display("STIM_SUMMARY instance=0 scenario=0 case={case_id} mode=%0d frames=%0d n=%0d k=%0d verify=%0d erasures=%0d payload_count=%0d payload_fnv64=%016h corruption_count=%0d corruption_fnv64=%016h ready_cycles=%0d ready_fnv64=%016h ready_ones=%0d ready_zeros=%0d accepted_inputs=%0d accepted_fnv64=%016h output_count=%0d output_frames=%0d input_stalls=%0d output_bubbles=%0d",
                 MODE, FRAMES, N, K, VERIFY, ERASURES, payload_count,
                 payload_fingerprint, corruption_count, corruption_fingerprint,
                 ready_count, ready_fingerprint, ready_ones, ready_zeros,
                 accepted_input_count, accepted_input_fingerprint, output_count,
                 output_frames, input_stalls, output_bubbles);
        $display("THROUGHPUT: frames=%0d input_stalls=%0d output_bubbles=%0d",
                 FRAMES, input_stalls, output_bubbles);
        if ((MODE == 1) &&
            ((input_stalls != 0) || (output_bubbles != 0)))
            $fatal(1, "mode 1 failed continuous full-length throughput");
        if ((MODE == 1) && INJECT_ERRORS)
            $display("PASS: mode 1 continuous full-length correcting input/output throughput");
        else if (MODE == 1)
            $display("PASS: mode 1 continuous full-length input/output throughput");
        else if (INJECT_ERRORS)
            $display("PASS: mode 0 correcting multi-frame throughput and ordering");
        else
            $display("PASS: mode 0 multi-frame throughput and ordering");
        $finish;'''
    old_tail = '''        wait (output_frames == FRAMES);
        repeat (3) @(posedge clk);
        $display("THROUGHPUT: frames=%0d input_stalls=%0d output_bubbles=%0d",
                 FRAMES, input_stalls, output_bubbles);
        if ((MODE == 1) &&
            ((input_stalls != 0) || (output_bubbles != 0)))
            $fatal(1, "mode 1 failed continuous full-length throughput");
        if ((MODE == 1) && INJECT_ERRORS)
            $display("PASS: mode 1 continuous full-length correcting input/output throughput");
        else if (MODE == 1)
            $display("PASS: mode 1 continuous full-length input/output throughput");
        else if (INJECT_ERRORS)
            $display("PASS: mode 0 correcting multi-frame throughput and ordering");
        else
            $display("PASS: mode 0 multi-frame throughput and ordering");
        $finish;'''
    source = replace_once(source, old_tail, summary, "throughput final summary")
    return source


def sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--seed", type=parse_seed, default=DEFAULT_SEED)
    parser.add_argument("--case", choices=sorted(THROUGHPUT_CASES),
                        default="codex_throughput_default")
    parser.add_argument("--workload", choices=("full",), default=None)
    args = parser.parse_args()

    verify_known_answer()
    if not args.source.is_file():
        parser.error(f"source testbench does not exist: {args.source}")
    case_configuration = THROUGHPUT_CASES[args.case]
    verify = case_configuration["verify"]
    erasures = case_configuration["erasures"]
    mode = case_configuration["mode"]
    original = args.source.read_text(encoding="utf-8")
    generated = generate_testbench(original, args.seed, args.case)
    output_path = args.output_dir / "tb_rs_decoder_throughput.sv"
    metadata_path = args.output_dir / "metadata.json"
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output_path.write_text(generated, encoding="utf-8", newline="\n")

    expected_beats = 12 * 255
    marker = (
        f"STIM_KAT algorithm={ALGORITHM_VERSION} seed=0x6d2b79f5 words="
        + ",".join(f"{word:08x}" for word in KNOWN_ANSWER)
    )
    metadata = {
        "schema": "fsim-performance-codex-throughput-fixture-v1",
        "case": args.case,
        "case_id": args.case,
        "workload": "original",
        "source": str(args.source.resolve()),
        "source_sha256": sha256_file(args.source),
        "generated_testbench": output_path.name,
        "generated_testbench_sha256": sha256_text(generated),
        "generator": str(Path(__file__).resolve()),
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "seed": f"0x{args.seed:08x}",
        "algorithm": ALGORITHM_VERSION,
        "algorithm_purpose": "Fixed known-answer check; directed zero-codeword workload uses no random draws",
        "known_answer_seed": "0x6d2b79f5",
        "known_answer_words": [f"0x{word:08x}" for word in KNOWN_ANSWER],
        "known_answer_marker": marker,
        "transcript_plusarg": "+PERF_PREFLIGHT",
        "transcript_prefix": "STIM_EVENT",
        "summary_prefix": "STIM_SUMMARY",
        "fingerprint": "FNV-1a 64-bit, explicit big-endian integer bytes",
        "configuration": {
            "symbol_width": 8,
            "field_poly": "0x11d",
            "fcr": 0,
            "prim": 1,
            "n": 255,
            "k": 223,
            "frames": 12,
            "symbols_per_frame": 255,
            "expected_symbols": expected_beats,
            "mode": mode,
            "num_kes": 1,
            "verify": verify,
            "erasures": erasures,
            "fixed_features": 0,
            "inject_errors": 0,
            "shorten_codewords": True,
            "shorten_parity": True,
            "erasure_decoding": bool(erasures),
            "parity_syms": 32,
            "wrapper_parameters": case_configuration["wrapper_parameters"],
        },
        "stimulus": {
            "instance": "0",
            "scenario": "0",
            "payload": {
                "algorithm": "codex-directed-zero-codeword-v1",
                "seed": None,
                "description": "Twelve 255-symbol frames of zero payload",
            },
            "corruption_locations": {
                "algorithm": "none-v1",
                "seed": None,
                "positions": [],
            },
            "corruption_magnitudes": {
                "algorithm": "none-v1",
                "seed": None,
                "magnitudes": [],
            },
            "backpressure": {
                "algorithm": "constant-ready-v1",
                "seed": None,
                "ready_value": 1,
            },
        },
        "expected_summary": {
            "identity": {"instance": "0", "scenario": "0"},
            "summary_count": 1,
            "counts": {
                "payload": expected_beats,
                "corruption": 0,
                "accepted": expected_beats,
            },
            "event_categories": ["payload", "ready", "accepted"],
        },
        "correctness": {
            "input_symbols": expected_beats,
            "output_frames": 12,
            "output_symbols": expected_beats,
            "input_stalls": 0,
            "output_bubbles": 0,
            "all_data_zero": True,
            "tuser": False,
            "corrected_syms": 0,
            "corrected_bits": 0,
            "uncorrectable": False,
        },
        "workload_option": args.workload or "full",
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
