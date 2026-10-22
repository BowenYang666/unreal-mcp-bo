# 同系统复制 Emitter 后记录存在但不执行：缺少系统图节点重建

## 状态

2026-10-22 在 NF UE5.7.4 的实际 VFX 作者流程中发现。未修改插件代码。
这是独立于之前保存重入崩溃的问题：本次没有崩溃，但复制接口成功回执和发射器数并不能证明新增层运行。

## 实际表现

在 `/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Lv3` 中，
`OrbitArc` 是能正常出画的单粒子CPU网格环。
使用正常grouped MCP调用同系统复制为OrbitLow、OrbitHigh、OrbitCrown：

```json
{
  "tool": "add_emitter_to_system",
  "arguments": {
    "asset_full_path": "/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Lv3",
    "source_emitter_name": "OrbitArc",
    "new_emitter_name": "OrbitLow"
  }
}
```

外层 endpoint 为 `niagara_call_write`。后续两次只换 `new_emitter_name`。
三个新层均返回成功，读取的记录均enabled=true、CPU、正确网格／MI，
Spawn Count1、非零Lifetime、白色ParticleColor、正确位置／Scale，
Alpha动态曲线也能读回。但相同年龄的真实场景中只有原OrbitArc显示，新三层不出画。

为排除旧预览实例缓存，曾把预览系统切到普通版，再切回重建实例；仍不显示新三层。
随后用跨系统独立复制接口重建三层并配置同样位置／尺寸，实际照片才出现四层完整分布。
因此不是只看Emitter数量、MI编译或赋值就下结论。

## 代码差异

维护／部署插件：

```text
MCPGameProject\Plugins\UnrealMCP\Source\UnrealMCP\Private\Commands\UnrealMCPNiagaraCommands.cpp
HandleAddEmitterToSystem，同系统分支约1764行
```

当前分支仅执行：

```cpp
TargetSystem->DuplicateEmitterHandle(*SourceHandle, FName(*NewEmitterName));
SyncOverviewGraph(TargetSystem);
TargetSystem->InvalidateCachedData();
TargetSystem->RequestCompile(true);
TargetSystem->WaitForCompilationComplete();
```

之后保存／PostEditChange。`SyncOverviewGraph` 不是System Spawn/Update执行图重建。

UE5.7.4的标准编辑器复制流程：

```text
Engine\Plugins\FX\Niagara\Source\NiagaraEditor\Private\ViewModels\NiagaraSystemViewModel.cpp
FNiagaraSystemViewModel::DuplicateEmitters
```

该流程在`DuplicateEmitterHandle`之后还调用（约781行）：

```cpp
FNiagaraStackGraphUtilities::RebuildEmitterNodes(GetSystem());
GetEditorData().SynchronizeOverviewGraphWithSystem(GetSystem());
RefreshAll();
```

同时先处理系统实例／ViewModel，并保留复制用户参数等步骤。
而运行时`NiagaraSystem.cpp:2847`的`DuplicateEmitterHandle`本身只增加Handle、
刷新参数和使缓存失效，并没有做上述图重建。

证据强烈指向同系统分支没有把新增Handle对应的节点接回系统执行图。
未为本报告新增C++节点枚举工具或修改实现，最终修复需按原生编辑流程核对。

## 当前使用的标准替代路径

只在本地Dev创建复制源快照：

```text
/Game/Dev/VFX/Particles/NS_Lv3_OrbitCopySource
```

移除不执行的三层后，通过现有API：

```json
{
  "tool": "add_emitter_to_system",
  "arguments": {
    "asset_full_path": "/Game/Features/TowerDefense/Common/VFX/Upgrade/NS_TowerUpgrade_Lv3",
    "source_asset_full_path": "/Game/Dev/VFX/Particles/NS_Lv3_OrbitCopySource",
    "source_emitter_name": "OrbitArc",
    "new_emitter_name": "OrbitLowerBand"
  }
}
```

该分支使用`FNiagaraEditorUtilities::AddEmitterToSystem`，返回
`inheritance_mode=independent_snapshot`、`saved=true`，通过私有源对象引用检查。
新名字OrbitLowerBand、OrbitUpperBand、OrbitTopBand实际出画，未修改MCP、引擎或原版资产。
这个Dev源只用于作者过程，不应作为运行时根或迁移对象。

## 验收建议

1. 同系统复制最小有效Burst emitter，核对系统Spawn/Update图内的Emitter节点／Handle ID，
   而非只核对Overview或Handle数量。
2. 在真实模拟中确认每个新Emitter生成非零粒子；不同位置的网格都能实际显示。
3. 核对Rapid Iteration命名空间、曲线DI、Renderer绑定、父继承与User参数复制行为。
4. 修改新Emitter参数不影响源Emitter，保存并独立冷加载后仍能执行。
5. 与UE原生ViewModel复制流程对齐，保持现有跨系统独立复制、保存错误回执和安全调度。

## 本地证据位置

本次会话 artifacts：

```text
C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files
  lv3-orbit-density-expand-receipt.json
  lv3-four-rings-draw-inspect-receipt.json
  lv3-four-rings-refresh-instance-receipt.json
  lv3-four-rings-copy-rebuild-receipt.json
```

NF实际照片在`Saved\Screenshots\UnrealMCP`：

```text
vfx_lv3_four_rings_028_lv3wide.png          同系统复制后只有原环
vfx_lv3_four_rings_refreshed_lv3wide.png    重建预览实例仍缺三层
vfx_lv3_four_rings_graphfixed_lv3wide.png   独立复制后四层实际可见
vfx_lv3_four_ring_0750_lv3wide.png         后续运动／淡出阶段
vfx_lv3_four_ring_saved_lv3wide.png        保存并地图重载后仍显示四层
```

没有把NF第三方制作素材或`.uasset`复制到维护仓库。
