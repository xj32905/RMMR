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
                            const cv::Rect2f& armor_rect,
                            float not_armor_threshold = 0.7f,
                            bool debug_log = false,
                            float vertical_bias = 0.25f) {
        ClassifyResult out;
        if (!loaded_ || bgr.empty()) return out;

        cv::Mat roi = cropDigitRoi(bgr, armor_rect, vertical_bias);
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

            // 低置信度抑制：仅当 top-1 不是 not_armor 且置信度不足时压制
            // not_armor 由其专属阈值控制，避免误杀正常分类
            if (out.label_text != "not_armor" && max_val < 0.45) {
                out.valid = false;
            }
            if (out.label_text == "not_armor" && out.confidence >= not_armor_threshold) {
                out.valid = false;
            }
        }
        return out;
    }

    const cv::Mat& lastDebugInput() const { return last_debug_input_; }

    /// 从原图中裁出数字 ROI。
    /// 策略：居中矩形 + 垂直偏移 — 宽 1.0x，高 1.0x，中心可上下偏移。
    static cv::Mat cropDigitRoi(const cv::Mat& bgr, const cv::Rect2f& armor_rect,
                                float vertical_bias = 0.25f) {
        float roi_w = armor_rect.width;
        float roi_h = armor_rect.height;
        float side = std::max(roi_w, roi_h);
        side = std::max(16.0f, std::min(side, static_cast<float>(std::min(bgr.cols, bgr.rows))));

        float cx = armor_rect.x + armor_rect.width * 0.5f;
        float cy = armor_rect.y + armor_rect.height * (0.5f + vertical_bias);

        cv::Rect roi;
        roi.x = std::max(0, cvRound(cx - side * 0.5f));
        roi.y = std::max(0, cvRound(cy - side * 0.5f));
        roi.width = std::min(cvRound(side), bgr.cols - roi.x);
        roi.height = std::min(cvRound(side), bgr.rows - roi.y);

        if (roi.width <= 0 || roi.height <= 0) return {};
        return bgr(roi).clone();
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
