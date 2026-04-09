"""
Project Tools for Unreal MCP.

This module provides tools for managing project-wide settings and configuration.
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_project_tools(mcp: FastMCP):
    """Register project tools with the MCP server."""
    
    @mcp.tool()
    def create_input_mapping(
        ctx: Context,
        action_name: str,
        key: str,
        input_type: str = "Action"
    ) -> Dict[str, Any]:
        """
        Create an input mapping for the project.
        
        Args:
            action_name: Name of the input action
            key: Key to bind (SpaceBar, LeftMouseButton, etc.)
            input_type: Type of input mapping (Action or Axis)
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "action_name": action_name,
                "key": key,
                "input_type": input_type
            }
            
            logger.info(f"Creating input mapping '{action_name}' with key '{key}'")
            response = unreal.send_command("create_input_mapping", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Input mapping creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error creating input mapping: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def read_data_asset(
        ctx: Context,
        asset_path: str
    ) -> Dict[str, Any]:
        """Read all properties from a DataAsset (or any UObject asset) via Unreal reflection.

        Returns the full set of BlueprintVisible UPROPERTY fields serialized as JSON.
        Works with any UDataAsset, UPrimaryDataAsset subclass, or other UObject-based assets.

        Args:
            ctx: The MCP context
            asset_path: Asset path, e.g. "/Game/Data/TowerConfig/DA_HoneyBarrel"

        Returns:
            Dict with asset_name, asset_path, class_name, and properties object

        Examples:
            read_data_asset(asset_path="/Game/Data/TowerConfig/DA_HoneyBarrel")
            read_data_asset(asset_path="/Game/Data/EnemyConfig/DA_Goblin")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("read_data_asset", {"asset_path": asset_path})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            logger.error(f"Error reading data asset: {e}")
            return {"success": False, "message": str(e)}

    logger.info("Project tools registered successfully") 