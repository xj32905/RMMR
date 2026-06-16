#!/usr/bin/env python3
"""
订阅 /armor/debug_image，保存最新一帧到文件。
用法:
  python3 save_debug_image.py [输出路径]
  默认保存到: src/armor_detector/docs/w7_debug_latest.png
"""

import sys
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
import cv2
import numpy as np


def image_msg_to_bgr8(msg):
    if msg.encoding not in {"bgr8", "rgb8", "mono8"}:
        raise ValueError(f"unsupported image encoding: {msg.encoding}")

    channels = 1 if msg.encoding == "mono8" else 3
    expected_step = msg.width * channels
    data = np.frombuffer(msg.data, dtype=np.uint8)
    if msg.step < expected_step:
        raise ValueError(f"invalid image step: {msg.step} < {expected_step}")

    rows = data.reshape((msg.height, msg.step))[:, :expected_step]
    if channels == 1:
        gray = rows.reshape((msg.height, msg.width))
        return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

    img = rows.reshape((msg.height, msg.width, channels))
    if msg.encoding == "rgb8":
        return cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
    return img.copy()


class SaveDebugImageNode(Node):
    def __init__(self, output_path):
        super().__init__("save_debug_image")
        self.output_path = output_path
        self.sub = self.create_subscription(
            Image, "/armor/debug_image", 
            self.on_image, 
            10
        )
        self.saved = False
        self.get_logger().info(f"等待 /armor/debug_image，保存到: {output_path}")

    def on_image(self, msg):
        if self.saved:
            return
        try:
            cv_img = image_msg_to_bgr8(msg)
            cv2.imwrite(self.output_path, cv_img)
            self.get_logger().info(f"已保存: {self.output_path}")
            self.saved = True
        except Exception as e:
            self.get_logger().error(f"保存失败: {e}")


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "src/armor_detector/docs/w7_debug_latest.png"
    rclpy.init()
    node = SaveDebugImageNode(output)
    
    # 最多等 30 秒，收到一帧就退出
    end = node.get_clock().now() + rclpy.duration.Duration(seconds=30)
    while rclpy.ok() and not node.saved and node.get_clock().now() < end:
        rclpy.spin_once(node, timeout_sec=0.1)
    
    if not node.saved:
        node.get_logger().warn("30 秒内未收到图像")
    node.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
