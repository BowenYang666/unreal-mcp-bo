# Niagara Tools

Tools for creating and editing Niagara particle systems programmatically.

Default grouped mode exposes `niagara_search`, `niagara_call_read` and
`niagara_call_write`. The operation names/parameters below are passed to these
entry points, or called directly with `MCP_TOOL_MODE=direct`.
See the [calling guide](README.md#grouped-mode-default).

## System Management

### `create_niagara_system`
Create an empty Niagara system asset.
- `asset_full_path` — Content path (e.g. `/Game/Effects/NS_MyFire`)
- `template_system_path` — optional source Niagara system to duplicate

### `list_niagara_systems`
List Niagara systems with optional `path`, `include_engine_content`, and `name_filter` filters.

### `read_niagara_system`
Read a Niagara system's emitters, module stacks, rapid iteration parameters, and renderer properties. Identify it with `asset_full_path`.

## Emitter Management

### `add_emitter_to_system`
Add an emitter to a system. Three modes:
- **Template**: specify `template_name` (e.g. `Fountain`, `Smoke`). Use `list_niagara_emitter_templates` to see available templates.
- **Duplicate**: specify `source_emitter_name` within the same system.
- **Cross-system copy**: specify `source_emitter_name` + `source_asset_full_path`.

Optional `new_emitter_name` to rename the emitter after adding.

Cross-system copying now creates an **independent snapshot**, not an inheritance link
to the source System's private embedded Emitter. The native add utility still performs
graph synchronization; the newly created version's parent/previous-merge parent are
removed with the engine API before compilation. The source's enabled state is retained.
Template addition and same-system duplication keep their existing behavior.

After compilation, the target package is checked for private source-package references
before saving. Residual references (including unsupported external scratch-pad cases)
return `success=false`, `stage=reference_preflight`, `modified=true`, `saved=false` and
`private_references`; they are not passed to the fatal save path. Invalid compilation
also prevents saving. Target user-parameter values are not automatically imported or
renamed: configure required bindings explicitly. This is not a full replacement for
every Niagara editor clipboard feature, and copies no longer inherit future changes
from the source embedded emitter.

The successful cross-system receipt reports `inheritance_mode=independent_snapshot`
and actual `success`/`modified`/`saved`. Failure can leave the added emitter in memory;
inspect it before retrying, and never treat a connection reset as a harmless network
retry. Python and Bridge preserve partial failure details. Same-system and template
paths have not acquired these additional receipt guarantees.

2026-10-03 regression: `UnrealMCP.Niagara.CrossSystemEmitterCopy` passed synthetic
embedded-inheritable tests, actual save and separate-process reload. SkillTest also
passed using an isolated copy of
`/Game/Features/Enemies/E2_cannonDog/VFX/Impact/NS_CannonImpact_Normal` and its `Refraction`
layer, preserving the layer's enabled/local-space settings. It copied into a fresh
default-system fixture, NOT either existing `NS_BomberImpact_Stylized` asset. Fixture:
`/Game/__Dev/EmitterCopy_82d6ac9cfe8c4fb489b1c0199f5ff598`. Target-native test logs:
`Saved/Logs/Emitter-Material-Deployment.log` and `Emitter-Material-ColdReload.log`.
No original explosion asset was changed or failing call replayed. The UE 5.7.4
view-model clipboard and some graph helpers are not exported to plugins; this uses
exported APIs without modifying the engine or making private objects public.

Both fixes were initially built and deployed to SkillTest (UE 5.7.4). Backup:
`SkillTest/Saved/UnrealMCP-before-emitter-material-20261003-091139`. All 43 plugin
source/descriptor files matched the repository; 111 protected asset/map/config hashes
and the 8879-file existing Content inventory were unchanged. Only four fixture assets
were added under the isolated roots above and in the material deletion test. A fresh
MCP session read the copied test system and previewed material deletion with no new
dirty packages. Python regression: 67 tests passed; grouped public tools remain 70.
Existing clients must reconnect for the new material operation. Source changes are
not committed automatically.

Subsequent NF deployment on 2026-10-03 was explicitly authorized. `NodeFallEditor`
built successfully in 120.01 seconds (exit 0). Only Niagara commands, Material commands,
the Material command header and Bridge were updated; NF's independent navigation/tests,
build rules and migration implementation were preserved. Backup:
`NodeFall/Saved/UnrealMCP-before-emitter-material-20261003-092737`.
All 2374 protected file hashes, including existing Content and remaining plugin files,
were unchanged; no Content files were added. This does not deploy the separate GoodSky
texture-migration fix to NF.

SkillTest was closed with explicit permission after live checks found no dirty packages
or active task; close returned `saved_count=0`. NF then launched on its normal port
13090 without changing client configuration. At verification it was PID 39876 on
`/Game/Dev/Enemies/Maps/L_cyberpunk_start1_dev`. A fresh session using NF's existing
Claude profile discovered 62 filtered public tools, read `M_CommonPBR_Rough`, previewed
deletion with `modified=false`/`saved=false` and all 12 nodes unchanged, and read
`BP_NFPlayerController`; dirty packages stayed empty. Emitter copying was not executed
on NF assets: its behavior was verified in the prior isolated SkillTest tests.
Claude CLI still reported Pending approval; the user must approve the project server
in Claude. Fresh stdio verification does not replace client trust approval.

### `remove_emitter_from_system`
Remove an emitter by name from a system.

### `list_niagara_emitter_templates`
List all available engine emitter templates (Fountain, Smoke, etc.).

## Module Management

### `add_module_to_emitter`
Add a Niagara module script to an emitter's stack (e.g. adding GravityForce to Particle Update).

### `remove_module_from_emitter`
Remove a module from an emitter's stack. Automatically bridges pin connections to prevent stack corruption.

## Parameter Editing

### `set_niagara_rapid_parameter`
Set a rapid iteration parameter on an emitter. Key parameters:
- `asset_full_path` — identify the Niagara system
- `emitter_name` — which emitter
- `parameter_name` — full or partial rapid parameter name (e.g. `InitializeParticle.Lifetime Min`)
- `value` — new value (scalar, vector, color)
- `script_type` — required: `"spawn"`, `"update"`, `"emitter_spawn"`, or `"emitter_update"`. It must match the module's stack stage.

### `set_niagara_parameter`
Set a runtime Niagara component parameter on a placed actor. Parameters: `actor_name`, `parameter_name`, `parameter_type`, `value`.

### `get_niagara_parameters`
Get all exposed parameter values from a Niagara component on a placed actor (`actor_name`).

### `modify_emitter_properties`
Modify emitter-level properties (sim target, determinism, local space, etc.).

## Module Input Inspection & Editing

### `list_module_inputs`
List a module's input pins with their type and current value mode. Discovery step before editing inputs.
- `asset_full_path`, `emitter_name`, `module_name`, `script_type` (`"spawn"`/`"update"`/`"emitter_spawn"`/`"emitter_update"`)

Each input reports `name`, `type`, `is_static`, `is_hidden`, `can_enable_local`, `can_bind_datainterface`, `rapid_parameter_name`, and `current_mode`:
- `"Default"` — not exposed / using the module default.
- `"Local"` — has a local (rapid-iteration) value, returned in `value`.
- `"DynamicInput"` — driven by a dynamic input sub-function; a `dynamic_input` object reports its `name` (script), `node`, and local `values` (e.g. `RandomRangeVector` → `Minimum`/`Maximum`).
- `"Linked"` — bound to another parameter (`linked_parameter`).
- `"Expression"` — driven by a custom HLSL expression.

### `enable_module_input`
Expose a module input as a Local Value (creates a rapid-iteration parameter so `set_niagara_rapid_parameter` can write it). Optional `initial_value`. Constant types only (not data interfaces).

### `list_module_static_switches`
List a module's static switch pins and their current values (e.g. `Unset` / `Direct Set` / `Random`).

### `set_module_static_switch`
Set a module static switch by name. Accepts display name, raw name, or numeric index.

### `bind_module_input_datainterface`
Bind a data-interface-typed module input (e.g. a Sprite/Mesh Renderer info, Curve, or Static Mesh sampler) to an asset or renderer.

### `set_module_dynamic_input`
Attach a dynamic input (e.g. `FloatFromCurve`, `VectorFromCurve`, `RandomRangeVector`) to a module input so it's driven by a sub-function. After attaching a `...FromCurve`, call `set_ns_curve_keys` on the same input to author its curve. Only fresh inputs are supported (replacing an existing override isn't yet).

## Curves

### `read_ns_curve`
Read a curve on a module input (multi-channel supported, e.g. a Color curve's R/G/B/A). Dives through a dynamic input when present.

### `set_ns_curve_keys`
Author a curve's keys on a module input. Works on direct curve data interfaces and on `...FromCurve` dynamic inputs.

## Renderer Management

### `set_niagara_renderer_property`
Set a property on an emitter's renderer via reflection (e.g. `Material`, `SubImageSize`, `Alignment`, `FacingMode`). Select the renderer by `renderer_type` (class-name substring) or `renderer_index`.

### `add_renderer_to_emitter`
Add a renderer to an emitter. `renderer_type` is one of `Sprite`, `Mesh`, `Ribbon`, `Light`, `Decal`, `Component`. Returns `renderer_index` and `renderer_class_name`.

### `remove_renderer_from_emitter`
Remove a renderer from an emitter by `renderer_index`. Use `read_niagara_system` to see indices.

### `list_renderer_types`
List the renderer type strings accepted by `add_renderer_to_emitter`.

### `set_mesh_renderer_mesh`
Assign a static mesh and/or override material to a Mesh renderer. Provide at least one of:
- `static_mesh_path` → fills `Meshes[mesh_slot]` (optional per-slot `scale`).
- `override_material_path` → fills `OverrideMaterials[material_slot]` and enables `bOverrideMaterials`.

Selects the Mesh renderer by `renderer_index`, or the first Mesh renderer when omitted. A Mesh renderer added by `add_renderer_to_emitter` has an empty `Meshes[]` and renders nothing until a mesh is assigned.
