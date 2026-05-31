#pragma once

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace armor_detect {

struct Params {
    std::string debug_mode = "result";   // result / red_mask / blue_mask / candidates
    std::string target_color = "red";    // red / blue / all
    std::string sort_by = "score";       // score / length / area / aspect

    int target_width = 1280;
    double gamma = 1.0;

    int s_min = 70;
    int v_min = 70;
    int r_h_low1 = 0;
    int r_h_high1 = 10;
    int r_h_low2 = 170;
    int r_h_high2 = 180;
    int b_h_low = 90;
    int b_h_high = 130;
    int morph_close_size = 3;
    int morph_open_size = 0;

    // 注意：这里的面积使用 boundingRect 面积，不用 contourArea。
    // 细灯条/远距离灯条在 mask 里可能只有 1~2 像素宽，contourArea 容易接近 0。
    double min_contour_area = 5.0;
    double max_contour_area = 3000.0;
    double min_aspect_ratio = 1.5;
};

struct Bar {
    cv::RotatedRect rect;
    cv::Rect box;
    double area = 0.0;      // boundingRect 面积
    double length = 0.0;    // boundingRect 长边
    double width = 0.0;     // boundingRect 短边
    double aspect = 1.0;    // length / width
    double score = 0.0;
};

struct Rejected {
    cv::Rect box;
    double area = 0.0;
    double length = 0.0;
    double width = 0.0;
    double aspect = 1.0;
    std::string reason;
};

struct ColorResult {
    int contours = 0;
    std::vector<Bar> bars;
    std::vector<Rejected> rejected;
    int armors = 0;
};

struct Result {
    cv::Mat debug_image;
    ColorResult red;
    ColorResult blue;
};

class Detector {
public:
    Result detect(const cv::Mat& bgr, const Params& p) const {
        Result result;
        if (bgr.empty()) return result;

        cv::Mat img = prepareImage(bgr, p);
        cv::Mat hsv, red_mask, blue_mask;
        cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);
        redMask(hsv, red_mask, p);
        blueMask(hsv, blue_mask, p);
        applyMorph(red_mask, p);
        applyMorph(blue_mask, p);

        if (p.debug_mode == "red_mask") {
            result.debug_image = red_mask;
            return result;
        }
        if (p.debug_mode == "blue_mask") {
            result.debug_image = blue_mask;
            return result;
        }

        cv::Mat canvas = img.clone();
        const bool draw_armor = p.debug_mode != "candidates";
        const bool draw_rejected = p.debug_mode == "candidates";

        if (p.target_color == "red" || p.target_color == "all") {
            result.red = detectColor(red_mask, p);
            drawColor(canvas, result.red, true, draw_armor, draw_rejected);
        }
        if (p.target_color == "blue" || p.target_color == "all") {
            result.blue = detectColor(blue_mask, p);
            drawColor(canvas, result.blue, false, draw_armor, draw_rejected);
        }

        result.debug_image = canvas;
        return result;
    }

    ColorResult detectColor(const cv::Mat& mask, const Params& p) const {
        ColorResult out;
        out.bars = findBars(mask, p, out.contours, out.rejected);
        out.armors = out.bars.size() >= 2 ? 1 : 0;
        return out;
    }

    std::string summary(const Result& r, const Params& p) const {
        std::ostringstream oss;
        oss << "mode=" << p.debug_mode
            << ", color=" << p.target_color
            << ", sort=" << p.sort_by
            << ", red=" << r.red.contours << "/" << r.red.bars.size() << "/" << r.red.armors
            << ", blue=" << r.blue.contours << "/" << r.blue.bars.size() << "/" << r.blue.armors;
        return oss.str();
    }

private:
    cv::Mat prepareImage(const cv::Mat& bgr, const Params& p) const {
        cv::Mat img = bgr.clone();
        if (img.cols > p.target_width) {
            const double scale = static_cast<double>(p.target_width) / img.cols;
            cv::resize(img, img, cv::Size(p.target_width, static_cast<int>(img.rows * scale)));
        }
        applyGamma(img, p.gamma);
        return img;
    }

    void redMask(const cv::Mat& hsv, cv::Mat& mask, const Params& p) const {
        cv::Mat a, b;
        cv::inRange(hsv, cv::Scalar(p.r_h_low1, p.s_min, p.v_min), cv::Scalar(p.r_h_high1, 255, 255), a);
        cv::inRange(hsv, cv::Scalar(p.r_h_low2, p.s_min, p.v_min), cv::Scalar(p.r_h_high2, 255, 255), b);
        cv::bitwise_or(a, b, mask);
    }

    void blueMask(const cv::Mat& hsv, cv::Mat& mask, const Params& p) const {
        cv::inRange(hsv, cv::Scalar(p.b_h_low, p.s_min, p.v_min), cv::Scalar(p.b_h_high, 255, 255), mask);
    }

    void applyMorph(cv::Mat& mask, const Params& p) const {
        if (p.morph_close_size > 1) {
            cv::Mat k = cv::getStructuringElement(cv::MORPH_RECT, {p.morph_close_size, p.morph_close_size});
            cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, k);
        }
        if (p.morph_open_size > 1) {
            cv::Mat k = cv::getStructuringElement(cv::MORPH_RECT, {p.morph_open_size, p.morph_open_size});
            cv::morphologyEx(mask, mask, cv::MORPH_OPEN, k);
        }
    }

    std::vector<Bar> findBars(const cv::Mat& mask, const Params& p,
                              int& contour_count, std::vector<Rejected>& rejected) const {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        contour_count = static_cast<int>(contours.size());

        std::vector<Bar> bars;
        for (const auto& contour : contours) {
            const cv::Rect box = cv::boundingRect(contour);
            const double area = static_cast<double>(box.area());
            const double length = static_cast<double>(std::max(box.width, box.height));
            const double width = static_cast<double>(std::max(1, std::min(box.width, box.height)));
            const double aspect = length / width;

            if (area < p.min_contour_area) {
                rejected.push_back({box, area, length, width, aspect, "SMALL"});
                continue;
            }
            if (area > p.max_contour_area) {
                rejected.push_back({box, area, length, width, aspect, "BIG"});
                continue;
            }
            if (aspect < p.min_aspect_ratio) {
                rejected.push_back({box, area, length, width, aspect, "R<"});
                continue;
            }

            double score = length * aspect;
            if (p.sort_by == "area") score = area;
            else if (p.sort_by == "length") score = length;
            else if (p.sort_by == "aspect") score = aspect;

            cv::RotatedRect rect = cv::minAreaRect(contour);
            if (rect.size.width < 1.0f || rect.size.height < 1.0f) {
                rect = cv::RotatedRect(cv::Point2f(box.x + box.width * 0.5f, box.y + box.height * 0.5f),
                                       cv::Size2f(static_cast<float>(box.width), static_cast<float>(box.height)), 0.0f);
            }
            bars.push_back({rect, box, area, length, width, aspect, score});
        }

        std::sort(bars.begin(), bars.end(), [](const Bar& a, const Bar& b) { return a.score > b.score; });
        if (bars.size() > 2) bars.resize(2);
        if (bars.size() == 2 && bars[0].rect.center.x > bars[1].rect.center.x) std::swap(bars[0], bars[1]);
        return bars;
    }

    void drawColor(cv::Mat& canvas, ColorResult& result, bool is_red, bool draw_armor, bool draw_rejected) const {
        const cv::Scalar pass_color = is_red ? cv::Scalar(0, 255, 255) : cv::Scalar(255, 255, 0);
        const cv::Scalar fail_color = cv::Scalar(80, 80, 80);
        const cv::Scalar armor_color = is_red ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 0, 0);

        if (draw_rejected) {
            for (const auto& r : result.rejected) {
                cv::rectangle(canvas, r.box, fail_color, 1);
                cv::putText(canvas, cv::format("%s A%.0f R%.1f", r.reason.c_str(), r.area, r.aspect),
                            r.box.tl() + cv::Point(0, -3), cv::FONT_HERSHEY_SIMPLEX, 0.36, fail_color, 1);
            }
        }

        for (size_t i = 0; i < result.bars.size(); ++i) {
            const auto& b = result.bars[i];
            cv::rectangle(canvas, b.box, pass_color, 2);
            cv::putText(canvas, cv::format("TOP%zu A%.0f L%.0f R%.1f S%.0f", i + 1, b.area, b.length, b.aspect, b.score),
                        b.box.tl() + cv::Point(0, -4), cv::FONT_HERSHEY_SIMPLEX, 0.42, pass_color, 1);
        }

        if (draw_armor && result.bars.size() >= 2) {
            drawArmor(canvas, result.bars[0].rect, result.bars[1].rect, armor_color, is_red ? "Red Armor" : "Blue Armor");
            result.armors = 1;
        } else {
            result.armors = 0;
        }
    }

    void drawArmor(cv::Mat& img, const cv::RotatedRect& a, const cv::RotatedRect& b,
                   const cv::Scalar& color, const std::string& label) const {
        cv::RotatedRect left = a;
        cv::RotatedRect right = b;
        if (left.center.x > right.center.x) std::swap(left, right);

        auto le = ends(left);
        auto re = ends(right);
        std::vector<cv::Point> quad = {
            toPoint(le.first), toPoint(re.first), toPoint(re.second), toPoint(le.second)
        };
        cv::polylines(img, quad, true, color, 2);
        cv::putText(img, label, quad[0] + cv::Point(0, -8), cv::FONT_HERSHEY_SIMPLEX, 0.55, color, 2);
    }

    std::pair<cv::Point2f, cv::Point2f> ends(const cv::RotatedRect& rect) const {
        cv::Point2f pts[4];
        rect.points(pts);
        std::sort(pts, pts + 4, [](const cv::Point2f& a, const cv::Point2f& b) { return a.y < b.y; });
        cv::Point2f top = (pts[0] + pts[1]) * 0.5f;
        cv::Point2f bottom = (pts[2] + pts[3]) * 0.5f;
        return {top, bottom};
    }

    cv::Point toPoint(const cv::Point2f& p) const {
        return {cvRound(p.x), cvRound(p.y)};
    }

    void applyGamma(cv::Mat& img, double gamma) const {
        if (std::abs(gamma - 1.0) < 1e-3) return;
        cv::Mat lut(1, 256, CV_8U);
        for (int i = 0; i < 256; ++i) {
            lut.at<uchar>(i) = cv::saturate_cast<uchar>(std::pow(i / 255.0, gamma) * 255.0);
        }
        cv::LUT(img, lut, img);
    }
};

}  // namespace armor_detect
