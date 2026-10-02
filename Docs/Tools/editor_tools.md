# Unreal MCP Editor Tools

Tools for inspecting editor state, reading logs, saving/opening assets, and managing levels. Actor operations are documented separately in [Actor Tools](actor_tools.md).

`focus_viewport` and `take_screenshot` exist as legacy C++ commands but are not registered as Python MCP tools and are therefore not part of the supported tool surface.

## Editor Tools

### get_editor_logs

Read Unreal Editor output log entries directly from a local file; the editor
does not need to be running. Supply `log_path` or set `UNREAL_PROJECT_LOG`.
The tool does not automatically discover the active project's log.

At least one time selector is required: `start_time`, `end_time`, a positive
`relative_seconds_ago`, or a nonzero `pie_session_index`.

**Parameters:**
- `count` (int, optional) - Maximum matching entries, taken from the newest entries and returned oldest-first (default: 100; use a positive value).
- `verbosity` (string, optional) - Severity threshold: `fatal`, `error`, `warning`, `display`, `log`, `verbose`, `veryverbose`, or `all` (default). For example, `warning` includes warnings, errors and fatal entries.
- `category` (string, optional) - Case-insensitive exact category match, e.g. `LogTemp`.
- `search` (string, optional) - Case-insensitive regular expression over the message, e.g. `Spawn|Destroy`. Invalid regex falls back to a literal substring search.
- `log_path` (string, optional) - Override the log file path. Otherwise uses `UNREAL_PROJECT_LOG` env var.
- `start_time` / `end_time` (string, optional) - Inclusive second-resolution bounds in UE format `YYYY.MM.DD-HH.MM.SS` or ISO `YYYY-MM-DDTHH:MM:SS` (a space instead of `T` is also accepted).
- `relative_seconds_ago` (int, optional) - Positive values replace `start_time` with the local system time minus this many seconds; default `0` disables it.
- `pie_session_index` (int, optional) - `-1` selects the latest PIE session, `-2` the previous one; default `0` disables this selector. Overrides the other time bounds. The window ends at the next PIE start or EOF, not necessarily at the selected session's end.

Time values are compared to timestamps as written in the log, without timezone
conversion. If UE logs use UTC but the machine's local clock does not, prefer
explicit bounds copied from the log or `pie_session_index` over relative time.

**Returns:**
- `total_lines`, `returned`, `log_file`, `time_window`, and `logs`.
- Each `logs` entry has `timestamp`, `category`, `verbosity`, and `message`.
- PIE queries also include `pie_sessions_found` and `pie_session_used`.
- An optional `warning` explains when category/search/severity filters removed all entries in the time window.
- Validation/read failures return `success=false` and `message`; a successful result need not contain a `success` or `status` field.

```python
get_editor_logs(
  log_path="D:/UnrealProjects/MyProject/Saved/Logs/MyProject.log",
  pie_session_index=-1,
  verbosity="warning",
  search="Spawn|Destroy")

get_editor_logs(
  log_path="D:/UnrealProjects/MyProject/Saved/Logs/MyProject.log",
  start_time="2026.09.25-00.00.00",
  end_time="2026.09.25-00.05.00")
```

### get_unsaved_changes

Check for unsaved changes in the Unreal Editor.

**Parameters:** None

**Returns:**
- `total_unsaved`, `unsaved_content_count`, and `unsaved_map_count`.
- `unsaved_content` and `unsaved_maps` are arrays of objects with `name` and `path`, not bare strings.

### close_editor

Gracefully close the Unreal Editor. Closes all open asset editor tabs first to avoid crashes, then schedules engine exit.

**Parameters:**
- `save_all` (bool, optional) - If true (default), saves all dirty packages before closing. Set to false to close without saving.

**Returns:**
- Dict with closing status, `saved_count`, and any `failed_saves`

**Example:**
```python
get_unsaved_changes()
close_editor(save_all=True)
```

Review the dirty-package list before closing. `save_all=True` saves all dirty
packages, not just assets touched by the current task. Use `False` only after
confirming it is safe to close without saving.

### set_component_physical_material

Set a primitive component's physical material override in the current editor
world, using `SetPhysMaterialOverride` to update existing collision immediately.
Parameters: `level_path`, `actor_name`, `component_name`, `physical_material_path`,
optional `expected_value`, and `save=False`. Actor name or label must be unique;
component name is exact. The level must exactly match the current editor world;
PIE, external-actor packages and components outside that world's package are
unsupported. This tool does not edit Blueprint templates.

```python
set_component_physical_material(level_path="/Game/Maps/Test",
  actor_name="Wall", component_name="StaticMeshComponent0",
  physical_material_path="/Game/Physics/PM_Metal", expected_value="", save=False)
```

An empty reference clears the override; `expected_value=""` expects none.
Default `save=False` marks the map dirty. `save=True` refuses pre-existing dirty
changes and saves only this map. Returns `modified`, `saved`, `package_dirty`,
`before`, `after`, `target`, `component`, `success`. Does not rebuild mass/inertia.
Failure after mutation need not roll back; timeout is indeterminate.

### trace_physical_material

Read-only, current editor-world Visibility trace with physical material return
enabled. Parameters: exact `level_path`, finite 3-number `start`/`end` vectors and
`trace_complex=False`. No PIE or arbitrary-world selection. It remains available
in read-only mode; both tools above require the Editor category.

```python
trace_physical_material(level_path="/Game/Maps/Test",
  start=[0, 0, 200], end=[0, 0, -200], trace_complex=False)
```

Returns `success`, `blocking_hit`, `actor`, `component`, `physical_material` and
the stable enum `surface_type`. Always check the hit and material identity as
well as surface type. A miss or default surface does not prove assignment.
Use complex traces when testing a mesh triangle's Material/MI; simple collision
may use BodySetup or component settings instead. These calls do not change those
collision settings to force the expected result.

## Asset & Level Management

### save_asset

Save an asset to disk.

**Parameters:**
- `asset_path` (string) - Asset path to save, e.g. `/Game/VFX/NS_Explosion`

### rename_asset

Rename an asset while keeping it in the same Content Browser folder. Unreal updates loaded references; an existing destination is never overwritten.

**Parameters:**
- `asset_path` (string) - Full source path, e.g. `/Game/ThirdParty/UE5_TPS_Anim/Mannequins/Anims/Rifle/Walk/SK_Walk`
- `new_name` (string) - Asset name only, without a folder or object suffix, e.g. `SKM_Walk`

### move_asset

Move an asset to another Content Browser folder without changing its name. The destination folder is created when needed; an existing destination asset is never overwritten.

**Parameters:**
- `asset_path` (string) - Full source asset path, e.g. `/Game/ThirdParty/Animations/A_Walk`
- `destination_folder` (string) - Full destination folder path, e.g. `/Game/Characters/Animations`

### duplicate_asset

Duplicate one content asset with Unreal's native `DuplicateAsset` API and save
only the new copy with `SaveLoadedAsset`. No filesystem copying, recursive
dependency duplication, reference replacement, source rename, or redirector creation.

**Parameters:**
- `source_asset_path` (string) - Existing full `/Game/.../AssetName` package path.
- `destination_asset_path` (string) - New full `/Game/.../AssetName` package path, including the name.

Object suffixes, file extensions, folder-only paths, identical paths, existing
destination packages (including unsaved packages), redirector sources, maps, and
PIE execution are rejected. Unreal performs the authoritative package-name validation.
Texture pixels and settings are preserved by native duplication; dependencies
remain referenced at their existing paths. No original or unrelated package is saved.
The wrapper restores the source texture's `OodleTextureSdkVersion` before saving:
UE's new-texture initialization can otherwise upgrade this compression setting
even when duplicating an unchanged texture. Only the new copy is updated.

```python
duplicate_asset(
  source_asset_path="/Game/MarketPlugins/Realistic_Starter_VFX_Pack_Vol2/Textures/T_Impact_Flare",
  destination_asset_path="/Game/ThirdParty/Realistic_Starter_VFX_Pack_Vol2/Textures/T_Impact_Flare")
```

**Returns:** `success`, `operation="duplicate"`, `source_path`, `destination_path`,
`asset_class`, `object_path`, `duplicate_created`, and `saved`. Failure returns
`success=false` and a `message`. If saving fails after duplication,
`duplicate_created=true` and `saved=false`; an unsaved copy may remain in memory.
The tool does not delete it or overwrite it on retry. Resolve that copy explicitly
before retrying. A transport timeout is indeterminate; inspect the destination
before retrying rather than assuming nothing happened.

These three asset operations require `MCP_ASSET_ENABLED=1` (default when unset)
and are removed by read-only mode. They are independent of `MCP_EDITOR_ENABLED`.

#### Verification

From `Python`, run `uv run python -m unittest discover -s tests -p test_duplicate_asset.py -v`.
For saved texture comparisons, launch the target editor with:

```text
-MCPDuplicateSourceRoot=/Game/MarketPlugins/Realistic_Starter_VFX_Pack_Vol2/Textures
-MCPDuplicateDestinationRoot=/Game/ThirdParty/Realistic_Starter_VFX_Pack_Vol2/Textures
-MCPDuplicateAssetNames=T_Impact_Flare,T_Spark_A
-ExecCmds="Automation RunTests UnrealMCP.Assets.DuplicatePersistedTextures"
```

Run this after closing the copying editor to verify fresh disk loads. This test
only reads assets: it compares all source mip bytes, dimensions, formats, editable
settings, and dependencies, and checks original/copy identities and dirty states.
It requires pre-existing source and copied textures; it never creates or saves them.

### Dependency Copy Workflow

These four direct **Asset-category** tools connect Python MCP calls to the native
`UnrealMCPAssetMigration` implementation. They are independent of the Editor and
Project category switches. `UNREAL_MCP_READ_ONLY=1` retains plan/status/verify,
but removes execution. They require the matching deployed C++ plugin; Python
schema visibility alone does not prove that the editor has command routing.

This is dependency copying **within the current project**, not Unreal's final
cross-project **Migrate** command. It does not operate on another project's
Content folder or authorize changes to NF. Ordinary `duplicate_asset` is unchanged.

| Tool | Required Parameters | Optional Parameters |
|---|---|---|
| `plan_asset_migration` | `roots: list[str]`, `path_rules: list[dict]` | `rebind_assets: list[str]`, `retain_roots: list[str]` |
| `execute_asset_migration` | `plan_id: str`, `confirmation_token: str` | None |
| `get_asset_migration_status` | `plan_id: str` | None |
| `verify_asset_migration` | `plan_id: str` | None |

#### plan_asset_migration

Reads recursive AssetRegistry package dependencies plus serialized hard/soft
references, including editor, preview and cache objects. Returns `mapping`,
`dependencies`, `retained_external`, `namespace_mapping`, `blockers`, `executable`,
`plan_id`, `confirmation_token`, and `state="planned"`. A successful planning
request can still contain blockers: require `executable=true` before execution.

Rules are `{"source_root": "/Game/Source", "target_root": "/Game/Destination"}`;
the longest matching source prefix wins, preserving the relative hierarchy.
Targets must not exist, including unsaved loaded packages. Every reachable
`/Game` package needs a copy rule or an explicit `retain_roots` exemption.
Engine/plugin content and script dependencies remain external; missing dependencies,
dirty source/rebind packages, unsupported types and destination conflicts block
execution. Limits: 512 visited packages, 32 path rules, 32 in-memory plans per
editor session. Paths encoded as arbitrary strings are not generally discoverable.
Loading can trigger engine compilation/cache work; planning never saves assets.

For an existing NS that must retain its identity and tuning, include its exact
path in both `roots` and `rebind_assets`. Only explicitly listed Niagara Systems
are eligible for in-place rebinding; no implicit overwrite or reconstruction.
Omitting `rebind_assets` copies every root instead. NPC namespace changes needed
for copied collection bindings are explicitly included in `namespace_mapping`.

Example **preview only**, using a new test destination:

```python
plan_asset_migration(
  roots=["/Game/VFX/NS_Impact"],
  path_rules=[{"source_root": "/Game", "target_root": "/Game/__Dev/ImpactCopy_Unique"}],
  rebind_assets=["/Game/VFX/NS_Impact"])
```

Large results return `overflow=true` and `file_path` with the complete JSON.
Read that file, not merely the preview, before confirming. Plan state and
identifiers remain at the top level. This file does not replace the native plan
cache: plans expire when the editor closes.

#### execute_asset_migration

Pass the exact `plan_id` and `confirmation_token` from the reviewed plan.
Execution rechecks source file hashes, dirty packages and destination collisions,
then uses native duplication and package-scoped reference replacement. It never
uses global replacement, Consolidate or Save All. It waits for compilation and
target saves, then checks residual references. Existing NS comparison distinguishes
compiled caches from authored bindings, curves and typed rapid-iteration values.
Expected references are normalized using the native duplication's complete object
mapping, including renamed assets and subobjects, not just package prefixes.
Both the immediate rebind comparison and the post-compile comparison report
`comparison_stage` (`rebind` or `post_compile`), `asset_path`, and
`state_differences` entries with `field`, `before`, `after`. Missing fields use
JSON `null`; `before` is the expected snapshot after applying only planned
reference/namespace mappings. A mismatch still prevents automatic saving.

Texture compilation remains enabled. Execution finishes load-triggered source
compilation before duplication, then finishes duplicate-triggered compilation before
changing settings or serializing reference replacements. Restoring the source's
`OodleTextureSdkVersion` occurs inside the target's `PreEditChange`/`PostEditChange`
lifecycle, not while the duplicate's previous build is pending. Opened edit scopes
are also closed on early failure. Final compilation completes before target saves.
Texture item receipts include `texture_compilation_pending_after_duplicate`, an
observation of the duplicate at creation, not its current compilation state.

Read `success`, `state`, per-item `created`/`modified`/`saved`, compilation results
and errors. An error may leave unsaved copies or in-memory changes; no rollback is
promised. An execution journal is maintained under the active project's
`Saved/UnrealMCP/AssetMigration/<plan_id>.json`. This is observed progress, not a
transactional disk guarantee. Repeating a started plan returns its receipt rather
than copying again. Do not create a new plan to blindly retry a timeout.

#### get_asset_migration_status

Query the same `plan_id` after a timeout or uncertain result. Returns the current
plan or execution receipt; after editor restart it reads the journal. A journal
left `running` is reported as `interrupted_unknown`. It cannot resume execution.
Status reads leave the original journal unchanged as evidence; its on-disk `running`
text is not proof of a live task. Existing per-item progress is retained, without
claiming that interrupted work was rolled back or completed.
An unknown ID is not evidence that no files were created. Oversized failure
receipts retain `success=false` and their full item details in `file_path`.

#### verify_asset_migration

Read back an executed plan's saved targets, destination hashes, recursive
dependencies and Material/Niagara compilation state. Returns `issues`,
`retained_external`, `compilation`, `fresh_reload`, and `already_loaded` without
saving. It never forcibly unloads assets. For disk-persistence acceptance, use a
separate editor process; startup-loaded packages are reported honestly.

SkillTest acceptance includes native copy/remap and existing-NS rebinding on
isolated `__Dev` assets, plus a real stdio MCP session discovering the four tools
and producing an executable read-only plan for the three LaserImpact roots and
`MI_BulletHole`. The connection test performs no formal asset rebinding and does
not run cross-project Migrate; those require separate review and authorization.

#### GoodSky Async Texture Regression (2026-09-30)

Fixed the UE 5.7.4 fatal texture-compiler reentry observed while executing plan
`9204EAB54D1EBED6F49D30874166BB17`. That historical plan was used only for status
inspection, never replayed. The failure involved restoring a copied texture's SDK
setting before its earlier asynchronous build had finished; waiting only after
reference replacement was too late. The repair covers source/duplicate compilation
barriers, the complete target edit interval, and final compilation before saving.

SkillTest validation used the same three GoodSky roots and longest-prefix rules,
but a fresh plan and an isolated destination:

- Root: `/Game/__Dev/AssetMigration_GoodSky_abc49a1da5034275b53051d06f01d6df`.
- New plan: `DEF265514455EEDA621D0C8001C51622`, completed and saved all 15 items.
- `Editor.AsyncTextureCompilation=1`; all seven texture copies were actually pending
  after duplication. No compilation-disable workaround or original target overwrite.
- All editable texture settings, including the original Oodle SDK, matched after
  copying and again after loading from disk in a separate editor process.
- Fresh-process dependency/hash/compilation verification passed. Existing Niagara
  move/rename rebinding and five focused migration regressions also passed.
- `UnrealMCP.AssetMigration.GoodSkyAsyncTextures` uses explicit `-MCPGoodSkyRoot=`;
  cold verification adds `-MCPGoodSkyVerify=<new_completed_plan_id>`. It rejects
  non-isolated roots. `InterruptedJournal` checks status and non-resumption against
  a unique orphan journal while preserving its per-item progress and original bytes.
- Logs: SkillTest `Saved/Logs/Migration-GoodSky-Async.log` and
  `Saved/Logs/Migration-GoodSky-ColdVerify.log`; receipt:
  `Saved/AssetMigration-GoodSky-Receipt.json`.
- Actual MCP after restart: old plan `interrupted_unknown`/`success=false`, new plan
  `completed`, verification `success=true`/`issues=[]`, OpeningRescue dirty packages empty.
- All 50 original GoodSky files, protected SkillTest/NF maps, project/config files
  and original crash journal hashes were unchanged. The original business target
  directory still had no saved assets. One unrelated SkillTest source test was edited
  concurrently and left untouched; this was not counted as a migration write.
- Fix and tests were built/deployed to SkillTest only. No NF plugin update or asset
  migration was performed for this repair, and no level actors were replaced.

### open_asset

Open an asset in its default editor window.

**Parameters:**
- `asset_path` (string) - Asset path to open

### open_level

Open (load) a level/map into the editor viewport.

**Parameters:**
- `level_path` (string) - Level asset path, e.g. `/Game/Maps/MyLevel`
- `save_dirty` (bool, optional) - Save unsaved changes before switching levels (default: false)

### save_level

Save the currently open level/map to disk.

**Parameters:**
- None

### create_level

Create a new level/map at the given content path, optionally from a template.

**Parameters:**
- `level_path` (string) - Content path for the new level, e.g. `/Game/Maps/NewLevel`
- `template_path` (string, optional) - Template level to copy from
- `partitioned` (bool, optional) - Create as a World Partition level (default: false)

## Error Handling

Response shapes depend on the tool and transport layer; there is no universal
`status` field on every Python MCP result.

The raw UE TCP bridge normally wraps successful data in
`{"status": "success", "result": {...}}` and failures in
`{"status": "error", "error": "..."}`. Python tools commonly unwrap `result`
and normalize errors to this shape:

```json
{
  "success": false,
  "message": "Failed to get active viewport"
}
```

Some tools retain the bridge envelope; local tools such as `get_editor_logs`
return their own data directly. Check each tool's return contract, explicit
failure fields and, where provided, `saved`/`modified`/`duplicate_created`.
A successful transport exchange does not guarantee an asset was saved or a
build completed. Timeouts may occur after UE has started or completed a change;
inspect state before retrying a mutation.

## Troubleshooting

- **Logs unavailable**: pass `log_path` or set `UNREAL_PROJECT_LOG` to the target project's current `.log` file.
- **Build blocked by Live Coding**: save dirty assets, close the relevant editor, then build externally.
- **Asset/level path fails**: use a full Unreal content path such as `/Game/Maps/MyLevel`.
