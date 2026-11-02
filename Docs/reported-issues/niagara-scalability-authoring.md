# Niagara Scalability 创建与配置写入缺口

日期：2026-11-01。状态：**有限受控 authoring 合同已实现并验收 SkillTest；已追加部署 NF。**
下文第1～8节保留修复前反馈与矩阵，实现验收见第9节，NF部署见第10节。
来源：NodeFall UE5.7.4，ADO [#140 首批VFX画质分层](https://dev.azure.com/BowenGame/UE5/_workitems/edit/140)，New／rev1。
这是新增写能力需求与现有合同不足，不是重新报告已修复的读取缺陷。

[问题索引](README.md) · [已修复的保存配置读取](niagara-scalability-reading.md) ·
[独立的外观参数绑定问题](niagara-vfx-appearance-parameter-binding.md)

## 1. 业务目标与最低范围

NF希望少量代表效果按Low／Medium／High减少次级碎粒、烟或灯光，同时保留攻击、命中、
状态与连接的关键信息。需要创建少量NiagaraEffectType、关联实际NS，
按System／Emitter／Renderer配置保存的平台范围和覆盖行，并完成预检、编译、保存及完整读回。

这不是将作者enabled总开关关闭，也不是修改伤害、冷却、状态时间。
具体类型、倍率、预算和保底视觉层由项目设计确认，MCP不替作者猜。
不要求第一阶段自动解析任意Profile最终eligibility或诊断运行实例裁剪。

## 2. 环境与已确认的现有能力

| 项目 | 实际事实 |
|---|---|
| UE | 5.7.4-51494982+++UE5+Release-5.7 |
| 使用项目 | `C:\04_unrealProjects\NodeFall\NodeFall.uproject` |
| Python server | `E:\04_projects\unreal-mcp-bo\Python`，默认grouped公开接口 |
| 本轮读取 | 44个配置／代码已接入的NS，均reader_contract=2、scalability_complete=true |
| EffectType | NET_Gameplay_Burst专用读取成功，三个尾迹的保存关联与规则保留 |
| 当前Niagara catalog | 完整分页共26个operation，没有下文的专用Scalability authoring入口 |
| 写入验证边界 | 本轮没有试写、创建EffectType、改变NS规则、切质量或运行PIE |

**读取修复已部署到NF，不再作为阻塞项。**
已有reader返回System／Emitter／Renderer Platforms、Overrides、EffectType、Handle／版本及完整性状态。
已有NS、模块、Renderer和MI编辑工具也仍可使用，但不能由此宣称新写合同已具备。

`set_niagara_renderer_property`有property_name＋字符串／ImportText的struct写路径及index选择。
因此Renderer并非完全不能写；只是缺少本需求的受控typed平台／条件、身份及安全保存合同，
本轮没有用正式资源执行该文本路径的Platforms写入验收。

## 3. 可复查的能力探测

通过现有公开stdio客户端：

```json
{"tool":"niagara_search","arguments":{"query":"","limit":10,"offset":0}}
{"tool":"niagara_search","arguments":{"query":"","limit":10,"offset":10}}
{"tool":"niagara_search","arguments":{"query":"","limit":10,"offset":20}}
{"tool":"niagara_search","arguments":{"tool":"modify_emitter_properties"}}
{"tool":"niagara_search","arguments":{"tool":"set_niagara_renderer_property"}}
{"tool":"niagara_search","arguments":{"tool":"read_niagara_system"}}
{"tool":"niagara_search","arguments":{"tool":"read_niagara_effect_type"}}
```

保存的catalog总数为26，Emitterwriter参数只有：
`asset_full_path`、`emitter_name`、`enabled`、`local_space`、`determinism`、`random_seed`、`sim_target`。
没有Platforms／ScalabilityOverrides／SpawnCountScale。

没有调用不存在的write名称、以错误参数探测生产writer、raw TCP或扩权限。
缺少专用入口的结论来自完整catalog／schema与native代码，不是零匹配或一次传输失败。

## 4. 必补项与优先级

这里P0表示阻塞自动化创建／配置，不表示所有手工UE操作都不可用。
P1可以一并纳入同一受控writer，不要求新建万能UObject写接口。

| 优先级 | 缺口 | 当前实际限制 | 最低修复目标 |
|---|---|---|---|
| P0 | EffectType创建 | `create_niagara_system`只创建NS；无EffectType factory；`create_data_asset`要求UDataAsset子类，EffectType父类Object | 精确新路径、防覆盖、明确class和created／saved／dirty状态 |
| P0 | EffectType规则编辑 | 无频率、CullReaction、Significance及System／Emitter设置行的专用write | typed patch完整行、顺序和开关／值；包含Platforms、Visibility、Proxy、BudgetScaling及真实ramp／曲线 |
| P0 | NS关联和System规则 | `set_asset_properties`限定DataAsset／PhysicalMaterial，不接收NS | EffectType引用／显式null解绑、System Platforms／覆盖行及LocalPlayer继承选项独立更新 |
| P0 | Emitter Platforms／Overrides | `modify_emitter_properties`只有前述固定字段 | 以当前Handle／所选版本修改saved Platforms和有序覆盖行，数量倍率对应flag和值一起处理 |
| P1 | Renderer平台规则 | 通用struct文本路径不是已验收的typed合同 | 相同typed Platforms patch，精确slot／对象；禁用Renderer不能过滤导致改错身份 |
| P1 | 预检、用户草稿及存盘 | 若干writer会自动compile／save；Emitter schema却说另外调用save_asset | 同步schema与native语义；preview／dry_run、dirty保护、save=false草稿或明确保存，不save_all |
| P1 | 编译／保存／部分失败结果 | WaitForCompilationComplete后保存不等于编译错误被报告 | 每资产validated／modified／compiled／saved及错误清单；已改未存须明确，不能成功形状回退 |
| P1 | 精确身份和并发基线 | 名字／首个同类型选择无法充分保护版本／重名对象 | 精确System路径、Handle／selected GUID、Renderer slot／path和expected内容基线；stale拒绝 |

### 规则数据不能丢失或误解释

- 保留原始signed QualityLevelMask和引擎实际质量名称／index，负数不是错误。
- ProfileName、Default／Enabled／Disabled三态和继承mask；CVar比较条件及Pass／Fail响应完整保留。
- 保存行顺序、覆盖／启用flag与数值一起处理，灰显保存值不宣称是effective覆盖。
- 本机BudgetScaling可以是StartX／StartY／EndX／EndY线性ramp，不一律套RuntimeFloatCurve.Keys。
  若真实结构有曲线，则保留keys、切线和外部引用。
- 未指定字段保持原值；null清引用显式，未知字段／非法类型不能静默补默认值。
- Stateless若不支持，明确unsupported，不能忽略或换成Stateful后返回成功。
- 不通过临时切全局质量／Profile／Editor preview实现所谓无副作用查询。

## 5. 建议的最小使用流程

**完整读取 → typed patch预检 → 明确执行 → 编译结果 → 选择保存 → 完整读回。**

创建／编辑接口名称由维护者决定，但应明确各层职责、能力版本和输入schema。
每个资产先完整验证再修改；保存失败返回真实修改和脏状态，不自动重放创建。
多包保存不能冒充一个原子事务；批次返回逐项结果，部分失败保留已发生的事实。
原有安全调度、帧边界执行和保存错误回执应保留，不为新功能绕过。

## 6. 修复验收矩阵

在明确隔离的fixture／测试资产中执行，不修改NF或Lyra生产样例制造测试结果。

| 用例 | 预期 |
|---|---|
| 创建EffectType、同路径再创建 | 首次存盘与冷读一致；再次拒绝，不覆盖 |
| NS绑定／解绑类型 | 引用读回准确；显式null解绑，未指定不改变 |
| 主层全档、装饰层高档 | Handle版本与Platforms正确；作者enabled不改，Renderer身份不丢 |
| Low行override+scale=true／0.1；High行两flag=false／1 | 完整有序行读回；不能把High灰显1说成强制覆盖 |
| -1／-29／-4和非空Profile／CVar | raw、decoded、三态、比较与响应均正确，不把Default写成false |
| Proxy／Visibility／Budget flag与值 | typed保留；非默认ramp或真实curve读写一致 |
| 错类型、未知字段、非finite／非法patch | 修改前拒绝，hash／dirty不变，错误含字段路径 |
| 用户已有dirty草稿 | 默认拒绝或明确获批处理，不能顺手自动保存 |
| 已变版本／内容基线／错误slot | stale拒绝，不盲写最新版或首个同类型对象 |
| compile／save失败、批次部分失败 | 各阶段和modified／saved事实正确，不能静默恢复为success |
| save=false／Undo／显式保存／另进程冷读 | 草稿不落盘；Undo边界清楚；最终保存真实存在 |

只读与dry_run前后：质量值／flags、Profile、preview、dirty集合、包hash不变。
写入仅批准目标改变，其他层、供应商原件和用户草稿有基线保护。
编译错误与保存错误分别验证，不以仅有文件、exit0或零匹配替代成功。

## 7. 可后补与非目标

- 任意上下文解析P2、实际组件裁剪诊断P3可以后补，不阻挡保存规则writer。
- metadata-only AssetRegistry类／引用分页对盘点有帮助，不是本项主阻塞。
- 同条件视觉预览及性能录制是独立验证能力；截图／reader不是GPU成本证明。
- 不要求工具自动推理任意模块是否消费SpawnCountScale。
  路径／版本／绑定事实归工具，算法及具体低档视觉方案归可追溯Knowledge与作者。
- [#141参数接收绑定](niagara-vfx-appearance-parameter-binding.md)单独报告，
  不将User变量authoring或万能图编辑扩成#140全部最低要求。
- 本次不要求调整NF的倍率、预算或玩法，也没有授权迁移／重写样例。

## 8. 原生定位与原始证据

仓库相关实现：

- [Niagara命令](../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPNiagaraCommands.cpp)：
  Emitter固定字段、Renderer属性反射、自动保存、按名字定位模块。
- [Asset写入约束](../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAssetProperties.cpp)：
  DataAsset创建／set目标类别，不能借用它写EffectType／NS。
- [Python Niagara工具](../../Python/tools/niagara_tools.py)：实际schema与grouped operation。

NF侧报告：
`C:\04_unrealProjects\NodeFall\Docs\AI\ZZ_AI_self_workingDir\MCPNiagaraScalabilityAuthoring140.md`。
原始本地只读结果：
`C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files\nf-vfx-parameter-audit-1101`，
包含44个NS完整JSON、contract与catalog、代表input／switch探测、材质读回和hash验证。
ADO live结果在其上级`ado140-parameter-and-scalability-gap-1101.json`。

本目录忽略的证据归档：
`evidence\2026-11-01\vfx-parameter-and-scalability\`，
保留catalog／合同、验证回执和SHA256 manifest；不把原始机器数据或崩溃日志纳入提交。
报告入库只创建／更新文档，不代表维护者已经实现、部署或验收以上writer。

## 9. 维护实现与验证：2026-11-01

新增 `read_niagara_authoring`、`create_niagara_effect_type`、`set_niagara_scalability`、
`save_niagara_authoring`，经现有 grouped 入口执行，`authoring_contract=1`。
范围包括System引用/null、Platforms/Overrides/本地玩家开关、选定Emitter规则、Renderer
Platforms、Effect Type设置行及Age/Distance/None子对象；不开放任意UObject反射写入。
字段级struct patch保留未指定内容；数组要求完整原始typed行，不接收解码mask注释。
默认dry_run=true/save=false，已有dirty拒绝、identity/content baseline过期拒绝，事务Undo、
脚本编译status/events和真实modified/compiled/saved/after回执；保存失败不回滚或重试。

详细合同见[工具文档](../Tools/niagara_tools.md#guarded-niagara-authoring)。
新增MCP参数使用具体类型，无Any/Optional/Union参数；列表默认None在函数体内处理。

- Python完整回归80项通过；grouped仍74/readonly31，direct155/readonly45，Niagara33项。
- 仓库原生 TypedFields、Scalability、Parameters通过；SkillTest同三项及既有Platforms、
  SavedRules、ReadCompatibility六项明确Success。覆盖负mask、非空Profile三态/CVar、
  完整ordered rows、inactive flag/value、dirty/stale、Undo、私有compile/save拒绝注入。
- SkillTest五个原生文件定点更新，保留其他定制；UE5.7.4构建330.03秒成功。
  备份 `Saved/UnrealMCPBackups/Authoring_20261101_092009`，9953个保护文件未变。
- 另进程冷读规则、User/Linked及材质引用通过；真实双组件MID的Scalar2/5、Color红/绿
  接收互相隔离，未改共享默认或材质。该测试属于#141接收机制，不是#140性能验收。
- 公开MCP只写新`__Dev`副本：创建预检/执行/碰撞、EffectType配置/NS关联、User草稿/dirty
  拒绝/显式保存、既有Linked no-op/异值拒绝、材质映射/完整读回全部通过；前后脏包0、
  原NS/材质与现有配置SHA256未变。成功回执为SkillTest本地
  `Saved/NiagaraAuthoringAcceptance/30daff68efb34bb2912ffe8d040e8f36/receipt.json`。
- 未完成的首次自动化日志和测试脚本错误结果保留；未把exit0或Schema可见当作通过。

本轮未部署NF、未改NF/Lyra生产资产或ADO，也未commit/push。
限于当前引擎反射支持的保存结构；自定义Significance设置、Stateless、跨包原子事务和
任意上下文/运行裁剪诊断未实现。规则保存允许不等于实际生成数量或具体Low/High视觉完成。

## 10. NF追加部署：2026-11-01

用户随后授权部署NF；只更新Bridge、Niagara Commands、Scalability及两个Authoring helper，
五文件与SkillTest已验收源码SHA256一致。保留NF本地导航、测试Tag、Build.cs及其他定制。
原会话零脏包后正常close_editor，saved_count=0；NodeFallEditor UE5.7.4构建成功，286.25秒。
恢复`/Game/Worlds/GameScene01/Maps/L_map01`，PID25536（历史值）、普通13090，现有配置不变。

NF现有stdio配置经公开grouped端点回读五NS／NET_Gameplay_Burst，contract2及完整层保留；
精确System／Module／Renderer authoring读取为contract1，七个新operation均已实际到达native。
创建／同值Scalability／User声明仅dry-run，陈旧保存基线／缺少User的module和material绑定
在preflight拒绝；未实际创建、修改、编译或保存NF资产。前后baseline、质量CVar／flags、
ActiveProfile及零脏包不变，BP_NFPlayerController窄列表／读取通过。

备份`Saved/UnrealMCPBackups/Authoring_NF_20261101_095757`；启动/烟测后5638个保护文件
哈希与4754个Content文件的数量／路径／大小／时间全部不变。随后仅给NF既有AI反馈文档
追加部署记录，未改生产资源、游戏源码、配置或非目标插件源码。
完整回执：NF本地`Saved/UnrealMCPDeployment/Authoring_20261101_cd296e29/receipt.json`。

实际write／冷读／双MID验收仍为第9节的SkillTest证据，不把NF预检冒充实际写入或视觉验收。
Claude仍为Pending approval，用户在NF客户端确认并重连；未绕过信任。本轮未commit/push。

## 11. NF客户端独立复核：用户2026-11-01 10:13反馈后

NF原stdio配置重新发现33个Niagara operation，七个新合同实际可达当前原生DLL。
NS_CannonImpact_Normal的System／Glow Emitter／Renderer／InitializeParticle模块与
NET_Gameplay_Burst精确读取通过；新建、关联、负mask、完整有序EffectType规则行及参数
声明／绑定只执行dry-run。碰撞、未知字段、缺失／错类型User、重复声明、材质类型冲突、
陈旧保存基线明确preflight拒绝，modified=false/saved=false。

原各层基线、脏包0、L_map01／Editor模式、质量CVar值/flags及Profile不变；
768个相关保存资产与Config哈希不变。没有实际写入、编译请求、保存或PIE，
不把NF预检扩写成实际写入／冷读／视觉或性能验收。
完整NF客户端回执：
`C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files\mcp-vfx-authoring-fixed-final-1013.json`。

125项合同／回执／不变性检查通过。首轮测试脚本误要求统一dry_run回显、
preflight failure的authoring_contract；原native合同无此承诺。保留两份红回执，
按真实请求、proposal和原生Failure的stage/modified/saved/error离线校正第二轮证据，
未重放写命令、未修改MCP代码；详细纠正链在最终回执和NF AI记录第10节。
主要工具阻塞解除，正式#138/#140资产适配另行实施；未更新ADO或commit/push。
