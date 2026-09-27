# Unreal MCP Project Tools

## Overview

Project tools provide cross-cutting functionality that isn't tied to a specific asset type — input mappings, data asset reading, and class property discovery.

## Project Tools

### create_input_mapping

Create an input action or axis mapping.

**Parameters:**
- `action_name` (string) - Name of the mapping (e.g. "Jump", "MoveForward")
- `key` (string) - Key or axis to bind (e.g. "SpaceBar", "Gamepad_FaceButton_Bottom")
- `input_type` (string, optional) - `"Action"` or `"Axis"` (default: `"Action"`)

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

Discover reflected properties of any resolved UClass or loaded UObject asset. Transient/deprecated fields are skipped.

Provide either `class_name` or `asset_path`:
- `class_name` returns property metadata plus class-default-object (CDO) values
- `asset_path` loads the asset and also returns current values

**Parameters:**
- `class_name` (string, optional) - UClass name, e.g. "BlendSpace1D", "StaticMeshActor", "PlayerController"
- `asset_path` (string, optional) - Asset path to load and inspect, e.g. "/Game/Player/Animations/BS_Locomotion"
- `category` (string, optional) - Filter to only return properties in this category (e.g. "Physics", "Rendering")
- `structured` (boolean, default false) - Opt into the DataAsset write contract described below. Requires an existing `/Game/...` DataAsset instance, not `class_name`.
- `property_paths` (array of strings, optional) - Exact structured paths, e.g. `["Stats.Health", "Attacks[0]"]`; requires `structured=true`. At most 64 selections. Omit to inspect top-level editable fields. When provided, this selection takes precedence over `category`.

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
    "asset_path": "/Game/Player/Animations/BS_MyLocomotion1d"
  }
}
```

Filter by category:
```json
{
  "command": "get_class_properties",
  "params": {
    "asset_path": "/Game/Player/Animations/BS_MyLocomotion1d",
    "category": "InputInterpolation"
  }
}
```

## AI / Asset Reading

### Structured Asset Inspection

Legacy `get_class_properties` output is unchanged unless `structured=true`.
Structured output contains `asset_path`, `class_path`, `package_dirty` and
`properties` entries with `path`, `writable`, typed `value`, and a `schema` with
C++ type, reference class constraints, enum choices and numeric metadata.
Unsupported/noneditable values have `writable=false` and a `reason`. A writable
field is not a promise that every proposed value is valid or that saving will
succeed. `EditDefaultsOnly` fields are allowed on these DataAsset assets; this
does not enable editing Actor instances or class-default objects. The schema
reports `edit_defaults_only`. Additional reference-picker constraints remain
conservatively refused, and the implementation respects `CanEditChange`.

`EditCondition` supports a sibling boolean property or its negation, such as
`bEnabled` or `!bEnabled`, resolved in the containing UObject/struct, including
array-element structs. Parent conditions apply to nested paths too. False or
unsupported conditions report `writable=false` with a reason, while retaining
the typed value/schema when otherwise readable. Complex expressions (comparisons,
logical combinations, function calls and dotted references) fail closed; this
is not a full implementation of the UE Details expression language.

```json
{
  "asset_path": "/Game/Data/DA_Enemy",
  "structured": true,
  "property_paths": ["Stats.Health", "AttackMontage"]
}
```

This opt-in contract supports DataAsset instances and the allowlisted fields of
exact `UPhysicalMaterial` assets. It is not an arbitrary UObject writer.
This reader does not follow object references or mutate the target. Bounds:
8 levels, 1024 elements per array/map, 8192 visited values per request. Select a
narrower path when a large value cannot be represented. Legacy reflection reads
remain available for other asset classes and unsupported DataAsset fields.

### set_asset_properties

Patch one existing `UDataAsset`/`UPrimaryDataAsset` instance, including instances
of Blueprint-derived DataAsset classes, or allowlisted `UPhysicalMaterial` fields.
This is a direct **Asset-category** tool
in both MCP modes (`MCP_ASSET_ENABLED`), not part of the Project category. It is
removed in read-only mode. Requires the updated C++ plugin; restart alone does
not add the native command to an older deployed DLL.

Parameters: `asset_path: string`, `changes: array of objects`, `save: boolean=true`.
Use a full `/Game/...` package path, not a Windows path, object suffix, class or CDO.
Each change has `path`, `value`, and optional `expected_value`; unknown keys and
overlapping paths are rejected. Exact reflected field names are case-sensitive.

```json
{
  "asset_path": "/Game/Data/DA_Enemy",
  "changes": [
    {"path": "Stats.Health", "value": 150, "expected_value": 100},
    {"path": "Attacks[0].Damage", "value": 25},
    {"path": "AttackMontage", "value": "/Game/Animations/AM_Attack.AM_Attack"}
  ],
  "save": true
}
```

Supported values and boundaries:

- Boolean, numeric, string, name, plain text, declared enum names, ordinary
  reflected structs, arrays, hard/soft object or class references and GameplayTags.
  Numeric values must be finite and obey the property's storage range and
  `ClampMin`/`ClampMax`; integers are restricted to JSON's safe range
  `[-9007199254740991, 9007199254740991]`. No silent integer truncation.
- A struct value replaces all its fields and must supply every field. Use a
  dotted path for partial edits. An array value replaces the array; `[index]`
  edits an existing element. No append/remove operations in v1. No traversal
  through object references; editing a reference never edits the referenced asset.
- Maps use a complete array of `{"key": typed_key, "value": typed_value}` entries.
  `[]` clears the map; single-key property paths are not supported. Hashable enum,
  integer, name and string keys are supported; duplicate typed keys are rejected.
  `expected_value` compares typed map contents independent of entry order.
  Whole struct values include all inherited reflected fields, not only fields
  declared in the derived struct. Schema uses `format="map_entries"`, `key` and
  `value`; enum choices include stable names and separate `enum_display_names`.
- Reference strings must resolve to compatible assets/classes, including soft
  references. Class references use `/Script/Module.Class` or a Blueprint generated
  class path such as `/Game/Data/BP_Config.BP_Config_C`, not the Blueprint asset.
  `null` explicitly clears nullable references; empty string is not a clear token.
- GameplayTag uses a registered tag name (empty string clears); TagContainer
  uses an array of registered names (empty array clears). No tag registration.
- FText writes use plain strings and do not preserve localization identity.
  Sets, instanced objects, delegates, static C arrays, unsupported conditions and
  unsupported nested structures are rejected. Texture/Mesh/Material/Blueprint
  graph editing, MI Parent and class reparenting are excluded. Creation uses the
  separate bounded tools below.
- 1..64 patches; Python JSON request limit 256 KiB; reflected traversal bounds
  are the same as the structured reader. Underlying native TCP accepts at most
  1 MiB per request; this is not an unlimited bulk asset API.

All changes and expected values are converted/validated in temporary property
storage before touching the target. A mismatch returns stage `conflict`, with
`modified=false` and `saved=false`. This is not an asset-wide revision lock.
Supported edit conditions are checked against the complete proposed batch after
conversion, independent of patch order. For example, if enabling a feature is
explicitly authorized, both `BulletHits.bEnabled=true` and
`BulletHits.HeadRootBones=["head"]` may be submitted together. Without that
explicit enabling change, a false condition rejects the write with stage
`edit_condition`; the tool never toggles a controller automatically. A batch
that disables the condition and edits its guarded value is also rejected.
Whole-struct/array/map replacement checks changed descendants, so it cannot bypass
these rules; unchanged guarded values can be retained while disabling a feature.
An entirely new struct-array element has no previous values and checks all its
guarded fields. Read-time writability reflects current state, not every possible
future batch; use exact child paths to inspect or patch controllers separately.
The game-thread edit uses a transaction and root-property editor notifications;
project-specific edit hooks can change actual values, which are read back into
the response. No claim of rollback across arbitrary project hooks is made.

With `save=true`, a pre-existing dirty target package is rejected before writes,
so unrelated user edits are not silently saved. With `save=false`, edits remain
in memory; use existing `save_asset` explicitly when ready (it saves the package,
including all of its existing edits). Only the target is automatically saved.

Result: `success`, `modified`, `saved`, `asset_path`, `package_dirty`, and `changes`
with typed `before`/`after` values. Validation failure leaves the target unchanged.
Save/readback failure may leave modified state; inspect `stage` and `error` or
the Python `message`. Save success is checked against the save API, dirty flag
and package file existence. This is not a forced package reload inside the call.
A transport timeout is **indeterminate**: do not blindly retry a mutation.

Acceptance in the source test project covers C++ and Blueprint DataAsset
instances, batch prevalidation, conflict/type/range rejection, references/tags,
dirty protection, save and fresh-process reload. Test assets live under
`/Game/__Dev/AssetProperties_*`. Native tests can run with `-UnrealMCPNoServer`
to avoid competing with another editor's MCP listener. These are not a claim
that SkillTest or NF already has the new plugin.

### create_data_asset

Create a new instance of an existing concrete DataAsset-derived class. Parameters:
`asset_path: string`, `class_path: string`, `save: boolean=true`. Destination must
be a new `/Game/...` package, including the asset name. Existing loaded, unsaved,
registered or on-disk packages are refused; no overwrite or automatic suffixing.
The class must derive from `UDataAsset` and cannot be abstract, deprecated or
superseded. This does not create a class or permit arbitrary UObject creation.

```python
create_data_asset(asset_path="/Game/Data/DA_Impact",
          class_path="/Script/NodeFall.NFWeaponImpactProfile")
get_class_properties(asset_path="/Game/Data/DA_Impact", structured=True,
           property_paths=["Default", "Surfaces"])
```

Native class paths use `/Script/Module.Class`; Blueprint-derived classes use
their generated class path, e.g. `/Game/Data/BP_Profile.BP_Profile_C`.
Creation copies class defaults; inspect then use `set_asset_properties` for
separately validated initialization. Returns `created`, `modified`, `saved`,
`package_dirty`, `asset_path`, `class_path` and `success`. Only the new package is
saved. A save failure can leave a created asset in memory; a timeout is unknown,
not permission to repeat creation or overwrite the destination.

For an enum-keyed impact map, copy the complete inspected `Default` structure
for each entry, modify the desired fields, then submit the complete entries as
`Surfaces`. Keep every inherited/nested field from the readback. Use JSON `null`
for disabled System, Sound or Decal.Material references, never a missing field.
For NF, `SurfaceType1` and `SurfaceType2` are stable enum names; Metal and Concrete
are project display labels, not accepted replacement identifiers.

### create_physical_material

Parameters: `asset_path: string`, `save: boolean=true`. Creates exactly
`UPhysicalMaterial`, using engine defaults and the same no-overwrite/save rules
as `create_data_asset`. Both creation tools belong to the Asset category and are
unavailable in read-only mode.

```python
create_physical_material(asset_path="/Game/Physics/PM_Metal")
get_class_properties(asset_path="/Game/Physics/PM_Metal", structured=True,
           property_paths=["SurfaceType", "Friction"])
set_asset_properties(asset_path="/Game/Physics/PM_Metal", changes=[
  {"path": "SurfaceType", "value": "SurfaceType1",
   "expected_value": "SurfaceType_Default"}])
```

The exact editable whitelist is `SurfaceType`, `Friction`, `StaticFriction`,
`Restitution`, `Density`, `FrictionCombineMode`, `RestitutionCombineMode`,
`bOverrideFrictionCombineMode`, `bOverrideRestitutionCombineMode`. Omitted fields
are preserved. Numeric and edit-condition constraints still apply. This does
not rebuild mass/inertia or edit mesh BodySetup/collision-complexity settings.

Assign the asset through [Material/MI assignment](material_tools.md#set_material_physical_material),
[level component overrides](editor_tools.md#set_component_physical_material) or
the existing [Blueprint setter](blueprint_tools.md#set_component_property).
Validate collision with [trace_physical_material](editor_tools.md#trace_physical_material).
Deferred decals use existing `create_material(material_domain="DeferredDecal",
blend_mode="Translucent")`, not a separate bullet-decal tool.

The source project's `UnrealMCP.SurfaceFeedback.EndToEnd` test creates isolated
assets and uses real simple/complex collision queries. Supply a unique
`-MCPSurfaceTestRoot=/Game/__Dev/SurfaceFeedback_<unique>`; a second process with
the same root and `-MCPSurfaceReload` verifies saved references and surface types
without writing. Both runs use `-UnrealMCPNoServer`. The optional
`-MCPImpactProfileClass=/Script/NodeFall.NFWeaponImpactProfile` requires that actual
class and the matching plugin in the target project. Fixture coverage in this
repository is not acceptance of the real NF class or authorization to deploy.

### read_behavior_tree

Read the full structure of a Behavior Tree asset (root, composite/decorator/service/task nodes, hierarchy).

**Parameters:**
- `asset_path` (string) - Behavior Tree asset path, e.g. `/Game/AI/BT_Enemy`

### read_blackboard

Read all keys from a Blackboard data asset (key names, types, parent blackboard).

**Parameters:**
- `asset_path` (string) - Blackboard asset path, e.g. `/Game/AI/BB_Enemy`

### read_state_tree

Read the full structure of a StateTree asset (states, tasks, transitions, evaluators).

**Parameters:**
- `asset_path` (string) - StateTree asset path, e.g. `/Game/AI/ST_Enemy`
