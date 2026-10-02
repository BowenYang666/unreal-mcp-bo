# Scene Tools

Default grouped mode exposes `scene_search`, `scene_call_read` and `scene_call_write`.
The 20 operation names used below are internal in this mode, not separate public tools.
`MCP_TOOL_MODE=direct` retains their original public names for compatible scripts.
Disable the entire category with `MCP_SCENE_ENABLED=0`. Read-only mode retains search
and read dispatch, but no write entry point; hidden operations cannot be discovered
or invoked through the read route. Grouping only needs a Python MCP restart, not a UE
rebuild. Underlying native features still require the matching deployed plugin.

## Discovery And Calls

```text
scene_search(tool="get_editor_context")
scene_call_read(tool="get_editor_context", arguments={})

scene_search(query="exposure")
scene_search(tool="patch_scene_target")
scene_call_write(tool="patch_scene_target", arguments={
  "project_path":"E:/Projects/Test/Test.uproject",
  "level_path":"/Game/Test",
  "actor_path":"/Game/Test.Test:PersistentLevel.PostProcessVolume_0",
  "changes":[{"path":"Settings.AutoExposureBias","value":0.5}]
})
```

Use actual selectors from inspection, not the illustrative paths above. Search returns
`input_schema`, `effect` and `call_tool` on exact lookup. Empty query browses summaries
with offset/limit; cache discovered contracts rather than fetching all 20 schemas.
Use the current client's server prefix. Do not inject `ctx`, bypass filters or retry
mutations on transport errors. Native commands, defaults and receipts are unchanged.

## Status

Temporary editor visibility was built and deployed to SkillTest and NodeFall on
2026-09-30. SkillTest passed the temporary-visibility, instance-patch and Outliner-folder
native tests; the full Python suite passed 65 tests. NF's running plugin reported
`editor_visibility_contract=1` and a real grouped MCP hide preview for an existing BP
returned `would_modify=true`, `modified=false`; before/after readback was identical
and dirty packages remained empty. No business actor was actually hidden or restored.
Only four Scene files were deployed to NF; its local extensions and migration module
were left untouched. This visibility update does not deploy the separate texture
migration fix to NF. Full profiles still expose 70 tools; NF's existing filters expose
62, with three Scene entry points in either case.

The Outliner folder extension was built and deployed to SkillTest on 2026-09-29.
The actual loaded plugin reports `folder_contract=1` through both client profiles.
Six targeted native tests passed, including folder persistence and legacy compatibility.
OpeningRescue folder inspection and a same-folder dry-run passed without modifying
or saving the scene. No OpeningRescue actors have been organized by this extension.

NodeFall was also updated on 2026-09-29 at the user's request. `NodeFallEditor`
compiled successfully against UE 5.7.4. The running editor on default port 13090
confirmed `folder_contract=1` through its existing Claude profile; context, actor
listing and `BP_NFPlayerController` reads passed with no dirty packages. Its original
node-tool filter and authoring permissions remain unchanged (62 public tools).
NF-specific navigation validation, tests and DirectoryWatcher dependency were retained;
this target is intentionally not a byte-identical copy of the shared plugin.
403 protected project-file hashes and the 1276-file Content inventory stayed unchanged.
No NF scene writes or test assets were created. Claude CLI reports Pending approval:
the user must approve the project server in Claude; the standalone stdio smoke test
does not replace that trust step. Only the existing Claude profile was used; no new
NF VS Code configuration was created. Use separate explicit port profiles if running
NF and SkillTest concurrently; both ordinary single-editor profiles default to 13090.

The previously validated scene baseline is deployed to SkillTest. On 2026-09-29, the user confirmed that
interactive Play testing passed: player spawn, movement, and floor/object collision
all behaved normally. The tested scene workflow is accepted within the documented
support boundaries below.
Native instance/PPV patch and actor/material-slot automation pass on
UE 5.7. On 2026-09-28, SkillTest 5.7.4 passed actual MCP context, fixture creation,
fixed-camera 1280x720 A/B capture (RectLight 2000 to 8000), target-map save and reopen.
The user confirmed identical framing, brighter B, and no obvious image anomaly.
Pixel sampling independently found 30.08% significantly changed samples. Reopened
Intensity=8000, SourceWidth/Height=150 and Mobility=Movable; map remained clean.
Artifacts are in MCPGameProject/Saved/SceneTools and SkillTest/Saved/Screenshots/UnrealMCP.
Imports, placement manifests, controlled undo, CineCamera fields and SkyLight recapture
are implemented. FBX/glTF/GLB/PNG import fixtures and manifest/undo regressions pass
in isolated real-RHI automation. The deployed SkillTest plugin also passed all seven
scene tests. Actual MCP calls verified guarded undo, manifest no-op repeat, CineCamera
fields, PlayerStart creation and a queued SkyLight recapture request.
Material v2 preview preserved the on-disk MI SHA256; the user confirmed A green and
B red. Clearing Tint restored its inherited value. After saving only the MI, imported
mesh and test map, a fresh editor process verified the cleared override, valid shader
resource and both mesh/material assignments with clean packages.
The GUI FBX import produced 100x100x100 cm bounds, 48 triangles, 3 UV channels,
1 material slot and 1 simple collision element. Simple/complex traces hit the placed
imported cube, and a separate trace hit the floor. These automated checks were
followed by the user's successful interactive Play test on 2026-09-29; trace results
alone were not treated as proof of playable behavior.
Do not infer deployment from Python schemas; the earlier SkillTest acceptance run
did not update NF. The separately authorized NF update is recorded above.
Blueprint construction-script instances are not writable through the property patch.

### Acceptance Record

- Project: `E:/04_UnrealProjects/UE5Test/SkillTest/SkillTest.uproject`, UE 5.7.4.
- Map: `/Game/__Dev/SceneTools/SceneAcceptance_20260928`; configured MCP port 13091.
- Play: user-confirmed pass on 2026-09-29 for spawn, movement and floor/object collision.
- Receipt: `MCPGameProject/Saved/SceneTools/Acceptance_20260928.json`.
- Pictures: `SceneAB_20260928_A01/B01.png` (lighting) and `SceneMI_20260928_A01/B01.png`
  under `MCPGameProject/Saved/SceneTools/`.
- Source changes remain uncommitted. Existing cached MCP clients need restart/reconnect
  for the new schemas. The acceptance run above used port 13091; both SkillTest client
  configs now target the ordinary editor default 13090, verified by an actual read-only
  context call. No launch-time port override is needed for this single-editor profile.
  Concurrent editors still require distinct explicit ports and matching client profiles.

## Identity And Safety

Start by discovering and calling `get_editor_context` through the Scene read route.
Confirm `project_path`, `level_path`, mode,
dirty packages, viewport IDs, post-process volumes and exposure diagnostics.
Every other scene call requires the project path; map operations also require the
current map package path. Nothing selects another project or opens another map.
PIE/Simulate writes, external actor packages and streaming-level targets are refused.
Actor creation and saving also refuse World Partition. No arbitrary script/console API.

Use `list_scene_actors(project_path, level_path, filter="", offset=0, limit=50)`.
Limit is 1..200; the response includes exact actor object paths, classes and transforms.
With native folder contract 1 it also includes `folder_path` (empty for world root),
`scene_managed` and `managed_namespaces`; these tags, not folders, describe ownership.
Labels are descriptive, never fallback selectors. `inspect_scene_target` without a
component name returns owned components. Pass that exact component name/path to
inspect its domain-allowlisted properties, values, schemas and units.

## Outliner Folders

Availability: requires native `get_editor_context.folder_contract == 1`. Creation and
manifest wrappers check this on each folder-bearing request and refuse old/unavailable
plugins with `native_folder_contract_required` before sending a mutation. Existing
requests without folder fields keep their original behavior. The new bulk command is
write-only inside Scene grouping; the public Scene surface remains three entry points.

These are World Outliner directories, NOT Content directories or Actor parents.
An example root for tool-authored environment actors is:

```text
OpeningEnvironment/
  Architecture/
    MainCourtyard/
    OppositeStreet/
    Background/
  Props/
  Vegetation/
  Lighting/
  Cameras/
```

The root can be collapsed manually in the Outliner. Automatic UI collapse is not
implemented. Only directories used by assigned actors are created. No parent Actor,
mesh merge or editor default creation folder is introduced. Actor attachments and
folder membership are independent; moving a parent does not move attached children.

- `manage_scene_actor(operation="create", ..., folder_path="OpeningEnvironment/Lighting")`
  assigns the newly created actor. Omit the parameter for legacy behavior; empty string
  explicitly selects the world root. It is refused for transform/delete operations.
- `apply_scene_manifest` accepts `manifest.folder_root="OpeningEnvironment"` and an
  optional per-object `folder="Architecture/MainCourtyard"`. Missing/empty object folder
  selects the declared root. Omit BOTH root and object folder fields to preserve existing
  folders. An object folder without an explicit nonempty root is rejected. Folder-only
  updates do not rewrite transforms, attachments or materials; idempotence is preserved.
- `set_scene_actor_folders` accepts 1..200 exact assignments in `changes`, defaults
  `dry_run=True` and `managed_only=True`, and uses one unsaved undoable transaction.
  Each item requires `actor_path` and `folder_path`, with optional `expected_folder_path`.
  Unsupported targets, duplicates, stale old folders or invalid paths abort all preflight.
  Empty `folder_path` clears membership but never removes ownership tags.

Discover the bulk contract before calling its returned write endpoint:

```text
scene_search(tool="set_scene_actor_folders")
scene_call_write(tool="set_scene_actor_folders", arguments={
  "project_path":"E:/Projects/Test/Test.uproject",
  "level_path":"/Game/Test",
  "changes":[{
    "actor_path":"/Game/Test.Test:PersistentLevel.MCPScene_Wall",
    "folder_path":"OpeningEnvironment/Architecture/MainCourtyard",
    "expected_folder_path":""
  }],
  "dry_run":true,
  "managed_only":true
})
```

Replace illustrative selectors with reviewed creation receipts/manifests and actual
inspection. Never sweep all static meshes or assume every actor already in a folder is
tool-owned. Legacy placements without `UnrealMCP.SceneManaged` require reviewed exact
selectors and explicit `managed_only=False`; selected BP instances can be organized
without changing their construction-script properties. General property-patch BP
restrictions remain unchanged. Split larger sets into reviewed batches of at most 200;
each has a separate transaction, not an atomic multi-batch undo.

Paths use single forward slashes, maximum 512 characters and 16 segments. Empty/dot/
parent/None segments, padded segment whitespace, backslashes, control characters and
`:*?"<>|` are rejected, not silently normalized. Names use UE FName identity (case
insensitive); this API is not a case-only folder rename operation. No absolute paths.

Folder writes support only a non-partitioned single persistent level without external
Actor/object storage. They do not save, change transforms/materials/collision/tags, or
move unselected attached actors. Dry runs and identical assignments make no changes.
Inspect per-item `before`, `requested`, `after` and `would_modify`; actual apply readback
failure can leave partial work, accurately marked `modified` with an undo ticket when
available. A timeout is unknown: re-inspect rather than blindly retry. Undo uses the
existing top-transaction guard; map saving remains a separate explicitly confirmed step.

Passed native regressions: folder read/create and validation, full-batch rejection,
dry-run/no-op, selected/unselected BP behavior, unchanged transforms/material/collision/
ownership/default folder, guarded undo, manifest directories and save/reopen persistence.
`OutlinerFolders`, `FolderManifest` and `FolderPersistence` passed in the deployed
SkillTest DLL. `InstancePatch`, `Lifecycle` and `PlacementManifest` also passed.
The initial Outliner test incorrectly filtered by object name instead of display label;
the test was corrected with a result-count guard, rebuilt, and the same three folder
tests reran successfully. No native folder behavior change was required by this repair.

### Folder Deployment Record

- Target update: `E:/04_UnrealProjects/UE5Test/SkillTest/SkillTest.uproject`.
- Engine: UE 5.7.4, `C:/01_install/UE5_7/UE_5.7`.
- Source: this repository's `MCPGameProject/Plugins/UnrealMCP`.
- Destination: `SkillTest/Plugins/UnrealMCP`; all 43 source/descriptor files match.
- Build: `SkillTestEditor Win64 Development`, clean plugin build succeeded in 347.75s;
  test-only correction incrementally rebuilt successfully in 4.08s.
- Native logs: `SkillTest/Saved/Logs/SceneFolders-Deployment-Retry.log` and
  `SceneFolders-LegacyRegression.log`; six explicit Success completions, no fatal/ensure.
- Prior Python regression: 63 tests passed. Public catalog remains 70 tools, three
  Scene entry points, 19 internal Scene operations (138 total direct operations).
- Old plugin backup: `SkillTest/Saved/UnrealMCP-before-folders-20260929-181215`.
  Guard: `SkillTest/Saved/UnrealMCP-Folders-DeploymentGuard.clixml`, 873 protected files.
- GUI: SkillTest PID 40076 at verification, `/Game/Worlds/Opening/Maps/OpeningRescue`,
  1385 actors. Listener `127.0.0.1:13090` uses the normal default, not a port override.
  Runtime PIDs/counts are dated observations; recheck before later operations.
- Clients: actual `SkillTest/.vscode/mcp.json` and `SkillTest/.mcp.json` both verified
  with fresh stdio sessions, full-authoring mode and grouped discovery. Existing GUI
  MCP sessions still need restart/reconnect; client trust/approval was not changed.
- Real MCP smoke: context capability 1; actor folder inspection with
  `folder_editable=true`; one exact same-folder dry-run returned `modified=false`,
  `saved=false`; `BP_ThirdPersonCharacter` read succeeded; dirty packages stayed empty.
- Default ThirdPerson startup map correctly rejects external-actor targets. The clean
  session was gracefully reopened on the user's original OpeningRescue map; no default
  startup configuration was changed. No business-map organization or automatic UI
  collapse was performed, and NF was not touched.

## Temporary Editor Visibility

`set_scene_actor_visibility` is a separate Actor-level operation for Outliner-style
temporary hiding, including existing Blueprint instances. Require native context
`editor_visibility_contract=1`; a discovered Python schema alone is not proof of a
deployed native command. It remains behind the existing Scene write endpoint and is
absent in read-only mode. Older plugins reject the new command instead of silently
ignoring it. General reflected Blueprint property-patch restrictions are unchanged.

List or inspect the exact actor first. Both return `editor_visibility` with:

- `temporary_hidden`: the actor's own temporary editor flag, used by the setter and
  its optional `expected_hidden_in_editor` conflict check.
- `temporary_hidden_in_hierarchy`: UE's query including the ChildActor parent chain.
- `editor_hidden`: UE's editor-hidden query, also affected by layers/level state.
- `hidden_in_game`: read-only gameplay hiding state, never changed by this command.

None of these alone guarantees rendered pixels: component visibility, viewport modes
and normal parent rendering rules still apply. Clearing the temporary flag does not
clear layer/level hiding or force components visible.

```text
scene_search(tool="set_scene_actor_visibility")
scene_call_write(tool="set_scene_actor_visibility", arguments={
  "project_path":"E:/Projects/Test/Test.uproject",
  "level_path":"/Game/Test",
  "actor_path":"/Game/Test.Test:PersistentLevel.BP_ExistingSky_C_0",
  "hidden_in_editor":true,
  "expected_hidden_in_editor":false,
  "dry_run":true
})
```

Use the returned `call_tool` with real inspected paths; these are only examples.
Review the preview, then explicitly set `dry_run=false` to apply. Restoring means
setting `hidden_in_editor` to the earlier `before.temporary_hidden`, preferably with
the currently observed value as `expected_hidden_in_editor`. It is a setter, not a
toggle; identical requests are no-ops and stale expected state aborts before mutation.

Only one exact actor is selected, no label/prefix fallback, bulk sweep or recursive
child writes. Native and Blueprint instances do not need a managed ownership tag.
Wrong project/map, PIE/Simulate, World Partition, external packages, sublevel targets,
component selectors and unknown parameters are refused. Capture/import tasks block it.

The implementation calls `SetIsTemporarilyHiddenInEditor` and redraws editor viewports.
It does not call `Modify` or `PostEditChange`, rerun construction scripts, save, dirty
packages, edit Blueprint assets, or replace/recreate components. It does not change
attachments, component visibility, gameplay hidden state, collision, tick or lighting
properties. It does not recursively set another actor's flag, but UE may still hide
ChildActor rendering through normal parent rules. Hiding is NOT a gameplay disable:
BP logic, timers and gameplay effects are not stopped, and Play is not promised hidden.

Receipts contain `before`, `after`, `requested_hidden_in_editor`, `would_modify`,
`modified`, `dry_run`, `package_dirty_before` and `package_dirty`; `saved=false`,
`session_only=true`, `undo_supported=false`. This session-only state is not a saved
map setting and is not added to the map undo stack; use explicit restoration instead
of `undo_scene_edit`. On readback failure inspect the actual state; a timeout is an
unknown outcome and never permission to blindly retry or delete the actor.

Native regression `UnrealMCP.Scene.TemporaryEditorVisibility` covers an unmanaged BP,
preview/identity/conflict rejection, hide/restore/no-op, unchanged clean and dirty map
states, unchanged Blueprint package and undo stack, component identity, attachments,
mesh/material/visibility/collision/tick, unselected light and child flags, and layer
hiding remaining visible in the receipt after the temporary flag is restored.

### Visibility Deployment Record

- UE 5.7.4; `SkillTestEditor` and `NodeFallEditor` builds both succeeded. SkillTest
  target-native tests all completed with Success in `Saved/Logs/Scene-Visibility-Deployment.log`.
  Repository native test log: `MCPGameProject/Saved/Logs/Scene-TemporaryVisibility.log`.
- SkillTest was already closed. Its GUI was not launched into NF's occupied default
  port; validation used an isolated `-UnrealMCPNoServer` editor process and test map.
- NF was checked through `get_editor_context` and `get_unsaved_changes` immediately
  before `close_editor`: zero unsaved packages, idle editor, `saved_count=0` on close.
  It was reopened on the actual pre-close map `/Game/Dev/Enemies/Maps/L_cyberpunk_start1_dev`,
  not on the different map mentioned in its old process command line.
- New NF process PID 46908 at acceptance; normal `127.0.0.1:13090`, existing Claude
  config/filters unchanged. Actual BP preview and `BP_NFPlayerController` read passed.
  Existing MCP clients need restart/reconnect; client trust/approval was not changed.
- Backups: SkillTest `Saved/UnrealMCP-before-visibility-20260930-161746` and NF
  `Saved/UnrealMCP-before-visibility-20260930-162524`. `UnrealMCP-Visibility-Guard.clixml`
  under each project's Saved folder protects 604 SkillTest and 1002 NF files, including
  NF plugin files outside the four explicitly updated Scene files. No save, deletion,
  sky replacement or live visibility apply was performed in either business map.

## Preview Edits

All new scene edits are unsaved by default; they have no implicit Save All.
`patch_scene_target` validates the entire 1..64-field batch before a transaction.
Duplicate/unsupported fields and stale `expected_value` values fail before mutation.

```text
patch_scene_target(
  project_path="E:/Projects/Test/Test.uproject", level_path="/Game/Test",
  actor_path="/Game/Test.Test:PersistentLevel.MCPScene_KeyLight",
  component_name="LightComponent0",
  changes=[{"path":"Intensity", "value":1200, "expected_value":800}]
)
```

Copy selectors from inspection rather than assuming generated component names.
Intensity is on the light component, not the actor. Local lights report `IntensityUnits`;
directional intensity is lux. Positions/dimensions are normally cm, rotations degrees,
temperature Kelvin, LightColor sRGB byte R/G/B/A. Do not equate Blender watts with UE
intensity. Extended luminance-range projects interpret min/max exposure as EV100;
the context and field units report the actual project setting.

PPV examples: `Settings.BloomIntensity`, `Settings.AutoExposureBias` and
`Settings.bOverride_BloomIntensity`. Camera settings use `PostProcessSettings.`.
A value patch enables its paired override unless explicitly included in the batch.
Set an override to false to clear it; this does not replace the entire settings struct.
Other volumes/camera/editor exposure can still dominate the final image.

Receipts contain before/requested/after values and dirty/save state. A timeout means
UNKNOWN, never rollback. Reinspect before retrying. Native editor Undo is supported
for tested edits. `undo_scene_edit` requires the last receipt's transaction_id and
the same map/world. It refuses another user/tool operation on top of the undo stack,
never searches backwards, never saves, and cannot reuse a consumed ticket.

`manage_scene_actor` supports create/transform/delete. Creation requires an actor_type
and unique managed_id; internal name is `MCPScene_<managed_id>`. Collisions fail without
suffixing. Delete only accepts tool-tagged actors. Types include Rect/Point/Spot/
Directional/Sky lights, SkyAtmosphere, HeightFog, PPV, StaticMeshActor, CameraActor,
CineCameraActor and PlayerStart. CineCamera supports CurrentFocalLength (mm),
CurrentAperture (f-stop), Filmback.SensorWidth/Height (mm), FocusSettings.FocusMethod
and FocusSettings.ManualFocusDistance (cm). Actual values can be constrained by lens
settings; read back. `recapture_scene_skylight` queues native RecaptureSky on one
component; acceptance is not proof of a completed capture frame.

`get_scene_mesh` returns all slots with names, indices and override flags.
`set_scene_mesh` optionally replaces an existing mesh and assigns explicit slots:
`materials=[{"slot_index":1,"material_path":"/Game/M_Wall.M_Wall"}]`.
Use exactly one slot_index or slot_name. Empty material_path clears that override.
All assignments preflight before modifying; no implicit slot zero.

`save_scene_level(..., confirm_all_changes_in_map=True)` saves only the confirmed
current map, including ALL pre-existing changes in that map. Default false refuses.
Review dirty packages first. Unsupported multi-package maps are refused, not partially
saved. Save failure leaves unsaved changes in memory.

## Viewport And Capture

Read `get_scene_viewport` for an explicit viewport_id from context. This is a registered
perspective Level Viewport, not the active asset preview. `set_scene_viewport` accepts
location/rotation/FOV or an exact camera_path, plus optional game_view. It reports
editor fixed exposure and camera PP metadata without silently resetting overrides.
Viewport-only changes are not level transactions.

`capture_scene_viewport` requires a unique request_id and fixed width/height (64..4096).
It returns a pending receipt. Query `get_scene_task_status` with the same project and ID;
only `state=completed` with success and a file_path proves file completion. Existing IDs
return their receipt, and existing output files are never overwritten. The bounded
cache holds 32 tasks per editor session; restarting expires status, not output files.

The task waits for detectable asset/shader compilation and async loading, draws
1..300 warmup frames, renders the selected viewport client to an independent offscreen
target, and validates PNG dimensions and written byte count. The visible viewport is
not resized; its client pointer is restored after each draw. Success/failure releases
the offscreen target without altering source size, fixed-size mode or camera settings.
Changes to map, camera lock/pose/aspect constraint, exposure or Game View invalidate
capture; external user edits are not reverted. MCP commands that could interfere
are refused during capture. Timeout is 5..120 seconds. Rendering/resource convergence,
particularly Lumen/eye adaptation, is not guaranteed by a finite warmup count.

Accepted task receipts distinguish `requested_width/height`, `actual_width/height`
(allocated target dimensions; PNG dimensions on success), and `source_viewport_width/height`
(source at submission). Legacy `width/height` now match actual output dimensions, not
the editor window. `render_target=offscreen` identifies the updated native implementation.
`source_viewport_unchanged` reports whether the source pointer and dimensions still match
at completion. This fix requires an updated native plugin, not only Python reconnection.

If a locked camera has `bConstrainAspectRatio=true`, `aspect_ratio_policy=camera_letterbox`
preserves its framing with centered black bars (letterbox/pillarbox) when the output
ratio differs. Camera aspect/FOV are not modified, and pixels are not stretched/cropped.
Free or unconstrained views use `aspect_ratio_policy=viewport_projection`: UE's existing
projection/aspect-axis behavior applies to the requested dimensions.

Regression group `UnrealMCP.Scene.CaptureResolution` checks smaller/larger/native sizes,
portrait/wide outputs, locked/free views, source2345x1833 to output1440x1000 at48warmup
frames, existing fixed-size source mode, and source-change/write-failure cleanup.
It decodes the real PNG and checks nonblack scene pixels, output dimensions and black bars.

Output: `<Project>/Saved/Screenshots/UnrealMCP/<request_id>.png`. A/B comparisons must
use the same pose, resolution, exposure and render settings. File creation alone is
not visual acceptance: inspect actual images and pixel changes.

## Asset Imports

`inspect_scene_asset(project_path, asset_path)` adds render-data inspection for
StaticMesh and Texture2D. For reflected values, use get_class_properties first.
The specialized reader reports cm bounds, render LOD vertices/triangles/UV channels,
material slots, simple collision element count, collision complexity, Nanite state,
or texture dimensions/sRGB/compression. `compiling=true` means mesh data is not ready.

`import_scene_asset` requires project_path, unique request_id, absolute source_file,
and a NEW destination_path directory below /Game. Existing directories/assets are
refused. Default save=false; explicit saving targets only returned destination assets.
FBX uses the static-mesh factory, preserves separate meshes and source unit conversion,
disables automatic material/texture import, and requests simple collision/lightmap UVs.
FBX-only import_uniform_scale defaults 1. glTF/GLB use the installed engine importer
with standard meter/Y-up conversion; inspect actual outputs and bounds.
PNG accepts srgb and compression=default|normalmap|masks; masks/normalmap require false.

The pending receipt is not completion. Query get_scene_import_status. JSON intent and
results persist under Saved/UnrealMCP/SceneImports. Repeating an ID returns its receipt.
Overdue work remains locked until native completion; a blocking engine factory can
temporarily block status queries. There is no guaranteed hard cancellation or rollback.
Restarted incomplete tasks report interrupted_unknown, never blindly resume/reimport.
Inspect returned assets, compilation, dimensions, slots and collision before placement.

### Import Receipt Cache

The import cache holds at most 32 in-memory receipts, not 32 imports per editor session.
When space is needed, the oldest successfully persisted completed/failed receipt is
evicted. Its disk journal remains available to status queries and same-ID replay;
neither operation invokes the importer or refills the memory cache. Active, overdue,
unknown and non-durable records are never evicted. A changed, missing or unreadable
journal also prevents eviction of the corresponding cached record.

Journals are written to a same-directory temporary file and replaced without deleting
the previous journal first. A failed final replacement retains the in-memory outcome
and previous journal. `receipt_persisted=false` and `persistence_error` report that
failure without changing the actual `modified`/`saved` asset outcome. A durable receipt
for a save=false import can be evicted; eviction never saves or unloads its assets.
After restart such a receipt describes historical import results, not proof that
unsaved assets survived. Do not manually delete journals to make room.

Admission errors retain stage=preflight and no-mutation flags:
- `error_code=scene_task_busy`: another scene task is active.
- `error_code=receipt_cache_full`: all cache slots are protected; no safe eviction
  candidate remains. `cached_receipts` and `cache_limit` report the counts.
- Unreadable/invalid disk receipts instead return `receipt_unavailable` at
  stage=receipt_load with state=interrupted_unknown, without claiming rollback.

The native plugin must be rebuilt/deployed for this fix; Python reconnect alone cannot
update the running DLL. Public tool names, parameter signatures and counts are unchanged.
Regression tests: `UnrealMCP.Scene.ImportReceiptCapacity` (continuous 40 and two batches
of 20 imports) and `UnrealMCP.Scene.ImportReceiptSafety` (isolated -UnrealMCPNoServer).
Cold replay: `UnrealMCP.Scene.ImportReceiptColdRead` with
`-MCPReceiptReplay=<request_id>` from a completed capacity test in a fresh editor process.

## Placement Manifests

`apply_scene_manifest` defaults dry_run=true and never saves. Review items then apply
the same input with dry_run=false. Version 1 takes namespace, source_hash, units=cm,
up_axis=Z, handedness=left and 1..64 objects. Convert DCC transforms explicitly first.
Each object requires stable id, existing mesh path, location, rotation (pitch/yaw/roll
degrees) and scale. Transforms are local to optional parent_id in the same manifest.
Optional label and materials use the same explicit slot selectors as set_scene_mesh.
Parent cycles, unknown references and unmanaged name collisions fail before mutation.

Stable names MCPScene_<namespace>_<id> plus ownership tags support idempotent updates.
Optional expected_source_hash guards existing actors. Same input produces no changes;
omitted objects are not deleted. Source hash is caller provenance, not proof that actor
properties were unchanged by another user. No mesh combining. One map transaction
covers application, with requested/before/after per item and a controlled undo receipt.
Rare native apply failures can leave partial changes; inspect items before retrying.

## Multiple Editors

Default TCP endpoint remains `127.0.0.1:13090`. Start a second editor with
`-UnrealMCPPort=13091` and configure its Python MCP server environment with
`UNREAL_MCP_PORT=13091`. Native command-line port takes precedence over the same
environment variable. Invalid ports are refused. This is routing, not authentication.
Always verify returned project identity before mutation.