# Particle System 诊断读取：事件链、Ribbon、参数语义与隔离显示

日期：2026-11-16；维护更新2026-11-17。状态：**只读诊断及后续受控Solo均已完整构建部署SkillTest／NF并通过公开验收。运行快照及电表现仍未验收。**

以下原始报告只整理已有公开 MCP 回执、已有资产读取结果和本机 UE5.7.4 源码证据，
没有为写报告访问 Editor、修改插件、Build、重启或写资产。
下文“建议”均不是已经存在的 API，也不授权维护者直接修改业务资产。
最新实施状态见文末维护记录；原反馈与历史证据保留。

## 真实使用场景与结论边界

SkillTest 工程：`E:\04_UnrealProjects\UE5Test\SkillTest\SkillTest.uproject`。

```text
原 Cascade：
/Game/ThirdParty/GlowingOrbsPack/ParticleSystems/PS_GlowingOrb_8

用户用官方插件生成的 Niagara：
/Game/ThirdParty/GlowingOrbsPack/ParticleSystems/PS_GlowingOrb_8_Converted
```

原效果的 Electricity 是跟随不可见 TrailCore 运动源的 Ribbon，不是固定端点 Beam。
用户要密集、多段弯折、围绕球体的电弧，并希望**单独观察电这一层**。
截至本报告，用户仍反馈“只有直线”，电表现尚未验收；不能以编译成功、几张截图或
修改 Lifetime／Facing／Tessellation 就声称原版已经复原。

现有读取并非“基本参数也读不到”：Cascade 的保存属性、模块开关、分布、曲线和
源引用已经提供了大量有效数据。真正需要补充的是**配置之间的执行关系、有效值来源、
预览状态，以及受限的运行观测**，而不是绕过权限增加任意 UObject setter。

## 1. 已有能力与不能归咎于工具的错误

本次 `read_cascade_system` 返回 5 个 Emitter、105 个内联对象、4 个外部资源引用，
`complete=true`、`warnings=[]`，读取前后脏状态均为 false。其 scope 已明确声明：

```text
Authored reflected data for the selected emitters/LODs; no simulation,
runtime overrides, external asset contents or custom binary payloads
```

因此 complete 只表示此读取范围完整，不表示获得了所有 UE 执行状态或外部材质内容。
Niagara `reader_contract=2` 中的 `scalability_complete` 也不是整个模拟完整性声明。

此前 PS5 的 Orbit／禁用 Sphere、PS8 的 SubUV 模式及 TrailCore 引用均有可读证据；
作者误用禁用模块、漏读模式或把移动源换成固定 Beam，属于作者理解／验收错误，
**不应反馈成 reader 丢失这些字段**。完整材质和纹理仍应通过对应 reader 单独读取。

## 2. 高优先级：事件处理器与实际执行栈读取

### 已确认的缺口

完整 Converted 读取中可见 `GenerateLocationEvent`／`ReceiveLocationEvent` 函数节点，
但 Emitter 只提供四类常规脚本字段，没有独立的 typed 事件处理器集合。
共用图中的节点出现在哪个返回列表，不能证明该节点属于那个阶段或实际执行顺序。

已有公开调用：

```json
{
  "tool": "list_module_inputs",
  "arguments": {
    "asset_full_path": "/Game/ThirdParty/GlowingOrbsPack/ParticleSystems/PS_GlowingOrb_8_Converted",
    "emitter_name": "Electricity",
    "module_name": "ReceiveLocationEvent",
    "script_type": "event"
  }
}
```

实际拒绝：

```text
Invalid script_type 'event'. Must be 'spawn', 'update', 'emitter_spawn', or 'emitter_update'.
```

根资产反射成功返回 67 个属性；改用以下内联路径补读则失败：

```text
...PS_GlowingOrb_8_Converted.PS_GlowingOrb_8_Converted:TrailCore_0
Failed to load asset
```

所以“根 UObject 可以反射”不等于能通过 asset_path 读取任意内嵌对象。
不应让调用方继续猜事件阶段字符串，或用 spawn/update 冒充事件阶段。

### 最小建议

- 返回事件处理器稳定身份、Script Usage／Usage ID、所选版本、实际执行栈和有序模块；
  模块必须有完整资产、Emitter Handle、图／节点身份，避免同名或多 Handler 选错。
- 返回 Source Emitter Handle 的解析结果、Source Event Name、Execution Mode、
  SpawnNumber、MaxEventsPerFrame 等实际保存设置；缺失、失效引用、不支持分别表达。
- 返回事件模块输入的类型、来源、动态输入链和枚举类型／值／显示名；
  `NewEnumerator1` 不能直接解释为“忽略速度”或某个继承模式。
- 用同一套输入读取合同覆盖事件栈；保留不支持状态，不强制本阶段增加事件写接口。
- 读出真正的源依赖链：本例为 TrailCore 产生位置事件，Electricity 接收；
  源无 Renderer 是合法配置，不能判断为坏 Emitter 或建议删除。

这能帮助判断电弧为什么变成直线／短片，避免仅凭原点位置或单个速度参数猜原因。
现有 [Particle Attribute Reader 合同](niagara-particle-attribute-reader-authoring.md)
解决的是另一条 DI 读取链，不等于已经解决事件处理器读取。

## 3. 高优先级：预览 Solo 与作者 enabled 分开读

本次 Niagara 专用读取和根反射结果没有返回当前 Isolation 状态。
源码证明该状态确实存在，而且不只是 Emitter 的作者 enabled：

| UE5.7 本机源码位置，相对 Engine | 事实 |
| --- | --- |
| `Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraSystem.h:955-964` | `bIsolateEnabled` 是 WITH_EDITORONLY_DATA 下的非 UPROPERTY 成员，不能靠普通反射遍历获得 |
| `Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraEmitterHandle.h:158-159` | Handle 的 `bIsolated` 标记为 UPROPERTY(Transient) |
| `Plugins\FX\Niagara\Source\Niagara\Private\NiagaraSystemInstance.cpp:2919-2925` | 实例从 System 读取 Isolation 状态 |
| `Plugins\FX\Niagara\Source\Niagara\Private\NiagaraSystemRenderData.cpp:195-203` | 渲染数据生成检查 `IsDisabledFromIsolation()` |
| `Plugins\FX\Niagara\Source\NiagaraEditor\Private\ViewModels\NiagaraSystemViewModel.cpp:2159-2171` | UI 改隔离状态时，对引用同一 System 的已注册组件标记渲染状态更新 |

这解释了为什么 enabled=true 不足以证明该层当前可见；**它不证明本次核心不显示
必然由 Solo 导致，也不证明 Solo 停止了源粒子模拟**。当前状态没有读到，原因仍未证实。
用户有意只观察 Electricity，不能把“请取消全部 Solo”当成满足其需求。

建议增加只读预览状态，至少包含 System Isolation、每个 Handle 的 isolated 标志、
作者 enabled、Renderer 作者开关、目标组件身份及可确定的渲染排除原因。
区分已知 false、无法访问 Editor 状态、没有预览实例、未知裁剪原因。

如后续另做受控 Solo 操作，应复用原生预览语义，不改持久 enabled、不保存包，
明确它可能影响同一资产的多个组件；只隐藏其它层的画面时，不误停用于电弧的不可见源。
需独立授权、身份／状态校验和恢复约定；本报告不要求当前会话解除用户 Solo。

## 4. 高优先级：参数来源、时间域、单位及转换差异

单个数值正确读出，不代表照抄到 Niagara 后行为相同。需要可追溯的诊断信息：

| 本例 | 已证实事实 | 对读取／诊断的建议 |
| --- | --- | --- |
| Cascade VelocityCone Direction=(0,0,0) | `ParticleModules_Velocity.cpp:412-447` 的 SpawnEx 明确零向量回退 +Z；不是有效零方向 | 保存值与已核实的引擎规则分列，记录版本／源码依据；不能强行将全体效果改成 +X |
| Trails SizeScaleBySpeed | 官方 `CascadeSizeScaleBySpeedToNiagara.py:25-26` 将 MaxScale 乘 10；本例从(.5,20)变成(5,200) | 显示源→目标参数及转换规则／近似，不能将转换成功当外观等效 |
| Electricity SpawnPerUnit | 源记录 UnitScalar=50、SpawnPerUnit=10；结合源运动才有意义 | 保留距离单位、时间域、分布类型及连接来源，不把它当固定每秒生成数 |
| Ribbon 上限／几何片数 | 源 MaxTrailCount=60、MaxParticleInTrailCount=500、SheetsPerTrail=10 | 明示“上限／几何配置”，不可当实际活跃电弧数或帧内粒子数 |
| SubUV、Orbit 等 | 开关、模式、曲线都可能同时存在，禁用模块也有数值 | 将模块启用、模式选择、输入来源放在同一诊断结果中；不因看见曲线就断言实际按它播放 |

建议返回参数来源（本地常量、RI、Linked、DI、动态输入、脚本默认值）、真实枚举信息、
曲线键／插值与可确定的采样时间域；保留模式下未生效的保存值并说明依据。
复杂动态计算无法解析时返回 unresolved，不通过模块名称猜消费关系。
不要求实现通用 HLSL／C++ 符号解释器，有限支持的引擎规则必须可追溯。

## 5. 后续独立能力：有限的 CPU 运行快照

保存属性无法回答“每条电弧实际几个连接点、是否都接到同一根、为何只有直线”。
当前公开回执没有完整逐帧粒子位置、速度、Ribbon ID／连接次序、事件数量与源对应；
截图也不能替代这些数据。

建议先限于已存在且明确指定的 Editor CPU 预览组件，Cascade／Niagara 分别报告支持范围。
不为一次读取启动 PIE、创建 Actor、推进／重置模拟、改变随机种子或全局画质。
可返回当前实际模拟时间／帧、局部／世界坐标及组件变换、每层存活数和有上限的粒子采样；
可用时提供稳定粒子 ID、源 ID、Ribbon ID、连接顺序、位置／速度及事件生产／接收计数。
裁剪或采样必须标明总数、返回数和截断原因；不可用属性／GPU 读回明确 unsupported，
不能填零或让采样数量冒充总数。

这属于新诊断能力，不是“现有 Cascade 反射读取损坏”。读取应绑定真实实例和同一快照，
不能把不同帧的源粒子、事件和 Ribbon 拼成一张假想的对应表。

## 6. 诊断分类与完整性提示

本例原转换日志同时包含：

- `System successfully compiled.`
- TrailCore 的 SizeMultiplyLife 转换异常；前文已明确 ERM_None 跳过 Renderer。
- 两条 `Skipped converting bApplyGlobalSpawnRateScale.`

建议分开输出历史转换消息、当前编译结果、Editor 栈提示及运行／预览状态，并附
Emitter／Module 身份、消息来源和时间／有效性。根 MessageStore 未显示消息，不等于
子 Emitter 没有转换报告；有历史红字也不等于当前编译失败。
此处只建议诊断读取，不建议清消息、补可见 Renderer 或修改官方转换器来消红。

保留现有 complete/scope 合同；若扩展诊断，增加分域 coverage／unsupported，
让调用方清楚地区分 authored_data、execution_stack、editor_preview、runtime、
external_dependencies，而不是把完整性布尔扩张成“全部可见”。

## 7. 建议验收与安全范围

1. 在维护侧获准的隔离夹具中设置不可见移动源、事件驱动 Ribbon 和独立可见层。
   读取应恢复真实事件来源、Handler 设置和栈顺序；多 Handler、失效源、重名、版本切换有明确结果。
2. 用户手动 Solo 前后，只读结果应区分作者 enabled 与隔离可见性；
   核验同资产场景组件的影响，不把渲染过滤等同停止模拟。读取不改变包脏状态或 Solo。
3. 已知两条独立 Ribbon 的 CPU 夹具需读到可区分的连接 ID／点序；
   截断、缺属性及不支持 GPU 明示，不能仅凭截图或上限常量验收。
4. 复核零 Direction、禁用 Sphere、Random SubUV、曲线时间域和 MaxScale 转换差异；
   现有可读字段不回归，未知有效语义不伪造默认值。
5. 读取前后资产字节、用户草稿、全局质量、预览播放状态保持；新合同需经实际部署的
   grouped 公开入口验证，不能只新增 Python schema 或用私有命令跑通。

建议先补事件栈／参数来源／隔离状态／分层诊断，再单独评估运行快照成本。
本报告不请求资产作者扩权、通用反射写、自动修改供应商素材或生产项目集成。

## 本地证据与复现定位

已有回执采集索引时间为 UTC，下表是历史调用，不表示本次写报告重新调用了 Editor：

| 调用 ID | 时间（2026-11-16 UTC） | 证据 |
| --- | --- | --- |
| `call_vVsWuetWd7xGhLPLtp9Uy4rb` | 11:55:52 | 原 PS8 Cascade 完整读取 |
| `call_cnVxMxvliKexTEfsatkcwYms` | 13:47:16 | Converted 完整 Niagara 读取，缺独立事件处理器合同 |
| `call_Z9Yxab597Iy2WhRMcTdClehU` | 13:28:09 | Converted 根资产反射 |
| `call_fvLnfkD8VuqQo6fhdPC4TrCR` | 13:29:03 | 内联 TrailCore 路径无法作为资产加载 |
| `call_nfYkCZDjgifhOik9SNDpAogy` | 13:29:03 | list_module_inputs 不支持 event 阶段 |

这些回执、原转换日志、选定请求元数据和 SHA256 清单复制到本仓库既有忽略目录：

```text
Docs\reported-issues\evidence\2026-11-16\particle-system-inspection\
```

原始完整记录：

```text
C:\Users\15208\.copilot\session-state\c1c574cf-cdb2-4138-b212-7547c85fb92c\files\
  generator-orb-review\receipts.json
  generator-orb-review\native\
  generator-glowing-orbs\PS_GlowingOrb_8.json
  ps8-converted-repair\original-conversion-diagnostics.log
```

原始证据含本机路径及第三方素材配置，仅本地保留；发布前另行审查。
未复制供应商 `.uasset`、引擎源码或纹理图片到反馈仓库。
本报告不关闭当前 VFX 修改任务，也不宣称 Solo 是已查明的直线电弧原因。

## 8. 维护记录：第一阶段只读诊断

用户随后授权完成改动后部署 SkillTest 和 NF。本轮仅补只读诊断，不修改供应商／
生产 VFX，不取消用户 Solo，不增加事件作者能力；第5节逐粒子 CPU 快照单独评估。

新增 `read_niagara_diagnostics`，通过 `niagara_search` 取得 read 合同。
`diagnostics_contract=1` 要求匹配的原生插件；旧 DLL 明确 capability 错误。
参数使用具体类型，遵守 MCP 参数不使用 Any／object／Optional／Union 的仓库规则。

- 事件 Handler 独立集合，精确 Handle／Usage ID／Module GUID、所选版本、源解析、
  执行模式及保存上限；零 SourceEmitterID 按原生 self 解释，失效源显式不完整。
- 沿唯一 ParameterMap 链读取输出栈顺序，不把共享图枚举当执行顺序；仅普通／插值
  Spawn 使用引擎等价规则，Usage ID 始终精确匹配，多 Handler 不串读。
- 输入区分 override、RI、Linked、动态输入、DI／表达式未解析及脚本默认值，枚举
  给出原生类型／值／名称；默认引脚 API 未导出时明确 unresolved，不伪造有效值。
  时间域／单位不按模块名猜测，尚未实现通用表达式或所有曲线语义解释。
- 只读当前 System Isolation、Handle isolated、作者 enabled、Renderer 开关及
  已有非模板组件身份；渲染隔离不等于停止模拟，重启也不保证保留瞬态 Solo。
- 分域 coverage 和 unsupported；实时事件计数、完整裁剪及 CPU／GPU 逐粒子快照
  显式 unsupported。`complete=true` 只涵盖声明的支持范围，绝非运行全貌。
- Cascade 保留已有反射数据和完整性语义，增加分域覆盖及四类 UE5.7 已核实规则的
  版本／源码依据注解，不重写保存值，不自动执行转换或解析历史转换日志。

维护验收：109 项 Python 回归通过，包括用户已有 SDK 构造兼容修正、过滤、只读、
精确转发、旧 DLL、失败未知状态及完整 JSON spill；源码完整 DLL 构建／链接通过。
原生 `UnrealMCP.Cascade.ReadAuthoredData` 和
`UnrealMCP.Niagara.Diagnostics.ReadOnly` 通过。后者独立 `__Dev` 夹具有不可见
TrailCore 源、Ribbon Renderer、两个真实事件栈，检查精确来源／错 Usage 拒绝、
输入、完整栈以及读取前后 Solo／脏状态／磁盘哈希保持；没有据此声称模拟连接点已测。

实际维护 grouped 入口二次核验 `complete=true`、2 Handler、精确事件选择与不存在
Usage 拒绝、runtime unsupported、上下文保持及草稿0通过。首次发现普通／插值
Spawn Usage 误判后已修复并增加原生及公开完整性断言，未隐藏不完整原因。
维护公开证据：`MCPGameProject/Saved/ParticleDiagnostics/public/30ff5e51dc2e4d3c87df0c8a65b2a050`。
临时维护实例正常关闭，未保存资产。启动未就绪的一次只读失败回执保留，未重试作者操作。

双项目部署备份标识：`ParticleDiagnostics_20261117_010335`，位于各项目
`Saved/UnrealMCPBackups`，验收记录位于 `Saved/UnrealMCPDeployment`。
四个变化原生文件经三方核对替换，其余本地源码逐字节保持；客户端配置、严格工程绑定、
权限及端口不变。SkillTest 关闭前新鲜草稿检查0，正常关闭保存0；NF原本关闭。
目标最终构建／原生／公开验收及文件保护已完成，详见下一节；先前仅源码复制的
SkillTest typed-path代码也已编入本次DLL，但本轮未重跑整套属性作者／冷读矩阵。

## 9. 双项目部署最终验收

- SkillTest 完整35动作构建／链接通过（1343.53秒），NF完整260动作构建／链接通过
  （294.12秒）。NF首次在编译前遇到UBT对Git中文转义路径解析异常；仅构建进程临时
  `core.quotepath=false` 后通过，未改Git持久配置、中文文件名、引擎或项目设置。
- 两边实际新DLL均通过 Cascade authored-data 与 Niagara diagnostics 两项原生测试，
  各自产生一个唯一 `Content/__Dev/NiagaraDiagnostics_*/NS_Test.uasset`，未运行玩法测试。
- 正常交互GUI启动，SkillTest恢复原 `/Game/PerfectVFX/Lvl_vfx_test` 及相机，NF通过
  项目规定启动脚本打开。沿原配置实际grouped入口验证：正确工程绑定、夹具完整、
  两Handler、精确Usage选择、错误Usage拒绝、runtime明确unsupported均通过。
- SkillTest 原 Cascade 与 Converted 真实只读通过，Converted `complete=true`、
  `unsupported=[]`；实际 Electricity Handler 的 Usage ID 为
  `02B0C331-4523-3225-F5BA-ABA2A38196A1`，源是 TrailCore，事件为 LocationEvent，
  原生模式 SpawnedParticles、SpawnNumber1、MaxEventsPerFrame0。后两项只是保存设置，
  不能解释成实测一粒子或零事件。源无Renderer合法，未补可见Renderer或改事件设置。
- 公开读取前后地图／视口／画质及Profile上下文一致，草稿0；原 Converted 隔离标记
  重复读取保持，未发送Solo修改。重启前后的瞬态Solo持久性并未承诺或作为验收结论。
- SkillTest 14905个、NF5914个原有Content／Config／项目Source／uproject／MCP配置
  文件SHA256全部保持；非目标插件源码逐字节保持，四个功能文件与维护源码一致。
  两边各仅新增上述一个 `__Dev` 夹具，无其它新增Content，完整旧插件备份保留。

目标记录均在 `Saved/UnrealMCPDeployment/ParticleDiagnostics_20261117_010335`：
`build*.log`、`native-tests.log`、`dll.json`、`public-final.json`、
`protected-final.json`、`source-final.json`。实际公开完整回执分别位于：

```text
SkillTest: public-dc0403c3dfdf473c9ad18cb94fb4c1ee
NF:        public-29fde4a461dc4218a86dda5fd7119763
```

验收后两边正常GUI保留打开，持久端口SkillTest13091／NF13090及严格工程绑定不变。
已有MCP服务需重连以加载共享Python工具表；独立SDK验证不代替客户端信任审批。
本轮未提交或push代码，也未修改供应商／生产资产、全局画质或NF玩法配置。
第一阶段工具缺口已处理；CPU／GPU逐粒子运行快照、完整转换日志诊断及电弧视觉复原
仍是明确未完成的独立范围，不能据此关闭原使用侧VFX任务。

## 10. 后续授权：受控 Emitter Solo

用户随后明确要求增加 AI 可控制的 Solo，并授权关闭闲置 SkillTest、完成后完整部署
NF 和 SkillTest。这是独立的瞬态写合同，不改变第8／9节诊断 reader 的只读承诺。

新增 `set_niagara_emitter_solo(asset_path, emitter_handle_ids, expected_baseline="", dry_run=true)`，
属于 Niagara write，只读模式隐藏。按精确 Handle GUID 设置完整集合，单个为一层、
多个为多层、空数组清除；不按显示名定位，不替换 authored enabled。
先预检影响，再使用新鲜 baseline 显式应用。回执含 before／after／proposed、
affected_components、warning 和带 after baseline 的 restore_arguments。
状态／影响实例变化后拒绝旧请求，恢复同样需要重新确认；超时不得盲目重试。

该工具复用原生标记和渲染刷新语义，**会影响引用同一 System 的所有已注册组件，
包括关卡 VFX，不是独立预览实例**。只能提供临时隔离和显式恢复，不会让 Solo 变成
“仅 Niagara 窗口可见”。不推进／重置／激活模拟，不停止事件源依赖，不编译／保存，
不清脏标记或修改 enabled，不提供持久 Undo，重启保留不作保证。
PIE／模拟、同资产游戏世界组件、保存／GC／模态／慢任务／编译忙碌、无效或重复身份、
不一致隔离标记、过期状态都拒绝。参数遵守仓库具体 MCP 类型规则。

维护侧完整DLL构建／链接通过；`UnrealMCP.Niagara.Solo` 和 `Diagnostics.ReadOnly`
通过。自有夹具验证两个同资产组件受影响、不同资产不受影响、单选／多选／清除／
无操作／恢复、UI与新增实例导致stale拒绝、disabled与草稿／磁盘保持、无激活或年龄
重置及保存／慢任务拒绝。首次组件准备触发编译忙碌，仅在夹具准备阶段完成队列后通过，
没有放宽工具保护。111项Python回归通过，包括只读隐藏、精确转发、未知结果无重试及
完整spill的恢复元数据。未据此声称像素或粒子运行快照验收。

双项目备份／验收标识为 `NiagaraSolo_20261117_115924`，三个原生文件经三方核对合入，
保留本地插件定制，完整旧插件移入各自 `Saved/UnrealMCPBackups`。
NF原本关闭；SkillTest预检曾有4项草稿，用户选择保存全部再关闭，但再次检查时已为0，
所以本轮没有发保存请求，正常关闭保存0。部署保护基线取自这次干净状态之后，不能拿
先前内存草稿状态声称磁盘回滚或由本工具保存。两边完整构建／链接及Solo／诊断原生矩阵
已通过（SkillTest35动作1412.60秒，NF36动作55.77秒）。两边沿原配置的实际公开MCP
预检／应用／无操作／stale拒绝／恢复均通过；诊断读回确认恢复原隔离状态、authored
enabled保持，地图／视口／画质与Profile上下文保持、草稿0、自有夹具磁盘SHA256未变。
没有向生产资产发送Solo请求。完整回执分别位于本轮记录目录的：

```text
SkillTest: public-ca6ec2972e6146d3970d01791679ceca
NF:        public-9734071407244cb48a85fdb8d1918c9a
```

两个正常GUI保持打开，SkillTest恢复本轮记录的原地图及相机，NF由规定助手启动，
持久端口13091／13090、工程绑定及权限不变。客户端重连加载共享Python合同，独立SDK
不代替信任审批。最终保护核对通过：SkillTest14913个、NF5915个原有Content／Config／
项目Source／uproject／MCP配置文件SHA256全部保持；非目标插件源码逐字节保持，三个
功能文件与维护源码一致，两边各仅新增一份诊断和一份Solo夹具，无其它新增Content。
两份公开Solo夹具磁盘SHA256前后相同，证明应用／恢复不保存资产。各项目记录中的
`protected-final.json`、`source-final.json`、`public-final.json`、`dll.json` 保留实际证据，
完整旧插件备份保留。此次Solo改动未提交或push，未修改生产VFX的Solo／enabled／配置；
用户的原始电弧视觉任务及逐粒子运行快照仍不因此关闭。
