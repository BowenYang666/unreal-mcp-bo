"""Navigation tools for Unreal MCP."""

import logging
from typing import Any, Dict, List

from mcp.server.fastmcp import Context, FastMCP


logger = logging.getLogger("UnrealMCP")


def _send_navigation_command(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    from unreal_mcp_server import get_unreal_connection

    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}

        response = unreal.send_command(command, params)
        if not response:
            return {"success": False, "message": "No response from Unreal Engine"}
        if response.get("status") == "error":
            return {"success": False, "message": response.get("error", "Unknown error")}
        return response.get("result", response)
    except Exception as exc:
        logger.error("Navigation command %s failed: %s", command, exc)
        return {"success": False, "message": str(exc)}


def _validate_vector(name: str, value: List[float]) -> str:
    if not isinstance(value, list) or len(value) != 3:
        return f"{name} must be a list of three numbers"
    return ""


def register_navigation_tools(mcp: FastMCP):
    """Register navigation-volume, build, and query tools."""

    @mcp.tool()
    def set_nav_mesh_bounds_volume(
        ctx: Context,
        level_path: str,
        location: List[float],
        full_size: List[float],
        actor_name: str = "NavMeshBoundsVolume",
    ) -> Dict[str, Any]:
        """Create or update a box-shaped NavMeshBoundsVolume in the current editor level.

        The command rejects PIE worlds and rejects calls when ``level_path`` does not
        exactly match the currently open editor map. ``full_size`` is the complete
        world-space box size in centimeters, not a half extent.

        Examples:
            set_nav_mesh_bounds_volume(
                level_path="/Game/Maps/Arena",
                location=[0, 0, 200],
                full_size=[8400, 6400, 1000])
            set_nav_mesh_bounds_volume(
                level_path="/Game/Maps/Arena",
                location=[100, 0, 200],
                full_size=[9000, 6400, 1000],
                actor_name="NavMeshBoundsVolume_Main")
        """
        location_error = _validate_vector("location", location)
        size_error = _validate_vector("full_size", full_size)
        if location_error or size_error:
            return {"success": False, "message": location_error or size_error}
        return _send_navigation_command(
            "set_nav_mesh_bounds_volume",
            {
                "level_path": level_path,
                "location": [float(value) for value in location],
                "full_size": [float(value) for value in full_size],
                "actor_name": actor_name,
            },
        )

    @mcp.tool()
    def list_nav_mesh_bounds_volumes(ctx: Context, level_path: str) -> Dict[str, Any]:
        """List NavMeshBoundsVolume actors and their world-space bounds.

        Example:
            list_nav_mesh_bounds_volumes(level_path="/Game/Maps/Arena")
        """
        return _send_navigation_command(
            "list_nav_mesh_bounds_volumes", {"level_path": level_path}
        )

    @mcp.tool()
    def build_navigation(ctx: Context, level_path: str) -> Dict[str, Any]:
        """Request a navigation build for the current editor level.

        The response contains a request id and a state of ``in_progress``,
        ``completed``, or ``failed``. Poll ``get_navigation_status`` until the build
        is no longer in progress; command acceptance alone does not mean completion.

        Example:
            build_navigation(level_path="/Game/Maps/Arena")
        """
        return _send_navigation_command("build_navigation", {"level_path": level_path})

    @mcp.tool()
    def get_navigation_status(ctx: Context, level_path: str) -> Dict[str, Any]:
        """Read navigation build state, task counts, dirty areas, and default NavData.

        Example:
            get_navigation_status(level_path="/Game/Maps/Arena")
        """
        return _send_navigation_command(
            "get_navigation_status", {"level_path": level_path}
        )

    @mcp.tool()
    def project_point_to_navigation(
        ctx: Context,
        level_path: str,
        point: List[float],
        agent_class_path: str,
        extent: List[float] = [50.0, 50.0, 250.0],
    ) -> Dict[str, Any]:
        """Project a point using NavData selected for a real agent class CDO.

        ``agent_class_path`` accepts a Blueprint asset path or generated class path.

        Example:
            project_point_to_navigation(
                level_path="/Game/Maps/Arena",
                point=[0, 0, 100],
                agent_class_path="/Game/Enemies/BP_Enemy",
                extent=[50, 50, 250])
        """
        point_error = _validate_vector("point", point)
        extent_error = _validate_vector("extent", extent)
        if point_error or extent_error:
            return {"success": False, "message": point_error or extent_error}
        return _send_navigation_command(
            "project_point_to_navigation",
            {
                "level_path": level_path,
                "point": [float(value) for value in point],
                "agent_class_path": agent_class_path,
                "extent": [float(value) for value in extent],
            },
        )

    @mcp.tool()
    def find_navigation_path(
        ctx: Context,
        level_path: str,
        start: List[float],
        end: List[float],
        agent_class_path: str,
        extent: List[float] = [50.0, 50.0, 250.0],
    ) -> Dict[str, Any]:
        """Project endpoints and synchronously find a path for a real agent class CDO.

        Returns projection results, matching NavData, agent dimensions/capabilities,
        query status, complete/partial flags, path points, length, cost, and a failure
        reason when a complete path is unavailable.

        Example:
            find_navigation_path(
                level_path="/Game/Maps/Arena",
                start=[-500, 0, 100],
                end=[500, 0, 100],
                agent_class_path="/Game/Enemies/BP_Enemy")
        """
        start_error = _validate_vector("start", start)
        end_error = _validate_vector("end", end)
        extent_error = _validate_vector("extent", extent)
        if start_error or end_error or extent_error:
            return {
                "success": False,
                "message": start_error or end_error or extent_error,
            }
        return _send_navigation_command(
            "find_navigation_path",
            {
                "level_path": level_path,
                "start": [float(value) for value in start],
                "end": [float(value) for value in end],
                "agent_class_path": agent_class_path,
                "extent": [float(value) for value in extent],
            },
        )