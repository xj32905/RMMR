#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <std_msgs/msg/string.hpp>
#include <chrono>
#include <memory>
#include <cmath>
#include <vector>
#include <utility>


using namespace std;

struct Node {
    string op_name;
    unique_ptr<cv::Mat> img;
    chrono::steady_clock::duration dt;
    string demonstration;

    Node() {}
    Node(string name, cv::Mat im, chrono::steady_clock::duration dtime, string demo = "") {
        op_name = name;
        img = make_unique<cv::Mat>(im);
        dt = dtime;
        demonstration = demo;
    }
};

struct InlineTimer {
    string name;
    chrono::steady_clock::time_point start;
    chrono::steady_clock::time_point end;

    void stopTimer_pushImg(cv::Mat& img, string demonstration = "") {
        end = chrono::steady_clock::now();
        extern vector<unique_ptr<Node>> mmry;
        mmry.push_back(make_unique<Node>(name, img.clone(), end - start, demonstration));
    }

    InlineTimer(string name) {
        this->name = name;
        start = chrono::steady_clock::now();
    }
};

vector<unique_ptr<Node>> mmry;

void gammaCorrect(cv::Mat& src, double gamma);
void gsBlur(cv::Mat& src);
void mask(cv::Mat& src);
void threshold(cv::Mat& src, int thresh, int type);
cv::Rect expandRoi(const cv::Rect& rect, const cv::Size& image_size, double scale);
void alignRoi(cv::Rect& rect);
cv::Mat split_red(cv::Mat& src);
cv::Mat split_blue(cv::Mat& src);
cv::Mat split_overexpose(cv::Mat& src_hsv);
pair<vector<vector<cv::Point>>, vector<vector<cv::Point>>> split_red_blue(cv::Mat src_hsv, vector<vector<cv::Point>> contours);
void erode(cv::Mat& src);
vector<vector<cv::Point>> contour(cv::Mat& src, cv::Mat backg, bool is_hsv);
bool filter_contour(vector<cv::Point> contour);
vector<pair<cv::Rect, cv::Rect>> geo_armorDetect(vector<vector<cv::Point>>& candidates);
float getLongSideAngle(const cv::RotatedRect& rect);
void draw_box(cv::Mat& src, pair<cv::Rect, cv::Rect>& armor, cv::Scalar color, string label);
void detect_img(cv::Mat& img);

class ArmorDetectorNode : public rclcpp::Node {
public:
    ArmorDetectorNode() : Node("armor_detector_node") {
        this->declare_parameter("debug", true);
        this->declare_parameter("gamma", 2.0);
        this->declare_parameter("target_width", 1280);
        sub_ = this->create_subscription<sensor_msgs::msg::Image>(
            "/camera/image", 10,
            std::bind(&ArmorDetectorNode::onImage, this, std::placeholders::_1));

        debug_pub_ = this->create_publisher<sensor_msgs::msg::Image>("/armor/debug_image", 10);
        result_pub_ = this->create_publisher<std_msgs::msg::String>("/armor_result", 10);

        RCLCPP_INFO(this->get_logger(), "W4 识别节点启动,等待 /camera/image ...");
    }

private:
    void onImage(const sensor_msgs::msg::Image::SharedPtr msg) {
        try {
            cv::Mat img = cv_bridge::toCvShare(msg, "bgr8")->image;
            if (img.empty()) {
                RCLCPP_WARN(this->get_logger(), "收到空图像");
                return;
            }

            mmry.clear();
            detect_img(img);
            // 在 detect_img(img); 后面加：
             std_msgs::msg::String result_msg;
// 简单描述识别状态，后续到基地再细化
             result_msg.data = "Frame processed"; 
             result_pub_->publish(result_msg);

            if (this->get_parameter("debug").as_bool()) {
                auto out_msg = cv_bridge::CvImage(msg->header, "bgr8", img).toImageMsg();
                debug_pub_->publish(*out_msg);
            }

            RCLCPP_INFO(this->get_logger(), "处理完成");
        } catch (const cv_bridge::Exception& e) {
            RCLCPP_ERROR(this->get_logger(), "cv_bridge 失败: %s", e.what());
        }
    }

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr debug_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr result_pub_;
};

void detect_img(cv::Mat& img) {
    int target_width = 1280;
    if (img.cols > target_width) {
        double scale = static_cast<double>(target_width) / img.cols;
        int target_height = static_cast<int>(img.rows * scale);
        cv::resize(img, img, cv::Size(target_width, target_height), 0, 0, cv::INTER_LINEAR);
    }

    gammaCorrect(img, 2.0);
    
    cv::Mat hsv;
    cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);
    
    cv::Mat hsv_v = split_overexpose(hsv);
    
    auto contours = contour(hsv_v, hsv, true);
    if (contours.empty()) {
        return;
    }
    
    pair<vector<vector<cv::Point>>, vector<vector<cv::Point>>> rect = split_red_blue(hsv, contours);
    
    vector<pair<cv::Rect, cv::Rect>> armors_red = geo_armorDetect(rect.first);
    for (auto& armor : armors_red) {
        draw_box(img, armor, cv::Scalar(0, 0, 255), "Red Armor"); 
    }
    
    vector<pair<cv::Rect, cv::Rect>> armors_blue = geo_armorDetect(rect.second);
    for (auto& armor : armors_blue) {
        draw_box(img, armor, cv::Scalar(255, 0, 0), "Blue Armor"); 
    }
}

void gammaCorrect(cv::Mat& src, double gamma) {
    InlineTimer t("gammaCorrect");
    cv::Mat lookupTable(1, 256, CV_8U);
    uchar* p = lookupTable.ptr();
    for (int i = 0; i < 256; ++i) {
        p[i] = cv::saturate_cast<uchar>(pow(i / 255.0, gamma) * 255.0);
    }
    cv::Mat tmp;
    cv::LUT(src, lookupTable, tmp);
    src = tmp;
    t.stopTimer_pushImg(tmp);
}

void gsBlur(cv::Mat& src) {
    InlineTimer t("gaussianBlur");
    cv::Mat tmp;
    cv::GaussianBlur(src, tmp, cv::Size(5, 5), 0);
    src = tmp;
    t.stopTimer_pushImg(tmp);
}

void mask(cv::Mat& src) {
    InlineTimer t("mask");
    cv::Mat kernel = (cv::Mat_<float>(3, 3) << 
         0, -1,  0,
        -1,  5, -1,
         0, -1,  0);
    cv::Mat tmp;
    cv::filter2D(src, tmp, -1, kernel);
    src = tmp;
    t.stopTimer_pushImg(tmp);
}

void threshold(cv::Mat& src, int thresh, int type) {
    InlineTimer t("threshold");
    cv::Mat tmp;
    cv::threshold(src, tmp, thresh, 255, type);
    src = tmp;
    t.stopTimer_pushImg(tmp);
}

cv::Rect expandRoi(const cv::Rect& rect, const cv::Size& image_size, double scale) {
    int new_width = static_cast<int>(rect.width * scale);
    int new_height = static_cast<int>(rect.height * scale);
    int new_x = rect.x + (rect.width - new_width) / 2;
    int new_y = rect.y + (rect.height - new_height) / 2;
    
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x + new_width > image_size.width) new_width = image_size.width - new_x;
    if (new_y + new_height > image_size.height) new_height = image_size.height - new_y;
    
    return cv::Rect(new_x, new_y, new_width, new_height);
}

void alignRoi(cv::Rect& rect) {
    float aspect_ratio = static_cast<float>(rect.width) / rect.height;
    float target_aspect_ratio = 1.8f;
    
    if (aspect_ratio > target_aspect_ratio) {
        int new_height = static_cast<int>(rect.width / target_aspect_ratio);
        rect.y += (rect.height - new_height) / 2;
        rect.height = new_height;
    } else {
        int new_width = static_cast<int>(rect.height * target_aspect_ratio);
        rect.x += (rect.width - new_width) / 2;
        rect.width = new_width;
    }
}

cv::Mat split_red(cv::Mat& src) {
    cv::Mat mask1, mask2, red_mask;
    cv::inRange(src, cv::Scalar(0, 50, 50), cv::Scalar(10, 255, 255), mask1);
    cv::inRange(src, cv::Scalar(170, 50, 50), cv::Scalar(180, 255, 255), mask2);
    cv::bitwise_or(mask1, mask2, red_mask);
    return red_mask;
}

cv::Mat split_blue(cv::Mat& src) {
    cv::Mat blue_mask;
    cv::inRange(src, cv::Scalar(100, 100, 100), cv::Scalar(130, 255, 255), blue_mask);
    return blue_mask;
}

cv::Mat split_overexpose(cv::Mat& src_hsv) {
    InlineTimer t("split_overexpose");
    cv::Mat channel_v;
    cv::extractChannel(src_hsv, channel_v, 2);
    cv::Mat v_mask;
    cv::threshold(channel_v, v_mask, 220, 255, cv::THRESH_BINARY);
    cv::Mat kernel_close = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(v_mask, v_mask, cv::MORPH_CLOSE, kernel_close);
    cv::Mat kernel_open = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::morphologyEx(v_mask, v_mask, cv::MORPH_OPEN, kernel_open);
    t.stopTimer_pushImg(v_mask);
    return v_mask;
}

pair<vector<vector<cv::Point>>, vector<vector<cv::Point>>> split_red_blue(cv::Mat src_hsv, vector<vector<cv::Point>> contours) {
    InlineTimer t("split_red_blue");
    vector<vector<cv::Point>> red_rect, blue_rect;
    cv::Mat canvas = src_hsv.clone();

    for (const auto& i : contours) {
        if (!filter_contour(i)) continue;
        auto rect = cv::boundingRect(i);
        if (rect.area() <= 0) continue;

        string dem;
        rect = expandRoi(rect, src_hsv.size(), 1.5);
        cv::Mat roi = src_hsv(rect);
        
        int red_count = cv::countNonZero(split_red(roi));
        int blue_count = cv::countNonZero(split_blue(roi));
        
        if (red_count > blue_count) {
            red_rect.push_back(i); 
            dem = "Detected Red LightBar";
        } else if (blue_count > red_count) {
            blue_rect.push_back(i); 
            dem = "Detected Blue LightBar";
        }
        
        cv::rectangle(canvas, rect, cv::Scalar(0, 255, 0), 2);
        t.stopTimer_pushImg(canvas, dem);
    }
    return {red_rect, blue_rect};
}

void erode(cv::Mat& src) {
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(3,3));
    cv::morphologyEx(src, src, cv::MORPH_ERODE, kernel);
}

vector<vector<cv::Point>> contour(cv::Mat& src, cv::Mat backg, bool is_hsv) {
    InlineTimer t("contour");
    cv::Mat gray = src.clone();
    if (!is_hsv) {
        if (src.channels() == 3) {
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        }
        threshold(gray, 160, cv::THRESH_BINARY);
    }
    vector<vector<cv::Point>> contours;
    vector<cv::Vec4i> hierarchy;
    cv::findContours(gray, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    cv::Mat draw_canvas = backg.clone();
    for (size_t i = 0; i < contours.size(); ++i) {
        cv::drawContours(draw_canvas, contours, static_cast<int>(i), cv::Scalar(0, 255, 255), 2);
    }
    
    t.stopTimer_pushImg(draw_canvas);
    return contours;
}

bool filter_contour(vector<cv::Point> contour) {
    double area = cv::contourArea(contour);
    if (area <= 10.0 || area >= 2000.0) return false;

    auto rect = cv::minAreaRect(contour);
    float w = rect.size.width;
    float h = rect.size.height;
    if (w == 0 || h == 0) return false;
    
    float ratio = max(w, h) / min(w, h);
    if (ratio <= 1.2) return false;

    float long_side_angle = getLongSideAngle(rect);
    if (long_side_angle < 0) long_side_angle += 180.0f;
    if (long_side_angle > 90.0f) long_side_angle = 180.0f - long_side_angle;
    
    if (std::abs(long_side_angle - 90.0f) > 25.0f) return false;

    return true;
}

vector<pair<cv::Rect, cv::Rect>> geo_armorDetect(vector<vector<cv::Point>>& candidates) {
    vector<pair<cv::Rect, cv::Rect>> result;
    int n = candidates.size();
    for (int i = 0; i < n; ++i) {
        auto rect1_rot = cv::minAreaRect(candidates[i]);
        auto rect1 = cv::boundingRect(candidates[i]);
        float len1 = max(rect1_rot.size.width, rect1_rot.size.height);
        float angle1 = getLongSideAngle(rect1_rot);

        for (int j = i + 1; j < n; ++j) {
            auto rect2_rot = cv::minAreaRect(candidates[j]);
            auto rect2 = cv::boundingRect(candidates[j]);
            float len2 = max(rect2_rot.size.width, rect2_rot.size.height);
            float angle2 = getLongSideAngle(rect2_rot);

            if (abs(len1 - len2) > 35) continue;

            float angle_diff = abs(angle1 - angle2);
            if (angle_diff > 90) angle_diff = 180 - angle_diff;
            if (angle_diff > 25) continue;

            float center_dist = static_cast<float>(cv::norm(rect1_rot.center - rect2_rot.center));
            float avg_len = (len1 + len2) / 2.0f;
            float ratio = center_dist / avg_len;
            if (ratio < 0.6f || ratio > 5.0f) continue;

            float y_diff = abs(rect1_rot.center.y - rect2_rot.center.y);
            if (y_diff > avg_len * 0.8f) continue;

            result.push_back({rect1, rect2});
        }
    }
    return result;
}

float getLongSideAngle(const cv::RotatedRect& rect) {
    float w = rect.size.width;
    float h = rect.size.height;
    float angle = rect.angle;
    if (w >= h) {
        return angle;
    } else {
        return angle + 90.0f;
    }
}

void draw_box(cv::Mat& src, pair<cv::Rect, cv::Rect>& armor, cv::Scalar color, string label) {
    cv::Rect box = armor.first | armor.second;
    alignRoi(box);
    cv::rectangle(src, box, color, 2);
    cv::putText(src, label, cv::Point(box.x, max(box.y - 10, 15)), cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
}

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ArmorDetectorNode>());
    rclcpp::shutdown();
    return 0;
}