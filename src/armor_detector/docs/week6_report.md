# 第 6 周：真实相机基线与定向改进记录

## 1. 本周目标

在真实相机输入下验证 `armor_detector` 的基础可用性，并围绕一个具体问题做定向改进和前后对比。

当前选择的问题方向：

```text
细灯条 / 远距离灯条在 mask 中可见，但可能在轮廓面积或长宽比筛选阶段被过滤，导致最终 result 不出框。
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

## 3. 基线参数

参数文件：

```text
src/armor_detector/config/params.yaml
```

当前主要参数：

```yaml
target_color: "red"      # red / blue / all
debug_mode: "result"     # result / red_mask / blue_mask / candidates
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

现场最终参数如有调整，记录在这里：

```yaml
# TODO: 粘贴最终现场参数
```

## 4. 素材来源

| 编号 | 来源 | 文件/截图 | 说明 |
| --- | --- | --- | --- |
| 1 | 真实相机 | `docs/baseline_result.png` | result 模式检测效果 |
| 2 | 真实相机 | `docs/baseline_candidates.png` | candidates 模式候选/过滤原因 |
| 3 | ROS topic | `docs/armor_result_topic.png` | `/armor_result` 输出截图 |
| 4 | bag/视频 | TODO | 如有录制，填写路径 |

## 5. 问题现象

现象描述：

```text
在 red_mask / blue_mask 中可以看到目标灯条，但 result 模式下可能无法形成装甲板框。
```

初步判断：

```text
颜色分割阶段基本有效，问题更可能出现在 findContours 后的候选灯条筛选阶段，尤其是面积过滤和长宽比过滤。
```

## 6. 改进内容

已做改进：

1. 使用 `boundingRect.area()` 作为轮廓面积，而不是 `cv::contourArea()`。
2. 增加 `candidates` 调试模式，显示通过筛选的候选灯条。
3. 对被过滤轮廓显示原因：

```text
SMALL  面积太小
BIG    面积太大
R<     长宽比不足
```

原因说明：

```text
远距离或很细的灯条在二值 mask 中可能只有 1~2 像素宽，cv::contourArea() 可能接近 0，容易误判为面积过小。boundingRect.area() 对这类细长目标更稳定，也更符合现场调参直觉。
```

## 7. 前后对比

### 改进前

```text
TODO: 放改进前现象截图或描述。
例如：mask 能看到灯条，但 result 不出框，且无法直接知道轮廓被过滤原因。
```

### 改进后

```text
TODO: 放改进后截图或描述。
例如：candidates 模式可以看到候选条及 SMALL/BIG/R< 原因，便于快速调整 min_contour_area、max_contour_area、min_aspect_ratio。
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

调试参数示例：

```bash
ros2 param set /detector_node debug_mode candidates
ros2 param set /detector_node target_color blue
ros2 param set /detector_node min_aspect_ratio 1.2
ros2 param set /detector_node min_contour_area 3.0
ros2 param set /detector_node debug_mode result
```

## 9. 未解决失败样例

至少记录 3 个失败样例。

| 编号 | 截图/素材 | 现象 | 推测原因 | 下一步 |
| --- | --- | --- | --- | --- |
| 1 | TODO | TODO | TODO | TODO |
| 2 | TODO | TODO | TODO | TODO |
| 3 | TODO | TODO | TODO | TODO |

## 10. 下一步计划

1. 在不同曝光、距离、光照下采集更多真实相机样例。
2. 固定一组稳定参数，减少现场临时调整。
3. 基于当前输出的四点结果，下一周接入数字 ROI 裁剪和 `tiny_resnet.onnx` 分类。
