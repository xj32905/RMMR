#include <armor_detector/armor_detect_core.hpp>
#include <armor_detector/onnx_classifier.hpp>
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

    // 针对清晰装甲板照片
    p.gamma = 1.0;
    p.s_min = 60;
    p.v_min = 80;
    p.min_contour_area = 20.0;
    p.max_contour_area = 50000.0;
    p.min_aspect_ratio = 2.0;
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

    // 加载 ONNX 模型
    armor_detect::OnnxClassifier classifier;
    const std::string model_path = "/home/xj/rm_test/assets/tiny_resnet.onnx";
    bool onnx_ok = classifier.loadModel(model_path);
    std::cout << "ONNX 模型加载: " << (onnx_ok ? "成功" : "失败") << std::endl;

    for (size_t i = 0; i < result.armors.size(); ++i) {
        const auto& armor = result.armors[i];

        // 保存 classify() 内部 cropDigitRoi() 裁出的 ROI（BGR）
        cv::Mat crop_roi = armor_detect::OnnxClassifier::cropDigitRoi(img, armor.points);
        if (!crop_roi.empty()) {
            std::string crop_path = out_dir + "/test_crop_roi_" + std::to_string(i) + ".png";
            cv::imwrite(crop_path, crop_roi);
            std::cout << "保存 cropDigitRoi: " << crop_path
                      << " size=" << crop_roi.cols << "x" << crop_roi.rows << std::endl;
        }

        // ONNX 分类（开启 debug_log）
        if (onnx_ok) {
            armor_detect::ClassifyResult cls = classifier.classify(img, armor.points, 0.7f, true);
            std::cout << "  -> ONNX 分类结果: "
                      << (cls.valid ? cls.label_text : "INVALID")
                      << " 置信度=" << cls.confidence
                      << " label_id=" << cls.label_id << std::endl;

            // 保存模型实际看到的 32x32 输入图
            cv::Mat input32 = classifier.lastDebugInput();
            if (!input32.empty()) {
                std::string input_path = out_dir + "/test_input32_" + std::to_string(i) + ".png";
                cv::imwrite(input_path, input32);
                std::cout << "  保存模型输入: " << input_path
                          << " size=" << input32.cols << "x" << input32.rows << std::endl;
            }
        }
    }

    return 0;
}
