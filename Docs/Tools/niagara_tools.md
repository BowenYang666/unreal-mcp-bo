# Niagara Tools

Tools for creating and editing Niagara particle systems programmatically.

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
