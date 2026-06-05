# 第 6 周：真实相机基线与定向改进记录

## 1. 本周目标

在真实相机输入下验证 `armor_detector` 的基础可用性，并围绕真实调试中出现的问题做定向改进和前后对比。

本周重点不是重写算法，而是在第 5 周已经整理好的 `armor_detector` 包基础上，完成真实相机场景下的稳定性检查、参数修正、复现素材记录和失败样例分析。

本周实际遇到的问题方向：

```text
red_mask 中灯条/红色区域已经清晰可见，但 candidates 模式中部分清晰目标被标记为 BIG，导致 result 模式无法稳定输出装甲板框。
```

## 2. 当前代码版本

仓库：

```text
https://github.com/xj32905/RMMR
```

包路径：

```text
src/armor_detector
```

当前检测链路：

```text
HSV mask -> findContours -> boundingRect 面积过滤 -> 长宽比过滤 -> TOP2 排序 -> 装甲板四点
```

ROS 节点输出：

```text
/armor_result        颜色 + 四点 JSON 风格结果
/armor/debug_image   调试图像
```

## 3. 基线参数与现场参数

参数文件：

```text
src/armor_detector/config/params.yaml
```

第 5 周/初始基线主要参数：

```yaml
target_color: "red"
debug_mode: "result"
s_min: 70
v_min: 70
r_thresh_low_h: 0
r_thresh_high_h: 10
r_thresh_low_h2: 170
r_thresh_high_h2: 180
b_thresh_low_h: 90
b_thresh_high_h: 130
morph_close_size: 3
morph_open_size: 0
min_contour_area: 5.0
max_contour_area: 3000.0
min_aspect_ratio: 1.5
sort_by: "score"
```

真实相机调试后当前现场参数：

```yaml
target_color: "red"
debug_mode: "result"
s_min: 70
v_min: 70
morph_close_size: 3
morph_open_size: 0
min_contour_area: 5.0
max_contour_area: 50000.0
min_aspect_ratio: 1.5
sort_by: "score"
pair_validation: false
strict_lightbar_filter: false
onnx_enabled: false
```

本次主要调整：

```text
max_contour_area: 3000.0 -> 50000.0
```

调整原因：真实相机画面中目标或红色区域较近/较亮时，`boundingRect.area()` 可能明显超过 3000。原上限会把清晰可见的区域直接标记为 `BIG`，导致无法进入候选灯条列表。

## 4. 素材来源

| 编号 | 来源 | 文件/截图 | 说明 |
| --- | --- | --- | --- |
| 1 | 真实相机 | `docs/week6_red_mask.png` | red_mask 模式，验证颜色分割结果 |
| 2 | 真实相机 | `docs/week6_candidates_BIG.png` | candidates 模式，复现 `BIG` 过滤现象 |
| 3 | 真实相机 | `docs/week6_result.png` | result 模式，调整参数后的检测效果 |
| 4 | ROS topic | `docs/week6_armor_result.png` | `/armor_result` 输出截图 |

## 5. 问题现象

现象描述：

```text
在 red_mask 中可以看到目标红色区域/灯条，说明 HSV 颜色分割基本有效；但 candidates 模式下，部分清晰区域被标记为 BIG，result 模式下无法形成稳定装甲板框。
```

初步判断：

```text
问题不是相机没有图像，也不是 red mask 完全失效，而是 findContours 后的面积上限过滤过严。真实画面中的清晰红色区域 boundingRect 面积超过 max_contour_area=3000，因此被过滤掉。
```

## 6. 改进内容

已做改进：

1. 使用 `boundingRect.area()` 作为轮廓面积，而不是 `cv::contourArea()`。
2. 增加 `candidates` 调试模式，显示通过筛选的候选灯条和被过滤原因。
3. 对被过滤轮廓显示原因：

```text
SMALL  面积太小
BIG    面积太大
R<     长宽比不足
ANGL   严格灯条筛选下角度不满足
FILL   严格灯条筛选下填充率不满足
```

4. 根据真实相机 candidates 结果，将 `max_contour_area` 从 `3000.0` 提高到 `50000.0`。
5. 保持 `pair_validation=false`、`strict_lightbar_filter=false`，避免在没有完整装甲板和充分样本前过早收紧筛选条件。
6. 保持 `onnx_enabled=false`，第 6 周只验证传统颜色 + 四点检测链路，ONNX 数字识别仅作为第 7 周预接入方向。

原因说明：

```text
细灯条或远距离灯条使用 contourArea 容易接近 0，因此使用 boundingRect.area() 更适合现场调参；但真实相机中近距离/较亮目标的 boundingRect 面积也可能变大，所以 max_contour_area 不能设置过小。本次通过 candidates 模式定位到 BIG 过滤原因，并放大面积上限。
```

## 7. 前后对比

### 改进前

```text
red_mask 中可以看到红色区域，但 candidates 中目标被标记为 BIG。由于 max_contour_area=3000.0 过小，清晰区域被误认为面积过大，从而不能进入 TOP2 候选，result 模式无法稳定出框。
```

对应素材：

```text
docs/week6_red_mask.png
docs/week6_candidates_BIG.png
```

### 改进后

```text
将 max_contour_area 调整为 50000.0 后，result 模式能够重新输出检测框，/armor_result 能发布 color + points 格式的检测结果。
```

对应素材：

```text
docs/week6_result.png
docs/week6_armor_result.png
```

## 8. 运行命令

编译：

```bash
cd /home/xj/rm_test
source /opt/ros/humble/setup.bash
colcon build --base-paths src/armor_detector --packages-select armor_detector --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

启动：

```bash
ros2 launch armor_detector detector.launch.py
```

查看检测结果：

```bash
ros2 topic echo /armor_result
```

查看调试图像：

```bash
ros2 run rqt_image_view rqt_image_view
```

如果 rqt_image_view 出现 Qt xcb 插件问题，可临时指定 Qt 插件路径后再启动：

```bash
export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt5/plugins/platforms
ros2 run rqt_image_view rqt_image_view
```

调试参数示例：

```bash
ros2 param set /detector_node debug_mode red_mask
ros2 param set /detector_node debug_mode candidates
ros2 param set /detector_node max_contour_area 50000.0
ros2 param set /detector_node debug_mode result
```

## 9. 未解决失败样例

| 编号 | 截图/素材 | 现象 | 推测原因 | 下一步 |
| --- | --- | --- | --- | --- |
| 1 | `docs/week6_candidates_BIG.png` | 清晰红色区域被标记为 BIG | 面积上限过小或 mask 区域较大 | 已将 `max_contour_area` 提高到 `50000.0` |
| 2 | `docs/week6_red_mask.png` | 无标准装甲板时，red_mask 仍会提取背景红色区域 | 当前链路只基于颜色和形状，缺少语义判断 | 后续使用真实装甲板继续固定参数，并在第 7 周接入数字分类 |
| 3 | `docs/week6_result.png` | 使用 TOP2 时可能将非标准目标组合成四点框 | 目前为简化现场调参版，几何验证默认关闭 | 后续根据现场误检情况尝试启用 `pair_validation` |

## 10. 下一步计划

1. 找到标准装甲板后，在不同距离、曝光、光照下继续采集真实样例。
2. 在真实装甲板上确认最终稳定参数，重点检查 `max_contour_area`、`min_contour_area`、`min_aspect_ratio`。
3. 根据误检情况决定是否启用 `pair_validation`；`strict_lightbar_filter` 暂不默认开启。
4. 第 7 周在当前 color + 四点输出后接入 ROI 裁剪和 `tiny_resnet.onnx` 数字分类。
5. ONNX 分类保持默认关闭，待模型路径、输入尺寸、类别顺序和预处理方式确认后再实测。
