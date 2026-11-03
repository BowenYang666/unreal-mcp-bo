# Enhanced Input：Input Mapping Context读取／安全配置缺口

日期：2026-11-02。状态：**专用 reader／有限追加已实现并部署NF；NF五行业务映射已实际保存，负路径与重复no-op通过；菜单运行接入仍Open。**
来源：NodeFall UE5.7.4，ADO [#139 Effects画质底层入口](https://dev.azure.com/BowenGame/UE5/_workitems/edit/139)。
用户已授权为#139增加规范的CommonUI简易设置菜单，并选择Esc打开时暂停世界；
遇到映射配置阻塞后，明确要求先反馈MCP缺口，尽量减少手工配置。

[问题索引](README.md)

## 1. 业务目标与当前阻塞

CommonUI菜单需要真实的Enhanced Input Action与Input Mapping Context，
通过正常CommonUI路由处理返回、确认及暂停时输入，不增加临时按键轮询、
控制台设置UI、Gameplay Blueprint特判或临时运行时MapKey来规避资产作者能力。

本次需要的保存映射：

| Action资产 | 键的内部名称 | 用途 |
|---|---|---|
| `/Game/Features/UI/Input/IA_UI_Back` | `Escape` | 返回／关闭 |
| 同上 | `Gamepad_FaceButton_Right` | 手柄返回 |
| `/Game/Features/UI/Input/IA_UI_Accept` | `Enter` | 键盘确认 |
| 同上 | `SpaceBar` | 键盘确认 |
| 同上 | `Gamepad_FaceButton_Bottom` | 手柄确认 |

目标IMC：`/Game/Features/UI/Input/IMC_UI_Menu`。
两个Action是消费输入的Boolean，`bTriggerWhenPaused=true`。
本次只需要无Trigger／Modifier的简单映射；完整的任意重绑定系统不属于最低修复要求。
菜单、暂停／恢复、Esc上下文优先级仍由NF负责，MCP不替项目决定输入设计。

**实际阻塞：**可以创建IMC，但不能经当前公开接口读取完整可编辑映射合同并安全写入这五行。
此前已替用户打开IMC准备有限人工步骤；用户改为先反馈工具缺口，因此人工配置未作为已完成记录。

## 2. 环境与已经成功的操作

| 项目 | 实际事实 |
|---|---|
| UE | 5.7.4-51494982+++UE5+Release-5.7 |
| 项目 | `C:\04_unrealProjects\NodeFall\NodeFall.uproject` |
| 当前交互编辑器 | 本轮核对PID33100，13090归属同PID，Editor模式，L_map01 |
| 修改前未保存包 | 0 |
| Python服务 | `E:\04_projects\unreal-mcp-bo\Python`，公开stdio客户端 |
| 新建资产 | 两个InputAction及一个InputMappingContext均真实created／saved，包不脏 |
| InputAction有限字段 | `set_asset_properties`成功保存`bTriggerWhenPaused=true` |
| 当前IMC映射写入 | 未执行；structured reader已明确不可写，不试投生产writer绕过拒绝 |

用户之前另有保存的`NS_T2_ExtractorLink`改动，本轮不回滚／覆盖它。
本报告不声称NF菜单已完成、按键已生效或#139已关闭。

## 3. 可复查的公开接口与现场回执

创建已成功：

```json
{
  "tool": "create_data_asset",
  "arguments": {
    "asset_path": "/Game/Features/UI/Input/IMC_UI_Menu",
    "class_path": "/Script/EnhancedInput.InputMappingContext"
  }
}
```

读取：

```json
{
  "tool": "get_class_properties",
  "arguments": {
    "asset_path": "/Game/Features/UI/Input/IMC_UI_Menu",
    "structured": true
  }
}
```

当前NF native返回的关键部分：

```json
{
  "success": true,
  "class_path": "/Script/EnhancedInput.InputMappingContext",
  "properties": [
    {
      "path": "DefaultKeyMappings",
      "writable": false,
      "reason": "Unknown, noneditable or instanced property"
    },
    {
      "path": "MappingProfileOverrides",
      "writable": false,
      "reason": "Unknown, noneditable or instanced property"
    },
    {
      "path": "InputModeFilterOptions",
      "value": "UseProjectDefaultQuery",
      "writable": false,
      "reason": "InputModeFilterOptions: unsupported EditCondition 'ShouldShowInputModeQuery()'; expected a sibling bool or !bool"
    }
  ]
}
```

`InputModeQueryOverride`也因复杂EditCondition被拒绝。
本次五行配置不需要修改这两个输入模式字段，不能顺便放宽通用EditCondition。
`RegistrationTrackingMode`、`ContextDescription`可读／可写，不代表Mappings已获支持。

当前`set_asset_properties`合同明确：

- 限DataAsset／allowlisted PhysicalMaterial字段；
- whole struct要求全部字段；
- 不允许对象遍历、set或instanced对象；
- 不支持的属性在修改前拒绝。

**这些安全限制本身应保留。** 不应通过忽略Instanced字段或整批放开通用UObject反射来“修好”本问题。
本报告没有实际执行被拒绝的Mappings patch；证据是部署reader明确标注不可写及公开合同不具备支持，
不是将传输失败或未知命令误判为保存失败。

公开工具表中还有`create_input_mapping(action_name,key,input_type)`，
其合同是项目Action／Axis输入映射，没有`asset_path`、InputAction引用或IMC行身份。
不能把这个旧项目配置入口当作Enhanced Input资产写入工具，或用它生成第二套Legacy映射。
没有发现专门的IMC映射reader／writer。

## 4. UE5.7实际结构：不能只写旧Mappings

本机引擎源码：

- `Engine\Plugins\EnhancedInput\Source\EnhancedInput\Public\InputMappingContext.h`
- `Engine\Plugins\EnhancedInput\Source\EnhancedInput\Public\EnhancedActionKeyMapping.h`

UE5.7中的旧`UInputMappingContext::Mappings`已经deprecated。
正式保存结构是：

```text
DefaultKeyMappings : FInputMappingContextMappingData
  Mappings : TArray<FEnhancedActionKeyMapping>
MappingProfileOverrides : TMap<FString, FInputMappingContextMappingData>
```

`FEnhancedActionKeyMapping`除Action／Key外还包含有序Instanced Triggers／Modifiers、
SettingBehavior及可选Instanced PlayerMappableKeySettings。
`bShouldBeIgnored`／`bHasAlwaysTickTrigger`是运行时／重建的Transient状态，不应作为资产作者输入。
同Action允许多个Key，同Key也可能存在多个真实映射；不能用仅Action名称定位或擅自去重。
该struct本身没有可凭空假设的稳定GUID。

专用工具应基于本机当前引擎的正式编辑器／资产API处理默认行和Profile，
不要绕过封装盲写已弃用数组。修改保存资产与向活动LocalPlayer添加Mapping Context是两件事；
作者操作不应自动启动PIE、注入按键或修改当前运行时映射集。

## 5. 请求的最小受控能力

接口名称由维护侧决定，建议提供专用Enhanced Input读写合同，而不是放宽万能writer。

### P0：完整读取与简单映射增量

1. 以精确`/Game/...`路径读取IMC，返回类、默认映射有序行、Profile overrides存在情况、
   dirty状态、原始完整性状态和optimistic baseline。
2. 每行至少包含真实index／scope、Action完整路径、Key内部名称、
   Triggers／Modifiers是否为空及可读完整性、mappable设置行为／是否有自有对象。
   无法完整展开的复杂行必须明确unsupported／partial，不能默认为空。
3. 支持在指定默认scope增量追加本次这类简单Action＋Key行；验证真实InputAction类别、
   Boolean／pause等读取状态和有效FKey，保留所有未指定旧行、顺序及全部Profile overrides。
4. 禁止按Action一次覆盖所有Key。重复请求需有明确语义：精确已存在行可no-op，
   多个相同候选无法唯一定位时拒绝，不能按第一个或无条件追加导致双触发。
5. 对空白新IMC的五行原子预检／写入即可解除本次阻塞；
   不必先支持任意Trigger／Modifier创建、mappable profile重建或CommonUI全部作者功能。

### P0：安全执行与持久化

- dry-run默认不写／不保存；完整验证后一次事务，dirty／stale基线在写前拒绝。
- 编辑已有复杂映射时，未指定内容必须原样保持；无法保证则整批拒绝，不偷偷丢弃子对象。
- 精确修改可用baseline＋scope＋index＋预期Action／Key（及必要内容）保护；
  不能假设index天然跨重排稳定，也不能虚构mapping GUID。
- target-only可选保存，返回modified／saved／package_dirty／before／after及真实错误阶段；
  保存失败不虚报成功，不自动重放。保持既有帧边界调度、安全过滤与互斥。
- 不切输入模式／当前Profile，不自动将IMC应用到LocalPlayer，不改Action、
  Project Settings、用户映射存档或其他Gameplay IMC。
- 若提供删除／整组替换，须显式选择、完整预览及基线保护；本次并不需要这些扩展。

### P1：后续复杂字段

需要时再逐类支持标准Triggers／Modifiers／mappable设置的typed读写、复制／实例归属、
Profile scope编辑及复杂EditCondition。未支持时清晰拒绝，不以默认字段冒充完整等价复制。
不要为解除这一个菜单阻塞而实现任意UObject子图写框架。

## 6. 修复验收矩阵

先在隔离测试IMC／Actions中执行，不擅自改NF现有Gameplay输入。

| 用例 | 预期 |
|---|---|
| 空IMC追加五行菜单映射 | 5行真实Action／Key／顺序正确，dry-run无副作用，保存后冷读一致 |
| 同Action多个Key | Back的两Key、Accept的三Key同时保留，不相互覆盖 |
| 精确重复请求 | 明确no-op或按合同拒绝，不变10行／双触发 |
| 相同Action／Key的歧义旧行 | 显式行身份／基线保护，不能随意选首行或去重 |
| 旧行含Trigger／Modifier／自有Mappable对象 | 增量操作原样保留对象与顺序，或预检整体拒绝；不静默抹掉 |
| 非空Profile overrides | 默认scope操作不改Profile，完整性读取不能把它们漏报为空 |
| 错路径／非InputAction／非法Key／不完整输入 | 修改前拒绝，hash／dirty／行集合不变 |
| 用户已有未保存编辑／stale baseline | 拒绝，不保存用户其他草稿 |
| 保存失败／请求超时 | 明确实际修改与未知／未保存状态，不能自动重试 |
| 原生Undo／Redo、冷进程重载 | 完整行与对象归属一致；不写deprecated Mappings假通过 |
| 实际CommonUI菜单使用 | 正常暂停时Back／Accept可路由；无双返回／Gameplay误触发；关闭后移除菜单输入上下文 |

最后一行属于NF完成接入后的游戏侧验证，不应由“5行已保存”替代。
修复工具后需明确通知Python与native实际部署项目／版本，再经公开接口在NF独立验收；
只增加Python schema或在另一个项目通过，不代表NF已能用。

## 7. 本地证据与当前暂停点

以下保留 NF 原始反馈时的现场状态；仓库最新维护进度见第 8 节。

NF会话原始回执：

```text
C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files
  vfx139-public-tool-catalog-1102.json
  vfx139-input-assets-create-1102.json
  vfx139-input-assets-create-receipt-1102.json
  vfx139-input-actions-config-1102.json
  vfx139-input-actions-config-receipt-1102.json
  vfx139-initial-discovery-receipt-1102.json
```

本次不修改插件、Python工具或引擎，不放宽写入过滤，不新增资产配置commandlet，
不以临时运行时MapKey替代已保存IMC；不要求用户继续手工录入这五行。
NF设置服务只有未构建草稿，CommonUI菜单／接入／保存恢复／运行验证尚未完成。
需要维护侧补能力并部署后继续，不能以本报告存在或资产创建成功关闭游戏工作项。

## 8. 维护记录：2026-11-02 仓库实现与隔离验收

新增两个直接 Asset 工具，完整合同见 [Enhanced Input Assets](../Tools/project_tools.md#enhanced-input-assets)：

- `read_input_mapping_context(asset_path)`：正式默认行、实际 Profile overrides（含空 Profile）、
  有序 Trigger／Modifier、raw SettingBehavior／自有 Mappable、子对象 class／path／outer／typed 值、
  独立的 Action ValueType／暂停／消费及继承设置、context／dirty／baseline。
  有界／不支持／自定义子类明确 incomplete，不伪装空数组；不完整时 baseline=null、禁止写入。
- `append_input_mapping_context_mappings(asset_path,expected_baseline,mappings,dry_run=true,save=false)`：
  仅默认 scope 的简单 Action＋Key 增量；整批预检／一次事务／完整提案读回核对／target-only 可选保存。
  唯一等价简单行 no-op，歧义／复杂同键行／重复请求／非法输入／dirty／stale 拒绝，
  保留旧行顺序、子对象、全部 Profile 与其他字段。没有删除／替换／复杂子对象作者功能。

使用 UE5.7 正式 `DefaultKeyMappings`，不改 deprecated Mappings，不调用有运行时重建副作用的
`MapKey`；通用 DataAsset instanced／EditCondition 限制、帧边界调度和权限过滤均保留。
保存失败返回实际 modified／saved／dirty／stage／after；超时执行状态未知，无自动重试。
MCP 新参数使用具体类型，未引入 Any／Optional／Union 参数。

**实际验收：**仓库 MCPGameProject UE5.7.4 构建成功；完整 Python 83 项通过；
`UnrealMCP.EnhancedInput.Read`／`Append` 原生通过，独立进程明确指定夹具的 `Reload` 通过。
覆盖五行顺序／同 Action 多 Key／重复 no-op／歧义、坏批次原子拒绝、dirty／stale、
标准 Hold Trigger／Negate Modifier／自有 Mappable／非空和空 Profile 原样保留、Undo／Redo、
注入保存失败时内存修改真实回执和磁盘 hash 未变，以及超限 incomplete 拒写。
原生注入不是声称执行过真实操作系统写盘故障；超时不重试由 Python 故障测试覆盖。

最终原生夹具：`/Game/__Dev/EnhancedInput_D7A486F94F12182CAC7B8DA715C0984B`。
日志：仓库工程 `Saved/Logs/EnhancedInput-Matrix-Names.log`、`EnhancedInput-Cold-Names.log`。
另补超长 Key 在 FName 构造前拒绝、规范目标路径／重定向拒绝；溢出回执保留完整错误总数。
默认未指定 reload root 的批次跳过不计为冷读证据。

真实公开 stdio→TCP 通过仓库工程临时 13092：新建隔离 Actions／IMC、五行 preview／实际保存、
重复 no-op／stale 拒绝／同 Key 不同 Action；独立新进程完整冷读、坏批次原子拒绝和复杂
Profile 预览复核通过，测试前后零脏包，原生来源夹具／Config 保护 hash 未变。
公开回执：仓库工程 `Saved/EnhancedInputPublic/acda3d70890947e096ca5c2313e444f0/receipt.json`。
测试编辑器已正常关闭；未修改用户客户端配置／权限。

**部署边界：**本轮只构建／运行仓库工程，未复制／构建／重启 NF 或 SkillTest，未提交／推送。
NF 的两 Action／菜单 IMC／Gameplay 映射及其他用户资产均未操作。
共享 Python 重启后能看到新 schema，不代表 NF 的旧 DLL 已支持；仍需明确授权后部署并经 NF
公开接口独立验收。CommonUI 暂停 Back／Accept 路由、关闭移除 context 和 #139 完成仍未验证。

## 9. 维护记录：2026-11-02 NF 增量部署

用户明确要求“部署到NF”后执行。本节更新第 8 节当时的未部署状态，不回写原现场经过。
目标为 `C:\04_unrealProjects\NodeFall\NodeFall.uproject`，UE5.7.4。

- 备份：`Saved/UnrealMCPBackups/EnhancedInput_NF_20261102_210317`，保留旧源码／描述符／二进制、
  5580 个原文件保护 hash 和 4770 个 Content 文件清单。
- NF Bridge 与仓库已有差异，未整树／整文件覆盖。仅复制完全一致的新 EnhancedInput cpp／h，
  对 NF Bridge、Build.cs 和 uplugin 局部增加路由／错误回执／依赖。
  核对去除新增文本后与备份完全一致；保留 DirectoryWatcher、导航、Tag／migration 等本地定制。
- 原 PID33100 在零脏包预检后正常退出，日志明确 Editor shut down／Exiting；关闭脚本终端回执
  不明确时仅核实实际状态，没有 force-kill 或重放写入。未伪称取得 close_editor 成功回执。
- 清理仅目标插件 Binaries／Intermediate；`NodeFallEditor Win64 Development` 构建成功，
  43 actions，101.91 秒。构建亦编译了既有 NFEffectsSettings 草稿，未修改该游戏源码。
- 部署后原生 `UnrealMCP.EnhancedInput.Read` 明确 Success，只使用瞬态对象；未在 NF 运行会保存
  __Dev 夹具的 Append／Reload 作者测试。实际写入／冷读证据仍来自第 8 节仓库隔离验收。
- 新 NF 编辑器 PID37868（本次证据，后续需重新核对）恢复 L_map01、Editor 模式，
  `127.0.0.1:13090` 归属匹配，保持打开。既有 `.mcp.json`／权限未改，未新建客户端配置。

**真实公开验收：**通过 NF 原 `.mcp.json` 启动新的 stdio 会话，现有过滤下发现 68 个工具。
`read_input_mapping_context` 返回 contract1／complete／baseline；菜单 IMC **实际仍为 0 行、
0 Profile**。五行默认 dry-run 提案为 5 行，两个 Action 的 Boolean／consume／pause 旗标正确，
modified=false／saved=false。故意 stale 的 apply+save 请求在 preflight 拒绝，回执完整保留；
复读 baseline 与全部保存状态未变。通用 structured reader 仍拒写两个 instanced 映射字段，
窄范围 list/read `BP_NFPlayerController` 通过，开始与结束未保存包均为 0。

原生测试日志：NF `Saved/Logs/EnhancedInput-Deployment-Read.log`。
公开回执及完整响应：NF
`Saved/UnrealMCPDeployment/EnhancedInput_fa93e93a7f1e4942bccb0cb689cc57f0/receipt.json`。
文档记录前 5577 个非目标保护 hash 全部一致；4770 个 Content 名称／大小／时间及 hash 未变，
未创建测试资产、保存菜单映射或操作其他 Gameplay IMC。仅另更新用户指定 AI 交接目录的记录和索引。

**客户端与范围：**Claude 仍 Pending approval，用户需在 NF 目录批准 unrealMCP 并重连；
独立 stdio 验收不是绕过客户端信任。已有客户端需刷新共享 Python schema。
未部署 SkillTest，未管理 NF Git／ADO，未 commit／push。五行业务配置、CommonUI 暂停输入路由、
菜单关闭时移除 context 和 #139 游戏侧交付均未执行／验收，部署不等于业务工作项完成。

## 10. NF调用侧独立复核与五行业务保存：2026-11-02 21:16后

用户通知MCP修改完成，NF开发会话按原五行范围重新发现当前公开schema并调用新native。
现场为PID37868／13090同归属、L_map01、Editor模式，mapping_contract=1、
complete=true、baseline可用；不是仅凭共享Python工具表判断已部署。
首次完整读取仍为0行／0 Profile，两个Action仍为Boolean／consume／trigger-when-paused。

**实际执行：**

- 完整读取并审查五行默认dry-run提案，Action完整对象路径、FKey、index／顺序、
  空局部Triggers／Modifiers及InheritSettingsFromAction均正确。
- 非法Key预览整批拒绝；故意stale基线的apply＋save在预检拒绝。
  随后完整读取与IMC磁盘hash都保持原样，没有半写前四行。
- 在fresh基线下只对IMC_UI_Menu执行一次真实追加与target-only保存：
  modified=true、saved=true、package_dirty=false。
- 完整读回与先审查的proposed状态逐字段一致，实际5行／0 Profile，
  原Context字段／Action输入类型／暂停与消费旗标保持。
- fresh基线下同请求再次apply＋save为no-op：modified=false、added_indices为空、
  existing_indices为[0,1,2,3,4]；仍5行，没有重复到10行。
- 54份其他输入资产／项目Config／设置服务草稿及指定生产包hash保持不变。
  未保存用户草稿不保存／不覆盖，编辑器未关闭，未改LocalPlayer活动输入映射、
  CommonUI配置或Gameplay映射。

调用期间用户正在制作T3资源。最初调用侧脚本要求“全项目零脏包”，在真正写入前停下；
该轮receipt complete=false／applied=false保留。
确认脏包属于无关T3内容后，改为检查目标IMC／Actions无草稿，并要求前后用户脏包集合一致，
通过受控target-only保存继续，不要求用户Save All或关闭Editor。
成功批次保留的一份草稿是SC_cannon_gun_laser_charge；更早检查曾看到T3 Montage草稿，
二者是不同时间的真实用户编辑状态，不混称同一个稳定快照。

**证据：**NF session files的
`vfx139-imc-fixed-target-only-1102-2116\receipt.json`及同目录完整请求／响应；
提案`vfx139-fixed-preview-full-1102-2116.json`；
最后复读`vfx139-fixed-final-read-receipt-1102-2116.json`。
保存后基线`46E43E60DE236FDE37D855CFAAF960EAC3761CFE`。
IMC文件SHA256为`d51877d55a347d3673e6b21baa01952e531d80fc528867586f28049d09a2a882`。

**边界：**本轮未重启正在编辑且有用户草稿的NF，因此未增加NF独立冷进程读回证明；
冷读／复杂Profile／Undo／保存故障矩阵仍引用第8节仓库隔离验收，不冒充本轮新执行。
没有修改MCP实现、源码草稿或其他项目配置，没有commit／push／更新ADO。
五行输入资产配置阻塞已解除，用户不再需要手工录入；
CommonUI菜单、暂停时实际Back／Accept路由、关闭移除context和#139交付仍须后续接入验证。
