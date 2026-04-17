---
name: ue-asset-property-reading
description: "Use this skill when reading, inspecting, or querying properties and values of any Unreal Engine asset file (.uasset). Covers AnimSequence, AnimMontage, BlendSpace, SkeletalMesh, StaticMesh, Material, DataAsset, and any other UObject-based asset. Use get_class_properties with asset_path to read instance values. Read this BEFORE telling the user you cannot read asset properties."
metadata:
  version: 1.0.0
---

# Reading Any UE Asset's Properties and Values

## `get_class_properties` — Two Modes

| Parameter | What it returns |
|-----------|----------------|
| `class_name="BlendSpace1D"` | Property definitions only (name, type, category) — **no values** |
| `asset_path="/Game/Path/To/Asset"` | Property definitions **AND current instance values** |

To read an asset's properties and values, ALWAYS use `asset_path`:

```
get_class_properties(asset_path="/Game/Player/Animations/MM_Rifle_Fire")
```

This works for **ANY** UObject-based asset, including but not limited to:
- `AnimSequence` — animation clips
- `AnimMontage` — montage compositions
- `BlendSpace` / `BlendSpace1D` — blend spaces
- `SkeletalMesh` / `StaticMesh` — meshes
- `Material` / `MaterialInstance` — materials
- `Texture2D` — textures
- `SoundWave` / `SoundCue` — audio
- `DataAsset` / `PrimaryDataAsset` — custom data assets
- `NiagaraSystem` — particle systems
- Any other asset that exists as a `.uasset` file

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
| `get_class_properties(asset_path=...)` | Reading ANY asset's properties and current values |
| `get_class_properties(class_name=...)` | Discovering what properties a class has (no instance needed) |
| `get_actor_properties` | Reading properties of an Actor placed in a level |
| `read_blueprint` | Reading Blueprint structure (components, variables, graphs, class defaults) |
| `read_data_asset` | Reading DataAsset properties (also works but `get_class_properties` is more comprehensive) |
| `read_material` | Reading Material graph nodes and connections (specialized) |
| `read_niagara_system` | Reading Niagara system structure (specialized) |

## Key Rule

**NEVER tell the user "MCP can't read this asset's values."** If it's a `.uasset` file, `get_class_properties(asset_path=...)` can read it.
