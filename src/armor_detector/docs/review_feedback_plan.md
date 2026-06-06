# 评审反馈整理与后续整改计划

## 1. 反馈结论

本次反馈可以理解为：当前项目已经具备基础检测链路，能够完成从图像到装甲板四点输出的阶段性目标，但工程化和鲁棒性还需要继续补强。

当前应保持第 6 周稳定版本，不在考试前进行大范围重构。第 7 周优先做低风险准备工作：ROI 验证、ONNX 预接入、输出结构规划和后续整改计划。

## 2. 反馈点 1：同一帧只能输出一个装甲板

### 当前状态

当前检测链路是现场调参简化版：

```text
mask -> contours -> aspect bars -> top2 armor
```

因此每种颜色只取排序后的 TOP2 灯条，最多生成一个装甲板。

### 问题

如果同一帧中存在多个装甲板，当前逻辑无法全部输出，鲁棒性不足。

### 后续整改方向

后续可以改为：

```text
1. 先筛出所有候选灯条
2. 按左右位置/长度/高度差进行两两配对
3. 对每一对通过几何验证的灯条生成一个 armor
4. 输出 armors 数组
```

当前不建议立刻大改，因为多目标配对容易引入新的误检，需要真实装甲板和多目标画面验证。

## 3. 反馈点 2：points 类型可以更严格

### 当前状态

当前装甲板四点使用：

```cpp
std::vector<cv::Point> points;
```

虽然约定为 4 个点，但类型本身没有强制长度。

### 问题

工程上不够严格，后续 ROI 裁剪、透视变换和数字识别都默认依赖 4 个点。

### 后续整改方向

可以改为固定长度结构：

```cpp
std::array<cv::Point, 4> points;
```

四点顺序保持：

```text
左上 -> 右上 -> 右下 -> 左下
```

这属于中等风险改动，会影响 detector core、JSON 输出、ONNX 分类输入和调试绘制函数。建议在 Week7 ROI 验证稳定后再统一修改。

## 4. 反馈点 3：输出不建议长期使用 JSON

### 当前状态

当前 `/armor_result` 使用：

```text
std_msgs/msg/String
```

内容为 JSON 风格文本，例如：

```json
{"detected":true,"armors":[{"color":"red","points":[[258,180],[368,180],[368,300],[258,300]]}]}
```

### 问题

JSON 字符串适合快速调试，但不适合长期工程集成。其他节点需要手动解析字符串，类型不明确，字段变更也不方便维护。

### 后续整改方向

后续应定义 ROS2 自定义消息，例如：

```text
Armor.msg
ArmorArray.msg
```

可能字段：

```text
std_msgs/Header header
string color
geometry_msgs/Point[] points
string digit
float32 confidence
```

或者更严格地使用固定 4 个点字段：

```text
geometry_msgs/Point top_left
geometry_msgs/Point top_right
geometry_msgs/Point bottom_right
geometry_msgs/Point bottom_left
```

当前不建议在 Week6 收尾阶段替换输出格式，因为这会影响现有截图、报告和调试链路。可以作为 Week7/Week8 工程化整改任务。

## 5. 反馈点 4：AI 使用方式

### 当前理解

AI 可以帮助完成重复性代码、文档整理、命令检查和错误定位，但开发者需要理解每个模块的输入、输出和参数含义。

### 需要掌握的内容

当前项目至少需要自己能讲清楚：

```text
1. camera_node 输入/输出是什么
2. detector_node 订阅什么话题、发布什么话题
3. armor_detect_core.hpp 中 detect() 的主流程
4. params.yaml 中每个关键参数影响什么
5. debug_mode=result/red_mask/blue_mask/candidates 的区别
6. /armor_result 中 color 和 points 的含义
7. ONNX 为什么默认关闭
8. ROI 裁剪为什么要先验证再接模型
```

## 6. 当前优先级

### P0：保持 Week6 稳定版

```text
不要回滚已经验证过的 max_contour_area=50000.0
不要默认开启 pair_validation
不要默认开启 strict_lightbar_filter
不要默认开启 onnx_enabled
```

### P1：提交 Week7 已完成的小功能

```text
ROI debug export 已实现
roi_debug_save 默认关闭
编译已通过
```

### P2：设备恢复后补 ROI 样例

受阻原因：

```text
装甲板和转换器暂时不可用
```

恢复后运行：

```bash
ros2 param set /detector_node roi_debug_save true
ros2 param set /detector_node roi_debug_max_count 20
ros2 param set /detector_node roi_debug_dir src/armor_detector/docs/week7_roi_samples
```

保存 ROI 样例后检查裁剪是否正确。

### P3：找到 tiny_resnet.onnx 并确认模型元数据

需要确认：

```text
输入尺寸
输入通道
预处理方式
类别顺序
输出维度
```

### P4：工程化整改

后续再考虑：

```text
1. 多装甲板输出
2. points 改为固定 4 点结构
3. 自定义 ROS2 msg 替代 JSON 字符串
```

## 7. 当前结论

这份反馈不是否定当前成果，而是指出下一阶段工程化方向。

当前最稳的路线是：

```text
Week6：保持已提交的稳定检测与报告
Week7：完成 ROI 验证准备 + ONNX 预接入 + 整改计划
Week8 或后续：再做多目标、固定四点类型、自定义消息
```
