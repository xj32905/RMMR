# W7 装甲板数字识别 — 答辩材料

## 1. 项目概述

**RoboMaster 装甲板检测管线**，8 周培训的视觉组训练项目。本阶段（W7）在已有检测管线基础上接入 ONNX 数字识别分类。

### 整体架构

```
┌─────────────┐     ┌──────────────────────────────────┐
│ camera_node │     │ detector_node                    │
│             │──→  │  /camera/image → cv::Mat         │
│ MVS/USB/    │     │       ↓                          │
│ Video       │     │  Detector::detect()              │
└─────────────┘     │       ↓                          │
                    │  matchArmors (贪心配对)           │
                    │       ↓                          │
                    │  getArmorRectFromBars (居中 ROI)  │
                    │       ↓                          │
                    │  cropDigitRoi + preprocess        │
                    │       ↓                          │
                    │  ONNX 分类 (tiny_resnet)          │
                    │       ↓                          │
                    │  /armor/result (+ digit_label)   │
                    │  /armor/debug_image (+ 标注)     │
                    └──────────────────────────────────┘
```

---

## 2. 检测链路

```
BGR → 缩放+Gamma → HSV → Red/Blue Mask → 形态学
  → findContours → 面积+长宽比筛选 → 灯条
  → 贪心配对 → 装甲板四点 → 居中 ROI → 灰度+缩放→32×32
  → ONNX → softmax → 数字类别
```

---

## 3. W7 核心工作：数字识别模型接入

### 3.1 模型信息

| 项目 | 值 |
|------|-----|
| 模型 | tiny_resnet.onnx |
| 输入 | 1×1×32×32 灰度，[0,1] |
| 输出 | 1×9 → softmax |
| 类别 | one, two, three, four, five, sentry, outpost, base, not_armor |

### 3.2 预处理管线

```
BGR ROI → 灰度 → 长边等比缩放到 24 → 居中放 32×32 黑底 → ÷255 → ONNX
```

---

## 4. ROI 方案演进（核心攻关）

### 4.1 问题定义

灯条检测给出的是**灯条端点**（四条边角），数字在灯条**下方**的装甲板本体上。如何从灯条位置推断数字区域是核心难题。

### 4.2 方案对比

| # | 方案 | three 命中 | three 最高置信 | 结论 |
|---|------|-----------|---------------|------|
| 1 | bbox 中心 60% | ~8% | 0.64 | 偶尔命中 |
| 2 | bbox 底部 35% | 11% | 0.79 | 有改善 |
| 3 | 底部扩展 | 0% | - | 失败 |
| 4 | 透视变换 ×3 | 0% | - | warp 失真过大 |
| 5 | 装甲平面延伸 | 0% | - | 失败 |
| 6 | **居中装甲平面 ROI (最终)** | **52%** | **0.97** | ✅ |

### 4.3 最终方案：居中装甲平面 ROI

```cpp
// 从灯条中心直接构造 ROI
cv::Rect2f getArmorRectFromBars(left_bar, right_bar) {
    float w = distance(left.center, right.center);   // 灯条间距
    float h = avg(left.length, right.length);         // 灯条均长
    cv::Point2f center = midpoint(left, right);
    return Rect(center.x - w/2, center.y - h/2, w, h);
}

// 裁剪：居中正方形 + 垂直偏移
ROI: width = armor.width × 1.0, height = armor.height × 1.0
     center.y += height × 0.25  // vertical_bias
```

**关键决策：**
- ❌ 不用 boundingRect（丢失方向信息）
- ❌ 不用 EMA 平滑（锁定到平均位置，降低命中率 60→20%）
- ✅ 用灯条中心间距和均长直接定义装甲平面
- ✅ vertical_bias 在 [-0.1, 0.3] 均稳定（bias 不敏感）

---

## 5. 辅助优化

| 优化 | 方案 | 效果 |
|------|------|------|
| 灯条配对 | 贪心匹配（每灯条至多用一次） | 稳定 1 装甲板/帧 |
| 时间滤波 | 5 帧历史，2 票多数，仅正向 | 桥接不稳定帧 |
| 低置信抑制 | 非 not_armor 且 <0.45 → 压制 | 消除幻觉 |
| not_armor 阈值 | ≥0.7 → 压制 | 过滤背景误检 |

---

## 6. 实测结果

| 指标 | 数值 |
|------|------|
| `three` 命中率 | **52%**（310/602 帧） |
| `three` 最高置信度 | **0.97** |
| 置信度 ≥0.9 | 43% |
| 置信度 ≥0.7 | 79% |
| 幻觉（one/four） | ~0 |
| ROI 方案稳定性 | bias [-0.1, 0.3] 均 51-52% |

**标注结果图**（`docs/w7_result_success_*.png`）：装甲板框 + `red_three` 数字标签 + HUD。

---

## 7. 错误分析

### 7.1 主要错误源

| # | 错误现象 | 根因 | 改进方向 |
|---|---------|------|---------|
| 1 | 48% 帧判 `not_armor` | 视频中装甲板转至不利角度，数字确实不可见 | 更稳定视频源/实机相机 |
| 2 | 输出视频完全失败 | 压缩破坏数字细节 | 使用原始输入视频 |
| 3 | 透视变换方案失败 | warp 区域过小导致像素失真 | 需更大/更清晰的输入 |

### 7.2 教训

- **灯条信息 ≠ 数字位置**：灯条端点不包含数字区域，需要沿装甲平面扩展
- **EMA 是双刃剑**：平滑有助于稳定性，但不恰当的静态变量会锁定错误位置
- **简单方案往往最有效**：居中 ROI 比复杂的透视变换更可靠

---

## 8. 代码结构

```
src/armor_detector/
├── include/armor_detector/
│   ├── armor_detect_core.hpp    # 检测算法 + 配对 + getArmorRectFromBars
│   └── onnx_classifier.hpp      # ONNX 分类 + cropDigitRoi + preprocess
├── src/
│   ├── detector_node.cpp        # ROS 节点（订阅/发布/参数）
│   └── camera_node.cpp          # 图像源（MVS/USB/Video）
├── config/params.yaml           # 所有参数入口
├── launch/detector.launch.py    # 一键启动
└── docs/
    ├── failure_cases.md          # 错误样例分析
    └── w7_result_success_*.png   # 标注结果图
```

---

## 9. 使用方式

```bash
# 启动（使用 params.yaml 默认配置）
ros2 launch armor_detector detector.launch.py

# 调参
ros2 param set /detector_node vertical_bias 0.30
ros2 param set /detector_node debug_mode candidates
```

---

## 10. 后续方向

1. **实机相机接入**：用 MVS 相机替代视频，获得更清晰、更大的装甲板画面
2. **装甲板完整四点检测**：从检测端输出含数字区域的完整装甲板边界，而非仅灯条端点
3. **模型重训**：用自动裁剪的 ROI 收集数据，微调模型适配当前场景
