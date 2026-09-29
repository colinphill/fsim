<!-- SPDX-License-Identifier: Apache-2.0 -->

# V23 throughput: Verilog and VHDL DUT comparison

The same frozen fsim binary, CPU 0, O2 ThinLTO mode, generated 6 × 12 testbench traffic, and correctness checks produced one uninstrumented cold observation for each case. The Verilog DUT took **57.080 s** and the mixed-language case with the VHDL DUT took **16.381 s**. Both emitted byte-identical raw and canonical transcripts (raw SHA-256 `fb3c8e3df18367ea054d137b8f720846c93b9f65f5639cffec5d707744b8cf3c`; canonical `d50a786447827198d6b31780f9a6199dcb347ec7639454bf67c0fdb3b367bd59`), six summaries, and seven correctness lines. One cold observation per case establishes this observed difference, not repeatability or a causal speedup estimate.

| Uninstrumented cold phase | Verilog DUT | VHDL DUT | Difference |
|---|---:|---:|---:|
| Compile | 0.622 s | 1.242 s | −0.621 s |
| Elaborate | 6.489 s | 3.080 s | 3.409 s |
| Native setup + simulation | 49.967 s | 12.056 s | 37.911 s |
| **Total Wall** | **57.080 s** | **16.381 s** | **40.699 s** |
| Peak RSS | 1,011,032 KiB | 313,528 KiB | 697,504 KiB |

The [side-by-side histogram](../build/performance-campaign/verilog-throughput-fast-loop/v23-language-comparison/cpu-comparison.png) uses one absolute estimated sampled-user-CPU-seconds scale; each label also gives that case's percentage. The [SVG](../build/performance-campaign/verilog-throughput-fast-loop/v23-language-comparison/cpu-comparison.svg), [category data and top symbols](../build/performance-campaign/verilog-throughput-fast-loop/v23-language-comparison/category-data.json), and [raw exclusive symbol table](../build/performance-campaign/verilog-throughput-fast-loop/v23-language-comparison/raw-self-samples.tsv) preserve the full attribution. Each symbol sample is assigned to exactly one category. These seconds multiply a category's exclusive-sample share by that profiled run's measured user CPU. They are statistical allocations of **instrumented** CPU, not cold Wall seconds or removable time.

| Exclusive sampled CPU category | Verilog DUT | VHDL DUT |
|---|---:|---:|
| Generated native JIT | 5.64 s (12.2%) | 2.48 s (23.0%) |
| Logic values / drivers | 5.58 s (12.1%) | 0.61 s (5.7%) |
| Masked region / frontier | 5.50 s (11.9%) | 0.01 s (0.1%) |
| Signal publication / fanout | 4.74 s (10.3%) | 0.40 s (3.7%) |
| Fused cohort host control | 3.85 s (8.3%) | 0.23 s (2.2%) |
| System libraries / allocator | 3.53 s (7.6%) | 0.72 s (6.7%) |
| Other interpreter / lookup | 3.42 s (7.4%) | 0.91 s (8.4%) |
| Scheduler / update staging | 3.28 s (7.1%) | 0.73 s (6.8%) |
| Other mapped host | 2.21 s (4.8%) | 0.26 s (2.4%) |
| Application executor / reads | 2.26 s (4.9%) | 1.04 s (9.6%) |
| Ordinary process execution | 1.32 s (2.9%) | 0.36 s (3.3%) |
| LLVM, function unresolved | 0.92 s (2.0%) | 1.46 s (13.6%) |
| LLVM compilation / JIT setup | 0.82 s (1.8%) | 0.94 s (8.7%) |
| Container access | 0.85 s (1.8%) | 0.05 s (0.5%) |
| Physical work queue | 0.76 s (1.6%) | 0.28 s (2.6%) |
| Private bridge / local commit | 0.51 s (1.1%) | <0.01 s (<0.1%) |
| Native entry / validation | 0.59 s (1.3%) | 0.07 s (0.7%) |
| Prepared wave admission / stage | 0.36 s (0.8%) | 0.21 s (2.0%) |
| **Profiled user CPU / samples** | **46.14 s / 4,555** | **10.77 s / 1,059** |

For a compact reading of the same disjoint categories, fusion/cohort host control totals **10.22 s** for Verilog versus **0.46 s** for VHDL; publication, logic values, update staging, queue, and container access total **15.21 s** versus **2.07 s**. These groupings account for much of the *profiled CPU allocation gap*, but they do not prove each second could be removed. Generated native code itself accounts for **5.64 s** versus **2.48 s**. LLVM compilation and unresolved LLVM functions together account for **1.74 s** versus **2.40 s**; the larger VHDL share is partly a denominator effect and cannot be localized to LLVM functions more precisely.

The full counter diagnostic on the Verilog case bound **540** normalized terminal readers and represented **11,878,638** terminal activations, of which **11,868,565** joined another member's region call. Compared with frozen V22, physical masked-frontier callbacks fell by **11,089,911** and queue entries by **11,089,934**. Masked native calls stayed near **15.07 million**, and **11,756,696** private local commits remained. The V23 change therefore removed terminal scheduler tasks/callbacks; it did not remove the intermediate signal publications, private update commits, or all original member readiness work. Its one cold Verilog observation was 57.080 s versus a separate V22 observation of 72.240 s; one observation each does not establish a repeatable gain or assign its Wall difference to this change.

The source structure explains why the profiles differ in a plausible, specific way. [Verilog gf_mult.v](/home/colin/vprojects/reed_solomon/rtl/gf_mult.v:14) builds separate generated term vectors and a red reduction cascade as continuously assigned signals. [VHDL gf_mult.vhd](/home/colin/vprojects/rs-vhdl/rtl_vhdl/gf_mult.vhd:24) computes the corresponding arithmetic in function-local variables and has [one outward concurrent product assignment](/home/colin/vprojects/rs-vhdl/rtl_vhdl/gf_mult.vhd:68). The shared testbench inputs and six multiplier-lane configuration traffic are the same. The simulator must preserve the Verilog intermediate signals' delta, publication, and reader behavior; V23's fusion still performs substantial scheduling, host control, and value/update work for them. The VHDL function has much less externally scheduled intermediate state. Its ports are `std_logic_vector`, retaining nine-state semantics; a cheaper two-state model is not the explanation. This is a source-supported structural explanation consistent with the measured call paths, not a proof that every profile category is caused solely by the multiplier or that all four-/nine-state semantics coincide.

Profile method: `cpu-clock:u` at 99 Hz, DWARF call graphs, CPU affinity 0, all sampled threads, same frozen binary SHA-256 `5908b06850324b2c86d4c69d65c01f82bd3d81e34bcdfe829dd25fedfdb3d88f`. The Verilog profile had 4,555 samples, 46.14 s user and 0.64 s system CPU; the VHDL profile had 1,059 samples, 10.77 s user and 0.26 s system CPU. Both reported zero lost samples. All 91 Verilog and 144 VHDL unresolved samples map to `libLLVM.so.22.1`; their module is known, their function is not. The native phase includes JIT setup and simulation. Profiled elapsed times are separate from the cold observations, and the classifier uses exclusive self samples rather than inclusive call stacks. The source and binary [freeze manifest](../build/performance-campaign/verilog-throughput-fast-loop/v23-terminal-closure/freeze/manifest.json), [mechanism receipt](../build/performance-campaign/verilog-throughput-fast-loop/v23-terminal-closure/freeze/mechanism-diagnostic-receipt.json), [Verilog cold receipt](../build/performance-campaign/verilog-throughput-fast-loop/v23-terminal-closure/freeze/cold-receipt.json), and [VHDL cold receipt](../build/performance-campaign/verilog-throughput-fast-loop/v23-terminal-closure/freeze/mixed-cold-receipt.json) provide provenance.

The V23 mixed owned-aggregate staging path retains one bounded exception-granularity limitation inherited from prior aggregate staging: an allocation failure inside an aggregate stage can retire that subset, rather than only the first failing original owner. Success paths, normal terminal failure ordering, projected original-owner behavior, and focused parity checks passed. No further optimization run is scheduled before user review.

The exact-source focused gate passed 7/7 checks: runtime, LLVM, elaboration, application core simulation, connected remap, translation-unit structure, and selftest. Application cases cover VHDL `bit_vector` and `std_logic_vector` widths 9/65/129; runtime and LLVM mixed-sink cases cover wide Verilog 65/129-bit behavior. The CTest source-package-manifest check stops on the protected, pre-existing `.aws` top-level entry; direct manifest and CMake registrations for the new files were checked without modifying that directory.
