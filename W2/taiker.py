import rclpy
from rclpy.node import Node

def main(args=None):
    rclpy.init(args=args)
    li4_node = Node("li4")        # 创建节点，变量名是 li4_node
    rclpy.spin(li4_node)          # 这里传变量 li4_node
    rclpy.shutdown()

