# Historical TODO Notes

1. ~~有时候AI不知道我现在UE5.7的property面板有哪些东西。比如说创建的blendspace里面有一些属性AI不知道，或者是他记忆的是旧版本的property名称，新版本可能换名字了。~~

	**Resolved:** use `get_class_properties(class_name=...)` for reflected metadata and class-default values, or `get_class_properties(asset_path=...)` for a loaded asset's current reflected values. Specialized graph/hierarchy readers remain preferable where available.