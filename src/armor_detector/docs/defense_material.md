# 装甲板检测与数字识别项目答辩材料

## 1. 项目概述

### 1.1 背景
- **RoboMaster 机甲大师赛视觉组核心任务**：实时识别敌方机器人装甲板并输出其位置、颜色、数字类别。
- **输入**：相机 / 视频 / rosbag 图像流。
- **输出**：`/armor/result` 结构化消息，包含颜色、数字类别、置信度、四个有序角点。

### 1.2 目标
1. 一帧图像中检测**多个装甲板**。
2. 输出**固定 4 点**的装甲板角点。
3. 用 ROS2 自定义消息替代 JSON，便于下游节点订阅。
4. 接入 `tiny_resnet.onnx` 数字识别模型。

---

## 2. 系统架构

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│  camera_node     │     │  detector_node   │     │  gimbal/serial   │
│  mvs_camera_node │────▶│                  │────▶│  （下游控制节点） │
│  rosbag play     │     │  detect + classify│     │                  │
└──────────────────┘     └──────────────────┘     └──────────────────┘
                                │
                                ▼
                       /armor/result
                       (armor_interfaces/Armors)
```

### 2.1 自定义消息包 `armor_interfaces`

```msg
# Armor.msg
std_msgs/Header header
string color
string digit_label
float32 confidence
geometry_msgs/Point32[4] points   # 左上、右上、右下、左下
```

```msg
# Armors.msg
std_msgs/Header header
armor_interfaces/Armor[] armors
```

---

## 3. 核心算法实现

### 3.1 传统视觉检测链路

`include/armor_detector/armor_detect_core.hpp`

```text
原图 → 颜色空间转换 HSV → 红/蓝 mask → 形态学 → 找轮廓 → 灯条筛选 → 两两配对 → 装甲板
```

#### 灯条筛选条件
- 面积范围：`min_contour_area` ~ `max_contour_area`
- 长宽比：`aspect = length / width > min_aspect_ratio`
- 角度约束：长边与竖直方向夹角 < 25°
- 填充率：`contourArea / boundingRectArea > 0.3`

### 3.2 多装甲板检测

**改进前**：强制只保留 2 个灯条，一帧最多 1 个装甲板。

```cpp
if (bars.size() > 2) bars.resize(2);
```

**改进后**：全排列配对 + 几何验证。

```cpp
std::vector<Armor> matchArmors(const std::vector<Bar>& bars, bool is_red, const Params& p) const {
    std::vector<Armor> armors;
    for (size_t i = 0; i < bars.size(); ++i) {
        for (size_t j = i + 1; j < bars.size(); ++j) {
            if (p.pair_validation && !validatePair(bars[i], bars[j])) continue;
            auto quad = armorQuad(bars[i].rect, bars[j].rect);
            armors.push_back({is_red ? "red" : "blue", quad});
        }
    }
    return armors;
}
```

几何验证 `validatePair()` 检查：长度差、角度差、中心距、y 方向偏差。

### 3.3 装甲板四点类型

**改进前**：`std::vector<cv::Point> points`，长度不固定。

**改进后**：`std::array<cv::Point2f, 4> points`，顺序固定为左上、右上、右下、左下。

```cpp
struct Armor {
    std::string color;
    std::array<cv::Point2f, 4> points;
};
```

### 3.4 倾斜灯条端点计算

基于 `RotatedRect` 长轴方向计算端点，避免按 y 坐标排序在倾斜情况下出错。

```cpp
std::pair<cv::Point2f, cv::Point2f> ends(const cv::RotatedRect& rect) const {
    const cv::Point2f center = rect.center;
    const bool width_is_long = rect.size.width >= rect.size.height;
    float angle_rad = rect.angle * CV_PI / 180.0f;
    if (!width_is_long) angle_rad += CV_PI / 2.0f;

    const float half_len = (width_is_long ? rect.size.width : rect.size.height) * 0.5f;
    const cv::Point2f dir(std::cos(angle_rad), std::sin(angle_rad));

    return {center + dir * half_len, center - dir * half_len};
}
```

---

## 4. 数字识别模型接入

### 4.1 输入规格

`include/armor_detector/onnx_classifier.hpp`

| 项目 | 值 |
|------|-----|
| 输入尺寸 | `1 × 1 × 32 × 32` (NCHW) |
| 通道 | 单通道灰度 |
| 缩放 | 数字长边等比缩放到 24，再居中放入 32×32 黑色画布 |
| 归一化 | 像素值 ÷ 255.0 |

### 4.2 类别映射

| 索引 | 标签 |
|------|------|
| 0 | one |
| 1 | two |
| 2 | three |
| 3 | four |
| 4 | five |
| 5 | sentry |
| 6 | outpost |
| 7 | base |
| 8 | not_armor |

### 4.3 ROI 裁剪

**核心问题**：透视变换会保留装甲板长宽比，导致数字在 ROI 中占比过小，缩放到 32×32 后几乎不可见。

**解决方案**：基于装甲板四点中心，从原图直接裁出中心正方形 ROI，使数字占比最大化。

```cpp
static cv::Mat cropDigitRoi(const cv::Mat& bgr,
                            const std::array<cv::Point2f, 4>& pts) {
    // 1. 四点中心
    cv::Point2f center(0.0f, 0.0f);
    for (const auto& p : pts) center += p;
    center *= 0.25f;

    // 2. 取长边的 60% 作为中心正方形边长
    std::vector<cv::Point> int_pts;
    for (const auto& p : pts) int_pts.emplace_back(cvRound(p.x), cvRound(p.y));
    cv::Rect box = cv::boundingRect(int_pts);
    int side = static_cast<int>(std::max(box.width, box.height) * 0.6f);

    // 3. 从原图裁出中心正方形
    cv::Rect roi_rect(center.x - side / 2, center.y - side / 2, side, side);
    // ... 边界限制 ...
    return bgr(roi_rect).clone();
}
```

---

## 5. ROS2 工程化

### 5.1 发布自定义消息

`src/detector_node.cpp`

```cpp
armor_interfaces::msg::Armors out;
out.header = msg->header;
for (const auto& armor : result.armors) {
    armor_interfaces::msg::Armor a;
    a.color = armor.color;
    if (onnx_enabled_ && labels[i].valid) {
        a.digit_label = labels[i].label_text;
        a.confidence = labels[i].confidence;
    }
    for (size_t j = 0; j < 4; ++j) {
        a.points[j].x = armor.points[j].x;
        a.points[j].y = armor.points[j].y;
    }
    out.armors.push_back(a);
}
result_pub_->publish(out);
```

### 5.2 参数化配置

`config/params.yaml` 支持动态调节：
- 颜色阈值、HSV 范围
- 形态学核大小
- 灯条面积、长宽比约束
- 是否启用配对验证、严格灯条过滤
- ONNX 开关、模型路径、not_armor 阈值

---

## 6. 实验结果

### 6.1 单张图片测试

使用清晰装甲板照片：

```text
检测到装甲板: 1
红灯条: 3
ONNX 分类结果: one 置信度=0.998159
```

- 灯条检测成功
- 装甲板配对成功
- 数字 "1" 识别成功，置信度 **99.82%**

### 6.2 模型输入可视化

- ROI：512×512 中心正方形，数字清晰完整
- 32×32 输入：数字居中，周围保留黑边，匹配训练分布

### 6.3 多装甲板输出

`matchArmors()` 支持一帧输出多个装甲板，突破了原来 `bars.resize(2)` 的限制。

---

## 7. 问题发现与解决

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| 一帧只能输出 1 个装甲板 | `bars.resize(2)` 强制只保留 2 灯条 | 全排列配对 + 几何验证 |
| 装甲板 points 类型不固定 | 使用 `std::vector<cv::Point>` | 改为 `std::array<cv::Point2f, 4>` |
| JSON 输出不适合工程化 | 下游需解析字符串 | 自定义 ROS2 消息 `/armor/result` |
| 数字识别输出 `not_armor` | ROI 中数字占比过小 + 预处理撑满 32×32 | 中心正方形 ROI + inner=24 黑边填充 |
| 倾斜装甲板框画叉 | 按 y 坐标取端点不稳定 | 基于 RotatedRect 长轴方向取端点 |

---

## 8. 项目亮点

1. **模块化设计**：检测、分类、ROS 节点、消息包解耦，便于后续替换模型。
2. **工程化意识**：自定义 ROS 消息、参数化配置、launch 文件、`.gitignore` 管理模型文件。
3. **问题导向**：从 `not_armor` 0.99 的异常出发，通过对比实验定位到 ROI 裁剪和预处理分布问题。
4. **可扩展性**：全排列配对为后续加 NMS、跟踪、多目标优先级排序留下接口。

---

## 9. 后续工作

1. **NMS 去重**：全排列配对会产生冗余装甲板，加入 IOU 抑制。
2. **多目标跟踪**：结合装甲板 ID 和运动预测，提升连续帧稳定性。
3. **模型迭代**：用实际 ROI 样本重训或微调 `tiny_resnet.onnx`。
4. **多相机支持**：完善 launch 文件，支持真实相机 / rosbag / 视频文件一键切换。

---

## 10. 总结

本项目完成了从图像采集、装甲板检测、数字识别到 ROS2 结构化输出的完整视觉链路。通过全排列配对、固定四点类型、自定义 ROS 消息、中心正方形 ROI + inner=24 预处理等关键改进，实现了多装甲板检测与高置信度数字识别，满足 RM 视觉组的工程化需求。
