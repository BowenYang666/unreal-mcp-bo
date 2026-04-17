# Unreal MCP Project Tools

## Overview

Project tools provide cross-cutting functionality that isn't tied to a specific asset type — input mappings, data asset reading, and class property discovery.

## Project Tools

### create_input_mapping

Create an input action or axis mapping.

**Parameters:**
- `mapping_name` (string) - Name of the mapping (e.g. "Jump", "MoveForward")
- `key` (string) - Key or axis to bind (e.g. "SpaceBar", "Gamepad_FaceButton_Bottom")
- `mapping_type` (string, optional) - "Action" or "Axis" (default: "Action")
- `scale` (float, optional) - Scale value for axis mappings (default: 1.0)

### read_data_asset

Read all properties from a DataAsset (or any UObject asset) via Unreal reflection.

**Parameters:**
- `asset_path` (string) - Asset path, e.g. "/Game/Data/TowerConfig/DA_HoneyBarrel"

**Returns:**
- Dict with `asset_name`, `asset_path`, `class_name`, and `properties` object containing all BlueprintVisible UPROPERTY fields.

**Example:**
```json
{
  "command": "read_data_asset",
  "params": {
    "asset_path": "/Game/Data/EnemyConfig/DA_Goblin"
  }
}
```

### get_class_properties

Discover all editable properties of any UClass or loaded asset. Essential for understanding what properties are available before setting them.

Provide either `class_name` or `asset_path`:
- `class_name` returns property metadata only (name, type, category, tooltip)
- `asset_path` loads the asset and also returns current values

**Parameters:**
- `class_name` (string, optional) - UClass name, e.g. "BlendSpace1D", "StaticMeshActor", "PlayerController"
- `asset_path` (string, optional) - Asset path to load and inspect, e.g. "/Game/Player/Animations/BS_Locomotion"
- `category` (string, optional) - Filter to only return properties in this category (e.g. "Physics", "Rendering")

**Returns:**
- `class` - Class name
- `parent_class` - Parent class name
- `asset_path` - (only when asset_path was provided)
- `property_count` - Number of properties returned
- `properties` - Array of property objects, each containing:
  - `name` - Property name
  - `type` - Human-readable type string (e.g. "float", "enum (ECollisionChannel)", "struct (FVector)", "array (object (UStaticMesh))")
  - `category` - Editor category
  - `editable` - Whether the property is editable
  - `tooltip` - Editor tooltip text (if available)
  - `defined_in` - Which class in the hierarchy defines this property
  - `value` - Current value (only when asset_path was provided)

**Examples:**

Discover all properties of a class:
```json
{
  "command": "get_class_properties",
  "params": {
    "class_name": "BlendSpace1D"
  }
}
```

Inspect a specific asset with current values:
```json
{
  "command": "get_class_properties",
  "params": {
    "asset_path": "/Game/Player/Aimations/BS_MyLocomotion1d"
  }
}
```

Filter by category:
```json
{
  "command": "get_class_properties",
  "params": {
    "asset_path": "/Game/Player/Aimations/BS_MyLocomotion1d",
    "category": "InputInterpolation"
  }
}
```
