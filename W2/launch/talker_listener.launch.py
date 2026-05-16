from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration

def generate_launch_description():
    param_file_arg = DeclareLaunchArgument(
        'params_file',
        default_value='',
        description='Path to YAML parameter file'
    )
    talker_node = Node(
        package='training_pkg',
        executable='talker',
        name='talker',
        parameters=[{'my_param': 'direct_value'}]
    )
    listener_node = Node(
        package='training_pkg',
        executable='listener',
        name='listener'
    )
    return LaunchDescription([
        param_file_arg,
        talker_node,
        listener_node,
    ])