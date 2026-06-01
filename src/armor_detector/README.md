# armor_detector

ROS2 装甲板检测包。当前版本用于真实相机现场调参和后续数字识别接入。

## 1. 功能

- 订阅图像话题 `/camera/image`
- 基于 HSV 颜色分割检测红色/蓝色装甲板灯条
- 通过面积和长宽比筛选长条灯条
- 对候选灯条排序，取前两根组成装甲板四边形
- 发布检测结果 `/armor_result`
- 发布调试图像 `/armor/debug_image`
- 支持 MVS 工业相机、OpenCV 摄像头/视频输入

当前检测链路保持简单：

```text
red_mask / blue_mask
  -> findContours
  -> boundingRect 面积过滤
  -> 长宽比过滤，筛出长条灯条
  -> 按 score / length / area / aspect 排序
  -> 取排序最高的两根
  -> 用两根长条上下端点画装甲板四边形
```

## 2. 目录结构

```text
armor_detector/
├── CMakeLists.txt
├── package.xml
├── config/
│   └── params.yaml
├── launch/
│   └── detector.launch.py
├── include/
│   └── armor_detector/
│       └── armor_detect_core.hpp
└── src/
    ├── camera_node.cpp
    ├── detector_node.cpp
    └── mvs_camera_node.cpp
```

## 3. 检测逻辑和节点逻辑分工

### `include/armor_detector/armor_detect_core.hpp`

纯 C++ / OpenCV 检测核心，不依赖 ROS。

输入：

```cpp
cv::Mat
armor_detect::Params
```

输出：

```cpp
armor_detect::Result
```

其中包含：

- 红/蓝轮廓数量
- 候选灯条数量
- 装甲板颜色
- 装甲板四个角点
- 调试图像

### `src/detector_node.cpp`

ROS2 包装节点，只负责：

- 声明和读取参数
- 订阅 `/camera/image`
- 将 ROS 图像转为 `cv::Mat`
- 调用 `armor_detect::Detector`
- 发布 `/armor_result`
- 发布 `/armor/debug_image`
- 输出节流日志

## 4. 依赖

- ROS2 Humble
- OpenCV
- cv_bridge
- image_transport
- MVS SDK，可选，用于海康/MVS 工业相机

MVS SDK 典型路径：

```text
/opt/MVS/include/MvCameraControl.h
/opt/MVS/lib/64/libMvCameraControl.so
```

## 5. 编译

由于工作区中可能存在重复包名，建议只编译当前包：

```bash
cd /home/xj/rm_test
source /opt/ros/humble/setup.bash
colcon build --base-paths src/armor_detector --packages-select armor_detector --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

## 6. 一键启动

```bash
cd /home/xj/rm_test
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch armor_detector detector.launch.py
```

`launch/detector.launch.py` 会启动：

- `camera_node`
- `detector_node`

并加载：

```text
config/params.yaml
```

## 7. 输入输出话题

### 输入

```text
/camera/image    sensor_msgs/msg/Image
```

### 输出

```text
/armor_result        std_msgs/msg/String
/armor/debug_image   sensor_msgs/msg/Image
```

`/armor_result` 使用 JSON 风格字符串，包含颜色和四个角点：

```json
{"detected":true,"armors":[{"color":"blue","points":[[312,185],[420,188],[418,260],[310,258]]}]}
```

未检测到时：

```json
{"detected":false,"armors":[]}
```

四点顺序为：

```text
左上、右上、右下、左下
```

该格式后续可用于裁剪数字 ROI 并接入数字分类模型。

## 8. 参数入口

主要参数在：

```text
config/params.yaml
```

常用检测参数：

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

现场可运行时调参，例如：

```bash
ros2 param set /detector_node target_color blue
ros2 param set /detector_node debug_mode blue_mask
ros2 param set /detector_node b_thresh_low_h 85
ros2 param set /detector_node b_thresh_high_h 140
ros2 param set /detector_node debug_mode candidates
ros2 param set /detector_node min_aspect_ratio 1.2
ros2 param set /detector_node debug_mode result
```

## 9. 调试图像

打开调试图像：

```bash
ros2 run rqt_image_view rqt_image_view
```

选择话题：

```text
/armor/debug_image
```

调试模式：

```text
red_mask      只看红色 mask
blue_mask     只看蓝色 mask
candidates    看候选灯条和过滤原因
result        看最终装甲板四边形
```

`candidates` 模式会显示：

- 通过筛选的 TOP1/TOP2 候选灯条
- 被过滤轮廓及原因：`SMALL`、`BIG`、`R<`

## 10. 真实相机输入

默认使用 MVS 工业相机：

```yaml
camera_node:
  ros__parameters:
    source_type: "mvs"
    mvs_use_gentl: true
    mvs_cti_path: "/opt/MVS/lib/64/MvProducerU3V.cti"
```

启动：

```bash
ros2 launch armor_detector detector.launch.py
```

如果没有连接相机，可能出现：

```text
MVS 未找到相机
图像源打开失败：source_type=mvs
```

这是未连接相机或 MVS Viewer 占用设备时的常见现象。

## 11. 视频和 bag 输入

### OpenCV 视频输入

```bash
ros2 run armor_detector camera_node --ros-args -p source_type:=opencv -p video_path:=/path/to/video.mp4 -p loop:=true
```

### bag 输入

检测节点只要求有图像话题输入到 `/camera/image`。

如果 bag 中已经包含 `/camera/image`：

```bash
ros2 bag play /path/to/bag
ros2 run armor_detector detector_node --ros-args --params-file /home/xj/rm_test/install/armor_detector/share/armor_detector/config/params.yaml
```

如果 bag 中图像话题是 `/image_raw`，使用 remap：

```bash
ros2 bag play /path/to/bag
ros2 run armor_detector detector_node --ros-args --remap /camera/image:=/image_raw --params-file /home/xj/rm_test/install/armor_detector/share/armor_detector/config/params.yaml
```

## 12. 已知问题

- 当前 `/armor_result` 使用 `std_msgs/msg/String` 的 JSON 风格文本输出，后续可替换为自定义 msg。
- 当前检测逻辑是现场调参版，重点保持链路简单，没有使用复杂灯条配对规则。
- 远距离或过暗灯条依赖 HSV、曝光和 `min_aspect_ratio` 参数调节。
- MVS 相机需要正确安装 SDK，并避免被 MVS Viewer 占用。
- 后续数字识别需要基于四点结果裁剪 ROI，再接入 `tiny_resnet.onnx`。
