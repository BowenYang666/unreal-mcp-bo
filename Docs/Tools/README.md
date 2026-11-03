# Unreal MCP Tools

Index for **167 internal operations**. Default grouped mode exposes **57 MCP
tools** before read-only/category filtering; direct compatibility mode exposes 167.

- [Actor Tools](actor_tools.md) (8)
- [Scene Tools](scene_tools.md) (20) - Guarded instances, captures, imports, placement manifests, Outliner folders, temporary editor visibility and controlled undo
- [Editor Tools](editor_tools.md) (10 editor + 3 asset operations)
- [Bounded Asset Creation and Property Writes](project_tools.md#set_asset_properties) (3 additional Asset operations)
- [DataAsset FText References](project_tools.md#dataasset-text-references) (2 additional Asset operations: native identity read and guarded String Table binding)
- [String Table Assets](project_tools.md#string-table-assets) (4 additional Asset operations)
- [Enhanced Input Assets](project_tools.md#enhanced-input-assets) (2 additional Asset operations: full IMC read and guarded default append)
- [Animation Assets](project_tools.md#animation-assets) (3 additional Asset operations: authoring snapshot, Additive patch, native Montage Notify add)
- [Dependency Copy Workflow](editor_tools.md#dependency-copy-workflow) (4 additional Asset operations: plan, execute, status, verify)
- [Blueprint Tools](blueprint_tools.md) (11) - Includes [guarded skeletal component defaults](blueprint_tools.md#skeletal-component-defaults)
- [Reading Blueprints](reading_blueprints.md) - How to discover and inspect existing Blueprints
- [Node Tools](node_tools.md) (8)
- [Niagara Tools](niagara_tools.md) (36)
- [Material Tools](material_tools.md) (17)
- [UMG Tools](umg_tools.md) (23) - Widget Blueprints / in-game UI
- [Project Tools](project_tools.md) (6) - Input mappings, reflection, Behavior Trees, Blackboards, StateTrees
- [Navigation Tools](navigation_tools.md) (6) - NavMesh bounds, build status, point projection, and pathfinding
- [Cascade Reader](cascade_tools.md) (1) - Legacy ParticleSystem emitters, LODs, modules, distributions, and references

Legacy C++ commands without a registered Python wrapper are not counted as supported MCP tools.

## Grouped Mode (Default)

Material, Niagara, UMG, Scene, Blueprint and Asset use these entry points. Operation names and parameter
tables in the linked domain references remain unchanged.

| Category | Discovery | Read | Write |
|---|---|---|---|
| Material | `material_search` | `material_call_read` | `material_call_write` |
| Niagara | `niagara_search` | `niagara_call_read` | `niagara_call_write` |
| UMG | `umg_search` | `umg_call_read` | `umg_call_write` |
| Scene | `scene_search` | `scene_call_read` | `scene_call_write` |
| Blueprint assets | `blueprint_search` | `blueprint_call_read` | `blueprint_call_write` |
| Asset operations | `asset_search` | `asset_call_read` | `asset_call_write` |

Search parameters: `query=""`, `tool=""`, `limit=5` (1..10), `offset=0`.
The public schema declares the limit minimum/maximum and nonnegative offset.
An empty query browses summaries; keyword queries use case-insensitive English
word matching, not semantic/vector search. No match? Try a shorter English query
or browse. `has_more` and `next_offset` identify further pages.

An exact `tool` name (or exact name in `query`) returns the original description,
`input_schema`, `effect` and `call_tool`. Unknown/disabled names are rejected.
Exact `tool` takes precedence over other search parameters. Keyword searches
return summaries and a `next_call` for exact lookup; a single match additionally
includes its full `contract`. Search never invokes an editor operation, although
the existing server startup still attempts its normal editor connection.

Example MCP calls, in order:

```text
material_search(query="parent")
material_search(tool="set_material_instance_parameters")
material_call_write(
	tool="set_material_instance_parameters",
	arguments={
		"asset_path": "/Game/Materials/MI_Test",
		"parent_material_path": "/Game/Materials/M_Parent"
	}
)
```

Skip the second lookup when the search already supplied the exact contract.
Reuse a known contract within the session; repeated searches are not required.
Do not send Python's injected `ctx`. Read/write calls require both `tool` and
an `arguments` object (use `{}` for parameterless operations).

Blueprint and Asset examples:

```text
blueprint_search(tool="read_blueprint")
blueprint_call_read(tool="read_blueprint", arguments={"blueprint_path":"/Game/Blueprints/BP_Player"})
asset_search(tool="read_data_asset_text_references")
asset_call_read(tool="read_data_asset_text_references", arguments={"asset_path":"/Game/Data/DA_Tower","property_paths":["DisplayName"]})
```

This covers Blueprint component-default authoring and all Asset animation,
Enhanced Input, String Table, FText, creation/property, migration and rename/move/duplicate
operations, even when their Python implementation lives in project/editor tool modules.
Blueprint **Node** operations remain direct under the independent `MCP_NODE_ENABLED`
filter. Project readers such as `get_class_properties`, `read_data_asset` and
`read_state_tree` also remain direct; Blueprint/Asset grouping does not move them.

The dispatcher retains original defaults, parameter validation and response
payloads, including save failures and large-result file pointers. It additionally
rejects undeclared top-level operation arguments, cross-category calls and
read/write route mismatches. It does not add saves, retries, transactions or
rollback. An underlying `success=false` payload keeps its original semantics;
routing/argument errors are MCP tool errors.

## Modes And Filters

- `MCP_TOOL_MODE=grouped` (default): 57 public tools; original names from these
	six categories are internal and cannot be called directly via MCP.
- `MCP_TOOL_MODE=direct`: all 167 public operations, no grouped entry points.
- `UNREAL_MCP_READ_ONLY=1`: 29 public tools in grouped mode, 49 in direct mode.
	Only approved read operations remain; hidden writes cannot be searched or run.
- Existing `MCP_MATERIAL_ENABLED`, `MCP_NIAGARA_ENABLED`, `MCP_UMG_ENABLED`, `MCP_SCENE_ENABLED`,
  `MCP_BLUEPRINT_ENABLED`, `MCP_ASSET_ENABLED`
	filters remove the whole category, including its entry points, when disabled.
- Other categories, including Cascade and Navigation, keep their direct API.

Counts assume no category is disabled. Restart/reconnect the MCP server/client
after changing modes; no UE plugin rebuild is needed. Clients sharing this
Python server source pick up grouped mode on their next restart unless configured
with `MCP_TOOL_MODE=direct`. Do not expose both modes simultaneously.

Blueprint/Asset grouping reduces the unfiltered surface from 83 to 57; with only
Node disabled (the current NF profile), from 75 to 49. These are tool counts,
not measured tokens. No operation was removed. Existing direct-call clients must
use the discovered grouped endpoints or explicitly retain `MCP_TOOL_MODE=direct`.
Python MCP restart/reconnect is sufficient; this routing change requires no UE
rebuild, editor restart, native plugin deployment or permission changes.


These checks are Python MCP dispatch rules, not native TCP authentication or a
new C++ permission boundary. Client approval UI and annotations are not relied on
to enforce read/write separation. Context savings depend on the client's own
tool discovery/caching; fewer advertised tools are not a measured token count.

Scene grouping replaces 20 public operation schemas with three entry points. Scene
search returns compact summaries by default and a complete schema on exact lookup.
Future Scene operations added to its category use the same entry points. Existing
scripts that call original scene names must use discovery/dispatch or explicitly
select MCP_TOOL_MODE=direct; there is no hidden direct-call fallback in grouped mode.
