---
name: ue-asset-property-reading
description: "Use this skill when reading, inspecting, or querying reflected properties and values of Unreal Engine UObject assets (.uasset). Covers AnimSequence, AnimMontage, BlendSpace, SkeletalMesh, StaticMesh, Material, DataAsset, and similar loadable assets. Use get_class_properties with asset_path for asset instance values. Read this BEFORE claiming an asset cannot be inspected."
metadata:
  version: 1.3.0
---

# Reading Reflected UE Asset Properties and Values

## `get_class_properties` — Target Selection

| Parameter | What it returns |
|-----------|----------------|
| `class_name="BlendSpace1D"` | Reflected property metadata **and class-default-object (CDO) values** |
| `asset_path="/Game/Path/To/Asset"` | Property definitions **AND current instance values** |

To read an asset's properties and values, ALWAYS use `asset_path`:

```
get_class_properties(asset_path="/Game/Player/Animations/MM_Rifle_Fire")
```

This works for loadable UObject-based assets whose data is exposed through reflected `FProperty` fields, including but not limited to:
- `AnimSequence` — animation clips
- `AnimMontage` — montage compositions
- `BlendSpace` / `BlendSpace1D` — blend spaces
- `SkeletalMesh` / `StaticMesh` — meshes
- `Material` / `MaterialInstance` — materials
- `Texture2D` — textures
- `SoundWave` / `SoundCue` — audio
- `DataAsset` / `PrimaryDataAsset` — custom data assets
- `NiagaraSystem` — particle systems
- Other loadable UObject assets with reflected properties

The tool skips transient/deprecated fields and cannot reconstruct data stored only in custom binary serialization or editor UI state. Specialized readers (`read_blueprint`, `read_material`, `read_niagara_system`, `read_state_tree`) remain preferable when graph/hierarchy semantics matter.

## Public Tools and Internal Operations

Use the current client's discovered tools, including its server prefix.
`get_class_properties`, `read_data_asset`, `read_blueprint`, `read_state_tree`
and `read_cascade_system` remain direct tools when their categories are enabled.
In default grouped mode, Material/Niagara/UMG/Scene readers are internal operations:

| Operation | Discover its exact contract | Execute |
|---|---|---|
| `read_material` | `material_search(tool="read_material")` | Returned `call_tool`, normally `material_call_read` |
| `read_niagara_system` | `niagara_search(tool="read_niagara_system")` | Returned `call_tool`, normally `niagara_call_read` |
| `read_widget_layout` | `umg_search(tool="read_widget_layout")` | Returned `call_tool`, normally `umg_call_read` |
| `get_editor_context` | `scene_search(tool="get_editor_context")` | Returned `call_tool`, normally `scene_call_read` |
| `inspect_scene_target` / `inspect_scene_asset` | `scene_search(tool=exact_operation)` | Returned `call_tool`, normally `scene_call_read` |

Pass `tool` and an `arguments` object matching `input_schema`; do not send `ctx`.
Reuse contracts rather than searching before every call. `MCP_TOOL_MODE=direct`
exposes the original operation names directly. Respect category/read-only
filters; missing tools do not authorize changing configuration or using raw TCP.

## Structured Asset Inspection

With a matching updated plugin, use this opt-in mode to inspect the write
contract of an existing `/Game/...` DataAsset/PrimaryDataAsset instance or the
allowlisted fields of an exact PhysicalMaterial asset:

```python
get_class_properties(asset_path="/Game/Data/DA_Enemy", structured=True,
                     property_paths=["Stats.Health", "Attacks[0]"])
```

Omit `property_paths` to inspect top-level editable fields. Results include typed
values, `writable`, restrictions and reference type constraints. Legacy reads
without `structured=True` remain the default for other asset classes and CDOs.
This mode does not create DataAssets or expose arbitrary UObject writes.

Maps use complete typed `[{"key": ..., "value": ...}]` entry arrays; `[]` clears
the map. Keys may be hashable enum, integer, name or string values. No single-key
path edits. Map schemas report `format="map_entries"`, `key` and `value`.
Whole struct values require every inherited and nested field. Preserve the
inspected structure; JSON null clears nullable references. Stable physical-surface
names such as `SurfaceType1` are accepted; project labels such as Metal are only
display names (`enum_display_names`). Expected map values ignore entry order.

PhysicalMaterial editing is limited to SurfaceType, friction/static friction,
restitution, density, combine modes and their override booleans. Material/MI
assignment instead uses the material operation `set_material_physical_material`;
level components use `set_component_physical_material`. The existing Blueprint
`set_component_property` handles PhysMaterialOverride on SCS components. Read
actual collision with `trace_physical_material`, checking the hit and asset path,
not only SurfaceType. No automatic collision-complexity or mass/inertia rebuild.

If editing is separately authorized, `set_asset_properties` uses these paths and
typed values; it belongs to the Asset category and is absent in read-only mode.
Its default `save=True` rejects already dirty target packages. A visible Python
schema does not prove the target DLL implements these operations; verify
deployment and report unavailable support, never silently fall back to raw edits.
Reading permission alone does not authorize writing, saving or plugin deployment.
Separately authorized creation uses `create_data_asset` with an existing concrete
DataAsset class, or `create_physical_material` for exactly PhysicalMaterial. Both
refuse existing destination packages, create class defaults, and report actual
created/modified/saved/package_dirty state. Inspect before initialization. On a
timeout, read back before retrying; do not infer rollback or persistence.

## Converting File Path to Asset Path

Users provide Windows file paths like:
```
D:\UnrealProjects\MyProject\Content\Player\Anims\MM_Fire.uasset
```

Convert to UE asset path by:
1. Find the `Content` folder in the path
2. Replace everything up to and including `Content` with `/Game`
3. Remove the `.uasset` extension

Result: `/Game/Player/Anims/MM_Fire`

### Examples

| File path | Asset path |
|-----------|------------|
| `D:\Projects\MyGame\Content\Characters\SK_Hero.uasset` | `/Game/Characters/SK_Hero` |
| `D:\Projects\MyGame\Content\UI\Textures\T_Icon.uasset` | `/Game/UI/Textures/T_Icon` |
| `D:\Projects\MyGame\Content\Audio\SFX\S_Gunshot.uasset` | `/Game/Audio/SFX/S_Gunshot` |

## Optional: Filter by Category

If you only need properties from a specific category (e.g. "AdditiveSettings", "Compression", "RootMotion"):

```
get_class_properties(asset_path="/Game/Player/Anims/MM_Fire", category="AdditiveSettings")
```

## Tool Comparison: When to Use What

| Tool | Use when |
|------|----------|
| `get_class_properties(asset_path=...)` | Reading reflected properties and current values from a loadable UObject asset |
| `get_class_properties(class_name=...)` | Discovering reflected properties plus native/Blueprint CDO defaults |
| `get_actor_properties` | Reading properties of an Actor placed in a level |
| `read_blueprint` | Reading Blueprint structure (components, variables, graphs, class defaults) |
| `read_data_asset` | Reading DataAsset properties (also works but `get_class_properties` is more comprehensive) |
| `read_material` | Reading Material graph nodes and connections (specialized) |
| `read_niagara_system` | Reading Niagara system structure (specialized) |

## Key Rule

Before saying an asset cannot be inspected, try `get_class_properties(asset_path=...)`. If it fails or omits domain-specific data, report that concrete limitation and use the appropriate specialized reader when available.
