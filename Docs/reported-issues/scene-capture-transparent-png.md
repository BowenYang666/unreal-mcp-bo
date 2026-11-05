# 场景截图PNG近乎全透明：RGB有画面但用户查看时近乎空白

## 状态与结论边界

- **状态：维护源码已修复，NF／SkillTest均已构建部署并验收。SkillTest后续授权完成build，较早copy-only记录及原始证据保留。**
- 报告日期：2026-11-05，Windows／UE5.7.4。
- 影响工具：`scene_call_write(tool="capture_scene_viewport", arguments=...)`。
- **确定事实：成功输出的PNG含有效RGB场景画面，但Alpha几乎全部为0。**
  支持透明度合成的查看器中近乎空白，与部分查看工具显示RGB画面的结果不一致。
- 尚未独立导出`ReadPixels`返回值或追踪具体渲染Pass，不能声称已确定是哪一个引擎环节
  首次产生了这些Alpha。现有截图输出路径没有把普通场景截图规范化为不透明。
- 用户只是正常打开工具返回的文件路径；不是用户选错Niagara资产或VFX根本没有渲染。

任务历史达到32条后拒绝新截图是独立问题，见
[截图任务缓存耗尽](scene-capture-task-cache-exhaustion.md)。
本次只反馈，不修改插件、引擎、项目渲染配置或原始PNG。

## 环境与受影响文件

| 项目 | 实际值 |
|---|---|
| UE | `5.7.4-51494982+++UE5+Release-5.7` |
| 实际使用工程 | `E:\04_UnrealProjects\UE5Test\SkillTest\SkillTest.uproject` |
| 地图 | `/Game/Dev/VFX/Maps/L_CannonVFX_Review` |
| 调用方式 | 正常stdio／grouped MCP，native offscreen capture |
| 请求尺寸／Warmup | 1280×720，16帧 |
| 输出格式 | 8-bit RGBA PNG，非交错 |
| 维护仓库HEAD | `44c6517` |

用户明确指出的原始文件：

```text
E:\04_UnrealProjects\UE5Test\SkillTest\Saved\Screenshots\UnrealMCP\CannonExplosion_DetailV2_Continuous_3_1105_v1.png
```

文件在2026-11-05本地约09:03:41保存，大小544539字节。原文件SHA256：

```text
d91f111b6481890f7b3bd1e668757e420bb0b7e68bc2e5fa14fb1387d5d23e34
```

PNG原始扫描线解压／反滤波后的统计：

| 指标 | 实测 |
|---|---:|
| 总像素 | 921600 |
| Alpha=0 | 921490 |
| Alpha=255 | 110 |
| 其他Alpha值 | 0 |
| RGB非零通道数 | 1498563 |
| RGB最大通道值 | 255 |

约99.988%的像素完全透明。RGB中可以看到橙色爆炸、短火星、深灰烟团、
灰色地板及右侧圆柱；这并不意味着原RGBA文件按正常透明合成后也可见。
不能用文件尺寸、非零RGB或查看工具忽略Alpha的缩略图，替代用户可查看性验证。

## 对照检查

使用Python标准库解析PNG／IDAT，正确反解扫描线滤波后，仅将Alpha改为255，
保存成**新的**不透明副本，原图不覆盖：

```text
C:\Users\15208\.copilot\session-state\0c9ca56b-4750-4e5b-b4ee-9dfe98650c9b\files\
  CannonExplosion_DetailV2_Continuous_3_1105_v1_opaque.png
```

保留原PNG其余块；未改变RGB、分辨率、曝光、后期、模拟时间或VFX资产。
原始／副本RGB字节流SHA256均为：

```text
6c82d545df6977a41e804864b6e22dea1eebe84a74f3f92790dbed87ff746827
```

不透明副本已在普通浏览器页面打开并再次读取画面，能够正常显示。
该对照用于区分“没有渲染任何画面”和“画面被透明通道隐藏”，不是插件已修复的证明。

## 当前源码路径

`MCPGameProject\Plugins\UnrealMCP\Source\UnrealMCP\Private\Commands\UnrealMCPSceneCommands.cpp`：

- 约1165行：输出路径固定为`FPaths::ProjectSavedDir()/Screenshots/UnrealMCP/<id>.png`。
  图片不默认保存到Copilot会话目录，修正副本／Review页是客户端另外生成的产物。
- 约1171行：offscreen viewport使用`PF_B8G8R8A8`。
- 约1223行：`CaptureViewport->ReadPixels(Pixels)`。
- 约1224行：直接将`Pixels`传给`FImageUtils::PNGCompressImageArray`，未显式规范化Alpha。
- 约1226行：检查PNG能否解码和宽高，不检查Alpha语义／普通截图的可见性。
- 约1229～1231行：保存成功即返回文件路径和saved=true。

Python入口`Python\tools\scene_tools.py:324～350`没有opaque／preserve-alpha选项，
回执也未声明alpha_mode或此次文件是否适合透明合成。

维护仓库、SkillTest、NF中上述SceneCommands源码在报告时SHA256一致：

```text
ED778924A827CEE59F3BA75F6BA50E9E0F0581BA0D87073B1BCE67552AD9BF87
```

源码一致性不是已加载DLL身份的证明；本轮没有重新构建或部署。

## 复现建议（维护侧执行）

1. 在正常可渲染Editor场景中放置清楚可见的模型／地板，不必依赖本项目VFX或预览控制器。
2. 用唯一request_id调用capture_scene_viewport，并查询同ID至completed。
3. 在按Alpha正常合成的图片查看器或浏览器中打开回执的原始file_path。
4. 对PNG真实解压后统计Alpha，不能只看RGB缩略图或缓存预览。
5. 若近乎空白，另存Alpha=255的对照副本并验证RGB逐字节相同；原图保持不变。

这是本次具体文件的真实问题；没有宣称所有Editor设置／RHI或每次截图都会产生同样Alpha。

## 建议修复与验收标准

- 先明确工具合同：默认普通场景截图应为不透明；若需透明背景，使用独立且显式的选项。
  不要把未定义／无意义的framebuffer Alpha当作有效的场景透明度交付。
- 默认opaque路径可在PNG编码前令A=255，同时严格保持RGB、分辨率和色彩处理不变；
  或采用明确的RGB格式。保留透明选项时需验证该模式有实际有效的Alpha来源。
- 回执声明alpha_mode／实际是否不透明；返回success／saved不再被误解为已经完成图像语义检查。
- 在本例可复现条件下，默认输出的所有像素应不透明，普通图片查看器和浏览器均可正常显示。
- 对照保存前RGB和修正后RGB，证明未通过曝光、后期、缩放、颜色调整或重拍伪造结果。
- 覆盖带背景、黑色场景、半透明VFX、相机letterbox及不同输出尺寸。
  黑色但不透明的合法截图不应仅因RGB暗就被判失败。
- 继续保持异步pending／completed语义、任务ID幂等、相机稳定性、无资产／配置保存等边界。
- 需要部署并编译修复后的native插件；仅改共享Python签名不等于修好已运行Editor。

## 本地证据

证据副本放在本仓库既有Git忽略目录：

```text
Docs\reported-issues\evidence\2026-11-05\scene-capture\
  alpha-original.png
  alpha-opaque.png
  alpha-analysis.json
  alpha-task-completed.json
```

统计JSON同时记录原图路径／文件SHA256、Alpha分布、RGB SHA256和不透明副本的RGB一致性。
截图含本地授权制作的VFX和项目上下文，附件不纳入提交或向外上传，发布前需审查。
报告Markdown可以独立描述问题，不依赖把UE资产或整个素材包复制进维护仓库。

## 维护侧修复与验证（2026-11-05）

先独立提交之前的项目隔离代码`f5adff4`，再开始截图修复；本次修复未commit／部署。
普通场景截图现在明确为opaque合同，没有新增“保留未定义Alpha”的透明模式：

- ReadPixels后的FColor仅设置A=255，不改变RGB、分辨率、曝光、后期或VFX。
- 编码后实际解码PNG，核对宽高、每个像素A=255，以及RGB与输入逐通道完全一致。
- 回执从pending声明`alpha_mode=opaque`、`capture_receipt_contract=1`；
  保存完成后`opaque=true`。黑色但不透明的合法图片不会因暗而被拒绝。
- PNG先写同目录唯一临时文件，OS原子发布时拒绝覆盖目标。新增真实用例发现
  UE保存路径单加NoReplace标志未阻止覆盖，随后改为原子发布；原红日志保留，
  修复后pending中出现的既有文件内容保持不变，任务如实失败。

仓库隔离真实RHI矩阵18项通过：15个尺寸／纵横比／锁相机黑边／固定源／黑场景／
失败场景，加连续100张容量、持久化安全及codec测试。每个真实成功PNG解码检查Alpha；
codec包含A=0／127／255和RGB=0的输入，证明修改A后RGB逐字节保持。
在保存前后同一组像素上比对，不通过重拍、曝光或颜色调整伪造结果。
连续100张中的所有成功图片也经过实际编码后RGB／Alpha校验。
完整Python97项与显式独立进程旧回执冷读通过，异步pending／completed、
源视口／地图／相机／曝光稳定性、无资产保存及任务ID幂等继续保留。

证据日志在仓库项目`Saved/Logs/Capture-Matrix-Final.log`、
`Capture-ColdRead-Final.log`；保存冲突红／绿记录为
`Capture-NoOverwrite.log`、`Capture-NoOverwrite-Fixed.log`。
未改变用户原始PNG或另存修正副本，未操作NF／SkillTest、生产VFX或项目渲染配置。
没有新声明“首次Alpha=0的具体引擎Pass已找到”，也不将像素验证冒充特定生产VFX人工视觉验收。
对应工具说明已更新，但当前运行中的旧DLL需后续单独授权编译部署才获得修复。

## NF部署／SkillTest源码同步（2026-11-05）

本轮按用户要求只构建NF；SkillTest仅同步SceneCommands.cpp／.h和MCPSceneTests.cpp，
保留原DLL与Binaries／Intermediate清单，没有build、clean或启动SkillTest。
NF UE5.7.4构建38动作成功，实际DLL的18项矩阵及独立冷读通过。
随后原L_map01中通过公开grouped MCP保存独立320×180 PNG，回执为
`capture_receipt_contract=1`、`alpha_mode=opaque`、`opaque=true`、
`receipt_persisted=true`。System.Drawing独立解码所有57600像素Alpha均为255，
非黑像素57091；同ID重放后文件SHA256及写入时间保持，原视口／相机／地图／dirty0保持。

新验收图：`Saved/Screenshots/UnrealMCP/CaptureFixNF_4b27a1a07bfc444abcca262d209a8a5b.png`；
回执／像素分析：`Saved/UnrealMCPDeployment/CaptureFix_20261105_102338/receipt.json`、
`public-png-analysis.json`。此图是NF独立部署烟测，不是对报告中SkillTest原PNG的覆盖修正，
也不宣称原生产VFX的艺术效果已获用户确认。
SkillTest仍需用户手动build后才能在该项目获得新Alpha合同，Python描述刷新不是native部署。
未改变两个项目的绑定／端口／权限，没有commit／push。

## SkillTest完整部署（2026-11-05，后续授权）

用户后续明确授权SkillTest完整部署。SkillTestEditor增量7动作／1138.49秒构建成功；
实际新DLL的18项矩阵及显式独立冷读通过，包括连续100张PNG、RGB逐通道保持、
所有像素Alpha=255、合法黑场景、相机黑边和pending期间同名输出文件不被覆盖。
详细保护与冷读记录见[任务缓存报告](scene-capture-task-cache-exhaustion.md#skilltest完整部署2026-11-05后续授权)。

原Review地图／13091中使用原VSCode绑定配置公开截图，完成回执确认contract1、opaque、
saved=true及receipt_persisted=true。独立System.Drawing解码320×180全部57600像素，
非不透明像素0、非黑像素24855；同ID重放及原Claude配置重连后PNG哈希／时间保持，
原完整视口／相机／曝光／地图及dirty0保持。

新图：`Saved/Screenshots/UnrealMCP/CaptureFixSkill_ec0e8a387e7044238d1f52204deb2546.png`；
SHA256为`830f748baa59301887caef1d6d8697b7c0cdf9c7cef4fb74b057f2696cd16683`。
公开回执／像素验证：`Saved/UnrealMCPDeployment/CaptureBuild_20261105_143610/`
下的`public-capture-receipt.json`、`public-png-validation.json`及`claude-public-receipt.json`。
这是新工具合同的部署验收，不是对原报告PNG的覆盖修正或特定生产VFX艺术效果的人工确认。
14194份原件及全部14024个Content文件保持；未保存业务资产、修改渲染配置、操作NF或
commit／push。旧copy-only记录保留为历史，不代表现在的SkillTest DLL状态。
