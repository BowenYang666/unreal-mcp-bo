
# Guidelines for using Python for MCP Tools

The following guidelines apply to any method or function marked with the @mcp.tool() decorator.

- Parameters should not have any of the following types: `Any`, `object`, `Optional[T]`, `Union[T]`.
- For a given parameter `x` of type `T` that has a default value, do not use type `x : T | None = None`. Instead, use `x: T = None` and handle defaults within the method body itself.
- Always include method docstrings and make sure to given proper examples of valid inputs especially when no type hints are present.

When this rule is applied, please remember to explicitly mention it.


# Reading Asset Properties and Values

To read reflected properties and current values from a loadable UObject asset (AnimSequence, AnimMontage, BlendSpace, SkeletalMesh, Material, etc.), first use `get_class_properties` with `asset_path`:

```
get_class_properties(asset_path="/Game/Path/To/Asset")
```

Convert Windows file paths to asset paths: replace everything up to and including `Content` with `/Game`, remove `.uasset`.
Example: `D:\Projects\MyGame\Content\Player\Anims\MM_Fire.uasset` → `/Game/Player/Anims/MM_Fire`

Before claiming an asset cannot be inspected, try `get_class_properties(asset_path=...)`. It skips transient/deprecated fields and cannot expose data stored only in custom binary serialization; use specialized readers such as `read_blueprint`, `read_material`, `read_niagara_system`, or `read_state_tree` when graph/hierarchy semantics matter.


# Deploying the UnrealMCP Plugin to Another Project

When a user asks to install, copy, deploy, configure, build, update, or set up UnrealMCP for a project, load `.github/skills/unreal-mcp-project-onboarding/SKILL.md` and follow `Docs/copy_plugin_to_project.md`. Do not stop after copying. The key steps are:

1. Find the `.uproject` file (may be at the given path, one level up, or one level down).
2. Verify the engine version is 5.5+.
3. Create the `Plugins` directory if it doesn't exist.
4. Copy `MCPGameProject/Plugins/UnrealMCP` to `<ProjectRoot>/Plugins/UnrealMCP`.
5. Clean any stale `Intermediate` and `Binaries` folders at the destination.
6. Verify key files exist after copy.
7. Close the relevant editor safely, build `<ProjectName>Editor`, and fix build failures.
8. Configure the requested VS Code (`.vscode/mcp.json`) and/or Claude Code (`.mcp.json`) client.
9. Launch the target editor, verify `127.0.0.1:13090`, restart/approve the client, and run a read-only smoke test.