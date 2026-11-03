# Blueprint继承组件：受控Mesh／Anim Class作者能力缺口

日期：2026-11-03。状态：**有限专用合同已在仓库实现并验收，已部署 NF；NF读取／纯预览烟测通过，生产装配与游戏侧接入仍 Open。**
来源：NodeFall UE5.7.4，T3 Cannon Lv0／A正式装配。
[问题索引](README.md)

## 1. 目标与授权边界

动画专用工具已补齐并通过NF独立读／预检，不是本问题的失败原因。
本次需要给两个继承`/Script/NodeFall.NFTowerBase`的生产塔BP配置其原生
`SkeletalTowerMesh`组件：匹配的SkeletalMesh与Anim Class。
它们是BP类的编辑器默认配置，不是临时场景Actor实例或游戏启动注入。

用户已确认现有建造栏、只做Lv0＋A、A蓄力后通知出膛；原目标失效仍向最后位置放炮。
本轮又批准A实际释放与既有约1.349418秒的开火音效对齐，并允许初版可调DA／GA数值。
用户**不批准新增T3 FT**，当前只接玩法并跑现有回归。
上述是NF游戏侧授权，不是MCP维护侧可写生产动画／选数值的许可。

给出两个BP各选Mesh／Anim Class的四项限定手工方案后，用户明确选择：
**先补齐继承组件MCP能力，再继续。**
本次不自行修改插件、放开万能UObject写入或新增项目资产配置commandlet。

## 2. 当前实际证据

目标工程`C:\04_unrealProjects\NodeFall\NodeFall.uproject`，UE5.7.4。
NF公开71工具合同中：

- `read_blueprint`可完整读出已有T1的`SkeletalTowerMesh`，明确
  `source="cpp_inherited"`、真实Mesh／Anim Class／局部Transform与组件父子关系。
- `set_component_property`公开说明及本地native实现均只查当前BP的SCS节点；
  原生／继承组件不在其支持范围。当前不应用其PhysMaterial特殊合同推导普通字段能力。
- `set_static_mesh_properties`同样只查当前SCS，并且不是SkeletalMesh作者入口。
- `set_blueprint_property`操作Actor CDO的直接FProperty；其底层
  `Object->GetClass()->FindPropertyByName(*PropertyName)`不是对象路径解析器。
  `SkeletalTowerMesh.SkeletalMesh`不会变成子对象属性；给`SkeletalTowerMesh`写对象引用
  也不是更改组件Mesh，更不能用组件指针替换或导出文本旁路来冒充配置。
- 通用`set_asset_properties`仍仅支持既有DataAsset／指定PhysicalMaterial范围；
  不授权遍历任意Instanced子对象。

真实只读实例：

```text
/Game/Features/TowerDefense/T1_basic/BP_t1_basic
parent_class_path=/Script/NodeFall.NFTowerBase
SkeletalTowerMesh: SkeletalMeshComponent, source=cpp_inherited
component outer/object:
  /Game/Features/TowerDefense/T1_basic/BP_t1_basic.Default__BP_t1_basic_C:SkeletalTowerMesh
Mesh:
  /Game/Features/TowerDefense/T1_basic/Meshes/SKM_NF_t1_basic_lv0
AnimClass:
  /Game/Features/TowerDefense/T1_basic/ABP/ABP_t1_basic.ABP_t1_basic_C
local rotation: Yaw180
```

`ANFTowerBase`构造器用`CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalTowerMesh"))`，
父组件TowerRoot，明确为原生组件，不应再创建一个同名SCS Mesh来绕过。

源码定位（NF已部署插件，不是维护侧新实现）：

```text
Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp
  HandleSetComponentProperty / HandleSetStaticMeshProperties: SCS-only
  HandleSetBlueprintProperty: delegates direct Actor CDO property write
Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPCommonUtils.cpp
  SetObjectProperty: direct FindPropertyByName
```

此为已确认的能力范围缺口，**不是声称执行过生产writer失败或保存失败**。
本轮只做公开读取，不向T1／T2试写、不创建生产T3 BP作为拒绝测试。
最后读取为Editor模式、L_map01、零未保存包；未关闭、重启或启动PIE。

## 3. 最小有限合同

接口名称由维护侧决定，可新增专用工具或受保护扩展；需要同时更新公开schema与native。

### 读取与定位

- 既有BP路径＋可稳定复核的组件身份，明确cpp_inherited／父BP inherited_scs／本地scs来源。
- 返回真实组件class、outer、声明者／当前编辑目标、有效默认值、可编辑字段／限制。
- 区分自身可写override与父类源模板，不按显示名称猜同名对象或随意拿父CDO。
- 明确完整性／baseline；不完整、歧义或不支持组件类型不可盲写。

### 首版作者范围

- 可先限制`USkeletalMeshComponent`的Mesh、AnimationMode、Anim Class。
  使用匹配UE5.7正式setter／编辑器通知／组件模板及BP编译流程，具体参数名以公开schema为准。
- 引用为typed完整对象／生成类路径；Anim Class必须真正为合适的UAnimInstance派生类，
  Mesh／AnimBP Skeleton兼容性及AnimationMode组合在写前检查，不接受错误资产类型。
- 对**当前子BP自己的有效默认组件**操作。若是父BP的SCS组件，按UE标准override机制
  创建／更新当前子BP override；若是原生组件，必须保留子对象身份、native类／根与附着关系。
- 不能写父BP／原生父CDO、不能给所有子类共用模板改值、不能换组件指针或
  新增同名SCS副本、不能通过Construction Script／运行时BeginPlay设置来替代保存默认值。
- 其他Material／Transform／Collision／Sockets／EventGraph／父类配置及未指定字段保持。
  本轮不要求任意组件字段／骨架重定向／AnimGraph编辑／Construction Script执行框架。

### 写入安全

- 默认dry-run、不保存；完整expected_baseline／expected values，dirty／stale／并发保护。
- 全请求预检，实际写入只一个事务；正确编译、实例化重建与目标组件读回。
- target-only保存，明确modified／compiled／saved／package_dirty／stage及完整after。
  不Save All，不保存父BP或其他用户草稿；保存失败保留真实已修改状态，超时未知且不盲重放。
- Undo／Redo、冷读、实际spawn的组件应一致；仅“CDO字符串显示已改”不足以验收。
- 不绕过当前客户端信任／分类权限，不用raw TCP或任意对象写fallback。

## 4. 先隔离验收，再部署NF

| 用例 | 期望 |
|---|---|
| 原生SkeletalMesh组件子BP | 实际Mesh＋AnimationMode＋Anim Class预览、编译、保存、独立冷读；spawn后正确 |
| 父BP的SCS组件／多级子BP | override属于目标子BP；父BP和兄弟BP的有效值、资产hash不变 |
| 重名／不存在／不支持来源组件 | 写前明确拒绝，不新增同名组件或换父类／指针 |
| 错Mesh类型／错误Anim Class／Skeleton不兼容／非法组合 | 整批拒绝，hash／dirty／旧配置不变 |
| dirty目标／stale baseline／忙碌编译 | 不覆盖用户草稿，不完成无关资产编译 |
| 同值、重复请求 | 明确no-op，不重复建override、组件或无谓重写父资产 |
| Undo／Redo／保存故障／超时 | 状态真实；保存故障不伪回滚，未知写入不自动重试 |
| 保护既有SCS接口 | 本地SCS正常作者、PhysMaterial特殊合同等既有行为不回归 |

NF只读烟测可用上述T1 BP，但不得写它来获得通过。真实写验收先用维护／SkillTest隔离夹具。
部署NF时保留Animation／Enhanced Input以及本地DirectoryWatcher／导航／Tag定制；
明确DLL加载项目／端口／PID、已保存生产资产保护和用户未保存状态。
工具补齐不等于批准修改生产T3资产，更不代表T3蓄力／炮弹已完成。

## 5. NF恢复点

本轮未构建的候选原生改动已隔离到session files，并只撤回本轮自己的9份原文件改动与6份新文件。
正式`Source\NodeFall\TowerDefense`相对本轮开始状态无差异；
没有创建塔BP／GA／DA、改动画、改建造Loadout、开新FT、编译／重启或commit／push／ADO写入。
候选源码未构建／未验收，不能直接当成完成实现重放。

```text
NF session files:
C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files
  t3-inherited-component-preflight-receipt-1103.json
  t3-inherited-component-inspection-1103/
  t3-charge-wip-paused-20261103-170238/
    tracked-source.patch
    NFTowerChargedCannonAbility.h/.cpp
    NFTowerCannonReleaseNotify.h/.cpp
    NFTowerAnimationValidation.h/.cpp
```

后续fresh读取正式Lv0／A Mesh、ABP、Montage和目标BP存在性，再按已批准建造栏／LastPosition政策继续。
准确的Skeleton／姿态／Socket校验、唯一释放、发射时快照与GAS冷却仍属NF游戏侧工作。

## 6. 维护记录：2026-11-03 仓库实现与隔离验收

本节为仓库实现时状态；后续 NF 部署见第 7 节。

以上保留 NF 原始需求／恢复点，不代表已操作生产 BP 或恢复候选游戏源码。
用户授权修复后，新增两个直接 Blueprint 分类工具，合同见
[Skeletal Component Defaults](../Tools/blueprint_tools.md#skeletal-component-defaults)：

- `read_blueprint_component_defaults`：准确区分 cpp_inherited／inherited_scs／local_scs，
  selector 使用声明类、名字与原 SCS GUID；返回实际模板／Outer／archetype／目标 override、
  三字段 typed 默认值、hierarchy／相关保存身份、有界只读保存字段见证、complete／baseline。
  不创建 override／handler，不编译、spawn 或保存。旧 read_blueprint 标签不作为可写身份。
- `set_blueprint_skeletal_component_defaults`：仅 Mesh／AnimationMode／Anim Class 的合并请求，
  默认预览／不保存。首版限精确且可编辑的 USkeletalMeshComponent、已有 AnimationBlueprint 模式，
  Mesh／生成 AnimBlueprint Class 使用同一实际 Skeleton，或同时明确清空两引用；
  不支持 SingleNode／custom 模式转换、任意派生组件、native AnimInstance 任意类或兼容骨架推断。
  拒绝 wrong type／组合、脏或未编译目标／父BP／相关引用、stale／歧义身份／忙碌编译。

原生分支只改目标子 CDO 自有默认子对象；父 SCS 分支通过 **Blueprint 源 handler** 的标准
override 创建／更新目标模板，本地 SCS 保持自己模板。实际数据改动一个事务，正式 setter／通知后
在事务外编译目标，编译后重新定位，不引用旧重实例化模板。核对未指定组件／Actor保存字段、
本地SCS结构、父级scope未变后才 target-only 可选保存；无父模板写入、无同名副本／指针替换，
不使用 ConstructionScript／BeginPlay 注入。modified／compiled／saved／dirty／stage／完整after
真实保留；保存故障可留已应用草稿，超时未知，无自动重试。通用对象限制与旧 SCS／PhysMaterial 不改。
MCP 新参数为具体类型，未引入 Any／Optional／Union 参数。

**实际验证：**仓库 UE5.7.4 构建成功；完整 Python 87 项通过，局部23项通过。
原生 Read／Write 明确 Success，独立进程明确指定根的 Reload Success；默认无根跳过不计冷读。
隔离 Character 子BP＋父SCS／多级子BP／本地SCS三来源实际保存、编译后值、父／中间父／兄弟BP
完整scope及hash保持、两条继承路径 Undo／Redo、故障注入磁盘未变、stale／错引用／不同Skeleton／
非法模式／身份等负例通过。真实热／冷进程 spawn 后三 SkeletalMeshComponents 的 Mesh／Anim Class
均符合保存值，无新增重复组件，不只看 CDO 文本。旧 `UnrealMCP.SurfaceFeedback.EndToEnd`
本地SCS／PhysMaterial端到端回归通过。本次未新增任何 NF 游戏 Functional Test。

原生最终夹具：`/Game/__Dev/ComponentDefaultsWrite_04637CAE44D7E3AE3451809B1C8BD1C3`。
仓库工程日志：`Saved/Logs/ComponentDefaults-Matrix-ValidNegative.log`、`ComponentDefaults-Cold.log`、
`ComponentDefaults-LegacySurfaceRegression.log`。完整冷读oracle留于 `Saved/ComponentDefaultsAcceptance`。

真实公开 stdio→TCP 通过仓库工程临时13092：只复制新隔离 BP，在副本上对 native／父 SCS
分别做纯预览、真实编译／保存明确清空组合、重复 no-op 与 stale 拒绝，目标读回与回执一致；
来源／父兄弟／Config hash保持，结束零脏包并正常关闭测试编辑器。
公开回执及完整响应：仓库工程
`Saved/ComponentDefaultsPublic/1a609c39f8f94fdcb352d12af9475c8e/receipt.json`。

**失败／修正保留：**早期空几何 Mesh 复制及世界重复初始化是隔离夹具缺陷，未伪报写入通过；
引擎保存字段 `SkeletalMesh` 被明确归入 Mesh 作者scope，其他字段见证仍严格比较。
生成类 handler 单独创建未登记 Blueprint 源，编译后 override 丢失时工具正确拒绝保存；
经用户确认继续排查，改用 `UBlueprint::GetInheritableComponentHandler(true)` 源入口。
将编译／生成类布局排除于数据事务后，两路径Undo稳定；不同Skeleton负例使用有效但不兼容资产，
未屏蔽资产校验错误或降低验收。原失败日志保留，不把它们说成 NF 生产保存事故。

**尚未执行：**未部署／重启／查询 NF 或 SkillTest，未试写 T1／T2、创建生产 T3 BP 或新NF FT，
未改 Loadout／动画／Skeleton／GA／DA、未恢复session候选源码、未 commit／push 本轮实现。
NF 类／姿态／Socket、炮弹／冷却及 T3 正式装配仍需独立授权与游戏侧验证，工具验收不替代它们。

## 7. 维护记录：2026-11-03 NF增量部署

用户明确要求“部署到NF”后执行。目标 `C:\04_unrealProjects\NodeFall\NodeFall.uproject`，
UE5.7.4；源码仍来自维护仓库共享 Python 和 UnrealMCP 插件，没有复制 Python 到 NF。

- 公开预检确认原 PID39240／13090、L_map01、Editor 模式、零未保存包与空闲编译／导入。
  备份 `Saved/UnrealMCPBackups/ComponentDefaults_20261103_184447`，5603 个原文件保护 hash，
  4777 个 Content 文件清单，含源码／配置／客户端与既有本地插件定制。
- 再次零脏包检查后按项目流程正常 close_editor(save_all=true)，回执 closing=true、saved_count=0；
  等待实际退出／端口释放，全部5603保护 hash 未变后才补丁，没有 force-kill。
- 精确复制两份 ComponentDefaults cpp／h，仅在 NF Bridge 添加 include、两路由与失败回执条件。
  去除新增文本后完全匹配备份；Build.cs／uplugin／Animation／EnhancedInput／DirectoryWatcher／
  导航／Tag 等其他文件未替换，未覆盖游戏源码或用户改动。
- 清理仅目标插件 Binaries／Intermediate；`NodeFallEditor Win64 Development` 明确 Success，
  109 actions，274.36 秒。构建同时编译现有游戏源码，本轮未编辑它们，不代表 T3 游戏验收。
- 部署后原生 `UnrealMCP.ComponentDefaults.Read` 明确 Success，只生成未保存隔离对象；
  未在 NF 运行保存作者夹具的 Write／Reload，不新增 NF FT 或测试资产。
- 恢复 L_map01、Editor、最大化／NoLiveCoding，PID23052／127.0.0.1:13090 匹配并保持打开。
  PID 为本次有时间边界的证据，后续需重新核对。`.mcp.json`／既有权限保持不变。

**实际公开烟测：**通过 NF 原 `.mcp.json` 的新 stdio 会话发现73项（现有分类过滤下），
两项专用工具真实调用到新 native。T1
`/Game/Features/TowerDefense/T1_basic/BP_t1_basic` 返回 contract1／complete／baseline：
`SkeletalTowerMesh` 的 source=cpp_inherited、declaring_class=/Script/NodeFall.NFTowerBase，
实际 Outer 为目标 Default__BP_t1_basic_C；Mesh／ABP生成类／AnimationBlueprint 模式准确。
同值 dry-run 回执 would_modify=false／modified=false／compiled=false／saved=false；
故意 stale 的 apply+save 请求在 preflight 拒绝，仍全部 false。复读完整scope／baseline 一致，
窄范围 BP_NFPlayerController list/read 通过，开始与结束未保存包均0。
没有对 T1／T2／T3 作实际 apply、编译、保存或新增 override，未选择或修改业务数值／时点。

原生测试日志：NF `Saved/Logs/ComponentDefaults-Deployment-Read.log`。
公开回执／全量原始响应／构建日志／保护证据：
`Saved/UnrealMCPDeployment/ComponentDefaults_d6b5e93ab2cf4a1f89fbaa48a3cb9292/receipt.json`、
`guard-verification-before-docs.json`。文档前5602个非目标原文件 hash 一致；
全部4777个Content名称／大小／时间及hash未变，两份helper与已验证仓库源码一致。
另外仅更新 NF 指定 AI 目录已有继承组件交接／索引，不改正式设计或业务恢复点。

**客户端与范围：**Claude 仍 Pending approval，用户需在 NF 目录批准 unrealMCP 并重连；
已有客户端需刷新共享 Python schema，独立配置 stdio 不绕过客户端信任。
本轮未部署 SkillTest、未管理 NF Git／ADO、未 commit／push。
父 SCS 真实写入、Undo／独立冷读／spawn 证据仍来自第6节仓库隔离验收，未冒充本次 NF 执行。
候选源码和已批准游戏方案仍按原恢复点接续；工具部署不是批准重放草稿、修改生产 BP 或交付 T3。
