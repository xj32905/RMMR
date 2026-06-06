# 装甲板检测项目自查与答辩提纲

## 1. 项目当前目标

本项目当前目标是完成一个 ROS2 装甲板检测原型：

```text
相机图像 -> 颜色分割 -> 灯条候选 -> 装甲板四点 -> ROS 结果输出
```

第 6 周重点是传统视觉检测链路在真实相机画面下的调试和稳定性验证。

第 7 周重点是为后续数字识别做准备，包括 ROI 裁剪验证和 ONNX 接口预接入。

## 2. 主要节点

### camera_node

作用：

```text
从相机或视频读取图像，发布到 /camera/image
```

当前配置中默认使用 MVS 工业相机：

```yaml
source_type: "mvs"
topic: "/camera/image"
```

如果使用普通摄像头或视频，可以切换为：

```yaml
source_type: "opencv"
```

### detector_node

作用：

```text
订阅 /camera/image
调用 armor_detect_core.hpp 中的 Detector::detect()
发布 /armor/debug_image
发布 /armor_result
```

输入：

```text
/camera/image    sensor_msgs/msg/Image
```

输出：

```text
/armor/debug_image    sensor_msgs/msg/Image
/armor_result         std_msgs/msg/String
```

## 3. 核心算法流程

当前核心算法位于：

```text
include/armor_detector/armor_detect_core.hpp
```

主流程：

```text
1. resize + gamma
2. BGR -> HSV
3. red_mask / blue_mask
4. morphology close/open
5. findContours
6. boundingRect 面积过滤
7. 长宽比过滤
8. 候选灯条排序
9. TOP2 灯条组成装甲板
10. 输出 color + 四点 points
```

当前设计是简化现场调参版，重点是参数少、逻辑直观、方便定位问题。

## 4. 为什么使用 boundingRect 面积

当前面积过滤使用：

```text
boundingRect.area()
```

而不是：

```text
cv::contourArea()
```

原因：

```text
远距离或较细灯条在二值 mask 中可能只有 1~2 像素宽，contourArea 容易非常小甚至接近 0。
boundingRect.area() 对细长目标更稳定，更适合当前灯条筛选。
```

## 5. 关键参数说明

### debug_mode

```yaml
debug_mode: "result"
```

可选值：

```text
result      显示最终检测结果
red_mask    只显示红色二值 mask，不继续跑 contours 和装甲板框
blue_mask   只显示蓝色二值 mask，不继续跑 contours 和装甲板框
candidates  显示候选和 rejected 原因，例如 SMALL/BIG/R<
```

注意：

```text
red_mask / blue_mask 模式会提前 return，不会画最终装甲板框。
```

### target_color

```yaml
target_color: "red"
```

可选值：

```text
red
blue
all
```

### s_min / v_min

用于控制 HSV 颜色分割的饱和度和亮度下限：

```yaml
s_min: 70
v_min: 70
```

调高可以减少背景干扰，但可能误杀较暗灯条。

### min_contour_area / max_contour_area

当前使用 boundingRect 面积：

```yaml
min_contour_area: 5.0
max_contour_area: 50000.0
```

第 6 周真实相机中曾经出现 `BIG`，原因是：

```text
max_contour_area=3000.0 对当前画面偏小，清晰红色区域被判定为过大。
```

修正：

```text
max_contour_area 提高到 50000.0
```

### min_aspect_ratio

```yaml
min_aspect_ratio: 1.5
```

用于保留长条形区域，过滤太接近正方形的区域。

### pair_validation

```yaml
pair_validation: false
```

作用：

```text
是否启用灯条配对几何验证。
```

当前默认关闭，保持简化 TOP2 方案。

### strict_lightbar_filter

```yaml
strict_lightbar_filter: false
```

作用：

```text
额外检查灯条角度和填充率，用于过滤背景伪灯条。
```

当前默认关闭，因为真实装甲板不足时贸然开启可能误杀有效灯条。

### onnx_enabled

```yaml
onnx_enabled: false
onnx_model_path: ""
```

ONNX 数字识别默认关闭。原因：

```text
tiny_resnet.onnx 尚未找到，输入尺寸、类别顺序和预处理方式尚未确认。
```

## 6. /armor_result 输出

当前输出使用：

```text
std_msgs/msg/String
```

内容为 JSON 风格字符串：

```json
{"detected":true,"armors":[{"color":"red","points":[[258,180],[368,180],[368,300],[258,300]]}]}
```

无检测时：

```json
{"detected":false,"armors":[]}
```

四点顺序：

```text
左上 -> 右上 -> 右下 -> 左下
```

当前 JSON 输出适合快速调试；后续工程化应考虑自定义 ROS2 消息。

## 7. 第 6 周真实调试结论

第 6 周主要现象：

```text
red_mask 能清晰提取红色区域。
candidates 模式中出现 BIG。
BIG 表示 boundingRect.area() > max_contour_area。
将 max_contour_area 从 3000.0 提高到 50000.0 后，result 模式恢复输出。
/armor_result 可以发布 color + points。
```

相关截图：

```text
docs/week6_red_mask.png
docs/week6_candidates_BIG.png
docs/week6_result.png
docs/week6_armor_result.png
```

## 8. 第 7 周当前进展

第 7 周已完成：

```text
1. 编写 week7_plan.md
2. 查找 tiny_resnet.onnx，当前未找到
3. 预接入 ONNX 分类接口，默认关闭
4. 新增 ROI debug export 功能，默认关闭
5. 整理 review_feedback_plan.md
```

ROI 保存相关参数：

```yaml
roi_debug_save: false
roi_debug_dir: "src/armor_detector/docs/week7_roi_samples"
roi_debug_max_count: 20
```

打开方式：

```bash
ros2 param set /detector_node roi_debug_save true
ros2 param set /detector_node roi_debug_max_count 20
ros2 param set /detector_node roi_debug_dir src/armor_detector/docs/week7_roi_samples
```

当前未能采集 ROI 样例的原因：

```text
装甲板和转换器暂时不可用，真实相机链路无法继续验证。
```

## 9. 已知不足

当前已知不足：

```text
1. 当前 TOP2 方案同一颜色最多输出一个装甲板。
2. points 使用 vector，类型上没有强制固定 4 点。
3. /armor_result 当前使用 JSON 字符串，不是自定义 ROS2 消息。
4. ONNX 分类没有实测，因为 tiny_resnet.onnx 未到位。
5. ROI 裁剪目前是 boundingRect + margin，尚未升级为透视变换。
```

## 10. 后续改进顺序

推荐顺序：

```text
1. 设备恢复后采集 ROI 样例
2. 检查 boundingRect ROI 是否能覆盖数字区域
3. 如果 ROI 偏斜严重，改为四点透视变换
4. 找到 tiny_resnet.onnx 并确认模型元数据
5. 启用 onnx_enabled 进行数字识别实测
6. 改进多装甲板输出
7. 将 points 改为固定四点结构
8. 用自定义 ROS2 msg 替代 JSON 字符串
```

## 11. 答辩时可以这样概括

可以这样说明当前项目：

```text
目前我完成的是一个可调参的 ROS2 装甲板检测原型。相机节点负责发布图像，检测节点负责订阅图像并调用纯 OpenCV 核心算法。核心链路是 HSV 颜色分割、轮廓提取、长宽比筛选和 TOP2 灯条配对。第 6 周我用真实相机完成了 red_mask、candidates 和 result 的对照调试，定位到 max_contour_area 过小导致 BIG，并把参数调整到 50000。第 7 周我没有强行宣称数字识别完成，而是先加入 ROI 保存调试开关，并整理 ONNX 接入前需要确认的模型文件、输入尺寸、类别顺序和预处理方式。后续会继续做 ROI 透视变换、多目标输出和自定义消息工程化。
```
