# 场景截图任务记录不回收：完成32次后拒绝新截图

## 状态与范围

- **状态：维护源码已修复，NF／SkillTest均已构建部署并验收。SkillTest后续授权完成build，较早copy-only记录及原始反馈保留。**
- 报告日期：2026-11-05，Windows／UE5.7.4。
- 影响工具：`scene_call_write(tool="capture_scene_viewport", arguments=...)`，
  配套查询为`scene_call_read(tool="get_scene_task_status", arguments=...)`。
- 用户已成功保存截图，随后无法创建新截图。**不是磁盘空间、PNG文件数量或GPU图像缓存耗尽。**
- 此次仅提交反馈和本地证据，不修改插件／引擎／配置，不为复现重启Editor或重放32次截图。

透明PNG导致用户看不到画面是另一个问题，见
[截图PNG透明通道](scene-capture-transparent-png.md)。

## 环境和源码一致性

| 项目 | 实际值 |
|---|---|
| UE | `5.7.4-51494982+++UE5+Release-5.7` |
| 使用工程 | `E:\04_UnrealProjects\UE5Test\SkillTest\SkillTest.uproject` |
| 编辑器地图 | `/Game/Dev/VFX/Maps/L_CannonVFX_Review` |
| 当次Editor／端口 | PID22044，127.0.0.1:13091；只用于说明当次证据，后续需重新核对 |
| 客户端 | 当前仓库Python server，stdio／grouped模式，串行等待每个截图任务完成 |
| 报告时维护仓库HEAD | `44c6517` |
| 原始截图输出 | 实际UE项目的`Saved\Screenshots\UnrealMCP\<request_id>.png` |

报告时维护仓库、SkillTest部署目录、NF部署目录的
`Private\Commands\UnrealMCPSceneCommands.cpp`具有相同SHA256：

```text
ED778924A827CEE59F3BA75F6BA50E9E0F0581BA0D87073B1BCE67552AD9BF87
```

源码一致性不是DLL构建身份校验；本次没有重新构建、部署或校验已加载DLL的符号。
实际公开工具拒绝结果与以下源码路径一致。

## 真实表现与证据

前一个截图任务已完成，回执包含：

```json
{
  "success": true,
  "state": "completed",
  "request_id": "CannonExplosion_DetailV2_Continuous_5_1105_v1",
  "saved": true,
  "bytes": 506404,
  "render_target": "offscreen",
  "actual_width": 1280,
  "actual_height": 720
}
```

后续使用不同request_id的新请求被预检拒绝：

```json
{
  "success": false,
  "modified": false,
  "saved": false,
  "stage": "preflight",
  "error": "Capture already active or task cache full",
  "request_id": "CannonExplosion_DetailV2_Continuous_6_1105_v1"
}
```

拒绝后的`get_editor_context`明确返回`scene_task_active=false`、
`shader_compiling=false`、`remaining_asset_compilations=0`、`import_state=idle`。
公开`scene_search(query="task")`没有完成任务清理／释放入口。
失败请求未生成成功截图，不将其计作完成，也没有不断换ID重试。

本次未通过公开工具直接读取`Tasks.Num()`，也未另外跑空Editor的精确32/33边界夹具。
运行证据证明无活动任务时发生容量类拒绝；32条阈值及无回收原因来自对应源码。

## 源码原因

维护仓库文件：
`MCPGameProject\Plugins\UnrealMCP\Source\UnrealMCP\Private\Commands\UnrealMCPSceneCommands.cpp`。

| 位置 | 行号（报告时） | 行为 |
|---|---:|---|
| 命名空间级`Tasks` | 61 | `TMap<FString, TSharedPtr<FJsonObject>>`，保留任务状态／路径等回执，不是PNG像素数组 |
| `Capture()`相同ID处理 | 1152 | 已存在ID直接返回旧回执，不重新执行 |
| `Capture()`容量门控 | 1153 | `CaptureBusy || Tasks.Num() >= 32`统一拒绝 |
| 任务入表 | 1189 | `Tasks.Add(Id, Result)`，成功接受后占一个位置 |
| 完成／失败的`Finish` | 1195～1205 | 更新状态、释放`CaptureViewport`、令`CaptureBusy=false`，没有删除任务记录 |
| `TaskStatus()` | 1238～1244 | 查询并返回表内回执，不消费／回收记录 |

文件中没有`Tasks.Remove`／`Empty`／`Reset`或TTL／LRU淘汰路径。
成功完成和进入执行后失败的任务都留在表中；早于入表的预检拒绝不应算作已接纳任务。
因此当前实现是在**同一UE编辑器进程生命周期累计接纳32条**之后阻止所有新的ID，
而不是仅限制同时进行中的任务数量。

图片落盘和任务表是两条独立生命周期：

```text
渲染 -> PNG保存到项目Saved目录 -> 任务回执标completed
                                  |
                                  +-> 任务仍留在UE进程的Tasks表
```

删除PNG不会删除内存记录。创建新的Copilot会话、重连stdio／Python server也不会自然清除
仍在UE进程中的表。Python工具文档的“32 cached tasks/session”容易被误解成客户端会话，
应明确它对应的生命周期。

## 最小复现建议（维护侧执行）

1. 在隔离Editor／固定可渲染地图中，确认没有其他截图任务。
2. 使用唯一ID串行提交32个有效截图，每个都查询到completed／failed再继续。
3. 核对成功PNG已经落盘、`scene_task_active=false`，再提交第33个唯一ID。
4. 当前预期故障为容量拒绝，即使前32个均已成功保存。
5. 不通过删除原PNG、关闭用户工程或修改缓存上限来掩盖回收问题。

## 建议修复与验收标准

- 保留有界状态历史，但对**已经终结**的成功／失败任务采用TTL或LRU淘汰，
  或提供受控的完成任务释放接口；不把“曾经执行过的总数”作为永久硬上限。
- 清理任务元数据不删除已保存PNG，不取消正在执行的任务，不释放仍被Ticker使用的资源。
- 相同ID在保留窗口中仍幂等；已过期ID应有明确结果，不能悄悄重做／覆盖已有文件。
- 将“当前有活动截图”和“任务记录达到上限”分成可诊断的错误码／消息；
  可返回活动request_id、已保存记录数及支持的恢复动作。
- 串行100次唯一ID截图无需重启Editor，PNG和最终回执均真实可读；内存记录仍有上限。
- 覆盖成功、执行失败、超时、重复ID、过期ID、输出文件已存在和并发拒绝等边界。
- 保持当前project/world/viewport身份检查、offscreen尺寸、相机／曝光稳定性及不保存资产的边界。

## 本地原始证据

本目录的忽略路径（不纳入Git提交）：

```text
Docs\reported-issues\evidence\2026-11-05\scene-capture\
  task-last-completed.json
  task-cache-refused.json
  task-context-after-refusal.json
```

原始会话证据位于：

```text
C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files\
  cannon-explosion-details-review-0851\
  cannon-explosion-capture-cache-contract-0851\
```

没有把VFX资产或整个项目日志复制到维护仓库，也没有自动commit／push。

## 维护侧修复与验证（2026-11-05）

按用户要求，先将上一轮项目绑定／懒连接代码独立提交为`f5adff4`，
再开始本问题修复；原始报告／索引未混入该提交。本次截图修复尚未commit／部署。

原`Tasks`保留32条上限，但新ID先预留持久化意图，完成／失败回执原子发布到
`Saved/UnrealMCP/SceneCaptures/<id>.json`。只有终结且当前磁盘哈希仍匹配的记录
进入FIFO可淘汰集合；活动任务、写盘失败或被外部改动的回执不被淘汰。
旧ID由内存或磁盘返回，不开始新的渲染；旧成功／失败在换地图或重启后仍可查询。
未终结意图、损坏／缺失历史返回`interrupted_unknown`，不伪造saved=false回滚结论。
原PNG不删除；真正发布PNG也采用原子no-replace，pending期间出现同名文件仍不覆盖。

错误区分`capture_active`、`capture_cache_pinned`、`capture_output_exists`和
`capture_receipt_write_failed`，带缓存数／上限及适用时的活动ID。
`saved`与`receipt_persisted`独立：PNG已保存但最终回执失败仍如实保留完成／已保存状态，
附`persistence_error`并固定内存记录，不能为腾空间丢弃不确定状态。
磁盘持久历史不受32条内存窗口限制；项目负责人仍需管理Saved保留策略。

仓库UE5.7.4真实RHI／NoServer验证：

- `CaptureCapacity`：同一进程三类执行失败（Game View变更、写出失败、私有超时注入）
  后连续100张真实PNG成功；每次同IDpending幂等、不同IDbusy拒绝、busy释放、
  内存≤32及World／源视口保持。包含PNG成功但终结回执失败的私有注入，原图仍保存，
  回执固定且警告真实。旧成功／失败回执从磁盘返回，重放成功ID不改变PNG哈希。
- `CaptureReceiptSafety`：独占意图预留、原意图字节保持、未终结冷读未知、旧记录
  淘汰不回填、损坏回执拒绝、非持久／外改回执全满时保守拒绝。
- `CaptureResolution`15项及`CaptureOpaqueCodec`一起共18项矩阵通过；
  最终日志`MCPGameProject/Saved/Logs/Capture-Matrix-Final.log`。
- 另一个独立Editor进程显式冷读
  `CaptureBatch_A15D4DD6442219E35D391FB0EF7E2922_4`通过，内存0、不启动截图、
  PNG哈希保持；日志`Capture-ColdRead-Final.log`。未传ID的跳过测试不算冷读证明。
- 完整Python97项通过，参数／分组／项目绑定／失败回执及超时不重试保留。

仅修改维护仓库；未连接、关闭、重启或部署NF／SkillTest，未修改原证据PNG、
业务VFX／地图／渲染配置。Windows原子发布经过运行验证；POSIX分支未在本机运行。
实际客户端仍需后续授权部署并编译新native DLL，不能因共享Python说明已更新就视为修好。

## NF部署／SkillTest源码同步（2026-11-05）

按用户明确范围，NF完整部署，SkillTest仅copy、不build／不清理输出／不启动Editor。
预检两个Editor均已关闭，无需关闭或Save All。三份目标源码都匹配旧HEAD基线，
只同步SceneCommands.cpp／.h和MCPSceneTests.cpp，其他本地插件定制／配置保持。
NF NodeFallEditor38动作构建成功191.56秒；18项实际部署DLL截图矩阵与
显式独立冷读通过，冷读ID为`CaptureBatch_95672CC547170B11D43FA9A807C99003_4`。

NF恢复L_map01、Editor40216／13090，实际原绑定配置stdio完成小截图、
contract1／opaque／持久回执读取、相同ID回放，PNG哈希与写入时间不变，
地图／视口／相机／dirty0保持。第一次验收脚本因Windows路径分隔符比较失败，
当时截图与回执已完成；规范化路径后只恢复同一ID验证，未重拍新图或盲重试写入。
SkillTest旧DLL哈希和Binaries／Intermediate名称、大小、时间保持原样，
三份源码与已验收源精确一致，但**不因此声称SkillTest运行中的插件已修复**。

两个项目备份：`Saved/UnrealMCPBackups/CaptureFix_20261105_102338`；
NF构建／原生矩阵／冷读／公开回执：
`Saved/UnrealMCPDeployment/CaptureFix_20261105_102338`；SkillTest同目录
`copy-only.json`注明Built=false／Restarted=false。原始透明PNG与业务资产未改，
没有移动目录或重放生产VFX制作。截图修复仍未commit／push。

## SkillTest完整部署（2026-11-05，后续授权）

用户随后要求“部署到skilltest”，解除较早copy-only范围。本次不再复制或修改其他源码；
三份截图文件仍与已验收维护源码精确一致，UE5.7.4的SkillTestEditor增量7动作构建成功，
总耗时1138.49秒，新DLL已链接。未中止编译器或Editor，也未操作NF。

实际SkillTest DLL的18项矩阵全部通过，包括三类执行失败后的连续100张真实PNG、
32条有界内存窗口、持久化失败固定／警告、busy／同ID幂等、输出冲突不覆盖及Alpha校验。
独立Editor进程显式冷读`CaptureBatch_2D1DB4DC43F1D444E252F5878C77E36E_4`
通过，不渲染、不改变旧PNG哈希；无参数跳过不计作证明。

恢复原`/Game/Dev/VFX/Maps/L_CannonVFX_Review`，按项目INI使用13091，未加端口覆盖。
原VSCode配置的公开grouped MCP保存独立320×180截图，contract1／opaque／持久回执
真实成立；同ID重放后PNG哈希／时间不变，完整视口／相机／曝光／地图及dirty0保持。
原Claude配置独立重连读取同ID及BP_ThirdPersonCharacter通过，57个工具、正确项目绑定。
System.Drawing独立检查所有57600像素Alpha=255，非黑像素24855。

新备份：`Saved/UnrealMCPBackups/CaptureBuild_20261105_143610`；本次构建、
`native-matrix.log`、`native-cold.log`、公开完整回执及保护审计位于
`Saved/UnrealMCPDeployment/CaptureBuild_20261105_143610`。
最终14194份原件SHA256及14024个Content名称／大小／时间均保持；无业务资产保存、
配置／端口／权限变更，原报告PNG未改。Editor22368／13091是当次观察，后续需重查。
Claude项目服务仍待正常审批；已有MCP客户端需重连，未绕过信任。未commit／push。
