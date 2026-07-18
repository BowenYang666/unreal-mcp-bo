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
        """Read detailed info about a specific Niagara system asset.

        Returns the system's emitters (names, sim targets, settings) and all
        User-exposed parameters (names, types, default values). This is the key
        tool for understanding what a Niagara system does and what can be tweaked.

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_Explosion") or
                short asset name (e.g. "NS_Explosion").

        Returns:
            Dict with system info including:
            - name, path
            - emitters: list of {name, enabled, sim_target, local_space, determinism}
            - user_parameters: list of {name, type, default_value}

            Each emitter also includes a "renderers" list. Every renderer entry has
            "type" and "enabled", plus type-specific fields reflected from its
            UPROPERTYs, e.g.:
            - Sprite: material (asset path), sub_image_size {x,y}, sub_uv_blending_enabled,
              alignment, facing_mode, sort_mode, pivot_offset, min/max_facing_camera_blend_distance,
              sort_only_when_translucent, use_gpu_init, material_user_binding
            - Mesh: meshes (array), override_materials, source_mode, facing_mode,
              mesh_alignment, sort_mode, enable_frustum_culling, enable_camera_distance_culling,
              sub_image_size
            - Ribbon: material, facing_mode, uv0_settings, uv1_settings, draw_direction,
              tessellation_mode, curve_tension, tessellation_factor, sub_image_size, shape
            - Light: radius_scale, color_add {r,g,b}, use_inverse_squared_falloff,
              affects_translucency, light_rendering_enabled, volumetric_scattering_intensity
            Common to all: motion_vector_setting, renderer_visibility_tag, platforms.

        Examples:
            read_niagara_system(asset_full_path="/Game/VFX/NS_Explosion")
            read_niagara_system(asset_full_path="NS_Explosion")
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
        """List every input pin of a module in an emitter script, with its type and value mode.

        This solves the "which pins can I enable?" exploration problem. Niagara module
        templates only expose a few inputs as rapid-iteration parameters by default; the rest
        are on their module default and cannot be set with set_niagara_rapid_parameter until
        enabled. Use this to discover the full input list, then enable_module_input to expose one.

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_Fire") or short asset name.
            emitter_name: Name of the emitter within the system (e.g. "Fire").
            module_name: Module/function name in the stack (e.g. "InitializeParticle", "AddVelocity").
            script_type: One of "spawn", "update", "emitter_spawn", "emitter_update" (default: "spawn").

        Returns:
            Dict with "inputs" (list). Each input has:
            - name: short input name (e.g. "Color", "Uniform Sprite Size", "Cone Angle")
            - type: Niagara type name (e.g. "float", "Vector2D", "LinearColor")
            - current_mode: "Local" (has a rapid-iteration value), "Linked", or "Default" (not exposed)
            - can_enable_local: True if it can be enabled as a Local Value via enable_module_input
            - is_static / is_hidden: metadata flags
            - rapid_parameter_name: the full Constants.* name that enabling would create (rapid types)
            - value: current value when already Local

        Examples:
            list_module_inputs(asset_full_path="/Game/VFX/NS_Fire", emitter_name="Fire",
                module_name="InitializeParticle", script_type="spawn")
            list_module_inputs(asset_full_path="NS_Fire", emitter_name="Fire",
                module_name="AddVelocity", script_type="spawn")
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
        """Enable a module input pin as a Local Value so it can be read/written.

        Equivalent to opening the input's dropdown in the Niagara editor and choosing
        "Local Value". For constant (rapid-iteration) input types this creates a rapid
        iteration parameter, after which set_niagara_rapid_parameter can read/write it.
        Only constant types are supported (float, int, bool, vec2, vec3, vec4/quat, color);
        data interfaces and object assets cannot be enabled this way.

        Use list_module_inputs first to discover the exact input_name and confirm
        can_enable_local is True.

        Args:
            ctx: The MCP context
            asset_full_path: Full asset path (e.g. "/Game/VFX/NS_Fire") or short asset name.
            emitter_name: Name of the emitter within the system (e.g. "Fire").
            module_name: Module/function name (e.g. "InitializeParticle", "AddVelocity").
            input_name: Short input name to enable (e.g. "Color", "Uniform Sprite Size", "Cone Angle").
            script_type: One of "spawn", "update", "emitter_spawn", "emitter_update" (default: "spawn").
            initial_value: Optional value to set at the same time. Omit to use the module default.
                Format matches the input type:
                - float/int: a number (e.g. 60)
                - bool: true/false
                - vec2: [x, y]
                - vec3: [x, y, z]
                - vec4/quat: [x, y, z, w]
                - color: {"r": 1.0, "g": 0.5, "b": 0.0, "a": 1.0} or [r, g, b, a]

        Returns:
            Dict with parameter_name (full Constants.* name), type, value, and already_enabled flag.

        Examples:
            enable_module_input(asset_full_path="/Game/VFX/NS_Fire", emitter_name="Fire",
                module_name="InitializeParticle", input_name="Color",
                initial_value={"r": 5.0, "g": 1.5, "b": 0.2, "a": 1.0})
            enable_module_input(asset_full_path="NS_Fire", emitter_name="Fire",
                module_name="AddVelocity", input_name="Cone Angle", initial_value=45.0)
            enable_module_input(asset_full_path="NS_Fire", emitter_name="Fire",
                module_name="InitializeParticle", input_name="Uniform Sprite Size")
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
