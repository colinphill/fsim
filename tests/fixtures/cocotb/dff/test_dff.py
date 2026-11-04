# SPDX-License-Identifier: Apache-2.0
"""fsim cocotb smoke test: drive a Verilog flip-flop through VPI."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, ReadOnly, RisingEdge


@cocotb.test()
async def dff_follows_d(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    dut.rst.value = 1
    dut.d.value = 0
    await ClockCycles(dut.clk, 2)
    assert dut.q.value == 0
    dut.rst.value = 0
    for value in (3, 0x5A, 0xFF, 1):
        await FallingEdge(dut.clk)
        dut.d.value = value
        await RisingEdge(dut.clk)
        await ReadOnly()
        assert dut.q.value == value, f"q={dut.q.value} after d={value}"


@cocotb.test()
async def dff_simulator_identity(dut):
    assert cocotb.SIM_NAME.lower().startswith("fsim")
    assert dut._path == "dff"
