
# Guidelines for using Python for MCP Tools

The following guidelines apply to any method or function marked with the @mcp.tool() decorator.

- Parameters should not have any of the following types: `Any`, `object`, `Optional[T]`, `Union[T]`.
- For a given parameter `x` of type `T` that has a default value, do not use type `x : T | None = None`. Instead, use `x: T = None` and handle defaults within the method body itself.
- Always include method docstrings and make sure to given proper examples of valid inputs especially when no type hints are present.

When this rule is applied, please remember to explicitly mention it.


# Reading Asset Properties and Values

To read properties and current values from ANY `.uasset` file (AnimSequence, AnimMontage, BlendSpace, SkeletalMesh, Material, etc.), use `get_class_properties` with `asset_path`:

```
get_class_properties(asset_path="/Game/Path/To/Asset")
```

Convert Windows file paths to asset paths: replace everything up to and including `Content` with `/Game`, remove `.uasset`.
Example: `D:\Projects\MyGame\Content\Player\Anims\MM_Fire.uasset` → `/Game/Player/Anims/MM_Fire`

NEVER say "MCP can't read this asset's values" — `get_class_properties(asset_path=...)` reads any UObject asset.


# Deploying the UnrealMCP Plugin to Another Project

When a user asks to "copy the plugin to" or "deploy to" a project path, follow the step-by-step guide at `Docs/copy_plugin_to_project.md`. The key steps are:

1. Find the `.uproject` file (may be at the given path, one level up, or one level down).
2. Verify the engine version is 5.5+.
3. Create the `Plugins` directory if it doesn't exist.
4. Copy `MCPGameProject/Plugins/UnrealMCP` to `<ProjectRoot>/Plugins/UnrealMCP`.
5. Clean any stale `Intermediate` and `Binaries` folders at the destination.
6. Verify key files exist after copy.
7. Tell the user to rebuild the project.