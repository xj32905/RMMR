#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>

class CameraNode : public rclcpp::Node {
public:
    CameraNode() : Node("camera_node") {
        this->declare_parameter("video_path", "");
        this->declare_parameter("topic", "/camera/image");

        std::string path = this->get_parameter("video_path").as_string();
        std::string topic = this->get_parameter("topic").as_string();

        if (path.empty()) {
            cap_.open(0);
            RCLCPP_INFO(this->get_logger(), "使用电脑摄像头");
        } else {
            cap_.open(path);
            RCLCPP_INFO(this->get_logger(), "播放视频: %s", path.c_str());
        }

        if (!cap_.isOpened()) {
            RCLCPP_ERROR(this->get_logger(), "打开视频源失败！");
            return;
        }

        pub_ = this->create_publisher<sensor_msgs::msg::Image>(topic, 10);
        timer_ = this->create_wall_timer(
            std::chrono::milliseconds(33),
            std::bind(&CameraNode::timerCallback, this));
    }

private:
    void timerCallback() {
        cv::Mat frame;
        if (!cap_.read(frame)) {
            cap_.set(cv::CAP_PROP_POS_FRAMES, 0);
            return;
        }
        auto msg = cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", frame).toImageMsg();
        msg->header.stamp = this->now();
        pub_->publish(*msg);
    }

    cv::VideoCapture cap_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<CameraNode>());
    rclcpp::shutdown();
    return 0;
}