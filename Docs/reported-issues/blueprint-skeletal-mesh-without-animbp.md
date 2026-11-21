# Blueprint 骨骼网格不能在没有 AnimBP 时单独配置

## 状态

**2026-11-21：已修复并部署到 NF 和 SkillTest。** 问题在 NodeFall T9 Lv0 装配讨论中发现，未向生产蓝图发起写入。

## 影响与原因

有些蓝图需要先绑定 Skeletal Mesh，等动画制作完成后再配 AnimBP；还有些蓝图只需要显示网格，从头到尾都不需要 AnimBP。UE 编辑器允许这两种状态。例如 T9 Lv0 的踏板蓝图可以先给继承的 `SkeletalTowerMesh` 挂上网格，`Anim Class` 保持为空，不应为了通过工具校验创建空 AnimBP。

`set_blueprint_skeletal_component_defaults` 已支持定位 C++ 继承、父蓝图继承及蓝图自有组件，但旧版校验拒绝 Mesh 和 Anim Class 恰有一项为空。在既有 `AnimationBlueprint` 模式下，若组件原本没有 Anim Class，仅请求设置 `mesh`，合并后的状态会触发 `Mesh and Anim Class must both be valid or explicitly cleared together`。旧 `set_component_property` 仅查目标蓝图自身的 SCS 节点，不能作为 T9 继承组件的替代入口。

这是[继承组件作者接口](fixed/blueprint-inherited-component-authoring.md)首版合同的剩余缺口，不影响已经验收的 Mesh 与 AnimBP 成对绑定。

## 建议合同与验收

- 在已支持的 `AnimationBlueprint` 模式下，允许有效的 Mesh 配空 Anim Class。请求省略 `anim_class` 时保留原值；需要从已有绑定切到仅网格时，允许显式传 `anim_class: null` 但保留 Mesh。不要自动创建 AnimBP，也不要自动切换动画模式。
- 若结果仍有 Anim Class，继续要求有效 Mesh、已编译 AnimBP、同一 Skeleton 等原有检查。换网格导致 Skeleton 不兼容时，预检和应用均应拒绝，不应自动解绑原 AnimBP。保留现有两个字段同时清空的行为。
- 用隔离的原生继承组件蓝图验证：初始 Mesh／Anim Class 皆空，只设置 Mesh，默认预检不写入；应用后编译、保存，独立进程读回 Mesh 非空而 Anim Class 为空，并确认生成的组件确实使用该 Mesh。再验证后来补设同 Skeleton 的 AnimBP，以及显式清空 Anim Class 而保留 Mesh。
- 回归已配 AnimBP 时的兼容换网格、不兼容拒绝、父级／兄弟蓝图隔离及未保存草稿保护。本反馈不扩大到 SingleNode、自定义动画模式或任意 native AnimInstance 类。

## 修复与部署验收

- [原生校验](../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPComponentDefaults.cpp#L319) 允许 Mesh 非空而 Anim Class 为空。省略字段保留原值，显式 `anim_class: null` 只清动画类；已有动画类仍要求同一 Skeleton。模式、权限和参数形状不变。
- 维护项目和 SkillTest 的 `UnrealMCP.ComponentDefaults.Write`、显式夹具的独立 `Reload` 均通过。覆盖只设 Mesh、补绑 AnimBP、兼容换网格、不兼容拒绝、解绑并保留 Mesh，以及冷读后生成实例的实际默认值。两项 Python 接口测试通过。
- SkillTest 正常启动于 `13091`，通过原有绑定 MCP 读回隔离蓝图。只换 Mesh 的预检成功，`anim_class` 仍为 `null`，未修改、编译或保存；前后 baseline 和脏状态相同。66 项受保护的配置、客户端和非目标源码等文件哈希不变。
- NF 首次构建因游戏模块 `NFTowerSlowTrap.cpp` 的 `UNFTowerAttackAbility` 类型不完整失败；随后的尝试被 Live Coding 锁拒绝。NF 补齐头文件并链接新 DLL 后，标准增量构建确认目标已是最新，原生 `Write` 和独立 `Reload` 通过。
- NF 正常启动于 `13090`，通过原有绑定 MCP 读回隔离蓝图。只换 Mesh 的预检保留空 Anim Class，未修改、编译或保存，baseline 和脏状态不变。69 项受保护文件哈希不变。
- 目标备份与回执批次为 `ComponentMeshOnly_20261121_124548434`。运行测试只写入 `__Dev` 夹具，未写入 T9 或其他生产蓝图。
