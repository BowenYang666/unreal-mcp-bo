---
name: unreal-mcp-project-onboarding
description: "Onboard any Unreal Engine 5.5+ project or repository to UnrealMCP. Use when asked to install, copy, deploy, configure, build, update, or set up UnrealMCP for a new UE project, VS Code/Copilot workspace, or Claude Code folder. Covers project discovery, version checks, plugin deployment, compilation, client MCP configuration, approval, launch, and connectivity smoke tests."
argument-hint: "[project path] [client: vscode|claude|both] [mode: read-only|full]"
---

# UnrealMCP Project Onboarding

Use this workflow to make a new Unreal Engine project ready for UnrealMCP end to end. Do not stop after copying the plugin: onboarding is complete only after the plugin builds, the editor is running, the selected MCP client can discover the server, and a read-only smoke test succeeds.

## Defaults

- Treat the user-provided path as a hint; locate the actual directory containing the `.uproject`.
- Require Unreal Engine 5.5 or newer. Warn and ask before continuing on older versions.
- Use **copy deployment** for independent projects. Use a directory junction only when the user explicitly wants live development against one plugin source tree.
- For project inspection, learning, or reverse engineering, configure `UNREAL_MCP_READ_ONLY=1`.
- For asset authoring, configure `UNREAL_MCP_READ_ONLY=0` and enable only the required categories.
- Configure the client(s) implied by the folder or request:
  - VS Code/Copilot: `.vscode/mcp.json`
  - Claude Code: `.mcp.json`
  - If both are present or requested, configure both.

## Source of Truth

Resolve these paths from the current UnrealMCP repository; do not hardcode another user's checkout:

```text
<MCP_REPO>/MCPGameProject/Plugins/UnrealMCP   # C++ plugin source
<MCP_REPO>/Python                            # Python MCP server
```

The deployed project path is:

```text
<PROJECT_ROOT>/Plugins/UnrealMCP
```

## Phase 1: Discover and Preflight

1. Find exactly one `.uproject` in the supplied directory, its parent, or one level of children.
2. Set `<PROJECT_ROOT>` to the directory containing that file and `<PROJECT_NAME>` to its base name.
3. Read `EngineAssociation` from the `.uproject` and resolve the matching engine installation.
4. Verify the source plugin contains:
   - `UnrealMCP.uplugin`
   - `Source/UnrealMCP/UnrealMCP.Build.cs`
   - `Source/UnrealMCP/Private/UnrealMCPBridge.cpp`
   - `Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp`
5. Check whether `<PROJECT_ROOT>/Plugins/UnrealMCP` already exists.
   - If absent, continue.
   - If present, report that this is an update and ask before replacing it. Never merge an unknown destination tree over the source of truth.
6. Check for running Unreal Editor processes and identify which project each process opened from its command line.

Example project discovery:

```powershell
$hint = '<USER_PATH>'
$projects = @()
$projects += Get-ChildItem $hint -Filter *.uproject -ErrorAction SilentlyContinue
$projects += Get-ChildItem (Split-Path $hint -Parent) -Filter *.uproject -ErrorAction SilentlyContinue
$projects += Get-ChildItem $hint -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Get-ChildItem $_.FullName -Filter *.uproject -ErrorAction SilentlyContinue }
$projects | Select-Object -Unique FullName
```

## Phase 2: Deploy the Plugin

After confirmation when replacing an existing plugin:

1. Close the target editor gracefully if possible and save dirty assets. Do not force-kill an editor with unsaved work without explicit approval.
2. Remove the old destination plugin directory instead of overlaying files; overlay copies can retain deleted or stale source files.
3. Create `<PROJECT_ROOT>/Plugins` if needed.
4. Copy the complete `UnrealMCP` plugin directory from the MCP repository.
5. Remove destination `Intermediate` and `Binaries` directories.
6. Verify the four required files from Phase 1 at the destination.

```powershell
$src = '<MCP_REPO>\MCPGameProject\Plugins\UnrealMCP'
$dst = '<PROJECT_ROOT>\Plugins\UnrealMCP'

if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
Copy-Item $src $dst -Recurse -Force
Remove-Item "$dst\Intermediate", "$dst\Binaries" -Recurse -Force -ErrorAction SilentlyContinue
```

### Optional Development Junction

Use only with explicit user approval. A junction removes copy drift but couples the target project directly to the MCP repository:

```powershell
New-Item -ItemType Junction -Path '<PROJECT_ROOT>\Plugins\UnrealMCP' `
    -Target '<MCP_REPO>\MCPGameProject\Plugins\UnrealMCP'
```

## Phase 3: Build

1. Resolve the engine root from `EngineAssociation`, the running editor executable, Epic installation registry entries, or known local installs.
2. Build `<PROJECT_NAME>Editor Win64 Development` with the project's `.uproject`.
3. Content-only projects are valid; UnrealBuildTool may generate temporary Target files.
4. If Live Coding blocks the build, close the relevant editor(s) and retry. Tell the user if another project's editor must also close.
5. If UnrealBuildTool incorrectly reports `Target is up to date` after a source update, clean the destination plugin `Intermediate`/`Binaries` and rebuild. Do not rely only on copied timestamps.
6. Treat compilation or link errors as onboarding failures; fix them before proceeding.

```powershell
& '<ENGINE_ROOT>\Engine\Build\BatchFiles\Build.bat' `
    '<PROJECT_NAME>Editor' Win64 Development `
    -Project='<PROJECT_ROOT>\<PROJECT_NAME>.uproject' `
    -WaitMutex -FromMsBuild
```

## Phase 4: Configure MCP Clients

First resolve `uv` with `Get-Command uv`. Prefer its absolute path so GUI clients do not depend on a stale `PATH`.

Use the same external Python server for every onboarded project; do not copy the `Python` directory into the UE project.

### Environment Profiles

For read-only inspection:

```json
{
  "UNREAL_MCP_READ_ONLY": "1",
  "MCP_ASSET_ENABLED": "1",
  "MCP_EDITOR_ENABLED": "1",
  "MCP_BLUEPRINT_ENABLED": "1",
  "MCP_NODE_ENABLED": "1",
  "MCP_PROJECT_ENABLED": "1",
  "MCP_UMG_ENABLED": "1",
  "MCP_MATERIAL_ENABLED": "1",
  "MCP_NIAGARA_ENABLED": "1"
}
```

`UNREAL_MCP_READ_ONLY=1` removes mutation tools after registration. Keeping categories enabled allows their safe query tools through the read-only whitelist, including `find_blueprint_nodes`, `get_class_properties`, `read_data_asset`, `read_behavior_tree`, `read_blackboard`, and `read_state_tree`. `get_class_properties` is essential for inspecting editable class metadata and current values on any loaded asset without modifying it.

For authoring, set `UNREAL_MCP_READ_ONLY=0`; set category flags to `0` only when that category should not be exposed. Use `MCP_ASSET_ENABLED=0` when the client must not rename or move assets.

### VS Code / Copilot

Create or merge `<WORKSPACE>/.vscode/mcp.json`. Preserve unrelated servers and existing comments.

```jsonc
{
  "servers": {
    "unrealMCP": {
      "type": "stdio",
      "command": "<ABSOLUTE_UV_PATH>",
      "args": [
        "--directory",
        "<MCP_REPO>\\Python",
        "run",
        "unreal_mcp_server.py"
      ],
      "env": { "UNREAL_MCP_READ_ONLY": "1" }
    }
  }
}
```

Restart the VS Code MCP server after creating or changing this file.

### Claude Code

Create or merge strict JSON at `<PROJECT_ROOT>/.mcp.json`. Preserve unrelated servers.

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "<ABSOLUTE_UV_PATH>",
      "args": [
        "--directory",
        "<MCP_REPO>\\Python",
        "run",
        "unreal_mcp_server.py"
      ],
      "env": { "UNREAL_MCP_READ_ONLY": "1" }
    }
  }
}
```

Validate from the project root:

```powershell
claude mcp list
claude mcp get unrealMCP
```

Project-scoped servers initially show `Pending approval`. This is expected: launch `claude` in the project root and let the user approve `unrealMCP` once. Do not attempt to bypass this trust prompt.

## Phase 5: Launch and Verify

1. Launch the target `.uproject` with the resolved `UnrealEditor.exe`.
2. Wait for the process and MCP listener to start.
3. Verify the plugin TCP endpoint at `127.0.0.1:13090`.
4. Restart/approve the configured MCP client.
5. Run a read-only smoke test:
   - `list_blueprints` for a narrow `/Game/...` folder
   - `read_blueprint` for one selected Blueprint
6. If the tool schema appears but calls fail:
   - Confirm `127.0.0.1:13090` is listening.
   - Confirm the editor opened the intended project.
   - Confirm the destination plugin matches the source plugin.
   - Restart the Python MCP server after Python/schema changes.

```powershell
Get-NetTCPConnection -LocalPort 13090 -State Listen -ErrorAction SilentlyContinue
```

## Completion Report

Report all of the following:

- Project root and `.uproject`
- Engine version and engine root
- Plugin source and destination
- Whether this was a fresh install or update
- Build target and build result
- Editor launch status and PID
- Port `13090` listener status
- Configured clients and config paths
- Read-only vs full-authoring mode
- Claude approval status when applicable
- Smoke-test command and result

Do not call onboarding complete while the build is failing, the editor is closed, the listener is absent, or the client cannot discover the server.

## Updating an Onboarded Project

When UnrealMCP changes later:

1. Stop the Python MCP server only if Python tools or schemas changed.
2. Save and close the target editor for C++ changes.
3. Replace the destination plugin from the MCP repository; do not copy in the reverse direction.
4. Clean stale plugin build outputs when needed.
5. Rebuild the target editor.
6. Relaunch the editor and restart the MCP client.
7. Repeat the listener and read-only smoke tests.

## Common Failure Signatures

| Symptom | Likely cause | Action |
|---|---|---|
| `Live Coding is active` | An Unreal Editor is still running with Live Coding | Save/close the relevant editor and rebuild |
| `Unknown command` / `Unknown Niagara command` | Deployed plugin source or DLL is stale | Replace the entire destination plugin, clean, rebuild |
| Tool exists but schema is old | Python MCP server was not restarted | Stop/restart the client MCP process |
| `127.0.0.1:13090` not listening | Plugin not loaded or editor still starting | Check Output Log and plugin build/load state |
| Claude shows `Pending approval` | Project `.mcp.json` has not been trusted | Launch `claude` in project root and approve once |
| `uv` not found from GUI client | Client did not inherit shell `PATH` | Use absolute `uv.exe` path |
| Build links old symbols or misses new handlers | Stale copied source/build artifacts | Replace destination plugin, touch/clean if needed, rebuild |
