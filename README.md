# Unreal MCP (Fork)

Fork of [chongdashu/unreal-mcp](https://github.com/chongdashu/unreal-mcp) for AI-assisted Unreal Engine 5.5+ inspection and authoring. This fork is developed and tested on UE 5.7.

## Tool Surface

The Python MCP server currently registers **99 tools** before read-only/category filtering.

| Category | Count | Reference |
|---|---:|---|
| Actor + Editor | 16 | [Actor tools](Docs/Tools/actor_tools.md), [Editor tools](Docs/Tools/editor_tools.md) |
| Blueprint assets | 9 | [Blueprint tools](Docs/Tools/blueprint_tools.md), [reading guide](Docs/Tools/reading_blueprints.md) |
| Blueprint nodes | 8 | [Node tools](Docs/Tools/node_tools.md) |
| Materials | 15 | [Material tools](Docs/Tools/material_tools.md) |
| UMG / Widgets | 20 | [UMG tools](Docs/Tools/umg_tools.md) |
| Niagara | 25 | [Niagara tools](Docs/Tools/niagara_tools.md) |
| Project / AI assets | 6 | [Project tools](Docs/Tools/project_tools.md) |

Notable read support includes Blueprint collapsed subgraphs and pin defaults, Material compile results, Niagara dynamic inputs/curves/renderers, arbitrary reflected UObject properties, Behavior Trees, Blackboards, and StateTrees.

`focus_viewport` and `take_screenshot` remain legacy C++ commands but do not have registered Python MCP tools, so they are not part of the supported tool surface.

## Architecture

```text
MCP client (VS Code/Copilot or Claude Code)
  -> stdio: Python/unreal_mcp_server.py
  -> TCP: 127.0.0.1:13090
  -> Unreal Editor plugin: Plugins/UnrealMCP
```

The editor plugin must be built and loaded in the target project. The shared Python server stays in this repository; do not copy `Python/` into every Unreal project.

## Onboard a Project

Use the complete onboarding workflow in:

- [UnrealMCP project onboarding skill](.github/skills/unreal-mcp-project-onboarding/SKILL.md)
- [Deployment guide](Docs/copy_plugin_to_project.md)

The workflow discovers the `.uproject`, checks UE version, deploys and builds the plugin, configures the selected MCP client, launches the editor, verifies port `13090`, and performs a read-only smoke test.

## MCP Client Configuration

Resolve an absolute `uv` executable with `Get-Command uv`; GUI clients may not inherit your shell `PATH`.

### VS Code / Copilot

Create `.vscode/mcp.json` in the workspace:

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
      "env": {
        "UNREAL_MCP_READ_ONLY": "1"
      }
    }
  }
}
```

### Claude Code

Create `.mcp.json` at the Claude project root:

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
      "env": {
        "UNREAL_MCP_READ_ONLY": "1"
      }
    }
  }
}
```

Project-scoped Claude servers require one-time approval. Run `claude` in the project root and approve `unrealMCP`; `claude mcp list` shows `Pending approval` until then.

The repository's `mcp.json` is a Claude-format example template. Claude auto-discovers `.mcp.json`, not `mcp.json`.

## Read-Only Mode

Set `UNREAL_MCP_READ_ONLY=1` for project learning, review, or reverse engineering. The server retains exactly these 24 query tools:

- Actor/editor: `get_actors_in_level`, `find_actors_by_name`, `get_actor_properties`, `get_editor_logs`, `get_unsaved_changes`
- Blueprint/node: `list_blueprints`, `read_blueprint`, `find_blueprint_nodes`
- Project/AI assets: `get_class_properties`, `read_data_asset`, `read_behavior_tree`, `read_blackboard`, `read_state_tree`
- Material: `list_materials`, `read_material`, `get_material_instance_parameters`
- UMG: `read_widget_layout`
- Niagara: `list_niagara_systems`, `read_niagara_system`, `get_niagara_parameters`, `list_module_inputs`, `list_module_static_switches`, `read_ns_curve`, `list_renderer_types`

Set `UNREAL_MCP_READ_ONLY=0` (or omit it) for authoring.

## Category Filtering

Category filters can be combined with read-only mode. A value of `0`, `false`, `no`, or `off` disables the category; unset/other values enable it.

| Environment variable | Category |
|---|---|
| `MCP_EDITOR_ENABLED` | Actors, editor state, assets, levels, logs |
| `MCP_BLUEPRINT_ENABLED` | Blueprint asset creation/properties/reading |
| `MCP_NODE_ENABLED` | Blueprint graph node operations/search |
| `MCP_PROJECT_ENABLED` | Input mappings, reflection, BT/BB/StateTree reads |
| `MCP_UMG_ENABLED` | Widget creation/layout/reading |
| `MCP_MATERIAL_ENABLED` | Material and MaterialInstance tools |
| `MCP_NIAGARA_ENABLED` | Niagara system/emitter/module/renderer tools |

## Development Updates

- Python-only changes: restart the MCP client/server; no Unreal build is required.
- C++ plugin or `Build.cs` changes: save/close the target editor, replace the deployed plugin from this repository, clean stale plugin build outputs if needed, rebuild `<ProjectName>Editor`, relaunch, and verify `127.0.0.1:13090`.
- After schema changes, restart the MCP client so it discovers the new tool parameters.

## Documentation

See [Docs/README.md](Docs/README.md) and [Docs/Tools/README.md](Docs/Tools/README.md) for the complete reference.

## License

MIT, matching the upstream project.
