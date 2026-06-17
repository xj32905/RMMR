#!/usr/bin/env python3
"""
实验：改变数字 ROI 在 32x32 画布中的占比，观察模型输出变化。

用法：
  python3 experiment_input_size.py <roi图片路径>
"""

import sys
import cv2
import numpy as np

LABEL_NAMES = ["one", "two", "three", "four", "five",
               "sentry", "outpost", "base", "not_armor"]


def preprocess(roi_bgr, target_inner):
    """
    复现 onnx_classifier.hpp 的预处理：
    - ROI 转灰度
    - 长边等比缩放到 target_inner
    - 放在 32x32 黑色画布中央
    - 归一化到 [0,1]
    - 转成 NCHW blob
    """
    gray = cv2.cvtColor(roi_bgr, cv2.COLOR_BGR2GRAY)
    h, w = gray.shape
    scale = target_inner / max(h, w)
    new_h, new_w = int(h * scale), int(w * scale)
    resized = cv2.resize(gray, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

    canvas = np.zeros((32, 32), dtype=np.uint8)
    y_off = (32 - new_h) // 2
    x_off = (32 - new_w) // 2
    canvas[y_off:y_off+new_h, x_off:x_off+new_w] = resized

    blob = canvas.astype(np.float32) / 255.0
    blob = np.expand_dims(blob, axis=(0, 1))  # (1, 1, 32, 32)
    return blob


def classify(net, roi_bgr, target_inner, label=""):
    blob = preprocess(roi_bgr, target_inner)
    net.setInput(blob)
    output = net.forward()

    logits = output.reshape(-1)
    probs = np.exp(logits)
    probs = probs / np.sum(probs)

    top3_idx = np.argsort(probs)[-3:][::-1]
    top3 = [(LABEL_NAMES[i], float(probs[i])) for i in top3_idx]

    print(f"{label} target_inner={target_inner:2d}:  " + "  ".join(
        f"{name}={prob:.4f}" for name, prob in top3))
    return top3


def main():
    roi_path = sys.argv[1] if len(sys.argv) > 1 else "../docs/test_roi_0_red.png"
    roi = cv2.imread(roi_path)
    if roi is None:
        print(f"无法读取 ROI: {roi_path}")
        return

    model_path = "/home/xj/rm_test/assets/tiny_resnet.onnx"
    net = cv2.dnn.readNetFromONNX(model_path)
    print(f"模型加载: {'成功' if not net.empty() else '失败'}")
    print(f"ROI 尺寸: {roi.shape}")
    print()

    print("=== 实验1：数字缩小 + 加黑边 ===")
    for target in [32, 28, 24, 20, 16]:
        classify(net, roi, target, "实验1")

    print()
    print("=== 实验2：额外加 padding（先缩到 target，再在外面包更多黑边）===")
    # 让数字先缩到 24，但 canvas 用 40 或更大，然后再整体 resize 到 32
    for outer_canvas in [32, 40, 48]:
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        scale = 24 / max(gray.shape)
        new_h, new_w = int(gray.shape[0] * scale), int(gray.shape[1] * scale)
        resized = cv2.resize(gray, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

        canvas = np.zeros((outer_canvas, outer_canvas), dtype=np.uint8)
        y_off = (outer_canvas - new_h) // 2
        x_off = (outer_canvas - new_w) // 2
        canvas[y_off:y_off+new_h, x_off:x_off+new_w] = resized
        canvas32 = cv2.resize(canvas, (32, 32), interpolation=cv2.INTER_LINEAR)

        blob = canvas32.astype(np.float32) / 255.0
        blob = np.expand_dims(blob, axis=(0, 1))
        net.setInput(blob)
        output = net.forward()
        logits = output.reshape(-1)
        probs = np.exp(logits)
        probs = probs / np.sum(probs)
        top3_idx = np.argsort(probs)[-3:][::-1]
        top3 = [(LABEL_NAMES[i], float(probs[i])) for i in top3_idx]
        print(f"实验2 outer={outer_canvas:2d}->32:  " + "  ".join(
            f"{name}={prob:.4f}" for name, prob in top3))


if __name__ == "__main__":
    main()
