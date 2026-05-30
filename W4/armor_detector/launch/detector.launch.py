from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os

def generate_launch_description():
    config = os.path.join(
        get_package_share_directory('armor_detector'),
        'config',
        'params.yaml'
    )
    
    return LaunchDescription([
        Node(
            package='armor_detector',
            executable='camera_node',
            name='camera_node',
            parameters=[config],
            output='screen'
        ),
        Node(
            package='armor_detector',
            executable='detector_node',
            name='detector_node',
            parameters=[config],
            output='screen'
        ),
    ])