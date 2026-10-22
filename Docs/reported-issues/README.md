# 问题报告与修复汇总

更新日期：2026-10-22。

本文件汇总本目录的问题、修复、验证和遗留事项，作为代码提交与后续维护的统一入口。
详细报告保留原始复现经过及当时状态；阅读时以各报告最新的维护记录和续作反馈为准，
不能把历史描述中的“当前缺陷”或早期“尚未验收”直接当作最新结论。

## 状态总览

| 问题 | 当前状态 | 结论边界 |
| --- | --- | --- |
| [Blueprint 折叠子图不可读](cannot-read-bp-subgraph.md) | 已修复并验证，2026-08-15 | 支持可选子图遍历和引脚默认值；属于既有修复，不是本次新增代码 |
| [Niagara 保存重入崩溃](niagara-save-reentrancy-crash.md) | 修复已部署；原 NF 草稿续作及冷加载通过；报告仍 Open | 有真实使用正向证据，但未证明后续 D3D12 访问违例的唯一原因，未受控重建旧嵌套等待栈 |
| [同系统复制 Emitter 后不执行](same-system-emitter-duplicate-not-executed.md) | Open，尚未修复 | 有 Handle 和成功回执不等于能运行；缺少执行图重建是强烈怀疑方向，仍需实现与验证 |

## 1. Blueprint 折叠子图不可读

**问题：**`read_blueprint` 原先只读取根 Event Graph，返回 `UK2Node_Composite` 外壳，
遗漏 `BoundGraph` 内部节点、连接及未连接引脚上的常量，导致折叠逻辑无法完整检查。

**既有修复：**增加 `include_subgraphs=false`、`max_subgraph_depth=8`（限制 0..32）。
启用后返回 `subgraphs`、图关联信息及内部节点／引脚／连接；引脚补充默认字符串、文本
和对象引用。遍历有去重与深度限制，不使 Blueprint 变脏，默认调用形状保持兼容。

**已验证：**UE 5.7 Content Examples 的 UFO Blueprint 两个折叠图分别读出 87 和
83 个节点，能恢复插值速度、延迟和 FOV 映射等常量。大结果仍可转存为 JSON 文件。
本次文档整理没有重新执行该历史验收。

## 2. Niagara 保存重入崩溃

### 现象与原因边界

NF UE 5.7.4 的两次串行作者会话出现递归 `FlushRenderingCommands`、
`!IsAssetStreamingSuspended()` 断言和随后 D3D12 worker 访问违例；切到 Entry 空场景
也能出现。第二次最后收到连接重置的是 static switch 请求，但断言仍位于前一个输入
setter 的保存栈，不能据此归咎于 `Color Mode` 枚举值。

已确认的路径是：普通 GameThread task 被 DataValidation／Slate／RenderCore 等待中的
任务泵执行，并嵌套调用保存。线程正确不代表执行时机安全。后续 D3D12 访问违例是否只有
这一个原因，尚未证明。

### 本次修复

- 外部命令进入线程安全 FIFO，仅由主循环 `OnBeginFrame` 逐条取出；每个边界最多一条，
  禁止嵌套执行，保留原有路由、参数、权限过滤和 Scene 互斥。
- 保存、GC、慢任务或模态窗口期间不取队。队列上限 64 条，排队 20 秒仍未开始则取消，
  不在等待超时后悄悄开始修改。
- 停止服务器前释放待执行请求；执行中停止则释放等待线程并报告结果未知，不中途回滚、
  不伪报没有修改／没有保存，且 Promise 只完成一次。
- 检查原先忽略结果的 16 个 Niagara 自动保存调用；**保留自动保存语义**，失败返回
  `modified=true`、`saved=false`、`stage=save`、资产路径及脏状态。
- Bridge 和 Python 保留 native 失败回执；超时、连接重置或无响应标为结果未知，
  不自动重试作者操作。

主要实现：[调度队列](../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/MCPCommandQueue.h)、
[Bridge](../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp)、
[Niagara 命令](../../MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPNiagaraCommands.cpp)、
[Python 包装](../../Python/tools/niagara_tools.py)。

### 验证与部署

- 仓库完整 Python 回归：**74 项通过**。
- 仓库和部署后 SkillTest 原生回归：**6 项通过**，覆盖 `CommandQueue`、
  `FrameDispatchSafety`、`FragmentedSocket`、`RequestBuffer`、
  `CrossSystemEmitterCopy`、`SaveFailure`。包含受控保存失败注入，不等于真实操作系统写盘故障。
- SkillTest 的复制／保存夹具独立进程冷加载通过；两个项目的真实 MCP 只读烟测通过。
- SkillTest 与 NF 均已编译部署匹配的 native 插件，不仅是更新 Python。
  NF 本地插件定制保留；部署验收时 SkillTest 9269 个、NF 4613 个保护文件哈希未变。
- 部署及维护回归未修改驱动、RHI、流送或 DataValidation 配置，未重放生产创建批次。
  当时客户端配置和权限未改，已有客户端需重连；独立 stdio 烟测不代替客户端信任审批。

### 最新 NF 实际续作反馈

以下来自详细报告中 **2026-10-22、16:39 后** 的作者会话反馈，不是本次文档整理新增执行：

- 先读回部分完成的草稿，确认缺项后继续，不重复创建已有资产。
- 完成 **102 条连续调用**：80 条 Niagara 写入、9 条显式保存、8 条材质读取、
  2 条 Niagara 读取、3 条状态读取，覆盖 switch、RI、曲线、动态输入和 Renderer。
- 真实 Dev 视口完成多年龄出画检查及结束态无残留检查；显式保存、零未保存内容后，
  正常关闭并用独立进程冷加载，系统、材质、动态绑定和 Actor 引用持久化且实际出画。
- 本轮续作及冷进程日志未出现原来的递归 flush、流送暂停断言和访问违例三类标记。
  这不代表全部日志没有其他历史启动警告。

**尚未关闭：**没有从头重建原 171 操作批次，也没有人工受控构造旧的
DataValidation／Slate／RenderCore 嵌套等待链。上述正向证据不能扩大成“所有 D3D12
崩溃已根治”；是否完成完整故障回归及关闭报告，仍由维护者决定。

## 3. 同系统复制 Emitter 后不执行

**问题：**同系统复制接口返回成功，新层的 Handle、enabled、CPU、Renderer、Spawn、
Lifetime 和曲线数据都可读，但实际模拟只显示源层。重建预览实例仍无效；改用跨系统
独立快照复制后，对应多层网格实际显示。这与保存重入是独立问题，本次没有崩溃。

**强烈怀疑的缺口：**同系统分支调用 `DuplicateEmitterHandle` 后只同步 Overview、
使缓存失效并编译，未执行原生编辑器复制流程中的系统 Spawn／Update 执行图节点重建。
Overview 和 Handle 数量不是执行图连接正确的证明；当前报告尚未提供完整修复验证。

**临时替代路径：**从独立 Dev 系统快照使用跨系统复制分支，返回
`inheritance_mode=independent_snapshot`，并以真实模拟／图像确认新层出画。
这是报告中的本地作者替代路径，不是对同系统分支的修复，也不要求把 Dev 源作为运行时依赖。

**待办：**在 SkillTest 独立夹具中核对系统执行图的 Emitter 节点与 Handle ID，按原生流程
实现复制；检查真实粒子生成、源／副本参数隔离、RI 命名空间、曲线 DI、Renderer、继承和
User 参数，再验证保存及独立冷加载后仍执行。必须保留既有跨系统快照、安全调度和保存错误回执。

## 提交与证据边界

- 本次重入修复的提交说明可以写“帧边界调度、保存失败回执及已通过的回归”，不能写成
  “解决所有 Niagara 复制／渲染问题”。同系统复制缺陷应作为独立未完成事项保留。
- Markdown 原始报告随代码保留；崩溃日志、请求／回执和哈希清单不纳入提交。
  本次 12 个原始证据文件归档到仓库本地的
  `MCPGameProject/Saved/ReportedIssues/2026-10-22/niagara-save-reentrancy/`，
  内容保留不改写，该路径已被现有 `Saved/` 规则忽略。
  `Docs/reported-issues/evidence/` 也已加入忽略规则，防止后续误提交原始附件。
  截图仍在原报告列出的本地位置。日志包含本机路径和项目上下文，另行发布前须审查。
- 原崩溃附件清单与部署后 NF 续作证据有各自的 SHA256 清单，不能把早期清单当作覆盖
  后续全部证据的证明。哈希只证明指定文件完整性，不证明资产可交付或问题已关闭。
- 新候选的实际作者修改与本插件修复是不同交付范围；本汇总不代表批准接入正式 Gameplay DA，
  也不授权重放生产请求、覆盖现有资产、提交代码或推送远端。
