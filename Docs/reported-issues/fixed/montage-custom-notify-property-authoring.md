# Montage 自定义 Notify 参数：受控编辑能力缺口

日期：2026-11-13。状态：**有限参数合同已验收，并经授权构建部署NF／SkillTest；新DLL回归、冷读及绑定公开只读烟测通过。原始反馈及早期失败记录保留，未重放NF四层视觉。**
[问题索引](../README.md) · [既有 Additive／原生标记合同](animation-additive-montage-notify-authoring.md)

## 1. 实际操作场景

NodeFall UE 5.7.4 为 T5 狙击塔接入已选定的共享枪口 Niagara。
两份正式 Montage 已有用户手工添加的 `NF Play Niagara Appearance`：

```text
/Game/Features/TowerDefense/T5_sniper/Animations/AM_T5_sniper_lv0_fire_Montage
/Game/Features/TowerDefense/T5_sniper/Animations/AM_T5_sniper_lv3a_fire

Notify class: /Script/NodeFall.NFAnimNotify_PlayNiagaraAppearance
Niagara template:
/Game/Features/TowerDefense/T1_basic/VFX/NS_T1_TowerFire_muzzle_simple
```

共享 NS 已有 Float `User.Size`，默认 1，四层实际尺寸入口均消费该参数。
接下来需要代理修改**现有 Notify 实例的配置**，而不是新增 Gameplay 发射标记、
改共享 NS 默认值，或另加第二条 VFX 通知。

本次实际需要填入：

| Notify 属性 | 目标值／用途 |
| --- | --- |
| `Template` | 上述 `NiagaraSystem` 资产引用 |
| `SocketName` | `Muzzle` |
| `bAttached` | `true` |
| `bAbsoluteScale` | `true` |
| `Scale` | `(1,1,1)`，不再以组件缩放代替粒子尺寸参数 |
| `LocationOffset`／`RotationOffset` | 保持既有作者值；本例为零 |
| `Appearance.SizeParameter` | `User.Size`，FName |
| `Appearance.SizeMultiplier` | `0.1`，Float，按本次用户选择填写 |

未指定的 `Appearance.Colors`、亮度配置及其他字段必须保持。
用户已在当前会话确认“四层正常，已保存并停止编辑”。
后续实际读回：基础 Notify 已绑定 `User.Size`，用户选值为 **0.3**、组件 Scale1；
高阶仍为 `SizeParameter=None`、组件 Scale0.8，两份 Absolute Scale 均为 false。
因此上表是当时请求的配置／示例值，不能把用户的缩放确认扩大为“两份所有建议字段已填完”。
这证明手工参数路径可用，**不等于 MCP 已能完成参数写入或本报告已修复**。

用户要求将缺口反馈到本目录，希望下次由代理自动填写。
本次只记录问题和需求，不授权实现插件、部署、重启 UE 或改写生产 Montage。

## 2. 已观察到的公开读取结果

已执行的公开读取形状如下；这是已有回执的摘录，不是为报告重放写入：

```json
{
  "tool": "asset_call_read",
  "arguments": {
    "tool": "read_animation_asset",
    "arguments": {
      "asset_path": "/Game/Features/TowerDefense/T5_sniper/Animations/AM_T5_sniper_lv0_fire_Montage"
    }
  }
}
```

读取能返回 Notify 的实际 class／object path、所属 Montage、GUID、轨道、
时间和反射字段；其中能看到 `Template`、`SocketName`、`Scale` 及 `Appearance`。
但两份结果均为：

```json
{
  "complete": false,
  "unsupported_count": 1,
  "unsupported": [
    {
      "path": "notifies[1].Notify",
      "reason": "Custom notify may contain unreflected native data; fields are partial"
    }
  ],
  "semantics": "Stored animation authoring scope, not full raw animation reconstruction or runtime playback. No preview/play/save. Unsupported data blocks writes."
}
```

基础 Montage 的路径为 `notifies[1].Notify`，高阶为 `notifies[0].Notify`；
index 仅描述该次快照，**不能作为后续修改的稳定身份**。
这些回执保留了用户本次手工填 SizeParameter **之前**的配置，
不能据其中的 `None` 或 Scale 值断言用户手调之后仍未配置。

## 3. 为什么现有合同不能自动填

这是已知有限合同之外的能力需求，不是笼统的“MCP 完全不能编辑 Montage”：

- `read_animation_asset` 可读取当前可反射字段，但把这个项目自定义类标记为 partial；
  不能拿不完整快照当作安全可写的完整 baseline。
- `add_montage_native_notify` 是**新增**单个原生标记的合同，不是按 GUID 编辑已有
  Notify 对象的接口。其 `event_settings` 只接受 `FAnimNotifyEvent` 的过滤、
  触发概率、专服等有限事件字段，不接收 `Template`／`Scale`／`Appearance`。
- 该 Add 合同明确要求 native 类显式 opt-in，且不允许 `UAnimNotify` 之外的额外
  保存字段。仅给当前类加 `MCPAnimationNotifyAuthoringSafe=true` 也不能获得
  带参数的作者能力，不能靠修改 metadata 跳过安全边界。
- `set_anim_sequence_additive` 与此无关；通用 DataAsset writer 也不是任意
  Montage／Instanced Notify 对象属性编辑器。

维护源码核对位置：

```text
Python\tools\project_tools.py:196-233
  Add API、event_settings 白名单、无 custom object-field writer。

MCPGameProject\Plugins\UnrealMCP\Source\UnrealMCP\Private\Commands\UnrealMCPAnimationCommands.cpp:115-121
  项目自定义 Notify 的 partial 标记。

同文件:402-429
  Add 参数范围、native opt-in 与“无额外保存字段”限制。
```

源码核对不等于证明 NF 当前 DLL 与维护源码逐字一致；实际 NF 读取回执是独立证据。
本次没有构造一个不存在的修改请求，也没有记录“写入失败／保存失败”的实际调用，
不能把这些源码拒绝字符串描述成已经重放得到的错误。
目前可确认的是：**已用合同没有本例需要的受支持参数编辑路径**，
而不是调用者把 `User.Size`、Scale 类型或参数位置填错后导致运行失败。

## 4. 请求的最小能力

接口命名由维护侧决定。需要支持受控的 **Montage + 现有事件 GUID + Notify 属性 patch**：

- 精确资产路径和事件 GUID，同时校验预期 Notify class／Outer；拒绝找不到或重复 GUID。
- 对支持的类／属性提供 typed 读取与明确可写范围，涵盖本例的资产引用、FName、
  Float、Bool、Vector、Rotator 和有限嵌套结构体叶子。
  可用有限类适配或明确属性合同，不要求放开任意 UObject／Blueprint 执行；
  MCP 维护插件不应为此硬依赖 NodeFall 游戏模块。
- 默认预览、fresh baseline、dirty／并发保护；完整预检后单事务应用，
  正确属性通知／缓存更新、目标包可选保存、保存后精确读回。
- 只 patch 指定叶子。现有事件 GUID、顺序、时间／轨道／TickType、其他 Notify 的对象
  身份和配置、Slot／Segments／Sections／Blend／Skeleton／Sequence 引用全部保持。
  本例不需要自动对齐用户手调的 VFX 时间，更不能改唯一 Native Fire。
- 未支持或无法完整保护的内容继续 fail closed；不得仅把 `complete` 强改为 true，
  清空再重建全部 Notifies，或用 ImportText／私有 writer 绕过合同。
- 如果后续也支持创建带参数 Notify，应在同样的 typed／身份／预检边界内完成，
  而不是让用户先手工新建一个空 Notify 才能使用参数工具。

## 5. 验收要求

先用隔离 Montage 和具有同类额外保存字段的 native Notify 验收，不改 NF 生产资产制造通过。

| 用例 | 预期 |
| --- | --- |
| 按 GUID 修改已有 Notify 的 `Appearance.SizeParameter`／`SizeMultiplier` | 预览不脏；Apply 后读回准确；不新增第二个 Notify |
| Template／Socket／Scale／嵌套结构体叶子 patch | 引用与类型合法，未指定字段保持；不存在的字段明确拒绝 |
| 保留同 Montage 原生 Gameplay Fire 和其他声音／VFX | GUID、对象身份、时间／顺序／轨道／分支旗标及配置逐项保持 |
| 错 class／GUID／Outer、非法引用／类型、非有限值 | 写前拒绝，无部分修改或成功形状回退 |
| dirty／stale／并发编辑及同值重复请求 | 不覆盖用户草稿；同值 no-op 不增加事件、不无故变脏或 Save All |
| Undo／Redo、保存失败及独立冷加载 | 身份／配置保持；明确 modified／saved／package_dirty／stage，失败不伪回滚、超时不盲重放 |
| 实际 Montage 预览／播放 | 单条表现通知按预期触发；参数真的传给 Niagara，尺寸 1→0.1 时四层同步缩放；共享 NS 默认 1 保持 |

不能以 schema 存在、参数读回或工具返回 success 代替最后一项运行验证。
原 Additive／无额外字段 Native Notify 合同必须回归保持，不把本需求描述为旧功能全坏。

## 6. 证据与当前边界

本地原始读取、接线、编译和打开资产回执位于：

```text
C:\Users\15208\.copilot\session-state\d6ded13a-420e-400f-aa66-e6cee37736cd\files\shared-muzzle-user-size
  final-4.json                         基础 Montage，完整原始结构化读取
  final-5.json                         高阶 Montage，完整原始结构化读取
  verified-saved-size-contract.json    NS 四层 active Size 接收端；不代表 Notify 参数 writer
  handoff-0.json / 1.json / 2.json      已打开 NS 及两份 Montage
```

回执保留在原位置，不复制业务 uasset／DLL／日志进本目录。
报告写作阶段未访问或控制 UE，未修改插件、Python 工具、引擎或业务资产；
未构建／部署、创建测试地图、提交／推送代码或修改 ADO。
本报告为后续维护输入，**不声称修复或部署已完成**。

## 7. 维护实现早期进度（2026-11-13，当时未验收，已由第8节续作）

用户随后授权支持此能力。维护源码新增read_montage_notify_properties及
patch_montage_notify_properties，以Montage路径／已有事件GUID定位，写入要求
预期class／Outer及独立baseline。有限适配本报告原生类，并逐次校验字段形状，
没有硬链接NodeFall模块；其他直接原生类可明确opt-in同一合同。旧reader的partial
与Add无额外保存字段限制保持，没有修改NF源码、资产或部署插件。

接口支持Template／SocketName／bAttached／bAbsoluteScale／LocationOffset／
RotationOffset／Scale和Appearance的SizeParameter／SizeMultiplier，默认预览不保存。
暂存typed patch、GUID／Outer校验、脏包／过期拒绝、事务、未指定数据见证及目标保存
已接入源码；这些保护的实际应用、Undo、故障及冷读验收**还没有通过**，不能据代码
存在声称可交付。调用草案见[有限参数合同](../../Tools/project_tools.md#patch_montage_notify_properties)。

当前已完成：新接口缺失红例后转绿；完整104项Python测试通过，包含参数默认值、
分组／独立分类及只读权限、部分失败、完整溢出和超时未知／不重试；维护项目UHT、
原生编译及DLL链接通过。MCP参数使用具体类型，没有新增Any／Optional／Union参数。
工具计数为172原始操作／51只读，Asset24，分组仍57／只读29。

原生夹具首次裸建NiagaraSystem在保存时因缺脚本源触发断言，已改用仓库既有创建入口。
随后原生矩阵在首个preview被正确写前拒绝：game_thread=1、editor=1、PIE=0、
simulation=0、save=0、GC=0、slow_task=0、compiling=2、modal=0。夹具调用
FinishAllCompilation后仍有两个任务；读取前后数量一致断言未报错，不能归因于reader
新启动编译。更换非持久化内存见证也未解除该前置条件，因此未继续放宽writer保护。
待下一步定位具体编译管理器及测试准备时序，而非声称参数写入失败或已经修复。

当前最后日志为MCPGameProject/Saved/Logs/NotifyProperties-MemoryWitness.log；
先前失败及诊断日志保留为NotifyProperties-Matrix.log、Matrix2.log、Matrix3.log、
PreflightDiagnostic.log（均带NotifyProperties-前缀）。无参数Cold跳过不是冷读证据。
尚未完成实际apply／保存／独立冷读／公开MCP原生调用，也未验证四层视觉缩放。
本轮未连接／测试／部署／重启NF或SkillTest，未改生产Montage／共享NS，未commit／push。

## 8. 维护验收通过与部署授权（2026-11-13）

用户要求继续后，按实际管理器定位到残留任务为UE-NiagaraSystem=2。引擎聚合
FinishAllCompilation调用Niagara的SkipPendingOnDemand等待，并非完整刷新／收尾。
仅在自有隔离夹具中增加System.WaitForCompilationComplete(true,false)及一次
ProcessAsyncTasks(false)，零任务断言通过；writer的全局忙碌保护未放宽。

实际写入矩阵随后通过预览、非法末项／隐藏字段整批拒绝、类型／引用／非有限值／
class／Outer／GUID／stale／dirty保护、未指定字段及其他事件身份保持、Undo／Redo、
注入保存失败的磁盘字节不变及实际after、恢复保存与同值no-op。

运行夹具的手工栈Montage实例原先未注册到AnimInstance.MontageInstances，引擎在
第一个回调后发现实例列表为空即退出，不能归咎于writer缓存。改用有明确生命周期的
已注册实例后，Gameplay Fire及Appearance各触发一次；真实Niagara组件收到0.1，
共享系统默认User.Size仍为1。极短夹具关闭自动淡出，不改变生产Montage设置。
这证明隔离回调传参，**不等于验证NF四层视觉缩放或任意自定义Notify运行语义**。

独立进程以显式MCPNotifyPropertiesReload加载
/Game/__Dev/NotifyProperties_DE4464EE4F3673C9453C948720DAA668，完整快照、GUID、
class／Outer／配置、序列化见证及baseline全部与保存后相同且包保持干净。日志为
MCPGameProject/Saved/Logs/NotifyProperties-AuthoringGreen.log、-Cold.log。
NotifyProperties-Compatibility.log中的旧Read／Additive／Notify Add与新patch均通过；
未把无参数Cold跳过计作冷读证据。

维护项目独立13092的真实公开grouped MCP也通过：独立读取、默认预览、非法隐藏
末项拒绝后基线不变、明确apply/save将隔离项Size从0.1改为0.2、旧基线拒绝、同值
no-op。旧reader仍partial；完整overflow文件保留，地图／视口／dirty0前后不变。
证据在MCPGameProject/Saved/NotifyPropertiesPublic/4a47e093f7fa4eccbe29e7ecf37a2812，
receipt.json为passed；自有维护实例正常关闭saved0，13092释放。

随后用户明确“NF已经停止开发，搞完之后可以部署到NF和skilltest”。两目标原已关闭，
5.7项目和既有13090／13091严格绑定配置确认，权限未更改。部署批次为
NotifyProperties_20261113_173613，各项目Saved/UnrealMCPBackups下完整保留旧插件，
Saved/UnrealMCPDeployment下保留保护哈希、源码清单和构建日志。NF5672个、SkillTest
14724个原业务Content／Config／Source及客户端文件建立保护基线。NF的本地清理、
Tag、DirectoryWatcher及额外NavMesh测试保留；新目标不带旧Binaries／Intermediate。
本段只记录部署准备及源码切换，目标DLL构建、启动和公开烟测仍在进行，尚不宣称部署完成。

## 9. 双项目部署完成（2026-11-13）

NF保留本地定制后，首轮链接发现合并副本遗漏既有TextReload测试实现；补回维护
源码对应函数，未替换本地清理／运行期Tag逻辑。失败build.log保留，随后
NodeFallEditor构建成功（修复增量4.49秒）；SkillTestEditor全量35动作成功1298.15秒。
两个匹配DLL及SHA256记录在各项目部署目录build-receipt.json。

NF新DLL的Animation.Read／AssetProperties.Validation／Cleanup三项通过，确认
DirectoryWatcher、Tag、自清理和额外NavMesh源码保留。SkillTest新DLL的Read／
Additive／旧Notify Add／新NotifyProperties／StructArrayInitialization五项实际通过；
随后显式指定两个生成根，独立进程NotifyPropertiesCold及StructArrayInitializationReload
均通过。日志native-nf.log、native-skill-Matrix.log、native-skill-Cold.log保留完整记录。

两边通过正常交互方式启动，NF31240／13090、SkillTest28244／13091（仅为本次观测）。
原INI严格绑定、客户端权限及过滤未改。既有配置的全新stdio客户端返回正确项目，
NF49／SkillTest57个公开工具，实际Engine Cube、窄Blueprint内容、新Notify及数组
合同读取通过，地图／视口不变且dirty0。public-final/receipt.json均passed。

NF实际两份已保存T5 Montage取得完整独立基线。当前SizeMultiplier分别约0.3和1，
不是早先报告的0.1示例；DA_T5_Upgrades已有两项。部署只读取并保持这些现场值，
没有新增Notify、重填数组、调整时间或写共享NS。NF5672个、SkillTest14724个原保护
文件SHA全部不变；NF无新增Content，SkillTest只新增13个__Dev夹具。最终
protected-final.json和plugin-source-final.json留在各项目部署证据目录。

两编辑器留开；既有代理／MCP服务需重连以加载新Python注册表。独立配置stdio验收
不绕过客户端信任审批，也不等于验证所有Notify类型、NF四层视觉或Gameplay接入。
未commit／push，未修改引擎、RHI、业务源码或项目／客户端配置。
