# Unreal MCP Python Server

Python MCP server for Unreal Engine 5.5+ (tested on UE 5.7).

```text
MCP client -> stdio -> unreal_mcp_server.py -> TCP 127.0.0.1:13090 -> UE plugin
```

## Run with uv

Install [uv](https://docs.astral.sh/uv/), then run from the repository root:

```powershell
uv --directory ./Python run unreal_mcp_server.py
```

`uv run` resolves the project environment from `pyproject.toml`/`uv.lock`; manually activating a virtual environment is not required.

For VS Code and Claude Code configuration, see the repository [README](../README.md#mcp-client-configuration) and [onboarding workflow](../.github/skills/unreal-mcp-project-onboarding/SKILL.md).

## Direct Test Scripts

Scripts under [scripts](./scripts) connect directly to the editor plugin on `127.0.0.1:13090`; the Python MCP stdio server is not required for them. The target Unreal Editor must be running with the current UnrealMCP plugin loaded.

```powershell
uv --directory ./Python run python scripts/actors/test_cube.py
```

## Environment Controls

- `MCP_TOOL_MODE=grouped` (default): expose Material/Niagara/UMG/Scene search/read/write entry points, 70 public tools before filtering. `direct` exposes all 140 operations directly. Scene's original names are internal in grouped mode; discover contracts through `scene_search`. See the [calling guide](../Docs/Tools/README.md#grouped-mode-default).
- `MCP_SCENE_ENABLED`: controls 20 guarded scene operations. See [scene tools](../Docs/Tools/scene_tools.md) for identity, preview, task status and save constraints. Folder parameters require native `folder_contract=1`; temporary actor hiding requires `editor_visibility_contract=1`, not merely updated Python schemas.
- `UNREAL_MCP_PORT`: native editor TCP port, default 13090. Match `-UnrealMCPPort=<port>` when running multiple editors. Host remains loopback.
- `UNREAL_MCP_READ_ONLY=1`: expose only the current read-only whitelist.
- `MCP_ASSET_ENABLED`: controls `rename_asset`, `move_asset`, `duplicate_asset`, `create_data_asset`, `create_physical_material`, `set_asset_properties` (DataAsset instances and allowlisted PhysicalMaterial fields), plus `plan_asset_migration`, `execute_asset_migration`, `get_asset_migration_status`, and `verify_asset_migration`. Read-only mode retains the three migration queries but removes execution. See the [dependency-copy workflow](../Docs/Tools/editor_tools.md#dependency-copy-workflow).
- `MCP_EDITOR_ENABLED`, `MCP_BLUEPRINT_ENABLED`, `MCP_NODE_ENABLED`, `MCP_PROJECT_ENABLED`, `MCP_UMG_ENABLED`, `MCP_MATERIAL_ENABLED`, `MCP_NIAGARA_ENABLED`, `MCP_NAVIGATION_ENABLED`, `MCP_CASCADE_ENABLED`: set to `0`/`false`/`no`/`off` to disable a category.
- `UNREAL_PROJECT_LOG`: default log file used by `get_editor_logs`.

## Development

- Register Python MCP tools in `tools/*.py` with `@mcp.tool()` and call `register_*_tools(mcp)` from `unreal_mcp_server.py`.
- Keep category membership and the read-only allowlist in sync. Grouped entry points derive their contracts from the original registered tools after filtering; do not duplicate schemas in the dispatcher.
- Add or update C++ command handlers under `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/`, then route new command names through `UnrealMCPBridge.cpp`.
- Python-only/schema changes require an MCP client/server restart, not an Unreal build.
- C++ or `Build.cs` changes require redeploying the plugin to the target project, rebuilding its Editor target, and relaunching the editor.
- DataAsset writes/structured reads and fragmented TCP request support require the matching new plugin. Requests are serialized per Python client and the native receiver accepts one JSON object per connection (1 MiB limit, 10-second receive deadline). Transport timeout is not cancellation or proof that nothing changed.

Offline unit and mocked-editor stdio tests (do not require or modify Unreal):

```powershell
uv --directory ./Python run python -m unittest discover -s tests -v
```

## Troubleshooting

- Confirm the intended Unreal Editor project is running.
- Verify `Get-NetTCPConnection -LocalPort 13090 -State Listen` returns the editor process.
- Restart the MCP client after Python tool signature changes.
- Check `unreal_mcp.log` and the target project's `Saved/Logs/` directory for errors.