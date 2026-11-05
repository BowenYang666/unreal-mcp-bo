# 跨系统复制Particle Reader尾迹保留源私有DI，失败后删除触发崩溃

## 状态与结论边界

- **状态：维护源码修复及隔离回归通过，已构建部署SkillTest／NF并完成分范围公开验收。下文原始反馈与结论边界保留。**
- 原始发生时间：2026-11-05；报告日期同日。
- 这是跨系统独立快照问题，不与[同系统Emitter复制不执行](same-system-emitter-duplicate-not-executed.md)混为一项。
- 已确认：复制尾迹后，目标SpawnScript仍强引用源SystemUpdateScript下的私有Particle Read DI；
  reference_preflight正确阻止了保存，但目标已被修改并留下未保存层。
- 随后通过公开remove_emitter_from_system删除该失败副本时，SkillTest发生Niagara访问违例并退出。
- **尚未证明**删除崩溃的唯一原因就是这一条私有引用；没有受控隔离ViewModel、
  活跃预览、编译／缓存和依赖图因素，也没有再次重现该崩溃。
- 本轮文档反馈不修改插件、引擎或业务VFX，不重放故障路径。

独立作者接口的缺口见
[Particle Attribute Reader读取／创建／绑定](niagara-particle-attribute-reader-authoring.md)。

## 环境和真实资产

| 项目 | 实际值 |
|---|---|
| UE | `5.7.4-51494982+++UE5+Release-5.7`，Windows Editor |
| 工程 | `E:\04_UnrealProjects\UE5Test\SkillTest\SkillTest.uproject` |
| 当次Editor／端口 | PID22044，127.0.0.1:13091 |
| 当前维护仓库HEAD | `82caaf1`；不是原故障时已加载DLL的构建身份声明 |
| 调用方式 | stdio／grouped MCP，串行调用，未用raw TCP |
| 场景 | `/Game/Dev/VFX/Maps/L_CannonVFX_Review`；当时有该候选的编辑器预览及资产编辑器 |

```text
源：
/Game/ThirdParty/NiagaraExplosion01/Niagaras/Air/N_ExplosionAir_004
  NE_SparkTrails_Head001
  NE_SparkTrails_Trails001

目标：
/Game/Shared/VFX/Explosions/NS_CannonExplosion
```

Trails001通过Particle Attribute Reader读取Head001；
来源名由用户在UE界面确认。复制时保留同样的Emitter名称，
不能仅因两层名称匹配就认定嵌套DI和编译数据已完成本地化。

## 发生顺序

1. 目标五层候选已保存且无脏包，精确备份保留。
2. Head001通过add_emitter_to_system跨系统复制成功并保存。
3. 使用同一公开接口复制Trails001，返回modified=true／saved=false及reference_preflight拒绝。
4. 只读检查确认内存有七层，目标包为dirty；原五层与复制前完全一致。
5. 尝试仅删除本轮失败的Trails001时，连接重置；原生日志确认访问违例，
   而非仅因Python超时就推断Editor崩溃。
6. 未重试复制或删除，也未通过强行save绕过私有引用检查。

复制请求的内部操作形状，外层为niagara_call_write：

```json
{
  "tool": "add_emitter_to_system",
  "arguments": {
    "asset_full_path": "/Game/Shared/VFX/Explosions/NS_CannonExplosion",
    "source_asset_full_path": "/Game/ThirdParty/NiagaraExplosion01/Niagaras/Air/N_ExplosionAir_004",
    "source_emitter_name": "NE_SparkTrails_Trails001",
    "new_emitter_name": "NE_SparkTrails_Trails001"
  }
}
```

实际关键回执：

```json
{
  "inheritance_mode": "independent_snapshot",
  "modified": true,
  "saved": false,
  "success": false,
  "stage": "reference_preflight",
  "error": "Copied emitter still references private source objects; target not saved. Inspect references before retrying",
  "private_references": [{
    "referencer": "/Game/Shared/VFX/Explosions/NS_CannonExplosion.NS_CannonExplosion:NE_SparkTrails_Trails001_5.NE_SparkTrails_Trails001_5.SpawnScript_35",
    "reference": "/Game/ThirdParty/NiagaraExplosion01/Niagaras/Air/N_ExplosionAir_004.N_ExplosionAir_004:SystemUpdateScript.NiagaraDataInterfaceParticleRead_7"
  }]
}
```

这是目标脚本指向**源包的私有UObject**，不是缺少一张可公开引用的贴图。
不能通过增加迁移root、保留整个源系统或关闭检查来宣称独立快照已成立。

## 删除时的实际崩溃

清理操作：

```json
{
  "tool": "remove_emitter_from_system",
  "arguments": {
    "asset_full_path": "/Game/Shared/VFX/Explosions/NS_CannonExplosion",
    "emitter_name": "NE_SparkTrails_Trails001"
  }
}
```

Python收到`WinError 10054`。UE日志：

```text
UTC 2026.11.05-02.12.16:248 / 本地10:12:16 +08:00
UnrealMCPBridge: Executing command: remove_emitter_from_system

UTC 2026.11.05-02.12.20:430 / 本地10:12:20 +08:00
Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x000000000000000c
UnrealEditor-Niagara.dll
UnrealEditor-NiagaraEditor.dll ... 多帧
UnrealEditor-Niagara.dll ... 多帧
FUnrealMCPNiagaraCommands::HandleRemoveEmitterFromSystem()
  SkillTest\Plugins\UnrealMCP\Source\UnrealMCP\Private\Commands\UnrealMCPNiagaraCommands.cpp:2117
UUnrealMCPBridge::ExecuteCommandOnGameThread()
FMCPCommandQueue::RunNext()
```

本次不是之前报告的`!IsAssetStreamingSuspended()`断言或已证实的D3D12 worker故障；
不要直接套用保存重入问题的根因结论。

## 当前源码中值得维护侧核对的路径

`MCPGameProject\Plugins\UnrealMCP\Source\UnrealMCP\Private\Commands\UnrealMCPNiagaraCommands.cpp`：

- 跨系统分支约1882～1942行：等待源／目标编译，调用AddEmitterToSystem，
  RemoveParent／改名，再PostEditChange／RequestCompile／WaitForCompilationComplete，
  **之后**遍历目标包引用并检查源私有对象。
- 因此本次不是“只差再编译一次”的已证实结论；现有分支检查前已经请求并等待过编译。
- 被指向对象位于源SystemUpdateScript下，强烈提示需要同时核对作者图DI和解析／编译DI缓存的
  所有权／重建；未确定究竟是哪一步迁移不完整。
- 删除分支约2083～2120行：RemoveEmitterHandle、SyncOverviewGraph、
  InvalidateCachedData、RequestCompile、WaitForCompilationComplete、保存和PostEditChange。
  旧部署栈2117位于该附近；行号不能代替更完整的带符号调试。
- 失败复制没有自动移除新Handle；用户面对的是已经修改的dirty目标，
  而当时清理接口又不能安全完成此恢复过程。

报告时NiagaraCommands源码SHA256：

```text
D42C353E55B9400A30EFD4D7D79FE16552F183920F0682D0947F53A8D30B1836
```

未校验旧进程实际DLL构建身份，不将当前文件hash当作旧DLL的符号证明。

## 恢复结果与当前业务边界

经用户批准重开Entry只读核对；Editor先停在Restore Packages，
用户明确选择不恢复未保存副本，进入正常循环后才继续读。
最初的项目绑定未验证回执发生在恢复窗口阻塞阶段，不能据此断言另有部署身份缺陷。

冷读取证实：

- 原五层逐字段匹配复制前基线；
- 磁盘上只有原五层＋已成功保存的Head001，共六层；
- 失败Trails001没有落盘，无脏包；
- 此次只读核对前后目标文件SHA256相同。

后来用户拒绝手动原生复制，选择把有效独立层重搭为简单重力火花。
这避免了再次执行故障复制，但**没有修复MCP，也没有实现原Trails001的沿轨迹子火花**。
本轮新需求正被上述作者能力缺口阻塞，不能拿独立火花的照片宣称源依赖问题已解决。

## 验收建议

1. 在隔离系统中构建“少量头粒子＋Particle Reader子粒子”的可见最小夹具，
   跨系统复制并正确处理源名重映射和图／编译DI所有权。
2. 复制后所有非公开DI归目标所有，保存／独立冷加载后仍沿目标的头粒子生成子粒子；
   源包hash不变，无源系统运行依赖。
3. 保留私有引用阻止保存的检查，禁止以禁用检查或强行保存为修复。
4. 对拒绝路径给出真实before／after、created Handle／GUID、dirty／saved和明确阶段；
   能事务回退则验证回退，否则应提供经过验证的安全恢复路径。
5. 在活跃预览／资产编辑器存在以及无预览的条件下，分别验证成功副本删除、
   失败副本清理和带跨Emitter依赖的删除顺序；不能只测空Emitter。
6. 删除、Undo、编译及清理不能访问失效数据，也不能误删旧Handle／其他用户更改。
7. 覆盖来源缺失／改名、复制多次、stale状态、无关脏包及保存失败；
   不重放生产素材批次来替代隔离回归。

## 本地证据

忽略目录：

```text
Docs\reported-issues\evidence\2026-11-05\niagara-particle-reader\
  copy-reference-refused.json
  cleanup-connection-reset.json
  recovery-cold-read.json
  copy-remove-crash.log
```

原始会话记录位于：

```text
C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files\
  cannon-air004-arc-structure-0949\
  cannon-air004-copy-failure-cleanup-0949\
  cannon-air004-recovery-after-dialog-0949\
  cannon-air004-copy-remove-crash-0949.log
```

原始日志保留为本地忽略附件，不上传、自动提交或复制UE资产包。

## 维护侧修复与验证（2026-11-05）

隔离标准Reader双层夹具受控复现同型私有引用拒绝：复制目标的SpawnScript仍引用
源SystemUpdateScript下的ParticleRead DI，保存未执行。修复仅在新增层中，把残留缓存
引用映射到唯一等价的当前目标作者Reader；按节点实际DataInterface属性及直属Emitter
识别当前配置，排除孤儿默认DI和嵌套合并快照。仍检查目标真实序列化引用可达对象中的
源私有引用，不把移除Handle后不可达的残留快照当作活动导出，也不删除这些对象。

复制前拒绝脏目标和缺失／歧义的依赖源；改名需要显式源／目标Handle GUID映射，
不按相似名或尾缀猜测。回执增加created_handle_id及实际名称／dirty状态。
删除改走已导出的NiagaraEditorModule／SystemViewModel::DeleteEmitters原生流程，
先处理预览／实例及执行图生命周期，再编译／保存。支持expected_handle_id和显式save=false；
默认保存不能带入预存用户草稿，仍被Reader依赖的源层不允许先删除。

已验证：有／无预览删除；真实可达源私有Reader的私有故障注入触发保存拒绝后，
活跃预览下精确GUID无保存清理、原目标磁盘字节不变、其余Handle保持，再次复制成功。
另含保存失败、Undo／Redo、stale／dirty／错源／自采样、源改名映射、实际粒子位置和
结束态。100项Python、11项实际原生回归及明确夹具的独立冷读通过。
最终源日志：MCPGameProject/Saved/Logs/Niagara-ParticleReader-Final-Regression.log、
Niagara-ParticleReader-Final-Cold.log；受控红例为Niagara-ParticleReader-Copy-Red.log。
未重放NS_CannonExplosion或原市场素材故障路径，未修改原证据或引擎。
旧崩溃的唯一因果仍未通过带符号调试确定；本次关闭的是已验证的工具生命周期缺陷，
不是宣称所有Niagara访问违例已根治。需匹配native DLL，双项目部署完成另记。

## 双项目部署（2026-11-05）

SkillTest仅更新三份Niagara文件，并在其有差异的Bridge中新增两条Reader路由，
路由外文本与备份精确保持；6动作增量构建871.83秒成功。实际DLL11项矩阵及独立冷读
通过；原绑定VSCode配置公开读取／预览／同值／stale拒绝，以及新__Dev副本中新本地
Reader创建、编译、读回和保存通过，源样例哈希／地图／视口及dirty0保持。
最新用户GUI原地图为Lvl_ThirdPerson，按INI13091恢复，不强行切回更早Review地图。

NF按修复前基线只更新四份原生文件，其他定制保留。首次正常close saved0后，另一会话
重开NF导致DLL链接占用；用户再次批准维护窗口，重新fresh检查dirty0并正常关闭saved0，
4个链接动作4.79秒成功。恢复实际L_map01／INI13090；原绑定Claude配置验证新native合同、
Reader精确身份预检拒绝、样例哈希／上下文／dirty0保持及Blueprint列表／读取通过。
NF未创建或删除生产Reader，未声称在NF重跑作者夹具；完整实际作者／运行／冷读证据在
仓库及SkillTest。没有重放报告中的NS_CannonExplosion失败复制／删除。

备份／回执分别位于各项目Saved/UnrealMCPBackups和Saved/UnrealMCPDeployment：
SkillTest为ParticleReader_20261105_201849，NF为ParticleReader_20261105_211436。
首轮保护检查SkillTest15386份非目标原件及14024个原Content保持；后续并发区间最终发现
SkillTest原ArrowTrail40个旧路径缺失，NF四处Content差异及两份T4 Blender源文件变化，
完整前后哈希／新增清单已记录，全部保留，不回滚也不归因于特定进程。
两项目原客户端配置／权限／端口／绑定保持，已有客户端重连，Claude正常审批仍由用户完成。
本轮未commit／push。

最终运行状态另记：验收后外部会话于21:51正常关闭SkillTest，21:54将NF重启为PID43756。
NF最终仍监听13090，但一次绑定只读上下文请求无响应，未能确认新会话已就绪；维护侧未
再次竞争关闭／启动。Saved回执明确ready=false，不把历史验收PID当作当前状态。
两项目已构建部署及先前分范围验收事实保留，使用前由当前会话恢复MCP就绪。
