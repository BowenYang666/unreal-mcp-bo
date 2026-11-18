# DataTable原生行结构创建／导入／读取能力待确认

日期：2026-11-18。状态：**有限原生合同、113 Python、作者／独立冷读通过；已完整构建部署NF／SkillTest，真实公开MCP和文件保护验收通过。**
[问题索引](../README.md)

以下第1～6节保留原使用侧反馈；最新维护状态见第7节，不能将新增schema当成目标DLL已支持。

## 1. 实际需求与范围

NodeFall UE5.7正在实现用户批准的正式Wave／Subwave系统，使用两张职责不同的DataTable：

- `DT_Waves`：一行一个整Wave，原生`FNFWaveRow : FTableRowBase`，
  包含稳定ID、顺序、准备／波间倒计时、超时。
- `DT_WaveSpawns`：一行一种敌人的批次，原生`FNFWaveSpawnRow : FTableRowBase`，
  包含Wave／Subwave关联、相对时间、`TSoftClassPtr<ANFEnemyCharacter>`、数量、
  间隔和逻辑出生组ID。地图Actor引用不保存在可移植表行中。

用户已确认首轮Map1验证数据。需要在原生结构体编译可用之后，
以UE支持的DataTable工厂／CSV或JSON导入流程创建新表，再读回真实行类型和全部行值。
运行态不写回表；不要求开放任意UObject、私有序列化或通用脚本执行。

本次只有文档反馈，没有修改MCP代码／插件、部署、构建或写入任何DataTable。
Wave业务代码仍在独立任务中开发；目前新行结构尚未编入运行Editor，
**不能把“当前无法加载尚未编译的业务结构体”当成MCP缺陷证据。**

## 2. 已观察到的公开能力

通过当前项目配置的官方stdio MCP／SDK使用公开发现接口：

```json
{"tool": "asset_search", "arguments": {"query": "DataTable CSV import", "limit": 10}}
```

实际返回共5项，无下一页：

```text
import_string_table_csv
bind_data_asset_text_references
patch_montage_notify_properties
read_data_asset_text_references
set_anim_sequence_additive
total=5, has_more=false
```

另一次`query="data table import create read"`返回DataAsset／StringTable相关入口，
同样没有发现专用DataTable操作。当前`list_tools`完整公开catalog中也没有直接DataTable工具；
维护仓库`Python`下搜索`DataTable`／`data_table`／`CSVImport`未找到实现。
以上说明本次实际发现面没有可调用的入口，**不是声称执行过DataTable写入后报错**。

已存在的`import_string_table_csv`明确面向UStringTable的Key／SourceString／元数据，
不应套用到UDataTable。`create_data_asset`是具体DataAsset子类实例工厂，
也不能因名称相近就假定支持`UDataTable`或其RowStruct。
通用`read_data_asset`／`get_class_properties`可能读取部分UObject反射值，
但本次没有用它们宣称完整读取DataTable自定义行内存或证明可作者。

没有尝试伪造工具名调用写端点、执行私有writer、增加commandlet／插件，
也没有用测试代码偷偷SavePackage创建业务表。原生编辑器手工CSV导入是已提出、
但尚未执行／验收的备选，不是当前MCP能力已解决。

## 3. 原始证据

本机NodeFall会话artifacts（未复制到仓库或上传）：

```text
C:\Users\15208\.copilot\session-state\d6ded13a-420e-400f-aa66-e6cee37736cd\files\wave184-20261118\
  catalog.json
  initial-reads.json
  datatable-discovery.json
  datatable-discovery-value.json
  tool-reads.json
  datatable-exact-discovery.json
  datatable-exact-discovery-value.json
```

业务Editor当时经OS／公开context确认是NodeFall；初始草稿0，
无shader／asset compilation／scene task／import。
“发现工具”本身不进行UE资产作者，没有DataTable写后状态或运行验证可报告。
维护仓库此刻HEAD为`a26fc97f7e833b5b0787e44f22122b0998d7ad56`，
不将源码HEAD等同于NF实际部署DLL身份。

## 4. 请求维护侧先确认

1. 是否已有正确公开入口但当前分类／搜索／部署未暴露；如有，请提供准确工具及完整schema，
   区分新建表、整表导入、已有行增量编辑、完整读回。
2. 如果确实未支持，建议先补有限的原生DataTable合同，不要求通用运行脚本。
3. 不因报告而自动改NF、替换插件、重启共享Editor或创建业务表；修复／部署另获授权。

## 5. 建议的最小能力与安全合同

- 专用只读：准确资产路径、实际RowStruct路径／字段类型、稳定RowName、
  每个真实行值、软类引用路径、ImportSource、package_dirty及完整性标记。
  不只返回schema／行数或将CDO当表实例；分页须可证明未漏行。
- 新建：仅允许不存在的目标包，指定已经加载／编译的`FTableRowBase`派生结构；
  使用原生`UDataTableFactory`／等价支持流程，禁止覆盖已有包。
- 导入：使用UE原生CSV／JSON解析／类型转换；先在隔离临时表预检，
  明确replace／merge、缺列／多列、重复RowName、解析错误、
  原生row validation失败和软引用类型约束。不得跳过坏行后返回整表成功。
- 修改：精确路径／RowStruct／fresh baseline，已有用户dirty或stale拒绝；
  删除与整表替换显式授权，保未指定行／字段，不改源结构定义或非目标资产。
- 默认dry-run、不保存；应用是可Undo事务，targetsave可选且仅保存目标包。
  返回真实modified／saved／stage／error；保存失败或超时标明已知修改状态，
  不输出成功形状或自动重试未知作者请求。
- FText保持真正history，软类引用保类型，不用当前显示字符串／短类名猜值；
  CSV仅作导入源，不能另造一套与运行DataTable竞争的配置真值。

## 6. 建议验收（尚未执行）

- 隔离原生row fixture创建两表，真实全部行值／RowStruct读回，
  保存后独立冷加载仍相同；原生runtime查表得到同值。
- 非法行类型、未编译结构、重复行、缺列／非法数值、错误软类引用拒绝，
  原表和未指定行保持。
- merge／replace／显式删除、no-op／stale／dirty保护、Undo、
  保存失败与超时未知状态均有准确回执。
- 全量与分页一致，不能把schema／行数当完整读取。
- 测试只写隔离夹具；NodeFall正式Wave业务接入、Map1实际生成／调度另验，
  工具验收不自动批准业务美术、平衡、场景或Wave完成。

## 7. 维护实施与验收

用户先授权补全工具，初始限定维护仓库与隔离夹具；之后又明确要求“测试完之后，deploy到nf和skilltest”。
因此目标插件更新／构建／正常重启已获授权，业务表作者仍未授权；不使用尚未编译的NF
Wave行结构或创建正式业务表，未提交或push。

当前新增三项Asset操作：`read_data_table`、`create_data_table`、`import_data_table`，
只有读取进入只读白名单，分类不依赖Project。默认作者`dry_run=true, save=false`。
MCP参数使用具体类型，遵守不使用Any／object／Optional／Union的仓库规则。
导入源采用`source={"format":"csv"|"json","text":"<原生文档>"}`，避免当前SDK把顶层
JSON字符串自动解析成数组而破坏函数参数；真实Tool.run合同已验证。

原生专用适配器遍历真实RowMap，返回RowStruct／字段schema、稳定行序和分页、
FText身份及全表二进制基线；不支持的布局明确不完整，基线不可用于写入。
当前有限范围为普通UDataTable、已编译／加载的原生FTableRowBase子结构、无自定义
native serializer；Map／Set等超出合同的行字段不开放作者。创建拒绝已有包；导入先
严格检查文档／重复身份／列，再用隔离临时表走原生CSV／JSON解析／行验证。
合并、显式replace／delete、脏状态与stale保护、单事务、目标可选保存及真实失败回执
已进入实现，仍须实际行为测试证明，不能以代码存在替代验收。

Python完整113项回归通过：具体参数与默认值、Asset独立过滤／只读、旧DLL能力拒绝、
完整分页spill、预算、部分保存失败和超时未知状态不重试。三工具加入后为181内部操作、
55只读；默认grouped仍57／29。原生完整UHT／DLL构建通过，作者矩阵及两表独立冷加载
已通过；实际目标grouped MCP及最终保护检查待部署后完成。

实现中已纠正两处原生边界：JSON importer会清空旧行，故先独立解析再复制显式提交列，
不使用其CSV preserve开关伪称merge；临时表生成的FText会在保存时被UE归属目标包，
故使用隔离包命名空间、稳定输入表名和原生文本复制保留key，并保留严格保存后见证。
工厂完成RowStruct初始化后再开启事务；空JSON数组仅在严格解析后作为显式delete-only。
没有忽略原生错误或放宽未提交字段／FText身份保护。

维护原生矩阵验证CSV／JSON、重复属性／行名、缺列／未知列、非法计数和错误软类拒绝、
merge／显式delete、dirty／stale／no-op、相同文本CSV no-op、损坏CSV引用拒绝、Undo／
Redo、保存故障内存已改但磁盘不变、FText namespace与两表runtime查表。
最终夹具 `/Game/__Dev/DataTable_DB36553D4E16D0BE10FFD0A26174F410` 的作者矩阵和显式
独立冷读通过，日志位于维护项目 `Saved/DataTableAcceptance/authoring-stable-text.log`
和 `cold-stable-text.log`。早期失败日志保留，不能当作最新结果或删除。

两个目标预检均已关闭，无需发保存／关闭请求。部署备份标识
`DataTable_20261118_113029`，完整旧插件保留在各自 `Saved/UnrealMCPBackups`，
验收记录在 `Saved/UnrealMCPDeployment`。只合并Bridge／既有测试头并添加两份专用
DataTable源码，其余本地定制逐字节保持；现有配置／端口／权限未改。双项目完整DLL
构建及各自作者／显式独立冷读均通过：SkillTest36动作1452.11秒，NF144动作188.36秒。
两边正常GUI已启动，并通过实际原配置grouped MCP的创建预检／应用保存、全量／分页读回、
JSON merge／CSV replace、非法末行原子拒绝、stale／no-op、FText／软类及未提交行字段
保持；操作前后地图／视口／工程上下文一致、草稿0。写请求只指向新建的自有
`/Game/__Dev/DataTablePublic_*`，没有向NF正式Wave表发送创建／导入请求。

最终保护核对：SkillTest15608个、NF5972个原有Content／Config／项目Source／uproject／
MCP配置文件SHA256全部保持；非目标插件源码逐字节保持，四个功能文件与维护源码一致。
每个项目仅新增两张原生夹具表与一张公开夹具表，没有其它新增Content，完整旧插件
备份保留。各记录目录的 `protected-final.json`、`source-final.json`、`dll.json` 和
`public-final.json` 保留实际证据，公开完整回执位于：

```text
SkillTest: public-8701a7c3aacf4d63b56946fa17830bb1
NF:        public-22c4afa887d34aad8a3ce5c5a720e68d
```

两边正常交互GUI保留打开，持久端口SkillTest13091／NF13090、工程绑定／权限／客户端
配置不变。已有MCP服务需重连加载共享Python合同，独立SDK验收不代替GUI信任审批。
本轮未提交或push；正式Wave／Subwave业务表内容、原生行类型业务设计及Map1生成／
调度仍由原任务另行配置和验收，不能把工具通过当作游戏功能已完成。

原始反馈与业务设计未被覆盖；后续以实际成功／失败日志和本节续作记录为准。
