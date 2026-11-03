# DataAsset FText：String Table引用的受控作者缺口

日期：2026-11-03。状态：**维护侧已修复；NF已部署并通过真实身份读取／同值预览／stale拒绝验收。SkillTest未部署。NF三处用户手动引用已复核，本次未改写。**
[问题索引](README.md)

## 1. 实际需求

NF T3 Cannon接入需要配置三个现有反射FText字段：

- `UNFTowerDefinition.DisplayName`：T3塔名。
- `UNFTowerUpgradeDefinition.A.Name`：A分支名称。
- `UNFTowerUpgradeDefinition.A.Description`：A分支说明。

按NF既有UI规范应保存真正的String Table FText引用，沿用UE本地化历史、
动态语言切换和Gather／PO流程；不能只把当前显示字符串复制进去。
原T1 Definition已经真实引用
`/Game/Data/Localization/Text/ST_TowerNames.ST_TowerNames`的`Tower.T1.Name`，
不允许为了本次验收改写这份生产配置。

用户已选择：**先反馈文档，这次手动处理绑定**，不是授权NF会话修改MCP插件，
也不是采用运行时注入／新资产配置commandlet或放弃String Table规范。

## 2. 已确认范围，不是生产保存事故

NF当前73项公开工具含String Table作者、DataAsset typed writer、
Animation和Skeletal Component Defaults；后三者的修复结论不受本问题影响。

`set_asset_properties`对FText的已部署合同明确只接受普通字符串：

```text
Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAssetProperties.cpp
SerializeValue: FText -> ToString()
ParseValue: FText::FromString(Value->AsString())
```

维护文档`Docs/Tools/project_tools.md`也明确：
`FText writes use plain strings and do not preserve localization identity.`

因此：

- 写英文源文案不能生成String Table历史。
- 把`LOCTABLE(...)`作为普通字符串提交只会得到字面内容，不是引用；
  本轮没有向生产资产尝试这种无效写法。
- typed读取当前显示值不足以区分“同文案、不同Table／Key”的FText身份，
  expected_value不能只按ToString判断引用并发。
- `set_blueprint_property`操作BP CDO，不是DataAsset实例FText作者入口。
- 四个String Table工具可以创建／维护表的Key与源文案，但不能因此宣称
  DataAsset使用方已经绑定。UMG专用文本作者也不等于任意DataAsset字段作者。

这是能力范围缺口，未报告发生过writer／保存失败。
本轮未修改原T1名字引用，没有新增运行时文字映射或C++塔名常量。

## 3. 最小有限合同

接口名称由维护侧决定。可提供专用绑定入口或显式扩展typed FText值格式，
保留原普通字符串写入语义，**不要把所有字符串自动当作ImportText脚本解析**。

- 目标限已支持DataAsset／PrimaryDataAsset；使用现有受保护属性路径解析。
  至少支持根FText与非Instanced普通struct内FText，例如`A.Name`。
  不开放任意UObject／Instanced对象遍历。
- 明确的String Table资产完整路径＋现有Key；验证实际UStringTable、
  Namespace／Table ID／Key存在、引用保存状态。不能静默改Key／Namespace或自动建缺失条目。
- 保存真正的`FText::FromStringTable`等原生历史，而非已翻译ToString；
  Reader返回可核对的Table ID／Key与文字，必要时新增opt-in身份读取，保持旧读合同兼容。
- 有身份感知的expected baseline／expected value：同显示文案的不同引用也必须区分。
- 默认预览／不保存，dirty／stale／未支持字段／错误类型整体拒绝；
  单事务、正式属性通知、target-only保存，保留其余属性及相关表／其他资产。
- no-op不写；Undo／Redo、实际modified／saved／package_dirty／stage、
  保存失败已修改状态及超时未知真实保留；不自动重试。

## 4. 隔离验收

| 用例 | 期望 |
|---|---|
| 根DisplayName与struct内A.Name／Description | 真正Table ID／Key保存，其他配置保持 |
| en→zh-Hans→en | 原生FText随语言切换，非临时显示缓存／语言分支代码 |
| 同文案不同Key／同Key不同表 | 身份读取／stale判断准确，不只比较ToString |
| 缺表／缺Key／错误类型／不支持属性路径 | 写前拒绝，不自动创建／猜测引用 |
| dirty目标／脏表／stale | 不覆盖用户草稿、不Save All |
| Undo／Redo／保存故障／独立冷读 | 引用历史及未指定字段保持，回执真实 |
| 旧plain-string调用 | 原行为不变，不把合法字面LOCTABLE文本当成命令执行 |

先在维护仓库／SkillTest隔离夹具验收；不得试写NF生产T1／T2获得通过。
部署后NF只读确认旧T1真实引用，业务T3写入需单独授权／fresh状态检查。
这不授权替NF选新文本、修改已有翻译或进行全库FText迁移。

## 5. 本轮NF手动交接范围

拟用生产数据路径：
`/Game/Data/TowerDefense/TowerCofigs/DA_T3_Cannon`、
`/Game/Data/TowerDefense/TowerCofigs/DA_T3_Upgrades`。
表仍为`/Game/Data/Localization/Text/ST_TowerNames`，
准备增量Key后由用户在UE Details中选择String Table引用；保留原T1条目。
本轮不通过该受限writer填这三个FText，不将源条目存在当作使用方已绑定。
NF具体准备／手动完成状态以NF交接记录为准，非本能力报告自动结项。

## 6. 维护侧实现与隔离验收（2026-11-03）

保留以上原始反馈与手动交接范围。新增两个独立Asset工具，未改变旧plain-string
codec、默认save行为或把字符串当ImportText执行：

- `read_data_asset_text_references(asset_path, property_paths)`：显式1..64条
  FText路径，返回原生Table ID／Key／只读history、显示值、writable及baseline。
  baseline包括排序路径身份、目标class／package和磁盘包哈希，不包括语言显示值。
- `bind_data_asset_text_references(asset_path, expected_baseline, bindings,
  dry_run=true, save=false)`：每行明确`path / table_path / key /
  expected_table_id / expected_namespace`。路径是规范`/Game/...`包路径，无object后缀；
  实际ID／Namespace从`read_string_table`获取，不按表名猜测。

目标为DataAsset实例，复用原受保护路径解析、普通struct／现有数组索引及
EditCondition；不开放Instanced／任意UObject图。表须实际存在、已保存且clean，
ID／Namespace吻合且Key已存在，不建Key、不改源文本／Namespace／翻译。
dirty目标（包括save=false）、脏表、stale／重复路径／不支持类型整体拒绝。
整批staging后原生`FText::FromStringTable`，一次事务及正式root属性通知；
按真正history身份判断修改，不依赖ToString或泛型FText显示相等。
完整staged root及引用身份读回，核对表磁盘哈希；仅可选保存目标包。
no-op不修改／不保存。回执保留before／proposed_identities／实际after及
modified／saved／package_dirty／stage；失败不伪造回滚，超时未知且不重试。

已验证：

- 完整Python89项通过：schema具体类型、真实Tool.run参数、read-only／category／
  direct／grouped、完整overflow、保存失败回执及timeout单次调用。
- 原生TextRead／TextBind以及旧Validation／EditConditions通过：根与struct三字段、
  同显示不同Key／同Key不同表stale、错误末行原子拒绝、缺表／缺Key／类型／路径／
  Namespace／ID／dirty、原生Undo／Redo、no-op、保存故障注入内存已变但磁盘不变、
  literal LOCTABLE仍为字面值、未指定cost与其他root保持。
- 独立进程TextReload验证保存后身份baseline及兄弟字段；真实Gather／PO Import／
  locres Compile各exit0、插件启用，已有DataAsset根／struct FText随
  en→zh-Hans→en切换，baseline不随显示语言变化。显式加载夹具locres，
  不代表业务本地化target已安装或字体／UI视觉验收。
- 实际fresh stdio→仓库临时Editor13092：新__Dev副本三字段preview／保存、
  invalid-last-row／stale拒绝、同身份no-op；源夹具与配置10份哈希不变，dirty0，
  自有Editor正常关闭、临时端口释放。首轮启动构建未完成时只读context失败，
  当时未执行复制／写入；失败原始记录保留，未重放任何未知写入。

证据保留在仓库项目Saved（不提交原始证据／资产）：
`Saved/Logs/AssetText-Matrix-Validated.log`、`AssetText-Legacy-Regression.log`；
原生夹具`/Game/__Dev/AssetText_B102C43B44B87107AA9417A776E78202`及
`Saved/StringTableLocalization/AssetText_B102C43B44B87107AA9417A776E78202/receipt.json`；
公开回执`Saved/AssetTextPublic/f1756113d7db416b9ed6fdcc64f1d3a1/receipt.json`。
完整合同见[DataAsset Text References](../Tools/project_tools.md#dataasset-text-references)。

**此次仅维护仓库源码与隔离验收，不部署NF／SkillTest，不改NF业务DataAsset／
String Table／翻译，不接管用户手动绑定，不创建NF测试或FT。**
部署／NF只读确认旧T1引用／后续T3业务绑定仍须各自授权与fresh状态检查。
本轮未commit／push。

## 7. NF部署与受限验收（2026-11-03）

用户明确要求部署到NF。预检时NF Editor已正常退出，原日志地图为L_map01；
未由维护会话关闭／强杀Editor，也未Save All。目标UE5.7.4，项目
`C:\04_unrealProjects\NodeFall\NodeFall.uproject`。

仅更新`UnrealMCPAssetProperties.cpp/.h`和Bridge三个原生文件；前两份目标
与仓库旧基线一致，Bridge除本次include／两路由／错误回执条件外全部文本一致，
清理仅插件Binaries／Intermediate。其余Build.cs、uplugin、导航／DirectoryWatcher、
NF本地Tag测试修复、组件默认值及Animation／Input代码均保留。未复制仓库新测试
types／TextBind测试到NF，未创建NF夹具。NodeFallEditor构建101动作成功，232.93秒；
构建也重编译了部分用户既有game代码，不表示本次修改或运行验收了那些gameplay用例。

恢复原L_map01、Maximized／NoLiveCoding，Editor1340、127.0.0.1:13090。
实际原`.mcp.json`fresh stdio保留READ_ONLY=0／Node=0等过滤后75项工具，
没有新建VS Code配置或改变权限。以下为真实已保存引用的复核，不是本次绑定：

| DataAsset字段 | 原生Table ID | 原生Key |
|---|---|---|
| DA_T1_basic.DisplayName | `/Game/Data/Localization/Text/ST_TowerNames.ST_TowerNames` | `Tower.T1.Name` |
| DA_T3_Cannon.DisplayName | 同上 | `Tower.T3.Name` |
| DA_T3_Upgrades.A.Name | 同上 | `Tower.T3.A.Name` |
| DA_T3_Upgrades.A.Description | 同上 | `Tower.T3.A.Description` |

三个目标scope均contract1／complete，same-identity默认dry-run返回
would_modify=false／modified=false／saved=false；确定stale的apply+save请求均
在preflight拒绝且modified=false／saved=false。复读整个scope及baseline完全相同，
BP_NFPlayerController窄list／read通过，地图／editor mode不变，dirty0→0。
没有执行实际生产绑定／保存，没有修改T1／T2／T3、表条目、Namespace、译文、占地或FT。
完整作者／Undo／保存故障／冷读／Gather语言切换仍为第6节仓库证据，不冒充NF重跑。

部署前备份：`Saved/UnrealMCPBackups/FText_20261103_202319`；
部署／公开原始回执及build日志：`Saved/UnrealMCPDeployment/FText_20261103_202347`。
文档记录前5716份非目标原件哈希保持、所有4784个Content文件名称／大小／时间／
哈希保持，三份目标源码与已验收仓库版本精确一致，无源码文件新增。
NF仅追加原有AI交接记录及其索引；不管理NF Git，不commit／push。

验收后曾保持Editor1340／13090打开。最终保护检查时，另一NF会话已在20:34:57
正常关闭GUI（20:35:00退出日志），并启动Editor-Cmd14148执行
NodeFall.TowerDefense.Cannon／Combat.Montage测试，带NoServer；该测试不是本部署会话
启动，也不算本次新接口验收。维护侧未打断、强杀或竞争重开，当前13090不监听。
待原会话恢复Editor后，已有客户端重连／刷新工具；Claude项目服务仍显示
Pending approval，用户首次在NF运行Claude时自行批准，不绕过信任。
SkillTest本轮未更新；NF业务本地化新翻译与运行语言视觉验收仍未因此完成。
