# Reading Blueprints with Unreal MCP

This guide explains how to use the Blueprint reading tools to understand your Unreal Engine project structure through MCP.

## Overview

When working with AI assistants to modify or extend your Unreal Engine project, the assistant needs to understand the existing Blueprint architecture. Two tools are provided for this purpose:

| Tool | Purpose |
|------|---------|
| `list_blueprints` | Discover what Blueprints exist in your project |
| `read_blueprint` | Read the full structure of a specific Blueprint |

These tools are **read-only** — they inspect existing Blueprints without modifying them.

## Quick Start

### 1. Discover Blueprints in Your Project

Use `list_blueprints` to find what Blueprints exist:

```
"List all the blueprints in my project"
```

The AI assistant will call `list_blueprints()` and return a summary like:

```json
{
  "count": 3,
  "blueprints": [
    {
      "name": "BP_Player",
      "path": "/Game/Blueprints/BP_Player.BP_Player",
      "package_path": "/Game/Blueprints",
      "parent_class": "/Script/Engine.APawn"
    },
    {
      "name": "BP_Enemy",
      "path": "/Game/Blueprints/BP_Enemy.BP_Enemy",
      "package_path": "/Game/Blueprints",
      "parent_class": "/Script/Engine.AActor"
    },
    {
      "name": "BP_Projectile",
      "path": "/Game/Weapons/BP_Projectile.BP_Projectile",
      "package_path": "/Game/Weapons",
      "parent_class": "/Script/Engine.AActor"
    }
  ]
}
```

### 2. Read a Specific Blueprint

Once you know which Blueprint you're interested in, use `read_blueprint` to get its full structure:

```
"Read the BP_Player blueprint and explain its structure"
```

The AI assistant will call `read_blueprint(blueprint_name="BP_Player")` and get a detailed response.

---

## Tool Reference

### list_blueprints

List all Blueprint assets in the project, optionally filtered by path and name.

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `path` | string | `"/Game"` | Asset path to search in |
| `recursive` | bool | `true` | Whether to search subdirectories |
| `name_filter` | string | `""` | Substring filter for Blueprint names |

**Example Requests:**

```json
// List ALL blueprints in the project
{
  "command": "list_blueprints",
  "params": {}
}

// List only blueprints in /Game/Characters
{
  "command": "list_blueprints",
  "params": {
    "path": "/Game/Characters",
    "recursive": true
  }
}

// Find blueprints with "Player" in the name
{
  "command": "list_blueprints",
  "params": {
    "name_filter": "Player"
  }
}

// List blueprints in a specific folder, non-recursive
{
  "command": "list_blueprints",
  "params": {
    "path": "/Game/Blueprints",
    "recursive": false
  }
}
```

**Response Structure:**

```json
{
  "count": 2,
  "blueprints": [
    {
      "name": "BP_Player",
      "path": "/Game/Blueprints/BP_Player.BP_Player",
      "package_path": "/Game/Blueprints",
      "parent_class": "/Script/Engine.APawn"
    }
  ]
}
```

| Field | Description |
|-------|-------------|
| `count` | Total number of Blueprints found |
| `blueprints[].name` | Blueprint asset name |
| `blueprints[].path` | Full object path |
| `blueprints[].package_path` | Package/folder path |
| `blueprints[].parent_class` | Parent class path (if available from asset metadata) |

---

### read_blueprint

Read the complete structure of a Blueprint asset.

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `blueprint_name` | string | *(required)* | Name or full path of the Blueprint |
| `include_nodes` | bool | `true` | Include event graph node details |
| `include_properties` | bool | `true` | Include component property details |

**Blueprint Name Resolution:**

The tool is flexible in how you specify the Blueprint:

| Input | Resolution |
|-------|-----------|
| `"BP_Player"` | Searches `/Game/Blueprints/BP_Player` first, then scans the entire `/Game` tree |
| `"/Game/Characters/BP_Player"` | Loads directly from the specified path |
| `"/Game/UI/WBP_HUD"` | Loads directly from the specified path |

**Example Request:**

```json
{
  "command": "read_blueprint",
  "params": {
    "blueprint_name": "BP_Player"
  }
}
```

**Full Response Structure:**

The response contains several sections. Here is a complete example with all sections:

```json
{
  "name": "BP_Player",
  "path": "/Game/Blueprints/BP_Player",
  "parent_class": "Pawn",
  "parent_class_path": "/Script/Engine.Pawn",
  "blueprint_type": "Normal",

  "components": [
    {
      "name": "CapsuleComponent",
      "class": "CapsuleComponent",
      "location": [0, 0, 0],
      "rotation": [0, 0, 0],
      "scale": [1, 1, 1],
      "properties": {
        "CapsuleHalfHeight": "88.0",
        "CapsuleRadius": "34.0"
      }
    },
    {
      "name": "PlayerMesh",
      "class": "StaticMeshComponent",
      "location": [0, 0, -88],
      "rotation": [0, 0, 0],
      "scale": [1, 1, 1],
      "parent": "CapsuleComponent",
      "properties": {
        "StaticMesh": "StaticMesh'/Engine/BasicShapes/Cube.Cube'",
        "bSimulatePhysics": "False"
      }
    },
    {
      "name": "Camera",
      "class": "CameraComponent",
      "location": [0, 0, 300],
      "rotation": [-20, 0, 0],
      "scale": [1, 1, 1],
      "parent": "SpringArm",
      "properties": {
        "FieldOfView": "90.0",
        "bUsePawnControlRotation": "False"
      }
    }
  ],

  "variables": [
    {
      "name": "Health",
      "type": "real",
      "sub_category": "float",
      "is_array": false,
      "is_set": false,
      "is_map": false,
      "default_value": "100.0",
      "is_instance_editable": true,
      "is_blueprint_read_only": false,
      "category": "Stats",
      "tooltip": "Current health points"
    },
    {
      "name": "MoveSpeed",
      "type": "real",
      "sub_category": "float",
      "is_array": false,
      "is_set": false,
      "is_map": false,
      "default_value": "600.0",
      "is_instance_editable": true,
      "is_blueprint_read_only": false,
      "category": "Movement"
    },
    {
      "name": "Inventory",
      "type": "object",
      "sub_type": "Actor",
      "is_array": true,
      "is_set": false,
      "is_map": false,
      "is_instance_editable": false,
      "is_blueprint_read_only": false
    }
  ],

  "event_graphs": [
    {
      "name": "EventGraph",
      "node_count": 5,
      "nodes": [
        {
          "node_id": "A1B2C3D4-E5F6-7890-ABCD-EF1234567890",
          "node_class": "K2Node_Event",
          "node_title": "Event BeginPlay",
          "event_name": "ReceiveBeginPlay",
          "pos_x": 0,
          "pos_y": 0,
          "pins": [
            {
              "name": "then",
              "type": "exec",
              "direction": "Output",
              "is_connected": true,
              "linked_to": [
                {
                  "node_id": "B2C3D4E5-F6A7-8901-BCDE-F12345678901",
                  "pin_name": "execute"
                }
              ]
            },
            {
              "name": "OutputDelegate",
              "type": "delegate",
              "direction": "Output",
              "is_connected": false
            }
          ]
        },
        {
          "node_id": "B2C3D4E5-F6A7-8901-BCDE-F12345678901",
          "node_class": "K2Node_CallFunction",
          "node_title": "Print String",
          "function_name": "PrintString",
          "function_class": "KismetSystemLibrary",
          "pos_x": 300,
          "pos_y": 0,
          "pins": [
            {
              "name": "execute",
              "type": "exec",
              "direction": "Input",
              "is_connected": true
            },
            {
              "name": "InString",
              "type": "string",
              "direction": "Input",
              "is_connected": false
            },
            {
              "name": "then",
              "type": "exec",
              "direction": "Output",
              "is_connected": false
            }
          ]
        }
      ]
    }
  ],

  "functions": [
    {
      "name": "CalculateDamage",
      "node_count": 8
    },
    {
      "name": "ResetPlayer",
      "node_count": 3
    }
  ],

  "interfaces": [
    "BPI_Damageable",
    "BPI_Interactable"
  ]
}
```

---

## Response Field Reference

### Top-Level Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Blueprint asset name |
| `path` | string | Full asset path |
| `parent_class` | string | Parent class name (e.g., `"Actor"`, `"Pawn"`, `"Character"`) |
| `parent_class_path` | string | Full parent class path (e.g., `"/Script/Engine.Pawn"`) |
| `blueprint_type` | string | One of: `"Normal"`, `"MacroLibrary"`, `"Interface"`, `"FunctionLibrary"`, `"Other"` |

### Component Fields

Each entry in the `components` array contains:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Component variable name |
| `class` | string | Component class (e.g., `"StaticMeshComponent"`, `"CameraComponent"`) |
| `location` | [x, y, z] | Relative location |
| `rotation` | [pitch, yaw, roll] | Relative rotation |
| `scale` | [x, y, z] | Relative scale |
| `parent` | string | Parent component name (if any) |
| `properties` | object | Key-value pairs of component-specific properties |

### Variable Fields

Each entry in the `variables` array contains:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Variable name |
| `type` | string | Pin category type (e.g., `"real"`, `"bool"`, `"object"`, `"struct"`) |
| `sub_type` | string | Sub-type object name (for object/struct types) |
| `sub_category` | string | Sub-category (e.g., `"float"`, `"double"`) |
| `is_array` | bool | Whether this is an array variable |
| `is_set` | bool | Whether this is a set variable |
| `is_map` | bool | Whether this is a map variable |
| `default_value` | string | Default value (if set) |
| `is_instance_editable` | bool | Whether editable per-instance in the editor |
| `is_blueprint_read_only` | bool | Whether read-only in Blueprints |
| `category` | string | Variable category (if set) |
| `tooltip` | string | Variable tooltip (if set) |

### Event Graph Fields

Each entry in the `event_graphs` array contains:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Graph name (usually `"EventGraph"`) |
| `node_count` | number | Total number of nodes in the graph |
| `nodes` | array | List of node objects |

Each **node** contains:

| Field | Type | Description |
|-------|------|-------------|
| `node_id` | string | Unique GUID (use this for `connect_blueprint_nodes`) |
| `node_class` | string | Node class (e.g., `"K2Node_Event"`, `"K2Node_CallFunction"`) |
| `node_title` | string | Human-readable node title |
| `pos_x`, `pos_y` | number | Node position in the graph |
| `comment` | string | Node comment (if any) |
| `event_name` | string | Event name (only for event nodes) |
| `function_name` | string | Function name (only for function call nodes) |
| `function_class` | string | Function's owning class (only for function call nodes) |
| `pins` | array | List of pin objects |

Each **pin** contains:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Pin name |
| `type` | string | Pin type (e.g., `"exec"`, `"bool"`, `"real"`, `"object"`) |
| `direction` | string | `"Input"` or `"Output"` |
| `is_connected` | bool | Whether this pin has any connections |
| `linked_to` | array | List of linked pins (only if connected) |

Each **linked_to** entry contains:

| Field | Type | Description |
|-------|------|-------------|
| `node_id` | string | GUID of the connected node |
| `pin_name` | string | Name of the connected pin |

### Function Fields

Each entry in the `functions` array contains:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Function name |
| `node_count` | number | Number of nodes in the function graph |

### Interface Fields

The `interfaces` array contains string names of implemented Blueprint interfaces.

---

## Common Workflows

### Understanding a Blueprint Before Modifying It

```
Step 1: "List all blueprints in my project"
Step 2: "Read the BP_Player blueprint"
Step 3: "Add a PointLightComponent to BP_Player at location [0, 0, 100]"
```

### Comparing Two Blueprints

```
Step 1: "Read the BP_Enemy blueprint"
Step 2: "Read the BP_BossEnemy blueprint"
Step 3: "What components does BP_BossEnemy have that BP_Enemy doesn't?"
```

### Finding Blueprints by Category

```
// Find all character blueprints
"List all blueprints with 'Character' in the name"

// Find UI widgets
"List all blueprints in /Game/UI"

// Find all BP_ prefixed blueprints in a specific folder
"List blueprints in /Game/Gameplay with name filter BP_"
```

### Inspecting Event Graph Logic

```
Step 1: "Read the BP_Player blueprint"
Step 2: "What happens in the BeginPlay event?"
Step 3: "What function calls are connected to the Tick event?"
```

### Using Node IDs for Connections

After reading a Blueprint, you can use the `node_id` values from the response to create new connections:

```
Step 1: "Read BP_Player to see existing nodes"
Step 2: "Add a PrintString function node to BP_Player"
Step 3: "Connect the BeginPlay event to the new PrintString node"
   (The assistant will use the node_id from the read_blueprint response)
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| **"Blueprint not found"** | Check the name is correct. Try using `list_blueprints` first to find the exact name. If the Blueprint is not in `/Game/Blueprints/`, provide the full path. |
| **Empty components list** | The Blueprint may not have any custom components added via the Simple Construction Script. Native C++ components won't appear here. |
| **Missing properties on components** | Properties inherited from `UObject` and `UActorComponent` base classes are filtered out to reduce noise. Only class-specific properties are shown. |
| **Large response size** | For Blueprints with complex event graphs, the response can be large. Use `include_nodes=false` to skip graph node details, or `include_properties=false` to skip component properties. |
| **"Failed to connect to Unreal Engine"** | Make sure the Unreal Editor is running with the UnrealMCP plugin enabled. The TCP server runs on port 55557 by default. |

## Related Tools

- [Blueprint Tools](blueprint_tools.md) — Create and modify Blueprints (create, add components, set properties, compile)
- [Node Tools](node_tools.md) — Manipulate Blueprint graph nodes (add events, functions, connect nodes)
- [Editor Tools](editor_tools.md) — Control the Unreal Editor viewport and take screenshots
