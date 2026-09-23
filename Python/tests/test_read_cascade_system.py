import json
import os
import subprocess
import sys
import unittest
from types import ModuleType
from unittest.mock import Mock, patch

from mcp.server.fastmcp import FastMCP
from tools.project_tools import register_project_tools


class ReadCascadeSystemTests(unittest.TestCase):
    def setUp(self):
        server = FastMCP("cascade-test")
        register_project_tools(server)
        self.tool = server._tool_manager.get_tool("read_cascade_system")
        self.connection = Mock()
        self.module = ModuleType("unreal_mcp_server")
        self.module.get_unreal_connection = Mock(return_value=self.connection)
        self.module.spill_if_oversized = Mock(side_effect=lambda result, *args, **kwargs: result)
        self.patch = patch.dict(sys.modules, unreal_mcp_server=self.module)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.path = "/Game/FX/P_Test"

    def test_schema_and_selection_forwarding(self):
        self.assertEqual(self.tool.parameters["required"], ["asset_path"])
        result = {"success": True, "complete": True, "emitters": []}
        self.connection.send_command.return_value = {"status": "success", "result": result}
        self.assertEqual(self.tool.fn(None, self.path, 3, 0), result)
        self.connection.send_command.assert_called_once_with("read_cascade_system", {
            "asset_path": self.path, "emitter_index": 3, "lod_index": 0})
        self.module.spill_if_oversized.assert_called_once()

    def test_invalid_inputs_do_not_connect(self):
        for path in ("", "Game/FX/P_Test", "/Game/FX/", "C:\\FX\\P_Test.uasset",
                     "/Game/../P_Test", "/Game//P_Test", "/Game/P_Test:Subobject"):
            with self.subTest(path=path):
                self.assertFalse(self.tool.fn(None, path)["success"])
        for index in (-2, 1.5, True, "0"):
            self.assertFalse(self.tool.fn(None, self.path, index)["success"])
            self.assertFalse(self.tool.fn(None, self.path, -1, index)["success"])
        self.module.get_unreal_connection.assert_not_called()

    def test_errors(self):
        for response in (None, {"status": "error", "error": "Not a Cascade ParticleSystem"}):
            self.connection.send_command.return_value = response
            self.assertFalse(self.tool.fn(None, self.path)["success"])
        self.module.get_unreal_connection.return_value = None
        self.assertFalse(self.tool.fn(None, self.path)["success"])

    def test_registration_filters(self):
        environment = {key: value for key, value in os.environ.items()
                       if key != "UNREAL_MCP_READ_ONLY" and not (key.startswith("MCP_") and key.endswith("_ENABLED"))}
        for overrides, expected in (({}, True), ({"UNREAL_MCP_READ_ONLY": "1"}, True),
                                    ({"MCP_CASCADE_ENABLED": "0"}, False),
                                    ({"MCP_PROJECT_ENABLED": "0"}, True)):
            with self.subTest(overrides=overrides):
                result = subprocess.run(
                    [sys.executable, "-c", "import json, unreal_mcp_server as server; "
                     "print(json.dumps(sorted(server.mcp._tool_manager._tools)))"],
                    env={**environment, **overrides}, text=True, capture_output=True, check=True)
                self.assertEqual("read_cascade_system" in json.loads(result.stdout), expected)


if __name__ == "__main__":
    unittest.main()