# SPDX-License-Identifier: Apache-2.0
TOPLEVEL_LANG = vhdl
VHDL_SOURCES = $(PWD)/counter.vhd
COCOTB_TOPLEVEL = counter
COCOTB_TEST_MODULES = test_counter
ELAB_ARGS = --generic WIDTH=6 --generic ENABLE_HIGH=false
include $(shell fsim cocotb makefile)
