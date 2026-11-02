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

    def test_niagara_save_and_dispatch_failures_keep_receipts(self):
        from tools.niagara_tools import register_niagara_tools
        server = FastMCP("niagara-save-test")
        register_niagara_tools(server)
        tools = server._tool_manager
        calls = [
            ("enable_module_input", dict(asset_full_path="/Game/Test", emitter_name="Layer",
                                        module_name="InitializeParticle", input_name="Lifetime", initial_value=1.2)),
            ("set_module_static_switch", dict(asset_full_path="/Game/Test", emitter_name="Layer",
                                             module_name="InitializeParticle", switch_name="Color Mode", value="Direct Set")),
        ]
        for name, arguments in calls:
            for modified, stage in ((True, "save"), (False, "dispatch")):
                with self.subTest(tool=name, stage=stage):
                    self.connection.reset_mock()
                    self.connection.send_command.return_value = {"status": "error", "error": "Not saved", "result": {
                        "modified": modified, "saved": False, "stage": stage,
                        "execution_state": "complete" if modified else "not_started", "package_dirty": modified}}
                    result = tools.get_tool(name).fn(None, **arguments)
                    self.assertFalse(result["success"])
                    self.assertEqual(result["modified"], modified)
                    self.assertFalse(result["saved"])
                    self.assertEqual(result["stage"], stage)
                    self.assertEqual(result["package_dirty"], modified)
                    self.connection.send_command.assert_called_once()
            for error in (TimeoutError("timeout"), ConnectionResetError("reset")):
                with self.subTest(tool=name, error=type(error).__name__):
                    self.connection.reset_mock()
                    self.connection.send_command.side_effect = error
                    result = tools.get_tool(name).fn(None, **arguments)
                    self.assertFalse(result["success"])
                    self.assertEqual(result["stage"], "transport")
                    self.assertEqual(result["execution_state"], "unknown")
                    self.assertNotIn("saved", result)
                    self.assertNotIn("modified", result)
                    self.connection.send_command.assert_called_once()
            self.connection.send_command.side_effect = None

    def test_niagara_scalability_read_contract_and_spill(self):
        from tools.niagara_tools import register_niagara_tools
        server = FastMCP("niagara-scalability")
        register_niagara_tools(server)
        module = sys.modules["unreal_mcp_server"]
        module.spill_if_oversized = Mock(side_effect=lambda result, *args, **kwargs: result)
        tool = server._tool_manager.get_tool("read_niagara_effect_type")
        self.assertEqual(tool.parameters["required"], ["asset_path"])
        self.connection.send_command.return_value = {"status": "success", "result": {
            "contract_version": 1, "complete": False, "unsupported": [{"path": "test"}], "scope": "saved"}}
        result = tool.fn(None, "/Game/Effects/EffectTypes/Test")
        self.assertFalse(result["complete"])
        self.connection.send_command.assert_called_once_with("read_niagara_effect_type", {"asset_path": "/Game/Effects/EffectTypes/Test"})
        module.spill_if_oversized.return_value = {"success": True, "overflow": True, "file_path": "full.json"}
        module.spill_if_oversized.side_effect = None
        result = tool.fn(None, "/Game/Test")
        self.assertFalse(result["complete"])
        self.assertEqual(result["contract_version"], 1)
        self.assertTrue(result["overflow"])
        module.spill_if_oversized.side_effect = lambda result, *args, **kwargs: result
        self.connection.send_command.return_value = {"status": "success", "result": {"name": "Legacy", "emitters": []}}
        result = server._tool_manager.get_tool("read_niagara_system").fn(None, "/Game/Legacy")
        self.assertEqual(result["name"], "Legacy")
        self.assertFalse(result["scalability_complete"])
        self.assertIn("native_capability_missing", result["scalability_status"])

    def test_clear_override_forwarding(self):
        self.tool.fn(None, self.path, clear_scalar_params=["Roughness"], clear_vector_params=["Tint"], clear_texture_params=["MainTex"])
        self.connection.send_command.assert_called_with("set_material_instance_parameters", {
            "asset_path": self.path, "save": False, "clear_scalar_params": ["Roughness"],
            "clear_vector_params": ["Tint"], "clear_texture_params": ["MainTex"]})


    def test_niagara_authoring_schema_defaults_and_receipts(self):
        from tools.niagara_tools import register_niagara_tools
        server = FastMCP("niagara-authoring-test")
        register_niagara_tools(server)
        tool = server._tool_manager.get_tool("set_niagara_scalability")
        self.assertEqual(tool.parameters["required"], ["asset_path", "expected_baseline"])
        self.assertEqual(tool.parameters["properties"]["changes"]["type"], "array")
        self.connection.send_command.return_value = {"status": "success", "result": {
            "authoring_contract": 1, "success": True, "modified": False, "saved": False}}
        tool.fn(None, "/Game/NS_Test", "baseline", [{"field": "Platforms", "value": {"QualityLevelMask": -29}}])
        arguments = self.connection.send_command.call_args.args[1]
        self.assertTrue(arguments["dry_run"])
        self.assertFalse(arguments["save"])
        self.connection.send_command.return_value = {"status": "error", "error": "Save failed", "result": {
            "modified": True, "saved": False, "stage": "save", "after": {"package_dirty": True}}}
        result = tool.fn(None, "/Game/NS_Test", "baseline", dry_run=False)
        self.assertTrue(result["modified"])
        self.assertFalse(result["saved"])
        self.assertTrue(result["after"]["package_dirty"])

    def test_niagara_parameter_authoring_contracts(self):
        from tools.niagara_tools import register_niagara_tools
        server = FastMCP("niagara-parameter-authoring-test")
        register_niagara_tools(server)
        self.connection.send_command.return_value = {"status": "success", "result": {
            "authoring_contract": 1, "success": True, "modified": False, "saved": False}}
        tool = server._tool_manager.get_tool("set_niagara_user_parameters")
        parameters = [{"name": "User.Brightness", "type": "float", "value": 1.0}]
        tool.fn(None, "/Game/NS_Test", "baseline", parameters)
        self.assertEqual(self.connection.send_command.call_args.args[1]["parameters"], parameters)
        self.assertFalse(self.connection.send_command.call_args.args[1]["save"])
        tool = server._tool_manager.get_tool("bind_niagara_module_input")
        tool.fn(None, "/Game/NS_Test", "handle", "emitter_update", "node-guid", "baseline", "Module.Rate", "User.Rate")
        arguments = self.connection.send_command.call_args.args[1]
        self.assertEqual(arguments["module_node_guid"], "node-guid")
        self.assertEqual(arguments["target"], "module")
        self.assertTrue(arguments["dry_run"])
        tool = server._tool_manager.get_tool("set_niagara_material_bindings")
        tool.fn(None, "/Game/NS_Test", "handle", 1, "baseline", [{"material_parameter": "Tint", "user_parameter": "User.Tint"}])
        self.assertEqual(self.connection.send_command.call_args.args[1]["renderer_index"], 1)
        self.assertEqual(tool.parameters["properties"]["bindings"]["type"], "array")

    def test_niagara_explicit_link_extension_contracts(self):
        from tools.niagara_tools import register_niagara_tools
        server = FastMCP("niagara-link-extensions")
        register_niagara_tools(server)
        self.connection.send_command.return_value = {"status": "success", "result": {
            "authoring_contract": 1, "success": True, "modified": False, "saved": False}}
        tool = server._tool_manager.get_tool("redirect_niagara_user_link")
        tool.fn(None, "/Game/NS_Test", "handle", "spawn", "node", "baseline", "Module.Color", "User.Old", "User.New")
        args = self.connection.send_command.call_args.args[1]
        self.assertEqual(args["expected_linked_parameter"], "User.Old")
        self.assertTrue(args["dry_run"])
        self.assertFalse(args["save"])
        tool = server._tool_manager.get_tool("bind_niagara_dynamic_input")
        tool.fn(None, "/Game/NS_Test", "handle", "update", "parent", "Module.Scale", "function", "baseline", "Module.Value", "User.Size")
        self.assertEqual(self.connection.send_command.call_args.args[1]["target"], "dynamic_input")
        tool = server._tool_manager.get_tool("multiply_niagara_float_input")
        tool.fn(None, "/Game/NS_Test", "handle", "update", "parent", "baseline", "Module.Value", "User.Size", "source-node", "source-pin", "version")
        args = self.connection.send_command.call_args.args[1]
        self.assertEqual(args["expected_source_pin_id"], "source-pin")
        self.assertEqual(args["multiplier_version_guid"], "version")
        self.connection.reset_mock()
        self.connection.send_command.side_effect = TimeoutError("timeout")
        result = tool.fn(None, "/Game/NS_Test", "handle", "update", "parent", "baseline", "Module.Value", "User.Size", "source-node", "source-pin", "version")
        self.assertEqual(result["execution_state"], "unknown")
        self.assertNotIn("saved", result)
        self.connection.send_command.assert_called_once()

    def test_niagara_authoring_never_retries_unknown_transport(self):
        from tools.niagara_tools import register_niagara_tools
        server = FastMCP("niagara-authoring-timeout-test")
        register_niagara_tools(server)
        for name, arguments in (("create_niagara_effect_type", ("/Game/NET_Test",)),
                                ("save_niagara_authoring", ("/Game/NS_Test", "baseline"))):
            self.connection.reset_mock()
            self.connection.send_command.side_effect = TimeoutError("timeout")
            result = server._tool_manager.get_tool(name).fn(None, *arguments)
            self.assertEqual(result["execution_state"], "unknown")
            self.assertNotIn("saved", result)
            self.connection.send_command.assert_called_once()


if __name__ == "__main__":
    unittest.main()