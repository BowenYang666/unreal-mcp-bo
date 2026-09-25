# Material Tools

Tools for creating and editing Material graphs and Material Instances programmatically.

The material category contains 15 tools. `MCP_MATERIAL_ENABLED=0` disables them;
read-only mode retains `list_materials`, `read_material` and
`get_material_instance_parameters`. `save_asset` belongs to the editor category.
MCP callers do not supply the injected Python `ctx` parameter.

Use full asset paths when resources have duplicate names. Graph tools operate
on a Material, not a Material Instance. Use `set_material_instance_parameters`
for an existing MI's Parent or overrides; there is no general asset-property setter.

## Parameters At A Glance

Parameters without a default are required. Options shown as `None` can be
omitted. The `name`/`path` readers require at least one of those selectors.

| Tool | Parameters |
|---|---|
| `list_materials` | `path="/Game"`, `recursive=True`, `name_filter=""`, `type="all"` |
| `read_material` | `name=""`, `path=""` |
| `get_material_instance_parameters` | `name=""`, `path=""` |
| `create_material` | `asset_path`, `blend_mode="Opaque"`, `shading_model="DefaultLit"`, `two_sided=False`, `material_domain="Surface"` |
| `create_material_instance` | `asset_path`, `parent_material_path`, `scalar_params=None`, `vector_params=None`, `texture_params=None` |
| `set_material_instance_parameters` | `asset_path`, `scalar_params=None`, `vector_params=None`, `texture_params=None`, `parent_material_path=""` |
| `add_material_expression` | `asset_path`, `expression_type`, `pos_x=0`, `pos_y=0` |
| `set_material_expression_property` | `asset_path`, `node_index`, `property_name`, `value=None` (supply a value appropriate for the property) |
| `connect_material_expressions` | `asset_path`, `from_node_index`, `to_node_index`, `to_input_name`, `from_output_name=""` |
| `connect_material_to_property` | `asset_path`, `node_index`, `material_property`, `output_name=""` |
| `add_custom_hlsl_expression` | `asset_path`, `code`, `output_type="CMOT_Float3"`, `description="Custom"`, `inputs=None`, `pos_x=0`, `pos_y=0` |
| `set_material_property` | `asset_path`, `blend_mode=""`, `shading_model=""`, `two_sided=None`, `material_domain=""`, `opacity_mask_clip_value=None` |
| `add_material_comment` | `asset_path`, `text`, `pos_x=0`, `pos_y=0`, `size_x=400`, `size_y=300` |
| `set_expression_position` | `asset_path`, `node_index`, `pos_x`, `pos_y` |
| `reset_material_node_layout` | `asset_path`, `spacing_x=400`, `spacing_y=250` |

Node indices, positions, sizes and spacing are integers. Paths, names, pin names
and enum selections are strings. MI override objects are described below.

## Saving & Readback

| Operation | Persistence |
|---|---|
| Read/list tools | No explicit edit, compile or save |
| `create_material`, `create_material_instance` | Attempt to save the new package; current handlers do not check the save return value, so creation alone is not proof of persistence |
| `set_material_instance_parameters` | Saves only the target instance package and checks saving; inspect `success`, `saved`, `modified` and actual Parent |
| Graph/property/layout tools | Modify in memory and mark the material dirty; call `save_asset(asset_path=...)` after finishing |

Read before editing, serialize mutations to the same asset, and inspect the
result afterward. Do not run parallel node edits on the same material. Node
indices identify the current graph snapshot; re-read after structural changes
rather than assuming old indices still refer to the same node.
See [Editor error handling](editor_tools.md#error-handling) for response envelopes
and indeterminate transport failures. Do not retry creation blindly after a
timeout: the destination may already exist.

## Reading & Discovery

### `list_materials`
List Material and MaterialInstance assets. `type` accepts `material`, `instance`
or `all`; `name_filter` filters by a name substring.

```python
list_materials(path="/Game/ThirdParty/ArrowTrail", type="instance", name_filter="MI_Arrow")
```

### `read_material`
Read the full structure of a Material: domain, blend mode, shading model, all expression nodes (with positions, parameters, textures, input connections and inline defaults), the main material input pins (BaseColor, EmissiveColor, etc.), and comment boxes. Identify by `name` or `path`.

Prefer a full `path` over an ambiguous short `name`. Large results may return
`overflow=true`, `file_path` and `preview`; the full JSON is stored in that file.

Also returns a read-only **`compile_result`** from the material's current cached resource. `read_material` does not force a recompile or dirty the package. Many mutation tools trigger `PostEditChange`; inspect the resulting cached resource afterward:
```jsonc
"compile_result": {
  "available": true,
  "ok": true,          // a resource exists and its cached error list is empty
  "recompiled": false,
  "source": "cached_material_resource",
  "error_count": 0,
  "errors": []         // translation/compile error messages
}
```
`available=false` also produces `ok=false`. An empty cached error list does not
prove that a fresh asynchronous shader compile has completed. Inspect errors,
editor compilation status and the rendered result when validating a change;
this read does not wait for shaders or initiate compilation.

```python
read_material(path="/Game/ThirdParty/Trail/M_Master")
```

### `get_material_instance_parameters`
Read an MI's actual `parent` / `parent_name` and stored scalar/vector/texture
override arrays. This is not a complete list of inherited effective defaults or
a static-switch/layer-parameter editor. Prefer `path` over a short `name`.
Large responses use the same JSON spill mechanism as `read_material`.

```python
get_material_instance_parameters(path="/Game/ThirdParty/Trail/MI_Trail")
```

## Creating Assets

### `create_material`
Create a new Material at a package path including its asset name. An existing
destination is rejected, not overwritten.

- `blend_mode`: `Opaque`, `Masked`, `Translucent`, `Additive`, `Modulate`.
- Creation `shading_model`: `DefaultLit`, `Unlit`, `Subsurface`, `ClearCoat`.
- `material_domain`: `Surface`, `PostProcess`, `DeferredDecal`, `LightFunction`, `UI`.
- `two_sided`: boolean; defaults to `False`.

Unlike `set_material_property`, creation can leave defaults in place for
unrecognized enum strings; use the supported values and read back the result.

```python
create_material(asset_path="/Game/__Dev/Materials/M_Glow",
                blend_mode="Additive", shading_model="Unlit", two_sided=True)
```

### `create_material_instance`
Create a Material Instance Constant. Required: `asset_path` (new full package
path) and `parent_material_path` (full parent Material/MI path). Existing
destinations fail; use the update tool below for existing instances.
Optional overrides default to no overrides:
- `scalar_params` — `{"ParamName": value}`
- `vector_params` — `{"ParamName": {"r":1,"g":0,"b":0,"a":1}}`
- `texture_params` — `{"ParamName": "/Game/.../T_MyTex"}` (path tolerates a missing object suffix)

This is the vendor pattern: one master material + many instances that only swap the texture (and a scalar or two). No need to duplicate the graph per variant.

```python
create_material_instance(
  asset_path="/Game/__Dev/Materials/MI_Glow",
  parent_material_path="/Game/__Dev/Materials/M_Glow",
  scalar_params={"Intensity": 2.0},
  vector_params={"Tint": {"r": 0.2, "g": 0.8, "b": 1.0, "a": 1.0}})
```

Returns `name`, `path`, `parent` and stored override arrays. The creation helper
can silently skip texture paths that fail to load; inspect `texture_parameters`
rather than assuming every requested texture was applied. The existing-instance
update tool below validates these paths before mutation.

### `set_material_instance_parameters`
Override parameters on an **existing** Material Instance Constant (for iteration, no re-creation). Same `scalar_params` / `vector_params` / `texture_params` shapes as `create_material_instance`; only the parameters you pass are changed.

`scalar_params` maps names to finite numbers. `vector_params` maps names to
objects with required `r`, `g`, `b` and optional `a` (defaults to 1).
`texture_params` maps names to existing Texture asset paths. Omitted/empty
override maps do not clear stored overrides. An empty `parent_material_path`
keeps the Parent; it does not clear it.

Optional `parent_material_path` changes the Parent via Unreal's native editor API
before applying parameter overrides. Omit it or pass `""` to keep the Parent.
Use full paths for both the instance and the parent, not ambiguous short names.
The parent must be an existing Material or MaterialInstanceConstant; self,
descendant and cyclic parent chains are rejected. Calls during PIE are rejected.
All supplied overrides are validated before mutation, including texture asset
existence/type, numeric values and vector channels. Invalid texture paths are
errors for this update tool, not silently skipped.

```python
set_material_instance_parameters(
  asset_path="/Game/ThirdParty/Trail/MI_Trail",
  parent_material_path="/Game/ThirdParty/Trail/M_Master")
```

Existing overrides are not cleared or copied from the new parent. Their visual
effect still depends on matching parameters in the new parent; use
`get_material_instance_parameters` to inspect the actual Parent and stored
overrides afterward. This is not a guarantee of an unchanged rendered appearance.
Static switch/layer parameter editing is not added by this operation.

Returns the existing instance/override fields plus `success`, `saved`,
`previous_parent`, `parent_changed`, and `modified`. `parent` is the actual
current parent, not simply the requested path. Only the target instance's package
is saved (including any pre-existing unsaved edits to that instance), never Save
All. Parent and texture packages are not saved. Validation errors do not edit the
instance. If saving fails after the update, `success=false`, `saved=false`,
`modified=true` and readback fields describe the remaining in-memory state;
there is no automatic rollback. Transport errors/timeouts are indeterminate,
so inspect before retrying. `saved=true` does not assert shader compilation is
complete. The operation supports an editor undo transaction; undo itself is not
automatically saved.

Tests: `uv run python -m unittest discover -s tests -p test_material_instance_parameters.py -v`
from `Python`, and UE automation `UnrealMCP.Material.InstanceParent` (transient
fixtures, no project assets saved).

## Graph Editing

### `add_material_expression`
Use an expression class name with or without the `MaterialExpression` prefix.
Unknown classes fail. Keep the returned `node_index` for subsequent edits.

```python
add_material_expression(asset_path="/Game/__Dev/Materials/M_Glow",
                        expression_type="ScalarParameter", pos_x=-400, pos_y=0)
```

Add an expression node (e.g. `Multiply`, `TextureCoordinate`, `ScalarParameter`, `DynamicParameter`) to a material graph. Returns the node index.

### `set_material_expression_property`
Supply an actual `value` despite its Python default of `None`:

| Property kind | Example value |
|---|---|
| Numeric / boolean / string / enum | `0.5`, `True`, `"Tint"`, an enum member name |
| Linear color / vector | `{"r": 1, "g": 0, "b": 0, "a": 1}` / `{"x": 0, "y": 0, "z": 1}` |
| Object reference | Full asset path; hard references must load and match the expected class |
| String/name array | `ParamNames` takes `["Emissive", "Dissolve", "Param3", "Param4"]`; provide four names for DynamicParameter |
| Custom HLSL arrays | `Inputs` as names, `IncludeFilePaths` as strings, `AdditionalDefines` as name/value objects or `"NAME=VALUE"` strings, `AdditionalOutputs` as name/type objects |

Array writes replace the array, not a single element. Re-read pins after changing
Custom inputs or DynamicParameter output names. Unknown properties, unsupported
types, out-of-range node indices and incompatible hard references return errors.
Successful edits are not automatically saved.

```python
set_material_expression_property(asset_path="/Game/__Dev/Materials/M_Glow",
                                 node_index=0, property_name="ParameterName", value="Intensity")
```

Set a property on an expression node via reflection (scalars, vectors, enums, and array properties like a `DynamicParameter`'s `ParamNames`).

### `connect_material_expressions`
`from_output_name=""` selects the default output; an explicit numeric output
index must be a string, e.g. `"0"`. `to_input_name` is required. Get pin names
and node indices from `read_material`; invalid nodes or pins are errors.

Connect one expression's output to another expression's input. Output is resolved by name or index via the node's virtual `GetOutputs()`; input by name.

### `connect_material_to_property`
`output_name=""` selects the default output. Supported `material_property`
names include `BaseColor`, `Metallic`, `Specular`, `Roughness`, `Anisotropy`,
`Normal`, `Tangent`, `EmissiveColor`, `Opacity`, `OpacityMask`,
`WorldPositionOffset`, `SubsurfaceColor`, `AmbientOcclusion`, `Refraction`,
and `PixelDepthOffset`. Whether an input affects rendering depends on the
material's domain, blend mode and shading model.

Connect an expression output to a main material property pin (e.g. `EmissiveColor`, `BaseColor`, `Normal`, `Opacity`).

### `add_custom_hlsl_expression`
Output types include `CMOT_Float1`, `CMOT_Float2`, `CMOT_Float3`, `CMOT_Float4`,
and `CMOT_MaterialAttributes`. Returns the new `node_index` and node details.
`inputs` declares pin names; wire them separately with `connect_material_expressions`.

```python
add_custom_hlsl_expression(asset_path="/Game/__Dev/Materials/M_Glow",
                           code="return saturate(Value);", output_type="CMOT_Float1",
                           description="Clamp intensity", inputs=["Value"])
```

Add a Custom HLSL expression node with code and typed inputs.

### `set_material_property`
Set properties on an existing base Material, not an MI's Parent. Supply at least
one property; empty enum strings and omitted optional values leave it unchanged.
Explicit `two_sided=False` disables two-sided rendering.

- `blend_mode`: `Opaque`, `Masked`, `Translucent`, `Additive`, `Modulate`.
- `shading_model`: `DefaultLit`, `Unlit`, `Subsurface`, `ClearCoat`, `SubsurfaceProfile`, `TwoSidedFoliage`, `Hair`, `Cloth`, `Eye`, `ThinTranslucent`.
- `material_domain`: `Surface`, `PostProcess`, `DeferredDecal`, `LightFunction`, `UI`.
- `opacity_mask_clip_value`: number, used by masked materials.

Invalid enum strings fail, but this handler applies fields sequentially and
does not roll back earlier fields if a later one fails; inspect after an error.
Returns `success` and current settings, not a save confirmation.

```python
set_material_property(asset_path="/Game/__Dev/Materials/M_Glow",
                      blend_mode="Masked", opacity_mask_clip_value=0.3)
save_asset(asset_path="/Game/__Dev/Materials/M_Glow")
```

## Layout & Comments

### `add_material_comment`
Position and size are integer graph coordinates. Defaults are position `(0, 0)`
and size `(400, 300)`; the text is required.

Add a comment box around part of the graph.

### `set_expression_position`
All position/index arguments are required integers. This changes editor layout,
not wiring. Read the current graph to obtain the node index.

Move an expression node to a specific graph position.

### `reset_material_node_layout`
Uses graph topology and spacing defaults `(400, 250)`. Layout and comment
operations require an explicit save to persist, just like graph edits.

Auto-arrange the material graph nodes.
