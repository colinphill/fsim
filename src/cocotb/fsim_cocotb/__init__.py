# SPDX-License-Identifier: Apache-2.0
"""fsim support for cocotb.

Importing this package registers the ``fsim`` runner, so that
``cocotb_tools.runner.get_runner("fsim")`` returns :class:`Fsim`.
"""

from cocotb_tools import runner as _runner

from fsim_cocotb.runner import Fsim

_runner.SUPPORTED_RUNNERS["fsim"] = Fsim

__all__ = ["Fsim"]
