# AnimSequence Additive与Montage原生通知：受控读写能力缺口

日期：2026-11-03。状态：**有限专用合同已实现，已部署 SkillTest／NF；隔离作者验收与 NF 只读烟测通过，NF生产配置与游戏侧接入仍 Open。**
来源：NodeFall UE5.7.4的T3 Cannon Lv0／A正式接入。
[问题索引](README.md)

## 1. 业务目标与边界

用户已批准T3接入现有建造栏，本轮只做Lv0＋A，B下一轮。
Lv0复用单炮能力与纯后坐Montage；A要求蓄力后在Montage作者指定的通知点释放真实炮弹。
用户又确认：原目标死亡／越出射程／失效后，A仍完成蓄力，向原目标最后位置放炮并花正常发射冷却。
这些游戏规则由NF能力、瞄准及战斗状态机实现，不由MCP编辑器工具执行。

为了使正式保存资产与游戏执行一致，需要两项最小作者能力：

1. 将已存在的Lv0 Sequence改为经确认的Local Space Additive及其参考姿态设置。
2. 在已存在的A Montage中安全增量添加一个**已编译注册的原生AnimNotify类**，
   精确设置时间／轨道／BranchingPoint相关字段，并完整读取现有通知子对象的真实配置。

不能以每次游戏启动临时注入Notify／修改CDO／复制瞬态Montage代替正式资产配置，
也不能为绕过工具缺口新造项目资产配置commandlet、放开万能UObject写入或自动修引擎／插件。
本报告只请求有限工具能力，不授权修改NF动画、骨架、现有通知或选择生产平衡／发射时点。

## 2. 当前现场事实

UE5.7.4，项目`C:\04_unrealProjects\NodeFall\NodeFall.uproject`。
当前公开stdio工具表68项，含受控DataAsset写、Blueprint CDO写、Material／Niagara／UMG grouped工具、
Enhanced Input及String Table工具；**没有Animation／Montage／Notify作者入口**。
本轮Editor39808与13090归属一致，L_map01、Editor模式，仅读，未为检查关闭编辑器或启动新模拟。

| 目标 | 实际保存状态 |
|---|---|
| `/Game/Features/TowerDefense/T3_Cannon/Animations/A_T3_Cannon_Lv0_Attack` | 0.8秒；AdditiveAnimType=AAT_None、RefPoseType=ABPT_None、Frame0、RefPoseSeq=None |
| 同目录`A_NF_t3_cannon_lv3a_attack` | 2秒；AAT_LocalSpaceBase、ABPT_LocalAnimFrame、Frame0 |
| 同目录`AM_T3_Cannon_Lv0_Attack` | 原BlendIn／Out0.03，已有约0.042638秒Niagara／0.044507秒声音通知，必须保留 |
| 同目录`AM_NF_t3_cannon_lv3a_attack` | 2秒、BlendIn／Out0.25；已有约0.031838秒与1.349418秒两条原生PlaySound，尚未插入新的Cannon释放通知 |

新Cannon释放Notify的原生类和能力尚待NF实现／构建；本报告不把候选类名当作现已注册。
不能用已有`Tower Fire Hitscan`替代，它只转发Hitscan，不能为Cannon生成炮弹。
准确Gameplay释放时间也尚未冻结，不能凭后段声音时间自行宣告它就是用户批准的发射点。
B动画／ABP不属于本次作者目标，不顺带改它们。

## 3. 已有工具为何不能完成

已实际成功读取：

```json
{"tool":"get_class_properties","arguments":{
  "asset_path":"/Game/Features/TowerDefense/T3_Cannon/Animations/A_T3_Cannon_Lv0_Attack"
}}
{"tool":"get_class_properties","arguments":{
  "asset_path":"/Game/Features/TowerDefense/T3_Cannon/Animations/AM_NF_t3_cannon_lv3a_attack"
}}
```

通用实例反射可返回Additive枚举、Skeleton、SequenceLength、SlotAnimTracks和部分Notifies导出文本。
它不是完整的动画图／原生通知子对象结构读取合同：

- 部分Sequence／Montage的Notifies只有metadata没有value，不能解释为已证实空通知；
- Montage已暴露的Notify对象路径不等于返回PlaySound.Sound、
  Niagara.Template／Socket／Scale或游戏原生Notify字段；
- 缺少保存通知GUID、轨道index／名字、真实触发时间／偏移、分支点、
  概率／过滤／专服触发、子对象class／outer／typed值的完整可写基线。

`set_asset_properties`只允许DataAsset／指定PhysicalMaterial字段，
不允许任意UObject／Instanced对象遍历；AnimSequence和Montage不属于支持目标。
`set_blueprint_property`写的是Blueprint CDO，不是Animation资产或其Notify子对象。
通用限制应继续保留，不把生产动画投给已知不支持的writer试图“碰运气”，
也不把本缺口误报成已经发生的保存失败。

## 4. 最小专用合同建议

接口名称由维护侧决定。需要明确能力版本、实际native部署、dry-run及真实错误回执。

### P0：完整动画／通知读取

- 精确`/Game/...`资产路径与实际类，Skeleton、片段长度／RateScale、
  Additive类型、RefPoseType／RefPoseSeq／FrameIndex及范围。
- Montage的完整有序Slot／Segments、片段引用／起止／速率／循环数，
  BlendIn／Out及BlendOutTriggerTime，必要的Section链接。
- 完整有序Notifies：GUID、原index、真实track index／名字、
  时间／duration／trigger offsets、BranchingPoint／queued方式、
  概率／过滤／LOD／专服和Follower相关旗标；
  原生Notify／NotifyState真实class／path／outer、可读typed字段和子对象完整性。
- 无法支持的子类／对象／有界截断明确partial／incomplete，不能默认为空；
  不完整的目标scope不提供可盲写的baseline。
- 读不启动／改变动画播放、PIE、预览时间、全局画质、Skeleton／骨架兼容表或文件保存。

### P0：有限Additive编辑

- 只修改指定已有AnimSequence的确认字段：
  AdditiveAnimType、RefPoseType、RefPoseSeq、RefFrameIndex。
  提供typed枚举／引用／整数，不接受任意ImportText属性路径。
- 按真实UE版本核对LocalAnimFrame／AnimFrame等参考策略及所需字段；
  参考动画必须Skeleton兼容、Frame在真实采样范围，非法组合整体拒绝。
- 不默认把自己第0帧当正确中立姿态；这是项目作者的选择，工具只校验结构／范围。
- 未指定字段、原轨道／曲线、Skeleton／导入数据、RootMotion、Notify、RateScale保持。
  正确调用原生修改／缓存失效／派生数据更新／保存路径，不能只写枚举但播放仍用旧压缩结果。

### P0：Montage单条原生通知增量

- 已存在Montage＋完整expected_baseline，原生AnimNotify类完整路径且必须已注册、非抽象，
  为它创建**属于Montage正确Outer**的通知对象，不运行构造脚本／Blueprint Notify逻辑。
- 可以先只支持有限allowlist的无额外字段原生Notify及本项目已编译释放Notify；
  如何扩展allowlist应明确，不为游戏侧候选类硬编码不存在的假成功。
- 明确时间／轨道定位和必要的BranchingPoint等字段，完整预检后单事务添加；
  `CreateUniqueGuid`等原生身份与Link／Notify缓存更新一致，不把index当稳定ID。
- 对重复请求必须有可审查的idempotency语义：精确已存在的同一作者事件no-op，
  或显式拒绝／返回GUID；不能多次相同请求偷偷新增多个Gameplay释放事件。
  同时不能按class／时间随意合并作者本来需要的多个合法声音／VFX通知。
- 现有Slot／Segments／Sections／Blend／通知GUID及Sound／VFX子对象、轨道顺序全部保留；
  禁止清空Notifies再重新创建、改变所有通知时间或把其他Notify转为自定义类。
- 默认预览不写／不保存；dirty与stale保护，target-only可选保存。
  错误返回modified／saved／package_dirty／stage及完整after，保存失败不伪回滚，超时不自动重试。
- 本次不需要任意时间轴关键帧作者、Skeleton重定向、动画导入、NotifyState编辑、
  Blueprint执行或任意Instanced UObject子图写框架。

## 5. 验收矩阵

先用隔离测试动画／Montage，不改NF生产资源制造通过。

| 用例 | 预期 |
|---|---|
| 原Sequence非Additive→确认的Local Space／本地Frame0 | 预览无副作用、单次真实保存、冷读一致；实际采样姿态／播放使用新配置 |
| 有Sound／Niagara通知的Montage追加一个原生释放标记 | 精确新GUID／时间／轨道／分支旗标，原通知子对象及音画配置和所有轨道保持 |
| 真实原生Project Notify类 | 已编译加载可实例化；未注册／抽象／非法类在写前拒绝 |
| 增量重复请求 | 明确no-op／拒绝，不增加第二个发射标记；重名／同类多个合法事件不被擅自去重 |
| 错误时间／非finite／缺轨道／非法参考Frame／不兼容Skeleton | 整批预检拒绝，hash／dirty／缓存／原通知身份不变 |
| dirty目标／stale内容／意外并发编辑 | 不覆盖用户草稿、不Save All |
| 保存故障／超时 | 明确实际修改／未保存／未知状态，无盲重放 |
| Undo／Redo与独立冷加载 | 原事件及新事件Outer／GUID／引用／原始Blend、轨道与Segment信息保持 |
| 实际Montage播放 | 原生分支通知在指定时点、实例ID／Payload正确；不把“列表有GUID”当作会触发 |

游戏侧唯一释放、骨变换发布后真实枪口、发射快照／冷却及目标失效策略由NF另外验证。
工具修复／资产保存不能代替T3正式建造、索敌与攻击验收。

## 6. 证据与当前状态

本轮只读取并记录，未使用不存在的writer，未修改MCP／引擎或资产。
原始公开catalog及实例读取位于NF会话files：

```text
C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files
  t3-start-tool-catalog-1103-1117.json
  t3-production-preflight-1103-1117.json
  t3-production-preflight-receipt-1103-1117.json
  t3-current-integration-read-receipt-1103-0732.json
  t3-current-integration-1103-0732\A_T3_Cannon_Lv0_Attack.json
  t3-current-integration-1103-0732\AM_NF_t3_cannon_lv3a_attack.json
```

本报告是能力需求，不声称实现或部署已完成，也不授权新增任意通用写接口。
NF的A蓄力能力、正式塔／GA／DA、数值及实战装配仍是独立未完成工作。

## 7. 维护记录：2026-11-03 仓库实现与隔离验收

本节为仓库实现时状态；后续双项目部署见第 8 节。

以上保留 NF 原始现场事实。本节不代表原项目已加载新 DLL 或动画已经配置。
用户授权实现后新增三个直接 Asset 工具，完整参数见 [Animation Assets](../Tools/project_tools.md#animation-assets)：

- `read_animation_asset`：明确的 Sequence／Montage 作者 scope、Skeleton／源 DataModel 身份、
  Additive／采样范围、Slot／Segments／Sections／Blend、轨道及通知有序行、GUID、Link／偏移／
  概率与过滤、Notify／NotifyState 真实 class／path／outer／typed 字段。区分存储 TickType、
  native 强制标志和 effective BranchingPoint。不支持／有界／非有限数据明确 incomplete，
  baseline=null；不是原始骨曲线导出或任意动画对象重建器。
- `set_anim_sequence_additive`：仅四字段 typed patch，合并组合预检；LocalAnimFrame 与 AnimFrame
  分别检查目标／参考的真实 sampled keys。首版外部参考限非 Additive、同一个实际 Skeleton，
  不做 compatible Skeleton／重定向推断。拒绝自引用、相关草稿、非法组合及引擎隐式夹帧／清引用。
  一次事务、正式属性通知、目标派生数据完成、scope 读回核对后 target-only 可选保存。
- `add_montage_native_notify`：单个已注册原生类，明确 `track={index,name}`、时间／TickType／
  允许的事件字段、调用者新生成的 `event_guid`。GUID 直接成为实际事件身份；碰撞明确拒绝，
  不按 class／时间合并合法事件。仅支持显式 C++ class metadata
  `MCPAnimationNotifyAuthoringSafe="true"` 且无额外保存字段的 native UAnimNotify；
  项目类需另行编译／opt-in，未替 NF 构造假类。引擎库创建正确 Outer 并处理 Link／偏移／缓存；
  保留旧 GUID／子对象配置／相对顺序和整个时间轴 scope，排序会改旧顺序则预检拒绝。

默认 dry-run／不保存，完整 baseline、dirty／并发内容保护，写前要求资产编译队列空闲；
不完成无关资产编译，不保存无关用户草稿。完整 before／proposed／after、modified／saved／
dirty／stage 及溢出文件保持真实。保存失败可留已应用草稿，超时未知，无自动重试。
MCP 新参数为具体类型，未引入 Any／Optional／Union 参数；通用 DataAsset 限制和帧边界调度保留。

**实际验收：**仓库 UE5.7.4 构建成功；公开轨道参数修正后的最终完整 Python 85 项回归通过，
局部 21 项亦通过。原生 Read／Additive／Notify 明确 Success，独立进程指定根的 Reload Success。
三键骨轨道绝对位移末帧 10，在确认的本地 Frame0 后实际引擎采样为 delta 8，冷压缩采样仍为 8；
同 Skeleton 的 AnimFrame／AnimScaled 正例及非法 Frame／自引用／不同 Skeleton／脏相关依赖负例通过。
带原 Sound／Niagara Socket／Scale／音量／引用的 Montage 增量保留、Undo／Redo、GUID 重复拒绝、
类／轨道／时间负例与私有保存故障注入通过；FAnimMontageInstance 真正推进到分支点回调一次，
验证正确 MontageInstanceID／源资产 Payload，不是手工调用 Notify 冒充运行。

最终原生夹具：
`/Game/__Dev/Animation_C56C7521402A3DD88DEE7A802010873B` 与
`/Game/__Dev/AnimationNotify_DF3277224F0678CB4E4239A3F017038F`。
日志：仓库工程 `Saved/Logs/Animation-Matrix-Final.log`、`Animation-Cold-Final.log`。
无根 Reload 的默认跳过不计冷读证据。

真实公开 stdio→TCP 通过仓库工程临时 13092，在新 `__Dev` 副本读取、预览、真实保存参考帧 patch／
Notify 增量，验证非法帧、stale、Additive no-op、重复 GUID 拒绝及完整复读。来源 scope 未变，
续作前后来源／Config 文件 hash 一致，结束零脏包并正常关闭测试编辑器。
完整回执位于仓库工程
`Saved/AnimationPublic/073504cd7bbb44f6b79e0097f460ebcc/receipt.json`。

**失败与修正证据保留：**早期测试骨架 modifier 生命周期导致缓存未完成，复制动态 Montage 的
保存夹具长度需通过正式 API 同步；空 SoundWave 不是有效音频验收，改用引擎现成有效 Sound。
冷加载姿态要先等待目标加载触发的异步压缩，未以旧／零姿态替代验收。
公开调用首次在 FastMCP 顶层 JSON 预解析处把轨道名 `"1"` 转成整数，native Notify 尚未调用；
改为嵌套轨道身份并新增真实 Tool.run 验证后，从已保存 Additive 副本继续未执行的 Notify 步骤，
未重放已经成功的写入。原失败／原始响应仍保留，不把它们误记为生产保存故障。

**未执行／未授权：**不部署／重启／查询 NF 或 SkillTest，不修改生产 Sequence／Montage／Skeleton，
不增加项目 commandlet 或运行时注入；未 commit／push 本轮实现。
插件测试 marker 不是 Cannon 实现。NF 释放类、精确 Gameplay 时点、唯一炮弹释放、枪口／目标快照／
冷却和 T3 建造／战斗接入仍由 NF 另行完成，不能以本工具验收关闭这些游戏工作项。

## 8. 维护记录：2026-11-03 SkillTest／NF 部署

用户明确要求部署两个项目后执行。保持共享 Python、既有 authoring／分类权限，
未修改 `.mcp.json`／`.vscode/mcp.json`，未复制 Python 到目标工程。

### SkillTest

- 目标：`E:\04_UnrealProjects\UE5Test\SkillTest\SkillTest.uproject`，UE5.7.4。
  原本关闭；备份 `Saved/UnrealMCPBackups/Animation_20261103_143737`。
- SkillTest 尚无前轮 Enhanced Input，因此同时补其两份 helper 与依赖。
  本轮精确复制 Animation／EnhancedInput 四份 cpp／h；Bridge、Build.cs、uplugin、
  既有测试类型 header 仅加预期路由／依赖／原生 marker，去除新增文本后完全匹配备份。
- 首次构建被终端中断，批处理 Y/N 按用户回答 Y 结束；不记为源码失败或通过。
  确认无残留 UBT 后仅重跑构建，`SkillTestEditor Win64 Development` 明确 Success，355.15 秒。
- 部署后动画 Read／Additive／Notify 与 EnhancedInput Read／Append 五项原生矩阵通过。
  实际 delta 姿态、FAnimMontageInstance 回调／Payload、旧 Sound／Niagara、Undo／故障保护覆盖。
  两个新进程明确指定根的独立 Reload 均通过，不计默认无根跳过。
- 临时 GUI PID30316／13091 通过原 `.vscode` 配置的新公开 stdio 会话验证读／纯预览、
  stale／非法类拒绝、IMC reader 和窄 BP_ThirdPersonCharacter 读取；前后零脏包。
  验收后正常关闭，恢复原关闭状态；临时端口释放，默认配置仍 13090。
- 9965 个非目标原文件 hash 一致；11 个新 Content 文件仅在隔离 `__Dev`，未修改原资源。
  新文件来自原生验收夹具，不是 NF 生产迁移或业务配置。

实际 SkillTest 夹具根：
`/Game/__Dev/Animation_A89500FB4BA5F73E4A062F8E9D34E920`、
`/Game/__Dev/AnimationNotify_8C8CA7AF4D17353907CCEA81DD383CFE`、
`/Game/__Dev/EnhancedInput_1846DABA441CA6A22E30B2BB8C4CE3AC`。
日志：SkillTest `Saved/Logs/Animation-Deployment-Matrix.log`／`Animation-Deployment-Cold.log`
及对应 EnhancedInput 日志。公开回执和保护核对：
`Saved/UnrealMCPDeployment/Animation_20261103_144055/receipt.json`、`guard-verification.json`。

### NF

- 目标：`C:\04_unrealProjects\NodeFall\NodeFall.uproject`，UE5.7.4。
  最初 PID39808／13090、L_map01、Editor、零脏包。SkillTest 构建期间 NF 自行正常退出，
  日志 14:48:39 明确 Exiting；本次未向 NF 发送 close/save 请求，未 force-kill。
  后次只读预检因已关闭无响应，未把它当成功；以最新磁盘状态重新备份，保留期间用户改动。
- 备份：`Saved/UnrealMCPBackups/Animation_20261103_150207`，5599 个保护原文件。
  精确复制 Animation cpp／h，只在 NF Bridge／Build.cs／测试类型 header 加动画内容；
  uplugin／既有 EnhancedInput 未替换，DirectoryWatcher、导航／Tag／其他本地定制保留。
- 批处理终端结果不明确，改用引擎自带同一 dotnet／UBT；首次 up-to-date 不作为新 DLL 证明，
  再清理仅目标插件输出，全量 35 actions 明确 Success，72.87 秒。
  原生 `UnrealMCP.Animation.Read` 明确 Success，只用瞬态对象；未运行会创建生产项目夹具的作者测试。
- 恢复 L_map01、Editor 模式、最大化／NoLiveCoding，新 PID39240／127.0.0.1:13090，保持打开。
  PID 是本次有时间边界的证据，后续操作需重新核对。
- NF 原 `.mcp.json` 新公开 stdio 会话实际发现 71 个工具；四个生产动画均 contract1／complete、
  baseline 可用。Lv0 Sequence 仍 AAT_None／ABPT_None／Frame0，A Sequence 仍 LocalSpace／LocalFrame0；
  两 Montage 仍各原有两通知，Blend 仍 0.03／0.25，全部作者快照复读一致。
- 同设置 Additive dry-run 不改／不保存，stale apply/save 预检拒绝；Notify 非注册类预览拒绝，
  未以测试 marker／声音时间选择生产释放事件。窄 BP_NFPlayerController list/read 通过，前后零脏包。
- 文档前 5596 个非目标原文件 hash 一致，全部 4777 个 Content 文件名称／大小／时间及 hash 未变；
  未创建 NF 测试资产、写生产动画或编辑其 Skeleton。仅另更新指定 AI 交接目录记录／索引。

NF 原生日志：`Saved/Logs/Animation-Deployment-Read.log`；公开完整回执及保护证据：
`Saved/UnrealMCPDeployment/Animation_553f666196864d0ab57f6ee2e5f8d703/receipt.json`、
`guard-verification-before-docs.json`。

**客户端与剩余边界：**两项目 Claude 都仍 Pending approval；用户须分别批准并重连，
独立配置 stdio 验收不替代信任审批。SkillTest 再打开时须确认实际端口／项目身份，
不能把它的默认 13090 客户端误连为当前 NF。VS Code 使用共享 Python 的客户端须刷新 schema。
未管理目标 Git／ADO、未 commit／push。本次 NF 只读／预览证据不是 NF 实际写入、冷加载、
Gameplay 释放类／发射时点或 T3 实战验收；这些业务配置仍需独立授权与项目侧验证。
