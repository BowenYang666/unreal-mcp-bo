"""
Niagara VFX Tools for Unreal MCP.

This module provides tools for listing, inspecting, creating, and configuring
Niagara particle systems and their User-exposed parameters.
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_niagara_tools(mcp: FastMCP):
    """Register Niagara tools with the MCP server."""

    def _map_asset_path(asset_full_path: str, name_key: str = "system_name") -> dict:
        """Route asset_full_path to the correct C++ JSON key ('path' or name_key)."""
        if "/" in asset_full_path:
            return {"path": asset_full_path}
        return {name_key: asset_full_path}

    @mcp.tool()
    def list_niagara_systems(
        ctx: Context,
        path: str = "/Game",
        include_engine_content: bool = False,
        name_filter: str = ""
    ) -> Dict[str, Any]:
        """List all Niagara system assets in the project.

        Args:
            ctx: The MCP context
            path: Asset path to search in (default: "/Game")
            include_engine_content: Include engine built-in Niagara systems (default: False)
            name_filter: Only include systems whose name contains this string (case-insensitive)

        Returns:
            Dict with "count" (int) and "systems" (list of dicts with name, path, package_path,
            is_engine_content, emitter_count)

        Examples:
            list_niagara_systems()
            list_niagara_systems(path="/Game/VFX")
            list_niagara_systems(include_engine_content=True, name_filter="Explosion")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {"path": path, "include_engine_content": include_engine_content}
            if name_filter:
                params["name_filter"] = name_filter

            response = unreal.send_command("list_niagara_systems", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Found {result.get('count', 0)} Niagara systems")
            return result

        except Exception as e:
            logger.error(f"Error listing Niagara systems: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def read_niagara_system(
        ctx: Context,
        asset_full_path: str
    ) -> Dict[str, Any]:
        """Read a Niagara system: emitters, User parameters, and per-emitter renderers.

        asset_full_path: full path ("/Game/VFX/NS_Explosion") or short name.
        Returns name, path, emitters (name, sim_target, local_space, module stacks) and
        each emitter's renderers with fully reflected properties (Sprite/Mesh/Ribbon/Light:
        material, sub_image_size, alignment, facing_mode, sort_mode, etc.).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = _map_asset_path(asset_full_path, "name")

            response = unreal.send_command("read_niagara_system", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Read Niagara system: {result.get('name', '?')} with {result.get('emitter_count', 0)} emitters")
            return result

        except Exception as e:
            logger.error(f"Error reading Niagara system: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_niagara_parameter(
        ctx: Context,
        actor_name: str,
        parameter_name: str,
        parameter_type: str,
        value: Any = None
    ) -> Dict[str, Any]:
        """Set a User-exposed parameter on a Niagara component in the level.

        The actor must have a NiagaraComponent (e.g. a NiagaraActor placed in the level).
        Parameter names must include the "User." prefix (matches the Niagara editor convention).

        Args:
            ctx: The MCP context
            actor_name: Name of the actor in the level that has a NiagaraComponent
            parameter_name: Full parameter name including namespace, e.g. "User.Color", "User.Speed"
            parameter_type: One of: "float", "int", "bool", "vec2", "vec3", "vec4", "color"
            value: The value to set. Format depends on parameter_type:
                - float: a number, e.g. 1.5
                - int: a number, e.g. 10
                - bool: true/false
                - vec2: [x, y]
                - vec3: [x, y, z]
                - vec4: [x, y, z, w]
                - color: {"r": 1.0, "g": 0.5, "b": 0.0, "a": 1.0} or [r, g, b, a]

        Returns:
            Dict with success message or error

        Examples:
            set_niagara_parameter(actor_name="NiagaraActor_1", parameter_name="User.Speed",
                                  parameter_type="float", value=500.0)
            set_niagara_parameter(actor_name="NiagaraActor_1", parameter_name="User.Color",
                                  parameter_type="color", value={"r": 1, "g": 0, "b": 0, "a": 1})
        """
        from unreal_mcp_server import get_unreal_connection

        if value is None:
            return {"success": False, "message": "'value' parameter is required"}

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "actor_name": actor_name,
                "parameter_name": parameter_name,
                "parameter_type": parameter_type,
                "value": value,
            }

            response = unreal.send_command("set_niagara_parameter", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            return result

        except Exception as e:
            logger.error(f"Error setting Niagara parameter: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def get_niagara_parameters(
        ctx: Context,
        actor_name: str
    ) -> Dict[str, Any]:
        """Get all User-exposed parameter values from a Niagara component in the level.

        Reads the current parameter names, types, and override values from a placed
        NiagaraComponent. This helps understand what can be tweaked on a specific effect.

        Args:
            ctx: The MCP context
            actor_name: Name of the actor in the level that has a NiagaraComponent

        Returns:
            Dict with actor_name, system_name, system_path, is_active, and
            parameters list (name, type, value)

        Examples:
            get_niagara_parameters(actor_name="NiagaraActor_1")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_niagara_parameters", {"actor_name": actor_name})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Got {result.get('parameter_count', 0)} Niagara parameters from '{actor_name}'")
            return result

        except Exception as e:
            logger.error(f"Error getting Niagara parameters: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def create_niagara_system(
        ctx: Context,
        asset_full_path: str,
        template_system_path: str = ""
    ) -> Dict[str, Any]:
        """Create a new Niagara system asset.

        Can create an empty system or duplicate an existing one as a starting point.
        Duplication is recommended — copy an existing system then modify its parameters.

        Args:
            ctx: The MCP context
            asset_full_path: Full path for the new asset, e.g. "/Game/VFX/NS_Explosion"
            template_system_path: Optional path to an existing Niagara system to duplicate.
                                  If empty, creates a blank system.
                                  Example: "/Game/VFX/NS_Fire.NS_Fire"

        Returns:
            Dict with name, path, emitter_count of the created system

        Examples:
            create_niagara_system(asset_full_path="/Game/VFX/NS_MyExplosion")
            create_niagara_system(asset_full_path="/Game/VFX/NS_MyFire",
                                  template_system_path="/Game/VFX/NS_Fire.NS_Fire")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {"asset_path": asset_full_path}
            if template_system_path:
                params["template_system_path"] = template_system_path

            response = unreal.send_command("create_niagara_system", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Created Niagara system: {result.get('path', '?')}")
            return result

        except Exception as e:
            logger.error(f"Error creating Niagara system: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_niagara_rapid_parameter(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        parameter_name: str,
        value: Any,
        script_type: str
    ) -> Dict[str, Any]:
        """Set a rapid-iteration parameter on a Niagara emitter script.

        This modifies the asset-level parameter value (the value you see in the Niagara editor),
        NOT a runtime override on a placed actor. After modifying, the system is automatically
        recompiled and saved.

        IMPORTANT: script_type must match the module's stage.
        - "spawn": Particle Spawn modules (InitializeParticle, AddVelocity, ShapeLocation)
        - "update": Particle Update modules (GravityForce, Drag, ScaleColor, SolveForcesAndVelocity)
        - "emitter_spawn": Emitter Spawn modules (EmitterState)
        - "emitter_update": Emitter Update modules (SpawnBurstInstantaneous, SpawnRate, EmitterState)

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_Explosion_Cannon") or
                short asset name (e.g. "NS_Explosion_Cannon").
            emitter_name: Name of the emitter within the system (e.g. "Smoke")
            parameter_name: Full or partial parameter name. Partial matching is supported,
                e.g. "InitializeParticle.Lifetime" matches "Constants.Smoke.InitializeParticle.Lifetime"
            value: The new value. Type depends on parameter:
                - float/int: a number (e.g. 500)
                - bool: true/false
                - vec2: [x, y]
                - vec3: [x, y, z]
                - vec4: [x, y, z, w]
                - color: {"r": 1.0, "g": 0.5, "b": 0.0, "a": 1.0}
            script_type: REQUIRED. One of "spawn", "update", "emitter_spawn", "emitter_update".
                Using the wrong stage will silently fail to change the visible value.

        Returns:
            Dict with old_value, new_value, parameter name, type, etc.

        Examples:
            set_niagara_rapid_parameter(asset_full_path="/Game/VFX/NS_Explosion_Cannon",
                emitter_name="Smoke", parameter_name="AddVelocity.Velocity Speed",
                value=500, script_type="spawn")
            set_niagara_rapid_parameter(asset_full_path="/Game/VFX/NS_Explosion_Cannon",
                emitter_name="Flare", parameter_name="InitializeParticle.Color",
                value={"r": 5.0, "g": 2.0, "b": 0.5, "a": 1.0}, script_type="spawn")
            set_niagara_rapid_parameter(asset_full_path="/Game/Effects/NS_Tower",
                emitter_name="Burst", parameter_name="SpawnBurst_Instantaneous.Spawn Count",
                value=15, script_type="emitter_update")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "parameter_name": parameter_name,
                "value": value,
                "script_type": script_type,
            }

            response = unreal.send_command("set_niagara_rapid_parameter", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Set rapid parameter: {result.get('parameter', '?')} = {result.get('new_value', '?')}")
            return result

        except Exception as e:
            logger.error(f"Error setting rapid parameter: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def modify_emitter_properties(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        enabled: bool = None,
        local_space: bool = None,
        determinism: bool = None,
        random_seed: int = None,
        sim_target: str = None
    ) -> Dict[str, Any]:
        """Modify emitter-level properties in a Niagara system asset.

        Only specified properties will be changed; omitted ones remain unchanged.
        The system is automatically recompiled after modifications.
        Use save_asset to persist changes to disk.

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_MyExplosion") or
                short asset name (e.g. "NS_MyExplosion").
            emitter_name: Name of the emitter to modify (e.g. "SimpleSpriteBurst")
            enabled: Enable or disable the emitter (default: None = no change)
            local_space: True for local-space simulation, False for world-space (default: None)
            determinism: True for deterministic RNG (default: None)
            random_seed: Integer seed for deterministic mode (default: None)
            sim_target: "CPU" or "GPU" simulation target (default: None)

        Returns:
            Dict with status, system, emitter, changes_count, and per-property old/new values

        Examples:
            modify_emitter_properties(asset_full_path="/Game/VFX/NS_MyExplosion",
                emitter_name="SimpleSpriteBurst", local_space=True)
            modify_emitter_properties(asset_full_path="/Game/VFX/NS_MyExplosion",
                emitter_name="Smoke", sim_target="GPU", determinism=True, random_seed=42)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
            }
            if enabled is not None:
                params["enabled"] = enabled
            if local_space is not None:
                params["local_space"] = local_space
            if determinism is not None:
                params["determinism"] = determinism
            if random_seed is not None:
                params["random_seed"] = random_seed
            if sim_target is not None:
                params["sim_target"] = sim_target

            response = unreal.send_command("modify_emitter_properties", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Modified emitter properties: {result.get('emitter', '?')} ({result.get('changes_count', 0)} changes)")
            return result

        except Exception as e:
            logger.error(f"Error modifying emitter properties: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_emitter_to_system(
        ctx: Context,
        asset_full_path: str,
        source_emitter_name: str = "",
        new_emitter_name: str = "",
        source_asset_full_path: str = "",
        template_name: str = ""
    ) -> Dict[str, Any]:
        """Add an emitter to a Niagara system from an engine template, another system, or by duplicating within the same system.

        Three modes:
        1. template_name: Add from engine built-in template (e.g. "Fountain", "SimpleSpriteBurst", "Empty")
        2. source_asset_full_path + source_emitter_name: Copy emitter from another system
        3. source_emitter_name only: Duplicate emitter within the same system

        Use list_niagara_emitter_templates() to see available templates.
        The system is automatically recompiled after the change.

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path of the target system (e.g. "/Game/VFX/NS_MyExplosion")
                or short asset name (e.g. "NS_MyExplosion").
            source_emitter_name: Name of emitter to copy/duplicate (modes 2 & 3)
            new_emitter_name: Name for the new emitter (default: same as source/template)
            source_asset_full_path: Full asset path or name of source system to copy from (mode 2)
            template_name: Engine template name (mode 1, e.g. "Fountain", "SimpleSpriteBurst")

        Returns:
            Dict with status, system, new_emitter name, emitter_count

        Examples:
            add_emitter_to_system(asset_full_path="/Game/VFX/NS_MyExplosion",
                template_name="Fountain", new_emitter_name="MyFountain")
            add_emitter_to_system(asset_full_path="/Game/VFX/NS_MyExplosion",
                source_emitter_name="Flare",
                source_asset_full_path="/Game/VFX/NS_Explosion_Cannon")
            add_emitter_to_system(asset_full_path="/Game/VFX/NS_MyExplosion",
                source_emitter_name="SimpleSpriteBurst", new_emitter_name="SpriteCopy")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
            }
            if template_name:
                params["template_name"] = template_name
            if source_emitter_name:
                params["source_emitter_name"] = source_emitter_name
            if new_emitter_name:
                params["new_emitter_name"] = new_emitter_name
            if source_asset_full_path:
                source_params = _map_asset_path(source_asset_full_path, "source_system_name")
                if "path" in source_params:
                    params["source_system_path"] = source_params["path"]
                else:
                    params["source_system_name"] = source_params["source_system_name"]

            response = unreal.send_command("add_emitter_to_system", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Added emitter: {result.get('new_emitter', '?')} to {result.get('system', '?')}")
            return result

        except Exception as e:
            logger.error(f"Error adding emitter: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def remove_emitter_from_system(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str
    ) -> Dict[str, Any]:
        """Remove an emitter from a Niagara system by name.

        The system is automatically recompiled after the removal.
        Use save_asset to persist the change to disk.

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_MyExplosion") or
                short asset name (e.g. "NS_MyExplosion").
            emitter_name: Name of the emitter to remove (e.g. "SimpleSpriteBurst")

        Returns:
            Dict with status, system, removed_emitter, old/new emitter counts

        Examples:
            remove_emitter_from_system(asset_full_path="/Game/VFX/NS_MyExplosion",
                emitter_name="SimpleSpriteBurst")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
            }

            response = unreal.send_command("remove_emitter_from_system", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Removed emitter: {emitter_name} from {result.get('system', '?')}")
            return result

        except Exception as e:
            logger.error(f"Error removing emitter: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def list_niagara_emitter_templates(
        ctx: Context,
        category: str = ""
    ) -> Dict[str, Any]:
        """List available Niagara emitter templates from the engine.

        Returns built-in templates that can be used with add_emitter_to_system(template_name=...).
        Categories: Emitters (common presets), BehaviorExamples (advanced demos),
        Systems (system-level templates), CascadeConversion (legacy conversion).

        Args:
            ctx: The MCP context
            category: Filter by category (e.g. "Emitters"). Empty returns all.

        Returns:
            Dict with templates list containing name, category, path

        Examples:
            list_niagara_emitter_templates()
            list_niagara_emitter_templates(category="Emitters")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {}
            if category:
                params["category"] = category

            response = unreal.send_command("list_niagara_emitter_templates", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Found {result.get('count', 0)} emitter templates")
            return result

        except Exception as e:
            logger.error(f"Error listing emitter templates: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_module_to_emitter(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str = "",
        module_name: str = "",
        script_type: str = "update",
        index: int = -1
    ) -> Dict[str, Any]:
        """Add a module script to an emitter's spawn or update stack.

        Adds a Niagara module to the specified emitter stage. Use read_niagara_system
        to see existing modules.

        script_type must match the module's scope, or Niagara will fail to compile
        ("Cannot Set external constant Emitter.Module.*"):
          - "spawn"          — Particle Spawn (e.g. InitializeParticle, ShapeLocation, AddVelocity)
          - "update"         — Particle Update (e.g. GravityForce, Drag, ScaleColor, SolveForcesAndVelocity)
          - "emitter_spawn"  — Emitter Spawn (e.g. EmitterState initial setup)
          - "emitter_update" — Emitter Update (e.g. SpawnRate, SpawnPerUnit,
                               SpawnBurst_Instantaneous, EmitterState, EmitterLifeCycle)

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_MyExplosion") or
                short asset name (e.g. "NS_MyExplosion").
            emitter_name: Name of the emitter within the system
            module_name: Name of the module script to add (e.g. "GravityForce", "SpawnRate")
            script_type: Target stage. One of "spawn", "update", "emitter_spawn",
                "emitter_update" (default: "update")
            index: Position in the stack (-1 = append to end)

        Returns:
            Dict with status, module name, and placement info

        Examples:
            add_module_to_emitter(asset_full_path="/Game/VFX/NS_MyExplosion",
                emitter_name="Burst", module_name="GravityForce", script_type="update")
            add_module_to_emitter(asset_full_path="/Game/VFX/NS_MyRibbon",
                emitter_name="TrailRibbon", module_name="SpawnRate", script_type="emitter_update")
            add_module_to_emitter(asset_full_path="/Game/VFX/NS_MyExplosion",
                emitter_name="Burst", module_name="SpawnBurst_Instantaneous", script_type="emitter_update")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {**_map_asset_path(asset_full_path), "emitter_name": emitter_name, "module_name": module_name, "script_type": script_type}
            if index >= 0:
                params["index"] = index

            response = unreal.send_command("add_module_to_emitter", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Added module {module_name} to {emitter_name}/{script_type}")
            return result

        except Exception as e:
            logger.error(f"Error adding module: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def remove_module_from_emitter(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str = "",
        module_name: str = "",
        script_type: str = "update"
    ) -> Dict[str, Any]:
        """Remove a module from an emitter's spawn or update stack.

        Removes a Niagara module by name from the specified emitter stage.
        Use read_niagara_system to see module names.

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_MyExplosion") or
                short asset name (e.g. "NS_MyExplosion").
            emitter_name: Name of the emitter within the system
            module_name: Name of the module to remove (display name or script name)
            script_type: Target stage. One of "spawn", "update", "emitter_spawn",
                "emitter_update" (default: "update")

        Returns:
            Dict with status and removed module info

        Examples:
            remove_module_from_emitter(asset_full_path="/Game/VFX/NS_MyExplosion", emitter_name="Burst", module_name="GravityForce", script_type="update")
            remove_module_from_emitter(asset_full_path="/Game/VFX/NS_MyRibbon", emitter_name="Trail", module_name="SpawnRate", script_type="emitter_update")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {**_map_asset_path(asset_full_path), "emitter_name": emitter_name, "module_name": module_name, "script_type": script_type}

            response = unreal.send_command("remove_module_from_emitter", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Removed module {module_name} from {emitter_name}/{script_type}")
            return result

        except Exception as e:
            logger.error(f"Error removing module: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_niagara_renderer_property(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        property_name: str,
        property_value,
        renderer_type: str = "",
        renderer_index: int = 0
    ) -> Dict[str, Any]:
        """Set a property on an emitter's renderer (e.g. Material, SubImageSize).

        Renderer properties are NOT rapid-iteration parameters, so they can't be set
        with set_niagara_rapid_parameter. This sets them directly via reflection.

        Common properties (Sprite renderer):
          - "Material": asset path string, e.g. "/Game/VFX/M_Spark.M_Spark"
          - "SubImageSize": struct string "(X=8,Y=8)" for an 8x8 SubUV sheet
          - "bSubImageBlend": true/false (blend between SubUV frames)
          - "Alignment", "FacingMode": enum name strings
        Mesh renderer uses "bOverrideMaterials" + the OverrideMaterials array; for simple
        cases set the material on the mesh asset itself.

        Use read_niagara_system to see each emitter's renderer types.

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_MyExplosion") or short name.
            emitter_name: Name of the emitter within the system.
            property_name: Name of the renderer property to set (e.g. "Material", "SubImageSize").
            property_value: New value. Asset paths for Material; "(X=8,Y=8)" for SubImageSize;
                true/false for bools; enum name string for enums.
            renderer_type: Optional class-name substring to pick the renderer
                (e.g. "Sprite", "Mesh", "Ribbon", "Light"). If empty, uses renderer_index.
            renderer_index: Index of the renderer when renderer_type is not given (default: 0).

        Returns:
            Dict with status, system, emitter, renderer_index, renderer_type, property

        Examples:
            set_niagara_renderer_property(asset_full_path="/Game/VFX/NS_Fire", emitter_name="Flames",
                property_name="Material", property_value="/Game/VFX/M_Fire.M_Fire", renderer_type="Sprite")
            set_niagara_renderer_property(asset_full_path="/Game/VFX/NS_Fire", emitter_name="Flames",
                property_name="SubImageSize", property_value="(X=8,Y=8)", renderer_type="Sprite")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "property_name": property_name,
                "property_value": property_value,
                "renderer_index": renderer_index,
            }
            if renderer_type:
                params["renderer_type"] = renderer_type

            response = unreal.send_command("set_niagara_renderer_property", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Set renderer property {property_name} on {emitter_name}")
            return result

        except Exception as e:
            logger.error(f"Error setting renderer property: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def list_module_inputs(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        module_name: str,
        script_type: str = "spawn"
    ) -> Dict[str, Any]:
        """List a module's input pins with type and value mode (discovery for enable_module_input).

        script_type: "spawn"/"update"/"emitter_spawn"/"emitter_update". Each input returns
        name, type, current_mode (Local=has value / Default=not exposed), can_enable_local,
        rapid_parameter_name, value.

        Example: list_module_inputs("NS_Fire", "Fire", "InitializeParticle", "spawn")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "module_name": module_name,
                "script_type": script_type,
            }

            response = unreal.send_command("list_module_inputs", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Listed {result.get('input_count', 0)} inputs of module {module_name}")
            return result

        except Exception as e:
            logger.error(f"Error listing module inputs: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def enable_module_input(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        module_name: str,
        input_name: str,
        script_type: str = "spawn",
        initial_value: Any = None
    ) -> Dict[str, Any]:
        """Enable a module input pin as a Local Value (creates a rapid-iteration parameter).

        Like choosing "Local Value" in the editor dropdown, so set_niagara_rapid_parameter can
        then write it. Only constant types (float/int/bool/vec2/vec3/vec4/quat/color); not data
        interfaces/objects. Run list_module_inputs first. initial_value optionally assigns a value
        (color as {"r","g","b","a"}, vectors as arrays). For pins gated by a "Mode" dropdown, also
        call set_module_static_switch or the value is ignored.

        Example: enable_module_input("NS_Fire","Fire","InitializeParticle","Color",
            initial_value={"r":5,"g":1.5,"b":0.2,"a":1})
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "module_name": module_name,
                "input_name": input_name,
                "script_type": script_type,
            }
            if initial_value is not None:
                params["initial_value"] = initial_value

            response = unreal.send_command("enable_module_input", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Enabled module input {module_name}.{input_name} -> {result.get('parameter_name', '?')}")
            return result

        except Exception as e:
            logger.error(f"Error enabling module input: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def list_module_static_switches(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        module_name: str,
        script_type: str = "spawn"
    ) -> Dict[str, Any]:
        """List a module's static-switch "Mode" selectors with current + allowed values.

        Modules like InitializeParticle gate value pins behind a Mode switch; if Mode is "Unset"
        the compiler ignores enabled pins. Discover switches here, then set_module_static_switch.
        Each switch returns name, current_value, allowed_values (UI names, e.g.
        Unset/Direct Set/Random Range).

        Example: list_module_static_switches("NS_Fire","Fire","InitializeParticle","spawn")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "module_name": module_name,
                "script_type": script_type,
            }

            response = unreal.send_command("list_module_static_switches", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Listed {result.get('switch_count', 0)} static switches of module {module_name}")
            return result

        except Exception as e:
            logger.error(f"Error listing module static switches: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_module_static_switch(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        module_name: str,
        switch_name: str,
        value: str,
        script_type: str = "spawn"
    ) -> Dict[str, Any]:
        """Set a module static-switch "Mode" selector (e.g. "Sprite Rotation Mode" = "Random Range").

        The missing layer above enable_module_input: flips the Mode so the compiler uses the value
        pins (otherwise they're ignored and the emitter shows a warning). value is the UI name from
        list_module_static_switches (case-insensitive), or "true"/"false" for bool switches.
        Returns previous_value, new_value, resulting_visible_pins.

        Example: set_module_static_switch("NS_Fire","Fire","InitializeParticle",
            "Sprite Rotation Mode","Random Range")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "module_name": module_name,
                "switch_name": switch_name,
                "value": value,
                "script_type": script_type,
            }

            response = unreal.send_command("set_module_static_switch", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Set static switch {module_name}.{switch_name} = {result.get('new_value', '?')}")
            return result

        except Exception as e:
            logger.error(f"Error setting module static switch: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def bind_module_input_datainterface(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        module_name: str,
        input_name: str,
        binding_kind: str = "Renderer",
        renderer_type: str = "",
        renderer_index: int = 0,
        asset_path: str = "",
        script_type: str = "spawn"
    ) -> Dict[str, Any]:
        """Bind a DataInterface-typed module input (e.g. SubUVAnimation "Sprite Renderer") to an emitter renderer or asset.

        DataInterface inputs (Sprite/Mesh Renderer info, Curve, StaticMesh...) are object bindings,
        not Local values — enable_module_input can't set them. Use list_module_inputs to find inputs
        with can_bind_datainterface=true. binding_kind: "Renderer" (bind to the emitter's renderer of
        renderer_type at renderer_index — the common SubUVAnimation case) or "Asset" (bind asset_path).
        Rebinding an already-bound input is not supported yet.

        Example: bind_module_input_datainterface("NS_Fire","Fire","SubUVAnimation","Sprite Renderer",
            binding_kind="Renderer", renderer_type="Sprite", script_type="update")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "module_name": module_name,
                "input_name": input_name,
                "binding_kind": binding_kind,
                "renderer_index": renderer_index,
                "script_type": script_type,
            }
            if renderer_type:
                params["renderer_type"] = renderer_type
            if asset_path:
                params["asset_path"] = asset_path

            response = unreal.send_command("bind_module_input_datainterface", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Bound DI input {module_name}.{input_name} -> {result.get('resolved_binding', '?')}")
            return result

        except Exception as e:
            logger.error(f"Error binding module input data interface: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def read_ns_curve(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        module_name: str,
        input_name: str,
        script_type: str = "spawn"
    ) -> Dict[str, Any]:
        """Read the keyframes of a Niagara curve module input (ScaleColor, ScaleSpriteSize, FloatFromCurve...).

        Curves hold the time-based aesthetic of VFX (size grow/peak/fade, color-over-life). Locate the
        curve by module + input_name (use list_module_inputs). Single-channel curves return top-level
        "keys"; multi-channel (color/vector) return "channels" (r/g/b/a or x/y/z/w). Each key: time,
        value, arrive_tangent, leave_tangent, interp_mode. via_dynamic_input=true means the curve was
        reached through a "...FromCurve" dynamic input on the requested input.

        Example: read_ns_curve("NS_Fire","Fire","ScaleColor","Scale Alpha","spawn")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "module_name": module_name,
                "input_name": input_name,
                "script_type": script_type,
            }

            response = unreal.send_command("read_curve", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Read curve {module_name}.{input_name} ({result.get('type', '?')})")
            return result

        except Exception as e:
            logger.error(f"Error reading curve: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_ns_curve_keys(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        module_name: str,
        input_name: str,
        script_type: str = "spawn",
        keys: Any = None,
        channels: Any = None,
        pre_infinity: str = "",
        post_infinity: str = ""
    ) -> Dict[str, Any]:
        """Author the keyframes of a Niagara curve module input (creates an override curve).

        Scripts the "grow-peak-fade" / color-over-life curves that make VFX look alive. Provide
        `keys` for single-channel curves (float), or `channels` for multi-channel (color: r/g/b/a,
        vector: x/y/z/w). Each key: {time, value, interp_mode?("Cubic"/"Linear"/"Constant"),
        arrive_tangent?, leave_tangent?}. Missing tangents are auto-set. pre_infinity/post_infinity:
        "Constant"/"Cycle"/"CycleWithOffset"/"Oscillate"/"Linear". Replaces all existing keys. If the
        input is a scalar with a "...FromCurve" dynamic input, it edits that dynamic input's curve.

        Example: set_ns_curve_keys("NS_Fire","Fire","ScaleSpriteSize","Scale Factor","spawn",
            keys=[{"time":0,"value":0},{"time":0.15,"value":1.5},{"time":1,"value":0}])
        """
        from unreal_mcp_server import get_unreal_connection

        if keys is None and channels is None:
            return {"success": False, "message": "Provide either 'keys' (single-channel) or 'channels' (multi-channel)"}

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "module_name": module_name,
                "input_name": input_name,
                "script_type": script_type,
            }
            if keys is not None:
                params["keys"] = keys
            if channels is not None:
                params["channels"] = channels
            if pre_infinity:
                params["pre_infinity"] = pre_infinity
            if post_infinity:
                params["post_infinity"] = post_infinity

            response = unreal.send_command("set_curve_keys", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Set curve keys {module_name}.{input_name} -> {result.get('applied_key_counts', '?')}")
            return result

        except Exception as e:
            logger.error(f"Error setting curve keys: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_module_dynamic_input(
        ctx: Context,
        asset_full_path: str,
        emitter_name: str,
        module_name: str,
        input_name: str,
        dynamic_input_name: str,
        script_type: str = "spawn"
    ) -> Dict[str, Any]:
        """Set a module input's dynamic input (e.g. "Float from Curve") so it's driven by a sub-function.

        Many curves live behind a dynamic input rather than a direct curve DI: an input like
        ScaleColor.Scale RGB or ScaleSpriteSize.Scale Factor is a scalar until you attach a
        "...FromCurve" dynamic input. dynamic_input_name is the script asset name (no spaces):
        FloatFromCurve, ColorFromCurve, VectorFromCurve, Vector2DFromCurve, Vector4FromCurve, or a
        ScaleByCurve variant. Pick one whose output type matches the input. After this, call
        set_ns_curve_keys on the SAME input — it dives into the dynamic input and authors its curve.
        Only fresh inputs are supported (replacing an existing override isn't yet).

        Example: set_module_dynamic_input("NS_Fire","Fire","ScaleColor","Scale RGB","VectorFromCurve","spawn")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                **_map_asset_path(asset_full_path),
                "emitter_name": emitter_name,
                "module_name": module_name,
                "input_name": input_name,
                "dynamic_input_name": dynamic_input_name,
                "script_type": script_type,
            }

            response = unreal.send_command("set_module_dynamic_input", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Set dynamic input {module_name}.{input_name} = {dynamic_input_name}")
            return result

        except Exception as e:
            logger.error(f"Error setting module dynamic input: {e}")
            return {"success": False, "message": str(e)}
