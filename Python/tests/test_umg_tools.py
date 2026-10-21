import sys
import unittest
from types import ModuleType
from unittest.mock import Mock, patch

from mcp.server.fastmcp import FastMCP
from mcp.server.fastmcp.exceptions import ToolError
from tools.umg_tools import register_umg_tools
import test_grouped_tools


class WidgetEditingTests(unittest.TestCase):
    def setUp(self):
        server = FastMCP("widget-editing-test")
        register_umg_tools(server)
        self.tools = server._tool_manager
        self.connection = Mock()
        module = ModuleType("unreal_mcp_server")
        module.get_unreal_connection = Mock(return_value=self.connection)
        context = patch.dict(sys.modules, unreal_mcp_server=module)
        context.start()
        self.addCleanup(context.stop)
        self.connection.send_command.return_value = {"status": "success", "result": {"success": True, "saved": False}}

    def test_reparent_schema_and_preview_defaults(self):
        tool = self.tools.get_tool("reparent_widget")
        self.assertEqual(tool.parameters["required"], ["path", "widget_name", "new_parent_name", "insert_index"])
        tool.fn(None, "/Game/UI/WBP_Test", "KeyText", "Overlay", 0)
        self.connection.send_command.assert_called_once_with("reparent_widget", {
            "path": "/Game/UI/WBP_Test", "widget_name": "KeyText", "new_parent_name": "Overlay", "insert_index": 0, "dry_run": True})
        properties = {"HorizontalAlignment": "HAlign_Center", "VerticalAlignment": "VAlign_Center"}
        tool.fn(None, "path", "KeyText", "Overlay", 2, properties, False)
        self.assertEqual(self.connection.send_command.call_args.args[1]["slot_properties"], properties)

    def test_preview_read_write_contracts(self):
        self.tools.get_tool("get_widget_preview_settings").fn(None, "/Game/UI/WBP_Test")
        self.connection.send_command.assert_called_once_with("get_widget_preview_settings", {"path": "/Game/UI/WBP_Test"})
        self.tools.get_tool("set_widget_preview_settings").fn(None, "path", "Desired")
        self.connection.send_command.assert_called_with("set_widget_preview_settings", {"path": "path", "mode": "Desired", "dry_run": True})
        self.tools.get_tool("set_widget_preview_settings").fn(None, "path", "Custom", [320, 120], False)
        self.connection.send_command.assert_called_with("set_widget_preview_settings", {"path": "path", "mode": "Custom", "size": [320, 120], "dry_run": False})

    def test_nonfinite_and_partial_failure_receipt(self):
        result = self.tools.get_tool("set_widget_preview_settings").fn(None, "path", "Custom", [float("nan"), 120])
        self.assertFalse(result["modified"])
        self.connection.send_command.assert_not_called()
        self.connection.send_command.return_value = {"status": "error", "error": "Insert failed", "result": {
            "modified": False, "saved": False, "rolled_back": True, "after": {"parent": "Root", "index": 0}}}
        result = self.tools.get_tool("reparent_widget").fn(None, "path", "KeyText", "Overlay", 0, dry_run=False)
        self.assertFalse(result["success"])
        self.assertTrue(result["rolled_back"])
        self.assertEqual(result["after"]["parent"], "Root")
        self.connection.reset_mock()
        self.connection.send_command.side_effect = TimeoutError("timeout")
        result = self.tools.get_tool("reparent_widget").fn(None, "path", "KeyText", "Overlay", 0, dry_run=False)
        self.assertEqual(result["stage"], "transport")
        self.assertNotIn("modified", result)
        self.connection.send_command.assert_called_once()

    def test_grouped_permissions_and_pagination_schema(self):
        for readonly in (False, True):
            server = test_grouped_tools.GroupedServerTests().load_server("grouped", readonly)
            search = server.mcp._tool_manager.get_tool("umg_search")
            self.assertEqual(search.parameters["properties"]["limit"]["minimum"], 1)
            self.assertEqual(search.parameters["properties"]["limit"]["maximum"], 10)
            self.assertEqual(search.fn(tool="get_widget_preview_settings")["call_tool"], "umg_call_read")
            for name in ("reparent_widget", "set_widget_preview_settings"):
                if readonly:
                    with self.assertRaises(ToolError):
                        search.fn(tool=name)
                else:
                    self.assertEqual(search.fn(tool=name)["call_tool"], "umg_call_write")
        disabled = test_grouped_tools.GroupedServerTests().load_server("grouped", disabled=("umg",))
        self.assertIsNone(disabled.mcp._tool_manager.get_tool("umg_search"))