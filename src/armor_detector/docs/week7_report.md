# 第 7 周最终报告：ROI 裁剪与 ONNX 数字识别预接入

## 1. 本周目标

第 7 周目标是在第 6 周 HSV 颜色检测、灯条筛选和装甲板四点输出基础上，为数字识别链路做工程预接入：

1. 从检测出的装甲板四点中裁剪数字 ROI。
2. 预留 `tiny_resnet.onnx` 数字分类模型加载入口。
3. 完成灰度化、32×32 等比例缩放、黑底填充、归一化等预处理代码。
4. 将颜色、四点、数字类别、置信度以 ROS2 结构化消息发布。
5. 保持 ONNX 默认关闭，避免未完成实测前影响原有检测链路。

## 2. 当前完成内容

### 2.1 ROI 裁剪调试接口

已在 `detector_node.cpp` 中加入 ROI 保存功能，默认关闭：

```yaml
roi_debug_save: false
roi_debug_dir: "src/armor_detector/docs/week7_roi_samples"
roi_debug_max_count: 20
```

启用后，节点会根据检测结果中的四点进行透视裁剪，并额外保存模型真正吃到的 `32×32` 灰度输入图，便于核对 ROI 与分类输入是否一致。

运行时可设置：

```bash
ros2 param set /detector_node roi_debug_save true
ros2 param set /detector_node roi_debug_max_count 20
ros2 param set /detector_node roi_debug_dir src/armor_detector/docs/week7_roi_samples
```

当前已从第一阶段 `boundingRect + margin` 升级为四点透视裁剪：

```text
points[4]
  -> getPerspectiveTransform
  -> warpPerspective
  -> 裁出装甲板中心区域
  -> 保存 roi_*.png
  -> 灰度化/缩放/黑底填充到 32×32
  -> 保存 roi_*_input32.png
```

本次验证样例已整理到：

```text
src/armor_detector/docs/week7_roi_samples/w7_roi_input32_contact_sheet.png
```

从样例可以看到：透视裁剪比旧的中心方框更接近装甲板区域，但部分 `32×32` 输入中数字仍偏左、靠边或被灯条干扰。这说明 ROI 质量仍需继续优化，同时也需要确认模型训练时的输入分布是否与当前 ROI 一致。

### 2.2 ONNX 数字识别接口预接入

新增/完善：

```text
include/armor_detector/onnx_classifier.hpp
```

当前预处理流程：

```text
BGR ROI
  -> 灰度化
  -> 等比例缩放到 32×32 范围内
  -> 黑底填充到 32×32
  -> 归一化到 [0, 1]
  -> OpenCV DNN blob
  -> tiny_resnet.onnx forward
  -> softmax + argmax
```

当前参数入口：

```yaml
onnx_enabled: false
onnx_model_path: ""
```

启动示例：

```bash
ros2 launch armor_detector detector.launch.py onnx_enabled:=true onnx_model_path:=/home/xj/Downloads/tiny_resnet.onnx
```

当前默认关闭。现已在 `/home/xj/rm_test/assets/tiny_resnet.onnx` 接入模型文件，并通过 OpenCV DNN 加载和视频链路验证：模型可加载，检测节点可运行，`/armor/result` 可发布结构化结果。

但在 `W3/red_1000_output1.mp4` 上，当前输出仍出现：

```text
digit_label: ''
confidence: 0.0
```

因此当前结论是：ONNX 推理链路已接入并可运行，但当前模型对实测 ROI 的分类结果尚未稳定，不能宣称数字识别准确率已验证。

### 2.3 固定四点结构

装甲板四点由动态 vector 改为固定长度数组：

```cpp
std::array<cv::Point2f, 4> points;
```

四点顺序：

```text
左上、右上、右下、左下
```

这保证了 ROI 裁剪和后续透视变换始终使用固定 4 个点。

### 2.4 支持同一帧多装甲板

第 6 周反馈指出旧逻辑只保留 TOP2 灯条，导致同一帧只能输出一个装甲板。本周已整改：

- 不再强制 `bars.resize(2)`。
- 保留全部候选灯条。
- 对灯条进行两两配对。
- `Result::armors` 支持输出多个装甲板。

调试图中会显示多个目标序号，例如：

```text
Red#1, Red#2, Blue#1
```

### 2.5 ROS2 结构化消息输出

新增接口包：

```text
src/armor_interfaces
```

消息定义：

```text
src/armor_interfaces/msg/Armor.msg
src/armor_interfaces/msg/Armors.msg
```

单个装甲板：

```text
std_msgs/Header header
string color
string digit_label
float32 confidence
geometry_msgs/Point32[4] points
```

一帧结果：

```text
std_msgs/Header header
armor_interfaces/Armor[] armors
```

当前发布话题：

```text
/armor/result
```

类型：

```text
armor_interfaces/msg/Armors
```

查看结果：

```bash
ros2 topic echo /armor/result
```

## 3. 编译与接口验证

已完成编译验证：

```bash
cd /home/xj/rm_test
source /opt/ros/humble/setup.bash
source install/setup.bash
colcon build --packages-select armor_interfaces armor_detector --cmake-args -DCMAKE_BUILD_TYPE=Release
```

验证结果：

```text
Finished <<< armor_interfaces
Finished <<< armor_detector
```

消息接口验证：

```bash
ros2 interface show armor_interfaces/msg/Armors
ros2 interface show armor_interfaces/msg/Armor
```

接口可正常显示。

## 4. 当前未完成项与原因

### 4.1 ONNX 视频推理已接入，但分类结果未稳定

当前不能宣称数字识别已在真实画面中完成稳定识别。模型文件已接入：

```text
/home/xj/rm_test/assets/tiny_resnet.onnx
```

已完成验证：

```text
模型可加载
测试输入: 1×1×32×32
输出维度: 1×9
W3 视频链路可运行
/armor/result 可输出检测到的装甲板四点
```

本次在 `W3/red_1000_output1.mp4` 上运行后，检测结果中能输出红色装甲板，但数字字段仍为空：

```text
digit_label: ''
confidence: 0.0
```

结合保存的 `roi_*.png`、`roi_*_input32.png` 样例以及 ONNX raw top-3 调试日志，目前主要问题不是模型无法加载，也不是推理接口没有跑通，而是当前 `tiny_resnet.onnx` 对实测 ROI 的分类输出明显偏向 `not_armor`。

短测日志中，多数 ROI 的 top-1 输出为：

```text
[OnnxClassifier] top3: not_armor=0.9988 four=0.0004 two=0.0002
[OnnxClassifier] top3: not_armor=0.9996 three=0.0001 sentry=0.0001
```

这说明 ONNX forward 已经执行，但当前模型看到实测 ROI 后几乎全部判为 `not_armor`。因此问题更接近于“模型训练域/预处理方式与当前 ROI 输入不匹配”，ROI 四点质量仍会影响效果，但不是唯一原因。

因此目前完成的是：

```text
ONNX 接口接入 + 模型加载 + 视频链路运行 + 透视 ROI 调试证据 + 结构化输出
```

尚未完成的是：

```text
稳定非空 digit_label 输出、数字识别准确率验证、最终带数字标签截图
```

### 4.2 模型元数据未确认

启用模型前必须确认：

| 项目 | 当前状态 |
| --- | --- |
| 模型文件 | 已找到 `/home/xj/Downloads/tiny_resnet.onnx` |
| 输入尺寸 | OpenCV 测试输入 `1×1×32×32` 可 forward |
| 输入通道 | 当前按灰度单通道处理 |
| 类别顺序 | 当前为占位映射，仍需训练配置确认 |
| 归一化方式 | 当前为 [0,1]，仍需模型说明确认 |
| 输出维度 | 已验证为 `1×9` |

当前占位类别映射：

```text
0: one
1: two
2: three
3: four
4: five
5: sentry
6: outpost
7: base
8: not_armor
```

实际使用必须以训练代码或模型说明为准。

### 4.3 可视化截图和错误样例状态

目前已补充 ROI 与 `32×32` 输入图证据：

```text
src/armor_detector/docs/week7_roi_samples/w7_roi_input32_contact_sheet.png
```

这些样例能证明：检测结果已进入 ONNX 前处理链路，且可以看到模型输入图像。但由于当前模型 raw top-3 基本由 `not_armor` 占据，`/armor/result` 中 `digit_label` 仍为空，目前还不能提供“带稳定数字类别”的最终识别截图。

已有第 6 周真实相机调试截图可以证明颜色检测链路；本次 W3 视频样例可以证明 ONNX 前处理链路；但二者都不足以证明 ONNX 数字识别准确率。

## 5. 错误风险分析

虽然 ONNX 视频链路已经跑通，但数字分类结果尚未稳定。根据当前 raw top-3 日志和 ROI 样例，后续最可能出现的问题包括：

### 5.1 模型训练域与实测 ROI 不匹配

原因：

- 当前 `tiny_resnet.onnx` 可能是在居中、清晰、单独裁出的数字图案上训练的。
- 当前推理输入来自检测四点透视裁剪，可能包含灯条、黑边、背景或偏移数字。
- raw top-3 日志显示模型对实测 ROI 强烈偏向 `not_armor`，说明模型虽然能 forward，但不能稳定识别当前输入分布。

改进：

- 优先确认原模型训练配置，包括 label map、输入通道、resize/padding、归一化方式。
- 如果无法找到训练配置，则用当前检测链路保存的 `roi_*_input32.png` 重新整理数据集并重训/微调模型。
- 训练时控制 `not_armor` 类比例，避免负样本过多导致模型继续偏向 `not_armor`。

### 5.2 ROI 裁剪偏移

原因：

- 检测四点不够准确，透视变换后数字仍会靠边或被灯条干扰。
- 两根灯条配对不准。
- 当前 ROI 虽已使用 `warpPerspective`，但源四点质量决定了最终输入质量。

改进：

- 继续优化四点生成逻辑，确保 points 尽量覆盖完整装甲板区域。
- 对 `roi_*.png` 和 `roi_*_input32.png` 保存样例进行人工检查。
- ROI 调整应服务于模型输入一致性，不应单独用 margin 参数硬凑分类结果。

### 5.3 类别映射错误

原因：

- 模型输出 index 与代码 `LABEL_NAMES` 顺序不一致。

改进：

- 从训练代码或模型说明中确认 label map。
- 用已知数字图片逐类测试。

### 5.4 预处理不匹配

原因：

- 模型可能不是灰度输入。
- 可能需要 mean/std 归一化。
- 可能要求白底/黑底或反色。

改进：

- 用 Netron 或训练代码确认输入格式。
- 对比训练集预处理代码。

### 5.5 多灯条误配对

原因：

- 同一帧多个候选灯条两两组合，可能在复杂背景下形成假 armor。

改进：

- 实测时开启 `pair_validation`。
- 继续调几何约束阈值。
- 必要时增加装甲板宽高比、面积、颜色一致性约束。

## 6. 本周结论

本周已完成的可交付内容：

```text
1. ROI 裁剪调试入口已完成，默认关闭。
2. ROI 已从 `boundingRect` 升级为四点 `warpPerspective` 透视裁剪。
3. 已保存 `roi_*.png` 和 `roi_*_input32.png`，可直接核对模型输入质量。
4. ONNX 数字识别类已接入，包含 32×32 灰度归一化预处理。
5. 检测结果已支持固定四点 std::array<cv::Point2f, 4>。
6. 检测结果已支持同一帧多个装甲板。
7. ROS2 输出已改为结构化消息 /armor/result。
8. 编译和 ROS2 接口验证通过。
```

当前不能写成：

```text
ONNX 数字识别已实测完成。
数字识别准确率已验证。
模型分类结果稳定。
```

因为虽然模型文件已找到并可加载，视频链路也可运行，但当前 ONNX raw top-3 显示模型几乎全部输出 `not_armor`，导致 `/armor/result` 中数字字段仍为空。现阶段更适合表述为：ONNX 接入、模型加载、前处理和推理链路已打通；数字分类效果受模型训练域/预处理匹配影响，尚未达到稳定识别。

## 7. 后续计划

继续工作重点：

1. 固定当前可用检测参数，优先保留稳定输出装甲板四点和 ROI 的能力。
2. 确认 `tiny_resnet.onnx` 的训练配置：类别映射、输入通道、resize/padding、归一化方式。
3. 用当前检测链路批量保存 `roi_*_input32.png`，人工整理为 `one/two/three/four/five/sentry/outpost/base/not_armor` 数据集。
4. 如果原训练配置无法确认，则基于当前 ROI 数据集重训或微调一个新的 9 分类模型，并导出 ONNX。
5. 继续用同一段 W3 视频反复验证：

```bash
ros2 run armor_detector camera_node --ros-args --params-file /tmp/rm_w7_perspective_params.yaml
ros2 run armor_detector detector_node --ros-args --params-file /tmp/rm_w7_perspective_params.yaml
```

6. 查看结构化输出：

```bash
ros2 topic echo /armor/result --once
```

7. 当出现稳定非空 `digit_label` 后，再保存至少 2 张带颜色和数字标签的调试图。
8. 继续保存 ROI 与 `32×32` 输入图，分析 2~3 个错误案例。

## 8. 涉及文件

```text
src/armor_detector/include/armor_detector/armor_detect_core.hpp
src/armor_detector/include/armor_detector/onnx_classifier.hpp
src/armor_detector/src/detector_node.cpp
src/armor_detector/config/params.yaml
src/armor_detector/launch/detector.launch.py
src/armor_detector/CMakeLists.txt
src/armor_detector/package.xml
src/armor_detector/docs/week7_plan.md
src/armor_detector/docs/week7_feedback_fix_report.md
src/armor_detector/docs/week7_next_steps.md
src/armor_detector/docs/week7_retrain_prepare.md
src/armor_detector/docs/week7_report.md
src/armor_interfaces/CMakeLists.txt
src/armor_interfaces/package.xml
src/armor_interfaces/msg/Armor.msg
src/armor_interfaces/msg/Armors.msg
```
