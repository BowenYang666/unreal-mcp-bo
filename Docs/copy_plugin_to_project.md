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

The current default is `MCP_TOOL_MODE=grouped`: Material, Niagara, UMG, Scene, Blueprint and Asset each
expose a category search/read/write interface. Search by internal operation name
to obtain its `input_schema` and `call_tool`, then execute with `tool` and
`arguments`. Other categories retain direct tools. `MCP_TOOL_MODE=direct` is the
compatibility option; preserve existing explicit modes and permissions unless
their change is authorized. See the [calling guide](Tools/README.md#grouped-mode-default).

Blueprint smoke tests use `blueprint_search(tool="list_blueprints")` and
`blueprint_search(tool="read_blueprint")`, followed by each returned read endpoint
with `tool` and `arguments`. Asset operations similarly use `asset_search`.
Blueprint Node tools and Project readers (`get_class_properties`, `read_data_asset`,
`read_state_tree`) remain direct. Grouping-only Python updates require MCP restart,
not an editor restart, native rebuild or broader client permissions.

Shared Python updates take effect when each MCP server/client restarts, but do
not update a running editor's C++ DLL. Grouping alone needs no UE build; new
native features such as DataAsset writes/structured reads do. Category filters
still apply: `MCP_ASSET_ENABLED=0` hides rename/move/duplicate/DataAsset writes,
not every asset mutation in other categories.

When authorized to update project skills, preserve local workflow/safety rules
and add a self-contained grouped/direct routing note. Do not blindly replace
customized skills or copy links to this repository's `Docs/Tools` tree into a
project that lacks it. Historical test receipts remain dated evidence, not the
current tool list. Documentation changes alone authorize no editor operations.

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

## Multiple Editors and Project Binding

With the updated native plugin, persist each project's listener settings in its
`Config/DefaultEngine.ini`. Normal double-clicking of the `.uproject` then reads
these settings; no special shortcut or per-launch argument is needed:

```ini
[UnrealMCP]
Port=13091
RequireProjectBinding=True
```

Assign distinct loopback ports, for example NF=13090 and SkillTest=13091.
Configuration priority is command-line `-UnrealMCPPort=...`, then the editor
process's `UNREAL_MCP_PORT`, then this project's INI, then legacy default 13090.
Avoid a machine-wide environment override that makes every editor use one port.
Invalid selected ports refuse startup; occupied ports are not automatically
replaced or retried with another port. New listeners disable address reuse.
This is not a guarantee of Windows exclusive binding against an old or unrelated
program using socket reuse; upgrade both projects and give them different ports.

For each project-local MCP service, use the matching client environment:

```json
{
    "UNREAL_MCP_PORT": "13091",
    "UNREAL_MCP_EXPECTED_PROJECT": "E:/04_UnrealProjects/UE5Test/SkillTest/SkillTest.uproject",
    "UNREAL_MCP_READ_ONLY": "1"
}
```

These are example settings, **not automatically applied to NF or SkillTest**.
Each service is a separate Python process with a fixed port and expected absolute
`.uproject` path. In VS Code place its environment under `servers.<name>.env`;
in Claude use `mcpServers.<name>.env`. Preserve existing category filters and
authoring permissions; choose read/write only when authorized. Never replace an
active project's configuration or restart its editor while another agent works.

An NF workspace can expose both `unrealNF` (NF's authorized access) and
`unrealSkillTest` (read-only by default). Choose the client-visible server prefix,
not an ambiguous `/Game/...` path. Each profile still has its own grouped
discovery and permission filters. Two workspaces can independently use the same
shared Python source; they do not share a connection instance.

Setting `UNREAL_MCP_EXPECTED_PROJECT` wraps **every command** in the native
`execute_project_command` protocol. The UE game-thread gate compares the full
normalized project path before dispatching the internal command, and returns a
`project_binding` contract1 attestation with the actual project path. A mismatch,
malformed/nested wrapper, or an unbound request to a strict project is rejected
before execution with `stage=project_binding`, `execution_state=not_started`,
`modified=false`, `saved=false`. Existing operation receipts/defaults are retained.
Path matching is lexical full-path normalization (case-insensitive on Windows),
not symlink/junction equivalence; configure the path actually used to launch UE.

An old DLL cannot execute the unknown wrapper command. The bound client requires
native attestation and never falls back to an unbound write or retries. Missing,
malformed or unexpected attestation is an unknown outcome, not a rollback claim.
Transport timeout/reset also stays unknown. This prevents accidental wrong-project
dispatch; it is **not TCP authentication or a new native read-only security layer**.
Legacy unbound profiles remain compatible unless the project opts into
`RequireProjectBinding=True`; do not call those legacy profiles project-safe.

After deploying the native changes and configuring both projects during an
authorized maintenance window, restart each matching editor/client and verify
`get_editor_context` through the selected service. Do not equate copied source or
an available Python schema with a deployed DLL. This does not isolate shared CPU,
GPU, memory, or two agents editing the same asset in the same project.

Source verification: real loopback clients with two independent profiles/ports,
same-path asset reads to distinct test identities and intentional wrong-target
refusal; native startup INI port/strict reads, correct/wrong project, legacy/nested
refusal, real fragmented bound TCP, two ephemeral listeners and port-collision
refusal. All editor tests use the repository's isolated NoServer process, not NF
or SkillTest. NF and SkillTest deployment and simultaneous GUI acceptance were
subsequently authorized and completed on 2026-11-04 as described below.

### NF and SkillTest Deployment

Both projects now persist `RequireProjectBinding=True` with NF port13090 and
SkillTest port13091. Their existing primary service remains named `unrealMCP`
with original permissions/category filters. NF also has `skillTestReadOnly` in
its Claude and new workspace VS Code configuration; that service binds SkillTest
and exposes only read tools. SkillTest's other configured services remain intact.
No per-launch port argument was used for either accepted GUI startup.

Both deployed DLLs passed startup-INI, project-identity, bound TCP and temporary
port-collision tests. Actual NF/SkillTest GUIs then ran simultaneously: configured
VS Code/Claude primary and cross-read profiles (six profiles total) returned the
correct project, same-path Engine asset properties and narrow Blueprint reads.
NF stayed on its original L_map01/13090/PID while SkillTest opened on Entry/13091.
An intentional wrong-port read was rejected as `not_started`; both dirty counts
were zero at acceptance. This does not claim gameplay or asset-migration validation.

The first six-profile run exposed a startup idle-connection timeout. Python startup
now connects lazily only for actual commands, so extra services do not reserve idle
UE sockets. Its new lifecycle regression and all96 Python tests passed; the fresh
six-profile run passed. Initial timeout/raw records were retained, no write retried.

Backups and receipts in each project:
`Saved/UnrealMCPBackups/ProjectBinding_20261104_220742` and
`Saved/UnrealMCPDeployment/ProjectBinding_20261104_220742`.
Only connection-layer files and project/client binding configuration were deployed;
local plugin customizations were preserved. SkillTest did not receive unrelated
component-default or FText authoring upgrades. During the long deployment window,
NF had three existing Content hashes change and an added inventory item; SkillTest
had350 old paths disappear within NiagaraExplosion01 while total Content count was
unchanged. These concurrent content changes were recorded and not restored; do
not describe the projects' entire Content as unchanged. No production asset
authoring/move/delete/save request was issued by deployment acceptance.

Existing Agent/MCP processes must restart/reconnect once to load the new environment
and lazy client. Claude still reports project approval pending; the user approves
each service normally. Existing unbound services are refused by strict native
projects, never silently redirected. Future normal double-click project launches
read the persistent INI. Both GUIs were left open at acceptance; always verify live
PID/port rather than treating this dated deployment record as permanent state.

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
