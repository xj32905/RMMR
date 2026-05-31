#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>

#include <armor_detector/armor_detect_core.hpp>

#include <algorithm>
#include <string>

class ArmorDetectorNode : public rclcpp::Node {
public:
    ArmorDetectorNode() : Node("detector_node") {
        declareParams();

        sub_ = create_subscription<sensor_msgs::msg::Image>(
            "/camera/image", rclcpp::SensorDataQoS(),
            std::bind(&ArmorDetectorNode::onImage, this, std::placeholders::_1));
        debug_pub_ = create_publisher<sensor_msgs::msg::Image>("/armor/debug_image", 10);
        result_pub_ = create_publisher<std_msgs::msg::String>("/armor_result", 10);

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
        return p;
    }

    void onImage(const sensor_msgs::msg::Image::SharedPtr msg) {
        try {
            cv::Mat img = cv_bridge::toCvShare(msg, "bgr8")->image;
            if (img.empty()) return;

            auto p = readParams();
            auto result = detector_.detect(img, p);
            auto text = detector_.summary(result, p);

            std_msgs::msg::String out;
            out.data = text;
            result_pub_->publish(out);
            log(text);

            if (!debug_ || result.debug_image.empty()) return;
            cv::Mat debug_img = result.debug_image;
            if (debug_img.channels() == 1) cv::cvtColor(debug_img, debug_img, cv::COLOR_GRAY2BGR);
            drawHud(debug_img, text);
            debug_pub_->publish(*cv_bridge::CvImage(msg->header, "bgr8", debug_img).toImageMsg());
        } catch (const std::exception& e) {
            RCLCPP_ERROR(get_logger(), "detector_node error: %s", e.what());
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
    bool debug_ = true;
    int log_interval_ms_ = 1000;
    rclcpp::Time last_log_;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr debug_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr result_pub_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ArmorDetectorNode>());
    rclcpp::shutdown();
    return 0;
}
