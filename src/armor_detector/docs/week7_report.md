# 第 7 周最终报告：ROI 裁剪与 ONNX 数字识别预接入

## 1. 本周目标

第 7 周目标是在第 6 周 HSV 颜色检测、灯条筛选和装甲板四点输出基础上，为数字识别链路做工程预接入：

1. 从检测出的装甲板四点中裁剪数字 ROI。
2. 预留 `tiny_resnet.onnx` 数字分类模型加载入口。
3. 完成灰度化、32×32 等比例缩放、黑底填充、归一化等预处理代码。
4. 将颜色、四点、数字类别、置信度以 ROS2 结构化消息发布。
5. 保持 ONNX 默认关闭，避免模型文件缺失影响原有检测链路。

## 2. 当前完成内容

### 2.1 ROI 裁剪调试接口

已在 `detector_node.cpp` 中加入 ROI 保存功能，默认关闭：

```yaml
roi_debug_save: false
roi_debug_dir: "src/armor_detector/docs/week7_roi_samples"
roi_debug_max_count: 20
```

启用后，节点会根据检测结果中的四点计算 `boundingRect + 15% margin`，从原图保存 ROI 样例。

运行时可设置：

```bash
ros2 param set /detector_node roi_debug_save true
ros2 param set /detector_node roi_debug_max_count 20
ros2 param set /detector_node roi_debug_dir src/armor_detector/docs/week7_roi_samples
```

当前采用的是第一阶段低风险方案：

```text
四点 points -> boundingRect -> 加 margin -> 裁剪 ROI
```

后续更稳定方案是四点透视变换：

```text
points[4] -> getPerspectiveTransform -> warpPerspective -> 固定尺寸数字 ROI
```

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
ros2 launch armor_detector detector.launch.py onnx_enabled:=true onnx_model_path:=/path/to/tiny_resnet.onnx
```

当前默认关闭，原因是本地尚未找到 `tiny_resnet.onnx` 模型文件，也缺少模型元数据。

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

### 4.1 ONNX 真实推理未完成

当前不能宣称数字识别已实测完成。原因：

```text
tiny_resnet.onnx 模型文件尚未获得。
```

已查找：

```bash
find /home/xj/rm_test -name "tiny_resnet.onnx"
find /home/xj -name "tiny_resnet.onnx"
```

结果：未找到。

因此目前完成的是：

```text
ONNX 接口预接入 + 预处理代码 + 参数入口 + 输出字段预留
```

尚未完成的是：

```text
模型真实加载、数字分类准确率验证、真实错误样例分析
```

### 4.2 模型元数据未确认

启用模型前必须确认：

| 项目 | 当前状态 |
| --- | --- |
| 模型文件 | 未找到 |
| 输入尺寸 | 代码按 32×32 预处理，仍需与模型确认 |
| 输入通道 | 代码按灰度单通道处理，仍需与模型确认 |
| 类别顺序 | 当前为占位映射，需训练配置确认 |
| 归一化方式 | 当前为 [0,1]，需模型说明确认 |
| 输出维度 | 当前按 9 类处理，需模型确认 |

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

### 4.3 可视化截图和错误样例缺失

由于模型未获得、真实 ONNX 推理未跑通，目前还不能提供：

1. 至少 2 张带颜色和数字类别的最终识别截图。
2. 2~3 个真实分类错误样例。
3. 分类错误原因统计。

已有第 6 周真实相机调试截图，但它们只能证明颜色检测链路，不足以证明 ONNX 数字识别链路。

## 5. 错误风险分析

虽然未完成真实 ONNX 实测，但根据当前链路，后续最可能出现的问题包括：

### 5.1 ROI 裁剪偏移

原因：

- 灯条检测框偏大/偏小。
- 两根灯条配对不准。
- 当前 ROI 使用 boundingRect，倾斜装甲板会包含较多背景。

改进：

- 使用四点透视变换替代 boundingRect 裁剪。
- 对 ROI 保存样例进行人工检查。

### 5.2 类别映射错误

原因：

- 模型输出 index 与代码 `LABEL_NAMES` 顺序不一致。

改进：

- 从训练代码或模型说明中确认 label map。
- 用已知数字图片逐类测试。

### 5.3 预处理不匹配

原因：

- 模型可能不是灰度输入。
- 可能需要 mean/std 归一化。
- 可能要求白底/黑底或反色。

改进：

- 用 Netron 或训练代码确认输入格式。
- 对比训练集预处理代码。

### 5.4 多灯条误配对

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
2. ONNX 数字识别类已预接入，包含 32×32 灰度归一化预处理。
3. 检测结果已支持固定四点 std::array<cv::Point2f, 4>。
4. 检测结果已支持同一帧多个装甲板。
5. ROS2 输出已改为结构化消息 /armor/result。
6. 编译和 ROS2 接口验证通过。
```

当前不能写成：

```text
ONNX 数字识别已实测完成。
数字识别准确率已验证。
模型分类结果稳定。
```

因为模型文件和模型元数据尚未获得。

## 7. 后续计划

拿到 `tiny_resnet.onnx` 后继续：

1. 确认模型输入尺寸、通道数、归一化方式和类别映射。
2. 启用：

```bash
ros2 launch armor_detector detector.launch.py onnx_enabled:=true onnx_model_path:=/path/to/tiny_resnet.onnx
```

3. 查看结构化输出：

```bash
ros2 topic echo /armor/result
```

4. 保存至少 2 张带颜色和数字标签的调试图。
5. 保存 ROI 样例，分析 2~3 个错误案例。
6. 将 ROI 从 boundingRect 升级为四点透视变换。

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
src/armor_detector/docs/week7_report.md
src/armor_interfaces/CMakeLists.txt
src/armor_interfaces/package.xml
src/armor_interfaces/msg/Armor.msg
src/armor_interfaces/msg/Armors.msg
```
