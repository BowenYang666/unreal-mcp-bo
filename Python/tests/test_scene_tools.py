import os
import sys
import unittest
from types import ModuleType
from unittest.mock import Mock, patch
from mcp.server.fastmcp import FastMCP
from mcp.server.fastmcp.exceptions import ToolError
from tools.scene_tools import register_scene_tools
import test_grouped_tools


class SceneToolsTests(unittest.TestCase):
    def setUp(self):
        server = FastMCP("scene-test")
        register_scene_tools(server)
        self.tools = server._tool_manager
        self.connection = Mock()
        module = ModuleType("unreal_mcp_server")
        module.get_unreal_connection = Mock(return_value=self.connection)
        context = patch.dict(sys.modules, unreal_mcp_server=module)
        context.start()
        self.addCleanup(context.stop)
        self.connection.send_command.return_value = {"status": "success", "result": {"success": True, "saved": False}}

    def test_preview_defaults_and_schema(self):
        tool = self.tools.get_tool("patch_scene_target")
        self.assertEqual(tool.parameters["required"], ["project_path", "level_path", "actor_path", "changes"])
        changes = [{"path": "Intensity", "value": 1200, "expected_value": 800}]
        tool.fn(None, "E:/Test/Test.uproject", "/Game/Test", "exact-path", changes, "LightComponent0")
        self.connection.send_command.assert_called_once_with("patch_scene_target", {
            "project_path": "E:/Test/Test.uproject", "level_path": "/Game/Test", "actor_path": "exact-path",
            "component_name": "LightComponent0", "changes": changes})
        self.tools.get_tool("save_scene_level").fn(None, "project", "map")
        self.assertFalse(self.connection.send_command.call_args.args[1]["confirm_all_changes_in_map"])
        self.assertNotIn("materials", self.tools.get_tool("get_scene_mesh").parameters["properties"])

    def test_invalid_json_timeout_and_partial_receipt(self):
        tool = self.tools.get_tool("patch_scene_target")
        self.assertFalse(tool.fn(None, "project", "map", "actor", [{"path": "Intensity", "value": float("nan")}])["success"])
        self.connection.send_command.assert_not_called()
        self.connection.send_command.return_value = {"status": "error", "error": "readback failed", "result": {
            "modified": True, "saved": False, "changes": [{"before": 1, "after": 2}]}}
        receipt = tool.fn(None, "project", "map", "actor", [{"path": "Intensity", "value": 2}])
        self.assertTrue(receipt["modified"])
        self.assertEqual(receipt["changes"][0]["after"], 2)
        self.connection.reset_mock()
        self.connection.send_command.side_effect = TimeoutError("timeout")
        receipt = self.tools.get_tool("capture_scene_viewport").fn(None, "project", "map", 0, "capture001")
        self.assertEqual(receipt["request_id"], "capture001")
        self.assertEqual(receipt["stage"], "transport")
        self.assertNotIn("saved", receipt)
        self.connection.send_command.assert_called_once()

    def test_filters_and_port(self):
        fixture = test_grouped_tools.GroupedServerTests()
        for mode in ("direct", "grouped"):
            for readonly in (False, True):
                for disabled in ((), ("scene",)):
                    server = fixture.load_server(mode, readonly, disabled)
                    for name in server._CATEGORY_TOOLS["scene"]:
                        exposed = not disabled and (not readonly or name in server._READ_ONLY_TOOLS)
                        if mode == "direct":
                            self.assertEqual(server.mcp._tool_manager.get_tool(name) is not None, exposed, name)
                        else:
                            self.assertIsNone(server.mcp._tool_manager.get_tool(name))
                            search = server.mcp._tool_manager.get_tool("scene_search")
                            if disabled:
                                self.assertIsNone(search)
                            elif exposed:
                                self.assertEqual(search.fn(tool=name)["tool"], name)
                            else:
                                with self.assertRaises(ToolError):
                                    search.fn(tool=name)
        with patch.dict(os.environ, UNREAL_MCP_PORT="13091"):
            self.assertEqual(fixture.load_server().UNREAL_PORT, 13091)
        for port in ("0", "65536", "bad"):
            with patch.dict(os.environ, UNREAL_MCP_PORT=port), self.assertRaises(ValueError):
                fixture.load_server()

    def test_import_defaults_and_request_receipt(self):
        operation = self.tools.get_tool("import_scene_asset")
        self.assertEqual(operation.parameters["required"], ["project_path", "request_id", "source_file", "destination_path"])
        operation.fn(None, "project", "import001", "E:/Exports/mesh.fbx", "/Game/__Dev/SceneTools/Import001")
        payload = self.connection.send_command.call_args.args[1]
        self.assertFalse(payload["save"])
        self.assertNotIn("srgb", payload)
        self.assertNotIn("compression", payload)
        self.connection.send_command.side_effect = TimeoutError("timeout")
        result = self.tools.get_tool("get_scene_import_status").fn(None, "project", "import001")
        self.assertEqual(result["request_id"], "import001")
        self.assertEqual(result["stage"], "transport")

    def test_folder_forwarding_defaults_and_schema(self):
        tool = self.tools.get_tool("set_scene_actor_folders")
        self.assertEqual(tool.parameters["required"], ["project_path", "level_path", "changes"])
        changes = [{"actor_path": "exact-path", "folder_path": "OpeningEnvironment/Props", "expected_folder_path": ""}]
        tool.fn(None, "project", "map", changes)
        self.connection.send_command.assert_called_once_with("set_scene_actor_folders", {
            "project_path": "project", "level_path": "map", "changes": changes, "dry_run": True, "managed_only": True})
        self.assertEqual(self.tools.get_tool("manage_scene_actor").parameters["properties"]["folder_path"]["type"], "string")
        self.connection.reset_mock()
        self.connection.send_command.side_effect = TimeoutError("timeout")
        result = tool.fn(None, "project", "map", changes, False, False)
        self.assertEqual(result["stage"], "transport")
        self.assertNotIn("modified", result)
        self.connection.send_command.assert_called_once()

    def test_folder_parameters_refuse_old_native_without_mutation(self):
        invocations = [
            lambda: self.tools.get_tool("manage_scene_actor").fn(None, "project", "map", "create", folder_path=""),
            lambda: self.tools.get_tool("manage_scene_actor").fn(None, "project", "map", "create", folder_path="OpeningEnvironment/Lighting"),
            lambda: self.tools.get_tool("apply_scene_manifest").fn(None, "project", "map", {"folder_root": "OpeningEnvironment", "objects": []}),
            lambda: self.tools.get_tool("apply_scene_manifest").fn(None, "project", "map", {"objects": [{"folder": "Props"}]}),
        ]
        for invocation in invocations:
            for native in ({"success": True}, {"success": True, "folder_contract": 2}, {"success": False, "stage": "transport"}):
                with self.subTest(native=native):
                    self.connection.reset_mock()
                    self.connection.send_command.return_value = {"status": "success", "result": native}
                    result = invocation()
                    self.assertFalse(result["success"])
                    self.assertFalse(result["modified"])
                    self.assertFalse(result["saved"])
                    self.assertEqual(result["error_code"], "native_folder_contract_required")
                    self.connection.send_command.assert_called_once_with("get_editor_context", {})

    def test_folder_capability_and_legacy_omission(self):
        create = self.tools.get_tool("manage_scene_actor")
        create.fn(None, "project", "map", "create")
        self.connection.send_command.assert_called_once()
        self.assertNotIn("folder_path", self.connection.send_command.call_args.args[1])
        self.connection.reset_mock()
        self.connection.send_command.return_value = {"status": "success", "result": {"success": True, "folder_contract": 1}}
        create.fn(None, "project", "map", "create", folder_path="OpeningEnvironment/Lighting")
        self.assertEqual(self.connection.send_command.call_count, 2)
        self.assertEqual(self.connection.send_command.call_args.args[1]["folder_path"], "OpeningEnvironment/Lighting")
        self.connection.reset_mock()
        manifest = {"folder_root": "OpeningEnvironment", "objects": [{"folder": "Props"}]}
        self.tools.get_tool("apply_scene_manifest").fn(None, "project", "map", manifest)
        self.assertEqual(self.connection.send_command.call_count, 2)
        self.connection.send_command.assert_called_with("apply_scene_manifest", {
            "project_path": "project", "level_path": "map", "manifest": manifest, "dry_run": True})

    def test_folder_readback_failure_keeps_partial_and_undo_receipt(self):
        self.connection.send_command.return_value = {"status": "error", "result": {
            "success": False, "stage": "folder_readback", "modified": True, "saved": False,
            "transaction_id": "folder-transaction", "items": [{"actor_path": "exact-path", "before": "", "requested": "Props", "after": ""}]}}
        result = self.tools.get_tool("set_scene_actor_folders").fn(None, "project", "map", [{"actor_path": "exact-path", "folder_path": "Props"}], False)
        self.assertTrue(result["modified"])
        self.assertFalse(result["saved"])
        self.assertEqual(result["transaction_id"], "folder-transaction")
        self.assertEqual(result["items"][0]["before"], "")

    def test_visibility_schema_defaults_and_expected_false(self):
        tool = self.tools.get_tool("set_scene_actor_visibility")
        self.assertEqual(tool.parameters["required"], ["project_path", "level_path", "actor_path", "hidden_in_editor"])
        self.assertEqual(tool.parameters["properties"]["expected_hidden_in_editor"]["type"], "boolean")
        tool.fn(None, "project", "map", "exact-path", True)
        self.connection.send_command.assert_called_once_with("set_scene_actor_visibility", {
            "project_path": "project", "level_path": "map", "actor_path": "exact-path", "hidden_in_editor": True, "dry_run": True})
        self.connection.reset_mock()
        tool.fn(None, "project", "map", "exact-path", True, False, False)
        self.connection.send_command.assert_called_once_with("set_scene_actor_visibility", {
            "project_path": "project", "level_path": "map", "actor_path": "exact-path", "hidden_in_editor": True,
            "dry_run": False, "expected_hidden_in_editor": False})

    def test_visibility_receipt_and_timeout_do_not_claim_save(self):
        payload = {"success": True, "modified": True, "saved": False, "session_only": True,
            "undo_supported": False, "package_dirty_before": False, "package_dirty": False,
            "before": {"temporary_hidden": False},
            "after": {"temporary_hidden": True, "hidden_in_game": False, "editor_hidden": True}}
        self.connection.send_command.return_value = {"status": "success", "result": payload}
        tool = self.tools.get_tool("set_scene_actor_visibility")
        self.assertEqual(tool.fn(None, "project", "map", "actor", True, False), payload)
        self.connection.reset_mock()
        self.connection.send_command.side_effect = TimeoutError("timeout")
        result = tool.fn(None, "project", "map", "actor", False, False)
        self.assertEqual(result["stage"], "transport")
        self.assertNotIn("modified", result)
        self.assertNotIn("saved", result)
        self.connection.send_command.assert_called_once()

    def test_manifest_and_undo_forwarding(self):
        manifest = {"version": 1, "namespace": "Test", "objects": []}
        self.tools.get_tool("apply_scene_manifest").fn(None, "project", "map", manifest)
        self.connection.send_command.assert_called_with("apply_scene_manifest", {
            "project_path": "project", "level_path": "map", "manifest": manifest, "dry_run": True})
        self.tools.get_tool("undo_scene_edit").fn(None, "project", "map", "transaction-id")
        self.connection.send_command.assert_called_with("undo_scene_edit", {
            "project_path": "project", "level_path": "map", "transaction_id": "transaction-id"})

    def test_import_admission_errors_are_preserved_without_retry(self):
        operation = self.tools.get_tool("import_scene_asset")
        for code in ("scene_task_busy", "receipt_cache_full"):
            with self.subTest(code=code):
                self.connection.reset_mock()
                self.connection.send_command.return_value = {"status": "error", "error": "Import refused", "result": {
                    "success": False, "modified": False, "saved": False, "stage": "preflight",
                    "error_code": code, "cached_receipts": 32, "cache_limit": 32}}
                result = operation.fn(None, "project", "import-capacity", "E:/Exports/mesh.fbx", "/Game/__Dev/SceneTools/NewImport")
                self.assertEqual(result["error_code"], code)
                self.assertEqual(result["request_id"], "import-capacity")
                self.assertEqual(result["cached_receipts"], 32)
                self.assertEqual(result["cache_limit"], 32)
                self.assertFalse(result["modified"])
                self.connection.send_command.assert_called_once()

    def test_import_persistence_warning_does_not_erase_asset_outcome(self):
        self.connection.send_command.return_value = {"status": "success", "result": {
            "success": True, "state": "completed", "modified": True, "saved": True,
            "receipt_persisted": False, "persistence_error": "Final receipt was not persisted",
            "assets": [{"asset_path": "/Game/__Dev/SceneTools/Imported", "saved": True}]}}
        result = self.tools.get_tool("get_scene_import_status").fn(None, "project", "import001")
        self.assertTrue(result["saved"])
        self.assertTrue(result["modified"])
        self.assertFalse(result["receipt_persisted"])
        self.assertEqual(result["assets"][0]["asset_path"], "/Game/__Dev/SceneTools/Imported")

    def test_unreadable_import_receipt_does_not_claim_rollback(self):
        self.connection.send_command.return_value = {"status": "error", "error": "Invalid journal", "result": {
            "success": False, "state": "interrupted_unknown", "stage": "receipt_load", "error_code": "receipt_unavailable"}}
        result = self.tools.get_tool("get_scene_import_status").fn(None, "project", "import001")
        self.assertEqual(result["error_code"], "receipt_unavailable")
        self.assertNotIn("saved", result)
        self.assertNotIn("modified", result)
        self.connection.send_command.assert_called_once()

    def test_capture_dimensions_and_failure_receipts_are_preserved(self):
        for state in ("pending", "completed", "failed"):
            with self.subTest(state=state):
                self.connection.reset_mock()
                payload = {"success": state != "failed", "state": state, "saved": state == "completed",
                    "requested_width": 1440, "requested_height": 1000, "actual_width": 1440, "actual_height": 1000,
                    "source_viewport_width": 2345, "source_viewport_height": 1833, "width": 1440, "height": 1000,
                    "aspect_ratio_policy": "camera_letterbox", "render_target": "offscreen", "source_viewport_unchanged": True}
                self.connection.send_command.return_value = {"status": "error" if state == "failed" else "success", "result": payload}
                if state == "pending":
                    result = self.tools.get_tool("capture_scene_viewport").fn(None, "project", "map", 1, "capture", width=1440, height=1000, warmup_frames=48, timeout_seconds=120)
                    self.connection.send_command.assert_called_once_with("capture_scene_viewport", {
                        "project_path": "project", "level_path": "map", "viewport_id": 1, "request_id": "capture",
                        "width": 1440, "height": 1000, "warmup_frames": 48, "timeout_seconds": 120})
                else:
                    result = self.tools.get_tool("get_scene_task_status").fn(None, "project", "capture")
                    self.connection.send_command.assert_called_once_with("get_scene_task_status", {"project_path": "project", "request_id": "capture"})
                for key, value in payload.items():
                    self.assertEqual(result[key], value)