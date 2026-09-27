"""Bounded discovery and dispatch for large tool categories."""

import re

from mcp.server.fastmcp import Context, FastMCP
from mcp.server.fastmcp.exceptions import ToolError


GROUPED_CATEGORIES = ("material", "niagara", "umg")


def register_grouped_tools(server: FastMCP, categories: dict, read_only_tools: set):
    """Wrap only operations remaining after category and read-only filtering."""
    for category in GROUPED_CATEGORIES:
        operations = {
            name: server._tool_manager.get_tool(name)
            for name in sorted(categories[category])
            if server._tool_manager.get_tool(name) is not None
        }
        if operations:
            _register_group(server, category, operations, read_only_tools)


def _register_group(server: FastMCP, category: str, operations: dict, read_only_tools: set):
    read_names = set(operations).intersection(read_only_tools)
    write_names = set(operations).difference(read_names)

    def effect(name):
        return "read" if name in read_names else "write"

    def summary(name):
        lines = operations[name].description.strip().splitlines()
        return lines[0].strip()[:240] if lines else name

    def contract(name):
        operation = operations[name]
        return {
            "tool": name,
            "effect": effect(name),
            "description": operation.description,
            "input_schema": operation.parameters,
            "call_tool": f"{category}_call_{effect(name)}",
        }

    @server.tool(
        name=f"{category}_search",
        description=(
            f"Discover {category} operations without contacting Unreal. "
            "Use query for keywords, or tool for an exact name and its full parameter schema. "
            "Empty query browses compact summaries with offset/limit. "
            "Example: query='parent' or tool copied from a search result. "
            "Search before calling; pass only the described arguments."
        ),
    )
    def search(query: str = "", tool: str = "", limit: int = 5, offset: int = 0) -> dict:
        """Find operations, e.g. query='parent'; tool='read_material' returns its contract."""
        if tool:
            if tool not in operations:
                raise ToolError(f"Unknown or disabled {category} tool: {tool}. Use {category}_search.")
            return contract(tool)
        if query.strip() in operations:
            return contract(query.strip())
        if not 1 <= limit <= 10 or offset < 0:
            raise ToolError("limit must be 1..10 and offset must be nonnegative")
        terms = set(re.findall(r"[^\W_]+", query.casefold()))
        ranked = []
        for name, operation in operations.items():
            text = f"{name.replace('_', ' ')} {operation.description}".casefold()
            score = sum(3 if term in name else 1 for term in terms if term in text)
            if not terms or score:
                ranked.append((name, score))
        ranked.sort(key=lambda item: (-item[1], item[0]))
        page = ranked[offset:offset + limit]
        result = {
            "category": category,
            "results": [
                {
                    "tool": name,
                    "effect": effect(name),
                    "summary": summary(name),
                    "next_call": {"tool": f"{category}_search", "arguments": {"tool": name}},
                }
                for name, score in page
            ],
            "total": len(ranked),
            "has_more": offset + len(page) < len(ranked),
        }
        if result["has_more"]:
            result["next_offset"] = offset + len(page)
        if len(ranked) == 1 and page:
            result["contract"] = contract(page[0][0])
        if not ranked:
            result["message"] = "No match. Try English keywords, an exact tool name, or an empty query to browse."
        return result

    async def dispatch(tool, arguments, allowed, context):
        if tool not in allowed:
            raise ToolError(f"Tool {tool} is unavailable through this {category} endpoint. Use {category}_search.")
        operation = operations[tool]
        unknown = set(arguments).difference(operation.parameters.get("properties", {}))
        if unknown:
            raise ToolError(f"Unknown arguments for {tool}: {', '.join(sorted(unknown))}")
        return await operation.run(arguments, context=context)

    if read_names:
        @server.tool(
            name=f"{category}_call_read",
            description=(f"Run a read-only {category} operation discovered via {category}_search. "
                         "Pass its exact tool name and arguments object; example: arguments={}. "
                         "Write operations and operations from other categories are rejected."),
        )
        async def call_read(ctx: Context, tool: str, arguments: dict) -> dict:
            """Run a discovered read operation, e.g. tool='list_materials', arguments={}."""
            return await dispatch(tool, arguments, read_names, ctx)

    if write_names:
        @server.tool(
            name=f"{category}_call_write",
            description=(f"Run a modifying {category} operation discovered via {category}_search. "
                         "Pass its exact tool name and arguments object. May modify or save assets; "
                         "read the operation's contract first. Example arguments: {\"asset_path\":\"/Game/Test\"}. "
                         "Operations from other categories are rejected. Do not blindly retry timeouts."),
        )
        async def call_write(ctx: Context, tool: str, arguments: dict) -> dict:
            """Run a discovered write operation with its described arguments; no implicit save or retry."""
            return await dispatch(tool, arguments, write_names, ctx)

    for name in operations:
        del server._tool_manager._tools[name]