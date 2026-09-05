# Material Tools

Tools for creating and editing Material graphs and Material Instances programmatically.

## Reading & Discovery

### `list_materials`
List Material assets in the project. Supports an optional path filter.

### `read_material`
Read the full structure of a Material: domain, blend mode, shading model, all expression nodes (with positions, parameters, textures, input connections and inline defaults), the main material input pins (BaseColor, EmissiveColor, etc.), and comment boxes. Identify by `name` or `path`.

Also returns a read-only **`compile_result`** from the material's current cached resource. `read_material` does not force a recompile or dirty the package. Material mutation tools trigger `PostEditChange`; read their resulting resource afterward:
```jsonc
"compile_result": {
  "available": true,
  "ok": true,          // false if the material failed to compile
  "recompiled": false,
  "source": "cached_material_resource",
  "error_count": 0,
  "errors": []         // translation/compile error messages
}
```
Recommended workflow: after editing a material, call `read_material` and require `compile_result.available=true` and `compile_result.ok=true`; if false, inspect `compile_result.errors`. Reading an untouched material remains side-effect free.

### `get_material_instance_parameters`
Read the parameters (scalar/vector/texture, override state) of a Material Instance Constant.

## Creating Assets

### `create_material`
Create a new Material asset at a content path.

### `create_material_instance`
Create a Material Instance Constant from a parent material, with optional parameter overrides:
- `scalar_params` — `{"ParamName": value}`
- `vector_params` — `{"ParamName": {"r":1,"g":0,"b":0,"a":1}}`
- `texture_params` — `{"ParamName": "/Game/.../T_MyTex"}` (path tolerates a missing object suffix)

This is the vendor pattern: one master material + many instances that only swap the texture (and a scalar or two). No need to duplicate the graph per variant.

### `set_material_instance_parameters`
Override parameters on an **existing** Material Instance Constant (for iteration, no re-creation). Same `scalar_params` / `vector_params` / `texture_params` shapes as `create_material_instance`; only the parameters you pass are changed.

## Graph Editing

### `add_material_expression`
Add an expression node (e.g. `Multiply`, `TextureCoordinate`, `ScalarParameter`, `DynamicParameter`) to a material graph. Returns the node index.

### `set_material_expression_property`
Set a property on an expression node via reflection (scalars, vectors, enums, and array properties like a `DynamicParameter`'s `ParamNames`).

### `connect_material_expressions`
Connect one expression's output to another expression's input. Output is resolved by name or index via the node's virtual `GetOutputs()`; input by name.

### `connect_material_to_property`
Connect an expression output to a main material property pin (e.g. `EmissiveColor`, `BaseColor`, `Normal`, `Opacity`).

### `add_custom_hlsl_expression`
Add a Custom HLSL expression node with code and typed inputs.

### `set_material_property`
Set a material-level property (e.g. blend mode, shading model, two-sided).

## Layout & Comments

### `add_material_comment`
Add a comment box around part of the graph.

### `set_expression_position`
Move an expression node to a specific graph position.

### `reset_material_node_layout`
Auto-arrange the material graph nodes.
