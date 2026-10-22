# Niagara 写入命令在资源验证期间重入保存，导致流送断言与 D3D12 崩溃

> 维护进展（2026-10-22）：修复已部署并编译到 SkillTest/NF；仓库 6 项相关原生回归、
> 74 项 Python 回归，以及部署后 SkillTest 的 6 项原生回归、独立进程复制夹具冷加载、
> 两项目真实 MCP 读验收均通过。**原报告等价连续作者批次与实际验证／渲染等待链
> 复现验收仍未完成，因此问题仍为 Open，不能宣称 D3D12 崩溃已经消除。**
> 下文“未修改插件”等表述保留的是原始反馈时的状态；最新范围见文末维护记录。

## 状态与结论边界

- **状态：Open / 已复现两次 / 未修改插件，等待维护者修复。**
- 报告日期：2026-10-22，时间均同时注明 UE 日志时间和本地 UTC+08:00。
- 影响：正常、串行的 Niagara 参数配置过程中 Unreal Editor 退出，Python MCP 收到连接重置；部分修改已经落盘，部分尚未保存。
- **已确认的缺陷路径：MCP 的 game-thread command lambda 被执行在 DataValidation / Slate / RenderCore 的调用栈内部，并在该重入上下文中调用 `UPackage::SavePackage`。保存触发 `!IsAssetStreamingSuspended()` 断言。**
- 随后发生的 D3D12 worker 访问违例与该断言、递归渲染等待出现在同一帧。调度／保存重入需要优先处理，但**尚未证明它是随后 D3D12 访问违例的唯一原因**；没有提交或验证任何修复。
- 这不是“命令没有在 Game Thread 执行”。代码确实派发到了 Game Thread，问题在于**执行时机可被等待中的 task pumping 重入**。

用户要求将问题和证据反馈到本目录，由用户维护 MCP 代码。本次反馈不修改插件、引擎、驱动、验证配置或渲染设置，也不继续重试失败的资产写入。

## 环境及源码一致性

| 项目 | 实际值 |
|---|---|
| Unreal Engine | `5.7.4-51494982+++UE5+Release-5.7`，Windows Editor Development |
| Engine 路径 | `C:\01_install\UE5_7\UE_5.7` |
| 使用工程 | `C:\04_unrealProjects\NodeFall\NodeFall.uproject` |
| OS | Windows 11 25H2，`10.0.26200.9711` |
| CPU | AMD Ryzen 9 9900X |
| RHI | D3D12；日志枚举到 RTX 5080、AMD 核显和虚拟显示适配器，适配器选择细节见原始日志 |
| NVIDIA 驱动记录 | `610.88 / 32.0.16.1088`，日期 `2026-07-22` |
| Python MCP | 当前仓库 `Python`，正常 stdio 桥接，grouped tool mode |
| Native endpoint | 本地 `127.0.0.1:13090`，每次都核对为对应 NF Editor PID |
| 访问方式 | `niagara_call_write(tool=..., arguments=...)`；没有绕过工具直接发送 raw TCP |
| Writer | 一个顺序执行的客户端；每次等待上一个调用返回再发下一次，没有并发资产写入 |
| MCP 仓库 HEAD | `aac7d06`，`add reparent umg mcp tool` |

实际运行插件源码位于：

```text
C:\04_unrealProjects\NodeFall\Plugins\UnrealMCP\Source\UnrealMCP
```

维护仓库对应源码位于：

```text
E:\04_projects\unreal-mcp-bo\MCPGameProject\Plugins\UnrealMCP\Source\UnrealMCP
```

报告时两个位置的下列文件 **SHA256 完全一致**，不是“仓库已有修复但 NF 源文件仍旧”的情况：

| 相对源码路径 | SHA256 |
|---|---|
| `Private\UnrealMCPBridge.cpp` | `DCD037A038E2C311B2B6740E702B061D9FD54AD9D14A60F952EE53198459C736` |
| `Private\Commands\UnrealMCPNiagaraCommands.cpp` | `934983094A0FA12FB35EDF1442BF082B6E0309F8C30E2A9EDB2816FFD0288329` |

修复后仍需把修复部署、编译到 NF 实际加载的插件 DLL。只修改维护仓库或重启 Python server，不会自动更新当前 Editor 的 native DLL。

## 工作场景与资产

原始任务是在 NF 中制作一个独立 Lv3 升级特效候选，不修改正式 Gameplay 配置。

```text
新草稿：
/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Lv3

复制来源：
/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Normal

首次发生时的编辑器地图：
/Game/Dev/VFX/Maps/L_VFX_UpgradeReview

第二次发生时的编辑器地图：
/Engine/Maps/Entry
```

新系统从正常工作的五层普通候选复制，移除复制件中的 `UpperRing`，
新增标准 Minimal CPU emitter：`CommitPulse`、`OrbitArc`、`OrbitAccent`。
最终目标为七个 CPU / Local Space emitter。
另创建专用 Unlit/Additive 发光材质及七个 MI；所有写入只针对新候选。

**新候选从未分配给 Dev 预览控制器，也未在场景里播放。**
第一次地图里显示的是已经验证过的第三方原版；
第二次特意在 Entry 空场景中进行作者操作，没有那两台 Dev 预览 Actor。
因此关闭该预览场景并不能避免重现。

## 复现经过

### Run 1：Dev 场景

- UE 日志时间：`2026.10.22-03.54.55`；本地约 `11:54:55 +08:00`。
- 当时 Editor PID：`31164`。
- 使用普通的串行 module input / switch / curve / renderer 配置请求。
- 在已经完成一段配置后，下面的调用收到连接重置：

```json
{
  "tool": "niagara_call_write",
  "arguments": {
    "tool": "enable_module_input",
    "arguments": {
      "asset_full_path": "/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Lv3",
      "emitter_name": "RisingBits",
      "module_name": "InitializeParticle",
      "input_name": "Lifetime",
      "initial_value": 1.2,
      "script_type": "spawn"
    }
  }
}
```

调用栈明确落在：

```text
SaveNiagaraSystemAsset()                          UnrealMCPNiagaraCommands.cpp:1224
FUnrealMCPNiagaraCommands::HandleEnableModuleInput UnrealMCPNiagaraCommands.cpp:2958
FUnrealMCPNiagaraCommands::HandleCommand           UnrealMCPNiagaraCommands.cpp:212
UUnrealMCPBridge::ExecuteCommand::<lambda>         UnrealMCPBridge.cpp:459
... Core / RenderCore / D3D12RHI / SlateRHIRenderer / Slate ...
... DataValidation ...
... Engine / UnrealEd / editor main loop ...
```

### Run 2：空场景恢复后

- UE 日志时间：`2026.10.22-04.06.32`；本地约 `12:06:32 +08:00`。
- Editor PID：`30656`。
- 正常重启 NF，但入口为 `/Engine/Maps/Entry`。
- 先只读已保存的草稿和材质，按真实磁盘状态恢复未保存的材质图。
- 随后继续同类串行配置，仍发生相同断言和 D3D12 访问违例。

**注意最后一个客户端请求和断言栈不是同一个精度的证据：**

- 栈中的断言仍发生在前一个 `enable_module_input` 的 `SaveNiagaraSystemAsset` 中。
- 对应的最后一条已返回请求是 `SpawnBurst_Instantaneous.Spawn Time = 0.12`。
- 该保存出现断言后，日志仍记录临时文件移动和 response 发送。
- 客户端接着发出 `set_module_static_switch(Color Mode = Direct Set)`，这时进程已经开始崩溃，最终这个请求收到连接重置。
- 所以不能把问题误诊为 `Color Mode` 的枚举值本身导致崩溃。

第二次末尾请求顺序：

```json
[
  {
    "tool": "enable_module_input",
    "arguments": {
      "asset_full_path": "/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Lv3",
      "emitter_name": "RisingBits",
      "module_name": "SpawnBurst_Instantaneous",
      "input_name": "Spawn Count",
      "initial_value": 24,
      "script_type": "emitter_update"
    }
  },
  {
    "tool": "enable_module_input",
    "arguments": {
      "asset_full_path": "/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Lv3",
      "emitter_name": "RisingBits",
      "module_name": "SpawnBurst_Instantaneous",
      "input_name": "Spawn Time",
      "initial_value": 0.12,
      "script_type": "emitter_update"
    }
  },
  {
    "tool": "set_module_static_switch",
    "arguments": {
      "asset_full_path": "/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Lv3",
      "emitter_name": "RisingBits",
      "module_name": "InitializeParticle",
      "switch_name": "Color Mode",
      "value": "Direct Set",
      "script_type": "spawn"
    }
  }
]
```

这里列出的短请求不是已验证的“一次调用必现”的最小复现；目前证据是两次连续作者会话。
附件中的长批次依赖当时部分已配置的本地 NF 资产，**不要原样重放到生产工程，也不要盲目从头重建已有路径**。
建议维护者在隔离工程用标准 Niagara 系统／材质构造等价批量编辑夹具，并加入保存后验证与任务泵送情形。

## 共同的日志特征

两次均出现：

```text
FlushRenderingCommands called recursively! 2 calls on the stack.

Ensure condition failed: !IsAssetStreamingSuspended()
StreamingManagerTexture.cpp, Line: 2099

Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000260
UnrealEditor-D3D12RHI.dll
Crash in runnable thread Foreground Worker #0 / #1
```

RHI breadcrumbs 指向：

```text
SlateUI Title = <none>
RenderGraphExecute - Slate
```

不是日志里明确命名的 Niagara 游戏渲染 pass。
完整日志保留了模块地址；中间引擎函数目前仍有 `UnknownFunction`，没有完成所有地址的符号化。

两次启动还都有一个较早的 `NativeGameplayTags.cpp:41` Editor-module tag ensure。
它发生在启动阶段，不是本报告这里的流送／保存断言，不应混淆。
首轮还存在 curve LUT `Table sizes don't match / Post CopyToInternal LUT generation is out of sync` 消息；
这些也保留在日志中，尚未证明与随后 RHI 崩溃有因果关系。

## 本地代码分析：确认的重入点

### 1. Bridge 把所有命令投递为可在等待中执行的 GT task

`Private\UnrealMCPBridge.cpp:228-237`：

```cpp
TPromise<FString> Promise;
TFuture<FString> Future = Promise.GetFuture();
AsyncTask(ENamedThreads::GameThread,
    [this, CommandType, Params, Promise = MoveTemp(Promise)]() mutable
    {
        // Command handlers, then Promise.SetValue(...)
    });
```

函数尾部是 `return Future.Get()`。
当前忙碌检查只覆盖 scene capture/import 任务，不覆盖保存、资源验证、慢任务／渲染等待的重入上下文。

### 2. Niagara setters 每次编译后立即同步保存

`Private\Commands\UnrealMCPNiagaraCommands.cpp:2954-2959`，
`HandleSetModuleStaticSwitch` 的 `3253-3257` 也有同样模式：

```cpp
System->RequestCompile(true);
System->WaitForCompilationComplete();
System->MarkPackageDirty();
SaveNiagaraSystemAsset(System);
System->PostEditChange();
```

`SaveNiagaraSystemAsset` 在 `1212-1224` 使用 `UPackage::SavePackage`。
这解释了为何“只是设置一个输入”也会进入保存、保存后验证和 UI 等待链。

### 3. 调用栈显示操作被嵌入验证／Slate 等待链

按堆栈层次还原，部分引擎函数名尚未解析：

```text
Editor main loop
  -> DataValidation
    -> Slate / SlateRHIRenderer
      -> D3D12RHI / RenderCore wait or task processing
        -> MCP GameThread lambda
          -> HandleEnableModuleInput
            -> SaveNiagaraSystemAsset
              -> UPackage::SavePackage
                -> Engine streaming wait
                  -> ensure(!IsAssetStreamingSuspended()) fails
```

这证明“已经切到 Game Thread”不足以保证编辑器操作在安全边界执行。
客户端单请求串行，也不等于命令不会嵌入上次保存后触发的验证／等待流程。

相关 UE 源码：

- `Engine\Source\Runtime\Engine\Private\Streaming\StreamingManagerTexture.cpp:2099`：
  `FRenderAssetStreamingManager::BlockTillAllRequestsFinished` 内有上述 ensure。
- `Engine\Plugins\Editor\DataValidation\Source\DataValidation\Private\EditorValidatorSubsystem.cpp:498`：
  验证流程创建 `FScopedSlowTask`，非 silent 时显示进度窗口。
- 同模块 `EditorValidator_Material.cpp:259` 有 `FAssetCompilingManager::Get().FinishAllCompilation()`。

### 4. 相邻可靠性问题（不等于已证实的崩溃原因）

上述两个 setter 当前没有检查 `SaveNiagaraSystemAsset` 的 bool 返回值，
之后仍构造 success 响应。修复时应避免实际保存失败却向调用者报告成功。
同时，改变自动保存语义属于兼容性变更，不能为了绕过崩溃直接移除保存而不更新契约。

## 已尝试与未做的事情

已尝试：

1. 单 writer、顺序等待回执，仍发生首次崩溃。
2. 正常重启到 Entry 空场景，不运行 Dev 预览 Actor。
3. 先读回磁盘状态，只恢复未保存的新材质图，不重复创建现有资产。
4. 空场景继续同类配置，第二次出现相同重入保存／流送断言。
5. 第二次后停止资产写入，恢复已保存 Dev 场景，保留日志和部分草稿。

未做：

- 未改 MCP、UE 引擎或显卡驱动。
- 未切换 D3D11、NullRHI，未关闭 RHI thread、资源流送或 DataValidation。
- 未增加 asset writer、作者 commandlet 或 raw TCP 调用以绕过现有工具。
- 未通过无限重试把不确定的写入当作未执行。
- 未对新草稿做视觉验收、接入正式 DA 或宣称制作完成。

## 建议修复方向及验收条件

以下是设计建议，**不是已实施或已验证的补丁**：

- 将外部编辑命令序列化到明确的编辑器主循环安全点，
  避免普通 GameThread task 在 Slate／渲染等待／保存验证里被重入执行。
  可评估帧边界队列；不要仅把“线程正确”当作“时机安全”。
- 处理保存、GC、慢任务／验证等不安全状态，明确返回 busy/deferred/cancelled，
  不得在拒绝前先修改资产，也不要用吞异常或关闭验证掩盖问题。
- 保留已有参数 schema、类别权限、read-only 过滤、scene-task 互斥及调用顺序。
- 队列化方案须处理 Editor 关闭、server stop、未完成 promise、超时和 UObject 生命周期，
  避免修复重入后引入等待 Future／join server thread 的死锁。
- 验证所有自动保存返回值；失败应明确报告是否已修改、是否已保存。

建议回归：

1. 可控地在验证／慢任务／render-wait 会泵送 GT tasks 时发送下一条 MCP 命令；
   证明它不会嵌套执行资产修改／保存。
2. 在隔离工程重复原型批次：复制系统、改 static switch、改 RI 常量、
   更新已有 curve keys、新建动态输入、改 renderer、保存并重新加载。
3. DataValidation 保持开启，D3D12 保持原配置；不再出现递归 flush、流送断言或 RHI 访问违例。
4. 请求响应和磁盘结果一致；有 transport timeout/reset 时能区分未知执行状态，不自动重放创建。
5. 待执行命令期间关闭 Editor、停止 server，确认 promise 释放、线程不死锁。
6. 修复部署回 NF 后，对当前部分草稿先读回，再完成剩余配置；最后用受控 Dev 摄像机确认真实出画。

## 证据文件

提交前整理说明（2026-10-22）：原始日志、请求／回执和哈希清单不纳入 Git，
12 个文件已原样归档到仓库本地的下列目录。该目录由 `Saved/` 规则忽略；
原 `Docs/reported-issues/evidence/` 路径也已加入忽略规则。以下文件清单是本地归档索引，
不是从 Git 克隆后应存在的附件；具体故障和验证结论已保留在本报告及目录汇总中。

```text
MCPGameProject/Saved/ReportedIssues/2026-10-22/niagara-save-reentrancy/
  run-1-dev-scene.log
  run-2-empty-scene.log
  run-1-requests.json
  run-1-receipts.json
  run-2-requests.json
  run-2-receipts.json
  recovery-disk-state.json
  SHA256.json
```

日志是完整本地副本，含本机和工程路径；对外发布前应按需要清理。
没有复制第三方 `.uasset`、引擎二进制或项目制作素材到 MCP 仓库。
`SHA256.json` 用于确认附件拷贝完整，不是资产是否可交付的证明。

首轮 ensure 的关键段在 `run-1-dev-scene.log` 约 `7902` 行，
第二轮在 `run-2-empty-scene.log` 约 `2703` 行。
对应的 `SaveNiagaraSystemAsset -> HandleEnableModuleInput -> ExecuteCommand lambda`
以及外层 `DataValidation / Slate / RenderCore` 栈都在其后。

## 当前 NF 状态与后续恢复注意事项

- Editor 已正常恢复到保存的 `/Game/Dev/VFX/Maps/L_VFX_UpgradeReview`，
  显示先前第三方 `P_UpgradeGlow5_Converted`，不是新草稿。
- `NS_TowerUpgrade_Lv3`、专用 MeshGlow 材质和七个 MI 已存在；
  **新系统配置未完成，不能接正式 DA，也不要直接全量重跑创建脚本。**
- 恢复时曾发现 MeshGlow 磁盘版本只有空图，之后已恢复并保存七节点图和专用 MI。
  七层系统已经存在，但 `RisingBits` 最后写入是否落盘以及新 pulse/orbit 层配置必须重新读回。
- 原普通升级系统／材质、借用源网格、第三方系统、生产 DA 和正式地图的保护哈希均未改变。
- 本次没有提交／推送代码，没有修改插件；维护者修复后再继续 VFX 制作。

## 维护记录：仓库候选修复（2026-10-22）

### 已实现

- Bridge 不再使用普通 `AsyncTask(GameThread)` 执行外部命令。线程安全 FIFO
  队列仅由引擎主循环 `FCoreDelegates::OnBeginFrame` 取出，每个边界最多执行一条，
  且已有命令执行时拒绝嵌套取队。原有命令路由、参数、权限过滤和 Scene 互斥保留。
- 保存、GC、慢任务或模态窗口期间不取队。队列最多 64 条；排队 20 秒仍未开始则
  取消，不在客户端已等待超时后悄悄开始修改。GT 上的直接递归调用明确拒绝。
- 停止服务器前先释放待执行请求；已经开始的命令若遇到嵌套停止，释放线程等待者并
  报告 `execution_state=unknown`，不伪报 `modified=false`/`saved=false`，不尝试
  中途回滚或重放。原命令后续返回不会再次完成同一个 Promise。
- 原先忽略结果的 16 个 Niagara 自动保存调用均检查返回值。自动保存语义没有移除；
  失败或保存上下文不安全时返回 `success=false`、`stage=save`、
  `error_code=niagara_save_failed`、`modified=true`、`saved=false`、资产路径及脏状态。
- Bridge 保留所有 Niagara 的失败回执。Python 保留保存／调度状态；无响应、超时或
  连接重置标为结果未知，不推断“没有执行”，不自动重试。

### 已验证

- `MCPGameProjectEditor Win64 Development` 构建成功；仅在仓库测试工程运行独立
  `/Engine/Maps/Entry`、`-UnrealMCPNoServer -RenderOffscreen` 进程，没有更改引擎、
  驱动、RHI、资源流送或 DataValidation 配置。
- `UnrealMCP.Transport.CommandQueue`：任务泵不能提前执行排队工作，FIFO、非重入、
  过期取消、停止后提交、重启、执行中停止释放等待者及 Promise 单次完成。
- `UnrealMCP.Transport.FrameDispatchSafety`：真实 Bridge 在慢任务／保存标记期间
  不取队，安全边界可返回正常结果，GT 递归入口被拒绝，停止能释放等待线程。
- `UnrealMCP.Transport.FragmentedSocket` 和 `RequestBuffer`：实际 socket 到新 Bridge
  的请求／响应及分片 JSON 解析仍正常。
- `UnrealMCP.Niagara.SaveFailure`：仅测试可触发的受控保存失败，确认修改留在内存、
  磁盘哈希未变、回执不报成功，检查后显式保存能恢复。不是实际操作系统写盘故障测试。
- `UnrealMCP.Niagara.CrossSystemEmitterCopy`：原有 Emitter 复制、编译及实际保存回归。
- 六项测试均明确输出 `Result={Success}`；最终日志为仓库测试工程
  `Saved/Logs/MCP-Reentrancy-Save-Final.log`，该轮未出现递归 flush、ensure 或 fatal。
- 完整 Python 回归 74 项通过；其中新增检查覆盖两个报告 setter 的部分保存失败、
  未开始取消、超时／连接重置及只发送一次。

### 仍未验收

- SkillTest 和 NF 已按文末记录部署；仅修改仓库或重启 Python 仍不能替代未来的 native 部署。
- 尚未在 SkillTest 重建报告等价的多模块／RI／曲线／动态输入／renderer 连续作者批次，
  也未验证真实 DataValidation + Slate + RenderCore 等待链中不再出现原崩溃。
- 尚未完成上述等价批次的保存后独立进程重新加载；现有复制／保存回归不能代替它。
- 未重试 `NS_TowerUpgrade_Lv3`，未修改 NF 资产、预览 Actor、正式 DA 或生产地图。
- 没有证明随后 D3D12 worker 访问违例的唯一原因，也不把本轮单元／集成回归视为
  原生产崩溃已经修复的证明。剩余连续作者场景验收仍需后续执行。
- 仓库修改未提交、未推送；原始日志、请求、回执附件未改动。

## 部署记录（2026-10-22）

- 用户明确授权部署 SkillTest 和 NF。预检两个 Editor 均已正常关闭，没有调用保存、
  关闭或强杀原 NF 会话。NF 最近实际地图为 `/Game/Dev/VFX/Maps/L_VFX_UpgradeReview`。
- 两项目均为 UE 5.7.4，使用 `C:\01_install\UE5_7\UE_5.7`；插件来源为本维护仓库。
  仅部署 `MCPCommandQueue.h`、Bridge 实现／头、Niagara 命令和传输测试五个文件。
  四个旧文件在部署前均与仓库提交基线一致；NF 的导航、Build.cs、迁移及其他测试定制保留。
- 两项目备份均位于各自 `Saved/UnrealMCPBackups/Reentrancy_20261022_160318`，包含旧
  Source、Binaries、描述文件、保护哈希与 Content 清单。旧插件 Intermediate/Binaries
  清理后重新构建，不复用旧 DLL，也没有复制 Python 到目标项目。
- `SkillTestEditor Win64 Development` 最终构建成功，482.83 秒。首轮 MSVC 编译器停滞，
  只清理了本次 SkillTest UBT／cl 子进程后按原参数重试；未修改调试信息、引擎或驱动配置。
- SkillTest 部署 DLL 的上述六项回归全部成功，日志 `Saved/Logs/MCP-Reentrancy-Deployment.log`。
  新进程 `CrossSystemEmitterCopy` 冷加载成功，日志 `MCP-Reentrancy-ColdReload.log`；夹具为
  `/Game/__Dev/EmitterCopy_Reentrancy_15b74efed876430eb4c81f800a3d15e9`。
  这只验证复制／保存夹具的持久化，不代替原报告完整连续作者批次的冷加载。
- SkillTest 使用现有 VS Code 配置，新 stdio 会话发现 70 工具，读取隔离 `NS_Target`
  的两个 emitter 和 `BP_ThirdPersonCharacter` 成功，前后脏包为空。随后正常关闭，保存数 0。
- `NodeFallEditor Win64 Development` 构建成功，46.58 秒。NF 已恢复上述原地图，
  正常监听 `127.0.0.1:13090`；本次验收 PID 为 41864，后续操作仍须重新核对。
- NF 现有 `.mcp.json` 新 stdio 会话发现 62 个过滤后工具；只读普通升级系统
  `NS_TowerUpgrade_Normal` 的五个 emitter、读取 `WBP_TowerActionButton` 成功。
  两个报告 setter 仅做契约发现，没有执行写入，没有重试或续做 Lv3 草稿；脏包始终为空。
- SkillTest 9269 个、NF 4613 个保护文件哈希全部未变；NF Content 无新增／删除。
  SkillTest 仅新增本次 `__Dev` 三份测试资产。两项目客户端配置、权限、用户技能未改动。
- NF 保持运行，SkillTest 关闭以避免同端口冲突。现有 MCP 客户端需重连；Claude CLI
  仍显示 NF 项目服务器 `Pending approval`，需用户自行批准。独立 stdio 烟测不代替信任审批。

## NF 实际草稿续作反馈（2026-10-22，16:39 后）

用户告知修复部署完成后，原 NF 作者会话通过正常 stdio 重新连接并继续原来的
`NS_TowerUpgrade_Lv3`，没有修改维护者补丁、客户端权限、RHI 或验证配置。

- 首先读回当前资产：七层 NS、七节点 MeshGlow 图存在，后三层尚未配置，
  RisingBits.Lifetime 仍为1.1。单独执行原来的 Lifetime1.2 和 Color Mode setter，
  实际读回成功，未重放创建操作。
- 随后只补缺项，共102条连续调用：80条 Niagara 写入、9条显式保存、
  8条材质读取、2条 Niagara 读取及3条状态读取。
  涵盖 static switch、RI 常量、已有曲线键、新动态输入、网格／材质 Renderer 以及编译保存。
- 完成的新系统为七层 CPU/Local Space，一次性循环状态；在真实 Dev 视口取得了
  0.28秒（量化为0.2833）、0.75秒、1.8秒及从完成态回退的1920×1080图像。
  Lv3在1.5167秒complete，结束图无残留，不只是组件引用／输入值存在。
- 所有任务资产和比较地图显式保存，unsaved0后正常关闭原进程PID41864；
  不经重新构造资产，独立启动PID22672冷加载同一地图。
  七层系统、已编译七节点材质图、动态Alpha绑定、Actor引用与时间均持久化，
  冷进程实际出画成功。
- 原有普通系统／材质、源网格、第三方效果、生产DA和正式地图按保护基线核对，
  没有被本轮续作覆盖。新候选尚未接入正式DA。
- 本轮续作日志片段与冷进程日志均未出现三类原始标记：
  `FlushRenderingCommands called recursively`、`!IsAssetStreamingSuspended()`、
  `EXCEPTION_ACCESS_VIOLATION`。不等于声明整个日志没有任何历史启动警告。

新增本地证据（旧附件和旧SHA256清单未覆盖；与前述证据一起归档，不随 Git 提交）：

```text
MCPGameProject/Saved/ReportedIssues/2026-10-22/niagara-save-reentrancy/
  nf-postfix-authoring-success.log
  nf-postfix-resume-receipts.json
  nf-postfix-cold-inspect-receipts.json
  NF-Postfix-SHA256.json
```

**范围边界：**这是原 NF 草稿的真实连续续作、保存及独立冷加载验收，提供修复的生产使用
正向证据。没有从头重建原171操作批次，也没有额外人工构造旧的
DataValidation/Slate/RenderCore嵌套等待栈；因此不擅自关闭此报告，仍由维护者决定
完整故障回归和随后D3D12访问违例的结论。
