# Build & Deploy MCP Plugin

Use this skill whenever you need to rebuild the UnrealMCP C++ plugin or update Python MCP tools. This ensures the MCP server is properly restarted so VS Code picks up the new code.

## Full Build & Deploy Workflow

### Step 1: Kill MCP Server Processes

Before any build or Python code change, kill all running `unreal_mcp_server` processes so VS Code will auto-restart them with the new code:

```powershell
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match "unreal_mcp_server" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
```

### Step 2: Deploy C++ Source (if C++ changed)

Copy updated source files from the repo to the target project. Only copy the contents, not the container folder:

```powershell
Copy-Item -Path "D:\githubcode\unreal-mcp-bo\MCPGameProject\Plugins\UnrealMCP\Source\UnrealMCP\*" -Destination "D:\UnrealProjects\Beginer_project_list\ThirdPersonTest1\Plugins\UnrealMCP\Source\UnrealMCP\" -Recurse -Force
```

### Step 3: Close UE Editor (if C++ changed)

The editor must be closed for external builds (Live Coding blocks them):

```powershell
Stop-Process -Name "UnrealEditor" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
```

### Step 4: Clean Intermediate Files (if build fails or first deploy)

Only needed when the build system doesn't detect changes (uses git status) or after structural changes:

```powershell
Remove-Item "D:\UnrealProjects\Beginer_project_list\ThirdPersonTest1\Plugins\UnrealMCP\Intermediate" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "D:\UnrealProjects\Beginer_project_list\ThirdPersonTest1\Plugins\UnrealMCP\Binaries" -Recurse -Force -ErrorAction SilentlyContinue
```

### Step 5: Build

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ThirdPersonTest1Editor Win64 Development "-project=D:\UnrealProjects\Beginer_project_list\ThirdPersonTest1\ThirdPersonTest1.uproject" 2>&1 | Select-Object -Last 15
```

### Step 6: Start UE Editor

```powershell
Start-Process "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList """D:\UnrealProjects\Beginer_project_list\ThirdPersonTest1\ThirdPersonTest1.uproject"""
```

## Python-Only Changes

If only Python tools changed (files in `Python/tools/`), you only need Step 1. VS Code will auto-restart the MCP server and load the new Python code. No C++ build or UE restart needed.

## Common Issues

- **"Target is up to date"**: The build uses `git status` for adaptive builds. If the project folder isn't git-tracked, clean Intermediate/Binaries to force rebuild (Step 4).
- **"Live Coding is active"**: Close UE editor first (Step 3).
- **Nested Source/Source directory**: When copying, use `Source\UnrealMCP\*` as the source path, NOT `Source` directly, to avoid creating `Source\Source\UnrealMCP\`.
- **MCP tools not updating**: Kill the MCP server processes (Step 1). VS Code auto-restarts them.
