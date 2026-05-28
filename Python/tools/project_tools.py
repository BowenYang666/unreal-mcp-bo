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

    @mcp.tool()
    def get_class_properties(
        ctx: Context,
        class_name: str = "",
        asset_path: str = "",
        category: str = ""
    ) -> Dict[str, Any]:
        """
        Get all editable properties of a UClass or asset, useful for discovering
        what properties are available before setting them.

        Provide either class_name or asset_path:
        - class_name: UClass name (e.g. "BlendSpace1D", "PlayerController", "StaticMeshActor")
        - asset_path: Asset path to load and inspect (e.g. "/Game/Player/Animations/BS_Locomotion")
          When asset_path is provided, current property values are also returned.

        Args:
            class_name: Name of the UClass to inspect. Supports engine and project classes.
            asset_path: Full asset path to load and inspect. Also returns current values.
            category: Optional filter to only return properties in this category
                (e.g. "Axis Settings", "Physics", "Rendering").

        Returns:
            Dict with class name, parent class, property_count, and properties array.
            Each property has: name, type, category, editable, and optionally value, tooltip.

        Examples:
            get_class_properties(class_name="BlendSpace1D")
            get_class_properties(asset_path="/Game/Player/Animations/BS_Locomotion")
            get_class_properties(class_name="StaticMeshComponent", category="Physics")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {}
            if class_name:
                params["class_name"] = class_name
            if asset_path:
                params["asset_path"] = asset_path
            if category:
                params["category"] = category

            if not class_name and not asset_path:
                return {"success": False, "message": "Must provide either class_name or asset_path"}

            response = unreal.send_command("get_class_properties", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            logger.error(f"Error getting class properties: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def read_behavior_tree(
        ctx: Context,
        asset_path: str
    ) -> Dict[str, Any]:
        """Read the full structure of a Behavior Tree asset.

        Returns the complete tree hierarchy: composites (Selector/Sequence), tasks,
        decorators (with flow abort mode), and services (with tick intervals).

        Args:
            asset_path: Full asset path of the BehaviorTree,
                e.g. "/Game/AI/BT_EnemyMain"

        Returns:
            Dict with name, blackboard reference, and root node tree (recursive).
            Each node has: class, name, execution_index, and type-specific properties.
            Composites have children[], services[]. Children have decorators[].

        Examples:
            read_behavior_tree(asset_path="/Game/AI/BT_EnemyMain")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("read_behavior_tree", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            logger.error(f"Error reading behavior tree: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def read_blackboard(
        ctx: Context,
        asset_path: str
    ) -> Dict[str, Any]:
        """Read all keys from a Blackboard data asset.

        Returns the list of blackboard keys with their names, types, and sync status.

        Args:
            asset_path: Full asset path of the BlackboardData,
                e.g. "/Game/AI/BB_EnemyMain"

        Returns:
            Dict with name, parent (if any), and keys array.
            Each key has: name, type (e.g. "Object", "Float", "Bool", "Enum", "Vector"),
            and instance_synced flag.

        Examples:
            read_blackboard(asset_path="/Game/AI/BB_EnemyMain")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("read_blackboard", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            logger.error(f"Error reading blackboard: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def read_state_tree(
        ctx: Context,
        asset_path: str
    ) -> Dict[str, Any]:
        """Read the full structure of a StateTree asset.

        Returns the state hierarchy with tasks, transitions, enter conditions,
        evaluators, global tasks, and global parameters. Recursively walks
        subtrees and child states.

        Args:
            asset_path: Full asset path of the StateTree,
                e.g. "/Game/AI/ST_Enemy_Dog"

        Returns:
            Dict with name, schema, global_parameters, evaluators, global_tasks, states.
            Each state has: name, type, selection_behavior, tasks[], transitions[],
            enter_conditions[], children[].
            Tasks/conditions have: class (struct name), instance_class (for BP nodes),
            instance_properties.
            Transitions have: trigger, priority, link_type, target_state, conditions[].

        Examples:
            read_state_tree(asset_path="/Game/AI/ST_Enemy_Dog")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("read_state_tree", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            logger.error(f"Error reading state tree: {e}")
            return {"success": False, "message": str(e)}

    logger.info("Project tools registered successfully") 