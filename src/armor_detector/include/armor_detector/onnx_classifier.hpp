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

    static cv::Mat cropDigitRoi(const cv::Mat& bgr,
                                const std::array<cv::Point2f, 4>& pts) {
        const cv::Point2f top_left = pts[0];
        const cv::Point2f top_right = pts[1];
        const cv::Point2f bottom_right = pts[2];
        const cv::Point2f bottom_left = pts[3];

        const double top_width = cv::norm(top_right - top_left);
        const double bottom_width = cv::norm(bottom_right - bottom_left);
        const double left_height = cv::norm(bottom_left - top_left);
        const double right_height = cv::norm(bottom_right - top_right);

        const int plate_width = std::max(16, cvRound(std::max(top_width, bottom_width)));
        const int plate_height = std::max(16, cvRound(std::max(left_height, right_height)));

        std::array<cv::Point2f, 4> dst = {
            cv::Point2f(0.0f, 0.0f),
            cv::Point2f(static_cast<float>(plate_width - 1), 0.0f),
            cv::Point2f(static_cast<float>(plate_width - 1), static_cast<float>(plate_height - 1)),
            cv::Point2f(0.0f, static_cast<float>(plate_height - 1))
        };

        cv::Mat transform = cv::getPerspectiveTransform(pts.data(), dst.data());
        cv::Mat warped;
        cv::warpPerspective(bgr, warped, transform, cv::Size(plate_width, plate_height));
        if (warped.empty()) return {};

        const int margin_x = std::max(1, cvRound(plate_width * 0.08f));
        const int margin_y = std::max(1, cvRound(plate_height * 0.06f));
        int x = margin_x;
        int y = margin_y;
        int w = std::max(1, plate_width - 2 * margin_x);
        int h = std::max(1, plate_height - 2 * margin_y);
        if (x + w > warped.cols) w = warped.cols - x;
        if (y + h > warped.rows) h = warped.rows - y;
        if (w <= 0 || h <= 0) return {};
        return warped(cv::Rect(x, y, w, h)).clone();
    }

    static cv::Mat preprocessForDebug(const cv::Mat& roi) {
        if (roi.empty()) return {};

        cv::Mat gray;
        if (roi.channels() == 3) {
            cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = roi.clone();
        }

        const int target = 32;
        double scale = static_cast<double>(target) / std::max(gray.cols, gray.rows);
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

        const int target = 32;
        double scale = static_cast<double>(target) / std::max(gray.cols, gray.rows);
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
