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

- `UNREAL_MCP_READ_ONLY=1`: expose only the current read-only whitelist.
- `MCP_ASSET_ENABLED`: controls `rename_asset` and `move_asset`.
- `MCP_EDITOR_ENABLED`, `MCP_BLUEPRINT_ENABLED`, `MCP_NODE_ENABLED`, `MCP_PROJECT_ENABLED`, `MCP_UMG_ENABLED`, `MCP_MATERIAL_ENABLED`, `MCP_NIAGARA_ENABLED`: set to `0`/`false`/`no`/`off` to disable a category.
- `UNREAL_PROJECT_LOG`: default log file used by `get_editor_logs`.

## Development

- Register Python MCP tools in `tools/*.py` with `@mcp.tool()` and call `register_*_tools(mcp)` from `unreal_mcp_server.py`.
- Add or update C++ command handlers under `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/`, then route new command names through `UnrealMCPBridge.cpp`.
- Python-only/schema changes require an MCP client/server restart, not an Unreal build.
- C++ or `Build.cs` changes require redeploying the plugin to the target project, rebuilding its Editor target, and relaunching the editor.

## Troubleshooting

- Confirm the intended Unreal Editor project is running.
- Verify `Get-NetTCPConnection -LocalPort 13090 -State Listen` returns the editor process.
- Restart the MCP client after Python tool signature changes.
- Check `unreal_mcp.log` and the target project's `Saved/Logs/` directory for errors.