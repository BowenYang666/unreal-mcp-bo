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
