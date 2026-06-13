#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace armor_detect {

// 数字标签映射表
const std::array<std::string, 9> LABEL_NAMES = {
    "one", "two", "three", "four", "five",
    "sentry", "outpost", "base", "not_armor"
};

struct ClassifyResult {
    int label_id = -1;          // 0~8, -1=未分类
    std::string label_text;     // "one", "two", ...
    float confidence = 0.0f;    // softmax 置信度
    bool valid = false;         // 推理是否成功
};

class OnnxClassifier {
public:
    OnnxClassifier() = default;

    /// 加载 ONNX 模型
    bool loadModel(const std::string& model_path) {
        try {
            net_ = cv::dnn::readNetFromONNX(model_path);
            loaded_ = !net_.empty();
            return loaded_;
        } catch (const cv::Exception& e) {
            fprintf(stderr, "[OnnxClassifier] 加载模型失败: %s\n", e.what());
            return false;
        }
    }

    bool isLoaded() const { return loaded_; }

    /// 输入：原图 BGR + 装甲板四点（左上 右上 右下 左下）
    /// 输出：分类结果
    ClassifyResult classify(const cv::Mat& bgr,
                            const std::array<cv::Point2f, 4>& armor_points) {
        ClassifyResult out;
        if (!loaded_ || bgr.empty()) return out;

        cv::Mat roi = cropArmor(bgr, armor_points);
        if (roi.empty()) return out;

        cv::Mat blob = preprocess(roi);
        if (blob.empty()) return out;

        net_.setInput(blob);
        cv::Mat output = net_.forward();
        if (output.empty()) return out;

        // output shape: [1, 9] → softmax + argmax
        cv::Mat probs;
        cv::exp(output, probs);
        float sum = static_cast<float>(cv::sum(probs)[0]);
        if (sum > 0) probs /= sum;

        double max_val = 0;
        cv::Point max_loc;
        cv::minMaxLoc(probs.reshape(1, 1), nullptr, &max_val, nullptr, &max_loc);

        int id = max_loc.x;
        if (id >= 0 && id < 9) {
            out.label_id = id;
            out.label_text = LABEL_NAMES[id];
            out.confidence = static_cast<float>(max_val);
            out.valid = true;
        }
        return out;
    }

private:
    /// 从原图裁出装甲板区域（boundingRect + margin）
    cv::Mat cropArmor(const cv::Mat& bgr,
                      const std::array<cv::Point2f, 4>& pts) const {
        // Point2f 转 Point 计算 boundingRect
        std::vector<cv::Point> int_pts;
        int_pts.reserve(4);
        for (const auto& p : pts) int_pts.emplace_back(cvRound(p.x), cvRound(p.y));
        cv::Rect box = cv::boundingRect(int_pts);

        // 加 15% margin
        int mw = cvRound(box.width * 0.15);
        int mh = cvRound(box.height * 0.15);
        box.x = std::max(0, box.x - mw);
        box.y = std::max(0, box.y - mh);
        box.width = std::min(bgr.cols - box.x, box.width + 2 * mw);
        box.height = std::min(bgr.rows - box.y, box.height + 2 * mh);
        if (box.width <= 0 || box.height <= 0) return {};
        return bgr(box).clone();
    }

    /// 预处理：灰度 → 等比缩放 32×32 → 黑底填充 → [0,1] 归一化
    cv::Mat preprocess(const cv::Mat& roi) const {
        // 1) 灰度单通道
        cv::Mat gray;
        if (roi.channels() == 3) {
            cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = roi.clone();
        }

        // 2) 等比缩放，短边缩到 32，长边等比压缩
        const int target = 32;
        double scale = static_cast<double>(target) / std::max(gray.cols, gray.rows);
        cv::Mat resized;
        cv::resize(gray, resized, cv::Size(),
                   scale, scale, cv::INTER_LINEAR);

        // 3) 黑底填充：放在 32×32 画布中央
        cv::Mat canvas(target, target, CV_8UC1, cv::Scalar(0));
        int x_off = (target - resized.cols) / 2;
        int y_off = (target - resized.rows) / 2;
        resized.copyTo(canvas(cv::Rect(x_off, y_off, resized.cols, resized.rows)));

        // 4) 归一化 [0, 1]，转为 NCHW blob
        cv::Mat blob = cv::dnn::blobFromImage(
            canvas, 1.0 / 255.0, cv::Size(target, target),
            cv::Scalar(), true, false);  // swapRB=false for grayscale
        return blob;
    }

    cv::dnn::Net net_;
    bool loaded_ = false;
};

}  // namespace armor_detect
