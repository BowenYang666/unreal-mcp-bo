# Niagara Tools

Tools for creating and editing Niagara particle systems programmatically.

## System Management

### `create_niagara_system`
Create an empty Niagara system asset.
- `asset_path` — Content path (e.g. `/Game/Effects/NS_MyFire`)

### `list_niagara_systems`
List all Niagara systems in the project. Supports optional path filter.

### `read_niagara_system`
Read the full structure of a Niagara system: emitters, modules, rapid iteration parameters. Use `name` or `path` to identify the system.

## Emitter Management

### `add_emitter_to_system`
Add an emitter to a system. Three modes:
- **Template**: specify `template_name` (e.g. `Fountain`, `Smoke`). Use `list_niagara_emitter_templates` to see available templates.
- **Duplicate**: specify `source_emitter_name` within the same system.
- **Cross-system copy**: specify `source_emitter_name` + `source_system_name`/`source_system_path`.

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
- `system_name` / `system_path` — identify the system
- `emitter_name` — which emitter
- `module_name` — module containing the parameter (e.g. `InitializeParticle`)
- `parameter_name` — parameter to set (e.g. `Lifetime Min`)
- `value` — new value (scalar, vector, color)
- `script_type` — `"spawn"` (default) or `"update"`. **Important**: modules in Particle Update (Gravity Force, Drag, Scale Color, Solve Forces) require `script_type: "update"`.

### `set_niagara_parameter`
Set a system/emitter-level parameter (non-rapid-iteration).

### `get_niagara_parameters`
Get all parameters for a Niagara component on an actor.

### `modify_emitter_properties`
Modify emitter-level properties (sim target, determinism, local space, etc.).
