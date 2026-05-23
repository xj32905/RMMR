#include <iostream>
#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <memory>
#include <cmath>
#include <vector>
#include <utility>

#include <opencv2/opencv.hpp>

using namespace std;

// ==========================================
// 1. 结构体与全局变量声明
// ==========================================
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

// 专门用于局部计时的 RAII 结构体
struct InlineTimer {
    string name;
    chrono::steady_clock::time_point start;
    chrono::steady_clock::time_point end;

    void stopTimer_pushImg(cv::Mat& img, string demonstration = "") {
        end = chrono::steady_clock::now();
        // 声明在下方的全局回溯记录
        extern vector<unique_ptr<Node>> mmry;
        mmry.push_back(make_unique<Node>(name, img.clone(), end - start, demonstration));
    }

    InlineTimer(string name) {
        this->name = name;
        start = chrono::steady_clock::now();
    }
};

// 全局回溯记录向量
vector<unique_ptr<Node>> mmry;

// ==========================================
// 2. 函数声明
// ==========================================
void equalizeHist(cv::Mat& src);
void gammaCorrect(cv::Mat& src, double gamma);
void gsBlur(cv::Mat& src);
void mask(cv::Mat& src);
void convertColor(cv::Mat& src, int code);
cv::Mat predo(cv::Mat& src);
void threshold(cv::Mat& src, int thresh = 180, int type = cv::THRESH_TOZERO);
cv::Rect expandRoi(const cv::Rect& rect, const cv::Size& image_size, double scale);
void alignRoi(cv::Rect& rect);
vector<cv::Mat> split_mask(cv::Mat& src);
cv::Mat split_red(cv::Mat& src);
cv::Mat split_blue(cv::Mat& src);
cv::Mat split_overexpose(cv::Mat& src_hsv);
pair<vector<vector<cv::Point>>, vector<vector<cv::Point>>> split_red_blue(cv::Mat src_hsv, vector<vector<cv::Point>> contours);
void erode(cv::Mat& src);
void open(cv::Mat& src);
void close(cv::Mat& src);
cv::Mat edge(cv::Mat& src, int min, int max);
vector<vector<cv::Point>> contour(cv::Mat& src, cv::Mat backg, bool is_hsv = false);
cv::Rect trans_contour2rect(vector<cv::Point> contour);
cv::RotatedRect trans_contour2rotatedRect(vector<cv::Point> contour);
bool filter_contour(vector<cv::Point> contour);
vector<pair<cv::Rect, cv::Rect>> geo_armorDetect(vector<vector<cv::Point>>& candidates);
float getLongSideAngle(const cv::RotatedRect& rect);
void draw_rect(cv::Mat& src, cv::Rect rect, cv::Scalar color);
void draw_box(cv::Mat& src, pair<cv::Rect, cv::Rect>& armor, cv::Scalar color = cv::Scalar(255, 0, 0), string label = "Armor");

void goback();
void detect_img(cv::Mat& img);

// ==========================================
// 3. 主函数 (Main)
// ==========================================
int main(int argc, char** argv) {
    if (argc <= 2) {
        cout << "Usage:\n  Image Mode: ./armor_detect -p <image_path>\n  Video Mode: ./armor_detect -v <video_path>" << endl;
        return 0;
    }
    
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif

    if (strcmp(argv[1], "-p") == 0) {
        cv::Mat img = cv::imread(argv[2]);
        if (img.empty()) {
            cout << "Failed to load image: " << argv[2] << endl;
            return -1;
        }
        mmry.clear();
        detect_img(img);
    }
    else if (strcmp(argv[1], "-v") == 0) {
        cv::VideoCapture vio(argv[2]);
        if (!vio.isOpened()) {
            cout << "Failed to open video: " << argv[2] << endl;
            return -1;
        }
        mmry.clear();

        // 视频写入配置 (保存输出)
        auto output = cv::VideoWriter("output.mp4", cv::VideoWriter::fourcc('M','P','4','V'), 
                        vio.get(cv::CAP_PROP_FPS), 
                        cv::Size(vio.get(cv::CAP_PROP_FRAME_WIDTH), vio.get(cv::CAP_PROP_FRAME_HEIGHT)));

        while(1) {
            mmry.clear();
            cv::Mat frame;
            vio >> frame; 
            if (frame.empty()) break; 
            
            detect_img(frame); 
            
            cv::imshow("Real-time Detection", frame); 
            if (!output.isOpened()) {
                // 如果尺寸因为缩放改变了，动态重构写入器
                output.open("output.mp4", cv::VideoWriter::fourcc('M','P','4','V'), vio.get(cv::CAP_PROP_FPS), frame.size());
            }
            output << frame; 
            
            int y = cv::waitKey(1);
            if (y == 'o' || y == 27) break; // 按 'o' 或 ESC 退出
        }
    }

    // 进入调试回溯交互模式
    while(1) {
        cout << "\n==========================================" << endl;
        cout << "Press 'o' to exit.\nPress 'g' to enter Step-by-Step Backtrack mode." << endl;
        int k = cv::waitKey(0); 
        if (k == 'o' || k == 27) {
            break;
        }
        if (k == 'g') {
            goback();
        }
    }
    return 0;
}

// ==========================================
// 4. 核心图像检测流水线 (含大图缩放优化)
// ==========================================
void detect_img(cv::Mat& img) {
    // 【核心改进】：实现大图变小图的思路。如果图像宽度超过 1280 像素，等比例缩放
    int target_width = 1280;
    if (img.cols > target_width) {
        double scale = static_cast<double>(target_width) / img.cols;
        int target_height = static_cast<int>(img.rows * scale);
        cv::resize(img, img, cv::Size(target_width, target_height), 0, 0, cv::INTER_LINEAR);
    }

    // 提高图像对比度
    gammaCorrect(img, 2.0);
    
    // 转换色彩空间（直接处理，不进行多余的 clone）
    cv::Mat hsv;
    cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);
    
    // 提取高亮（过暴）灯条区域
    cv::Mat hsv_v = split_overexpose(hsv);
    
    // 提取灯条的外轮廓
    auto contours = contour(hsv_v, hsv, true);
    if (contours.empty()) {
        cv::imshow("result", img);
        return;
    }
    
    // 灯条颜色红蓝分离
    pair<vector<vector<cv::Point>>, vector<vector<cv::Point>>> rect = split_red_blue(hsv, contours);
    
    // 红色装甲板几何配对并绘制
    vector<pair<cv::Rect, cv::Rect>> armors_red = geo_armorDetect(rect.first);
    for (auto& armor : armors_red) {
        draw_box(img, armor, cv::Scalar(0, 0, 255), "Red Armor"); 
    }
    
    // 蓝色装甲板几何配对并绘制
    vector<pair<cv::Rect, cv::Rect>> armors_blue = geo_armorDetect(rect.second);
    for (auto& armor : armors_blue) {
        draw_box(img, armor, cv::Scalar(255, 0, 0), "Blue Armor"); 
    }
    
    cv::imshow("result", img);
}

// ==========================================
// 5. 功能算子具体实现
// ==========================================

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
    float target_aspect_ratio = 1.8f; // 调整为更接近真实装甲板的横宽比
    
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
    // HSV 空间下红色的两个区间区间
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
    
    // 【性能优化】：直接抠出 V 通道，代替原先的全局分离
    cv::Mat channel_v;
    cv::extractChannel(src_hsv, channel_v, 2);

    cv::Mat v_mask;
    cv::threshold(channel_v, v_mask, 220, 255, cv::THRESH_BINARY);

    // 闭运算：连接可能断开的过暴灯条中心区域
    cv::Mat kernel_close = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(v_mask, v_mask, cv::MORPH_CLOSE, kernel_close);

    // 开运算：消去细小的发光噪点
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
        rect = expandRoi(rect, src_hsv.size(), 1.5); // 稍微放大以便采集灯条周围环境色
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
        if (src.channels() == 3)
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        threshold(gray, 160, cv::THRESH_BINARY);
    }
    vector<vector<cv::Point>> contours;
    cv::findContours(gray, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    cv::Mat draw_canvas = backg.clone();
    for (size_t i = 0; i < contours.size(); ++i) {
        cv::drawContours(draw_canvas, contours, static_cast<int>(i), cv::Scalar(0, 255, 255), 2);
    }
    
    t.stopTimer_pushImg(draw_canvas);
    return contours;
}

// 轮廓初步筛选（过滤太小的杂点与形状不符的块）
bool filter_contour(vector<cv::Point> contour) {
    double area = cv::contourArea(contour);
    // 【注意】图像缩小后，对应的有效面积阈值也需要适当缩小
    if (area <= 10.0 || area >= 2000.0) return false;

    auto rect = cv::minAreaRect(contour);
    float w = rect.size.width;
    float h = rect.size.height;
    if (w == 0 || h == 0) return false;
    
    float ratio = max(w, h) / min(w, h);
    if (ratio <= 1.2) return false;  // 过于接近正方形排除

    float long_side_angle = getLongSideAngle(rect);
    if (long_side_angle < 0) long_side_angle += 180.0f;
    if (long_side_angle > 90.0f) long_side_angle = 180.0f - long_side_angle;
    
    // 真正的机器灯条基本是垂直方向垂直的（允许有一定倾斜偏离）
    if (std::abs(long_side_angle - 90.0f) > 25.0f) return false;

    return true;
}

// 几何约束装甲板匹配
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

            // 1. 左右两灯条长度差不能太大
            if (abs(len1 - len2) > 35) continue;

            // 2. 灯条平行度误差（角度差不能太大）
            float angle_diff = abs(angle1 - angle2);
            if (angle_diff > 90) angle_diff = 180 - angle_diff;
            if (angle_diff > 25) continue;

            // 3. 距离比例：中心点距离 / 平均长度
            float center_dist = static_cast<float>(cv::norm(rect1_rot.center - rect2_rot.center));
            float avg_len = (len1 + len2) / 2.0f;
            float ratio = center_dist / avg_len;
            if (ratio < 0.6f || ratio > 5.0f) continue;

            // 4. 两灯条高度（垂直错位）偏差不能太大
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
    cv::Rect box = armor.first | armor.second; // 使用位或合并两个 Rect 矩形为大包围矩形
    alignRoi(box);
    cv::rectangle(src, box, color, 2);
    cv::putText(src, label, cv::Point(box.x, max(box.y - 10, 15)), cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
}

// ==========================================
// 6. 交互式快照时间回溯实现
// ==========================================
void goback() {
    if (mmry.empty()) {
        cout << "No memory record available yet." << endl;
        return;
    }
    int i = 0;
    cout << "\n>>> Entered Backtrack Mode. Controls:\n"
         << "    [n] - Next Step\n"
         << "    [b] - Previous Step\n"
         << "    [s] - Save current step image\n"
         << "    [o] - Exit Backtrack Mode" << endl;
         
    while(1) {
        cout << "\n[Step " << i << "] Operation: " << mmry[i]->op_name 
             << " | Time cost: " << chrono::duration_cast<chrono::microseconds>(mmry[i]->dt).count() << " us" << endl;
        if (!mmry[i]->demonstration.empty()) {
            cout << "  Info: " << mmry[i]->demonstration << endl;
        }
        
        cv::imshow("Backtrack Viewer", *(mmry[i]->img));
        int k = cv::waitKey(0);
        if (k == 'o' || k == 27) {
            cv::destroyWindow("Backtrack Viewer");
            return;
        }
        if (k == 'n' && i < (int)mmry.size() - 1) ++i;
        if (k == 'b' && i > 0) --i;
        if (k == 's') {
            string filename = mmry[i]->op_name + "_" + to_string(i) + ".png";
            cv::imwrite(filename, *(mmry[i]->img));
            cout << "Saved: " << filename << endl;
        }
