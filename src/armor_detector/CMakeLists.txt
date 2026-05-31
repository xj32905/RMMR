cmake_minimum_required(VERSION 3.8)
project(armor_detector)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(OpenCV REQUIRED)

# ===== 加这两行：MVS SDK 路径 =====
include_directories(/opt/MVS/include)
link_directories(/opt/MVS/lib/64)

# 原来的节点（保留！本地视频调试用）
add_executable(camera_node src/camera_node.cpp)
target_link_libraries(camera_node MvCameraControl)
ament_target_dependencies(camera_node rclcpp sensor_msgs cv_bridge OpenCV)

# 识别节点：ROS IO 在 detector_node.cpp，纯算法在 include/armor_detector/armor_detect_core.hpp
add_executable(detector_node src/detector_node.cpp)
target_include_directories(detector_node PRIVATE include)
ament_target_dependencies(detector_node rclcpp sensor_msgs cv_bridge image_transport OpenCV)

# ===== 新增：海康 MVS 相机节点 =====
add_executable(mvs_camera_node src/mvs_camera_node.cpp)
target_link_libraries(mvs_camera_node MvCameraControl)
ament_target_dependencies(mvs_camera_node rclcpp sensor_msgs cv_bridge OpenCV)

install(TARGETS camera_node detector_node mvs_camera_node
        DESTINATION lib/${PROJECT_NAME})

ament_package()