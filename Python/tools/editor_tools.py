"""
Editor Tools for Unreal MCP.

This module provides tools for controlling the Unreal Editor viewport and other editor functionality.
"""

import logging
from typing import Dict, List, Any, Optional
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_editor_tools(mcp: FastMCP):
    """Register editor tools with the MCP server."""
    
    @mcp.tool()
    def get_actors_in_level(ctx: Context) -> Dict[str, Any]:
        """Get all actors in the current level, plus which level they came from.

        Returns a dict with:
          - actors: list of actors (name, label, class, location, rotation, scale)
          - level_name: short map name of the queried world
          - level_package_path: full package path, e.g. "/Game/Maps/MyLevel"
          - current_level: package name of the current level

        The level_* fields make it explicit which world was queried, so callers
        don't mistake a different open level for "missing actors".
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.warning("Failed to connect to Unreal Engine")
                return {"actors": [], "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("get_actors_in_level", {})
            
            if not response:
                logger.warning("No response from Unreal Engine")
                return {"actors": [], "message": "No response from Unreal Engine"}
            
            # Unwrap the {"status": "success", "result": {...}} envelope if present.
            result = response.get("result", response)
            actors = result.get("actors", [])
            logger.info(f"Found {len(actors)} actors in level {result.get('level_package_path', '?')}")
            return result
            
        except Exception as e:
            logger.error(f"Error getting actors: {e}")
            return {"actors": [], "message": str(e)}

    @mcp.tool()
    def find_actors_by_name(ctx: Context, pattern: str) -> List[str]:
        """Find actors by name pattern.

        Matches against both the internal object name (e.g. "StaticMeshActor_26")
        and the World Outliner display label (e.g. "Cube"), case-insensitive.
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.warning("Failed to connect to Unreal Engine")
                return []
                
            response = unreal.send_command("find_actors_by_name", {
                "pattern": pattern
            })
            
            if not response:
                return []
            
            # Response may be wrapped as {"status": "success", "result": {"actors": [...]}}
            # or returned flat as {"actors": [...]}. Handle both.
            result = response.get("result", response)
            return result.get("actors", [])
            
        except Exception as e:
            logger.error(f"Error finding actors: {e}")
            return []
    
    @mcp.tool()
    def spawn_actor(
        ctx: Context,
        name: str,
        type: str,
        location: List[float] = [0.0, 0.0, 0.0],
        rotation: List[float] = [0.0, 0.0, 0.0]
    ) -> Dict[str, Any]:
        """Create a new actor in the current level.
        
        Args:
            ctx: The MCP context
            name: The name to give the new actor (must be unique)
            type: The type of actor to create (e.g. StaticMeshActor, PointLight)
            location: The [x, y, z] world location to spawn at
            rotation: The [pitch, yaw, roll] rotation in degrees
            
        Returns:
            Dict containing the created actor's properties
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # Ensure all parameters are properly formatted
            params = {
                "name": name,
                "type": type.upper(),  # Make sure type is uppercase
                "location": location,
                "rotation": rotation
            }
            
            # Validate location and rotation formats
            for param_name in ["location", "rotation"]:
                param_value = params[param_name]
                if not isinstance(param_value, list) or len(param_value) != 3:
                    logger.error(f"Invalid {param_name} format: {param_value}. Must be a list of 3 float values.")
                    return {"success": False, "message": f"Invalid {param_name} format. Must be a list of 3 float values."}
                # Ensure all values are float
                params[param_name] = [float(val) for val in param_value]
            
            logger.info(f"Creating actor '{name}' of type '{type}' with params: {params}")
            response = unreal.send_command("spawn_actor", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            # Log the complete response for debugging
            logger.info(f"Actor creation response: {response}")
            
            # Handle error responses correctly
            if response.get("status") == "error":
                error_message = response.get("error", "Unknown error")
                logger.error(f"Error creating actor: {error_message}")
                return {"success": False, "message": error_message}
            
            return response
            
        except Exception as e:
            error_msg = f"Error creating actor: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def delete_actor(ctx: Context, name: str) -> Dict[str, Any]:
        """Delete an actor by name.

        'name' accepts either the internal object name or the World Outliner
        display label.
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("delete_actor", {
                "name": name
            })
            return response or {}
            
        except Exception as e:
            logger.error(f"Error deleting actor: {e}")
            return {}
    
    @mcp.tool()
    def set_actor_transform(
        ctx: Context,
        name: str,
        location: List[float]  = None,
        rotation: List[float]  = None,
        scale: List[float] = None
    ) -> Dict[str, Any]:
        """Set the transform of an actor.

        'name' accepts either the internal object name or the World Outliner
        display label.
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            params = {"name": name}
            if location is not None:
                params["location"] = location
            if rotation is not None:
                params["rotation"] = rotation
            if scale is not None:
                params["scale"] = scale
                
            response = unreal.send_command("set_actor_transform", params)
            return response or {}
            
        except Exception as e:
            logger.error(f"Error setting transform: {e}")
            return {}
    
    @mcp.tool()
    def get_actor_properties(ctx: Context, name: str) -> Dict[str, Any]:
        """Get all properties of an actor.

        'name' accepts either the internal object name (e.g. "StaticMeshActor_26")
        or the World Outliner display label (e.g. "Cube").
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("get_actor_properties", {
                "name": name
            })
            return response or {}
            
        except Exception as e:
            logger.error(f"Error getting properties: {e}")
            return {}

    @mcp.tool()
    def set_actor_property(
        ctx: Context,
        name: str,
        property_name: str,
        property_value,
    ) -> Dict[str, Any]:
        """
        Set a property on an actor.
        
        Args:
            name: Name of the actor
            property_name: Name of the property to set
            property_value: Value to set the property to
            
        Returns:
            Dict containing response from Unreal with operation status
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("set_actor_property", {
                "name": name,
                "property_name": property_name,
                "property_value": property_value
            })
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set actor property response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting actor property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # @mcp.tool() commented out because it's buggy
    def focus_viewport(
        ctx: Context,
        target: str = None,
        location: List[float] = None,
        distance: float = 1000.0,
        orientation: List[float] = None
    ) -> Dict[str, Any]:
        """
        Focus the viewport on a specific actor or location.
        
        Args:
            target: Name of the actor to focus on (if provided, location is ignored)
            location: [X, Y, Z] coordinates to focus on (used if target is None)
            distance: Distance from the target/location
            orientation: Optional [Pitch, Yaw, Roll] for the viewport camera
            
        Returns:
            Response from Unreal Engine
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            params = {}
            if target:
                params["target"] = target
            elif location:
                params["location"] = location
            
            if distance:
                params["distance"] = distance
                
            if orientation:
                params["orientation"] = orientation
                
            response = unreal.send_command("focus_viewport", params)
            return response or {}
            
        except Exception as e:
            logger.error(f"Error focusing viewport: {e}")
            return {"status": "error", "message": str(e)}

    @mcp.tool()
    def spawn_blueprint_actor(
        ctx: Context,
        blueprint_name: str,
        actor_name: str,
        location: List[float] = [0.0, 0.0, 0.0],
        rotation: List[float] = [0.0, 0.0, 0.0]
    ) -> Dict[str, Any]:
        """Spawn an actor from a Blueprint.
        
        Args:
            ctx: The MCP context
            blueprint_name: The Blueprint to spawn from. Accepts a full asset path
                (e.g. "/Game/Test/AI/BP_FT_PerceptionFullCycle"), a bare name under
                /Game/Blueprints/ (legacy), or any unique blueprint name in the project
                (resolved via an asset-registry search).
            actor_name: Name to give the spawned actor
            location: The [x, y, z] world location to spawn at
            rotation: The [pitch, yaw, roll] rotation in degrees
            
        Returns:
            Dict containing the spawned actor's properties
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # Ensure all parameters are properly formatted
            params = {
                "blueprint_name": blueprint_name,
                "actor_name": actor_name,
                "location": location or [0.0, 0.0, 0.0],
                "rotation": rotation or [0.0, 0.0, 0.0]
            }
            
            # Validate location and rotation formats
            for param_name in ["location", "rotation"]:
                param_value = params[param_name]
                if not isinstance(param_value, list) or len(param_value) != 3:
                    logger.error(f"Invalid {param_name} format: {param_value}. Must be a list of 3 float values.")
                    return {"success": False, "message": f"Invalid {param_name} format. Must be a list of 3 float values."}
                # Ensure all values are float
                params[param_name] = [float(val) for val in param_value]
            
            logger.info(f"Spawning blueprint actor with params: {params}")
            response = unreal.send_command("spawn_blueprint_actor", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Spawn blueprint actor response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error spawning blueprint actor: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_unsaved_changes(ctx: Context) -> Dict[str, Any]:
        """Check for unsaved changes in the Unreal Editor.

        Returns the count and names of all dirty (unsaved) content packages
        and map/level packages. Useful before closing the editor to ensure
        nothing is lost.

        Returns:
            Dict with total_unsaved (int), unsaved_content (list), unsaved_maps (list)

        Examples:
            get_unsaved_changes()
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_unsaved_changes", {})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Unsaved changes: {result.get('total_unsaved', 0)} total")
            return result

        except Exception as e:
            logger.error(f"Error checking unsaved changes: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def get_editor_logs(
        ctx: Context,
        count: int = 100,
        verbosity: str = "all",
        category: str = "",
        search: str = "",
        log_path: str = "",
        start_time: str = "",
        end_time: str = "",
        relative_seconds_ago: int = 0,
        pie_session_index: int = 0
    ) -> Dict[str, Any]:
        """Read Unreal Editor output log entries from the log file.

        Reads <ProjectDir>/Saved/Logs/<ProjectName>.log directly, so it works even when
        the editor is closed. Set UNREAL_PROJECT_LOG env var or pass log_path.

        At least one time parameter is REQUIRED (start_time / end_time /
        relative_seconds_ago / pie_session_index).

        Args:
            count: Max entries after all filters (default: 100).
            verbosity: Minimum level - "fatal"/"error"/"warning"/"display"/"log"/"verbose"/"all".
            category: Exact category match, e.g. "LogTemp".
            search: Case-insensitive REGEX. Supports alternation like "died|HandleDeath".
                Falls back to literal substring if not valid regex.
            log_path: Full .log path, else uses UNREAL_PROJECT_LOG env.
            start_time / end_time: Inclusive, UE format "YYYY.MM.DD-HH.MM.SS" or ISO.
            relative_seconds_ago: If > 0, sets start_time = now - N seconds.
            pie_session_index: -1 = most recent PIE session, -2 = one before, etc.
                0 = disabled. Overrides start/end_time.

        Returns logs list plus time_window; also pie_sessions_found when using PIE.

        Examples:
            get_editor_logs(pie_session_index=-1, verbosity="error")
            get_editor_logs(relative_seconds_ago=60, search="died|HandleDeath")
        """
        import os
        import re
        from datetime import datetime, timedelta

        # Validate: at least one time param required
        if not start_time and not end_time and not relative_seconds_ago and not pie_session_index:
            return {
                "success": False,
                "message": "At least one time parameter is required: start_time, end_time, relative_seconds_ago, or pie_session_index. "
                           "Examples: relative_seconds_ago=60, start_time='2026.06.13-02.05.00', pie_session_index=-1"
            }

        # Helper: normalize time string to UE format "YYYY.MM.DD-HH.MM.SS" for comparison
        def normalize_time(t: str) -> str:
            if not t:
                return ""
            # Try ISO format first: 2026-06-13T02:05:00
            t = t.strip()
            for fmt in ("%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S"):
                try:
                    dt = datetime.strptime(t, fmt)
                    return dt.strftime("%Y.%m.%d-%H.%M.%S")
                except ValueError:
                    pass
            # Already UE format or close enough — validate basic structure
            # Accept with or without milliseconds
            return t.split(":")[0] if ":" in t and len(t) > 23 else t

        # Handle relative_seconds_ago → overrides start_time
        if relative_seconds_ago > 0:
            dt_start = datetime.now() - timedelta(seconds=relative_seconds_ago)
            start_time = dt_start.strftime("%Y.%m.%d-%H.%M.%S")

        # Resolve log file path (needed before PIE scanning)
        path = log_path or os.environ.get("UNREAL_PROJECT_LOG", "")
        if not path:
            return {
                "success": False,
                "message": "No log file path provided. Set UNREAL_PROJECT_LOG env var or pass log_path parameter. "
                           "Example: log_path='D:/UnrealProjects/MyProject/Saved/Logs/MyProject.log'"
            }

        if not os.path.isfile(path):
            return {"success": False, "message": f"Log file not found: {path}"}

        try:
            # Read the file (handle UE's utf-8 with BOM or utf-16)
            try:
                with open(path, "r", encoding="utf-8-sig") as f:
                    all_lines = f.readlines()
            except UnicodeDecodeError:
                with open(path, "r", encoding="utf-16") as f:
                    all_lines = f.readlines()

            total_lines = len(all_lines)

            # Timestamp extraction pattern for PIE scanning and main loop
            ts_extract = re.compile(r"^\[([^\]]+)\]")

            # PIE session detection: scan for PIE start markers and compute time window
            pie_sessions_found = 0
            if pie_session_index != 0:
                # PIE start marker: each PIE session logs exactly one "Creating play world package"
                # line via LogPlayLevel. This is the canonical, single-per-session marker across
                # UE versions (avoids matching the multiple "PIE: ..." sub-lines within a session).
                pie_start_pattern = re.compile(
                    r"^\[([^\]]+)\]\[[\s\d]*\]LogPlayLevel:.*Creating play world package",
                    re.IGNORECASE
                )
                # Collect timestamps of all PIE session starts
                pie_starts = []
                for line in all_lines:
                    m = pie_start_pattern.match(line.rstrip("\n\r"))
                    if m:
                        ts_raw = m.group(1)
                        ts = ts_raw.split(":")[0] if ":" in ts_raw else ts_raw
                        pie_starts.append(ts)

                pie_sessions_found = len(pie_starts)
                if pie_sessions_found == 0:
                    return {
                        "success": False,
                        "message": "No PIE sessions found in the log file. "
                                   "Look for LogPlayLevel entries indicating Play-In-Editor."
                    }

                # pie_session_index is negative: -1 = last, -2 = second to last
                idx = pie_session_index
                if idx < 0:
                    idx = pie_sessions_found + idx
                if idx < 0 or idx >= pie_sessions_found:
                    return {
                        "success": False,
                        "message": f"PIE session index {pie_session_index} out of range. "
                                   f"Found {pie_sessions_found} PIE session(s). Use -1 for most recent."
                    }

                start_time = pie_starts[idx]
                # End time: start of next PIE session, or unbounded (to EOF)
                if idx + 1 < pie_sessions_found:
                    end_time = pie_starts[idx + 1]
                else:
                    end_time = ""

            start_cmp = normalize_time(start_time)
            end_cmp = normalize_time(end_time)

            # UE log line pattern: [YYYY.MM.DD-HH.MM.SS:mmm][frame]Category: Verbosity: Message
            # or: [YYYY.MM.DD-HH.MM.SS:mmm][frame]Category: Message (for Display verbosity)
            log_pattern = re.compile(
                r"^\[([^\]]+)\]\[[\s\d]*\](\w+):\s*(?:(Fatal|Error|Warning|Display|Log|Verbose|VeryVerbose):\s*)?(.*)"
            )

            # Verbosity ranking (lower = more severe)
            verbosity_rank = {
                "fatal": 1, "error": 2, "warning": 3,
                "display": 4, "log": 5, "verbose": 6, "veryverbose": 7, "all": 99
            }
            min_rank = verbosity_rank.get(verbosity.lower(), 99)

            category_lower = category.lower() if category else ""

            # Compile search as a case-insensitive regex; fall back to literal substring on bad pattern
            search_regex = None
            search_lower = ""
            search_is_regex = False
            if search:
                try:
                    search_regex = re.compile(search, re.IGNORECASE)
                    search_is_regex = True
                except re.error:
                    search_lower = search.lower()

            # Track how many entries fell inside the time window before search/category/verbosity
            in_window_count = 0

            # Parse from end to start, collect up to count matching entries
            results = []
            for line in reversed(all_lines):
                line = line.rstrip("\n\r")
                m = log_pattern.match(line)
                if not m:
                    continue

                timestamp = m.group(1)
                cat = m.group(2)
                verb = m.group(3) or "Display"
                msg = m.group(4)

                # Extract comparable time (strip milliseconds): "2026.06.13-02.06.20:987" → "2026.06.13-02.06.20"
                ts_cmp = timestamp.split(":")[0] if ":" in timestamp else timestamp

                # Filter by time window (closed interval)
                # Since we iterate in reverse (newest first), lines older than start can break early
                if start_cmp and ts_cmp < start_cmp:
                    break
                if end_cmp and ts_cmp > end_cmp:
                    continue

                in_window_count += 1

                # Filter by verbosity
                entry_rank = verbosity_rank.get(verb.lower(), 5)
                if min_rank != 99 and entry_rank > min_rank:
                    continue

                # Filter by category
                if category_lower and cat.lower() != category_lower:
                    continue

                # Filter by search text (regex if valid, else literal substring)
                if search:
                    if search_is_regex:
                        if not search_regex.search(msg):
                            continue
                    elif search_lower not in msg.lower():
                        continue

                results.append({
                    "timestamp": timestamp,
                    "category": cat,
                    "verbosity": verb,
                    "message": msg
                })

                if len(results) >= count:
                    break

            # Reverse so oldest first, newest last
            results.reverse()

            logger.info(f"Read {len(results)} log entries from {path} (total lines: {total_lines})")
            result = {
                "total_lines": total_lines,
                "returned": len(results),
                "log_file": path,
                "time_window": {
                    "start": start_cmp or "(unbounded)",
                    "end": end_cmp or "(unbounded)"
                },
                "logs": results
            }
            if pie_session_index != 0:
                result["pie_sessions_found"] = pie_sessions_found
                result["pie_session_used"] = pie_session_index
            # Warn when the time window had entries but downstream filters removed them all
            if len(results) == 0 and in_window_count > 0:
                applied = []
                if search:
                    applied.append(f"search='{search}'" + ("" if search_is_regex else " (invalid regex, used literal match)"))
                if category:
                    applied.append(f"category='{category}'")
                if verbosity and verbosity.lower() != "all":
                    applied.append(f"verbosity='{verbosity}'")
                result["warning"] = (
                    f"{in_window_count} log line(s) matched the time window, but all were removed by "
                    f"filters [{', '.join(applied)}]. Note: 'search' is a REGEX (use 'a|b|c' for alternation). "
                    f"Try relaxing the filters."
                )
            return result

        except Exception as e:
            error_msg = f"Error reading log file: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def save_asset(
        ctx: Context,
        asset_path: str
    ) -> Dict[str, Any]:
        """Save an asset to disk.

        Persists any in-memory changes (e.g. after creating or modifying an asset via MCP)
        so they survive an editor restart.

        Args:
            ctx: The MCP context
            asset_path: The asset path to save, e.g. "/Game/VFX/NS_Explosion.NS_Explosion"
                        or "/Game/UI/WBP_MyWidget"

        Returns:
            Dict with success message or error

        Examples:
            save_asset(asset_path="/Game/VFX/NS_Explosion.NS_Explosion")
            save_asset(asset_path="/Game/UI/WBP_HUD")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("save_asset", {"asset_path": asset_path})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Asset saved: {asset_path}")
            return result

        except Exception as e:
            logger.error(f"Error saving asset: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def close_editor(
        ctx: Context,
        save_all: bool = True
    ) -> Dict[str, Any]:
        """Gracefully close the Unreal Editor.

        Optionally saves all unsaved assets and maps before requesting the editor
        to shut down. Much safer than killing the process, as it allows the engine
        to clean up properly.

        Args:
            save_all: If True (default), saves all dirty packages before closing.
                      Set to False to close without saving.

        Returns:
            Dict with closing status, saved_count, and any failed_saves

        Examples:
            close_editor()
            close_editor(save_all=False)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("close_editor", {"save_all": save_all})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Editor close requested (save_all={save_all}), saved {result.get('saved_count', 0)} packages")
            return result

        except Exception as e:
            logger.error(f"Error closing editor: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def open_asset(
        ctx: Context,
        asset_path: str
    ) -> Dict[str, Any]:
        """Open an asset in its default editor window.

        Works with any asset type: Blueprint, WidgetBlueprint, Material, AnimBlueprint,
        AnimSequence, AnimMontage, BlendSpace, DataAsset, NiagaraSystem, Skeleton,
        SkeletalMesh, Texture, SoundCue, etc.

        Args:
            asset_path: Full asset path, e.g. "/Game/UI/WBP_MechHUD" or
                "/Game/Enemies/Blueprints/BP_Enemy"

        Returns:
            Dict with success, asset_path, and asset_class

        Examples:
            open_asset(asset_path="/Game/UI/WBP_MechHUD")
            open_asset(asset_path="/Game/Materials/M_Base")
            open_asset(asset_path="/Game/Player/Animations/ABP_Player")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("open_asset", {"asset_path": asset_path})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Opened asset editor: {asset_path}")
            return result

        except Exception as e:
            logger.error(f"Error opening asset: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def open_level(
        ctx: Context,
        level_path: str,
        save_dirty: bool = False
    ) -> Dict[str, Any]:
        """Open (load) a level/map into the editor viewport.

        Unlike open_asset (which opens an asset-editor window), this loads a .umap
        into the editor world, replacing the currently open level.

        Args:
            level_path: Full package path to the level, e.g.
                "/Game/Maps/cyberpunk_start1" (the ".umap" suffix and any object
                suffix are optional).
            save_dirty: If True, save any unsaved packages before switching (no prompt).
                Defaults to False.

        Returns:
            Dict with success, level_package_path, and level_name

        Examples:
            open_level(level_path="/Game/Maps/cyberpunk_start1")
            open_level(level_path="/Game/Maps/FunctionalTest/cyberpunk_Test_DogChasePlayer", save_dirty=True)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("open_level", {
                "level_path": level_path,
                "save_dirty": save_dirty
            })

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Opened level: {level_path}")
            return result

        except Exception as e:
            logger.error(f"Error opening level: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def save_level(ctx: Context) -> Dict[str, Any]:
        """Save the currently open level/map to disk.

        Persists the current editor world (the .umap) including any actors added,
        moved, or modified via MCP. Fails if the current level is untitled/unsaved
        (use the editor's "Save As" first in that case).

        After saving, the Asset Registry is re-scanned for this map so that updated
        tags (e.g. newly added Functional Test actors) are picked up. If a new
        Functional Test was added, click "Refresh Tests" in Session Frontend to see
        it — no editor restart required.

        Returns:
            Dict with success, level_package_path, asset_registry_rescanned, message

        Examples:
            save_level()
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("save_level", {})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info("Saved current level")
            return result

        except Exception as e:
            logger.error(f"Error saving level: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def create_level(
        ctx: Context,
        level_path: str,
        template_path: str = "",
        partitioned: bool = False
    ) -> Dict[str, Any]:
        """Create a new level/map at the given content path, optionally from a template.

        The newly created level is saved to disk and loaded into the editor. Fails if
        a level already exists at level_path (use open_level to load an existing one).

        Args:
            level_path: Content path for the new level, e.g.
                "/Game/Maps/MyNewLevel" (no extension, no object suffix).
            template_path: Optional full path to an existing .umap to use as a template,
                e.g. "/Engine/Maps/Templates/OpenWorld" or "/Game/Maps/MyTemplate".
                When provided, the new level is a copy of the template.
            partitioned: If True (and no template), create as a World Partition map.
                Ignored when template_path is given (inherited from the template).

        Returns:
            Dict with success, level_package_path, level_name, partitioned,
            and template_path (if a template was used).

        Examples:
            create_level(level_path="/Game/Maps/MySandbox")
            create_level(level_path="/Game/Maps/MyPartitionedTest", partitioned=True)
            create_level(level_path="/Game/Maps/FT_MyTest",
                         template_path="/Game/Maps/FunctionalTest/FT_Template")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "level_path": level_path,
                "partitioned": partitioned,
            }
            if template_path:
                params["template_path"] = template_path

            response = unreal.send_command("create_level", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Created level: {level_path}")
            return result

        except Exception as e:
            logger.error(f"Error creating level: {e}")
            return {"success": False, "message": str(e)}

    logger.info("Editor tools registered successfully")
