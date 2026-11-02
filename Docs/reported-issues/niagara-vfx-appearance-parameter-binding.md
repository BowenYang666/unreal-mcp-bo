# Niagara 外观参数声明、绑定与Renderer材质接收合同缺口

日期：2026-11-01。状态：**有限声明／绑定合同已实现并验收 SkillTest；已追加部署 NF。**
第1～6节保留原始反馈；实现及限制见第7节、NF部署见第8节，不是任意图分析或新Notify实现请求。
来源：NodeFall ADO [#141 共享VFX参数与触发入口](https://dev.azure.com/BowenGame/UE5/_workitems/edit/141)。
范围：制作端怎样声明／绑定颜色、大小、独立亮度；与[Scalability写能力](niagara-scalability-authoring.md)分开。

[问题索引](README.md) · [保存配置读取已修复](niagara-scalability-reading.md)

## 1. 问题与实际检查

NF希望同一NS被不同角色／动画／命中配置复用，参数只写本次NiagaraComponent，
不改变共享NS默认值、供应商MI资产或Gameplay。
动画外观主要逐条通知填写；命中继续GA／Projectile／ImpactProfile归属。
通知代码负责运行传参，不是MCP工具运行依赖。

本轮44个已接入NS的公开读取成功，26个没有User参数、18个有，9个混合Local／World Space。
顺着78个材质引用及父链读取58个MI和54个母材质。
这说明不能只增加Setter或给所有NS写同名变量，就声称三个旋钮都有效。
材质图连通和参数声明也不等于全部实际执行分支／视觉效果已验收。

本轮只read／导出／文档，没有创建User参数、改绑定、写材质、运行PIE或替换通知。

## 2. 当前工具与缺口

| 项目 | 已有能力 | 本需求仍缺什么 |
|---|---|---|
| 播放实例参数 | set_niagara_parameter可给level Actor的组件写User Float／Color等 | 它不声明NS User变量，也不等于运行时动画／命中入口 |
| NS暴露参数 | read_niagara_system完整列出声明和类型 | 没有公开的受控User变量添加／默认值／类型预检合同；不能靠setter假定缺失参数被图消费 |
| 模块输入 | enable_module_input创建Local rapid值，switch／dynamic input工具存在 | Local常量不是Linked User绑定；需要准确的User变量链接和类型检查，保留旧曲线／动态输入 |
| 复杂override | set_module_dynamic_input仅支持fresh输入 | 不能直接替换已有override而不丢曲线／绑定；应明确只支持范围或提供预检／原图保护 |
| 身份定位 | reader已有node／graph／script路径及selected／resolved GUID | input／switch工具按名字选节点；需精确节点／版本定位或重名拒绝，不盲选第一项 |
| Renderer材质输入 | 读到material、material_user_binding、override_materials；通用属性文本setter存在 | 专用reader未输出完整MaterialParameters／AttributeBindings；typed读写和实例MID来源合同不足 |

当前公开Niagara完整catalog共26项。结论来自schema与native实现，不是调用不存在的接口后报错。
本轮未对正式资产试写struct、增加变量或替换动态输入，不标记这些新路径已成功验收。

## 3. UE已有机制：不是引擎不支持

UE5.7 `FNiagaraRendererMaterialParameters`包含AttributeBindings、ScalarParameters、
VectorParameters等。声明中明确非空绑定会生成MID；
`FNiagaraRenderer::ProcessMaterialParameterBindings`按绑定读User／系统属性，
对**运行实例MID**设置scalar／vector，不需修改共享MI资产。

本机来源：

```text
C:\01_install\UE5_7\UE_5.7\Engine\Plugins\FX\Niagara\Source\Niagara\Public\NiagaraRendererProperties.h
C:\01_install\UE5_7\UE_5.7\Engine\Plugins\FX\Niagara\Source\Niagara\Private\NiagaraRenderer.cpp
```

建议提供可复查的材质参数映射、类型／关联、Renderer slot与实例来源，
或通过严格受控领域反射实现，不开放任意UObject写。
它适合独立亮度／Tint接收，但材质必须确有对应输出乘法；绑定存在不等于画面效果保证。
已有Dynamic Material Parameter有侵蚀／动画用途，不能占用固定通道覆盖整个库。

## 4. 整体Scale不能只解决传输

实查CannonMuzzle_Normal包含Local Mesh／Sprite、World火花、烟及两个Light Renderer；
没有User参数，初始化颜色为HDR，灯光另有固定ColorAdd／RadiusScale。
CannonImpact_Normal有User.Custom Color，但Sprite／Mesh折射和固定MI颜色／Emission不同。

因此：

- Component Scale不自动覆盖所有粒子尺寸、散布和运动。ApplyOwnerScaleToAttributes也有独立开关。
- 一个Color值不代表精确全层换色；固定橙色乘蓝色可能变黑，Alpha消散应保留。
- 亮度应作用于明确发光／灯光，不偷改Opacity、折射、SpawnRate、曝光或Gameplay时间。
- 真实Beam端点、Mesh采样和弹道保持原有几何；世界位置不能直接乘倍数飞向世界原点。

这些是NF资产／算法适配责任，不要求MCP自动重写所有层。
MCP需要提供准确的参数、绑定、类型、身份和安全写入事实，
不能以名字或图节点列表推断执行顺序／实际消费／整体缩放已成功。

## 5. 建议验收

在隔离fixture中，覆盖：

1. 声明User Float／Color／Bool，默认值、重复名称、错类型和非法值明确预检；no-op不改包。
2. 把确切module input绑定到User变量，完整读回source／类型／node／version；
   名字重复或stale版本拒绝。
3. 有旧曲线／动态override时不静默破坏；unsupported明确返回，不复制错误输入为成功。
4. Renderer MaterialParameters完整读写，显示User到材质参数的映射；
   两个系统实例的MID参数互不影响，原NS／MI／其他实例hash与值不变。
5. 不覆盖已有DynamicParameter通道；错误材质名／类型／slot在修改前拒绝。
6. 预检、dirty保护、save=false／Undo、compile／save错误回执与显式冷读和
   [Scalability authoring安全合同](niagara-scalability-authoring.md)一致。

如果暂不提供某类复杂图编辑，明确范围即可；不要求任意图符号分析／HLSL证明或万能脚本作者。
传输正确后，具体NS的换色／亮度／0.5-1-2尺寸视觉验证仍由项目执行，不能拿UI字段存在作代理。

## 6. 原始证据与边界

NF方案：
`C:\04_unrealProjects\NodeFall\Docs\AI\ZZ_AI_self_workingDir\VFXParameterTransferProposal.md`。
逐系统表：
`C:\04_unrealProjects\NodeFall\Docs\Production\VFXParameterAudit.csv`。
完整原始read结果在本次session的`nf-vfx-parameter-audit-1101`目录。

归档与SHA256 manifest和[Scalability报告](niagara-scalability-authoring.md)共享：
`evidence\2026-11-01\vfx-parameter-and-scalability\`（已忽略原始证据）。
目录收录报告不意味着问题已修复、工具已部署或NF参数化已实施。

## 7. 维护实现、接收验证与限制：2026-11-01

新增 `set_niagara_user_parameters`、`bind_niagara_module_input`、
`set_niagara_material_bindings`，共用精确`read_niagara_authoring`基线与默认预检/不保存、
dirty/stale拒绝、事务、编译及真实保存/部分失败回执，不复用无限制UObject setter。
工具及[公开验收脚本](../../Python/scripts/niagara/verify_authoring.py)见
[完整合同](../Tools/niagara_tools.md#guarded-niagara-authoring)。

- User Float/Int/Bool/Color声明及默认值：重复/错类型/非法值预检，同值no-op，未指定保留。
   Color为linear HDR `[r,g,b,a]`。这是修改批准的NS authoring默认值，**不是**运行实例传参；
   后者仍只改本次Component，不能修改共享NS／MI。
- 模块定位用System/Handle/脚本阶段/node GUID，加选定版本及graph/pin/rapid状态基线。
   仅fresh/Local转换或相同User链接no-op；已存在的异值Linked、curve/dynamic/expression
   明确拒绝，未重写曲线或按同名第一节点定位。静态/object/DI输入不支持本链接writer。
- Renderer读取完整AttributeBindings、声明/解析/child变量、Scalar/Vector/Texture常量、
   nullable StaticBool、保存材质名/类型关联和supported/complete/unsupported。
   当前write只patch User Float->Scalar、Color->Vector的无歧义全局参数映射；保留其他
   binding/常量及DynamicParameter通道，冲突拒绝，不包含任意常量/texture/static/DI编辑。
- 仓库及SkillTest原生Parameters通过真实声明、绑定、material mapping、编译及保存；
   另进程冷读仍正确。两个真实NiagaraComponent有不同引擎MID，Scalar分别2/5、Tint红/绿；
   原NS/母材质仍clean且默认值不变。运行接收检查需要有粒子和结束帧渲染提交；此前默认
   getter/推进不足的失败日志保留，未改断言或用自写MID Setter替代引擎消费路径。
- 公开grouped全流程通过，成功回执共享#140的
   `Saved/NiagaraAuthoringAcceptance/30daff68efb34bb2912ffe8d040e8f36/receipt.json`。
   完整Python80项通过，SkillTest部署及保护范围见[#140维护结果](niagara-scalability-authoring.md#9-维护实现与验证2026-11-01)。

复杂override替换、任意图数据流/算法证明、全库颜色/尺度/亮度视觉适配和Notify/Gameplay
入口仍不在本轮实现范围；没有宣称NF参数化、性能或整体0.5/1/2缩放视觉完成。
本轮仅部署验收SkillTest，未部署NF、未改生产资产、未commit/push。

## 8. NF追加部署与烟测：2026-11-01

随后用户授权将同一五文件native实现部署NF，NodeFallEditor构建成功并恢复原L_map01／13090。
公开authoring System／Module／Renderer读取contract1及旧系统完整读取均通过；
User声明dry-run成功，缺少User的module／material绑定在preflight明确拒绝，不是Unknown command。
保存的原始规则／基线、质量/Profile、脏包及Content清单未变，没有新增NF变量、绑定或资产。

完整部署边界、备份和回执见[#140 NF追加部署](niagara-scalability-authoring.md#10-nf追加部署2026-11-01)。
现有客户端需重连，Claude审批仍由用户确认。实际写入、独立冷读与双MID接收仅引用SkillTest
已完成验收，不宣称NF生产效果参数化或视觉适配已完成；本轮不commit/push。

## 9. NF客户端独立参数预检复核：2026-11-01

用户10:13反馈后，NF当前native实际读取NS_CannonImpact_Normal的精确模块和Renderer身份、
保存User默认及完整MaterialParameters。HDR Color、Size/Brightness Float声明dry-run成功；
既有User.Custom Color链接识别为no-op，User Color到材质Color的实例映射预检成功。
缺失User、既有User类型迁移、重复声明和材质类型冲突均在修改前明确拒绝。

NS／模块／Renderer／EffectType基线、脏包、地图、质量/Profile及保护哈希前后不变。
本轮未应用任何声明／链接／材质映射；尤其不能拿此Color映射预检证明全层换色、
亮度或完整尺寸语义正确。真实写入、冷读和双MID接收仍引用此前SkillTest证据。
完整回执、验收脚本返回字段假设纠正及未重放写入的边界见
[#140客户端复核](niagara-scalability-authoring.md#11-nf客户端独立复核用户2026-11-01-1013反馈后)。
工具接收端主要阻塞已解除，可以安排#138实际视觉试点；没有改正式资产或关闭工作项。

## 10. #138现成效果适配追加需求：安全改旧User链接与动态输入内部绑定

**状态：已在NF task-owned Dev副本验证为明确不支持；这是当前有限合同之外的新接线需求，
不是第7至9节已实现接口回归，也不是要求任意图/HLSL重写。**
用户2026-11-01 10:34授权本次直接在NF试点。原六层
`/Game/Features/Enemies/E2_cannonDog/VFX/Impact/NS_CannonImpact_Normal`保持不变；
只新建／保存`/Game/Dev/VFX/SharedImpact138/NS_CannonImpact138_Working`，
声明同默认色的User.Star Color和User.Size／Brightness Float=1。
实际原生声明／编译／保存成功，不能据此声称已经被消费。

### 10.1 安全重定向一个已有简单User Linked

Stars／spawn／InitializeParticle的Module.Color已Linked到User.Custom Color。
在新User.Star Color已存在、同LinearColor类型且有fresh module baseline的情况下，
`bind_niagara_module_input(..., input_name="Module.Color", user_parameter="User.Star Color",
dry_run=true, save=false)`仍在preflight拒绝：

```text
Existing linked/dynamic/expression override preserved; replacement is unsupported
```

建议增加**显式opt-in**的同类型简单User link重定向：要求预期旧link和精确目标baseline，
预览完整before/proposed/after身份，只改这一input；不自动删除旧User声明或其他使用者，
不替换DynamicInput／Expression／curve。测试旧link不符、错类型、dirty/stale均不修改；
默认合同继续拒绝，不把新显式批准路径变成静默覆盖。
这是让旧单色效果分离主色／星芒颜色所需，不应以改Local常数替代运行实例参数。

### 10.2 精确动态输入函数的内部标量绑定

Refraction／update／ScaleMeshSize.Scale Factor已存在VectorFromFloat／FloatFromCurve链。
按其真实Node GUID调用authoring reader时，明确拒绝：

```text
Missing module/selected version; dynamic functions are not module targets
```

现成曲线应保留，只把必要的标量倍率接到User.Size；简单Float->Vector的三个／两个通道
也需要在其真实函数输入上绑定同一float，而不是声明一个未消费的Size变量。
建议有限`dynamic_input`读取／写入目标或等价的子输入路径：
System＋Handle＋script phase＋parent module/input＋function node GUID＋selected version，
包含graph/pin/link/rapid完整基线、实际canonical子输入和类型；只支持批准的User typed
input link及必要的标准math组合，不提供任意curve/dynamic/expression覆盖。
如需要给旧曲线增加乘法，显式保留旧子图并返回完整接线回执；不以fresh-only创建操作
替换已有整条曲线，不自动升级函数版本，不只按动态函数名字选第一个节点。

接线正确后仍由NF验证：所有Sprite／Mesh尺寸、按速度拉长、火花运动／重力／碰撞、
Stars抖动／扩散与0.5/1/2对照；不是工具承诺任意效果完整尺度算法。
现有模块、Parent material与Sprite Size Mode不得为“能绑定”而擅自重做。

### 10.3 证据与未修改边界

完整NF本地回执：
`C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files\vfx138-binding-preflight-1034\receipt.json`，
同目录有实际请求／原生响应；23个模块与材质默认读在`vfx138-inspect-1034`。
原NS SHA256和authoring baseline、原地图、脏包0未变。
没有修改或编译请求生产NS、供应商／Engine材质、GA／Montage／伤害／冷却／质量／C++，
没有PIE或视觉A/B；仅工作副本声明实际保存。未修改MCP代码、部署、ADO或commit/push。

### 10.4 维护实现与隔离验证：2026-11-01

用户随后要求开始实现。新增三个显式有限write operation：
`redirect_niagara_user_link`、`bind_niagara_dynamic_input`、`multiply_niagara_float_input`；
现有reader扩展dynamic_input目标。完整参数/限制见
[有限接线合同](../Tools/niagara_tools.md#limited-existing-link-and-curve-extensions)。

- 简单User重定向单独opt-in，预期旧link、同类型与当前module基线缺一不可。
   只断开目标边，旧变量/节点/其他使用者保留；默认旧bind仍拒绝异值覆盖。
- 动态函数由System/Handle/phase/父module/input/function GUID及实际链定位，
   选定/解析版本、脚本change ID和canonical子输入完整读回；未知或不可达函数拒绝。
- Float结果包装保留旧curve输出，用选定标准Multiply_Float版本与独立ParameterMapSet，
   防止复用旧曲线共享set造成环；User default1与原结果等价，重复同User/version no-op。
   不替换曲线/time/DI/Expression，也不推广为任意vector/color/HLSL编辑。

验证只在仓库新`/Game/__Dev`夹具执行，没有修改NF工作副本或生产NS。
81项Python通过；七项原生回归明确Success，独立进程冷读保留curve/User接线与脚本引用。
夹具保留曲线keys/tangents及共享消费者，预检不改图，Undo完整恢复；同值重复不新增包装。
真实固定步长模拟验证倍率1完全等于原链，.5/2按12每秒、59/60有效时间与整数生成独立
oracle验证，不把已截断的粒子数再乘倍率当预期。曲线夹具使用标准UpdateLUT，无该不同步警告。

回归日志：仓库本地`Saved/Logs/Niagara-LinkExtensions-Regression.log`、
`Niagara-LinkExtensions-Cold.log`、`Niagara-LinkExtensions-LUTVerified.log`。
冷读夹具`/Game/__Dev/NiagaraDynamicBindings_69EB100A4D86BB39AB584C98ED9DB699/NS_Test`。
此前孤立节点输入发现、图复用自环/栈溢出与错误整数oracle日志保留，未标成通过；
危险复制图预检和临时诊断已移除，不通过关闭检查或修改生产曲线获得绿色结果。

本轮未部署SkillTest/NF、未重启NF、未改ADO/Notify/Gameplay/画质或commit/push。
NF当前实际适配／视觉对照仍由项目后续执行；三个新operation要求再部署matching native。

### 10.5 SkillTest与NF追加部署：2026-11-01

用户随后授权部署两个项目。只更新Bridge、Niagara Commands、Authoring.cpp/.h四文件；
两目标原文件字节一致后分别备份，保留本地Build.cs/导航/测试Tag/其他源码和配置。
SkillTest构建315.77秒成功，部署DLL上七项原生回归与实际独立DynamicBindingsReload通过。
公开MCP精确读取曲线夹具、动态User绑定和倍率包装no-op、拒绝复杂源重定向均通过；
原始JSON回执`Saved/UnrealMCPDeployment/LinkExtensions_30a35798/receipt.json`。

NF原会话零脏包/空闲后正常关闭，saved_count=0；NodeFallEditor构建80.52秒成功。
恢复`/Game/Worlds/GameScene01/Maps/L_map01`、普通13090，PID29332（历史值）。
NF真实工作副本`/Game/Dev/VFX/SharedImpact138/NS_CannonImpact138_Working`只读/预检：
Stars Color从User.Custom Color到User.Star Color重定向dry-run成功，错误旧User拒绝；
Refraction ScaleMeshSize父输入下VectorFromFloat.Module.Value精确读取成功，
其既有FloatFromCurve不能被普通User binding覆盖，但保留曲线的User.Size乘法dry-run成功。
前后各层baseline、质量/Profile和脏包0不变，BP_NFPlayerController窄列表/读取通过。
NF成功回执`Saved/UnrealMCPDeployment/LinkExtensions_371db77e/receipt.json`。
首次烟测脚本把原生`NiagaraFloat`误按`float`字符串过滤，失败JSON保留；按实际typed名称
修正harness后完整独立dry-run通过，没有修改native或重放实际写入。

SkillTest备份`Saved/UnrealMCPBackups/LinkExtensions_20261101_123637`，9961保护文件哈希
未变，仅新增4个隔离`__Dev`资产；NF备份`LinkExtensions_20261101_124002`，5646保护
文件与4758个Content文件清单/哈希未变。之后仅追加NF既有AI部署记录，非目标代码/配置不变。
SkillTest临时Entry/13091会话零脏包后正常关闭saved0；NF保持原地图/13090开放。
已有客户端需重连，Claude仍Pending approval由用户确认。未改NF工作副本/生产资源、
Gameplay/画质/ADO，未commit/push；NF实际接线应用/视觉对照仍由项目后续批准执行。

### 10.6 NF客户端独立复核与真实Dev接线应用

用户14:28要求继续VFX，当前NF PID29332／matching DLL已重新确认。
Stars明确旧link重定向、Refraction exact dynamic_input读取与旧FloatFromCurve乘User.Size
预检通过；错误旧link／源pin、旧curve直接bind均拒绝，preflight无副作用。
随后只在既有NF task-owned Dev工作NS实际应用redirect和Float wrapper，
真实compiled/saved/after完整回读成功；五个发光层实例材质映射亦实际配置。
局部Vector2／Float Local常数使用已受支持的fresh标准动态输入＋精确child User绑定，
保留各层原常数，不要求将Float wrapper扩为无限制vector writer。

保护原NS／供应商，未改MCP代码或再部署。重载Dev地图后两个同NS组件参数独立，
固定0.2s offscreen PNG实际可见橙白小效果／蓝红大效果；这不是原样1全层视觉严格等价、
完整倍率阈值／生命周期／最终美术／性能或生产拥有者接入验收。
完整本地证据：session files `vfx138-link-repair-1428`、`vfx138-pilot-links-1428`、
`vfx138-remaining-scale-1430`、`vfx138-sparks-motion-1430`、
`vfx138-brightness-resume-1430`及`vfx138-review-save-reload-receipt-1430.json`。
实际PNG为NF `Saved/Screenshots/UnrealMCP/vfx138_clean_ab_1430.png`。
插件资产／World duplicate路径拒绝和单项数组脚本错误均保留，没有绕过接口或重放未知写入。
详细阶段／参数／未做边界见NF AI参数决策记录第12节；不关闭ADO或commit/push。
