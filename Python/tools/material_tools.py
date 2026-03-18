"""
Material Tools for Unreal MCP.

This module provides read-only tools for inspecting Material assets,
their node graphs, and MaterialInstance parameter values.
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_material_tools(mcp: FastMCP):
    """Register material tools with the MCP server."""

    @mcp.tool()
    def list_materials(
        ctx: Context,
        path: str = "/Game",
        recursive: bool = True,
        name_filter: str = "",
        type: str = "all"
    ) -> Dict[str, Any]:
        """List all Material and MaterialInstance assets in the project.

        Args:
            ctx: The MCP context
            path: Asset path to search in (default: "/Game")
            recursive: Search subdirectories recursively (default: True)
            name_filter: Only include assets whose name contains this string (case-insensitive)
            type: Filter by type - "material", "instance", or "all" (default: "all")

        Returns:
            Dict with "count" (int) and "materials" (list of dicts with name, path, package_path, type)

        Examples:
            list_materials()
            list_materials(path="/Game/Materials")
            list_materials(type="instance", name_filter="Wood")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "path": path,
                "recursive": recursive,
            }
            if name_filter:
                params["name_filter"] = name_filter
            if type != "all":
                params["type"] = type

            response = unreal.send_command("list_materials", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Found {result.get('count', 0)} materials")
            return result

        except Exception as e:
            logger.error(f"Error listing materials: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def read_material(
        ctx: Context,
        name: str = "",
        path: str = ""
    ) -> Dict[str, Any]:
        """Read a Material's node graph, properties, and connections.

        Returns the complete material structure including: material domain, blend mode,
        shading model, all expression nodes with their types/parameters/connections,
        and which nodes are connected to material input pins (BaseColor, Metallic, etc).

        Provide either 'name' or 'path' to identify the material.

        Args:
            ctx: The MCP context
            name: Material asset name (e.g. "M_Wood"). Searches in /Game.
            path: Full asset path (e.g. "/Game/Materials/M_Wood")

        Returns:
            Dict with material properties, nodes (expression graph), material_inputs (pin connections), comments

        Examples:
            read_material(name="M_Wood")
            read_material(path="/Game/Materials/M_Wood")
        """
        from unreal_mcp_server import get_unreal_connection

        if not name and not path:
            return {"success": False, "message": "Provide either 'name' or 'path'"}

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {}
            if path:
                params["path"] = path
            else:
                params["name"] = name

            response = unreal.send_command("read_material", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Read material: {result.get('name', 'unknown')} ({result.get('node_count', 0)} nodes)")
            return result

        except Exception as e:
            logger.error(f"Error reading material: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def get_material_instance_parameters(
        ctx: Context,
        name: str = "",
        path: str = ""
    ) -> Dict[str, Any]:
        """Get a MaterialInstance's parameter overrides (scalar, vector, texture).

        Returns the parent material reference and all overridden parameter values.

        Provide either 'name' or 'path' to identify the material instance.

        Args:
            ctx: The MCP context
            name: MaterialInstance asset name (e.g. "MI_Wood_Dark"). Searches in /Game.
            path: Full asset path (e.g. "/Game/Materials/MI_Wood_Dark")

        Returns:
            Dict with parent info and parameter arrays: scalar_parameters, vector_parameters, texture_parameters

        Examples:
            get_material_instance_parameters(name="MI_Wood_Dark")
            get_material_instance_parameters(path="/Game/Materials/MI_Wood_Dark")
        """
        from unreal_mcp_server import get_unreal_connection

        if not name and not path:
            return {"success": False, "message": "Provide either 'name' or 'path'"}

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {}
            if path:
                params["path"] = path
            else:
                params["name"] = name

            response = unreal.send_command("get_material_instance_parameters", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Read material instance: {result.get('name', 'unknown')}")
            return result

        except Exception as e:
            logger.error(f"Error getting material instance parameters: {e}")
            return {"success": False, "message": str(e)}

    logger.info("Material tools registered successfully")
