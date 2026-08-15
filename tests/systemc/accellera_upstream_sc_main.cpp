// SPDX-License-Identifier: Apache-2.0

#if defined(__GNUC__) || defined(__clang__)
#define FSIM_SYSTEMC_UPSTREAM_EXPORT __attribute__((visibility("default")))
#else
#define FSIM_SYSTEMC_UPSTREAM_EXPORT
#endif

extern "C" int fsim_systemc_upstream_entry(int argc, char* argv[]);

extern "C" FSIM_SYSTEMC_UPSTREAM_EXPORT int sc_main(
    const int argc, char* argv[])
{
    return fsim_systemc_upstream_entry(argc, argv);
}
