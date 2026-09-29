"""Bounded level-instance editing and asynchronous viewport capture."""

import json
from mcp.server.fastmcp import Context, FastMCP
from tools.project_tools import call_asset_command


def call_scene(command: str, parameters: dict) -> dict:
    """Forward finite JSON without retrying unknown mutation outcomes."""
    parameters = {key: value for key, value in parameters.items() if value is not None}
    try:
        json.dumps(parameters, allow_nan=False)
    except (ValueError, TypeError) as error:
        return {"success": False, "modified": False, "saved": False, "stage": "preflight", "error": str(error)}
    manifest = parameters.get("manifest")
    manifest_folders = isinstance(manifest, dict) and (
        "folder_root" in manifest or (
            isinstance(manifest.get("objects"), list) and any(
                isinstance(item, dict) and "folder" in item for item in manifest["objects"])))
    if (command == "manage_scene_actor" and "folder_path" in parameters) or (command == "apply_scene_manifest" and manifest_folders):
        capability = call_asset_command("get_editor_context", {})
        if not capability.get("success") or capability.get("folder_contract") != 1:
            return {"success": False, "modified": False, "saved": False, "stage": "preflight",
                    "error_code": "native_folder_contract_required",
                    "error": "Connected native plugin has not confirmed folder contract 1. Build/deploy/reconnect before using folder parameters; no mutation was sent."}
    result = call_asset_command(command, parameters)
    if "request_id" in parameters:
        result.setdefault("request_id", parameters["request_id"])
    return result


def register_scene_tools(mcp: FastMCP):
    """Register guarded scene operations, all edits preview-only until explicit save."""

    @mcp.tool()
    def undo_scene_edit(ctx: Context, project_path: str, level_path: str, transaction_id: str) -> dict:
        """Undo only the last recorded MCP scene transaction in the unchanged confirmed world.

        Example: transaction_id copied from patch_scene_target/manage_scene_actor/
        set_scene_mesh/apply_scene_manifest/set_scene_actor_folders receipt. Refuses if a different user/tool
        transaction is now at the top. One-use ticket; never traverses the undo stack.
        Does not save or undo imports/material assets/viewport changes. Reinspect after.
        """
        return call_scene("undo_scene_edit", dict(project_path=project_path, level_path=level_path, transaction_id=transaction_id))

    @mcp.tool()
    def apply_scene_manifest(ctx: Context, project_path: str, level_path: str, manifest: dict, dry_run: bool = True) -> dict:
        """Preview/apply an idempotent 1..64-object StaticMesh placement manifest, never saves.

        Example: manifest={'version':1,'namespace':'Room','source_hash':'abc123',
        'units':'cm','up_axis':'Z','handedness':'left','objects':[{'id':'Wall',
        'mesh':'/Engine/BasicShapes/Cube.Cube','location':[0,0,150],
        'rotation':[0,0,0],'scale':[4,0.2,3]}]}. Review dry_run=True before applying.
        Object transforms are parent-local cm and pitch/yaw/roll degrees. Optional
        parent_id refers to another object in the same manifest, label sets Outliner
        text, materials uses explicit slot_index/slot_name and material_path.
        Optional folder_root='OpeningEnvironment' places objects under one Outliner
        root, with each object's folder='Architecture/MainCourtyard' relative to it.
        Omit folder_root AND object folder to preserve existing folders. Empty object
        folder uses the root; folder alone is refused. Folder-only changes do not
        rewrite transforms, attachments or materials. Requires native folder contract 1.
        Convert DCC coordinates explicitly; no inferred axes/units. Stable names are
        MCPScene_<namespace>_<id>; unmanaged collisions fail. Optional root
        expected_source_hash guards existing managed actors. Repeating identical input
        produces no changes. Missing objects are NOT deleted. No mesh combining or
        implicit materials. Whole preflight precedes one undoable map transaction;
        rare apply failures can leave partial work, reported in items. No World Partition.
        """
        return call_scene("apply_scene_manifest", dict(project_path=project_path, level_path=level_path, manifest=manifest, dry_run=dry_run))

    @mcp.tool()
    def recapture_scene_skylight(ctx: Context, project_path: str, level_path: str, actor_path: str, component_name: str) -> dict:
        """Queue recapture on an exact level SkyLightComponent without saving.

        Example: actor_path and component_name from scene inspection. A successful
        receipt means requested, not a rendered/settled frame. Follow with bounded
        viewport capture and actual comparison. Refused during another scene task.
        """
        return call_scene("recapture_scene_skylight", dict(project_path=project_path, level_path=level_path, actor_path=actor_path, component_name=component_name))

    @mcp.tool()
    def inspect_scene_asset(ctx: Context, project_path: str, asset_path: str) -> dict:
        """Read StaticMesh bounds/LOD vertices/triangles/UVs/slots/collision or Texture2D settings.

        Example: asset_path='/Engine/BasicShapes/Cube.Cube'. For reflected asset fields,
        use get_class_properties(asset_path=...) first. This specialized reader adds
        mesh render-data semantics. Bounds are cm; compiling=true means mesh statistics
        are not ready. No compile, edit, collision generation or save is requested.
        """
        return call_scene("inspect_scene_asset", dict(project_path=project_path, asset_path=asset_path))

    @mcp.tool()
    def import_scene_asset(ctx: Context, project_path: str, request_id: str, source_file: str,
                           destination_path: str, save: bool = False, import_uniform_scale: float = 1,
                           srgb: bool = None, compression: str = None, timeout_seconds: int = 120) -> dict:
        """Start FBX/glTF/GLB/PNG import into a NEW /Game directory, default unsaved.

        Example: request_id='wall_import_001', source_file='E:/Exports/wall.fbx',
        destination_path='/Game/__Dev/SceneTools/WallImport001', save=False.
        Never overwrites or reimports. FBX uses static-mesh factory, separate meshes,
        source unit conversion, generated simple collision/lightmap UVs, no automatic
        materials/textures. Uniform scale is FBX-only. glTF uses installed engine
        importer/Interchange and its standard meters/Y-up conversion; inspect actual
        outputs/bounds. PNG accepts srgb and compression='default'|'normalmap'|'masks';
        normal/mask requires srgb=False. A requested format still needs its engine importer.
        Query get_scene_import_status(request_id). Pending is not completion; overdue
        means native work continues (no hard cancellation or automatic retry). Blocking
        factory work can prevent status replies temporarily. Durable receipts survive
        restart; interrupted_unknown requires inspecting output before further action.
        Explicit save=True saves only returned destination assets. No rollback promise.
        The 32-entry memory cache is not a lifetime import limit: durable completed/failed
        receipts are evicted oldest-first and remain queryable/replayable from disk.
        Running, overdue, unknown and non-durable records are not evicted. error_code
        distinguishes scene_task_busy from receipt_cache_full (no safe eviction candidate).
        receipt_persisted describes the journal, separately from assets' saved state.
        """
        return call_scene("import_scene_asset", dict(project_path=project_path, request_id=request_id,
            source_file=source_file, destination_path=destination_path, save=save, import_uniform_scale=import_uniform_scale,
            srgb=srgb, compression=compression, timeout_seconds=timeout_seconds))

    @mcp.tool()
    def get_scene_import_status(ctx: Context, project_path: str, request_id: str) -> dict:
        """Read durable import status/assets, e.g. request_id='wall_import_001'; never restarts work.

        Completed still requires asset inspection and visual/collision checks. Failed
        may have partial assets; interrupted_unknown after restart is not rollback.
        """
        return call_scene("get_scene_import_status", dict(project_path=project_path, request_id=request_id))

    @mcp.tool()
    def get_editor_context(ctx: Context) -> dict:
        """Read project/map identity, dirty packages, PIE mode, viewports, exposure and compilation state.

        Example: get_editor_context(). Copy project_path, level_path and viewport_id
        into subsequent scene requests. This does not select, open or save a map.
        Viewport IDs are session-local; recheck after opening/closing editor tabs.
        """
        return call_scene("get_editor_context", {})

    @mcp.tool()
    def list_scene_actors(ctx: Context, project_path: str, level_path: str,
                          filter: str = "", offset: int = 0, limit: int = 50) -> dict:
        """Read paged exact actor paths, transforms, labels and classes from the confirmed map.

        Example: project_path='E:/Projects/Test/Test.uproject', level_path='/Game/Test',
        filter='RectLight', limit=20. Limit is 1..200. Labels are not write selectors.
        """
        return call_scene("list_scene_actors", dict(project_path=project_path, level_path=level_path,
                                                   filter=filter, offset=offset, limit=limit))

    @mcp.tool()
    def inspect_scene_target(ctx: Context, project_path: str, level_path: str, actor_path: str,
                             component_name: str = "", property_paths: list[str] = None) -> dict:
        """Read typed, domain-allowlisted level actor/component fields with schemas and units.

        Example: actor_path='/Game/Test.Test:PersistentLevel.RectLight_0',
        component_name='LightComponent0', property_paths=['Intensity','SourceWidth'].
        First inspect the actor without component_name to discover its components.
        PPV paths include Settings.BloomIntensity and Settings.bOverride_BloomIntensity.
        Native components only; not Blueprint SCS defaults. Exact identities required.
        """
        return call_scene("inspect_scene_target", dict(project_path=project_path, level_path=level_path,
            actor_path=actor_path, component_name=component_name, property_paths=property_paths))

    @mcp.tool()
    def patch_scene_target(ctx: Context, project_path: str, level_path: str, actor_path: str,
                           changes: list[dict], component_name: str = "") -> dict:
        """Patch a confirmed scene target in one transaction; never saves. Read schema first.

        Example: changes=[{'path':'Intensity','value':1200,'expected_value':800}].
        1..64 fields, full preflight, duplicate/unknown paths rejected. Values use UE
        units, not Blender watts. PPV/camera setting values auto-enable corresponding
        overrides unless explicitly included (false clears an override). Returns actual
        before/requested/after values. Wrong project/map, PIE, external actors and
        streaming-level writes are refused. A transport timeout is UNKNOWN, not rollback;
        inspect the exact target before any retry. Save separately with save_scene_level.
        """
        return call_scene("patch_scene_target", dict(project_path=project_path, level_path=level_path,
            actor_path=actor_path, component_name=component_name, changes=changes))

    @mcp.tool()
    def manage_scene_actor(ctx: Context, project_path: str, level_path: str, operation: str,
                           actor_path: str = "", actor_type: str = "", managed_id: str = "",
                           location: list[float] = None, rotation: list[float] = None,
                           scale: list[float] = None, folder_path: str = None) -> dict:
        """Create, transform or delete a native scene actor without saving.

        Example: operation='create', actor_type='RectLight', managed_id='KeyLight',
        location=[0,0,200], rotation=[-30,0,0], scale=[1,1,1]. UE units are cm;
        rotation is pitch/yaw/roll in degrees. Creation uses MCPScene_<managed_id>,
        refusing collisions, never auto-suffixing. Types: RectLight, PointLight,
        SpotLight, DirectionalLight, SkyLight, SkyAtmosphere, HeightFog,
        PostProcessVolume, StaticMeshActor, CameraActor, CineCameraActor, PlayerStart. Transform/delete require
        exact actor_path. Delete only accepts tool-managed actors. No World Partition.
        Creation-only folder_path='OpeningEnvironment/Lighting' is an Outliner path,
        not an asset directory or parent actor. Omission preserves legacy creation;
        empty string explicitly uses the world root. Requires native folder contract 1.
        On timeout read actors before retrying creation; no automatic retry.
        """
        return call_scene("manage_scene_actor", dict(project_path=project_path, level_path=level_path,
            operation=operation, actor_path=actor_path, actor_type=actor_type, managed_id=managed_id,
            location=location, rotation=rotation, scale=scale, folder_path=folder_path))

    @mcp.tool()
    def set_scene_actor_folders(ctx: Context, project_path: str, level_path: str,
                               changes: list[dict], dry_run: bool = True, managed_only: bool = True) -> dict:
        """Preview/assign Outliner folders for 1..200 exact actors, in one unsaved transaction.

        Example: changes=[{'actor_path':'/Game/Test.Test:PersistentLevel.MCPScene_Wall',
        'folder_path':'OpeningEnvironment/Architecture','expected_folder_path':''}].
        First list/inspect exact targets; do not select all actors by class or folder.
        Paths are relative slash-separated names (max 512 characters/16 segments);
        empty folder_path clears membership, not ownership. Dot/parent/padded or
        empty segments and filesystem-invalid characters are refused, not normalized.
        Expected old folder is optional; stale values abort the entire preflight.
        managed_only=True requires UnrealMCP.SceneManaged; explicitly set False only
        for reviewed legacy/unmanaged targets (including BP instances). Never infer
        ownership from folder membership. No recursion: attached children, transforms,
        materials, tags and editor default creation folder remain untouched. No WP,
        external actor packages or multi-level worlds. Dry-run changes nothing;
        identical assignments are no-ops. Review per-item before/requested/after;
        rare readback failure can leave partial changes. Undo via transaction_id;
        save_scene_level is separate. More than 200 targets requires explicit batches,
        each with its own preflight/undo. Timeout is unknown; inspect before retrying.
        """
        return call_scene("set_scene_actor_folders", dict(project_path=project_path, level_path=level_path,
            changes=changes, dry_run=dry_run, managed_only=managed_only))

    @mcp.tool()
    def get_scene_mesh(ctx: Context, project_path: str, level_path: str,
                       actor_path: str, component_name: str) -> dict:
        """Read a level StaticMeshComponent's mesh and every material slot/override.

        Example: component_name='StaticMeshComponent0', actor_path from list_scene_actors.
        Does not accept mutation parameters; slot indices and names are returned.
        """
        return call_scene("get_scene_mesh", dict(project_path=project_path, level_path=level_path,
            actor_path=actor_path, component_name=component_name))

    @mcp.tool()
    def set_scene_mesh(ctx: Context, project_path: str, level_path: str, actor_path: str,
                       component_name: str, static_mesh: str = None, materials: list[dict] = None) -> dict:
        """Set existing mesh and explicit material slots on one component, without saving.

        Example: static_mesh='/Engine/BasicShapes/Cube.Cube',
        materials=[{'slot_index':0,'material_path':'/Game/M_Wall.M_Wall'}].
        Each assignment uses exactly one slot_index or slot_name. Empty material_path
        clears that override. All slots/assets are checked before changes; other slots
        remain untouched. No implicit slot zero or automatic asset import.
        """
        return call_scene("set_scene_mesh", dict(project_path=project_path, level_path=level_path,
            actor_path=actor_path, component_name=component_name, static_mesh=static_mesh, materials=materials))

    @mcp.tool()
    def save_scene_level(ctx: Context, project_path: str, level_path: str,
                         confirm_all_changes_in_map: bool = False) -> dict:
        """Save only the confirmed current map package, after explicit approval of ALL its changes.

        Example: level_path='/Game/__Dev/SceneTools/Test', confirm_all_changes_in_map=True.
        Default refuses saving. Inspect dirty packages first: this includes pre-existing
        edits in the same map, not merely MCP edits. No Save All. External actor packages,
        World Partition and streaming sublevels are unsupported and refused.
        Save failure leaves in-memory changes; it does not promise rollback.
        """
        return call_scene("save_scene_level", dict(project_path=project_path, level_path=level_path,
            confirm_all_changes_in_map=confirm_all_changes_in_map))

    @mcp.tool()
    def get_scene_viewport(ctx: Context, project_path: str, level_path: str, viewport_id: int) -> dict:
        """Read a specific perspective Level Viewport pose, camera, FOV, dimensions and exposure.

        Example: viewport_id=0 copied from get_editor_context. Never casts the active
        asset-preview viewport. Camera post-process settings can also be inspected
        through inspect_scene_target on its CameraComponent.
        """
        return call_scene("get_scene_viewport", dict(project_path=project_path, level_path=level_path, viewport_id=viewport_id))

    @mcp.tool()
    def set_scene_viewport(ctx: Context, project_path: str, level_path: str, viewport_id: int,
                           camera_path: str = None, location: list[float] = None,
                           rotation: list[float] = None, fov: float = None, game_view: bool = None) -> dict:
        """Set a confirmed Level Viewport pose or lock it to an exact CameraActor; never saves.

        Example: viewport_id=0, location=[-500,0,150], rotation=[0,0,0], fov=60,
        game_view=True. Or use camera_path from list_scene_actors without pose/FOV.
        Rotation is pitch/yaw/roll degrees. Empty camera_path releases the camera lock.
        Viewport changes are not level transactions. Existing exposure overrides are
        reported, not silently reset. Refused while a capture task is active.
        """
        return call_scene("set_scene_viewport", dict(project_path=project_path, level_path=level_path,
            viewport_id=viewport_id, camera_path=camera_path, location=location, rotation=rotation, fov=fov, game_view=game_view))

    @mcp.tool()
    def capture_scene_viewport(ctx: Context, project_path: str, level_path: str, viewport_id: int,
                               request_id: str, width: int = 1280, height: int = 720,
                               warmup_frames: int = 32, timeout_seconds: int = 60) -> dict:
        """Start a bounded fixed-resolution PNG capture; pending is NOT screenshot completion.

        Example: request_id='lighting_A_001', viewport_id=0, width=1280, height=720.
        Query get_scene_task_status with the same ID until state=completed or failed.
        Same ID returns its receipt without re-executing; use a NEW ID for a NEW image.
        Refuses existing output files. Output is Saved/Screenshots/UnrealMCP/<id>.png.
        Waits for detectable asset/shader compilation then 1..300 rendered warmup frames;
        this does not guarantee Lumen/auto-exposure convergence. Timeout 5..120 seconds.
        On completion returns validated PNG dimensions and real file path. Fixed camera,
        exposure and resolution are needed for A/B comparisons. 32 cached tasks/session.
        Editor restart expires status; inspect existing files before retrying.
        Updated native plugins use an offscreen render target independent of the editor
        window size; no image stretching or resizing of the visible viewport. Receipts
        distinguish requested_width/height, actual_width/height (render target; final
        image on success), and source_viewport_width/height. Legacy width/height match
        actual_width/height. render_target='offscreen' identifies this implementation.
        aspect_ratio_policy='camera_letterbox' preserves a locked camera's aspect
        constraint with black bars, never crops or changes camera settings. Otherwise
        'viewport_projection' keeps UE viewport projection behavior at the new aspect.
        Source viewport pointer is restored after each draw, including failed tasks.
        """
        return call_scene("capture_scene_viewport", dict(project_path=project_path, level_path=level_path,
            viewport_id=viewport_id, request_id=request_id, width=width, height=height,
            warmup_frames=warmup_frames, timeout_seconds=timeout_seconds))

    @mcp.tool()
    def get_scene_task_status(ctx: Context, project_path: str, request_id: str) -> dict:
        """Read capture receipt without restarting work, e.g. request_id='lighting_A_001'.

        Requires the exact project path. Check state, success and file_path. Pending
        only means accepted. Failed/unknown status is never proof of rollback.
        """
        return call_scene("get_scene_task_status", dict(project_path=project_path, request_id=request_id))