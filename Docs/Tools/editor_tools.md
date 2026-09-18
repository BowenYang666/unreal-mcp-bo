# Unreal MCP Editor Tools

Tools for inspecting editor state, reading logs, saving/opening assets, and managing levels. Actor operations are documented separately in [Actor Tools](actor_tools.md).

`focus_viewport` and `take_screenshot` exist as legacy C++ commands but are not registered as Python MCP tools and are therefore not part of the supported tool surface.

## Editor Tools

### get_editor_logs

Read recent Unreal Editor output log entries from the log file.

**Parameters:**
- `count` (int, optional) - Number of log lines to return (default: 100)
- `verbosity` (string, optional) - Filter by verbosity: "all", "error", "warning", "display" (default: "all")
- `category` (string, optional) - Filter by log category (e.g. "LogTemp", "LogBlueprintUserMessages")
- `search` (string, optional) - Filter lines containing this keyword
- `log_path` (string, optional) - Override the log file path. Otherwise uses `UNREAL_PROJECT_LOG` env var.
- `start_time` / `end_time` (string, optional) - ISO/local time range filter
- `relative_seconds_ago` (int, optional) - Only include entries from the recent time window
- `pie_session_index` (int, optional) - Select a Play-In-Editor session from the log

**Returns:**
- Dict with `log_entries` array, `total_lines`, and applied filters

### get_unsaved_changes

Check for unsaved changes in the Unreal Editor.

**Parameters:** None

**Returns:**
- Dict with `total_unsaved` (int), `unsaved_content` (list of package names), `unsaved_maps` (list of map names)

### close_editor

Gracefully close the Unreal Editor. Closes all open asset editor tabs first to avoid crashes, then schedules engine exit.

**Parameters:**
- `save_all` (bool, optional) - If true (default), saves all dirty packages before closing. Set to false to close without saving.

**Returns:**
- Dict with closing status, `saved_count`, and any `failed_saves`

**Example:**
```json
{
  "command": "close_editor",
  "params": {
    "save_all": true
  }
}
```

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

All command responses include a "status" field indicating whether the operation succeeded, and an optional "message" field with details in case of failure.

```json
{
  "status": "error",
  "message": "Failed to get active viewport"
}
```

## Troubleshooting

- **Logs unavailable**: pass `log_path` or set `UNREAL_PROJECT_LOG` to the target project's current `.log` file.
- **Build blocked by Live Coding**: save dirty assets, close the relevant editor, then build externally.
- **Asset/level path fails**: use a full Unreal content path such as `/Game/Maps/MyLevel`.
