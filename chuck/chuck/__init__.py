"""chuck — Python controller for tester, the Qt/QML automation library."""

from .client import Client, ChuckError
from .commands import ChuckClient
from .replayer import ReplayReport, StepResult
from .screenshots import BaselineStore
from .visual_diff import CompareResult

__all__ = [
    "Client", "ChuckError", "ChuckClient",
    "BaselineStore", "CompareResult",
    "ReplayReport", "StepResult",
]


def get_mcp_server():
    """Lazy import of the MCP server instance (avoids hard dep on mcp at import time)."""
    from .mcp_server import mcp
    return mcp
