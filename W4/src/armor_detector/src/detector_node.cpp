#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

class ArmorDetectorNode : public rclcpp::Node {
public:
    ArmorDetectorNode() : Node("detector_node") {
        declare_parameter("debug", true);
        declare_parameter("debug_mode", "result"); // result / red_mask / blue_mask / candidates
        declare_parameter("target_color", "red"); // red / blue / all
        declare_parameter("pick_mode", "lights"); // lights / top2
        declare_parameter("pick_rank", "score"); // area / length / score
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
        declare_parameter("min_aspect_ratio", 1.0);
        declare_parameter("max_angle_error", 90.0);

        declare_parameter("pair_length_diff", 50.0);
        declare_parameter("pair_angle_diff", 35.0);
        declare_parameter("pair_min_ratio", 0.6);
        declare_parameter("pair_max_ratio", 5.0);
        declare_parameter("pair_max_y_diff_ratio", 0.8);

        sub_ = create_subscription<sensor_msgs::msg::Image>(
            "/camera/image", rclcpp::SensorDataQoS(),
            std::bind(&ArmorDetectorNode::onImage, this, std::placeholders::_1));
        debug_pub_ = create_publisher<sensor_msgs::msg::Image>("/armor/debug_image", 10);
        result_pub_ = create_publisher<std_msgs::msg::String>("/armor_result", 10);
        RCLCPP_INFO(get_logger(), "color mask -> lights -> armor quad detector started");
    }

private:
    struct P {
        bool debug;
        std::string mode, color, pick, rank;
        int target_width, log_ms;
        double gamma;
        int s_min, v_min, rh1, rh2, rh3, rh4, bh1, bh2, close_k, open_k;
        double area_min, area_max, aspect_min, angle_max;
        double len_diff, angle_diff, ratio_min, ratio_max, y_diff_ratio;
    };

    struct Light {
        cv::RotatedRect rr;
        cv::Rect box;
        float len;
        float angle;
        float area;
    };

    struct Stats {
        int red_contours = 0, blue_contours = 0;
        int red_lights = 0, blue_lights = 0;
        int red_armors = 0, blue_armors = 0;
    };

    P params() {
        P p;
        p.debug = get_parameter("debug").as_bool();
        p.mode = get_parameter("debug_mode").as_string();
        p.color = get_parameter("target_color").as_string();
        p.pick = get_parameter("pick_mode").as_string();
        p.rank = get_parameter("pick_rank").as_string();
        p.target_width = std::clamp((int)get_parameter("target_width").as_int(), 160, 3840);
        p.gamma = std::max(0.05, get_parameter("gamma").as_double());
        p.log_ms = std::max(0, (int)get_parameter("log_interval_ms").as_int());

        p.s_min = std::clamp((int)get_parameter("s_min").as_int(), 0, 255);
        p.v_min = std::clamp((int)get_parameter("v_min").as_int(), 0, 255);
        p.rh1 = std::clamp((int)get_parameter("r_thresh_low_h").as_int(), 0, 180);
        p.rh2 = std::clamp((int)get_parameter("r_thresh_high_h").as_int(), 0, 180);
        p.rh3 = std::clamp((int)get_parameter("r_thresh_low_h2").as_int(), 0, 180);
        p.rh4 = std::clamp((int)get_parameter("r_thresh_high_h2").as_int(), 0, 180);
        p.bh1 = std::clamp((int)get_parameter("b_thresh_low_h").as_int(), 0, 180);
        p.bh2 = std::clamp((int)get_parameter("b_thresh_high_h").as_int(), 0, 180);
        p.close_k = std::max(0, (int)get_parameter("morph_close_size").as_int());
        p.open_k = std::max(0, (int)get_parameter("morph_open_size").as_int());

        p.area_min = std::max(0.0, get_parameter("min_contour_area").as_double());
        p.area_max = std::max(p.area_min, get_parameter("max_contour_area").as_double());
        p.aspect_min = std::max(1.0, get_parameter("min_aspect_ratio").as_double());
        p.angle_max = std::max(0.0, get_parameter("max_angle_error").as_double());

        p.len_diff = std::max(0.0, get_parameter("pair_length_diff").as_double());
        p.angle_diff = std::max(0.0, get_parameter("pair_angle_diff").as_double());
        p.ratio_min = std::max(0.0, get_parameter("pair_min_ratio").as_double());
        p.ratio_max = std::max(p.ratio_min, get_parameter("pair_max_ratio").as_double());
        p.y_diff_ratio = std::max(0.0, get_parameter("pair_max_y_diff_ratio").as_double());
        return p;
    }

    void onImage(const sensor_msgs::msg::Image::SharedPtr msg) {
        try {
            cv::Mat img = cv_bridge::toCvShare(msg, "bgr8")->image.clone();
            if (img.empty()) return;

            P p = params();
            Stats s;
            cv::Mat out = detect(img, p, s);

            std_msgs::msg::String result;
            result.data = summary(s, p);
            result_pub_->publish(result);
            log(result.data, p.log_ms);

            if (!p.debug) return;
            if (out.channels() == 1) cv::cvtColor(out, out, cv::COLOR_GRAY2BGR);
            hud(out, s, p);
            debug_pub_->publish(*cv_bridge::CvImage(msg->header, "bgr8", out).toImageMsg());
        } catch (const std::exception& e) {
            RCLCPP_ERROR(get_logger(), "detector error: %s", e.what());
        }
    }

    cv::Mat detect(cv::Mat img, const P& p, Stats& s) {
        if (img.cols > p.target_width) {
            double scale = (double)p.target_width / img.cols;
            cv::resize(img, img, cv::Size(p.target_width, (int)(img.rows * scale)));
        }
        gamma(img, p.gamma);

        cv::Mat hsv, red, blue;
        cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);
        redMask(hsv, red, p);
        blueMask(hsv, blue, p);
        morph(red, p);
        morph(blue, p);
        if (p.mode == "red_mask") return red;
        if (p.mode == "blue_mask") return blue;

        cv::Mat canvas = img.clone();
        // candidates 只看灯条候选；result 才画装甲板配对
        if (p.color == "red" || p.color == "all") detectColor(canvas, red, p, s, true, p.mode != "candidates");
        if (p.color == "blue" || p.color == "all") detectColor(canvas, blue, p, s, false, p.mode != "candidates");
        return canvas;
    }

    void detectColor(cv::Mat& canvas, const cv::Mat& mask, const P& p, Stats& s, bool red, bool draw_armors) {
        int contours = 0, picked = 0, armors = 0;
        cv::Scalar light_color = red ? cv::Scalar(0, 255, 255) : cv::Scalar(255, 255, 0);
        cv::Scalar armor_color = red ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 0, 0);
        std::string label = red ? "Red Armor" : "Blue Armor";

        if (p.pick == "top2") {
            armors = drawTop2(canvas, mask, p, light_color, armor_color, label, draw_armors, contours, picked);
        } else {
            auto lights = findLights(mask, p, contours);
            picked = (int)lights.size();
            for (const auto& l : lights) {
                cv::rectangle(canvas, l.box, light_color, 1);
                cv::putText(canvas, cv::format("A%.0f L%.0f E%.0f", l.area, l.len, verticalErr(l.angle)),
                            l.box.tl() + cv::Point(0, -3), cv::FONT_HERSHEY_SIMPLEX, 0.35, light_color, 1);
            }
            armors = draw_armors ? drawPairs(canvas, lights, p, armor_color, label) : 0;
        }

        if (red) {
            s.red_contours = contours;
            s.red_lights = picked;
            s.red_armors = armors;
        } else {
            s.blue_contours = contours;
            s.blue_lights = picked;
            s.blue_armors = armors;
        }
    }

    void redMask(const cv::Mat& hsv, cv::Mat& mask, const P& p) {
        cv::Mat a, b;
        cv::inRange(hsv, cv::Scalar(p.rh1, p.s_min, p.v_min), cv::Scalar(p.rh2, 255, 255), a);
        cv::inRange(hsv, cv::Scalar(p.rh3, p.s_min, p.v_min), cv::Scalar(p.rh4, 255, 255), b);
        cv::bitwise_or(a, b, mask);
    }

    void blueMask(const cv::Mat& hsv, cv::Mat& mask, const P& p) {
        cv::inRange(hsv, cv::Scalar(p.bh1, p.s_min, p.v_min), cv::Scalar(p.bh2, 255, 255), mask);
    }


    int drawTop2(cv::Mat& img, const cv::Mat& mask, const P& p,
                 const cv::Scalar& light_color, const cv::Scalar& armor_color,
                 const std::string& label, bool draw_armor, int& contours_count, int& picked) {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        contours_count = (int)contours.size();

        struct Blob {
            double area, len, aspect, score;
            cv::RotatedRect rr;
            cv::Rect box;
        };
        std::vector<Blob> blobs;
        for (const auto& c : contours) {
            double area = cv::contourArea(c);
            if (area < p.area_min || area > p.area_max) continue;
            cv::RotatedRect rr = cv::minAreaRect(c);
            double w = std::max(1.0f, rr.size.width);
            double h = std::max(1.0f, rr.size.height);
            double len = std::max(w, h);
            double aspect = len / std::max(1.0, std::min(w, h));
            if (aspect < p.aspect_min) continue;  // top2也只从“长条形”红色块里选，过滤小圆点噪声
            double score = p.rank == "area" ? area : (p.rank == "length" ? len : len * aspect);
            blobs.push_back({area, len, aspect, score, rr, cv::boundingRect(c)});
        }
        std::sort(blobs.begin(), blobs.end(), [](const Blob& a, const Blob& b) { return a.score > b.score; });
        picked = std::min(2, (int)blobs.size());

        for (int i = 0; i < picked; ++i) {
            cv::rectangle(img, blobs[i].box, light_color, 2);
            cv::putText(img, cv::format("TOP%d A%.0f L%.0f R%.1f S%.0f", i + 1, blobs[i].area, blobs[i].len, blobs[i].aspect, blobs[i].score),
                        blobs[i].box.tl() + cv::Point(0, -3), cv::FONT_HERSHEY_SIMPLEX, 0.42, light_color, 1);
        }
        if (!draw_armor || picked < 2) return 0;

        cv::RotatedRect l = blobs[0].rr, r = blobs[1].rr;
        if (l.center.x > r.center.x) std::swap(l, r);
        auto q = quad(l, r);
        std::vector<cv::Point> qi;
        for (auto& pt : q) qi.emplace_back(cvRound(pt.x), cvRound(pt.y));
        cv::polylines(img, qi, true, armor_color, 2);
        cv::putText(img, label + " top2", qi[0] + cv::Point(0, -8), cv::FONT_HERSHEY_SIMPLEX, 0.5, armor_color, 2);
        return 1;
    }

    std::vector<Light> findLights(
    const cv::Mat& mask,
    const P& p,
    int& contours_count)
{
    std::vector<std::vector<cv::Point>> contours;

    cv::findContours(
        mask.clone(),
        contours,
        cv::RETR_EXTERNAL,
        cv::CHAIN_APPROX_SIMPLE);

    contours_count = static_cast<int>(contours.size());

    std::vector<Light> lights;

    for(const auto& c : contours)
    {
        cv::RotatedRect rr = cv::minAreaRect(c);

        float w = rr.size.width;
        float h = rr.size.height;

        if(w < 1 || h < 1)
            continue;

        float long_side  = std::max(w, h);
        float short_side = std::min(w, h);

        if(short_side < 1)
            continue;

        float ratio = long_side / short_side;

        // ===== 灯条筛选 =====

        // 太小的点直接扔
        if(long_side < 8)
            continue;

        // 长宽比不足
        if(ratio < 2.0)
            continue;

        Light light;

        light.rr    = rr;
        light.box   = cv::boundingRect(c);
        light.len   = long_side;
        light.angle = longAngle(rr);
        light.area  = static_cast<float>(cv::contourArea(c));

        lights.push_back(light);
    }

    std::sort(
        lights.begin(),
        lights.end(),
        [](const Light& a, const Light& b)
        {
            return a.rr.center.x < b.rr.center.x;
        });

    return lights;
}
    int drawPairs(cv::Mat& img, const std::vector<Light>& lights, const P& p,
                  const cv::Scalar& color, const std::string& label) {
        int count = 0;
        for (size_t i = 0; i < lights.size(); ++i) {
            for (size_t j = i + 1; j < lights.size(); ++j) {
                const Light& l = lights[i];
                const Light& r = lights[j];
                float avg_len = (l.len + r.len) * 0.5f;
                if (avg_len <= 1e-3f) continue;
                if (std::abs(l.len - r.len) > p.len_diff) continue;

                float ad = std::abs(l.angle - r.angle);
                if (ad > 90) ad = 180 - ad;
                if (ad > p.angle_diff) continue;

                float dist = (float)cv::norm(r.rr.center - l.rr.center);
                float ratio = dist / avg_len;
                float ydiff = std::abs(r.rr.center.y - l.rr.center.y);
                if (ratio < p.ratio_min || ratio > p.ratio_max) continue;
                if (ydiff > avg_len * p.y_diff_ratio) continue;

                auto q = quad(l.rr, r.rr);
                std::vector<cv::Point> qi;
                for (auto& pt : q) qi.emplace_back(cvRound(pt.x), cvRound(pt.y));
                cv::polylines(img, qi, true, color, 2);
                cv::putText(img, label, qi[0] + cv::Point(0, -8), cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
                count++;
            }
        }
        return count;
    }

    std::vector<cv::Point2f> quad(const cv::RotatedRect& l, const cv::RotatedRect& r) {
        cv::Point2f lp[4], rp[4];
        l.points(lp);
        r.points(rp);
        auto a = lightEnds(lp);
        auto b = lightEnds(rp);
        return {a[0], b[0], b[1], a[1]};
    }

    std::vector<cv::Point2f> lightEnds(const cv::Point2f pts[4]) {
        std::vector<cv::Point2f> v(pts, pts + 4);
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.y < b.y; });
        return {(v[0] + v[1]) * 0.5f, (v[2] + v[3]) * 0.5f};
    }

    void morph(cv::Mat& mask, const P& p) {
        auto run = [&](int k, int op) {
            if (k <= 0) return;
            k = k % 2 ? k : k + 1;
            cv::morphologyEx(mask, mask, op, cv::getStructuringElement(cv::MORPH_RECT, {k, k}));
        };
        run(p.close_k, cv::MORPH_CLOSE);
        run(p.open_k, cv::MORPH_OPEN);
    }

    void gamma(cv::Mat& img, double g) {
        if (std::abs(g - 1.0) < 0.01) return;
        cv::Mat lut(1, 256, CV_8U);
        for (int i = 0; i < 256; ++i) lut.at<uchar>(i) = cv::saturate_cast<uchar>(std::pow(i / 255.0, g) * 255.0);
        cv::LUT(img, lut, img);
    }

    float longAngle(const cv::RotatedRect& r) {
        return r.size.width >= r.size.height ? r.angle : r.angle + 90.0f;
    }

    float verticalErr(float a) {
        while (a < 0) a += 180;
        while (a >= 180) a -= 180;
        if (a > 90) a = 180 - a;
        return std::abs(a - 90);
    }

    void hud(cv::Mat& img, const Stats& s, const P& p) {
        std::ostringstream oss;
        oss << "mode=" << p.mode << " color=" << p.color << " pick=" << p.pick << "/" << p.rank
            << " R=" << s.red_lights << "/" << s.red_armors
            << " B=" << s.blue_lights << "/" << s.blue_armors
            << " S=" << p.s_min << " V=" << p.v_min;
        cv::putText(img, oss.str(), {10, 24}, cv::FONT_HERSHEY_SIMPLEX, 0.55, {0, 255, 0}, 2);
    }

    std::string summary(const Stats& s, const P& p) {
        std::ostringstream oss;
        oss << "mode=" << p.mode << ", color=" << p.color << ", pick=" << p.pick << "/" << p.rank
            << ", red=" << s.red_contours << "/" << s.red_lights << "/" << s.red_armors
            << ", blue=" << s.blue_contours << "/" << s.blue_lights << "/" << s.blue_armors;
        return oss.str();
    }

    void log(const std::string& msg, int ms) {
        auto now = this->now();
        if (ms <= 0 || (last_log_.nanoseconds() != 0 && (now - last_log_).nanoseconds() / 1000000 < ms)) return;
        RCLCPP_INFO(get_logger(), "%s", msg.c_str());
        last_log_ = now;
    }

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr debug_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr result_pub_;
    rclcpp::Time last_log_{};
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ArmorDetectorNode>());
    rclcpp::shutdown();
    return 0;
}
