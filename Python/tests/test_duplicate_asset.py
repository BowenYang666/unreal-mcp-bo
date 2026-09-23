import json
import os
import subprocess
import sys
import unittest
from types import ModuleType
from unittest.mock import Mock, patch

from mcp.server.fastmcp import FastMCP
from tools.editor_tools import register_editor_tools


class DuplicateAssetTests(unittest.TestCase):
    def setUp(self):
        server = FastMCP("duplicate-asset-test")
        register_editor_tools(server)
        self.tool = server._tool_manager.get_tool("duplicate_asset")
        self.connection = Mock()
        module = ModuleType("unreal_mcp_server")
        module.get_unreal_connection = Mock(return_value=self.connection)
        self.patch = patch.dict(sys.modules, unreal_mcp_server=module)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.source = "/Game/MarketPlugins/Pack/T_Spark_A"
        self.destination = "/Game/ThirdParty/Pack/T_Spark_A"

    def call(self, source=None, destination=None):
        return self.tool.fn(None, self.source if source is None else source,
                            self.destination if destination is None else destination)

    def test_schema_and_native_command(self):
        self.assertEqual(self.tool.parameters["required"],
                         ["source_asset_path", "destination_asset_path"])
        result = {"success": True, "saved": True, "source_path": self.source,
                  "destination_path": self.destination, "asset_class": "Texture2D"}
        self.connection.send_command.return_value = {"status": "success", "result": result}
        self.assertEqual(self.call(), result)
        self.connection.send_command.assert_called_once_with("duplicate_asset", {
            "source_asset_path": self.source, "destination_asset_path": self.destination})

    def test_invalid_paths_do_not_contact_editor(self):
        for path in ("", "/Game/", "/Game//T", "/Game/A/../T", "/Game/A.T",
                     "/Game/A/", "C:\\Assets\\T.uasset", "/Game/Bad Name", "/Game/T?", "/Engine/T"):
            with self.subTest(path=path):
                self.assertFalse(self.call(source=path)["success"])
                self.assertFalse(self.call(destination=path)["success"])
        self.assertFalse(self.call(destination=self.source)["success"])
        self.assertFalse(self.call(destination=self.source.lower().replace("/game/", "/Game/"))["success"])
        self.connection.send_command.assert_not_called()

    def test_failures_are_not_success(self):
        for response in (None, {"status": "error", "error": "Destination asset already exists"},
                         {"status": "success", "result": {}},
                         {"result": {"success": False, "saved": False,
                                     "destination_path": self.destination, "message": "Save failed"}}):
            with self.subTest(response=response):
                self.connection.send_command.return_value = response
                self.assertFalse(self.call()["success"])
        self.connection.send_command.side_effect = TimeoutError("Timed out")
        self.assertFalse(self.call()["success"])

    def test_save_failure_keeps_partial_result(self):
        self.connection.send_command.return_value = {
            "status": "error", "error": "Copy created but saving failed",
            "result": {"success": False, "saved": False, "duplicate_created": True,
                       "destination_path": self.destination, "asset_class": "Texture2D"}}
        result = self.call()
        self.assertFalse(result["success"])
        self.assertFalse(result["saved"])
        self.assertTrue(result["duplicate_created"])
        self.assertEqual(result["destination_path"], self.destination)
        self.assertTrue(result["message"])

    def test_registration_filters(self):
        environment = {key: value for key, value in os.environ.items()
                       if key != "UNREAL_MCP_READ_ONLY" and not (key.startswith("MCP_") and key.endswith("_ENABLED"))}
        for overrides, expected_count, exposed in (
            ({}, 109, True),
            ({"UNREAL_MCP_READ_ONLY": "1"}, 29, False),
            ({"MCP_ASSET_ENABLED": "0"}, 106, False),
            ({"MCP_EDITOR_ENABLED": "0"}, 93, True),
        ):
            with self.subTest(overrides=overrides):
                process = subprocess.run(
                    [sys.executable, "-c", "import json, unreal_mcp_server as server; "
                     "print(json.dumps(sorted(server.mcp._tool_manager._tools)))"],
                    env={**environment, **overrides}, capture_output=True, text=True, check=True)
                tools = json.loads(process.stdout)
                self.assertEqual(len(tools), expected_count)
                self.assertEqual("duplicate_asset" in tools, exposed)


if __name__ == "__main__":
    unittest.main()