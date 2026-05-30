#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/header.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>

#include "MvCameraControl.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

class CameraNode : public rclcpp::Node {
public:
    CameraNode() : Node("camera_node") {
        this->declare_parameter("source_type", "mvs");  // mvs / opencv
        this->declare_parameter("video_path", "");      // opencv 模式：空=普通UVC摄像头，非空=视频
        this->declare_parameter("camera_id", 0);
        this->declare_parameter("mvs_index", 0);
        this->declare_parameter("mvs_use_gentl", true);    // U3V相机在Linux下常需要走GenTL/cti
        this->declare_parameter("mvs_cti_path", "/opt/MVS/lib/64/MvProducerU3V.cti");
        this->declare_parameter("topic", "/camera/image");
        this->declare_parameter("frame_id", "camera");
        this->declare_parameter("fps", 0.0);
        this->declare_parameter("loop", true);
        this->declare_parameter("start_frame", 0);
        this->declare_parameter("resize_width", 0);
        this->declare_parameter("paused", false);

        // MVS 工业相机常用现场参数。<=0 表示不设置，使用相机当前值。
        this->declare_parameter("exposure_time", 0.0);   // us
        this->declare_parameter("gain", 0.0);
        this->declare_parameter("auto_exposure", false);
        this->declare_parameter("auto_gain", false);
        this->declare_parameter("auto_white_balance", false);

        source_type_ = this->get_parameter("source_type").as_string();
        topic_ = this->get_parameter("topic").as_string();
        frame_id_ = this->get_parameter("frame_id").as_string();
        video_path_ = this->get_parameter("video_path").as_string();
        loop_ = this->get_parameter("loop").as_bool();
        paused_ = this->get_parameter("paused").as_bool();
        start_frame_ = std::max<int>(0, static_cast<int>(this->get_parameter("start_frame").as_int()));
        resize_width_ = std::max<int>(0, static_cast<int>(this->get_parameter("resize_width").as_int()));

        pub_ = this->create_publisher<sensor_msgs::msg::Image>(topic_, 10);

        bool ok = false;
        if (source_type_ == "mvs") {
            MV_CC_Initialize();
            ok = openMvsCamera();
        } else {
            ok = openOpenCvSource();
        }

        if (!ok) {
            RCLCPP_ERROR(this->get_logger(), "图像源打开失败：source_type=%s", source_type_.c_str());
            return;
        }

        double fps = this->get_parameter("fps").as_double();
        if (fps <= 0.0) {
            fps = (source_type_ == "opencv") ? cap_.get(cv::CAP_PROP_FPS) : 0.0;
            if (fps <= 1.0 || fps > 240.0) fps = 30.0;
        }
        int period_ms = std::max(1, static_cast<int>(1000.0 / fps));

        timer_ = this->create_wall_timer(
            std::chrono::milliseconds(period_ms),
            std::bind(&CameraNode::timerCallback, this));

        RCLCPP_INFO(this->get_logger(),
                    "图像源启动：source=%s, topic=%s, fps=%.2f, resize_width=%d",
                    source_type_.c_str(), topic_.c_str(), fps, resize_width_);
    }

    ~CameraNode() override {
        closeMvsCamera();
        if (cap_.isOpened()) cap_.release();
    }

private:
    bool openOpenCvSource() {
        if (video_path_.empty()) {
            int camera_id = static_cast<int>(this->get_parameter("camera_id").as_int());
            cap_.open(camera_id);
            RCLCPP_INFO(this->get_logger(), "OpenCV 使用 UVC 摄像头: /dev/video%d", camera_id);
        } else {
            cap_.open(video_path_);
            RCLCPP_INFO(this->get_logger(), "OpenCV 播放视频: %s", video_path_.c_str());
        }

        if (!cap_.isOpened()) return false;
        if (!video_path_.empty() && start_frame_ > 0) {
            cap_.set(cv::CAP_PROP_POS_FRAMES, start_frame_);
        }
        return true;
    }

    bool openMvsCamera() {
        bool use_gentl = this->get_parameter("mvs_use_gentl").as_bool();
        if (use_gentl && openMvsCameraByGenTL()) {
            return true;
        }
        if (use_gentl) {
            RCLCPP_WARN(this->get_logger(), "GenTL 打开失败，回退到普通 MV_CC_EnumDevices 枚举");
        }

        MV_CC_DEVICE_INFO_LIST dev_list;
        std::memset(&dev_list, 0, sizeof(dev_list));

        int ret = MV_CC_EnumDevices(MV_USB_DEVICE | MV_GIGE_DEVICE | MV_GENTL_GIGE_DEVICE, &dev_list);
        if (ret != MV_OK) {
            RCLCPP_ERROR(this->get_logger(), "MVS 枚举设备失败: 0x%x", ret);
            return false;
        }
        if (dev_list.nDeviceNum == 0) {
            RCLCPP_ERROR(this->get_logger(), "MVS 未找到相机。MVS Viewer 能看到但这里找不到时，通常是 U3V 需要 GenTL/cti 或 Viewer 独占");
            return false;
        }

        int index = static_cast<int>(this->get_parameter("mvs_index").as_int());
        if (index < 0 || index >= static_cast<int>(dev_list.nDeviceNum)) {
            RCLCPP_WARN(this->get_logger(), "mvs_index=%d 越界，自动使用 0", index);
            index = 0;
        }

        ret = MV_CC_CreateHandle(&mvs_handle_, dev_list.pDeviceInfo[index]);
        if (ret != MV_OK) {
            RCLCPP_ERROR(this->get_logger(), "MVS 创建句柄失败: 0x%x", ret);
            return false;
        }

        return finishOpenMvs(index, dev_list.nDeviceNum);
    }

    bool openMvsCameraByGenTL() {
        std::string cti_path = this->get_parameter("mvs_cti_path").as_string();
        MV_GENTL_IF_INFO_LIST if_list;
        std::memset(&if_list, 0, sizeof(if_list));

        int ret = MV_CC_EnumInterfacesByGenTL(&if_list, cti_path.c_str());
        if (ret != MV_OK) {
            RCLCPP_ERROR(this->get_logger(), "GenTL 枚举接口失败: 0x%x, cti=%s", ret, cti_path.c_str());
            return false;
        }
        RCLCPP_INFO(this->get_logger(), "GenTL 找到接口数: %u", if_list.nInterfaceNum);
        if (if_list.nInterfaceNum == 0) return false;

        MV_GENTL_DEV_INFO_LIST dev_list;
        int total_devices = 0;
        int target_index = static_cast<int>(this->get_parameter("mvs_index").as_int());

        for (unsigned int i = 0; i < if_list.nInterfaceNum; ++i) {
            std::memset(&dev_list, 0, sizeof(dev_list));
            ret = MV_CC_EnumDevicesByGenTL(if_list.pIFInfo[i], &dev_list);
            if (ret != MV_OK) {
                RCLCPP_WARN(this->get_logger(), "GenTL 接口 %u 枚举设备失败: 0x%x", i, ret);
                continue;
            }
            RCLCPP_INFO(this->get_logger(), "GenTL 接口 %u 设备数: %u", i, dev_list.nDeviceNum);

            for (unsigned int j = 0; j < dev_list.nDeviceNum; ++j) {
                if (total_devices == target_index) {
                    ret = MV_CC_CreateHandleByGenTL(&mvs_handle_, dev_list.pDeviceInfo[j]);
                    if (ret != MV_OK) {
                        RCLCPP_ERROR(this->get_logger(), "GenTL 创建句柄失败: 0x%x", ret);
                        return false;
                    }
                    RCLCPP_INFO(this->get_logger(), "GenTL 选择相机: model=%s serial=%s",
                                dev_list.pDeviceInfo[j]->chModelName,
                                dev_list.pDeviceInfo[j]->chSerialNumber);
                    return finishOpenMvs(target_index, total_devices + 1);
                }
                total_devices++;
            }
        }

        RCLCPP_ERROR(this->get_logger(), "GenTL 未找到目标相机，设备总数=%d, mvs_index=%d", total_devices, target_index);
        return false;
    }

    bool finishOpenMvs(int index, unsigned int count) {
        int ret = MV_CC_OpenDevice(mvs_handle_);
        if (ret != MV_OK) {
            RCLCPP_ERROR(this->get_logger(), "MVS 打开相机失败: 0x%x", ret);
            closeMvsCamera();
            return false;
        }

        // 连续采集，关闭触发。
        MV_CC_SetEnumValue(mvs_handle_, "TriggerMode", MV_TRIGGER_MODE_OFF);
        applyMvsParamsOnce();

        ret = MV_CC_StartGrabbing(mvs_handle_);
        if (ret != MV_OK) {
            RCLCPP_ERROR(this->get_logger(), "MVS 开始采集失败: 0x%x", ret);
            closeMvsCamera();
            return false;
        }

        mvs_opened_ = true;
        RCLCPP_INFO(this->get_logger(), "MVS 工业相机已打开，index=%d, count=%u", index, count);
        return true;
    }

    void closeMvsCamera() {
        if (mvs_handle_) {
            if (mvs_opened_) {
                MV_CC_StopGrabbing(mvs_handle_);
                MV_CC_CloseDevice(mvs_handle_);
            }
            MV_CC_DestroyHandle(mvs_handle_);
            mvs_handle_ = nullptr;
            mvs_opened_ = false;
            MV_CC_Finalize();
        }
    }

    void applyMvsParamsOnce() {
        if (!mvs_handle_) return;

        bool auto_exposure = this->get_parameter("auto_exposure").as_bool();
        bool auto_gain = this->get_parameter("auto_gain").as_bool();
        bool auto_wb = this->get_parameter("auto_white_balance").as_bool();
        double exposure_time = this->get_parameter("exposure_time").as_double();
        double gain = this->get_parameter("gain").as_double();

        MV_CC_SetEnumValue(mvs_handle_, "ExposureAuto", auto_exposure ? MV_EXPOSURE_AUTO_MODE_CONTINUOUS : MV_EXPOSURE_AUTO_MODE_OFF);
        MV_CC_SetEnumValue(mvs_handle_, "GainAuto", auto_gain ? MV_GAIN_MODE_CONTINUOUS : MV_GAIN_MODE_OFF);
        MV_CC_SetEnumValue(mvs_handle_, "BalanceWhiteAuto", auto_wb ? MV_BALANCEWHITE_AUTO_CONTINUOUS : MV_BALANCEWHITE_AUTO_OFF);

        if (!auto_exposure && exposure_time > 0.0) {
            int ret = MV_CC_SetFloatValue(mvs_handle_, "ExposureTime", static_cast<float>(exposure_time));
            if (ret != MV_OK) RCLCPP_WARN(this->get_logger(), "设置 ExposureTime 失败: 0x%x", ret);
        }
        if (!auto_gain && gain > 0.0) {
            int ret = MV_CC_SetFloatValue(mvs_handle_, "Gain", static_cast<float>(gain));
            if (ret != MV_OK) RCLCPP_WARN(this->get_logger(), "设置 Gain 失败: 0x%x", ret);
        }
    }

    void timerCallback() {
        paused_ = this->get_parameter("paused").as_bool();
        resize_width_ = std::max<int>(0, static_cast<int>(this->get_parameter("resize_width").as_int()));

        cv::Mat frame;
        if (paused_) {
            if (last_frame_.empty()) return;
            frame = last_frame_.clone();
        } else if (source_type_ == "mvs") {
            if (!readMvsFrame(frame)) return;
            last_frame_ = frame.clone();
        } else {
            if (!readOpenCvFrame(frame)) return;
            last_frame_ = frame.clone();
        }

        if (resize_width_ > 0 && frame.cols > 0 && frame.cols != resize_width_) {
            double scale = static_cast<double>(resize_width_) / frame.cols;
            cv::resize(frame, frame, cv::Size(resize_width_, static_cast<int>(frame.rows * scale)));
        }

        std_msgs::msg::Header header;
        header.stamp = this->now();
        header.frame_id = frame_id_;
        auto msg = cv_bridge::CvImage(header, "bgr8", frame).toImageMsg();
        pub_->publish(*msg);
    }

    bool readOpenCvFrame(cv::Mat& frame) {
        loop_ = this->get_parameter("loop").as_bool();
        if (!cap_.read(frame) || frame.empty()) {
            if (loop_ && !video_path_.empty()) {
                cap_.set(cv::CAP_PROP_POS_FRAMES, start_frame_);
                return cap_.read(frame) && !frame.empty();
            }
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000, "视频播放结束或摄像头无画面");
            return false;
        }
        return true;
    }

    bool readMvsFrame(cv::Mat& frame) {
        MV_FRAME_OUT out_frame;
        std::memset(&out_frame, 0, sizeof(out_frame));

        int ret = MV_CC_GetImageBuffer(mvs_handle_, &out_frame, 1000);
        if (ret != MV_OK) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000, "MVS 取帧失败: 0x%x", ret);
            return false;
        }

        const unsigned int width = out_frame.stFrameInfo.nWidth;
        const unsigned int height = out_frame.stFrameInfo.nHeight;
        const auto pixel_type = out_frame.stFrameInfo.enPixelType;

        if (pixel_type == PixelType_Gvsp_BGR8_Packed) {
            frame = cv::Mat(height, width, CV_8UC3, out_frame.pBufAddr).clone();
        } else if (pixel_type == PixelType_Gvsp_RGB8_Packed) {
            cv::Mat rgb(height, width, CV_8UC3, out_frame.pBufAddr);
            cv::cvtColor(rgb, frame, cv::COLOR_RGB2BGR);
        } else if (pixel_type == PixelType_Gvsp_Mono8) {
            cv::Mat mono(height, width, CV_8UC1, out_frame.pBufAddr);
            cv::cvtColor(mono, frame, cv::COLOR_GRAY2BGR);
        } else {
            std::vector<unsigned char> bgr_buffer(width * height * 3);
            MV_CC_PIXEL_CONVERT_PARAM_EX convert_param;
            std::memset(&convert_param, 0, sizeof(convert_param));
            convert_param.nWidth = width;
            convert_param.nHeight = height;
            convert_param.enSrcPixelType = pixel_type;
            convert_param.pSrcData = out_frame.pBufAddr;
            convert_param.nSrcDataLen = out_frame.stFrameInfo.nFrameLen;
            convert_param.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
            convert_param.pDstBuffer = bgr_buffer.data();
            convert_param.nDstBufferSize = static_cast<unsigned int>(bgr_buffer.size());

            ret = MV_CC_ConvertPixelTypeEx(mvs_handle_, &convert_param);
            if (ret != MV_OK) {
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                                     "MVS 像素格式转换失败: 0x%x, pixel=0x%lx", ret, static_cast<unsigned long>(pixel_type));
                MV_CC_FreeImageBuffer(mvs_handle_, &out_frame);
                return false;
            }
            frame = cv::Mat(height, width, CV_8UC3, bgr_buffer.data()).clone();
        }

        MV_CC_FreeImageBuffer(mvs_handle_, &out_frame);
        return !frame.empty();
    }

    std::string source_type_;
    std::string video_path_;
    std::string topic_;
    std::string frame_id_;
    bool loop_ = true;
    bool paused_ = false;
    int start_frame_ = 0;
    int resize_width_ = 0;

    cv::VideoCapture cap_;
    cv::Mat last_frame_;
    void* mvs_handle_ = nullptr;
    bool mvs_opened_ = false;

    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<CameraNode>());
    rclcpp::shutdown();
    return 0;
}
