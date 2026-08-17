# Unreal MCP Actor Tools

This document provides detailed information about the actor tools available in the Unreal MCP integration.

## Overview

Actor tools allow you to manipulate actors in the Unreal Engine scene.

## Actor Tools

### get_actors_in_level

Get a list of all actors in the current level.

**Parameters:**
- None

**Returns:**
- List of all actors with their properties

**Example:**
```json
{
  "command": "get_actors_in_level",
  "params": {}
}
```

### find_actors_by_name

Find actors in the current level by name pattern.

**Parameters:**
- `pattern` (string) - The name or partial name pattern to search for

**Returns:**
- List of matching actor names

**Example:**
```json
{
  "command": "find_actors_by_name",
  "params": {
    "pattern": "Cube"
  }
}
```

### spawn_actor

Create a new primitive/light/camera actor in the current level.

**Parameters:**
- `name` (string) - The name for the new actor (must be unique)
- `type` (string) - `StaticMeshActor`, `PointLight`, `SpotLight`, `DirectionalLight`, or `CameraActor` (case/underscores/spaces are normalized)
- `location` (array, optional) - [X, Y, Z] coordinates, defaults to [0, 0, 0]
- `rotation` (array, optional) - [Pitch, Yaw, Roll] values, defaults to [0, 0, 0]

**Returns:**
- Information about the created actor

**Example:**
```json
{
  "command": "spawn_actor",
  "params": {
    "name": "MyCube",
    "type": "StaticMeshActor",
    "location": [0, 0, 100],
    "rotation": [0, 45, 0]
  }
}
```

### delete_actor

Delete an actor by name.

**Parameters:**
- `name` (string) - The name of the actor to delete

**Returns:**
- Result of the delete operation

**Example:**
```json
{
  "command": "delete_actor",
  "params": {
    "name": "MyCube"
  }
}
```

### set_actor_transform

Set the transform (location, rotation, scale) of an actor.

**Parameters:**
- `name` (string) - The name of the actor to modify
- `location` (array, optional) - [X, Y, Z] coordinates for the actor's position
- `rotation` (array, optional) - [Pitch, Yaw, Roll] values for the actor's rotation
- `scale` (array, optional) - [X, Y, Z] values for the actor's scale

**Returns:**
- Result of the transform operation

**Example:**
```json
{
  "command": "set_actor_transform",
  "params": {
    "name": "MyCube",
    "location": [100, 200, 300],
    "rotation": [0, 90, 0]
  }
}
```

### get_actor_properties

Get all properties of an actor.

**Parameters:**
- `name` (string) - The name of the actor

**Returns:**
- Object containing all actor properties

**Example:**
```json
{
  "command": "get_actor_properties",
  "params": {
    "name": "MyCube"
  }
}
```

### set_actor_property

Set a single property on an actor via reflection.

**Parameters:**
- `name` (string) - The name of the actor
- `property_name` (string) - The property to set
- `property_value` - The new value (type depends on the property)

**Example:**
```json
{
  "command": "set_actor_property",
  "params": {
    "name": "MyPointLight",
    "property_name": "Intensity",
    "property_value": 5000
  }
}
```

### spawn_blueprint_actor

Spawn an actor instance from a Blueprint asset.

**Parameters:**
- `blueprint_name` (string) - Name or path of the Blueprint to spawn
- `actor_name` (string) - Unique name for the spawned actor
- `location` (array, optional) - [X, Y, Z] coordinates, defaults to [0, 0, 0]
- `rotation` (array, optional) - [Pitch, Yaw, Roll] values, defaults to [0, 0, 0]

**Example:**
```json
{
  "command": "spawn_blueprint_actor",
  "params": {
    "blueprint_name": "BP_Enemy",
    "actor_name": "Enemy_1",
    "location": [500, 0, 100]
  }
}
```

## Error Handling

All command responses include a "success" field indicating whether the operation succeeded, and an optional "message" field with details in case of failure.

```json
{
  "success": false,
  "message": "Actor 'MyCube' not found in the current level"
}
```

## Implementation Notes

- All numeric parameters for transforms (location, rotation, scale) must be provided as lists of 3 float values
- Actor identifiers accept either internal object names or World Outliner labels where documented.
- The server maintains logging of all operations with detailed information and error messages
- All commands are executed through a connection to the Unreal Engine editor

## Type Reference

### Actor Types

Supported actor types for `spawn_actor`:

- `StaticMeshActor`
- `PointLight`
- `SpotLight`
- `DirectionalLight`
- `CameraActor`

`spawn_actor` creates the actor class; use `set_actor_property` or component-specific tools to assign meshes, light settings, and other properties afterward.