"""
Blueprint Tools for Unreal MCP.

This module provides tools for creating and manipulating Blueprint assets in Unreal Engine.
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def call_component_defaults(command: str, arguments: dict) -> dict:
    """Keep complete Blueprint default scopes and truthful partial mutation receipts."""
    from tools.project_tools import call_asset_command
    from unreal_mcp_server import spill_if_oversized
    from uuid import uuid4

    result = call_asset_command(command, arguments)
    metadata = ("success", "component_defaults_contract", "blueprint_path", "complete", "baseline",
                "dry_run", "would_modify", "modified", "compiled", "saved", "package_dirty", "stage", "error", "existing_data_preserved")
    spilled = spill_if_oversized(result, command, uuid4().hex, preview_keys=metadata, count_keys=("components", "unsupported"),
        hint="Review the complete defaults/receipt file. Timeout is unknown execution state; never blindly repeat a write.")
    if spilled is not result and spilled.get("overflow"):
        spilled["success"] = result.get("success", False)
        for key in metadata:
            if key in result:
                spilled[key] = result[key]
    return spilled

def register_blueprint_tools(mcp: FastMCP):
    """Register Blueprint tools with the MCP server."""

    @mcp.tool()
    def read_blueprint_component_defaults(ctx: Context, blueprint_path: str) -> dict:
        """Read exact SkeletalMeshComponent defaults and proven local/inherited/native origins.

        Example: blueprint_path='/Game/Towers/BP_Tower'. Exact /Game Actor Blueprint
        package, not an instance/class/redirector. Returns component_defaults_contract=1,
        hierarchy, effective template class/path/outer/archetype, editable status,
        target_owns_override, typed mesh/animation_mode/anim_class and selector
        {source,name,declaring_class,scs_guid}. source is cpp_inherited, inherited_scs
        or local_scs. Native declaration is proven from native default subobjects;
        SCS uses declaring generated class plus original node GUID. Never guess from
        a display name or old read_blueprint cpp_inherited label. Only skeletal components
        are included; not an enumeration/serializer of every component kind.
        Non-author fields are bounded read-only ExportText preservation witnesses,
        NOT writable object paths or proof of arbitrary custom binary completeness.
        Unknown derived types, ambiguous names/identities/handler/bounds mark incomplete,
        baseline=null; do not write partial scopes. Read overflow.file_path in full.
        Does not create overrides, compile, spawn, execute scripts, dirty or save.
        Requires matching native plugin; Python discovery is not deployment.
        """
        return call_component_defaults("read_blueprint_component_defaults", {"blueprint_path": blueprint_path})

    @mcp.tool()
    def set_blueprint_skeletal_component_defaults(ctx: Context, blueprint_path: str, component: dict,
                                                  expected_baseline: str, settings: dict,
                                                  dry_run: bool = True, save: bool = False) -> dict:
        """Preview/patch only the selected child Blueprint's own skeletal default template.

        Example: blueprint_path='/Game/Towers/BP_Tower', component=<exact reader component
        selector>, expected_baseline=<complete read>, settings={'mesh':'/Game/Meshes/SKM_Tower',
        'animation_mode':'AnimationBlueprint','anim_class':'/Game/Anims/ABP_Tower.ABP_Tower_C'}.
        Only mesh, animation_mode, anim_class accepted. Merge retains unspecified fields.
        First contract supports AnimationBlueprint mode only, existing exact editable
        USkeletalMeshComponent templates and matching concrete AnimBlueprint generated
        classes with the SAME exact Skeleton. Mesh and Anim Class must both be valid
        or explicitly cleared together; no SingleNode/custom animation transitions,
        retargeting, arbitrary native classes or arbitrary instanced property paths.
        Dirty target/parents/relevant assets, stale/partial/ambiguous identity, wrong
        class/mesh/skeleton/mode or busy compilation reject before mutation. No global
        compilation finish, parent save or implicit draft save. Defaults dry-run/no save.
        Native writes use the target child CDO subobject; inherited SCS writes create
        a target Blueprint source handler override ONLY on apply; local SCS retains
        its own template. Never mutate parent templates, replace component pointers,
        add duplicate SCS nodes or inject ConstructionScript/BeginPlay setters.
        One edit transaction, formal setters/property notification, target compile
        OUTSIDE transaction and RE-RESOLVE after reinstancing. Verify actor/component
        saved-field witnesses, other components, local SCS structure and parent scopes.
        Same values no-op, never create an unnecessary override. save=True saves ONLY
        this target after successful readback. Inspect before/proposed_defaults/after,
        modified/compiled/saved/package_dirty/stage, including full spill files.
        Save/compile/readback failure can leave applied changes; timeout is UNKNOWN,
        no rollback or automatic retries. Existing SCS/PhysMaterial API is unchanged.
        """
        return call_component_defaults("set_blueprint_skeletal_component_defaults", dict(blueprint_path=blueprint_path,
            component=component, expected_baseline=expected_baseline, settings=settings, dry_run=dry_run, save=save))
    
    @mcp.tool()
    def create_blueprint(
        ctx: Context,
        name: str,
        parent_class: str
    ) -> Dict[str, Any]:
        """Create a new Blueprint class.

        Args:
            name: REQUIRED full content path, e.g. "/Game/Enemies/BP_MyActor".
                Must start with "/Game/" (or "/Script/" for an engine namespace).
                The previous /Game/Blueprints/ fallback was removed — bare names
                like "BP_MyActor" are now rejected.
            parent_class: Parent class. Accepts short names ("Actor", "Pawn",
                "Character"), engine class paths ("/Script/Engine.APawn"), or
                project class paths ("/Script/MyGame.MyClass").

        Returns:
            Dict containing creation status and the blueprint info.

        Examples:
            create_blueprint(name="/Game/Blueprints/BP_MyActor", parent_class="Actor")
            create_blueprint(name="/Game/Enemies/BP_Dog", parent_class="Character")
            create_blueprint(name="/Game/Tests/BP_FT_X", parent_class="/Script/FunctionalTesting.FunctionalTest")
        """
        # Import inside function to avoid circular imports
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("create_blueprint", {
                "name": name,
                "parent_class": parent_class
            })
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Blueprint creation response: {response}")
            return response or {}
            
        except Exception as e:
            error_msg = f"Error creating blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def add_component_to_blueprint(
        ctx: Context,
        blueprint_path: str,
        component_type: str,
        component_name: str,
        location: List[float] = [],
        rotation: List[float] = [],
        scale: List[float] = [],
        component_properties: Dict[str, Any] = {}
    ) -> Dict[str, Any]:
        """
        Add a component to a Blueprint.
        
        Args:
            blueprint_path: Name of the target Blueprint
            component_type: Type of component to add (use component class name without U prefix)
            component_name: Name for the new component
            location: [X, Y, Z] coordinates for component's position
            rotation: [Pitch, Yaw, Roll] values for component's rotation
            scale: [X, Y, Z] values for component's scale
            component_properties: Additional properties to set on the component
        
        Returns:
            Information about the added component
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            # Ensure all parameters are properly formatted
            params = {
                "blueprint_path": blueprint_path,
                "component_type": component_type,
                "component_name": component_name,
                "location": location or [0.0, 0.0, 0.0],
                "rotation": rotation or [0.0, 0.0, 0.0],
                "scale": scale or [1.0, 1.0, 1.0]
            }
            
            # Add component_properties if provided
            if component_properties and len(component_properties) > 0:
                params["component_properties"] = component_properties
            
            # Validate location, rotation, and scale formats
            for param_name in ["location", "rotation", "scale"]:
                param_value = params[param_name]
                if not isinstance(param_value, list) or len(param_value) != 3:
                    logger.error(f"Invalid {param_name} format: {param_value}. Must be a list of 3 float values.")
                    return {"success": False, "message": f"Invalid {param_name} format. Must be a list of 3 float values."}
                # Ensure all values are float
                params[param_name] = [float(val) for val in param_value]
            
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            logger.info(f"Adding component to blueprint with params: {params}")
            response = unreal.send_command("add_component_to_blueprint", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Component addition response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding component to blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_static_mesh_properties(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        static_mesh: str = "/Engine/BasicShapes/Cube.Cube"
    ) -> Dict[str, Any]:
        """
        Set static mesh properties on a StaticMeshComponent.
        
        Args:
            blueprint_path: Name of the target Blueprint
            component_name: Name of the StaticMeshComponent
            static_mesh: Path to the static mesh asset (e.g., "/Engine/BasicShapes/Cube.Cube")
            
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
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "static_mesh": static_mesh
            }
            
            logger.info(f"Setting static mesh properties with params: {params}")
            response = unreal.send_command("set_static_mesh_properties", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set static mesh properties response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting static mesh properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_component_property(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        property_name: str,
        property_value,
        expected_value: str = None,
        save: bool = False,
    ) -> Dict[str, Any]:
        """Set a property on an SCS component in a Blueprint.

        For property_name="PhysMaterialOverride" or "BodyInstance.PhysMaterialOverride",
        property_value is a PhysicalMaterial path or ""/null to clear. This path
        uses SetPhysMaterialOverride and compiles the Blueprint. Optional
        expected_value compares the old explicit ref ("" means none); save=True
        saves only this BP and rejects an already dirty package. These two options
        are unsupported for other properties. Native/inherited components are not
        resolved by this SCS-only tool. Example property_value="/Game/PM_Metal".
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "property_name": property_name,
                "property_value": property_value
            }
            
            if expected_value is not None or save:
                if property_name not in ("PhysMaterialOverride", "BodyInstance.PhysMaterialOverride"):
                    return {"success": False, "message": "expected_value/save are only supported for PhysMaterialOverride"}
                if expected_value is not None:
                    params["expected_value"] = expected_value
                params["save"] = save
            logger.info(f"Setting component property with params: {params}")
            response = unreal.send_command("set_component_property", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set component property response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting component property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_physics_properties(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        simulate_physics: bool = True,
        gravity_enabled: bool = True,
        mass: float = 1.0,
        linear_damping: float = 0.01,
        angular_damping: float = 0.0
    ) -> Dict[str, Any]:
        """Set physics properties on a component."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "simulate_physics": simulate_physics,
                "gravity_enabled": gravity_enabled,
                "mass": float(mass),
                "linear_damping": float(linear_damping),
                "angular_damping": float(angular_damping)
            }
            
            logger.info(f"Setting physics properties with params: {params}")
            response = unreal.send_command("set_physics_properties", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set physics properties response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting physics properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def compile_blueprint(
        ctx: Context,
        blueprint_path: str
    ) -> Dict[str, Any]:
        """Compile a Blueprint."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_path": blueprint_path
            }
            
            logger.info(f"Compiling blueprint: {blueprint_path}")
            response = unreal.send_command("compile_blueprint", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Compile blueprint response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error compiling blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_blueprint_property(
        ctx: Context,
        blueprint_path: str,
        property_name: str,
        property_value
    ) -> Dict[str, Any]:
        """
        Set a property on a Blueprint class default object.
        
        Args:
            blueprint_path: Name of the target Blueprint
            property_name: Name of the property to set
            property_value: Value to set the property to
            
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
                "blueprint_path": blueprint_path,
                "property_name": property_name,
                "property_value": property_value
            }
            
            logger.info(f"Setting blueprint property with params: {params}")
            response = unreal.send_command("set_blueprint_property", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set blueprint property response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting blueprint property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # @mcp.tool() commented out, just use set_component_property instead
    def set_pawn_properties(
        ctx: Context,
        blueprint_path: str,
        auto_possess_player: str = "",
        use_controller_rotation_yaw: bool = None,
        use_controller_rotation_pitch: bool = None,
        use_controller_rotation_roll: bool = None,
        can_be_damaged: bool = None
    ) -> Dict[str, Any]:
        """
        Set common Pawn properties on a Blueprint.
        This is a utility function that sets multiple pawn-related properties at once.
        
        Args:
            blueprint_path: Name of the target Blueprint (must be a Pawn or Character)
            auto_possess_player: Auto possess player setting (None, "Disabled", "Player0", "Player1", etc.)
            use_controller_rotation_yaw: Whether the pawn should use the controller's yaw rotation
            use_controller_rotation_pitch: Whether the pawn should use the controller's pitch rotation
            use_controller_rotation_roll: Whether the pawn should use the controller's roll rotation
            can_be_damaged: Whether the pawn can be damaged
            
        Returns:
            Response indicating success or failure with detailed results for each property
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # Define the properties to set
            properties = {}
            if auto_possess_player and auto_possess_player != "":
                properties["auto_possess_player"] = auto_possess_player
            
            # Only include boolean properties if they were explicitly set
            if use_controller_rotation_yaw is not None:
                properties["bUseControllerRotationYaw"] = use_controller_rotation_yaw
            if use_controller_rotation_pitch is not None:
                properties["bUseControllerRotationPitch"] = use_controller_rotation_pitch
            if use_controller_rotation_roll is not None:
                properties["bUseControllerRotationRoll"] = use_controller_rotation_roll
            if can_be_damaged is not None:
                properties["bCanBeDamaged"] = can_be_damaged
                
            if not properties:
                logger.warning("No properties specified to set")
                return {"success": True, "message": "No properties specified to set", "results": {}}
            
            # Set each property using the generic set_blueprint_property function
            results = {}
            overall_success = True
            
            for prop_name, prop_value in properties.items():
                params = {
                    "blueprint_path": blueprint_path,
                    "property_name": prop_name,
                    "property_value": prop_value
                }
                
                logger.info(f"Setting pawn property {prop_name} to {prop_value}")
                response = unreal.send_command("set_blueprint_property", params)
                
                if not response:
                    logger.error(f"No response from Unreal Engine for property {prop_name}")
                    results[prop_name] = {"success": False, "message": "No response from Unreal Engine"}
                    overall_success = False
                    continue
                
                results[prop_name] = response
                if not response.get("success", False):
                    overall_success = False
            
            return {
                "success": overall_success,
                "message": "Pawn properties set" if overall_success else "Some pawn properties failed to set",
                "results": results
            }
            
        except Exception as e:
            error_msg = f"Error setting pawn properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def read_blueprint(
        ctx: Context,
        blueprint_path: str,
        include_nodes: bool = True,
        include_properties: bool = True,
        include_anim_graph: bool = False,
        include_subgraphs: bool = False,
        max_subgraph_depth: int = 8
    ) -> Dict[str, Any]:
        """Read a Blueprint's structure: parent class, components, variables, event graphs, functions.

        Args:
            blueprint_path: Full asset path, e.g. "/Game/Characters/BP_Hero".
            include_nodes: Include per-node pin/link details in event_graphs (default True).
                Set False for large BPs to shrink the response.
            include_properties: Include component UPROPERTY dumps (default True).
            include_anim_graph: Include Animation Graph data for AnimBPs (default False).
            include_subgraphs: Recursively include collapsed UK2Node_Composite graphs (default False).
                Returned in a top-level subgraphs array with parent/owner graph metadata.
            max_subgraph_depth: Maximum collapsed-graph recursion depth, clamped to 0..32 (default 8).

        Returns dict with name/path/parent_class/components/variables/event_graphs/functions/interfaces.
        components entries have source: "blueprint" | "cpp_inherited".
        Oversized responses spill to a temp .json file; check `overflow` / `file_path`.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "blueprint_path": blueprint_path,
                "include_nodes": include_nodes,
                "include_properties": include_properties,
                "include_subgraphs": include_subgraphs,
                "max_subgraph_depth": max(0, min(32, int(max_subgraph_depth))),
            }
            if include_anim_graph:
                params["include_anim_graph"] = True

            logger.info(f"Reading blueprint: {blueprint_path}")
            response = unreal.send_command("read_blueprint", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            # Large BPs (Lyra's HUD widgets, character BPs with big event graphs) blow
            # past the ~25KB MCP tool payload ceiling. Spill oversized responses to a
            # temp .json file and return a small pointer for the LLM to read_file.
            from unreal_mcp_server import spill_if_oversized
            return spill_if_oversized(
                response,
                tool_name="read_blueprint",
                identifier=blueprint_path,
                preview_keys=("name", "path", "parent_class"),
                count_keys=("components", "variables", "functions", "event_graphs", "subgraphs"),
                hint="Consider include_nodes=False, include_properties=False, or a lower max_subgraph_depth for a smaller inline response.",
            )

        except Exception as e:
            error_msg = f"Error reading blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def list_blueprints(
        ctx: Context,
        path: str = "/Game",
        recursive: bool = True,
        name_filter: str = ""
    ) -> Dict[str, str]:
        """
        List all Blueprint assets in the project, optionally filtered by path and name.

        This tool is useful for discovering what Blueprints exist in the project before
        reading their details with read_blueprint.

        Args:
            path: Asset path to search in. Defaults to "/Game" which searches the entire project.
                  Examples: "/Game", "/Game/Blueprints", "/Game/Characters"
            recursive: Whether to search subdirectories recursively (default True).
            name_filter: Optional substring filter to match Blueprint names.
                        Examples: "BP_", "Player", "Enemy"

        Returns:
            A dict containing:
            - count: Number of Blueprints found
            - blueprints: List of Blueprint summaries with name, path, and parent class
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

            logger.info(f"Listing blueprints in path: {path}")
            response = unreal.send_command("list_blueprints", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"List blueprints response received")
            return response

        except Exception as e:
            error_msg = f"Error listing blueprints: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Blueprint tools registered successfully")
