# `read_blueprint` cannot read collapsed Blueprint subgraphs

## Resolution

**Fixed and validated on 2026-08-15 (UE 5.7 / Content Examples).**

`read_blueprint` now supports the opt-in parameters:

```text
include_subgraphs: bool = false
max_subgraph_depth: int = 8  # clamped to 0..32
```

With `include_subgraphs=true`, collapsed `UK2Node_Composite::BoundGraph` objects are returned in a top-level `subgraphs` array with graph/parent/owner IDs, depth, node counts, nodes, pins, links, comments, and function/event metadata. Composite wrapper nodes expose `subgraph_id`.

All graph pins now expose non-empty `default_value`, `default_text_value`, and `default_object` data. Traversal uses a visited graph set and maximum depth; reading does not dirty Blueprint packages.

Validation against `/Game/ExampleContent/Input_Examples/Blueprints/BP_UFO_Physics` returned:

- `Movement sequence`: 87 nodes and 222 pins with defaults;
- `Abduction beam sequences`: 83 nodes and 139 pins with defaults;
- internal calls including `SetPhysicsLinearVelocity`, `VInterpTo`, `GetInputAxisValue`, `RetriggerableDelay`, and `SetSimulatePhysics`;
- exact constants including FOV mapping `300..1000 -> 75..110`, abduction `VInterpTo.InterpSpeed=5`, and retriggerable delay `Duration=0.5`.

Calls without `include_subgraphs` retain the previous top-level shape and omit `subgraphs`. `include_nodes=false` still returns subgraph metadata/node counts without per-node payloads. Oversized expanded results continue to spill to a JSON file.

## Summary

The Unreal MCP `read_blueprint` tool can serialize nodes and links from a Blueprint's root Event Graph, but it does not descend into collapsed graphs represented by `UK2Node_Composite`.

The tool returns the composite node itself, including its title and external pins, but omits the internal graph stored in:

```cpp
UK2Node_Composite::BoundGraph
```

As a result, important Blueprint logic becomes a black box whenever the author uses **Collapse Nodes / Collapse to Graph**.

This was found while inspecting the Content Examples UFO Pawn:

```text
/Game/ExampleContent/Input_Examples/Blueprints/BP_UFO_Physics
```

Its two main logic groups are collapsed graphs:

```text
Movement sequence
Abduction beam sequences
```

The current tool sees both wrapper nodes but cannot return any node, pin, constant, or connection inside them.

---

## Environment

Issue reproduced on:

```text
Unreal Engine: 5.7
Project: Content Examples
Project path: E:/04_projects/GameRefProjects/ContentExamples
MCP source: E:/04_projects/unreal-mcp-bo
Blueprint: /Game/ExampleContent/Input_Examples/Blueprints/BP_UFO_Physics
Example map: /Game/Maps/Blueprint/Blueprint_Input_Examples
```

The MCP server is operating in read-only mode, but this issue is unrelated to read-only filtering. `read_blueprint` is available and otherwise works.

---

## Why this matters

The missing subgraphs contain nearly all of the UFO's important behavior:

- camera-relative 3-axis flight input;
- target velocity construction;
- velocity interpolation;
- `SetPhysicsLinearVelocity`;
- ascending and descending;
- ship pitch/roll lean;
- camera rotation and FOV response;
- speed boost impulse and cooldown;
- boost particle spawning;
- abduction beam activation;
- overlap collection;
- physics disable/restore;
- abducted object movement and release.

Without internal nodes and their unconnected pin defaults, MCP can identify that a collapsed graph exists but cannot explain or reproduce its implementation accurately.

For example, asset metadata and binary strings show that the movement graph contains `VInterpTo`, `SetPhysicsLinearVelocity`, FOV mapping, and boost logic. However, the current MCP response cannot reveal exact values such as:

```text
VInterpTo.InterpSpeed
Boost velocity increment
Boost cooldown duration
Pitch/Roll lean angles
FOV input/output ranges
```

Those values are stored on unconnected input pins inside the omitted `BoundGraph`.

---

## Reproduction

### Prerequisites

1. Open the Content Examples project in Unreal Editor.
2. Ensure the Unreal MCP plugin and Python MCP server are running.
3. Open any map; the issue is asset-level and does not depend on the currently loaded map.

### MCP call

Call:

```text
read_blueprint(
  blueprint_path="/Game/ExampleContent/Input_Examples/Blueprints/BP_UFO_Physics",
  include_nodes=true,
  include_properties=false,
  include_anim_graph=false
)
```

If using the raw Unreal bridge, send:

```json
{
  "type": "read_blueprint",
  "params": {
    "blueprint_path": "/Game/ExampleContent/Input_Examples/Blueprints/BP_UFO_Physics",
    "include_nodes": true,
    "include_properties": false,
    "include_anim_graph": false
  }
}
```

### Actual result

The root `EventGraph` contains 16 nodes. Two of them are returned like this:

```json
{
  "node_class": "K2Node_Composite",
  "node_title": "Movement sequence\nCollapsed Graph",
  "pins": [
    {
      "name": "then",
      "direction": "Output",
      "is_connected": true
    },
    {
      "name": "execute",
      "direction": "Input",
      "is_connected": true
    }
  ]
}
```

and:

```json
{
  "node_class": "K2Node_Composite",
  "node_title": "Abduction beam sequences\nCollapsed Graph",
  "pins": [
    {
      "name": "execute",
      "direction": "Input",
      "is_connected": true
    }
  ]
}
```

There is no corresponding internal graph object, no `subgraph` field, and no top-level `subgraphs` list.

Searching the returned JSON for known functions inside the collapsed graph returns nothing, for example:

```text
SetPhysicsLinearVelocity
VInterpTo
GetInputAxisValue
MapRangeClamped
RetriggerableDelay
SetFieldOfView
SetSimulatePhysics
```

### Manual editor comparison

1. Open `BP_UFO_Physics` in the Blueprint Editor.
2. Open `EventGraph`.
3. Double-click the node named `Movement sequence`.
4. Observe that it contains the omitted flight, camera, and boost nodes.
5. Return to `EventGraph`.
6. Double-click `Abduction beam sequences`.
7. Observe that it contains the omitted beam and physics-handling nodes.

This proves the internal graphs exist and are readable by the editor, but are not traversed by MCP.

---

## Expected result

`read_blueprint` should optionally return collapsed subgraphs and their complete node data.

At minimum, each subgraph must expose:

```text
graph id
graph name
graph type
parent graph id
owning composite node id/title
node count
nodes
pins
pin default values
pin links
comments
event/function metadata
```

The UFO result should make it possible to recover both:

```text
Movement sequence
Abduction beam sequences
```

without manually copying nodes from the Blueprint Editor.

---

## Root cause

The issue is in:

```text
MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp
```

`FUnrealMCPBlueprintCommands::HandleReadBlueprint` currently serializes root Event Graphs by iterating:

```cpp
for (UEdGraph* Graph : Blueprint->UbergraphPages)
{
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        // Serialize root graph node and pins.
    }
}
```

This code does not special-case:

```cpp
UK2Node_Composite
```

and therefore never visits:

```cpp
CompositeNode->BoundGraph
```

Relevant current implementation area:

```text
UnrealMCPBlueprintCommands.cpp
HandleReadBlueprint
"Event Graphs" section, approximately lines 1567-1655
```

The plugin already demonstrates access to other `BoundGraph` objects in its Animation Blueprint state/transition serialization, so the required editor data is available.

---

## Secondary issue: pin default values are missing

Even after recursive graph traversal is added, the current node serializer does not return unconnected pin defaults.

It currently returns fields such as:

```text
name
type
direction
is_connected
linked_to
```

but omits:

```cpp
UEdGraphPin::DefaultValue
UEdGraphPin::DefaultTextValue
UEdGraphPin::DefaultObject
```

This prevents reconstruction of constants such as interpolation speed, delay duration, rotation limits, FOV range, scalar multipliers, enum values, and asset references.

Recommended serialization:

```cpp
if (!Pin->DefaultValue.IsEmpty())
{
    PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
}

if (!Pin->DefaultTextValue.IsEmpty())
{
    PinObj->SetStringField(TEXT("default_text_value"), Pin->DefaultTextValue.ToString());
}

if (Pin->DefaultObject)
{
    PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
}
```

This should apply to root graphs and nested graphs alike.

---

## Recommended fix: shared recursive graph serializer

Do not implement UFO-specific logic. Extract the existing graph/node/pin serialization into reusable helpers and use them for root and nested graphs.

### Required includes

```cpp
#include "K2Node_Composite.h"
#include "K2Node_Tunnel.h"
```

### Suggested helpers

```cpp
TSharedPtr<FJsonObject> SerializeBlueprintNode(UEdGraphNode* Node);

TSharedPtr<FJsonObject> SerializeBlueprintGraph(
    UEdGraph* Graph,
    bool bIncludeNodes,
    bool bIncludeSubgraphs,
    int32 Depth,
    int32 MaxDepth,
    TSet<FGuid>& VisitedGraphs);
```

When serializing a node:

```cpp
if (UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node))
{
    if (bIncludeSubgraphs && Composite->BoundGraph)
    {
        // Recursively serialize Composite->BoundGraph.
    }
}
```

Retain `UK2Node_Tunnel` nodes in the child graph. Their pins connect the collapsed graph's internal control/data flow to the composite wrapper.

Useful tunnel metadata can be added, but complete pin/link output is more important than classifying tunnel role perfectly.

---

## Recommended output shape

Two formats are viable.

### Option A: nested on the composite node

```json
{
  "node_class": "K2Node_Composite",
  "node_title": "Movement sequence\nCollapsed Graph",
  "subgraph": {
    "graph_id": "...",
    "name": "Movement sequence",
    "node_count": 42,
    "nodes": []
  }
}
```

### Option B: top-level graph index

Recommended for MCP because it is easier to search and later supports graph-specific reads:

```json
{
  "event_graphs": [],
  "subgraphs": [
    {
      "graph_id": "...",
      "name": "Movement sequence",
      "graph_type": "collapsed",
      "parent_graph_id": "...",
      "owner_node_id": "...",
      "owner_node_title": "Movement sequence\nCollapsed Graph",
      "depth": 1,
      "node_count": 42,
      "nodes": []
    }
  ]
}
```

Whichever format is selected, graph identity and ownership must be present.

---

## Recommended request parameters

To preserve compatibility and avoid unexpectedly huge payloads, add optional parameters:

```text
include_subgraphs: bool = false
max_subgraph_depth: int = 8
```

Suggested Python signature:

```python
def read_blueprint(
    ctx,
    blueprint_path: str,
    include_nodes: bool = True,
    include_properties: bool = True,
    include_anim_graph: bool = False,
    include_subgraphs: bool = False,
    max_subgraph_depth: int = 8,
):
```

Defaulting `include_subgraphs` to `false` keeps current response size and behavior unchanged.

Clamp `max_subgraph_depth`, for example to `0..32`.

Use a visited graph set:

```cpp
TSet<FGuid> VisitedGraphs;
```

to avoid duplicate traversal or accidental cycles.

---

## Better long-term API design

A fully expanded Blueprint can be too large for one MCP response. The Python wrapper already spills oversized responses to a temporary JSON file, but graph-specific reading would be more efficient.

Recommended eventual split:

### `read_blueprint`

Returns:

```text
Blueprint metadata
components
variables
interfaces
graph index with ids/types/node counts
```

### `read_blueprint_graph`

Example:

```text
read_blueprint_graph(
  blueprint_path="/Game/ExampleContent/Input_Examples/Blueprints/BP_UFO_Physics",
  graph_name="Movement sequence",
  include_subgraphs=true
)
```

It should accept a stable `graph_id` as well as a name because graph names are not guaranteed unique.

This is optional for the first fix. Recursive `read_blueprint` support is sufficient to resolve the reported UFO issue.

---

## Full Blueprint coverage beyond this issue

Supporting `UK2Node_Composite::BoundGraph` fixes this reproduction, but a future claim of “complete Blueprint reading” should also consider:

- `Blueprint->FunctionGraphs`: currently only name and `node_count` are returned;
- `Blueprint->MacroGraphs`;
- `Blueprint->DelegateSignatureGraphs`;
- nested collapsed graphs inside functions/macros;
- Timeline templates and tracks/keyframes;
- specialized Animation Blueprint graph data.

These are follow-up concerns, not required to close this issue.

---

## Acceptance criteria

### Backward compatibility

- Existing calls without `include_subgraphs` retain the previous output shape and size behavior.
- `include_nodes=false` still omits per-node data.
- `include_properties=false` still omits component property dumps.

### UFO reproduction

A call with:

```text
include_nodes=true
include_subgraphs=true
```

must return both internal graphs:

```text
Movement sequence
Abduction beam sequences
```

The returned graph data must include internal nodes such as the movement/boost and abduction operations visible in the Blueprint Editor.

### Pin constants

Unconnected input values must be available through `default_value`, `default_text_value`, or `default_object` as appropriate.

This should allow recovery of, among other things:

```text
VInterpTo interpolation speed
Boost velocity amount
Boost cooldown/delay
Camera FOV mapping values
Pitch/Roll limits
Beam interpolation/strength constants
Asset references used by spawn/material nodes
```

### Safety

- Recursive traversal has a visited set and maximum depth.
- `read_blueprint` remains read-only.
- Oversized output still uses `spill_if_oversized` rather than truncating data.
- No Blueprint package is marked dirty by reading.

---

## Suggested validation assets

### Regression: simple Blueprint

```text
/Game/ExampleContent/Blueprints/Blueprints/BP_Gears
```

Verify existing root graph output remains valid.

### Primary reproduction

```text
/Game/ExampleContent/Input_Examples/Blueprints/BP_UFO_Physics
```

Verify both collapsed graphs are returned with nodes, links, and constants.

### Large/nested Blueprint

Use a Blueprint with one or more nested collapsed graphs to verify:

- recursion depth;
- visited graph handling;
- no duplicate graphs;
- spill-to-file behavior.

---

## Temporary workaround

Until the MCP fix is available:

1. Open `BP_UFO_Physics` in the Blueprint Editor.
2. Double-click `Movement sequence`.
3. Select all internal nodes and copy them.
4. Paste the Blueprint clipboard text into a file for analysis.
5. Repeat for `Abduction beam sequences`.

Copying nodes is read-only as long as the graph is not edited or saved.
