# Niagara Scalability 保存配置读取修复

日期：2026-10-31。状态：第一阶段已实现、验证并部署 SkillTest/Lyra/NF。
第二阶段任意上下文解析、第三阶段运行实例诊断未实现，不作为本次交付结果。

## 原问题

`read_niagara_system` 只有作者 enabled 和 Renderer 平台数据，遗漏 Emitter Platforms、
ScalabilityOverrides 及完整 System/Effect Type 关联。导致互斥画质层可能被解释为同时运行，
灰显保存倍率可能被解释为启用覆盖。反射 ExportText 补读不能代替完整 typed 契约。

本轮只增加读取；未修改任何 Lyra/NF Niagara 资产、作者开关、画质或权限配置。

## 实现

- 旧 System 调用签名和字段保留，`reader_contract=2` 增加分层保存配置；新增只读
  `read_niagara_effect_type`，经现有 grouped discovery/read 执行。
- System 保留 EffectType 引用/null、完整平台集、全局覆盖开关、所有覆盖行及本地玩家覆盖。
  Effect Type 返回频率、反应、SignificanceHandler 及全部 System/Emitter 规则，开关和值同时保留。
- Emitter 使用显示名、Handle ID、UniqueName、实际对象路径和选定/解析 GUID；不强制最新版本。
  Stateful 完整读取平台与有序覆盖行；Stateless 明确 unsupported，不伪造默认 false/空规则。
- Renderer 保留旧材质、绑定、作者 enabled、platforms；原索引及空槽说明避免过滤造成身份变化。
- 统一平台结构保留负数原始 mask、动态引擎质量名称、设备掩码/三态及完整 CVar 条件。
  三态默认继承不被折算为 false，掩码展开不冒充最终 eligibility。
- complete/unsupported/field_states 区分不可读、未绑定与实际 false/空数组；默认匹配只是值相等，
  不声称已序列化或覆盖生效。曲线键和子结构不回退成不透明 ExportText。
- 模块提供节点/图/脚本路径、GUID、所选版本及 exposed-default 状态、保存引脚/链接。
  不做算法符号推理，不按模块名猜倍率消费，不把图节点列表冒充执行顺序。
- 大结果可靠 spill 到完整 JSON，保留能力和完整性状态；旧 DLL 明确缺少新能力。

接口细节见[Niagara 工具文档](../Tools/niagara_tools.md#read_niagara_system)。

## 验证

- 完整 Python 回归：77 项通过；覆盖 grouped/只读过滤、旧 DLL 提示和 spill 完整性。
- 仓库及部署后 SkillTest 原生测试：`UnrealMCP.Niagara.Scalability.Platforms`、
  `SavedRules`、`ReadCompatibility` 均明确 Success。覆盖 -1/-29/-4、设备三态、未注册
  CVar 条件只读序列化、禁用覆盖与倍率、实际曲线键、旧材质/绑定/User参数、质量/Profile/脏包不变。
- 几次静默自动化启动未产生完成标记，未计为通过；改用实时 stdout 启动后完整通过，
  没有降低预期、关闭验证或修改渲染环境。
- Lyra 使用现有只读配置，经公开 `niagara_search` 和 `niagara_call_read` 读取六个 System、
  四个 Effect Type，所有相关层 complete=true/unsupported=[]，完整 JSON 留在 Saved。

| 样例 | 验收结果 |
| --- | --- |
| WallPortal Tunnel / TunnelLow | Emitter mask 28 / 3；作者 enabled 均 true，Renderer 可全档位；没有将这些层等同 |
| DeathCubes 两层 | mask 30 / 1，显示名与 Handle 身份保留 |
| Grenade Explosion Decal | mask 28 |
| Sparkle 两行 | 原 mask -29 / -4；第一行 override+scale=true、约0.1；第二行两开关 false、保存1.0 |
| Portal System | EffectType_World_Portal，System 不启用整组覆盖，System 平台全当前档位 |
| World_Portal | Continuous、Distance、DeactivateImmediateResume；per-system 实例开关 true / 6，其余保存数值和开关一起返回 |
| Weapon_Impacts | Continuous，未渲染裁剪 true，MaxTimeWithoutRender=0 |
| World_Placeable / GunPad | Continuous、Distance、DeactivateImmediateResume、未渲染裁剪 true；其保存超时实际为1，不误要求为0 |
| World_Burst / GunPad_Loading | SpawnOnly / Deactivate，不解释成连续预算监测 |
| Sparkle SpawnRate | 实际引擎脚本路径、调用节点和选定/解析版本，未输出算法消费结论 |

最终回执：Lyra 本地
`Saved/NiagaraScalabilityAcceptance/20261031_082336_8f50c8ae/receipt.json`。
此前脚本误要求 Placeable 超时也为0，失败 JSON 保留；按原约定修正该断言后全套通过，未改资产。
读取前后脏包、10个样例包/配置 SHA256、质量 CVar 值/来源 flags、Active Profile 一致。

可重复[验收脚本](../../Python/scripts/niagara/verify_scalability_read.py)只写本地 Saved JSON，
不使用原生文本导出替代公开 MCP 验收，也不修改任何全局上下文。

## 部署范围

- 首次用户授权目标为 SkillTest 与 Lyra；随后追加 NF 部署及当前仓库 commit。Cropout 未部署。
- SkillTest 更新四个相关文件，构建成功，355.36秒；部署 DLL 上三项测试通过。
- Lyra 旧插件与仓库差异较大，未整包升级：新增同一 Scalability helper 实现/头，
  原样移植模块/系统两个读取函数，只增 include 和 Effect Type 只读路由。
  函数外旧写入实现比较未变，不引入 String Table、迁移或其他无关更新。
- LyraEditor 构建成功，170.63秒；原会话脏包0后正常关闭，未保存/强杀。
  恢复 `/Game/System/DefaultEditorMap/L_DefaultEditorOverview`，普通端口13090，
  本次 PID 37328（历史记录，后续操作须重查）。只读配置未改。
- 备份位于各项目 `Saved/UnrealMCPBackups/Scalability_20261031`。SkillTest 9952个、
  Lyra 819个保护文件哈希均未变化；Lyra 保护范围含全部 Content/Effects、配置、源码、
  VFX研究文档、非目标插件源码和客户端配置。
- 上轮 String Table 等已验证改动保留，按后续用户要求与 Scalability 一起纳入当前仓库提交；不推送。

## NF 追加部署与验收

2026-10-31 晚间，更新 NodeFall 插件的 Bridge、Niagara Commands 和两个 Scalability helper
文件；四文件 SHA256 与仓库一致。其余 NF 插件定制（包括测试 Tag 兼容修复）保留。
目标为 `C:/04_unrealProjects/NodeFall/NodeFall.uproject`；引擎根为
`C:/01_install/UE5_7/UE_5.7`。从仓库 `MCPGameProject/Plugins/UnrealMCP` 定点更新到
`C:/04_unrealProjects/NodeFall/Plugins/UnrealMCP`，不是新安装或整包覆盖。
原会话脏包0、正常关闭且 saved_count=0；NodeFallEditor UE 5.7.4 构建成功，109.61秒。
恢复 `/Game/Worlds/GameScene01/Maps/L_map01`，PID26384（历史值），端口13090。

使用 NF 现有 `.mcp.json` 经公开 grouped discovery/read 验收，不改配置或资产：

| 样例 | 结果 |
| --- | --- |
| StunBody / T2_ExtractorLink | contract 2、完整读取；分别2 / 5个 Emitter，Handle 与所选/解析 GUID 可读 |
| CannonTrail_Normal / E4_BomberTrail / e4_gatling_trail | 各1个 Emitter、contract 2、完整读取；三个实际关联均为 NET_Gameplay_Burst |
| NET_Gameplay_Burst | contract 1、complete=true；SpawnOnly / DeactivateImmediate / Age Significance |
| Emitter 保存行 | 原 mask -31 / -30 / -28；bScaleSpawnCount=true，约0.3 / 0.6 / 0.8 |
| System 距离行 | 原 mask -31 / -30 / -28 / -24，裁剪开关 true、距离15000 / 20000 / 30000 / 50000；-16行开关 false、保存0 |
| 三条尾迹 Spawn 调用 | 实际引擎脚本 SpawnPerUnit 1.2 / SpawnBurst_Instantaneous 1.1；所选与解析 GUID 一致，节点/图路径及保存输入可读；不推断倍率消费 |

五个系统共10个 Emitter；相关 System/Emitter/Renderer 完整状态均通过，重复 StunBody
读取的质量 CVar 值/flags、Active Profile 与首次一致。脏包前后0，六个样例包 SHA256 未变；
BP_NFPlayerController 窄列表/读取烟测通过；4743个 Content 文件的数量、路径、大小、时间不变。
部署、启动和回读后5612个保护文件哈希全部未变；
之后仅追加 NF 本地反馈文档的部署记录，未改 Content、游戏源码、配置或其他插件源码。

完整 JSON 与最终回执位于 NF 本地
`Saved/NiagaraScalabilityAcceptance/NF_20261031_192616/receipt.json`。
同目录 `module-identity-audit.json` 记录三条尾迹的实际调用节点与版本，不以重复 GUID 跨资产匹配。
首次验收的输出语句受 PowerShell `python -c` 引号处理影响，触发 NameError；
不是原生 reader 失败，首个已读 JSON 保留，修正脚本引号后独立完整重跑通过。

现有 Claude 项目配置仍为 `UNREAL_MCP_READ_ONLY=0`、`MCP_NODE_ENABLED=0`；本次仅执行读取。
已有客户端需重连；Claude 项目服务器仍为 Pending approval，用户须在 NF 中启动
`claude` 确认，不绕过审批。独立 stdio 验收不等于 GUI/Claude 已完成信任审批。
本次未在 NF 执行写入型自动化夹具、PIE、切画质或性能/粒子数量验收；
没有补齐 Effect Type 创建、绑定/规则写入或任意上下文解析能力。

## 限制

保存规则允许不代表运行中产生粒子。未实现任意设备/CVar上下文的解析、覆盖来源归并、
真实组件裁剪原因或粒子数预测。没有调用全局 SetQuality/Profile 或预览 delegate 后再恢复。
`editor_preview_override_bound` 只是绑定状态，不是预览档位；不能读取的预览值没有伪造。

本次按既有 Editor Scalability Context/原始保存数据 oracle 复核，未额外进行修改后人工
UI截图、Low/High运行对比或性能验收；不将这些未做的项目标为通过。
