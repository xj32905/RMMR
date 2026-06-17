#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace armor_detect {

const std::array<std::string, 9> LABEL_NAMES = {
    "one", "two", "three", "four", "five",
    "sentry", "outpost", "base", "not_armor"
};

struct ClassifyResult {
    int label_id = -1;
    std::string label_text;
    float confidence = 0.0f;
    bool valid = false;
};

class OnnxClassifier {
public:
    OnnxClassifier() = default;

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

    ClassifyResult classify(const cv::Mat& bgr,
                            const std::array<cv::Point2f, 4>& armor_points,
                            float not_armor_threshold = 0.7f,
                            bool debug_log = false) {
        ClassifyResult out;
        if (!loaded_ || bgr.empty()) return out;

        cv::Mat roi = cropDigitRoi(bgr, armor_points);
        if (roi.empty()) return out;

        cv::Mat debug_input = preprocessForDebug(roi);
        if (!debug_input.empty()) {
            last_debug_input_ = debug_input.clone();
        }

        cv::Mat blob = preprocess(roi);
        if (blob.empty()) return out;

        net_.setInput(blob);
        cv::Mat output = net_.forward();
        if (output.empty()) return out;

        cv::Mat probs;
        cv::exp(output, probs);
        float sum = static_cast<float>(cv::sum(probs)[0]);
        if (sum > 0) probs /= sum;

        double max_val = 0;
        cv::Point max_loc;
        cv::minMaxLoc(probs.reshape(1, 1), nullptr, &max_val, nullptr, &max_loc);

        int id = max_loc.x;
        if (debug_log) {
            cv::Mat flat_probs = probs.reshape(1, 1);
            std::vector<std::pair<float, int>> ranked;
            ranked.reserve(flat_probs.total());
            for (int i = 0; i < static_cast<int>(flat_probs.total()); ++i) {
                ranked.emplace_back(flat_probs.at<float>(i), i);
            }
            std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.first > rhs.first;
            });

            const int top_k = std::min<int>(3, ranked.size());
            std::fprintf(stderr, "[OnnxClassifier] top%d:", top_k);
            for (int i = 0; i < top_k; ++i) {
                const int cls = ranked[i].second;
                const char* label = (cls >= 0 && cls < static_cast<int>(LABEL_NAMES.size()))
                    ? LABEL_NAMES[cls].c_str()
                    : "unknown";
                std::fprintf(stderr, " %s=%.4f", label, ranked[i].first);
            }
            std::fprintf(stderr, "\n");
        }
        if (id >= 0 && id < 9) {
            out.label_id = id;
            out.label_text = LABEL_NAMES[id];
            out.confidence = static_cast<float>(max_val);
            out.valid = true;

            if (out.label_text == "not_armor" && out.confidence >= not_armor_threshold) {
                out.valid = false;
            }
        }
        return out;
    }

    const cv::Mat& lastDebugInput() const { return last_debug_input_; }

    /// 从原图中裁出数字 ROI。
    /// 策略：基于装甲板四点中心，取中心正方形区域，使数字在 ROI 中占比较大，
    /// 便于后续缩放到 32×32 时保留清晰特征。
    static cv::Mat cropDigitRoi(const cv::Mat& bgr,
                                const std::array<cv::Point2f, 4>& pts) {
        // 计算四点中心
        cv::Point2f center(0.0f, 0.0f);
        for (const auto& p : pts) center += p;
        center *= 0.25f;

        // 计算四点 bounding box，用其长边的 60% 作为中心正方形边长
        std::vector<cv::Point> int_pts;
        int_pts.reserve(4);
        for (const auto& p : pts) int_pts.emplace_back(cvRound(p.x), cvRound(p.y));
        cv::Rect box = cv::boundingRect(int_pts);
        int side = static_cast<int>(std::max(box.width, box.height) * 0.6f);
        side = std::max(16, std::min(side, std::min(bgr.cols, bgr.rows)));

        cv::Rect roi_rect;
        roi_rect.x = static_cast<int>(center.x) - side / 2;
        roi_rect.y = static_cast<int>(center.y) - side / 2;
        roi_rect.width = side;
        roi_rect.height = side;

        // 边界限制
        roi_rect.x = std::max(0, std::min(roi_rect.x, bgr.cols - side));
        roi_rect.y = std::max(0, std::min(roi_rect.y, bgr.rows - side));
        if (roi_rect.width <= 0 || roi_rect.height <= 0) return {};
        return bgr(roi_rect).clone();
    }

    static cv::Mat preprocessForDebug(const cv::Mat& roi) {
        if (roi.empty()) return {};

        cv::Mat gray;
        if (roi.channels() == 3) {
            cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = roi.clone();
        }

        // 课程要求：输出 32×32 单通道黑底图。
        // 训练数据中数字并非撑满 32×32，周围有黑边，因此先缩放到 inner 尺寸再居中。
        const int target = 32;
        const int inner = 24;
        double scale = static_cast<double>(inner) / std::max(gray.cols, gray.rows);
        cv::Mat resized;
        cv::resize(gray, resized, cv::Size(), scale, scale, cv::INTER_LINEAR);

        cv::Mat canvas(target, target, CV_8UC1, cv::Scalar(0));
        int x_off = (target - resized.cols) / 2;
        int y_off = (target - resized.rows) / 2;
        resized.copyTo(canvas(cv::Rect(x_off, y_off, resized.cols, resized.rows)));
        return canvas;
    }

private:
    cv::Mat preprocess(const cv::Mat& roi) const {
        cv::Mat gray;
        if (roi.channels() == 3) {
            cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = roi.clone();
        }

        // 课程要求：输出 32×32 单通道黑底图。
        // 训练数据中数字占比约为 inner/target，保留黑边更匹配分布。
        const int target = 32;
        const int inner = 24;
        double scale = static_cast<double>(inner) / std::max(gray.cols, gray.rows);
        cv::Mat resized;
        cv::resize(gray, resized, cv::Size(), scale, scale, cv::INTER_LINEAR);

        cv::Mat canvas(target, target, CV_8UC1, cv::Scalar(0));
        int x_off = (target - resized.cols) / 2;
        int y_off = (target - resized.rows) / 2;
        resized.copyTo(canvas(cv::Rect(x_off, y_off, resized.cols, resized.rows)));

        cv::Mat blob = cv::dnn::blobFromImage(
            canvas, 1.0 / 255.0, cv::Size(target, target),
            cv::Scalar(), true, false);
        return blob;
    }

    cv::dnn::Net net_;
    bool loaded_ = false;
    cv::Mat last_debug_input_;
};

}  // namespace armor_detect
