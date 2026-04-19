# Unreal MCP Blueprint Tools

This document provides detailed information about the Blueprint tools available in the Unreal MCP integration.

## Overview

Blueprint tools allow you to create and manipulate Blueprint assets in Unreal Engine, including creating new Blueprint classes, adding components, setting properties, and spawning Blueprint actors in the level.

## Blueprint Tools

### create_blueprint

Create a new Blueprint class.

**Parameters:**
- `name` (string) - The name for the new Blueprint class
- `parent_class` (string) - The parent class for the Blueprint

**Returns:**
- Information about the created Blueprint including success status and message

**Example:**
```json
{
  "command": "create_blueprint",
  "params": {
    "name": "MyActor",
    "parent_class": "Actor"
  }
}
```

### add_component_to_blueprint

Add a component to a Blueprint.

**Parameters:**
- `blueprint_name` (string) - The name of the Blueprint
- `component_type` (string) - The type of component to add (use component class name without U prefix)
- `component_name` (string) - The name for the new component
- `location` (array, optional) - [X, Y, Z] coordinates for component's position, defaults to [0, 0, 0]
- `rotation` (array, optional) - [Pitch, Yaw, Roll] values for component's rotation, defaults to [0, 0, 0]
- `scale` (array, optional) - [X, Y, Z] values for component's scale, defaults to [1, 1, 1]
- `component_properties` (object, optional) - Additional properties to set on the component

**Returns:**
- Information about the added component including success status and message

**Example:**
```json
{
  "command": "add_component_to_blueprint",
  "params": {
    "blueprint_name": "MyActor",
    "component_type": "StaticMeshComponent",
    "component_name": "Mesh",
    "location": [0, 0, 0],
    "rotation": [0, 0, 0],
    "scale": [1, 1, 1],
    "component_properties": {
      "bVisible": true
    }
  }
}
```

### set_static_mesh_properties

Set the mesh for a StaticMeshComponent.

**Parameters:**
- `blueprint_name` (string) - The name of the Blueprint
- `component_name` (string) - The name of the StaticMeshComponent
- `static_mesh` (string, default: "/Engine/BasicShapes/Cube.Cube") - Path to the static mesh asset

**Returns:**
- Result of the mesh setting operation including success status and message

**Example:**
```json
{
  "command": "set_static_mesh_properties",
  "params": {
    "blueprint_name": "MyActor",
    "component_name": "Mesh",
    "static_mesh": "/Engine/BasicShapes/Sphere.Sphere"
  }
}
```

### set_component_property

Set a property on a component in a Blueprint.

**Parameters:**
- `blueprint_name` (string) - The name of the Blueprint
- `component_name` (string) - The name of the component
- `property_name` (string) - The name of the property to set
- `property_value` (any) - The value to set for the property

**Returns:**
- Result of the property setting operation including success status and message

**Example:**
```json
{
  "command": "set_component_property",
  "params": {
    "blueprint_name": "MyActor",
    "component_name": "Mesh",
    "property_name": "StaticMesh",
    "property_value": "/Game/StarterContent/Shapes/Shape_Cube.Shape_Cube"
  }
}
```

### set_physics_properties

Set physics properties on a component.

**Parameters:**
- `blueprint_name` (string) - The name of the Blueprint
- `component_name` (string) - The name of the component
- `simulate_physics` (boolean, optional) - Whether to simulate physics, defaults to true
- `gravity_enabled` (boolean, optional) - Whether gravity is enabled, defaults to true
- `mass` (float, optional) - The mass of the component, defaults to 1.0
- `linear_damping` (float, optional) - Linear damping value, defaults to 0.01
- `angular_damping` (float, optional) - Angular damping value, defaults to 0.0

**Returns:**
- Result of the physics properties setting operation including success status and message

**Example:**
```json
{
  "command": "set_physics_properties",
  "params": {
    "blueprint_name": "MyActor",
    "component_name": "Mesh",
    "simulate_physics": true,
    "gravity_enabled": true,
    "mass": 10.0,
    "linear_damping": 0.05,
    "angular_damping": 0.1
  }
}
```

### compile_blueprint

Compile a Blueprint.

**Parameters:**
- `blueprint_name` (string) - The name of the Blueprint to compile

**Returns:**
- Result of the compilation operation including success status and message

**Example:**
```json
{
  "command": "compile_blueprint",
  "params": {
    "blueprint_name": "MyActor"
  }
}
```

### set_blueprint_property

Set a property on a Blueprint class default object.

**Parameters:**
- `blueprint_name` (string) - The name of the Blueprint
- `property_name` (string) - The name of the property to set
- `property_value` (any) - The value to set for the property

**Returns:**
- Result of the property setting operation including success status and message

**Example:**
```json
{
  "command": "set_blueprint_property",
  "params": {
    "blueprint_name": "MyActor",
    "property_name": "bCanBeDamaged",
    "property_value": true
  }
}
```

### set_pawn_properties

Set common Pawn properties on a Blueprint.

**Parameters:**
- `blueprint_name` (string) - Name of the target Blueprint (must be a Pawn or Character)
- `auto_possess_player` (string, optional) - Auto possess player setting (None, "Disabled", "Player0", "Player1", etc.), defaults to empty string
- `use_controller_rotation_yaw` (boolean, optional) - Whether the pawn should use the controller's yaw rotation, defaults to false
- `use_controller_rotation_pitch` (boolean, optional) - Whether the pawn should use the controller's pitch rotation, defaults to false
- `use_controller_rotation_roll` (boolean, optional) - Whether the pawn should use the controller's roll rotation, defaults to false
- `can_be_damaged` (boolean, optional) - Whether the pawn can be damaged, defaults to true

**Returns:**
- Response indicating success or failure with detailed results for each property

**Example:**
```json
{
  "command": "set_pawn_properties",
  "params": {
    "blueprint_name": "MyPawn",
    "auto_possess_player": "Player0",
    "use_controller_rotation_yaw": true,
    "can_be_damaged": true
  }
}
```

### spawn_blueprint_actor

Spawn an actor from a Blueprint.

**Parameters:**
- `blueprint_name` (string) - The name of the Blueprint to spawn
- `actor_name` (string) - The name for the spawned actor
- `location` (array, optional) - [X, Y, Z] coordinates for the actor's position, defaults to [0, 0, 0]
- `rotation` (array, optional) - [Pitch, Yaw, Roll] values for the actor's rotation, defaults to [0, 0, 0]
- `scale` (array, optional) - [X, Y, Z] values for the actor's scale, defaults to [1, 1, 1]

**Returns:**
- Information about the spawned actor including success status and message

**Example:**
```json
{
  "command": "spawn_blueprint_actor",
  "params": {
    "blueprint_name": "MyActor",
    "actor_name": "MyActorInstance",
    "location": [0, 0, 100],
    "rotation": [0, 45, 0],
    "scale": [1, 1, 1]
  }
}
```

## Error Handling

All command responses include a "success" field indicating whether the operation succeeded, and a "message" field with details in case of failure.

```json
{
  "success": false,
  "message": "Failed to connect to Unreal Engine"
}
```

### read_blueprint

Read the full structure of a Blueprint asset, including its parent class, components, variables, event graph nodes, functions, and implemented interfaces. This tool is useful for understanding the structure of an existing Blueprint in your project.

**Parameters:**
- `blueprint_path` (string) - Full asset path of the Blueprint, e.g. "/Game/Player/Blueprints/BP_Player".
- `include_nodes` (bool, optional) - Whether to include detailed event graph node info. Defaults to true.
- `include_properties` (bool, optional) - Whether to include component property details. Defaults to true.
- `include_anim_graph` (bool, optional) - Whether to include structured AnimGraph data (states, transitions, connections). Only applies to Animation Blueprints. Defaults to false.

**Returns:**

Top-level fields:

| Field | Description |
|-------|-------------|
| `name` | Blueprint asset name |
| `path` | Full asset path |
| `parent_class` | Parent class name |
| `blueprint_type` | Normal, MacroLibrary, Interface, etc. |
| `components` | List of components with class, transform, parent, and properties |
| `variables` | List of variables with types, defaults, and metadata |
| `event_graphs` | Event graph nodes with pins and connections |
| `anim_graphs` | (AnimBP only) Animation graph with structured nodes and connections |
| `functions` | List of Blueprint functions with nodes |
| `interfaces` | List of implemented interfaces |
| `class_defaults` | CDO overridden values |

---

#### Event Graph Format

Event graphs are fully expanded with nodes, pins, and connections:

```json
{
  "event_graphs": [
    {
      "name": "EventGraph",
      "nodes": [
        {
          "node_id": "GUID",
          "node_class": "K2Node_Event",
          "node_title": "Event BeginPlay",
          "pos_x": 0,
          "pos_y": 0,
          "event_name": "ReceiveBeginPlay",
          "pins": [
            {
              "name": "then",
              "type": "exec",
              "direction": "Output",
              "is_connected": true,
              "linked_to": [
                { "node_id": "TARGET_GUID", "pin_name": "execute" }
              ]
            }
          ]
        },
        {
          "node_id": "TARGET_GUID",
          "node_class": "K2Node_CallFunction",
          "node_title": "Print String",
          "function_name": "PrintString",
          "function_class": "KismetSystemLibrary",
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
              "default_value": "Hello"
            }
          ]
        }
      ]
    }
  ]
}
```

---

#### Animation Graph Format (AnimBP only)

For Animation Blueprints, the `anim_graphs` field provides structured node and connection data.
Each AnimGraph node uses a simplified `type` field instead of the full class name.

**Top-level AnimGraph nodes:**

```json
{
  "anim_graphs": [
    {
      "name": "AnimGraph",
      "nodes": [
        {
          "node_id": "GUID",
          "type": "StateMachine",
          "name": "Locomotion",
          "pos_x": 0,
          "pos_y": 0,
          "properties": {
            "max_transitions_per_frame": 3
          },
          "states": [ "..." ],
          "transitions": [ "..." ]
        },
        {
          "node_id": "GUID",
          "type": "Slot",
          "slot_name": "DefaultSlot"
        },
        {
          "node_id": "GUID",
          "type": "AimOffset",
          "blend_space": "/Game/Animations/AO_Rifle",
          "alpha": 1.0
        },
        {
          "node_id": "GUID",
          "type": "OutputPose"
        }
      ],
      "connections": [
        { "from": "StateMachine_GUID", "to": "Slot_GUID", "to_pin": "Source" },
        { "from": "Slot_GUID", "to": "AimOffset_GUID", "to_pin": "BasePose" },
        { "from": "AimOffset_GUID", "to": "OutputPose_GUID", "to_pin": "Result" }
      ]
    }
  ]
}
```

**StateMachine states and transitions:**

```json
{
  "type": "StateMachine",
  "name": "Locomotion",
  "states": [
    {
      "name": "Idle",
      "nodes": [
        {
          "type": "SequencePlayer",
          "sequence": "/Game/Anims/MM_Idle.MM_Idle",
          "loop": true,
          "play_rate": 1.0
        }
      ]
    },
    {
      "name": "Walk",
      "nodes": [
        {
          "type": "BlendSpace",
          "blend_space": "/Game/Anims/BS_Locomotion",
          "samples": [
            "/Game/Anims/MM_Walk_Fwd.MM_Walk_Fwd",
            "/Game/Anims/MM_Walk_Bwd.MM_Walk_Bwd",
            "/Game/Anims/MM_Walk_Left.MM_Walk_Left",
            "/Game/Anims/MM_Walk_Right.MM_Walk_Right"
          ]
        }
      ]
    }
  ],
  "transitions": [
    {
      "from": "Idle",
      "to": "Walk",
      "blend_time": 0.2,
      "blend_mode": "Standard",
      "variables_used": ["Speed"]
    },
    {
      "from": "Walk",
      "to": "Idle",
      "blend_time": 0.2,
      "blend_mode": "Standard",
      "variables_used": ["Speed"]
    }
  ]
}
```

**Other AnimGraph node types:**

| Type | Key fields | Description |
|------|------------|-------------|
| `OutputPose` | — | AnimGraph output (root) |
| `StateMachine` | `name`, `states[]`, `transitions[]` | State machine with states and transition rules |
| `SequencePlayer` | `sequence`, `loop`, `play_rate` | Plays a single animation |
| `BlendSpace` | `blend_space`, `samples[]` | 1D/2D blend space with sample animations |
| `Slot` | `slot_name` | Montage slot node |
| `AimOffset` | `blend_space`, `alpha` | Rotation offset blend space (aim offset) |
| `LayeredBoneBlend` | `branch_filters[]`, `blend_weights[]` | Per-bone layered blend |
| `ApplyAdditive` | `alpha` | Apply additive animation |
| `BlendByBool` | `blend_time` | Blend between two poses by bool |
| `SaveCachedPose` | `pose_name` | Cache a pose for reuse |
| `UseCachedPose` | `pose_name` | Reference a cached pose |

---

**Example:**
```json
{
  "command": "read_blueprint",
  "params": {
    "blueprint_name": "BP_Player"
  }
}
```

### list_blueprints

List all Blueprint assets in the project, optionally filtered by path and name.

**Parameters:**
- `path` (string, optional) - Asset path to search in. Defaults to "/Game".
- `recursive` (bool, optional) - Whether to search subdirectories. Defaults to true.
- `name_filter` (string, optional) - Substring filter for Blueprint names.

**Returns:**
- `count` - Number of Blueprints found
- `blueprints` - List of Blueprint summaries with name, path, and parent class

**Example:**
```json
{
  "command": "list_blueprints",
  "params": {
    "path": "/Game/Blueprints",
    "recursive": true,
    "name_filter": "BP_"
  }
}
```

## Implementation Notes

- All transform arrays (location, rotation, scale) must contain exactly 3 float values
- Empty lists for transform parameters will be automatically converted to default values
- The server maintains detailed logging of all operations
- All commands require a successful connection to the Unreal Engine editor
- Failed operations will return detailed error messages in the response
- Component types should be specified without the 'U' prefix (e.g., "StaticMeshComponent" instead of "UStaticMeshComponent")
- For socket-based communication, refer to the test scripts in unreal-mcp/Python/scripts/blueprints for examples
