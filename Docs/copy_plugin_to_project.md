# Copy UnrealMCP Plugin to Another Project

This guide is for **Copilot / AI agents** to follow when a user says something like:
> "帮我 copy 到 D:\UnrealProjects\MyProject"
> "Deploy the plugin to D:\SomeFolder\SomeProject"

Follow the steps below **in order**. Do not skip steps.

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

**Source:** `d:\githubcode\unreal-mcp-bo\MCPGameProject\Plugins\UnrealMCP`

**Destination:** `<PROJECT_ROOT>\Plugins\UnrealMCP`

**Action:** If the destination already exists, warn the user and ask for confirmation before overwriting.

```powershell
$src = "d:\githubcode\unreal-mcp-bo\MCPGameProject\Plugins\UnrealMCP"
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

## Step 7: Post-Copy Instructions

After successful copy, tell the user:

1. **Open the project** in Unreal Engine (double-click the `.uproject` file).
2. UE will detect the new plugin and **prompt to rebuild** — click **Yes**.
3. Alternatively, if using Visual Studio:
   - Right-click the `.uproject` file → **Generate Visual Studio project files**
   - Open the `.sln` in Visual Studio
   - Build the project (Ctrl+B)
4. After rebuild, the MCP TCP server will start automatically when the editor opens.
5. Make sure the **MCP Python server** is configured in your `mcp.json`:
   ```jsonc
   {
       "servers": {
           "unrealMCP": {
               "command": "uv",
               "args": [
                   "--directory",
                   "d:\\githubcode\\unreal-mcp-bo\\Python",
                   "run",
                   "unreal_mcp_server.py"
               ]
           }
       }
   }
   ```

---

## Quick Reference: One-Liner Copy

For a known-good project root, the entire copy can be done in one command:

```powershell
$src = "d:\githubcode\unreal-mcp-bo\MCPGameProject\Plugins\UnrealMCP"
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
| Plugin not loading | Check the Output Log for "UnrealMCP" messages. Ensure port 55557 is available. |
| MCP server can't connect | Make sure UE editor is running and the plugin is loaded before starting the Python server. |
