#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <armor_interfaces/msg/armors.hpp>
#include <armor_interfaces/msg/armor.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>

#include <armor_detector/armor_detect_core.hpp>
#include <armor_detector/onnx_classifier.hpp>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>

class ArmorDetectorNode : public rclcpp::Node {
public:
    ArmorDetectorNode() : Node("detector_node") {
        declareParams();

        // W7: ROI 裁剪调试 + ONNX 数字识别（可选，默认关闭）
        declare_parameter("roi_debug_save", false);
        declare_parameter("roi_debug_dir", "src/armor_detector/docs/week7_roi_samples");
        declare_parameter("roi_debug_max_count", 20);
        declare_parameter("onnx_enabled", false);
        declare_parameter("onnx_model_path", "");
        declare_parameter("not_armor_threshold", 0.7);

        sub_ = create_subscription<sensor_msgs::msg::Image>(
            "/camera/image", rclcpp::SensorDataQoS(),
            std::bind(&ArmorDetectorNode::onImage, this, std::placeholders::_1));
        debug_pub_ = create_publisher<sensor_msgs::msg::Image>("/armor/debug_image", 10);
        result_pub_ = create_publisher<armor_interfaces::msg::Armors>("/armor/result", 10);

        // 尝试加载 ONNX 模型
        onnx_enabled_ = get_parameter("onnx_enabled").as_bool();
        not_armor_threshold_ = static_cast<float>(get_parameter("not_armor_threshold").as_double());
        if (onnx_enabled_) {
            std::string model_path = get_parameter("onnx_model_path").as_string();
            if (!model_path.empty() && classifier_.loadModel(model_path)) {
                RCLCPP_INFO(get_logger(), "ONNX 模型已加载: %s", model_path.c_str());
            } else {
                RCLCPP_WARN(get_logger(), "ONNX 模型加载失败或路径为空，关闭数字识别");
                onnx_enabled_ = false;
            }
        }

        RCLCPP_INFO(get_logger(), "detector_node started: ROS IO only, algorithm in armor_detect_core.hpp");
    }

private:
    void declareParams() {
        declare_parameter("debug", true);
        declare_parameter("debug_mode", "result");  // result / red_mask / blue_mask / candidates
        declare_parameter("target_color", "red");  // red / blue / all
        declare_parameter("target_width", 1280);
        declare_parameter("gamma", 1.0);
        declare_parameter("log_interval_ms", 1000);

        declare_parameter("s_min", 70);
        declare_parameter("v_min", 70);
        declare_parameter("r_thresh_low_h", 0);
        declare_parameter("r_thresh_high_h", 10);
        declare_parameter("r_thresh_low_h2", 170);
        declare_parameter("r_thresh_high_h2", 180);
        declare_parameter("b_thresh_low_h", 90);
        declare_parameter("b_thresh_high_h", 130);
        declare_parameter("morph_close_size", 3);
        declare_parameter("morph_open_size", 0);

        declare_parameter("min_contour_area", 5.0);
        declare_parameter("max_contour_area", 3000.0);
        declare_parameter("min_aspect_ratio", 1.5);
        declare_parameter("sort_by", "score");  // score / length / area / aspect

        declare_parameter("pair_validation", false);
        declare_parameter("strict_lightbar_filter", false);
    }

    armor_detect::Params readParams() {
        armor_detect::Params p;
        debug_ = get_parameter("debug").as_bool();
        p.debug_mode = get_parameter("debug_mode").as_string();
        p.target_color = get_parameter("target_color").as_string();
        p.sort_by = get_parameter("sort_by").as_string();
        p.target_width = std::clamp((int)get_parameter("target_width").as_int(), 160, 3840);
        p.gamma = std::max(0.05, get_parameter("gamma").as_double());
        log_interval_ms_ = std::max(0, (int)get_parameter("log_interval_ms").as_int());

        p.s_min = std::clamp((int)get_parameter("s_min").as_int(), 0, 255);
        p.v_min = std::clamp((int)get_parameter("v_min").as_int(), 0, 255);
        p.r_h_low1 = std::clamp((int)get_parameter("r_thresh_low_h").as_int(), 0, 180);
        p.r_h_high1 = std::clamp((int)get_parameter("r_thresh_high_h").as_int(), 0, 180);
        p.r_h_low2 = std::clamp((int)get_parameter("r_thresh_low_h2").as_int(), 0, 180);
        p.r_h_high2 = std::clamp((int)get_parameter("r_thresh_high_h2").as_int(), 0, 180);
        p.b_h_low = std::clamp((int)get_parameter("b_thresh_low_h").as_int(), 0, 180);
        p.b_h_high = std::clamp((int)get_parameter("b_thresh_high_h").as_int(), 0, 180);
        p.morph_close_size = std::max(0, (int)get_parameter("morph_close_size").as_int());
        p.morph_open_size = std::max(0, (int)get_parameter("morph_open_size").as_int());

        p.min_contour_area = std::max(0.0, get_parameter("min_contour_area").as_double());
        p.max_contour_area = std::max(p.min_contour_area, get_parameter("max_contour_area").as_double());
        p.min_aspect_ratio = std::max(1.0, get_parameter("min_aspect_ratio").as_double());
        p.pair_validation = get_parameter("pair_validation").as_bool();
        p.strict_lightbar_filter = get_parameter("strict_lightbar_filter").as_bool();
        return p;
    }

    void onImage(const sensor_msgs::msg::Image::SharedPtr msg) {
        try {
            cv::Mat img = cv_bridge::toCvShare(msg, "bgr8")->image;
            if (img.empty()) return;

            auto p = readParams();
            auto result = detector_.detect(img, p);

            // W7: 先保存 ROI 样例，验证四点裁剪是否可靠；默认关闭，不影响 W6 检测链路。
            saveRoiSamples(img, result);

            // W7: ONNX 数字识别
            std::vector<armor_detect::ClassifyResult> labels;
            if (onnx_enabled_) {
                for (const auto& armor : result.armors) {
                    labels.push_back(classifier_.classify(img, armor.points, not_armor_threshold_, debug_));
                }
            }

            auto summary = detector_.summary(result, p);
            publishResult(msg->header, result, labels);
            log(summary);

            if (!debug_ || result.debug_image.empty()) return;
            cv::Mat debug_img = result.debug_image;
            if (debug_img.channels() == 1) cv::cvtColor(debug_img, debug_img, cv::COLOR_GRAY2BGR);
            drawLabels(debug_img, result, labels);
            drawHud(debug_img, summary);
            debug_pub_->publish(*cv_bridge::CvImage(msg->header, "bgr8", debug_img).toImageMsg());
        } catch (const std::exception& e) {
            RCLCPP_ERROR(get_logger(), "detector_node error: %s", e.what());
        }
    }

    void publishResult(const std_msgs::msg::Header& header,
                       const armor_detect::Result& result,
                       const std::vector<armor_detect::ClassifyResult>& labels) {
        armor_interfaces::msg::Armors out;
        out.header = header;

        for (size_t i = 0; i < result.armors.size(); ++i) {
            const auto& armor = result.armors[i];
            armor_interfaces::msg::Armor a;
            a.color = armor.color;

            if (onnx_enabled_ && i < labels.size() && labels[i].valid) {
                a.digit_label = labels[i].label_text;
                a.confidence = labels[i].confidence;
            } else {
                a.digit_label = "";
                a.confidence = 0.0f;
            }

            for (size_t j = 0; j < 4; ++j) {
                a.points[j].x = armor.points[j].x;
                a.points[j].y = armor.points[j].y;
                a.points[j].z = 0.0f;
            }

            out.armors.push_back(a);
        }

        result_pub_->publish(out);
    }

    void saveRoiSamples(const cv::Mat& img, const armor_detect::Result& result) {
        if (!get_parameter("roi_debug_save").as_bool()) return;
        const int max_count = std::max(0, (int)get_parameter("roi_debug_max_count").as_int());
        if (max_count <= 0 || roi_save_count_ >= max_count) return;

        const std::string dir = get_parameter("roi_debug_dir").as_string();
        if (dir.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                                 "ROI 保存目录创建失败: %s", dir.c_str());
            return;
        }

        for (size_t i = 0; i < result.armors.size() && roi_save_count_ < max_count; ++i) {
            const auto& armor = result.armors[i];

            const cv::Mat roi = armor_detect::OnnxClassifier::cropDigitRoi(img, armor.points);
            if (roi.empty()) continue;

            const cv::Mat input = armor_detect::OnnxClassifier::preprocessForDebug(roi);
            const std::string stem = "/roi_" + std::to_string(roi_save_count_) + "_" + armor.color;
            const std::string roi_path = dir + stem + ".png";
            const std::string input_path = dir + stem + "_input32.png";
            bool saved = cv::imwrite(roi_path, roi);
            if (!input.empty()) saved = cv::imwrite(input_path, input) && saved;
            if (saved) {
                ++roi_save_count_;
                RCLCPP_INFO(get_logger(), "保存 ROI 样例: %s", roi_path.c_str());
            }
        }
    }

    void drawLabels(cv::Mat& img, const armor_detect::Result& result,
                    const std::vector<armor_detect::ClassifyResult>& labels) {
        for (size_t i = 0; i < result.armors.size() && i < labels.size(); ++i) {
            if (!labels[i].valid) continue;
            const auto& pts = result.armors[i].points;
            std::string tag = result.armors[i].color + "_" + labels[i].label_text;
            cv::Point center(static_cast<int>((pts[0].x + pts[1].x + pts[2].x + pts[3].x) / 4.0f),
                             static_cast<int>((pts[0].y + pts[1].y + pts[2].y + pts[3].y) / 4.0f));
            cv::putText(img, tag, center + cv::Point(0, 22),
                        cv::FONT_HERSHEY_SIMPLEX, 0.8, {0, 255, 255}, 2);
        }
    }

    void drawHud(cv::Mat& img, const std::string& text) {
        cv::putText(img, text, {10, 28}, cv::FONT_HERSHEY_SIMPLEX, 0.7, {0, 255, 0}, 2);
    }

    void log(const std::string& text) {
        if (log_interval_ms_ <= 0) return;
        auto now = this->now();
        if (last_log_.nanoseconds() == 0 || (now - last_log_).nanoseconds() / 1000000 >= log_interval_ms_) {
            RCLCPP_INFO(get_logger(), "%s", text.c_str());
            last_log_ = now;
        }
    }

    armor_detect::Detector detector_;
    armor_detect::OnnxClassifier classifier_;
    bool debug_ = true;
    bool onnx_enabled_ = false;
    float not_armor_threshold_ = 0.7f;
    int log_interval_ms_ = 1000;
    int roi_save_count_ = 0;
    rclcpp::Time last_log_;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr debug_pub_;
    rclcpp::Publisher<armor_interfaces::msg::Armors>::SharedPtr result_pub_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ArmorDetectorNode>());
    rclcpp::shutdown();
    return 0;
}
