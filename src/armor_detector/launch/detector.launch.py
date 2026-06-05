from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.conditions import IfCondition, UnlessCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    config = os.path.join(
        get_package_share_directory('armor_detector'),
        'config',
        'params.yaml'
    )

    # ===== launch 参数 =====
    use_bag = LaunchConfiguration('use_bag')
    bag_path = LaunchConfiguration('bag_path')
    debug = LaunchConfiguration('debug')
    debug_mode = LaunchConfiguration('debug_mode')
    onnx_enabled = LaunchConfiguration('onnx_enabled')
    onnx_model_path = LaunchConfiguration('onnx_model_path')

    return LaunchDescription([

        # ---- 启动参数声明 ----
        DeclareLaunchArgument(
            'use_bag', default_value='false',
            description='true=bag回放模式, false=真实相机模式'),
        DeclareLaunchArgument(
            'bag_path', default_value='',
            description='bag文件路径(仅use_bag:=true时需要)'),
        DeclareLaunchArgument(
            'debug', default_value='true',
            description='是否发布 /armor/debug_image'),
        DeclareLaunchArgument(
            'debug_mode', default_value='result',
            description='result / red_mask / blue_mask / candidates'),
        DeclareLaunchArgument(
            'onnx_enabled', default_value='false',
            description='W7: true=启用 ONNX 数字识别'),
        DeclareLaunchArgument(
            'onnx_model_path', default_value='',
            description='tiny_resnet.onnx 模型路径'),

        # ---- bag 模式：自动 ros2 bag play ----
        ExecuteProcess(
            condition=IfCondition(use_bag),
            cmd=['ros2', 'bag', 'play', bag_path],
            output='screen',
        ),

        # ---- 相机模式：启动 camera_node ----
        Node(
            condition=UnlessCondition(use_bag),
            package='armor_detector',
            executable='camera_node',
            name='camera_node',
            parameters=[config],
            output='screen',
        ),

        # ---- detector_node（两种模式都启动） ----
        Node(
            package='armor_detector',
            executable='detector_node',
            name='detector_node',
            parameters=[config, {
                'debug': debug,
                'debug_mode': debug_mode,
                'onnx_enabled': onnx_enabled,
                'onnx_model_path': onnx_model_path,
            }],
            output='screen',
        ),
    ])
