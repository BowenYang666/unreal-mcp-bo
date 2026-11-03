# AnimSequence Additive与Montage原生通知：受控读写能力缺口

日期：2026-11-03。状态：**Open；NF现场catalog与实例读取确认，未实现／未验收修复。**
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
