# Navigation Tools

Tools for creating NavMesh bounds, building editor navigation data, and running
agent-aware projection and path queries. All six tools require `level_path` and
reject the request unless it exactly matches the currently open Editor world.
They do not operate on PIE or game worlds.

## Bounds Volumes

### `set_nav_mesh_bounds_volume`

Create or update one box-shaped `NavMeshBoundsVolume` transactionally.

- `level_path` - current map package path, for example `/Game/Maps/Arena`
- `location` - world-space center `[x, y, z]` in centimeters
- `full_size` - full box dimensions `[x, y, z]` in centimeters, not half extents
- `actor_name` - internal name or World Outliner label; defaults to `NavMeshBoundsVolume`

The response reports whether the actor was created or updated, its transform,
and the resulting world-space `bounds_min`, `bounds_max`, `bounds_center`, and
`full_size`. The command refuses ambiguous matches instead of creating a
duplicate.

### `list_nav_mesh_bounds_volumes`

List every `NavMeshBoundsVolume` in the current map with its transform and
world-space bounds. This tool is available in read-only mode.

## Navigation Build

### `build_navigation`

Request a navigation build for the current editor map. The response includes a
`request_id`, `accepted`, and the current status. A successful request is not by
itself proof that generation completed; poll `get_navigation_status` until
`build_in_progress` is false and `nav_data_available` is true.

### `get_navigation_status`

Return the build state (`not_built`, `in_progress`, `completed`, or `failed`),
dirty-area count, remaining/running task counts, and default NavData identity.
This tool is available in read-only mode.

## Agent Queries

### `project_point_to_navigation`

Project `point=[x,y,z]` onto matching NavData. `agent_class_path` accepts a
Blueprint asset path such as `/Game/Enemies/BP_Enemy` or a generated class path.
The tool reads the class default object's `INavAgentInterface`; when Pawn CDO
dimensions are unset, it fills radius and height from that Pawn's capsule.
Optional `extent` defaults to `[50,50,250]`.

### `find_navigation_path`

Project `start` and `end`, select NavData for the same real agent properties, and
run synchronous pathfinding. The response includes:

- projected endpoints and selected NavData
- agent radius, height, step height, movement capabilities, and property source
- query result, `path_valid`, `partial`, and `complete`
- path points, length, cost, and a failure reason when no complete path exists

Both query tools are available in read-only mode. They never change Supported
Agents or project navigation settings.

## Category Control

Set `MCP_NAVIGATION_ENABLED=0` (also accepts `false`, `no`, or `off`) to remove
all six tools. In global read-only mode, the two mutators
`set_nav_mesh_bounds_volume` and `build_navigation` are removed while the four
inspection/query tools remain available.