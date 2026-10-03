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
        self.tools = server._tool_manager
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
            "status": "success", "result": {"success": True, "saved": False, "parent": self.parent,
                                             "supports_preview_updates": True, "parameter_contract": 2}}

    def test_optional_schema_and_old_call(self):
        self.assertEqual(self.tool.parameters["required"], ["asset_path"])
        self.assertEqual(self.tool.parameters["properties"]["parent_material_path"]["type"], "string")
        self.tool.fn(None, self.path, {"Scale": 2.0})
        self.connection.send_command.assert_called_with("set_material_instance_parameters", {
            "asset_path": self.path, "scalar_params": {"Scale": 2.0}, "save": False})

    def test_parent_only_and_combined(self):
        result = self.tool.fn(None, self.path, parent_material_path=self.parent)
        self.assertFalse(result["saved"])
        self.connection.send_command.assert_called_with("set_material_instance_parameters", {
            "asset_path": self.path, "parent_material_path": self.parent, "save": False})
        self.tool.fn(None, self.path, {"Scale": 3.0}, {"Tint": {"r": 1, "g": 0, "b": 0}},
                     {"MainTex": "/Game/__Dev/T_Test"}, self.parent)
        self.assertEqual(self.connection.send_command.call_args.args[1]["parent_material_path"], self.parent)
        self.assertEqual(self.connection.send_command.call_args.args[1]["scalar_params"], {"Scale": 3.0})

    def test_empty_parent_preserves_old_behavior(self):
        self.tool.fn(None, self.path, parent_material_path="")
        self.connection.send_command.assert_called_with("set_material_instance_parameters", {"asset_path": self.path, "save": False})

    def test_old_native_plugin_cannot_silently_save_preview(self):
        self.connection.send_command.return_value = {"status": "success", "result": {"parent": self.parent}}
        self.assertFalse(self.tool.fn(None, self.path)["success"])
        self.connection.send_command.assert_called_once_with("get_material_instance_parameters", {"path": self.path})

    def test_invalid_parent_never_sends(self):
        for path in ("M_Parent", "/Game/Folder/", "C:\\M.uasset", "/Game//M", "/Game/M:Sub", "/Game/../M", " /Game/M"):
            self.assertFalse(self.tool.fn(None, self.path, parent_material_path=path)["success"])
        self.connection.send_command.assert_not_called()

    def test_failure_retains_actual_parent_and_save_state(self):
        failure = {
            "status": "error", "error": "Save failed", "result": {
                "success": False, "saved": False, "modified": True, "previous_parent": "/Game/Old",
                "parent": self.parent, "parent_changed": True}}
        self.connection.send_command.side_effect = [self.connection.send_command.return_value, failure]
        result = self.tool.fn(None, self.path, parent_material_path=self.parent, save=True)
        self.assertFalse(result["success"])
        self.assertFalse(result["saved"])
        self.assertTrue(result["modified"])
        self.assertEqual(result["parent"], self.parent)
        self.assertEqual(result["message"], "Save failed")

    def test_delete_expression_contract_and_receipt(self):
        tool = self.tools.get_tool("delete_material_expression")
        self.assertEqual(tool.parameters["required"], ["asset_path", "node_index", "expected_node_path"])
        tool.fn(None, "/Game/M_Test", 2, "/Game/M_Test.M_Test:MaterialExpressionTextureSample_0")
        self.connection.send_command.assert_called_once_with("delete_material_expression", {
            "asset_path": "/Game/M_Test", "node_index": 2,
            "expected_node_path": "/Game/M_Test.M_Test:MaterialExpressionTextureSample_0", "dry_run": True})
        self.connection.send_command.return_value = {"status": "error", "error": "Readback failed", "result": {
            "modified": True, "saved": False, "indices_invalidated": True}}
        result = tool.fn(None, "/Game/M_Test", 2, "exact-node", False)
        self.assertFalse(result["success"])
        self.assertTrue(result["modified"])
        self.assertFalse(result["saved"])
        self.assertTrue(result["indices_invalidated"])

    def test_niagara_copy_failure_keeps_private_reference_receipt(self):
        from tools.niagara_tools import register_niagara_tools
        server = FastMCP("niagara-copy-test")
        register_niagara_tools(server)
        tool = server._tool_manager.get_tool("add_emitter_to_system")
        self.connection.send_command.return_value = {"status": "error", "error": "Private source reference", "result": {
            "modified": True, "saved": False, "stage": "reference_preflight", "private_references": [{"reference": "source-private"}]}}
        result = tool.fn(None, "/Game/Target", "Refraction", source_asset_full_path="/Game/Source")
        self.assertTrue(result["modified"])
        self.assertFalse(result["saved"])
        self.assertEqual(result["private_references"][0]["reference"], "source-private")
        self.connection.send_command.assert_called_once()
        self.connection.reset_mock()
        self.connection.send_command.side_effect = TimeoutError("timeout")
        result = tool.fn(None, "/Game/Target", "Refraction", source_asset_full_path="/Game/Source")
        self.assertEqual(result["stage"], "transport")
        self.assertNotIn("saved", result)
        self.assertNotIn("modified", result)
        self.connection.send_command.assert_called_once()

    def test_clear_override_forwarding(self):
        self.tool.fn(None, self.path, clear_scalar_params=["Roughness"], clear_vector_params=["Tint"], clear_texture_params=["MainTex"])
        self.connection.send_command.assert_called_with("set_material_instance_parameters", {
            "asset_path": self.path, "save": False, "clear_scalar_params": ["Roughness"],
            "clear_vector_params": ["Tint"], "clear_texture_params": ["MainTex"]})


if __name__ == "__main__":
    unittest.main()