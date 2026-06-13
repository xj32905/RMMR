#pragma once

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <array>
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

    bool pair_validation = false;         // W5: true=启用几何配对验证
    bool strict_lightbar_filter = false;  // W6: true=启用灯条角度+填充率校验（防背景误检）
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

// 四点顺序：左上、右上、右下、左下
struct Armor {
    std::string color;
    std::array<cv::Point2f, 4> points;
};

struct Result {
    cv::Mat debug_image;
    ColorResult red;
    ColorResult blue;
    std::vector<Armor> armors;
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
            auto red_armors = matchArmors(result.red.bars, true, p);
            result.red.armors = static_cast<int>(red_armors.size());
            result.armors.insert(result.armors.end(), red_armors.begin(), red_armors.end());
            drawColor(canvas, result.red, red_armors, true, draw_armor, draw_rejected, p);
        }
        if (p.target_color == "blue" || p.target_color == "all") {
            result.blue = detectColor(blue_mask, p);
            auto blue_armors = matchArmors(result.blue.bars, false, p);
            result.blue.armors = static_cast<int>(blue_armors.size());
            result.armors.insert(result.armors.end(), blue_armors.begin(), blue_armors.end());
            drawColor(canvas, result.blue, blue_armors, false, draw_armor, draw_rejected, p);
        }

        result.debug_image = canvas;
        return result;
    }

    ColorResult detectColor(const cv::Mat& mask, const Params& p) const {
        ColorResult out;
        out.bars = findBars(mask, p, out.contours, out.rejected);
        return out;
    }

    std::string summary(const Result& r, const Params& p) const {
        std::ostringstream oss;
        oss << "mode=" << p.debug_mode
            << ", color=" << p.target_color
            << ", sort=" << p.sort_by
            << ", red=" << r.red.contours << "/" << r.red.bars.size() << "/" << r.red.armors
            << ", blue=" << r.blue.contours << "/" << r.blue.bars.size() << "/" << r.blue.armors
            << ", total=" << r.armors.size();
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

            // W6: 严格灯条筛选 —— 角度约束 + 填充率校验（默认关闭）
            if (p.strict_lightbar_filter) {
                cv::RotatedRect rect = cv::minAreaRect(contour);
                // 角度约束：装甲板灯条竖直安装，长边应接近垂直（±25°）
                float ww = rect.size.width, hh = rect.size.height;
                float long_angle = (ww >= hh) ? rect.angle : rect.angle + 90.0f;
                if (long_angle < 0.0f) long_angle += 180.0f;
                if (long_angle > 90.0f) long_angle = 180.0f - long_angle;
                float angle_from_vertical = std::abs(long_angle);
                if (angle_from_vertical > 25.0f) {
                    rejected.push_back({box, area, length, width, aspect, "ANGL"});
                    continue;
                }
                // 填充率：真灯条 contourArea/boundingRectArea 应较高（>0.3）
                double contour_area = cv::contourArea(contour);
                double fill = contour_area / std::max(1.0, area);
                if (fill < 0.3) {
                    rejected.push_back({box, area, length, width, aspect, "FILL"});
                    continue;
                }
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
        return bars;
    }

    // 全排列配对：所有满足几何约束的灯条组合都生成装甲板
    std::vector<Armor> matchArmors(const std::vector<Bar>& bars, bool is_red, const Params& p) const {
        std::vector<Armor> armors;
        for (size_t i = 0; i < bars.size(); ++i) {
            for (size_t j = i + 1; j < bars.size(); ++j) {
                if (p.pair_validation && !validatePair(bars[i], bars[j])) continue;
                auto quad = armorQuad(bars[i].rect, bars[j].rect);
                armors.push_back({is_red ? "red" : "blue", quad});
            }
        }
        return armors;
    }

    void drawColor(cv::Mat& canvas, ColorResult& result, const std::vector<Armor>& armors,
                   bool is_red, bool draw_armor, bool draw_rejected, const Params& p) const {
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

        for (size_t i = 0; i < armors.size(); ++i) {
            if (draw_armor) {
                drawArmor(canvas, armors[i].points, armor_color,
                          is_red ? cv::format("Red#%zu", i + 1) : cv::format("Blue#%zu", i + 1));
            }
        }
    }

    // 几何配对验证：检查两根候选灯条是否满足真实装甲板的物理约束
    bool validatePair(const Bar& a, const Bar& b) const {
        // 1) 长度相近：两根灯条长度差 < 较长者的 0.6 倍
        const double len_a = a.length, len_b = b.length;
        const double len_max = std::max(len_a, len_b);
        const double len_min = std::min(len_a, len_b);
        if (len_max - len_min > len_max * 0.6) return false;

        // 2) 角度相近：灯条倾斜角差 < 30°
        const double ang_a = a.rect.size.width >= a.rect.size.height ? a.rect.angle : a.rect.angle + 90.0;
        const double ang_b = b.rect.size.width >= b.rect.size.height ? b.rect.angle : b.rect.angle + 90.0;
        double ang_diff = std::abs(ang_a - ang_b);
        if (ang_diff > 90.0) ang_diff = 180.0 - ang_diff;
        if (ang_diff > 30.0) return false;

        // 3) 间距合理：中心距在 [0.5, 5.0] 倍平均长度之间
        const double cx = std::abs(a.rect.center.x - b.rect.center.x);
        const double cy = std::abs(a.rect.center.y - b.rect.center.y);
        const double dist = std::sqrt(cx * cx + cy * cy);
        const double avg_len = (len_a + len_b) * 0.5;
        if (dist < avg_len * 0.5 || dist > avg_len * 5.0) return false;

        // 4) y 坐标接近：灯条垂直偏差 < 平均长度的 1.0 倍
        if (cy > avg_len * 1.0) return false;

        return true;
    }

    std::array<cv::Point2f, 4> armorQuad(const cv::RotatedRect& a, const cv::RotatedRect& b) const {
        cv::RotatedRect left = a;
        cv::RotatedRect right = b;
        if (left.center.x > right.center.x) std::swap(left, right);

        auto le = ends(left);
        auto re = ends(right);
        return {le.first, re.first, re.second, le.second};
    }

    void drawArmor(cv::Mat& img, const std::array<cv::Point2f, 4>& quad,
                   const cv::Scalar& color, const std::string& label) const {
        // Point2f 转 Point 画线
        std::vector<cv::Point> pts;
        pts.reserve(4);
        for (const auto& p : quad) pts.emplace_back(cvRound(p.x), cvRound(p.y));
        cv::polylines(img, pts, true, color, 2);
        cv::putText(img, label, pts[0] + cv::Point(0, -8), cv::FONT_HERSHEY_SIMPLEX, 0.55, color, 2);
    }

    std::pair<cv::Point2f, cv::Point2f> ends(const cv::RotatedRect& rect) const {
        cv::Point2f pts[4];
        rect.points(pts);
        std::sort(pts, pts + 4, [](const cv::Point2f& a, const cv::Point2f& b) { return a.y < b.y; });
        cv::Point2f top = (pts[0] + pts[1]) * 0.5f;
        cv::Point2f bottom = (pts[2] + pts[3]) * 0.5f;
        return {top, bottom};
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
