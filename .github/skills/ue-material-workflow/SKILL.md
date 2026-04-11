---
name: ue-material-workflow
description: "Use this skill when creating, editing, or organizing Material graphs via MCP tools. Covers the complete workflow: creating materials, adding expression nodes, wiring connections, positioning/layouting nodes, grouping with comment boxes, creating material instances, and HLSL custom expressions. Read this BEFORE calling any material MCP tool."
metadata:
  version: 1.0.0
---

# UE Material Workflow (MCP Tools)

You are controlling Unreal Engine's Material Editor through MCP tools. Follow this workflow precisely.

## Available Material MCP Tools

| Tool | Purpose |
|------|---------|
| `create_material` | Create a new empty material asset |
| `add_material_expression` | Add a node (e.g. Multiply, VectorParameter) |
| `set_material_expression_property` | Set a property on a node (e.g. ParameterName, Constant) |
| `connect_material_expressions` | Wire one node's output to another's input |
| `connect_material_to_property` | Wire a node to a material output (BaseColor, Roughness, etc.) |
| `add_custom_hlsl_expression` | Add a Custom HLSL node with code and typed inputs/outputs |
| `set_expression_position` | Move a single node to an exact (x, y) coordinate |
| `reset_material_node_layout` | Auto-layout all nodes (row-based per material property chain) |
| `add_material_comment` | Add a comment box (group) to visually contain nodes |
| `create_material_instance` | Create a material instance with scalar/vector overrides |
| `list_materials` | List all materials in the project |
| `read_material` | Read full material graph (nodes, connections, positions) |

---

## Workflow: Creating a Material

Follow these steps **in order**:

### Step 1 — Create the material

```
create_material(asset_path="/Game/Materials/M_MyMat")
```

### Step 2 — Add all expression nodes

Add every node you need. Each call returns the node's `index` — **record it**.

```
add_material_expression(asset_path=..., expression_class="Noise", ...)        → index 0
add_material_expression(asset_path=..., expression_class="ScalarParameter")   → index 1
add_material_expression(asset_path=..., expression_class="Multiply")          → index 2
```

### Step 3 — Configure node properties

Set parameters, constants, names, etc.

```
set_material_expression_property(asset_path=..., node_index=1, property_name="ParameterName", value="FrostAmount")
set_material_expression_property(asset_path=..., node_index=1, property_name="DefaultValue", value=0.5)
```

### Step 4 — Wire connections

Connect nodes to each other, then to material outputs.

```
connect_material_expressions(asset_path=..., from_index=0, to_index=2, to_input_name="A")
connect_material_expressions(asset_path=..., from_index=1, to_index=2, to_input_name="B")
connect_material_to_property(asset_path=..., node_index=2, material_property="Opacity")
```

### Step 5 — Position nodes for readability

**THIS IS CRITICAL.** After wiring, use `read_material` to see all node indices and their current positions. Then decide on a clean horizontal layout and move each node.

#### Layout Convention (Horizontal Left-to-Right Flow)

```
X axis: data flows LEFT → RIGHT toward the material output node (which is at x≈0).
        Source nodes (textures, parameters) at large negative X (e.g. -1200).
        Intermediate processing at medium negative X (e.g. -600).
        Final nodes near X=0.

Y axis: Each material-property chain occupies its own horizontal row.
        Row 0 starts at Y=0, Row 1 at Y=300, Row 2 at Y=600, etc.
        Nodes within the same chain share the same Y.
```

**Example layout for a 3-chain material:**

```
Y=0:    [Noise](-1200,0) → [Multiply](-600,0) → [Saturate](0,0)        → Opacity
Y=350:  [VectorParam](-600,350) → [Multiply](-200,350)                  → BaseColor
Y=600:  [Constant](-200,600)                                            → Roughness
```

Use `set_expression_position` for each node:

```
set_expression_position(asset_path=..., node_index=0, pos_x=-1200, pos_y=0)
set_expression_position(asset_path=..., node_index=2, pos_x=-600,  pos_y=0)
...
```

**Alternatively**, call `reset_material_node_layout` to auto-layout. It organizes nodes into horizontal rows per material property chain automatically.

### Step 6 — Add comment boxes (groups)

Comment boxes are **horizontal rectangles** that span a row of related nodes.

**Rules:**
- Each comment covers ONE functional chain (one horizontal row).
- Comment `pos_x` = leftmost node X − 80 margin.
- Comment `pos_y` = row Y − 100 margin (above).
- Comment `size_x` = (rightmost node X − leftmost node X) + 400 (node width + margins).
- Comment `size_y` = 250–350 (enough to wrap one row of nodes).
- If a row has vertically stacked nodes, increase `size_y` accordingly.

```
add_material_comment(asset_path=..., text="Frost Pattern", pos_x=-1280, pos_y=-100, size_x=1100, size_y=300)
add_material_comment(asset_path=..., text="Base Color",    pos_x=-680,  pos_y=250,  size_x=900,  size_y=300)
```

### Step 7 — Save the material

Always save after finishing all edits:

```
save_asset(asset_path="/Game/Materials/M_MyMat")
```

### Step 8 — Create material instances (optional)

```
create_material_instance(
    asset_path="/Game/Materials/MI_FrostLight",
    parent_path="/Game/Materials/M_MyMat",
    scalar_parameters={"FrostAmount": 0.3},
    vector_parameters={"IceColor": {"r":0.7,"g":0.9,"b":1.0,"a":1.0}}
)
```

---

## Key Principles

1. **Always read first**: Before modifying an existing material, call `read_material` to see current nodes, indices, positions, and connections.

2. **Horizontal rows, not vertical columns**: Material graphs flow left-to-right. Each material property chain (BaseColor chain, Roughness chain, etc.) should be its own horizontal row. **Never** stack unrelated chains vertically in a single column.

3. **Position before grouping**: Always set node positions (Step 5) BEFORE adding comment boxes (Step 6). Comment coordinates depend on where nodes are.

4. **Comment boxes are wide, not tall**: A typical comment box is ~800-1200px wide and ~250-350px tall — it wraps a horizontal chain. If you see a comment that's taller than it is wide, something is wrong.

5. **Node index is stable per session**: The index returned by `add_material_expression` or shown by `read_material` stays valid until nodes are added/removed.

6. **Shared nodes**: If a node (e.g. a Noise texture) feeds into multiple chains, place it between those rows at an intermediate Y position. It belongs to whichever chain you assign visually, but keep connections clear.

---

## Common Expression Classes

| Short Name | Full Class | Common Properties |
|------------|-----------|-------------------|
| `Noise` | MaterialExpressionNoise | NoiseFunction, Scale, Levels, OutputMin, OutputMax |
| `Constant` | MaterialExpressionConstant | R (the value) |
| `Constant3Vector` | MaterialExpressionConstant3Vector | Constant (FLinearColor) |
| `ScalarParameter` | MaterialExpressionScalarParameter | ParameterName, DefaultValue |
| `VectorParameter` | MaterialExpressionVectorParameter | ParameterName, DefaultValue (FLinearColor) |
| `Multiply` | MaterialExpressionMultiply | (inputs: A, B) |
| `Add` | MaterialExpressionAdd | (inputs: A, B) |
| `Lerp` | MaterialExpressionLinearInterpolate | (inputs: A, B, Alpha) |
| `Saturate` | MaterialExpressionSaturate | (input: unnamed/"") |
| `TextureSample` | MaterialExpressionTextureSample | Texture, SamplerType |
| `Fresnel` | MaterialExpressionFresnel | Exponent, BaseReflectFractionIn |
| `Power` | MaterialExpressionPower | (inputs: Base, Exponent) |
| `OneMinus` | MaterialExpressionOneMinus | (input: unnamed/"") |

## Material Property Names (for connect_material_to_property)

`BaseColor`, `Metallic`, `Specular`, `Roughness`, `Normal`, `EmissiveColor`, `Opacity`, `OpacityMask`, `WorldPositionOffset`, `AmbientOcclusion`, `Refraction`, `SubsurfaceColor`
