import sys
import unittest
from types import ModuleType
from unittest.mock import Mock, patch

from mcp.server.fastmcp import FastMCP
from tools.material_tools import register_material_tools


class MaterialInstanceParametersTests(unittest.TestCase):
    def setUp(self):
        server = FastMCP("material-instance-test")
        register_material_tools(server)
        self.tool = server._tool_manager.get_tool("set_material_instance_parameters")
        self.connection = Mock()
        module = ModuleType("unreal_mcp_server")
        module.get_unreal_connection = Mock(return_value=self.connection)
        self.patch = patch.dict(sys.modules, unreal_mcp_server=module)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.path = "/Game/__Dev/MI_Test"
        self.parent = "/Game/__Dev/M_Parent"
        self.connection.send_command.return_value = {
            "status": "success", "result": {"success": True, "saved": True, "parent": self.parent}}

    def test_optional_schema_and_old_call(self):
        self.assertEqual(self.tool.parameters["required"], ["asset_path"])
        self.assertEqual(self.tool.parameters["properties"]["parent_material_path"]["type"], "string")
        self.tool.fn(None, self.path, {"Scale": 2.0})
        self.connection.send_command.assert_called_once_with("set_material_instance_parameters", {
            "asset_path": self.path, "scalar_params": {"Scale": 2.0}})

    def test_parent_only_and_combined(self):
        result = self.tool.fn(None, self.path, parent_material_path=self.parent)
        self.assertTrue(result["saved"])
        self.connection.send_command.assert_called_with("set_material_instance_parameters", {
            "asset_path": self.path, "parent_material_path": self.parent})
        self.tool.fn(None, self.path, {"Scale": 3.0}, {"Tint": {"r": 1, "g": 0, "b": 0}},
                     {"MainTex": "/Game/__Dev/T_Test"}, self.parent)
        self.assertEqual(self.connection.send_command.call_args.args[1]["parent_material_path"], self.parent)
        self.assertEqual(self.connection.send_command.call_args.args[1]["scalar_params"], {"Scale": 3.0})

    def test_empty_parent_preserves_old_behavior(self):
        self.tool.fn(None, self.path, parent_material_path="")
        self.connection.send_command.assert_called_once_with("set_material_instance_parameters", {"asset_path": self.path})

    def test_invalid_parent_never_sends(self):
        for path in ("M_Parent", "/Game/Folder/", "C:\\M.uasset", "/Game//M", "/Game/M:Sub", "/Game/../M", " /Game/M"):
            self.assertFalse(self.tool.fn(None, self.path, parent_material_path=path)["success"])
        self.connection.send_command.assert_not_called()

    def test_failure_retains_actual_parent_and_save_state(self):
        self.connection.send_command.return_value = {
            "status": "error", "error": "Save failed", "result": {
                "success": False, "saved": False, "modified": True, "previous_parent": "/Game/Old",
                "parent": self.parent, "parent_changed": True}}
        result = self.tool.fn(None, self.path, parent_material_path=self.parent)
        self.assertFalse(result["success"])
        self.assertFalse(result["saved"])
        self.assertTrue(result["modified"])
        self.assertEqual(result["parent"], self.parent)
        self.assertEqual(result["message"], "Save failed")


if __name__ == "__main__":
    unittest.main()