# 第 7 周：ROI 裁剪与 ONNX 数字识别预接入计划

## 1. 本周目标

在第 6 周已经跑通的颜色检测和四点输出基础上，为后续数字识别做准备。

本周目标保持为**方案准备 + 接口预接入 + 验证清单**，不强行宣称 ONNX 数字识别已经完成。

当前计划：

```text
/camera/image
  -> HSV mask
  -> contours
  -> light bars
  -> TOP2 armor
  -> armor 四点 points
  -> ROI 裁剪
  -> tiny_resnet.onnx 数字分类
  -> /armor_result 增加 digit/confidence
```

## 2. 当前已完成基础

当前包路径：

```text
src/armor_detector
```

第 6 周已经验证：

```text
1. 真实相机画面可以进入 detector_node
2. red_mask 能提取红色区域
3. candidates 模式能显示过滤原因，例如 BIG
4. 调整 max_contour_area 后 result 模式恢复输出
5. /armor_result 可以发布 color + points
```

当前 `/armor_result` 示例：

```json
{"detected":true,"armors":[{"color":"red","points":[[258,180],[368,180],[368,300],[258,300]]}]}
```

四点顺序：

```text
左上 -> 右上 -> 右下 -> 左下
```

该四点结果可以作为第 7 周 ROI 裁剪输入。

## 3. ONNX 当前状态

当前代码状态：

```text
ONNX 数字识别接口已预接入，默认关闭。
```

相关文件：

```text
include/armor_detector/onnx_classifier.hpp
src/detector_node.cpp
config/params.yaml
launch/detector.launch.py
```

当前参数：

```yaml
onnx_enabled: false
onnx_model_path: ""
```

当前限制：

```text
tiny_resnet.onnx 模型文件暂未找到。
```

已执行查找：

```bash
find /home/xj/rm_test -name "tiny_resnet.onnx"
find /home/xj -name "tiny_resnet.onnx"
```

结果：

```text
未找到 tiny_resnet.onnx
```

因此本周不能宣称数字识别已完成，只能写作：

```text
ONNX 数字识别接口已预接入，默认关闭；待 tiny_resnet.onnx 模型文件、输入尺寸、类别顺序和预处理方式确认后，再启用 onnx_enabled:=true 进行实测。
```

## 4. 第 7 周需要确认的信息

启用 ONNX 前必须确认：

| 项目 | 当前状态 | 说明 |
| --- | --- | --- |
| 模型文件 | 未找到 | 需要获得 `tiny_resnet.onnx` |
| 输入尺寸 | 未确认 | 例如 32x32 / 28x28 / 其他 |
| 输入通道 | 未确认 | 灰度单通道或 BGR/RGB 三通道 |
| 预处理方式 | 未确认 | 是否归一化、是否减均值/除方差 |
| 类别顺序 | 未确认 | 输出 index 到数字/类别名的映射 |
| ROI 方式 | 待验证 | boundingRect 裁剪或四点透视变换 |
| 置信度阈值 | 待验证 | 低置信度时是否输出 unknown |

## 5. ROI 裁剪方案

### 方案 A：boundingRect 裁剪

做法：

```text
对 armor.points 求 boundingRect，直接从原图中裁剪矩形 ROI。
```

优点：

```text
实现简单，当前代码方向已经接近该方案。
```

缺点：

```text
装甲板倾斜时会包含较多背景，数字区域不一定对齐，影响分类稳定性。
```

适合作为第一步验证。

### 方案 B：四点透视变换

做法：

```text
使用 armor.points 中的四点：左上、右上、右下、左下
通过 getPerspectiveTransform + warpPerspective
把装甲板区域矫正为固定尺寸 ROI。
```

优点：

```text
ROI 更标准，适合数字分类模型输入。
```

缺点：

```text
需要确认四点稳定性；如果检测框本身偏差较大，透视变换也会裁错。
```

推荐最终采用方案 B。

## 6. 计划实现步骤

### Step 1：保持 ONNX 默认关闭

```yaml
onnx_enabled: false
onnx_model_path: ""
```

原因：模型文件和元数据尚未确认，默认开启会影响第 6 周已经稳定的检测链路。

### Step 2：确认模型文件

```bash
find /home/xj/rm_test -name "tiny_resnet.onnx"
find /home/xj -name "tiny_resnet.onnx"
```

如果仍未找到，需要向学长或资料仓库确认模型来源。

### Step 3：确认模型输入输出

需要记录：

```text
输入尺寸
输入通道
归一化方式
类别标签顺序
输出维度
```

### Step 4：先验证 ROI 截图

在不启用 ONNX 的情况下，先把 ROI 裁剪出来保存或显示，确认裁剪区域是否正确。

验证标准：

```text
ROI 中应主要包含装甲板数字区域，背景占比不能太大。
```

### Step 5：启用 ONNX 实测

模型确认后再运行：

```bash
ros2 launch armor_detector detector.launch.py onnx_enabled:=true onnx_model_path:=/path/to/tiny_resnet.onnx
```

观察：

```text
/armor_result 是否增加 digit/confidence
/armor/debug_image 是否显示数字标签
分类是否稳定
```

## 7. 预期输出格式

当前第 6 周输出：

```json
{"detected":true,"armors":[{"color":"red","points":[[258,180],[368,180],[368,300],[258,300]]}]}
```

第 7 周 ONNX 验证后预期输出：

```json
{"detected":true,"armors":[{"color":"red","points":[[258,180],[368,180],[368,300],[258,300]],"digit":"one","confidence":0.92}]}
```

如果未启用 ONNX，保持第 6 周格式，不输出 `digit/confidence`。

## 8. 风险与应对

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| 模型文件未找到 | 无法实测 ONNX | 先完成预接入和验证方案 |
| 输入尺寸不一致 | 推理结果错误 | 读取模型说明或用 Netron 查看模型输入 |
| 类别顺序未知 | 数字标签可能错 | 必须从训练代码/说明中确认 |
| ROI 裁剪不准 | 分类不稳定 | 优先实现四点透视变换 |
| 检测误框 | 数字分类无意义 | 先保证 color + points 检测稳定 |

## 9. 当前结论

第 7 周当前可交付内容为：

```text
1. 已明确 ROI + ONNX 数字识别接入位置
2. 已保留 onnx_enabled / onnx_model_path 参数
3. 已确认 ONNX 默认关闭，不影响第 6 周检测链路
4. 已列出模型文件和元数据确认清单
5. 已规划 boundingRect 裁剪到透视变换的迭代路线
```

当前不能写成：

```text
数字识别已完成
ONNX 已实测通过
识别准确率已验证
```

因为 `tiny_resnet.onnx` 及其元数据尚未确认。
