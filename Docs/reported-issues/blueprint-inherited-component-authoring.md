# Blueprint继承组件：受控Mesh／Anim Class作者能力缺口

日期：2026-11-03。状态：**Open；NF T3用户选择先补齐此能力，再继续接入。**
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
