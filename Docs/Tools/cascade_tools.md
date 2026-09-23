# Cascade Reader

## read_cascade_system

Read a legacy `UParticleSystem` directly, without converting it to Niagara.
This is the only Cascade-specific MCP tool. Discover paths with the Content
Browser or filesystem tools; filenames alone do not establish asset type.

```python
read_cascade_system(
    asset_path="/Game/ParagonWraith/FX/Particles/Abilities/Drone/FX/P_Wraith_Drone_Targeting",
    emitter_index=-1,
    lod_index=-1)
```

- `asset_path`: full asset path (a matching `.AssetName` object suffix is accepted).
  Folders and subobject paths are not supported. Niagara and other classes fail explicitly.
- `emitter_index`: zero-based emitter array index, or `-1` for all (default).
- `lod_index`: zero-based LOD array index, or `-1` for all (default).
  When selecting all emitters, the requested LOD must exist on every selected
  emitter; otherwise the request fails rather than silently skipping emitters.

The tool is available in read-only mode. `MCP_CASCADE_ENABLED=0` disables it;
the category is independent of `MCP_PROJECT_ENABLED` and `MCP_NIAGARA_ENABLED`.

## Response

- `properties`: authored system properties including LOD distances and warmup.
- `emitters`: original indices/names, emitter properties and selected `lods`.
- Each LOD has `index`, `level`, `enabled`, `required_module`, `spawn_module`,
  `type_data_module`, `event_generator`, and the original ordered `modules` array.
  Null type data is normal for a sprite emitter. Do not infer render type from
  `ParticleSpriteEmitter` alone; mesh/beam/etc. are specified by type data.
- `objects`: inline module/distribution objects keyed by object path. Resolve any
  `{"object_ref": "..."}` through this table. Shared modules retain the same ID
  across LODs and slots, so callers must not count those references as extra modules.
- `referenced_assets`: external material, mesh and other references. External
  assets are not recursively decoded. Use the existing material/property readers.
- `complete`, `warnings`: whether the selected reflected traversal hit unsupported
  fields, custom classes or safety limits. `complete=true` is NOT a claim about
  runtime behavior or custom binary serialization.
- `package_dirty_before_read` / `package_dirty_after_read`: dirty state after
  loading and after traversal. For a full side-effect check, also call
  `get_unsaved_changes` before and after the tool to include asset-loading effects.

Properties retain native names. Numbers and bitfield booleans are typed; enums
use names. Structs have a `__struct` tag. Distribution objects are NOT evaluated:
constant values, random Min/Max ranges, parameter mapping and curve keys are read
from their authored fields. Curve `Points` include `InVal`, `OutVal`,
`ArriveTangent`, `LeaveTangent`, and `InterpMode`. Integer values outside JSON's
exact double range are emitted as strings.

In particular, inspect `DynamicParams` for `ParamName`, `ValueMethod`,
`bUseEmitterTime`, `bSpawnTimeOnly`, and `ParamValue`. A time input is not always
normalized lifetime: retain the module's time flags and meaning. Event generators
expose `Events` (type, custom name, frequency); receiver modules retain their
event names and spawn settings. These are configurations, not inferred runtime
event traces or proof that matching names execute at a particular time.

## Boundaries

No simulation, compilation, conversion, module-list rebuilding, editing, dirty
flag clearing or saving is performed. Transient/deprecated properties, the
curve-editor UI setup and the embedded thumbnail image are excluded. The latter
two are explicitly listed in `excluded_editor_properties`. Runtime Blueprint overrides, simulated
particle state, cooked lookup-table-only values and custom binary payloads are
outside this reader's scope. Unsupported property types retain exported text
with a warning; custom classes are explicitly marked incomplete.

Safety limits: 24 traversal levels, 2,048 inline objects, 4,096 array elements,
100,000 property values and up to 100 warning messages. Limits produce explicit
`unavailable` markers and `complete=false`, never silent truncation.
Results above the existing 24,000-character MCP threshold spill to a local JSON
file. The response includes `file_path`, `preview`, and `overflow=true`.
Emitter/LOD selections use distinct spill filenames. Read the file or select
one emitter/LOD for smaller responses.

## Verification

From `Python`:

```text
uv run python -m unittest discover -s tests -v
```

UE automation test: `UnrealMCP.Cascade.ReadAuthoredData`. It uses only transient
fixtures to check curves/tangents, booleans, shared module IDs, dynamic parameters,
event configuration, selection failures, truncation warnings and dirty-state
preservation. Run through the editor's Automation UI or
`-ExecCmds="Automation RunTests UnrealMCP.Cascade.ReadAuthoredData"`.

Live acceptance should read `P_Wraith_Drone_Targeting` and compare its 8 emitters,
4 LODs per emitter, mesh/material references and event/dynamic modules against
Cascade. Repeat with emitter/LOD filters, invalid paths/indices and a wrong asset
class. Check dirty packages and original asset hashes before/after reading.