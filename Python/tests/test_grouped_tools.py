import importlib.util
import json
import logging
import os
from pathlib import Path
import sys
from types import ModuleType
import unittest
from unittest.mock import Mock, patch

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp.server.fastmcp import Context, FastMCP
from mcp.server.fastmcp.exceptions import ToolError
from tools.grouped_tools import register_grouped_tools


class GroupedToolsTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.server = FastMCP("group-test")
        self.calls = []

        @self.server.tool()
        def read_sample(ctx: Context, asset_path: str, detailed: bool = False) -> dict:
            """Read a material graph without changes."""
            self.calls.append((ctx, asset_path, detailed))
            return {"asset_path": asset_path, "detailed": detailed}

        @self.server.tool()
        def write_sample(asset_path: str) -> dict:
            """Change a material parent."""
            self.calls.append(asset_path)
            return {"success": False, "modified": True, "saved": False}

        @self.server.tool()
        def other_sample() -> dict:
            """An unrelated operation."""
            self.calls.append("other")
            return {}

        self.categories = {"material": {"read_sample", "write_sample"}, "niagara": set(), "umg": set()}
        self.original_schema = self.server._tool_manager.get_tool("read_sample").parameters

    def group(self):
        register_grouped_tools(self.server, self.categories, {"read_sample"})

    async def invoke(self, name, arguments, context=None):
        return await self.server._tool_manager.get_tool(name).run(arguments, context=context)

    async def test_public_list_and_exact_contract(self):
        self.group()
        self.assertEqual(set(self.server._tool_manager._tools), {
            "other_sample", "material_search", "material_call_read", "material_call_write"})
        result = await self.invoke("material_search", {"tool": "read_sample"})
        self.assertEqual(result["input_schema"], self.original_schema)
        self.assertEqual(result["call_tool"], "material_call_read")
        self.assertNotIn("ctx", result["input_schema"]["properties"])
        self.assertEqual(self.calls, [])

    async def test_browse_search_and_limits(self):
        self.group()
        page = await self.invoke("material_search", {"limit": 1})
        self.assertEqual(page["total"], 2)
        self.assertTrue(page["has_more"])
        self.assertEqual(page["next_offset"], 1)
        self.assertNotIn("contract", page)
        self.assertNotIn("input_schema", page["results"][0])
        next_page = await self.invoke("material_search", {"limit": 1, "offset": 1})
        self.assertFalse(next_page["has_more"])
        result = await self.invoke("material_search", {"query": "parent"})
        self.assertEqual(result["contract"]["tool"], "write_sample")
        self.assertEqual((await self.invoke("material_search", {"query": "missing"}))["total"], 0)
        for arguments in ({"limit": 11}, {"offset": -1}, {"tool": "other_sample"}):
            with self.assertRaises(ToolError):
                await self.invoke("material_search", arguments)
        self.assertEqual(self.calls, [])

    async def test_dispatch_defaults_context_and_failure_receipt(self):
        self.group()
        context = Context()
        result = await self.invoke("material_call_read", {
            "tool": "read_sample", "arguments": {"asset_path": "/Game/Test"}}, context)
        self.assertFalse(result["detailed"])
        self.assertIs(self.calls[0][0], context)
        result = await self.invoke("material_call_write", {
            "tool": "write_sample", "arguments": {"asset_path": "/Game/Test"}})
        self.assertEqual(result, {"success": False, "modified": True, "saved": False})

    async def test_reject_wrong_route_and_bad_parameters_before_execution(self):
        self.group()
        for endpoint, tool, arguments in (
            ("material_call_read", "write_sample", {"asset_path": "/Game/Test"}),
            ("material_call_write", "read_sample", {"asset_path": "/Game/Test"}),
            ("material_call_write", "other_sample", {}),
            ("material_call_read", "read_sample", {}),
            ("material_call_read", "read_sample", {"asset_path": []}),
            ("material_call_read", "read_sample", {"asset_path": "/Game/Test", "ctx": "injected"}),
        ):
            with self.assertRaises(ToolError):
                await self.invoke(endpoint, {"tool": tool, "arguments": arguments})
        self.assertEqual(self.calls, [])

    async def test_filter_before_grouping_cannot_be_bypassed(self):
        del self.server._tool_manager._tools["write_sample"]
        self.group()
        self.assertIsNone(self.server._tool_manager.get_tool("material_call_write"))
        with self.assertRaises(ToolError):
            await self.invoke("material_search", {"tool": "write_sample"})
        with self.assertRaises(ToolError):
            await self.invoke("material_call_read", {"tool": "write_sample", "arguments": {}})
        self.assertIsNone(self.server._tool_manager.get_tool("niagara_search"))
        self.assertEqual(self.calls, [])


class GroupedServerTests(unittest.IsolatedAsyncioTestCase):
    def load_server(self, mode=None, read_only=False, disabled=()):
        environment = {f"MCP_{category.upper()}_ENABLED": "1" for category in (
            "material", "niagara", "umg", "editor", "asset", "blueprint", "node", "project", "navigation", "cascade")}
        environment.update({f"MCP_{category.upper()}_ENABLED": "0" for category in disabled})
        environment["UNREAL_MCP_READ_ONLY"] = "1" if read_only else "0"
        path = Path(__file__).resolve().parents[1] / "unreal_mcp_server.py"
        spec = importlib.util.spec_from_file_location("_grouped_test_server", path)
        module = importlib.util.module_from_spec(spec)
        with patch.dict(os.environ, environment):
            if mode is None:
                os.environ.pop("MCP_TOOL_MODE", None)
            else:
                os.environ["MCP_TOOL_MODE"] = mode
            with patch("logging.FileHandler", return_value=logging.NullHandler()):
                spec.loader.exec_module(module)
        return module

    async def test_modes_and_schema_size(self):
        direct = self.load_server("direct")
        grouped = self.load_server()
        direct_tools = await direct.mcp.list_tools()
        grouped_tools = await grouped.mcp.list_tools()
        self.assertEqual(len(direct_tools), 119)
        self.assertEqual(len(grouped_tools), 67)
        self.assertEqual(set().union(*direct._CATEGORY_TOOLS.values()), {tool.name for tool in direct_tools})
        size = lambda tools: len(json.dumps([tool.model_dump(exclude_none=True) for tool in tools]))
        self.assertLess(size(grouped_tools), size(direct_tools) * 0.5)
        for category in ("material", "niagara", "umg"):
            search = grouped.mcp._tool_manager.get_tool(f"{category}_search")
            for name in direct._CATEGORY_TOOLS[category]:
                with self.subTest(tool=name):
                    self.assertIsNone(grouped.mcp._tool_manager.get_tool(name))
                    contract = await search.run({"tool": name})
                    original = direct.mcp._tool_manager.get_tool(name)
                    self.assertEqual(contract["input_schema"], original.parameters)
                    self.assertEqual(contract["description"], original.description)
                    expected_effect = "read" if name in direct._READ_ONLY_TOOLS else "write"
                    self.assertEqual(contract["effect"], expected_effect)

    async def test_readonly_and_disabled_categories(self):
        self.assertEqual(len(await self.load_server("direct", True).mcp.list_tools()), 33)
        readonly = self.load_server(read_only=True)
        self.assertEqual(len(await readonly.mcp.list_tools()), 28)
        for category in ("material", "niagara", "umg"):
            self.assertIsNone(readonly.mcp._tool_manager.get_tool(f"{category}_call_write"))
            write_name = next(iter(readonly._CATEGORY_TOOLS[category] - readonly._READ_ONLY_TOOLS))
            with self.assertRaises(ToolError):
                await readonly.mcp._tool_manager.get_tool(f"{category}_search").run({"tool": write_name})
        disabled = self.load_server(disabled=("material", "niagara", "umg"))
        self.assertEqual(len(await disabled.mcp.list_tools()), 58)
        for category in ("material", "niagara", "umg"):
            for suffix in ("search", "call_read", "call_write"):
                self.assertIsNone(disabled.mcp._tool_manager.get_tool(f"{category}_{suffix}"))
        with self.assertRaises(ValueError):
            self.load_server("typo")

    async def test_real_wrappers_forward_without_schema_or_result_changes(self):
        server = self.load_server()
        connection = Mock()
        module = ModuleType("unreal_mcp_server")
        module.get_unreal_connection = Mock(return_value=connection)
        connection.send_command.return_value = {"status": "success", "result": {"success": True, "saved": True}}
        with patch.dict(sys.modules, unreal_mcp_server=module):
            for category, name, arguments, expected_command in (
                ("material", "set_material_instance_parameters", {
                    "asset_path": "/Game/Test", "parent_material_path": "/Game/Parent"}, "set_material_instance_parameters"),
                ("material", "set_material_physical_material", {
                    "asset_path": "/Game/Test", "physical_material_path": "", "expected_value": ""}, "set_material_physical_material"),
                ("niagara", "set_niagara_parameter", {
                    "actor_name": "Test", "parameter_name": "User.Speed", "parameter_type": "float", "value": 2.0}, "set_niagara_parameter"),
                ("umg", "set_widget_property", {
                    "path": "/Game/Test", "widget_name": "Text", "property_name": "Text", "property_value": "Hi"}, "set_widget_property"),
            ):
                with self.subTest(category=category):
                    result = await server.mcp._tool_manager.get_tool(f"{category}_call_write").run({
                        "tool": name, "arguments": arguments})
                    self.assertIsInstance(result, dict)
                    self.assertEqual(connection.send_command.call_args.args[0], expected_command)
        self.assertEqual(connection.send_command.call_count, 4)

    async def test_stdio_discovery_dispatch_and_errors(self):
        environment = {key: value for key, value in os.environ.items()
                       if key != "UNREAL_MCP_READ_ONLY" and not key.startswith("MCP_")}
        environment["MCP_TOOL_MODE"] = "grouped"
        script = (
            "from unittest.mock import Mock\n"
            "import unreal_mcp_server as server\n"
            "connection = Mock()\n"
            "connection.send_command.side_effect = lambda command, params: "
            "{'status': 'success', 'result': {'command': command, 'params': params}}\n"
            "server.get_unreal_connection = lambda: connection\n"
            "server.mcp.run(transport='stdio')\n"
        )
        parameters = StdioServerParameters(
            command=sys.executable, args=["-c", script], env=environment,
            cwd=str(Path(__file__).resolve().parents[1]))
        async with stdio_client(parameters) as (reader, writer):
            async with ClientSession(reader, writer) as session:
                await session.initialize()
                self.assertEqual(len((await session.list_tools()).tools), 67)
                migration = await session.call_tool("plan_asset_migration", {
                    "roots": ["/Game/NS_Test"],
                    "path_rules": [{"source_root": "/Game", "target_root": "/Game/__Dev/Test"}]})
                self.assertFalse(migration.isError)
                self.assertEqual(json.loads(migration.content[0].text), {
                    "command": "plan_asset_migration", "params": {
                        "roots": ["/Game/NS_Test"],
                        "path_rules": [{"source_root": "/Game", "target_root": "/Game/__Dev/Test"}],
                        "rebind_assets": [], "retain_roots": []}})
                contract = await session.call_tool("material_search", {"tool": "set_material_instance_parameters"})
                self.assertFalse(contract.isError)
                self.assertEqual(json.loads(contract.content[0].text)["call_tool"], "material_call_write")
                result = await session.call_tool("material_call_write", {
                    "tool": "set_material_instance_parameters",
                    "arguments": {"asset_path": "/Game/Test", "parent_material_path": "/Game/Parent"}})
                self.assertFalse(result.isError)
                self.assertEqual(json.loads(result.content[0].text), {
                    "command": "set_material_instance_parameters",
                    "params": {"asset_path": "/Game/Test", "parent_material_path": "/Game/Parent"}})
                for endpoint, arguments in (
                    ("material_call_read", {"tool": "set_material_instance_parameters", "arguments": {}}),
                    ("material_call_write", {"tool": "set_niagara_parameter", "arguments": {}}),
                    ("material_call_write", {"tool": "set_material_instance_parameters", "arguments": {"unexpected": True}}),
                    ("material_call_write", {"tool": "set_material_instance_parameters", "arguments": {}}),
                    ("set_material_instance_parameters", {"asset_path": "/Game/Test"}),
                ):
                    with self.subTest(endpoint=endpoint, arguments=arguments):
                        self.assertTrue((await session.call_tool(endpoint, arguments)).isError)


if __name__ == "__main__":
    unittest.main()