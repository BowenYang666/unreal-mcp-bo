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
