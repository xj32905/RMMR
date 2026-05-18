from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration

def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value='',
            description='Path to YAML parameter file'
        ),
        Node(
            package='training_pkg',
            executable='talker',
            name='talker',
            parameters=[LaunchConfiguration('params_file')]
        ),
        Node(
            package='training_pkg',
            executable='listener',
            name='listener'
        ),
    ])
