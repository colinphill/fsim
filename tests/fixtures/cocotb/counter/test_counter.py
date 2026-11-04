# SPDX-License-Identifier: Apache-2.0
"""fsim cocotb smoke test: drive a VHDL counter through VHPI."""

import cocotb
from cocotb.clock import Clock
from cocotb.handle import HierarchyObject
from cocotb.triggers import ClockCycles, FallingEdge, Timer


@cocotb.test()
async def counter_counts_when_enabled(dut):
    width = len(dut.count)
    # The elaborated generics: WIDTH=6, ENABLE_HIGH=false.
    assert not dut.ENABLE_HIGH.value
    on, off = 0, 1
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.rst.value = 1
    dut.enable.value = off
    await ClockCycles(dut.clk, 2)
    await FallingEdge(dut.clk)
    assert dut.count.value == 0
    dut.rst.value = 0
    dut.enable.value = on
    await ClockCycles(dut.clk, 5)
    await FallingEdge(dut.clk)
    assert dut.count.value == 5
    dut.enable.value = off
    await ClockCycles(dut.clk, 3)
    await FallingEdge(dut.clk)
    assert dut.count.value == 5
    assert width == int(dut.WIDTH.value)


@cocotb.test()
async def generate_instances_by_name(dut):
    # Each tap's CODE is to_unsigned(i, 2), and it outputs CODE's right bit.
    await Timer(1, unit="ns")
    for index in (0, 1):
        tap = dut.gen_tap[index].inst_tap
        assert isinstance(tap, HierarchyObject)
        assert tap.CODE.value == index
    assert dut.taps.value == "10"
