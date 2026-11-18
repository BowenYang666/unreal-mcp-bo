# GA Blueprint CDO嵌套Map叶子属性：安全作者入口缺口

日期：2026-11-13。状态：**共用typed路径核心已验收并构建部署NF，生产值保持；SkillTest随后仅复制源码，按用户要求待其自行build，旧DLL未变。原始反馈保留。**
[问题索引](../README.md)

## 1. 结论与边界

不是“所有GA值都不能编辑”，也不是“资产不可读取”。

本例的GA是GameplayAbility的**Blueprint生成类默认对象（CDO）**，不是DataAsset实例。
旧Blueprint setter可定位CDO的**顶层**属性，但没有在本次核实的实现中解析
“结构体 → 整数键Map → 结构体 → float叶子”的路径。
已有structured DataAsset读写合同则有不同的目标类型边界，实际拒绝这个GA。

因此，无法用已核实的公开合同，安全地只修改已有Map项中的一个float；
整份结构体ExportText／ImportText回写不是已经证明无损的等价替代。
这属于**已确认工作流缺口／待维护核实的能力边界**，不预设必须新增工具，
不声称历史版本曾经支持，也不声称所有其他公开入口都已被排除。

## 2. 实际需求

NodeFall在工作项[172](https://dev.azure.com/BowenGame/UE5/_workitems/edit/172)
中添加T4攻击判定可视化FT，用户明确要求Lv3A拖拽范围遵循设计。

| 项目 | 实际对象／要求 |
| --- | --- |
| 引擎 | 现场回执为UE `5.7.4-51494982+++UE5+Release-5.7` |
| 资产 | `/Game/Features/TowerDefense/T4_AirPulseTower/GAS/GA_T4_Pulse` |
| 生成类／父类 | `GA_T4_Pulse_C`／`NFTowerAirPulseAbility` |
| 顶层属性 | `Stages`，`FNFTowerAirPulseStageTable` |
| 目标叶子 | `Stages → A → key 3 → Vortex → PullOuterRadiusFraction` |
| 目标更改 | `0.333333…` → `0.5` |
| 必须保留 | A3的`PullPeakRadiusFraction=0.3`、`CoreSlowRadiusFraction=0.5`，A4／A5／B、其他阶段字段和全部资源引用 |

上面是**语义路径**，不是声称现有setter支持的参数语法。
`3`是`TMap<int32, ...>`中的键，不是数组下标或第4个元素。
请求是修改已有可编辑叶子，不是添加／删除Map项、创建GA、修改父类或遍历任意UObject。

## 3. 已有公开调用及真实回执

### 3.1 Legacy反射读取成功

实际调用：

```text
get_class_properties(
  asset_path="/Game/Features/TowerDefense/T4_AirPulseTower/GAS/GA_T4_Pulse"
)
```

返回45项属性，包括：

```json
{
  "class": "GA_T4_Pulse_C",
  "parent_class": "NFTowerAirPulseAbility",
  "name": "Stages",
  "type": "struct (NFTowerAirPulseStageTable)",
  "editable": true,
  "blueprint_visible": true,
  "blueprint_read_only": true,
  "value": "(Base=...,A=((3,(...Vortex=(...PullOuterRadiusFraction=0.333333...))),...),B=...)"
}
```

这份摘录缩短了`value`，不是完整可回写文本。真实完整回执保留在原证据目录。
`blueprint_read_only`本身不证明属性在Class Defaults中不可编辑；
本例Editor确实允许修改，问题是已核实的MCP写路径。

### 3.2 Structured读取被目标类型门禁拒绝

实际请求：

```json
{
  "tool": "get_class_properties",
  "arguments": {
    "asset_path": "/Game/Features/TowerDefense/T4_AirPulseTower/GAS/GA_T4_Pulse",
    "structured": true,
    "property_paths": ["Stages"]
  }
}
```

真实回执：

```json
{
  "success": false,
  "message": "Target must be an existing DataAsset or PhysicalMaterial instance, not a class, redirector or other asset type"
}
```

这与现有DataAsset合同的类型限制一致，不能直接当作该reader内部故障。
本次没有用此拒绝结果声称legacy读取也失败。

### 3.3 Blueprint setter公开schema没有叶子路径合同

实际发现的入口为：

```text
blueprint_call_write(tool="set_blueprint_property", arguments=...)
```

返回的schema要求`blueprint_path`、`property_name`、`property_value`，
三者均为string，描述为设置Blueprint CDO属性。
schema没有说明Map键定位语法、typed叶子修改或非目标内容保留保证。

本次没有向生产资产提交伪造的点号／索引路径，没有试写整个`Stages`，
也没有为了反馈重放作者操作。下节路径限制来自源码核实，不是虚构的运行失败回执。

## 4. 已定位的实现原因

维护仓库检查时HEAD为`ea07667236ea2c9061afe9cb32d8f2964faef280`，但工作树已有其他维护改动；
以下依据**当时实际工作树源码**，不是声称该HEAD或NF DLL单独包含全部最新实现。
NF与维护仓库下列三个文件的完整SHA256逐项相同。
现场`plugin_version=1.0`不是精确构建指纹，不据此推定所有部署一致。

| 文件（均位于插件`Private\Commands`） | SHA256 |
| --- | --- |
| `UnrealMCPBlueprintCommands.cpp` | `E46FAC6A8C6A221372143B7759D38A16D52787FD00D509D00A01464D28079FB0` |
| `UnrealMCPCommonUtils.cpp` | `DD6DD860DD33188EE60E1E9434D837C526DF740ABDB7B3259870C52BC3DBC732` |
| `UnrealMCPAssetProperties.cpp` | `8A04975B32241DA8E258EE6F830ABABD1E58F1A3DE4FF517D2325BBAFDF7780A` |

1. [Blueprint handler](../../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp#L971)
   在971～1022行取得`Blueprint->GeneratedClass->GetDefaultObject()`，
   然后把整个`property_name`交给`SetObjectProperty`。
2. [属性定位](../../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPCommonUtils.cpp#L701)
   在710行使用`FindPropertyByName(*PropertyName)`，随后对CDO本身取属性地址；
   不是逐段解析Struct／Map键的resolver。将完整嵌套路径作为名称传入，不会自动变成叶子地址。
3. [通用string fallback](../../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPCommonUtils.cpp#L976)
   在976～985行对已定位的**整个属性**执行`ImportText_Direct`。
   将`property_name`设成`Stages`属于整结构体导入，不是只修改目标float。
4. [Structured目标门禁](../../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAssetProperties.cpp#L566)
   在566～578行仅接受既有DataAsset／指定PhysicalMaterial实例，
   并排除CDO／Archetype等目标，因此不能将其叶子合同直接套到GA Blueprint CDO。

### 为什么没有采用整Stages回写

读取的ExportText中float以六位小数出现，包括归一比例和风体分段尺寸。
六位小数不是任意float的无损往返编码；只改一个子串再Import整个`Stages`，
会同时重新解析未获准修改的A4／A5／B等值。

本次没有取得全部原始float位模式，也没有执行该替代写法，
所以**不声称已发生精度损坏，或每个数都会变化**；
结论是现有回执不足以证明该整表替代路径满足“其他值完全保持”的授权。
通过反射定位目标叶子并直接typed赋值，才不需要重新解析其他字段。

## 5. 用户可见影响与临时处理

- 一个float的修改需要转交用户在Class Defaults中手工操作，FT合并编译因保存读回门禁等待。
- 实际发生过相邻字段误操作：用户先改了`PullPeakRadiusFraction`，而`PullOuterRadiusFraction`
  仍未变，导致`Peak >= Outer`不符合原有参数校验。该差异被完整读回发现，没有放宽校验或当作通过。
- 后续手工纠正为Peak恢复`0.3`、Outer为`0.5`、CoreSlow保持`0.5`。
  最终完整反射读回相对最初preflight的唯一净差异为A3的Outer目标字段，
  A4／A5／B与其他属性保持；0draft及实际保存包身份已记录。
- 最终保存包SHA256为`640B12C1376C25CE8A7DBCCF8973E35DF54AAA2E701F8071FBB785F573D4403F`，
  40431 bytes。这是保存／读回证据，**不是独立冷加载或FT运行通过证明**。

人工处理是临时工作流，不是MCP修复。反馈作者没有修改插件、扩大writer范围、
整表覆盖业务配置或因此自行重建／重启UE。

## 6. 请求维护侧确认的最小合同

如果已有合法入口，请给出准确工具、目标类型、Map键／叶子路径语法及隔离示例，
并明确默认读取、structured读取与写入的适用范围。
不要求调用方猜点号路径或把业务整表ExportText作为作者协议。

若确需补能力，建议有限支持“已有Blueprint CDO中、普通结构体内、既有整数键Map项的可编辑数值叶子”：

- 明确Blueprint包／生成类／父类／CDO身份，禁止隐式改父类或兄弟Blueprint。
- 区分Map键与数组下标；不存在的键写前拒绝，不暗中新增、删除或重排容器。
- 仅定位并typed写入目标可编辑叶子；未指定成员、隐藏成员、Map其他项、资源引用及float位模式保持。
- 保持dirty／stale／权限／编辑器执行时机保护；整请求预检，使用标准事务及属性变更通知。
- 编译／保存失败要返回真实`modified`、`saved`、`stage`与目标脏状态；
  setter成功不等于持久化成功，不隐藏部分修改或自动重试结果未知的写入。
- 对只读、非法路径、错误键类型、错误叶子类型、对象所有权边界给出明确原因；
  不借本需求开放任意UObject深度遍历或复用不匹配的DataAsset合同。

建议维护侧在隔离GA／Blueprint夹具中验证：A3单叶子修改、A4／A5位模式不变、
父／子／兄弟隔离、错误键不扩容、dirty／stale拒绝、Undo、保存失败回执、
保存后独立冷读及真实实例消费。这里是验收建议，**不是授权试写NF生产GA或部署插件**。

## 7. 本机证据与未验证项

原始证据仍在本地会话目录，未复制进可提交文档：

```text
C:\Users\15208\.copilot\session-state\d6ded13a-420e-400f-aa66-e6cee37736cd\files\wi172-a3-core\
  preflight.json                  实际只读／schema发现请求
  preflight-2.json                初始完整legacy GA反射读回
  preflight-3.json                Blueprint setter公开schema
  preflight-4.json                structured目标类型拒绝
  capability-0.json               本次相关Blueprint搜索结果
  blocked-read-0.json             后续完整legacy读回及六位小数文本
  user-saved-precise-delta.json    相邻Peak误改的原始差异
  final\corrected-core-handoff.json
```

最后handoff SHA256：
`4D7F95BEB4931243D5A93804BCA868FD78DD8531C8954529CA193C725E173890`。

已执行：读取现有回执、公开schema及两仓库相关源码／身份核对；
未执行：本报告专用生产写请求、隔离writer复现、插件修改／构建／部署、
新接口回归或独立冷加载。GA不能编辑的笼统说法应以本报告的具体边界替代。

## 8. 共用路径核心实现与维护验收（2026-11-13）

用户随后要求修改并尽量避免同类组合再次缺口。维护侧没有按NF业务类名硬编码，
也未将GA伪装成DataAsset。四个入口共用路径resolver／typed codec／条件检查／
精确保护核心，Asset和Blueprint保持独立分类权限：

- read_asset_property_paths／patch_asset_property_paths：既有DataAsset／指定PhysicalMaterial；
- read_blueprint_property_paths／patch_blueprint_property_paths：普通Blueprint的生成类CDO。

调用步骤以显式类型区分字段、Map键和数组下标：

```json
{
  "blueprint_path": "/Game/Abilities/GA_Test",
  "paths": [["Stages", "A", {"key": 3}, "Vortex", "PullOuterRadiusFraction"]]
}
```

读取支持根／容器浏览、schema、实际Map键／数组数量、每段失败位置、可写范围及
实际class／CDO／Outer／parent／Blueprint GUID和独立baseline。写入要求同一选择
的baseline和changes，默认预览不保存。整数／Enum／Name／String键、已有数组
和普通自定义结构体可组合；点号和方括号也可作为String键的字面内容。
旧DataAsset字符串路径同样通过共用resolver适配，旧合同和独立权限不被扩大。

所有root先暂存，整批验证最终EditCondition、类型／范围、引用、重复／重叠、
dirty／stale和编辑器执行时机；Map缺键不插入，数组越界不扩容，不穿越UObject。
用原生二进制属性见证比较完整暂存root，未指定项、隐藏值、资源引用及float位模式
不经ExportText舍入重解析。单事务／属性链通知；Blueprint只编译目标并在重实例化
后重新取得CDO，读回成功才可保存目标。编译／保存失败返回实际已修改状态，
同值no-op不脏、不编译、不保存，超时不重试。已知存在派生Blueprint的目标明确
拒绝，避免把父默认值传播给未授权的子类；全层级编辑仍需独立合同。

这不是任意对象编辑器：隐藏／非编辑字段、容器增删／重排／整结构体替换、FText
历史、Animation／Widget／Level Blueprint及任意Instanced子对象仍有专用边界。
现有Notify专用合同没有被无条件放宽。旧Blueprint setter遇到嵌套名称会明确指向
新reader／patch，DataAsset目标类型拒绝也指出Blueprint CDO入口，避免只返回
笼统unsupported或建议整表ImportText。完整合同见
[共用typed路径](../../Tools/project_tools.md#typed-property-paths)和
[Blueprint CDO入口](../../Tools/blueprint_tools.md#read_blueprint_property_paths)。

维护项目隔离原生TypedLeaves矩阵通过：

- DataAsset和自定义Blueprint CDO均按A键3单叶子修改，A4／A5精确float位模式保持；
- 四种Map键和Struct／Array组合实际apply／保存；隐藏字段、缺键／错键类型、越界拒绝；
- 原生BlueprintReadOnly字段在可编辑Class Defaults中仍可写；父／兄弟默认值保持；
- 预览不脏、非法最后一项整批拒绝、Name大小写等价重复路径拒绝、对象边界不穿越；
- 最终暂存Sibling bool禁用条件不可绕过；Undo／Redo、编译失败及保存失败真实回执；
- 实际新对象消费编译保存后的CDO数值；显式独立进程冷读的baseline和完整root见证相同。

最终夹具根为/Game/__Dev/PropertyPaths_30F8D7A04C773A87672A4F8097F82D38；
日志MCPGameProject/Saved/Logs/PropertyPaths-MatrixFinal.log、-Cold.log。
第一次子类门禁误把AssetRegistry枚举中的查询类自身计为后代，已排除自身，真正父
Blueprint的后代拒绝用例保持。定点编译修正了UE头／GUID访问器以及测试缺头，
失败记录保留；最终DLL构建通过，不把单文件临时对象编译算作已链接DLL。

共享resolver影响范围的12项实际资产／文本／数组追加／动画／Notify／表面反馈回归，
以及既有标量／Blueprint持久化和动画的独立冷读均通过。首次回归脚本错用了旧测试
不接受的路径前缀，修正为AssetProperties_后通过，没有为测试放宽实现保护。
日志PropertyPaths-Compatibility.log与-CompatibilityCold.log。106项完整Python通过，
包括具体参数类型、默认值、独立分类／只读过滤、完整溢出／部分失败和超时未知不重试。
工具计数176原始／53只读，Asset26／Blueprint13，分组仍57／只读29。

实际公开grouped MCP在维护项目绑定13092分别执行Asset和Blueprint路径读／预览、
非法最后一项拒绝后基线不变、明确apply/save、旧基线拒绝和重复no-op，全部通过。
Map选择指向同一__Dev夹具，未触碰生产GA。证据目录为
MCPGameProject/Saved/PropertyPathsPublic/910514ad14e84ed89b5f0103cfd0b297，
receipt.json为passed，完整overflow回执保留。地图／视口／dirty0保持，自有维护
实例正常关闭saved0，13092释放。本轮未连接／修改／重启／部署NF或SkillTest，
未commit／push；共享Python新schema不代表目标旧DLL已有四个新入口。

## 9. 用户授权部署NF（2026-11-13）

用户随后明确“部署到NF”。预检时NF编辑器已关闭，UE5.7项目、原13090严格绑定和
既有客户端权限保持。以此前已验收插件的最终源指纹核对六个变化文件，NF本地
DirectoryWatcher、Tag、自清理、NavMesh及其他定制逐项保持。测试CPP使用已知
维护基线三方合并，没有整段覆盖本地代码。备份完整旧插件（含旧编译产物）至
Saved/UnrealMCPBackups/PropertyPaths_20261113_214418/UnrealMCP，替换的新源码树
不携带旧Binaries／Intermediate；NodeFallEditor构建／链接成功，用时70.04秒。

NF实际新DLL通过PropertyPaths.TypedLeaves、AssetProperties.Validation和Cleanup；
明确生成根/Game/__Dev/PropertyPaths_EFA3F54A457662A162D2C69C016B81C2的独立
PropertyPaths.Cold通过，未把无参数跳过计作冷读。新增Content仅四个__Dev夹具，
没有新建或替换正式GA。完整日志native-Matrix.log与native-Cold.log保留。

通过项目正常交互启动脚本恢复NF，观测PID19304／127.0.0.1:13090；此PID是本次
记录，不作为将来固定身份。使用原.mcp.json严格项目绑定的新stdio进程，49个公开
工具可见，Scene身份、Engine Cube、窄Blueprint列举及GA内容读取通过。新的
read_blueprint_property_paths实际返回GA_T4_Pulse的五个叶子完整typed数据和
baseline，A3 Outer标为可写；现场值分别为Outer0.5、Peak约0.3、CoreSlow0.5，
A4／A5 Outer均为0.33333298563957214。read_asset_property_paths读取T5已有
BaseLevels[0].Cost也完整且可写。全程只读，没有生产patch、preview、播放或保存。

原保护Content／Config／Source／uproject／客户端文件共5681个SHA256全部保持，
地图／视口不变且dirty0；新DLL指纹为
C55FD3940B3326232AD1379863C3FF08CFD62F462B1937B44634ADEEF6D1191F。
证据在NF的Saved/UnrealMCPDeployment/PropertyPaths_20261113_214418：plan.json、
build.log、public/receipt.json、protected-final.json和plugin-source-final.json。
NF编辑器留开；既有MCP客户端重连后才加载新Python注册表，独立stdio验收不绕过
客户端信任审批。未操作SkillTest、未改生产GA／父类／兄弟、未commit／push。

## 10. SkillTest仅复制源码（2026-11-13，待用户构建）

用户明确要求SkillTest“先copy过去，下次自己build”。据此只更新
E:/04_UnrealProjects/UE5Test/SkillTest/Plugins/UnrealMCP/Source，未build、未启动／
关闭／重启编辑器，未删除或替换Binaries／Intermediate。六个路径核心相关文件更新，
全部56个源码文件哈希与维护源一致；原源码经此前已知基线验证无本地新增修改。
旧Source和descriptor备份到Saved/UnrealMCPBackups/PropertyPaths_SourceOnly_20261113_215322。

现有DLL和uproject／DefaultEngine.ini／两种MCP客户端配置SHA保持，原权限／端口未改。
回执在Saved/UnrealMCPDeployment/PropertyPaths_SourceOnly_20261113_215322/receipt.json，
明确source_only=true、build_performed=false、native_deployment_verified=false。
这不是SkillTest新DLL已部署／已运行验收；下次用户构建SkillTestEditor Win64 Development
并重启对应UE／重连MCP后才加载新能力。NF当前插件及本地定制未在本轮复制中触碰，
未commit／push。
