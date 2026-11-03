# Unreal MCP (Fork)

Fork of [chongdashu/unreal-mcp](https://github.com/chongdashu/unreal-mcp) for AI-assisted Unreal Engine 5.5+ inspection and authoring. This fork is developed and tested on UE 5.7.

## Tool Surface

**167 internal operations**, exposed as **57 tools** in default grouped mode.
Material, Niagara, UMG, Scene, Blueprint and Asset use search/read/write entry
points; other categories remain direct. `MCP_TOOL_MODE=direct` exposes original names.

| Category | Operations | Access In Grouped Mode | Main Capabilities |
|---|---:|---|---|
| [Material](Docs/Tools/material_tools.md) | 17 | `material_search` | Material graphs, instances, parameters and physical-material assignment |
| [Niagara](Docs/Tools/niagara_tools.md) | 36 | `niagara_search` | Systems, emitters, modules, curves, renderers, User bindings and scalability |
| [UMG](Docs/Tools/umg_tools.md) | 23 | `umg_search` | Widgets, layout, bindings, reparenting and Designer preview settings |
| [Scene](Docs/Tools/scene_tools.md) | 20 | `scene_search` | Guarded level instances, placement, captures, imports, Outliner folders and undo |
| [Blueprint](Docs/Tools/blueprint_tools.md) | 11 | `blueprint_search` | Asset structure, components, properties, inherited skeletal defaults and compilation |
| [Asset](Docs/Tools/project_tools.md) | 21 | `asset_search` | Asset management, DataAssets, FText/String Tables, animation, Enhanced Input and dependency migration |
| [Actor / Editor](Docs/Tools/editor_tools.md) | 18 | Direct | Actor inspection/editing, editor state, logs, opening assets/levels and explicit saves |
| [Blueprint Node](Docs/Tools/node_tools.md) | 8 | Direct | Graph nodes, events, variables and connections |
| [Project / AI](Docs/Tools/project_tools.md#ai--asset-reading) | 6 | Direct | Reflected properties, DataAsset/Behavior Tree/Blackboard/StateTree reads and legacy input mappings |
| [Navigation](Docs/Tools/navigation_tools.md) | 6 | Direct | NavMesh bounds, build/status, point projection and pathfinding |
| [Cascade](Docs/Tools/cascade_tools.md) | 1 | Direct | Read legacy ParticleSystem emitters, LODs, modules and curves |

For grouped operations, use `<category>_search(tool="<operation>")` to obtain the
exact contract, then the returned `*_call_read` or `*_call_write` with `tool` and
`arguments`. Reuse contracts; original operation names are not public in grouped mode.
The table counts internal operations, not public entry points; totals assume all categories enabled.

See the [tool index and calling guide](Docs/Tools/README.md) for all contracts and
examples. Grouping changes discovery, not permissions or save behavior. Python
tool visibility alone does not prove native plugin support.

## Architecture

```text
MCP client (VS Code/Copilot or Claude Code)
  -> stdio: Python/unreal_mcp_server.py
  -> TCP: 127.0.0.1:13090
  -> Unreal Editor plugin: Plugins/UnrealMCP
```

Build/load the plugin in the target project. Keep the shared Python server here.

## Onboard a Project

Follow the [deployment guide](Docs/copy_plugin_to_project.md) or
[onboarding skill](.github/skills/unreal-mcp-project-onboarding/SKILL.md): deploy,
build, configure the client, launch and verify the intended editor.

## MCP Client Configuration

Use the [VS Code / Claude configuration examples](Docs/copy_plugin_to_project.md#step-8-configure-the-mcp-client)
with an absolute `uv` path. Claude project servers require user approval.
The repository [mcp.json](mcp.json) is a template, not an auto-discovered client config.

## Read-Only Mode

Set `UNREAL_MCP_READ_ONLY=1` for project learning, review, or reverse engineering.
This retains 49 query operations as **29 grouped tools** or **49 direct tools**
before category filtering. Write endpoints are absent. Authoring uses `0` (or unset).

## Category Filtering

`MCP_<CATEGORY>_ENABLED=0` disables a category before grouping; read-only filters
still apply. Blueprint and Blueprint Node have independent switches.
See [environment controls](Python/README.md#environment-controls) for the full list.

## Development Updates

- Python/grouping changes: restart/reconnect the MCP service; no UE rebuild or editor restart.
- Native plugin changes: follow the deployment guide, preserve target customizations and user drafts, rebuild and verify.
- Always verify project/port identity and review actual save/failure receipts; never blindly retry timed-out writes.

## Documentation

See [Docs/README.md](Docs/README.md) and [Docs/Tools/README.md](Docs/Tools/README.md) for the complete reference.

## License

MIT, matching the upstream project.
