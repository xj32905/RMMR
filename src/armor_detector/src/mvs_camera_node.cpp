#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <MvCameraControl.h>

class MvsCameraNode : public rclcpp::Node {
public:
    MvsCameraNode() : Node("mvs_camera_node") {
        this->declare_parameter("topic", "/camera/image");
        topic_ = this->get_parameter("topic").as_string();

        // 1. 初始化 MVS
        MV_CC_Initialize();
        
        // 2. 枚举 USB 设备
        MV_CC_DEVICE_INFO_LIST stDeviceList;
        memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
        int nRet = MV_CC_EnumDevices(MV_USB_DEVICE, &stDeviceList);
        if (MV_OK != nRet || stDeviceList.nDeviceNum == 0) {
            RCLCPP_ERROR(this->get_logger(), "没找到 USB 相机！先关掉 MVS 客户端再试。");
            return;
        }
        RCLCPP_INFO(this->get_logger(), "找到 %d 个 USB 相机", stDeviceList.nDeviceNum);
        
        // 3. 创建句柄并打开
        nRet = MV_CC_CreateHandle(&handle_, stDeviceList.pDeviceInfo[0]);
        if (MV_OK != nRet) {
            RCLCPP_ERROR(this->get_logger(), "创建句柄失败");
            return;
        }
        nRet = MV_CC_OpenDevice(handle_);
        if (MV_OK != nRet) {
            RCLCPP_ERROR(this->get_logger(), "打开设备失败");
            return;
        }
        
        // 4. 开始取流
        nRet = MV_CC_StartGrabbing(handle_);
        if (MV_OK != nRet) {
            RCLCPP_ERROR(this->get_logger(), "开始取流失败");
            return;
        }
        
        pub_ = this->create_publisher<sensor_msgs::msg::Image>(topic_, 10);
        timer_ = this->create_wall_timer(
            std::chrono::milliseconds(33),
            std::bind(&MvsCameraNode::timerCallback, this));
            
        RCLCPP_INFO(this->get_logger(), "海康相机启动成功，发布到 %s", topic_.c_str());
    }
    
    ~MvsCameraNode() {
        MV_CC_StopGrabbing(handle_);
        MV_CC_CloseDevice(handle_);
        MV_CC_DestroyHandle(handle_);
    }

private:
    void timerCallback() {
        MV_FRAME_OUT stFrame;
        memset(&stFrame, 0, sizeof(MV_FRAME_OUT));
        int nRet = MV_CC_GetImageBuffer(handle_, &stFrame, 100); // 100ms 超时
        if (MV_OK != nRet) return;
        
        int width = stFrame.stFrameInfo.nWidth;
        int height = stFrame.stFrameInfo.nHeight;
        unsigned char* pData = (unsigned char*)stFrame.pBufAddr;
        
        // 海康 USB 相机通常是 BayerRG8 格式，转成 BGR
        cv::Mat bayer(height, width, CV_8UC1, pData);
        cv::Mat bgr;
        cv::cvtColor(bayer, bgr, cv::COLOR_BayerRG2BGR);
        
        auto msg = cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", bgr).toImageMsg();
        msg->header.stamp = this->now();
        pub_->publish(*msg);
        
        MV_CC_FreeImageBuffer(handle_, &stFrame);
    }

    void* handle_ = nullptr;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
    std::string topic_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<MvsCameraNode>());
    rclcpp::shutdown();
    return 0;
}