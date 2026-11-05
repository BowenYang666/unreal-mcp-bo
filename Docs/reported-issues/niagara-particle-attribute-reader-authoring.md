# Particle Attribute Reader缺少受控读取／创建／发射器绑定接口

## 状态与本轮范围

- **状态：有限Reader读写合同已实现，构建部署SkillTest／NF并完成分范围公开验收。原始需求保留。**
- 日期：2026-11-05；UE5.7.4，Windows，grouped MCP。
- 报告时维护仓库HEAD：`82caaf1`。
- 本轮只读取公开合同、当前资产和相关源码；没有修改Niagara C++／Python实现，
  没有构建、部署、重启Editor或给业务VFX增加新层。
- 当前`NS_CannonExplosion`存在用户未保存修改；没有自动保存或覆盖。
- 本文是维护需求，不表示文中建议的接口已经存在。

相关的实际故障单独记录：
[跨系统Reader复制保留源私有对象，清理时崩溃](niagara-cross-system-reader-copy-removal-crash.md)。

## 需要实现的真实效果

SkillTest当前候选：

```text
/Game/Shared/VFX/Explosions/NS_CannonExplosion
```

已有独立弧线头层的内部名为`NE_SparkTrails_Head001`，
粒子带初速度／重力，能够沿弧线运动。
用户需要的是**沿这些移动粒子的轨迹持续产生的小火花**，不是再从爆炸中心独立Burst一组粒子。
当前独立大火花只实现了运动头，没有实现这种轨迹周围的子火花，不能将其当作等价交付。

真实参考：

```text
/Game/ThirdParty/NiagaraExplosion01/Niagaras/Air/N_ExplosionAir_004
  NE_SparkTrails_Head001
  NE_SparkTrails_Trails001
```

已读取的Trails001配置：

| 阶段／模块 | 实际情况 |
|---|---|
| Emitter Update / SpawnRate | 普通SpawnRate为0，不能据此判断此层不生成粒子 |
| Emitter Update / SpawnParticlesFromOtherEmitter | Attribute Reader类型为NiagaraDataInterfaceParticleRead，当前为Override |
| 同模块的Spawn Rate | 由FloatFromCurve动态输入驱动 |
| 同模块的Per Particle Cap | 100，属于原素材上限，不是本候选已批准的性能预算 |
| 同模块的年龄门限 | Minimum约0.3333、Maximum1 |
| Particle Spawn / SampleParticlesFromOtherEmitter | 从母粒子取位置／部分速度；Applied Velocity Scale约−0.5～0.9 |
| 子粒子后续 | 小范围偏移／扰动、重力、阻力及淡出，形成沿弧线留下的点和短线 |

**Attribute Reader的Emitter Name由用户在UE界面确认是NE_SparkTrails_Head001。**
公开读取没有取得该值，不能将这次手工确认写成MCP已经支持完整读回。
原Trails001使用Sprite Renderer；这个子火花需求不必强制改成Ribbon或位置事件方案。

## 当前公开接口缺什么

2026-11-05实际调用`niagara_search`检查particle reader、data interface、emitter binding，
没有发现针对Particle Attribute Reader创建／源发射器绑定的专用入口。

现有接口并非完全不能处理DataInterface，但不能完成这里的核心配置：

| 接口 | 能做到 | 缺失部分 |
|---|---|---|
| list_module_inputs | 返回Attribute Reader类型、Override、can_bind_datainterface=true | 不返回EmitterBinding的模式、发射器名及其解析到的Handle |
| read_niagara_authoring | 返回精确Module／Graph／Node身份、绑定节点GUID及baseline | 没有读取Reader对象的EmitterBinding，不能验证实际绑定来源 |
| bind_module_input_datainterface | 支持Renderer或Asset的对象槽绑定 | 不支持本地Particle Reader的EmitterBinding配置 |
| enable_module_input | 常量Local输入 | 明确不支持DI对象创建／属性配置 |
| get_class_properties | 可读NS根资产的反射属性 | 此流程尝试读取内嵌对象路径失败，不能替代稳定的Reader读取合同 |

现有bind_module_input_datainterface的参数只有：

```text
asset_full_path / emitter_name / module_name / input_name
binding_kind = Renderer | Asset
renderer_type / renderer_index / asset_path / script_type
```

没有源Emitter Handle、Reader绑定模式或受控EmitterBinding字段。
本轮未将不支持的参数硬传进去，也没有借Console／raw TCP／新资产作者commandlet绕过限制。

## 源码说明

维护侧：

```text
MCPGameProject\Plugins\UnrealMCP\Source\UnrealMCP\Private\Commands\UnrealMCPNiagaraCommands.cpp
  HandleBindModuleInputDataInterface，约3443～3610行
```

当前实现：

1. 查找DI输入并创建Override pin；
2. 约3514行调用SetDataInterfaceValueForFunctionInput创建DI；
3. Renderer分支查找接受Renderer的FObjectProperty；
4. Asset分支加载资产，再查找接受该资产类型的FObjectProperty；
5. 编译并保存目标NS。

UE5.7对应定义：

```text
Engine\Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraDataInterfaceParticleRead.h
  UNiagaraDataInterfaceParticleRead::EmitterBinding

Engine\Plugins\FX\Niagara\Source\Niagara\Public\NiagaraDataInterfaceEmitterBinding.h
  FNiagaraDataInterfaceEmitterBinding::BindingMode
  FNiagaraDataInterfaceEmitterBinding::EmitterName
```

这里是**枚举＋FName的结构体**，不是指向Renderer／素材资产的FObjectProperty；
把Emitter名称当asset_path或Renderer绑定传给旧接口，不能正确完成它。

另一个安全注意点：旧DI binder在验证具体BindingKind／AssetSlot之前已经创建Override和DI。
因此未支持调用不一定纯粹“失败但什么都没改”；新接口应先完成全部可完成的预检，
不能继续制造半完成对象后仍返回success-shaped结果。
本报告没有专门执行旧binder的这类失败夹具，此项为源码确认，不是新增运行复现。

## 期望的最小能力（建议，尚未实现）

只需要同一NS内、精确模块输入上的**本地Particle Attribute Reader**读写，
不需要通用任意UObject／Struct属性setter。

### 读取

- 输入使用完整/Game资产路径、Emitter Handle、script_type、Module Node GUID和规范输入名；
  不能只按显示名或第一个同名模块查找。
- 区分未配置、默认DI、本地Override、外部／共享DI及不支持状态。
- 返回DI类／对象路径／所有者、BindingMode、EmitterName、
  实际解析到的源Handle／UniqueName／选定版本，无法解析时明确报错或标记。
- baseline包含真实绑定字段和身份；源Emitter改名／换版本或模块／输入被替换后，
  旧baseline不能继续写入。
- 读取不标脏、不创建Override、不保存资产。

### 创建／绑定

- 先支持同一NS内stateful CPU源与目标，其他类型明确拒绝；不默认声称支持GPU／跨系统／循环依赖。
- 根据精确源Handle解析真实UniqueName，创建目标本地DI，设置Other及准确源名；
  不保留原素材SystemUpdateScript下的私有DI，不凭尾缀猜发射器。
- 同值绑定为no-op；已有不同绑定／共享对象不得被静默覆盖，重绑定需要独立的显式合同。
- 拒绝不存在／跨系统源、非法输入、错误DI类型、自采样和依赖环等危险配置。
- 默认dry-run、不保存；保护预先存在的用户脏包，使用Undo事务，只保存批准的目标包。
- 返回真实modified／compiled／saved／package_dirty、before／after及错误阶段；
  超时或连接断开不自动重试。

## 维护侧验收建议

1. 用隔离NS建立两个分离移动的父粒子和一个子火花层，实际使用标准
   SpawnParticlesFromOtherEmitter／SampleParticlesFromOtherEmitter。
2. 验证子粒子在选定母粒子当时的位置生成，并沿已走过的路径自然衰减；
   仅增加Emitter数、编译成功、从原点Burst或画一张弧线贴图都不算通过。
3. 错源／错误位置预期应使oracle失败；母粒子停止后不得继续从无效来源生新子粒子。
4. 独立检查数量上限、短寿命、结束／重触发及Low关闭／Medium减少；
   限制每条轨迹的生成率和存活数，不把配置倍率当作已测性能收益。
5. 保存、独立冷加载后，绑定和局部所有权保持，无源私有对象或源系统运行依赖。
6. 覆盖dry-run、同值no-op、stale／dirty拒绝、错误类型／Handle、改名、
   自采样／依赖环、失败保存及Undo；源资产和无关层保持。
7. 经公开grouped读写调用验证，不能仅新增Python Schema而未更新native插件。

## 本地证据

忽略目录：

```text
Docs\reported-issues\evidence\2026-11-05\niagara-particle-reader\
  bind-di-contract.json
  particle-reader-search.json
  reader-module-inputs.json
  reader-module-authoring.json
  current-user-drafts.json
```

原始会话记录：

```text
C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files\
  cannon-child-sparks-capabilities-1713\
  cannon-air004-selected-trails-inspect-0949\
```

当前仍是能力缺口反馈；没有完成新的沿轨迹子火花层，也不把已有独立弧线头冒充此需求完成。

## 维护侧实现与验证（2026-11-05）

新增read_niagara_particle_reader／bind_niagara_particle_reader，经既有Niagara分组发现，
原始操作169／direct只读50，分组仍57／只读29。MCP参数使用具体类型，未引入
Any／Optional／Union参数。沿用精确Handle／Module GUID／规范输入解析、baseline、
dry-run默认／不保存、事务和编译读回；只支持同系统stateful CPU本地Reader。
真实DI所有权／BindingMode／EmitterName及解析源Handle／版本进入读取合同；baseline
同时覆盖模块Reader绑定和全Emitter身份。已存在的不同／复杂／共享Override不隐式替换；
同值no-op不创建、不编译、不保存，脏包／stale／缺源／自采样及依赖环在修改前拒绝。

标准模块的实际数据流也已核对：SpawnParticlesFromOtherEmitter在Emitter Update
配置Module Reader，SampleParticlesFromOtherEmitter在Particle Spawn消费其
Emitter.SpawnParticlesAttributeReader输出，不伪造第二个本地Module输入。

隔离真实运行：至少两个移动且分离的母粒子；推进45步得到49个存活子粒子，
位置位于移离原点的母粒子区域，而不是独立原点Burst。停用Head并推进模拟后不再生新
子粒子，最终无残留。保存故障保留真实modified／compiled／saved=false及磁盘字节；
Undo／Redo、显式草稿恢复保存、预览／同值／stale／dirty／错源／自采样通过。
源改名显式GUID映射复制、可达私有DI保护／失败副本活跃预览清理、独立进程只加载目标
且重编译后绑定及局部所有权持久化也通过。100项Python及11项实际原生回归通过；
无参数的Reload／MID跳过未计作冷读证据。

这没有制作或保存NS_CannonExplosion生产层，没有接受该业务效果的艺术风格、
每轨迹粒子预算、Low关闭／Medium减少或实机性能收益；GPU／通用重绑定仍不支持。
接口存在不等于目标旧DLL已更新，双项目构建及公开grouped验收完成后另追加部署记录。

## 双项目部署与公开创建（2026-11-05）

SkillTest正式DLL6动作增量构建871.83秒成功，11项实际原生矩阵及显式独立冷读通过。
原绑定VSCode配置通过Niagara／Asset分组，在唯一新路径
`/Game/__Dev/ParticleReaderPublic_2702ddf2a08e43fea642a7b722f69343/NS_Test`
复制隔离系统，新增PublicChild及标准生成／采样模块，实际Reader预览would_modify=true／
modified=false，随后明确apply/save得到modified=true／compiled=true／saved=true，
本地所有权及精确源Handle读回成立，同值no-op不保存。另有旧基线实际写入拒绝，
完整spill JSON已读取；原源样例哈希、地图／视口／dirty0保持，不是仅Schema或Mock验证。

NF已构建部署并恢复L_map01／13090，原绑定Claude配置确认新native合同、Reader精确
身份拒绝及只读烟测通过；未在NF写生产Reader。SkillTest按最新用户原地图恢复ThirdPerson／
13091。两项目定制与客户端绑定／权限保持，外部并发Content／Blender变化不回滚，
完整保护／构建／公开回执见[复制与清理报告](niagara-cross-system-reader-copy-removal-crash.md#双项目部署2026-11-05)。
生产爆炸层、艺术风格、分档预算及GPU／通用重绑定不属于本轮实现或验收，没有commit／push。
