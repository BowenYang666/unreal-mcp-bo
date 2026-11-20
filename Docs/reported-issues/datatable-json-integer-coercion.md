# DataTable JSON 导入可能静默改写整数

## 状态

**2026-11-20 已修复并部署到 NF 和 SkillTest。** JSON 数字值与普通整数列的 JSON 字符串均在原生导入前校验。两边原生作者、独立冷读与绑定 MCP 验收通过。此报告补充[原生 DataTable 作者合同](fixed/datatable-native-row-authoring.md)。

## 影响

`create_data_table` 带 JSON 初始数据、以及 `import_data_table` 的 JSON 导入共用原生暂存路径。初次报告发现，整数列收到非整数、超出目标类型范围或无法精确表示的数值时，实现可能先取整或窄化，再把转换后的值当成有效结果。若行级业务校验没有拦住，预览和落盘都可能把不同于原始输入的值当作正常数据。结论限于 JSON；未确认 CSV 存在相同问题。

## 初次报告的源码证据

1. [UnrealMCPDataTableCommands.cpp](../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPDataTableCommands.cpp) `Document`（约 338–348 行）检查 JSON 行对象、行名和列名，未在此处按目标属性类型校验原始数值。`StageImport`（约 355–394 行）随后调用 `CreateTableFromJSONString`。
2. UE 5.7 `DataTableJSON.cpp:922–929` 在整数分支用 `TryGetNumber(int64&)` 取值，然后调用 `SetIntPropertyValue`。`JsonValue.cpp:126–132` 的默认转换从 `double` 做 `RoundHalfFromZero`；`UnrealType.h:2081–2084` 的 setter 又将值转换为具体属性类型，没有在该处检查目标整数的位宽或符号范围。
3. 插件的 `ValidateField`（约 199–220 行）对已导入的属性做编码／解码校验。它检查的是转换后的内存值，不能判断它是否仍等于原始 JSON 数值。

## 修复验收

- 预览和实际应用使用同一份校验：整数输入必须是整数、落在目标属性的有符号或无符号范围内，并能原样导入。不能精确处理的值应拒绝，不应把已经过 `double` 舍入的值当原值校验。
- 拒绝时返回行名、字段路径及原因；新建不生成资产，更新不修改现有行、不改变原有脏状态、不保存。保留现有合法整数和浮点列的行为。
- 用维护项目的隔离原生行夹具覆盖合法边界、分数输入、窄类型越界和大整数精度，并比较预览、应用和独立冷读结果。无需对生产表试写。

原始反馈只记录静态证据，没有已观测到的生产数据损坏。

## 维护结果

- 维护原生测试在修复前复现 `1.4`、`4294967297` 和 `1.00000000000000001` 均被整数列预检接受。修复后在进入 UE 原生 JSON 导入器前保留数字原文，逐字段检查是否为数学整数、目标位宽／符号范围及现有安全整数范围 `±9007199254740991`。嵌套对象和数组使用原生字段映射，并在拒绝原因中标明行名和字段路径。
- 创建／更新的预览和应用共用门禁。扩展回归覆盖上述样本、极小分数、大整数精度、`uint8` 上界和符号校验、数组索引路径；确认失败不创建包、不更改原表或脏状态、不落盘。CSV 和浮点列的导入路径未改。
- 维护夹具 `/Game/__Dev/DataTable_78730AF7410F171BB8167891F7410CCF` 的原生作者和独立冷读均通过。合法指数写法 `2.147483647e9` 导入 `int32` 上界 `2147483647`，浮点列 `2.25` 原样保存、冷读。日志见维护项目 `Saved/DataTableAcceptance` 中的 `json-integer-red.log`、`json-integer-boundaries.log` 和 `json-integer-cold.log`。

## 双项目部署

- 2026-11-20，两边仅更新 DataTable 命令源码文件、重新构建并链接插件。备份与回执分别位于各项目 `Saved/UnrealMCPBackups/DataTableJsonInteger_20261120_095939990` 和 `Saved/UnrealMCPDeployment/DataTableJsonInteger_20261120_095939990`。
- SkillTest 和 NF 的 `UnrealMCP.DataTable.Authoring` 及单独进程的 `UnrealMCP.DataTable.Cold` 均通过。正常重启后，用各自现有绑定配置从 `__Dev` 夹具读回 `2147483647`／`2.25`，并确认 `1.00000000000000001` 的预检返回 `Probe.Count`、`modified=false` 和 `saved=false`；草稿数保持 0。
- 非目标插件源码与上次验收清单一致，NF 最新 `DefaultEngine.ini` 与本轮预检哈希相符。重启前后 SkillTest 65 项、NF 68 项配置／客户端／非目标插件源码哈希相符；这不是全量业务资产哈希检查。未写入生产表，未改权限或工程绑定。

## 2026-11-20 复审：整数列的 JSON 字符串仍待修复

- 维护仓库和 NF 当前源码中的 `ValidateJsonIntegerValue`（`UnrealMCPDataTableCommands.cpp:346–356`）都允许普通整数列接收 JSON 字符串。预检按完整数值语义校验字符串，但随后仍把原始 JSON 交给 UE（约 453、475 行）。UE 5.7 的字符串整数读取走的是另一套转换（`DataTableJSON.cpp:922–929`、`JsonValue.h:479–482`）。若两个路径得到的都是合法整数但数值不同，导入后的校验无法发现改值。
- 建议仅接受原生导入能无损识别的整数字符串，或明确拒绝整数列中的字符串。保留合法 JSON 数字、枚举名、浮点列和 CSV 的既有行为。在隔离夹具中验证 Create／Import 的预览和应用走同一规则；拒绝时返回行名与字段路径，不创建资产、不改原表或原脏状态、不保存。
- 本次为静态复审，未执行导入或写入生产表；既有数字值验收不覆盖此字符串路径。

## 2026-11-20 字符串修复及部署验收

- 预检现在把普通整数列的字符串交给与 UE 原生导入相同的 `TryGetNumber(int64&)`，结果必须等于原文的精确整数值。拒绝 `"1e2"` 和 `"1.2e2"`，接受能无损导入的 `"8"`；数字值 `1e2` 仍可使用。枚举名、浮点列和 CSV 路径不变。
- 维护、NF 和 SkillTest 均通过 `UnrealMCP.DataTable.Authoring` 和显式指定夹具的独立 `UnrealMCP.DataTable.Cold`。创建与更新在预览和应用中均拒绝不一致的字符串，不创建或改写资产、不改变脏状态、不保存。无损 `"8"` 的应用与 Undo/Redo 通过。维护日志为 `json-integer-string-authoring.log` 和 `json-integer-string-cold.log`。
- 两边仅更新 DataTable 命令源码并重新链接。原有绑定 MCP 在正常启动后读回隔离表的 `Count=2147483647`、`Delay=2.25`，并确认创建和更新预览均在 `Probe.Count` 拒绝 `"1e2"`，返回 `modified=false`、`saved=false`；草稿数保持 0。NF 运行于 `13090`，SkillTest 运行于 `13091`。
- 两边备份与回执使用 `DataTableJsonIntegerString_20261120_152113183`；重启后 NF 69 项、SkillTest 66 项配置／客户端／描述文件／非目标插件源码哈希相符。写入仅限 `__Dev` 原生测试夹具，未写入生产表或修改项目设置、权限或 MCP schema。
