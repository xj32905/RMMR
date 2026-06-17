# armor_detector

ROS2 装甲板检测包。基于 HSV 颜色分割 + 形态学筛选 + 几何配对，从图像中检测 RoboMaster 装甲板灯条并输出四点坐标。

## 1. 目录结构

```text
src/armor_detector/
├── CMakeLists.txt                          # 编译配置
├── package.xml                             # 包元信息
├── README.md                               # 本文件
├── config/
│   └── params.yaml                         # 所有可调参数入口
├── launch/
│   └── detector.launch.py                  # 一键启动脚本
├── include/
│   └── armor_detector/
│       ├── armor_detect_core.hpp           # 纯 OpenCV 检测算法（不依赖 ROS）
│       └── onnx_classifier.hpp             # ONNX 数字识别分类器（不依赖 ROS）
└── src/
    ├── detector_node.cpp                   # ROS 检测节点（订阅图像 + 调用算法 + 发布结果）
    ├── camera_node.cpp                     # 图像源节点（MVS 工业相机 / USB 摄像头 / 视频文件）
    └── mvs_camera_node.cpp                 # [备用] 海康 MVS 相机节点精简版
```

## 2. 架构设计

```
┌──────────────────────┐      ┌─────────────────────────────┐
│   camera_node        │      │   detector_node              │
│                      │──┐   │                              │
│ MVS/USB/Video →      │  │   │  /camera/image → cv::Mat     │
│ /camera/image        │  │   │       ↓                      │
└──────────────────────┘  │   │  readParams() → Params       │
                           └──>│       ↓                      │
                               │  Detector::detect()          │
                               │       ↓                      │
                               │  [W7] OnnxClassifier         │
                               │   ROI crop + classify        │
                               │       ↓                      │
                               │  /armor/result (Armors msg)  │
                               │  /armor/debug_image (+label) │
                               └──────────────────────────────┘
```

**算法/ROS 完全分离：**
- `armor_detect_core.hpp`：纯 C++/OpenCV，输入 `cv::Mat` + `Params` → 输出 `Result`（颜色、四点坐标、调试图像）。不依赖 ROS，可独立用于图片/视频/bag/相机。
- `detector_node.cpp`：ROS2 节点，只负责订阅图像、读取参数、调用检测核心、发布结果。

## 3. 代码分层原则

所有检测算法逻辑在 `armor_detect::Detector` 类中，**禁止将算法代码写入 ROS 回调函数**。新增检测功能请修改 `armor_detect_core.hpp`，新增 ROS 功能请修改 `detector_node.cpp`。

## 4. 依赖

| 依赖 | 版本要求 | 用途 |
|------|---------|------|
| ROS2 | Humble | 基础框架 |
| OpenCV | ≥4.5 | 图像处理 |
| cv_bridge | Humble | ROS ↔ OpenCV 图像转换 |
| image_transport | Humble | 图像传输 |
| MVS SDK | ≥2.0 | 海康工业相机（可选，USB 摄像头模式不需要） |

MVS SDK 安装路径（如不使用 MVS 相机可忽略）：

```text
/opt/MVS/include/MvCameraControl.h
/opt/MVS/lib/64/libMvCameraControl.so
```

## 5. 编译

```bash
cd /home/xj/rm_test
source /opt/ros/humble/setup.bash
colcon build --packages-select armor_interfaces armor_detector --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

## 6. 启动

### 6.1 真实相机模式（默认）

```bash
ros2 launch armor_detector detector.launch.py
```

等价于：

```bash
ros2 launch armor_detector detector.launch.py use_bag:=false
```

### 6.2 Bag 回放模式

```bash
ros2 launch armor_detector detector.launch.py use_bag:=true bag_path:=/path/to/your/bag
```

该命令会自动执行 `ros2 bag play` + 启动 `detector_node`，不启动 `camera_node`。

### 6.3 调试开关

```bash
# 关闭调试图像（减少带宽）
ros2 launch armor_detector detector.launch.py debug:=false

# 只看红色 mask
ros2 launch armor_detector detector.launch.py debug_mode:=red_mask

# 看候选灯条
ros2 launch armor_detector detector.launch.py debug_mode:=candidates

# 完整参数覆盖
ros2 launch armor_detector detector.launch.py debug:=true debug_mode:=result
```

### 6.4 W7 ONNX 数字识别预接入

ONNX 数字识别接口已预接入，默认关闭。当前已在 `/home/xj/Downloads/tiny_resnet.onnx` 找到模型，并验证 OpenCV DNN 可加载、测试输入 `1×1×32×32` 可 forward、输出维度为 `1×9`。仍需真实相机/装甲板画面完成最终实测。

```bash
# ONNX 实测启动示例
ros2 launch armor_detector detector.launch.py onnx_enabled:=true onnx_model_path:=/home/xj/Downloads/tiny_resnet.onnx
```

### 6.5 运行时调参

```bash
ros2 param set /detector_node target_color blue
ros2 param set /detector_node debug_mode red_mask
ros2 param set /detector_node pair_validation true
```

### 6.5 单独运行节点（高级）

```bash
# 只启动检测节点（需要已有 /camera/image 话题）
ros2 run armor_detector detector_node --ros-args --params-file install/armor_detector/share/armor_detector/config/params.yaml

# 相机节点用 OpenCV 播放视频
ros2 run armor_detector camera_node --ros-args -p source_type:=opencv -p video_path:=/path/to/video.mp4
```

## 7. 话题

| 话题 | 类型 | 方向 | 说明 |
|------|------|------|------|
| `/camera/image` | `sensor_msgs/msg/Image` | 输入 | 相机图像流 |
| `/armor/result` | `armor_interfaces/msg/Armors` | 输出 | 结构化装甲板数组，包含颜色、数字标签、置信度和四点 |
| `/armor/debug_image` | `sensor_msgs/msg/Image` | 输出 | 调试标注图像 |

### `/armor/result` 格式

查看结构化结果：

```bash
ros2 topic echo /armor/result
```

一帧结果类型：

```text
std_msgs/Header header
armor_interfaces/Armor[] armors
```

单个装甲板类型：

```text
std_msgs/Header header
string color
string digit_label
float32 confidence
geometry_msgs/Point32[4] points
```

四点顺序：**左上 → 右上 → 右下 → 左下**，可用于后续数字 ROI 裁剪和透视变换。

旧版 `/armor_result` JSON 字符串接口已被 `/armor/result` 结构化消息替代，后续模块应优先订阅结构化话题。

## 8. 检测链路

```text
BGR 图像
  → 缩放 + Gamma 校正
  → HSV 色彩空间转换
  → Red Mask / Blue Mask（inRange，两套 HSV 阈值）
  → 形态学操作（闭运算 / 开运算）
  → findContours → boundingRect
  → 灯条筛选：面积 + 长宽比 + 排序
  → 保留多个候选灯条
  → 灯条两两配对生成多个装甲板
  → [可选] 几何配对验证（pair_validation=true）
  → 装甲板四点数组输出
```

## 9. 参数配置入口

所有参数集中在 `config/params.yaml`，分三大类：

### 9.1 颜色分割（HSV）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `s_min` | 70 | S 通道下限 |
| `v_min` | 70 | V 通道下限 |
| `r_thresh_low_h` / `r_thresh_high_h` | 0 / 10 | 红色 H 区间1 |
| `r_thresh_low_h2` / `r_thresh_high_h2` | 170 / 180 | 红色 H 区间2 |
| `b_thresh_low_h` / `b_thresh_high_h` | 90 / 130 | 蓝色 H 区间 |
| `morph_close_size` | 3 | 闭运算核大小 |
| `morph_open_size` | 0 | 开运算核大小（0=关闭） |

### 9.2 灯条筛选

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `min_contour_area` | 5.0 | 最小 boundingRect 面积 |
| `max_contour_area` | 3000.0 | 最大 boundingRect 面积 |
| `min_aspect_ratio` | 1.5 | 最小长宽比 |
| `sort_by` | score | 排序依据: score / length / area / aspect |

### 9.3 装甲板配对

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `pair_validation` | false | false=宽松配对; true=启用几何配对验证 |

几何验证阈值硬编码在 `armor_detect_core.hpp` 的 `validatePair()` 中，基于装甲板物理约束：

| 约束项 | 硬编码值 | 说明 |
|--------|---------|------|
| 长度差 | < 长边的 60% | 两根灯条长度应接近 |
| 角度差 | < 30° | 两根灯条应近似平行 |
| 中心距比例 | [0.5, 5.0] × 平均长度 | 间距不能太近或太远 |
| y 偏移 | < 1.0 × 平均长度 | 垂直方向应接近 |

### 9.4 W6 严格灯条筛选

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `strict_lightbar_filter` | false | true=额外校验角度+填充率，过滤背景伪灯条 |

校验项硬编码在 `findBars()` 中：

| 校验项 | 硬编码值 | 说明 |
|--------|---------|------|
| 角度约束 | 长边偏离垂直 ±25° | 装甲板灯条竖直安装 |
| 填充率 | > 0.3 | contourArea / boundingRectArea，真灯条为实心矩形 |

### 9.5 W7 ONNX 数字识别

#### 9.5.1 模型输入规格

| 项目 | 值 | 说明 |
|------|-----|------|
| 输入尺寸 | `1 × 1 × 32 × 32` (NCHW) | batch=1，通道=1，宽高=32×32 |
| 输入通道 | 单通道灰度 | ROI 先转灰度再送入网络 |
| 缩放方式 | 等比缩放 + 黑底填充 | 长边等比缩到 32，短边保持比例，整体置于 32×32 黑色画布中央 |
| 归一化 | 像素值 ÷ 255.0 | 将 `[0, 255]` 映射到 `[0.0, 1.0]` |
| 数据格式 | NCHW | 由 `cv::dnn::blobFromImage` 生成，形状 `[1, 1, 32, 32]` |

#### 9.5.2 类别映射

模型输出 `1 × 9`，经过 softmax 后取 argmax，索引与标签对应关系如下（定义在 `onnx_classifier.hpp` 的 `LABEL_NAMES`）：

| 输出索引 | 标签 | 含义 |
|----------|------|------|
| 0 | `one` | 数字 1 |
| 1 | `two` | 数字 2 |
| 2 | `three` | 数字 3 |
| 3 | `four` | 数字 4 |
| 4 | `five` | 数字 5 |
| 5 | `sentry` | 哨兵 |
| 6 | `outpost` | 前哨站 |
| 7 | `base` | 基地 |
| 8 | `not_armor` | 非装甲板 / 背景误检 |

#### 9.5.3 ROI 如何从装甲板检测结果中裁出

数字识别 ROI 完全基于 `Armor.points`（检测到的装甲板四点，顺序：左上→右上→右下→左下），流程如下：

1. **透视变换拉正**：用 `cv::getPerspectiveTransform` + `cv::warpPerspective` 把倾斜的装甲板四点映射为正视矩形，得到 `plate_width × plate_height` 的装甲板正视图。
2. **去边距**：去掉左右 8%、上下 6% 的边距，剔除灯条边缘，只保留中间数字区域。
3. **灰度转换**：把 BGR ROI 转成单通道灰度。
4. **等比缩放**：把灰度图长边缩放到 32，短边按比例缩放。
5. **黑底填充**：将缩放后的图像居中放到 32×32 的黑色画布上。
6. **归一化 blob**：用 `cv::dnn::blobFromImage(..., 1.0/255.0, ...)` 生成 `[1, 1, 32, 32]` 的 float blob。

实现位置：`include/armor_detector/onnx_classifier.hpp` 中的 `cropDigitRoi()` 和 `preprocess()`。

#### 9.5.4 运行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `roi_debug_save` | true | 保存 ROI 与 `32×32` 输入图，便于继续收集重训样本 |
| `roi_debug_dir` | `src/armor_detector/docs/week7_roi_samples` | ROI 样本输出目录 |
| `roi_debug_max_count` | 100 | 单次运行最多保存的 ROI 样本数量 |
| `onnx_enabled` | false | true=启用 ONNX 数字识别 |
| `onnx_model_path` | `/home/xj/rm_test/assets/tiny_resnet.onnx` | `tiny_resnet.onnx` 模型文件路径 |
| `not_armor_threshold` | 0.7 | 模型输出 `not_armor` 且置信度超过阈值时，该分类结果不写入 `digit_label` |

启动示例：

```bash
ros2 launch armor_detector detector.launch.py onnx_enabled:=true onnx_model_path:=/home/xj/rm_test/assets/tiny_resnet.onnx
```

#### 9.5.5 当前状态

- 检测、配对、ROI 裁剪、ONNX 推理链路已全部打通。
- `/armor/result` 中每个 armor 的 `digit_label` 和 `confidence` 字段已按上述类别映射填充。
- 当前 `tiny_resnet.onnx` 对实测 ROI 明显偏向 `not_armor`（置信度 >0.99），因此数字识别准确率尚未验证，需与模型训练方确认训练配置、类别分布及预处理一致性。

## 10. Bag / 相机切换步骤

### 相机 → Bag 切换

1. 准备 bag 文件，确保包含 `/camera/image` 话题
2. 启动 bag 模式：

```bash
ros2 launch armor_detector detector.launch.py use_bag:=true bag_path:=/path/to/bag
```

### Bag → 相机切换

```bash
# 直接启动相机模式（默认）
ros2 launch armor_detector detector.launch.py
```

### Bag 话题是 `/image_raw` 的情况

```bash
# 先播放 bag（带 remap）
ros2 bag play /path/to/bag --remap /image_raw:=/camera/image

# 另一个终端启动检测节点
ros2 run armor_detector detector_node --ros-args --params-file install/armor_detector/share/armor_detector/config/params.yaml
```

## 11. 已知问题与后续计划

### 当前缺陷

1. **背景误检**：场地红/蓝色长条形物体会通过 HSV + 长宽比筛选，产生伪灯条。W6 已加入 `strict_lightbar_filter`（角度+填充率）和 `pair_validation`（几何配对验证），需真实场景验证效果。
2. **远距离漏检**：远距离灯条在图像中很小（<5px），mask 中只有 1~2 像素宽，`contourArea` 接近零，依赖 `boundingRect` 面积。
3. **光照敏感**：过曝/低照度场景下 HSV mask 可能失效，需要调节曝光或 S/V 阈值。
4. **ONNX 分类输入仍需优化**：模型 `/home/xj/rm_test/assets/tiny_resnet.onnx` 已找到并可加载，W3 视频链路可运行，但当前 `/armor/result` 中 `digit_label` 仍为空。已保存 `roi_*.png` 和 `roi_*_input32.png`，样例显示透视裁剪后数字仍可能靠边或被灯条干扰。
5. **真实多装甲板场景待验证**：当前代码已支持多个灯条两两配对和多个 armor 输出，但仍需真实多目标画面验证误配对情况。

### W6 状态（已完成本轮真实相机调试）

- 方向：**真实相机下检测稳定性与背景/面积过滤问题**
- 已做：通过 `red_mask`、`candidates`、`result` 三种模式采集截图证据
- 已定位：`max_contour_area=3000.0` 时，清晰红色区域可能被标记为 `BIG`
- 已调整：`max_contour_area` 提高到 `50000.0`，恢复 result 模式输出
- 后续：拿到标准装甲板后继续确认最终稳定参数

### W7 状态（ONNX 已接入，分类结果待稳定）

- ROI 保存调试接口已加入，默认关闭
- ROI 已从 `boundingRect` 升级为四点 `warpPerspective` 透视裁剪
- ROI 调试会同时保存 `roi_*.png` 和模型输入 `roi_*_input32.png`
- ONNX 数字识别接口已接入（`onnx_classifier.hpp` + `detector_node.cpp` 集成），默认关闭
- `points` 已改为固定 `std::array<cv::Point2f, 4>`
- 检测结果已支持同一帧多个装甲板
- 输出已改为 ROS2 结构化消息 `/armor/result`，类型为 `armor_interfaces/msg/Armors`
- 已找到模型 `/home/xj/rm_test/assets/tiny_resnet.onnx`，OpenCV DNN 可加载，测试输入 `1×1×32×32`，输出 `1×9`
- W3 视频验证结果：检测和 ONNX 链路可运行，但 `digit_label` 仍为空，需继续优化四点质量、类别映射或预处理匹配

## 12. 运行截图与调试

查看调试图像：

```bash
ros2 run rqt_image_view rqt_image_view
# 选择话题: /armor/debug_image
```

调试模式说明：

| debug_mode | 显示内容 |
|------------|---------|
| `result` | 最终装甲板四边形 + HUD 文字 |
| `red_mask` | 红色 HSV mask |
| `blue_mask` | 蓝色 HSV mask |
| `candidates` | 候选灯条标注 + 被拒绝轮廓及原因 |
