# Onboard an Unreal Project to UnrealMCP

This guide is for **Copilot / AI agents** to follow when a user says something like:
> "帮我 copy 到 D:\UnrealProjects\MyProject"
> "Deploy the plugin to D:\SomeFolder\SomeProject"

Follow the steps below **in order**. Do not stop after copying: onboarding is complete only after the plugin builds, the editor starts, the MCP client is configured, and a read-only smoke test succeeds.

For the full agent workflow (VS Code + Claude Code, read-only/full profiles, update handling, and validation), load `.github/skills/unreal-mcp-project-onboarding/SKILL.md`.

---

## Step 1: Locate the Unreal Engine Project Root

The user may provide a path that is **not exactly** the UE project root. The project root is the directory that contains a `.uproject` file.

**Action:** Given the user-provided path, search for a `.uproject` file:

```powershell
# Check the given path, one level up, and one level down
Get-ChildItem -Path "<USER_PATH>" -Filter "*.uproject" -ErrorAction SilentlyContinue
Get-ChildItem -Path "<USER_PATH>\.." -Filter "*.uproject" -ErrorAction SilentlyContinue
Get-ChildItem -Path "<USER_PATH>\*" -Filter "*.uproject" -Depth 0 -ErrorAction SilentlyContinue
```

**Expected result:** You find exactly one `.uproject` file. The directory containing it is the **Project Root**.

**If not found:** Ask the user to provide the correct path. Do NOT proceed.

**If found:** Print the project root and `.uproject` file name, then continue.

---

## Step 2: Verify the `.uproject` Engine Version

**Action:** Read the `.uproject` file and check the `EngineAssociation` field.

```powershell
Get-Content "<PROJECT_ROOT>\<Name>.uproject"
```

**Check:**
- If `EngineAssociation` is `5.5` or higher → OK, proceed.
- If `EngineAssociation` is lower than `5.5` → Warn the user that this plugin requires UE 5.5+. Ask if they want to continue anyway.

---

## Step 3: Create the Plugins Directory (if needed)

**Action:** Check if `<PROJECT_ROOT>\Plugins` exists. If not, create it.

```powershell
if (!(Test-Path "<PROJECT_ROOT>\Plugins")) {
    New-Item -ItemType Directory -Path "<PROJECT_ROOT>\Plugins" -Force
    Write-Host "Created Plugins directory"
} else {
    Write-Host "Plugins directory already exists"
}
```

---

## Step 4: Copy the Plugin

**Source:** `<MCP_REPO>\MCPGameProject\Plugins\UnrealMCP`

**Destination:** `<PROJECT_ROOT>\Plugins\UnrealMCP`

**Action:** If the destination already exists, warn the user and ask for confirmation before overwriting.

```powershell
$src = "<MCP_REPO>\MCPGameProject\Plugins\UnrealMCP"
$dst = "<PROJECT_ROOT>\Plugins\UnrealMCP"

# Check if already exists
if (Test-Path $dst) {
    Write-Host "WARNING: UnrealMCP plugin already exists at $dst"
    # Ask user before overwriting - or if user already confirmed, proceed
    Remove-Item $dst -Recurse -Force
    Write-Host "Removed old plugin"
}

# Copy the plugin
Copy-Item -Path $src -Destination $dst -Recurse -Force
Write-Host "Plugin copied successfully"
```

---

## Step 5: Clean Intermediate Build Files

If the destination had an old version, clean any stale build artifacts:

```powershell
$intermediate = "<PROJECT_ROOT>\Plugins\UnrealMCP\Intermediate"
if (Test-Path $intermediate) {
    Remove-Item $intermediate -Recurse -Force
    Write-Host "Cleaned Intermediate build files"
}

$binaries = "<PROJECT_ROOT>\Plugins\UnrealMCP\Binaries"
if (Test-Path $binaries) {
    Remove-Item $binaries -Recurse -Force
    Write-Host "Cleaned old Binaries"
}
```

---

## Step 6: Verify the Copy

**Action:** Confirm key files exist at the destination.

```powershell
$requiredFiles = @(
    "<PROJECT_ROOT>\Plugins\UnrealMCP\UnrealMCP.uplugin",
    "<PROJECT_ROOT>\Plugins\UnrealMCP\Source\UnrealMCP\UnrealMCP.Build.cs",
    "<PROJECT_ROOT>\Plugins\UnrealMCP\Source\UnrealMCP\Private\UnrealMCPBridge.cpp",
    "<PROJECT_ROOT>\Plugins\UnrealMCP\Source\UnrealMCP\Private\Commands\UnrealMCPBlueprintCommands.cpp"
)

$allGood = $true
foreach ($f in $requiredFiles) {
    if (Test-Path $f) {
        Write-Host "OK: $f"
    } else {
        Write-Host "MISSING: $f"
        $allGood = $false
    }
}

if ($allGood) { Write-Host "All files verified!" }
else { Write-Host "ERROR: Some files are missing!" }
```

---

## Step 7: Build the Target Editor

Close the target Unreal Editor gracefully (save dirty assets first), then build the editor target:

```powershell
& "<ENGINE_ROOT>\Engine\Build\BatchFiles\Build.bat" `
    "<PROJECT_NAME>Editor" Win64 Development `
    -Project="<PROJECT_ROOT>\<PROJECT_NAME>.uproject" `
    -WaitMutex -FromMsBuild
```

Content-only projects are supported; UnrealBuildTool may generate temporary target files. If Live Coding blocks the build, close the relevant editor(s) and retry.

## Step 8: Configure the MCP Client

Use the Python server from the UnrealMCP repository; do not copy the Python directory into the target UE project. Resolve an absolute `uv` path with `Get-Command uv`.

For VS Code/Copilot, create or merge `.vscode/mcp.json`:

   ```jsonc
   {
         "servers": {
             "unrealMCP": {
                 "type": "stdio",
                 "command": "<ABSOLUTE_UV_PATH>",
                 "args": ["--directory", "<MCP_REPO>\\Python", "run", "unreal_mcp_server.py"],
                 "env": { "UNREAL_MCP_READ_ONLY": "1" }
             }
         }
   }
   ```

For Claude Code, create or merge strict JSON at the project root in `.mcp.json`:

```json
{
    "mcpServers": {
        "unrealMCP": {
            "command": "<ABSOLUTE_UV_PATH>",
            "args": ["--directory", "<MCP_REPO>\\Python", "run", "unreal_mcp_server.py"],
            "env": { "UNREAL_MCP_READ_ONLY": "1" }
        }
    }
}
```

Claude project servers require one-time approval: launch `claude` in the project root and approve `unrealMCP`. `claude mcp list` reports `Pending approval` until this is done.

Use `UNREAL_MCP_READ_ONLY=1` for project inspection. Set it to `0` only when authoring is requested.

## Step 9: Launch and Verify

1. Launch the target `.uproject` with the matching `UnrealEditor.exe`.
2. Verify that UnrealMCP listens on `127.0.0.1:13090`:

```powershell
Get-NetTCPConnection -LocalPort 13090 -State Listen -ErrorAction SilentlyContinue
```

3. Restart or approve the MCP client.
4. Smoke-test with a narrow `list_blueprints` query, then `read_blueprint` on one asset.
5. Do not report success while the build fails, the listener is absent, or the client cannot discover `unrealMCP`.

---

## Quick Reference: One-Liner Copy

For a known-good project root, the entire copy can be done in one command:

```powershell
$src = "<MCP_REPO>\MCPGameProject\Plugins\UnrealMCP"
$dst = "<PROJECT_ROOT>\Plugins\UnrealMCP"
if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
if (!(Test-Path (Split-Path $dst))) { New-Item -ItemType Directory -Path (Split-Path $dst) -Force }
Copy-Item -Path $src -Destination $dst -Recurse -Force
Remove-Item "$dst\Intermediate" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$dst\Binaries" -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "UnrealMCP plugin deployed to $dst"
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Build error: `ANY_PACKAGE` | You're on UE 5.7+. Make sure you have the latest plugin source. |
| Build error: `BufferSize` hides global | You're on UE 5.7+. Make sure you have the latest plugin source. |
| Build error: `VisualStudioTools` module rules | Remove the `VisualStudioTools` entry from the `.uproject` Plugins section. |
| Plugin not loading | Check the Output Log for "UnrealMCP" messages. Ensure port 13090 is available. |
| MCP server can't connect | Make sure UE editor is running and the plugin is loaded before starting the Python server. |
| Claude shows `Pending approval` | Launch `claude` in the project root and approve the project-scoped server once. |
| `uv` works in a terminal but not the client | Use the absolute `uv.exe` path in the MCP config. |
