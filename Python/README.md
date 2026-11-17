# Unreal MCP Python Server

Python MCP server for Unreal Engine 5.5+ (tested on UE 5.7).

```text
MCP client -> stdio -> unreal_mcp_server.py -> TCP 127.0.0.1:13090 -> UE plugin
```

## Run with uv

Install [uv](https://docs.astral.sh/uv/), then run from the repository root:

```powershell
uv --directory ./Python run unreal_mcp_server.py
```

`uv run` resolves the project environment from `pyproject.toml`/`uv.lock`; manually activating a virtual environment is not required.

For VS Code and Claude Code configuration, see the [client configuration guide](../Docs/copy_plugin_to_project.md#step-8-configure-the-mcp-client) and [onboarding workflow](../.github/skills/unreal-mcp-project-onboarding/SKILL.md).

## Direct Test Scripts

Scripts under [scripts](./scripts) connect directly to the editor plugin on `127.0.0.1:13090`; the Python MCP stdio server is not required for them. The target Unreal Editor must be running with the current UnrealMCP plugin loaded.

```powershell
uv --directory ./Python run python scripts/actors/test_cube.py
```

## Environment Controls

- `MCP_TOOL_MODE=grouped` (default): expose Material/Niagara/UMG/Scene/Blueprint/Asset search/read/write entry points, 57 public tools before filtering (29 read-only). `direct` exposes all 177 operations directly (54 read-only). Original names from these six categories are internal; discover their contracts with the corresponding `<category>_search`. Blueprint Node, Project, Editor, Navigation and Cascade remain direct. See the [calling guide](../Docs/Tools/README.md#grouped-mode-default).
- [Particle diagnostics](../Docs/Tools/niagara_tools.md#read_niagara_diagnostics): exact Handle/Usage/Module event stacks, source relationships, input provenance/enums and current editor isolation. Read-only, `diagnostics_contract=1` requires the matching native DLL; CPU/GPU particle snapshots and live event counts remain unsupported. Existing four-stage input writers are not widened.
- `MCP_SCENE_ENABLED`: controls 20 guarded scene operations. See [scene tools](../Docs/Tools/scene_tools.md) for identity, preview, task status and save constraints. Folder parameters require native `folder_contract=1`; temporary actor hiding requires `editor_visibility_contract=1`, not merely updated Python schemas.
- `UNREAL_MCP_PORT`: native editor TCP port, default 13090. Match `-UnrealMCPPort=<port>` when running multiple editors. Host remains loopback.
- `UNREAL_MCP_EXPECTED_PROJECT`: optional absolute `.uproject` path binding for every request. Requires the matching native plugin; a wrong project refuses before execution, missing native attestation fails closed without legacy fallback or automatic retry. Leave unset only for legacy unbound profiles. Use a separate Python MCP service/profile per project; see [persistent project ports and binding](../Docs/copy_plugin_to_project.md#multiple-editors-and-project-binding).
- `UNREAL_MCP_READ_ONLY=1`: expose only the current read-only whitelist.
- [Typed property paths](../Docs/Tools/project_tools.md#typed-property-paths): `read_asset_property_paths` / `patch_asset_property_paths` belong to Asset; `read_blueprint_property_paths` / `patch_blueprint_property_paths` belong to Blueprint independently. Shared native field/array-index/typed-Map-key traversal, preview/baseline/protected patch; no container growth or arbitrary object traversal. Readers survive read-only filtering. NF matching DLL/native/cold/public read acceptance passed on 2026-11-13. SkillTest's deferred source was compiled in the complete 2026-11-17 particle-diagnostics deployment; the full property-authoring matrix was not repeated in that deployment. Reconnect clients after the matching DLL update; Python discovery alone is not deployment.
- `MCP_ASSET_ENABLED`: controls `rename_asset`, `move_asset`, `duplicate_asset`, `create_data_asset`, `create_physical_material`, `set_asset_properties` (DataAsset instances and allowlisted PhysicalMaterial fields), the two Asset property-path operations, plus `plan_asset_migration`, `execute_asset_migration`, `get_asset_migration_status`, and `verify_asset_migration`. Read-only mode retains query operations but removes execution. See the [dependency-copy workflow](../Docs/Tools/editor_tools.md#dependency-copy-workflow).
- The Asset category also includes String Table tools and [Enhanced Input IMC tools](../Docs/Tools/project_tools.md#enhanced-input-assets): `read_input_mapping_context` and `append_input_mapping_context_mappings`. The reader is read-only; append defaults to dry-run/no save and requires a complete baseline. Updated native plugin required; Legacy `create_input_mapping` is not an IMC operation.
- [Animation tools](../Docs/Tools/project_tools.md#animation-assets) are Asset operations: `read_animation_asset`, `set_anim_sequence_additive`, `add_montage_native_notify`, `read_montage_notify_properties`, `patch_montage_notify_properties`. Use `asset_search` and its returned endpoint in grouped mode; direct mode exposes original names. Both readers survive read-only filtering. Writes default preview/no save and require matching native support. The finite existing-Notify patch has its own class/Outer/GUID/baseline contract; legacy partial snapshots and Add restrictions remain unchanged. NF/SkillTest matching DLL deployment and bound public read smoke passed on 2026-11-13; reconnect existing MCP services to load the updated Python registry.
- `MCP_EDITOR_ENABLED`, `MCP_BLUEPRINT_ENABLED`, `MCP_NODE_ENABLED`, `MCP_PROJECT_ENABLED`, `MCP_UMG_ENABLED`, `MCP_MATERIAL_ENABLED`, `MCP_NIAGARA_ENABLED`, `MCP_NAVIGATION_ENABLED`, `MCP_CASCADE_ENABLED`: set to `0`/`false`/`no`/`off` to disable a category.
- [Skeletal component defaults](../Docs/Tools/blueprint_tools.md#skeletal-component-defaults) use the Blueprint category: `read_blueprint_component_defaults` and `set_blueprint_skeletal_component_defaults`. Only the reader remains in read-only mode; the writer defaults to preview/no save and needs exact source identity/baseline. Matching native plugin required, not just refreshed schemas.
- `UNREAL_PROJECT_LOG`: default log file used by `get_editor_logs`.

## Development

- Register Python MCP tools in `tools/*.py` with `@mcp.tool()` and call `register_*_tools(mcp)` from `unreal_mcp_server.py`.
- Keep category membership and the read-only allowlist in sync. Grouped entry points derive their contracts from the original registered tools after filtering; do not duplicate schemas in the dispatcher.
- Add or update C++ command handlers under `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/`, then route new command names through `UnrealMCPBridge.cpp`.
- Python-only/schema changes require an MCP client/server restart, not an Unreal build.
- C++ or `Build.cs` changes require redeploying the plugin to the target project, rebuilding its Editor target, and relaunching the editor.
- DataAsset writes/structured reads and fragmented TCP request support require the matching new plugin. Requests are serialized per Python client and the native receiver accepts one JSON object per connection (1 MiB limit, 10-second receive deadline). Transport timeout is not cancellation or proof that nothing changed.
- [DataAsset FText references](../Docs/Tools/project_tools.md#dataasset-text-references): `read_data_asset_text_references` and `bind_data_asset_text_references` use the Asset group in grouped mode. Identity-aware native String Table binding defaults to dry-run/no-save, validates existing clean saved tables/keys and preserves legacy plain strings. Read-only keeps only the reader. Repository acceptance passed; deployed to NF on 2026-11-03 with read/preview/refusal smoke and no production writes. SkillTest has not received this addition.

Offline unit and mocked-editor stdio tests (do not require or modify Unreal):

```powershell
uv --directory ./Python run python -m unittest discover -s tests -v
```

## Troubleshooting

- Confirm the intended Unreal Editor project is running.
- Verify `Get-NetTCPConnection -LocalPort 13090 -State Listen` returns the editor process.
- Restart the MCP client after Python tool signature changes.
- Check `unreal_mcp.log` and the target project's `Saved/Logs/` directory for errors.
