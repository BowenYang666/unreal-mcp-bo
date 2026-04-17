# Unreal MCP (Fork)

Fork of [chongdashu/unreal-mcp](https://github.com/chongdashu/unreal-mcp) with additional read/query tools for AI-assisted Unreal Engine development. 

> **What's different from the original?** Add more tools to cover more cases, make it support 5.7.

## Supported Tools (73 total)

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
| **`get_unsaved_changes`** | **List all unsaved (dirty) content packages and maps in the editor** | **Added** |
| **`close_editor`** | **Gracefully close the Unreal Editor (optionally saving all unsaved work first)** | **Added** |

### Material Tools (Added)

| Tool | Description |
|------|-------------|
| **`list_materials`** | List all Material and MaterialInstance assets |
| **`read_material`** | Read a Material's node graph, expressions, connections, and input pins |
| **`get_material_instance_parameters`** | Read a MaterialInstance's scalar, vector, and texture parameter overrides |
| **`create_material`** | **Create a new Material asset with blend mode, shading model, domain options** |
| **`add_material_expression`** | **Add a material expression node (Constant, Multiply, Noise, Parameter, etc.)** |
| **`set_material_expression_property`** | **Set properties on a material node (float, color, vector, enum, string)** |
| **`connect_material_expressions`** | **Connect two material expression nodes (output → input)** |
| **`connect_material_to_property`** | **Connect a node to a material property (BaseColor, Normal, Roughness, etc.)** |
| **`add_custom_hlsl_expression`** | **Add a Custom HLSL node with code, output type, and named inputs** |
| **`create_material_instance`** | **Create a MaterialInstanceConstant with scalar/vector parameter overrides** |
| **`add_material_comment`** | **Add a comment box (group) to the material editor graph** |
| **`set_expression_position`** | **Move a material expression node to a specific (x, y) position** |
| **`reset_material_node_layout`** | **Auto-layout material nodes in horizontal rows per property chain** |

### UMG / Widget Tools (Original + Added)

| Tool | Description | Status |
|------|-------------|--------|
| `create_umg_widget_blueprint` | Create a UMG Widget Blueprint | Original |
| `add_text_block_to_widget` | Add a text block to a widget (supports anchor, alignment) | Original (enhanced) |
| `add_button_to_widget` | Add a button to a widget (supports anchor, alignment) | Original (enhanced) |
| `bind_widget_event` | Bind an event to a widget element | Original |
| `set_text_block_binding` | Set text binding on a text block | Original |
| `add_widget_to_viewport` | Add a widget to the viewport | Original |
| **`add_progress_bar_to_widget`** | **Add a ProgressBar to a widget (percent, fill color, fill type). For health bars, loading bars, etc.** | **Added** |
| **`add_image_to_widget`** | **Add an Image widget (texture/brush, tint color, size). For backgrounds, icons, etc.** | **Added** |
| **`add_vertical_box_to_widget`** | **Add a VerticalBox layout container. Children stack top-to-bottom.** | **Added** |
| **`add_horizontal_box_to_widget`** | **Add a HorizontalBox layout container. Children stack left-to-right.** | **Added** |
| **`add_overlay_to_widget`** | **Add an Overlay container. Children stack on top of each other (z-order).** | **Added** |
| **`add_size_box_to_widget`** | **Add a SizeBox to constrain child width/height (e.g., fixed-size health bar wrapper).** | **Added** |
| **`add_border_to_widget`** | **Add a Border widget (background color/brush with one child slot).** | **Added** |
| **`add_spacer_to_widget`** | **Add a Spacer for padding/spacing between elements in layout containers.** | **Added** |
| **`set_widget_anchor`** | **Set anchor, alignment, offset, and size on any existing widget in a CanvasPanel.** | **Added** |
| **`set_widget_slot_property`** | **Set slot properties (size_rule, padding, h/v alignment) on any widget in any slot type (Canvas, HBox, VBox, Overlay).** | **Added** |
| **`read_widget_layout`** | **Read the full widget tree layout (recursive: name, type, slot, properties, children). Read-only.** | **Added** |

### Niagara / VFX Tools (Added)

| Tool | Description |
|------|-------------|
| **`create_niagara_system`** | **Create an empty Niagara particle system asset** |
| **`list_niagara_systems`** | **List all Niagara systems in the project** |
| **`read_niagara_system`** | **Read full system structure: emitters, modules, rapid iteration params** |
| **`add_emitter_to_system`** | **Add emitter from template, duplicate, or cross-system copy** |
| **`remove_emitter_from_system`** | **Remove an emitter from a system** |
| **`list_niagara_emitter_templates`** | **List available engine emitter templates** |
| **`add_module_to_emitter`** | **Add a module script to an emitter's stack** |
| **`remove_module_from_emitter`** | **Remove a module with automatic pin chain bridging** |
| **`set_niagara_rapid_parameter`** | **Set rapid iteration parameter (spawn/update script)** |
| **`set_niagara_parameter`** | **Set system/emitter-level parameter** |
| **`get_niagara_parameters`** | **Get all parameters for a Niagara component** |
| **`modify_emitter_properties`** | **Modify emitter sim target, determinism, local space** |

### Project Tools (Original + Added)

| Tool | Description | Status |
|------|-------------|--------|
| `create_input_mapping` | Create an input action mapping | Original |
| **`read_data_asset`** | **Read any DataAsset's properties via reflection (generic, works on all UDataAsset subclasses)** | **Added** |
| **`get_class_properties`** | **Discover all editable properties of any UClass or asset (name, type, category, current value). Supports filtering by category.** | **Added** |

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

**Read-only tools (12):**

| Tool | Category |
|------|----------|
| `get_actors_in_level` | Actor |
| `find_actors_by_name` | Actor |
| `get_actor_properties` | Actor |
| `list_blueprints` | Blueprint |
| `read_blueprint` | Blueprint |
| `find_blueprint_nodes` | Blueprint Node |
| `get_editor_logs` | Editor |
| `get_unsaved_changes` | Editor |
| `close_editor` | Editor |
| `list_materials` | Material |
| `read_material` | Material |
| `get_material_instance_parameters` | Material |
| `read_widget_layout` | UMG / Widget |
| `list_niagara_systems` | Niagara |
| `read_niagara_system` | Niagara |
| `list_niagara_emitter_templates` | Niagara |
| `get_niagara_parameters` | Niagara |\n| `get_class_properties` | Project |

Set `UNREAL_MCP_READ_ONLY=0` or remove the `env` block to enable all 76 tools.

## Setup

See the [original repo](https://github.com/chongdashu/unreal-mcp) for full setup instructions, or:

1. Copy `MCPGameProject/Plugins/UnrealMCP` to your project's `Plugins/` folder
2. Build the project (requires UE 5.5+, tested on UE 5.7)
3. Configure your MCP client to point to the Python server

Detailed deployment guide: [Docs/copy_plugin_to_project.md](Docs/copy_plugin_to_project.md)

## License

MIT — same as the original project.