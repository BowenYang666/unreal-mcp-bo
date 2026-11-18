# DataAsset 结构体数组初始化：能力边界与调用方式待核实

日期：2026-11-13。状态：**有限追加合同已在维护源码验收，随后经用户新授权构建部署NF／SkillTest；SkillTest新DLL追加／独立冷读及NF公开只读合同通过。未重写业务数组，原始反馈保留。**
[问题索引](../README.md)

## 1. 实际操作场景

NodeFall UE 5.7.4 接入 T5，需要初始化新 DataAsset：

```text
/Game/Data/TowerDefense/TowerCofigs/DA_T5_Upgrades
class: /Script/NodeFall.NFTowerUpgradeDefinition
property: BaseLevels
type: TArray<FNFTowerBaseUpgrade>
initial state: empty
desired state: two entries, Cost 75 and 100
```

相关业务结构包含：

```cpp
USTRUCT(BlueprintType)
struct FNFTowerBaseUpgrade
{
    GENERATED_BODY()
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    int32 Cost = 0;
    UPROPERTY()
    float DamageMultiplier = 1;
    UPROPERTY()
    float FireRateMultiplier = 1;
};
```

用户已批准费用，只要求创建两个普通升级配置项，未要求编辑任意 UObject、
Instanced 对象或私有子对象。旧塔与 T5 使用同一个升级配置类，不是 T5 特殊类型。

本次使用公开 grouped MCP：

- 读取：`get_class_properties(asset_path=..., structured=true)`。
- 写入：`asset_call_write(tool="set_asset_properties", arguments=...)`。
- **没有调用独立的 Add Array Element 工具，不能把本例称为“Add 工具报错”。**

本报告只记录既有回执；为报告未再次写入业务资产、修改插件、构建或重启编辑器。
源码仓库 HEAD 不等于 NF 当前部署 DLL，本报告不据此虚构插件构建版本。

## 2. 已观察到的读取与写入结果

### 2.1 读取先明确标记整体数组不可写

新 T5 DataAsset 的 structured reader 返回：

```json
{
  "path": "BaseLevels",
  "writable": false,
  "reason": "Unsupported type or nested ownership; use a specialized editor"
}
```

### 2.2 调用方仍尝试整体替换数组，被写前拒绝

以下是实际批量请求中 `BaseLevels` 变更的摘录，去除了同批其他属性，
**不是声明已经额外执行过这个单项最小请求**：

```json
{
  "tool": "asset_call_write",
  "arguments": {
    "tool": "set_asset_properties",
    "arguments": {
      "asset_path": "/Game/Data/TowerDefense/TowerCofigs/DA_T5_Upgrades",
      "changes": [
        {
          "path": "BaseLevels",
          "value": [
            {"Cost": 75, "DamageMultiplier": 1, "FireRateMultiplier": 1},
            {"Cost": 100, "DamageMultiplier": 1, "FireRateMultiplier": 1}
          ]
        }
      ],
      "save": true
    }
  }
}
```

实际原生拒绝回执：

```json
{
  "success": false,
  "modified": false,
  "saved": false,
  "stage": "validation",
  "error": "BaseLevels: Unsupported type or nested ownership; use a specialized editor",
  "message": "BaseLevels: Unsupported type or nested ownership; use a specialized editor"
}
```

该调用没有成功创建数组项，也没有写入／保存资产。没有报告崩溃、数据损坏、
脏包被保存或结果未知。不能将其他步骤的成功保存算成本请求成功。

### 2.3 旧塔对照：整体数组同样拒绝，已有元素的 Cost 可写

本次只读检查了同类的既有 T3／T4 配置：

| 资产 | 属性路径 | 当前值 | writable |
| --- | --- | --- | --- |
| `DA_T3_Upgrades` | `BaseLevels` | 整体类型不支持 | false |
| `DA_T3_Upgrades` | `BaseLevels[0].Cost` | 50 | true |
| `DA_T3_Upgrades` | `BaseLevels[1].Cost` | 75 | true |
| `DA_T4_Upgrades` | `BaseLevels` | 整体类型不支持 | false |
| `DA_T4_Upgrades` | `BaseLevels[0].Cost` | 75 | true |
| `DA_T4_Upgrades` | `BaseLevels[1].Cost` | 100 | true |

普通标量／引用、分支 `A.Costs`／`B.Costs` 整数数组已有成功写入回执。
因此不是“所有 DataAsset 或所有数组都不能写”。

旧资产现在已有元素，以及后续修改已有叶子字段的记录，**不能证明最早的数组项
是通过这次同一个公开接口添加的**。最初创建方式尚未查实；本报告不据此声称
发生了“以前 Add 可以、现在 Add 回归失败”。

## 3. 问题性质与尚未证明的结论

已确认的是：

1. 当前公开 structured 合同将这个整体数组标为 unsupported。
2. 整数组写请求的格式符合公开 schema，但所选路径不在其实际可写范围。
3. 读取声明和写入拒绝一致。调用方在读到 `writable=false` 后仍尝试写入，
   本身不能证明 MCP 实现有故障。
4. 当前空数组无法直接通过已验证的叶子路径编辑完成初始化，实际工作流存在缺口。

**尚未确认：**

- 这是维护侧刻意设置的安全边界、类型支持遗漏、错误的 writable 判断，
  还是存在尚未正确使用的公开数组操作／替代调用。
- 结构体中的非编辑成员是否是拒绝原因。上面只列出真实字段，
  不把“隐藏字段导致”写成已定位根因。
- 是否可以通过合法的模板复制后修改已有叶子字段完成需求；未将该方案写成已验收修复。

调用方此前把它直接描述为“MCP 有 Bug／只能用户手工处理”过于绝对，已明确更正。
本报告请求维护者核实接口能力及正确调用方式，不预设必须改代码。

## 4. 希望维护侧明确的最小合同

- 如果已有支持该操作的公开入口，请给出准确工具、参数及隔离示例，
  明确“修改已有元素”与“从空数组新增元素／扩容”的区别。
- 如果当前合同故意不支持此结构，请明确是哪种类型／成员／所有权限制，
  不仅返回笼统的 `Unsupported type or nested ownership`。
- 若需补能力，可考虑受控地默认构造普通结构体数组项，再编辑可写叶子字段；
  不要求开放任意对象遍历，也不要求允许调用方修改非编辑成员。
- 保持 dirty／stale 拒绝、全请求预检、单目标保存、准确的
  modified／saved／stage 回执；不得为了支持初始化而放宽既有安全边界。

建议先在维护仓库的隔离 DataAsset 中验证：空数组到两项、已有项改 Cost、
其他成员默认值保留、非法成员整体拒绝、Undo／保存失败／独立冷读。
这不是授权维护流程试写 NF 生产旧塔、覆盖用户草稿或运行私有 writer。

## 5. 原始证据

本机回执位于：

```text
C:\Users\15208\.copilot\session-state\d6ded13a-420e-400f-aa66-e6cee37736cd\files\
  t5-readiness\definition-contract-1.json
  t5-readiness\production-config-recovery2\upgrades-before.json
  t5-readiness\production-config-recovery2\upgrades-apply.json
  t5-manual-resource-readback\compare-existing-upgrade-contract.json
  t5-manual-resource-readback\compare-existing-upgrade-contract-result.json
```

其中 writer 拒绝与后来的旧塔对照是不同调用，不合并成不存在的完整复现批次。
上文已内嵌关键请求片段和原生回执，避免报告仅依赖临时文件路径。

## 6. 维护侧核对（2026-11-13，只读阶段）

在用户允许的早先只读阶段，通过NF项目已有的绑定stdio配置核对三份实际资产：
T3为50／75、T4为75／100，整体BaseLevels同样不可写，而已有Cost叶子可写；
T5为空，0／1索引均越界。三个资产文件SHA、地图／视口和dirty0前后保持。
缓存维护客户端未配置项目绑定被正确拒绝，改用原NF绑定配置读取，未绕过保护。
证据保存在维护项目Saved/ReportedIssues/DataAssetStructArray_20261113_150136。

原因已落实到Supported()的全量结构体判定：每个成员都必须Editable。Cost具备CPF_Edit，
两个倍率仅UPROPERTY()，故整体数组拒绝；已有Cost路径只验证该叶子，索引不会扩容。
省略JSON倍率字段不能改变这条类型判定。构造函数也未填入BaseLevels，因此不是T5特殊
类型错误；旧塔最早初始化途径尚未查实，不能声称同一调用发生回归。

## 7. 受控追加实现及验证（2026-11-13）

用户随后授权修改MCP，并明确“改完先不用NF测试”。本轮在维护仓库增加
append_data_asset_struct_array，只用于已有DataAsset上的普通结构体数组，保持原
set_asset_properties整体覆盖限制。结构化读取的array_append返回数量／默认值／可编辑
相对字段／基线。新项先在暂存根副本中默认构造，之后只给可编辑字段赋值；隐藏字段即使
提交默认值也拒绝，已有项和隐藏非默认值不被覆盖。无需改变NF业务结构体的UPROPERTY。

调用方式见[结构体数组追加合同](../../Tools/project_tools.md#append_data_asset_struct_array)。
参数为asset_path、array_path、expected_baseline、items，默认dry_run=true／save=false。
原始操作170，Asset22，分组仍57／只读29，direct只读50；MCP参数均使用具体类型，
没有新增Any／Optional／Union参数。一次1～64项、总长≤1024，严格基线及脏包拒绝、
全批预检／最终EditCondition、事务Undo、单目标保存和完整读回／失败回执保留。

维护项目隔离原生验证通过：

- 原缺口红例StructArray-Initialization-Red.log，随后同用例转绿；
- 空数组到Cost75／100，隐藏倍率默认1；非法最后行／隐藏成员整批拒绝；
- 旧非默认倍率1.25在后续追加及Cost叶子更新后保持；整体旧writer继续拒绝；
- preview／dirty／stale，Undo／Redo，保存故障磁盘字节不变、真实after及显式恢复；
- 带禁用条件的嵌套数组不能借追加写入；最终暂存Sibling bool为true时合法初始化；
- 独立进程加载后四个追加项、隐藏值、条件嵌套数组及完整基线持久化；
- 8项实际共享属性／FText／BP／表面反馈回归，以及标量／BP两项独立冷读通过。

源日志位于MCPGameProject/Saved/Logs/StructArray-Initialization-{Matrix,Cold}.log及
StructArray-Compatibility-{Matrix,Cold}.log。追加冷读根为
/Game/__Dev/StructArrayInitialization_7F8C339C417821922F59A088A125EFBE；
无参数Reload跳过未计作冷读。102项Python离线回归通过，包括schema／默认／partial
failure／完整溢出／超时不重试和独立分类／只读过滤。

真实公开grouped MCP在维护项目独立13092完成新DataAsset创建、读取基线、默认预览、
非法隐藏字段拒绝后仍为空、明确apply/save得到75／100且两倍率1，以及旧基线重复追加
拒绝；地图／视口／dirty0保持。公开资产为
/Game/__Dev/StructArrayPublic_eb497f085d5f4bba84ab2890f9950254/DA_Test，完整回执位于
MCPGameProject/Saved/StructArrayPublic/eb497f085d5f4bba84ab2890f9950254。
最初普通GUI停在Target Upgrade Required，无资产写请求；正常关闭该自有窗口后使用
unattended启动，不修改Target设置。最终自有维护实例正常关闭saved0，13092释放。

本轮修复授权后未连接／测试／部署／重启NF或SkillTest，未写T3／T4／T5业务配置，
也未commit／push。新Python可见不表示NF旧DLL已获得能力；以后部署需另行维护窗口。

## 8. 后续授权部署（2026-11-13）

第7节的“不做NF测试／未部署”是当时的授权边界。随后用户明确NF开发已停止，
允许完成Notify参数能力后部署NF与SkillTest；本合同随同配套插件发布，未以旧授权
推断部署许可。完整备份与保护基线批次为NotifyProperties_20261113_173613。

两项目Editor目标编译／链接成功并正常启动，保留NF本地插件定制与现有严格项目绑定。
SkillTest新DLL实际StructArrayInitialization通过，显式冷读根
/Game/__Dev/StructArrayInitialization_2DB401784A2D546AB67857928163A5D5的独立
进程验证通过，四项／隐藏默认值及旧值、条件嵌套数组、基线和干净包持久化。
NF现有配置的公开只读检查已返回BaseLevels.array_append，当前T5已有两项；这些项
是部署前的现场配置，**不是本轮调用append创建**。未向T3／T4／T5发送任何业务写入。

两边公开绑定读取、Engine资产及Blueprint内容烟测通过且dirty0。NF5672个和SkillTest
14724个原保护文件SHA全部保持，SkillTest13个新增Content仅为__Dev隔离夹具，NF
无新增Content。各项目Saved/UnrealMCPDeployment/NotifyProperties_20261113_173613
内保留build-receipt.json、public-final/receipt.json、protected-final.json及原生日志。
未重放NF业务追加，未commit／push；旧客户端重连后才加载共享Python的新注册表。
