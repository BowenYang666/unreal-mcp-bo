"""
Project Tools for Unreal MCP.

This module provides tools for managing project-wide settings and configuration.
"""

import logging
import json
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def call_asset_command(command: str, params: dict) -> dict:
    """Preserve partial mutation receipts for the bounded asset operations."""
    from unreal_mcp_server import get_unreal_connection
    try:
        response = get_unreal_connection().send_command(command, params)
        if not response:
            return {"success": False, "stage": "transport", "message": "No response; modification/save state unknown"}
        if response.get("status") == "error":
            return {**response.get("result", {}), "success": False, "message": response.get("error", "Operation failed")}
        return response.get("result", response)
    except Exception as error:
        return {"success": False, "stage": "transport", "message": f"{error}; modification/save state unknown"}

def call_migration_command(command: str, params: dict) -> dict:
    """Keep migration receipts truthful and preserve complete oversized plans on disk."""
    from unreal_mcp_server import spill_if_oversized
    from uuid import uuid4

    result = call_asset_command(command, params)
    if "plan_id" in params:
        result.setdefault("plan_id", params["plan_id"])
    spilled = spill_if_oversized(result, command, str(result.get("plan_id", uuid4().hex)),
        preview_keys=("success", "state", "plan_id", "executable", "fresh_reload", "error", "stage"),
        count_keys=("mapping", "dependencies", "items", "blockers", "issues"),
        hint="Read the complete plan/receipt file before confirming or retrying any operation.")
    if spilled is not result and spilled.get("overflow"):
        spilled["success"] = result.get("success", False)
        for key in ("state", "plan_id", "executable", "confirmation_token", "fresh_reload", "error", "stage"):
            if key in result:
                spilled[key] = result[key]
    return spilled

def register_project_tools(mcp: FastMCP):
    """Register project tools with the MCP server."""

    @mcp.tool()
    def plan_asset_migration(ctx: Context, roots: list[str], path_rules: list[dict],
                             rebind_assets: list[str] = None, retain_roots: list[str] = None) -> dict:
        """Preview recursive dependency copies and explicit existing-NS rebinding without saving assets.

        Example: roots=["/Game/VFX/NS_Test"],
        path_rules=[{"source_root":"/Game", "target_root":"/Game/__Dev/CopyTest"}].
        Longest source prefix wins. All source/target paths are /Game package paths,
        not Windows paths or object paths. Every /Game dependency needs a path rule
        or explicit retain_roots entry; engine/plugin references are retained.
        rebind_assets is an optional subset of roots containing existing Niagara
        Systems to keep in place and rebind ONLY; omission copies all roots.
        Returns executable, blockers, full mapping, dependencies, retained_external,
        namespace_mapping, plan_id and confirmation_token. Review the entire plan
        (read file_path when overflow=True; preview is not the complete plan)
        before execute_asset_migration; blocked plans cannot execute. Plan success
        alone does not mean executable. Does not run Unreal's cross-project Migrate.
        Planning loads assets but never saves them; loading can trigger engine work.
        Maximum 512 packages and 32 cached plans per editor session.
        """
        return call_migration_command("plan_asset_migration", {
            "roots": roots, "path_rules": path_rules,
            "rebind_assets": [] if rebind_assets is None else rebind_assets,
            "retain_roots": [] if retain_roots is None else retain_roots})

    @mcp.tool()
    def execute_asset_migration(ctx: Context, plan_id: str, confirmation_token: str) -> dict:
        """Execute exactly one reviewed migration plan; never overwrite existing destinations.

        Example: plan_id and confirmation_token are the strings returned by
        plan_asset_migration. Source hashes, dirty packages and collisions are
        rechecked. Only copies and explicitly listed rebind_assets may be saved;
        no Save All, Consolidate, global replacement or cross-project Migrate.
        Returns state and per-item created/modified/saved results after native work.
        Check success AND state, not merely transport success. Failure can leave
        copies or unsaved changes; no rollback is promised. A timeout is unknown:
        query get_asset_migration_status(plan_id), never issue a new copy plan as
        a blind retry. Repeating a started plan returns its receipt, not a new run.
        After editor restart only persisted receipts remain; plans cannot resume.
        Use verify_asset_migration for recursive readback; it does not force unload.
        """
        return call_migration_command("execute_asset_migration", {
            "plan_id": plan_id, "confirmation_token": confirmation_token})

    @mcp.tool()
    def get_asset_migration_status(ctx: Context, plan_id: str) -> dict:
        """Read a migration plan or persisted execution receipt without retrying work.

        Example: plan_id="169F602B478CDD396E519C8A3B39ABF0".
        Inspect state and each item's created/modified/saved flags. A persisted
        running receipt after restart is interrupted_unknown, not rollback or
        completion. Receipts describe observed progress, not a fresh disk audit.
        Unknown or expired IDs do not authorize overwriting or blindly retrying.
        """
        return call_migration_command("get_asset_migration_status", {"plan_id": plan_id})

    @mcp.tool()
    def verify_asset_migration(ctx: Context, plan_id: str) -> dict:
        """Read back saved migration targets, recursive dependencies, hashes and compilation.

        Example: plan_id="169F602B478CDD396E519C8A3B39ABF0".
        Loads targets and waits for load-triggered compilation without saving.
        Reports issues, retained_external, compilation, fresh_reload and already_loaded.
        It never force-unloads packages: use a separate editor process to prove
        persistence from disk. String-encoded paths are not generally discoverable.
        This checks a prior execution; it does not execute or resume a plan.
        """
        return call_migration_command("verify_asset_migration", {"plan_id": plan_id})
    
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
    def create_data_asset(ctx: Context, asset_path: str, class_path: str, save: bool = True) -> dict:
        """Create an instance of a concrete DataAsset subclass, never overwriting.

        Example: asset_path="/Game/Data/DA_Impact",
        class_path="/Script/NodeFall.NFWeaponImpactProfile". Blueprint classes
        use a full generated class path ending in _C, not the Blueprint asset.
        Only DataAsset-derived classes are allowed, not arbitrary UObjects.
        Defaults come from the class; inspect with get_class_properties and edit
        with set_asset_properties afterwards. Saves only the new package unless
        save=False. Read created/modified/saved/package_dirty; failure may leave
        a created asset in memory. Transport timeout is indeterminate; don't retry.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            response = get_unreal_connection().send_command("create_data_asset", {
                "asset_path": asset_path, "class_path": class_path, "save": save})
            if not response:
                return {"success": False, "stage": "transport", "message": "No response; creation/save state unknown"}
            if response.get("status") == "error":
                return {**response.get("result", {}), "success": False, "message": response.get("error", "Creation failed")}
            return response.get("result", response)
        except Exception as error:
            return {"success": False, "stage": "transport", "message": f"{error}; creation/save state unknown"}

    @mcp.tool()
    def create_physical_material(ctx: Context, asset_path: str, save: bool = True) -> dict:
        """Create a PhysicalMaterial with engine defaults; refuses existing packages.

        Example: asset_path="/Game/Physics/PM_Metal". Then inspect with
        get_class_properties(structured=True) and configure SurfaceType via
        set_asset_properties, using e.g. "SurfaceType1", not display name "Metal".
        Only creates PhysicalMaterial, not arbitrary UObject types. Returns
        created/modified/saved/package_dirty. Timeout is indeterminate; no retry.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            response = get_unreal_connection().send_command("create_physical_material", {
                "asset_path": asset_path, "save": save})
            if not response:
                return {"success": False, "stage": "transport", "message": "No response; creation/save state unknown"}
            if response.get("status") == "error":
                return {**response.get("result", {}), "success": False, "message": response.get("error", "Creation failed")}
            return response.get("result", response)
        except Exception as error:
            return {"success": False, "stage": "transport", "message": f"{error}; creation/save state unknown"}

    @mcp.tool()
    def set_asset_properties(
        ctx: Context,
        asset_path: str,
        changes: list[dict],
        save: bool = True
    ) -> dict:
        """Patch a DataAsset instance or allowlisted PhysicalMaterial fields.

        First inspect get_class_properties(asset_path=..., structured=True).
        changes is a nonempty list of {path, value, expected_value?}; paths use
        reflected names, dots and zero-based array indices, e.g. Stats.Health or
        Attacks[0].Damage. Set an array value to replace the whole array. An
        expected_value must match the typed current value before any writes.
        null clears nullable references only. References use full object/class
        paths. Maps use complete [{"key":"SurfaceType1","value":{...}}]
        replacement with typed keys; [] clears the map. No single-key paths.
        Inherited struct fields are included; whole structs require every field.
        PhysicalMaterial allows SurfaceType, Friction, StaticFriction, Restitution,
        Density and friction/restitution combine modes plus their override bools.
        No object traversal, graph editing, sets or instanced objects.
        The complete request is validated before editing. Only the target package
        is saved; save=True rejects already dirty packages. save=False leaves it
        dirty for explicit save_asset. A save failure can leave in-memory changes.
        A timeout is indeterminate: read back before retrying.

        Example: set_asset_properties(asset_path="/Game/Data/DA_Enemy",
            changes=[{"path":"Health","value":150,"expected_value":100}])
        Returns success, modified, saved, changes and error stage when available.
        """
        from unreal_mcp_server import get_unreal_connection

        if (not asset_path.startswith("/Game/") or asset_path.endswith("/")
                or asset_path != asset_path.strip() or any(char in asset_path for char in ("\\", ":", "."))
                or "//" in asset_path):
            return {"success": False, "modified": False, "saved": False, "message": "Use a full /Game/... package path"}
        if not isinstance(changes, list) or not 1 <= len(changes) <= 64:
            return {"success": False, "modified": False, "saved": False, "message": "changes must contain 1..64 patches"}
        for change in changes:
            if (not isinstance(change, dict) or not isinstance(change.get("path"), str)
                    or not change["path"] or "value" not in change
                    or set(change) - {"path", "value", "expected_value"}):
                return {"success": False, "modified": False, "saved": False, "message": "Each patch needs path and value; expected_value is optional"}
        try:
            params = {"asset_path": asset_path, "changes": changes, "save": save}
            encoded = json.dumps(params, allow_nan=False).encode("utf-8")
            if len(encoded) > 256 * 1024:
                return {"success": False, "modified": False, "saved": False, "message": "Patch request exceeds 256 KiB"}
        except (ValueError, TypeError) as error:
            return {"success": False, "modified": False, "saved": False, "message": str(error)}
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "modified": False, "saved": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("set_asset_properties", params)
            if not response:
                return {"success": False, "stage": "transport", "message": "No response; modification/save state unknown"}
            if response.get("status") == "error":
                return {**response.get("result", {}), "success": False, "message": response.get("error", "Asset update failed")}
            return response.get("result", response)
        except Exception as error:
            return {"success": False, "stage": "transport", "message": f"{error}; modification/save state unknown"}

    @mcp.tool()
    def get_class_properties(
        ctx: Context,
        class_name: str = "",
        asset_path: str = "",
        category: str = "",
        structured: bool = False,
        property_paths: list[str] = None
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
            structured: For DataAsset/PhysicalMaterial instances, return typed values and the
                supported write contract instead of legacy text. Does not edit.
            property_paths: Optional exact paths for structured reads, e.g.
                ["Stats.Health", "Attacks[0]"]. Omit to list top-level fields.

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
            if structured:
                params["structured"] = True
                if property_paths is not None:
                    params["property_paths"] = property_paths
            elif property_paths is not None:
                return {"success": False, "message": "property_paths requires structured=True"}

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
    def read_cascade_system(
        ctx: Context,
        asset_path: str,
        emitter_index: int = -1,
        lod_index: int = -1,
    ) -> Dict[str, Any]:
        """Read a legacy Cascade ParticleSystem without editing or simulating it.

        asset_path is a full Unreal asset path, not a folder. Indices are zero-based;
        -1 selects all emitters or all LODs. Returns ordered modules, typed properties,
        distributions/curve keys, resource references and event/dynamic parameters.
        Shared inline objects use object_ref links into the returned objects table.
        Read complete/warnings for unsupported fields or traversal limits. External
        assets are referenced, not recursively read. Runtime parameter overrides and
        particle simulation state are not included. Large results spill to a JSON file.

        Example:
            read_cascade_system(
                asset_path="/Game/ParagonWraith/FX/Particles/Abilities/Drone/FX/P_Wraith_Drone_Targeting",
                emitter_index=3, lod_index=0)
        """
        if (not isinstance(asset_path, str) or not asset_path.startswith("/")
                or asset_path.endswith("/") or "\\" in asset_path or ":" in asset_path
                or asset_path != asset_path.strip() or "//" in asset_path
                or any(part in (".", "..") for part in asset_path.split("/"))):
            return {"success": False, "message": "asset_path must be a full Unreal asset path"}
        for parameter, index in (("emitter_index", emitter_index), ("lod_index", lod_index)):
            if type(index) is not int or index < -1:
                return {"success": False, "message": f"{parameter} must be -1 or a nonnegative integer"}

        from unreal_mcp_server import get_unreal_connection, spill_if_oversized

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("read_cascade_system", {
                "asset_path": asset_path,
                "emitter_index": emitter_index,
                "lod_index": lod_index,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error") or "Cascade read failed"}
            result = response.get("result", response)
            return spill_if_oversized(
                result, "read_cascade_system", f"{asset_path}_e{emitter_index}_lod{lod_index}",
                preview_keys=("success", "name", "asset_path", "class", "complete", "warnings",
                              "emitter_count", "selected_emitter_count", "object_count", "referenced_asset_count"),
                count_keys=("emitters",),
            )
        except Exception as exc:
            logger.error("Error reading Cascade system: %s", exc)
            return {"success": False, "message": str(exc)}

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
            Each state has: name, type, selection_behavior, weight (float),
            considerations[], tasks[], transitions[],
            enter_conditions[], children[].
            LinkedAsset states also have linked_asset, the referenced StateTree's
            full /Game package path; read that asset separately for its hierarchy.
            Tasks/conditions/considerations have: class (struct name),
            instance_class (for BP nodes), instance_properties.
            Considerations also expose Operand and DeltaIndent in node_properties.
            Transitions have: trigger, priority, link_type, target_state, conditions[].
            weight and considerations are used with Utility AI selection
            behaviors (TrySelectChildrenWithHighestUtility,
            TrySelectChildrenAtRandomWeightedByUtility).
            Note: Utility AI is EXPERIMENTAL in UE 5.5+ — API may change.

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