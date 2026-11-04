# SPDX-License-Identifier: Apache-2.0
"""cocotb runner for fsim (cocotb 2.1.x and 2.2).

fsim's workspace (the directory that holds .fsim) is the build directory,
where the runner runs fsim. The design is compiled by ``build()`` and
elaborated and simulated by ``test()``, so parameters and generics given to
``test()`` apply.
"""

from __future__ import annotations

import shutil
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any, ClassVar

import cocotb_tools.config
from cocotb_tools.runner import VHDL, Runner, Verilog

_Command = list[str]

# fsim is driven through the GPI implementations that follow the LRMs
# without simulator quirks: DSim's for VPI and NVC's for VHPI.
_IMPLEMENTATIONS: dict[str, str] = {"vpi": "dsim", "vhpi": "nvc"}
_INTERFACES: dict[str, str] = {"verilog": "vpi", "vhdl": "vhpi"}


def _load_option(interface: str) -> _Command:
    library = cocotb_tools.config.lib_entry(interface, _IMPLEMENTATIONS[interface])
    return [f"--{interface}", library]


class Fsim(Runner):
    """Implementation of :class:`~cocotb_tools.runner.Runner` for fsim.

    * ``timescale`` sets the time unit and precision of Verilog files that
      have no ``timescale`` directive, and its precision is the resolution
      of a design with VHDL units.
    * ``parameters`` set Verilog parameters and VHDL generics of the
      toplevel at elaboration.
    """

    supported_gpi_interfaces: ClassVar[dict[str, list[str]]] = {
        "verilog": ["vpi"],
        "vhdl": ["vhpi"],
    }

    def _simulator_in_path(self) -> None:
        if shutil.which("fsim") is None:
            raise SystemExit("ERROR: fsim executable not found!")

    def _set_env_test(self) -> None:
        super()._set_env_test()
        languages = {self.hdl_toplevel_lang, *self._other_languages()}
        interfaces = [_INTERFACES[self.hdl_toplevel_lang]] + [
            _INTERFACES[language]
            for language in sorted(languages - {self.hdl_toplevel_lang})
        ]
        if hasattr(cocotb_tools.config, "gpi_impl"):
            # cocotb 2.2: the bootstrap library loads every implementation.
            self.env["GPI_IMPL"] = ",".join(
                cocotb_tools.config.gpi_impl(_IMPLEMENTATIONS[interface], interface)
                for interface in interfaces
            )
        elif len(interfaces) > 1:
            # cocotb 2.1: the other language's implementation is an extra.
            self.env["GPI_EXTRA"] = ",".join(
                f"{cocotb_tools.config.lib_name_path(interface, _IMPLEMENTATIONS[interface])}"
                f":cocotb{interface}_entry_point"
                for interface in interfaces[1:]
            )
        # Zero-delay inertial writes follow the LRMs (see Makefile.fsim).
        if "COCOTB_TRUST_INERTIAL_WRITES" not in self.env:
            self.env["COCOTB_TRUST_INERTIAL_WRITES"] = "1"

    def _other_languages(self) -> set[str]:
        languages: set[str] = set()
        for source in self._sources + self._vhdl_sources + self._verilog_sources:
            if source.tag is VHDL:
                languages.add("vhdl")
            elif source.tag is Verilog:
                languages.add("verilog")
        return languages

    def _get_include_options(self, includes: Sequence[Path | str]) -> _Command:
        return [option for include in includes for option in ("-I", str(include))]

    def _get_define_options(self, defines: Mapping[str, object]) -> _Command:
        return [
            option
            for name, value in defines.items()
            for option in ("-D", f"{name}={value}")
        ]

    def _get_parameter_options(self, parameters: Mapping[str, object]) -> _Command:
        return [
            option
            for name, value in parameters.items()
            for option in ("--generic", f"{name}={value}")
        ]

    def _waves_file(self) -> str | None:
        assert self.build_dir is not None
        return str(Path(self.build_dir) / f"{self.sim_hdl_toplevel}.fst")

    def _build_command(self) -> list[_Command]:
        sources = self._sources + self._vhdl_sources + self._verilog_sources
        for source in sources:
            if source.tag not in (VHDL, Verilog):
                raise ValueError(
                    f"{type(self).__qualname__} only supports VHDL and Verilog. "
                    f"{str(source.value)!r} cannot be compiled."
                )
        # One source language per fsim compile, inferred from the extension:
        # VHDL, then Verilog (.v), then SystemVerilog.
        def language(source: Any) -> str:
            if source.tag == VHDL:
                return "vhdl"
            return "verilog" if Path(source.value).suffix.lower() == ".v" else "sv"

        commands: list[_Command] = []
        for kind in ("vhdl", "verilog", "sv"):
            files = [
                str(Path(source.value).resolve())
                for source in sources
                if language(source) == kind
            ]
            if not files:
                continue
            command = ["fsim", "compile", "--library", self.hdl_library]
            if kind != "vhdl":
                if self.timescale is not None:
                    command += ["--timescale", "/".join(self.timescale)]
                command += self._get_include_options(self.includes)
                command += self._get_define_options(self.defines)
            command += [str(arg.value) for arg in self._build_args]
            commands.append(command + files)
        return commands

    def _test_command(self) -> list[_Command]:
        # The libraries are in the workspace build() ran in.
        if Path(self.test_dir).resolve() != Path(self.build_dir).resolve():
            raise ValueError(
                f"{type(self).__qualname__} runs tests in the build directory, "
                "its workspace; test_dir must be build_dir"
            )
        top = f"{self.hdl_toplevel_library}.{self.sim_hdl_toplevel}"
        if self.hdl_toplevel_lang == "vhdl":
            # Apart from a Verilog unit of the same name.
            top = f"vhdl:{top}"
        # A design with VHDL units otherwise counts time in femtoseconds.
        resolution: _Command = []
        if self.timescale is not None and any(
            source.tag == VHDL for source in self._sources + self._vhdl_sources
        ):
            resolution = ["--time-resolution", self.timescale[1]]
        elaborate = [
            "fsim",
            "elaborate",
            *resolution,
            *self.elab_args,
            *self._get_parameter_options(self.parameters),
            top,
        ]
        simulate = [
            *self._get_sim_cmd_prefix(),
            "fsim",
            "simulate",
            *_load_option(_INTERFACES[self.hdl_toplevel_lang]),
            # cocotb's ReadOnly registers VHPI timers at the end of a time
            # step, which IEEE 1076-2019 21.3.6.8 forbids; fsim accepts it
            # as an extension (docs/cocotb.md).
            "--vhpi-end-of-step-timers",
            *self.test_args,
            *self.plusargs,
        ]
        if self.waves:
            simulate += ["--trace", str(self._waves_file()), "--trace-format", "fst"]
        simulate += self._get_sim_cmd_suffix()
        return [elaborate, simulate]
