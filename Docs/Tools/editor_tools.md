# Unreal MCP Editor Tools

This document provides detailed information about the editor tools available in the Unreal MCP integration.

## Overview

Editor tools allow you to control the Unreal Editor viewport and other editor functionality through MCP commands. These tools are particularly useful for automating tasks like focusing the camera on specific actors or locations.

## Editor Tools

### focus_viewport

Focus the viewport on a specific actor or location.

**Parameters:**
- `target` (string, optional) - Name of the actor to focus on (if provided, location is ignored)
- `location` (array, optional) - [X, Y, Z] coordinates to focus on (used if target is None)
- `distance` (float, optional) - Distance from the target/location (default: 1000.0)
- `orientation` (array, optional) - [Pitch, Yaw, Roll] for the viewport camera

**Returns:**
- Response from Unreal Engine containing the result of the focus operation

**Example:**
```json
{
  "command": "focus_viewport",
  "params": {
    "target": "PlayerStart",
    "distance": 500,
    "orientation": [0, 180, 0]
  }
}
```

### take_screenshot

Capture a screenshot of the viewport.

**Parameters:**
- `filename` (string, optional) - Name of the file to save the screenshot as (default: "screenshot.png")
- `show_ui` (boolean, optional) - Whether to include UI elements in the screenshot (default: false)
- `resolution` (array, optional) - [Width, Height] for the screenshot

**Returns:**
- Result of the screenshot operation

**Example:**
```json
{
  "command": "take_screenshot",
  "params": {
    "filename": "my_scene.png",
    "show_ui": false,
    "resolution": [1920, 1080]
  }
}
```

### get_editor_logs

Read recent Unreal Editor output log entries from the log file.

**Parameters:**
- `count` (int, optional) - Number of log lines to return (default: 100)
- `verbosity` (string, optional) - Filter by verbosity: "all", "error", "warning", "display" (default: "all")
- `category` (string, optional) - Filter by log category (e.g. "LogTemp", "LogBlueprintUserMessages")
- `search` (string, optional) - Filter lines containing this keyword
- `log_path` (string, optional) - Override the log file path. Otherwise uses `UNREAL_PROJECT_LOG` env var.

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

## Error Handling

All command responses include a "status" field indicating whether the operation succeeded, and an optional "message" field with details in case of failure.

```json
{
  "status": "error",
  "message": "Failed to get active viewport"
}
```

## Usage Examples

### Python Example

```python
from unreal_mcp_server import get_unreal_connection

# Get connection to Unreal Engine
unreal = get_unreal_connection()

# Focus on a specific actor
focus_response = unreal.send_command("focus_viewport", {
    "target": "PlayerStart",
    "distance": 500,
    "orientation": [0, 180, 0]
})
print(focus_response)

# Take a screenshot
screenshot_response = unreal.send_command("take_screenshot", {"filename": "my_scene.png"})
print(screenshot_response)
```

## Troubleshooting

- **Command fails with "Failed to get active viewport"**: Make sure Unreal Editor is running and has an active viewport.
- **Actor not found**: Verify that the actor name is correct and the actor exists in the current level.
- **Invalid parameters**: Ensure that location and orientation arrays contain exactly 3 values (X, Y, Z for location; Pitch, Yaw, Roll for orientation).

## Future Enhancements

- Support for setting viewport display mode (wireframe, lit, etc.)
- Camera animation paths for cinematic viewport control
- Support for multiple viewports
