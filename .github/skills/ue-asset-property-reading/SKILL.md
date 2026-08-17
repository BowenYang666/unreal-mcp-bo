---
name: ue-asset-property-reading
description: "Use this skill when reading, inspecting, or querying reflected properties and values of Unreal Engine UObject assets (.uasset). Covers AnimSequence, AnimMontage, BlendSpace, SkeletalMesh, StaticMesh, Material, DataAsset, and similar loadable assets. Use get_class_properties with asset_path for asset instance values. Read this BEFORE claiming an asset cannot be inspected."
metadata:
  version: 1.1.0
---

# Reading Reflected UE Asset Properties and Values

## `get_class_properties` — Two Modes

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
