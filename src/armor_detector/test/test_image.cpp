#include <armor_detector/armor_detect_core.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <图片路径> [输出目录]" << std::endl;
        return 1;
    }

    std::string img_path = argv[1];
    std::string out_dir = (argc >= 3) ? argv[2] : "src/armor_detector/docs";

    cv::Mat img = cv::imread(img_path);
    if (img.empty()) {
        std::cerr << "无法读取图片: " << img_path << std::endl;
        return 1;
    }
    std::cout << "读取图片: " << img_path << " size=" << img.cols << "x" << img.rows << std::endl;

    armor_detect::Params p;
    p.debug_mode = "result";
    p.target_color = "red";
    p.target_width = 1280;
    p.gamma = 0.5;               // 这张图不算极暗，gamma 0.5 适中
    p.s_min = 30;
    p.v_min = 20;
    p.min_contour_area = 5.0;
    p.max_contour_area = 50000.0;
    p.min_aspect_ratio = 1.2;    // 灯条可能有倾斜，放宽一点
    p.pair_validation = true;
    p.strict_lightbar_filter = false;

    armor_detect::Detector det;
    auto result = det.detect(img, p);

    std::cout << "检测到装甲板: " << result.armors.size() << std::endl;
    std::cout << "红灯条: " << result.red.bars.size() << ", 蓝灯条: " << result.blue.bars.size() << std::endl;

    std::filesystem::create_directories(out_dir);

    // 保存 debug 图
    std::string debug_path = out_dir + "/test_debug.png";
    cv::imwrite(debug_path, result.debug_image);
    std::cout << "保存 debug: " << debug_path << std::endl;

    // 保存每个装甲板的 ROI
    for (size_t i = 0; i < result.armors.size(); ++i) {
        const auto& armor = result.armors[i];
        std::vector<cv::Point> pts;
        pts.reserve(4);
        for (const auto& pt : armor.points) pts.emplace_back(cvRound(pt.x), cvRound(pt.y));
        cv::Rect box = cv::boundingRect(pts);

        // 加 10% margin
        int mw = cvRound(box.width * 0.1);
        int mh = cvRound(box.height * 0.1);
        box.x = std::max(0, box.x - mw);
        box.y = std::max(0, box.y - mh);
        box.width = std::min(img.cols - box.x, box.width + 2 * mw);
        box.height = std::min(img.rows - box.y, box.height + 2 * mh);

        if (box.width <= 0 || box.height <= 0) continue;
        cv::Mat roi = img(box).clone();
        std::string roi_path = out_dir + "/test_roi_" + std::to_string(i) + "_" + armor.color + ".png";
        cv::imwrite(roi_path, roi);
        std::cout << "保存 ROI: " << roi_path << " size=" << roi.cols << "x" << roi.rows << std::endl;
    }

    return 0;
}
