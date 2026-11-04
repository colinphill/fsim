# SPDX-License-Identifier: Apache-2.0
"""fsim cocotb smoke test: a SystemVerilog top over a VHDL child."""

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer


@cocotb.test()
async def mixed_reaches_both_languages(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    previous = None
    for a, b in ((1, 2), (100, 27), (250, 10)):
        dut.a.value = a
        dut.b.value = b
        await Timer(1, unit="ns")
        expected = (a + b) & 0xFF
        assert dut.sum.value == expected
        # The VHDL child's internal signal, through the other interface.
        assert dut.u_adder.total.value == expected
        await RisingEdge(dut.clk)
        await Timer(1, unit="ns")
        assert dut.registered.value == expected
        # The VHDL flop took the SystemVerilog flop's value from before the
        # edge, which the cocotb clock wrote through VPI.
        if previous is not None:
            assert dut.staged.value == previous
        previous = expected
