# Unreal MCP (Fork)

Fork of [chongdashu/unreal-mcp](https://github.com/chongdashu/unreal-mcp) with additional read/query tools for AI-assisted Unreal Engine development.

> **What's different from the original?** This fork adds read-only tools (blueprint reading, editor log capture, material inspection) and a read-only mode so AI can safely understand your project without modifying it.

## Supported Tools (36 total)

### Actor Tools (Original)

| Tool | Description |
|------|-------------|
| `get_actors_in_level` | List all actors in the current level |
| `find_actors_by_name` | Find actors by name pattern |
| `get_actor_properties` | Get all properties of an actor |
| `spawn_actor` | Create a new actor (cube, sphere, light, camera, etc.) |
| `delete_actor` | Delete an actor by name |
| `set_actor_transform` | Set position, rotation, scale of an actor |
| `set_actor_property` | Set a specific property on an actor |
| `spawn_blueprint_actor` | Spawn an instance of a Blueprint class |

### Blueprint Tools (Original + Added)

| Tool | Description | Status |
|------|-------------|--------|
| `create_blueprint` | Create a new Blueprint class | Original |
| `add_component_to_blueprint` | Add a component to a Blueprint | Original |
| `set_component_property` | Set a property on a Blueprint component | Original |
| `set_physics_properties` | Configure physics on a Blueprint component | Original |
| `set_blueprint_property` | Set a Blueprint-level property | Original |
| `set_static_mesh_properties` | Set static mesh and material on a component | Original |
| `compile_blueprint` | Compile a Blueprint | Original |
| **`read_blueprint`** | **Read Blueprint structure (components, variables, graphs, interfaces)** | **Added** |
| **`list_blueprints`** | **List all Blueprint assets with filtering** | **Added** |

### Blueprint Node Graph Tools (Original)

| Tool | Description |
|------|-------------|
| `add_blueprint_event_node` | Add an event node (BeginPlay, Tick, etc.) |
| `add_blueprint_function_node` | Add a function call node |
| `connect_blueprint_nodes` | Connect two nodes in the graph |
| `add_blueprint_variable` | Add a variable to a Blueprint |
| `add_blueprint_get_self_component_reference` | Add a component reference node |
| `add_blueprint_self_reference` | Add a self-reference node |
| `add_blueprint_input_action_node` | Add an input action node |
| `find_blueprint_nodes` | Find nodes in a Blueprint graph |

### Editor Tools (Original + Added)

| Tool | Description | Status |
|------|-------------|--------|
| `focus_viewport` | Focus viewport on an actor or location | Original |
| `take_screenshot` | Capture a viewport screenshot | Original |
| **`get_editor_logs`** | **Read UE output log from file (filter by verbosity, category, keyword). Set `UNREAL_PROJECT_LOG` env var or pass `log_path`.** | **Added** |

### Material Tools (Added)

| Tool | Description |
|------|-------------|
| **`list_materials`** | List all Material and MaterialInstance assets |
| **`read_material`** | Read a Material's node graph, expressions, connections, and input pins |
| **`get_material_instance_parameters`** | Read a MaterialInstance's scalar, vector, and texture parameter overrides |

### UMG / Widget Tools (Original)

| Tool | Description |
|------|-------------|
| `create_umg_widget_blueprint` | Create a UMG Widget Blueprint |
| `add_text_block_to_widget` | Add a text block to a widget |
| `add_button_to_widget` | Add a button to a widget |
| `bind_widget_event` | Bind an event to a widget element |
| `set_text_block_binding` | Set text binding on a text block |
| `add_widget_to_viewport` | Add a widget to the viewport |

### Project Tools (Original)

| Tool | Description |
|------|-------------|
| `create_input_mapping` | Create an input action mapping |

## Read-Only Mode

Set the `UNREAL_MCP_READ_ONLY` environment variable to restrict the AI to query-only tools — no creation, deletion, or modification.

```jsonc
// mcp.json
{
    "servers": {
        "unrealMCP": {
            "command": "uv",
            "args": ["--directory", "<path-to>/Python", "run", "unreal_mcp_server.py"],
            "env": {
                "UNREAL_MCP_READ_ONLY": "1",
                "UNREAL_PROJECT_LOG": "D:/UnrealProjects/MyProject/Saved/Logs/MyProject.log"
            }
        }
    }
}
```

**Read-only tools (10):**

| Tool | Category |
|------|----------|
| `get_actors_in_level` | Actor |
| `find_actors_by_name` | Actor |
| `get_actor_properties` | Actor |
| `list_blueprints` | Blueprint |
| `read_blueprint` | Blueprint |
| `find_blueprint_nodes` | Blueprint Node |
| `get_editor_logs` | Editor |
| `list_materials` | Material |
| `read_material` | Material |
| `get_material_instance_parameters` | Material |

Set `UNREAL_MCP_READ_ONLY=0` or remove the `env` block to enable all 36 tools.

## Setup

See the [original repo](https://github.com/chongdashu/unreal-mcp) for full setup instructions, or:

1. Copy `MCPGameProject/Plugins/UnrealMCP` to your project's `Plugins/` folder
2. Build the project (requires UE 5.5+, tested on UE 5.7)
3. Configure your MCP client to point to the Python server

Detailed deployment guide: [Docs/copy_plugin_to_project.md](Docs/copy_plugin_to_project.md)

## License

MIT — same as the original project.