import sys
import threading
from concurrent.futures import ThreadPoolExecutor
import unittest
import json
import tempfile
from pathlib import Path
from types import ModuleType
from unittest.mock import Mock, patch

from mcp.server.fastmcp import FastMCP
from tools.blueprint_tools import register_blueprint_tools
from tools.editor_tools import register_editor_tools
from tools.material_tools import register_material_tools
from tools.project_tools import register_project_tools
import test_grouped_tools


class AssetPropertiesTests(unittest.TestCase):
    def setUp(self):
        server = FastMCP("asset-properties")
        register_project_tools(server)
        register_blueprint_tools(server)
        register_editor_tools(server)
        register_material_tools(server)
        self.tools = server._tool_manager
        self.write = server._tool_manager.get_tool("set_asset_properties")
        self.read = server._tool_manager.get_tool("get_class_properties")
        self.connection = Mock()
        module = ModuleType("unreal_mcp_server")
        module.get_unreal_connection = Mock(return_value=self.connection)
        module.spill_if_oversized = Mock(side_effect=lambda response, *args, **kwargs: response)
        context = patch.dict(sys.modules, unreal_mcp_server=module)
        context.start()
        self.addCleanup(context.stop)
        self.connection.send_command.return_value = {"status": "success", "result": {"success": True, "saved": True}}

    def test_patch_schema_and_forwarding(self):
        self.assertEqual(self.write.parameters["required"], ["asset_path", "changes"])
        changes = [{"path": "Stats.Health", "value": 150, "expected_value": 100}]
        self.assertTrue(self.write.fn(None, "/Game/Data/Test", changes)["saved"])
        self.connection.send_command.assert_called_once_with("set_asset_properties", {
            "asset_path": "/Game/Data/Test", "changes": changes, "save": True})

    def test_migration_schema_and_forwarding(self):
        tool = self.tools.get_tool("plan_asset_migration")
        self.assertEqual(tool.parameters["required"], ["roots", "path_rules"])
        roots = ["/Game/VFX/NS_Test"]
        rules = [{"source_root": "/Game", "target_root": "/Game/__Dev/CopyTest"}]
        tool.fn(None, roots, rules)
        self.connection.send_command.assert_called_with("plan_asset_migration", {
            "roots": roots, "path_rules": rules, "rebind_assets": [], "retain_roots": []})
        tool.fn(None, roots, rules, roots, ["/Game/Shared"])
        self.connection.send_command.assert_called_with("plan_asset_migration", {
            "roots": roots, "path_rules": rules, "rebind_assets": roots, "retain_roots": ["/Game/Shared"]})
        for name, arguments in (
            ("execute_asset_migration", {"plan_id": "id", "confirmation_token": "confirmation"}),
            ("get_asset_migration_status", {"plan_id": "id"}),
            ("verify_asset_migration", {"plan_id": "id"}),
        ):
            with self.subTest(tool=name):
                operation = self.tools.get_tool(name)
                self.assertEqual(operation.parameters["required"], list(arguments))
                operation.fn(None, **arguments)
                self.connection.send_command.assert_called_with(name, arguments)

    def test_migration_preserves_partial_receipt_and_never_retries_timeout(self):
        operation = self.tools.get_tool("execute_asset_migration")
        items = [{"source": "/Game/A", "created": True, "modified": True, "saved": False}]
        self.connection.send_command.return_value = {"status": "error", "error": "save failed", "result": {
            "state": "failed", "plan_id": "id", "items": items}}
        result = operation.fn(None, "id", "confirmation")
        self.assertFalse(result["success"])
        self.assertEqual(result["items"], items)
        self.assertEqual(result["state"], "failed")
        self.connection.reset_mock()
        self.connection.send_command.side_effect = TimeoutError("Timed out")
        result = operation.fn(None, "id", "confirmation")
        self.assertEqual(result["stage"], "transport")
        self.assertNotIn("saved", result)
        self.connection.send_command.assert_called_once()

    def test_migration_oversized_failure_retains_state_and_full_receipt(self):
        server = test_grouped_tools.GroupedServerTests().load_server()
        module = sys.modules["unreal_mcp_server"]
        with tempfile.TemporaryDirectory() as directory, patch("tempfile.gettempdir", return_value=directory), \
                patch.object(module, "spill_if_oversized", server.spill_if_oversized):
            for success, state in ((False, "failed"), (True, "planned")):
                payload = {"success": success, "state": state, "plan_id": "id",
                           "executable": False, "blockers": ["blocked"], "dependencies": ["x" * 26000]}
                self.connection.send_command.return_value = {"status": "success", "result": payload}
                result = self.tools.get_tool("get_asset_migration_status").fn(None, "id")
                self.assertTrue(result["overflow"])
                self.assertEqual(result["success"], success)
                self.assertEqual(result["state"], state)
                self.assertFalse(result["executable"])
                self.assertEqual(json.loads(Path(result["file_path"]).read_text(encoding="utf-8")), payload)

    def test_migration_registration_permissions(self):
        fixture = test_grouped_tools.GroupedServerTests()
        names = ("plan_asset_migration", "execute_asset_migration", "get_asset_migration_status", "verify_asset_migration")
        for mode in ("direct", "grouped"):
            for readonly in (False, True):
                for disabled in ((), ("asset",), ("project",)):
                    tools = fixture.load_server(mode, readonly, disabled).mcp._tool_manager
                    for name in names:
                        self.assertEqual(tools.get_tool(name) is not None,
                            "asset" not in disabled and not (readonly and name == "execute_asset_migration"),
                            (mode, readonly, disabled, name))

    def test_invalid_requests_never_send(self):
        for path, changes in (("/Engine/Test", [{"path": "Health", "value": 1}]),
                              ("/Game/../Test", []), ("/Game/Test", []),
                              ("/Game/Test", [{"path": "Health"}]),
                              ("/Game/Test", [{"path": "Health", "value": float("nan")}]),
                              ("/Game/Test", [{"path": "Health", "value": 1, "typo": True}])):
            self.assertFalse(self.write.fn(None, path, changes)["success"])
        self.connection.send_command.assert_not_called()

    def test_save_error_and_timeout_are_not_false_rollback(self):
        self.connection.send_command.return_value = {"status": "error", "error": "save failed", "result": {
            "modified": True, "saved": False, "stage": "save", "changes": [{"path": "Health", "after": 150}]}}
        result = self.write.fn(None, "/Game/Test", [{"path": "Health", "value": 150}])
        self.assertTrue(result["modified"])
        self.assertEqual(result["stage"], "save")
        self.connection.send_command.side_effect = TimeoutError("Timed out")
        result = self.write.fn(None, "/Game/Test", [{"path": "Health", "value": 150}])
        self.assertNotIn("modified", result)
        self.assertNotIn("saved", result)

    def test_opt_in_structured_read_preserves_legacy_parameters(self):
        self.read.fn(None, asset_path="/Game/Test")
        self.connection.send_command.assert_called_with("get_class_properties", {"asset_path": "/Game/Test"})
        self.read.fn(None, asset_path="/Game/Test", structured=True, property_paths=["Stats.Health"])
        self.connection.send_command.assert_called_with("get_class_properties", {
            "asset_path": "/Game/Test", "structured": True, "property_paths": ["Stats.Health"]})

    def test_registration_filters(self):
        fixture = test_grouped_tools.GroupedServerTests()
        for mode in ("direct", "grouped"):
            for readonly, disabled, exposed in ((False, (), True), (True, (), False),
                                                (False, ("asset",), False), (False, ("project",), True)):
                server = fixture.load_server(mode, readonly, disabled)
                for name in ("set_asset_properties", "create_data_asset", "create_physical_material"):
                    self.assertEqual(server.mcp._tool_manager.get_tool(name) is not None, exposed)

    def test_creation_defaults_and_partial_failure_receipts(self):
        for name, arguments in (
            ("create_data_asset", {"asset_path": "/Game/Test", "class_path": "/Script/NodeFall.NFWeaponImpactProfile"}),
            ("create_physical_material", {"asset_path": "/Game/PM_Test"}),
        ):
            with self.subTest(tool=name):
                tool = self.tools.get_tool(name)
                self.assertEqual(tool.parameters["required"], list(arguments))
                self.connection.send_command.return_value = {"status": "error", "error": "save failed", "result": {
                    "created": True, "modified": True, "saved": False, "package_dirty": True, "stage": "save"}}
                result = tool.fn(None, **arguments)
                self.connection.send_command.assert_called_with(name, {**arguments, "save": True})
                self.assertFalse(result["success"])
                self.assertTrue(result["created"])
                self.assertTrue(result["package_dirty"])
                self.assertFalse(result["saved"])
                self.connection.send_command.side_effect = TimeoutError("Timed out")
                result = tool.fn(None, **arguments, save=False)
                self.assertEqual(result["stage"], "transport")
                self.assertNotIn("created", result)
                self.assertNotIn("saved", result)
                self.connection.send_command.side_effect = None

    def test_map_payload_preserves_null_references_and_expected_entries(self):
        entries = [{"key": "SurfaceType1", "value": {
            "System": None, "Sound": "/Game/Sound.Sound", "Scale": 2.5,
            "Decal": {"Material": None, "Size": {"X": 2, "Y": 8, "Z": 9}}}}]
        for before, after in (([], entries), (entries, [])):
            changes = [{"path": "Surfaces", "value": after, "expected_value": before}]
            self.write.fn(None, "/Game/Profile", changes, save=False)
            self.connection.send_command.assert_called_with("set_asset_properties", {
                "asset_path": "/Game/Profile", "changes": changes, "save": False})

    def test_physical_assignment_and_trace_forwarding(self):
        for name, arguments, save in (
            ("set_material_physical_material", {"asset_path": "/Game/M_Test", "physical_material_path": ""}, True),
            ("set_component_physical_material", {"level_path": "/Game/Test", "actor_name": "Wall",
                "component_name": "StaticMeshComponent0", "physical_material_path": "/Game/PM_Test"}, False),
        ):
            with self.subTest(tool=name):
                tool = self.tools.get_tool(name)
                tool.fn(None, **arguments)
                self.connection.send_command.assert_called_with(name, {**arguments, "save": save})
                self.connection.send_command.return_value = {"status": "error", "error": "save failed", "result": {
                    "modified": True, "saved": False, "package_dirty": True, "after": "/Game/PM_Test.PM_Test"}}
                result = tool.fn(None, **arguments, expected_value="")
                self.connection.send_command.assert_called_with(name, {**arguments, "save": save, "expected_value": ""})
                self.assertFalse(result["success"])
                self.assertTrue(result["modified"])
                self.assertFalse(result["saved"])
        self.tools.get_tool("trace_physical_material").fn(None, "/Game/Test", [0, 0, 200], [0, 0, -200])
        self.connection.send_command.assert_called_with("trace_physical_material", {
            "level_path": "/Game/Test", "start": [0, 0, 200], "end": [0, 0, -200], "trace_complex": False})

    def test_blueprint_physical_override_reuses_existing_contract(self):
        tool = self.tools.get_tool("set_component_property")
        receipt = {"status": "error", "error": "compile failed", "result": {"modified": True, "saved": False}}
        self.connection.send_command.return_value = receipt
        for name in ("PhysMaterialOverride", "BodyInstance.PhysMaterialOverride"):
            result = tool.fn(None, "/Game/BP_Test", "Collider", name, None, expected_value="", save=True)
            self.connection.send_command.assert_called_with("set_component_property", {
                "blueprint_path": "/Game/BP_Test", "component_name": "Collider", "property_name": name,
                "property_value": None, "expected_value": "", "save": True})
            self.assertEqual(result, receipt)
        self.connection.reset_mock()
        self.assertFalse(tool.fn(None, "/Game/BP_Test", "Collider", "Visible", True, save=True)["success"])
        self.connection.send_command.assert_not_called()
        tool.fn(None, "/Game/BP_Test", "Collider", "Visible", True)
        self.connection.send_command.assert_called_once_with("set_component_property", {
            "blueprint_path": "/Game/BP_Test", "component_name": "Collider", "property_name": "Visible", "property_value": True})

    def test_physical_tools_filters(self):
        fixture = test_grouped_tools.GroupedServerTests()
        for mode in ("direct", "grouped"):
            readonly = fixture.load_server(mode, True).mcp._tool_manager
            self.assertIsNotNone(readonly.get_tool("trace_physical_material"))
            self.assertIsNone(readonly.get_tool("set_component_physical_material"))
            disabled = fixture.load_server(mode, disabled=("editor",)).mcp._tool_manager
            self.assertIsNone(disabled.get_tool("trace_physical_material"))
            self.assertIsNone(disabled.get_tool("set_component_physical_material"))

    def test_connection_getter_does_no_io_and_requests_are_serialized(self):
        server = test_grouped_tools.GroupedServerTests().load_server()
        with patch.object(server.UnrealConnection, "connect", side_effect=AssertionError("Getter must not connect")):
            connection = server.get_unreal_connection()
            self.assertIs(connection, server.get_unreal_connection())
        inside = threading.Lock()
        ready = threading.Barrier(8)
        def execute(command, params):
            if not inside.acquire(blocking=False):
                raise AssertionError("Concurrent request entered")
            try:
                self.assertTrue(connection._request_lock.locked())
                return command
            finally:
                inside.release()
        def call(index):
            ready.wait(timeout=5)
            return connection.send_command(str(index), {})
        with patch.object(connection, "_send_command_locked", side_effect=execute):
            with ThreadPoolExecutor(max_workers=8) as pool:
                self.assertEqual(list(pool.map(call, range(8))), [str(index) for index in range(8)])


if __name__ == "__main__":
    unittest.main()